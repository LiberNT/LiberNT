/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS core environment and operations for the Linux kernel glue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntfslib_new.h>
#include <ntfslib_new_internal.h>
#include "ntfslib_k.h"

#define NK_ENOENT 2
#define NK_EIO 5
#define NK_ENOMEM 12
#define NK_EACCES 13
#define NK_EEXIST 17
#define NK_ENOTDIR 20
#define NK_EISDIR 21
#define NK_EINVAL 22
#define NK_EFBIG 27
#define NK_ENOSPC 28
#define NK_EROFS 30
#define NK_ENAMETOOLONG 36
#define NK_ENOTEMPTY 39
#define NK_EILSEQ 84
#define NK_EOPNOTSUPP 95
#define NK_ESTALE 116

#define NK_MAX_NAME 255
#define NK_MAX_PATH 32768

struct NtfsFastRun
{
    ULONGLONG Start;
    ULONGLONG End;
    ULONGLONG Disk;
};

#define NK_TOUCHED 4

struct NkTouched
{
    nk_u64 Reference;
    nk_u64 Time;
};

struct nk_volume
{
    PNtfsVolume Volume;
    PNtfsMasterFileTable Mft;
    /* The directory of the last operation: still current while nothing else wrote to the device. */
    PNtfsFileRecord Directory;
    nk_u64 DirectoryGeneration;
    /* Directories whose change times are still to be stored: one write when they leave this list, not one per entry. */
    NkTouched Touched[NK_TOUCHED];
    /* Likewise the file of the last read or write. */
    PNtfsFileRecord File;
    nk_u64 FileGeneration;
    ULONG ClusterBytes;
    bool Mappable;
    /* Where the file records live, for reading them ahead: runs of $MFT as byte ranges. */
    NtfsFastRun* RecordRuns;
    ULONG RecordRunCount;
    ULONG RecordBytes;
    nk_u64 RecordRunsGeneration;
    PWCHAR OldPath;
    PWCHAR NewPath;
};

static nk_u64 NkWrites;

BOOLEAN
NtfsIsNameInExpressionFallback(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCHAR UpcaseTable);

extern "C" {

/* The core only folds case through the volume's own table; this covers the plain cases left. */
unsigned int
towupper(unsigned int Character)
{
    return Character >= 'a' && Character <= 'z' ? Character - 'a' + 'A' : Character;
}

void*
NtfsAllocatePoolWithTag(POOL_TYPE PoolType, size_t Size, ULONG Tag)
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(Tag);
    return nk_alloc(Size ? Size : 1);
}

void
NtfsFreePool(void* Object)
{
    nk_free(Object);
}

