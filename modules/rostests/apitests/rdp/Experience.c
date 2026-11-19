/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the visual experience the RDP console server gives a remote session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <freerdp/settings_types.h>
#include "experience.h"

static const UINT EffectGets[EXPERIENCE_EFFECTS] =
{
    SPI_GETFONTSMOOTHING, SPI_GETMENUANIMATION, SPI_GETCOMBOBOXANIMATION, SPI_GETLISTBOXSMOOTHSCROLLING,
    SPI_GETMENUFADE, SPI_GETTOOLTIPANIMATION, SPI_GETUIEFFECTS, SPI_GETCLIENTAREAANIMATION
};
static const UINT EffectSets[EXPERIENCE_EFFECTS] =
{
    SPI_SETFONTSMOOTHING, SPI_SETMENUANIMATION, SPI_SETCOMBOBOXANIMATION, SPI_SETLISTBOXSMOOTHSCROLLING,
    SPI_SETMENUFADE, SPI_SETTOOLTIPANIMATION, SPI_SETUIEFFECTS, SPI_SETCLIENTAREAANIMATION
};

typedef struct SETTINGS
{
    WCHAR Wallpaper[MAX_PATH + 1];
    BOOL DragFullWindows;
    BOOL Effects[EXPERIENCE_EFFECTS];
} SETTINGS;

typedef struct REGISTRY
{
    BYTE Mask[16];
    WCHAR Drag[16], Smoothing[16], Wallpaper[MAX_PATH + 1];
    DWORD MaskSize;
} REGISTRY;

static BOOL GetFlag(UINT action)
{
    BOOL value = FALSE;
    ok(SystemParametersInfoW(action, 0, &value, 0), "SystemParametersInfo 0x%04x failed\n", action);
    return value;
}

static void ReadSettings(SETTINGS *settings)
{
    UINT i;
    memset(settings, 0, sizeof(*settings));
    ok(SystemParametersInfoW(SPI_GETDESKWALLPAPER, ARRAYSIZE(settings->Wallpaper), settings->Wallpaper, 0),
       "SPI_GETDESKWALLPAPER failed\n");
    settings->DragFullWindows = GetFlag(SPI_GETDRAGFULLWINDOWS);
    for (i = 0; i < EXPERIENCE_EFFECTS; ++i) settings->Effects[i] = GetFlag(EffectGets[i]);
}

static void WriteSettings(const SETTINGS *settings)
{
    UINT i;
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)settings->Wallpaper, SPIF_SENDCHANGE);
    SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS, settings->DragFullWindows, NULL, SPIF_SENDCHANGE);
    for (i = 0; i < EXPERIENCE_EFFECTS; ++i)
    {
        if (EffectSets[i] == SPI_SETFONTSMOOTHING)
            SystemParametersInfoW(EffectSets[i], settings->Effects[i], NULL, SPIF_SENDCHANGE);
        else
            SystemParametersInfoW(EffectSets[i], 0, (PVOID)(ULONG_PTR)settings->Effects[i], SPIF_SENDCHANGE);
    }
}

static void ReadRegistry(REGISTRY *registry)
{
    HKEY key;
    DWORD size;
    memset(registry, 0, sizeof(*registry));
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_QUERY_VALUE, &key)) return;
    registry->MaskSize = sizeof(registry->Mask);
    RegQueryValueExW(key, L"UserPreferencesMask", NULL, NULL, registry->Mask, &registry->MaskSize);
    size = sizeof(registry->Drag);
    RegQueryValueExW(key, L"DragFullWindows", NULL, NULL, (BYTE *)registry->Drag, &size);
    size = sizeof(registry->Smoothing);
    RegQueryValueExW(key, L"FontSmoothing", NULL, NULL, (BYTE *)registry->Smoothing, &size);
    size = sizeof(registry->Wallpaper) - sizeof(WCHAR);
    RegQueryValueExW(key, L"WallPaper", NULL, NULL, (BYTE *)registry->Wallpaper, &size);
    RegCloseKey(key);
}

static BOOL WriteBitmap(const WCHAR *path)
{
    static const BYTE bitmap[] =
    {
        'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 16, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 255, 0, 0, 255, 0, 0, 0, 0, 255, 0, 0, 255, 0, 0
    };
    DWORD written = 0;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    WriteFile(file, bitmap, sizeof(bitmap), &written, NULL);
    CloseHandle(file);
    return written == sizeof(bitmap);
}

static void CheckSettings(const WCHAR *wallpaper, BOOL drag, BOOL effects, const char *name)
{
    SETTINGS now;
    UINT i;
    ReadSettings(&now);
    ok(!lstrcmpiW(now.Wallpaper, wallpaper), "%s: wallpaper is '%ls', expected '%ls'\n", name, now.Wallpaper, wallpaper);
    ok(!now.DragFullWindows == !drag, "%s: full window drag %d, expected %d\n", name, now.DragFullWindows, drag);
    for (i = 0; i < EXPERIENCE_EFFECTS; ++i)
        ok(!now.Effects[i] == !effects, "%s: SystemParametersInfo 0x%04x gives %d, expected %d\n", name, EffectGets[i],
           now.Effects[i], effects);
}

