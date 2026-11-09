/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling whoever watches a directory what happened inside it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_NOTIFY_TAG 'NRsF'

#define FSRTL_NOTIFY_INLINE_WATCHERS 16

/* PRIVATE FUNCTIONS **********************************************************/

static
PREAL_NOTIFY_SYNC
FsRtlpSyncOf(
    _In_ PNOTIFY_SYNC NotifySync)
{
    return (PREAL_NOTIFY_SYNC)NotifySync;
}

static
VOID
FsRtlNotifyAcquireFastMutex(
    _In_ PREAL_NOTIFY_SYNC Sync)
{
    if (Sync->OwningThread == (ULONG_PTR)KeGetCurrentThread())
    {
        Sync->OwnerCount++;
        return;
    }

    ExAcquireFastMutex(&Sync->FastMutex);
    Sync->OwningThread = (ULONG_PTR)KeGetCurrentThread();
    Sync->OwnerCount = 1;
}

static
VOID
FsRtlNotifyReleaseFastMutex(
    _In_ PREAL_NOTIFY_SYNC Sync)
{
    if (--Sync->OwnerCount != 0) return;

    Sync->OwningThread = 0;
    ExReleaseFastMutex(&Sync->FastMutex);
}

static
PVOID
FsRtlpIrpBuffer(
    _In_ PIRP Irp,
    _Out_ PULONG Length)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    *Length = Stack->Parameters.NotifyDirectory.Length;

    if (Irp->AssociatedIrp.SystemBuffer != NULL)
    {
        return Irp->AssociatedIrp.SystemBuffer;
    }

    if (Irp->MdlAddress != NULL)
    {
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                                            NormalPagePriority);
    }

    *Length = 0;

    return NULL;
}

static
PNOTIFY_CHANGE
FsRtlpFindNotify(
    _In_ PLIST_ENTRY NotifyList,
    _In_ PVOID FsContext)
{
    PLIST_ENTRY Entry;

    for (Entry = NotifyList->Flink;
         Entry != NotifyList;
         Entry = Entry->Flink)
    {
        PNOTIFY_CHANGE Notify = CONTAINING_RECORD(Entry,
                                                  NOTIFY_CHANGE,
                                                  NotifyList);

        if (Notify->FsContext == FsContext) return Notify;
    }

    return NULL;
}

static
VOID
FsRtlNotifyCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;

    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
}

static
VOID
FsRtlNotifyCompleteIrpList(
    _Inout_ PLIST_ENTRY Collected,
    _In_ NTSTATUS Status)
{
    while (!IsListEmpty(Collected))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(Collected),
                                     IRP,
                                     Tail.Overlay.ListEntry);

        FsRtlNotifyCompleteIrp(Irp, Status, 0);
    }
}

static
VOID
FsRtlpQueueCompletion(
    _Inout_ PLIST_ENTRY Collected,
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;

    InsertTailList(Collected, &Irp->Tail.Overlay.ListEntry);
}