/* The glue holds one lock around every call into the core. */
void
NtfsInitializeMutex(PFAST_MUTEX Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

void
NtfsAcquireMutex(PFAST_MUTEX Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

void
NtfsReleaseMutex(PFAST_MUTEX Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

void*
NtfsReadPointerAcquire(void* const volatile* Source)
{
    return __atomic_load_n(Source, __ATOMIC_ACQUIRE);
}

void
NtfsWritePointerRelease(void* volatile* Destination, void* Value)
{
    __atomic_store_n(Destination, Value, __ATOMIC_RELEASE);
}

BOOLEAN
NtfsIsNameInExpression(PUNICODE_STRING Expression, PUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCHAR UpcaseTable)
{
    return NtfsIsNameInExpressionFallback(Expression, Name, IgnoreCase, UpcaseTable);
}

NTSTATUS
NtfsGenerate8dot3Name(PCUNICODE_STRING Name, PGENERATE_NAME_CONTEXT Context, PUNICODE_STRING ShortName)
{
    return NtfsGenerate8dot3NameFallback(Name, Context, ShortName);
}

NTSTATUS
NtfsQuerySystemTime(PULONGLONG NtfsTime)
{
    *NtfsTime = nk_time_now();
    return STATUS_SUCCESS;
}

unsigned long long
NtfsQueryTicks(void)
{
    return 0;
}

NTSTATUS
NtfsReadVolumeContext(void* Context, ULONG BytesPerSector, ULONGLONG Offset, ULONG Length, PUCHAR Buffer)
{
    UNREFERENCED_PARAMETER(BytesPerSector);
    return nk_dev_read(Context, Offset, Length, Buffer) == 0 ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS
NtfsWriteVolumeContext(void* Context, ULONG BytesPerSector, ULONGLONG Offset, ULONG Length, PUCHAR Buffer)
{
    UNREFERENCED_PARAMETER(BytesPerSector);
    NkWrites++;
    return nk_dev_write(Context, Offset, Length, Buffer) == 0 ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS
NtfsFlushVolumeContext(void* Context)
{
    return nk_dev_flush(Context) == 0 ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
}

void
__cxa_pure_virtual(void)
{
    __builtin_trap();
}

}

static int
ErrnoOf(NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
        return 0;
    switch (Status)
    {
        case STATUS_NOT_FOUND:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:
            return -NK_ENOENT;
        case STATUS_OBJECT_NAME_COLLISION:
            return -NK_EEXIST;
        case STATUS_DISK_FULL:
            return -NK_ENOSPC;
        case STATUS_INSUFFICIENT_RESOURCES:
            return -NK_ENOMEM;
        case STATUS_DIRECTORY_NOT_EMPTY:
            return -NK_ENOTEMPTY;
        case STATUS_NOT_A_DIRECTORY:
            return -NK_ENOTDIR;
        case STATUS_FILE_IS_A_DIRECTORY:
            return -NK_EISDIR;
        case STATUS_ACCESS_DENIED:
            return -NK_EACCES;
        case STATUS_MEDIA_WRITE_PROTECTED:
            return -NK_EROFS;
        case STATUS_NAME_TOO_LONG:
            return -NK_ENAMETOOLONG;
        case STATUS_OBJECT_NAME_INVALID:
            return -NK_EILSEQ;
        case STATUS_FILE_TOO_LARGE:
            return -NK_EFBIG;
        case STATUS_INVALID_PARAMETER:
            return -NK_EINVAL;
        case STATUS_NOT_IMPLEMENTED:
            return -NK_EOPNOTSUPP;
        default:
            return -NK_EIO;
    }
}

/* UTF-8 to UTF-16; returns the number of code units or 0 for a malformed or oversized name. */
static ULONG
NameToNtfs(const char* Name, nk_u32 Length, PWCHAR Out, ULONG Capacity)
{
    const unsigned char* In = (const unsigned char*)Name;
    ULONG Used = 0;

    for (nk_u32 Index = 0; Index < Length;)
    {
        ULONG Code = In[Index];
        unsigned Extra = Code < 0x80 ? 0 : (Code & 0xE0) == 0xC0 ? 1 : (Code & 0xF0) == 0xE0 ? 2 : (Code & 0xF8) == 0xF0 ? 3 : 4;

        if (Extra == 4 || Index + Extra >= Length + (Extra == 0))
            return 0;
        if (Extra)
            Code &= 0x3F >> Extra;
        for (unsigned Step = 1; Step <= Extra; Step++)
        {
            if ((In[Index + Step] & 0xC0) != 0x80)
                return 0;
            Code = (Code << 6) | (In[Index + Step] & 0x3F);
        }
        Index += Extra + 1;
        if (Code == 0 || Code > 0x10FFFF || (Code >= 0xD800 && Code < 0xE000) ||
            Used + (Code >= 0x10000 ? 2 : 1) > Capacity)
        {
            return 0;
        }
        if (Code >= 0x10000)
        {
            Code -= 0x10000;
            Out[Used++] = (WCHAR)(0xD800 | (Code >> 10));
            Out[Used++] = (WCHAR)(0xDC00 | (Code & 0x3FF));
        }
        else
            Out[Used++] = (WCHAR)Code;
    }
    return Used;
}

static nk_u32
NameFromNtfs(const WCHAR* In, ULONG Length, char* Out, nk_u32 Capacity)
{
    nk_u32 Used = 0;

    for (ULONG Index = 0; Index < Length; Index++)
    {
        ULONG Code = (USHORT)In[Index];

        if (Code >= 0xD800 && Code < 0xDC00 && Index + 1 < Length && ((USHORT)In[Index + 1] & 0xFC00) == 0xDC00)
            Code = 0x10000 + ((Code - 0xD800) << 10) + ((USHORT)In[++Index] - 0xDC00);
        if (Used + 4 > Capacity)
            return 0;
        if (Code < 0x80)
            Out[Used++] = (char)Code;
        else if (Code < 0x800)
        {
            Out[Used++] = (char)(0xC0 | (Code >> 6));
            Out[Used++] = (char)(0x80 | (Code & 0x3F));
        }
        else if (Code < 0x10000)
        {
            Out[Used++] = (char)(0xE0 | (Code >> 12));
            Out[Used++] = (char)(0x80 | ((Code >> 6) & 0x3F));
            Out[Used++] = (char)(0x80 | (Code & 0x3F));
        }
        else
        {
            Out[Used++] = (char)(0xF0 | (Code >> 18));
            Out[Used++] = (char)(0x80 | ((Code >> 12) & 0x3F));
            Out[Used++] = (char)(0x80 | ((Code >> 6) & 0x3F));
            Out[Used++] = (char)(0x80 | (Code & 0x3F));
        }
    }
    return Used;
}

static void
FillAttr(PNtfsFileRecord File, struct nk_attr* Attr);

static NkTouched*
FindTouched(struct nk_volume* Volume, nk_u64 Reference)
{
    for (ULONG Index = 0; Index < NK_TOUCHED; Index++)
    {
        if (Volume->Touched[Index].Time && Volume->Touched[Index].Reference == Reference)
            return &Volume->Touched[Index];
    }
    return NULL;
}

static void
FillAttr(struct nk_volume* Volume, PNtfsFileRecord File, struct nk_attr* Attr)
{
    FillAttr(File, Attr);
    if (!Volume->Mappable)
        Attr->mapped = Attr->empty = 0;
    if (Attr->directory)
    {
        NkTouched* Slot = FindTouched(Volume, Attr->reference);

        if (Slot)
            Attr->written = Attr->changed = Slot->Time;
    }
}

static const char* const NkOwnerNames[4] = {"$LXUID", "$LXGID", "$LXMOD", "$LXDEV"};

#define NK_LINK_BYTES 4096

static nk_u32
NameFromNtfs(const WCHAR* In, ULONG Length, char* Out, nk_u32 Capacity);

/* The target of a symbolic link or junction as a POSIX path; its length, or -1 when the record is not one. */
static long
LinkTarget(PNtfsFileRecord File, char* Out, nk_u32 Capacity)
{
    NtfsReparseNameView View;
    const UCHAR* Name;
    PUCHAR Raw;
    PWCHAR Wide;
    ULONG Units;
    ULONG RawLength = 0;
    long Length = -1;

    if (NtfsFileRecordReadReparsePoint(File, NULL, &RawLength) != STATUS_BUFFER_TOO_SMALL || !RawLength ||
        RawLength > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return -1;
    }
    Raw = (PUCHAR)nk_alloc(2 * RawLength);
    if (!Raw)
        return -1;
    Wide = (PWCHAR)(Raw + RawLength);
    if (NT_SUCCESS(NtfsFileRecordReadReparsePoint(File, Raw, &RawLength)) &&
        NT_SUCCESS(NtfsParseNameSurrogateReparsePoint(Raw, RawLength, &View)))
    {
        /* The substitute name drives a relative link; an absolute one shows its print name, or loses the NT prefix. */
        Name = View.SubstituteName;
        Units = View.SubstituteNameLength;
        if (!(View.Flags & NTFS_SYMLINK_FLAG_RELATIVE) && View.PrintNameLength)
        {
            Name = View.PrintName;
            Units = View.PrintNameLength;
        }
        memcpy(Wide, Name, Units * sizeof(WCHAR));
        if (Name == View.SubstituteName && Units >= 4 && Wide[0] == L'\\' && Wide[1] == L'?' && Wide[2] == L'?' &&
            Wide[3] == L'\\')
        {
            Wide += 4;
            Units -= 4;
        }
        for (ULONG Index = 0; Index < Units; Index++)
        {
            if (Wide[Index] == L'\\')
                Wide[Index] = L'/';
        }
        Length = (long)NameFromNtfs(Wide, Units, Out, Capacity);
        if (!Length)
            Length = -1;
    }
    nk_free(Raw);
    return Length;
}

/* The ownership, mode and device number kept in the extended attributes Linux ntfs3 and WSL use. */
static void
FillOwner(PNtfsFileRecord File, struct nk_attr* Attr)
{
    NtfsExtendedAttributeView View;
    PAttribute Ea;
    const UCHAR* Data;
    PUCHAR Raw = NULL;
    ULONG Length = 0;
    ULONG Offset = 0;

    Attr->has = 0;
    Attr->uid = Attr->gid = Attr->mode = Attr->device = 0;
    if (!NtfsFileRecordGetAttribute(File, TypeEAInformation, NULL))
        return;
    Ea = NtfsFileRecordGetAttribute(File, TypeEA, NULL);
    if (Ea && !Ea->IsNonResident && Ea->Resident.DataOffset <= Ea->Length &&
        Ea->Resident.DataLength <= Ea->Length - Ea->Resident.DataOffset)
    {
        Data = (const UCHAR*)Ea + Ea->Resident.DataOffset;
        Length = Ea->Resident.DataLength;
    }
    else
    {
        if (NtfsFileRecordReadExtendedAttributes(File, NULL, &Length, NULL) != STATUS_BUFFER_TOO_SMALL || !Length)
            return;
        Raw = (PUCHAR)nk_alloc(Length);
        if (!Raw || !NT_SUCCESS(NtfsFileRecordReadExtendedAttributes(File, Raw, &Length, NULL)))
        {
            nk_free(Raw);
            return;
        }
        Data = Raw;
    }
    while (NT_SUCCESS(NtfsGetNextExtendedAttribute(Data, Length, &Offset, &View)))
    {
        nk_u32* Fields[4] = {&Attr->uid, &Attr->gid, &Attr->mode, &Attr->device};

        if (View.ValueLength != sizeof(nk_u32) || View.NameLength != 6)
            continue;
        for (ULONG Which = 0; Which < 4; Which++)
        {
            ULONG Index = 0;

            while (Index < 6 && (View.Name[Index] & ~0x20) == (NkOwnerNames[Which][Index] & ~0x20) &&
                   (View.Name[Index] == NkOwnerNames[Which][Index] || View.Name[Index] >= 'a'))
            {
                Index++;
            }
            if (Index == 6)
            {
                memcpy(Fields[Which], View.Value, sizeof(nk_u32));
                Attr->has |= 1u << Which;
            }
        }
    }
    nk_free(Raw);
}

static void
FillAttr(PNtfsFileRecord File, struct nk_attr* Attr)
{
    PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    NtfsFileBasicInformation Basic = {};

    Attr->reference = NtfsFileRecordGetReference(File);
    Attr->directory = (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) != 0;
    Attr->links = NtfsFileRecordGetLinkCount(File);
    Attr->size = 0;
    Attr->allocated = 0;
    Attr->mapped = 0;
    Attr->empty = 0;
    Attr->valid = 0;
    if (Data && !Attr->directory)
    {
        bool Plain = !Data->Flags && !NtfsFileRecordGetAttribute(File, TypeReparsePoint, NULL);

        Attr->size = Data->IsNonResident ? Data->NonResident.DataSize : Data->Resident.DataLength;
        Attr->allocated = Data->IsNonResident ? Data->NonResident.AllocatedSize : (Attr->size + 7) & ~7ull;
        Attr->valid = Data->IsNonResident ? Data->NonResident.InitalizedDataSize : Attr->size;
        Attr->mapped = Plain && Data->IsNonResident;
        Attr->empty = Plain && !Data->IsNonResident && Attr->size == 0;
    }
    (void)NtfsFileRecordGetBasicInformation(File, &Basic);
    Attr->created = Basic.CreationTime;
    Attr->accessed = Basic.LastAccessTime;
    Attr->written = Basic.LastWriteTime;
    Attr->changed = Basic.ChangeTime;
    FillOwner(File, Attr);
    if (NtfsFileRecordGetAttribute(File, TypeReparsePoint, NULL))
    {
        /* A symbolic link or junction, also one Windows made: a link whose size is the length of its target. */
        char* Target = (char*)nk_alloc(NK_LINK_BYTES);
        long Length = Target ? LinkTarget(File, Target, NK_LINK_BYTES) : -1;

        nk_free(Target);
        if (Length >= 0)
        {
            Attr->has |= NK_HAS_MODE;
            Attr->mode = 0120777;
            Attr->size = (nk_u64)Length;
            Attr->mapped = Attr->empty = 0;
        }
    }
}

static void
DropDirectory(struct nk_volume* Volume)
{
    NtfsFileRecordDestroy(Volume->Directory);
    Volume->Directory = NULL;
}

/* Through a fresh record; the write moves the generation, so a cached record of the same directory is reloaded. */
static void
StoreTouched(struct nk_volume* Volume, NkTouched* Slot)
{
    PNtfsFileRecord Fresh = NULL;
    NtfsFileBasicInformation Basic = {};

    if (!Slot || !Slot->Time)
        return;
    Basic.Fields = NTFS_BASIC_INFO_LAST_WRITE_TIME | NTFS_BASIC_INFO_CHANGE_TIME;
    Basic.LastWriteTime = Basic.ChangeTime = Slot->Time;
    Slot->Time = 0;
    if (NT_SUCCESS(NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Slot->Reference, &Fresh)))
        (void)NtfsFileRecordSetBasicInformation(Fresh, &Basic);
    NtfsFileRecordDestroy(Fresh);
}

static void
StoreAllTouched(struct nk_volume* Volume)
{
    DropDirectory(Volume);
    for (ULONG Index = 0; Index < NK_TOUCHED; Index++)
        StoreTouched(Volume, &Volume->Touched[Index]);
}

static void
NoteTouched(struct nk_volume* Volume, nk_u64 Reference)
{
    NkTouched* Slot = FindTouched(Volume, Reference);

    if (!Slot)
    {
        Slot = &Volume->Touched[0];
        for (ULONG Index = 1; Index < NK_TOUCHED; Index++)
        {
            if (Volume->Touched[Index].Time < Slot->Time)
                Slot = &Volume->Touched[Index];
        }
        StoreTouched(Volume, Slot);
        Slot->Reference = Reference;
    }
    Slot->Time = nk_time_now();
}

/* The record of a directory, kept across calls for as long as only operations through it wrote. */
static NTSTATUS
GetDirectory(struct nk_volume* Volume, nk_u64 Reference, PNtfsFileRecord* Directory)
{
    NTSTATUS Status;

    if (Volume->Directory && NtfsFileRecordGetReference(Volume->Directory) == Reference &&
        Volume->DirectoryGeneration == NkWrites)
    {
        *Directory = Volume->Directory;
        return STATUS_SUCCESS;
    }
    DropDirectory(Volume);
    Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Reference, &Volume->Directory);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!(NtfsFileRecordGetHeader(Volume->Directory)->Flags & FR_IS_DIRECTORY))
    {
        DropDirectory(Volume);
        return STATUS_NOT_A_DIRECTORY;
    }
    Volume->DirectoryGeneration = NkWrites;
    (void)NtfsFileRecordSetAutomaticTimestampMask(Volume->Directory, 0);
    (void)NtfsFileRecordDeferDirectoryTimes(Volume->Directory, TRUE);
    *Directory = Volume->Directory;
    return STATUS_SUCCESS;
}

static NTSTATUS
GetFile(struct nk_volume* Volume, nk_u64 Reference, PNtfsFileRecord* File)
{
    NTSTATUS Status;

    if (Volume->File && NtfsFileRecordGetReference(Volume->File) == Reference && Volume->FileGeneration == NkWrites)
    {
        *File = Volume->File;
        return STATUS_SUCCESS;
    }
    NtfsFileRecordDestroy(Volume->File);
    Volume->File = NULL;
    Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Reference, &Volume->File);
    Volume->FileGeneration = NkWrites;
    *File = Volume->File;
    return Status;
}

