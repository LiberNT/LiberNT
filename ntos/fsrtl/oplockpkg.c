/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The promise a file system makes about who else is using a stream
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "oplockp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* The value the public header gives for GUID_ECP_OPLOCK_KEY, which nothing in the build defines */
static const GUID FsRtlpOplockKeyEcpType =
{
    0x48850596, 0x3050, 0x4be7,
    {0x98, 0x63, 0xfe, 0xc3, 0x50, 0xce, 0x8d, 0x7f}
};

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
FsRtlpCacheLevelOfKind(
    _In_ ULONG Kind)
{
    switch (Kind)
    {
        case FSRTL_OPLOCK_LEVEL_2:
            return OPLOCK_LEVEL_CACHE_READ;

        case FSRTL_OPLOCK_LEVEL_1:
            return OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE;

        case FSRTL_OPLOCK_BATCH:
            return OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE |
                   OPLOCK_LEVEL_CACHE_HANDLE;

        case FSRTL_OPLOCK_FILTER:
            return OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_HANDLE;

        default:
            return 0;
    }
}

static
BOOLEAN
FsRtlpIsExclusiveKind(
    _In_ ULONG Kind,
    _In_ ULONG CacheLevel)
{
    if (FlagOn(Kind, FSRTL_OPLOCK_LEGACY_EXCLUSIVE)) return TRUE;

    if (FlagOn(Kind, FSRTL_OPLOCK_CACHE) &&
        FlagOn(CacheLevel, OPLOCK_LEVEL_CACHE_WRITE))
    {
        return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
FsRtlpIsLevelValid(
    _In_ ULONG CacheLevel)
{
    if (FlagOn(CacheLevel, ~FSRTL_OPLOCK_CACHE_ALL)) return FALSE;

    if (CacheLevel == 0) return FALSE;

    if (!FlagOn(CacheLevel, OPLOCK_LEVEL_CACHE_READ)) return FALSE;

    return TRUE;
}

PFSRTL_OPLOCK_STATE
NTAPI
FsRtlpAllocateOplock(
    _Inout_ POPLOCK Oplock)
{
    PFSRTL_OPLOCK_STATE State = (PFSRTL_OPLOCK_STATE)*Oplock;
    PFSRTL_OPLOCK_STATE Raced;

    if (State != NULL) return State;

    State = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(FSRTL_OPLOCK_STATE),
                                  FSRTL_OPLOCK_TAG);

    if (State == NULL) return NULL;

    RtlZeroMemory(State, sizeof(*State));
    ExInitializeFastMutex(&State->Mutex);
    InitializeListHead(&State->WaitingIrps);
    InitializeListHead(&State->SharedGrants);
    InitializeListHead(&State->BreakingGrants);

    Raced = InterlockedCompareExchangePointer(Oplock, State, NULL);

    if (Raced != NULL)
    {
        ExFreePoolWithTag(State, FSRTL_OPLOCK_TAG);
        return Raced;
    }

    return State;
}

VOID
NTAPI
FsRtlpCompleteOplockIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;

    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
}

VOID
NTAPI
FsRtlpCompleteCollectedIrps(
    _Inout_ PLIST_ENTRY Collected)
{
    while (!IsListEmpty(Collected))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(Collected),
                                     IRP,
                                     Tail.Overlay.ListEntry);

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    }
}

VOID
NTAPI
FsRtlpReleaseWaiters(
    _Inout_ PLIST_ENTRY Released)
{
    while (!IsListEmpty(Released))
    {
        PFSRTL_OPLOCK_WAIT Wait = CONTAINING_RECORD(RemoveHeadList(Released),
                                                    FSRTL_OPLOCK_WAIT,
                                                    Links);

        if (Wait->Mode == FSRTL_WAIT_SYNC)
        {
            KeSetEvent(&Wait->Event, IO_NO_INCREMENT, FALSE);
            continue;
        }

        if (Wait->Mode == FSRTL_WAIT_COMPLETE)
        {
            FsRtlpCompleteOplockIrp(Wait->Irp, Wait->Status, 0);
        }
        else
        {
            Wait->Irp->IoStatus.Status = Wait->Status;
            Wait->CompletionRoutine(Wait->Context, Wait->Irp);
        }

        ExFreePoolWithTag(Wait, FSRTL_OPLOCK_TAG);
    }
}

VOID
NTAPI
FsRtlpCollectWaiters(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _Inout_ PLIST_ENTRY Released,
    _In_ NTSTATUS Status)
{
    while (!IsListEmpty(&State->WaitingIrps))
    {
        PFSRTL_OPLOCK_WAIT Wait =
            CONTAINING_RECORD(RemoveHeadList(&State->WaitingIrps),
                              FSRTL_OPLOCK_WAIT,
                              Links);

        IoSetCancelRoutine(Wait->Irp, NULL);
        Wait->Status = Status;
        InsertTailList(Released, &Wait->Links);
    }
}

static
VOID
NTAPI
FsRtlpCancelWaitingIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PFSRTL_OPLOCK_WAIT Wait = (PFSRTL_OPLOCK_WAIT)
        Irp->Tail.Overlay.DriverContext[0];
    PFSRTL_OPLOCK_STATE State = (PFSRTL_OPLOCK_STATE)
        Irp->Tail.Overlay.DriverContext[1];
    LIST_ENTRY Released;
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    InitializeListHead(&Released);

    ExAcquireFastMutex(&State->Mutex);

    for (Entry = State->WaitingIrps.Flink;
         Entry != &State->WaitingIrps;
         Entry = Entry->Flink)
    {
        if (Entry == &Wait->Links)
        {
            RemoveEntryList(Entry);
            Wait->Status = STATUS_CANCELLED;
            InsertTailList(&Released, &Wait->Links);
            Found = TRUE;
            break;
        }
    }

    ExReleaseFastMutex(&State->Mutex);

    if (Found) FsRtlpReleaseWaiters(&Released);
}

