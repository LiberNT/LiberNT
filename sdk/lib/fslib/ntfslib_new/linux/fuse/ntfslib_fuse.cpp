/*
 * FUSE frontend and image inspection tool for ntfslib_new.
 *
 * Mounting intentionally stays single-threaded because the shared library
 * currently lazy-loads per-volume metadata without locking. Mounts are
 * read-only unless --writable is given; writable mounts publish mutations
 * through the shared core in a readable order but are not crash-atomic
 * until LFS transactions exist, so use disposable images only. The direct
 * image commands exercise the shared lookup/read/write paths without
 * requiring /dev/fuse or mount privileges. Mutation commands open the backing
 * image writable only for the duration of one explicit operation.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <map>
#include <tuple>

#include <ntfslib_new.h>
#include <ntfsformat.h>
#include <ntfs_linux.h>
extern "C" {
#include <expandcore.h>
}

struct NtfsDirectoryName
{
    std::string Name;
    UINT64 Reference;
    mode_t Type;
};

struct NtfsDirectoryListing
{
    UINT64 Self;
    UINT64 Parent;
    std::vector<NtfsDirectoryName> Entries;
};


extern "C" NTSTATUS
NtfsFlushVolumeContext(void* Context);

extern "C" unsigned long long
NtfsQueryTicks(void)
{
    /* Diagnostics only; the host build has no use for it. */
    return 0;
}

#if NTFSLIB_HAVE_FUSE
#define FUSE_USE_VERSION 31
#include <fuse_lowlevel.h>
#endif

struct NtfsFuseOpenFile
{
    PNtfsFileRecord File;
    unsigned References;
    /* Sequential growth preallocates; the last close trims it like NT cleanup. */
    bool Written;
    bool Preallocated;
    /* The last name is gone; the records go with the last handle. */
    bool Unlinked;
    /* Like NT, data writes keep their write time in memory and the record
     * takes it at fsync, explicit time changes or the last close. */
    UINT64 PendingWriteTime;
    /* relatime: the first read of an open may move the access time. */
    UINT64 PendingAccessTime;
    bool AccessChecked;
    /* A write by an unprivileged caller has dropped set-id bits. */
    bool SetIdChecked;
};

struct NtfsFuseInode
{
    ULONGLONG Reference;
    uint64_t Lookups;
};

struct NtfsFuseState
{
    PNtfsVolume Volume;
    uid_t Owner;
    gid_t Group;
    bool Writable;
    bool WritebackCache;
    bool DirectIo;
    bool NoAtime;
    bool StrictAtime;
#if NTFSLIB_HAVE_FUSE
    struct fuse_session* Session;
#endif
    /* Inodes the kernel holds, keyed by inode number, with lookup counts. */
    std::unordered_map<uint64_t, NtfsFuseInode> Inodes;
    /* One record per open file, shared by all its handles. */
    std::unordered_map<uint64_t, NtfsFuseOpenFile> OpenFiles;
    /* Values are shared with open directory handles so a cache
     * invalidation cannot free a listing a reader still holds.
     */
    std::unordered_map<uint64_t, std::shared_ptr<NtfsDirectoryListing>> DirectoryCache;
    std::vector<UCHAR> ReadBuffer;
    /* Lazy access times of closed files, by file reference, as NT keeps them. */
    std::unordered_map<uint64_t, UINT64> PendingAccess;
    /* Attributes of removed directories the kernel still holds open, by inode. */
    std::unordered_map<uint64_t, struct stat> RemovedDirectories;
};

#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#endif
#define NTFS_MAX_PATH_WCHARS 32768

static int
NtStatusToErrno(NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_SUCCESS:
            return 0;
        case STATUS_NOT_FOUND:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:
            return ENOENT;
        case STATUS_INVALID_HANDLE:
            return ESTALE;
        case STATUS_NO_MORE_FILES:
            return ENOENT;
        case STATUS_END_OF_FILE:
            return EIO;
        case STATUS_INVALID_PARAMETER:
        case STATUS_OBJECT_NAME_INVALID:
        case STATUS_NOT_A_REPARSE_POINT:
        case STATUS_IO_REPARSE_TAG_INVALID:
        case STATUS_IO_REPARSE_TAG_MISMATCH:
        case STATUS_IO_REPARSE_DATA_INVALID:
        case STATUS_REPARSE_ATTRIBUTE_CONFLICT:
        case STATUS_INVALID_EA_NAME:
            return EINVAL;
        case STATUS_NOT_A_DIRECTORY:
            return ENOTDIR;
        case STATUS_FILE_IS_A_DIRECTORY:
            return EISDIR;
        case STATUS_OBJECT_NAME_COLLISION:
            return EEXIST;
        case STATUS_DIRECTORY_NOT_EMPTY:
            return ENOTEMPTY;
        case STATUS_EA_TOO_LARGE:
            return E2BIG;
        case STATUS_NO_EAS_ON_FILE:
        case STATUS_OBJECT_NOT_EXTERNALLY_BACKED:
            return ENODATA;
        case STATUS_NAME_TOO_LONG:
            return ENAMETOOLONG;
        case STATUS_INSUFFICIENT_RESOURCES:
            return ENOMEM;
        case STATUS_ACCESS_DENIED:
        case STATUS_MEDIA_WRITE_PROTECTED:
            return EROFS;
        case STATUS_DISK_FULL:
            return ENOSPC;
        case STATUS_NOT_IMPLEMENTED:
            return ENOSYS;
        case STATUS_IO_REPARSE_TAG_NOT_HANDLED:
        case STATUS_EAS_NOT_SUPPORTED:
            return EOPNOTSUPP;
        case STATUS_FILE_TOO_LARGE:
            return EFBIG;
        case STATUS_UNRECOGNIZED_VOLUME:
            return ENODEV;
        case STATUS_FILE_CORRUPT_ERROR:
        default:
            return EIO;
    }
}

static bool
AppendUtf16(uint32_t CodePoint, std::vector<WCHAR>& Output)
{
    if (CodePoint > 0x10ffff ||
        (CodePoint >= 0xd800 && CodePoint <= 0xdfff))
    {
        return false;
    }

    if (CodePoint < 0x10000)
    {
        Output.push_back((WCHAR)CodePoint);
    }
    else
    {
        CodePoint -= 0x10000;
        Output.push_back((WCHAR)(0xd800 + (CodePoint >> 10)));
        Output.push_back((WCHAR)(0xdc00 + (CodePoint & 0x3ff)));
    }
    return true;
}

static bool
Utf8ToNtfsString(const char* Input,
                 bool TranslateSeparators,
                 bool AllowEmpty,
                 std::vector<WCHAR>& Output)
{
    const unsigned char* Cursor = (const unsigned char*)Input;

    Output.clear();
    if (!Input || (!AllowEmpty && *Input == '\0'))
        return false;

    while (*Cursor)
    {
        uint32_t CodePoint;
        unsigned Needed;

        if (*Cursor < 0x80)
        {
            CodePoint = *Cursor++;
            Needed = 0;
        }
        else if ((*Cursor & 0xe0) == 0xc0)
        {
            CodePoint = *Cursor++ & 0x1f;
            Needed = 1;
            if (CodePoint < 2)
                return false;
        }
        else if ((*Cursor & 0xf0) == 0xe0)
        {
            CodePoint = *Cursor++ & 0x0f;
            Needed = 2;
        }
        else if ((*Cursor & 0xf8) == 0xf0)
        {
            CodePoint = *Cursor++ & 0x07;
            Needed = 3;
        }
        else
        {
            return false;
        }

        for (unsigned Index = 0; Index < Needed; Index++)
        {
            if ((Cursor[Index] & 0xc0) != 0x80)
                return false;
            CodePoint = (CodePoint << 6) | (Cursor[Index] & 0x3f);
        }
        Cursor += Needed;

        if ((Needed == 2 && CodePoint < 0x800) ||
            (Needed == 3 && CodePoint < 0x10000))
        {
            return false;
        }

        if (TranslateSeparators && CodePoint == '\\')
            return false;
        if (TranslateSeparators && CodePoint == '/')
            CodePoint = '\\';
        if (!AppendUtf16(CodePoint, Output))
            return false;
    }

    Output.push_back(L'\0');
    return true;
}

static bool
Utf8ToNtfsPath(const char* Path, std::vector<WCHAR>& Output)
{
    return Utf8ToNtfsString(Path, true, false, Output);
}

static bool
AppendUtf8(uint32_t CodePoint, std::string& Output)
{
    if (CodePoint <= 0x7f)
    {
        Output.push_back((char)CodePoint);
    }
    else if (CodePoint <= 0x7ff)
    {
        Output.push_back((char)(0xc0 | (CodePoint >> 6)));
        Output.push_back((char)(0x80 | (CodePoint & 0x3f)));
    }
    else if (CodePoint <= 0xffff)
    {
        if (CodePoint >= 0xd800 && CodePoint <= 0xdfff)
            return false;
        Output.push_back((char)(0xe0 | (CodePoint >> 12)));
        Output.push_back((char)(0x80 | ((CodePoint >> 6) & 0x3f)));
        Output.push_back((char)(0x80 | (CodePoint & 0x3f)));
    }
    else if (CodePoint <= 0x10ffff)
    {
        Output.push_back((char)(0xf0 | (CodePoint >> 18)));
        Output.push_back((char)(0x80 | ((CodePoint >> 12) & 0x3f)));
        Output.push_back((char)(0x80 | ((CodePoint >> 6) & 0x3f)));
        Output.push_back((char)(0x80 | (CodePoint & 0x3f)));
    }
    else
    {
        return false;
    }
    return true;
}

static bool
Utf16ToUtf8(const WCHAR* Input, size_t Length, std::string& Output)
{
    Output.clear();
    for (size_t Index = 0; Index < Length; Index++)
    {
        uint32_t CodePoint = (UINT16)Input[Index];
        if (CodePoint >= 0xd800 && CodePoint <= 0xdbff)
        {
            uint32_t Low;
            if (++Index >= Length)
                return false;
            Low = (UINT16)Input[Index];
            if (Low < 0xdc00 || Low > 0xdfff)
                return false;
            CodePoint = 0x10000 +
                        ((CodePoint - 0xd800) << 10) +
                        (Low - 0xdc00);
        }
        else if (CodePoint >= 0xdc00 && CodePoint <= 0xdfff)
        {
            return false;
        }

        if (!AppendUtf8(CodePoint, Output))
            return false;
    }
    return true;
}

static uint16_t
ReadUtf16Le(const UCHAR* Input, size_t Index)
{
    return (uint16_t)Input[Index * sizeof(WCHAR)] |
           ((uint16_t)Input[Index * sizeof(WCHAR) + 1] << 8);
}

static bool
Utf16LeToUtf8(const UCHAR* Input, size_t Length, std::string& Output)
{
    Output.clear();
    for (size_t Index = 0; Index < Length; Index++)
    {
        uint32_t CodePoint = ReadUtf16Le(Input, Index);

        if (CodePoint >= 0xd800 && CodePoint <= 0xdbff)
        {
            uint32_t Low;

            if (++Index >= Length)
                return false;
            Low = ReadUtf16Le(Input, Index);
            if (Low < 0xdc00 || Low > 0xdfff)
                return false;
            CodePoint = 0x10000 +
                        ((CodePoint - 0xd800) << 10) +
                        (Low - 0xdc00);
        }
        else if (CodePoint >= 0xdc00 && CodePoint <= 0xdfff)
        {
            return false;
        }

        if (!AppendUtf8(CodePoint, Output))
            return false;
    }
    return true;
}

static NTSTATUS
Lookup(PNtfsVolume Volume,
       const char* Path,
       PNtfsFileRecord* File)
{
    std::vector<WCHAR> NtfsPath;

    *File = NULL;
    if (!Utf8ToNtfsPath(Path, NtfsPath))
        return STATUS_OBJECT_NAME_INVALID;
    if (NtfsPath.size() > MAXUSHORT / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    return NtfsMasterFileTableGetFileRecordFromQuery(
        NtfsVolumeGetMft(Volume),
        NtfsPath.data(),
        File);
}

static int
ProbeReparseLookup(PNtfsVolume Volume,
                   const char* Path,
                   bool OpenFinal)
{
    std::vector<WCHAR> NtfsPath;
    PNtfsFileRecord File = NULL;
    ULONG RemainingNameLength = 0;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath))
        return 1;
    if (NtfsPath.empty() ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        return 1;
    }
    ULONG QueryLength =
        (ULONG)(NtfsPath.size() - 1);
    NtfsPath.back() = L'X';
    Status =
        NtfsMasterFileTableGetFileRecordFromQueryEx(
            NtfsVolumeGetMft(Volume),
            NtfsPath.data(),
            QueryLength,
            OpenFinal,
            &RemainingNameLength,
            &File);
    printf("0x%08" PRIx32 " %u %u\n",
           (uint32_t)Status,
           RemainingNameLength,
           File
               ? NtfsFileRecordGetHeader(File)->
                    MFTRecordNumber
               : 0);
    NtfsFileRecordDestroy(File);
    return Status == STATUS_REPARSE ||
           NT_SUCCESS(Status)
        ? 0
        : 1;
}

