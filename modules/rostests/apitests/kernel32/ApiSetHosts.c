/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that API set contracts resolve to the modules Windows 11 hosts them on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *IS_API_SET_IMPLEMENTED)(PCSTR);

START_TEST(ApiSetHosts)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    HMODULE Contract = LoadLibraryW(L"api-ms-win-core-apiquery-l2-1-0.dll");
    IS_API_SET_IMPLEMENTED pIsApiSetImplemented;

    ok(KernelBase != NULL, "kernelbase.dll is not loaded\n");
    ok(Contract != NULL, "Loading api-ms-win-core-apiquery-l2-1-0.dll failed with %lu\n", GetLastError());
    if (!Contract)
        return;
    ok(Contract == KernelBase, "api-ms-win-core-apiquery-l2-1-0 resolved to %p, kernelbase.dll is %p\n", Contract, KernelBase);
    pIsApiSetImplemented = (IS_API_SET_IMPLEMENTED)GetProcAddress(Contract, "IsApiSetImplemented");
    ok(pIsApiSetImplemented != NULL, "IsApiSetImplemented is missing from the contract host\n");
    if (pIsApiSetImplemented)
    {
        ok(pIsApiSetImplemented("api-ms-win-core-processthreads-l1-1-0"), "processthreads-l1-1-0 is not implemented\n");
        ok(pIsApiSetImplemented("api-ms-win-core-apiquery-l2-1-0"), "apiquery-l2-1-0 is not implemented\n");
        ok(!pIsApiSetImplemented("api-ms-win-core-nonexistent-l1-1-0"), "A nonexistent contract is implemented\n");
    }
    FreeLibrary(Contract);
}