/* An operation changed the directory through its cached record, which therefore stays current. */
static void
DirectoryChanged(struct nk_volume* Volume, NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
    {
        Volume->DirectoryGeneration = NkWrites;
        NoteTouched(Volume, NtfsFileRecordGetReference(Volume->Directory));
    }
    else
        DropDirectory(Volume);
}

extern "C" int
nk_mount(void* Device, nk_u32 SectorBytes, int ReadOnly, struct nk_volume** Out, nk_u64* Root)
{
    struct nk_volume* Volume = (struct nk_volume*)nk_alloc(sizeof(*Volume));
    PNtfsFileRecord RootRecord = NULL;
    NTSTATUS Status;

    if (!Volume)
        return -NK_ENOMEM;
    memset(Volume, 0, sizeof(*Volume));
    NtfsSetGenerate8dot3Names(FALSE);
    NtfsSetReadOnlyMode(ReadOnly ? TRUE : FALSE);
    Volume->OldPath = (PWCHAR)nk_alloc(2 * NK_MAX_PATH * sizeof(WCHAR));
    Status = Volume->OldPath ? NtfsProbePartitionAndOpenVolumeEx(SectorBytes, Device, &Volume->Volume)
                             : STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(Status))
    {
        Volume->NewPath = Volume->OldPath + NK_MAX_PATH;
        Volume->Mft = NtfsVolumeGetMft(Volume->Volume);
        Volume->ClusterBytes = NtfsVolumeGetBytesPerSector(Volume->Volume) * NtfsVolumeGetSectorsPerCluster(Volume->Volume);
        /* Clusters that are whole 4096-byte device blocks can be handed to the block layer as they are. */
        Volume->Mappable = Volume->ClusterBytes % 4096 == 0;
        Status = NtfsMasterFileTableGetFileRecordFromQuery(Volume->Mft, const_cast<PWCHAR>(L"\\"), &RootRecord);
    }
    if (!NT_SUCCESS(Status))
    {
        nk_note("volume open status", (long)(ULONG)Status);
        NtfsVolumeDestroy(Volume->Volume);
        nk_free(Volume->OldPath);
        nk_free(Volume);
        return ErrnoOf(Status);
    }
    *Root = NtfsFileRecordGetReference(RootRecord);
    NtfsFileRecordDestroy(RootRecord);
    *Out = Volume;
    return 0;
}