static NTSTATUS
LookupAttribute(PNtfsVolume Volume,
                const char* Path,
                PNtfsFileRecord* File,
                AttributeType* Type,
                PWSTR* Stream)
{
    std::vector<WCHAR> NtfsPath;
    UNICODE_STRING CountedPath;
    NTSTATUS Status;

    *File = NULL;
    *Type = TypeData;
    *Stream = NULL;
    if (!Utf8ToNtfsPath(Path, NtfsPath))
        return STATUS_OBJECT_NAME_INVALID;
    if (NtfsPath.size() > MAXUSHORT / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    CountedPath.Buffer = NtfsPath.data();
    CountedPath.Length =
        (USHORT)((NtfsPath.size() - 1) * sizeof(WCHAR));
    CountedPath.MaximumLength =
        (USHORT)(NtfsPath.size() * sizeof(WCHAR));
    Status = NtfsVolumeGetADSPreference(
        Volume,
        &CountedPath,
        Type,
        Stream);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsMasterFileTableGetFileRecordFromQuery(
        NtfsVolumeGetMft(Volume),
        NtfsPath.data(),
        File);
    if (!NT_SUCCESS(Status))
    {
        delete[] *Stream;
        *Stream = NULL;
    }
    return Status;
}

static UINT64
AttributeSize(PAttribute Attribute)
{
    if (!Attribute)
        return 0;
    return Attribute->IsNonResident
        ? Attribute->NonResident.DataSize
        : Attribute->Resident.DataLength;
}

static NTSTATUS
GetReparseTarget(PNtfsFileRecord File, std::string& Target)
{
    NtfsReparseNameView View;
    std::vector<UCHAR> RawData;
    const UCHAR* Name;
    ULONG NameLength;
    ULONG RawLength = 0;
    NTSTATUS Status;

    Target.clear();
    Status = NtfsFileRecordReadReparsePoint(File, NULL, &RawLength);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    if (RawLength == 0 ||
        RawLength > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    RawData.resize(RawLength);
    Status = NtfsFileRecordReadReparsePoint(
        File,
        RawData.data(),
        &RawLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsParseNameSurrogateReparsePoint(
        RawData.data(),
        RawLength,
        &View);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * The substitute name drives relative links. For an absolute symlink or
     * junction, prefer its display-safe print name when one is present.
     */
    Name = View.SubstituteName;
    NameLength = View.SubstituteNameLength;
    if (!(View.Flags & NTFS_SYMLINK_FLAG_RELATIVE) &&
        View.PrintNameLength != 0)
    {
        Name = View.PrintName;
        NameLength = View.PrintNameLength;
    }
    else if (NameLength >= 4 &&
             ReadUtf16Le(Name, 0) == L'\\' &&
             ReadUtf16Le(Name, 1) == L'?' &&
             ReadUtf16Le(Name, 2) == L'?' &&
             ReadUtf16Le(Name, 3) == L'\\')
    {
        Name += 4 * sizeof(WCHAR);
        NameLength -= 4;
    }

    if (!Utf16LeToUtf8(Name, NameLength, Target))
        return STATUS_IO_REPARSE_DATA_INVALID;
    for (char& Character : Target)
    {
        if (Character == '\\')
            Character = '/';
    }
    return STATUS_SUCCESS;
}

#if NTFSLIB_HAVE_FUSE
static struct timespec
NtfsTimeToTimespec(UINT64 NtfsTime)
{
    const UINT64 EpochDifference = UINT64_C(116444736000000000);
    struct timespec Result = {};

    if (NtfsTime == 0)
        return Result;
    if (NtfsTime < EpochDifference)
    {
        UINT64 Before = EpochDifference - NtfsTime;
        UINT64 Seconds = (Before + UINT64_C(9999999)) / UINT64_C(10000000);

        Result.tv_sec = -(time_t)Seconds;
        Result.tv_nsec = (long)((Seconds * UINT64_C(10000000) - Before) * 100);
        return Result;
    }

    NtfsTime -= EpochDifference;
    Result.tv_sec = (time_t)(NtfsTime / UINT64_C(10000000));
    Result.tv_nsec = (long)((NtfsTime % UINT64_C(10000000)) * 100);
    return Result;
}

/*
 * POSIX ownership, mode and device numbers in the EAs Linux ntfs3 and WSL
 * use: $LXUID, $LXGID, $LXMOD (the whole st_mode) and $LXDEV (major << 20 |
 * minor, as ntfs3 writes it), each a little-endian 32-bit value.
 */
struct NtfsPosixMetadata
{
    bool HasUid, HasGid, HasMode, HasDev;
    UINT32 Uid, Gid, Mode, Dev;
};

static const char* const NtfsPosixEaNames[] = {"$LXUID", "$LXGID", "$LXMOD", "$LXDEV"};

static bool
IsPosixEaName(const UCHAR* Name, size_t Length)
{
    return Length == 6 && strncasecmp((const char*)Name, "$LX", 3) == 0 &&
           (strncasecmp((const char*)Name, "$LXUID", 6) == 0 ||
            strncasecmp((const char*)Name, "$LXGID", 6) == 0 ||
            strncasecmp((const char*)Name, "$LXMOD", 6) == 0 ||
            strncasecmp((const char*)Name, "$LXDEV", 6) == 0);
}

static void
ReadPosixMetadata(PNtfsFileRecord File, NtfsPosixMetadata* Metadata)
{
    std::vector<UCHAR> Raw;
    ULONG Length = 0;
    ULONG Offset = 0;
    NtfsExtendedAttributeView View;

    memset(Metadata, 0, sizeof(*Metadata));
    if (!NtfsFileRecordGetAttribute(File, TypeEAInformation, NULL) ||
        NtfsFileRecordReadExtendedAttributes(File, NULL, &Length, NULL) != STATUS_BUFFER_TOO_SMALL ||
        Length == 0)
    {
        return;
    }
    Raw.resize(Length);
    if (!NT_SUCCESS(NtfsFileRecordReadExtendedAttributes(File, Raw.data(), &Length, NULL)))
        return;
    while (NT_SUCCESS(NtfsGetNextExtendedAttribute(Raw.data(), Length, &Offset, &View)))
    {
        UINT32 Value;

        if (View.ValueLength != sizeof(Value) || !IsPosixEaName(View.Name, View.NameLength))
            continue;
        memcpy(&Value, View.Value, sizeof(Value));
        if (strncasecmp((const char*)View.Name, "$LXUID", 6) == 0)
            Metadata->HasUid = true, Metadata->Uid = Value;
        else if (strncasecmp((const char*)View.Name, "$LXGID", 6) == 0)
            Metadata->HasGid = true, Metadata->Gid = Value;
        else if (strncasecmp((const char*)View.Name, "$LXMOD", 6) == 0)
            Metadata->HasMode = true, Metadata->Mode = Value;
        else
            Metadata->HasDev = true, Metadata->Dev = Value;
    }
}

static NTSTATUS
WritePosixMetadata(PNtfsFileRecord File,
                   const UINT32* Uid,
                   const UINT32* Gid,
                   const UINT32* Mode,
                   const UINT32* Dev)
{
    const UINT32* Values[] = {Uid, Gid, Mode, Dev};
    NtfsExtendedAttributeUpdate Updates[4] = {};
    ULONG Count = 0;

    for (ULONG Index = 0; Index < 4; Index++)
    {
        if (!Values[Index])
            continue;
        Updates[Count].Operation = NTFS_EA_UPDATE_SET;
        Updates[Count].NameLength = 6;
        Updates[Count].ValueLength = sizeof(UINT32);
        Updates[Count].Name = (const UCHAR*)NtfsPosixEaNames[Index];
        Updates[Count].Value = (const UCHAR*)Values[Index];
        Count++;
    }
    return Count ? NtfsFileRecordUpdateExtendedAttributes(File, Updates, Count) : STATUS_SUCCESS;
}

static NTSTATUS
FillStatFromRecord(PNtfsFileRecord File,
                   const NtfsFuseState* State,
                   struct stat* Stat)
{
    PFileRecordHeader Header = NtfsFileRecordGetHeader(File);
    PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    PStandardInformationEx Standard = NULL;
    PUCHAR StandardData = NULL;
    NTSTATUS StandardStatus;

    memset(Stat, 0, sizeof(*Stat));
    Stat->st_ino = (ino_t)(NtfsFileRecordGetReference(File) & UINT64_C(0x0000FFFFFFFFFFFF));
    Stat->st_uid = State->Owner;
    Stat->st_gid = State->Group;
    Stat->st_blksize =
        NtfsVolumeGetBytesPerSector(State->Volume) *
        NtfsVolumeGetSectorsPerCluster(State->Volume);
    Stat->st_nlink = NtfsFileRecordGetLinkCount(File);

    if (Header->Flags & FR_IS_DIRECTORY)
    {
        Stat->st_mode = S_IFDIR | (State->Writable ? 0755 : 0555);
    }
    else
    {
        Stat->st_mode = S_IFREG | (State->Writable ? 0755 : 0555);
        Stat->st_size = (off_t)AttributeSize(Data);
        if (Data && Data->IsNonResident)
        {
            Stat->st_blocks = (blkcnt_t)(
                (Data->Flags & (ATTR_COMPRESSED | ATTR_SPARSE)
                     ? Data->NonResident.CompressedDataSize
                     : Data->NonResident.AllocatedSize) / 512);
        }
        else
        {
            Stat->st_blocks = (blkcnt_t)((Stat->st_size + 511) / 512);
        }
    }

    StandardStatus = NtfsFileRecordGetAttributeData(
        File,
        TypeStandardInformation,
        NULL,
        &StandardData);
    if (NT_SUCCESS(StandardStatus))
    {
        Standard = (PStandardInformationEx)StandardData;
        Stat->st_ctim = NtfsTimeToTimespec(Standard->ChangeTime);
        Stat->st_mtim = NtfsTimeToTimespec(Standard->LastWriteTime);
        Stat->st_atim = NtfsTimeToTimespec(Standard->LastAccessTime);
        {
            NtfsPosixMetadata Metadata;

            ReadPosixMetadata(File, &Metadata);
            if (Metadata.HasUid)
                Stat->st_uid = Metadata.Uid;
            if (Metadata.HasGid)
                Stat->st_gid = Metadata.Gid;
            if (Metadata.HasMode)
            {
                mode_t Type = Metadata.Mode & S_IFMT;

                if (Header->Flags & FR_IS_DIRECTORY)
                    Type = S_IFDIR;
                else if (Type != S_IFIFO && Type != S_IFSOCK && Type != S_IFCHR && Type != S_IFBLK)
                    Type = S_IFREG;
                Stat->st_mode = Type | (Metadata.Mode & 07777);
                if (Type != S_IFREG && Type != S_IFDIR)
                    Stat->st_size = 0;
            }
            if (Metadata.HasDev && (S_ISCHR(Stat->st_mode) || S_ISBLK(Stat->st_mode)))
                Stat->st_rdev = makedev(Metadata.Dev >> 20, Metadata.Dev & 0xFFFFF);
        }
        if (!(Header->Flags & FR_IS_DIRECTORY) &&
            (Standard->FilePermissions & FILE_PERM_READONLY))
        {
            Stat->st_mode &= ~(mode_t)0222;
        }

        if (Standard->FilePermissions & FILE_PERM_REPARSE_PT)
        {
            std::string Target;
            NTSTATUS Status = GetReparseTarget(File, Target);

            if (NT_SUCCESS(Status))
            {
                Stat->st_mode = S_IFLNK | 0777;
                Stat->st_size = (off_t)Target.size();
                Stat->st_blocks = 0;
            }
            else if (Status != STATUS_IO_REPARSE_TAG_NOT_HANDLED)
            {
                return Status;
            }
        }
    }
    return StandardStatus;
}

#endif

static NTSTATUS
OpenImageWithMode(const char* Image,
                  bool ShowMetadata,
                  bool Writable,
                  NtfsFuseState* State)
{
    ULONG BytesPerSector;
    NTSTATUS Status;

    static_assert(sizeof(WCHAR) == 2, "NTFS requires 16-bit WCHAR");
    static_assert(sizeof(BootSector) == 512, "Boot sector layout changed");
    static_assert(sizeof(FileRecordHeader) == 48,
                  "File record layout changed");
    static_assert(FIELD_OFFSET(FileNameEx, Name) == 0x42,
                  "File name attribute layout changed");
    static_assert(FIELD_OFFSET(IndexEntry, IndexStream) == 0x10,
                  "Index entry layout changed");

    State->Volume = NULL;
    State->Owner = getuid();
    State->Group = getgid();

    Status = Writable
        ? NtfsDiskInitializeLinuxWritable(Image, &BytesPerSector)
        : NtfsDiskInitializeLinux(Image, &BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;

    NtfsSetShowMetadataFiles(ShowMetadata ? TRUE : FALSE);
    NtfsSetReadOnlyMode(Writable ? FALSE : TRUE);
    Status = NtfsProbePartitionAndOpenVolume(BytesPerSector, &State->Volume);
    if (!NT_SUCCESS(Status))
        NtfsDiskCloseLinux();
    return Status;
}

static void
PrintCheckEvent(void* Context, NtfsCheckEvent Event, UINT64 Argument1, UINT64 Argument2)
{
    (void)Context;
    printf("event %d %" PRIx64 " %" PRIx64 "\n", (int)Event, (uint64_t)Argument1, (uint64_t)Argument2);
}

static NTSTATUS
OpenImage(const char* Image,
          bool ShowMetadata,
          NtfsFuseState* State)
{
    return OpenImageWithMode(Image, ShowMetadata, false, State);
}

static NTSTATUS
OpenImageWritable(const char* Image,
                  NtfsFuseState* State)
{
    return OpenImageWithMode(Image, false, true, State);
}

static void
CloseImage(NtfsFuseState* State)
{
    State->DirectoryCache.clear();
    State->Inodes.clear();
    NtfsVolumeDestroy(State->Volume);
    State->Volume = NULL;
    NtfsDiskCloseLinux();
}

static int
ListDirectory(NtfsFuseState* State,
              const char* Path,
              bool IncludeAllocation)
{
    PNtfsFileRecord File = NULL;
    PNtfsDirectory Directory = NULL;
    NtfsDirectoryEntry Entry;
    BOOLEAN Restart = TRUE;
    NTSTATUS Status;
    int Result = 1;

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (!(NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY))
    {
        errno = ENOTDIR;
        goto Done;
    }

    Directory = NtfsDirectoryCreate(State->Volume);
    if (!Directory)
    {
        errno = ENOMEM;
        goto Done;
    }

    Status = NtfsDirectoryLoadDirectory(Directory, File);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        std::string Name;
        Status = NtfsDirectoryReadNext(Directory, Restart, &Entry);
        Restart = FALSE;
        if (Status == STATUS_NO_MORE_FILES)
        {
            Result = 0;
            break;
        }
        if (!NT_SUCCESS(Status))
            break;
        if (!Utf16ToUtf8(Entry.Name, Entry.NameLength, Name))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        if (IncludeAllocation)
        {
            printf("%c %10" PRIu64 " %10" PRIu64
                   " %s %" PRIu16 " 0x%08" PRIx32 "\n",
                   Entry.FileAttributes & FN_DIRECTORY ? 'd' : '-',
                   (uint64_t)Entry.EndOfFile,
                   (uint64_t)Entry.AllocationSize,
                   Name.c_str(),
                   Entry.EaSize,
                   Entry.ReparseTag);
        }
        else
        {
            printf("%c %10" PRIu64 " %s\n",
                   Entry.FileAttributes & FN_DIRECTORY ? 'd' : '-',
                   (uint64_t)Entry.EndOfFile,
                   Name.c_str());
        }
    }

Done:
    if (Result != 0)
    {
        if (NT_SUCCESS(Status))
            fprintf(stderr, "%s: %s\n", Path, strerror(errno));
        else
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Path,
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
    }
    NtfsDirectoryDestroy(Directory);
    NtfsFileRecordDestroy(File);
    return Result;
}

static int
CatFile(NtfsFuseState* State,
        const char* Path,
        UINT64 StartOffset,
        UINT64 MaximumLength)
{
    PNtfsFileRecord File = NULL;
    PAttribute Attribute;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;
    UINT64 Offset;
    UINT64 EndOffset;
    UINT64 Size;
    UCHAR Buffer[64 * 1024];
    int Result = 1;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;
    if ((NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) &&
        RequestedType == TypeData &&
        !RequestedStream)
    {
        errno = EISDIR;
        Status = STATUS_SUCCESS;
        goto Done;
    }

    Attribute = NtfsFileRecordGetAttribute(File,
                                           RequestedType,
                                           RequestedStream);
    if (!Attribute)
    {
        Status = STATUS_NOT_FOUND;
        goto Done;
    }
    if (Attribute->Flags & ATTR_ENCRYPTED)
    {
        errno = EOPNOTSUPP;
        Status = STATUS_SUCCESS;
        goto Done;
    }

    Size = AttributeSize(Attribute);
    if (StartOffset > Size)
    {
        Status = STATUS_END_OF_FILE;
        goto Done;
    }
    Offset = StartOffset;
    EndOffset = Size;
    if (MaximumLength < Size - StartOffset)
        EndOffset = StartOffset + MaximumLength;

    while (Offset < EndOffset)
    {
        ULONG Requested = (ULONG)((EndOffset - Offset) < sizeof(Buffer)
                                  ? EndOffset - Offset
                                  : sizeof(Buffer));
        ULONG Remaining = Requested;
        ULONG Completed;

        Status = NtfsFileRecordCopyData(File,
                                        RequestedType,
                                        RequestedStream,
                                        Buffer,
                                        &Remaining,
                                        Offset);
        if (!NT_SUCCESS(Status))
            goto Done;
        Completed = Requested - Remaining;
        if (Completed == 0)
        {
            Status = STATUS_END_OF_FILE;
            goto Done;
        }

        ULONG Written = 0;
        while (Written < Completed)
        {
            ssize_t Count = write(STDOUT_FILENO,
                                  Buffer + Written,
                                  Completed - Written);
            if (Count > 0)
            {
                Written += (ULONG)Count;
                continue;
            }
            if (Count < 0 && errno == EINTR)
                continue;
            Status = STATUS_SUCCESS;
            goto Done;
        }
        Offset += Completed;
    }

    Result = 0;

Done:
    if (Result != 0)
    {
        if (NT_SUCCESS(Status))
            fprintf(stderr, "%s: %s\n", Path, strerror(errno));
        else
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Path,
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return Result;
}

static int
ReadLink(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    std::string Target;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
        Status = GetReparseTarget(File, Target);

    if (NT_SUCCESS(Status))
    {
        printf("%s\n", Target.c_str());
    }
    else
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
DumpReparsePoint(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    ULONG RawLength = 0;
    ULONG Written = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordReadReparsePoint(
            File,
            NULL,
            &RawLength);
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        RawData.resize(RawLength);
        Status = NtfsFileRecordReadReparsePoint(
            File,
            RawData.data(),
            &RawLength);
    }

    while (NT_SUCCESS(Status) && Written < RawLength)
    {
        ssize_t Count = write(STDOUT_FILENO,
                              RawData.data() + Written,
                              RawLength - Written);
        if (Count > 0)
        {
            Written += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        Status = STATUS_ACCESS_DENIED;
    }

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
ListExtendedAttributes(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    EAInformationEx Information = {};
    NtfsExtendedAttributeView View;
    std::vector<UCHAR> RawData;
    ULONG Offset = 0;
    ULONG RawLength = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordReadExtendedAttributes(
            File,
            NULL,
            &RawLength,
            &Information);
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        RawData.resize(RawLength);
        Status = NtfsFileRecordReadExtendedAttributes(
            File,
            RawData.data(),
            &RawLength,
            &Information);
    }
    else if (Status == STATUS_NO_EAS_ON_FILE)
    {
        Status = STATUS_SUCCESS;
    }

    while (NT_SUCCESS(Status))
    {
        Status = NtfsGetNextExtendedAttribute(
            RawData.data(),
            RawLength,
            &Offset,
            &View);
        if (Status == STATUS_NO_MORE_EAS)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
            break;

        printf("%c ",
               View.Flags & NTFS_EA_FLAG_NEED_EA ? '!' : '-');
        fwrite(View.Name, 1, View.NameLength, stdout);
        printf(" %" PRIu16 " ", View.ValueLength);
        for (ULONG Index = 0; Index < View.ValueLength; Index++)
            printf("%02x", View.Value[Index]);
        putchar('\n');
    }

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
DumpSecurityDescriptor(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Descriptor;
    ULONG DescriptorLength = 0;
    ULONG Written = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordReadSecurityDescriptor(
            File,
            NULL,
            &DescriptorLength);
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Descriptor.resize(DescriptorLength);
        Status = NtfsFileRecordReadSecurityDescriptor(
            File,
            Descriptor.data(),
            &DescriptorLength);
    }

    while (NT_SUCCESS(Status) && Written < DescriptorLength)
    {
        ssize_t Count = write(
            STDOUT_FILENO,
            Descriptor.data() + Written,
            DescriptorLength - Written);
        if (Count > 0)
        {
            Written += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        Status = STATUS_ACCESS_DENIED;
    }

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
ProbeImage(NtfsFuseState* State)
{
    WCHAR Label[32] = {};
    USHORT LabelBytes = sizeof(Label);
    LARGE_INTEGER FreeClusters = {};
    std::string LabelUtf8;
    NTSTATUS LabelStatus;
    NTSTATUS FreeStatus;
    USHORT Flags = 0;

    LabelStatus = NtfsVolumeGetVolumeLabel(State->Volume,
                                           Label,
                                           &LabelBytes);
    if (!NT_SUCCESS(LabelStatus) ||
        !Utf16ToUtf8(Label, LabelBytes / sizeof(WCHAR), LabelUtf8))
    {
        LabelUtf8 = "<unavailable>";
    }
    FreeStatus = NtfsVolumeGetFreeClusters(State->Volume, &FreeClusters);

    printf("NTFS %" PRIu16 ".%" PRIu16 "\n",
           (uint16_t)NtfsVolumeGetMajorVersion(State->Volume),
           (uint16_t)NtfsVolumeGetMinorVersion(State->Volume));
    printf("label: %s\n", LabelUtf8.c_str());
    printf("serial: %016" PRIx64 "\n",
           (uint64_t)NtfsVolumeGetSerialNumber(State->Volume));
    printf("sector size: %" PRIu32 "\n",
           (uint32_t)NtfsVolumeGetBytesPerSector(State->Volume));
    printf("cluster size: %" PRIu32 "\n",
           (uint32_t)(NtfsVolumeGetBytesPerSector(State->Volume) *
                      NtfsVolumeGetSectorsPerCluster(State->Volume)));
    printf("clusters: %" PRIu64 "\n",
           (uint64_t)NtfsVolumeGetClustersInVolume(State->Volume));
    if (NT_SUCCESS(FreeStatus))
        printf("free clusters: %" PRIi64 "\n",
               (int64_t)FreeClusters.QuadPart);
    else
        printf("free clusters: unavailable (0x%08" PRIx32 ")\n",
               (uint32_t)FreeStatus);
    if (NT_SUCCESS(NtfsVolumeQueryFlags(State->Volume, &Flags)))
    {
        printf("volume flags: 0x%04" PRIx16 "\n", (uint16_t)Flags);
        printf("startup repair: %s\n", NtfsVolumeNeedsStartupRepair(Flags) ? "yes" : "no");
    }
    return 0;
}

static int
SetImageLabel(NtfsFuseState* State, const char* Label)
{
    std::vector<WCHAR> Utf16Label;
    ULONG LabelLength;
    NTSTATUS Status;

    if (!Utf8ToNtfsString(Label,
                          false,
                          true,
                          Utf16Label))
    {
        fprintf(stderr, "invalid UTF-8 volume label\n");
        return 1;
    }

    LabelLength =
        (ULONG)((Utf16Label.size() - 1) * sizeof(WCHAR));
    if (LabelLength > MAXIMUM_VOLUME_LABEL_LENGTH)
    {
        fprintf(stderr, "volume label exceeds 32 UTF-16 code units\n");
        return 1;
    }

    Status = NtfsVolumeSetVolumeLabel(
        State->Volume,
        Utf16Label.data(),
        LabelLength);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "volume label: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }

    return 0;
}

static int
PrintBasicInformation(NtfsFuseState* State,
                      const char* Path)
{
    PNtfsFileRecord File = NULL;
    NtfsFileBasicInformation Information = {};
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordGetBasicInformation(
            File,
            &Information);
    }

    if (NT_SUCCESS(Status))
    {
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64
               " %" PRIu64 " 0x%08" PRIx32 "\n",
               Information.CreationTime,
               Information.LastAccessTime,
               Information.LastWriteTime,
               Information.ChangeTime,
               Information.FileAttributes);
    }
    else
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
SetBasicInformation(NtfsFuseState* State,
                    const char* Path,
                    const NtfsFileBasicInformation* Information)
{
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetBasicInformation(
            File,
            Information);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
CreateNode(NtfsFuseState* State,
           const char* Path,
           bool IsDirectory,
           ULONG FileAttributes)
{
    std::vector<WCHAR> NtfsPath;
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else
    {
        Status = NtfsMasterFileTableCreateFile(
            NtfsVolumeGetMft(State->Volume),
            NtfsPath.data(),
            (ULONG)(NtfsPath.size() - 1),
            IsDirectory,
            FileAttributes,
            &File);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }

    printf("%u\n",
           NtfsFileRecordGetHeader(File)->
               MFTRecordNumber);
    NtfsFileRecordDestroy(File);
    return 0;
}

static int
RemoveNode(NtfsFuseState* State,
           const char* Path,
           bool IsDirectory)
{
    std::vector<WCHAR> NtfsPath;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else
    {
        Status = NtfsMasterFileTableDeleteFile(
            NtfsVolumeGetMft(State->Volume),
            NtfsPath.data(),
            (ULONG)(NtfsPath.size() - 1),
            IsDirectory);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }
    return 0;
}

static int
UnlinkNode(NtfsFuseState* State,
           const char* Path,
           bool IsDirectory)
{
    std::vector<WCHAR> NtfsPath;
    PNtfsFileRecord File = NULL;
    ULONGLONG FileReference = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        FileReference = NtfsFileRecordGetReference(File);
        if (!Utf8ToNtfsPath(Path, NtfsPath) ||
            NtfsPath.size() < 2 ||
            NtfsPath.size() - 1 > MAXULONG)
        {
            Status = STATUS_OBJECT_NAME_INVALID;
        }
        else
        {
            Status = NtfsMasterFileTableUnlinkFile(
                NtfsVolumeGetMft(State->Volume),
                NtfsPath.data(),
                (ULONG)(NtfsPath.size() - 1),
                IsDirectory,
                File);
        }
        NtfsFileRecordDestroy(File);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }
    printf("0x%016" PRIx64 "\n", (uint64_t)FileReference);
    return 0;
}

static int
DeleteUnlinkedNode(NtfsFuseState* State,
                   ULONGLONG FileReference)
{
    NTSTATUS Status;

    Status = NtfsMasterFileTableDeleteUnlinkedFile(
        NtfsVolumeGetMft(State->Volume),
        FileReference,
        NULL);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "0x%016" PRIx64 ": ntfslib status 0x%08" PRIx32
                " (%s)\n",
                (uint64_t)FileReference,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }
    return 0;
}

static int
RenameNode(NtfsFuseState* State,
           const char* OldPath,
           const char* NewPath,
           bool HardLink)
{
    std::vector<WCHAR> OldNtfsPath;
    std::vector<WCHAR> NewNtfsPath;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(OldPath, OldNtfsPath) ||
        !Utf8ToNtfsPath(NewPath, NewNtfsPath) ||
        OldNtfsPath.size() < 2 ||
        NewNtfsPath.size() < 2 ||
        OldNtfsPath.size() - 1 > MAXULONG ||
        NewNtfsPath.size() - 1 > MAXULONG)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else if (HardLink)
    {
        Status =
            NtfsMasterFileTableCreateHardLink(
                NtfsVolumeGetMft(State->Volume),
                OldNtfsPath.data(),
                (ULONG)(OldNtfsPath.size() - 1),
                NewNtfsPath.data(),
                (ULONG)(NewNtfsPath.size() - 1));
    }
    else
    {
        Status = NtfsMasterFileTableRenameFile(
            NtfsVolumeGetMft(State->Volume),
            OldNtfsPath.data(),
            (ULONG)(OldNtfsPath.size() - 1),
            NewNtfsPath.data(),
            (ULONG)(NewNtfsPath.size() - 1));
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s -> %s: ntfslib status "
                "0x%08" PRIx32 " (%s)\n",
                OldPath,
                NewPath,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }
    return 0;
}

static int
SetSecurityFromHost(NtfsFuseState* State,
                    const char* Path,
                    const char* SourcePath)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Value;
    struct stat SourceStat;
    ULONG Completed = 0;
    NTSTATUS Status;
    int Source = -1;
    int Result = 1;

    Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
    if (Source < 0 || fstat(Source, &SourceStat) != 0 ||
        SourceStat.st_size <= 0 ||
        (uint64_t)SourceStat.st_size > MAXULONG)
    {
        fprintf(stderr, "%s: %s\n", SourcePath, strerror(errno));
        goto Done;
    }
    Value.resize((size_t)SourceStat.st_size);
    while (Completed < Value.size())
    {
        ssize_t Count = read(Source,
                             Value.data() + Completed,
                             Value.size() - Completed);
        if (Count > 0)
        {
            Completed += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        fprintf(stderr, "%s: incomplete input\n", SourcePath);
        goto Done;
    }

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetSecurityDescriptor(
            File,
            Value.data(),
            (ULONG)Value.size());
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        goto Done;
    }
    Result = 0;

Done:
    NtfsFileRecordDestroy(File);
    if (Source >= 0)
        close(Source);
    return Result;
}

static int
UpdateExtendedAttribute(NtfsFuseState* State,
                        const char* Path,
                        UINT8 Flags,
                        const char* Name,
                        const char* SourcePath,
                        bool Remove)
{
    PNtfsFileRecord File = NULL;
    NtfsExtendedAttributeUpdate Update = {};
    std::vector<UCHAR> Value;
    struct stat SourceStat;
    size_t NameLength;
    ULONG Completed = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    int Source = -1;
    int Result = 1;

    NameLength = Name ? strlen(Name) : 0;
    if (NameLength == 0 || NameLength >= 0xff)
    {
        fprintf(stderr, "EA name must contain 1-254 ASCII bytes\n");
        goto Done;
    }

    if (!Remove)
    {
        Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
        if (Source < 0 || fstat(Source, &SourceStat) != 0)
        {
            fprintf(stderr,
                    "%s: %s\n",
                    SourcePath,
                    strerror(errno));
            goto Done;
        }
        if (SourceStat.st_size < 0 ||
            (uint64_t)SourceStat.st_size > MAXUSHORT)
        {
            fprintf(stderr,
                    "%s: EA value exceeds 65535 bytes\n",
                    SourcePath);
            goto Done;
        }

        Value.resize((size_t)SourceStat.st_size);
        while (Completed < Value.size())
        {
            ssize_t Count = read(Source,
                                 Value.data() + Completed,
                                 Value.size() - Completed);
            if (Count > 0)
            {
                Completed += (ULONG)Count;
                continue;
            }
            if (Count < 0 && errno == EINTR)
                continue;
            fprintf(stderr, "%s: incomplete input\n", SourcePath);
            goto Done;
        }
    }

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Update.Operation = Remove
        ? NTFS_EA_UPDATE_REMOVE
        : NTFS_EA_UPDATE_SET;
    Update.Flags = Flags;
    Update.NameLength = (UINT8)NameLength;
    Update.ValueLength = (UINT16)Value.size();
    Update.Name = (const UCHAR*)Name;
    Update.Value =
        Value.empty() ? NULL : Value.data();
    Status = NtfsFileRecordUpdateExtendedAttributes(
        File,
        &Update,
        1);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    if (Source >= 0)
        close(Source);
    return Result;
}

static int
UpdateReparsePointFromHost(NtfsFuseState* State,
                           const char* Path,
                           const char* SourcePath,
                           bool Delete)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Buffer;
    struct stat SourceStat;
    ULONG Completed = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    int Source = -1;
    int Result = 1;

    Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
    if (Source < 0 || fstat(Source, &SourceStat) != 0)
    {
        fprintf(stderr,
                "%s: %s\n",
                SourcePath,
                strerror(errno));
        goto Done;
    }
    if (SourceStat.st_size <= 0 ||
        (uint64_t)SourceStat.st_size >
            NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        fprintf(stderr,
                "%s: reparse buffer must contain 1-%u bytes\n",
                SourcePath,
                NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
        goto Done;
    }

    Buffer.resize((size_t)SourceStat.st_size);
    while (Completed < Buffer.size())
    {
        ssize_t Count = read(Source,
                             Buffer.data() + Completed,
                             Buffer.size() - Completed);
        if (Count > 0)
        {
            Completed += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        fprintf(stderr, "%s: incomplete input\n", SourcePath);
        goto Done;
    }

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    Status = Delete
        ? NtfsFileRecordDeleteReparsePoint(
            File,
            Buffer.data(),
            (ULONG)Buffer.size())
        : NtfsFileRecordSetReparsePoint(
            File,
            Buffer.data(),
            (ULONG)Buffer.size());
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    if (Source >= 0)
        close(Source);
    return Result;
}

static bool
ParseUnsigned(const char* Text,
              ULONGLONG Maximum,
              ULONGLONG* Value)
{
    char* End;
    ULONGLONG Parsed;

    if (!Text || !Value ||
        *Text == '\0' || *Text == '-')
    {
        return false;
    }
    errno = 0;
    Parsed = strtoull(Text, &End, 0);
    if (errno != 0 || *End != '\0' ||
        Parsed > Maximum)
    {
        return false;
    }
    *Value = Parsed;
    return true;
}

struct NtfsFormatHostContext
{
    int Descriptor;
};

static NTSTATUS
WriteFormattedImage(void* Context,
                    ULONGLONG Offset,
                    ULONG Length,
                    const void* Buffer)
{
    NtfsFormatHostContext* Host =
        static_cast<NtfsFormatHostContext*>(Context);
    const UCHAR* Bytes = static_cast<const UCHAR*>(Buffer);
    ULONG Completed = 0;

    while (Completed < Length)
    {
        ssize_t Written;

        do
        {
            Written = pwrite(Host->Descriptor,
                             Bytes + Completed,
                             Length - Completed,
                             static_cast<off_t>(Offset + Completed));
        } while (Written < 0 && errno == EINTR);

        if (Written <= 0)
        {
            if (Written == 0)
                errno = EIO;
            perror("pwrite");
            return STATUS_INVALID_DEVICE_STATE;
        }
        Completed += static_cast<ULONG>(Written);
    }
    return STATUS_SUCCESS;
}

static void*
AllocateFormatBuffer(void* Context, ULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    return malloc(Length);
}

static void
FreeFormatBuffer(void* Context, void* Buffer)
{
    UNREFERENCED_PARAMETER(Context);
    free(Buffer);
}

static int
FormatImage(const char* Path,
            ULONGLONG SectorCount,
            ULONG HiddenSectors,
            ULONG SectorsPerCluster,
            const char* Label,
            ULONG BytesPerSector)
{
    static const ULONGLONG NtEpochOffset = 11644473600ULL;
    NtfsFormatHostContext Host = {};
    NtfsFormatParameters Parameters = {};
    std::vector<WCHAR> LabelBuffer;
    NTSTATUS Status;
    time_t CurrentTime;
    int Result = 1;

    if (SectorCount > INT64_MAX / BytesPerSector ||
        BytesPerSector < 512 || BytesPerSector > 4096 ||
        (BytesPerSector & (BytesPerSector - 1)) != 0 ||
        !Utf8ToNtfsString(Label, false, true, LabelBuffer) ||
        LabelBuffer.size() > 33)
    {
        fprintf(stderr, "invalid NTFS format parameters\n");
        return 2;
    }

    Host.Descriptor = open(Path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (Host.Descriptor < 0)
    {
        fprintf(stderr, "%s: %s\n", Path, strerror(errno));
        return 1;
    }

    if (ftruncate(Host.Descriptor,
                  static_cast<off_t>(SectorCount * BytesPerSector)) != 0)
    {
        fprintf(stderr, "%s: %s\n", Path, strerror(errno));
        close(Host.Descriptor);
        return 1;
    }

    CurrentTime = time(NULL);
    if (CurrentTime == static_cast<time_t>(-1))
    {
        fprintf(stderr, "time: %s\n", strerror(errno));
        close(Host.Descriptor);
        return 1;
    }

    Parameters.TotalSectors = SectorCount;
    Parameters.BytesPerSector = BytesPerSector;
    if (BytesPerSector > NTFS_FORMAT_DEFAULT_MFT_RECORD_SIZE)
        Parameters.MftRecordSize = BytesPerSector;
    Parameters.SectorsPerCluster = SectorsPerCluster;
    Parameters.SectorsPerTrack = 63;
    Parameters.NumberOfHeads = 255;
    Parameters.HiddenSectors = HiddenSectors;
    Parameters.MediaDescriptor = 0xF8;
    Parameters.CurrentTime =
        (static_cast<ULONGLONG>(CurrentTime) + NtEpochOffset) * 10000000ULL;
    Parameters.VolumeLabel = LabelBuffer.data();
    Parameters.QuickFormat = TRUE;
    Parameters.IoContext = &Host;
    Parameters.Write = WriteFormattedImage;
    Parameters.Allocate = AllocateFormatBuffer;
    Parameters.Free = FreeFormatBuffer;

    Status = NtfsVolumeFormat(&Parameters);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: NtfsVolumeFormat failed: 0x%08" PRIx32 "\n",
                Path,
                static_cast<uint32_t>(Status));
    }
    else if (fsync(Host.Descriptor) != 0)
    {
        fprintf(stderr, "%s: %s\n", Path, strerror(errno));
    }
    else
    {
        Result = 0;
    }

    if (close(Host.Descriptor) != 0 && Result == 0)
    {
        fprintf(stderr, "%s: %s\n", Path, strerror(errno));
        Result = 1;
    }
    return Result;
}

static bool
ParseOptionalUnsigned(const char* Text,
                      ULONGLONG Maximum,
                      ULONGLONG* Value,
                      bool* Present)
{
    char* End;
    ULONGLONG Parsed;

    if (!Text || !Value || !Present)
        return false;
    if (strcmp(Text, "-") == 0)
    {
        *Present = false;
        *Value = 0;
        return true;
    }
    if (*Text == '\0' || *Text == '-')
        return false;

    errno = 0;
    Parsed = strtoull(Text, &End, 0);
    if (errno != 0 || *End != '\0' ||
        Parsed > Maximum)
    {
        return false;
    }

    *Present = true;
    *Value = Parsed;
    return true;
}

static int
WriteFileFromHost(NtfsFuseState* State,
                  const char* Path,
                  ULONGLONG Offset,
                  const char* SourcePath)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    std::vector<UCHAR> Buffer;
    struct stat SourceStat;
    LARGE_INTEGER ByteOffset;
    ULONG Length;
    ULONG Completed = 0;
    NTSTATUS Status;
    int Source = -1;
    int Result = 1;

    Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
    if (Source < 0 || fstat(Source, &SourceStat) != 0)
    {
        fprintf(stderr, "%s: %s\n", SourcePath, strerror(errno));
        goto Done;
    }
    if (SourceStat.st_size < 0 ||
        (uint64_t)SourceStat.st_size > MAXULONG)
    {
        fprintf(stderr, "%s: input is too large\n", SourcePath);
        goto Done;
    }

    Length = (ULONG)SourceStat.st_size;
    Buffer.resize(Length);
    while (Completed < Length)
    {
        ssize_t Count = read(Source,
                             Buffer.data() + Completed,
                             Length - Completed);
        if (Count > 0)
        {
            Completed += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        fprintf(stderr, "%s: incomplete input\n", SourcePath);
        goto Done;
    }

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (RequestedType != TypeData ||
        ((NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) &&
         !RequestedStream))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto NtfsError;
    }

    ByteOffset.QuadPart = (LONGLONG)Offset;
    Status = NtfsFileRecordWriteFileData(
        File,
        RequestedType,
        RequestedStream,
        Buffer.data(),
        &Length,
        &ByteOffset);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (Length != (ULONG)SourceStat.st_size)
    {
        Status = STATUS_END_OF_FILE;
        goto NtfsError;
    }

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    if (Source >= 0)
        close(Source);
    return Result;
}

static std::string
NormalizeImagePath(const std::string& Input)
{
    std::string Path = Input;

    for (char& Character : Path)
    {
        if (Character == '\\')
            Character = '/';
    }
    while (!Path.empty() && Path.front() == '/')
        Path.erase(Path.begin());
    while (!Path.empty() && Path.back() == '/')
        Path.pop_back();
    return Path;
}

struct SecurityRule
{
    bool Directory;
    bool Recursive;
    std::string Path;
    std::vector<UCHAR> Descriptor;
};

static bool
ParseSddlSid(const char*& Cursor, std::vector<UCHAR>& Sid)
{
    static const struct
    {
        const char* Alias;
        const char* Sid;
    } Aliases[] =
    {
        { "SY", "S-1-5-18" },
        { "BA", "S-1-5-32-544" },
        { "BU", "S-1-5-32-545" },
        { "CO", "S-1-3-0" },
        { "AU", "S-1-5-11" },
        { "WD", "S-1-1-0" },
        { "AC", "S-1-15-2-1" },
        { "IU", "S-1-5-4" },
        { "SU", "S-1-5-6" },
        { "LS", "S-1-5-19" },
        { "NS", "S-1-5-20" },
    };
    std::string Text;
    const char* Position;
    char* End;
    unsigned long long Value;

    if (Cursor[0] == 'S' && Cursor[1] == '-')
    {
        const char* Start = Cursor;

        Cursor++;
        while (*Cursor == '-' || isdigit((unsigned char)*Cursor))
            Cursor++;
        Text.assign(Start, Cursor);
    }
    else
    {
        for (const auto& Alias : Aliases)
        {
            if (strncmp(Cursor, Alias.Alias, 2) == 0)
            {
                Text = Alias.Sid;
                Cursor += 2;
                break;
            }
        }
        if (Text.empty())
            return false;
    }

    Position = Text.c_str() + 2;
    if (strtoull(Position, &End, 10) != 1 || *End != '-')
        return false;
    Position = End + 1;
    Value = strtoull(Position, &End, 10);
    if (End == Position)
        return false;
    Sid.assign(8, 0);
    Sid[0] = 1;
    for (int Index = 0; Index < 6; Index++)
        Sid[2 + Index] = (UCHAR)(Value >> (8 * (5 - Index)));
    while (*End == '-')
    {
        Position = End + 1;
        Value = strtoull(Position, &End, 10);
        if (End == Position || Value > MAXULONG || Sid[1] == 15)
            return false;
        for (int Index = 0; Index < 4; Index++)
            Sid.push_back((UCHAR)(Value >> (8 * Index)));
        Sid[1]++;
    }
    return *End == '\0' && Sid[1] != 0;
}

static bool
ParseSddlFlags(const std::string& Text,
               const char* const* Names,
               const ULONG* Values,
               size_t Count,
               ULONG& Result)
{
    if (Text.size() % 2 != 0)
        return false;
    for (size_t Offset = 0; Offset < Text.size(); Offset += 2)
    {
        size_t Index;

        for (Index = 0; Index < Count; Index++)
        {
            if (Text.compare(Offset, 2, Names[Index]) == 0)
                break;
        }
        if (Index == Count)
            return false;
        Result |= Values[Index];
    }
    return true;
}

static bool
ParseSddl(const std::string& Text, std::vector<UCHAR>& Descriptor)
{
    static const char* const RightNames[] =
    {
        "GA", "GX", "GW", "GR", "RC", "SD", "WD", "WO",
        "FA", "FR", "FW", "FX",
        "CC", "DC", "LC", "SW", "RP", "WP", "DT", "LO", "CR",
    };
    static const ULONG RightValues[] =
    {
        0x10000000, 0x20000000, 0x40000000, 0x80000000,
        0x00020000, 0x00010000, 0x00040000, 0x00080000,
        0x001f01ff, 0x00120089, 0x00120116, 0x001200a0,
        0x001, 0x002, 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100,
    };
    static const char* const AceFlagNames[] = { "OI", "CI", "NP", "IO", "ID" };
    static const ULONG AceFlagValues[] = { 0x01, 0x02, 0x04, 0x08, 0x10 };
    const USHORT SelfRelative = 0x8000;
    const USHORT DaclPresent = 0x0004;
    const USHORT DaclAutoInheritRequired = 0x0100;
    const USHORT DaclAutoInherited = 0x0400;
    const USHORT DaclProtected = 0x1000;
    const char* Cursor = Text.c_str();
    std::vector<UCHAR> Owner;
    std::vector<UCHAR> Group;
    std::vector<UCHAR> Acl;
    USHORT Control = SelfRelative;
    USHORT AceCount = 0;
    ULONG Offset;

    while (*Cursor)
    {
        if (Cursor[0] == 'O' && Cursor[1] == ':')
        {
            Cursor += 2;
            if (!ParseSddlSid(Cursor, Owner))
                return false;
        }
        else if (Cursor[0] == 'G' && Cursor[1] == ':')
        {
            Cursor += 2;
            if (!ParseSddlSid(Cursor, Group))
                return false;
        }
        else if (Cursor[0] == 'D' && Cursor[1] == ':' && Acl.empty())
        {
            Cursor += 2;
            Control |= DaclPresent;
            while (*Cursor && *Cursor != '(')
            {
                if (Cursor[0] == 'P')
                {
                    Control |= DaclProtected;
                    Cursor++;
                }
                else if (Cursor[0] == 'A' && Cursor[1] == 'I')
                {
                    Control |= DaclAutoInherited;
                    Cursor += 2;
                }
                else if (Cursor[0] == 'A' && Cursor[1] == 'R')
                {
                    Control |= DaclAutoInheritRequired;
                    Cursor += 2;
                }
                else
                {
                    return false;
                }
            }

            Acl.assign(8, 0);
            while (*Cursor == '(')
            {
                const char* Close = strchr(Cursor, ')');
                std::string Fields[6];
                std::vector<UCHAR> Sid;
                const char* SidCursor;
                size_t Field = 0;
                ULONG Flags = 0;
                ULONG Mask = 0;
                USHORT AceSize;
                UCHAR Type;

                if (!Close)
                    return false;
                for (const char* Position = Cursor + 1;
                     Position < Close;
                     Position++)
                {
                    if (*Position != ';')
                        Fields[Field] += *Position;
                    else if (++Field == 6)
                        return false;
                }
                Cursor = Close + 1;

                if (Field != 5 || !Fields[3].empty() || !Fields[4].empty())
                    return false;
                if (Fields[0] == "A")
                    Type = 0;
                else if (Fields[0] == "D")
                    Type = 1;
                else
                    return false;
                if (!ParseSddlFlags(Fields[1],
                                    AceFlagNames,
                                    AceFlagValues,
                                    RTL_NUMBER_OF(AceFlagNames),
                                    Flags))
                {
                    return false;
                }
                if (Fields[2].compare(0, 2, "0x") == 0)
                {
                    char* End;

                    Mask = (ULONG)strtoul(Fields[2].c_str(), &End, 16);
                    if (*End != '\0')
                        return false;
                }
                else if (!ParseSddlFlags(Fields[2],
                                         RightNames,
                                         RightValues,
                                         RTL_NUMBER_OF(RightNames),
                                         Mask))
                {
                    return false;
                }
                SidCursor = Fields[5].c_str();
                if (Mask == 0 ||
                    !ParseSddlSid(SidCursor, Sid) ||
                    *SidCursor != '\0' ||
                    Acl.size() + 8 + Sid.size() > MAXUSHORT)
                {
                    return false;
                }

                AceSize = (USHORT)(8 + Sid.size());
                Acl.push_back(Type);
                Acl.push_back((UCHAR)Flags);
                Acl.push_back((UCHAR)AceSize);
                Acl.push_back((UCHAR)(AceSize >> 8));
                for (int Index = 0; Index < 4; Index++)
                    Acl.push_back((UCHAR)(Mask >> (8 * Index)));
                Acl.insert(Acl.end(), Sid.begin(), Sid.end());
                AceCount++;
            }
            Acl[0] = 2;
            Acl[2] = (UCHAR)Acl.size();
            Acl[3] = (UCHAR)(Acl.size() >> 8);
            Acl[4] = (UCHAR)AceCount;
            Acl[5] = (UCHAR)(AceCount >> 8);
        }
        else
        {
            return false;
        }
    }
    if (Owner.empty() || Group.empty() || Acl.empty())
        return false;

    Descriptor.assign(20, 0);
    Descriptor[0] = 1;
    Descriptor[2] = (UCHAR)Control;
    Descriptor[3] = (UCHAR)(Control >> 8);
    Offset = 20;
    for (int Index = 0; Index < 4; Index++)
        Descriptor[16 + Index] = (UCHAR)(Offset >> (8 * Index));
    Descriptor.insert(Descriptor.end(), Acl.begin(), Acl.end());
    Offset += (ULONG)Acl.size();
    for (int Index = 0; Index < 4; Index++)
        Descriptor[4 + Index] = (UCHAR)(Offset >> (8 * Index));
    Descriptor.insert(Descriptor.end(), Owner.begin(), Owner.end());
    Offset += (ULONG)Owner.size();
    for (int Index = 0; Index < 4; Index++)
        Descriptor[8 + Index] = (UCHAR)(Offset >> (8 * Index));
    Descriptor.insert(Descriptor.end(), Group.begin(), Group.end());
    return true;
}

static std::string
SecurityRulePath(const std::string& Input)
{
    std::string Path = NormalizeImagePath(Input);

    for (char& Character : Path)
        Character = (char)tolower((unsigned char)Character);
    return Path;
}

static bool
LoadSecurityRules(const char* PolicyPath, std::vector<SecurityRule>& Rules)
{
    std::unordered_map<std::string, std::string> Definitions;
    FILE* Policy = fopen(PolicyPath, "rb");
    char* Line = NULL;
    size_t Capacity = 0;
    unsigned long LineNumber = 0;
    bool Result = false;

    if (!Policy)
    {
        fprintf(stderr, "%s: %s\n", PolicyPath, strerror(errno));
        return false;
    }

    for (;;)
    {
        SecurityRule Rule;
        std::string Kind;
        std::string Name;
        std::string Value;
        char* Separator;
        char* Space;
        ssize_t Length;
        size_t Open;

        Length = getline(&Line, &Capacity, Policy);
        if (Length < 0)
        {
            Result = feof(Policy) != 0;
            break;
        }
        LineNumber++;
        while (Length > 0 &&
               (Line[Length - 1] == '\n' ||
                Line[Length - 1] == '\r' ||
                Line[Length - 1] == ' '))
        {
            Line[--Length] = '\0';
        }
        if (Length == 0)
            continue;

        Separator = strchr(Line, '=');
        Space = strchr(Line, ' ');
        if (!Separator || !Space || Space > Separator)
            break;
        Kind.assign(Line, Space);
        Name.assign(Space + 1, Separator);
        Value = Separator + 1;
        while ((Open = Value.find('{')) != std::string::npos)
        {
            size_t Close = Value.find('}', Open);
            std::unordered_map<std::string, std::string>::iterator Found;

            if (Close == std::string::npos)
                break;
            Found = Definitions.find(Value.substr(Open + 1, Close - Open - 1));
            if (Found == Definitions.end())
                break;
            Value.replace(Open, Close - Open + 1, Found->second);
        }
        if (Open != std::string::npos)
            break;

        if (Kind == "define")
        {
            Definitions[Name] = Value;
            continue;
        }
        if (Kind == "dir" || Kind == "dirs")
            Rule.Directory = true;
        else if (Kind == "file" || Kind == "files")
            Rule.Directory = false;
        else
            break;
        Rule.Recursive = Kind.back() == 's';
        Rule.Path = SecurityRulePath(Name);
        if (!ParseSddl(Value, Rule.Descriptor))
            break;
        Rules.push_back(std::move(Rule));
    }

    if (!Result)
    {
        fprintf(stderr,
                "%s:%lu: invalid security rule\n",
                PolicyPath,
                LineNumber);
    }
    free(Line);
    fclose(Policy);
    return Result;
}

static const SecurityRule*
FindSecurityRule(const std::vector<SecurityRule>& Rules,
                 const std::string& Path,
                 bool Directory)
{
    const SecurityRule* Best = NULL;

    for (const SecurityRule& Rule : Rules)
    {
        if (Rule.Directory != Directory)
            continue;
        if (!Rule.Recursive)
        {
            if (Rule.Path == Path)
                Best = &Rule;
            continue;
        }
        if (Best && (!Best->Recursive || Best->Path.size() > Rule.Path.size()))
            continue;
        if (Rule.Path.empty()
                ? !Path.empty()
                : (Path.size() > Rule.Path.size() &&
                   Path[Rule.Path.size()] == '/' &&
                   Path.compare(0, Rule.Path.size(), Rule.Path) == 0))
        {
            Best = &Rule;
        }
    }
    return Best;
}

static NTSTATUS
ApplySecurityRules(NtfsFuseState* State,
                   const std::vector<SecurityRule>& Rules,
                   const std::string& Path,
                   bool IsDirectory)
{
    std::vector<std::pair<std::string, bool>> Children;
    const SecurityRule* Rule;
    PNtfsFileRecord File = NULL;
    PNtfsDirectory Directory = NULL;
    BOOLEAN Restart = TRUE;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path.empty() ? "/" : Path.c_str(), &File);
    if (NT_SUCCESS(Status))
    {
        Rule = FindSecurityRule(Rules, SecurityRulePath(Path), IsDirectory);
        if (Rule)
        {
            Status = NtfsFileRecordSetSecurityDescriptor(
                File,
                Rule->Descriptor.data(),
                (ULONG)Rule->Descriptor.size());
        }
    }
    if (NT_SUCCESS(Status) && IsDirectory)
    {
        Directory = NtfsDirectoryCreate(State->Volume);
        Status = Directory
            ? NtfsDirectoryLoadDirectory(Directory, File)
            : STATUS_INSUFFICIENT_RESOURCES;
        while (NT_SUCCESS(Status))
        {
            std::unique_ptr<NtfsDirectoryEntry> Entry(new NtfsDirectoryEntry);
            std::string Name;

            Status = NtfsDirectoryReadNext(Directory, Restart, Entry.get());
            Restart = FALSE;
            if (Status == STATUS_NO_MORE_FILES)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            if (!NT_SUCCESS(Status))
                break;
            if (!Utf16ToUtf8(Entry->Name, Entry->NameLength, Name))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                break;
            }
            Children.emplace_back(
                Path.empty() ? Name : Path + "/" + Name,
                (Entry->FileAttributes & FN_DIRECTORY) != 0);
        }
        NtfsDirectoryDestroy(Directory);
    }
    NtfsFileRecordDestroy(File);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path.empty() ? "/" : Path.c_str(),
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return Status;
    }

    for (const auto& Child : Children)
    {
        Status = ApplySecurityRules(State, Rules, Child.first, Child.second);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static int
ApplySecurityFromPolicy(NtfsFuseState* State, const char* PolicyPath)
{
    std::vector<SecurityRule> Rules;

    if (!LoadSecurityRules(PolicyPath, Rules))
        return 1;
    return NT_SUCCESS(ApplySecurityRules(State, Rules, std::string(), true))
        ? 0
        : 1;
}

static NTSTATUS
EnsureImageDirectory(NtfsFuseState* State,
                     const std::string& Directory,
                     BOOLEAN CaseSensitive)
{
    std::string Path = NormalizeImagePath(Directory);
    size_t End = 0;

    while (End < Path.size())
    {
        PNtfsFileRecord File = NULL;
        std::vector<WCHAR> NtfsPath;
        std::string Component;
        NTSTATUS Status;

        End = Path.find('/', End);
        if (End == std::string::npos)
            End = Path.size();
        Component = Path.substr(0, End);

        Status = Lookup(State->Volume, Component.c_str(), &File);
        if (NT_SUCCESS(Status))
        {
            if (!(NtfsFileRecordGetHeader(File)->Flags &
                  FR_IS_DIRECTORY))
            {
                NtfsFileRecordDestroy(File);
                return STATUS_NOT_A_DIRECTORY;
            }
            NtfsFileRecordDestroy(File);
        }
        else
        {
            if (Status != STATUS_NOT_FOUND)
                return Status;
            if (!Utf8ToNtfsPath(Component.c_str(), NtfsPath) ||
                NtfsPath.size() < 2 ||
                NtfsPath.size() - 1 > MAXULONG)
            {
                return STATUS_OBJECT_NAME_INVALID;
            }
            Status = NtfsMasterFileTableCreateFile(
                NtfsVolumeGetMft(State->Volume),
                NtfsPath.data(),
                (ULONG)(NtfsPath.size() - 1),
                TRUE,
                0,
                &File);
            if (NT_SUCCESS(Status) && CaseSensitive)
                Status = NtfsFileRecordSetCaseSensitiveDirectory(File, TRUE);
            NtfsFileRecordDestroy(File);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        if (End < Path.size())
            End++;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
PrepareImageFile(NtfsFuseState* State,
                 const std::string& Path)
{
    PNtfsFileRecord File = NULL;
    std::vector<WCHAR> NtfsPath;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path.c_str(), &File);
    if (NT_SUCCESS(Status))
    {
        if (NtfsFileRecordGetHeader(File)->Flags &
            FR_IS_DIRECTORY)
        {
            NtfsFileRecordDestroy(File);
            return STATUS_FILE_IS_A_DIRECTORY;
        }
        Status = NtfsFileRecordSetFileDataSize(
            File,
            TypeData,
            NULL,
            0);
        NtfsFileRecordDestroy(File);
        return Status;
    }
    if (Status != STATUS_NOT_FOUND)
        return Status;
    if (!Utf8ToNtfsPath(Path.c_str(), NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    Status = NtfsMasterFileTableCreateFile(
        NtfsVolumeGetMft(State->Volume),
        NtfsPath.data(),
        (ULONG)(NtfsPath.size() - 1),
        FALSE,
        0,
        &File);
    NtfsFileRecordDestroy(File);
    return Status;
}

static NTSTATUS
CopyHostFileToImage(NtfsFuseState* State,
                    const std::string& ImagePath,
                    const std::string& SourcePath,
                    std::vector<UCHAR>& Buffer)
{
    PNtfsFileRecord File = NULL;
    LARGE_INTEGER Offset = {};
    FILE* Source;
    NTSTATUS Status;

    Source = fopen(SourcePath.c_str(), "rb");
    if (!Source)
        return STATUS_NOT_FOUND;

    Status = Lookup(State->Volume, ImagePath.c_str(), &File);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        size_t ReadLength = fread(Buffer.data(),
                                  1,
                                  Buffer.size(),
                                  Source);
        if (ReadLength != 0)
        {
            ULONG WriteLength = (ULONG)ReadLength;

            Status = NtfsFileRecordWriteFileData(
                File,
                TypeData,
                NULL,
                Buffer.data(),
                &WriteLength,
                &Offset);
            if (!NT_SUCCESS(Status))
                goto Done;
            if (WriteLength != ReadLength)
            {
                Status = STATUS_END_OF_FILE;
                goto Done;
            }
            Offset.QuadPart += WriteLength;
        }
        if (ReadLength != Buffer.size())
        {
            Status = ferror(Source)
                ? STATUS_INVALID_DEVICE_STATE
                : STATUS_SUCCESS;
            break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        PAttribute DataAttribute =
            NtfsFileRecordGetAttribute(File, TypeData, NULL);

        if (DataAttribute && DataAttribute->IsNonResident)
        {
            Status = NtfsFileRecordSetFileAllocationSize(
                File,
                TypeData,
                NULL,
                (ULONGLONG)Offset.QuadPart);
        }
    }

Done:
    NtfsFileRecordDestroy(File);
    fclose(Source);
    return Status;
}

#ifndef FILE_ATTRIBUTE_SYSTEM
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#endif
#ifndef FILE_ATTRIBUTE_ARCHIVE
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#endif

static UINT64
UnixTimeToNtfsTime(int64_t Seconds)
{
    return (UINT64)((Seconds * 10000000LL) + 116444736000000000LL);
}

static std::string
EncodeLinuxName(const std::string& Path)
{
    std::string Result;

    for (unsigned char Character : Path)
    {
        if (Character == ':' || Character == '*' || Character == '?' ||
            Character == '"' || Character == '<' || Character == '>' ||
            Character == '|' || Character == '\\' ||
            (Character >= 1 && Character < 0x20))
        {
            unsigned int Code = 0xF000 + Character;

            Result.push_back((char)(0xE0 | (Code >> 12)));
            Result.push_back((char)(0x80 | ((Code >> 6) & 0x3F)));
            Result.push_back((char)(0x80 | (Code & 0x3F)));
        }
        else
        {
            Result.push_back((char)Character);
        }
    }
    return Result;
}

static NTSTATUS
SetLinuxAttributes(NtfsFuseState* State,
                   const std::string& Path,
                   UINT32 Mode,
                   UINT32 Uid,
                   UINT32 Gid,
                   UINT32 Major,
                   UINT32 Minor,
                   bool Device)
{
    static const UCHAR UidName[] = "$LXUID";
    static const UCHAR GidName[] = "$LXGID";
    static const UCHAR ModName[] = "$LXMOD";
    static const UCHAR DevName[] = "$LXDEV";
    NtfsExtendedAttributeUpdate Updates[4] = {};
    PNtfsFileRecord File = NULL;
    UINT32 DeviceValue[2] = { Major, Minor };
    ULONG Count = 3;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path.c_str(), &File);
    if (!NT_SUCCESS(Status))
        return Status;

    Updates[0].Operation = NTFS_EA_UPDATE_SET;
    Updates[0].NameLength = (UINT8)(sizeof(UidName) - 1);
    Updates[0].ValueLength = sizeof(Uid);
    Updates[0].Name = UidName;
    Updates[0].Value = (const UCHAR*)&Uid;
    Updates[1].Operation = NTFS_EA_UPDATE_SET;
    Updates[1].NameLength = (UINT8)(sizeof(GidName) - 1);
    Updates[1].ValueLength = sizeof(Gid);
    Updates[1].Name = GidName;
    Updates[1].Value = (const UCHAR*)&Gid;
    Updates[2].Operation = NTFS_EA_UPDATE_SET;
    Updates[2].NameLength = (UINT8)(sizeof(ModName) - 1);
    Updates[2].ValueLength = sizeof(Mode);
    Updates[2].Name = ModName;
    Updates[2].Value = (const UCHAR*)&Mode;
    if (Device)
    {
        Updates[3].Operation = NTFS_EA_UPDATE_SET;
        Updates[3].NameLength = (UINT8)(sizeof(DevName) - 1);
        Updates[3].ValueLength = sizeof(DeviceValue);
        Updates[3].Name = DevName;
        Updates[3].Value = (const UCHAR*)DeviceValue;
        Count = 4;
    }

    Status = NtfsFileRecordUpdateExtendedAttributes(File, Updates, Count);
    NtfsFileRecordDestroy(File);
    return Status;
}

static NTSTATUS
SetLinuxTimes(NtfsFuseState* State,
              const std::string& Path,
              bool Special,
              int64_t MTime)
{
    NtfsFileBasicInformation Basic = {};
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path.c_str(), &File);
    if (!NT_SUCCESS(Status))
        return Status;

    Basic.Fields = NTFS_BASIC_INFO_ALL_FIELDS;
    Basic.CreationTime = UnixTimeToNtfsTime(MTime);
    Basic.LastAccessTime = Basic.CreationTime;
    Basic.LastWriteTime = Basic.CreationTime;
    Basic.ChangeTime = Basic.CreationTime;
    Basic.FileAttributes = (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY)
        ? FILE_ATTRIBUTE_DIRECTORY
        : (Special ? FILE_ATTRIBUTE_SYSTEM : FILE_ATTRIBUTE_ARCHIVE);
    Status = NtfsFileRecordSetBasicInformation(File, &Basic);
    NtfsFileRecordDestroy(File);
    return Status;
}

static NTSTATUS
WriteImageBytes(NtfsFuseState* State,
                const std::string& Path,
                const std::string& Data)
{
    PNtfsFileRecord File = NULL;
    LARGE_INTEGER Offset = {};
    ULONG Length = (ULONG)Data.size();
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path.c_str(), &File);
    if (NT_SUCCESS(Status) && Length != 0)
    {
        Status = NtfsFileRecordWriteFileData(File,
                                             TypeData,
                                             NULL,
                                             (PUCHAR)Data.data(),
                                             &Length,
                                             &Offset);
    }
    NtfsFileRecordDestroy(File);
    return Status;
}

static int
AddLinuxTreeFromList(NtfsFuseState* State,
                     const char* ListPath)
{
    static const size_t CopyBufferSize = 1024 * 1024;
    FILE* List = fopen(ListPath, "rb");
    char* Line = NULL;
    size_t Capacity = 0;
    unsigned long LineNumber = 0;
    unsigned long Failures = 0;

    if (!List)
    {
        fprintf(stderr, "%s: %s\n", ListPath, strerror(errno));
        return 1;
    }

    std::vector<UCHAR> CopyBuffer(CopyBufferSize);
    std::map<std::string, std::tuple<UINT32, UINT32, UINT32, std::string>> Sources;

    for (;;)
    {
        std::vector<std::string> Fields;
        std::string Destination;
        std::string Parent;
        ssize_t Length;
        NTSTATUS Status = STATUS_SUCCESS;
        UINT32 Mode, Uid, Gid, Major, Minor;
        int64_t MTime;
        char Type;
        size_t Start = 0;

        Length = getline(&Line, &Capacity, List);
        if (Length < 0)
            break;
        LineNumber++;
        while (Length > 0 && (Line[Length - 1] == '\n' || Line[Length - 1] == '\r'))
            Line[--Length] = '\0';
        if (Length == 0)
            continue;

        for (size_t Index = 0; Index <= (size_t)Length; Index++)
        {
            if (Index == (size_t)Length || (Line[Index] == '\t' && Fields.size() < 8))
            {
                Fields.push_back(std::string(Line + Start, Index - Start));
                Start = Index + 1;
            }
        }
        if (Fields.size() != 9 || Fields[0].size() != 1)
        {
            fprintf(stderr, "%s:%lu: malformed entry\n", ListPath, LineNumber);
            Failures++;
            continue;
        }

        Type = Fields[0][0];
        Mode = (UINT32)strtoul(Fields[1].c_str(), NULL, 8);
        Uid = (UINT32)strtoul(Fields[2].c_str(), NULL, 10);
        Gid = (UINT32)strtoul(Fields[3].c_str(), NULL, 10);
        Major = (UINT32)strtoul(Fields[4].c_str(), NULL, 10);
        Minor = (UINT32)strtoul(Fields[5].c_str(), NULL, 10);
        MTime = strtoll(Fields[6].c_str(), NULL, 10);
        Destination = NormalizeImagePath(EncodeLinuxName(Fields[7]));
        if (Destination.empty())
            continue;

        size_t Slash = Destination.rfind('/');
        if (Slash != std::string::npos)
            Parent = Destination.substr(0, Slash);

        switch (Type)
        {
            case 'd':
                Mode |= 0040000;
                break;
            case 'f':
                Mode |= 0100000;
                break;
            case 'l':
                Mode |= 0120000;
                break;
            case 'c':
                Mode |= 0020000;
                break;
            case 'b':
                Mode |= 0060000;
                break;
            case 'p':
                Mode |= 0010000;
                break;
            case 's':
                Mode |= 0140000;
                break;
        }

        if (Type == 'h')
        {
            std::vector<WCHAR> OldPath, NewPath;
            std::string Existing = NormalizeImagePath(EncodeLinuxName(Fields[8]));

            Status = EnsureImageDirectory(State, Parent, TRUE);
            if (NT_SUCCESS(Status) &&
                (!Utf8ToNtfsPath(Existing.c_str(), OldPath) ||
                 !Utf8ToNtfsPath(Destination.c_str(), NewPath)))
            {
                Status = STATUS_OBJECT_NAME_INVALID;
            }
            if (NT_SUCCESS(Status))
            {
                Status = NtfsMasterFileTableCreateHardLink(
                    NtfsVolumeGetMft(State->Volume),
                    OldPath.data(),
                    (ULONG)(OldPath.size() - 1),
                    NewPath.data(),
                    (ULONG)(NewPath.size() - 1));
            }
            if (!NT_SUCCESS(Status))
            {
                auto Original = Sources.find(Existing);

                if (Original != Sources.end())
                {
                    Status = PrepareImageFile(State, Destination);
                    if (NT_SUCCESS(Status))
                        Status = SetLinuxAttributes(State, Destination, std::get<0>(Original->second),
                                                    std::get<1>(Original->second), std::get<2>(Original->second),
                                                    0, 0, false);
                    if (NT_SUCCESS(Status))
                        Status = CopyHostFileToImage(State, Destination, std::get<3>(Original->second), CopyBuffer);
                    if (NT_SUCCESS(Status))
                        Status = SetLinuxTimes(State, Destination, false, MTime);
                }
            }
            if (!NT_SUCCESS(Status))
            {
                fprintf(stderr, "%s:%lu: %s: ntfslib status 0x%08" PRIx32 "\n",
                        ListPath, LineNumber, Destination.c_str(), (uint32_t)Status);
                Failures++;
            }
            continue;
        }

        if (Type == 'd')
        {
            Status = EnsureImageDirectory(State, Destination, TRUE);
        }
        else
        {
            Status = EnsureImageDirectory(State, Parent, TRUE);
            if (NT_SUCCESS(Status))
                Status = PrepareImageFile(State, Destination);
        }
        if (NT_SUCCESS(Status))
            Status = SetLinuxAttributes(State, Destination, Mode, Uid, Gid, Major, Minor, (Type == 'c') || (Type == 'b'));
        if (NT_SUCCESS(Status) && Type == 'f')
        {
            Status = CopyHostFileToImage(State, Destination, Fields[8], CopyBuffer);
            Sources[Destination] = std::make_tuple(Mode, Uid, Gid, Fields[8]);
        }
        if (NT_SUCCESS(Status) && Type == 'l')
            Status = WriteImageBytes(State, Destination, Fields[8]);
        if (NT_SUCCESS(Status))
            Status = SetLinuxTimes(State, Destination, (Type != 'd') && (Type != 'f'), MTime);

        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr, "%s:%lu: %s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    ListPath, LineNumber, Destination.c_str(), (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            Failures++;
        }
    }

    free(Line);
    fclose(List);
    fprintf(stderr, "%s: %lu entries, %lu failures\n", ListPath, LineNumber, Failures);
    return Failures ? 1 : 0;
}

static int
AddFilesFromList(NtfsFuseState* State,
                 const char* ListPath)
{
    static const size_t CopyBufferSize = 1024 * 1024;
    FILE* List = fopen(ListPath, "rb");
    char* Line = NULL;
    size_t Capacity = 0;
    unsigned long LineNumber = 0;
    int Result = 1;

    if (!List)
    {
        fprintf(stderr, "%s: %s\n", ListPath, strerror(errno));
        return 1;
    }

    // Unoptimized libc++ builds may initialize and destroy vector elements
    // individually, so reuse this large buffer for the whole import.
    std::vector<UCHAR> CopyBuffer(CopyBufferSize);

    for (;;)
    {
        struct stat SourceStat;
        std::string Destination;
        std::string Parent;
        std::string Source;
        char* Separator;
        ssize_t Length;
        NTSTATUS Status;

        errno = 0;
        Length = getline(&Line, &Capacity, List);
        if (Length < 0)
        {
            if (feof(List))
            {
                Result = 0;
                break;
            }
            fprintf(stderr, "%s: %s\n", ListPath, strerror(errno));
            break;
        }
        LineNumber++;
        while (Length > 0 &&
               (Line[Length - 1] == '\n' ||
                Line[Length - 1] == '\r'))
        {
            Line[--Length] = '\0';
        }
        if (Length == 0)
            continue;

        Separator = strstr(Line, "=/");
        if (!Separator)
            Separator = strchr(Line, '=');
        if (!Separator)
        {
            fprintf(stderr,
                    "%s:%lu: missing '='\n",
                    ListPath,
                    LineNumber);
            break;
        }
        *Separator = '\0';
        Destination = NormalizeImagePath(Line);
        Source = Separator + 1;
        if (Destination.empty() || Source.empty())
        {
            fprintf(stderr,
                    "%s:%lu: empty path\n",
                    ListPath,
                    LineNumber);
            break;
        }
        if (stat(Source.c_str(), &SourceStat) != 0)
        {
            fprintf(stderr,
                    "%s:%lu: %s: %s\n",
                    ListPath,
                    LineNumber,
                    Source.c_str(),
                    strerror(errno));
            break;
        }

        if (S_ISDIR(SourceStat.st_mode))
        {
            Status = EnsureImageDirectory(State, Destination, FALSE);
        }
        else if (S_ISREG(SourceStat.st_mode))
        {
            size_t Slash = Destination.rfind('/');

            if (Slash != std::string::npos)
                Parent = Destination.substr(0, Slash);
            Status = EnsureImageDirectory(State, Parent, FALSE);
            if (NT_SUCCESS(Status))
                Status = PrepareImageFile(State, Destination);
            if (NT_SUCCESS(Status))
            {
                Status = CopyHostFileToImage(
                    State,
                    Destination,
                    Source,
                    CopyBuffer);
            }
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
        }

        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s:%lu: %s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    ListPath,
                    LineNumber,
                    Destination.c_str(),
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            break;
        }
    }

    free(Line);
    fclose(List);
    return Result;
}

