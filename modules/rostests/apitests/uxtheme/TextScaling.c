/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the uxtheme text scaling ordinals 141 and 142
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>

typedef BOOL (WINAPI *PFN_PROCESS_TEXT_SCALING)(void);
typedef BOOL (WINAPI *PFN_WINDOW_TEXT_SCALING)(HWND);

START_TEST(TextScaling)
{
    HMODULE UxTheme = LoadLibraryW(L"uxtheme.dll");
    PFN_PROCESS_TEXT_SCALING pEnableTextScalingForCurrentProcess;
    PFN_WINDOW_TEXT_SCALING pEnableTextScalingForWindow;
    HWND Windows[4];
    DWORD Index;
    BOOL Result;

    ok(UxTheme != NULL, "uxtheme.dll is missing\n");
    if (!UxTheme)
        return;

    pEnableTextScalingForCurrentProcess = (PFN_PROCESS_TEXT_SCALING)GetProcAddress(UxTheme, MAKEINTRESOURCEA(141));
    pEnableTextScalingForWindow = (PFN_WINDOW_TEXT_SCALING)GetProcAddress(UxTheme, MAKEINTRESOURCEA(142));
    ok(pEnableTextScalingForCurrentProcess != NULL, "ordinal 141 is missing\n");
    ok(pEnableTextScalingForWindow != NULL, "ordinal 142 is missing\n");

    if (pEnableTextScalingForCurrentProcess)
    {
        SetLastError(0xdeadbeef);
        Result = pEnableTextScalingForCurrentProcess();
        ok(Result == TRUE && GetLastError() == 0xdeadbeef, "first call: %d %lu\n", Result, GetLastError());
        SetLastError(0xdeadbeef);
        Result = pEnableTextScalingForCurrentProcess();
        ok(Result == TRUE && GetLastError() == 0xdeadbeef, "second call: %d %lu\n", Result, GetLastError());
    }

    Windows[0] = CreateWindowExW(0, L"STATIC", L"text scaling", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    Windows[1] = GetDesktopWindow();
    Windows[2] = NULL;
    Windows[3] = (HWND)(ULONG_PTR)0x12345678;
    if (pEnableTextScalingForWindow)
    {
        for (Index = 0; Index < _countof(Windows); Index++)
        {
            SetLastError(0xdeadbeef);
            Result = pEnableTextScalingForWindow(Windows[Index]);
            ok(Result == FALSE && GetLastError() == 0xdeadbeef, "window %lu: %d %lu\n", Index, Result, GetLastError());
        }
    }
    DestroyWindow(Windows[0]);
}
