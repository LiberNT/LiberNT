/*
 * PROJECT:     LiberNT NTFS file system driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Transactional NTFS: transacted namespace, data isolation and KTM enlistment
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

#define NTFS_TXF_STAGING_ROOT L"\\$Extend\\$TxfStaging"
#define NTFS_TXF_COPY_CHUNK (64 * 1024)
#define NTFS_TXF_RECORD_MASK 0x0000FFFFFFFFFFFFULL
#define NTFS_TXF_WRITE_ACCESS (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | \
                               FILE_WRITE_EA | WRITE_DAC | WRITE_OWNER | MAXIMUM_ALLOWED)
#define NTFS_TXF_MODIFY_ACCESS (NTFS_TXF_WRITE_ACCESS | DELETE)
#define TAG_NTFS_TXF 'fxTN'

typedef enum _NTFS_TXF_KIND
{
    NtfsTxfCreated,
    NtfsTxfShadow,
    NtfsTxfDeleted,
    NtfsTxfRenamed,
    NtfsTxfLinked,
    NtfsTxfLocked
} NTFS_TXF_KIND;

typedef enum _NTFS_TXF_VIEW
{
    NtfsTxfViewCommitted,
    NtfsTxfViewMapped,
    NtfsTxfViewHidden,
    NtfsTxfViewHiddenParent
} NTFS_TXF_VIEW;

typedef struct _NTFS_TXF_CHANGE
{
    LIST_ENTRY Link;
    NTFS_TXF_KIND Kind;
    BOOLEAN Directory;
    ULONGLONG FileReference;
    UNICODE_STRING Name;
    UNICODE_STRING Source;
    UNICODE_STRING Physical;
} NTFS_TXF_CHANGE, *PNTFS_TXF_CHANGE;

typedef struct _NTFS_TXF_READER
{
    LIST_ENTRY Link;
    ULONGLONG FileReference;
    ULONG Count;
} NTFS_TXF_READER, *PNTFS_TXF_READER;

typedef struct _NTFS_TXF_TRANSACTION
{
    LIST_ENTRY Link;
    LONG References;
    PVolumeContextBlock VolCB;
    PKTRANSACTION Transaction;
    HANDLE EnlistmentHandle;
    ULONG Id;
    LONG NextStage;
    LIST_ENTRY Changes;
    LIST_ENTRY Readers;
    PETHREAD ActiveThread;
    BOOLEAN Ending;
} NTFS_TXF_TRANSACTION, *PNTFS_TXF_TRANSACTION;

typedef struct _NTFS_TXF_INFO
{
    BOOLEAN Directory;
    ULONGLONG FileReference;
} NTFS_TXF_INFO, *PNTFS_TXF_INFO;

static PKRESOURCEMANAGER NtfsTxfResourceManager;

static const UNICODE_STRING NtfsTxfStagingRoot = RTL_CONSTANT_STRING(NTFS_TXF_STAGING_ROOT);

/* STRINGS ********************************************************************/

static
VOID
NtfsTxfFreeString(_Inout_ PUNICODE_STRING String)
{
    if (String->Buffer)
        ExFreePoolWithTag(String->Buffer, TAG_NTFS_TXF);
    String->Buffer = NULL;
    String->Length = String->MaximumLength = 0;
}

static
NTSTATUS
NtfsTxfJoin(_Out_ PUNICODE_STRING Result,
            _In_ PCUNICODE_STRING First,
            _In_opt_ PCWSTR Separator,
            _In_reads_bytes_opt_(SecondLength) PCWSTR Second,
            _In_ USHORT SecondLength)
{
    ULONG Length = (ULONG)First->Length + (Separator ? sizeof(WCHAR) : 0) + SecondLength;

    Result->Buffer = NULL;
    Result->Length = Result->MaximumLength = 0;
    if (Length > MAXUSHORT)
        return STATUS_NAME_TOO_LONG;
    Result->Buffer = ExAllocatePoolWithTag(PagedPool, max(Length, sizeof(WCHAR)), TAG_NTFS_TXF);
    if (!Result->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Result->Buffer, First->Buffer, First->Length);
    Result->Length = First->Length;
    if (Separator)
    {
        Result->Buffer[Result->Length / sizeof(WCHAR)] = *Separator;
        Result->Length += sizeof(WCHAR);
    }
    if (SecondLength)
        RtlCopyMemory((PUCHAR)Result->Buffer + Result->Length, Second, SecondLength);
    Result->Length += SecondLength;
    Result->MaximumLength = (USHORT)max(Length, sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTxfCopy(_Out_ PUNICODE_STRING Result,
            _In_ PCUNICODE_STRING Source)
{
    return NtfsTxfJoin(Result, Source, NULL, NULL, 0);
}

static
NTSTATUS
NtfsTxfChild(_Out_ PUNICODE_STRING Result,
             _In_ PCUNICODE_STRING Parent,
             _In_reads_bytes_(LeafLength) PCWSTR Leaf,
             _In_ USHORT LeafLength)
{
    static const WCHAR Backslash = L'\\';

    if (Parent->Length == sizeof(WCHAR) && Parent->Buffer[0] == L'\\')
        return NtfsTxfJoin(Result, Parent, NULL, Leaf, LeafLength);
    return NtfsTxfJoin(Result, Parent, &Backslash, Leaf, LeafLength);
}

static
BOOLEAN
NtfsTxfEqual(_In_ PCUNICODE_STRING Left,
             _In_ PCUNICODE_STRING Right)
{
    return RtlEqualUnicodeString(Left, Right, TRUE);
}

static
BOOLEAN
NtfsTxfUnder(_In_ PCUNICODE_STRING Directory,
             _In_ PCUNICODE_STRING Path)
{
    if (Directory->Length == sizeof(WCHAR) && Directory->Buffer[0] == L'\\')
        return Path->Length > sizeof(WCHAR) && Path->Buffer[0] == L'\\';
    return Path->Length > Directory->Length &&
           Path->Buffer[Directory->Length / sizeof(WCHAR)] == L'\\' &&
           RtlPrefixUnicodeString(Directory, Path, TRUE);
}

static
VOID
NtfsTxfSplit(_In_ PCUNICODE_STRING Path,
             _Out_ PUNICODE_STRING Parent,
             _Out_ PUNICODE_STRING Leaf)
{
    USHORT Index = Path->Length / sizeof(WCHAR);

    while (Index != 0 && Path->Buffer[Index - 1] != L'\\')
        Index--;
    Leaf->Buffer = Path->Buffer + Index;
    Leaf->Length = Leaf->MaximumLength = Path->Length - Index * sizeof(WCHAR);
    Parent->Buffer = Path->Buffer;
    Parent->Length = Parent->MaximumLength = (Index > 1) ? (Index - 1) * sizeof(WCHAR) : sizeof(WCHAR);
}

static
VOID
NtfsTxfSplitStream(_In_ PCUNICODE_STRING Path,
                   _Out_ PUNICODE_STRING Name,
                   _Out_ PUNICODE_STRING Stream)
{
    USHORT Index;

    for (Index = 0; Index < Path->Length / sizeof(WCHAR); Index++)
    {
        if (Path->Buffer[Index] == L':')
            break;
    }
    Name->Buffer = Path->Buffer;
    Name->Length = Name->MaximumLength = Index * sizeof(WCHAR);
    Stream->Buffer = Path->Buffer + Index;
    Stream->Length = Stream->MaximumLength = Path->Length - Index * sizeof(WCHAR);
}

static
BOOLEAN
NtfsTxfIsStaging(_In_ PCUNICODE_STRING Path)
{
    return NtfsTxfEqual(&NtfsTxfStagingRoot, Path) || NtfsTxfUnder(&NtfsTxfStagingRoot, Path);
}

/* TRANSACTION STATE **********************************************************/

static
VOID
NtfsTxfFreeChange(_In_ PNTFS_TXF_CHANGE Change)
{
    NtfsTxfFreeString(&Change->Name);
    NtfsTxfFreeString(&Change->Source);
    NtfsTxfFreeString(&Change->Physical);
    ExFreePoolWithTag(Change, TAG_NTFS_TXF);
}

static
VOID
NtfsTxfReference(_In_ PNTFS_TXF_TRANSACTION Txf)
{
    InterlockedIncrement(&Txf->References);
}

static
VOID
NtfsTxfDereference(_In_ PNTFS_TXF_TRANSACTION Txf)
{
    if (InterlockedDecrement(&Txf->References) != 0)
        return;

    while (!IsListEmpty(&Txf->Changes))
        NtfsTxfFreeChange(CONTAINING_RECORD(RemoveHeadList(&Txf->Changes), NTFS_TXF_CHANGE, Link));
    while (!IsListEmpty(&Txf->Readers))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Txf->Readers), NTFS_TXF_READER, Link), TAG_NTFS_TXF);
    ObDereferenceObject(Txf->Transaction);
    ExFreePoolWithTag(Txf, TAG_NTFS_TXF);
}

static
BOOLEAN
NtfsTxfSkip(_In_ PNTFS_TXF_TRANSACTION Txf,
            _In_opt_ PNTFS_TXF_TRANSACTION Exclude)
{
    return Txf == Exclude || Txf->ActiveThread == PsGetCurrentThread();
}

static
NTFS_TXF_VIEW
NtfsTxfResolveLocked(_In_ PNTFS_TXF_TRANSACTION Txf,
                     _In_ PCUNICODE_STRING Name,
                     _Out_ PNTFS_TXF_CHANGE* Match)
{
    PLIST_ENTRY Entry;

    *Match = NULL;
    for (Entry = Txf->Changes.Blink; Entry != &Txf->Changes; Entry = Entry->Blink)
    {
        PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);

        switch (Change->Kind)
        {
            case NtfsTxfCreated:
            case NtfsTxfShadow:
            case NtfsTxfRenamed:
            case NtfsTxfLinked:
                if (NtfsTxfEqual(&Change->Name, Name) ||
                    (Change->Directory && NtfsTxfUnder(&Change->Name, Name)))
                {
                    *Match = Change;
                    return NtfsTxfViewMapped;
                }
                if (Change->Kind == NtfsTxfRenamed)
                {
                    if (NtfsTxfEqual(&Change->Source, Name))
                        return NtfsTxfViewHidden;
                    if (Change->Directory && NtfsTxfUnder(&Change->Source, Name))
                        return NtfsTxfViewHiddenParent;
                }
                break;

            case NtfsTxfDeleted:
                if (NtfsTxfEqual(&Change->Name, Name))
                    return NtfsTxfViewHidden;
                if (Change->Directory && NtfsTxfUnder(&Change->Name, Name))
                    return NtfsTxfViewHiddenParent;
                break;

            case NtfsTxfLocked:
                break;
        }
    }
    return NtfsTxfViewCommitted;
}

