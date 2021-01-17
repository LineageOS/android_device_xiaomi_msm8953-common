package msm8953

import (
    "android/soong/android"
    "android/soong/cc"
    "strings"
)

func biometricsFlags(ctx android.BaseContext) []string {
    var cflags []string

    var config = ctx.AConfig().VendorConfig("XIAOMI_MSM8953_BIOMETRICS")
    var halModules = strings.Split(strings.TrimSpace(config.String("HAL_MODULES")), ",")

    cflags = append(cflags, "-DHAL_MODULES=\"" + strings.Join(halModules, "\", \"") + "\"")

    return cflags
}

func biometricsHalBinary(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
            Android struct {
                Cflags []string
            }
        }
    }

    p := &props{}
    p.Target.Android.Cflags = biometricsFlags(ctx)
    ctx.AppendProperties(p)
}

func biometricsHalBinaryFactory() android.Module {
    module, _ := cc.NewBinary(android.HostAndDeviceSupported)
    newMod := module.Init()
    android.AddLoadHook(newMod, biometricsHalBinary)
    return newMod
}
