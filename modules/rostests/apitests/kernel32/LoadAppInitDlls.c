/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test LoadAppInitDlls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef VOID (WINAPI *PFN_LOAD_APP_INIT_DLLS)(VOID);

START_TEST(LoadAppInitDlls)
{
    PFN_LOAD_APP_INIT_DLLS pKernelBaseLoadAppInitDlls, pKernel32LoadAppInitDlls;
    NTSTATUS Status = STATUS_SUCCESS;

    pKernelBaseLoadAppInitDlls = (PFN_LOAD_APP_INIT_DLLS)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),
                                                                        "LoadAppInitDlls");
    pKernel32LoadAppInitDlls = (PFN_LOAD_APP_INIT_DLLS)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                      "LoadAppInitDlls");
    if (!pKernelBaseLoadAppInitDlls)
    {
        skip("LoadAppInitDlls is not available\n");
        return;
    }
    ok(pKernel32LoadAppInitDlls == pKernelBaseLoadAppInitDlls,
       "kernel32 LoadAppInitDlls %p, kernelbase %p\n", pKernel32LoadAppInitDlls, pKernelBaseLoadAppInitDlls);

    _SEH2_TRY
    {
        pKernelBaseLoadAppInitDlls();
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_hex(Status, STATUS_SUCCESS);
}