#define NK_RECORD_RUNS 256

static void
LoadRecordRuns(struct nk_volume* Volume)
{
    PNtfsFileRecord Table = NULL;
    NtfsVolumeInformation Information;
    ULONGLONG Vcn = 0;

    Volume->RecordRunCount = 0;
    Volume->RecordRunsGeneration = NkWrites;
    if (!Volume->RecordRuns)
        Volume->RecordRuns = (NtfsFastRun*)nk_alloc(NK_RECORD_RUNS * sizeof(NtfsFastRun));
    if (!Volume->RecordRuns || !NT_SUCCESS(NtfsVolumeQueryInformation(Volume->Volume, &Information)) ||
        !NT_SUCCESS(NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, UINT64_C(1) << 48, &Table)))
    {
        return;
    }
    Volume->RecordBytes = Information.BytesPerFileRecordSegment;
    while (Volume->RecordRunCount < NK_RECORD_RUNS)
    {
        NtfsRetrievalExtent Extent;
        ULONGLONG Start;
        ULONG Count = 1;
        NTSTATUS Status = NtfsFileRecordQueryRetrievalPointers(Table, TypeData, NULL, Vcn, &Start, &Extent, &Count);

        if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) || Count != 1 || Extent.Lcn < 0 || Extent.NextVcn <= Vcn)
            break;
        Volume->RecordRuns[Volume->RecordRunCount++] = {Start * Volume->ClusterBytes, Extent.NextVcn * Volume->ClusterBytes,
                                                        (ULONGLONG)Extent.Lcn * Volume->ClusterBytes};
        Vcn = Extent.NextVcn;
    }
    NtfsFileRecordDestroy(Table);
}

extern "C" nk_u64
nk_record_offset(struct nk_volume* Volume, nk_u64 Reference)
{
    for (unsigned Attempt = 0; Attempt < 2; Attempt++)
    {
        ULONGLONG Offset = (Reference & UINT64_C(0x0000FFFFFFFFFFFF)) * Volume->RecordBytes;

        for (ULONG Index = 0; Volume->RecordBytes && Index < Volume->RecordRunCount; Index++)
        {
            if (Offset >= Volume->RecordRuns[Index].Start && Offset < Volume->RecordRuns[Index].End)
                return Volume->RecordRuns[Index].Disk + (Offset - Volume->RecordRuns[Index].Start);
        }
        /* The table may have grown since its runs were read. */
        if (Attempt || (Volume->RecordRuns && Volume->RecordRunsGeneration == NkWrites))
            break;
        LoadRecordRuns(Volume);
    }
    return 0;
}

