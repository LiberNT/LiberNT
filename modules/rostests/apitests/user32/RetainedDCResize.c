/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a window DC retained by another process keeps drawing into the window after a resize
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"

#define CHILD_CX 120
#define CHILD_CY 80

static HANDLE OpenTestEvent(PCWSTR Name, DWORD Pid)
{
    WCHAR Buffer[64];

    wsprintfW(Buffer, L"RetainedDCResize%s%lu", Name, Pid);
    return CreateEventW(NULL, FALSE, FALSE, Buffer);
}

static void Fill(HDC hdc, COLORREF Color)
{
    RECT rc = { 0, 0, CHILD_CX, CHILD_CY };
    HBRUSH hbr = CreateSolidBrush(Color);

    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);
    GdiFlush();
}

static int RunHolder(HWND hwnd, DWORD Pid)
{
    HANDLE hReady = OpenTestEvent(L"Ready", Pid);
    HANDLE hDraw = OpenTestEvent(L"Draw", Pid);
    HANDLE hDrawn = OpenTestEvent(L"Drawn", Pid);
    HDC hdc = GetDC(hwnd);
    int Phase;

    if (!hdc)
        return 1;
    SetEvent(hReady);
    for (Phase = 0; Phase < 2; Phase++)
    {
        if (WaitForSingleObject(hDraw, 5000) != WAIT_OBJECT_0)
            return 2;
        Fill(hdc, Phase ? RGB(0, 255, 0) : RGB(0, 0, 255));
        SetEvent(hDrawn);
    }
    if (WaitForSingleObject(hDraw, 5000) != WAIT_OBJECT_0)
        return 3;
    ReleaseDC(hwnd, hdc);
    return 0;
}

static void Pump(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static COLORREF ReadPixel(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);
    COLORREF Color = GetPixel(hdc, CHILD_CX / 2, CHILD_CY / 2);

    ReleaseDC(hwnd, hdc);
    return Color;
}

START_TEST(RetainedDCResize)
{
    char **test_argv;
    int argc = winetest_get_mainargs(&test_argv);
    WNDCLASSW wc = { 0 };
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE hReady, hDraw, hDrawn;
    HWND hwndFrame, hwndChild;
    char CommandLine[MAX_PATH + 64];
    DWORD ExitCode = 0xdeadbeef;
    COLORREF Color;

    if (argc >= 5 && !strcmp(test_argv[2], "holder"))
        ExitProcess(RunHolder((HWND)(ULONG_PTR)strtoull(test_argv[3], NULL, 16), strtoul(test_argv[4], NULL, 10)));

    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"RetainedDCResize";
    RegisterClassW(&wc);
    hwndFrame = CreateWindowExW(0, wc.lpszClassName, L"RetainedDCResize", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                100, 100, 300, 240, NULL, NULL, wc.hInstance, NULL);
    ok(hwndFrame != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    hwndChild = CreateWindowExW(0, wc.lpszClassName, NULL, WS_CHILD | WS_VISIBLE, 10, 10, CHILD_CX, CHILD_CY,
                                hwndFrame, NULL, wc.hInstance, NULL);
    ok(hwndChild != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwndFrame || !hwndChild)
        return;
    ShowWindow(hwndFrame, SW_SHOW);
    SetForegroundWindow(hwndFrame);
    UpdateWindow(hwndFrame);
    Pump();

    hReady = OpenTestEvent(L"Ready", GetCurrentProcessId());
    hDraw = OpenTestEvent(L"Draw", GetCurrentProcessId());
    hDrawn = OpenTestEvent(L"Drawn", GetCurrentProcessId());
    sprintf(CommandLine, "\"%s\" RetainedDCResize holder %Ix %lu", test_argv[0], (ULONG_PTR)hwndChild,
            GetCurrentProcessId());
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        ok(0, "CreateProcessA failed, error %lu\n", GetLastError());
        DestroyWindow(hwndFrame);
        return;
    }
    ok(WaitForSingleObject(hReady, 5000) == WAIT_OBJECT_0, "the holder did not get the DC\n");

    SetEvent(hDraw);
    ok(WaitForSingleObject(hDrawn, 5000) == WAIT_OBJECT_0, "the holder did not draw\n");
    Color = ReadPixel(hwndChild);
    ok(Color == RGB(0, 0, 255), "before the resize: pixel %08lx, expected %08lx\n", Color, RGB(0, 0, 255));

    SetWindowPos(hwndFrame, NULL, 0, 0, 360, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RedrawWindow(hwndFrame, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
    Pump();
    SetEvent(hDraw);
    ok(WaitForSingleObject(hDrawn, 5000) == WAIT_OBJECT_0, "the holder did not draw\n");
    Color = ReadPixel(hwndChild);
    ok(Color == RGB(0, 255, 0), "after the resize: pixel %08lx, expected %08lx\n", Color, RGB(0, 255, 0));

    SetEvent(hDraw);
    ok(WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0, "the holder did not exit\n");
    GetExitCodeProcess(pi.hProcess, &ExitCode);
    ok(ExitCode == 0, "the holder exited with %lu\n", ExitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReady);
    CloseHandle(hDraw);
    CloseHandle(hDrawn);
    DestroyWindow(hwndFrame);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
