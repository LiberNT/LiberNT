/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger with a kernel APC held back, for its thread and APC views
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static KAPC HeldApc;
static volatile LONG HeldApcRuns;

static
VOID
NTAPI
HeldApcKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    InterlockedIncrement(&HeldApcRuns);
}

START_TEST(KdbThreadApc)
{
    BOOLEAN Inserted;

    KeEnterGuardedRegion();

    KeInitializeApc(&HeldApc,
                    KeGetCurrentThread(),
                    OriginalApcEnvironment,
                    HeldApcKernelRoutine,
                    NULL,
                    NULL,
                    KernelMode,
                    NULL);
    Inserted = KeInsertQueueApc(&HeldApc, NULL, NULL, IO_NO_INCREMENT);
    ok_bool_true(Inserted, "KeInsertQueueApc");

    /* The debugger's start-up file looks at this thread and its queue here */
    DbgBreakPoint();

    ok_eq_long(HeldApcRuns, 0);
    KeLeaveGuardedRegion();
    ok_eq_long(HeldApcRuns, Inserted ? 1 : 0);
}
