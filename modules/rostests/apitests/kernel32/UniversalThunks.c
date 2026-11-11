/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that Win32s universal thunks are refused
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_UT_REGISTER)(HMODULE, LPSTR, LPSTR, LPSTR, FARPROC *, FARPROC, LPVOID);
typedef VOID (WINAPI *PFN_UT_UNREGISTER)(HMODULE);

START_TEST(UniversalThunks)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_UT_REGISTER pUTRegister;
    PFN_UT_UNREGISTER pUTUnRegister;
    FARPROC Thunk = NULL;

    pUTRegister = (PFN_UT_REGISTER)GetProcAddress(Kernel32, "UTRegister");
    pUTUnRegister = (PFN_UT_UNREGISTER)GetProcAddress(Kernel32, "UTUnRegister");
    if (!pUTRegister || !pUTUnRegister)
    {
        skip("UTRegister or UTUnRegister is not available\n");
        return;
    }

    ok_int(pUTRegister(GetModuleHandleW(NULL), "UTTEST16.DLL", NULL, "UTProc", &Thunk, NULL, NULL), FALSE);
    ok(Thunk == NULL, "A thunk was returned: %p\n", Thunk);
    ok_int(pUTRegister(GetModuleHandleW(NULL), "UTTEST16.DLL", "UTInit", "UTProc", &Thunk, NULL, NULL), FALSE);
    ok(Thunk == NULL, "A thunk was returned: %p\n", Thunk);

    pUTUnRegister(GetModuleHandleW(NULL));
    pUTUnRegister(NULL);
}