extern "C" void
nk_unmount(struct nk_volume* Volume)
{
    nk_free(Volume->RecordRuns);
    StoreAllTouched(Volume);
    NtfsFileRecordDestroy(Volume->File);
    NtfsVolumeDestroy(Volume->Volume);
    nk_free(Volume->OldPath);
    nk_free(Volume);
}

extern "C" long
nk_readlink(struct nk_volume* Volume, nk_u64 Reference, char* Target, nk_u32 Capacity)
{
    PNtfsFileRecord File = NULL;
    NTSTATUS Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Reference, &File);
    long Length;

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    Length = LinkTarget(File, Target, Capacity);
    NtfsFileRecordDestroy(File);
    return Length < 0 ? -NK_EINVAL : Length;
}

extern "C" int
nk_set_owner(struct nk_volume* Volume, nk_u64 Reference, nk_u32 Uid, nk_u32 Gid, nk_u32 Mode)
{
    const nk_u32* Values[3] = {&Uid, &Gid, &Mode};
    NtfsExtendedAttributeUpdate Updates[3];
    PNtfsFileRecord File = NULL;
    NTSTATUS Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Reference, &File);

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    for (ULONG Which = 0; Which < 3; Which++)
    {
        Updates[Which] = {};
        Updates[Which].Operation = NTFS_EA_UPDATE_SET;
        Updates[Which].NameLength = 6;
        Updates[Which].ValueLength = sizeof(nk_u32);
        Updates[Which].Name = (const UCHAR*)NkOwnerNames[Which];
        Updates[Which].Value = (const UCHAR*)Values[Which];
    }
    Status = NtfsFileRecordUpdateExtendedAttributes(File, Updates, 3);
    NtfsFileRecordDestroy(File);
    return ErrnoOf(Status);
}

/* The change time a directory has pending, for the caller to show the same one; zero when it has none. */
extern "C" nk_u64
nk_dir_time(struct nk_volume* Volume, nk_u64 Reference)
{
    NkTouched* Slot = FindTouched(Volume, Reference);

    return Slot ? Slot->Time : 0;
}

/* Whatever the shim holds back reaches the device. */
extern "C" void
nk_settle(struct nk_volume* Volume)
{
    StoreAllTouched(Volume);
}

extern "C" int
nk_space(struct nk_volume* Volume, struct nk_space* Space)
{
    NtfsVolumeInformation Information;
    NTSTATUS Status = NtfsVolumeQueryInformation(Volume->Volume, &Information);

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    Space->clusters = Information.TotalClusters;
    Space->free_clusters = Information.FreeClusters;
    Space->cluster_bytes = Information.BytesPerCluster;
    return 0;
}

extern "C" int
nk_getattr(struct nk_volume* Volume, nk_u64 Reference, struct nk_attr* Attr)
{
    PNtfsFileRecord File = NULL;
    NTSTATUS Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Reference, &File);

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    FillAttr(Volume, File, Attr);
    NtfsFileRecordDestroy(File);
    return 0;
}

extern "C" int
nk_lookup(struct nk_volume* Volume, nk_u64 DirectoryReference, const char* Name, nk_u32 Length, struct nk_attr* Child)
{
    WCHAR Wide[NK_MAX_NAME + 1];
    ULONG WideLength = NameToNtfs(Name, Length, Wide, NK_MAX_NAME);
    PNtfsFileRecord Directory;
    PNtfsFileRecord File = NULL;
    UNICODE_STRING Requested;
    UNICODE_STRING Stored;
    NTSTATUS Status;

    if (!WideLength)
        return -NK_ENOENT;
    Status = GetDirectory(Volume, DirectoryReference, &Directory);
    if (NT_SUCCESS(Status))
        Status = NtfsMasterFileTableGetFileRecordInDirectory(Volume->Mft, Directory, Wide, WideLength, &File);
    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    /* The index compares without case; a name here is the one stored, byte for byte. */
    Requested.Buffer = Wide;
    Requested.Length = (USHORT)(WideLength * sizeof(WCHAR));
    Requested.MaximumLength = Requested.Length;
    Status = NtfsMasterFileTableGetLinkName(Volume->Mft, File, DirectoryReference, &Requested, &Stored);
    if (!NT_SUCCESS(Status) || Stored.Length != Requested.Length || memcmp(Stored.Buffer, Wide, Requested.Length) != 0)
    {
        NtfsFileRecordDestroy(File);
        return -NK_ENOENT;
    }
    FillAttr(Volume, File, Child);
    NtfsFileRecordDestroy(File);
    return 0;
}

extern "C" int
nk_create(struct nk_volume* Volume, nk_u64 DirectoryReference, const char* Name, nk_u32 Length, int IsDirectory,
          const struct nk_owner* Owner, struct nk_attr* Child)
{
    const nk_u32* Values[4] = {&Owner->uid, &Owner->gid, &Owner->mode, &Owner->device};
    NtfsExtendedAttributeUpdate Updates[4];
    ULONG Count = 0;
    WCHAR Wide[NK_MAX_NAME + 1];
    ULONG WideLength = NameToNtfs(Name, Length, Wide, NK_MAX_NAME);
    PNtfsFileRecord Directory;
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    if (!WideLength)
        return Length > NK_MAX_NAME ? -NK_ENAMETOOLONG : -NK_EILSEQ;
    Status = GetDirectory(Volume, DirectoryReference, &Directory);
    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    for (ULONG Which = 0; Which < 4; Which++)
    {
        if (!(Owner->has & (1u << Which)))
            continue;
        Updates[Count] = {};
        Updates[Count].Operation = NTFS_EA_UPDATE_SET;
        Updates[Count].NameLength = 6;
        Updates[Count].ValueLength = sizeof(nk_u32);
        Updates[Count].Name = (const UCHAR*)NkOwnerNames[Which];
        Updates[Count].Value = (const UCHAR*)Values[Which];
        Count++;
    }
    /*
     * The caller looked the name up first; the index insertion still refuses a duplicate. A FIFO, socket or
     * device is an ordinary record marked as a system file, so that a listing knows not to call it a plain file.
     */
    Status = NtfsMasterFileTableCreateFileWithEas(Volume->Mft, Directory, Wide, WideLength, IsDirectory ? TRUE : FALSE,
                                                  !IsDirectory && (Owner->has & NK_HAS_MODE) &&
                                                          (Owner->mode & 0170000) != 0100000
                                                      ? FILE_PERM_SYSTEM
                                                      : 0,
                                                  TRUE, Count ? Updates : NULL, Count, &File);
    DirectoryChanged(Volume, Status);
    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    FillAttr(Volume, File, Child);
    NtfsFileRecordDestroy(File);
    return 0;
}

