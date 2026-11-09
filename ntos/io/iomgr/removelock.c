/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The lock that holds a device alive while requests are in flight
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
IoInitializeRemoveLockEx(
    _Out_ PIO_REMOVE_LOCK RemoveLock,
    _In_ ULONG AllocateTag,
    _In_ ULONG MaxLockedMinutes,
    _In_ ULONG HighWatermark,
    _In_ ULONG RemlockSize)
{
    PEXTENDED_IO_REMOVE_LOCK Lock = (PEXTENDED_IO_REMOVE_LOCK)RemoveLock;

    RtlZeroMemory(Lock, RemlockSize);

    Lock->Common.Removed = FALSE;
    Lock->Common.IoCount = 1;

    KeInitializeEvent(&Lock->Common.RemoveEvent, SynchronizationEvent, FALSE);

    if (RemlockSize < sizeof(EXTENDED_IO_REMOVE_LOCK)) return;

    Lock->Dbg.Signature = 'COLR';
    Lock->Dbg.HighWatermark = HighWatermark;
    Lock->Dbg.MaxLockedTicks = KeQueryTimeIncrement() != 0
                                   ? (LONGLONG)MaxLockedMinutes * 60 * 10000000
                                   : 0;
    Lock->Dbg.AllocateTag = AllocateTag;

    KeInitializeSpinLock(&Lock->Dbg.Spin);
}

NTSTATUS
NTAPI
IoAcquireRemoveLockEx(
    _Inout_ PIO_REMOVE_LOCK RemoveLock,
    _In_opt_ PVOID Tag,
    _In_ PCSTR File,
    _In_ ULONG Line,
    _In_ ULONG RemlockSize)
{
    PEXTENDED_IO_REMOVE_LOCK Lock = (PEXTENDED_IO_REMOVE_LOCK)RemoveLock;
    LONG Count;

    UNREFERENCED_PARAMETER(Tag);
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Line);
    UNREFERENCED_PARAMETER(RemlockSize);

    Count = InterlockedIncrement(&Lock->Common.IoCount);
    ASSERT(Count > 0);

    if (!Lock->Common.Removed) return STATUS_SUCCESS;

    if (InterlockedDecrement(&Lock->Common.IoCount) == 0)
    {
        KeSetEvent(&Lock->Common.RemoveEvent, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_DELETE_PENDING;
}

VOID
NTAPI
IoReleaseRemoveLockEx(
    _Inout_ PIO_REMOVE_LOCK RemoveLock,
    _In_opt_ PVOID Tag,
    _In_ ULONG RemlockSize)
{
    PEXTENDED_IO_REMOVE_LOCK Lock = (PEXTENDED_IO_REMOVE_LOCK)RemoveLock;
    LONG Count;

    UNREFERENCED_PARAMETER(Tag);
    UNREFERENCED_PARAMETER(RemlockSize);

    Count = InterlockedDecrement(&Lock->Common.IoCount);
    ASSERT(Count >= 0);

    if (Count != 0) return;

    ASSERT(Lock->Common.Removed);

    KeSetEvent(&Lock->Common.RemoveEvent, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
IoReleaseRemoveLockAndWaitEx(
    _Inout_ PIO_REMOVE_LOCK RemoveLock,
    _In_opt_ PVOID Tag,
    _In_ ULONG RemlockSize)
{
    PEXTENDED_IO_REMOVE_LOCK Lock = (PEXTENDED_IO_REMOVE_LOCK)RemoveLock;

    PAGED_CODE();

    Lock->Common.Removed = TRUE;

    IoReleaseRemoveLockEx(RemoveLock, Tag, RemlockSize);

    if (InterlockedDecrement(&Lock->Common.IoCount) > 0)
    {
        KeWaitForSingleObject(&Lock->Common.RemoveEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}
