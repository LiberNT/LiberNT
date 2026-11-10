/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test caption layout retained with front surfaces
 */
#include <apitest.h>
#include <windows.h>
#include <reactos/dwmframe.h>
#include "../../../../subsystems/win/win32k/user/ntuser/captioncache.h"

START_TEST(caption_cache)
{
    DWM_CAPTION_CACHE Cache = {0};
    DWM_WIN Window = {0};
    ULONG Restored = DWM_CAPTION_STATE_VALID | DWM_CAPTION_STATE_RIGHT(0);
    ULONG Maximized = Restored | DWM_CAPTION_STATE_RESTORE | DWM_CAPTION_STATE_HOT(0);

    Window.CaptionButtons[0].top = 8;
    Window.CaptionState = Maximized;
    IntCompositionCaptionCache(&Cache, TRUE, 1, &Window);
    ok(!Cache.Valid && Window.CaptionButtons[0].top == 8, "First held front must retain live metadata\n");
    Window.CaptionButtons[0].top = 0;
    Window.CaptionButtons[0].bottom = 21;
    Window.CaptionState = Restored;
    IntCompositionCaptionCache(&Cache, FALSE, 1, &Window);
    Window.CaptionButtons[0].top = 8;
    Window.CaptionButtons[0].bottom = 29;
    Window.CaptionState = Maximized;
    IntCompositionCaptionCache(&Cache, TRUE, 1, &Window);
    ok(Window.CaptionButtons[0].top == 0 && Window.CaptionButtons[0].bottom == 21,
       "Held front lost its original row\n");
    ok(Window.CaptionState == (Restored | DWM_CAPTION_STATE_HOT(0)), "Held layout or live hot state lost\n");
    Window.CaptionState = 0;
    IntCompositionCaptionCache(&Cache, TRUE, 1, &Window);
    ok(Window.CaptionState == Restored, "Invalid current state replaced retained layout\n");
    Window.CaptionButtons[0].top = 8;
    Window.CaptionState = Maximized;
    IntCompositionCaptionCache(&Cache, TRUE, 2, &Window);
    ok(Window.CaptionButtons[0].top == 8 && Window.CaptionState == Maximized,
       "Old generation contaminated another front\n");
    IntCompositionCaptionCache(&Cache, FALSE, 2, &Window);
    Window.CaptionButtons[0].top = 0;
    Window.CaptionState = Restored;
    IntCompositionCaptionCache(&Cache, TRUE, 2, &Window);
    ok(Window.CaptionButtons[0].top == 8 && (Window.CaptionState & DWM_CAPTION_STATE_RESTORE),
       "Newly published front did not replace the snapshot\n");
    Window.CaptionState = 0;
    ZeroMemory(Window.CaptionButtons, sizeof(Window.CaptionButtons));
    IntCompositionCaptionCache(&Cache, FALSE, 3, &Window);
    Window.CaptionButtons[0].top = 8;
    Window.CaptionState = Maximized;
    IntCompositionCaptionCache(&Cache, TRUE, 3, &Window);
    ok(Window.CaptionState == 0 && Window.CaptionButtons[0].top == 0,
       "A front without buttons gained current decorations\n");
    ZeroMemory(&Cache, sizeof(Cache));
    Window.CaptionButtons[0].top = 4;
    Window.CaptionState = Restored;
    IntCompositionCaptionCache(&Cache, TRUE, 2, &Window);
    ok(Window.CaptionButtons[0].top == 4 && !Cache.Valid, "Reused entry kept old metadata\n");
}