NTSTATUS
NTAPI
FsRtlpWaitOnIrp(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine,
    _In_ ULONG Mode,
    _Inout_opt_ PLIST_ENTRY Collected)
{
    PFSRTL_OPLOCK_WAIT Wait;
    NTSTATUS Status;

    Wait = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(FSRTL_OPLOCK_WAIT),
                                 FSRTL_OPLOCK_TAG);

    if (Wait == NULL)
    {
        ExReleaseFastMutex(&State->Mutex);
        if (Collected != NULL) FsRtlpCompleteCollectedIrps(Collected);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Wait, sizeof(*Wait));
    Wait->Irp = Irp;
    Wait->Context = Context;
    Wait->CompletionRoutine = CompletionRoutine;
    Wait->Status = STATUS_SUCCESS;
    Wait->Mode = Mode;

    if (Mode == FSRTL_WAIT_SYNC)
    {
        KeInitializeEvent(&Wait->Event, NotificationEvent, FALSE);
    }
    else
    {
        if (PostIrpRoutine != NULL) PostIrpRoutine(Context, Irp);

        IoMarkIrpPending(Irp);
    }

    Irp->Tail.Overlay.DriverContext[0] = Wait;
    Irp->Tail.Overlay.DriverContext[1] = State;

    InsertTailList(&State->WaitingIrps, &Wait->Links);
    IoSetCancelRoutine(Irp, FsRtlpCancelWaitingIrp);

    if (Irp->Cancel && (IoSetCancelRoutine(Irp, NULL) != NULL))
    {
        RemoveEntryList(&Wait->Links);
        ExReleaseFastMutex(&State->Mutex);
        ExFreePoolWithTag(Wait, FSRTL_OPLOCK_TAG);

        if (Collected != NULL) FsRtlpCompleteCollectedIrps(Collected);

        return STATUS_CANCELLED;
    }

    ExReleaseFastMutex(&State->Mutex);

    if (Collected != NULL) FsRtlpCompleteCollectedIrps(Collected);

    if (Mode != FSRTL_WAIT_SYNC) return STATUS_PENDING;

    KeWaitForSingleObject(&Wait->Event, Executive, KernelMode, FALSE, NULL);

    Status = Wait->Status;
    ExFreePoolWithTag(Wait, FSRTL_OPLOCK_TAG);

    return Status;
}

BOOLEAN
NTAPI
FsRtlpOplockKeyFromIrp(
    _In_ PIRP Irp,
    _Out_ LPGUID Key)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    POPLOCK_KEY_ECP_CONTEXT Context;
    PECP_LIST EcpList = NULL;
    ULONG Size = 0;

    RtlZeroMemory(Key, sizeof(*Key));

    /* TODO: only a create carries the key; an operation on an open file object has nowhere to read it from until the file object extension holds one */
    if (Stack->MajorFunction != IRP_MJ_CREATE) return FALSE;

    if (!NT_SUCCESS(FsRtlGetEcpListFromIrp(Irp, &EcpList)) ||
        (EcpList == NULL))
    {
        return FALSE;
    }

    if (!NT_SUCCESS(FsRtlFindExtraCreateParameter(EcpList,
                                                  &FsRtlpOplockKeyEcpType,
                                                  (PVOID *)&Context,
                                                  &Size)))
    {
        return FALSE;
    }

    if ((Context == NULL) || (Size < sizeof(OPLOCK_KEY_ECP_CONTEXT)))
    {
        return FALSE;
    }

    *Key = Context->OplockKey;

    return TRUE;
}

BOOLEAN
NTAPI
FsRtlpOplockKeysEqual(
    _In_ PFSRTL_OPLOCK_GRANT Grant,
    _In_ BOOLEAN HasKey,
    _In_ LPCGUID Key)
{
    if (!Grant->HasKey || !HasKey) return FALSE;

    return (BOOLEAN)(RtlCompareMemory(&Grant->Key, Key, sizeof(GUID)) ==
                     sizeof(GUID));
}

ULONG
NTAPI
FsRtlpHeldCacheLevel(
    _In_ PFSRTL_OPLOCK_STATE State)
{
    ULONG Level = 0;
    PLIST_ENTRY Entry;

    if (State->ExclusiveGrant != NULL)
    {
        Level |= State->ExclusiveGrant->CacheLevel;
    }

    for (Entry = State->BreakingGrants.Flink;
         Entry != &State->BreakingGrants;
         Entry = Entry->Flink)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        Level |= Grant->CacheLevel;
    }

    for (Entry = State->SharedGrants.Flink;
         Entry != &State->SharedGrants;
         Entry = Entry->Flink)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        Level |= Grant->CacheLevel;
    }

    return Level;
}

static
ULONG
FsRtlpHeldKinds(
    _In_ PFSRTL_OPLOCK_STATE State)
{
    ULONG Kinds = 0;
    PLIST_ENTRY Entry;

    if (State->ExclusiveGrant != NULL) Kinds |= State->ExclusiveGrant->Kind;

    for (Entry = State->BreakingGrants.Flink;
         Entry != &State->BreakingGrants;
         Entry = Entry->Flink)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        Kinds |= Grant->Kind;
    }

    for (Entry = State->SharedGrants.Flink;
         Entry != &State->SharedGrants;
         Entry = Entry->Flink)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        Kinds |= Grant->Kind;
    }

    return Kinds;
}

static
PREQUEST_OPLOCK_OUTPUT_BUFFER
FsRtlpOplockOutputBuffer(
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MajorFunction != IRP_MJ_FILE_SYSTEM_CONTROL) return NULL;

    if (Stack->Parameters.FileSystemControl.FsControlCode !=
        FSCTL_REQUEST_OPLOCK)
    {
        return NULL;
    }

    if (Stack->Parameters.FileSystemControl.OutputBufferLength <
        sizeof(REQUEST_OPLOCK_OUTPUT_BUFFER))
    {
        return NULL;
    }

    return (PREQUEST_OPLOCK_OUTPUT_BUFFER)Irp->AssociatedIrp.SystemBuffer;
}

static
VOID
FsRtlpWriteBreakOutput(
    _In_ PFSRTL_OPLOCK_GRANT Grant)
{
    PREQUEST_OPLOCK_OUTPUT_BUFFER Output;

    if (!FlagOn(Grant->Kind, FSRTL_OPLOCK_CACHE)) return;

    Output = FsRtlpOplockOutputBuffer(Grant->Irp);
    if (Output == NULL) return;

    Output->StructureVersion = REQUEST_OPLOCK_CURRENT_VERSION;
    Output->StructureLength = sizeof(*Output);
    Output->OriginalOplockLevel = Grant->BrokenFrom;
    Output->NewOplockLevel = Grant->BrokenTo;
    Output->Flags = Grant->AckRequired ?
        REQUEST_OPLOCK_OUTPUT_FLAG_ACK_REQUIRED : 0;
    Output->AccessMode = 0;
    Output->ShareMode = 0;

    Grant->Irp->IoStatus.Information = sizeof(*Output);
}

