/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Retirement of a completed synchronous paging request
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define PAGING_IRP_TAG          ' prI'
#define PAGING_IRP_STACK_SIZE   16
#define PAGING_IRP_ROUNDS       256
#define PAGING_IRP_TOLERANCE    32

static
BOOLEAN
QueryLiveIrps(
    _Out_ PLONG Live)
{
    PSYSTEM_POOLTAG_INFORMATION Info;
    ULONG Length = 0;
    NTSTATUS Status;
    ULONG i;

    *Live = 0;

    Status = ZwQuerySystemInformation(SystemPoolTagInformation, NULL, 0, &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH || Length == 0) return FALSE;

    Length += 64 * sizeof(SYSTEM_POOLTAG);
    Info = ExAllocatePoolWithTag(PagedPool, Length, 'tPmK');
    if (Info == NULL) return FALSE;

    Status = ZwQuerySystemInformation(SystemPoolTagInformation, Info, Length, NULL);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Info, 'tPmK');
        return FALSE;
    }

    for (i = 0; i < Info->Count; i++)
    {
        if (Info->TagInfo[i].TagUlong != PAGING_IRP_TAG) continue;

        *Live = (LONG)(Info->TagInfo[i].NonPagedAllocs - Info->TagInfo[i].NonPagedFrees);
        ExFreePoolWithTag(Info, 'tPmK');
        return TRUE;
    }

    ExFreePoolWithTag(Info, 'tPmK');
    return FALSE;
}

START_TEST(IoPagingIrpRetire)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    BOOLEAN Allocated = TRUE;
    LONG Before, After;
    KEVENT Event;
    ULONG Rounds;
    PIRP Irp;

    if (skip(QueryLiveIrps(&Before), "Pool tag information unavailable\n")) return;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    RtlZeroMemory(&IoStatus, sizeof(IoStatus));

    for (Rounds = 0; Rounds < PAGING_IRP_ROUNDS; Rounds++)
    {
        Irp = IoAllocateIrp(PAGING_IRP_STACK_SIZE, FALSE);
        if (Irp == NULL)
        {
            Allocated = FALSE;
            break;
        }

        KeClearEvent(&Event);
        Irp->Flags = IRP_PAGING_IO | IRP_NOCACHE | IRP_SYNCHRONOUS_PAGING_IO;
        Irp->RequestorMode = KernelMode;
        Irp->UserIosb = &IoStatus;
        Irp->UserEvent = &Event;
        Irp->Tail.Overlay.Thread = PsGetCurrentThread();

        IoSetNextIrpStackLocation(Irp);
        Stack = IoGetCurrentIrpStackLocation(Irp);
        Stack->MajorFunction = IRP_MJ_READ;

        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = PAGE_SIZE;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    ok_bool_true(Allocated, "IoAllocateIrp");
    ok_eq_ulong(Rounds, (ULONG)PAGING_IRP_ROUNDS);

    /* The issuer sees the result in its own status block and event */
    ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(IoStatus.Information, PAGE_SIZE);
    ok_bool_true(KeReadStateEvent(&Event) != 0, "Event signalled");

    /* The packet itself belongs to the I/O manager, which retires it */
    if (!skip(QueryLiveIrps(&After), "Pool tag information unavailable\n"))
    {
        ok(After - Before < PAGING_IRP_TOLERANCE,
           "%ld packets of %lu outlived their completion\n",
           After - Before, (ULONG)PAGING_IRP_ROUNDS);
    }
}
