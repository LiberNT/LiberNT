/*
 * PROJECT:     LiberNT HID input drivers
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Lifetime and cancellation of a reusable HID report request
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

typedef struct _HID_READ_STATE
{
    KSPIN_LOCK Lock;
    KEVENT Idle;
    IO_REMOVE_LOCK RemoveLock;
    PDEVICE_OBJECT Target;
    PIO_COMPLETION_ROUTINE Completion;
    PVOID Context;
    PIRP Irp;
    PMDL Mdl;
    PFILE_OBJECT File;
    ULONG Length;
    ULONG Stops;
    BOOLEAN Started;
    BOOLEAN Removed;
    BOOLEAN Active;
    BOOLEAN Submitting;
    BOOLEAN InFlight;
    BOOLEAN Resubmit;
} HID_READ_STATE, *PHID_READ_STATE;

static inline VOID
HidReadInitialize(
    PHID_READ_STATE Read,
    PDEVICE_OBJECT Target,
    PIO_COMPLETION_ROUTINE Completion,
    PVOID Context,
    ULONG Tag)
{
    KeInitializeSpinLock(&Read->Lock);
    KeInitializeEvent(&Read->Idle, NotificationEvent, TRUE);
    IoInitializeRemoveLock(&Read->RemoveLock, Tag, 0, 0);
    Read->Target = Target;
    Read->Completion = Completion;
    Read->Context = Context;
}

static inline BOOLEAN
HidReadCanSubmit(
    PHID_READ_STATE Read)
{
    return Read->Started && Read->File && !Read->Stops;
}

static inline BOOLEAN
HidReadReserve(
    PHID_READ_STATE Read)
{
    if (Read->Active || Read->Submitting || !HidReadCanSubmit(Read))
        return FALSE;

    ASSERT(!Read->InFlight && !Read->Resubmit);
    KeClearEvent(&Read->Idle);
    Read->Active = Read->Submitting = TRUE;
    return TRUE;
}

static VOID
HidReadSubmit(
    PHID_READ_STATE Read)
{
    PIO_STACK_LOCATION Stack;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    for (;;)
    {
        ASSERT(Read->Submitting && Read->Active && !Read->InFlight);
        if (!HidReadCanSubmit(Read))
        {
            Read->Active = Read->Submitting = FALSE;
            KeSetEvent(&Read->Idle, IO_NO_INCREMENT, FALSE);
            break;
        }

        IoReuseIrp(Read->Irp, STATUS_SUCCESS);
        Read->Irp->MdlAddress = Read->Mdl;
        Stack = IoGetNextIrpStackLocation(Read->Irp);
        Stack->MajorFunction = IRP_MJ_READ;
        Stack->FileObject = Read->File;
        Stack->Parameters.Read.Length = Read->Length;
        Stack->Parameters.Read.Key = 0;
        Stack->Parameters.Read.ByteOffset.QuadPart = 0;
        IoSetCompletionRoutine(Read->Irp, Read->Completion, Read->Context, TRUE, TRUE, TRUE);
        Read->InFlight = TRUE;
        KeReleaseSpinLock(&Read->Lock, OldIrql);

        IoCallDriver(Read->Target, Read->Irp);

        KeAcquireSpinLock(&Read->Lock, &OldIrql);
        if (!Read->Resubmit)
        {
            Read->Submitting = FALSE;
            if (!Read->Active)
                KeSetEvent(&Read->Idle, IO_NO_INCREMENT, FALSE);
            break;
        }

        Read->Resubmit = FALSE;
    }
    KeReleaseSpinLock(&Read->Lock, OldIrql);
}

static inline BOOLEAN
HidReadBeginCompletion(
    PHID_READ_STATE Read,
    NTSTATUS Status)
{
    KIRQL OldIrql;
    BOOLEAN Deliver;

    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    ASSERT(Read->InFlight && Read->Active);
    Read->InFlight = FALSE;
    Deliver = NT_SUCCESS(Status) && HidReadCanSubmit(Read);
    KeReleaseSpinLock(&Read->Lock, OldIrql);
    return Deliver;
}

static inline NTSTATUS
HidReadEndCompletion(
    PHID_READ_STATE Read,
    BOOLEAN Continue)
{
    KIRQL OldIrql;
    BOOLEAN Submit = FALSE;

    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    ASSERT(Read->Active && !Read->InFlight);
    if (!Continue || !HidReadCanSubmit(Read))
    {
        Read->Active = FALSE;
        if (!Read->Submitting)
            KeSetEvent(&Read->Idle, IO_NO_INCREMENT, FALSE);
    }
    else if (Read->Submitting)
    {
        Read->Resubmit = TRUE;
    }
    else
    {
        Read->Submitting = Submit = TRUE;
    }
    KeReleaseSpinLock(&Read->Lock, OldIrql);

    if (Submit)
        HidReadSubmit(Read);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static inline BOOLEAN
HidReadCanOpen(
    PHID_READ_STATE Read)
{
    KIRQL OldIrql;
    BOOLEAN Present;

    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    Present = !Read->Removed;
    KeReleaseSpinLock(&Read->Lock, OldIrql);
    return Present;
}

static inline BOOLEAN
HidReadIsStarted(
    PHID_READ_STATE Read)
{
    KIRQL OldIrql;
    BOOLEAN Started;

    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    Started = Read->Started;
    KeReleaseSpinLock(&Read->Lock, OldIrql);
    return Started;
}

static inline BOOLEAN
HidReadOpen(
    PHID_READ_STATE Read,
    PFILE_OBJECT File)
{
    KIRQL OldIrql;
    BOOLEAN Submit = FALSE;

    ASSERT(File != NULL);
    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    if (!Read->File && !Read->Removed)
    {
        Read->File = File;
        Submit = HidReadReserve(Read);
    }
    KeReleaseSpinLock(&Read->Lock, OldIrql);
    return Submit;
}

static inline BOOLEAN
HidReadStart(
    PHID_READ_STATE Read,
    PMDL Mdl,
    ULONG Length)
{
    KIRQL OldIrql;
    BOOLEAN Submit;

    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    ASSERT(!Read->Started && !Read->Removed && !Read->Active && !Read->Submitting);
    Read->Mdl = Mdl;
    Read->Length = Length;
    Read->Started = TRUE;
    Submit = HidReadReserve(Read);
    KeReleaseSpinLock(&Read->Lock, OldIrql);
    return Submit;
}

static inline BOOLEAN
HidReadBeginStop(
    PHID_READ_STATE Read,
    PFILE_OBJECT File,
    BOOLEAN Remove)
{
    KIRQL OldIrql;
    BOOLEAN Cancel;

    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    if (File)
    {
        if (Read->File != File)
        {
            KeReleaseSpinLock(&Read->Lock, OldIrql);
            return FALSE;
        }
        Read->File = NULL;
    }
    else
    {
        Read->Started = FALSE;
        Read->Removed |= Remove;
    }
    Cancel = (Read->Stops++ == 0 && Read->InFlight);
    KeReleaseSpinLock(&Read->Lock, OldIrql);

    if (Cancel)
        IoCancelIrp(Read->Irp);
    return TRUE;
}

static inline BOOLEAN
HidReadFinishStop(
    PHID_READ_STATE Read)
{
    KIRQL OldIrql;
    BOOLEAN Submit;

    KeWaitForSingleObject(&Read->Idle, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Read->Lock, &OldIrql);
    ASSERT(Read->Stops && !Read->Active && !Read->Submitting && !Read->InFlight);
    --Read->Stops;
    Submit = HidReadReserve(Read);
    KeReleaseSpinLock(&Read->Lock, OldIrql);
    return Submit;
}

static NTSTATUS NTAPI
HidReadReleaseCloseLock(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PHID_READ_STATE Read = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);
    IoReleaseRemoveLock(&Read->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}