static
PFSRTL_OPLOCK_GRANT
FsRtlpAllocateGrant(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG Kind,
    _In_ ULONG CacheLevel)
{
    PFSRTL_OPLOCK_GRANT Grant;

    Grant = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(FSRTL_OPLOCK_GRANT),
                                  FSRTL_OPLOCK_TAG);

    if (Grant == NULL) return NULL;

    RtlZeroMemory(Grant, sizeof(*Grant));
    Grant->Irp = Irp;
    Grant->FileObject = Stack->FileObject;
    Grant->Kind = Kind;
    Grant->CacheLevel = CacheLevel;
    Grant->HasKey = FsRtlpOplockKeyFromIrp(Irp, &Grant->Key);

    return Grant;
}

static
VOID
NTAPI
FsRtlpCancelGrantIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PFSRTL_OPLOCK_STATE State = (PFSRTL_OPLOCK_STATE)
        Irp->Tail.Overlay.DriverContext[0];
    PFSRTL_OPLOCK_GRANT Grant = (PFSRTL_OPLOCK_GRANT)
        Irp->Tail.Overlay.DriverContext[1];
    LIST_ENTRY Released;
    BOOLEAN Owned = FALSE;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    InitializeListHead(&Released);

    ExAcquireFastMutex(&State->Mutex);

    if (State->ExclusiveGrant == Grant)
    {
        State->ExclusiveGrant = NULL;
        Owned = TRUE;
    }
    else
    {
        PLIST_ENTRY Entry;

        for (Entry = State->SharedGrants.Flink;
             Entry != &State->SharedGrants;
             Entry = Entry->Flink)
        {
            if (Entry == &Grant->Links)
            {
                RemoveEntryList(Entry);
                Owned = TRUE;
                break;
            }
        }
    }

    if (Owned && (FsRtlpHeldCacheLevel(State) == 0))
    {
        ClearFlag(State->Flags, FSRTL_OPLOCK_STATE_CLOSE_PENDING);
        FsRtlpCollectWaiters(State, &Released, STATUS_SUCCESS);
    }

    ExReleaseFastMutex(&State->Mutex);

    FsRtlpReleaseWaiters(&Released);

    if (Owned)
    {
        FsRtlpCompleteOplockIrp(Irp, STATUS_CANCELLED, 0);
        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
    }
}

static
NTSTATUS
FsRtlpPendGrant(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PFSRTL_OPLOCK_GRANT Grant,
    _In_ BOOLEAN Exclusive)
{
    PIRP Irp = Grant->Irp;

    IoMarkIrpPending(Irp);

    if (Exclusive) State->ExclusiveGrant = Grant;
    else InsertTailList(&State->SharedGrants, &Grant->Links);

    Irp->Tail.Overlay.DriverContext[0] = State;
    Irp->Tail.Overlay.DriverContext[1] = Grant;
    IoSetCancelRoutine(Irp, FsRtlpCancelGrantIrp);

    if (Irp->Cancel && (IoSetCancelRoutine(Irp, NULL) != NULL))
    {
        if (Exclusive) State->ExclusiveGrant = NULL;
        else RemoveEntryList(&Grant->Links);

        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);

        return STATUS_CANCELLED;
    }

    return STATUS_PENDING;
}

static
BOOLEAN
FsRtlpBreakGrant(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _Inout_ PFSRTL_OPLOCK_GRANT Grant,
    _In_ ULONG LostFlags,
    _In_ BOOLEAN Exclusive,
    _Inout_ PLIST_ENTRY Collected)
{
    PIRP Irp = Grant->Irp;
    ULONG NewLevel = Grant->CacheLevel & ~LostFlags;

    Grant->BrokenFrom = Grant->CacheLevel;
    Grant->BrokenTo = NewLevel;
    Grant->AckRequired = FlagOn(LostFlags,
                                OPLOCK_LEVEL_CACHE_WRITE |
                                    OPLOCK_LEVEL_CACHE_HANDLE) ? TRUE : FALSE;

    if (FlagOn(Grant->Kind, FSRTL_OPLOCK_FILTER))
    {
        Grant->BrokenTo = 0;
        NewLevel = 0;
    }

    IoSetCancelRoutine(Irp, NULL);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    if (FlagOn(Grant->Kind, FSRTL_OPLOCK_CACHE))
    {
        FsRtlpWriteBreakOutput(Grant);
    }
    else if (Grant->BrokenTo == OPLOCK_LEVEL_CACHE_READ)
    {
        Irp->IoStatus.Information = FILE_OPLOCK_BROKEN_TO_LEVEL_2;
    }
    else
    {
        Irp->IoStatus.Information = FILE_OPLOCK_BROKEN_TO_NONE;
    }

    InsertTailList(Collected, &Irp->Tail.Overlay.ListEntry);

    if (Exclusive) State->ExclusiveGrant = NULL;
    else RemoveEntryList(&Grant->Links);

    if (!Grant->AckRequired)
    {
        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
        return FALSE;
    }

    Grant->Irp = NULL;
    Grant->CacheLevel = Grant->BrokenTo;

    InsertTailList(&State->BreakingGrants, &Grant->Links);
    SetFlag(State->Flags, FSRTL_OPLOCK_STATE_BREAK_PENDING);

    return TRUE;
}

static
BOOLEAN
FsRtlpHasWritableSection(
    _In_ PIO_STACK_LOCATION Stack)
{
    PFILE_OBJECT FileObject = Stack->FileObject;

    if (FileObject == NULL) return FALSE;
    if (FileObject->SectionObjectPointer == NULL) return FALSE;

    return (BOOLEAN)(MmDoesFileHaveUserWritableReferences(
                         FileObject->SectionObjectPointer) != 0);
}

static
VOID
FsRtlpBreakSharedGrants(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ ULONG LostFlags,
    _In_ BOOLEAN HasKey,
    _In_ LPCGUID Key,
    _In_ BOOLEAN IgnoreKeys,
    _Inout_ PLIST_ENTRY Collected,
    _Out_ PBOOLEAN AckNeeded)
{
    PLIST_ENTRY Entry = State->SharedGrants.Flink;

    *AckNeeded = FALSE;

    while (Entry != &State->SharedGrants)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        Entry = Entry->Flink;

        if (!FlagOn(Grant->CacheLevel, LostFlags)) continue;

        if (!IgnoreKeys && FsRtlpOplockKeysEqual(Grant, HasKey, Key))
        {
            continue;
        }

        if (FsRtlpBreakGrant(State, Grant, LostFlags, FALSE, Collected))
        {
            *AckNeeded = TRUE;
        }
    }
}

