/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Window frame metrics by subsystem version of the executable
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PADJUSTWINDOWRECTEXFORDPI)(LPRECT, DWORD, BOOL, DWORD, UINT);

static PADJUSTWINDOWRECTEXFORDPI pAdjustWindowRectExForDpi;

static BOOL IsLegacyImage(void)
{
    PIMAGE_DOS_HEADER Dos = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
    PIMAGE_NT_HEADERS Nt = (PIMAGE_NT_HEADERS)((PBYTE)Dos + Dos->e_lfanew);

    return Nt->OptionalHeader.MajorSubsystemVersion < 6;
}

static void CheckAdjust(DWORD Style, DWORD ExStyle, LONG Frame, BOOL Caption)
{
    RECT Rect = {0, 0, 0, 0};
    LONG Top = Frame + (Caption ? GetSystemMetrics((ExStyle & WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION) : 0);

    ok(AdjustWindowRectEx(&Rect, Style, FALSE, ExStyle), "AdjustWindowRectEx %#lx %#lx failed\n", Style, ExStyle);
    ok(Rect.left == -Frame && Rect.top == -Top && Rect.right == Frame && Rect.bottom == Frame,
       "AdjustWindowRectEx %#lx %#lx: (%ld,%ld)-(%ld,%ld), expected (%ld,%ld)-(%ld,%ld)\n", Style, ExStyle,
       Rect.left, Rect.top, Rect.right, Rect.bottom, -Frame, -Top, Frame, Frame);

    SetRectEmpty(&Rect);
    ok(pAdjustWindowRectExForDpi(&Rect, Style, FALSE, ExStyle, USER_DEFAULT_SCREEN_DPI),
       "AdjustWindowRectExForDpi %#lx %#lx failed\n", Style, ExStyle);
    ok(Rect.left == -Frame && Rect.top == -Top && Rect.right == Frame && Rect.bottom == Frame,
       "AdjustWindowRectExForDpi %#lx %#lx: (%ld,%ld)-(%ld,%ld), expected (%ld,%ld)-(%ld,%ld)\n", Style, ExStyle,
       Rect.left, Rect.top, Rect.right, Rect.bottom, -Frame, -Top, Frame, Frame);
}

static void CheckWindow(DWORD Style, DWORD ExStyle, int Width, int Height, LONG Frame)
{
    HWND Window = CreateWindowExW(ExStyle, L"static", L"frame", Style, 0, 0, Width, Height, NULL, NULL, NULL, NULL);
    RECT Rect = {0}, Client = {0};
    LONG ExpectedWidth, ExpectedHeight;

    ok(Window != NULL, "CreateWindowEx %#lx failed: %lu\n", Style, GetLastError());
    if (!Window)
        return;
    GetWindowRect(Window, &Rect);
    GetClientRect(Window, &Client);
    ExpectedWidth = Rect.right - Rect.left - 2 * Frame;
    ExpectedHeight = Rect.bottom - Rect.top - 2 * Frame - GetSystemMetrics(SM_CYCAPTION);
    ok(Client.right == ExpectedWidth && Client.bottom == ExpectedHeight,
       "style %#lx size %dx%d: window %ldx%ld client %ldx%ld, expected client %ldx%ld\n", Style, Width, Height,
       Rect.right - Rect.left, Rect.bottom - Rect.top, Client.right, Client.bottom, ExpectedWidth, ExpectedHeight);
    DestroyWindow(Window);
}

static void CheckFrames(BOOL Legacy)
{
    NONCLIENTMETRICSW Metrics;
    UINT Sizes[2] = {sizeof(Metrics), FIELD_OFFSET(NONCLIENTMETRICSW, iPaddedBorderWidth)};
    LONG CaptionFrame = Legacy ? 3 : 8;
    INT Border = -1;
    UINT i;

    ok_int(GetSystemMetrics(SM_CXSIZEFRAME), Legacy ? 8 : 4);
    ok_int(GetSystemMetrics(SM_CYSIZEFRAME), Legacy ? 8 : 4);
    ok_int(GetSystemMetrics(SM_CXPADDEDBORDER), Legacy ? 0 : 4);
    ok_int(GetSystemMetrics(SM_CXFIXEDFRAME), 3);
    ok_int(GetSystemMetrics(SM_CYFIXEDFRAME), 3);
    ok_int(GetSystemMetrics(SM_CXBORDER), 1);

    for (i = 0; i < ARRAYSIZE(Sizes); ++i)
    {
        ZeroMemory(&Metrics, sizeof(Metrics));
        Metrics.cbSize = Sizes[i];
        ok(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, Sizes[i], &Metrics, 0), "SPI_GETNONCLIENTMETRICS %u failed: %lu\n", Sizes[i], GetLastError());
        ok_int(Metrics.iBorderWidth, Legacy ? 5 : 1);
        if (Sizes[i] == sizeof(Metrics))
            ok_int(Metrics.iPaddedBorderWidth, Legacy ? 0 : 4);
    }
    ok(SystemParametersInfoW(SPI_GETBORDER, 0, &Border, 0), "SPI_GETBORDER failed: %lu\n", GetLastError());
    ok_int(Border, 1);

    CheckAdjust(WS_CAPTION, 0, CaptionFrame, TRUE);
    CheckAdjust(WS_CAPTION | WS_CLIPSIBLINGS, WS_EX_WINDOWEDGE, CaptionFrame, TRUE);
    CheckAdjust(WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, CaptionFrame, TRUE);
    CheckAdjust(WS_CAPTION, WS_EX_TOOLWINDOW, CaptionFrame, TRUE);
    CheckAdjust(WS_OVERLAPPEDWINDOW, 0, 8, TRUE);
    CheckAdjust(WS_OVERLAPPEDWINDOW, WS_EX_WINDOWEDGE, 8, TRUE);
    CheckAdjust(WS_POPUP | WS_DLGFRAME, 0, 3, FALSE);
    CheckAdjust(WS_POPUP | WS_BORDER, 0, 1, FALSE);
    CheckAdjust(WS_POPUP | WS_THICKFRAME, 0, 7, FALSE);

    CheckWindow(0, 0, 0, 0, CaptionFrame);
    CheckWindow(WS_CAPTION, 0, 0, 0, CaptionFrame);
    CheckWindow(0, 0, 1, 1, CaptionFrame);
    CheckWindow(0, 0, 200, 100, CaptionFrame);
    CheckWindow(WS_POPUP | WS_CAPTION, 0, 200, 100, CaptionFrame);
    CheckWindow(WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, 0, 0, CaptionFrame);
    CheckWindow(WS_OVERLAPPEDWINDOW, 0, 0, 0, 8);
    CheckWindow(WS_OVERLAPPEDWINDOW, 0, 200, 100, 8);
    CheckWindow(WS_POPUP | WS_CAPTION | WS_THICKFRAME, 0, 200, 100, 8);
}