static int
SetFileSize(NtfsFuseState* State,
            const char* Path,
            ULONGLONG NewSize,
            bool AllocationOnly)
{
    PNtfsFileRecord File = NULL;
    PAttribute DataAttribute;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;
    int Result = 1;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (RequestedType != TypeData ||
        ((NtfsFileRecordGetHeader(File)->Flags &
          FR_IS_DIRECTORY) &&
         !RequestedStream))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto NtfsError;
    }

    DataAttribute = NtfsFileRecordGetAttribute(
        File,
        RequestedType,
        RequestedStream);
    if (!DataAttribute)
    {
        Status = STATUS_NOT_FOUND;
        goto NtfsError;
    }

    Status = AllocationOnly
        ? NtfsFileRecordSetFileAllocationSize(
            File,
            RequestedType,
            RequestedStream,
            NewSize)
        : NtfsFileRecordSetFileDataSize(
            File,
            RequestedType,
            RequestedStream,
            NewSize);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return Result;
}

static int
SetSparseState(NtfsFuseState* State,
               const char* Path,
               bool SetSparse)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetSparse(
            File,
            RequestedType,
            RequestedStream,
            SetSparse ? TRUE : FALSE);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
SetZeroDataRange(NtfsFuseState* State,
                 const char* Path,
                 ULONGLONG FileOffset,
                 ULONGLONG BeyondFinalZero)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetZeroData(
            File,
            RequestedType,
            RequestedStream,
            FileOffset,
            BeyondFinalZero);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