static
VOID
FsRtlpSwitchGrantToNewHandle(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _Inout_ PFSRTL_OPLOCK_GRANT Grant,
    _In_ BOOLEAN Exclusive,
    _Inout_ PLIST_ENTRY Collected)
{
    PIRP Irp = Grant->Irp;

    IoSetCancelRoutine(Irp, NULL);
    Irp->IoStatus.Status = STATUS_OPLOCK_SWITCHED_TO_NEW_HANDLE;
    Irp->IoStatus.Information = 0;

    Grant->BrokenFrom = Grant->CacheLevel;
    Grant->BrokenTo = Grant->CacheLevel;
    Grant->AckRequired = FALSE;
    FsRtlpWriteBreakOutput(Grant);

    InsertTailList(Collected, &Irp->Tail.Overlay.ListEntry);

    if (Exclusive) State->ExclusiveGrant = NULL;
    else RemoveEntryList(&Grant->Links);

    ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
}

static
NTSTATUS
FsRtlpGrantLegacyExclusive(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG Kind,
    _In_ ULONG OpenCount)
{
    PFSRTL_OPLOCK_GRANT Grant;
    LIST_ENTRY Collected;
    NTSTATUS Status;

    InitializeListHead(&Collected);

    if (FlagOn(Stack->FileObject->Flags, FO_SYNCHRONOUS_IO))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (OpenCount != 1)
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (FlagOn(State->Flags, FSRTL_OPLOCK_STATE_BREAK_PENDING) ||
        (State->ExclusiveGrant != NULL))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (!IsListEmpty(&State->SharedGrants))
    {
        BOOLEAN AckNeeded;

        if (FlagOn(FsRtlpHeldKinds(State), FSRTL_OPLOCK_CACHE))
        {
            ExReleaseFastMutex(&State->Mutex);
            return STATUS_OPLOCK_NOT_GRANTED;
        }

        FsRtlpBreakSharedGrants(State,
                                FSRTL_OPLOCK_CACHE_ALL,
                                FALSE,
                                NULL,
                                TRUE,
                                &Collected,
                                &AckNeeded);
    }

    Grant = FsRtlpAllocateGrant(Irp,
                                Stack,
                                Kind,
                                FsRtlpCacheLevelOfKind(Kind));

    if (Grant == NULL)
    {
        ExReleaseFastMutex(&State->Mutex);
        FsRtlpCompleteCollectedIrps(&Collected);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FsRtlpPendGrant(State, Grant, TRUE);

    ExReleaseFastMutex(&State->Mutex);
    FsRtlpCompleteCollectedIrps(&Collected);

    return Status;
}

static
NTSTATUS
FsRtlpGrantLevel2(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG OpenCount)
{
    PFSRTL_OPLOCK_GRANT Grant;
    NTSTATUS Status;

    if (FlagOn(Stack->FileObject->Flags, FO_SYNCHRONOUS_IO))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (OpenCount != 0)
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (FlagOn(State->Flags, FSRTL_OPLOCK_STATE_BREAK_PENDING) ||
        (State->ExclusiveGrant != NULL))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (FlagOn(FsRtlpHeldCacheLevel(State), OPLOCK_LEVEL_CACHE_HANDLE))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    Grant = FsRtlpAllocateGrant(Irp,
                                Stack,
                                FSRTL_OPLOCK_LEVEL_2,
                                OPLOCK_LEVEL_CACHE_READ);

    if (Grant == NULL)
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FsRtlpPendGrant(State, Grant, FALSE);

    ExReleaseFastMutex(&State->Mutex);

    return Status;
}

static
NTSTATUS
FsRtlpGrantCacheOplock(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG Requested,
    _In_ ULONG OpenCount,
    _In_ ULONG Flags,
    _In_ BOOLEAN CompleteOnClose)
{
    PFSRTL_OPLOCK_GRANT Grant;
    LIST_ENTRY Collected;
    PLIST_ENTRY Entry;
    GUID Key;
    BOOLEAN HasKey;
    BOOLEAN Exclusive;
    ULONG Kinds;
    NTSTATUS Status;

    InitializeListHead(&Collected);

    if (!FsRtlpIsLevelValid(Requested))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_PARAMETER;
    }

    if (FlagOn(Stack->FileObject->Flags, FO_SYNCHRONOUS_IO))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    Exclusive = FlagOn(Requested, OPLOCK_LEVEL_CACHE_WRITE) ? TRUE : FALSE;

    if (!Exclusive && (OpenCount != 0))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (Exclusive && (OpenCount > 1) &&
        !FlagOn(Flags, OPLOCK_FSCTRL_FLAG_ALL_KEYS_MATCH))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (FsRtlpHasWritableSection(Stack))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_CANNOT_GRANT_REQUESTED_OPLOCK;
    }

    if (FlagOn(State->Flags, FSRTL_OPLOCK_STATE_BREAK_PENDING))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    Kinds = FsRtlpHeldKinds(State);
    HasKey = FsRtlpOplockKeyFromIrp(Irp, &Key);

    if (FlagOn(Kinds, FSRTL_OPLOCK_LEGACY_EXCLUSIVE))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (FlagOn(Requested, OPLOCK_LEVEL_CACHE_HANDLE) &&
        FlagOn(Kinds, FSRTL_OPLOCK_LEVEL_2))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_NOT_GRANTED;
    }

    if (Exclusive)
    {
        if ((State->ExclusiveGrant != NULL) &&
            !FsRtlpOplockKeysEqual(State->ExclusiveGrant, HasKey, &Key))
        {
            ExReleaseFastMutex(&State->Mutex);
            return STATUS_OPLOCK_NOT_GRANTED;
        }

        Entry = State->SharedGrants.Flink;

        while (Entry != &State->SharedGrants)
        {
            PFSRTL_OPLOCK_GRANT Other = CONTAINING_RECORD(Entry,
                                                          FSRTL_OPLOCK_GRANT,
                                                          Links);

            Entry = Entry->Flink;

            if (!FsRtlpOplockKeysEqual(Other, HasKey, &Key))
            {
                ExReleaseFastMutex(&State->Mutex);
                FsRtlpCompleteCollectedIrps(&Collected);

                return STATUS_OPLOCK_NOT_GRANTED;
            }

            FsRtlpSwitchGrantToNewHandle(State, Other, FALSE, &Collected);
        }

        if (State->ExclusiveGrant != NULL)
        {
            FsRtlpSwitchGrantToNewHandle(State,
                                         State->ExclusiveGrant,
                                         TRUE,
                                         &Collected);
        }
    }
    else
    {
        if (State->ExclusiveGrant != NULL)
        {
            ExReleaseFastMutex(&State->Mutex);
            return STATUS_OPLOCK_NOT_GRANTED;
        }

        Entry = State->SharedGrants.Flink;

        while (Entry != &State->SharedGrants)
        {
            PFSRTL_OPLOCK_GRANT Other = CONTAINING_RECORD(Entry,
                                                          FSRTL_OPLOCK_GRANT,
                                                          Links);

            Entry = Entry->Flink;

            if (!FsRtlpOplockKeysEqual(Other, HasKey, &Key)) continue;

            if (FlagOn(Other->CacheLevel, OPLOCK_LEVEL_CACHE_HANDLE) &&
                !FlagOn(Requested, OPLOCK_LEVEL_CACHE_HANDLE))
            {
                ExReleaseFastMutex(&State->Mutex);
                FsRtlpCompleteCollectedIrps(&Collected);

                return STATUS_OPLOCK_NOT_GRANTED;
            }

            FsRtlpSwitchGrantToNewHandle(State, Other, FALSE, &Collected);
        }
    }

    Grant = FsRtlpAllocateGrant(Irp,
                                Stack,
                                FSRTL_OPLOCK_CACHE,
                                Requested);

    if (Grant == NULL)
    {
        ExReleaseFastMutex(&State->Mutex);
        FsRtlpCompleteCollectedIrps(&Collected);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Grant->HasKey = HasKey;
    Grant->Key = Key;
    Grant->CompleteOnClose = CompleteOnClose;

    Status = FsRtlpPendGrant(State, Grant, Exclusive);

    ExReleaseFastMutex(&State->Mutex);
    FsRtlpCompleteCollectedIrps(&Collected);

    return Status;
}