static
VOID
FsRtlpCompleteQueued(
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

static
VOID
NTAPI
FsRtlCancelNotify(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PNOTIFY_CHANGE Notify = (PNOTIFY_CHANGE)
        Irp->Tail.Overlay.DriverContext[0];
    PREAL_NOTIFY_SYNC Sync;
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    if (Notify == NULL)
    {
        FsRtlNotifyCompleteIrp(Irp, STATUS_CANCELLED, 0);
        return;
    }

    Sync = Notify->NotifySync;

    FsRtlNotifyAcquireFastMutex(Sync);

    for (Entry = Notify->NotifyIrps.Flink;
         Entry != &Notify->NotifyIrps;
         Entry = Entry->Flink)
    {
        if (Entry == &Irp->Tail.Overlay.ListEntry)
        {
            RemoveEntryList(Entry);
            Found = TRUE;
            break;
        }
    }

    FsRtlNotifyReleaseFastMutex(Sync);

    if (Found) FsRtlNotifyCompleteIrp(Irp, STATUS_CANCELLED, 0);
}

static
BOOLEAN
FsRtlNotifySetCancelRoutine(
    _In_ PIRP Irp,
    _In_ PNOTIFY_CHANGE Notify)
{
    IoMarkIrpPending(Irp);
    Irp->Tail.Overlay.DriverContext[0] = Notify;
    InsertTailList(&Notify->NotifyIrps, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, FsRtlCancelNotify);

    if (Irp->Cancel && (IoSetCancelRoutine(Irp, NULL) != NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        return FALSE;
    }

    return TRUE;
}

static
VOID
FsRtlpReferenceNotify(
    _Inout_ PNOTIFY_CHANGE Notify)
{
    Notify->ReferenceCount++;
}

static
VOID
FsRtlpFreeNotifyBuffer(
    _Inout_ PNOTIFY_CHANGE Notify)
{
    if (Notify->AllocatedBuffer != NULL)
    {
        ExFreePoolWithTag(Notify->AllocatedBuffer, FSRTL_NOTIFY_TAG);
        Notify->AllocatedBuffer = NULL;
    }

    Notify->Buffer = NULL;
    Notify->ThisBufferLength = 0;
    Notify->DataLength = 0;
    Notify->LastEntry = 0;
}

static
VOID
FsRtlpFreeNotify(
    _Inout_ PNOTIFY_CHANGE Notify)
{
    FsRtlpFreeNotifyBuffer(Notify);

    if (Notify->SubjectContext != NULL)
    {
        SeReleaseSubjectContext(Notify->SubjectContext);
        ExFreePool(Notify->SubjectContext);
        Notify->SubjectContext = NULL;
    }

    if (Notify->FullDirectoryName != NULL)
    {
        ExFreePoolWithTag(Notify->FullDirectoryName, FSRTL_NOTIFY_TAG);
        Notify->FullDirectoryName = NULL;
    }

    ExFreePoolWithTag(Notify, FSRTL_NOTIFY_TAG);
}

static
BOOLEAN
FsRtlpDereferenceNotify(
    _Inout_ PNOTIFY_CHANGE Notify)
{
    if (Notify->ReferenceCount > 1)
    {
        Notify->ReferenceCount--;
        return FALSE;
    }

    Notify->ReferenceCount = 0;

    return TRUE;
}

static
PSTRING
FsRtlpCopyName(
    _In_ PSTRING Name)
{
    PSTRING Copy = ExAllocatePoolWithTag(PagedPool,
                                         sizeof(STRING) + Name->Length,
                                         FSRTL_NOTIFY_TAG);

    if (Copy == NULL) return NULL;

    Copy->Length = Name->Length;
    Copy->MaximumLength = Name->Length;
    Copy->Buffer = (PCHAR)(Copy + 1);

    if (Name->Length != 0)
    {
        RtlCopyMemory(Copy->Buffer, Name->Buffer, Name->Length);
    }

    return Copy;
}

static
ULONG
FsRtlpEntrySize(
    _In_ ULONG NameBytes)
{
    ULONG Size = FIELD_OFFSET(FILE_NOTIFY_INFORMATION, FileName) + NameBytes;

    return ALIGN_UP_BY(Size, sizeof(ULONG));
}

static
BOOLEAN
FsRtlNotifyUpdateBuffer(
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _Inout_ PULONG DataLength,
    _Inout_ PULONG LastEntry,
    _In_ ULONG Action,
    _In_reads_bytes_(NameBytes) PVOID Name,
    _In_ ULONG NameBytes,
    _In_reads_bytes_opt_(StreamBytes) PVOID StreamName,
    _In_ ULONG StreamBytes)
{
    PFILE_NOTIFY_INFORMATION Information;
    ULONG Separator = (StreamBytes != 0) ? sizeof(WCHAR) : 0;
    ULONG TotalName = NameBytes + Separator + StreamBytes;
    ULONG Size = FsRtlpEntrySize(TotalName);
    PUCHAR Target;

    if (*DataLength + Size > BufferLength) return FALSE;

    if (*DataLength != 0)
    {
        Information = (PFILE_NOTIFY_INFORMATION)((PUCHAR)Buffer + *LastEntry);
        Information->NextEntryOffset = *DataLength - *LastEntry;
    }

    *LastEntry = *DataLength;

    Information = (PFILE_NOTIFY_INFORMATION)((PUCHAR)Buffer + *DataLength);
    Information->NextEntryOffset = 0;
    Information->Action = Action;
    Information->FileNameLength = TotalName;

    Target = (PUCHAR)Information->FileName;
    RtlCopyMemory(Target, Name, NameBytes);
    Target += NameBytes;

    if (Separator != 0)
    {
        *(PWCHAR)Target = L':';
        Target += sizeof(WCHAR);
        RtlCopyMemory(Target, StreamName, StreamBytes);
    }

    *DataLength += Size;

    return TRUE;
}

/*
 * Builds the name a watcher should be told about when the file system hands
 * over the parent path with its short names already expanded: the long parent
 * path, a separator, and the name of whatever changed.
 */
static
VOID
FsRtlpJoinNormalizedName(
    _In_ PSTRING ParentName,
    _In_ PSTRING FullTargetName,
    _In_ USHORT TargetNameOffset,
    _Out_ PSTRING Joined)
{
    USHORT TailLength;
    USHORT Length;
    PCHAR Buffer;

    Joined->Buffer = NULL;
    Joined->Length = 0;
    Joined->MaximumLength = 0;

    if (FullTargetName->Length < TargetNameOffset) return;

    TailLength = (USHORT)(FullTargetName->Length - TargetNameOffset);
    if (TailLength == 0) return;

    Length = (USHORT)(ParentName->Length + sizeof(WCHAR) + TailLength);

    Buffer = ExAllocatePoolWithTag(PagedPool, Length, FSRTL_NOTIFY_TAG);
    if (Buffer == NULL) return;

    RtlCopyMemory(Buffer, ParentName->Buffer, ParentName->Length);
    *(PWCHAR)&Buffer[ParentName->Length] = L'\\';
    RtlCopyMemory(&Buffer[ParentName->Length + sizeof(WCHAR)],
                  &FullTargetName->Buffer[TargetNameOffset],
                  TailLength);

    Joined->Buffer = Buffer;
    Joined->Length = Length;
    Joined->MaximumLength = Length;
}

static
BOOLEAN
FsRtlpSameName(
    _In_reads_bytes_(Bytes) PCHAR Left,
    _In_reads_bytes_(Bytes) PCHAR Right,
    _In_ ULONG Bytes)
{
    PCWCH One = (PCWCH)Left;
    PCWCH Two = (PCWCH)Right;
    ULONG Count = Bytes / sizeof(WCHAR);
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        if (RtlUpcaseUnicodeChar(One[i]) != RtlUpcaseUnicodeChar(Two[i]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
BOOLEAN
FsRtlpNameIsUnder(
    _In_ PSTRING Directory,
    _In_ PSTRING Target,
    _In_ USHORT TargetNameOffset,
    _In_ BOOLEAN WatchTree,
    _Out_ PUSHORT RelativeOffset)
{
    USHORT ParentLength;

    if (Target->Length < TargetNameOffset) return FALSE;

    ParentLength = TargetNameOffset;

    if ((ParentLength > sizeof(WCHAR)) &&
        (*(PWCHAR)&Target->Buffer[ParentLength - sizeof(WCHAR)] == L'\\'))
    {
        ParentLength -= sizeof(WCHAR);
    }

    if (Directory->Length > ParentLength) return FALSE;

    if (!FsRtlpSameName(Directory->Buffer, Target->Buffer, Directory->Length))
    {
        return FALSE;
    }

    if (Directory->Length == ParentLength)
    {
        *RelativeOffset = TargetNameOffset;
        return TRUE;
    }

    if (!WatchTree) return FALSE;

    if (*(PWCHAR)&Target->Buffer[Directory->Length] != L'\\') return FALSE;

    *RelativeOffset = (USHORT)(Directory->Length + sizeof(WCHAR));

    return TRUE;
}

static
VOID
FsRtlpReportToNotify(
    _Inout_ PNOTIFY_CHANGE Notify,
    _In_ PSTRING FullTargetName,
    _In_ USHORT RelativeOffset,
    _In_opt_ PSTRING StreamName,
    _In_ ULONG Action,
    _Inout_ PLIST_ENTRY Completed)
{
    PVOID Name = &FullTargetName->Buffer[RelativeOffset];
    ULONG NameBytes = FullTargetName->Length - RelativeOffset;
    PVOID Stream = NULL;
    ULONG StreamBytes = 0;

    if (StreamName != NULL)
    {
        Stream = StreamName->Buffer;
        StreamBytes = StreamName->Length;
    }

    if (!IsListEmpty(&Notify->NotifyIrps))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Notify->NotifyIrps),
                                     IRP,
                                     Tail.Overlay.ListEntry);
        ULONG Length = 0;
        PVOID Buffer;
        ULONG DataLength = 0;
        ULONG LastEntry = 0;

        IoSetCancelRoutine(Irp, NULL);

        Buffer = FsRtlpIrpBuffer(Irp, &Length);

        if ((Buffer != NULL) &&
            FsRtlNotifyUpdateBuffer(Buffer,
                                    Length,
                                    &DataLength,
                                    &LastEntry,
                                    Action,
                                    Name,
                                    NameBytes,
                                    Stream,
                                    StreamBytes))
        {
            FsRtlpQueueCompletion(Completed, Irp, STATUS_SUCCESS, DataLength);
        }
        else
        {
            FsRtlpQueueCompletion(Completed,
                                  Irp,
                                  STATUS_NOTIFY_ENUM_DIR,
                                  0);
        }

        return;
    }

    if (FlagOn(Notify->Flags, NOTIFY_IMMEDIATELY)) return;

    if (Notify->Buffer == NULL)
    {
        if (Notify->BufferLength == 0)
        {
            SetFlag(Notify->Flags, NOTIFY_IMMEDIATELY);
            return;
        }

        Notify->AllocatedBuffer = ExAllocatePoolWithTag(PagedPool,
                                                        Notify->BufferLength,
                                                        FSRTL_NOTIFY_TAG);

        if (Notify->AllocatedBuffer == NULL)
        {
            SetFlag(Notify->Flags, NOTIFY_IMMEDIATELY);
            return;
        }

        Notify->Buffer = Notify->AllocatedBuffer;
        Notify->ThisBufferLength = Notify->BufferLength;
        Notify->DataLength = 0;
        Notify->LastEntry = 0;
    }

    if (!FsRtlNotifyUpdateBuffer(Notify->Buffer,
                                 Notify->ThisBufferLength,
                                 &Notify->DataLength,
                                 &Notify->LastEntry,
                                 Action,
                                 Name,
                                 NameBytes,
                                 Stream,
                                 StreamBytes))
    {
        FsRtlpFreeNotifyBuffer(Notify);
        SetFlag(Notify->Flags, NOTIFY_IMMEDIATELY);
    }
}

static
VOID
FsRtlNotifyCleanupOneEntry(
    _Inout_ PNOTIFY_CHANGE Notify,
    _Inout_ PLIST_ENTRY Collected)
{
    while (!IsListEmpty(&Notify->NotifyIrps))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Notify->NotifyIrps),
                                     IRP,
                                     Tail.Overlay.ListEntry);

        IoSetCancelRoutine(Irp, NULL);
        InsertTailList(Collected, &Irp->Tail.Overlay.ListEntry);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlNotifyInitializeSync(
    _In_ PNOTIFY_SYNC *NotifySync)
{
    PREAL_NOTIFY_SYNC Sync;

    PAGED_CODE();

    *NotifySync = NULL;

    Sync = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(REAL_NOTIFY_SYNC),
                                 FSRTL_NOTIFY_TAG);

    if (Sync == NULL) ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

    ExInitializeFastMutex(&Sync->FastMutex);
    Sync->OwningThread = 0;
    Sync->OwnerCount = 0;

    *NotifySync = (PNOTIFY_SYNC)Sync;
}

