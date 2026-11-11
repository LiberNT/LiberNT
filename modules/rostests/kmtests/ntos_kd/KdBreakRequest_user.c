/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A break asked for through NtSystemDebugControl reaches the kernel debugger and returns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/kdfuncs.h>
#include <ndk/setypes.h>

START_TEST(KdBreakRequest)
{
    SYSTEM_KERNEL_DEBUGGER_INFORMATION Info = {0};
    BOOLEAN WasEnabled = FALSE;
    BOOLEAN Ignored;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemKernelDebuggerInformation, &Info, sizeof(Info), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && Info.KernelDebuggerEnabled, "The kernel debugger is not enabled\n"))
        return;

    /* Hidden test: something must answer this stop, such as KDBG with "cont" in its start-up file */
    ok(!Info.KernelDebuggerNotPresent, "KernelDebuggerNotPresent = %u\n", Info.KernelDebuggerNotPresent);

    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtSystemDebugControl(SysDbgBreakPoint, NULL, 0, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, WasEnabled, FALSE, &Ignored);
}

/* EOF */