static
PFSRTL_OPLOCK_GRANT
FsRtlpFindBreakingGrant(
    _In_ PFSRTL_OPLOCK_STATE State,
    _In_ PFILE_OBJECT FileObject)
{
    PLIST_ENTRY Entry;

    for (Entry = State->BreakingGrants.Flink;
         Entry != &State->BreakingGrants;
         Entry = Entry->Flink)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        if (Grant->FileObject == FileObject) return Grant;
    }

    return NULL;
}

static
NTSTATUS
FsRtlpAcknowledgeBreak(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG NewLevel,
    _In_ BOOLEAN FromCacheRequest)
{
    PFSRTL_OPLOCK_GRANT Breaking;
    PFSRTL_OPLOCK_GRANT Grant;
    LIST_ENTRY Released;
    NTSTATUS Status;

    InitializeListHead(&Released);

    Breaking = FsRtlpFindBreakingGrant(State, Stack->FileObject);

    if (Breaking == NULL)
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_OPLOCK_PROTOCOL;
    }

    if (FromCacheRequest != (FlagOn(Breaking->Kind, FSRTL_OPLOCK_CACHE) ?
                                 TRUE : FALSE))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_OPLOCK_PROTOCOL;
    }

    if (FlagOn(NewLevel, ~Breaking->CacheLevel))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_OPLOCK_PROTOCOL;
    }

    RemoveEntryList(&Breaking->Links);

    if (IsListEmpty(&State->BreakingGrants))
    {
        ClearFlag(State->Flags,
                  FSRTL_OPLOCK_STATE_BREAK_PENDING |
                      FSRTL_OPLOCK_STATE_CLOSE_PENDING);

        FsRtlpCollectWaiters(State, &Released, STATUS_SUCCESS);
    }

    if (NewLevel == 0)
    {
        ExFreePoolWithTag(Breaking, FSRTL_OPLOCK_TAG);

        ExReleaseFastMutex(&State->Mutex);
        FsRtlpReleaseWaiters(&Released);

        return STATUS_SUCCESS;
    }

    Grant = Breaking;
    Grant->Irp = Irp;
    Grant->CacheLevel = NewLevel;
    Grant->BrokenFrom = 0;
    Grant->BrokenTo = 0;
    Grant->AckRequired = FALSE;

    if (!FromCacheRequest) Grant->Kind = FSRTL_OPLOCK_LEVEL_2;

    Status = FsRtlpPendGrant(State,
                             Grant,
                             FsRtlpIsExclusiveKind(Grant->Kind, NewLevel));

    ExReleaseFastMutex(&State->Mutex);
    FsRtlpReleaseWaiters(&Released);

    return Status;
}

static
NTSTATUS
FsRtlpOpBatchBreakClosePending(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIO_STACK_LOCATION Stack)
{
    PFSRTL_OPLOCK_GRANT Breaking;
    LIST_ENTRY Released;

    InitializeListHead(&Released);

    Breaking = FsRtlpFindBreakingGrant(State, Stack->FileObject);

    if (Breaking == NULL)
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_OPLOCK_PROTOCOL;
    }

    RemoveEntryList(&Breaking->Links);
    ExFreePoolWithTag(Breaking, FSRTL_OPLOCK_TAG);

    if (IsListEmpty(&State->BreakingGrants))
    {
        ClearFlag(State->Flags,
                  FSRTL_OPLOCK_STATE_BREAK_PENDING |
                      FSRTL_OPLOCK_STATE_CLOSE_PENDING);

        FsRtlpCollectWaiters(State, &Released, STATUS_SUCCESS);
    }

    ExReleaseFastMutex(&State->Mutex);
    FsRtlpReleaseWaiters(&Released);

    return STATUS_SUCCESS;
}