static
NTSTATUS
NtfsTxfMapped(_In_ PNTFS_TXF_CHANGE Change,
              _In_ PCUNICODE_STRING Name,
              _Out_ PUNICODE_STRING Physical)
{
    return NtfsTxfJoin(Physical,
                       &Change->Physical,
                       NULL,
                       Name->Buffer + Change->Name.Length / sizeof(WCHAR),
                       Name->Length - Change->Name.Length);
}

static
NTSTATUS
NtfsTxfResolve(_In_ PNTFS_TXF_TRANSACTION Txf,
               _In_ PCUNICODE_STRING Name,
               _Out_ NTFS_TXF_VIEW* View,
               _Out_ PUNICODE_STRING Physical)
{
    PNTFS_TXF_CHANGE Match;
    NTSTATUS Status = STATUS_SUCCESS;

    Physical->Buffer = NULL;
    Physical->Length = Physical->MaximumLength = 0;
    ExAcquireFastMutex(&Txf->VolCB->TxfMutex);
    *View = NtfsTxfResolveLocked(Txf, Name, &Match);
    if (*View == NtfsTxfViewMapped)
        Status = NtfsTxfMapped(Match, Name, Physical);
    else if (*View == NtfsTxfViewCommitted)
        Status = NtfsTxfCopy(Physical, Name);
    ExReleaseFastMutex(&Txf->VolCB->TxfMutex);
    return Status;
}

static
BOOLEAN
NtfsTxfIsReservedLocked(_In_ PVolumeContextBlock VolCB,
                        _In_opt_ PNTFS_TXF_TRANSACTION Exclude,
                        _In_ PCUNICODE_STRING Name)
{
    PLIST_ENTRY TxEntry, Entry;

    for (TxEntry = VolCB->TxfTransactions.Flink; TxEntry != &VolCB->TxfTransactions; TxEntry = TxEntry->Flink)
    {
        PNTFS_TXF_TRANSACTION Txf = CONTAINING_RECORD(TxEntry, NTFS_TXF_TRANSACTION, Link);

        if (NtfsTxfSkip(Txf, Exclude))
            continue;
        for (Entry = Txf->Changes.Flink; Entry != &Txf->Changes; Entry = Entry->Flink)
        {
            PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);

            switch (Change->Kind)
            {
                case NtfsTxfCreated:
                case NtfsTxfRenamed:
                case NtfsTxfLinked:
                    if (NtfsTxfEqual(&Change->Name, Name))
                        return TRUE;
                    break;
                case NtfsTxfDeleted:
                    if (Change->Directory && NtfsTxfUnder(&Change->Name, Name))
                        return TRUE;
                    break;
                default:
                    break;
            }
        }
    }
    return FALSE;
}

static
BOOLEAN
NtfsTxfIsLockedLocked(_In_ PVolumeContextBlock VolCB,
                      _In_opt_ PNTFS_TXF_TRANSACTION Exclude,
                      _In_ ULONGLONG FileReference,
                      _In_ BOOLEAN Readers)
{
    PLIST_ENTRY TxEntry, Entry;

    FileReference &= NTFS_TXF_RECORD_MASK;
    for (TxEntry = VolCB->TxfTransactions.Flink; TxEntry != &VolCB->TxfTransactions; TxEntry = TxEntry->Flink)
    {
        PNTFS_TXF_TRANSACTION Txf = CONTAINING_RECORD(TxEntry, NTFS_TXF_TRANSACTION, Link);

        if (NtfsTxfSkip(Txf, Exclude))
            continue;
        for (Entry = Txf->Changes.Flink; Entry != &Txf->Changes; Entry = Entry->Flink)
        {
            PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);

            if (Change->FileReference && (Change->FileReference & NTFS_TXF_RECORD_MASK) == FileReference)
                return TRUE;
        }
        if (!Readers)
            continue;
        for (Entry = Txf->Readers.Flink; Entry != &Txf->Readers; Entry = Entry->Flink)
        {
            PNTFS_TXF_READER Reader = CONTAINING_RECORD(Entry, NTFS_TXF_READER, Link);

            if ((Reader->FileReference & NTFS_TXF_RECORD_MASK) == FileReference)
                return TRUE;
        }
    }
    return FALSE;
}

static
BOOLEAN
NtfsTxfHasLockLocked(_In_ PNTFS_TXF_TRANSACTION Txf,
                     _In_ ULONGLONG FileReference)
{
    PLIST_ENTRY Entry;

    for (Entry = Txf->Changes.Flink; Entry != &Txf->Changes; Entry = Entry->Flink)
    {
        PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);

        if (Change->FileReference &&
            (Change->FileReference & NTFS_TXF_RECORD_MASK) == (FileReference & NTFS_TXF_RECORD_MASK))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
PNTFS_TXF_CHANGE
NtfsTxfFindPhysicalLocked(_In_ PNTFS_TXF_TRANSACTION Txf,
                          _In_ NTFS_TXF_KIND Kind,
                          _In_ PCUNICODE_STRING Physical)
{
    PLIST_ENTRY Entry;

    for (Entry = Txf->Changes.Flink; Entry != &Txf->Changes; Entry = Entry->Flink)
    {
        PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);

        if (Change->Kind == Kind && NtfsTxfEqual(&Change->Physical, Physical))
            return Change;
    }
    return NULL;
}

static
NTSTATUS
NtfsTxfRecord(_In_ PNTFS_TXF_TRANSACTION Txf,
              _In_ NTFS_TXF_KIND Kind,
              _In_ PCUNICODE_STRING Name,
              _In_opt_ PCUNICODE_STRING Source,
              _In_opt_ PCUNICODE_STRING Physical,
              _In_ ULONGLONG FileReference,
              _In_ BOOLEAN Directory,
              _Out_opt_ PNTFS_TXF_CHANGE* Result)
{
    PNTFS_TXF_CHANGE Change;
    NTSTATUS Status;

    if (Result)
        *Result = NULL;
    Change = ExAllocatePoolZero(PagedPool, sizeof(*Change), TAG_NTFS_TXF);
    if (!Change)
        return STATUS_INSUFFICIENT_RESOURCES;
    Change->Kind = Kind;
    Change->Directory = Directory;
    Change->FileReference = FileReference;
    Status = NtfsTxfCopy(&Change->Name, Name);
    if (NT_SUCCESS(Status) && Source)
        Status = NtfsTxfCopy(&Change->Source, Source);
    if (NT_SUCCESS(Status) && Physical)
        Status = NtfsTxfCopy(&Change->Physical, Physical);
    if (!NT_SUCCESS(Status))
    {
        NtfsTxfFreeChange(Change);
        return Status;
    }

    ExAcquireFastMutex(&Txf->VolCB->TxfMutex);
    if (Txf->Ending)
        Status = STATUS_TRANSACTION_NOT_ACTIVE;
    else
        InsertTailList(&Txf->Changes, &Change->Link);
    ExReleaseFastMutex(&Txf->VolCB->TxfMutex);
    if (!NT_SUCCESS(Status))
    {
        NtfsTxfFreeChange(Change);
        return Status;
    }
    if (Result)
        *Result = Change;
    return STATUS_SUCCESS;
}

/* INTERNAL FILE OPERATIONS ***************************************************/