extern "C" int
nk_remove(struct nk_volume* Volume, nk_u64 DirectoryReference, const char* Name, nk_u32 Length, nk_u64 ChildReference,
          int IsDirectory, int KeepRecord, nk_u32* LinksLeft)
{
    WCHAR Wide[NK_MAX_NAME + 1];
    ULONG WideLength = NameToNtfs(Name, Length, Wide, NK_MAX_NAME);
    PNtfsFileRecord Directory;
    PNtfsFileRecord File = NULL;
    BOOLEAN Deleted = FALSE;
    NTSTATUS Status;

    if (!WideLength)
        return -NK_ENOENT;
    Status = GetDirectory(Volume, DirectoryReference, &Directory);
    if (NT_SUCCESS(Status))
        Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, ChildReference, &File);
    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    Status = NtfsMasterFileTableDeleteChild(Volume->Mft, Directory, File, Wide, WideLength, IsDirectory ? TRUE : FALSE, NULL,
                                            KeepRecord ? FALSE : TRUE, KeepRecord ? TRUE : FALSE, &Deleted);
    if (NT_SUCCESS(Status) && IsDirectory)
    {
        /* A removed directory has no times left to store. */
        NkTouched* Slot = FindTouched(Volume, ChildReference);

        if (Slot)
            Slot->Time = 0;
    }
    DirectoryChanged(Volume, Status);
    *LinksLeft = NT_SUCCESS(Status) && !Deleted ? NtfsFileRecordGetLinkCount(File) : 0;
    NtfsFileRecordDestroy(File);
    return ErrnoOf(Status);
}

extern "C" int
nk_delete_unlinked(struct nk_volume* Volume, nk_u64 Reference)
{
    DropDirectory(Volume);
    return ErrnoOf(NtfsMasterFileTableDeleteUnlinkedFile(Volume->Mft, Reference, NULL));
}

static NTSTATUS
ChildPath(struct nk_volume* Volume, nk_u64 DirectoryReference, const char* Name, nk_u32 Length, PWCHAR Path, PULONG PathLength)
{
    ULONG Used = 0;
    NTSTATUS Status = NtfsMasterFileTableGetPathFromFileReference(Volume->Mft, DirectoryReference, Path,
                                                                  NK_MAX_PATH - NK_MAX_NAME - 2, &Used);

    if (!NT_SUCCESS(Status))
        return Status;
    if (!Used || Path[Used - 1] != L'\\')
        Path[Used++] = L'\\';
    ULONG NameLength = NameToNtfs(Name, Length, Path + Used, NK_MAX_NAME);
    if (!NameLength)
        return Length > NK_MAX_NAME ? STATUS_NAME_TOO_LONG : STATUS_OBJECT_NAME_INVALID;
    *PathLength = Used + NameLength;
    return STATUS_SUCCESS;
}

/* The caller has already removed whatever the new name pointed at. */
extern "C" int
nk_rename(struct nk_volume* Volume, nk_u64 OldDirectory, const char* OldName, nk_u32 OldLength, nk_u64 NewDirectory,
          const char* NewName, nk_u32 NewLength)
{
    ULONG OldPathLength = 0;
    ULONG NewPathLength = 0;
    NTSTATUS Status;

    StoreAllTouched(Volume);
    Status = ChildPath(Volume, OldDirectory, OldName, OldLength, Volume->OldPath, &OldPathLength);
    if (NT_SUCCESS(Status))
        Status = ChildPath(Volume, NewDirectory, NewName, NewLength, Volume->NewPath, &NewPathLength);
    if (NT_SUCCESS(Status))
        Status = NtfsMasterFileTableRenameFile(Volume->Mft, Volume->OldPath, OldPathLength, Volume->NewPath, NewPathLength);
    return ErrnoOf(Status);
}

extern "C" int
nk_link(struct nk_volume* Volume, nk_u64 Reference, nk_u64 DirectoryReference, const char* Name, nk_u32 Length,
        struct nk_attr* After)
{
    ULONG OldPathLength = 0;
    ULONG NewPathLength = 0;
    NTSTATUS Status;

    StoreAllTouched(Volume);
    Status = NtfsMasterFileTableGetPathFromFileReference(Volume->Mft, Reference, Volume->OldPath, NK_MAX_PATH - 1,
                                                         &OldPathLength);
    if (NT_SUCCESS(Status))
        Status = ChildPath(Volume, DirectoryReference, Name, Length, Volume->NewPath, &NewPathLength);
    if (NT_SUCCESS(Status))
        Status = NtfsMasterFileTableCreateHardLink(Volume->Mft, Volume->OldPath, OldPathLength, Volume->NewPath,
                                                   NewPathLength);
    return NT_SUCCESS(Status) ? nk_getattr(Volume, Reference, After) : ErrnoOf(Status);
}

