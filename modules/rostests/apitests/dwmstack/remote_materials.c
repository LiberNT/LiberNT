/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the compositor draws backdrop materials solid in a remote session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <libernt/dwmframe.h>
#include "../../../../base/system/dwm/settings.h"

static void InitWindow(DWM_WIN *Window, ULONG Type, ULONG Region)
{
    memset(Window, 0, sizeof(*Window));
    Window->BackdropType = Type;
    Window->BackdropRegion = Region;
    Window->BackdropOpacity = 200;
    Window->LayerFlags = DWM_WINDOW_NC_SHADOW;
}

static void TestBackdrop(BOOL Remote, ULONG Type, ULONG Region, ULONG ExpectedType, ULONG ExpectedRegion,
                         ULONG ExpectedOpacity)
{
    DWM_SETTINGS Settings = {DWM_EFFECT_ALL, FALSE, NULL, Remote};
    DWM_WIN Window;

    InitWindow(&Window, Type, Region);
    DwmSettingsApplyWindow(&Settings, &Window);
    ok(Window.BackdropType == ExpectedType, "remote %d type %lu: type %lu, expected %lu\n", Remote, Type,
       Window.BackdropType, ExpectedType);
    ok(Window.BackdropRegion == ExpectedRegion, "remote %d type %lu: region %lu, expected %lu\n", Remote, Type,
       Window.BackdropRegion, ExpectedRegion);
    ok(Window.BackdropOpacity == ExpectedOpacity, "remote %d type %lu: opacity %lu, expected %lu\n", Remote, Type,
       Window.BackdropOpacity, ExpectedOpacity);
    ok(Window.LayerFlags & DWM_WINDOW_NC_SHADOW, "remote %d type %lu: the shadow was dropped\n", Remote, Type);
    ok(Settings.Effects == DWM_EFFECT_ALL, "remote %d type %lu: the user's effects changed to 0x%lx\n", Remote, Type,
       Settings.Effects);
}

static void TestBlur(BOOL Remote, BOOL ExpectFilter)
{
    DWM_SETTINGS Settings = {DWM_EFFECT_ALL, FALSE, NULL, Remote};
    DWM_WIN Window;

    InitWindow(&Window, DWM_BACKDROP_NONE, 0);
    Window.BlurFlags = DWM_BLUR_ENABLE | DWM_BLUR_REGION_ENTIRE_WINDOW;
    Window.BlurRectCount = 3;
    DwmSettingsApplyWindow(&Settings, &Window);
    ok(((Window.BlurFlags & DWM_BLUR_DISABLE_FILTER) == 0) == ExpectFilter, "remote %d: blur flags 0x%lx\n", Remote,
       Window.BlurFlags);
    ok((Window.BlurRectCount == 3) == ExpectFilter, "remote %d: %lu blur rectangles\n", Remote, Window.BlurRectCount);
    ok(Window.BlurFlags & DWM_BLUR_ENABLE, "remote %d: the pixel alpha rule was dropped\n", Remote);
}

static void TestUpdate(void)
{
    BOOL Remote = GetSystemMetrics(SM_REMOTESESSION) != 0;
    DWM_SETTINGS Settings = {DWM_EFFECT_ALL, FALSE, NULL, Remote};

    DwmSettingsUpdateRemote(&Settings);
    ok(Settings.Remote == Remote && !Settings.Changed, "an unchanged session state marked the settings changed\n");
    Settings.Remote = !Remote;
    DwmSettingsUpdateRemote(&Settings);
    ok(Settings.Remote == Remote, "the session state was not picked up\n");
    ok(Settings.Changed, "a session state change did not mark the settings changed\n");
}

START_TEST(remote_materials)
{
    TestBackdrop(FALSE, DWM_BACKDROP_MAIN, DWM_BACKDROP_REGION_WINDOW, DWM_BACKDROP_MAIN, DWM_BACKDROP_REGION_WINDOW, 200);
    TestBackdrop(FALSE, DWM_BACKDROP_TABBED, DWM_BACKDROP_REGION_NONCLIENT, DWM_BACKDROP_TABBED,
                 DWM_BACKDROP_REGION_NONCLIENT, 200);
    TestBackdrop(FALSE, DWM_BACKDROP_TRANSIENT, DWM_BACKDROP_REGION_WINDOW, DWM_BACKDROP_TRANSIENT,
                 DWM_BACKDROP_REGION_WINDOW, 200);
    TestBackdrop(TRUE, DWM_BACKDROP_MAIN, DWM_BACKDROP_REGION_WINDOW, DWM_BACKDROP_NONE, 0, 255);
    TestBackdrop(TRUE, DWM_BACKDROP_TABBED, DWM_BACKDROP_REGION_NONCLIENT, DWM_BACKDROP_NONE, 0, 255);
    TestBackdrop(TRUE, DWM_BACKDROP_TRANSIENT, DWM_BACKDROP_REGION_WINDOW, DWM_BACKDROP_NONE, 0, 255);
    TestBackdrop(TRUE, DWM_BACKDROP_NONE, 0, DWM_BACKDROP_NONE, 0, 200);
    TestBlur(FALSE, TRUE);
    TestBlur(TRUE, FALSE);
    TestUpdate();
}
