/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Names, lookups, file control blocks and the mapping of inode attributes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

#define EXT4_UNIX_EPOCH 116444736000000000LL

NTSTATUS
Ext4Utf8ToUnicode(
    _In_reads_bytes_(Length) const CHAR *Utf8,
    _In_ ULONG Length,
    _Out_ PUNICODE_STRING Name)
{
    ULONG Bytes = 0;
    NTSTATUS Status;
    PWCHAR Buffer;

    RtlInitEmptyUnicodeString(Name, NULL, 0);

    Status = RtlUTF8ToUnicodeN(NULL, 0, &Bytes, Utf8, Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_SOME_NOT_MAPPED) return Status;
    if (Bytes == 0 || Bytes > MAXUSHORT - sizeof(WCHAR)) return STATUS_OBJECT_NAME_INVALID;

    Buffer = ExAllocatePoolUninitialized(PagedPool, Bytes + sizeof(WCHAR), TAG_EXT4);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlUTF8ToUnicodeN(Buffer, Bytes, &Bytes, Utf8, Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_SOME_NOT_MAPPED)
    {
        ExFreePoolWithTag(Buffer, TAG_EXT4);
        return Status;
    }

    Buffer[Bytes / sizeof(WCHAR)] = UNICODE_NULL;
    Name->Buffer = Buffer;
    Name->Length = (USHORT)Bytes;
    Name->MaximumLength = (USHORT)(Bytes + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

NTSTATUS
Ext4UnicodeToUtf8(
    _In_ PCUNICODE_STRING Name,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG Length)
{
    NTSTATUS Status;

    *Length = 0;
    Status = RtlUnicodeToUTF8N(Buffer, BufferSize, Length, Name->Buffer, Name->Length);
    if (Status == STATUS_SOME_NOT_MAPPED) return STATUS_OBJECT_NAME_INVALID;
    return Status;
}

typedef struct _EXT4_CASE_SEARCH
{
    PCUNICODE_STRING Wanted;
    ULONG Inode;
    UCHAR Type;
    ULONG NameLength;
    CHAR Name[EXT4_NAME_BYTES];
    BOOLEAN Found;
} EXT4_CASE_SEARCH, *PEXT4_CASE_SEARCH;

static
bool
Ext4CaseSearchEntry(
    void *Context,
    const char *Name,
    uint32_t NameLength,
    uint32_t Inode,
    uint8_t Type,
    uint64_t Offset)
{
    PEXT4_CASE_SEARCH Search = Context;
    WCHAR Buffer[EXT4_NAME_BYTES + 1];
    UNICODE_STRING Candidate;
    ULONG Bytes = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Offset);

    if (NameLength == 0 || NameLength > EXT4_NAME_BYTES) return true;
    Status = RtlUTF8ToUnicodeN(Buffer, EXT4_NAME_BYTES * sizeof(WCHAR), &Bytes, Name, NameLength);
    if (!NT_SUCCESS(Status) && Status != STATUS_SOME_NOT_MAPPED) return true;

    Candidate.Buffer = Buffer;
    Candidate.Length = Candidate.MaximumLength = (USHORT)Bytes;
    if (!RtlEqualUnicodeString(&Candidate, Search->Wanted, TRUE)) return true;

    Search->Inode = Inode;
    Search->Type = Type;
    Search->NameLength = NameLength;
    RtlCopyMemory(Search->Name, Name, NameLength);
    Search->Found = TRUE;
    return false;
}

/* FIXME: a case-insensitive miss reads the whole directory; filling a large directory through Win32 names is quadratic */
NTSTATUS
Ext4LookupName(
    _In_ PEXT4_VCB Vcb,
    _In_ ULONG Directory,
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN IgnoreCase,
    _Out_ PULONG Inode,
    _Out_ PUCHAR Type,
    _Out_writes_bytes_(EXT4_NAME_BYTES) PCHAR DiskName,
    _Out_ PULONG DiskNameLength)
{
    CHAR Utf8[EXT4_NAME_BYTES * 4];
    EXT4_CASE_SEARCH Search;
    ULONGLONG Offset = 0;
    uint32_t Found = 0;
    uint8_t FoundType = 0;
    NTSTATUS Status;
    ULONG Length;
    int Error;

    *Inode = 0;
    *Type = 0;
    *DiskNameLength = 0;

    Status = Ext4UnicodeToUtf8(Name, Utf8, sizeof(Utf8), &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || (NT_SUCCESS(Status) && Length > EXT4_NAME_BYTES)) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (!NT_SUCCESS(Status)) return Status;
    if (Length == 0) return STATUS_OBJECT_NAME_INVALID;

    Ext4LockCore(Vcb);
    Error = E4Lookup(&Vcb->Core, Directory, Utf8, Length, &Found, &FoundType);
    if (Error == EOK)
    {
        *Inode = Found;
        *Type = FoundType;
        *DiskNameLength = Length;
        RtlCopyMemory(DiskName, Utf8, Length);
    }
    else if (Error == ENOENT && IgnoreCase)
    {
        Search.Wanted = Name;
        Search.Found = FALSE;
        Error = E4ReadDir(&Vcb->Core, Directory, &Offset, Ext4CaseSearchEntry, &Search);
        if (Error == EOK)
        {
            if (!Search.Found)
            {
                Error = ENOENT;
            }
            else
            {
                *Inode = Search.Inode;
                *Type = Search.Type;
                *DiskNameLength = Search.NameLength;
                RtlCopyMemory(DiskName, Search.Name, Search.NameLength);
                if (*Type == EXT4_DE_UNKNOWN)
                {
                    E4_ATTR Attr;

                    Error = E4GetAttr(&Vcb->Core, Search.Inode, &Attr);
                    if (Error == EOK)
                        *Type = ((Attr.Mode & E4_S_IFMT) == E4_S_IFDIR) ? EXT4_DE_DIR : EXT4_DE_REG_FILE;
                }
            }
        }
    }
    Status = Ext4Status(Vcb, Error);
    Ext4UnlockCore(Vcb);
    return Status;
}

LARGE_INTEGER
Ext4TimeToNt(
    _In_ const E4_TIME *Time)
{
    LARGE_INTEGER Result;
    LONGLONG Limit = (MAXLONGLONG - EXT4_UNIX_EPOCH) / 10000000 - 1;

    if (Time->Seconds < -(EXT4_UNIX_EPOCH / 10000000) || Time->Seconds > Limit)
        Result.QuadPart = (Time->Seconds < 0) ? 0 : MAXLONGLONG;
    else
        Result.QuadPart = Time->Seconds * 10000000 + Time->Nanoseconds / 100 + EXT4_UNIX_EPOCH;
    return Result;
}

VOID
Ext4TimeFromNt(
    _In_ LARGE_INTEGER Time,
    _Out_ E4_TIME *Result)
{
    LONGLONG Ticks = Time.QuadPart - EXT4_UNIX_EPOCH;
    LONGLONG Seconds = Ticks / 10000000;
    LONGLONG Rest = Ticks % 10000000;

    if (Rest < 0)
    {
        Seconds--;
        Rest += 10000000;
    }
    Result->Seconds = Seconds;
    Result->Nanoseconds = (ULONG)Rest * 100;
}

VOID
Ext4CurrentTime(
    _Out_ E4_TIME *Time)
{
    LARGE_INTEGER Now;

    KeQuerySystemTime(&Now);
    Ext4TimeFromNt(Now, Time);
}

ULONG
Ext4FileAttributes(
    _In_ const E4_ATTR *Attr,
    _In_reads_bytes_opt_(NameLength) const CHAR *Name,
    _In_ ULONG NameLength,
    _Out_opt_ PULONG ReparseTag)
{
    ULONG Attributes = 0, Tag = 0;

    switch (Attr->Mode & E4_S_IFMT)
    {
        case E4_S_IFDIR: Attributes = FILE_ATTRIBUTE_DIRECTORY; break;
        case E4_S_IFLNK: Tag = IO_REPARSE_TAG_LX_SYMLINK; break;
        case E4_S_IFIFO: Tag = IO_REPARSE_TAG_LX_FIFO; break;
        case E4_S_IFCHR: Tag = IO_REPARSE_TAG_LX_CHR; break;
        case E4_S_IFBLK: Tag = IO_REPARSE_TAG_LX_BLK; break;
        case E4_S_IFSOCK: Tag = IO_REPARSE_TAG_AF_UNIX; break;
        default: Attributes = FILE_ATTRIBUTE_ARCHIVE; break;
    }

    /* The system attribute is how the Linux subsystem tells a special file from a regular one before it asks for the mode */
    if (Tag != 0) Attributes |= FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SYSTEM;
    if ((Attr->Mode & E4_S_IFMT) == E4_S_IFREG && (Attr->Mode & 0222) == 0) Attributes |= FILE_ATTRIBUTE_READONLY;
    if (Name != NULL && NameLength > 1 && Name[0] == '.' && !(NameLength == 2 && Name[1] == '.'))
        Attributes |= FILE_ATTRIBUTE_HIDDEN;
    if (ReparseTag != NULL) *ReparseTag = Tag;
    return Attributes;
}

static
PEXT4_FCB
Ext4AllocateFcb(
    _In_ PEXT4_VCB Vcb)
{
    PEXT4_FCB Fcb = ExAllocatePoolZero(NonPagedPoolNx, sizeof(EXT4_FCB), TAG_EXT4_FCB);

    if (Fcb == NULL) return NULL;

    Fcb->Signature = EXT4_FCB_SIGNATURE;
    Fcb->Vcb = Vcb;
    Fcb->References = 1;
    ExInitializeFastMutex(&Fcb->HeaderMutex);
    ExInitializeResourceLite(&Fcb->MainResource);
    ExInitializeResourceLite(&Fcb->PagingIoResource);
    FsRtlSetupAdvancedHeader(&Fcb->Header, &Fcb->HeaderMutex);
    Fcb->Header.NodeTypeCode = (CSHORT)0xE4F1;
    Fcb->Header.NodeByteSize = sizeof(EXT4_FCB);
    Fcb->Header.Resource = &Fcb->MainResource;
    Fcb->Header.PagingIoResource = &Fcb->PagingIoResource;
    Fcb->Header.IsFastIoPossible = FastIoIsPossible;
    FsRtlInitializeFileLock(&Fcb->FileLock, NULL, NULL);
    return Fcb;
}

static
VOID
Ext4FreeFcb(
    _In_ PEXT4_FCB Fcb)
{
    ASSERT(Fcb->References == 0);
    ASSERT(Fcb->OpenHandles == 0);
    ASSERT(Fcb->SectionObjectPointers.DataSectionObject == NULL);
    ASSERT(Fcb->SectionObjectPointers.SharedCacheMap == NULL);

    FsRtlTeardownPerStreamContexts(&Fcb->Header);
    FsRtlUninitializeFileLock(&Fcb->FileLock);
    ExDeleteResourceLite(&Fcb->MainResource);
    ExDeleteResourceLite(&Fcb->PagingIoResource);
    Fcb->Signature = 0;
    ExFreePoolWithTag(Fcb, TAG_EXT4_FCB);
}

PEXT4_FCB
Ext4CreateVolumeFcb(
    _In_ PEXT4_VCB Vcb)
{
    PEXT4_FCB Fcb = Ext4AllocateFcb(Vcb);

    if (Fcb == NULL) return NULL;

    Fcb->Volume = TRUE;
    Fcb->Header.AllocationSize.QuadPart = (LONGLONG)Vcb->PartitionLength;
    Fcb->Header.FileSize.QuadPart = (LONGLONG)Vcb->PartitionLength;
    Fcb->Header.ValidDataLength.QuadPart = (LONGLONG)Vcb->PartitionLength;
    return Fcb;
}

static
VOID
Ext4ApplyAttr(
    _Inout_ PEXT4_FCB Fcb,
    _In_ const E4_ATTR *Attr)
{
    ULONG Type = Attr->Mode & E4_S_IFMT;

    Fcb->Attr = *Attr;
    Fcb->Directory = (Type == E4_S_IFDIR);
    Fcb->Symlink = (Type == E4_S_IFLNK);
    Ext4FileAttributes(Attr, NULL, 0, &Fcb->ReparseTag);
}

PEXT4_FCB
Ext4FindFcb(
    _In_ PEXT4_VCB Vcb,
    _In_ ULONG Inode)
{
    PLIST_ENTRY Head = &Vcb->FcbTable[Inode % EXT4_FCB_BUCKETS];
    PLIST_ENTRY Entry;

    ASSERT(ExIsResourceAcquiredSharedLite(&Vcb->Resource));

    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        PEXT4_FCB Candidate = CONTAINING_RECORD(Entry, EXT4_FCB, TableLinks);

        if (Candidate->Inode == Inode) return Candidate;
    }
    return NULL;
}

