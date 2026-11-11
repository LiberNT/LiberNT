/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A file pull that no host answers does not hold up the requests after it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/kdfuncs.h>
#include <ndk/setypes.h>

START_TEST(KdPullRemoteFile)
{
    SYSTEM_KERNEL_DEBUGGER_INFORMATION Info = {0};
    SYSDBG_KD_PULL_REMOTE_FILE Pull;
    LARGE_INTEGER Frequency, Start, End;
    BOOLEAN WasEnabled = FALSE;
    BOOLEAN Ignored;
    NTSTATUS Status;
    ULONG Attempt;

    Status = NtQuerySystemInformation(SystemKernelDebuggerInformation, &Info, sizeof(Info), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && Info.KernelDebuggerEnabled && !Info.KernelDebuggerNotPresent,
             "No kernel debugger is attached\n"))
    {
        return;
    }

    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    ok_eq_hex(Status, STATUS_SUCCESS);

    QueryPerformanceFrequency(&Frequency);
    RtlInitUnicodeString(&Pull.ImageFileName, L"\\SystemRoot\\system32\\drivers\\kmtest_no_such_image.sys");
    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        QueryPerformanceCounter(&Start);
        Status = NtSystemDebugControl(SysDbgKdPullRemoteFile, &Pull, sizeof(Pull), NULL, 0, NULL);
        QueryPerformanceCounter(&End);
        ok(!NT_SUCCESS(Status), "Attempt %lu: Status = 0x%08lx\n", Attempt, Status);
    }

    ok(End.QuadPart - Start.QuadPart < Frequency.QuadPart / 10,
       "The second pull took %I64d ms\n", (End.QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart);

    RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, WasEnabled, FALSE, &Ignored);
}

/* EOF */