VOID
NTAPI
FsRtlNotifyUninitializeSync(
    _In_ PNOTIFY_SYNC *NotifySync)
{
    PAGED_CODE();

    if (*NotifySync == NULL) return;

    ExFreePoolWithTag(*NotifySync, FSRTL_NOTIFY_TAG);
    *NotifySync = NULL;
}

BOOLEAN
NTAPI
FsRtlIsNotifyOnList(
    _In_ PLIST_ENTRY NotifyList,
    _In_ PVOID FsContext)
{
    PAGED_CODE();

    return (BOOLEAN)(FsRtlpFindNotify(NotifyList, FsContext) != NULL);
}

VOID
NTAPI
FsRtlCheckNotifyForDelete(
    _In_ PLIST_ENTRY NotifyList,
    _In_ PVOID FsContext)
{
    LIST_ENTRY Collected;
    PNOTIFY_CHANGE Notify;

    PAGED_CODE();

    InitializeListHead(&Collected);

    Notify = FsRtlpFindNotify(NotifyList, FsContext);

    if (Notify != NULL)
    {
        FsRtlNotifyCleanupOneEntry(Notify, &Collected);
    }

    FsRtlNotifyCompleteIrpList(&Collected, STATUS_DELETE_PENDING);
}

VOID
NTAPI
FsRtlNotifyFilterChangeDirectory(
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PLIST_ENTRY NotifyList,
    _In_ PVOID FsContext,
    _In_ PSTRING FullDirectoryName,
    _In_ BOOLEAN WatchTree,
    _In_ BOOLEAN IgnoreBuffer,
    _In_ ULONG CompletionFilter,
    _In_opt_ PIRP NotifyIrp,
    _In_opt_ PCHECK_FOR_TRAVERSE_ACCESS TraverseCallback,
    _In_opt_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_opt_ PFILTER_REPORT_CHANGE FilterCallback)
{
    PREAL_NOTIFY_SYNC Sync = FsRtlpSyncOf(NotifySync);
    PIO_STACK_LOCATION Stack;
    PNOTIFY_CHANGE Notify;
    LIST_ENTRY Collected;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Information = 0;
    PIRP Complete = NULL;

    PAGED_CODE();

    InitializeListHead(&Collected);

    FsRtlNotifyAcquireFastMutex(Sync);

    if (NotifyIrp == NULL)
    {
        Notify = FsRtlpFindNotify(NotifyList, FsContext);

        if (Notify != NULL)
        {
            FsRtlNotifyCleanupOneEntry(Notify, &Collected);
        }

        FsRtlNotifyReleaseFastMutex(Sync);

        if (SubjectContext != NULL)
        {
            SeReleaseSubjectContext(SubjectContext);
            ExFreePool(SubjectContext);
        }

        FsRtlNotifyCompleteIrpList(&Collected, STATUS_DELETE_PENDING);

        return;
    }

    Stack = IoGetCurrentIrpStackLocation(NotifyIrp);
    IoMarkIrpPending(NotifyIrp);

    if (FlagOn(Stack->FileObject->Flags, FO_CLEANUP_COMPLETE))
    {
        FsRtlNotifyReleaseFastMutex(Sync);

        if (SubjectContext != NULL)
        {
            SeReleaseSubjectContext(SubjectContext);
            ExFreePool(SubjectContext);
        }

        FsRtlNotifyCompleteIrp(NotifyIrp, STATUS_NOTIFY_CLEANUP, 0);

        return;
    }

    Notify = FsRtlpFindNotify(NotifyList, FsContext);

    if (Notify == NULL)
    {
        Notify = ExAllocatePoolWithTag(PagedPool,
                                       sizeof(NOTIFY_CHANGE),
                                       FSRTL_NOTIFY_TAG);

        if (Notify == NULL)
        {
            FsRtlNotifyReleaseFastMutex(Sync);

            if (SubjectContext != NULL)
            {
                SeReleaseSubjectContext(SubjectContext);
                ExFreePool(SubjectContext);
            }

            FsRtlNotifyCompleteIrp(NotifyIrp,
                                   STATUS_INSUFFICIENT_RESOURCES,
                                   0);

            return;
        }

        RtlZeroMemory(Notify, sizeof(*Notify));

        Notify->NotifySync = Sync;
        Notify->FsContext = FsContext;
        Notify->StreamID = Stack->FileObject->FsContext;
        Notify->TraverseCallback = TraverseCallback;
        Notify->SubjectContext = SubjectContext;
        Notify->FilterCallback = FilterCallback;
        Notify->CompletionFilter = CompletionFilter;
        Notify->OwningProcess = PsGetCurrentProcess();
        Notify->ReferenceCount = 1;

        /* TODO: an ANSI-name file system is not told apart here, so the reported names are taken as Unicode */
        Notify->CharacterSize = sizeof(WCHAR);

        Notify->FullDirectoryName = FsRtlpCopyName(FullDirectoryName);

        if (Notify->FullDirectoryName == NULL)
        {
            FsRtlNotifyReleaseFastMutex(Sync);
            Notify->SubjectContext = NULL;
            ExFreePoolWithTag(Notify, FSRTL_NOTIFY_TAG);

            if (SubjectContext != NULL)
            {
                SeReleaseSubjectContext(SubjectContext);
                ExFreePool(SubjectContext);
            }

            FsRtlNotifyCompleteIrp(NotifyIrp,
                                   STATUS_INSUFFICIENT_RESOURCES,
                                   0);

            return;
        }

        if (WatchTree) SetFlag(Notify->Flags, WATCH_TREE);
        if (FullDirectoryName->Length == 0) SetFlag(Notify->Flags, WATCH_ROOT);

        InitializeListHead(&Notify->NotifyIrps);
        InsertTailList(NotifyList, &Notify->NotifyList);
    }
    else
    {
        if (SubjectContext != NULL)
        {
            SeReleaseSubjectContext(SubjectContext);
            ExFreePool(SubjectContext);
        }
    }

    if (!IgnoreBuffer && (Notify->BufferLength == 0))
    {
        Notify->BufferLength = Stack->Parameters.NotifyDirectory.Length;
    }

    if (FlagOn(Notify->Flags, NOTIFY_IMMEDIATELY))
    {
        ClearFlag(Notify->Flags, NOTIFY_IMMEDIATELY);
        FsRtlpFreeNotifyBuffer(Notify);

        Complete = NotifyIrp;
        Status = STATUS_NOTIFY_ENUM_DIR;
        Information = 0;
    }
    else if (Notify->DataLength != 0)
    {
        ULONG Length = 0;
        PVOID Buffer = FsRtlpIrpBuffer(NotifyIrp, &Length);

        Complete = NotifyIrp;

        if ((Buffer != NULL) && (Length >= Notify->DataLength))
        {
            RtlCopyMemory(Buffer, Notify->Buffer, Notify->DataLength);
            Status = STATUS_SUCCESS;
            Information = Notify->DataLength;
        }
        else
        {
            Status = STATUS_NOTIFY_ENUM_DIR;
            Information = 0;
        }

        FsRtlpFreeNotifyBuffer(Notify);
    }
    else if (FsRtlNotifySetCancelRoutine(NotifyIrp, Notify))
    {
        NOTHING;
    }
    else
    {
        Complete = NotifyIrp;
        Status = STATUS_CANCELLED;
        Information = 0;
    }

    FsRtlNotifyReleaseFastMutex(Sync);

    FsRtlNotifyCompleteIrpList(&Collected, STATUS_DELETE_PENDING);

    if (Complete != NULL)
    {
        FsRtlNotifyCompleteIrp(Complete, Status, Information);
    }
}