static void TestFromClient(UINT32 flags, UINT32 type, BOOL wallpaper, BOOL drag, BOOL effects)
{
    RDP_EXPERIENCE experience;
    ExperienceFromClient(flags, type, &experience);
    ok(!experience.Wallpaper == !wallpaper, "flags 0x%lx type %lu: wallpaper %d\n", (ULONG)flags, (ULONG)type,
       experience.Wallpaper);
    ok(!experience.DragFullWindows == !drag, "flags 0x%lx type %lu: full window drag %d\n", (ULONG)flags, (ULONG)type,
       experience.DragFullWindows);
    ok(!experience.Effects == !effects, "flags 0x%lx type %lu: effects %d\n", (ULONG)flags, (ULONG)type,
       experience.Effects);
}

static void TestMapping(void)
{
    const UINT32 lan = PERF_ENABLE_FONT_SMOOTHING | PERF_ENABLE_DESKTOP_COMPOSITION;
    const UINT32 slow = PERF_DISABLE_WALLPAPER | PERF_DISABLE_FULLWINDOWDRAG | PERF_DISABLE_MENUANIMATIONS;
    TestFromClient(lan, CONNECTION_TYPE_LAN, TRUE, TRUE, TRUE);
    TestFromClient(lan | PERF_DISABLE_WALLPAPER, CONNECTION_TYPE_LAN, FALSE, TRUE, TRUE);
    TestFromClient(lan | PERF_DISABLE_FULLWINDOWDRAG, CONNECTION_TYPE_LAN, TRUE, FALSE, TRUE);
    TestFromClient(PERF_ENABLE_DESKTOP_COMPOSITION | PERF_DISABLE_MENUANIMATIONS | PERF_DISABLE_THEMING,
                   CONNECTION_TYPE_LAN, TRUE, TRUE, TRUE);
    TestFromClient(lan, CONNECTION_TYPE_WAN, TRUE, TRUE, TRUE);
    TestFromClient(lan, CONNECTION_TYPE_MODEM, TRUE, TRUE, FALSE);
    TestFromClient(slow | PERF_DISABLE_THEMING, CONNECTION_TYPE_MODEM, FALSE, FALSE, FALSE);
    TestFromClient(slow, CONNECTION_TYPE_BROADBAND_LOW, FALSE, FALSE, FALSE);
    TestFromClient(lan | slow, CONNECTION_TYPE_BROADBAND_LOW, FALSE, FALSE, FALSE);
    TestFromClient(slow | PERF_ENABLE_DESKTOP_COMPOSITION, CONNECTION_TYPE_SATELLITE, FALSE, FALSE, FALSE);
    TestFromClient(slow | PERF_ENABLE_DESKTOP_COMPOSITION, CONNECTION_TYPE_BROADBAND_HIGH, FALSE, FALSE, FALSE);
}

static void TestApply(void)
{
    SETTINGS original, baseline = {0};
    REGISTRY before, after;
    RDP_EXPERIENCE experience;
    RDP_EXPERIENCE_STATE state = {0};
    WCHAR path[MAX_PATH];
    UINT i;

    if (!GetTempPathW(ARRAYSIZE(path) - 32, path) || !lstrcatW(path, L"rdpexperience.bmp") || !WriteBitmap(path))
    {
        skip("Cannot write a wallpaper bitmap\n");
        return;
    }
    ReadSettings(&original);
    ReadRegistry(&before);
    lstrcpyW(baseline.Wallpaper, path);
    baseline.DragFullWindows = TRUE;
    for (i = 0; i < EXPERIENCE_EFFECTS; ++i) baseline.Effects[i] = TRUE;
    WriteSettings(&baseline);
    CheckSettings(path, TRUE, TRUE, "baseline");

    ExperienceFromClient(PERF_ENABLE_FONT_SMOOTHING, CONNECTION_TYPE_MODEM, &experience);
    ok(ExperienceApply(&state, &experience), "applying a modem connection failed\n");
    ok(state.Saved, "the console's settings were not saved\n");
    CheckSettings(path, TRUE, FALSE, "modem");

    ExperienceFromClient(PERF_DISABLE_WALLPAPER | PERF_DISABLE_FULLWINDOWDRAG | PERF_ENABLE_FONT_SMOOTHING,
                         CONNECTION_TYPE_LAN, &experience);
    ok(ExperienceApply(&state, &experience), "applying a second client failed\n");
    CheckSettings(L"", FALSE, TRUE, "second client without wallpaper and drag");

    ReadRegistry(&after);
    ok(after.MaskSize == before.MaskSize && !memcmp(after.Mask, before.Mask, sizeof(after.Mask)),
       "UserPreferencesMask changed in the registry\n");
    ok(!lstrcmpW(after.Drag, before.Drag), "DragFullWindows changed in the registry: '%ls'\n", after.Drag);
    ok(!lstrcmpW(after.Smoothing, before.Smoothing), "FontSmoothing changed in the registry: '%ls'\n", after.Smoothing);
    ok(!lstrcmpiW(after.Wallpaper, before.Wallpaper), "WallPaper changed in the registry: '%ls'\n", after.Wallpaper);

    ok(ExperienceRestore(&state), "restoring failed\n");
    ok(!state.Saved, "the state still holds saved settings\n");
    CheckSettings(path, TRUE, TRUE, "restored");
    ok(ExperienceRestore(&state), "restoring twice failed\n");
    CheckSettings(path, TRUE, TRUE, "restored twice");

    WriteSettings(&original);
    DeleteFileW(path);
}

START_TEST(Experience)
{
    TestMapping();
    TestApply();
}