static
NTSTATUS
NtfsTxfEnsureDeviceName(_In_ PVolumeContextBlock VolCB,
                        _In_ PDEVICE_OBJECT VolumeDeviceObject)
{
    POBJECT_NAME_INFORMATION NameInfo;
    UNICODE_STRING Name;
    ULONG ReturnLength = 0;
    NTSTATUS Status;

    if (VolCB->TxfDeviceName.Buffer)
        return STATUS_SUCCESS;
    if (!VolumeDeviceObject->Vpb || !VolumeDeviceObject->Vpb->RealDevice)
        return STATUS_RM_NOT_ACTIVE;

    NameInfo = ExAllocatePoolWithTag(PagedPool, 1024, TAG_NTFS_TXF);
    if (!NameInfo)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = ObQueryNameString(VolumeDeviceObject->Vpb->RealDevice, NameInfo, 1024, &ReturnLength);
    if (NT_SUCCESS(Status) && NameInfo->Name.Length == 0)
        Status = STATUS_RM_NOT_ACTIVE;
    if (NT_SUCCESS(Status))
        Status = NtfsTxfCopy(&Name, &NameInfo->Name);
    ExFreePoolWithTag(NameInfo, TAG_NTFS_TXF);
    if (!NT_SUCCESS(Status))
        return Status;

    ExAcquireFastMutex(&VolCB->TxfMutex);
    if (!VolCB->TxfDeviceName.Buffer)
    {
        VolCB->TxfDeviceName = Name;
        Name.Buffer = NULL;
    }
    ExReleaseFastMutex(&VolCB->TxfMutex);
    NtfsTxfFreeString(&Name);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTxfFullName(_In_ PVolumeContextBlock VolCB,
                _In_ PCUNICODE_STRING Path,
                _Out_ PUNICODE_STRING FullName)
{
    return NtfsTxfJoin(FullName, &VolCB->TxfDeviceName, NULL, Path->Buffer, Path->Length);
}

static
NTSTATUS
NtfsTxfOpen(_In_ PVolumeContextBlock VolCB,
            _In_ PCUNICODE_STRING Path,
            _In_ ACCESS_MASK Access,
            _In_ ULONG Attributes,
            _In_ ULONG Disposition,
            _In_ ULONG Options,
            _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING FullName;
    NTSTATUS Status;

    *Handle = NULL;
    Status = NtfsTxfFullName(VolCB, Path, &FullName);
    if (!NT_SUCCESS(Status))
        return Status;
    InitializeObjectAttributes(&ObjectAttributes, &FullName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(Handle,
                          Access | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          Attributes,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          Disposition,
                          Options | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT,
                          NULL,
                          0);
    NtfsTxfFreeString(&FullName);
    return Status;
}

static
NTSTATUS
NtfsTxfQuery(_In_ PVolumeContextBlock VolCB,
             _In_ PCUNICODE_STRING Path,
             _Out_ PNTFS_TXF_INFO Info)
{
    FILE_STANDARD_INFORMATION Standard;
    FILE_INTERNAL_INFORMATION Internal;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;

    RtlZeroMemory(Info, sizeof(*Info));
    Status = NtfsTxfOpen(VolCB, Path, FILE_READ_ATTRIBUTES, 0, FILE_OPEN, FILE_OPEN_REPARSE_POINT, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryInformationFile(Handle, &IoStatus, &Standard, sizeof(Standard), FileStandardInformation);
    if (NT_SUCCESS(Status))
        Status = ZwQueryInformationFile(Handle, &IoStatus, &Internal, sizeof(Internal), FileInternalInformation);
    if (NT_SUCCESS(Status))
    {
        Info->Directory = Standard.Directory;
        Info->FileReference = (ULONGLONG)Internal.IndexNumber.QuadPart;
    }
    ZwClose(Handle);
    return Status;
}

static
BOOLEAN
NtfsTxfExists(_In_ PVolumeContextBlock VolCB,
              _In_ PCUNICODE_STRING Path)
{
    NTFS_TXF_INFO Info;

    return NT_SUCCESS(NtfsTxfQuery(VolCB, Path, &Info));
}

static
NTSTATUS
NtfsTxfSetName(_In_ PVolumeContextBlock VolCB,
               _In_ PCUNICODE_STRING Existing,
               _In_ PCUNICODE_STRING NewName,
               _In_ BOOLEAN Link)
{
    PFILE_RENAME_INFORMATION Rename;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING FullName;
    HANDLE Handle;
    ULONG Size;
    NTSTATUS Status;

    Status = NtfsTxfFullName(VolCB, NewName, &FullName);
    if (!NT_SUCCESS(Status))
        return Status;
    Size = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + FullName.Length;
    Rename = ExAllocatePoolZero(PagedPool, Size, TAG_NTFS_TXF);
    if (!Rename)
    {
        NtfsTxfFreeString(&FullName);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Rename->ReplaceIfExists = FALSE;
    Rename->RootDirectory = NULL;
    Rename->FileNameLength = FullName.Length;
    RtlCopyMemory(Rename->FileName, FullName.Buffer, FullName.Length);
    NtfsTxfFreeString(&FullName);

    Status = NtfsTxfOpen(VolCB, Existing, Link ? FILE_WRITE_ATTRIBUTES : DELETE, 0, FILE_OPEN,
                         FILE_OPEN_REPARSE_POINT, &Handle);
    if (NT_SUCCESS(Status))
    {
        Status = ZwSetInformationFile(Handle, &IoStatus, Rename, Size,
                                      Link ? FileLinkInformation : FileRenameInformation);
        ZwClose(Handle);
    }
    ExFreePoolWithTag(Rename, TAG_NTFS_TXF);
    return Status;
}

static
NTSTATUS
NtfsTxfDeleteOne(_In_ PVolumeContextBlock VolCB,
                 _In_ PCUNICODE_STRING Path)
{
    FILE_DISPOSITION_INFORMATION Disposition;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;

    Status = NtfsTxfOpen(VolCB, Path, DELETE | FILE_WRITE_ATTRIBUTES, 0, FILE_OPEN, FILE_OPEN_REPARSE_POINT, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Disposition.DeleteFile = TRUE;
    Status = ZwSetInformationFile(Handle, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
    if (Status == STATUS_CANNOT_DELETE)
    {
        FILE_BASIC_INFORMATION Basic;

        RtlZeroMemory(&Basic, sizeof(Basic));
        Basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (NT_SUCCESS(ZwSetInformationFile(Handle, &IoStatus, &Basic, sizeof(Basic), FileBasicInformation)))
            Status = ZwSetInformationFile(Handle, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
    }
    ZwClose(Handle);
    return Status;
}

static
NTSTATUS
NtfsTxfDeleteTree(_In_ PVolumeContextBlock VolCB,
                  _In_ PCUNICODE_STRING Path,
                  _In_ BOOLEAN ChildrenOnly)
{
    PFILE_DIRECTORY_INFORMATION Entry;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Child;
    HANDLE Handle;
    PUCHAR Buffer;
    NTSTATUS Status;
    BOOLEAN Restart = TRUE;

    Status = NtfsTxfOpen(VolCB, Path, FILE_LIST_DIRECTORY, 0, FILE_OPEN,
                         FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, &Handle);
    if (Status == STATUS_NOT_A_DIRECTORY)
        return ChildrenOnly ? STATUS_SUCCESS : NtfsTxfDeleteOne(VolCB, Path);
    if (!NT_SUCCESS(Status))
        return Status;

    Buffer = ExAllocatePoolWithTag(PagedPool, PAGE_SIZE, TAG_NTFS_TXF);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (;;)
    {
        Status = ZwQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, PAGE_SIZE,
                                      FileDirectoryInformation, TRUE, NULL, Restart);
        if (!NT_SUCCESS(Status))
            break;
        Entry = (PFILE_DIRECTORY_INFORMATION)Buffer;
        Restart = FALSE;
        if ((Entry->FileNameLength == sizeof(WCHAR) && Entry->FileName[0] == L'.') ||
            (Entry->FileNameLength == 2 * sizeof(WCHAR) && Entry->FileName[0] == L'.' && Entry->FileName[1] == L'.'))
        {
            continue;
        }
        if (!NT_SUCCESS(NtfsTxfChild(&Child, Path, Entry->FileName, (USHORT)Entry->FileNameLength)))
            break;
        NtfsTxfDeleteTree(VolCB, &Child, FALSE);
        NtfsTxfFreeString(&Child);
        Restart = TRUE;
    }
    ExFreePoolWithTag(Buffer, TAG_NTFS_TXF);
    ZwClose(Handle);
    return ChildrenOnly ? STATUS_SUCCESS : NtfsTxfDeleteOne(VolCB, Path);
}

static
NTSTATUS
NtfsTxfCopySecurity(_In_ HANDLE Source,
                    _In_ HANDLE Target)
{
    SECURITY_INFORMATION Information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR Descriptor;
    ULONG Needed = 0;
    NTSTATUS Status;

    Status = ZwQuerySecurityObject(Source, Information, NULL, 0, &Needed);
    if (Status != STATUS_BUFFER_TOO_SMALL || Needed == 0)
        return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
    Descriptor = ExAllocatePoolWithTag(PagedPool, Needed, TAG_NTFS_TXF);
    if (!Descriptor)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = ZwQuerySecurityObject(Source, Information, Descriptor, Needed, &Needed);
    if (NT_SUCCESS(Status))
        Status = ZwSetSecurityObject(Target, Information, Descriptor);
    ExFreePoolWithTag(Descriptor, TAG_NTFS_TXF);
    return Status;
}

static
NTSTATUS
NtfsTxfCopyData(_In_ PVolumeContextBlock VolCB,
                _In_ PCUNICODE_STRING Source,
                _In_ PCUNICODE_STRING Target,
                _In_ BOOLEAN IntoExisting)
{
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    FILE_BASIC_INFORMATION Basic;
    FILE_BASIC_INFORMATION Plain;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    HANDLE SourceHandle, TargetHandle;
    PVOID Buffer;
    NTSTATUS Status;

    Status = NtfsTxfOpen(VolCB, Source, FILE_READ_DATA | FILE_READ_ATTRIBUTES | READ_CONTROL, 0, FILE_OPEN,
                         FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY, &SourceHandle);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryInformationFile(SourceHandle, &IoStatus, &Basic, sizeof(Basic), FileBasicInformation);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(SourceHandle);
        return Status;
    }

    RtlZeroMemory(&Plain, sizeof(Plain));
    Plain.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    if (IntoExisting)
    {
        Status = NtfsTxfOpen(VolCB, Target, FILE_WRITE_ATTRIBUTES, 0, FILE_OPEN,
                             FILE_NON_DIRECTORY_FILE, &TargetHandle);
        if (NT_SUCCESS(Status))
        {
            Status = ZwSetInformationFile(TargetHandle, &IoStatus, &Plain, sizeof(Plain), FileBasicInformation);
            ZwClose(TargetHandle);
        }
        if (NT_SUCCESS(Status))
        {
            Status = NtfsTxfOpen(VolCB, Target, FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES, 0, FILE_OPEN,
                                 FILE_NON_DIRECTORY_FILE, &TargetHandle);
        }
    }
    else
    {
        Status = NtfsTxfOpen(VolCB, Target, FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | WRITE_DAC | WRITE_OWNER,
                             FILE_ATTRIBUTE_NORMAL, FILE_CREATE, FILE_NON_DIRECTORY_FILE, &TargetHandle);
        if (NT_SUCCESS(Status))
        {
            Status = NtfsTxfCopySecurity(SourceHandle, TargetHandle);
            if (!NT_SUCCESS(Status))
                ZwClose(TargetHandle);
        }
    }
    if (!NT_SUCCESS(Status))
    {
        ZwClose(SourceHandle);
        return Status;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, NTFS_TXF_COPY_CHUNK, TAG_NTFS_TXF);
    if (!Buffer)
        Status = STATUS_INSUFFICIENT_RESOURCES;
    EndOfFile.EndOfFile.QuadPart = 0;
    if (NT_SUCCESS(Status) && IntoExisting)
        Status = ZwSetInformationFile(TargetHandle, &IoStatus, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    Offset.QuadPart = 0;
    while (NT_SUCCESS(Status))
    {
        Status = ZwReadFile(SourceHandle, NULL, NULL, NULL, &IoStatus, Buffer, NTFS_TXF_COPY_CHUNK, &Offset, NULL);
        if (Status == STATUS_END_OF_FILE)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status) || IoStatus.Information == 0)
            break;
        Status = ZwWriteFile(TargetHandle, NULL, NULL, NULL, &IoStatus, Buffer, (ULONG)IoStatus.Information, &Offset, NULL);
        Offset.QuadPart += IoStatus.Information;
    }
    if (NT_SUCCESS(Status))
    {
        EndOfFile.EndOfFile = Offset;
        Status = ZwSetInformationFile(TargetHandle, &IoStatus, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    }
    if (NT_SUCCESS(Status))
        Status = ZwSetInformationFile(TargetHandle, &IoStatus, &Basic, sizeof(Basic), FileBasicInformation);
    if (Buffer)
        ExFreePoolWithTag(Buffer, TAG_NTFS_TXF);
    ZwClose(TargetHandle);
    ZwClose(SourceHandle);
    return Status;
}

static
NTSTATUS
NtfsTxfEnsureStaging(_In_ PVolumeContextBlock VolCB)
{
    HANDLE Handle;
    NTSTATUS Status = STATUS_SUCCESS;

    if (VolCB->TxfStagingReady)
        return STATUS_SUCCESS;

    KeEnterCriticalRegion();
    ExfAcquirePushLockExclusive(&VolCB->TxfStagingLock);
    if (!VolCB->TxfStagingReady)
    {
        Status = NtfsTxfOpen(VolCB, &NtfsTxfStagingRoot, FILE_LIST_DIRECTORY,
                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, FILE_OPEN_IF, FILE_DIRECTORY_FILE, &Handle);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
            /* TODO: replay a commit cut by a crash; the leftovers of uncommitted work are only discarded */
            NtfsTxfDeleteTree(VolCB, &NtfsTxfStagingRoot, TRUE);
            VolCB->TxfStagingReady = TRUE;
        }
    }
    ExfReleasePushLockExclusive(&VolCB->TxfStagingLock);
    KeLeaveCriticalRegion();
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : STATUS_RM_NOT_ACTIVE;
}

static
NTSTATUS
NtfsTxfStagePath(_In_ PNTFS_TXF_TRANSACTION Txf,
                 _Out_ PUNICODE_STRING Path)
{
    WCHAR Leaf[32];
    size_t Remaining;
    NTSTATUS Status;

    Status = RtlStringCchPrintfExW(Leaf, RTL_NUMBER_OF(Leaf), NULL, &Remaining, 0, L"%lu-%ld",
                                   Txf->Id, InterlockedIncrement(&Txf->NextStage));
    if (!NT_SUCCESS(Status))
        return Status;
    return NtfsTxfChild(Path, &NtfsTxfStagingRoot, Leaf,
                        (USHORT)((RTL_NUMBER_OF(Leaf) - Remaining) * sizeof(WCHAR)));
}

/* KTM ************************************************************************/

static
VOID
NtfsTxfApply(_In_ PNTFS_TXF_TRANSACTION Txf)
{
    PVolumeContextBlock VolCB = Txf->VolCB;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    /* FIXME: the committed changes are applied one by one without a log, so a crash during commit leaves part of them */
    for (Entry = Txf->Changes.Flink; Entry != &Txf->Changes; Entry = Entry->Flink)
    {
        PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);

        Status = STATUS_SUCCESS;
        switch (Change->Kind)
        {
            case NtfsTxfCreated:
                if (NtfsTxfExists(VolCB, &Change->Physical))
                    Status = NtfsTxfSetName(VolCB, &Change->Physical, &Change->Name, FALSE);
                break;
            case NtfsTxfShadow:
                if (NtfsTxfExists(VolCB, &Change->Physical) && NtfsTxfExists(VolCB, &Change->Name))
                    Status = NtfsTxfCopyData(VolCB, &Change->Physical, &Change->Name, TRUE);
                break;
            case NtfsTxfDeleted:
                if (NtfsTxfExists(VolCB, &Change->Name))
                    Status = NtfsTxfDeleteOne(VolCB, &Change->Name);
                break;
            case NtfsTxfRenamed:
                Status = NtfsTxfSetName(VolCB, &Change->Source, &Change->Name, FALSE);
                break;
            case NtfsTxfLinked:
                Status = NtfsTxfSetName(VolCB, &Change->Source, &Change->Name, TRUE);
                break;
            case NtfsTxfLocked:
                break;
        }
        if (!NT_SUCCESS(Status))
            DPRINT1("TxF: commit of %wZ failed with 0x%08lx\n", &Change->Name, Status);
    }
}

static
VOID
NtfsTxfDiscard(_In_ PNTFS_TXF_TRANSACTION Txf)
{
    PLIST_ENTRY Entry;

    for (Entry = Txf->Changes.Flink; Entry != &Txf->Changes; Entry = Entry->Flink)
    {
        PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);

        if ((Change->Kind == NtfsTxfCreated || Change->Kind == NtfsTxfShadow) &&
            Change->Physical.Buffer && NtfsTxfExists(Txf->VolCB, &Change->Physical))
        {
            NtfsTxfDeleteTree(Txf->VolCB, &Change->Physical, FALSE);
        }
    }
}

static
VOID
NtfsTxfEnd(_In_ PNTFS_TXF_TRANSACTION Txf,
           _In_ BOOLEAN Commit)
{
    PVolumeContextBlock VolCB = Txf->VolCB;

    ExAcquireFastMutex(&VolCB->TxfMutex);
    Txf->Ending = TRUE;
    Txf->ActiveThread = PsGetCurrentThread();
    ExReleaseFastMutex(&VolCB->TxfMutex);

    if (Commit)
        NtfsTxfApply(Txf);
    NtfsTxfDiscard(Txf);

    ExAcquireFastMutex(&VolCB->TxfMutex);
    RemoveEntryList(&Txf->Link);
    InitializeListHead(&Txf->Link);
    InterlockedDecrement(&VolCB->TxfActiveCount);
    Txf->ActiveThread = NULL;
    ExReleaseFastMutex(&VolCB->TxfMutex);
}

static
NTSTATUS
NTAPI
NtfsTxfNotification(_In_ PKENLISTMENT EnlistmentObject,
                    _In_ PVOID RMContext,
                    _In_ PVOID TransactionContext,
                    _In_ ULONG TransactionNotification,
                    _Inout_ PLARGE_INTEGER TmVirtualClock,
                    _In_ ULONG ArgumentLength,
                    _In_ PVOID Argument)
{
    PNTFS_TXF_TRANSACTION Txf = TransactionContext;
    HANDLE Enlistment;

    UNREFERENCED_PARAMETER(RMContext);
    UNREFERENCED_PARAMETER(ArgumentLength);
    UNREFERENCED_PARAMETER(Argument);

    switch (TransactionNotification)
    {
        case TRANSACTION_NOTIFY_PREPREPARE:
            TmPrePrepareComplete(EnlistmentObject, TmVirtualClock);
            break;

        case TRANSACTION_NOTIFY_PREPARE:
            TmPrepareComplete(EnlistmentObject, TmVirtualClock);
            break;

        case TRANSACTION_NOTIFY_COMMIT:
        case TRANSACTION_NOTIFY_ROLLBACK:
            NtfsTxfEnd(Txf, TransactionNotification == TRANSACTION_NOTIFY_COMMIT);
            Enlistment = Txf->EnlistmentHandle;
            Txf->EnlistmentHandle = NULL;
            if (TransactionNotification == TRANSACTION_NOTIFY_COMMIT)
                TmCommitComplete(EnlistmentObject, TmVirtualClock);
            else
                TmRollbackComplete(EnlistmentObject, TmVirtualClock);
            if (Enlistment)
                ZwClose(Enlistment);
            NtfsTxfDereference(Txf);
            break;

        default:
            break;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTxfEnsureResourceManager(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PKRESOURCEMANAGER ResourceManager;
    HANDLE TmHandle, RmHandle;
    NTSTATUS Status;
    GUID Guid;

    if (NtfsTxfResourceManager)
        return STATUS_SUCCESS;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateTransactionManager(&TmHandle, TRANSACTIONMANAGER_ALL_ACCESS, &ObjectAttributes,
                                        NULL, TRANSACTION_MANAGER_VOLATILE, 0);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ExUuidCreate(&Guid);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCreateResourceManager(&RmHandle, RESOURCEMANAGER_ALL_ACCESS, TmHandle, &Guid,
                                         &ObjectAttributes, RESOURCE_MANAGER_VOLATILE, NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        ZwClose(TmHandle);
        return Status;
    }
    Status = ObReferenceObjectByHandle(RmHandle, 0, *TmResourceManagerObjectType, KernelMode,
                                       (PVOID*)&ResourceManager, NULL);
    if (NT_SUCCESS(Status))
    {
        Status = TmEnableCallbacks(ResourceManager, NtfsTxfNotification, NULL);
        if (NT_SUCCESS(Status) &&
            InterlockedCompareExchangePointer((PVOID*)&NtfsTxfResourceManager, ResourceManager, NULL) == NULL)
        {
            return STATUS_SUCCESS;
        }
        ObDereferenceObject(ResourceManager);
    }
    ZwClose(RmHandle);
    ZwClose(TmHandle);
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
NtfsTxfFindTransaction(_In_ PVolumeContextBlock VolCB,
                       _In_ PKTRANSACTION Transaction,
                       _Out_ PNTFS_TXF_TRANSACTION* Result)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PNTFS_TXF_TRANSACTION Txf = NULL;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS;

    *Result = NULL;
    if (!TmIsTransactionActive(Transaction))
        return STATUS_TRANSACTION_NOT_ACTIVE;
    Status = NtfsTxfEnsureResourceManager();
    if (!NT_SUCCESS(Status))
        return STATUS_RM_NOT_ACTIVE;

    KeEnterCriticalRegion();
    ExfAcquirePushLockExclusive(&VolCB->TxfStagingLock);
    ExAcquireFastMutex(&VolCB->TxfMutex);
    for (Entry = VolCB->TxfTransactions.Flink; Entry != &VolCB->TxfTransactions; Entry = Entry->Flink)
    {
        PNTFS_TXF_TRANSACTION Candidate = CONTAINING_RECORD(Entry, NTFS_TXF_TRANSACTION, Link);

        if (Candidate->Transaction == Transaction)
        {
            Txf = Candidate;
            break;
        }
    }
    if (Txf)
    {
        if (Txf->Ending)
            Status = STATUS_TRANSACTION_NOT_ACTIVE;
        else
            NtfsTxfReference(Txf);
    }
    ExReleaseFastMutex(&VolCB->TxfMutex);

    if (!Txf)
    {
        Txf = ExAllocatePoolZero(NonPagedPool, sizeof(*Txf), TAG_NTFS_TXF);
        if (!Txf)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            Txf->References = 2;
            Txf->VolCB = VolCB;
            Txf->Transaction = Transaction;
            ObReferenceObject(Transaction);
            InitializeListHead(&Txf->Changes);
            InitializeListHead(&Txf->Readers);
            ExAcquireFastMutex(&VolCB->TxfMutex);
            Txf->Id = ++VolCB->TxfNextId;
            InsertTailList(&VolCB->TxfTransactions, &Txf->Link);
            InterlockedIncrement(&VolCB->TxfActiveCount);
            ExReleaseFastMutex(&VolCB->TxfMutex);

            InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
            Status = TmCreateEnlistment(&Txf->EnlistmentHandle,
                                        KernelMode,
                                        ENLISTMENT_ALL_ACCESS,
                                        &ObjectAttributes,
                                        NtfsTxfResourceManager,
                                        Transaction,
                                        0,
                                        TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                                            TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                                        Txf);
            if (!NT_SUCCESS(Status))
            {
                ExAcquireFastMutex(&VolCB->TxfMutex);
                Txf->Ending = TRUE;
                RemoveEntryList(&Txf->Link);
                InitializeListHead(&Txf->Link);
                InterlockedDecrement(&VolCB->TxfActiveCount);
                ExReleaseFastMutex(&VolCB->TxfMutex);
                NtfsTxfDereference(Txf);
                NtfsTxfDereference(Txf);
                Txf = NULL;
            }
        }
    }
    ExfReleasePushLockExclusive(&VolCB->TxfStagingLock);
    KeLeaveCriticalRegion();

    if (!NT_SUCCESS(Status))
        return Status;
    *Result = Txf;
    return STATUS_SUCCESS;
}

/* STREAMS ********************************************************************/

static
BOOLEAN
NtfsTxfStreamHasWriters(_In_ PVolumeContextBlock VolCB,
                        _In_ ULONGLONG FileReference)
{
    PLIST_ENTRY Bucket = NtfsStreamHashBucket(VolCB, FileReference);
    PLIST_ENTRY Entry;
    BOOLEAN Writers = FALSE;

    ExAcquireFastMutex(&VolCB->StreamListMutex);
    for (Entry = Bucket->Flink; Entry != Bucket; Entry = Entry->Flink)
    {
        PStreamContextBlock StreamCB = CONTAINING_RECORD(Entry, StreamContextBlock, HashEntry);

        if ((StreamCB->FileReference & NTFS_TXF_RECORD_MASK) == (FileReference & NTFS_TXF_RECORD_MASK) &&
            StreamCB->ShareAccess.Writers != 0)
        {
            Writers = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&VolCB->StreamListMutex);
    return Writers;
}

/* CREATE *********************************************************************/

VOID
NtfsTxfInitializeVolume(_Inout_ PVolumeContextBlock VolCB)
{
    ExInitializeFastMutex(&VolCB->TxfMutex);
    InitializeListHead(&VolCB->TxfTransactions);
    VolCB->TxfActiveCount = 0;
    VolCB->TxfNextId = 0;
    ExInitializePushLock(&VolCB->TxfStagingLock.Value);
    VolCB->TxfStagingReady = FALSE;
    RtlInitEmptyUnicodeString(&VolCB->TxfDeviceName, NULL, 0);
}

BOOLEAN
NtfsTxfVolumeSupported(_In_ PVolumeContextBlock VolCB)
{
    return !NtfsVolumeIsReadOnly(VolCB->DiskVolume);
}

static
BOOLEAN
NtfsTxfWriteIntent(_In_ ACCESS_MASK Access,
                   _In_ ULONG Disposition)
{
    return (Access & NTFS_TXF_WRITE_ACCESS) ||
           Disposition == FILE_SUPERSEDE ||
           Disposition == FILE_OVERWRITE ||
           Disposition == FILE_OVERWRITE_IF;
}

static
NTSTATUS
NtfsTxfReplaceName(_Inout_ PFILE_OBJECT FileObject,
                   _In_ PCUNICODE_STRING Physical,
                   _In_ PCUNICODE_STRING Stream)
{
    UNICODE_STRING Name;
    PWCHAR Buffer;
    NTSTATUS Status;

    Status = NtfsTxfJoin(&Name, Physical, NULL, Stream->Buffer, Stream->Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Name.Length <= FileObject->FileName.MaximumLength)
    {
        RtlCopyMemory(FileObject->FileName.Buffer, Name.Buffer, Name.Length);
        FileObject->FileName.Length = Name.Length;
        NtfsTxfFreeString(&Name);
        return STATUS_SUCCESS;
    }
    Buffer = ExAllocatePoolWithTag(PagedPool, Name.Length, TAG_NTFS);
    if (!Buffer)
    {
        NtfsTxfFreeString(&Name);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(Buffer, Name.Buffer, Name.Length);
    if (FileObject->FileName.Buffer)
        ExFreePool(FileObject->FileName.Buffer);
    FileObject->FileName.Buffer = Buffer;
    FileObject->FileName.Length = FileObject->FileName.MaximumLength = Name.Length;
    NtfsTxfFreeString(&Name);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTxfPrepareExisting(_In_ PVolumeContextBlock VolCB,
                       _In_ PNTFS_TXF_TRANSACTION Txf,
                       _In_ PCUNICODE_STRING Name,
                       _In_ PCUNICODE_STRING Stream,
                       _In_ PNTFS_TXF_INFO Info,
                       _In_ ACCESS_MASK Access,
                       _In_ ULONG Disposition,
                       _Inout_ PUNICODE_STRING Physical,
                       _Inout_ PNTFS_TXF_OPEN Open)
{
    UNICODE_STRING Shadow;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Locked, Conflict, Owned;

    if (NtfsTxfIsStaging(Physical))
        return STATUS_SUCCESS;

    if (!Info->Directory && NtfsTxfWriteIntent(Access, Disposition))
    {
        if (Stream->Length)
        {
            /* TODO: shadow named streams; a transacted writer of a named stream is refused */
            return STATUS_OPERATION_NOT_SUPPORTED_IN_TRANSACTION;
        }
        ExAcquireFastMutex(&VolCB->TxfMutex);
        Locked = NtfsTxfIsLockedLocked(VolCB, Txf, Info->FileReference, FALSE);
        ExReleaseFastMutex(&VolCB->TxfMutex);
        if (Locked)
            return STATUS_SHARING_VIOLATION;
        if (NtfsTxfStreamHasWriters(VolCB, Info->FileReference))
            return STATUS_TRANSACTIONAL_CONFLICT;

        Status = NtfsTxfStagePath(Txf, &Shadow);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = NtfsTxfCopyData(VolCB, Physical, &Shadow, FALSE);
        if (NT_SUCCESS(Status))
            Status = NtfsTxfRecord(Txf, NtfsTxfShadow, Name, NULL, &Shadow, Info->FileReference, FALSE, NULL);
        if (!NT_SUCCESS(Status))
        {
            NtfsTxfDeleteTree(VolCB, &Shadow, FALSE);
            NtfsTxfFreeString(&Shadow);
            return Status;
        }
        NtfsTxfFreeString(Physical);
        *Physical = Shadow;
        return STATUS_SUCCESS;
    }

    if (Access & DELETE)
    {
        ExAcquireFastMutex(&VolCB->TxfMutex);
        Locked = NtfsTxfIsLockedLocked(VolCB, Txf, Info->FileReference, FALSE);
        Owned = NtfsTxfHasLockLocked(Txf, Info->FileReference);
        ExReleaseFastMutex(&VolCB->TxfMutex);
        if (Locked)
            return STATUS_SHARING_VIOLATION;
        Conflict = !Info->Directory && NtfsTxfStreamHasWriters(VolCB, Info->FileReference);
        if (Conflict)
            return STATUS_TRANSACTIONAL_CONFLICT;
        if (!Owned)
            Status = NtfsTxfRecord(Txf, NtfsTxfLocked, Name, NULL, NULL, Info->FileReference, Info->Directory, NULL);
        return Status;
    }

    Open->Reader = !Info->Directory;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTxfPrepareNew(_In_ PVolumeContextBlock VolCB,
                  _In_ PNTFS_TXF_TRANSACTION Txf,
                  _In_ PCUNICODE_STRING Name,
                  _In_ BOOLEAN Directory,
                  _Inout_ PUNICODE_STRING Physical,
                  _Inout_ PNTFS_TXF_OPEN Open)
{
    UNICODE_STRING Parent, Leaf, ParentPhysical, Staged;
    NTFS_TXF_VIEW View;
    NTFS_TXF_INFO Info;
    NTSTATUS Status;
    BOOLEAN Reserved;

    NtfsTxfSplit(Name, &Parent, &Leaf);
    Status = NtfsTxfResolve(Txf, &Parent, &View, &ParentPhysical);
    if (!NT_SUCCESS(Status))
        return Status;
    if (View == NtfsTxfViewHidden || View == NtfsTxfViewHiddenParent)
        return STATUS_OBJECT_PATH_NOT_FOUND;

    NtfsTxfFreeString(Physical);
    if (NtfsTxfIsStaging(&ParentPhysical))
    {
        Status = NtfsTxfChild(Physical, &ParentPhysical, Leaf.Buffer, Leaf.Length);
        NtfsTxfFreeString(&ParentPhysical);
        return Status;
    }

    Status = NtfsTxfQuery(VolCB, &ParentPhysical, &Info);
    NtfsTxfFreeString(&ParentPhysical);
    if (!NT_SUCCESS(Status) || !Info.Directory)
        return STATUS_OBJECT_PATH_NOT_FOUND;

    ExAcquireFastMutex(&VolCB->TxfMutex);
    Reserved = NtfsTxfIsReservedLocked(VolCB, Txf, Name);
    ExReleaseFastMutex(&VolCB->TxfMutex);
    if (Reserved)
        return STATUS_TRANSACTIONAL_CONFLICT;

    Status = NtfsTxfStagePath(Txf, &Staged);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = NtfsTxfRecord(Txf, NtfsTxfCreated, Name, NULL, &Staged, 0, Directory, NULL);
    if (!NT_SUCCESS(Status))
    {
        NtfsTxfFreeString(&Staged);
        return Status;
    }
    *Physical = Staged;
    Open->Staged = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsTxfPrepareCreate(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                     _In_ PIRP Irp,
                     _Inout_ PNTFS_TXF_OPEN Open)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PVolumeContextBlock VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    PTXN_PARAMETER_BLOCK TxnParameters = IoGetTransactionParameterBlock(FileObject);
    ULONG Disposition = (IrpSp->Parameters.Create.Options >> 24) & 0xFF;
    ULONG Options = IrpSp->Parameters.Create.Options & 0x00FFFFFF;
    ACCESS_MASK Access = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    BOOLEAN OpenTarget = BooleanFlagOn(IrpSp->Flags, SL_OPEN_TARGET_DIRECTORY);
    UNICODE_STRING Name, Stream, Tail, Physical, Parent, Leaf, Logical;
    PNTFS_TXF_TRANSACTION Txf;
    NTFS_TXF_VIEW View;
    NTFS_TXF_INFO Info;
    NTSTATUS Status;
    BOOLEAN Reserved;

    NtfsTxfSplitStream(&FileObject->FileName, &Name, &Stream);
    while (Name.Length > sizeof(WCHAR) && Name.Buffer[Name.Length / sizeof(WCHAR) - 1] == L'\\')
        Name.Length -= sizeof(WCHAR);
    Tail.Buffer = Name.Buffer + Name.Length / sizeof(WCHAR);
    Tail.Length = Tail.MaximumLength = FileObject->FileName.Length - Name.Length;

    if (!TxnParameters)
    {
        if (VolCB->TxfActiveCount == 0 || OpenTarget || Disposition == FILE_OPEN ||
            Disposition == FILE_OVERWRITE || NtfsTxfIsStaging(&Name))
        {
            return STATUS_SUCCESS;
        }
        ExAcquireFastMutex(&VolCB->TxfMutex);
        Reserved = NtfsTxfIsReservedLocked(VolCB, NULL, &Name);
        ExReleaseFastMutex(&VolCB->TxfMutex);
        return Reserved ? STATUS_TRANSACTIONAL_CONFLICT : STATUS_SUCCESS;
    }

    if (Name.Length == 0 || NtfsTxfIsStaging(&Name))
        return STATUS_ACCESS_DENIED;
    if (!NtfsTxfVolumeSupported(VolCB))
        return STATUS_TRANSACTIONAL_OPEN_NOT_ALLOWED;

    Status = NtfsTxfEnsureDeviceName(VolCB, VolumeDeviceObject);
    if (NT_SUCCESS(Status))
        Status = NtfsTxfEnsureStaging(VolCB);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = NtfsTxfFindTransaction(VolCB, TxnParameters->TransactionObject, &Txf);
    if (!NT_SUCCESS(Status))
        return Status;
    Open->Txf = Txf;

    if (OpenTarget)
    {
        NtfsTxfSplit(&Name, &Parent, &Leaf);
        Status = NtfsTxfCopy(&Open->LogicalName, &Parent);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = NtfsTxfResolve(Txf, &Parent, &View, &Physical);
        if (!NT_SUCCESS(Status))
            return Status;
        if (View == NtfsTxfViewHidden || View == NtfsTxfViewHiddenParent)
        {
            NtfsTxfFreeString(&Physical);
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }
        Status = NtfsTxfChild(&Logical, &Physical, Leaf.Buffer, Leaf.Length);
        NtfsTxfFreeString(&Physical);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = NtfsTxfReplaceName(FileObject, &Logical, &Tail);
        NtfsTxfFreeString(&Logical);
        return Status;
    }

    Status = NtfsTxfJoin(&Open->LogicalName, &Name, NULL, Stream.Buffer, Stream.Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsTxfResolve(Txf, &Name, &View, &Physical);
    if (!NT_SUCCESS(Status))
        return Status;
    if (View == NtfsTxfViewHiddenParent)
    {
        NtfsTxfFreeString(&Physical);
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    if (View != NtfsTxfViewHidden && NT_SUCCESS(NtfsTxfQuery(VolCB, &Physical, &Info)))
    {
        if (Disposition == FILE_CREATE && Stream.Length == 0)
            Status = STATUS_OBJECT_NAME_COLLISION;
        else
            Status = NtfsTxfPrepareExisting(VolCB, Txf, &Name, &Stream, &Info, Access, Disposition, &Physical, Open);
    }
    else if (Disposition == FILE_OPEN || Disposition == FILE_OVERWRITE)
    {
        if (View == NtfsTxfViewHidden)
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    else
    {
        Status = NtfsTxfPrepareNew(VolCB, Txf, &Name, BooleanFlagOn(Options, FILE_DIRECTORY_FILE), &Physical, Open);
    }

    if (NT_SUCCESS(Status))
        Status = NtfsTxfReplaceName(FileObject, &Physical, &Tail);
    NtfsTxfFreeString(&Physical);
    return Status;
}

NTSTATUS
NtfsTxfCheckOpen(_In_ PVolumeContextBlock VolCB,
                 _Inout_ PFileContextBlock FileCB,
                 _In_ PIO_STACK_LOCATION IrpSp,
                 _Inout_ PNTFS_TXF_OPEN Open)
{
    ULONG Disposition = (IrpSp->Parameters.Create.Options >> 24) & 0xFF;
    ACCESS_MASK Access = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    PNTFS_TXF_READER Reader;
    PLIST_ENTRY Entry;
    ULONGLONG FileReference;
    BOOLEAN Locked;

    if (!FileCB->StreamCB)
        return STATUS_SUCCESS;
    FileReference = FileCB->StreamCB->FileReference;

    if (!Open->Txf)
    {
        if (VolCB->TxfActiveCount == 0 ||
            !((Access & NTFS_TXF_MODIFY_ACCESS) || NtfsTxfWriteIntent(0, Disposition)) ||
            NtfsTxfIsStaging(&FileCB->FileName))
        {
            return STATUS_SUCCESS;
        }
        ExAcquireFastMutex(&VolCB->TxfMutex);
        Locked = NtfsTxfIsLockedLocked(VolCB, NULL, FileReference, TRUE);
        ExReleaseFastMutex(&VolCB->TxfMutex);
        return Locked ? STATUS_SHARING_VIOLATION : STATUS_SUCCESS;
    }

    FileCB->Txf = Open->Txf;
    Open->Txf = NULL;
    FileCB->TxfLogicalName = Open->LogicalName;
    RtlInitEmptyUnicodeString(&Open->LogicalName, NULL, 0);
    if (!Open->Reader)
        return STATUS_SUCCESS;

    if (NtfsTxfStreamHasWriters(VolCB, FileReference))
        return STATUS_TRANSACTIONAL_CONFLICT;

    Reader = ExAllocatePoolZero(PagedPool, sizeof(*Reader), TAG_NTFS_TXF);
    ExAcquireFastMutex(&VolCB->TxfMutex);
    for (Entry = FileCB->Txf->Readers.Flink; Entry != &FileCB->Txf->Readers; Entry = Entry->Flink)
    {
        PNTFS_TXF_READER Existing = CONTAINING_RECORD(Entry, NTFS_TXF_READER, Link);

        if (Existing->FileReference == FileReference)
        {
            Existing->Count++;
            FileCB->TxfReaderReference = FileReference;
            break;
        }
    }
    if (!FileCB->TxfReaderReference && Reader)
    {
        Reader->FileReference = FileReference;
        Reader->Count = 1;
        InsertTailList(&FileCB->Txf->Readers, &Reader->Link);
        FileCB->TxfReaderReference = FileReference;
        Reader = NULL;
    }
    ExReleaseFastMutex(&VolCB->TxfMutex);
    if (Reader)
        ExFreePoolWithTag(Reader, TAG_NTFS_TXF);
    return FileCB->TxfReaderReference ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

VOID
NtfsTxfReleaseOpen(_Inout_ PNTFS_TXF_OPEN Open)
{
    NtfsTxfFreeString(&Open->LogicalName);
    if (Open->Txf)
        NtfsTxfDereference(Open->Txf);
    Open->Txf = NULL;
}

static
VOID
NtfsTxfDropReader(_In_ PVolumeContextBlock VolCB,
                  _Inout_ PFileContextBlock FileCB)
{
    PNTFS_TXF_READER Free = NULL;
    PLIST_ENTRY Entry;

    if (!FileCB->Txf || !FileCB->TxfReaderReference)
        return;
    ExAcquireFastMutex(&VolCB->TxfMutex);
    for (Entry = FileCB->Txf->Readers.Flink; Entry != &FileCB->Txf->Readers; Entry = Entry->Flink)
    {
        PNTFS_TXF_READER Reader = CONTAINING_RECORD(Entry, NTFS_TXF_READER, Link);

        if (Reader->FileReference == FileCB->TxfReaderReference)
        {
            if (--Reader->Count == 0)
            {
                RemoveEntryList(&Reader->Link);
                Free = Reader;
            }
            break;
        }
    }
    FileCB->TxfReaderReference = 0;
    ExReleaseFastMutex(&VolCB->TxfMutex);
    if (Free)
        ExFreePoolWithTag(Free, TAG_NTFS_TXF);
}

VOID
NtfsTxfReleaseFileContext(_In_ PVolumeContextBlock VolCB,
                          _Inout_ PFileContextBlock FileCB)
{
    NtfsTxfDropReader(VolCB, FileCB);
    NtfsTxfFreeString(&FileCB->TxfLogicalName);
    if (FileCB->Txf)
        NtfsTxfDereference(FileCB->Txf);
    FileCB->Txf = NULL;
    FileCB->TxfDeleteChange = NULL;
}

PCUNICODE_STRING
NtfsTxfDisplayName(_In_ PFileContextBlock FileCB)
{
    if (FileCB->Txf && FileCB->TxfLogicalName.Buffer)
        return &FileCB->TxfLogicalName;
    return &FileCB->FileName;
}

/* NAMESPACE CHANGES **********************************************************/

static
BOOLEAN
NtfsTxfIsPrivate(_In_ PFileContextBlock FileCB)
{
    UNICODE_STRING Name, Stream;
    BOOLEAN Private;

    NtfsTxfSplitStream(&FileCB->FileName, &Name, &Stream);
    if (!NtfsTxfIsStaging(&Name))
        return FALSE;
    ExAcquireFastMutex(&FileCB->Txf->VolCB->TxfMutex);
    Private = NtfsTxfFindPhysicalLocked(FileCB->Txf, NtfsTxfShadow, &Name) == NULL;
    ExReleaseFastMutex(&FileCB->Txf->VolCB->TxfMutex);
    return Private;
}

static
ULONGLONG
NtfsTxfCommittedReference(_In_ PFileContextBlock FileCB)
{
    PNTFS_TXF_CHANGE Shadow;
    UNICODE_STRING Name, Stream;
    ULONGLONG FileReference;

    NtfsTxfSplitStream(&FileCB->FileName, &Name, &Stream);
    if (!NtfsTxfIsStaging(&Name))
        return FileCB->StreamCB ? FileCB->StreamCB->FileReference : 0;
    ExAcquireFastMutex(&FileCB->Txf->VolCB->TxfMutex);
    Shadow = NtfsTxfFindPhysicalLocked(FileCB->Txf, NtfsTxfShadow, &Name);
    FileReference = Shadow ? Shadow->FileReference : 0;
    ExReleaseFastMutex(&FileCB->Txf->VolCB->TxfMutex);
    return FileReference;
}

static
BOOLEAN
NtfsTxfHasChildrenLocked(_In_ PNTFS_TXF_TRANSACTION Txf,
                         _In_ PCUNICODE_STRING Directory)
{
    PLIST_ENTRY Entry;

    for (Entry = Txf->Changes.Flink; Entry != &Txf->Changes; Entry = Entry->Flink)
    {
        PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);
        PNTFS_TXF_CHANGE Match;
        UNICODE_STRING Parent, Leaf;

        if (Change->Kind != NtfsTxfCreated && Change->Kind != NtfsTxfRenamed && Change->Kind != NtfsTxfLinked)
            continue;
        NtfsTxfSplit(&Change->Name, &Parent, &Leaf);
        if (NtfsTxfEqual(&Parent, Directory) &&
            NtfsTxfResolveLocked(Txf, &Change->Name, &Match) == NtfsTxfViewMapped && Match == Change)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
NTSTATUS
NtfsTxfRecordDelete(_In_ PVolumeContextBlock VolCB,
                    _Inout_ PFileContextBlock FileCB)
{
    PNTFS_TXF_CHANGE Change;
    UNICODE_STRING Name, Stream;
    NTSTATUS Status;
    BOOLEAN Directory = FileCB->FileRec && (NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY);
    BOOLEAN Children;

    if (FileCB->TxfDeleteChange)
        return STATUS_SUCCESS;
    NtfsTxfSplitStream(&FileCB->TxfLogicalName, &Name, &Stream);
    if (Directory)
    {
        ExAcquireFastMutex(&VolCB->TxfMutex);
        Children = NtfsTxfHasChildrenLocked(FileCB->Txf, &Name);
        ExReleaseFastMutex(&VolCB->TxfMutex);
        if (Children)
            return STATUS_DIRECTORY_NOT_EMPTY;
    }
    Status = NtfsTxfRecord(FileCB->Txf, NtfsTxfDeleted, &Name, NULL, NULL,
                           NtfsTxfCommittedReference(FileCB), Directory, &Change);
    if (NT_SUCCESS(Status))
        FileCB->TxfDeleteChange = Change;
    return Status;
}

NTSTATUS
NtfsTxfSetDisposition(_In_ PVolumeContextBlock VolCB,
                      _Inout_ PFileContextBlock FileCB,
                      _In_ ULONG Flags,
                      _In_ NTSTATUS DirectoryEmpty,
                      _Out_ PBOOLEAN Handled)
{
    NtfsFileBasicInformation Basic;
    PNTFS_TXF_CHANGE Change;
    NTSTATUS Status;

    *Handled = FALSE;
    if (!FileCB->Txf || NtfsTxfIsPrivate(FileCB))
        return STATUS_SUCCESS;
    *Handled = TRUE;
    if (FileCB->Txf->Ending)
        return STATUS_TRANSACTION_NOT_ACTIVE;
    if (!(FileCB->DesiredAccess & DELETE))
        return STATUS_ACCESS_DENIED;
    if (FileCB->RequestedStream)
    {
        /* TODO: delete a named stream inside a transaction */
        return STATUS_OPERATION_NOT_SUPPORTED_IN_TRANSACTION;
    }

    if (Flags & FILE_DISPOSITION_ON_CLOSE)
    {
        if (Flags & FILE_DISPOSITION_DELETE)
            FileCB->CreateOptions |= FILE_DELETE_ON_CLOSE;
        else
            FileCB->CreateOptions &= ~FILE_DELETE_ON_CLOSE;
        return STATUS_SUCCESS;
    }

    if (!(Flags & FILE_DISPOSITION_DELETE))
    {
        Change = FileCB->TxfDeleteChange;
        FileCB->TxfDeleteChange = NULL;
        if (Change)
        {
            ExAcquireFastMutex(&VolCB->TxfMutex);
            RemoveEntryList(&Change->Link);
            ExReleaseFastMutex(&VolCB->TxfMutex);
            NtfsTxfFreeChange(Change);
        }
        return STATUS_SUCCESS;
    }

    Status = NtfsFileRecordGetBasicInformation(FileCB->FileRec, &Basic);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Basic.FileAttributes & FILE_ATTRIBUTE_READONLY) && !(Flags & FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE))
        return STATUS_CANNOT_DELETE;
    /* FIXME: a directory whose last committed children were deleted in the same transaction still reads as not empty */
    if (!NT_SUCCESS(DirectoryEmpty))
        return DirectoryEmpty;
    return NtfsTxfRecordDelete(VolCB, FileCB);
}

VOID
NtfsTxfCleanup(_In_ PVolumeContextBlock VolCB,
               _Inout_ PFileContextBlock FileCB)
{
    if (!FileCB->Txf)
        return;
    NtfsTxfDropReader(VolCB, FileCB);
    if ((FileCB->CreateOptions & FILE_DELETE_ON_CLOSE) && !FileCB->Txf->Ending &&
        !FileCB->RequestedStream && !NtfsTxfIsPrivate(FileCB))
    {
        NtfsTxfRecordDelete(VolCB, FileCB);
        FileCB->CreateOptions &= ~FILE_DELETE_ON_CLOSE;
    }
}

NTSTATUS
NtfsTxfRename(_In_ PVolumeContextBlock VolCB,
              _Inout_ PFileContextBlock FileCB,
              _In_opt_ PFileContextBlock TargetFileCB,
              _In_ PCUNICODE_STRING LeafName,
              _In_ BOOLEAN ReplaceIfExists,
              _In_ BOOLEAN Link)
{
    UNICODE_STRING OldName, Stream, Parent, Leaf, NewName, Source, TargetPhysical, NewLogical;
    PNTFS_TXF_TRANSACTION Txf = FileCB->Txf;
    NTFS_TXF_VIEW View;
    NTFS_TXF_INFO Target;
    NTSTATUS Status;
    BOOLEAN Directory = FileCB->FileRec && (NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY);
    BOOLEAN Exists, Reserved, Locked;

    if (Txf->Ending)
        return STATUS_TRANSACTION_NOT_ACTIVE;
    NtfsTxfSplitStream(&FileCB->TxfLogicalName, &OldName, &Stream);
    if (Stream.Length)
    {
        /* TODO: rename a named stream inside a transaction */
        return STATUS_OPERATION_NOT_SUPPORTED_IN_TRANSACTION;
    }
    if (TargetFileCB)
        Parent = *NtfsTxfDisplayName(TargetFileCB);
    else
        NtfsTxfSplit(&OldName, &Parent, &Leaf);
    Status = NtfsTxfChild(&NewName, &Parent, LeafName->Buffer, LeafName->Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Link && RtlEqualUnicodeString(&NewName, &OldName, FALSE))
    {
        NtfsTxfFreeString(&NewName);
        return STATUS_SUCCESS;
    }

    ExAcquireFastMutex(&VolCB->TxfMutex);
    Reserved = NtfsTxfIsReservedLocked(VolCB, Txf, &NewName);
    ExReleaseFastMutex(&VolCB->TxfMutex);
    if (Reserved)
    {
        NtfsTxfFreeString(&NewName);
        return STATUS_TRANSACTIONAL_CONFLICT;
    }

    Status = NtfsTxfResolve(Txf, &NewName, &View, &TargetPhysical);
    Exists = NT_SUCCESS(Status) &&
             (View == NtfsTxfViewMapped || View == NtfsTxfViewCommitted) &&
             NT_SUCCESS(NtfsTxfQuery(VolCB, &TargetPhysical, &Target));
    if (NT_SUCCESS(Status) && View == NtfsTxfViewHiddenParent)
        Status = STATUS_OBJECT_PATH_NOT_FOUND;
    if (NT_SUCCESS(Status) && Exists && !(!Link && NtfsTxfEqual(&NewName, &OldName)))
    {
        if (!ReplaceIfExists)
            Status = STATUS_OBJECT_NAME_COLLISION;
        else if (Target.Directory)
            Status = STATUS_ACCESS_DENIED;
        else
        {
            ExAcquireFastMutex(&VolCB->TxfMutex);
            Locked = !NtfsTxfIsStaging(&TargetPhysical) &&
                     NtfsTxfIsLockedLocked(VolCB, Txf, Target.FileReference, TRUE);
            ExReleaseFastMutex(&VolCB->TxfMutex);
            if (Locked)
                Status = STATUS_SHARING_VIOLATION;
            else
                Status = NtfsTxfRecord(Txf, NtfsTxfDeleted, &NewName, NULL, NULL,
                                       NtfsTxfIsStaging(&TargetPhysical) ? 0 : Target.FileReference,
                                       FALSE, NULL);
        }
    }
    NtfsTxfFreeString(&TargetPhysical);

    Source = FileCB->FileName;
    if (NT_SUCCESS(Status))
    {
        UNICODE_STRING SourceName, SourceStream;

        NtfsTxfSplitStream(&Source, &SourceName, &SourceStream);
        Status = NtfsTxfRecord(Txf, Link ? NtfsTxfLinked : NtfsTxfRenamed, &NewName, &OldName, &SourceName,
                               NtfsTxfCommittedReference(FileCB), Directory, NULL);
    }
    if (NT_SUCCESS(Status) && !Link)
    {
        Status = NtfsTxfCopy(&NewLogical, &NewName);
        if (NT_SUCCESS(Status))
        {
            NtfsTxfFreeString(&FileCB->TxfLogicalName);
            FileCB->TxfLogicalName = NewLogical;
        }
    }
    NtfsTxfFreeString(&NewName);
    return Status;
}

NTSTATUS
NtfsTxfCheckNewName(_In_ PVolumeContextBlock VolCB,
                    _In_ PCUNICODE_STRING NewName)
{
    BOOLEAN Reserved;

    if (VolCB->TxfActiveCount == 0)
        return STATUS_SUCCESS;
    ExAcquireFastMutex(&VolCB->TxfMutex);
    Reserved = NtfsTxfIsReservedLocked(VolCB, NULL, NewName);
    ExReleaseFastMutex(&VolCB->TxfMutex);
    return Reserved ? STATUS_TRANSACTIONAL_CONFLICT : STATUS_SUCCESS;
}

/* DIRECTORY ENUMERATION ******************************************************/

BOOLEAN
NtfsTxfKeepDirectoryEntry(_In_ PVolumeContextBlock VolCB,
                          _In_ PFileContextBlock FileCB,
                          _In_reads_(NameLength) PCWSTR Name,
                          _In_ USHORT NameLength)
{
    UNICODE_STRING Logical, Physical, Mapped;
    PNTFS_TXF_CHANGE Match;
    NTFS_TXF_VIEW View;
    BOOLEAN Keep = TRUE;

    if (!FileCB->Txf)
        return TRUE;
    if (!NT_SUCCESS(NtfsTxfChild(&Logical, NtfsTxfDisplayName(FileCB), Name, NameLength * sizeof(WCHAR))))
        return TRUE;
    if (!NT_SUCCESS(NtfsTxfChild(&Physical, &FileCB->FileName, Name, NameLength * sizeof(WCHAR))))
    {
        NtfsTxfFreeString(&Logical);
        return TRUE;
    }

    ExAcquireFastMutex(&VolCB->TxfMutex);
    View = NtfsTxfResolveLocked(FileCB->Txf, &Logical, &Match);
    if (View == NtfsTxfViewHidden || View == NtfsTxfViewHiddenParent)
    {
        Keep = FALSE;
    }
    else if (View == NtfsTxfViewMapped)
    {
        if (NT_SUCCESS(NtfsTxfMapped(Match, &Logical, &Mapped)))
        {
            Keep = NtfsTxfEqual(&Mapped, &Physical);
            NtfsTxfFreeString(&Mapped);
        }
    }
    ExReleaseFastMutex(&VolCB->TxfMutex);

    NtfsTxfFreeString(&Physical);
    NtfsTxfFreeString(&Logical);
    return Keep;
}

static
BOOLEAN
NtfsTxfNameMatches(_In_opt_ PUNICODE_STRING Filter,
                   _In_ PCUNICODE_STRING Name)
{
    UNICODE_STRING Upcased;
    BOOLEAN Match;

    if (!Filter || Filter->Length == 0)
        return TRUE;
    if (!NT_SUCCESS(RtlUpcaseUnicodeString(&Upcased, Filter, TRUE)))
        return FALSE;
    Match = FsRtlIsNameInExpression(&Upcased, (PUNICODE_STRING)Name, TRUE, NULL);
    RtlFreeUnicodeString(&Upcased);
    return Match;
}

static
NTSTATUS
NtfsTxfFillEntry(_In_ PVolumeContextBlock VolCB,
                 _In_ PCUNICODE_STRING Physical,
                 _Out_ PFILE_ID_BOTH_DIR_INFORMATION Info)
{
    PNtfsMasterFileTable Mft = NtfsVolumeGetMft(VolCB->DiskVolume);
    NtfsFileBasicInformation Basic;
    PNtfsFileRecord Record = NULL;
    PAttribute Data;
    ULONG Remaining = 0;
    NTSTATUS Status;

    RtlZeroMemory(Info, sizeof(*Info));
    Status = NtfsMasterFileTableGetFileRecordFromQueryEx(Mft, Physical->Buffer, Physical->Length / sizeof(WCHAR),
                                                         TRUE, &Remaining, &Record);
    if (NT_SUCCESS(Status) && (Remaining != 0 || !Record))
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    if (NT_SUCCESS(Status))
        Status = NtfsFileRecordGetBasicInformation(Record, &Basic);
    if (NT_SUCCESS(Status))
    {
        Info->CreationTime.QuadPart = Basic.CreationTime;
        Info->LastAccessTime.QuadPart = Basic.LastAccessTime;
        Info->LastWriteTime.QuadPart = Basic.LastWriteTime;
        Info->ChangeTime.QuadPart = Basic.ChangeTime;
        Info->FileAttributes = Basic.FileAttributes;
        if (NtfsFileRecordGetHeader(Record)->Flags & FR_IS_DIRECTORY)
        {
            Info->FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }
        else
        {
            Data = NtfsFileRecordGetAttribute(Record, TypeData, NULL);
            if (Data && Data->IsNonResident)
            {
                Info->EndOfFile.QuadPart = Data->NonResident.DataSize;
                Info->AllocationSize.QuadPart = NtfsAttributeGetPhysicalAllocationSize(Data);
            }
            else if (Data)
            {
                Info->EndOfFile.QuadPart = Data->Resident.DataLength;
                Info->AllocationSize.QuadPart = Data->Resident.DataLength;
            }
        }
        if (!Info->FileAttributes)
            Info->FileAttributes = FILE_ATTRIBUTE_NORMAL;
        Info->FileId.QuadPart = NtfsFileRecordGetReference(Record);
    }
    if (Record)
        NtfsFileRecordDestroy(Record);
    return Status;
}

NTSTATUS
NtfsTxfNextDirectoryEntry(_In_ PVolumeContextBlock VolCB,
                          _Inout_ PFileContextBlock FileCB,
                          _In_opt_ PUNICODE_STRING FileNameFilter,
                          _In_ FILE_INFORMATION_CLASS InformationClass,
                          _Out_writes_bytes_(*Length) PVOID Buffer,
                          _Inout_ PULONG Length,
                          _Out_ PULONG EntrySize)
{
    PCUNICODE_STRING Directory = NtfsTxfDisplayName(FileCB);
    FILE_ID_BOTH_DIR_INFORMATION Info;
    UNICODE_STRING Leaf, Physical;
    PLIST_ENTRY Entry;
    ULONG Index;
    NTSTATUS Status;

    if (!FileCB->Txf)
        return STATUS_NO_MORE_FILES;

    for (;;)
    {
        Leaf.Buffer = NULL;
        Physical.Buffer = NULL;
        Index = 0;
        Status = STATUS_NO_MORE_FILES;
        ExAcquireFastMutex(&VolCB->TxfMutex);
        for (Entry = FileCB->Txf->Changes.Flink; Entry != &FileCB->Txf->Changes; Entry = Entry->Flink)
        {
            PNTFS_TXF_CHANGE Change = CONTAINING_RECORD(Entry, NTFS_TXF_CHANGE, Link);
            PNTFS_TXF_CHANGE Match;
            UNICODE_STRING Parent, ChangeLeaf;

            if (Change->Kind == NtfsTxfDeleted || Change->Kind == NtfsTxfLocked)
                continue;
            NtfsTxfSplit(&Change->Name, &Parent, &ChangeLeaf);
            if (!NtfsTxfEqual(&Parent, Directory) ||
                NtfsTxfResolveLocked(FileCB->Txf, &Change->Name, &Match) != NtfsTxfViewMapped ||
                Match != Change)
            {
                continue;
            }
            if (Index++ < FileCB->TxfInjectIndex)
                continue;
            Status = NtfsTxfCopy(&Leaf, &ChangeLeaf);
            if (NT_SUCCESS(Status))
                Status = NtfsTxfCopy(&Physical, &Change->Physical);
            break;
        }
        ExReleaseFastMutex(&VolCB->TxfMutex);
        if (Status == STATUS_NO_MORE_FILES || !NT_SUCCESS(Status))
        {
            NtfsTxfFreeString(&Leaf);
            NtfsTxfFreeString(&Physical);
            return Status;
        }

        FileCB->TxfInjectIndex++;
        if (NtfsTxfNameMatches(FileNameFilter, &Leaf) &&
            NT_SUCCESS(NtfsTxfFillEntry(VolCB, &Physical, &Info)))
        {
            Status = NtfsDirectoryStoreInfo(InformationClass, &Info, Leaf.Buffer, Leaf.Length, Buffer, Length, EntrySize);
            if (!NT_SUCCESS(Status))
                FileCB->TxfInjectIndex--;
            NtfsTxfFreeString(&Leaf);
            NtfsTxfFreeString(&Physical);
            return Status;
        }
        NtfsTxfFreeString(&Leaf);
        NtfsTxfFreeString(&Physical);
    }
}