/* A symbolic link is a symlink reparse point: backslashes between names, and the NT prefix on an absolute target. */
extern "C" int
nk_symlink(struct nk_volume* Volume, nk_u64 DirectoryReference, const char* Name, nk_u32 Length, const char* Target,
           nk_u32 TargetLength, const struct nk_owner* Owner, struct nk_attr* Child)
{
    static const WCHAR Prefix[4] = {L'\\', L'?', L'?', L'\\'};
    const UINT32 Tag = NTFS_IO_REPARSE_TAG_SYMLINK;
    bool Relative = TargetLength && Target[0] != '/';
    ULONG PrefixBytes = Relative ? 0 : sizeof(Prefix);
    UINT32 Flags = Relative ? NTFS_SYMLINK_FLAG_RELATIVE : 0;
    PWCHAR Wide = (PWCHAR)nk_alloc((TargetLength + 1) * sizeof(WCHAR));
    ULONG Units = Wide ? NameToNtfs(Target, TargetLength, Wide, TargetLength + 1) : 0;
    ULONG NameBytes = Units * sizeof(WCHAR);
    ULONG SubstituteBytes = PrefixBytes + NameBytes;
    ULONG PrintBytes = NameBytes;
    PNtfsFileRecord File = NULL;
    PUCHAR Buffer = NULL;
    ULONG Total;
    UINT16 Field;
    NTSTATUS Status;
    int Error;

    /* A target too long for both names keeps only the substitute name. */
    if (8 + 12 + SubstituteBytes + PrintBytes > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
        PrintBytes = 0;
    Total = 8 + 12 + SubstituteBytes + PrintBytes;
    if (Units && Total <= NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
        Buffer = (PUCHAR)nk_alloc(Total);
    if (!Buffer)
    {
        nk_free(Wide);
        return !Wide ? -NK_ENOMEM : !Units ? -NK_EINVAL : Total > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE ? -NK_ENAMETOOLONG : -NK_ENOMEM;
    }
    for (ULONG Index = 0; Index < Units; Index++)
    {
        if (Wide[Index] == L'/')
            Wide[Index] = L'\\';
    }
    memset(Buffer, 0, Total);
    memcpy(Buffer, &Tag, sizeof(Tag));
    Field = (UINT16)(Total - 8);
    memcpy(Buffer + 4, &Field, sizeof(Field));
    Field = (UINT16)SubstituteBytes;
    memcpy(Buffer + 10, &Field, sizeof(Field));
    memcpy(Buffer + 12, &Field, sizeof(Field));
    Field = (UINT16)PrintBytes;
    memcpy(Buffer + 14, &Field, sizeof(Field));
    memcpy(Buffer + 16, &Flags, sizeof(Flags));
    memcpy(Buffer + 20, Prefix, PrefixBytes);
    memcpy(Buffer + 20 + PrefixBytes, Wide, NameBytes);
    memcpy(Buffer + 20 + SubstituteBytes, Wide, PrintBytes);
    nk_free(Wide);

    Error = nk_create(Volume, DirectoryReference, Name, Length, 0, Owner, Child);
    if (Error)
    {
        nk_free(Buffer);
        return Error;
    }
    Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Child->reference, &File);
    if (NT_SUCCESS(Status))
        Status = NtfsFileRecordSetReparsePoint(File, Buffer, Total);
    nk_free(Buffer);
    if (NT_SUCCESS(Status))
    {
        FillAttr(Volume, File, Child);
        NtfsFileRecordDestroy(File);
        return 0;
    }
    NtfsFileRecordDestroy(File);
    /* No link without its target: the name goes again. */
    {
        nk_u32 Left = 0;

        (void)nk_remove(Volume, DirectoryReference, Name, Length, Child->reference, 0, 0, &Left);
    }
    return ErrnoOf(Status);
}

struct NkListing
{
    nk_u64 Skip;
    nk_u64 Seen;
    nk_emit Emit;
    void* Context;
    bool Full;
};

static NTSTATUS
ListName(void* Context, ULONGLONG Reference, const FileNameEx* Entry)
{
    NkListing* Listing = (NkListing*)Context;
    char Name[NK_MAX_NAME * 4];
    nk_u32 Length;

    if (Listing->Seen++ < Listing->Skip)
        return STATUS_SUCCESS;
    Length = NameFromNtfs(Entry->Name, Entry->NameLength, Name, sizeof(Name));
    if (Length && Listing->Emit(Listing->Context, Name, Length, Reference,
                                (Entry->Flags & FN_DIRECTORY) ? NK_NAME_DIRECTORY
                                : (Entry->Flags & (FN_SYSTEM | FILE_PERM_REPARSE_PT)) ? NK_NAME_UNKNOWN
                                                                                      : NK_NAME_FILE) != 0)
    {
        /* The caller has all it can take: the rest of the index is not walked. */
        Listing->Full = true;
        return STATUS_BUFFER_OVERFLOW;
    }
    return STATUS_SUCCESS;
}

/* TODO: a listing resumed at Skip walks the names before it again; resume from the last name instead. */
extern "C" int
nk_readdir(struct nk_volume* Volume, nk_u64 DirectoryReference, nk_u64 Skip, nk_emit Emit, void* Context)
{
    NkListing Listing = {Skip, 0, Emit, Context, false};
    PNtfsFileRecord Directory;
    NTSTATUS Status = GetDirectory(Volume, DirectoryReference, &Directory);

    if (NT_SUCCESS(Status))
        Status = NtfsDirectoryVisitNames(Volume->Volume, Directory, ListName, &Listing);
    if (Listing.Full && Status == STATUS_BUFFER_OVERFLOW)
        Status = STATUS_SUCCESS;
    return ErrnoOf(Status);
}

extern "C" long
nk_read(struct nk_volume* Volume, nk_u64 Reference, nk_u64 Offset, void* Buffer, nk_u32 Length)
{
    PNtfsFileRecord File;
    ULONG Remaining = Length;
    NTSTATUS Status = GetFile(Volume, Reference, &File);

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    Status = NtfsFileRecordCopyData(File, TypeData, NULL, (PUCHAR)Buffer, &Remaining, Offset);
    if (Status == STATUS_END_OF_FILE)
        return 0;
    return NT_SUCCESS(Status) ? (long)(Length - Remaining) : ErrnoOf(Status);
}

extern "C" long
nk_write(struct nk_volume* Volume, nk_u64 Reference, nk_u64 Offset, const void* Buffer, nk_u32 Length, struct nk_attr* After)
{
    PNtfsFileRecord File;
    LARGE_INTEGER Position;
    ULONG Written = Length;
    NTSTATUS Status = GetFile(Volume, Reference, &File);

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    Position.QuadPart = (LONGLONG)Offset;
    Status = NtfsFileRecordWriteFileData(File, TypeData, NULL, (PUCHAR)Buffer, &Written, &Position);
    if (!NT_SUCCESS(Status))
    {
        NtfsFileRecordDestroy(Volume->File);
        Volume->File = NULL;
        return ErrnoOf(Status);
    }
    Volume->FileGeneration = NkWrites;
    FillAttr(Volume, File, After);
    return (long)Written;
}

static void
ForgetFile(struct nk_volume* Volume)
{
    NtfsFileRecordDestroy(Volume->File);
    Volume->File = NULL;
}

/* A change made through the cached file record leaves it current. */
static int
FileChanged(struct nk_volume* Volume, PNtfsFileRecord File, NTSTATUS Status, struct nk_attr* After)
{
    if (!NT_SUCCESS(Status))
    {
        ForgetFile(Volume);
        return ErrnoOf(Status);
    }
    Volume->FileGeneration = NkWrites;
    if (After)
        FillAttr(Volume, File, After);
    return 0;
}

extern "C" int
nk_resize(struct nk_volume* Volume, nk_u64 Reference, nk_u64 Size, struct nk_attr* After)
{
    PNtfsFileRecord File;
    NTSTATUS Status = GetFile(Volume, Reference, &File);

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    return FileChanged(Volume, File, NtfsFileRecordSetFileDataSize(File, TypeData, NULL, Size), After);
}

extern "C" int
nk_map(struct nk_volume* Volume, nk_u64 Reference, nk_u64 Offset, nk_u64* Disk, nk_u64* Run)
{
    PNtfsFileRecord File;
    NtfsRetrievalExtent Extent;
    ULONGLONG Start;
    ULONG Count = 1;
    ULONG Cluster = Volume->ClusterBytes;
    NTSTATUS Status = GetFile(Volume, Reference, &File);
    PAttribute Data;

    *Run = 0;
    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    if (!Data || !Data->IsNonResident || Data->Flags)
        return -NK_EIO;
    if (Offset >= Data->NonResident.AllocatedSize)
        return 0;
    Status = NtfsFileRecordQueryRetrievalPointers(File, TypeData, NULL, Offset / Cluster, &Start, &Extent, &Count);
    if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) || Count != 1 || Extent.NextVcn <= Offset / Cluster)
        return NT_SUCCESS(Status) ? -NK_EIO : ErrnoOf(Status);
    if (Extent.Lcn < 0)
        return 0;
    *Disk = ((ULONGLONG)Extent.Lcn + (Offset / Cluster - Start)) * Cluster + Offset % Cluster;
    *Run = Extent.NextVcn * Cluster - Offset;
    return 0;
}

