/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The oplock calls that name a level the caller cares about
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "oplockp.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlCheckOplockEx(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine)
{
    PAGED_CODE();

    return FsRtlpCheckOplockInternal(Oplock,
                                     Irp,
                                     Flags,
                                     Context,
                                     CompletionRoutine,
                                     PostIrpRoutine,
                                     0);
}

NTSTATUS
NTAPI
FsRtlOplockBreakH(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine)
{
    PAGED_CODE();

    return FsRtlpCheckOplockInternal(Oplock,
                                     Irp,
                                     Flags,
                                     Context,
                                     CompletionRoutine,
                                     PostIrpRoutine,
                                     OPLOCK_LEVEL_CACHE_HANDLE);
}

BOOLEAN
NTAPI
FsRtlCurrentOplockH(
    _In_ POPLOCK Oplock)
{
    PFSRTL_OPLOCK_STATE State = (PFSRTL_OPLOCK_STATE)*Oplock;
    BOOLEAN Held;

    PAGED_CODE();

    if (State == NULL) return FALSE;

    ExAcquireFastMutex(&State->Mutex);

    Held = FlagOn(FsRtlpHeldCacheLevel(State), OPLOCK_LEVEL_CACHE_HANDLE) ?
        TRUE : FALSE;

    ExReleaseFastMutex(&State->Mutex);

    return Held;
}

BOOLEAN
NTAPI
FsRtlOplockIsSharedRequest(
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PREQUEST_OPLOCK_INPUT_BUFFER Input;

    PAGED_CODE();

    if (Stack->MajorFunction != IRP_MJ_FILE_SYSTEM_CONTROL) return FALSE;

    if (Stack->Parameters.FileSystemControl.FsControlCode ==
        FSCTL_REQUEST_OPLOCK_LEVEL_2)
    {
        return TRUE;
    }

    if (Stack->Parameters.FileSystemControl.FsControlCode !=
        FSCTL_REQUEST_OPLOCK)
    {
        return FALSE;
    }

    if (Stack->Parameters.FileSystemControl.InputBufferLength <
        sizeof(REQUEST_OPLOCK_INPUT_BUFFER))
    {
        return FALSE;
    }

    Input = (PREQUEST_OPLOCK_INPUT_BUFFER)Irp->AssociatedIrp.SystemBuffer;
    if (Input == NULL) return FALSE;

    if (!FlagOn(Input->Flags, REQUEST_OPLOCK_INPUT_FLAG_REQUEST)) return FALSE;

    return FlagOn(Input->RequestedOplockLevel, OPLOCK_LEVEL_CACHE_WRITE) ?
        FALSE : TRUE;
}

/* EOF */
