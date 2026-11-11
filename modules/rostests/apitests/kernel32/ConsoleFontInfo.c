/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the retired GetConsoleFontInfo and SetConsoleFont functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef DWORD (WINAPI *PGET_CONSOLE_FONT_INFO)(HANDLE, BOOL, DWORD, PCONSOLE_FONT_INFO);
typedef BOOL (WINAPI *PSET_CONSOLE_FONT)(HANDLE, DWORD);

static VOID
CheckFunctions(PGET_CONSOLE_FONT_INFO GetFontInfo, PSET_CONSOLE_FONT SetFont, HANDLE Output, PCSTR What)
{
    CONSOLE_FONT_INFO Info[4];
    DWORD Ret;

    memset(Info, 0x55, sizeof(Info));
    SetLastError(0xdeadbeef);
    Ret = GetFontInfo(NULL, FALSE, 0, Info);
    ok(!Ret, "%s: GetConsoleFontInfo(NULL) returned %lu\n", What, Ret);
    ok(GetLastError() == LOWORD(E_NOTIMPL), "%s: GetConsoleFontInfo(NULL) error %lu\n", What, GetLastError());
    ok(Info[0].nFont == 0x55555555, "%s: the buffer was written\n", What);

    SetLastError(0xdeadbeef);
    Ret = GetFontInfo(NULL, TRUE, 0, Info);
    ok(!Ret, "%s: GetConsoleFontInfo(NULL, TRUE) returned %lu\n", What, Ret);
    ok(GetLastError() == LOWORD(E_NOTIMPL), "%s: GetConsoleFontInfo(NULL, TRUE) error %lu\n", What, GetLastError());

    SetLastError(0xdeadbeef);
    Ret = SetFont(NULL, 0);
    ok(!Ret, "%s: SetConsoleFont(NULL) succeeded\n", What);
    ok(GetLastError() == LOWORD(E_NOTIMPL), "%s: SetConsoleFont(NULL) error %lu\n", What, GetLastError());

    if (Output == INVALID_HANDLE_VALUE)
        return;
    SetLastError(0xdeadbeef);
    Ret = GetFontInfo(Output, FALSE, ARRAYSIZE(Info), Info);
    ok(!Ret, "%s: GetConsoleFontInfo(CONOUT$) returned %lu\n", What, Ret);
    ok(GetLastError() == LOWORD(E_NOTIMPL), "%s: GetConsoleFontInfo(CONOUT$) error %lu\n", What, GetLastError());
    ok(Info[0].nFont == 0x55555555, "%s: the buffer was written\n", What);

    SetLastError(0xdeadbeef);
    Ret = SetFont(Output, 0);
    ok(!Ret, "%s: SetConsoleFont(CONOUT$) succeeded\n", What);
    ok(GetLastError() == LOWORD(E_NOTIMPL), "%s: SetConsoleFont(CONOUT$) error %lu\n", What, GetLastError());
}

START_TEST(ConsoleFontInfo)
{
    PGET_CONSOLE_FONT_INFO GetFontInfo, BaseGetFontInfo = NULL;
    PSET_CONSOLE_FONT SetFont, BaseSetFont = NULL;
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE KernelBase = LoadLibraryW(L"kernelbase.dll");
    HANDLE Output;
    BOOL Allocated = FALSE;

    GetFontInfo = (PGET_CONSOLE_FONT_INFO)GetProcAddress(Kernel32, "GetConsoleFontInfo");
    SetFont = (PSET_CONSOLE_FONT)GetProcAddress(Kernel32, "SetConsoleFont");
    if (KernelBase)
    {
        BaseGetFontInfo = (PGET_CONSOLE_FONT_INFO)GetProcAddress(KernelBase, "GetConsoleFontInfo");
        BaseSetFont = (PSET_CONSOLE_FONT)GetProcAddress(KernelBase, "SetConsoleFont");
    }
    if (!GetFontInfo || !SetFont)
    {
        skip("GetConsoleFontInfo or SetConsoleFont is not exported by kernel32\n");
        if (KernelBase)
            FreeLibrary(KernelBase);
        return;
    }

    Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (Output == INVALID_HANDLE_VALUE && AllocConsole())
    {
        Allocated = TRUE;
        Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
    }
    ok(Output != INVALID_HANDLE_VALUE, "Opening CONOUT$ failed with %lu\n", GetLastError());

    CheckFunctions(GetFontInfo, SetFont, Output, "kernel32");
    if (BaseGetFontInfo && BaseSetFont)
        CheckFunctions(BaseGetFontInfo, BaseSetFont, Output, "kernelbase");
    else
        skip("GetConsoleFontInfo or SetConsoleFont is not exported by kernelbase\n");

    if (Output != INVALID_HANDLE_VALUE)
        CloseHandle(Output);
    if (Allocated)
        FreeConsole();
    if (KernelBase)
        FreeLibrary(KernelBase);
}