extern "C" int
nk_reserve(struct nk_volume* Volume, nk_u64 Reference, nk_u64 Allocation, struct nk_attr* After)
{
    PNtfsFileRecord File;
    NTSTATUS Status = GetFile(Volume, Reference, &File);

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    return FileChanged(Volume, File, NtfsFileRecordSetFileAllocationSize(File, TypeData, NULL, Allocation), After);
}

/* Stores what the page cache has reached: the size, how far the data is valid, the times. */
extern "C" int
nk_commit(struct nk_volume* Volume, nk_u64 Reference, nk_u64 Size, nk_u64 Valid, int Trim, nk_u64 Accessed, nk_u64 Written,
          nk_u64 Changed, struct nk_attr* After)
{
    PNtfsFileRecord File;
    NtfsFileBasicInformation Basic = {};
    NTSTATUS Status = GetFile(Volume, Reference, &File);
    PAttribute Data;
    UINT32 Mask;

    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    Mask = NtfsFileRecordGetAutomaticTimestampMask(File);
    (void)NtfsFileRecordSetAutomaticTimestampMask(File, 0);
    Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    if (Data && Data->IsNonResident && !Data->Flags)
    {
        /* Clusters reserved ahead of the size stay reserved; the trim below is the one place that gives them back. */
        if (Data->NonResident.DataSize != Size)
            Status = NtfsFileRecordSetWriteSize(File, TypeData, NULL, Size, Data->NonResident.AllocatedSize);
        Data = NT_SUCCESS(Status) ? NtfsFileRecordGetAttribute(File, TypeData, NULL) : NULL;
        if (Data && Data->IsNonResident && Valid > Data->NonResident.InitalizedDataSize)
            Status = NtfsFileRecordSetFileValidDataLength(File, TypeData, NULL, Valid);
        Data = NT_SUCCESS(Status) ? NtfsFileRecordGetAttribute(File, TypeData, NULL) : NULL;
        if (Trim && Data && Data->IsNonResident)
        {
            ULONGLONG Needed = (Size + Volume->ClusterBytes - 1) / Volume->ClusterBytes * Volume->ClusterBytes;

            if (Data->NonResident.AllocatedSize > Needed && Needed != 0)
                Status = NtfsFileRecordSetFileAllocationSize(File, TypeData, NULL, Needed);
        }
    }
    if (NT_SUCCESS(Status))
    {
        Basic.Fields = NTFS_BASIC_INFO_LAST_ACCESS_TIME | NTFS_BASIC_INFO_LAST_WRITE_TIME | NTFS_BASIC_INFO_CHANGE_TIME;
        Basic.LastAccessTime = Accessed;
        Basic.LastWriteTime = Written;
        Basic.ChangeTime = Changed;
        Status = NtfsFileRecordSetBasicInformation(File, &Basic);
    }
    if (Volume->File)
        (void)NtfsFileRecordSetAutomaticTimestampMask(File, Mask);
    return FileChanged(Volume, File, Status, After);
}

extern "C" int
nk_set_times(struct nk_volume* Volume, nk_u64 Reference, nk_u64 Accessed, nk_u64 Written, nk_u64 Changed)
{
    PNtfsFileRecord File = NULL;
    NtfsFileBasicInformation Basic = {};
    NTSTATUS Status;

    StoreTouched(Volume, FindTouched(Volume, Reference));
    Status = NtfsMasterFileTableGetFileRecordByReference(Volume->Mft, Reference, &File);
    if (!NT_SUCCESS(Status))
        return ErrnoOf(Status);
    if (Accessed)
    {
        Basic.Fields |= NTFS_BASIC_INFO_LAST_ACCESS_TIME;
        Basic.LastAccessTime = Accessed;
    }
    if (Written)
    {
        Basic.Fields |= NTFS_BASIC_INFO_LAST_WRITE_TIME;
        Basic.LastWriteTime = Written;
    }
    if (Changed)
    {
        Basic.Fields |= NTFS_BASIC_INFO_CHANGE_TIME;
        Basic.ChangeTime = Changed;
    }
    Status = NtfsFileRecordSetBasicInformation(File, &Basic);
    NtfsFileRecordDestroy(File);
    return ErrnoOf(Status);
}