PrintAllocatedRanges(NtfsFuseState* State,
                     const char* Path,
                     ULONGLONG FileOffset,
                     ULONGLONG Length)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    std::vector<NtfsAllocatedRange> Ranges(16);
    ULONG Count;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        Count = (ULONG)Ranges.size();
        Status = NtfsFileRecordQueryAllocatedRanges(
            File,
            RequestedType,
            RequestedStream,
            FileOffset,
            Length,
            Ranges.data(),
            &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL &&
            Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        if (Ranges.size() >
            MAXULONG / (2 * sizeof(NtfsAllocatedRange)))
        {
            Status = STATUS_FILE_TOO_LARGE;
            break;
        }
        Ranges.resize(Ranges.size() * 2);
    }

    if (NT_SUCCESS(Status))
    {
        for (ULONG Index = 0; Index < Count; Index++)
        {
            printf("%" PRIu64 " %" PRIu64 "\n",
                   (uint64_t)Ranges[Index].FileOffset,
                   (uint64_t)Ranges[Index].Length);
        }
    }

Done:
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
PrintRetrievalPointers(NtfsFuseState* State,
                       const char* Path,
                       ULONGLONG StartingVcn)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    /* Start with one entry so focused fragmented-stream tests also exercise
     * the STATUS_BUFFER_OVERFLOW continuation contract.
     */
    std::vector<NtfsRetrievalExtent> Extents(1);
    ULONGLONG ReturnedStartingVcn = 0;
    ULONG Count;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        Count = (ULONG)Extents.size();
        Status = NtfsFileRecordQueryRetrievalPointers(
            File,
            RequestedType,
            RequestedStream,
            StartingVcn,
            &ReturnedStartingVcn,
            Extents.data(),
            &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL &&
            Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        if (Extents.size() >
            MAXULONG /
                (2 * sizeof(NtfsRetrievalExtent)))
        {
            Status = STATUS_FILE_TOO_LARGE;
            break;
        }
        Extents.resize(Extents.size() * 2);
    }

    if (NT_SUCCESS(Status))
    {
        printf("start %" PRIu64 "\n",
               (uint64_t)ReturnedStartingVcn);
        for (ULONG Index = 0; Index < Count; Index++)
        {
            printf("%" PRIu64 " %" PRId64 "\n",
                   (uint64_t)Extents[Index].NextVcn,
                   (int64_t)Extents[Index].Lcn);
        }
    }

Done:
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
PrintVolumeBitmap(NtfsFuseState* State,
                  ULONGLONG StartingLcn,
                  ULONG ByteCount)
{
    std::vector<UCHAR> Bitmap(ByteCount);
    ULONGLONG ReturnedStartingLcn = 0;
    ULONGLONG BitmapSize = 0;
    ULONG Length = ByteCount;
    NTSTATUS Status;

    Status = NtfsVolumeReadBitmap(
        State->Volume,
        StartingLcn,
        &ReturnedStartingLcn,
        &BitmapSize,
        Bitmap.empty() ? NULL : Bitmap.data(),
        &Length);
    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        printf("%" PRIu64 " %" PRIu64 " ",
               (uint64_t)ReturnedStartingLcn,
               (uint64_t)BitmapSize);
        for (ULONG Index = 0; Index < Length; Index++)
            printf("%02x", Bitmap[Index]);
        putchar('\n');
        return 0;
    }

    fprintf(stderr,
            "volume bitmap: ntfslib status 0x%08"
            PRIx32 " (%s)\n",
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));
    return 1;
}

static int
PrintVolumeData(NtfsFuseState* State)
{
    NtfsVolumeInformation Information = {};
    NTSTATUS Status;

    Status = NtfsVolumeQueryInformation(
        State->Volume,
        &Information);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "volume data: ntfslib status 0x%08"
                PRIx32 " (%s)\n",
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }

    printf("serial %" PRIu64 "\n",
           (uint64_t)Information.VolumeSerialNumber);
    printf("sectors %" PRIu64 "\n",
           (uint64_t)Information.NumberSectors);
    printf("clusters %" PRIu64 "\n",
           (uint64_t)Information.TotalClusters);
    printf("free-clusters %" PRIu64 "\n",
           (uint64_t)Information.FreeClusters);
    printf("reserved-clusters %" PRIu64 "\n",
           (uint64_t)Information.TotalReserved);
    printf("bytes-per-sector %" PRIu32 "\n",
           (uint32_t)Information.BytesPerSector);
    printf("bytes-per-cluster %" PRIu32 "\n",
           (uint32_t)Information.BytesPerCluster);
    printf("bytes-per-file-record %" PRIu32 "\n",
           (uint32_t)Information.BytesPerFileRecordSegment);
    printf("clusters-per-file-record %" PRIu32 "\n",
           (uint32_t)Information.ClustersPerFileRecordSegment);
    printf("mft-valid-bytes %" PRIu64 "\n",
           (uint64_t)Information.MftValidDataLength);
    printf("mft-lcn %" PRIu64 "\n",
           (uint64_t)Information.MftStartLcn);
    printf("mftmirr-lcn %" PRIu64 "\n",
           (uint64_t)Information.Mft2StartLcn);
    printf("mft-zone-start %" PRIu64 "\n",
           (uint64_t)Information.MftZoneStart);
    printf("mft-zone-end %" PRIu64 "\n",
           (uint64_t)Information.MftZoneEnd);
    printf("version %" PRIu16 ".%" PRIu16 "\n",
           (uint16_t)Information.MajorVersion,
           (uint16_t)Information.MinorVersion);
    return 0;
}

static int
PrintDataStreams(NtfsFuseState* State,
                 const char* Path)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    std::vector<NtfsDataStreamInformation> Streams(1);
    ULONG Count;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        Count = (ULONG)Streams.size();
        Status = NtfsFileRecordQueryDataStreams(
            File,
            Streams.data(),
            &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL &&
            Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        if (Streams.size() >
            MAXULONG /
                (2 * sizeof(NtfsDataStreamInformation)))
        {
            Status = STATUS_FILE_TOO_LARGE;
            break;
        }
        Streams.resize(Streams.size() * 2);
    }

    if (NT_SUCCESS(Status))
    {
        for (ULONG Index = 0; Index < Count; Index++)
        {
            std::string Name;

            if (!Utf16ToUtf8(
                    Streams[Index].Name,
                    Streams[Index].NameLength,
                    Name))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                break;
            }
            printf(":%s:$DATA %" PRIu64 " %" PRIu64 "\n",
                   Name.c_str(),
                   (uint64_t)Streams[Index].DataSize,
                   (uint64_t)Streams[Index].AllocationSize);
        }
    }

Done:
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
DumpMftRecord(NtfsFuseState* State,
              ULONGLONG RequestedFileReference)
{
    PNtfsMasterFileTable Mft =
        NtfsVolumeGetMft(State->Volume);
    std::vector<UCHAR> Record;
    ULONGLONG ReturnedFileReference = 0;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = NtfsMasterFileTableReadFileRecord(
        Mft,
        RequestedFileReference,
        &ReturnedFileReference,
        NULL,
        &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL ||
        Length == 0)
    {
        goto Failed;
    }

    Record.resize(Length);
    Status = NtfsMasterFileTableReadFileRecord(
        Mft,
        RequestedFileReference,
        &ReturnedFileReference,
        Record.data(),
        &Length);
    if (!NT_SUCCESS(Status))
        goto Failed;
    if (fwrite(Record.data(), 1, Length, stdout) !=
        Length)
    {
        fprintf(stderr,
                "MFT record: %s\n",
                strerror(errno));
        return 1;
    }
    fprintf(stderr,
            "file-record %" PRIu64 " %" PRIu32 "\n",
            (uint64_t)ReturnedFileReference,
            (uint32_t)Length);
    return 0;

Failed:
    fprintf(stderr,
            "MFT record: ntfslib status 0x%08"
            PRIx32 " (%s)\n",
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));
    return 1;
}

static int
DeleteExternalBacking(NtfsFuseState* State,
                      const char* Path)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;
    int Result = 1;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (RequestedType != TypeData ||
        RequestedStream ||
        (NtfsFileRecordGetHeader(File)->Flags &
         FR_IS_DIRECTORY))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto NtfsError;
    }

    Status = NtfsFileRecordDeleteExternalBacking(
        File);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return Result;
}

#if NTFSLIB_HAVE_FUSE
/*
 * Mounted operation uses the low-level FUSE API with the MFT record number as
 * the inode number, so every hard link of a file shares one kernel inode and
 * the kernel can keep entries, attributes and pages cached: every change goes
 * through this mount. Record 5 (root) and FUSE_ROOT_ID swap numbers. The core
 * has no internal locking, so the session loop is single-threaded.
 */
static const ULONGLONG NtfsRootRecord = 5;
static const double NtfsFuseCacheTimeout = 86400.0;

static fuse_ino_t
InoFromReference(ULONGLONG Reference)
{
    ULONGLONG Number = Reference & UINT64_C(0x0000FFFFFFFFFFFF);

    if (Number == NtfsRootRecord)
        return FUSE_ROOT_ID;
    if (Number == FUSE_ROOT_ID)
        return (fuse_ino_t)NtfsRootRecord;
    return (fuse_ino_t)Number;
}

static NtfsFuseState*
RequestState(fuse_req_t Request)
{
    return (NtfsFuseState*)fuse_req_userdata(Request);
}

static PNtfsMasterFileTable
StateMft(NtfsFuseState* State)
{
    return NtfsVolumeGetMft(State->Volume);
}

static UINT64
TimespecToNtfsTime(const struct timespec* Time)
{
    const int64_t EpochSeconds = INT64_C(11644473600);

    if (Time->tv_sec < -EpochSeconds)
        return 0;
    if (Time->tv_sec > INT64_MAX / 10000000 - EpochSeconds)
        return (UINT64)INT64_MAX;
    return (UINT64)(Time->tv_sec + EpochSeconds) * UINT64_C(10000000) +
           (UINT64)Time->tv_nsec / 100;
}

static bool
IsReadOnlyFile(PNtfsFileRecord File)
{
    NtfsFileBasicInformation Information;

    return !(NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) &&
           NT_SUCCESS(NtfsFileRecordGetBasicInformation(File, &Information)) &&
           (Information.FileAttributes & FILE_PERM_READONLY);
}

static void
ReplyStatus(fuse_req_t Request, NTSTATUS Status)
{
    fuse_reply_err(Request, NT_SUCCESS(Status) ? 0 : NtStatusToErrno(Status));
}

static NTSTATUS
FlushPendingWriteTime(NtfsFuseOpenFile* Open)
{
    NtfsFileBasicInformation Information;
    NTSTATUS Status;

    if (!Open || (Open->PendingWriteTime == 0 && Open->PendingAccessTime == 0))
        return STATUS_SUCCESS;
    Status = NtfsFileRecordGetBasicInformation(Open->File, &Information);
    if (NT_SUCCESS(Status))
    {
        Information.Fields = 0;
        if (Open->PendingWriteTime != 0)
        {
            Information.Fields |= NTFS_BASIC_INFO_LAST_WRITE_TIME | NTFS_BASIC_INFO_CHANGE_TIME;
            Information.LastWriteTime = Open->PendingWriteTime;
            Information.ChangeTime = Open->PendingWriteTime;
        }
        if (Open->PendingAccessTime != 0)
        {
            Information.Fields |= NTFS_BASIC_INFO_LAST_ACCESS_TIME;
            Information.LastAccessTime = Open->PendingAccessTime;
        }
        Status = NtfsFileRecordSetBasicInformation(Open->File, &Information);
    }
    if (NT_SUCCESS(Status))
        Open->PendingWriteTime = Open->PendingAccessTime = 0;
    return Status;
}

static NtfsFuseOpenFile*
FindOpen(NtfsFuseState* State, fuse_ino_t Ino)
{
    auto Open = State->OpenFiles.find(Ino);

    return Open == State->OpenFiles.end() ? NULL : &Open->second;
}

/* FillStatFromRecord plus the write time an open file has not stored yet. */
static NTSTATUS
FillInodeStat(NtfsFuseState* State, fuse_ino_t Ino, PNtfsFileRecord File, struct stat* Stat)
{
    NtfsFuseOpenFile* Open = FindOpen(State, Ino);
    NTSTATUS Status = FillStatFromRecord(File, State, Stat);

    if (NT_SUCCESS(Status) && Open && Open->PendingWriteTime != 0)
    {
        Stat->st_mtim = NtfsTimeToTimespec(Open->PendingWriteTime);
        Stat->st_ctim = Stat->st_mtim;
    }
    if (NT_SUCCESS(Status) && Open && Open->PendingAccessTime != 0)
        Stat->st_atim = NtfsTimeToTimespec(Open->PendingAccessTime);
    else if (NT_SUCCESS(Status) && !State->PendingAccess.empty())
    {
        auto Pending = State->PendingAccess.find(NtfsFileRecordGetReference(File));

        if (Pending != State->PendingAccess.end())
            Stat->st_atim = NtfsTimeToTimespec(Pending->second);
    }
    return Status;
}

/* Counts one kernel lookup of File's inode and fills the reply entry. */
static NTSTATUS
RememberEntry(NtfsFuseState* State,
              PNtfsFileRecord File,
              struct fuse_entry_param* Entry)
{
    ULONGLONG Reference = NtfsFileRecordGetReference(File);
    NTSTATUS Status;

    memset(Entry, 0, sizeof(*Entry));
    Status = FillInodeStat(State, InoFromReference(Reference), File, &Entry->attr);
    if (!NT_SUCCESS(Status))
        return Status;
    Entry->ino = InoFromReference(Reference);
    Entry->generation = Reference >> 48;
    Entry->attr_timeout = NtfsFuseCacheTimeout;
    Entry->entry_timeout = NtfsFuseCacheTimeout;

    NtfsFuseInode& Inode = State->Inodes[Entry->ino];
    Inode.Reference = Reference;
    Inode.Lookups++;
    State->RemovedDirectories.erase(Entry->ino);
    return STATUS_SUCCESS;
}

/* The record an inode's handles share, or a fresh caller-owned copy. */
static NTSTATUS
LoadInode(NtfsFuseState* State,
          fuse_ino_t Ino,
          PNtfsFileRecord* File,
          bool* Owned)
{
    auto Open = State->OpenFiles.find(Ino);
    auto Inode = State->Inodes.find(Ino);

    *File = NULL;
    *Owned = false;
    if (Open != State->OpenFiles.end())
    {
        *File = Open->second.File;
        return STATUS_SUCCESS;
    }
    *Owned = true;
    if (Inode == State->Inodes.end())
    {
        /* A file handle may name an inode the kernel forgot; it checks the generation. */
        ULONGLONG Number = Ino == FUSE_ROOT_ID ? NtfsRootRecord
                         : Ino == NtfsRootRecord ? FUSE_ROOT_ID : Ino;
        std::vector<UCHAR> Record(4096);
        ULONGLONG Returned = 0;
        ULONG Length = (ULONG)Record.size();
        NTSTATUS Status = NtfsMasterFileTableReadFileRecord(StateMft(State), Number, &Returned,
                                                            Record.data(), &Length);

        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            Record.resize(Length);
            Status = NtfsMasterFileTableReadFileRecord(StateMft(State), Number, &Returned,
                                                       Record.data(), &Length);
        }
        if (!NT_SUCCESS(Status) || (Returned & UINT64_C(0x0000FFFFFFFFFFFF)) != Number ||
            ((PFileRecordHeader)Record.data())->BaseFileRecord != 0)
        {
            return STATUS_INVALID_HANDLE;
        }
        return NtfsMasterFileTableGetFileRecordByReference(
            StateMft(State),
            Number | ((ULONGLONG)((PFileRecordHeader)Record.data())->SequenceNumber << 48),
            File);
    }
    return NtfsMasterFileTableGetFileRecordByReference(StateMft(State),
                                                      Inode->second.Reference,
                                                      File);
}

static void
ReleaseLoaded(PNtfsFileRecord File, bool Owned)
{
    if (Owned)
        NtfsFileRecordDestroy(File);
}

static bool
ComponentToNtfs(const char* Name, std::vector<WCHAR>& Output)
{
    if (!Utf8ToNtfsString(Name, true, false, Output) || Output.size() < 2)
        return false;
    for (WCHAR Character : Output)
    {
        if (Character == L'\\')
            return false;
    }
    return true;
}

/* Resolves Name in Parent with exact case, as Linux NTFS drivers do; the
 * core matches case-insensitively and still refuses a second name that
 * differs only in case.
 */
