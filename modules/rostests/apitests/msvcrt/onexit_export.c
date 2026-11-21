/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the _onexit export of msvcrt.dll can be called through GetProcAddress
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <stdlib.h>

typedef _onexit_t (__cdecl *PFN_ONEXIT)(_onexit_t Function);

static
int
__cdecl
EmptyExitFunction(void)
{
    return 0;
}

START_TEST(onexit_export)
{
    HMODULE Module;
    PFN_ONEXIT OnExit;

    Module = GetModuleHandleW(L"msvcrt.dll");
    ok(Module != NULL, "msvcrt.dll is not loaded\n");
    if (Module == NULL)
        return;

    OnExit = (PFN_ONEXIT)GetProcAddress(Module, "_onexit");
    ok(OnExit != NULL, "GetProcAddress(_onexit) failed, error %lu\n", GetLastError());
    if (OnExit == NULL)
        return;

    ok(OnExit(EmptyExitFunction) == EmptyExitFunction, "_onexit did not return the registered function\n");
}
