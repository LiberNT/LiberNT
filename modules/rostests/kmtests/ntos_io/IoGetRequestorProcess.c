/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoGetRequestorProcess names the process the requesting thread is attached to
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
PEPROCESS
RequestorOfNewIrp(
    _Out_ PULONG ProcessId)
{
    PEPROCESS Requestor;
    PIRP Irp;

    *ProcessId = MAXULONG;

    Irp = IoAllocateIrp(1, FALSE);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (Irp == NULL) return NULL;

    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Requestor = IoGetRequestorProcess(Irp);
    *ProcessId = IoGetRequestorProcessId(Irp);

    IoFreeIrp(Irp);
    return Requestor;
}

START_TEST(IoGetRequestorProcess)
{
    PEPROCESS Own = PsGetCurrentProcess();
    PEPROCESS Requestor;
    KAPC_STATE ApcState;
    ULONG ProcessId;
    PIRP Irp;

    Irp = IoAllocateIrp(1, FALSE);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (Irp != NULL)
    {
        ok_eq_pointer(IoGetRequestorProcess(Irp), NULL);
        IoFreeIrp(Irp);
    }

    Requestor = RequestorOfNewIrp(&ProcessId);
    ok_eq_pointer(Requestor, Own);
    ok_eq_ulong(ProcessId, (ULONG)(ULONG_PTR)PsGetProcessId(Own));

    if (skip(Own != PsInitialSystemProcess, "The test runs in the system process\n"))
        return;

    KeStackAttachProcess((PRKPROCESS)PsInitialSystemProcess, &ApcState);
    Requestor = RequestorOfNewIrp(&ProcessId);
    KeUnstackDetachProcess(&ApcState);

    ok_eq_pointer(Requestor, PsInitialSystemProcess);
    ok_eq_ulong(ProcessId, (ULONG)(ULONG_PTR)PsGetProcessId(PsInitialSystemProcess));
}