VOID
NTAPI
FsRtlNotifyFullChangeDirectory(
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PLIST_ENTRY NotifyList,
    _In_ PVOID FsContext,
    _In_ PSTRING FullDirectoryName,
    _In_ BOOLEAN WatchTree,
    _In_ BOOLEAN IgnoreBuffer,
    _In_ ULONG CompletionFilter,
    _In_opt_ PIRP NotifyIrp,
    _In_opt_ PCHECK_FOR_TRAVERSE_ACCESS TraverseCallback,
    _In_opt_ PSECURITY_SUBJECT_CONTEXT SubjectContext)
{
    FsRtlNotifyFilterChangeDirectory(NotifySync,
                                     NotifyList,
                                     FsContext,
                                     FullDirectoryName,
                                     WatchTree,
                                     IgnoreBuffer,
                                     CompletionFilter,
                                     NotifyIrp,
                                     TraverseCallback,
                                     SubjectContext,
                                     NULL);
}

VOID
NTAPI
FsRtlNotifyChangeDirectory(
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PVOID FsContext,
    _In_ PSTRING FullDirectoryName,
    _In_ PLIST_ENTRY NotifyList,
    _In_ BOOLEAN WatchTree,
    _In_ ULONG CompletionFilter,
    _In_opt_ PIRP NotifyIrp)
{
    FsRtlNotifyFilterChangeDirectory(NotifySync,
                                     NotifyList,
                                     FsContext,
                                     FullDirectoryName,
                                     WatchTree,
                                     FALSE,
                                     CompletionFilter,
                                     NotifyIrp,
                                     NULL,
                                     NULL,
                                     NULL);
}