static NTSTATUS
LookupChild(NtfsFuseState* State,
            PNtfsFileRecord Parent,
            const std::vector<WCHAR>& Name,
            PNtfsFileRecord* Child)
{
    UNICODE_STRING Requested;
    UNICODE_STRING Stored;
    NTSTATUS Status;

    *Child = NULL;
    Status = NtfsMasterFileTableGetFileRecordInDirectory(
        StateMft(State),
        Parent,
        const_cast<PWCHAR>(Name.data()),
        (ULONG)(Name.size() - 1),
        Child);
    if (!NT_SUCCESS(Status))
        return Status == STATUS_NOT_FOUND ? STATUS_OBJECT_NAME_NOT_FOUND : Status;

    Requested.Buffer = const_cast<PWCHAR>(Name.data());
    Requested.Length = (USHORT)((Name.size() - 1) * sizeof(WCHAR));
    Requested.MaximumLength = Requested.Length;
    Status = NtfsMasterFileTableGetLinkName(StateMft(State),
                                            *Child,
                                            NtfsFileRecordGetReference(Parent),
                                            &Requested,
                                            &Stored);
    if (!NT_SUCCESS(Status) ||
        Stored.Length != Requested.Length ||
        memcmp(Stored.Buffer, Requested.Buffer, Requested.Length) != 0)
    {
        NtfsFileRecordDestroy(*Child);
        *Child = NULL;
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    return STATUS_SUCCESS;
}

/* Builds the counted path of Name inside the directory Parent. */
static NTSTATUS
ChildPath(NtfsFuseState* State,
          ULONGLONG ParentReference,
          const std::vector<WCHAR>& Name,
          std::vector<WCHAR>& Path)
{
    ULONG Length = 0;
    NTSTATUS Status;

    Path.resize(NTFS_MAX_PATH_WCHARS);
    Status = NtfsMasterFileTableGetPathFromFileReference(StateMft(State),
                                                         ParentReference,
                                                         Path.data(),
                                                         (ULONG)Path.size(),
                                                         &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    Path.resize(Length);
    if (Path.back() != L'\\')
        Path.push_back(L'\\');
    Path.insert(Path.end(), Name.begin(), Name.end() - 1);
    return STATUS_SUCCESS;
}

static NTSTATUS
InodePath(NtfsFuseState* State, ULONGLONG Reference, std::vector<WCHAR>& Path)
{
    ULONG Length = 0;
    NTSTATUS Status;

    Path.resize(NTFS_MAX_PATH_WCHARS);
    Status = NtfsMasterFileTableGetPathFromFileReference(StateMft(State),
                                                         Reference,
                                                         Path.data(),
                                                         (ULONG)Path.size(),
                                                         &Length);
    if (NT_SUCCESS(Status))
        Path.resize(Length);
    return Status;
}

static void
DropListing(NtfsFuseState* State, fuse_ino_t Directory)
{
    State->DirectoryCache.erase(Directory);
}

/* The core changes names through its own record copies; reload the record
 * open handles share afterwards.
 */
static NTSTATUS
RefreshOpenFile(NtfsFuseState* State, fuse_ino_t Ino)
{
    auto Open = State->OpenFiles.find(Ino);
    PNtfsFileRecord Fresh = NULL;
    NTSTATUS Status;

    if (Open == State->OpenFiles.end())
        return STATUS_SUCCESS;
    Status = NtfsMasterFileTableGetFileRecordByReference(
        StateMft(State),
        NtfsFileRecordGetReference(Open->second.File),
        &Fresh);
    if (NT_SUCCESS(Status))
        Status = NtfsFileRecordRefresh(Open->second.File, Fresh);
    NtfsFileRecordDestroy(Fresh);
    return Status;
}

static void
FlushPendingAccess(NtfsFuseState* State)
{
    for (auto& Pending : State->PendingAccess)
    {
        PNtfsFileRecord File = NULL;
        NtfsFileBasicInformation Information;

        if (NT_SUCCESS(NtfsMasterFileTableGetFileRecordByReference(StateMft(State), Pending.first, &File)) &&
            NT_SUCCESS(NtfsFileRecordGetBasicInformation(File, &Information)))
        {
            Information.Fields = NTFS_BASIC_INFO_LAST_ACCESS_TIME;
            Information.LastAccessTime = Pending.second;
            (void)NtfsFileRecordSetBasicInformation(File, &Information);
        }
        NtfsFileRecordDestroy(File);
    }
    State->PendingAccess.clear();
}

static ULONG
ClusterBytes(NtfsFuseState* State)
{
    return NtfsVolumeGetBytesPerSector(State->Volume) *
           NtfsVolumeGetSectorsPerCluster(State->Volume);
}

static PNtfsFileRecord
AcquireOpenFile(NtfsFuseState* State, fuse_ino_t Ino, PNtfsFileRecord File)
{
    auto Existing = State->OpenFiles.find(Ino);

    if (Existing == State->OpenFiles.end())
    {
        State->OpenFiles.emplace(Ino, NtfsFuseOpenFile{File, 1u, false, false, false, 0, 0, false, false});
        if (State->Writable)
            (void)NtfsFileRecordDeferFileNameUpdates(File, TRUE);
        return File;
    }
    assert(Existing->second.File != File);
    assert(Existing->second.References != 0);
    NtfsFileRecordDestroy(File);
    Existing->second.References++;
    return Existing->second.File;
}

static void
ReleaseOpenFile(NtfsFuseState* State, fuse_ino_t Ino)
{
    auto Existing = State->OpenFiles.find(Ino);
    PNtfsFileRecord File;

    assert(Existing != State->OpenFiles.end());
    assert(Existing->second.References != 0);
    if (--Existing->second.References != 0)
        return;

    File = Existing->second.File;
    if (!Existing->second.Unlinked &&
        Existing->second.PendingWriteTime == 0 &&
        Existing->second.PendingAccessTime != 0)
    {
        State->PendingAccess[NtfsFileRecordGetReference(File)] = Existing->second.PendingAccessTime;
        Existing->second.PendingAccessTime = 0;
        if (State->PendingAccess.size() > 4096)
            FlushPendingAccess(State);
    }
    if (!Existing->second.Unlinked)
    {
        NTSTATUS Status = FlushPendingWriteTime(&Existing->second);

        if (!NT_SUCCESS(Status))
            fprintf(stderr, "ntfslib: storing the write time failed 0x%08" PRIx32 "\n",
                    (uint32_t)Status);
    }
    if (Existing->second.Unlinked)
    {
        NTSTATUS Status;

        (void)NtfsFileRecordDeferFileNameUpdates(File, FALSE);
        Status = NtfsMasterFileTableDeleteUnlinkedFile(
            StateMft(State),
            NtfsFileRecordGetReference(File),
            NULL);
        if (!NT_SUCCESS(Status))
            fprintf(stderr, "ntfslib: deleting an unlinked file failed 0x%08" PRIx32 "\n",
                    (uint32_t)Status);
    }
    else
    {
        PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);

        /* FIXME: a KEEP_SIZE reservation made before this open is trimmed with the growth. */
        if (Existing->second.Written &&
            !Existing->second.Preallocated &&
            Data && Data->IsNonResident &&
            !(Data->Flags & (ATTR_COMPRESSED | ATTR_SPARSE)) &&
            Data->NonResident.AllocatedSize - Data->NonResident.DataSize >=
                ClusterBytes(State))
        {
            NTSTATUS Status = NtfsFileRecordSetFileAllocationSize(
                File,
                TypeData,
                NULL,
                Data->NonResident.DataSize);

            if (!NT_SUCCESS(Status))
                fprintf(stderr, "ntfslib: trimming preallocation failed 0x%08" PRIx32 "\n",
                        (uint32_t)Status);
            else if (State->Session)
                fuse_lowlevel_notify_inval_inode(State->Session, Ino, -1, 0);
        }
        NTSTATUS Status = NtfsFileRecordDeferFileNameUpdates(File, FALSE);

        if (!NT_SUCCESS(Status))
            fprintf(stderr, "ntfslib: updating the file names failed 0x%08" PRIx32 "\n",
                    (uint32_t)Status);
    }
    State->OpenFiles.erase(Existing);
    NtfsFileRecordDestroy(File);
}

static void
NtfsLowInit(void* UserData, struct fuse_conn_info* Connection)
{
    NtfsFuseState* State = (NtfsFuseState*)UserData;

    Connection->time_gran = 100;
    if (Connection->capable & FUSE_CAP_READDIRPLUS)
        Connection->want |= FUSE_CAP_READDIRPLUS;
    /* Every listing carries attributes: one record load per entry beats a
     * lookup round trip per entry later. */
    Connection->want &= ~FUSE_CAP_READDIRPLUS_AUTO;
    if (Connection->capable & FUSE_CAP_CACHE_SYMLINKS)
        Connection->want |= FUSE_CAP_CACHE_SYMLINKS;
    if (Connection->capable & FUSE_CAP_EXPORT_SUPPORT)
        Connection->want |= FUSE_CAP_EXPORT_SUPPORT;
    if (State->Writable && (Connection->capable & FUSE_CAP_HANDLE_KILLPRIV))
        Connection->want |= FUSE_CAP_HANDLE_KILLPRIV;
    /* mmap of a direct-I/O file fails with ENODEV unless this capability is granted. */
#ifdef FUSE_CAP_DIRECT_IO_ALLOW_MMAP
    if (State->DirectIo && (Connection->capable & FUSE_CAP_DIRECT_IO_ALLOW_MMAP))
        Connection->want |= FUSE_CAP_DIRECT_IO_ALLOW_MMAP;
    else
        State->DirectIo = false;
#else
    State->DirectIo = false;
#endif
    if (State->Writable && State->WritebackCache &&
        (Connection->capable & FUSE_CAP_WRITEBACK_CACHE))
    {
        Connection->want |= FUSE_CAP_WRITEBACK_CACHE;
    }
    else
    {
        Connection->want &= ~FUSE_CAP_WRITEBACK_CACHE;
    }
}

static void
NtfsLowLookup(fuse_req_t Request, fuse_ino_t ParentIno, const char* Name)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord Parent = NULL;
    PNtfsFileRecord Child = NULL;
    std::vector<WCHAR> NtfsName;
    struct fuse_entry_param Entry;
    bool Owned;
    NTSTATUS Status;

    if (strcmp(Name, ".") == 0 || strcmp(Name, "..") == 0)
    {
        bool Dot = Name[1] == '\0';

        Status = LoadInode(State, ParentIno, &Parent, &Owned);
        if (NT_SUCCESS(Status) && !Dot)
        {
            ULONG Context = 0;
            ULONGLONG Above = 0;
            UNICODE_STRING LinkName;

            if (InoFromReference(NtfsFileRecordGetReference(Parent)) == FUSE_ROOT_ID)
                Above = NtfsFileRecordGetReference(Parent);
            else
                Status = NtfsMasterFileTableEnumerateLinks(StateMft(State), Parent, &Context,
                                                           &Above, &LinkName);
            if (NT_SUCCESS(Status))
                Status = NtfsMasterFileTableGetFileRecordByReference(StateMft(State), Above, &Child);
        }
        if (NT_SUCCESS(Status))
            Status = RememberEntry(State, Dot ? Parent : Child, &Entry);
        ReleaseLoaded(Parent, Owned);
        NtfsFileRecordDestroy(Child);
        if (!NT_SUCCESS(Status))
            ReplyStatus(Request, Status);
        else
            fuse_reply_entry(Request, &Entry);
        return;
    }
    if (!ComponentToNtfs(Name, NtfsName))
    {
        fuse_reply_err(Request, strlen(Name) > 255 * 4 ? ENAMETOOLONG : ENOENT);
        return;
    }
    if (NtfsName.size() - 1 > NTFS_MAX_FILE_NAME_LENGTH)
    {
        fuse_reply_err(Request, ENAMETOOLONG);
        return;
    }
    Status = LoadInode(State, ParentIno, &Parent, &Owned);
    if (NT_SUCCESS(Status))
        Status = LookupChild(State, Parent, NtfsName, &Child);
    ReleaseLoaded(Parent, Owned);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        memset(&Entry, 0, sizeof(Entry));
        Entry.entry_timeout = NtfsFuseCacheTimeout;
        fuse_reply_entry(Request, &Entry);
        return;
    }
    if (NT_SUCCESS(Status))
    {
        auto Open = State->OpenFiles.find(InoFromReference(NtfsFileRecordGetReference(Child)));

        Status = RememberEntry(State,
                               Open != State->OpenFiles.end() ? Open->second.File : Child,
                               &Entry);
    }
    NtfsFileRecordDestroy(Child);
    if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else
        fuse_reply_entry(Request, &Entry);
}

static void
ForgetInode(NtfsFuseState* State, fuse_ino_t Ino, uint64_t Count)
{
    auto Inode = State->Inodes.find(Ino);

    if (Ino == FUSE_ROOT_ID || Inode == State->Inodes.end())
        return;
    if (Inode->second.Lookups <= Count)
    {
        State->Inodes.erase(Inode);
        State->RemovedDirectories.erase(Ino);
    }
    else
        Inode->second.Lookups -= Count;
}

static void
NtfsLowForget(fuse_req_t Request, fuse_ino_t Ino, uint64_t Count)
{
    ForgetInode(RequestState(Request), Ino, Count);
    fuse_reply_none(Request);
}

static void
NtfsLowForgetMulti(fuse_req_t Request, size_t Count, struct fuse_forget_data* Forgets)
{
    for (size_t Index = 0; Index < Count; Index++)
        ForgetInode(RequestState(Request), Forgets[Index].ino, Forgets[Index].nlookup);
    fuse_reply_none(Request);
}

static void
NtfsLowGetattr(fuse_req_t Request, fuse_ino_t Ino, struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    struct stat Stat;
    bool Owned;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(FileInfo);
    Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status))
        Status = FillInodeStat(State, Ino, File, &Stat);
    ReleaseLoaded(File, Owned);
    if (!NT_SUCCESS(Status))
    {
        auto Removed = State->RemovedDirectories.find(Ino);

        if (Removed != State->RemovedDirectories.end())
        {
            fuse_reply_attr(Request, &Removed->second, NtfsFuseCacheTimeout);
            return;
        }
        ReplyStatus(Request, Status);
    }
    else
        fuse_reply_attr(Request, &Stat, NtfsFuseCacheTimeout);
}

static bool
CallerInGroup(fuse_req_t Request, UINT32 Gid)
{
    gid_t Groups[64];
    int Count;

    if (fuse_req_ctx(Request)->gid == Gid)
        return true;
    Count = fuse_req_getgroups(Request, (int)(sizeof(Groups) / sizeof(Groups[0])), Groups);
    for (int Index = 0; Index < Count && Index < (int)(sizeof(Groups) / sizeof(Groups[0])); Index++)
    {
        if (Groups[Index] == Gid)
            return true;
    }
    return false;
}

/* The server owns set-id clearing (FUSE_CAP_HANDLE_KILLPRIV) and applies the
 * Linux rule: an unprivileged change to a regular file drops set-user-ID, and
 * set-group-ID when the file is group-executable or the caller is outside its
 * group; chown of any non-directory drops set-user-ID whoever asks. */
static NTSTATUS
KillSetIdBits(NtfsFuseState* State,
              fuse_req_t Request,
              fuse_ino_t Ino,
              PNtfsFileRecord File,
              bool Chown,
              bool Notify)
{
    /* FIXME: libfuse does not pass the caller's capabilities; uid 0 stands for CAP_FSETID. */
    bool Privileged = fuse_req_ctx(Request)->uid == 0;
    NtfsPosixMetadata Metadata;
    UINT32 Mode;
    NTSTATUS Status;

    if (Privileged && !Chown)
        return STATUS_SUCCESS;
    ReadPosixMetadata(File, &Metadata);
    if (!Metadata.HasMode || !(Metadata.Mode & (S_ISUID | S_ISGID)) ||
        (Chown ? S_ISDIR(Metadata.Mode) : !S_ISREG(Metadata.Mode)))
    {
        return STATUS_SUCCESS;
    }
    Mode = Metadata.Mode & ~(UINT32)S_ISUID;
    if ((Mode & S_ISGID) &&
        ((Mode & S_IXGRP) ||
         (!Privileged && !CallerInGroup(Request, Metadata.HasGid ? Metadata.Gid : State->Group))))
    {
        Mode &= ~(UINT32)S_ISGID;
    }
    if (Mode == Metadata.Mode)
        return STATUS_SUCCESS;
    Status = WritePosixMetadata(File, NULL, NULL, &Mode, NULL);
    if (NT_SUCCESS(Status) && Notify)
        fuse_lowlevel_notify_inval_inode(State->Session, Ino, -1, 0);
    return Status;
}

static void
NtfsLowSetattr(fuse_req_t Request,
               fuse_ino_t Ino,
               struct stat* Attributes,
               int ToSet,
               struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    NtfsFileBasicInformation Information;
    struct timespec Now;
    struct stat Stat;
    bool Owned = false;
    bool IsDirectory;
    NTSTATUS Status;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    if (FileInfo && FileInfo->fh)
        File = (PNtfsFileRecord)(uintptr_t)FileInfo->fh;
    else
    {
        Status = LoadInode(State, Ino, &File, &Owned);
        if (!NT_SUCCESS(Status))
        {
            ReplyStatus(Request, Status);
            return;
        }
    }
    IsDirectory = (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) != 0;
    clock_gettime(CLOCK_REALTIME, &Now);
    if (ToSet & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_ATIME_NOW))
    {
        State->PendingAccess.erase(NtfsFileRecordGetReference(File));
        if (NtfsFuseOpenFile* Open = FindOpen(State, Ino))
            Open->PendingAccessTime = 0;
    }
    Status = FlushPendingWriteTime(FindOpen(State, Ino));

    if (ToSet & FUSE_SET_ATTR_SIZE)
    {
        if (IsDirectory)
            Status = STATUS_FILE_IS_A_DIRECTORY;
        else if (!(FileInfo && FileInfo->fh) && IsReadOnlyFile(File))
            Status = STATUS_ACCESS_DENIED;
        else
            Status = NtfsFileRecordSetFileDataSize(File, TypeData, NULL,
                                                   (ULONGLONG)Attributes->st_size);
        if (NT_SUCCESS(Status))
            Status = KillSetIdBits(State, Request, Ino, File, false, false);
        if (Status == STATUS_ACCESS_DENIED)
        {
            ReleaseLoaded(File, Owned);
            fuse_reply_err(Request, EACCES);
            return;
        }
    }
    if (NT_SUCCESS(Status) &&
        (ToSet & (FUSE_SET_ATTR_MODE | FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME |
                  FUSE_SET_ATTR_ATIME_NOW | FUSE_SET_ATTR_MTIME_NOW | FUSE_SET_ATTR_CTIME)))
    {
        Status = NtfsFileRecordGetBasicInformation(File, &Information);
        if (NT_SUCCESS(Status))
        {
            UINT32 Fields = 0;

            if (ToSet & FUSE_SET_ATTR_ATIME_NOW)
                Information.LastAccessTime = TimespecToNtfsTime(&Now), Fields |= NTFS_BASIC_INFO_LAST_ACCESS_TIME;
            else if (ToSet & FUSE_SET_ATTR_ATIME)
                Information.LastAccessTime = TimespecToNtfsTime(&Attributes->st_atim), Fields |= NTFS_BASIC_INFO_LAST_ACCESS_TIME;
            if (ToSet & FUSE_SET_ATTR_MTIME_NOW)
                Information.LastWriteTime = TimespecToNtfsTime(&Now), Fields |= NTFS_BASIC_INFO_LAST_WRITE_TIME;
            else if (ToSet & FUSE_SET_ATTR_MTIME)
                Information.LastWriteTime = TimespecToNtfsTime(&Attributes->st_mtim), Fields |= NTFS_BASIC_INFO_LAST_WRITE_TIME;
            if ((ToSet & FUSE_SET_ATTR_MODE) && !IsDirectory)
            {
                UINT32 Updated = Information.FileAttributes;

                if ((Attributes->st_mode & 0222) == 0)
                    Updated |= FILE_PERM_READONLY;
                else
                    Updated &= ~(UINT32)FILE_PERM_READONLY;
                if (Updated != Information.FileAttributes)
                {
                    Information.FileAttributes = Updated;
                    Fields |= NTFS_BASIC_INFO_FILE_ATTRIBUTES;
                }
            }
            if (Fields != 0 || (ToSet & (FUSE_SET_ATTR_CTIME | FUSE_SET_ATTR_MODE)))
            {
                Information.ChangeTime = (ToSet & FUSE_SET_ATTR_CTIME)
                    ? TimespecToNtfsTime(&Attributes->st_ctim)
                    : TimespecToNtfsTime(&Now);
                Information.Fields = Fields | NTFS_BASIC_INFO_CHANGE_TIME;
                Status = NtfsFileRecordSetBasicInformation(File, &Information);
            }
        }
    }
    if (NT_SUCCESS(Status) && (ToSet & (FUSE_SET_ATTR_MODE | FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)))
    {
        struct stat Current;

        Status = FillStatFromRecord(File, State, &Current);
        if (NT_SUCCESS(Status))
        {
            UINT32 Uid = Attributes->st_uid;
            UINT32 Gid = Attributes->st_gid;
            UINT32 Mode = (Current.st_mode & S_IFMT) | (Attributes->st_mode & 07777);

            if ((ToSet & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) && !(ToSet & FUSE_SET_ATTR_MODE))
                Status = KillSetIdBits(State, Request, Ino, File, true, false);
            if (NT_SUCCESS(Status))
            {
                Status = WritePosixMetadata(File,
                                            (ToSet & FUSE_SET_ATTR_UID) ? &Uid : NULL,
                                            (ToSet & FUSE_SET_ATTR_GID) ? &Gid : NULL,
                                            (ToSet & FUSE_SET_ATTR_MODE) ? &Mode : NULL,
                                            NULL);
            }
        }
        if (NtfsFuseOpenFile* Open = FindOpen(State, Ino))
            Open->SetIdChecked = false;
    }
    if (NT_SUCCESS(Status))
        Status = FillInodeStat(State, Ino, File, &Stat);
    ReleaseLoaded(File, Owned);
    if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else
        fuse_reply_attr(Request, &Stat, NtfsFuseCacheTimeout);
}

static void
NtfsLowReadlink(fuse_req_t Request, fuse_ino_t Ino)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    std::string Target;
    bool Owned;
    NTSTATUS Status;

    Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status))
        Status = GetReparseTarget(File, Target);
    ReleaseLoaded(File, Owned);
    if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else
        fuse_reply_readlink(Request, Target.c_str());
}

static NTSTATUS
RemoveChild(NtfsFuseState* State, fuse_ino_t ParentIno, const char* Name, bool IsDirectory);

/* Creates Name in Parent with the caller's ownership and Mode (type and
 * permissions); on success Created holds the new record. */
static NTSTATUS
CreateChild(NtfsFuseState* State,
            fuse_req_t Request,
            fuse_ino_t ParentIno,
            const char* Name,
            mode_t Mode,
            dev_t Device,
            PNtfsFileRecord* Created)
{
    bool IsDirectory = S_ISDIR(Mode);
    PNtfsFileRecord Parent = NULL;
    std::vector<WCHAR> NtfsName;
    bool Owned;
    NTSTATUS Status;

    *Created = NULL;
    if (!State->Writable)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (!ComponentToNtfs(Name, NtfsName))
        return STATUS_OBJECT_NAME_INVALID;
    if (NtfsName.size() - 1 > NTFS_MAX_FILE_NAME_LENGTH)
        return STATUS_NAME_TOO_LONG;
    NtfsPosixMetadata ParentMetadata;

    Status = LoadInode(State, ParentIno, &Parent, &Owned);
    if (!NT_SUCCESS(Status))
        return Status;
    ReadPosixMetadata(Parent, &ParentMetadata);
    Status = NtfsMasterFileTableCreateFileInDirectory(StateMft(State),
                                                      Parent,
                                                      NtfsName.data(),
                                                      (ULONG)(NtfsName.size() - 1),
                                                      IsDirectory,
                                                      0,
                                                      FALSE,
                                                      NULL,
                                                      0,
                                                      Created);
    ReleaseLoaded(Parent, Owned);
    if (NT_SUCCESS(Status))
    {
        const struct fuse_ctx* Context = fuse_req_ctx(Request);
        UINT32 Uid = Context->uid;
        UINT32 Gid = Context->gid;
        UINT32 FullMode = Mode;

        /* A set-group-ID directory passes its group on, and the bit to subdirectories. */
        if (ParentMetadata.HasMode && (ParentMetadata.Mode & S_ISGID))
        {
            Gid = ParentMetadata.HasGid ? ParentMetadata.Gid : State->Group;
            if (IsDirectory)
                FullMode |= S_ISGID;
        }
        UINT32 Dev = (UINT32)((major(Device) << 20) | (minor(Device) & 0xFFFFF));

        DropListing(State, ParentIno);
        Status = WritePosixMetadata(*Created, &Uid, &Gid, &FullMode,
                                    S_ISCHR(Mode) || S_ISBLK(Mode) ? &Dev : NULL);
        if (!NT_SUCCESS(Status))
        {
            NtfsFileRecordDestroy(*Created);
            *Created = NULL;
            (void)RemoveChild(State, ParentIno, Name, IsDirectory);
        }
    }
    return Status;
}

static void
ReplyCreatedEntry(fuse_req_t Request, NtfsFuseState* State, PNtfsFileRecord File, NTSTATUS Status)
{
    struct fuse_entry_param Entry;

    if (NT_SUCCESS(Status))
        Status = RememberEntry(State, File, &Entry);
    NtfsFileRecordDestroy(File);
    if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else
        fuse_reply_entry(Request, &Entry);
}

static void
NtfsLowMknod(fuse_req_t Request, fuse_ino_t Parent, const char* Name, mode_t Mode, dev_t Device)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;

    NTSTATUS Status;

    /* FIFOs, sockets and devices are ordinary records typed by $LXMOD, as ntfs3 makes them. */
    if (!S_ISREG(Mode) && !S_ISFIFO(Mode) && !S_ISSOCK(Mode) && !S_ISCHR(Mode) && !S_ISBLK(Mode))
    {
        fuse_reply_err(Request, EOPNOTSUPP);
        return;
    }
    Status = CreateChild(State, Request, Parent, Name, Mode, Device, &File);
    ReplyCreatedEntry(Request, State, File, Status);
}

static void
NtfsLowMkdir(fuse_req_t Request, fuse_ino_t Parent, const char* Name, mode_t Mode)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    Status = CreateChild(State, Request, Parent, Name, S_IFDIR | (Mode & 07777), 0, &File);
    ReplyCreatedEntry(Request, State, File, Status);
}

