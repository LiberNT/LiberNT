/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ProcessUserModeIOPL needs the TCB privilege and is not implemented on 64-bit Windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(ProcessUserModeIopl)
{
    BOOLEAN WasEnabled, Ignored;
    NTSTATUS Status;

    if (sizeof(PVOID) != sizeof(ULONG64))
    {
        skip("32-bit Windows grants user-mode port I/O\n");
        return;
    }

    Status = RtlAdjustPrivilege(SE_TCB_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (!NT_SUCCESS(Status))
    {
        skip("the TCB privilege is not held: 0x%lx\n", Status);
        return;
    }

    ok_ntstatus(NtSetInformationProcess(NtCurrentProcess(), ProcessUserModeIOPL, NULL, 0), STATUS_NOT_IMPLEMENTED);

    RtlAdjustPrivilege(SE_TCB_PRIVILEGE, FALSE, FALSE, &Ignored);
    ok_ntstatus(NtSetInformationProcess(NtCurrentProcess(), ProcessUserModeIOPL, NULL, 0), STATUS_PRIVILEGE_NOT_HELD);

    RtlAdjustPrivilege(SE_TCB_PRIVILEGE, WasEnabled, FALSE, &Ignored);
}