static void RunLegacyCopy(void)
{
    WCHAR Self[MAX_PATH], Dir[MAX_PATH], Copy[MAX_PATH + 16], Command[MAX_PATH + 64];
    static const WORD Version[2] = {5, 2};
    STARTUPINFOW Startup = {sizeof(Startup)};
    PROCESS_INFORMATION Process = {0};
    IMAGE_DOS_HEADER Dos;
    HANDLE File;
    DWORD Bytes;

    GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
    GetTempPathW(ARRAYSIZE(Dir), Dir);
    wsprintfW(Copy, L"%sframemetrics%lu.exe", Dir, GetCurrentProcessId());
    ok(CopyFileW(Self, Copy, FALSE), "CopyFile failed: %lu\n", GetLastError());
    SetFileAttributesW(Copy, FILE_ATTRIBUTE_NORMAL);
    File = CreateFileW(Copy, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ReadFile(File, &Dos, sizeof(Dos), &Bytes, NULL);
    SetFilePointer(File, Dos.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader.MajorSubsystemVersion), NULL, FILE_BEGIN);
    ok(WriteFile(File, Version, sizeof(Version), &Bytes, NULL), "WriteFile failed: %lu\n", GetLastError());
    CloseHandle(File);

    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    Startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    Startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    wsprintfW(Command, L"\"%s\" FrameMetrics legacy", Copy);
    ok(CreateProcessW(Copy, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Process), "CreateProcess failed: %lu\n", GetLastError());
    if (Process.hProcess)
    {
        winetest_wait_child_process(Process.hProcess);
        CloseHandle(Process.hProcess);
        CloseHandle(Process.hThread);
    }
    DeleteFileW(Copy);
}

START_TEST(FrameMetrics)
{
    char **Argv;
    int Argc = winetest_get_mainargs(&Argv);
    BOOL Legacy = IsLegacyImage();
    HDC Screen = GetDC(NULL);
    int Dpi = GetDeviceCaps(Screen, LOGPIXELSX);

    ReleaseDC(NULL, Screen);
    if (Dpi != 96)
    {
        skip("Display is at %d DPI\n", Dpi);
        return;
    }
    pAdjustWindowRectExForDpi = (PADJUSTWINDOWRECTEXFORDPI)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                          "AdjustWindowRectExForDpi");
    ok(pAdjustWindowRectExForDpi != NULL, "AdjustWindowRectExForDpi is not exported\n");
    if (!pAdjustWindowRectExForDpi)
        return;

    CheckFrames(Legacy);
    if (Argc <= 2)
        RunLegacyCopy();
}
