/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that window functions forwarded to win32k return exactly TRUE or FALSE
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(BoolReturnValues)
{
    TITLEBARINFO Info;
    HWND Window;
    BOOL Ret;
    int i;

    for (i = 0; i < 8; i++)
    {
        Window = CreateWindowExW(0, L"static", L"bool", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
        ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
        if (!Window)
            return;

        Info.cbSize = sizeof(Info);
        Ret = GetTitleBarInfo(Window, &Info);
        ok(Ret == TRUE, "GetTitleBarInfo returned %d\n", Ret);

        Ret = DestroyWindow(Window);
        ok(Ret == TRUE, "DestroyWindow returned %d\n", Ret);

        SetLastError(0xdeadbeef);
        Ret = DestroyWindow(Window);
        ok(Ret == FALSE, "DestroyWindow of a destroyed window returned %d\n", Ret);
        ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "error %lu\n", GetLastError());
    }
}