VOID
NTAPI
FsRtlNotifyFilterReportChange(
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PLIST_ENTRY NotifyList,
    _In_ PSTRING FullTargetName,
    _In_ USHORT TargetNameOffset,
    _In_opt_ PSTRING StreamName,
    _In_opt_ PSTRING NormalizedParentName,
    _In_ ULONG FilterMatch,
    _In_ ULONG Action,
    _In_opt_ PVOID TargetContext,
    _In_opt_ PVOID FilterContext)
{
    PREAL_NOTIFY_SYNC Sync = FsRtlpSyncOf(NotifySync);
    PNOTIFY_CHANGE StackSnapshot[FSRTL_NOTIFY_INLINE_WATCHERS];
    PNOTIFY_CHANGE *Snapshot = StackSnapshot;
    PSTRING Reported = FullTargetName;
    STRING Joined;
    LIST_ENTRY Completed;
    LIST_ENTRY Doomed;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Taken = 0;
    ULONG i;

    PAGED_CODE();

    InitializeListHead(&Completed);
    InitializeListHead(&Doomed);

    Joined.Buffer = NULL;
    Joined.Length = 0;
    Joined.MaximumLength = 0;

    if (NormalizedParentName != NULL)
    {
        FsRtlpJoinNormalizedName(NormalizedParentName,
                                 FullTargetName,
                                 TargetNameOffset,
                                 &Joined);

        if (Joined.Buffer != NULL)
        {
            Reported = &Joined;
            TargetNameOffset =
                (USHORT)(NormalizedParentName->Length + sizeof(WCHAR));
        }
    }

    FsRtlNotifyAcquireFastMutex(Sync);

    for (Entry = NotifyList->Flink; Entry != NotifyList; Entry = Entry->Flink)
    {
        Count++;
    }

    if (Count > FSRTL_NOTIFY_INLINE_WATCHERS)
    {
        Snapshot = ExAllocatePoolWithTag(PagedPool,
                                         Count * sizeof(PNOTIFY_CHANGE),
                                         FSRTL_NOTIFY_TAG);

        if (Snapshot == NULL)
        {
            /* FIXME: with the pool empty only the first watchers of a long list hear about the change */
            Snapshot = StackSnapshot;
            Count = FSRTL_NOTIFY_INLINE_WATCHERS;
        }
    }

    for (Entry = NotifyList->Flink;
         (Entry != NotifyList) && (Taken < Count);
         Entry = Entry->Flink)
    {
        PNOTIFY_CHANGE Notify = CONTAINING_RECORD(Entry,
                                                  NOTIFY_CHANGE,
                                                  NotifyList);

        FsRtlpReferenceNotify(Notify);
        Snapshot[Taken++] = Notify;
    }

    for (i = 0; i < Taken; i++)
    {
        PNOTIFY_CHANGE Notify = Snapshot[i];
        BOOLEAN WatchTree = FlagOn(Notify->Flags, WATCH_TREE) ? TRUE : FALSE;
        USHORT RelativeOffset = 0;

        if (FlagOn(Notify->Flags, CLEANUP_IN_PROCESS | DELETE_IN_PROCESS))
        {
            continue;
        }

        if (!FlagOn(Notify->CompletionFilter, FilterMatch)) continue;

        if (!FsRtlpNameIsUnder(Notify->FullDirectoryName,
                               Reported,
                               TargetNameOffset,
                               WatchTree,
                               &RelativeOffset))
        {
            continue;
        }

        if ((RelativeOffset != TargetNameOffset) &&
            (Notify->TraverseCallback != NULL) &&
            !Notify->TraverseCallback(Notify->FsContext,
                                      TargetContext,
                                      Notify->SubjectContext))
        {
            continue;
        }

        if ((Notify->FilterCallback != NULL) &&
            !Notify->FilterCallback(Notify->FsContext, FilterContext))
        {
            continue;
        }

        FsRtlpReportToNotify(Notify,
                             Reported,
                             RelativeOffset,
                             StreamName,
                             Action,
                             &Completed);
    }

    for (i = 0; i < Taken; i++)
    {
        if (FsRtlpDereferenceNotify(Snapshot[i]))
        {
            InsertTailList(&Doomed, &Snapshot[i]->NotifyList);
        }
    }

    FsRtlNotifyReleaseFastMutex(Sync);

    if (Snapshot != StackSnapshot)
    {
        ExFreePoolWithTag(Snapshot, FSRTL_NOTIFY_TAG);
    }

    if (Joined.Buffer != NULL)
    {
        ExFreePoolWithTag(Joined.Buffer, FSRTL_NOTIFY_TAG);
    }

    while (!IsListEmpty(&Doomed))
    {
        PNOTIFY_CHANGE Notify = CONTAINING_RECORD(RemoveHeadList(&Doomed),
                                                  NOTIFY_CHANGE,
                                                  NotifyList);

        FsRtlpFreeNotify(Notify);
    }

    FsRtlpCompleteQueued(&Completed);
}