static
NTSTATUS
FsRtlpOplockCleanup(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIO_STACK_LOCATION Stack)
{
    LIST_ENTRY Collected;
    LIST_ENTRY Released;
    PLIST_ENTRY Entry;

    InitializeListHead(&Collected);
    InitializeListHead(&Released);

    if ((State->ExclusiveGrant != NULL) &&
        (State->ExclusiveGrant->FileObject == Stack->FileObject))
    {
        PFSRTL_OPLOCK_GRANT Grant = State->ExclusiveGrant;

        State->ExclusiveGrant = NULL;
        IoSetCancelRoutine(Grant->Irp, NULL);
        Grant->BrokenFrom = Grant->CacheLevel;
        Grant->BrokenTo = 0;
        Grant->AckRequired = FALSE;
        Grant->Irp->IoStatus.Status = STATUS_SUCCESS;
        Grant->Irp->IoStatus.Information = FILE_OPLOCK_BROKEN_TO_NONE;
        FsRtlpWriteBreakOutput(Grant);
        InsertTailList(&Collected, &Grant->Irp->Tail.Overlay.ListEntry);
        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
    }

    Entry = State->SharedGrants.Flink;

    while (Entry != &State->SharedGrants)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        Entry = Entry->Flink;

        if (Grant->FileObject != Stack->FileObject) continue;

        RemoveEntryList(&Grant->Links);
        IoSetCancelRoutine(Grant->Irp, NULL);
        Grant->BrokenFrom = Grant->CacheLevel;
        Grant->BrokenTo = 0;
        Grant->AckRequired = FALSE;
        Grant->Irp->IoStatus.Status = STATUS_SUCCESS;
        Grant->Irp->IoStatus.Information = FILE_OPLOCK_BROKEN_TO_NONE;
        FsRtlpWriteBreakOutput(Grant);
        InsertTailList(&Collected, &Grant->Irp->Tail.Overlay.ListEntry);
        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
    }

    Entry = State->BreakingGrants.Flink;

    while (Entry != &State->BreakingGrants)
    {
        PFSRTL_OPLOCK_GRANT Grant = CONTAINING_RECORD(Entry,
                                                      FSRTL_OPLOCK_GRANT,
                                                      Links);

        Entry = Entry->Flink;

        if (Grant->FileObject != Stack->FileObject) continue;

        RemoveEntryList(&Grant->Links);
        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
    }

    if (IsListEmpty(&State->BreakingGrants) &&
        FlagOn(State->Flags, FSRTL_OPLOCK_STATE_BREAK_PENDING))
    {
        ClearFlag(State->Flags,
                  FSRTL_OPLOCK_STATE_BREAK_PENDING |
                      FSRTL_OPLOCK_STATE_CLOSE_PENDING);

        FsRtlpCollectWaiters(State, &Released, STATUS_SUCCESS);
    }

    ExReleaseFastMutex(&State->Mutex);

    FsRtlpReleaseWaiters(&Released);
    FsRtlpCompleteCollectedIrps(&Collected);

    return STATUS_SUCCESS;
}

static
NTSTATUS
FsRtlpRequestCacheOplock(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG OpenCount,
    _In_ ULONG Flags)
{
    PREQUEST_OPLOCK_INPUT_BUFFER Input;
    ULONG InputLength;
    ULONG OutputLength;
    BOOLEAN CompleteOnClose;

    InputLength = Stack->Parameters.FileSystemControl.InputBufferLength;
    OutputLength = Stack->Parameters.FileSystemControl.OutputBufferLength;
    Input = (PREQUEST_OPLOCK_INPUT_BUFFER)Irp->AssociatedIrp.SystemBuffer;

    if ((Input == NULL) ||
        (InputLength < sizeof(REQUEST_OPLOCK_INPUT_BUFFER)) ||
        (OutputLength < sizeof(REQUEST_OPLOCK_OUTPUT_BUFFER)))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if ((Input->StructureVersion != REQUEST_OPLOCK_CURRENT_VERSION) ||
        (Input->StructureLength != sizeof(REQUEST_OPLOCK_INPUT_BUFFER)))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_PARAMETER;
    }

    CompleteOnClose = FlagOn(Input->Flags,
                             REQUEST_OPLOCK_INPUT_FLAG_COMPLETE_ACK_ON_CLOSE) ?
        TRUE : FALSE;

    if (FlagOn(Input->Flags, REQUEST_OPLOCK_INPUT_FLAG_REQUEST) ==
        FlagOn(Input->Flags, REQUEST_OPLOCK_INPUT_FLAG_ACK))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_PARAMETER;
    }

    if (FlagOn(Input->Flags, REQUEST_OPLOCK_INPUT_FLAG_ACK))
    {
        return FsRtlpAcknowledgeBreak(State,
                                      Irp,
                                      Stack,
                                      Input->RequestedOplockLevel,
                                      TRUE);
    }

    return FsRtlpGrantCacheOplock(State,
                                  Irp,
                                  Stack,
                                  Input->RequestedOplockLevel,
                                  OpenCount,
                                  Flags,
                                  CompleteOnClose);
}

NTSTATUS
NTAPI
FsRtlpOplockFsctrlInternal(
    _Inout_ PFSRTL_OPLOCK_STATE State,
    _In_ PIRP Irp,
    _In_ ULONG OpenCount,
    _In_ ULONG Flags)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    ExAcquireFastMutex(&State->Mutex);

    if (Stack->MajorFunction == IRP_MJ_CREATE)
    {
        return FsRtlpGrantLegacyExclusive(State,
                                          Irp,
                                          Stack,
                                          FSRTL_OPLOCK_FILTER,
                                          OpenCount);
    }

    if (Stack->MajorFunction != IRP_MJ_FILE_SYSTEM_CONTROL)
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_INVALID_PARAMETER;
    }

    switch (Stack->Parameters.FileSystemControl.FsControlCode)
    {
        case FSCTL_REQUEST_OPLOCK_LEVEL_1:
            return FsRtlpGrantLegacyExclusive(State,
                                              Irp,
                                              Stack,
                                              FSRTL_OPLOCK_LEVEL_1,
                                              OpenCount);

        case FSCTL_REQUEST_BATCH_OPLOCK:
            return FsRtlpGrantLegacyExclusive(State,
                                              Irp,
                                              Stack,
                                              FSRTL_OPLOCK_BATCH,
                                              OpenCount);

        case FSCTL_REQUEST_FILTER_OPLOCK:
            return FsRtlpGrantLegacyExclusive(State,
                                              Irp,
                                              Stack,
                                              FSRTL_OPLOCK_FILTER,
                                              OpenCount);

        case FSCTL_REQUEST_OPLOCK_LEVEL_2:
            return FsRtlpGrantLevel2(State, Irp, Stack, OpenCount);

        case FSCTL_REQUEST_OPLOCK:
            return FsRtlpRequestCacheOplock(State,
                                            Irp,
                                            Stack,
                                            OpenCount,
                                            Flags);

        case FSCTL_OPLOCK_BREAK_ACKNOWLEDGE:
            return FsRtlpAcknowledgeBreak(State,
                                          Irp,
                                          Stack,
                                          OPLOCK_LEVEL_CACHE_READ,
                                          FALSE);

        case FSCTL_OPLOCK_BREAK_ACK_NO_2:
            return FsRtlpAcknowledgeBreak(State, Irp, Stack, 0, FALSE);

        case FSCTL_OPBATCH_ACK_CLOSE_PENDING:
            return FsRtlpOpBatchBreakClosePending(State, Stack);

        case FSCTL_OPLOCK_BREAK_NOTIFY:
            if (!FlagOn(State->Flags, FSRTL_OPLOCK_STATE_BREAK_PENDING))
            {
                ExReleaseFastMutex(&State->Mutex);
                return STATUS_SUCCESS;
            }

            return FsRtlpWaitOnIrp(State,
                                   Irp,
                                   NULL,
                                   NULL,
                                   NULL,
                                   FSRTL_WAIT_COMPLETE,
                                   NULL);

        default:
            ExReleaseFastMutex(&State->Mutex);
            return STATUS_INVALID_PARAMETER;
    }
}