static NTSTATUS
BuildSymlinkBuffer(const char* Target, std::vector<UCHAR>& Buffer)
{
    std::vector<WCHAR> Name;
    bool Relative = Target[0] != '/';
    size_t PrefixLength = Relative ? 0 : 4;
    size_t NameBytes;
    size_t PrintBytes;
    size_t SubstituteBytes;
    UINT32 Tag = NTFS_IO_REPARSE_TAG_SYMLINK;
    UINT32 Flags = Relative ? NTFS_SYMLINK_FLAG_RELATIVE : 0;
    UINT16 Field;

    if (!Utf8ToNtfsString(Target, false, false, Name))
        return STATUS_OBJECT_NAME_INVALID;
    Name.pop_back();
    for (WCHAR& Character : Name)
    {
        if (Character == L'/')
            Character = L'\\';
    }
    NameBytes = Name.size() * sizeof(WCHAR);
    SubstituteBytes = PrefixLength * sizeof(WCHAR) + NameBytes;
    PrintBytes = NameBytes;
    /* A target too long for both names keeps only the substitute name. */
    if (8 + 12 + SubstituteBytes + PrintBytes > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
        PrintBytes = 0;
    if (8 + 12 + SubstituteBytes + PrintBytes > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
        return STATUS_NAME_TOO_LONG;
    Buffer.assign(8 + 12 + SubstituteBytes + PrintBytes, 0);
    memcpy(&Buffer[0], &Tag, sizeof(Tag));
    Field = (UINT16)(Buffer.size() - 8);
    memcpy(&Buffer[4], &Field, sizeof(Field));
    Field = (UINT16)SubstituteBytes;
    memcpy(&Buffer[10], &Field, sizeof(Field));
    memcpy(&Buffer[12], &Field, sizeof(Field));
    Field = (UINT16)PrintBytes;
    memcpy(&Buffer[14], &Field, sizeof(Field));
    memcpy(&Buffer[16], &Flags, sizeof(Flags));
    if (!Relative)
        memcpy(&Buffer[20], L"\\??\\", 4 * sizeof(WCHAR));
    memcpy(&Buffer[20 + PrefixLength * sizeof(WCHAR)], Name.data(), NameBytes);
    if (PrintBytes != 0)
        memcpy(&Buffer[20 + SubstituteBytes], Name.data(), PrintBytes);
    return STATUS_SUCCESS;
}

static void
NtfsLowSymlink(fuse_req_t Request, const char* Target, fuse_ino_t Parent, const char* Name)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Buffer;
    NTSTATUS Status;

    Status = BuildSymlinkBuffer(Target, Buffer);
    if (NT_SUCCESS(Status))
        Status = CreateChild(State, Request, Parent, Name, S_IFLNK | 0777, 0, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetReparsePoint(File, Buffer.data(), (ULONG)Buffer.size());
        if (!NT_SUCCESS(Status))
        {
            NtfsFileRecordDestroy(File);
            File = NULL;
            (void)RemoveChild(State, Parent, Name, false);
        }
    }
    ReplyCreatedEntry(Request, State, File, Status);
}

/* Removes one name. An open file whose last name goes keeps its records
 * until the last handle closes, as an NT delete-on-close does.
 */
static NTSTATUS
RemoveChild(NtfsFuseState* State, fuse_ino_t ParentIno, const char* Name, bool IsDirectory)
{
    PNtfsFileRecord Parent = NULL;
    PNtfsFileRecord Child = NULL;
    std::vector<WCHAR> NtfsName;
    BOOLEAN Deleted = FALSE;
    bool Owned;
    NTSTATUS Status;

    if (!State->Writable)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (!ComponentToNtfs(Name, NtfsName))
        return STATUS_OBJECT_NAME_INVALID;
    Status = LoadInode(State, ParentIno, &Parent, &Owned);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = LookupChild(State, Parent, NtfsName, &Child);
    if (NT_SUCCESS(Status))
    {
        fuse_ino_t ChildIno = InoFromReference(NtfsFileRecordGetReference(Child));
        auto Open = State->OpenFiles.find(ChildIno);
        PNtfsFileRecord Canonical = Open != State->OpenFiles.end() ? Open->second.File : NULL;

        Status = NtfsMasterFileTableDeleteFileInDirectory(StateMft(State),
                                                          Parent,
                                                          NtfsName.data(),
                                                          (ULONG)(NtfsName.size() - 1),
                                                          IsDirectory,
                                                          Canonical,
                                                          Canonical == NULL,
                                                          Canonical != NULL,
                                                          &Deleted);
        if (NT_SUCCESS(Status))
        {
            struct stat Removed;

            DropListing(State, ParentIno);
            if (Canonical && NtfsFileRecordGetLinkCount(Canonical) == 0)
                Open->second.Unlinked = true;
            if (IsDirectory && State->Inodes.count(ChildIno) &&
                NT_SUCCESS(FillStatFromRecord(Child, State, &Removed)))
            {
                Removed.st_nlink = 0;
                State->RemovedDirectories[ChildIno] = Removed;
            }
        }
    }
    ReleaseLoaded(Parent, Owned);
    NtfsFileRecordDestroy(Child);
    return Status;
}

static void
NtfsLowUnlink(fuse_req_t Request, fuse_ino_t Parent, const char* Name)
{
    ReplyStatus(Request, RemoveChild(RequestState(Request), Parent, Name, false));
}

static void
NtfsLowRmdir(fuse_req_t Request, fuse_ino_t Parent, const char* Name)
{
    ReplyStatus(Request, RemoveChild(RequestState(Request), Parent, Name, true));
}

static void
NtfsLowRename(fuse_req_t Request,
              fuse_ino_t OldParentIno,
              const char* OldName,
              fuse_ino_t NewParentIno,
              const char* NewName,
              unsigned int Flags)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord OldParent = NULL;
    PNtfsFileRecord NewParent = NULL;
    PNtfsFileRecord Source = NULL;
    PNtfsFileRecord Target = NULL;
    std::vector<WCHAR> OldNtfs;
    std::vector<WCHAR> NewNtfs;
    std::vector<WCHAR> OldPath;
    std::vector<WCHAR> NewPath;
    bool OldOwned = false;
    bool NewOwned = false;
    fuse_ino_t SourceIno = 0;
    NTSTATUS Status;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    if (Flags & ~(unsigned)RENAME_NOREPLACE)
    {
        fuse_reply_err(Request, EINVAL);
        return;
    }
    if (!ComponentToNtfs(OldName, OldNtfs) || !ComponentToNtfs(NewName, NewNtfs))
    {
        fuse_reply_err(Request, EINVAL);
        return;
    }
    if (NewNtfs.size() - 1 > NTFS_MAX_FILE_NAME_LENGTH)
    {
        fuse_reply_err(Request, ENAMETOOLONG);
        return;
    }
    Status = LoadInode(State, OldParentIno, &OldParent, &OldOwned);
    if (NT_SUCCESS(Status))
        Status = LoadInode(State, NewParentIno, &NewParent, &NewOwned);
    if (NT_SUCCESS(Status))
        Status = LookupChild(State, OldParent, OldNtfs, &Source);
    if (NT_SUCCESS(Status))
    {
        NTSTATUS TargetStatus = LookupChild(State, NewParent, NewNtfs, &Target);

        if (!NT_SUCCESS(TargetStatus) && TargetStatus != STATUS_OBJECT_NAME_NOT_FOUND)
            Status = TargetStatus;
    }
    if (NT_SUCCESS(Status))
        Status = ChildPath(State, NtfsFileRecordGetReference(OldParent), OldNtfs, OldPath);
    if (NT_SUCCESS(Status))
        Status = ChildPath(State, NtfsFileRecordGetReference(NewParent), NewNtfs, NewPath);
    ReleaseLoaded(OldParent, OldOwned);
    ReleaseLoaded(NewParent, NewOwned);
    if (NT_SUCCESS(Status))
        SourceIno = InoFromReference(NtfsFileRecordGetReference(Source));

    if (NT_SUCCESS(Status) && Target)
    {
        if (NtfsFileRecordGetReference(Target) == NtfsFileRecordGetReference(Source))
        {
            /* Two links of one file: POSIX leaves both names in place. */
            NtfsFileRecordDestroy(Source);
            NtfsFileRecordDestroy(Target);
            fuse_reply_err(Request, 0);
            return;
        }
        if (Flags & RENAME_NOREPLACE)
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
        }
        else
        {
            /* FIXME: the replaced name is removed before the rename publishes; not atomic without LFS. */
            Status = RemoveChild(State,
                                 NewParentIno,
                                 NewName,
                                 (NtfsFileRecordGetHeader(Target)->Flags & FR_IS_DIRECTORY) != 0);
        }
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtfsMasterFileTableRenameFile(StateMft(State),
                                               OldPath.data(),
                                               (ULONG)OldPath.size(),
                                               NewPath.data(),
                                               (ULONG)NewPath.size());
    }
    if (NT_SUCCESS(Status))
    {
        DropListing(State, OldParentIno);
        DropListing(State, NewParentIno);
        Status = RefreshOpenFile(State, SourceIno);
    }
    NtfsFileRecordDestroy(Source);
    NtfsFileRecordDestroy(Target);
    ReplyStatus(Request, Status);
}

static void
NtfsLowLink(fuse_req_t Request, fuse_ino_t Ino, fuse_ino_t NewParentIno, const char* NewName)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    PNtfsFileRecord NewParent = NULL;
    std::vector<WCHAR> NewNtfs;
    std::vector<WCHAR> ExistingPath;
    std::vector<WCHAR> NewPath;
    bool FileOwned = false;
    bool ParentOwned = false;
    NTSTATUS Status;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    if (!ComponentToNtfs(NewName, NewNtfs))
    {
        fuse_reply_err(Request, EINVAL);
        return;
    }
    Status = LoadInode(State, Ino, &File, &FileOwned);
    if (NT_SUCCESS(Status) && (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY))
        Status = STATUS_FILE_IS_A_DIRECTORY;
    if (NT_SUCCESS(Status))
        Status = LoadInode(State, NewParentIno, &NewParent, &ParentOwned);
    if (NT_SUCCESS(Status))
        Status = InodePath(State, NtfsFileRecordGetReference(File), ExistingPath);
    if (NT_SUCCESS(Status))
        Status = ChildPath(State, NtfsFileRecordGetReference(NewParent), NewNtfs, NewPath);
    ReleaseLoaded(NewParent, ParentOwned);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsMasterFileTableCreateHardLink(StateMft(State),
                                                   ExistingPath.data(),
                                                   (ULONG)ExistingPath.size(),
                                                   NewPath.data(),
                                                   (ULONG)NewPath.size());
    }
    if (NT_SUCCESS(Status))
    {
        DropListing(State, NewParentIno);
        if (FileOwned)
        {
            PNtfsFileRecord Fresh = NULL;

            Status = NtfsMasterFileTableGetFileRecordByReference(
                StateMft(State), NtfsFileRecordGetReference(File), &Fresh);
            if (NT_SUCCESS(Status))
                Status = NtfsFileRecordRefresh(File, Fresh);
            NtfsFileRecordDestroy(Fresh);
        }
        else
        {
            Status = RefreshOpenFile(State, Ino);
        }
    }
    if (Status == STATUS_FILE_IS_A_DIRECTORY)
    {
        ReleaseLoaded(File, FileOwned);
        fuse_reply_err(Request, EPERM);
        return;
    }
    if (NT_SUCCESS(Status))
    {
        struct fuse_entry_param Entry;

        Status = RememberEntry(State, File, &Entry);
        ReleaseLoaded(File, FileOwned);
        if (NT_SUCCESS(Status))
        {
            fuse_reply_entry(Request, &Entry);
            return;
        }
    }
    else
    {
        ReleaseLoaded(File, FileOwned);
    }
    ReplyStatus(Request, Status);
}

static void
SetOpenCaching(NtfsFuseState* State, struct fuse_file_info* FileInfo)
{
    if (State->Writable && State->DirectIo)
        FileInfo->direct_io = 1;
    else
        FileInfo->keep_cache = 1;
}

/* Shared by open and create; consumes File, or uses the shared record when
 * File is NULL. */
static NTSTATUS
OpenRecord(NtfsFuseState* State,
           fuse_ino_t Ino,
           PNtfsFileRecord File,
           struct fuse_file_info* FileInfo)
{
    bool WriteAccess = (FileInfo->flags & O_ACCMODE) != O_RDONLY;
    NtfsFuseOpenFile* Open = File ? NULL : FindOpen(State, Ino);
    PNtfsFileRecord Checked = File ? File : Open ? Open->File : NULL;
    PAttribute Data;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Checked)
        return STATUS_INVALID_HANDLE;
    Data = NtfsFileRecordGetAttribute(Checked, TypeData, NULL);
    if (NtfsFileRecordGetHeader(Checked)->Flags & FR_IS_DIRECTORY)
        Status = STATUS_FILE_IS_A_DIRECTORY;
    else if (!Data || (Data->Flags & ATTR_ENCRYPTED))
        Status = Data ? STATUS_EAS_NOT_SUPPORTED : STATUS_OBJECT_NAME_NOT_FOUND;
    else if (WriteAccess && !State->Writable)
        Status = STATUS_MEDIA_WRITE_PROTECTED;
    else if (WriteAccess && IsReadOnlyFile(Checked))
        Status = STATUS_ACCESS_DENIED;
    if (!NT_SUCCESS(Status))
    {
        NtfsFileRecordDestroy(File);
        return Status;
    }
    if (File)
    {
        File = AcquireOpenFile(State, Ino, File);
    }
    else
    {
        Open->References++;
        File = Open->File;
    }
    if (WriteAccess && (FileInfo->flags & O_TRUNC))
    {
        Status = NtfsFileRecordSetFileDataSize(File, TypeData, NULL, 0);
        if (!NT_SUCCESS(Status))
        {
            ReleaseOpenFile(State, Ino);
            return Status;
        }
    }
    FileInfo->fh = (uint64_t)(uintptr_t)File;
    SetOpenCaching(State, FileInfo);
    return STATUS_SUCCESS;
}

static int
OpenErrno(NTSTATUS Status)
{
    if (Status == STATUS_ACCESS_DENIED)
        return EACCES;
    if (Status == STATUS_EAS_NOT_SUPPORTED)
        return EOPNOTSUPP;
    return NtStatusToErrno(Status);
}

static void
NtfsLowOpen(fuse_req_t Request, fuse_ino_t Ino, struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!FindOpen(State, Ino))
    {
        bool Owned;

        Status = LoadInode(State, Ino, &File, &Owned);
        assert(!NT_SUCCESS(Status) || Owned);
    }
    if (NT_SUCCESS(Status))
        Status = OpenRecord(State, Ino, File, FileInfo);
    if (NT_SUCCESS(Status) && (FileInfo->flags & O_TRUNC) &&
        (FileInfo->flags & O_ACCMODE) != O_RDONLY)
    {
        Status = KillSetIdBits(State, Request, Ino, (PNtfsFileRecord)(uintptr_t)FileInfo->fh,
                               false, true);
        if (!NT_SUCCESS(Status))
            ReleaseOpenFile(State, Ino);
    }
    if (!NT_SUCCESS(Status))
        fuse_reply_err(Request, OpenErrno(Status));
    else
        fuse_reply_open(Request, FileInfo);
}

static void
NtfsLowCreate(fuse_req_t Request,
              fuse_ino_t Parent,
              const char* Name,
              mode_t Mode,
              struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    struct fuse_entry_param Entry;
    fuse_ino_t Ino;
    NTSTATUS Status;

    Status = CreateChild(State, Request, Parent, Name, S_IFREG | (Mode & 07777), 0, &File);
    if (!NT_SUCCESS(Status))
    {
        ReplyStatus(Request, Status);
        return;
    }
    Ino = InoFromReference(NtfsFileRecordGetReference(File));
    Status = RememberEntry(State, File, &Entry);
    if (NT_SUCCESS(Status))
        Status = OpenRecord(State, Ino, File, FileInfo);
    else
        NtfsFileRecordDestroy(File);
    if (!NT_SUCCESS(Status))
        fuse_reply_err(Request, OpenErrno(Status));
    else
        fuse_reply_create(Request, &Entry, FileInfo);
}

static void
NtfsLowRelease(fuse_req_t Request, fuse_ino_t Ino, struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(FileInfo);
    ReleaseOpenFile(RequestState(Request), Ino);
    fuse_reply_err(Request, 0);
}

static void
NtfsLowRead(fuse_req_t Request,
            fuse_ino_t Ino,
            size_t Size,
            off_t Offset,
            struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = (PNtfsFileRecord)(uintptr_t)FileInfo->fh;
    ULONG Requested = Size > MAXULONG ? MAXULONG : (ULONG)Size;
    ULONG Remaining = Requested;
    NTSTATUS Status;

    if (State->Writable && !State->NoAtime)
    {
        NtfsFuseOpenFile* Open = FindOpen(State, Ino);
        NtfsFileBasicInformation Information;

        if (Open && !Open->AccessChecked &&
            NT_SUCCESS(NtfsFileRecordGetBasicInformation(File, &Information)))
        {
            const UINT64 Day = UINT64_C(864000000000);
            auto Lazy = State->PendingAccess.find(NtfsFileRecordGetReference(File));
            UINT64 Accessed = Open->PendingAccessTime ? Open->PendingAccessTime
                            : Lazy != State->PendingAccess.end() ? Lazy->second
                                                                 : Information.LastAccessTime;
            UINT64 Written = Open->PendingWriteTime ? Open->PendingWriteTime : Information.LastWriteTime;
            UINT64 Changed = Open->PendingWriteTime ? Open->PendingWriteTime : Information.ChangeTime;
            struct timespec Now;
            UINT64 Current;

            clock_gettime(CLOCK_REALTIME, &Now);
            Current = TimespecToNtfsTime(&Now);
            Open->AccessChecked = true;
            if (State->StrictAtime ||
                Accessed <= Written ||
                Accessed <= Changed ||
                Accessed + Day <= Current)
            {
                Open->PendingAccessTime = Current;
            }
        }
    }
    if (State->ReadBuffer.size() < Requested)
        State->ReadBuffer.resize(Requested);
    Status = NtfsFileRecordCopyData(File,
                                    TypeData,
                                    NULL,
                                    State->ReadBuffer.data(),
                                    &Remaining,
                                    (ULONGLONG)Offset);
    if (Status == STATUS_END_OF_FILE)
        fuse_reply_buf(Request, NULL, 0);
    else if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else
        fuse_reply_buf(Request, (const char*)State->ReadBuffer.data(), Requested - Remaining);
}



static void
NtfsLowWrite(fuse_req_t Request,
             fuse_ino_t Ino,
             const char* Buffer,
             size_t Size,
             off_t Offset,
             struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = (PNtfsFileRecord)(uintptr_t)FileInfo->fh;
    auto Open = State->OpenFiles.find(Ino);
    LARGE_INTEGER ByteOffset;
    ULONG Length = Size > MAXULONG ? MAXULONG : (ULONG)Size;
    NTSTATUS Status;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    ByteOffset.QuadPart = (LONGLONG)Offset;
    if (Open != State->OpenFiles.end())
    {
        UINT32 Mask = NtfsFileRecordGetAutomaticTimestampMask(File);
        struct timespec Now;

        if (!Open->second.SetIdChecked && fuse_req_ctx(Request)->uid != 0)
        {
            Status = KillSetIdBits(State, Request, Ino, File, false, true);
            if (!NT_SUCCESS(Status))
            {
                ReplyStatus(Request, Status);
                return;
            }
            Open->second.SetIdChecked = true;
        }
        Open->second.Written = true;
        (void)NtfsFileRecordSetAutomaticTimestampMask(File, 0);
        Status = NtfsFileRecordWriteFileData(File,
                                             TypeData,
                                             NULL,
                                             (PUCHAR)const_cast<char*>(Buffer),
                                             &Length,
                                             &ByteOffset);
        (void)NtfsFileRecordSetAutomaticTimestampMask(File, Mask);
        if (NT_SUCCESS(Status))
        {
            clock_gettime(CLOCK_REALTIME, &Now);
            Open->second.PendingWriteTime = TimespecToNtfsTime(&Now);
        }
    }
    else
    {
        Status = NtfsFileRecordWriteFileData(File,
                                             TypeData,
                                             NULL,
                                             (PUCHAR)const_cast<char*>(Buffer),
                                             &Length,
                                             &ByteOffset);
    }
    if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else
        fuse_reply_write(Request, Length);
}

static void
NtfsLowFsync(fuse_req_t Request, fuse_ino_t Ino, int DataOnly, struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DataOnly);
    UNREFERENCED_PARAMETER(FileInfo);
    if (State->Writable)
    {
        NtfsFuseOpenFile* Open = FindOpen(State, Ino);

        Status = FlushPendingWriteTime(Open);
        if (NT_SUCCESS(Status) && Open)
            Status = NtfsFileRecordFlushFileNameUpdates(Open->File);
        FlushPendingAccess(State);
        if (NT_SUCCESS(Status))
            Status = NtfsFlushVolumeContext(NULL);
    }
    ReplyStatus(Request, Status);
}

static void
NtfsLowFallocate(fuse_req_t Request,
                 fuse_ino_t Ino,
                 int Mode,
                 off_t Offset,
                 off_t Length,
                 struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = (PNtfsFileRecord)(uintptr_t)FileInfo->fh;
    auto Open = State->OpenFiles.find(Ino);
    PAttribute Data;
    ULONGLONG Target;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    /* TODO: FALLOC_FL_PUNCH_HOLE and FALLOC_FL_ZERO_RANGE through the core's zero-data range. */
    if (Mode & ~FALLOC_FL_KEEP_SIZE)
    {
        fuse_reply_err(Request, EOPNOTSUPP);
        return;
    }
    if (Offset < 0 || Length <= 0 || (uint64_t)Offset > UINT64_MAX - (uint64_t)Length)
    {
        fuse_reply_err(Request, EINVAL);
        return;
    }
    Status = KillSetIdBits(State, Request, Ino, File, false, true);
    if (!NT_SUCCESS(Status))
    {
        ReplyStatus(Request, Status);
        return;
    }
    Target = (ULONGLONG)Offset + (ULONGLONG)Length;
    Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    if (Mode & FALLOC_FL_KEEP_SIZE)
    {
        if (Open != State->OpenFiles.end())
            Open->second.Preallocated = true;
        if (!Data || Target > (Data->IsNonResident ? Data->NonResident.AllocatedSize : 0))
            Status = NtfsFileRecordSetFileAllocationSize(File, TypeData, NULL, Target);
    }
    else if (Target > AttributeSize(Data))
    {
        /* FIXME: holes of a sparse file stay unallocated, here and below EOF. */
        Status = NtfsFileRecordSetFileDataSize(File, TypeData, NULL, Target);
    }
    ReplyStatus(Request, Status);
}

static NTSTATUS
GetListing(NtfsFuseState* State, fuse_ino_t Ino, std::shared_ptr<NtfsDirectoryListing>& Result)
{
    auto Cached = State->DirectoryCache.find(Ino);

    if (Cached == State->DirectoryCache.end())
    {
        PNtfsFileRecord File = NULL;
        PNtfsDirectory Directory = NULL;
        NtfsDirectoryListing Listing;
        NtfsDirectoryEntry Entry;
        BOOLEAN Restart = TRUE;
        bool Owned;
        NTSTATUS Status;

        Status = LoadInode(State, Ino, &File, &Owned);
        if (NT_SUCCESS(Status) && !(NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY))
            Status = STATUS_NOT_A_DIRECTORY;
        if (NT_SUCCESS(Status))
        {
            ULONG Context = 0;
            ULONGLONG ParentReference = 0;
            UNICODE_STRING LinkName;

            Listing.Self = NtfsFileRecordGetReference(File);
            Listing.Parent = Listing.Self;
            if (InoFromReference(Listing.Self) != FUSE_ROOT_ID &&
                NT_SUCCESS(NtfsMasterFileTableEnumerateLinks(StateMft(State), File, &Context,
                                                             &ParentReference, &LinkName)))
            {
                Listing.Parent = ParentReference;
            }
            Directory = NtfsDirectoryCreate(State->Volume);
            if (!Directory)
                Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        if (NT_SUCCESS(Status))
            Status = NtfsDirectoryLoadDirectory(Directory, File);
        while (NT_SUCCESS(Status))
        {
            std::string Name;

            Status = NtfsDirectoryReadNext(Directory, Restart, &Entry);
            Restart = FALSE;
            if (Status == STATUS_NO_MORE_FILES)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            if (!NT_SUCCESS(Status))
                break;
            if (!Utf16ToUtf8(Entry.Name, Entry.NameLength, Name))
            {
                Status = STATUS_OBJECT_NAME_INVALID;
                break;
            }
            /* Reparse points and files with EAs stay untyped: only the record knows
             * whether one is a link, a FIFO or a device. */
            Listing.Entries.push_back({std::move(Name),
                                       Entry.FileReference,
                                       (mode_t)((Entry.FileAttributes & FILE_PERM_REPARSE_PT) ||
                                                        (Entry.EaSize != 0 &&
                                                         !(Entry.FileAttributes & FN_DIRECTORY))
                                                    ? 0
                                                    : (Entry.FileAttributes & FN_DIRECTORY)
                                                          ? S_IFDIR
                                                          : S_IFREG)});
        }
        NtfsDirectoryDestroy(Directory);
        ReleaseLoaded(File, Owned);
        if (!NT_SUCCESS(Status))
            return Status;
        Cached = State->DirectoryCache
                     .emplace(Ino, std::make_shared<NtfsDirectoryListing>(std::move(Listing)))
                     .first;
    }
    Result = Cached->second;
    return STATUS_SUCCESS;
}

static void
NtfsLowOpendir(fuse_req_t Request, fuse_ino_t Ino, struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    std::shared_ptr<NtfsDirectoryListing> Listing;
    NTSTATUS Status = GetListing(State, Ino, Listing);

    if (!NT_SUCCESS(Status) && State->RemovedDirectories.count(Ino))
    {
        Listing = std::make_shared<NtfsDirectoryListing>();
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        fuse_reply_err(Request, Status == STATUS_OBJECT_NAME_INVALID ? EILSEQ
                                                                     : NtStatusToErrno(Status));
        return;
    }
    FileInfo->fh = (uint64_t)(uintptr_t)new std::shared_ptr<NtfsDirectoryListing>(std::move(Listing));
    FileInfo->keep_cache = 1;
    FileInfo->cache_readdir = 1;
    fuse_reply_open(Request, FileInfo);
}

static void
NtfsLowReleasedir(fuse_req_t Request, fuse_ino_t Ino, struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(Ino);
    delete (std::shared_ptr<NtfsDirectoryListing>*)(uintptr_t)FileInfo->fh;
    fuse_reply_err(Request, 0);
}

static void
ReadDirectory(fuse_req_t Request,
              fuse_ino_t Ino,
              size_t Size,
              off_t Offset,
              struct fuse_file_info* FileInfo,
              bool Plus)
{
    NtfsFuseState* State = RequestState(Request);
    auto* Handle = (std::shared_ptr<NtfsDirectoryListing>*)(uintptr_t)FileInfo->fh;

    /* TODO: update the directory access time under relatime and strictatime. */
    /* A read from the start (opendir or rewinddir) sees names added since the open. */
    if (Offset == 0)
    {
        std::shared_ptr<NtfsDirectoryListing> Current;
        NTSTATUS Status = GetListing(State, Ino, Current);

        if (!NT_SUCCESS(Status) && State->RemovedDirectories.count(Ino))
        {
            fuse_reply_buf(Request, NULL, 0);
            return;
        }
        if (!NT_SUCCESS(Status))
        {
            fuse_reply_err(Request, Status == STATUS_OBJECT_NAME_INVALID ? EILSEQ
                                                                         : NtStatusToErrno(Status));
            return;
        }
        *Handle = std::move(Current);
    }

    const NtfsDirectoryListing* Listing = Handle->get();
    std::vector<char> Buffer(Size);
    size_t Used = 0;
    size_t Total = Listing->Entries.size() + 2;

    for (size_t Index = Offset < 0 ? 0 : (size_t)Offset; Index < Total; Index++)
    {
        struct fuse_entry_param Entry;
        PNtfsFileRecord File = NULL;
        const char* Name;
        size_t Needed;

        memset(&Entry, 0, sizeof(Entry));
        if (Index < 2)
        {
            Name = Index == 0 ? "." : "..";
            Entry.attr.st_ino = (ino_t)((Index == 0 ? Listing->Self : Listing->Parent) &
                                        UINT64_C(0x0000FFFFFFFFFFFF));
            Entry.attr.st_mode = S_IFDIR;
        }
        else
        {
            const NtfsDirectoryName& Child = Listing->Entries[Index - 2];

            Name = Child.Name.c_str();
            Entry.attr.st_ino = (ino_t)(Child.Reference & UINT64_C(0x0000FFFFFFFFFFFF));
            Entry.attr.st_mode = Child.Type;
            if (Plus)
            {
                auto Open = State->OpenFiles.find(InoFromReference(Child.Reference));
                NTSTATUS Status = STATUS_SUCCESS;

                if (Open == State->OpenFiles.end())
                    Status = NtfsMasterFileTableGetFileRecordByReference(StateMft(State),
                                                                         Child.Reference,
                                                                         &File);
                if (NT_SUCCESS(Status))
                {
                    Status = FillInodeStat(State,
                                           InoFromReference(Child.Reference),
                                           Open != State->OpenFiles.end() ? Open->second.File
                                                                          : File,
                                           &Entry.attr);
                }
                if (NT_SUCCESS(Status))
                {
                    Entry.ino = InoFromReference(Child.Reference);
                    Entry.generation = Child.Reference >> 48;
                    Entry.attr_timeout = NtfsFuseCacheTimeout;
                    Entry.entry_timeout = NtfsFuseCacheTimeout;
                }
            }
        }
        Needed = Plus ? fuse_add_direntry_plus(Request, Buffer.data() + Used, Size - Used, Name,
                                               &Entry, (off_t)Index + 1)
                      : fuse_add_direntry(Request, Buffer.data() + Used, Size - Used, Name,
                                          &Entry.attr, (off_t)Index + 1);
        NtfsFileRecordDestroy(File);
        if (Needed > Size - Used)
            break;
        Used += Needed;
        if (Plus && Index >= 2 && Entry.ino != 0)
        {
            NtfsFuseInode& Inode = State->Inodes[Entry.ino];
            Inode.Reference = Listing->Entries[Index - 2].Reference;
            Inode.Lookups++;
            State->RemovedDirectories.erase(Entry.ino);
        }
    }
    fuse_reply_buf(Request, Buffer.data(), Used);
}

static void
NtfsLowReaddir(fuse_req_t Request, fuse_ino_t Ino, size_t Size, off_t Offset,
               struct fuse_file_info* FileInfo)
{
    ReadDirectory(Request, Ino, Size, Offset, FileInfo, false);
}

static void
NtfsLowReaddirplus(fuse_req_t Request, fuse_ino_t Ino, size_t Size, off_t Offset,
                   struct fuse_file_info* FileInfo)
{
    ReadDirectory(Request, Ino, Size, Offset, FileInfo, true);
}

static void
NtfsLowStatfs(fuse_req_t Request, fuse_ino_t Ino)
{
    NtfsFuseState* State = RequestState(Request);
    LARGE_INTEGER FreeClusters;
    struct statvfs Stat;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Ino);
    memset(&Stat, 0, sizeof(Stat));
    Stat.f_bsize = ClusterBytes(State);
    Stat.f_frsize = Stat.f_bsize;
    Stat.f_blocks = NtfsVolumeGetClustersInVolume(State->Volume);
    Status = NtfsVolumeGetFreeClusters(State->Volume, &FreeClusters);
    if (!NT_SUCCESS(Status))
    {
        ReplyStatus(Request, Status);
        return;
    }
    Stat.f_bfree = FreeClusters.QuadPart;
    Stat.f_bavail = FreeClusters.QuadPart;
    Stat.f_namemax = NTFS_MAX_FILE_NAME_LENGTH;
    if (!State->Writable)
        Stat.f_flag = ST_RDONLY;
    fuse_reply_statfs(Request, &Stat);
}