VOID
NTAPI
FsRtlNotifyFullReportChange(
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PLIST_ENTRY NotifyList,
    _In_ PSTRING FullTargetName,
    _In_ USHORT TargetNameOffset,
    _In_opt_ PSTRING StreamName,
    _In_opt_ PSTRING NormalizedParentName,
    _In_ ULONG FilterMatch,
    _In_ ULONG Action,
    _In_opt_ PVOID TargetContext)
{
    FsRtlNotifyFilterReportChange(NotifySync,
                                  NotifyList,
                                  FullTargetName,
                                  TargetNameOffset,
                                  StreamName,
                                  NormalizedParentName,
                                  FilterMatch,
                                  Action,
                                  TargetContext,
                                  NULL);
}

VOID
NTAPI
FsRtlNotifyReportChange(
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PLIST_ENTRY NotifyList,
    _In_ PSTRING FullTargetName,
    _In_ PUSHORT FileNamePartLength,
    _In_ ULONG FilterMatch)
{
    USHORT TargetNameOffset;

    PAGED_CODE();

    if (FullTargetName->Length < *FileNamePartLength) return;

    TargetNameOffset = (USHORT)(FullTargetName->Length - *FileNamePartLength);

    FsRtlNotifyFilterReportChange(NotifySync,
                                  NotifyList,
                                  FullTargetName,
                                  TargetNameOffset,
                                  NULL,
                                  NULL,
                                  FilterMatch,
                                  FILE_ACTION_MODIFIED,
                                  NULL,
                                  NULL);
}

VOID
NTAPI
FsRtlNotifyCleanup(
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PLIST_ENTRY NotifyList,
    _In_ PVOID FsContext)
{
    PREAL_NOTIFY_SYNC Sync = FsRtlpSyncOf(NotifySync);
    PNOTIFY_CHANGE Notify;
    LIST_ENTRY Collected;

    PAGED_CODE();

    InitializeListHead(&Collected);

    FsRtlNotifyAcquireFastMutex(Sync);

    Notify = FsRtlpFindNotify(NotifyList, FsContext);

    if (Notify != NULL)
    {
        FsRtlNotifyCleanupOneEntry(Notify, &Collected);
        RemoveEntryList(&Notify->NotifyList);
        SetFlag(Notify->Flags, CLEANUP_IN_PROCESS);

        if (!FsRtlpDereferenceNotify(Notify)) Notify = NULL;
    }

    FsRtlNotifyReleaseFastMutex(Sync);

    FsRtlNotifyCompleteIrpList(&Collected, STATUS_NOTIFY_CLEANUP);

    if (Notify != NULL) FsRtlpFreeNotify(Notify);
}

/* EOF */
