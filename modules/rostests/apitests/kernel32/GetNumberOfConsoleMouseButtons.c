/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for GetNumberOfConsoleMouseButtons
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winuser.h>

typedef BOOL (WINAPI *PGET_NUMBER_OF_CONSOLE_MOUSE_BUTTONS)(LPDWORD);

START_TEST(GetNumberOfConsoleMouseButtons)
{
    PGET_NUMBER_OF_CONSOLE_MOUSE_BUTTONS Kernel32Function, KernelBaseFunction = NULL;
    HMODULE KernelBase;
    DWORD Buttons, Expected;
    BOOL Ret;

    Kernel32Function = (PGET_NUMBER_OF_CONSOLE_MOUSE_BUTTONS)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                           "GetNumberOfConsoleMouseButtons");
    KernelBase = LoadLibraryW(L"kernelbase.dll");
    if (KernelBase)
        KernelBaseFunction = (PGET_NUMBER_OF_CONSOLE_MOUSE_BUTTONS)GetProcAddress(KernelBase,
                                                                                 "GetNumberOfConsoleMouseButtons");
    if (!Kernel32Function || !KernelBaseFunction)
    {
        skip("GetNumberOfConsoleMouseButtons is not exported\n");
        if (KernelBase)
            FreeLibrary(KernelBase);
        return;
    }

    FreeConsole();
    Buttons = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Ret = Kernel32Function(&Buttons);
    ok(!Ret, "GetNumberOfConsoleMouseButtons succeeded without a console\n");
    ok_long(GetLastError(), ERROR_INVALID_HANDLE);
    ok_long(Buttons, 0xdeadbeef);

    if (!AllocConsole())
    {
        skip("AllocConsole failed with %lu\n", GetLastError());
        FreeLibrary(KernelBase);
        return;
    }
    Expected = GetSystemMetrics(SM_CMOUSEBUTTONS);
    trace("SM_CMOUSEBUTTONS is %lu\n", Expected);

    Buttons = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Ret = Kernel32Function(&Buttons);
    ok(Ret, "GetNumberOfConsoleMouseButtons failed with %lu\n", GetLastError());
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(Buttons, Expected);

    Buttons = 0xdeadbeef;
    Ret = KernelBaseFunction(&Buttons);
    ok(Ret, "kernelbase GetNumberOfConsoleMouseButtons failed with %lu\n", GetLastError());
    ok_long(Buttons, Expected);

    FreeConsole();
    FreeLibrary(KernelBase);
}
