/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A debugger stop in a system thread runs the start-up file without calling into session drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static KSTART_ROUTINE KdbSystemBreakThread;

static
VOID
NTAPI
KdbSystemBreakThread(
    _In_ PVOID Context)
{
    PBOOLEAN Returned = Context;

    DbgBreakPoint();
    *Returned = TRUE;
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KdbSystemBreak)
{
    BOOLEAN Returned = FALSE;
    PVOID ThreadObject;
    HANDLE Thread;
    NTSTATUS Status;

    if (skip(KD_DEBUGGER_ENABLED, "The kernel debugger is not enabled\n"))
        return;

    /* Hidden test: the first stop of the boot, answered by "cont" in KDBG's start-up file after an unknown command */
    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, KdbSystemBreakThread, &Returned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Thread);
    if (!NT_SUCCESS(Status))
        return;

    KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(ThreadObject);
    ok_eq_bool(Returned, TRUE);
}

/* EOF */