static
ULONG
FsRtlpCreateLostFlags(
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG Flags)
{
    ULONG Lost = 0;
    ULONG Disposition = (Stack->Parameters.Create.Options >> 24) & 0xFF;
    ACCESS_MASK Desired = 0;

    if (Stack->Parameters.Create.SecurityContext != NULL)
    {
        Desired = Stack->Parameters.Create.SecurityContext->DesiredAccess;
    }

    if (FlagOn(Desired,
               FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES |
                   FILE_WRITE_EA) ||
        (Disposition == FILE_SUPERSEDE) || (Disposition == FILE_OVERWRITE) ||
        (Disposition == FILE_OVERWRITE_IF))
    {
        Lost |= OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE;
    }

    if (FlagOn(Desired, DELETE) ||
        FlagOn(Stack->Parameters.Create.Options, FILE_DELETE_ON_CLOSE))
    {
        Lost |= OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE |
                OPLOCK_LEVEL_CACHE_HANDLE;
    }

    if (FlagOn(Flags,
               OPLOCK_FLAG_CLOSING_DELETE_ON_CLOSE |
                   OPLOCK_FLAG_REMOVING_FILE_OR_LINK))
    {
        Lost |= OPLOCK_LEVEL_CACHE_HANDLE;
    }

    return Lost;
}

static
ULONG
FsRtlpLostFlagsForIrp(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG Flags,
    _In_ ULONG ForcedLoss)
{
    if (ForcedLoss != 0) return ForcedLoss;

    if (FlagOn(Irp->Flags, IRP_PAGING_IO)) return 0;

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            return FsRtlpCreateLostFlags(Stack, Flags);

        case IRP_MJ_READ:
        case IRP_MJ_FLUSH_BUFFERS:
            return OPLOCK_LEVEL_CACHE_WRITE;

        case IRP_MJ_WRITE:
        case IRP_MJ_LOCK_CONTROL:
            return OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE;

        case IRP_MJ_FILE_SYSTEM_CONTROL:
            return OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE;

        case IRP_MJ_SET_INFORMATION:
            switch (Stack->Parameters.SetFile.FileInformationClass)
            {
                case FileEndOfFileInformation:
                case FileAllocationInformation:
                case FileShortNameInformation:
                    return OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE;

                case FileDispositionInformation:
                case FileRenameInformation:
                case FileLinkInformation:
                    return OPLOCK_LEVEL_CACHE_READ |
                           OPLOCK_LEVEL_CACHE_WRITE |
                           OPLOCK_LEVEL_CACHE_HANDLE;

                default:
                    return 0;
            }

        default:
            return 0;
    }
}

NTSTATUS
NTAPI
FsRtlpCheckOplockInternal(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine,
    _In_ ULONG ForcedLoss)
{
    PFSRTL_OPLOCK_STATE State = (PFSRTL_OPLOCK_STATE)*Oplock;
    PIO_STACK_LOCATION Stack;
    LIST_ENTRY Collected;
    BOOLEAN IgnoreKeys;
    BOOLEAN AckNeeded = FALSE;
    BOOLEAN HasKey;
    BOOLEAN CompleteIfOplocked;
    GUID Key;
    ULONG Lost;

    if (State == NULL) return STATUS_SUCCESS;

    Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MajorFunction == IRP_MJ_CLEANUP)
    {
        ExAcquireFastMutex(&State->Mutex);
        return FsRtlpOplockCleanup(State, Stack);
    }

    InitializeListHead(&Collected);

    IgnoreKeys = FlagOn(Flags, OPLOCK_FLAG_IGNORE_OPLOCK_KEYS) ? TRUE : FALSE;

    CompleteIfOplocked = (FlagOn(Flags, OPLOCK_FLAG_COMPLETE_IF_OPLOCKED) ||
                          ((Stack->MajorFunction == IRP_MJ_CREATE) &&
                           FlagOn(Stack->Parameters.Create.Options,
                                  FILE_COMPLETE_IF_OPLOCKED))) ? TRUE : FALSE;

    Lost = FsRtlpLostFlagsForIrp(Irp, Stack, Flags, ForcedLoss);

    HasKey = FsRtlpOplockKeyFromIrp(Irp, &Key);

    ExAcquireFastMutex(&State->Mutex);

    if ((Lost == 0) || !FlagOn(FsRtlpHeldCacheLevel(State), Lost))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_SUCCESS;
    }

    if (FlagOn(Flags, OPLOCK_FLAG_OPLOCK_KEY_CHECK_ONLY))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_OPLOCK_BREAK_IN_PROGRESS;
    }

    if ((FlagOn(Flags, OPLOCK_FLAG_BACK_OUT_ATOMIC_OPLOCK)) ||
        ((Stack->MajorFunction == IRP_MJ_CREATE) &&
         FlagOn(Stack->Parameters.Create.Options,
                FILE_OPEN_REQUIRING_OPLOCK)))
    {
        ExReleaseFastMutex(&State->Mutex);
        return STATUS_CANNOT_BREAK_OPLOCK;
    }

    if (FlagOn(State->Flags, FSRTL_OPLOCK_STATE_BREAK_PENDING))
    {
        if (CompleteIfOplocked)
        {
            ExReleaseFastMutex(&State->Mutex);
            return STATUS_OPLOCK_BREAK_IN_PROGRESS;
        }

        return FsRtlpWaitOnIrp(State,
                               Irp,
                               Context,
                               CompletionRoutine,
                               PostIrpRoutine,
                               (CompletionRoutine != NULL) ?
                                   FSRTL_WAIT_CALLBACK : FSRTL_WAIT_SYNC,
                               NULL);
    }

    FsRtlpBreakSharedGrants(State,
                            Lost,
                            HasKey,
                            &Key,
                            IgnoreKeys,
                            &Collected,
                            &AckNeeded);

    if ((State->ExclusiveGrant != NULL) &&
        FlagOn(State->ExclusiveGrant->CacheLevel, Lost) &&
        (IgnoreKeys ||
         !FsRtlpOplockKeysEqual(State->ExclusiveGrant, HasKey, &Key)))
    {
        if (FsRtlpBreakGrant(State,
                             State->ExclusiveGrant,
                             Lost,
                             TRUE,
                             &Collected))
        {
            AckNeeded = TRUE;
        }
    }

    if (!AckNeeded)
    {
        ExReleaseFastMutex(&State->Mutex);
        FsRtlpCompleteCollectedIrps(&Collected);

        return STATUS_SUCCESS;
    }

    if (CompleteIfOplocked)
    {
        ExReleaseFastMutex(&State->Mutex);
        FsRtlpCompleteCollectedIrps(&Collected);

        return STATUS_OPLOCK_BREAK_IN_PROGRESS;
    }

    return FsRtlpWaitOnIrp(State,
                           Irp,
                           Context,
                           CompletionRoutine,
                           PostIrpRoutine,
                           (CompletionRoutine != NULL) ?
                               FSRTL_WAIT_CALLBACK : FSRTL_WAIT_SYNC,
                           &Collected);
}

