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

#define JOIN_PATH(a, b)                     a "/" b

#define LED_FILE_BASE                       "/sys/class/leds"

#define RED_LED_FILE_BASE                   JOIN_PATH(LED_FILE_BASE, "red")
#define GREEN_LED_FILE_BASE                 JOIN_PATH(LED_FILE_BASE, "green")
#define BLUE_LED_FILE_BASE                  JOIN_PATH(LED_FILE_BASE, "blue")

#define BUTTON_BACKLIGHT_LED_FILE_BASE      JOIN_PATH(LED_FILE_BASE, "button-backlight")
#define LCD_BACKLIGHT_LED_FILE_BASE         JOIN_PATH(LED_FILE_BASE, "lcd-backlight")

static const std::string kRedLEDFile = JOIN_PATH(RED_LED_FILE_BASE, "brightness");
static const std::string kGreenLEDFile = JOIN_PATH(GREEN_LED_FILE_BASE, "brightness");
static const std::string kBlueLEDFile = JOIN_PATH(BLUE_LED_FILE_BASE, "brightness");

static const std::string kRedBlinkFile = JOIN_PATH(RED_LED_FILE_BASE, "blink");
static const std::string kGreenBlinkFile = JOIN_PATH(GREEN_LED_FILE_BASE, "blink");
static const std::string kBlueBlinkFile = JOIN_PATH(BLUE_LED_FILE_BASE, "blink");

static const std::string kLCDFile = JOIN_PATH(LCD_BACKLIGHT_LED_FILE_BASE, "brightness");
static const std::string kLCDFile2 = "/sys/class/backlight/panel0-backlight/brightness";

static const std::string kButtonFile = JOIN_PATH(BUTTON_BACKLIGHT_LED_FILE_BASE, "brightness");

static const std::string kPersistenceFile = "/sys/class/graphics/fb0/msm_fb_persist_mode";

#define AutoHwLight(light) {.id = (int)light, .type = light, .ordinal = 0}

// List of supported lights
const static std::vector<HwLight> kAvailableLights = {
    AutoHwLight(LightType::BACKLIGHT),
    AutoHwLight(LightType::BATTERY),
    AutoHwLight(LightType::BUTTONS),
    AutoHwLight(LightType::NOTIFICATIONS)
};

Lights::Lights() {
    mBacklightNode = (!access(kLCDFile.c_str(), F_OK)) ? kLCDFile : kLCDFile2;
    mButtonExists = !access(kButtonFile.c_str(), F_OK);
}

// AIDL methods
ndk::ScopedAStatus Lights::setLightState(int id, const HwLightState& state) {
    ndk::ScopedAStatus ret;

    switch (id) {
        case (int)LightType::BACKLIGHT:
            ret = setLightBacklight(state);
            break;
        case (int)LightType::BATTERY:
            ret = setLightBattery(state);
            break;
        case (int)LightType::BUTTONS:
            ret = setLightButtons(state);
            break;
        case (int)LightType::NOTIFICATIONS:
            ret = setLightNotification(state);
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            break;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::getLights(std::vector<HwLight>* lights) {
    for (auto i = kAvailableLights.begin(); i != kAvailableLights.end(); i++) {
        lights->push_back(*i);
    }
    return ndk::ScopedAStatus::ok();
}

// device methods
ndk::ScopedAStatus Lights::setLightBacklight(const HwLightState& state) {
    bool wantsLowPersistence = state.brightnessMode == BrightnessMode::LOW_PERSISTENCE;
    uint32_t brightness = (wantsLowPersistence) ? DEFAULT_LOW_PERSISTENCE_MODE_BRIGHTNESS : 
                        RgbaToBrightness(state.color);

    if (mLowPersistenceEnabled != wantsLowPersistence) {
        WriteToFile(kPersistenceFile, wantsLowPersistence);
        mLowPersistenceEnabled = wantsLowPersistence;
    }

    WriteToFile(mBacklightNode, brightness);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::setLightBattery(const HwLightState& state) {
    mBattery = state;
    handleSpeakerBatteryLocked();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::setLightButtons(const HwLightState& state) {
    if (mButtonExists)
        WriteToFile(kButtonFile, state.color & 0xFF);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::setLightNotification(const HwLightState& state) {
    mNotification = state;
    handleSpeakerBatteryLocked();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::setSpeakerLightLocked(const HwLightState& state) {
    uint32_t red, green, blue;
    uint32_t blink;
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
        if (red && WriteToFile(kRedBlinkFile, blink))
            WriteToFile(kRedLEDFile, 0);
        if (green && WriteToFile(kGreenBlinkFile, blink))
            WriteToFile(kGreenLEDFile, 0);
        if (blue && WriteToFile(kBlueBlinkFile, blink))
            WriteToFile(kBlueLEDFile, 0);
    } else {
        WriteToFile(kRedLEDFile, red);
        WriteToFile(kGreenLEDFile, green);
        WriteToFile(kBlueLEDFile, blue);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::handleSpeakerBatteryLocked() {
    if (IsLit(mBattery.color))
        return setSpeakerLightLocked(mBattery);
    else
        return setSpeakerLightLocked(mNotification);
}

// Utils
bool Lights::IsLit(uint32_t color) {
    return color & 0x00ffffff;
}

uint32_t Lights::RgbaToBrightness(uint32_t color) {
    // Extract brightness from AARRGGBB.
    uint32_t alpha = (color >> 24) & 0xFF;

    // Retrieve each of the RGB colors
    uint32_t red = (color >> 16) & 0xFF;
    uint32_t green = (color >> 8) & 0xFF;
    uint32_t blue = color & 0xFF;

    // Scale RGB colors if a brightness has been applied by the user
    if (alpha != 0xFF) {
        red = red * alpha / 0xFF;
        green = green * alpha / 0xFF;
        blue = blue * alpha / 0xFF;
    }

    return (77 * red + 150 * green + 29 * blue) >> 8;
}

// Write value to path and close file.
bool Lights::WriteToFile(const std::string& path, uint32_t content) {
    return WriteStringToFile(std::to_string(content), path);
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