NTSTATUS
Ext4GetFcb(
    _In_ PEXT4_VCB Vcb,
    _In_ ULONG Inode,
    _Out_ PEXT4_FCB *Fcb)
{
    PEXT4_FCB New;
    E4_ATTR Attr;
    NTSTATUS Status;
    ULONG Type;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->Resource));
    *Fcb = Ext4FindFcb(Vcb, Inode);
    if (*Fcb != NULL)
    {
        Ext4ReferenceFcb(*Fcb);
        return STATUS_SUCCESS;
    }

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4GetAttr(&Vcb->Core, Inode, &Attr));
    Ext4UnlockCore(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    /* An inode without a type or without a name is not a file: a stale file id leads here */
    Type = Attr.Mode & E4_S_IFMT;
    if (Type == 0 || Attr.Links == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (Attr.Size > (ULONGLONG)MAXLONGLONG - Vcb->BlockSize) return STATUS_FILE_CORRUPT_ERROR;

    New = Ext4AllocateFcb(Vcb);
    if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    New->Inode = Inode;
    Ext4ApplyAttr(New, &Attr);
    if (!New->Directory)
    {
        New->Header.FileSize.QuadPart = (LONGLONG)Attr.Size;
        New->Header.ValidDataLength.QuadPart = (LONGLONG)Attr.Size;
        New->Header.AllocationSize.QuadPart = (LONGLONG)ALIGN_UP_BY(Attr.Size, (ULONGLONG)Vcb->BlockSize);
    }

    InsertTailList(&Vcb->FcbTable[Inode % EXT4_FCB_BUCKETS], &New->TableLinks);
    Ext4ReferenceVcb(Vcb);
    *Fcb = New;
    return STATUS_SUCCESS;
}

NTSTATUS
Ext4RefreshFcb(
    _In_ PEXT4_FCB Fcb)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    E4_ATTR Attr;
    NTSTATUS Status;

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4GetAttr(&Vcb->Core, Fcb->Inode, &Attr));
    Ext4UnlockCore(Vcb);
    if (!NT_SUCCESS(Status)) return Status;

    /* The cached size and times are ahead of the inode while a write waits to be recorded */
    Attr.Size = Fcb->Attr.Size;
    if (Fcb->TimesDirty)
    {
        Attr.Modify = Fcb->Attr.Modify;
        Attr.Change = Fcb->Attr.Change;
    }
    Ext4ApplyAttr(Fcb, &Attr);
    return STATUS_SUCCESS;
}

