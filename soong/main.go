package msm8953

import (
    "android/soong/android"
)

func init() {
    android.RegisterModuleType("xiaomi_msm8953_biometrics_hal_binary", biometricsHalBinaryFactory)
}