static void
NtfsLowAccess(fuse_req_t Request, fuse_ino_t Ino, int Mask)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    bool Owned;
    NTSTATUS Status;

    if ((Mask & W_OK) && !State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status) && (Mask & W_OK) && IsReadOnlyFile(File))
    {
        ReleaseLoaded(File, Owned);
        fuse_reply_err(Request, EACCES);
        return;
    }
    ReleaseLoaded(File, Owned);
    ReplyStatus(Request, Status);
}

/*
 * Native NTFS extended attributes are exposed as user.* xattrs. NTFS-3G
 * only offers the packed list through system.ntfs_ea; projecting each EA
 * as its own attribute keeps ordinary getfattr/setfattr tooling usable.
 * EA names are case-insensitive single-byte strings.
 */
static const char NtfsFuseXattrPrefix[] = "user.";

static bool
XattrToEaName(const char* Name, const UCHAR** EaName, size_t* EaNameLength)
{
    size_t PrefixLength = sizeof(NtfsFuseXattrPrefix) - 1;
    size_t Length;

    if (strncmp(Name, NtfsFuseXattrPrefix, PrefixLength) != 0)
        return false;
    Length = strlen(Name + PrefixLength);
    if (Length == 0 || Length > 0xff ||
        IsPosixEaName((const UCHAR*)Name + PrefixLength, Length))
    {
        return false;
    }
    *EaName = (const UCHAR*)(Name + PrefixLength);
    *EaNameLength = Length;
    return true;
}

static bool
EaNamesEqual(const UCHAR* Left, size_t LeftLength, const UCHAR* Right, size_t RightLength)
{
    if (LeftLength != RightLength)
        return false;
    for (size_t Index = 0; Index < LeftLength; Index++)
    {
        if (toupper(Left[Index]) != toupper(Right[Index]))
            return false;
    }
    return true;
}

static NTSTATUS
ReadAllExtendedAttributes(PNtfsFileRecord File, std::vector<UCHAR>& RawData, ULONG* RawLength)
{
    NTSTATUS Status;

    *RawLength = 0;
    Status = NtfsFileRecordReadExtendedAttributes(File, NULL, RawLength, NULL);
    if (Status == STATUS_NO_EAS_ON_FILE)
    {
        *RawLength = 0;
        return STATUS_SUCCESS;
    }
    if (Status != STATUS_BUFFER_TOO_SMALL || *RawLength == 0)
        return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;

    RawData.resize(*RawLength);
    return NtfsFileRecordReadExtendedAttributes(File, RawData.data(), RawLength, NULL);
}

static NTSTATUS
FindExtendedAttribute(const std::vector<UCHAR>& RawData,
                      ULONG RawLength,
                      const UCHAR* Name,
                      size_t NameLength,
                      NtfsExtendedAttributeView* Found,
                      bool* Present)
{
    NtfsExtendedAttributeView View;
    ULONG Offset = 0;
    NTSTATUS Status;

    *Present = false;
    for (;;)
    {
        Status = NtfsGetNextExtendedAttribute(RawData.data(), RawLength, &Offset, &View);
        if (Status == STATUS_NO_MORE_EAS)
            return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status))
            return Status;
        if (EaNamesEqual(View.Name, View.NameLength, Name, NameLength))
        {
            *Found = View;
            *Present = true;
            return STATUS_SUCCESS;
        }
    }
}

static void
NtfsLowGetxattr(fuse_req_t Request, fuse_ino_t Ino, const char* Name, size_t Size)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    NtfsExtendedAttributeView View;
    const UCHAR* EaName;
    size_t EaNameLength;
    ULONG RawLength = 0;
    bool Present = false;
    bool Owned;
    NTSTATUS Status;

    if (!XattrToEaName(Name, &EaName, &EaNameLength))
    {
        fuse_reply_err(Request, ENODATA);
        return;
    }
    Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status))
        Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
    if (NT_SUCCESS(Status))
        Status = FindExtendedAttribute(RawData, RawLength, EaName, EaNameLength, &View, &Present);
    ReleaseLoaded(File, Owned);
    if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else if (!Present)
        fuse_reply_err(Request, ENODATA);
    else if (Size == 0)
        fuse_reply_xattr(Request, View.ValueLength);
    else if (Size < View.ValueLength)
        fuse_reply_err(Request, ERANGE);
    else
        fuse_reply_buf(Request, (const char*)View.Value, View.ValueLength);
}

static void
NtfsLowListxattr(fuse_req_t Request, fuse_ino_t Ino, size_t Size)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    std::string List;
    NtfsExtendedAttributeView View;
    ULONG RawLength = 0;
    ULONG Offset = 0;
    bool Owned;
    NTSTATUS Status;

    Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status))
        Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
    while (NT_SUCCESS(Status) && RawLength != 0)
    {
        Status = NtfsGetNextExtendedAttribute(RawData.data(), RawLength, &Offset, &View);
        if (Status == STATUS_NO_MORE_EAS)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (NT_SUCCESS(Status) && !IsPosixEaName(View.Name, View.NameLength))
        {
            List += NtfsFuseXattrPrefix;
            List.append((const char*)View.Name, View.NameLength);
            List.push_back('\0');
        }
    }
    ReleaseLoaded(File, Owned);
    if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else if (Size == 0)
        fuse_reply_xattr(Request, List.size());
    else if (Size < List.size())
        fuse_reply_err(Request, ERANGE);
    else
        fuse_reply_buf(Request, List.data(), List.size());
}

static void
NtfsLowSetxattr(fuse_req_t Request,
                fuse_ino_t Ino,
                const char* Name,
                const char* Value,
                size_t Size,
                int Flags)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    NtfsExtendedAttributeUpdate Update = {};
    const UCHAR* EaName;
    size_t EaNameLength;
    bool Owned = false;
    NTSTATUS Status;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    if (!XattrToEaName(Name, &EaName, &EaNameLength))
    {
        fuse_reply_err(Request, EOPNOTSUPP);
        return;
    }
    if (Size > 0xffff)
    {
        fuse_reply_err(Request, E2BIG);
        return;
    }
    Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status) && (Flags & (XATTR_CREATE | XATTR_REPLACE)))
    {
        std::vector<UCHAR> RawData;
        NtfsExtendedAttributeView View;
        ULONG RawLength = 0;
        bool Present = false;

        Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
        if (NT_SUCCESS(Status))
            Status = FindExtendedAttribute(RawData, RawLength, EaName, EaNameLength, &View, &Present);
        if (NT_SUCCESS(Status) && (Flags & XATTR_CREATE) && Present)
            Status = STATUS_OBJECT_NAME_COLLISION;
        if (NT_SUCCESS(Status) && (Flags & XATTR_REPLACE) && !Present)
        {
            ReleaseLoaded(File, Owned);
            fuse_reply_err(Request, ENODATA);
            return;
        }
    }
    if (NT_SUCCESS(Status))
    {
        Update.Operation = NTFS_EA_UPDATE_SET;
        Update.NameLength = (UINT8)EaNameLength;
        Update.ValueLength = (UINT16)Size;
        Update.Name = EaName;
        Update.Value = (const UCHAR*)Value;
        Status = NtfsFileRecordUpdateExtendedAttributes(File, &Update, 1);
    }
    ReleaseLoaded(File, Owned);
    ReplyStatus(Request, Status);
}

static void
NtfsLowRemovexattr(fuse_req_t Request, fuse_ino_t Ino, const char* Name)
{
    NtfsFuseState* State = RequestState(Request);
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    NtfsExtendedAttributeView View;
    NtfsExtendedAttributeUpdate Update = {};
    const UCHAR* EaName;
    size_t EaNameLength;
    ULONG RawLength = 0;
    bool Present = false;
    bool Owned = false;
    NTSTATUS Status;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    if (!XattrToEaName(Name, &EaName, &EaNameLength))
    {
        fuse_reply_err(Request, ENODATA);
        return;
    }
    Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status))
        Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
    if (NT_SUCCESS(Status))
        Status = FindExtendedAttribute(RawData, RawLength, EaName, EaNameLength, &View, &Present);
    if (NT_SUCCESS(Status) && !Present)
    {
        ReleaseLoaded(File, Owned);
        fuse_reply_err(Request, ENODATA);
        return;
    }
    if (NT_SUCCESS(Status))
    {
        Update.Operation = NTFS_EA_UPDATE_REMOVE;
        Update.NameLength = (UINT8)EaNameLength;
        Update.Name = EaName;
        Status = NtfsFileRecordUpdateExtendedAttributes(File, &Update, 1);
    }
    ReleaseLoaded(File, Owned);
    ReplyStatus(Request, Status);
}

static int
MountImage(int Argc, char** Argv, NtfsFuseState* State)
{
    struct fuse_lowlevel_ops Operations = {};
    struct fuse_args Arguments = FUSE_ARGS_INIT(Argc, Argv);
    struct fuse_cmdline_opts Options;
    struct fuse_session* Session;
    PNtfsFileRecord Root = NULL;
    int Result = 1;

    if (!NT_SUCCESS(NtfsMasterFileTableGetFileRecordFromQuery(StateMft(State),
                                                              const_cast<PWCHAR>(L"\\"),
                                                              &Root)))
    {
        fprintf(stderr, "cannot open the root directory\n");
        return 1;
    }
    State->Inodes[FUSE_ROOT_ID] = NtfsFuseInode{NtfsFileRecordGetReference(Root), 1};
    NtfsFileRecordDestroy(Root);

    Operations.init = NtfsLowInit;
    Operations.lookup = NtfsLowLookup;
    Operations.forget = NtfsLowForget;
    Operations.forget_multi = NtfsLowForgetMulti;
    Operations.getattr = NtfsLowGetattr;
    Operations.setattr = NtfsLowSetattr;
    Operations.readlink = NtfsLowReadlink;
    Operations.mknod = NtfsLowMknod;
    Operations.mkdir = NtfsLowMkdir;
    Operations.unlink = NtfsLowUnlink;
    Operations.rmdir = NtfsLowRmdir;
    Operations.symlink = NtfsLowSymlink;
    Operations.rename = NtfsLowRename;
    Operations.link = NtfsLowLink;
    Operations.open = NtfsLowOpen;
    Operations.read = NtfsLowRead;
    Operations.write = NtfsLowWrite;
    Operations.release = NtfsLowRelease;
    Operations.fsync = NtfsLowFsync;
    Operations.opendir = NtfsLowOpendir;
    Operations.readdir = NtfsLowReaddir;
    Operations.readdirplus = NtfsLowReaddirplus;
    Operations.releasedir = NtfsLowReleasedir;
    Operations.statfs = NtfsLowStatfs;
    Operations.setxattr = NtfsLowSetxattr;
    Operations.getxattr = NtfsLowGetxattr;
    Operations.listxattr = NtfsLowListxattr;
    Operations.removexattr = NtfsLowRemovexattr;
    Operations.access = NtfsLowAccess;
    Operations.create = NtfsLowCreate;
    Operations.fallocate = NtfsLowFallocate;

    if (fuse_parse_cmdline(&Arguments, &Options) != 0)
        return 2;
    if (!Options.mountpoint)
    {
        fprintf(stderr, "missing mount point\n");
        fuse_opt_free_args(&Arguments);
        return 2;
    }
    Session = fuse_session_new(&Arguments, &Operations, sizeof(Operations), State);
    if (Session)
    {
        State->Session = Session;
        if (fuse_set_signal_handlers(Session) == 0)
        {
            if (fuse_session_mount(Session, Options.mountpoint) == 0)
            {
                fuse_daemonize(Options.foreground);
                Result = fuse_session_loop(Session) == 0 ? 0 : 1;
                fuse_session_unmount(Session);
            }
            fuse_remove_signal_handlers(Session);
        }
        State->Session = NULL;
        fuse_session_destroy(Session);
    }
    while (!State->OpenFiles.empty())
    {
        auto Open = State->OpenFiles.begin();

        Open->second.References = 1;
        ReleaseOpenFile(State, Open->first);
    }
    FlushPendingAccess(State);
    free(Options.mountpoint);
    fuse_opt_free_args(&Arguments);
    return Result;
}
#endif

struct ShrinkImageIo
{
    int Fd;
};

static int
ShrinkImageRead(void* Context, uint64_t Offset, uint32_t Length, void* Buffer)
{
    ShrinkImageIo* Io = (ShrinkImageIo*)Context;

    return pread(Io->Fd, Buffer, Length, (off_t)Offset) == (ssize_t)Length;
}

static int
ShrinkImageWrite(void* Context, uint64_t Offset, uint32_t Length, const void* Buffer)
{
    ShrinkImageIo* Io = (ShrinkImageIo*)Context;

    return pwrite(Io->Fd, Buffer, Length, (off_t)Offset) == (ssize_t)Length;
}

static NTSTATUS
ResizeImageVolume(const char* Path, ULONGLONG NewVolumeSectors, bool Grow)
{
    std::vector<uint8_t> Workspace(EXPAND_WORKSPACE_BYTES);
    ShrinkImageIo Io;
    EXPAND_DEVICE Device;
    uint8_t Boot[512];
    struct stat Info;
    int Result;

    Io.Fd = open(Path, O_RDWR);
    if (Io.Fd < 0)
        return STATUS_INVALID_DEVICE_STATE;
    if (fstat(Io.Fd, &Info) != 0 ||
        pread(Io.Fd, Boot, sizeof(Boot), 0) != (ssize_t)sizeof(Boot))
    {
        close(Io.Fd);
        return STATUS_INVALID_DEVICE_STATE;
    }
    memset(&Device, 0, sizeof(Device));
    Device.Context = &Io;
    Device.Read = ShrinkImageRead;
    Device.Write = ShrinkImageWrite;
    Device.DiskSize = (uint64_t)Info.st_size;
    Device.SectorSize = (uint32_t)Boot[11] | ((uint32_t)Boot[12] << 8);
    Result = Grow
        ? ExpandGrowVolume(&Device, 0, NewVolumeSectors, Workspace.data())
        : ExpandShrinkVolume(&Device, 0, NewVolumeSectors, Workspace.data());
    fsync(Io.Fd);
    close(Io.Fd);
    if (Result != EXPAND_OK)
        fprintf(stderr, "resize: %s\n", ExpandStatusText(Result));
    switch (Result)
    {
        case EXPAND_OK:
            return STATUS_SUCCESS;
        case EXPAND_BAD_PARAMETER:
        case EXPAND_NOTHING_TO_DO:
            return STATUS_INVALID_PARAMETER;
        case EXPAND_NO_ROOM:
            return STATUS_ALREADY_COMMITTED;
        default:
            return STATUS_FILE_CORRUPT_ERROR;
    }
}

struct EvacuationItem
{
    ULONGLONG BaseReference;
    AttributeType Type;
    std::vector<WCHAR> Name;
    ULONGLONG Vcn;
    ULONGLONG Count;
    bool Units;
};

/* Decodes one attribute's mapping pairs into (VCN, LCN or -1, length). */
static bool
DecodeMappingPairs(const UCHAR* Pairs,
                   const UCHAR* End,
                   ULONGLONG Vcn,
                   std::vector<std::tuple<ULONGLONG, LONGLONG, ULONGLONG>>& Runs)
{
    LONGLONG Lcn = 0;

    while (Pairs < End && *Pairs != 0)
    {
        unsigned LengthBytes = *Pairs & 0x0f;
        unsigned OffsetBytes = *Pairs >> 4;
        ULONGLONG Length = 0;
        LONGLONG Delta = 0;

        if (LengthBytes == 0 || LengthBytes > 8 || OffsetBytes > 8 ||
            Pairs + 1 + LengthBytes + OffsetBytes > End)
        {
            return false;
        }
        for (unsigned Index = 0; Index < LengthBytes; Index++)
            Length |= (ULONGLONG)Pairs[1 + Index] << (8 * Index);
        for (unsigned Index = 0; Index < OffsetBytes; Index++)
            Delta |= (LONGLONG)Pairs[1 + LengthBytes + Index] << (8 * Index);
        if (OffsetBytes != 0 && OffsetBytes < 8 && (Pairs[LengthBytes + OffsetBytes] & 0x80))
            Delta -= (LONGLONG)1 << (8 * OffsetBytes);
        if (OffsetBytes == 0)
        {
            Runs.emplace_back(Vcn, -1, Length);
        }
        else
        {
            Lcn += Delta;
            Runs.emplace_back(Vcn, Lcn, Length);
        }
        Vcn += Length;
        Pairs += 1 + LengthBytes + OffsetBytes;
    }
    return true;
}

/*
 * Moves every movable cluster at or beyond Boundary below it, the way a
 * defragmenter prepares a shrink: new allocations are capped at Boundary
 * first, then each nonresident attribute run past it is moved into free
 * space below it. $MFTMirr and $LogFile are left to the offline shrink;
 * any other unmovable system file past Boundary fails the evacuation.
 */
static NTSTATUS
EvacuateAbove(NtfsFuseState* State, ULONGLONG Boundary)
{
    PNtfsMasterFileTable Mft = NtfsVolumeGetMft(State->Volume);
    NtfsVolumeInformation Information;
    std::vector<EvacuationItem> Items;
    std::vector<UCHAR> Record;
    std::vector<UCHAR> Bitmap;
    ULONGLONG Requested;
    ULONGLONG Cursor = 0;
    NTSTATUS Status;

    Status = NtfsVolumeQueryInformation(State->Volume, &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Boundary == 0 || Boundary >= Information.TotalClusters)
        return STATUS_INVALID_PARAMETER;
    NtfsVolumeSetAllocationLimit(State->Volume, Boundary);

    Record.resize(Information.BytesPerFileRecordSegment);
    Requested = Information.MftValidDataLength / Information.BytesPerFileRecordSegment;
    while (Requested-- != 0)
    {
        ULONGLONG Returned = 0;
        ULONG Length = (ULONG)Record.size();
        PFileRecordHeader Header;
        ULONGLONG Base;
        ULONG Offset;

        Status = NtfsMasterFileTableReadFileRecord(Mft, Requested, &Returned, Record.data(), &Length);
        if (!NT_SUCCESS(Status))
            return Status;
        Requested = Returned & UINT64_C(0x0000FFFFFFFFFFFF);
        Header = (PFileRecordHeader)Record.data();
        Base = Header->BaseFileRecord
            ? Header->BaseFileRecord
            : (Returned & UINT64_C(0x0000FFFFFFFFFFFF)) | ((ULONGLONG)Header->SequenceNumber << 48);
        for (Offset = Header->AttributeOffset; Offset + 16 <= Header->ActualSize;)
        {
            PAttribute Attribute = (PAttribute)(Record.data() + Offset);
            std::vector<std::tuple<ULONGLONG, LONGLONG, ULONGLONG>> Runs;
            ULONGLONG BaseNumber = Base & UINT64_C(0x0000FFFFFFFFFFFF);

            if (Attribute->AttributeType == TypeAttributeEndMarker || Attribute->Length == 0 ||
                Offset + Attribute->Length > Header->ActualSize)
            {
                break;
            }
            if (Attribute->IsNonResident &&
                !DecodeMappingPairs((const UCHAR*)Attribute + Attribute->NonResident.DataRunsOffset,
                                    (const UCHAR*)Attribute + Attribute->Length,
                                    Attribute->NonResident.FirstVCN,
                                    Runs))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            for (auto& Run : Runs)
            {
                ULONGLONG Vcn = std::get<0>(Run);
                LONGLONG Lcn = std::get<1>(Run);
                ULONGLONG Count = std::get<2>(Run);

                if (Lcn < 0 || (ULONGLONG)Lcn + Count <= Boundary)
                    continue;
                if (BaseNumber == _MFTMirr || BaseNumber == _LogFile)
                    continue;
                if (BaseNumber == _Bitmap || BaseNumber == _Boot || BaseNumber == _BadClus)
                {
                    fprintf(stderr, "shrink: record %llu has clusters past the new end\n",
                            (unsigned long long)BaseNumber);
                    return STATUS_ALREADY_COMMITTED;
                }
                EvacuationItem Item;
                ULONGLONG Skip = (ULONGLONG)Lcn < Boundary ? Boundary - (ULONGLONG)Lcn : 0;

                Item.BaseReference = Base;
                Item.Type = (AttributeType)Attribute->AttributeType;
                Item.Name.assign((const WCHAR*)((const UCHAR*)Attribute + Attribute->NameOffset),
                                 (const WCHAR*)((const UCHAR*)Attribute + Attribute->NameOffset) +
                                     Attribute->NameLength);
                Item.Name.push_back(L'\0');
                Item.Vcn = Vcn + Skip;
                Item.Count = Count - Skip;
                Item.Units = (Attribute->Flags & (ATTR_COMPRESSED | ATTR_SPARSE)) != 0;
                Items.push_back(std::move(Item));
            }
            Offset += Attribute->Length;
        }
    }
    if (Items.empty())
        return STATUS_SUCCESS;

    {
        ULONGLONG Returned = 0;
        ULONGLONG Size = 0;
        ULONG Length = (ULONG)((Boundary + 7) / 8 + 8);

        Bitmap.resize(Length);
        Status = NtfsVolumeReadBitmap(State->Volume, 0, &Returned, &Size, Bitmap.data(), &Length);
        if (Status == STATUS_BUFFER_OVERFLOW)
            Status = STATUS_SUCCESS;
        if (!NT_SUCCESS(Status))
            return Status;
    }
    auto Used = [&Bitmap](ULONGLONG Lcn) { return (Bitmap[Lcn / 8] >> (Lcn & 7)) & 1; };

    for (EvacuationItem& Item : Items)
    {
        PNtfsFileRecord File = NULL;

        Status = NtfsMasterFileTableGetFileRecordByReference(Mft, Item.BaseReference, &File);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Item.Units)
        {
            ULONGLONG End = Item.Vcn + Item.Count;

            Item.Vcn -= Item.Vcn % 16;
            Item.Count = ((End + 15) / 16) * 16 - Item.Vcn;
        }
        while (NT_SUCCESS(Status) && Item.Count != 0)
        {
            ULONGLONG Start;
            ULONGLONG Free = 0;
            ULONGLONG Want = Item.Units ? 16 : Item.Count;

            while (Cursor < Boundary && Used(Cursor))
                Cursor++;
            for (Start = Cursor; Start < Boundary && Free < Want;)
            {
                if (Used(Start + Free) || Start + Free >= Boundary)
                {
                    if (Item.Units)
                    {
                        Start += Free + 1;
                        Free = 0;
                        continue;
                    }
                    break;
                }
                Free++;
            }
            if (Free == 0 || Start >= Boundary)
            {
                Status = STATUS_DISK_FULL;
                break;
            }
            ULONG Take = (ULONG)std::min<ULONGLONG>(Free, Item.Units ? 16 : Item.Count);

            Status = NtfsFileRecordMoveClusters(File,
                                                Item.Type,
                                                Item.Name.size() > 1 ? Item.Name.data() : NULL,
                                                Item.Vcn,
                                                Start,
                                                Take);
            if (NT_SUCCESS(Status))
            {
                for (ULONGLONG Lcn = Start; Lcn < Start + Take; Lcn++)
                    Bitmap[Lcn / 8] |= (UCHAR)(1u << (Lcn & 7));
                Item.Vcn += Take;
                Item.Count -= Take;
            }
        }
        NtfsFileRecordDestroy(File);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr, "shrink: moving record %llu type 0x%x failed 0x%08" PRIx32 "\n",
                    (unsigned long long)(Item.BaseReference & UINT64_C(0x0000FFFFFFFFFFFF)),
                    (unsigned)Item.Type, (uint32_t)Status);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

#if NTFSLIB_EXPAND_OFFLINE
static void
ExpandDiskLog(void* Context, const char* Line)
{
    UNREFERENCED_PARAMETER(Context);
    fputs(Line, stdout);
}

/* Grows one partition of a whole-disk image to the end of the image file,
 * then its NTFS volume, as lntexpandstorage does on a live disk. */
static int
ExpandDiskImage(const char* Path, uint32_t PartitionNumber)
{
    ShrinkImageIo Io;
    EXPAND_DEVICE Device;
    EXPAND_PLAN Plan;
    struct stat Info;
    int Result;

    Io.Fd = open(Path, O_RDWR);
    if (Io.Fd < 0 || fstat(Io.Fd, &Info) != 0)
    {
        perror(Path);
        return 1;
    }
    memset(&Device, 0, sizeof(Device));
    Device.Context = &Io;
    Device.Read = ShrinkImageRead;
    Device.Write = ShrinkImageWrite;
    Device.Log = ExpandDiskLog;
    Device.DiskSize = (uint64_t)Info.st_size;
    Device.SectorSize = 512;
    Result = ExpandBuildPlan(&Device, PartitionNumber, 0, 0, &Plan);
    if (Result == EXPAND_OK)
        Result = ExpandApplyPlan(&Device, &Plan);
    fsync(Io.Fd);
    close(Io.Fd);
    printf("expand-disk: %s\n", ExpandStatusText(Result));
    return Result == EXPAND_OK ? 0 : 1;
}
#endif