NTSTATUS
Ext4WriteTimes(
    _In_ PEXT4_FCB Fcb)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    NTSTATUS Status;

    if (!Fcb->TimesDirty || Fcb->Orphaned || !Vcb->Writable) return STATUS_SUCCESS;

    Ext4LockCore(Vcb);
    Status = Ext4Status(Vcb, E4SetAttr(&Vcb->Core, Fcb->Inode, E4_SET_MODIFY | E4_SET_CHANGE, &Fcb->Attr));
    Ext4UnlockCore(Vcb);
    if (NT_SUCCESS(Status))
    {
        Fcb->TimesDirty = FALSE;
        Ext4MarkCoreDirty(Vcb);
    }
    return Status;
}

VOID
Ext4ReferenceFcb(
    _In_ PEXT4_FCB Fcb)
{
    LONG References;

    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);
    References = InterlockedIncrement(&Fcb->References);
    ASSERT(References > 1);
    UNREFERENCED_PARAMETER(References);
}

/* Called without the volume resource, or with it held exclusively */
VOID
Ext4DereferenceFcb(
    _In_ PEXT4_FCB Fcb)
{
    PEXT4_VCB Vcb = Fcb->Vcb;
    BOOLEAN Volume = Fcb->Volume;
    BOOLEAN Orphaned;
    ULONG Inode = Fcb->Inode;
    LONG References;

    ASSERT(Fcb->Signature == EXT4_FCB_SIGNATURE);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->Resource, TRUE);
    References = InterlockedDecrement(&Fcb->References);
    ASSERT(References >= 0);
    if (References != 0)
    {
        ExReleaseResourceLite(&Vcb->Resource);
        KeLeaveCriticalRegion();
        return;
    }

    Orphaned = Fcb->Orphaned;
    if (!Volume)
    {
        RemoveEntryList(&Fcb->TableLinks);
        if (Vcb->RootFcb == Fcb) Vcb->RootFcb = NULL;
    }
    else if (Vcb->VolumeFcb == Fcb)
    {
        Vcb->VolumeFcb = NULL;
    }

    /* The inode number stays taken until the release below, so no new file can appear under it meanwhile */
    if (Orphaned && Vcb->CoreMounted && Vcb->Writable)
    {
        int Error;

        Ext4LockCore(Vcb);
        Error = E4Release(&Vcb->Core, Inode);
        Ext4UnlockCore(Vcb);
        if (Error != EOK) DPRINT1("ext4: inode %lu could not be released (%d)\n", Inode, Error);
        Ext4MarkCoreDirty(Vcb);
    }
    ExReleaseResourceLite(&Vcb->Resource);
    KeLeaveCriticalRegion();

    Ext4FreeFcb(Fcb);
    Ext4DereferenceVcb(Vcb);
}