NTSTATUS
NTAPI
FsRtlpOplockBreakToNoneInternal(
    _Inout_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine)
{
    return FsRtlpCheckOplockInternal(Oplock,
                                     Irp,
                                     Flags | OPLOCK_FLAG_IGNORE_OPLOCK_KEYS,
                                     Context,
                                     CompletionRoutine,
                                     PostIrpRoutine,
                                     FSRTL_OPLOCK_CACHE_ALL);
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlInitializeOplock(
    _Inout_ POPLOCK Oplock)
{
    PAGED_CODE();

    *Oplock = NULL;
}

VOID
NTAPI
FsRtlUninitializeOplock(
    _Inout_ POPLOCK Oplock)
{
    PFSRTL_OPLOCK_STATE State;
    LIST_ENTRY Collected;
    LIST_ENTRY Released;

    PAGED_CODE();

    State = (PFSRTL_OPLOCK_STATE)InterlockedExchangePointer(Oplock, NULL);
    if (State == NULL) return;

    InitializeListHead(&Collected);
    InitializeListHead(&Released);

    ExAcquireFastMutex(&State->Mutex);

    if (State->ExclusiveGrant != NULL)
    {
        PFSRTL_OPLOCK_GRANT Grant = State->ExclusiveGrant;

        State->ExclusiveGrant = NULL;
        IoSetCancelRoutine(Grant->Irp, NULL);
        Grant->BrokenFrom = Grant->CacheLevel;
        Grant->BrokenTo = 0;
        Grant->AckRequired = FALSE;
        Grant->Irp->IoStatus.Status = STATUS_SUCCESS;
        Grant->Irp->IoStatus.Information = FILE_OPLOCK_BROKEN_TO_NONE;
        FsRtlpWriteBreakOutput(Grant);
        InsertTailList(&Collected, &Grant->Irp->Tail.Overlay.ListEntry);
        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
    }

    while (!IsListEmpty(&State->SharedGrants))
    {
        PFSRTL_OPLOCK_GRANT Grant =
            CONTAINING_RECORD(RemoveHeadList(&State->SharedGrants),
                              FSRTL_OPLOCK_GRANT,
                              Links);

        IoSetCancelRoutine(Grant->Irp, NULL);
        Grant->BrokenFrom = Grant->CacheLevel;
        Grant->BrokenTo = 0;
        Grant->AckRequired = FALSE;
        Grant->Irp->IoStatus.Status = STATUS_SUCCESS;
        Grant->Irp->IoStatus.Information = FILE_OPLOCK_BROKEN_TO_NONE;
        FsRtlpWriteBreakOutput(Grant);
        InsertTailList(&Collected, &Grant->Irp->Tail.Overlay.ListEntry);
        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
    }

    while (!IsListEmpty(&State->BreakingGrants))
    {
        PFSRTL_OPLOCK_GRANT Grant =
            CONTAINING_RECORD(RemoveHeadList(&State->BreakingGrants),
                              FSRTL_OPLOCK_GRANT,
                              Links);

        ExFreePoolWithTag(Grant, FSRTL_OPLOCK_TAG);
    }

    FsRtlpCollectWaiters(State, &Released, STATUS_SUCCESS);

    ExReleaseFastMutex(&State->Mutex);

    FsRtlpReleaseWaiters(&Released);
    FsRtlpCompleteCollectedIrps(&Collected);

    ExFreePoolWithTag(State, FSRTL_OPLOCK_TAG);
}

NTSTATUS
NTAPI
FsRtlOplockFsctrl(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG OpenCount)
{
    PFSRTL_OPLOCK_STATE State;

    PAGED_CODE();

    State = FsRtlpAllocateOplock(Oplock);
    if (State == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    return FsRtlpOplockFsctrlInternal(State, Irp, OpenCount, 0);
}

NTSTATUS
NTAPI
FsRtlCheckOplock(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine)
{
    PAGED_CODE();

    return FsRtlpCheckOplockInternal(Oplock,
                                     Irp,
                                     0,
                                     Context,
                                     CompletionRoutine,
                                     PostIrpRoutine,
                                     0);
}

NTSTATUS
NTAPI
FsRtlOplockBreakToNone(
    _Inout_ POPLOCK Oplock,
    _In_opt_ PIO_STACK_LOCATION IrpSp,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(IrpSp);

    return FsRtlpOplockBreakToNoneInternal(Oplock,
                                           Irp,
                                           0,
                                           Context,
                                           CompletionRoutine,
                                           PostIrpRoutine);
}

BOOLEAN
NTAPI
FsRtlOplockIsFastIoPossible(
    _In_ POPLOCK Oplock)
{
    PFSRTL_OPLOCK_STATE State = (PFSRTL_OPLOCK_STATE)*Oplock;
    BOOLEAN Possible;

    PAGED_CODE();

    if (State == NULL) return TRUE;

    ExAcquireFastMutex(&State->Mutex);

    Possible = (BOOLEAN)(!FlagOn(State->Flags,
                                 FSRTL_OPLOCK_STATE_BREAK_PENDING) &&
                         (State->ExclusiveGrant == NULL));

    ExReleaseFastMutex(&State->Mutex);

    return Possible;
}

BOOLEAN
NTAPI
FsRtlCurrentBatchOplock(
    _In_ POPLOCK Oplock)
{
    PFSRTL_OPLOCK_STATE State = (PFSRTL_OPLOCK_STATE)*Oplock;
    BOOLEAN Held;

    PAGED_CODE();

    if (State == NULL) return FALSE;

    ExAcquireFastMutex(&State->Mutex);

    Held = FlagOn(FsRtlpHeldKinds(State),
                  FSRTL_OPLOCK_BATCH | FSRTL_OPLOCK_FILTER) ? TRUE : FALSE;

    ExReleaseFastMutex(&State->Mutex);

    return Held;
}

/* EOF */