static void
PrintUsage(const char* Program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s --format IMAGE SECTORS HIDDEN_SECTORS SECTORS_PER_CLUSTER LABEL [BYTES_PER_SECTOR]\n"
            "  %s [--show-metadata] --probe IMAGE\n"
            "  %s [--show-metadata] --list IMAGE [PATH]\n"
            "  %s [--show-metadata] --list-info IMAGE [PATH]\n"
            "  %s [--show-metadata] --cat IMAGE PATH\n"
            "  %s [--show-metadata] --cat-range IMAGE PATH OFFSET LENGTH\n"
            "  %s [--show-metadata] --readlink IMAGE PATH\n"
            "  %s [--show-metadata] --reparse IMAGE PATH\n"
            "  %s [--show-metadata] --ea IMAGE PATH\n"
            "  %s [--show-metadata] --security IMAGE PATH\n"
            "  %s [--show-metadata] --basic IMAGE PATH\n"
            "  %s --recover IMAGE\n"
            "  %s --check IMAGE [--fix]\n"
            "  %s --set-label IMAGE LABEL\n"
            "  %s --set-basic IMAGE PATH CREATION ACCESS WRITE CHANGE ATTRS\n"
            "  %s --set-security IMAGE PATH SOURCE\n"
            "  %s --apply-security IMAGE POLICY\n"
            "  %s --set-reparse IMAGE PATH SOURCE\n"
            "  %s --delete-reparse IMAGE PATH SOURCE\n"
            "  %s --set-ea IMAGE PATH FLAGS NAME SOURCE\n"
            "  %s --remove-ea IMAGE PATH NAME\n"
            "  %s --delete-external-backing IMAGE PATH\n"
            "  %s [--short-names] --create-file IMAGE PATH [ATTRS]\n"
            "  %s [--short-names] --create-dir IMAGE PATH [ATTRS]\n"
            "  %s --remove IMAGE PATH\n"
            "  %s --remove-dir IMAGE PATH\n"
            "  %s --unlink IMAGE PATH\n"
            "  %s --unlink-dir IMAGE PATH\n"
            "  %s --delete-unlinked IMAGE REFERENCE\n"
            "  %s [--short-names] --rename IMAGE OLD NEW\n"
            "  %s --link IMAGE EXISTING NEW\n"
            "  %s --write IMAGE PATH OFFSET SOURCE\n"
            "  %s --addfiles IMAGE LIST\n"
            "  %s --add-lx IMAGE LIST\n"
            "  %s --truncate IMAGE PATH SIZE\n"
            "  %s --allocate IMAGE PATH SIZE\n"
            "  %s --set-sparse IMAGE PATH 0|1\n"
            "  %s --zero-data IMAGE PATH OFFSET BEYOND_FINAL_ZERO\n"
            "  %s --allocated-ranges IMAGE PATH OFFSET LENGTH\n"
            "  %s --retrieval-pointers IMAGE PATH START_VCN\n"
            "  %s --volume-bitmap IMAGE START_LCN BYTE_COUNT\n"
            "  %s --volume-data IMAGE\n"
            "  %s --move IMAGE PATH START_VCN START_LCN CLUSTER_COUNT [CLUSTER_LIMIT]\n"
            "  %s --shrink IMAGE NEW_NUMBER_OF_SECTORS\n"
            "  %s --grow IMAGE NEW_NUMBER_OF_SECTORS\n"
            "  %s --expand-disk DISK_IMAGE [PARTITION_NUMBER]\n"
            "  %s --mft-record IMAGE FILE_REFERENCE\n"
            "  %s [--show-metadata] --streams IMAGE PATH\n"
            "  %s [--show-metadata] [--writable [--direct-io] [--writeback-cache]]"
            " IMAGE MOUNTPOINT [FUSE options]\n",
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program);
}

int
main(int Argc, char** Argv)
{
    NtfsFuseState State = {};
    bool ShowMetadata = false;
    int First = 1;
    int Result;
    NTSTATUS Status;

    if (First < Argc && strcmp(Argv[First], "--short-names") == 0)
    {
        NtfsSetGenerate8dot3Names(TRUE);
        First++;
    }
    if (First < Argc && strcmp(Argv[First], "--show-metadata") == 0)
    {
        ShowMetadata = true;
        First++;
    }

    if (First >= Argc)
    {
        PrintUsage(Argv[0]);
        return 2;
    }

    if (strcmp(Argv[First], "--format") == 0)
    {
        ULONGLONG SectorCount;
        ULONGLONG HiddenSectors;
        ULONGLONG SectorsPerCluster;
        ULONGLONG BytesPerSector = 512;

        if ((First + 6 != Argc && First + 7 != Argc) ||
            ShowMetadata ||
            !ParseUnsigned(Argv[First + 2],
                           INT64_MAX / 512,
                           &SectorCount) ||
            !ParseUnsigned(Argv[First + 3],
                           MAXULONG,
                           &HiddenSectors) ||
            !ParseUnsigned(Argv[First + 4],
                           128,
                           &SectorsPerCluster) ||
            (First + 7 == Argc &&
             !ParseUnsigned(Argv[First + 6],
                            4096,
                            &BytesPerSector)))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        return FormatImage(Argv[First + 1],
                           SectorCount,
                           static_cast<ULONG>(HiddenSectors),
                           static_cast<ULONG>(SectorsPerCluster),
                           Argv[First + 5],
                           static_cast<ULONG>(BytesPerSector));
    }

    if (strcmp(Argv[First], "--probe") == 0)
    {
        if (First + 2 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ProbeImage(&State);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--list") == 0 ||
        strcmp(Argv[First], "--list-info") == 0)
    {
        bool IncludeAllocation =
            strcmp(Argv[First], "--list-info") == 0;
        const char* Path;
        if (First + 2 > Argc || First + 3 < Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Path = First + 2 < Argc ? Argv[First + 2] : "/";
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ListDirectory(&State,
                               Path,
                               IncludeAllocation);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--cat") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = CatFile(&State,
                         Argv[First + 2],
                         0,
                         ~(UINT64)0);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--cat-range") == 0)
    {
        char* End;
        UINT64 Offset;
        UINT64 Length;

        if (First + 5 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        Offset = strtoull(Argv[First + 3], &End, 0);
        if (errno || *Argv[First + 3] == '-' || *End)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        Length = strtoull(Argv[First + 4], &End, 0);
        if (errno || *Argv[First + 4] == '-' || *End)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = CatFile(&State,
                         Argv[First + 2],
                         Offset,
                         Length);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--lookup-reparse") == 0)
    {
        bool OpenFinal;

        if (First + 4 != Argc ||
            (strcmp(Argv[First + 3], "0") != 0 &&
             strcmp(Argv[First + 3], "1") != 0))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        OpenFinal =
            strcmp(Argv[First + 3], "1") == 0;
        Status = OpenImage(
            Argv[First + 1],
            ShowMetadata,
            &State);
        if (!NT_SUCCESS(Status))
            return 1;
        Result = ProbeReparseLookup(
            State.Volume,
            Argv[First + 2],
            OpenFinal);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--readlink") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ReadLink(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--reparse") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DumpReparsePoint(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--ea") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ListExtendedAttributes(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--security") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DumpSecurityDescriptor(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--basic") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintBasicInformation(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--recover") == 0)
    {
        if (First + 2 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = NtfsVolumeIsReadOnly(State.Volume) ? 3 : 0;
        printf("%s\n", Result ? "read-only" : "writable");
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--check") == 0)
    {
        NtfsCheckResult Check;
        USHORT Flags = 0;
        bool Fix = First + 3 == Argc && strcmp(Argv[First + 2], "--fix") == 0;

        if ((First + 2 != Argc && !Fix) || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = Fix ? OpenImageWritable(Argv[First + 1], &State)
                     : OpenImage(Argv[First + 1], false, &State);
        if (NT_SUCCESS(Status))
            Status = NtfsVolumeQueryFlags(State.Volume, &Flags);
        if (NT_SUCCESS(Status))
            Status = NtfsVolumeCheck(State.Volume, Fix ? TRUE : FALSE, PrintCheckEvent, NULL, &Check);
        if (NT_SUCCESS(Status) && Fix && Check.Errors == 0 && (Flags & VOL_DIRTY))
            Status = NtfsVolumeSetFlags(State.Volume, 0, VOL_DIRTY);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            if (State.Volume)
                CloseImage(&State);
            return 1;
        }
        printf("dirty=%u records=%" PRIu64 " directories=%" PRIu64 " entries=%" PRIu64
               " used=%" PRIu64 " lost=%" PRIu64 " freed=%" PRIu64 " claimed=%" PRIu64 " errors=%" PRIu64 "\n",
               (Flags & VOL_DIRTY) ? 1u : 0u,
               (uint64_t)Check.FileRecords, (uint64_t)Check.Directories,
               (uint64_t)Check.IndexEntries, (uint64_t)Check.UsedClusters,
               (uint64_t)Check.LostClusters, (uint64_t)Check.FreedClusters,
               (uint64_t)Check.ClaimedClusters,
               (uint64_t)Check.Errors);
        CloseImage(&State);
        return Check.Errors ? 3 : 0;
    }

    if (strcmp(Argv[First], "--set-label") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetImageLabel(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-basic") == 0)
    {
        static const UINT32 Masks[] =
        {
            NTFS_BASIC_INFO_CREATION_TIME,
            NTFS_BASIC_INFO_LAST_ACCESS_TIME,
            NTFS_BASIC_INFO_LAST_WRITE_TIME,
            NTFS_BASIC_INFO_CHANGE_TIME,
            NTFS_BASIC_INFO_FILE_ATTRIBUTES
        };
        NtfsFileBasicInformation Information = {};
        ULONGLONG Values[RTL_NUMBER_OF(Masks)] = {};

        if (First + 8 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        for (ULONG Index = 0;
             Index < RTL_NUMBER_OF(Masks);
             Index++)
        {
            bool Present;
            ULONGLONG Maximum =
                Masks[Index] ==
                    NTFS_BASIC_INFO_FILE_ATTRIBUTES
                ? MAXULONG
                : INT64_MAX;

            if (!ParseOptionalUnsigned(
                    Argv[First + 3 + Index],
                    Maximum,
                    &Values[Index],
                    &Present))
            {
                fprintf(stderr,
                        "invalid basic-information value\n");
                return 2;
            }
            if (Present)
                Information.Fields |= Masks[Index];
        }
        Information.CreationTime = Values[0];
        Information.LastAccessTime = Values[1];
        Information.LastWriteTime = Values[2];
        Information.ChangeTime = Values[3];
        Information.FileAttributes =
            (UINT32)Values[4];

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetBasicInformation(
            &State,
            Argv[First + 2],
            &Information);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-ea") == 0)
    {
        ULONGLONG ParsedFlags;
        char* End;

        if (First + 6 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        ParsedFlags = strtoull(Argv[First + 3],
                               &End,
                               0);
        if (errno != 0 ||
            *Argv[First + 3] == '\0' ||
            *End != '\0' ||
            (ParsedFlags != 0 &&
             ParsedFlags != NTFS_EA_FLAG_NEED_EA))
        {
            fprintf(stderr,
                    "EA flags must be 0 or 0x80\n");
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = UpdateExtendedAttribute(
            &State,
            Argv[First + 2],
            (UINT8)ParsedFlags,
            Argv[First + 4],
            Argv[First + 5],
            false);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-security") == 0)
    {
        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetSecurityFromHost(
            &State,
            Argv[First + 2],
            Argv[First + 3]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--apply-security") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ApplySecurityFromPolicy(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-reparse") == 0 ||
        strcmp(Argv[First], "--delete-reparse") == 0)
    {
        bool Delete =
            strcmp(Argv[First], "--delete-reparse") == 0;

        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = UpdateReparsePointFromHost(
            &State,
            Argv[First + 2],
            Argv[First + 3],
            Delete);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--remove-ea") == 0)
    {
        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = UpdateExtendedAttribute(
            &State,
            Argv[First + 2],
            0,
            Argv[First + 3],
            NULL,
            true);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--delete-external-backing") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DeleteExternalBacking(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--write") == 0)
    {
        ULONGLONG Offset;
        char* End;

        if (First + 5 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        Offset = strtoull(Argv[First + 3], &End, 0);
        if (errno != 0 || *Argv[First + 3] == '\0' || *End != '\0' ||
            Offset > INT64_MAX)
        {
            fprintf(stderr, "invalid write offset\n");
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = WriteFileFromHost(&State,
                                   Argv[First + 2],
                                   Offset,
                                   Argv[First + 4]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--add-lx") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = AddLinuxTreeFromList(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--addfiles") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = AddFilesFromList(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--create-file") == 0 ||
        strcmp(Argv[First], "--create-dir") == 0)
    {
        bool IsDirectory =
            strcmp(Argv[First],
                   "--create-dir") == 0;
        ULONGLONG FileAttributes = 0;

        if (First + 3 > Argc ||
            First + 4 < Argc ||
            ShowMetadata ||
            (First + 4 == Argc &&
             !ParseUnsigned(Argv[First + 3],
                            MAXULONG,
                            &FileAttributes)))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = CreateNode(
            &State,
            Argv[First + 2],
            IsDirectory,
            (ULONG)FileAttributes);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--remove") == 0 ||
        strcmp(Argv[First], "--remove-dir") == 0)
    {
        bool IsDirectory =
            strcmp(Argv[First],
                   "--remove-dir") == 0;

        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = RemoveNode(
            &State,
            Argv[First + 2],
            IsDirectory);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--unlink") == 0 ||
        strcmp(Argv[First], "--unlink-dir") == 0)
    {
        bool IsDirectory =
            strcmp(Argv[First],
                   "--unlink-dir") == 0;

        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = UnlinkNode(
            &State,
            Argv[First + 2],
            IsDirectory);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--delete-unlinked") == 0)
    {
        ULONGLONG FileReference;

        if (First + 3 != Argc || ShowMetadata ||
            !ParseUnsigned(Argv[First + 2],
                           ~(ULONGLONG)0,
                           &FileReference))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DeleteUnlinkedNode(&State, FileReference);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--rename") == 0 ||
        strcmp(Argv[First], "--link") == 0)
    {
        bool HardLink =
            strcmp(Argv[First], "--link") == 0;

        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = RenameNode(
            &State,
            Argv[First + 2],
            Argv[First + 3],
            HardLink);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-compression") == 0)
    {
        PNtfsFileRecord File = NULL;
        AttributeType RequestedType = TypeData;
        PWSTR RequestedStream = NULL;

        if (First + 4 != Argc || ShowMetadata ||
            (strcmp(Argv[First + 3], "0") != 0 &&
             strcmp(Argv[First + 3], "1") != 0))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(Argv[First + 1], &State);
        if (NT_SUCCESS(Status))
        {
            Status = LookupAttribute(State.Volume,
                                     Argv[First + 2],
                                     &File,
                                     &RequestedType,
                                     &RequestedStream);
            if (NT_SUCCESS(Status))
            {
                Status = NtfsFileRecordSetCompression(
                    File,
                    RequestedStream,
                    strcmp(Argv[First + 3], "1") == 0);
            }
            NtfsFileRecordDestroy(File);
            delete[] RequestedStream;
            CloseImage(&State);
        }
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 2],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        return 0;
    }

    if (strcmp(Argv[First], "--set-sparse") == 0)
    {
        bool SetSparse;

        if (First + 4 != Argc || ShowMetadata ||
            (strcmp(Argv[First + 3], "0") != 0 &&
             strcmp(Argv[First + 3], "1") != 0))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        SetSparse =
            strcmp(Argv[First + 3], "1") == 0;

        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetSparseState(
            &State,
            Argv[First + 2],
            SetSparse);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--zero-data") == 0 ||
        strcmp(Argv[First],
               "--allocated-ranges") == 0)
    {
        bool Query =
            strcmp(Argv[First],
                   "--allocated-ranges") == 0;
        ULONGLONG Start;
        ULONGLONG EndOrLength;

        if (First + 5 != Argc ||
            (!Query && ShowMetadata) ||
            !ParseUnsigned(Argv[First + 3],
                           INT64_MAX,
                           &Start) ||
            !ParseUnsigned(Argv[First + 4],
                           INT64_MAX,
                           &EndOrLength))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = Query
            ? OpenImage(Argv[First + 1],
                        ShowMetadata,
                        &State)
            : OpenImageWritable(Argv[First + 1],
                                &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = Query
            ? PrintAllocatedRanges(
                &State,
                Argv[First + 2],
                Start,
                EndOrLength)
            : SetZeroDataRange(
                &State,
                Argv[First + 2],
                Start,
                EndOrLength);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--retrieval-pointers") == 0)
    {
        ULONGLONG StartingVcn;

        if (First + 4 != Argc ||
            !ParseUnsigned(Argv[First + 3],
                           INT64_MAX,
                           &StartingVcn))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintRetrievalPointers(
            &State,
            Argv[First + 2],
            StartingVcn);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--volume-bitmap") == 0)
    {
        ULONGLONG StartingLcn;
        ULONGLONG ByteCount;

        if (First + 4 != Argc ||
            !ParseUnsigned(Argv[First + 2],
                           INT64_MAX,
                           &StartingLcn) ||
            !ParseUnsigned(Argv[First + 3],
                           MAXULONG,
                           &ByteCount))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintVolumeBitmap(
            &State,
            StartingLcn,
            (ULONG)ByteCount);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--move") == 0)
    {
        PNtfsFileRecord File = NULL;
        AttributeType RequestedType = TypeData;
        PWSTR RequestedStream = NULL;
        ULONGLONG StartingVcn;
        ULONGLONG StartingLcn;
        ULONGLONG ClusterCount;
        ULONGLONG ClusterLimit = 0;

        if ((First + 6 != Argc && First + 7 != Argc) ||
            !ParseUnsigned(Argv[First + 3], INT64_MAX, &StartingVcn) ||
            !ParseUnsigned(Argv[First + 4], INT64_MAX, &StartingLcn) ||
            !ParseUnsigned(Argv[First + 5], UINT32_MAX, &ClusterCount) ||
            (First + 7 == Argc &&
             !ParseUnsigned(Argv[First + 6], INT64_MAX, &ClusterLimit)))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(Argv[First + 1], &State);
        if (NT_SUCCESS(Status))
        {
            NtfsVolumeSetAllocationLimit(State.Volume, ClusterLimit);
            Status = LookupAttribute(State.Volume,
                                     Argv[First + 2],
                                     &File,
                                     &RequestedType,
                                     &RequestedStream);
            if (NT_SUCCESS(Status) &&
                (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) &&
                !RequestedStream)
            {
                Status = NtfsFileRecordMoveClusters(File,
                                                    TypeIndexAllocation,
                                                    (PWSTR)L"$I30",
                                                    StartingVcn,
                                                    StartingLcn,
                                                    (ULONG)ClusterCount);
            }
            else if (NT_SUCCESS(Status))
            {
                Status = NtfsFileRecordMoveClusters(File,
                                                    RequestedType,
                                                    RequestedStream,
                                                    StartingVcn,
                                                    StartingLcn,
                                                    (ULONG)ClusterCount);
            }
            NtfsFileRecordDestroy(File);
            delete[] RequestedStream;
            CloseImage(&State);
        }
        printf("move status 0x%08" PRIx32 "\n", (uint32_t)Status);
        return NT_SUCCESS(Status) ? 0 : 1;
    }

    if (strcmp(Argv[First], "--shrink") == 0)
    {
        ULONGLONG NewNumberOfSectors;
        ULONGLONG Clusters;
        ULONGLONG NewClusters;
        ULONG SectorsPerCluster;
        BOOLEAN Free = FALSE;

        if (First + 3 != Argc ||
            !ParseUnsigned(Argv[First + 2], INT64_MAX, &NewNumberOfSectors) ||
            NewNumberOfSectors == 0)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            printf("shrink status 0x%08" PRIx32 "\n", (uint32_t)Status);
            return 1;
        }
        SectorsPerCluster = NtfsVolumeGetSectorsPerCluster(State.Volume);
        Clusters = NtfsVolumeGetClustersInVolume(State.Volume);
        NewClusters = (NewNumberOfSectors - 1) / SectorsPerCluster;
        if (NewClusters == 0 || NewClusters >= Clusters)
            Status = STATUS_INVALID_PARAMETER;
        else
            Status = EvacuateAbove(&State, NewClusters);
#if !NTFSLIB_EXPAND_OFFLINE
        /* Without the offline resize core, $LogFile and $MFTMirr cannot move. */
        if (NT_SUCCESS(Status))
            Status = NtfsVolumeAreClustersFree(State.Volume, NewClusters, Clusters, &Free);
        if (NT_SUCCESS(Status) && !Free)
            Status = STATUS_ALREADY_COMMITTED;
#else
        UNREFERENCED_PARAMETER(Free);
#endif
        CloseImage(&State);
        if (NT_SUCCESS(Status))
            Status = ResizeImageVolume(Argv[First + 1], NewNumberOfSectors - 1, false);
        printf("shrink status 0x%08" PRIx32 "\n", (uint32_t)Status);
        return NT_SUCCESS(Status) ? 0 : 1;
    }

    if (strcmp(Argv[First], "--grow") == 0)
    {
        ULONGLONG NewNumberOfSectors;

        if (First + 3 != Argc ||
            !ParseUnsigned(Argv[First + 2], INT64_MAX, &NewNumberOfSectors) ||
            NewNumberOfSectors < 2)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = ResizeImageVolume(Argv[First + 1], NewNumberOfSectors - 1, true);
        printf("grow status 0x%08" PRIx32 "\n", (uint32_t)Status);
        return NT_SUCCESS(Status) ? 0 : 1;
    }

#if NTFSLIB_EXPAND_OFFLINE
    if (strcmp(Argv[First], "--expand-disk") == 0)
    {
        ULONGLONG PartitionNumber = 0;

        if ((First + 2 != Argc && First + 3 != Argc) ||
            (First + 3 == Argc &&
             !ParseUnsigned(Argv[First + 2], 128, &PartitionNumber)))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        return ExpandDiskImage(Argv[First + 1], (uint32_t)PartitionNumber);
    }
#endif

    if (strcmp(Argv[First],
               "--volume-data") == 0)
    {
        if (First + 2 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintVolumeData(&State);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--mft-record") == 0)
    {
        ULONGLONG FileReference;

        if (First + 3 != Argc ||
            !ParseUnsigned(Argv[First + 2],
                           ~(ULONGLONG)0,
                           &FileReference))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DumpMftRecord(&State,
                               FileReference);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--streams") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintDataStreams(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--truncate") == 0 ||
        strcmp(Argv[First], "--allocate") == 0)
    {
        bool AllocationOnly =
            strcmp(Argv[First], "--allocate") == 0;
        ULONGLONG NewSize;
        char* End;

        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        NewSize = strtoull(Argv[First + 3],
                           &End,
                           0);
        if (errno != 0 ||
            *Argv[First + 3] == '\0' ||
            *End != '\0' ||
            NewSize > INT64_MAX)
        {
            fprintf(stderr, "invalid file size\n");
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetFileSize(&State,
                             Argv[First + 2],
                             NewSize,
                             AllocationOnly);
        CloseImage(&State);
        return Result;
    }

    {
        bool WritableMount = false;

        /* Writeback caching would report ENOSPC only at writeback; opt-in. */
        State.WritebackCache = false;
        State.DirectIo = false;
        State.NoAtime = false;
        State.StrictAtime = false;
        if (First < Argc &&
            strcmp(Argv[First], "--writable") == 0)
        {
            WritableMount = true;
            First++;
        }
        while (First < Argc && strncmp(Argv[First], "--", 2) == 0)
        {
            /* FIXME: a shared mapping's stale page can be written back over a direct write. */
            if (strcmp(Argv[First], "--direct-io") == 0)
                State.DirectIo = true;
            else if (strcmp(Argv[First], "--writeback-cache") == 0)
                State.WritebackCache = true;
            else
                break;
            First++;
        }
        if (First + 2 > Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

#if NTFSLIB_HAVE_FUSE
        Status = OpenImageWithMode(Argv[First],
                                   ShowMetadata,
                                   WritableMount,
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        State.Writable = WritableMount;

        /*
         * Remove IMAGE from the FUSE argv and retain MOUNTPOINT/options.
         * Mounts stay read-only unless --writable was given; writable mounts
         * publish every mutation in a readable order but are not
         * crash-atomic until LFS transactions exist.
         */
        {
            std::vector<char*> FuseArguments;
            static char ReadOnly[] = "-oro";
            static char Permissions[] = "-odefault_permissions";

            static std::vector<std::string> Filtered;

            Filtered.reserve((size_t)Argc);
            FuseArguments.push_back(Argv[0]);
            for (int Index = First + 1; Index < Argc; Index++)
            {
                std::string Options;
                bool Separate = strcmp(Argv[Index], "-o") == 0 && Index + 1 < Argc;
                const char* List = Separate ? Argv[Index + 1]
                                            : strncmp(Argv[Index], "-o", 2) == 0 ? Argv[Index] + 2 : NULL;

                if (!List)
                {
                    FuseArguments.push_back(Argv[Index]);
                    continue;
                }
                /* libfuse rejects mount(8)'s access-time options; the driver applies them. */
                for (const char* Cursor = List; *Cursor;)
                {
                    const char* Comma = strchr(Cursor, ',');
                    std::string Option(Cursor, Comma ? (size_t)(Comma - Cursor) : strlen(Cursor));

                    if (Option == "noatime")
                        State.NoAtime = true;
                    else if (Option == "strictatime")
                        State.StrictAtime = true;
                    else if (Option != "atime" && Option != "relatime" && Option != "norelatime" &&
                             Option != "nostrictatime" &&
                             Option != "diratime" && Option != "nodiratime" &&
                             Option != "lazytime" && Option != "nolazytime" && !Option.empty())
                        Options += (Options.empty() ? "" : ",") + Option;
                    Cursor = Comma ? Comma + 1 : Cursor + strlen(Cursor);
                }
                if (Separate)
                    Index++;
                if (!Options.empty())
                {
                    Filtered.push_back("-o" + Options);
                    FuseArguments.push_back(const_cast<char*>(Filtered.back().c_str()));
                }
            }
            if (!WritableMount)
                FuseArguments.push_back(ReadOnly);
            FuseArguments.push_back(Permissions);

            Result = MountImage((int)FuseArguments.size(),
                                FuseArguments.data(),
                                &State);
        }
        CloseImage(&State);
        return Result;
#else
        fprintf(stderr,
                "This build has no libfuse3 development headers. Install "
                "libfuse3-dev and rebuild; image inspection commands work "
                "now.\n");
        return 2;
#endif
    }
}
