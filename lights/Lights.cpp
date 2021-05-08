/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Lights.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <fcntl.h>

#ifndef DEFAULT_LOW_PERSISTENCE_MODE_BRIGHTNESS
#define DEFAULT_LOW_PERSISTENCE_MODE_BRIGHTNESS 0x80
#endif

using ::android::base::WriteStringToFile;

namespace aidl {
namespace android {
namespace hardware {
namespace light {

const char *RED_LED_FILE = "/sys/class/leds/red/brightness";
const char *GREEN_LED_FILE = "/sys/class/leds/green/brightness";
const char *BLUE_LED_FILE = "/sys/class/leds/blue/brightness";

const char *RED_BLINK_FILE = "/sys/class/leds/red/blink";
const char *GREEN_BLINK_FILE = "/sys/class/leds/green/blink";
const char *BLUE_BLINK_FILE = "/sys/class/leds/blue/blink";

const char *LCD_FILE = "/sys/class/leds/lcd-backlight/brightness";
const char *LCD_FILE2 = "/sys/class/backlight/panel0-backlight/brightness";

const char *BUTTON_FILE = "/sys/class/leds/button-backlight/brightness";

const char *PERSISTENCE_FILE = "/sys/class/graphics/fb0/msm_fb_persist_mode";

#define AutoHwLight(light) {.id = (int)light, .type = light, .ordinal = 0}

// List of supported lights
const static std::vector<HwLight> kAvailableLights = {
    AutoHwLight(LightType::ATTENTION),
    AutoHwLight(LightType::BACKLIGHT),
    AutoHwLight(LightType::BATTERY),
    AutoHwLight(LightType::BUTTONS),
    AutoHwLight(LightType::NOTIFICATIONS)
};

static HwLightState *g_notification;
static HwLightState *g_battery;
static BrightnessMode g_last_backlight_mode = BrightnessMode::USER;
static int g_attention = 0;

// AIDL methods
ndk::ScopedAStatus Lights::setLightState(int id, const HwLightState& state) {
    ndk::ScopedAStatus ret;

    switch (id) {
        case (int)LightType::ATTENTION:
            ret = setLightAttention(state);
            break;
        case (int)LightType::BACKLIGHT:
            ret = setLightBacklight(state);
            break;
        case (int)LightType::BATTERY:
            ret = setLightBattery(state);
            break;
        case (int)LightType::BUTTONS:
            // enable light button when the file is present
            if (!access(BUTTON_FILE, F_OK))
                ret = setLightButtons(state);
            else
                ret = ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            break;
        case (int)LightType::NOTIFICATIONS:
            ret = setLightNotification(state);
            break;
        default:
            ret = ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            break;
    }

    return ret;
}

ndk::ScopedAStatus Lights::getLights(std::vector<HwLight>* lights) {
    for (auto i = kAvailableLights.begin(); i != kAvailableLights.end(); i++) {
        lights->push_back(*i);
    }
    return ndk::ScopedAStatus::ok();
}

// device methods
ndk::ScopedAStatus Lights::setLightAttention(const HwLightState& state) {
    if (state.flashMode == FlashMode::HARDWARE)
        g_attention = state.flashOnMs;
    else if (state.flashMode == FlashMode::NONE)
        g_attention = 0;

    handleSpeakerBatteryLocked();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::setLightBacklight(const HwLightState& state) {
    int err = 0;
    int brightness = RgbToBrightness(state);
    unsigned int lpEnabled = state.brightnessMode == BrightnessMode::LOW_PERSISTENCE;

    // Toggle low persistence mode state
    if ((g_last_backlight_mode != state.brightnessMode && lpEnabled) ||
        (!lpEnabled && g_last_backlight_mode == BrightnessMode::LOW_PERSISTENCE)) {
        err = WriteIntToFile(lpEnabled, PERSISTENCE_FILE);
        if (err != 0)
            LOG(ERROR) << __FUNCTION__ << ": Failed to write to " << PERSISTENCE_FILE <<
                ": " << strerror(errno);

        if (lpEnabled != 0)
            brightness = DEFAULT_LOW_PERSISTENCE_MODE_BRIGHTNESS;
    }
    g_last_backlight_mode = state.brightnessMode;

    if (!err) {
        if (!access(LCD_FILE, F_OK))
            err = WriteIntToFile(brightness, LCD_FILE);
        else
            err = WriteIntToFile(brightness, LCD_FILE2);
    }

    if (err == 0)
        return ndk::ScopedAStatus::ok();
    else
        return ndk::ScopedAStatus::fromServiceSpecificError(err);
}

ndk::ScopedAStatus Lights::setLightBattery(const HwLightState& state) {
    *g_battery = state;
    handleSpeakerBatteryLocked();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::setLightButtons(const HwLightState& state) {
    int err = 0;

    err = WriteIntToFile(state.color & 0xFF, BUTTON_FILE);
    if (err == 0)
        return ndk::ScopedAStatus::ok();
    else
        return ndk::ScopedAStatus::fromServiceSpecificError(err);
}

ndk::ScopedAStatus Lights::setLightNotification(const HwLightState& state) {
    *g_notification = state;
    handleSpeakerBatteryLocked();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::setSpeakerLightLocked(const HwLightState& state) {
    int red, green, blue;
    int blink;
    int onMS, offMS;
    unsigned int colorRGB;

    switch (state.flashMode) {
        case FlashMode::TIMED:
            onMS = state.flashOnMs;
            offMS = state.flashOffMs;
            break;
        case FlashMode::NONE:
        default:
            onMS = 0;
            offMS = 0;
            break;
    }

    colorRGB = state.color;

#ifdef WHITE_LED
    colorRGB = 16711680;

    if (!(state.color & 0xFFFFFF))
      colorRGB = state.color;
#endif

    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

    if (onMS > 0 && offMS > 0) {
        /*
         * if ON time == OFF time
         *   use blink mode 2
         * else
         *   use blink mode 1
         */
        if (onMS == offMS)
            blink = 2;
        else
            blink = 1;
    } else
        blink = 0;

    if (blink) {
        if (red && WriteIntToFile(blink, RED_BLINK_FILE))
            WriteIntToFile(0, RED_LED_FILE);
        if (green && WriteIntToFile(blink, GREEN_BLINK_FILE))
            WriteIntToFile(0, GREEN_LED_FILE);
        if (blue && WriteIntToFile(blink, BLUE_BLINK_FILE))
            WriteIntToFile(0, BLUE_LED_FILE);
    } else {
        WriteIntToFile(red, RED_LED_FILE);
        WriteIntToFile(green, GREEN_LED_FILE);
        WriteIntToFile(blue, BLUE_LED_FILE);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::handleSpeakerBatteryLocked() {
    if (isLit(*g_battery))
        return setSpeakerLightLocked(*g_battery);
    else
        return setSpeakerLightLocked(*g_notification);
}

bool Lights::WriteIntToFile(int value, char const *path) {
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%d\n", value);
    return WriteStringToFile(buffer, path);
}

int Lights::isLit(const HwLightState& state) {
    return state.color & 0x00ffffff;
}

int Lights::RgbToBrightness(const HwLightState& state) {
    int color = state.color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))
            + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
