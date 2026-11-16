/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test PsGetProcessProtection on the system process and the caller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef UCHAR (NTAPI *PFN_PS_GET_PROCESS_PROTECTION)(PEPROCESS);

START_TEST(PsProtection)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"PsGetProcessProtection");
    PFN_PS_GET_PROCESS_PROTECTION pPsGetProcessProtection;

    pPsGetProcessProtection = (PFN_PS_GET_PROCESS_PROTECTION)MmGetSystemRoutineAddress(&Name);
    ok(pPsGetProcessProtection != NULL, "PsGetProcessProtection is not exported\n");
    if (pPsGetProcessProtection == NULL)
        return;

    ok_eq_hex(pPsGetProcessProtection(PsInitialSystemProcess), 0x72);
    ok_eq_bool(PsIsProtectedProcess(PsInitialSystemProcess), TRUE);
    ok_eq_bool(PsIsProtectedProcessLight(PsInitialSystemProcess), FALSE);
    ok_eq_hex(pPsGetProcessProtection(PsGetCurrentProcess()), 0);
}
