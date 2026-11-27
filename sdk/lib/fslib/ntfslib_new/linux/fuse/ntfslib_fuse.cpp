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
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <inttypes.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if __has_include(<sys/sysmacros.h>)
#include <sys/sysmacros.h>
#endif
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <map>
#include <tuple>

#include <ntfslib_new.h>
#include <ntfsformat.h>
#include <ntfs_linux.h>
extern "C" {
#include <expandcore.h>
#include <wimboot.h>
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

/* One piece of a plain data stream: file bytes [Start, End) live at image byte Disk. */
struct NtfsFastExtent
{
    ULONGLONG Start;
    ULONGLONG End;
    ULONGLONG Disk;
};

/*
 * Data I/O that changes no allocation runs outside the request lock, on any thread, against this
 * snapshot. Everything else on the file first waits such I/O out and takes the snapshot down.
 */
struct NtfsFastFile
{
    PNtfsFileRecord File;
    struct NtfsFuseOpenFile* Owner;
    std::shared_mutex Lock;
    std::atomic<bool> Armed{false};
    bool ReadOk;
    bool WriteOk;
    bool SetIdChecked;
    ULONGLONG Committed;
    ULONGLONG DataSize;
    std::vector<NtfsFastExtent> Extents;
    /* Writes past the valid data length: [Committed, Frontier) is contiguous, Ahead holds the rest. */
    std::mutex PendingLock;
    ULONGLONG Frontier;
    std::vector<std::pair<ULONGLONG, ULONGLONG>> Ahead;
    std::atomic<bool> Written{false};
    std::atomic<UINT64> WriteTime{0};
};

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
    NtfsFastFile* Fast;
    unsigned DataRequests;
};

struct NtfsFuseInode
{
    ULONGLONG Reference;
    uint64_t Lookups;
    bool Unlinked;
    /* Where the last listing with attributes showed this inode: directory and position. */
    uint64_t ListDirectory;
    size_t ListIndex;
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
    size_t SpliceThreshold;
    std::unique_ptr<NtfsFileRecord, decltype(&NtfsFileRecordDestroy)> LookupRecord{nullptr, NtfsFileRecordDestroy};
    std::vector<WCHAR> LookupName;
    UINT64 LookupParent;
    UINT64 LookupGeneration;
    std::unordered_map<UINT64, decltype(LookupRecord)> ListedRecords;
    UINT64 ListingGeneration;
    std::unique_ptr<NtfsFileRecord, decltype(&NtfsFileRecordDestroy)> ParentRecord{nullptr, NtfsFileRecordDestroy};
    UINT64 ParentGeneration;
    bool ParentFromLookup;
#endif
    /* Inodes the kernel holds, keyed by inode number, with lookup counts. */
    std::unordered_map<uint64_t, NtfsFuseInode> Inodes;
    /* One record per open file, shared by all its handles. */
    std::unordered_map<uint64_t, NtfsFuseOpenFile> OpenFiles;
    /* Values are shared with the scans below so a cache invalidation
     * cannot free a listing a reader still walks.
     */
    std::unordered_map<uint64_t, std::shared_ptr<NtfsDirectoryListing>> DirectoryCache;
    /*
     * Directories are read without an open: a scan is tied to the listing it started on by a
     * number in the upper bits of its offsets. The last few listings of a directory are kept.
     */
    std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, std::shared_ptr<NtfsDirectoryListing>>>> DirectoryScans;
    bool OpenlessDirectories;
    std::vector<UCHAR> ReadBuffer;
    /* Lazy access times of closed files, by file reference, as NT keeps them. */
    std::unordered_map<uint64_t, UINT64> PendingAccess;
    std::mutex RequestLock;
    /* Helper threads drain the request queue when the kernel has several requests waiting. */
    std::vector<std::thread> Workers;
    std::mutex WorkerLock;
    std::condition_variable WorkerWake;
    unsigned WorkerTokens;
    std::atomic<unsigned> WorkersSleeping{0};
    std::atomic<unsigned> WorkersDone{0};
    std::atomic<bool> WorkerStop{false};
    bool RingTransport;
    /*
     * A reader walking a directory opens its files in listing order: the data of the small ones
     * ahead of it is pushed into the kernel's page cache, so each costs an open and no read.
     */
    std::thread PrefetchThread;
    std::mutex PrefetchLock;
    std::condition_variable PrefetchWake;
    std::shared_ptr<NtfsDirectoryListing> PrefetchListing;
    size_t PrefetchFrom;
    size_t PrefetchTo;
    uint64_t PrefetchJobGeneration;
    bool PrefetchPending;
    /* Held shared while pushed data is on its way; taken exclusive by whatever may free clusters. */
    std::shared_mutex PrefetchGate;
    std::atomic<uint64_t> PrefetchGeneration{0};
    const NtfsDirectoryListing* PrefetchStream;
    size_t PrefetchNext;
    size_t PrefetchBatch;
    std::mutex StoredLock;
    std::unordered_set<uint64_t> Stored;
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
        case STATUS_SHARING_VIOLATION:
            return EBUSY;
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
    Output.reserve(strlen(Input) + 1);

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
    const UCHAR* Data;
    ULONG Length = 0;
    ULONG Offset = 0;
    NtfsExtendedAttributeView View;
    PAttribute Ea;

    memset(Metadata, 0, sizeof(*Metadata));
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
        if (NtfsFileRecordReadExtendedAttributes(File, NULL, &Length, NULL) != STATUS_BUFFER_TOO_SMALL || Length == 0)
            return;
        Raw.resize(Length);
        if (!NT_SUCCESS(NtfsFileRecordReadExtendedAttributes(File, Raw.data(), &Length, NULL)))
            return;
        Data = Raw.data();
    }
    while (NT_SUCCESS(NtfsGetNextExtendedAttribute(Data, Length, &Offset, &View)))
    {
        UINT32 Value;

        if (View.ValueLength != sizeof(Value) || View.NameLength != 6)
            continue;
        memcpy(&Value, View.Value, sizeof(Value));
        if (strncasecmp((const char*)View.Name, "$LXUID", 6) == 0)
            Metadata->HasUid = true, Metadata->Uid = Value;
        else if (strncasecmp((const char*)View.Name, "$LXGID", 6) == 0)
            Metadata->HasGid = true, Metadata->Gid = Value;
        else if (strncasecmp((const char*)View.Name, "$LXMOD", 6) == 0)
            Metadata->HasMode = true, Metadata->Mode = Value;
        else if (strncasecmp((const char*)View.Name, "$LXDEV", 6) == 0)
            Metadata->HasDev = true, Metadata->Dev = Value;
    }
}

static ULONG
BuildPosixUpdates(NtfsExtendedAttributeUpdate (&Updates)[4],
                  const UINT32* Uid,
                  const UINT32* Gid,
                  const UINT32* Mode,
                  const UINT32* Dev)
{
    const UINT32* Values[] = {Uid, Gid, Mode, Dev};
    ULONG Count = 0;

    for (ULONG Index = 0; Index < 4; Index++)
    {
        if (!Values[Index])
            continue;
        Updates[Count] = {};
        Updates[Count].Operation = NTFS_EA_UPDATE_SET;
        Updates[Count].NameLength = 6;
        Updates[Count].ValueLength = sizeof(UINT32);
        Updates[Count].Name = (const UCHAR*)NtfsPosixEaNames[Index];
        Updates[Count].Value = (const UCHAR*)Values[Index];
        Count++;
    }
    return Count;
}

static NTSTATUS
WritePosixMetadata(PNtfsFileRecord File,
                   const UINT32* Uid,
                   const UINT32* Gid,
                   const UINT32* Mode,
                   const UINT32* Dev)
{
    NtfsExtendedAttributeUpdate Updates[4];
    ULONG Count = BuildPosixUpdates(Updates, Uid, Gid, Mode, Dev);

    return Count ? NtfsFileRecordUpdateExtendedAttributes(File, Updates, Count) : STATUS_SUCCESS;
}

static NTSTATUS
FillStatFromRecord(PNtfsFileRecord File,
                   const NtfsFuseState* State,
                   struct stat* Stat)
{
    PFileRecordHeader Header = NtfsFileRecordGetHeader(File);
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
        PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);

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
#if NTFSLIB_HAVE_FUSE
    State->ListedRecords.clear();
    State->LookupRecord.reset();
    State->ParentRecord.reset();
#endif
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
    bool Component;
    std::string Path;
    std::vector<UCHAR> Descriptor;
};

struct SecurityPolicy
{
    std::vector<SecurityRule> Rules;
    std::unordered_set<std::string> Components;
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
LoadComponentList(const char* PolicyPath,
                  const std::string& Name,
                  std::unordered_set<std::string>& Components)
{
    std::string ListPath = Name;
    const char* Slash = strrchr(PolicyPath, '/');
    FILE* List;
    char* Line = NULL;
    size_t Capacity = 0;
    ssize_t Length;
    bool Result;

    if (Slash && !ListPath.empty() && ListPath.front() != '/')
        ListPath.insert(0, PolicyPath, (size_t)(Slash - PolicyPath) + 1);
    List = fopen(ListPath.c_str(), "rb");
    if (!List)
    {
        fprintf(stderr, "%s: %s\n", ListPath.c_str(), strerror(errno));
        return false;
    }
    while ((Length = getline(&Line, &Capacity, List)) >= 0)
    {
        while (Length > 0 &&
               (Line[Length - 1] == '\n' ||
                Line[Length - 1] == '\r' ||
                Line[Length - 1] == ' '))
        {
            Line[--Length] = '\0';
        }
        if (Length != 0)
            Components.insert(SecurityRulePath(Line));
    }
    Result = feof(List) != 0;
    free(Line);
    fclose(List);
    return Result;
}

static bool
LoadSecurityRules(const char* PolicyPath, SecurityPolicy& Policy)
{
    std::unordered_map<std::string, std::string> Definitions;
    FILE* PolicyFile = fopen(PolicyPath, "rb");
    char* Line = NULL;
    size_t Capacity = 0;
    unsigned long LineNumber = 0;
    bool Result = false;

    if (!PolicyFile)
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

        Length = getline(&Line, &Capacity, PolicyFile);
        if (Length < 0)
        {
            Result = feof(PolicyFile) != 0;
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
        if (strncmp(Line, "componentlist ", 14) == 0)
        {
            if (!LoadComponentList(PolicyPath, Line + 14, Policy.Components))
                break;
            continue;
        }

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
        else if (Kind == "file" || Kind == "files" || Kind == "components")
            Rule.Directory = false;
        else
            break;
        Rule.Recursive = Kind.back() == 's';
        Rule.Component = Kind == "components";
        Rule.Path = SecurityRulePath(Name);
        if (!ParseSddl(Value, Rule.Descriptor))
            break;
        Policy.Rules.push_back(std::move(Rule));
    }

    if (!Result)
    {
        fprintf(stderr,
                "%s:%lu: invalid security rule\n",
                PolicyPath,
                LineNumber);
    }
    free(Line);
    fclose(PolicyFile);
    return Result;
}

static const SecurityRule*
FindSecurityRule(const SecurityPolicy& Policy,
                 const std::string& Path,
                 bool Directory)
{
    const SecurityRule* Best = NULL;

    for (const SecurityRule& Rule : Policy.Rules)
    {
        if (Rule.Directory != Directory)
            continue;
        assert(!Rule.Component || (Rule.Recursive && !Rule.Directory));
        if (Rule.Component && Policy.Components.count(Path) == 0)
            continue;
        if (!Rule.Recursive)
        {
            if (Rule.Path == Path)
                Best = &Rule;
            continue;
        }
        if (Best &&
            (!Best->Recursive ||
             Best->Path.size() > Rule.Path.size() ||
             (Best->Path.size() == Rule.Path.size() && Best->Component)))
        {
            continue;
        }
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
                   const SecurityPolicy& Policy,
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
        Rule = FindSecurityRule(Policy, SecurityRulePath(Path), IsDirectory);
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
        Status = ApplySecurityRules(State, Policy, Child.first, Child.second);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static int
ApplySecurityFromPolicy(NtfsFuseState* State, const char* PolicyPath)
{
    SecurityPolicy Policy;

    if (!LoadSecurityRules(PolicyPath, Policy))
        return 1;
    return NT_SUCCESS(ApplySecurityRules(State, Policy, std::string(), true))
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

#ifndef FILE_ATTRIBUTE_READONLY
#define FILE_ATTRIBUTE_READONLY 0x00000001
#endif
#ifndef FILE_ATTRIBUTE_HIDDEN
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#endif
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

struct WimPointerState
{
    NtfsFuseState* State;
    const WIMBOOT_IMAGE* Image;
    NTSTATUS Status;
    std::string FailedPath;
};

static void*
WimAllocate(size_t Size, void* Context)
{
    (void)Context;
    return malloc(Size);
}

static void
WimFree(void* Pointer, void* Context)
{
    (void)Context;
    free(Pointer);
}

static int
AddWimEntry(void* Context, const WIMBOOT_ENTRY* Entry)
{
    WimPointerState* Pointers = static_cast<WimPointerState*>(Context);
    UCHAR Reparse[WIMBOOT_POINTER_SIZE];
    std::vector<WCHAR> NtfsPath;

    if (Entry->IsDirectory)
    {
        Pointers->Status = EnsureImageDirectory(Pointers->State, Entry->Path, FALSE);
    }
    else if (!Utf8ToNtfsPath(Entry->Path, NtfsPath) || NtfsPath.size() < 2)
    {
        Pointers->Status = STATUS_OBJECT_NAME_INVALID;
    }
    else if (Entry->Size == 0)
    {
        PNtfsFileRecord File = NULL;

        Pointers->Status = NtfsMasterFileTableCreateFile(
            NtfsVolumeGetMft(Pointers->State->Volume),
            NtfsPath.data(),
            (ULONG)(NtfsPath.size() - 1),
            FALSE,
            Entry->Attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM),
            &File);
        NtfsFileRecordDestroy(File);
    }
    else
    {
        WimBootPointer(Reparse, Pointers->Image, Entry);
        Pointers->Status = NtfsMasterFileTableCreateWofFile(
            NtfsVolumeGetMft(Pointers->State->Volume),
            NtfsPath.data(),
            (ULONG)(NtfsPath.size() - 1),
            Entry->Attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM),
            Entry->Size,
            Reparse,
            sizeof(Reparse));
    }

    if (!NT_SUCCESS(Pointers->Status))
    {
        Pointers->FailedPath = Entry->Path;
        return 1;
    }
    return 0;
}

static int
AddWimImage(NtfsFuseState* State,
            const char* WimPath,
            const char* ImagePath,
            uint64_t PartitionOffset,
            uint32_t DiskSignature)
{
    static const char OverlayDirectory[] = "System Volume Information";
    static const char OverlayPath[] = "System Volume Information/WimOverlay.dat";
    WimCoreAllocator Allocator = { WimAllocate, WimFree, NULL };
    std::vector<UCHAR> CopyBuffer(1024 * 1024);
    std::string WimImagePath = NormalizeImagePath(ImagePath);
    std::string Overlay(4096, '\0');
    std::vector<WCHAR> OverlayName;
    std::vector<char> Data;
    WimPointerState Pointers;
    PNtfsFileRecord File = NULL;
    WIMBOOT_IMAGE Image = {};
    WimCoreCtx Wim;
    uint32_t OverlayLength = 0;
    size_t Separator;
    NTSTATUS Status;
    FILE* Host;
    long Size;
    int Result;

    Status = Lookup(State->Volume, OverlayPath, &File);
    NtfsFileRecordDestroy(File);
    if (Status != STATUS_NOT_FOUND)
    {
        /* TODO: add a WIM to a volume that already has one registered */
        fprintf(stderr, "%s: the volume already has a WIM overlay\n", OverlayPath);
        return 1;
    }

    Host = fopen(WimPath, "rb");
    if (!Host || fseek(Host, 0, SEEK_END) != 0 || (Size = ftell(Host)) <= 0 || fseek(Host, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "%s: %s\n", WimPath, strerror(errno));
        if (Host)
            fclose(Host);
        return 1;
    }
    Data.resize((size_t)Size);
    Result = fread(Data.data(), 1, Data.size(), Host) == Data.size() ? WIMBOOT_OK : WIMBOOT_IO_ERROR;
    fclose(Host);

    wimcore_init(&Wim);
    if (Result == WIMBOOT_OK)
        Result = wimcore_open_memory(&Wim, &Allocator, Data.data(), Data.size()) == 0 ? WIMBOOT_OK : WIMBOOT_BAD_WIM;
    if (Result == WIMBOOT_OK)
        Result = WimBootOpen(&Wim, &Image);
    if (Result == WIMBOOT_OK && !WimImagePath.empty() &&
        Utf8ToNtfsString(("\\" + WimImagePath).c_str(), false, false, OverlayName))
    {
        for (WCHAR& Character : OverlayName)
        {
            if (Character == L'/')
                Character = L'\\';
        }
        OverlayLength = WimBootOverlay(reinterpret_cast<uint8_t*>(&Overlay[0]),
                                       (uint32_t)Overlay.size(),
                                       &Image,
                                       reinterpret_cast<const uint16_t*>(OverlayName.data()),
                                       PartitionOffset,
                                       DiskSignature);
    }
    if (Result != WIMBOOT_OK || OverlayLength == 0)
    {
        fprintf(stderr, "%s: the WIM image cannot be used (%d)\n", WimPath, Result);
        wimcore_close(&Wim);
        return 1;
    }
    Overlay.resize(OverlayLength);

    Separator = WimImagePath.rfind('/');
    Status = Separator == std::string::npos
        ? STATUS_SUCCESS
        : EnsureImageDirectory(State, WimImagePath.substr(0, Separator), FALSE);
    if (NT_SUCCESS(Status))
        Status = PrepareImageFile(State, WimImagePath);
    if (NT_SUCCESS(Status))
        Status = CopyHostFileToImage(State, WimImagePath, WimPath, CopyBuffer);
    if (NT_SUCCESS(Status))
        Status = EnsureImageDirectory(State, OverlayDirectory, FALSE);
    if (NT_SUCCESS(Status))
        Status = PrepareImageFile(State, OverlayPath);
    if (NT_SUCCESS(Status))
        Status = WriteImageBytes(State, OverlayPath, Overlay);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr, "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                WimImagePath.c_str(), (uint32_t)Status, strerror(NtStatusToErrno(Status)));
        wimcore_close(&Wim);
        return 1;
    }

    Pointers.State = State;
    Pointers.Image = &Image;
    Pointers.Status = STATUS_SUCCESS;
    Result = WimBootWalk(&Wim, &Image, AddWimEntry, &Pointers);
    wimcore_close(&Wim);
    if (Result != WIMBOOT_OK)
    {
        if (!NT_SUCCESS(Pointers.Status))
        {
            fprintf(stderr, "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Pointers.FailedPath.c_str(), (uint32_t)Pointers.Status, strerror(NtStatusToErrno(Pointers.Status)));
        }
        else
        {
            fprintf(stderr, "%s: the WIM image cannot be used (%d)\n", WimPath, Result);
        }
        return 1;
    }
    return 0;
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

template<auto Handler, typename... Args>
static void
DispatchRequest(fuse_req_t Request, Args... Arguments)
{
    int OldCancelState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &OldCancelState);
    {
        std::lock_guard<std::mutex> Guard(RequestState(Request)->RequestLock);
        Handler(Request, Arguments...);
    }
    pthread_setcancelstate(OldCancelState, NULL);
}

static void
WakeWorker(NtfsFuseState* State);

/*
 * For requests that may free clusters or let a file's cached pages turn dirty: pushed data still on
 * its way would land on the wrong bytes. The request lock is dropped while waiting, so that a read
 * the push itself waits on can be answered.
 */
template<auto Handler, typename... Args>
static void
DispatchMutation(fuse_req_t Request, Args... Arguments)
{
    NtfsFuseState* State = RequestState(Request);
    int OldCancelState;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &OldCancelState);
    for (;;)
    {
        std::unique_lock<std::mutex> Guard(State->RequestLock);

        State->PrefetchGeneration++;
        if (!State->PrefetchThread.joinable() || State->PrefetchGate.try_lock())
        {
            if (State->PrefetchThread.joinable())
                State->PrefetchGate.unlock();
            Handler(Request, Arguments...);
            break;
        }
        Guard.unlock();
        WakeWorker(State);
        std::unique_lock<std::shared_mutex> Wait(State->PrefetchGate);
    }
    pthread_setcancelstate(OldCancelState, NULL);
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
    UINT64 Ticks = (UINT64)(Time->tv_sec + EpochSeconds) * UINT64_C(10000000) +
                   (UINT64)Time->tv_nsec / 100;
    return Ticks > INT64_MAX ? INT64_MAX : Ticks;
}

/* A stream that cannot hold holes, neither sparse nor compressed, is never larger than its volume. */
static bool
ExceedsVolume(NtfsFuseState* State, PNtfsFileRecord File, ULONGLONG Size)
{
    PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    NtfsFileBasicInformation Basic = {};
    NtfsVolumeInformation Information;

    if ((Data && (Data->Flags & (ATTR_SPARSE | ATTR_COMPRESSION_MASK))) ||
        (NT_SUCCESS(NtfsFileRecordGetBasicInformation(File, &Basic)) &&
         (Basic.FileAttributes & (FILE_PERM_SPARSE | FILE_PERM_COMPRESSED))))
    {
        return false;
    }
    return NT_SUCCESS(NtfsVolumeQueryInformation(State->Volume, &Information)) &&
           Size > Information.TotalClusters * (ULONGLONG)Information.BytesPerCluster;
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

static ULONG
ClusterBytes(NtfsFuseState* State);

static const ULONGLONG NtfsFastWindow = 32u * 1024u * 1024u;

/* Zeroes file bytes [From, To) of a plain stream on the image; the caller holds the request lock. */
static NTSTATUS
ZeroDataRange(NtfsFuseState* State, PNtfsFileRecord File, ULONGLONG From, ULONGLONG To)
{
    static const UCHAR Zeroes[64 * 1024] = {};
    ULONG Cluster = ClusterBytes(State);

    while (From < To)
    {
        NtfsRetrievalExtent Extent;
        ULONGLONG Start;
        ULONG Count = 1;
        NTSTATUS Status = NtfsFileRecordQueryRetrievalPointers(File, TypeData, NULL, From / Cluster, &Start, &Extent, &Count);

        if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) || Count != 1 || Extent.Lcn < 0 ||
            Extent.NextVcn <= From / Cluster)
        {
            return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
        }
        ULONGLONG End = Extent.NextVcn * Cluster < To ? Extent.NextVcn * Cluster : To;
        ULONGLONG Disk = ((ULONGLONG)Extent.Lcn + (From / Cluster - Start)) * Cluster + From % Cluster;

        while (From < End)
        {
            ULONG Chunk = End - From > sizeof(Zeroes) ? (ULONG)sizeof(Zeroes) : (ULONG)(End - From);

            Status = NtfsWriteFileDataLinux(Disk, Zeroes, Chunk);
            if (!NT_SUCCESS(Status))
                return Status;
            From += Chunk;
            Disk += Chunk;
        }
    }
    return STATUS_SUCCESS;
}

/*
 * Waits out the data I/O running outside the request lock, stores the sizes its writes reached and
 * takes the snapshot down. Every path that uses an open file under the request lock comes through here.
 */
static void
SettleFast(NtfsFuseState* State, NtfsFuseOpenFile* Open)
{
    NtfsFastFile* Fast = Open->Fast;

    if (!Fast || !Fast->Armed.load(std::memory_order_acquire))
        return;
    Fast->Armed.store(false, std::memory_order_release);
    {
        std::unique_lock<std::shared_mutex> Guard(Fast->Lock);
    }
    if (Fast->Written.exchange(false))
        Open->Written = true;
    UINT64 Time = Fast->WriteTime.exchange(0);
    if (Time > Open->PendingWriteTime)
        Open->PendingWriteTime = Time;

    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG End = Fast->Frontier;

    for (const auto& Range : Fast->Ahead)
    {
        /* Nothing wrote the bytes below this range: they are a hole and read as zero. */
        if (Range.first > End && NT_SUCCESS(Status))
        {
            Status = ZeroDataRange(State, Fast->File, End, Range.first);
        }
        if (Range.second > End)
            End = Range.second;
    }
    Fast->Ahead.clear();
    if (End > Fast->Committed && NT_SUCCESS(Status))
    {
        UINT32 Mask = NtfsFileRecordGetAutomaticTimestampMask(Fast->File);

        (void)NtfsFileRecordSetAutomaticTimestampMask(Fast->File, 0);
        if (End > Fast->DataSize)
        {
            PAttribute Data = NtfsFileRecordGetAttribute(Fast->File, TypeData, NULL);

            /* The extents handed out stay allocated: the size moves inside them. */
            Status = NtfsFileRecordSetWriteSize(Fast->File, TypeData, NULL, End,
                                                Data && Data->IsNonResident ? Data->NonResident.AllocatedSize : 0);
        }
        if (NT_SUCCESS(Status))
            Status = NtfsFileRecordSetFileValidDataLength(Fast->File, TypeData, NULL, End);
        (void)NtfsFileRecordSetAutomaticTimestampMask(Fast->File, Mask);
    }
    if (!NT_SUCCESS(Status))
        fprintf(stderr, "ntfslib: storing the written length failed 0x%08" PRIx32 "\n", (uint32_t)Status);
    Fast->Extents.clear();
}

/* Snapshots the extents around Offset once a second data request shows the file is being streamed. */
static void
ArmFast(NtfsFuseState* State, NtfsFuseOpenFile* Open, ULONGLONG Offset)
{
    NtfsFastFile* Fast = Open->Fast;
    NtfsRetrievalExtent Raw[32];
    ULONG Count = 32;
    ULONG Cluster = ClusterBytes(State);
    ULONGLONG Clusters = NtfsVolumeGetClustersInVolume(State->Volume);
    ULONGLONG Vcn;

    if (!Fast || ++Open->DataRequests < 2 || Fast->Armed.load(std::memory_order_relaxed))
        return;
    PAttribute Data = NtfsFileRecordGetAttribute(Open->File, TypeData, NULL);
    if (!Data || !Data->IsNonResident || Data->Flags || !Data->NonResident.AllocatedSize ||
        NtfsFileRecordGetAttribute(Open->File, TypeReparsePoint, NULL))
    {
        return;
    }
    if (Offset >= Data->NonResident.AllocatedSize)
        Offset = Data->NonResident.AllocatedSize - 1;
    NTSTATUS Status = NtfsFileRecordQueryRetrievalPointers(Open->File, TypeData, NULL, Offset / Cluster, &Vcn, Raw, &Count);
    if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) || !Count)
        return;
    Fast->Extents.clear();
    for (ULONG Index = 0; Index < Count; Index++)
    {
        ULONGLONG Lcn = (ULONGLONG)Raw[Index].Lcn;

        if (Raw[Index].Lcn < 0 || Raw[Index].NextVcn <= Vcn || Lcn >= Clusters ||
            Raw[Index].NextVcn - Vcn > Clusters - Lcn || Raw[Index].NextVcn > (ULONGLONG)INT64_MAX / Cluster)
        {
            break;
        }
        Fast->Extents.push_back({Vcn * Cluster, Raw[Index].NextVcn * Cluster, Lcn * Cluster});
        Vcn = Raw[Index].NextVcn;
    }
    if (Fast->Extents.empty())
        return;
    Fast->Committed = Data->NonResident.InitalizedDataSize;
    Fast->DataSize = Data->NonResident.DataSize;
    Fast->Frontier = Fast->Committed;
    Fast->ReadOk = !State->Writable || State->NoAtime || Open->AccessChecked;
    Fast->WriteOk = State->Writable;
    Fast->SetIdChecked = Open->SetIdChecked;
    Fast->Armed.store(true, std::memory_order_release);
}

static const ULONGLONG NtfsPrefetchFileLimit = 128u * 1024u;

static bool
IsStored(NtfsFuseState* State, fuse_ino_t Ino)
{
    std::lock_guard<std::mutex> Guard(State->StoredLock);

    return State->Stored.count(Ino) != 0;
}

static void
SetStored(NtfsFuseState* State, fuse_ino_t Ino, bool Stored)
{
    std::lock_guard<std::mutex> Guard(State->StoredLock);

    if (Stored)
        State->Stored.insert(Ino);
    else
        State->Stored.erase(Ino);
}

static NtfsFuseOpenFile*
FindOpen(NtfsFuseState* State, fuse_ino_t Ino)
{
    auto Open = State->OpenFiles.find(Ino);

    if (Open == State->OpenFiles.end())
        return NULL;
    SettleFast(State, &Open->second);
    return &Open->second;
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
        SettleFast(State, &Open->second);
        *File = Open->second.File;
        return STATUS_SUCCESS;
    }
    *Owned = true;
    if (State->ParentFromLookup && State->ParentRecord && Inode != State->Inodes.end() &&
        Inode->second.Reference == NtfsFileRecordGetReference(State->ParentRecord.get()) &&
        State->ParentGeneration == NtfsDiskWriteGenerationLinux())
    {
        *File = State->ParentRecord.release();
        return STATUS_SUCCESS;
    }
    if (State->LookupRecord && Inode != State->Inodes.end() &&
        Inode->second.Reference == NtfsFileRecordGetReference(State->LookupRecord.get()) &&
        State->LookupGeneration == NtfsDiskWriteGenerationLinux())
    {
        *File = State->LookupRecord.release();
        return STATUS_SUCCESS;
    }
    if (!State->ListedRecords.empty() && Inode != State->Inodes.end())
    {
        if (State->ListingGeneration != NtfsDiskWriteGenerationLinux())
            State->ListedRecords.clear();
        else
        {
            auto Record = State->ListedRecords.find(Inode->second.Reference);
            if (Record != State->ListedRecords.end())
            {
                *File = Record->second.release();
                State->ListedRecords.erase(Record);
                return STATUS_SUCCESS;
            }
        }
    }
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
    if (State->LookupRecord && State->LookupParent == NtfsFileRecordGetReference(Parent) &&
        Name == State->LookupName && State->LookupGeneration == NtfsDiskWriteGenerationLinux())
    {
        *Child = State->LookupRecord.release();
        return STATUS_SUCCESS;
    }
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
    SettleFast(State, &Open->second);
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
        auto Pending = State->PendingAccess.find(NtfsFileRecordGetReference(File));
        UINT64 Accessed = Pending == State->PendingAccess.end() ? 0 : Pending->second;
        auto Inode = State->Inodes.find(Ino);
        bool Unlinked = Inode != State->Inodes.end() && Inode->second.Unlinked;
        auto Added = State->OpenFiles.emplace(Ino, NtfsFuseOpenFile{File, 1u, false, false, Unlinked, 0, Accessed, false, false, NULL, 0});
        Added.first->second.Fast = new NtfsFastFile;
        Added.first->second.Fast->File = File;
        Added.first->second.Fast->Owner = &Added.first->second;
        if (Pending != State->PendingAccess.end())
            State->PendingAccess.erase(Pending);
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
DeleteUnlinkedRecord(NtfsFuseState* State, ULONGLONG Reference)
{
    NTSTATUS Status = NtfsMasterFileTableDeleteUnlinkedFile(StateMft(State), Reference, NULL);

    if (!NT_SUCCESS(Status))
        fprintf(stderr, "ntfslib: deleting an unlinked file failed 0x%08" PRIx32 "\n", (uint32_t)Status);
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

    SettleFast(State, &Existing->second);
    File = Existing->second.File;
    if (!Existing->second.Unlinked &&
        Existing->second.PendingWriteTime == 0 &&
        Existing->second.PendingAccessTime != 0)
    {
        State->PendingAccess[NtfsFileRecordGetReference(File)] = Existing->second.PendingAccessTime;
        Existing->second.PendingAccessTime = 0;
        if (State->PendingAccess.size() > 32768)
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
        (void)NtfsFileRecordDeferFileNameUpdates(File, FALSE);
        if (State->Inodes.find(Ino) == State->Inodes.end())
            DeleteUnlinkedRecord(State, NtfsFileRecordGetReference(File));
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
    delete Existing->second.Fast;
    State->OpenFiles.erase(Existing);
    NtfsFileRecordDestroy(File);
}

static void
NtfsLowInit(void* UserData, struct fuse_conn_info* Connection)
{
    NtfsFuseState* State = (NtfsFuseState*)UserData;

    Connection->time_gran = 100;
    Connection->want &= ~FUSE_CAP_ASYNC_DIO;
    if (Connection->max_background < 64)
    {
        Connection->max_background = 64;
        Connection->congestion_threshold = 48;
    }
#ifdef FUSE_CAP_OVER_IO_URING
    State->RingTransport = fuse_get_feature_flag(Connection, FUSE_CAP_OVER_IO_URING);
#endif
    long PageSize = sysconf(_SC_PAGESIZE);
    if (PageSize > 0 && (Connection->capable & FUSE_CAP_SPLICE_WRITE))
    {
        Connection->want |= FUSE_CAP_SPLICE_WRITE | (Connection->capable & FUSE_CAP_SPLICE_MOVE);
        State->SpliceThreshold = 2 * (size_t)PageSize;
    }
    if (Connection->capable & FUSE_CAP_NO_OPENDIR_SUPPORT)
    {
        Connection->want |= FUSE_CAP_NO_OPENDIR_SUPPORT;
        State->OpenlessDirectories = true;
    }
    if (Connection->capable & FUSE_CAP_READDIRPLUS)
        Connection->want |= FUSE_CAP_READDIRPLUS;
    Connection->want &= ~FUSE_CAP_READDIRPLUS_AUTO;
    if (Connection->capable & FUSE_CAP_CACHE_SYMLINKS)
        Connection->want |= FUSE_CAP_CACHE_SYMLINKS;
    if (Connection->capable & FUSE_CAP_EXPORT_SUPPORT)
        Connection->want |= FUSE_CAP_EXPORT_SUPPORT;
    /* With v2 the kernel stops asking for security.capability before every write; no security.* xattr is stored. */
    State->WritebackCache = State->Writable && !State->DirectIo && State->WritebackCache &&
                            (Connection->capable & FUSE_CAP_WRITEBACK_CACHE);
    NtfsUseWritebackClockLinux(State->WritebackCache);
    if (State->Writable && (Connection->capable & FUSE_CAP_HANDLE_KILLPRIV_V2))
        Connection->want |= FUSE_CAP_HANDLE_KILLPRIV_V2;
    else if (State->WritebackCache)
        Connection->want &= ~FUSE_CAP_HANDLE_KILLPRIV;
    else if (State->Writable && (Connection->capable & FUSE_CAP_HANDLE_KILLPRIV))
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
    if (State->WritebackCache)
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
    {
        Status = LookupChild(State, Parent, NtfsName, &Child);
        if (NT_SUCCESS(Status))
        {
            State->LookupRecord.reset(Child);
            State->LookupParent = NtfsFileRecordGetReference(Parent);
            State->LookupName = std::move(NtfsName);
            State->LookupGeneration = NtfsDiskWriteGenerationLinux();
        }
    }
    if (Owned && (NT_SUCCESS(Status) || Status == STATUS_OBJECT_NAME_NOT_FOUND) && Parent)
    {
        State->ParentRecord.reset(Parent);
        State->ParentFromLookup = true;
        State->ParentGeneration = NtfsDiskWriteGenerationLinux();
    }
    else
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

        if (Open != State->OpenFiles.end())
            SettleFast(State, &Open->second);
        Status = RememberEntry(State,
                               Open != State->OpenFiles.end() ? Open->second.File : Child,
                               &Entry);
    }
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
        if (Inode->second.Unlinked && State->OpenFiles.find(Ino) == State->OpenFiles.end())
            DeleteUnlinkedRecord(State, Inode->second.Reference);
        State->Inodes.erase(Inode);
        State->DirectoryScans.erase(Ino);
        SetStored(State, Ino, false);
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
    if (State->ParentRecord && Ino == InoFromReference(NtfsFileRecordGetReference(State->ParentRecord.get())) &&
        State->ParentGeneration == NtfsDiskWriteGenerationLinux())
    {
        File = State->ParentRecord.get();
        Owned = false;
        Status = STATUS_SUCCESS;
    }
    else
        Status = LoadInode(State, Ino, &File, &Owned);
    if (NT_SUCCESS(Status))
        Status = FillInodeStat(State, Ino, File, &Stat);
    ReleaseLoaded(File, Owned);
    if (!NT_SUCCESS(Status))
    {
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
    {
        File = ((NtfsFastFile*)(uintptr_t)FileInfo->fh)->File;
        SettleFast(State, ((NtfsFastFile*)(uintptr_t)FileInfo->fh)->Owner);
    }
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
    if (!IsDirectory && State->WritebackCache && (ToSet & FUSE_SET_ATTR_MTIME) && !(ToSet & FUSE_SET_ATTR_MTIME_NOW))
    {
        const struct timespec& Input = Attributes->st_mtim;
        struct timespec Stored = NtfsTimeToTimespec(TimespecToNtfsTime(&Input));
        if (Stored.tv_sec != Input.tv_sec || Stored.tv_nsec != Input.tv_nsec / 100 * 100)
        {
            Status = FillInodeStat(State, Ino, File, &Stat);
            if (!NT_SUCCESS(Status) || S_ISREG(Stat.st_mode))
            {
                ReleaseLoaded(File, Owned);
                fuse_reply_err(Request, NT_SUCCESS(Status) ? EOVERFLOW : NtStatusToErrno(Status));
                return;
            }
        }
    }
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
        else if (ExceedsVolume(State, File, (ULONGLONG)Attributes->st_size))
        {
            ReleaseLoaded(File, Owned);
            fuse_reply_err(Request, EFBIG);
            return;
        }
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
            const NtfsFileBasicInformation Original = Information;
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
                if (Information.LastAccessTime != Original.LastAccessTime ||
                    Information.LastWriteTime != Original.LastWriteTime ||
                    Information.ChangeTime != Original.ChangeTime ||
                    Information.FileAttributes != Original.FileAttributes)
                {
                    Status = NtfsFileRecordSetBasicInformation(File, &Information);
                }
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

static void
ReleaseChangedParent(NtfsFuseState* State, PNtfsFileRecord Parent, bool Owned, NTSTATUS Status)
{
    if (Owned && NT_SUCCESS(Status))
    {
        State->ParentRecord.reset(Parent);
        State->ParentFromLookup = false;
        State->ParentGeneration = NtfsDiskWriteGenerationLinux();
    }
    else
        ReleaseLoaded(Parent, Owned);
}

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

    const struct fuse_ctx* Context = fuse_req_ctx(Request);
    UINT32 Uid = Context->uid;
    UINT32 Gid = Context->gid;
    UINT32 FullMode = Mode;
    UINT32 Dev = (UINT32)((major(Device) << 20) | (minor(Device) & 0xFFFFF));
    NtfsExtendedAttributeUpdate Updates[4];

    /* A set-group-ID directory passes its group on, and the bit to subdirectories. */
    if (ParentMetadata.HasMode && (ParentMetadata.Mode & S_ISGID))
    {
        Gid = ParentMetadata.HasGid ? ParentMetadata.Gid : State->Group;
        if (IsDirectory)
            FullMode |= S_ISGID;
    }
    ULONG Count = BuildPosixUpdates(Updates, &Uid, &Gid, &FullMode,
                                    S_ISCHR(Mode) || S_ISBLK(Mode) ? &Dev : NULL);

    /* The kernel looked the name up first; the index insertion still refuses a duplicate. */
    Status = NtfsMasterFileTableCreateFileWithEas(StateMft(State),
                                                  Parent,
                                                  NtfsName.data(),
                                                  (ULONG)(NtfsName.size() - 1),
                                                  IsDirectory,
                                                  0,
                                                  TRUE,
                                                  Updates,
                                                  Count,
                                                  Created);
    ReleaseChangedParent(State, Parent, Owned, Status);
    if (NT_SUCCESS(Status))
        DropListing(State, ParentIno);
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
        if (Open != State->OpenFiles.end())
            SettleFast(State, &Open->second);
        PNtfsFileRecord Canonical = Open != State->OpenFiles.end() ? Open->second.File : NULL;
        auto Inode = State->Inodes.find(ChildIno);
        bool KeepRecord = Canonical || Inode != State->Inodes.end();

        Status = NtfsMasterFileTableDeleteChild(StateMft(State),
                                                Parent,
                                                Child,
                                                NtfsName.data(),
                                                (ULONG)(NtfsName.size() - 1),
                                                IsDirectory,
                                                Canonical,
                                                !KeepRecord,
                                                KeepRecord,
                                                &Deleted);
        if (NT_SUCCESS(Status))
        {
            DropListing(State, ParentIno);
            if (IsDirectory)
            {
                DropListing(State, ChildIno);
                fuse_lowlevel_notify_inval_inode(State->Session, ChildIno, 0, 0);
            }
            if (KeepRecord && NtfsFileRecordGetLinkCount(Child) == 0)
            {
                if (Canonical)
                    Open->second.Unlinked = true;
                if (Inode != State->Inodes.end())
                    Inode->second.Unlinked = true;
                State->PendingAccess.erase(NtfsFileRecordGetReference(Child));
            }
        }
    }
    ReleaseChangedParent(State, Parent, Owned, Status);
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
        if (OldParentIno != NewParentIno && (NtfsFileRecordGetHeader(Source)->Flags & FR_IS_DIRECTORY))
        {
            DropListing(State, SourceIno);
            fuse_lowlevel_notify_inval_inode(State->Session, SourceIno, 0, 0);
        }
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

/* relatime: the access time moves when it is not newer than the last change or is a day old. */
static bool
AccessTimeDue(NtfsFuseState* State, NtfsFuseOpenFile* Open, PNtfsFileRecord File, UINT64* Current)
{
    const UINT64 Day = UINT64_C(864000000000);
    NtfsFileBasicInformation Information;
    struct timespec Now;

    if (!NT_SUCCESS(NtfsFileRecordGetBasicInformation(File, &Information)))
        return false;
    UINT64 Accessed = Open->PendingAccessTime ? Open->PendingAccessTime : Information.LastAccessTime;
    UINT64 Written = Open->PendingWriteTime ? Open->PendingWriteTime : Information.LastWriteTime;
    UINT64 Changed = Open->PendingWriteTime ? Open->PendingWriteTime : Information.ChangeTime;

    clock_gettime(CLOCK_REALTIME, &Now);
    *Current = TimespecToNtfsTime(&Now);
    return State->StrictAtime || Accessed <= Written || Accessed <= Changed || Accessed + Day <= *Current;
}

static void
SetOpenCaching(NtfsFuseState* State, fuse_ino_t Ino, NtfsFuseOpenFile* Open, struct fuse_file_info* FileInfo)
{
    UINT64 Current;

    if (State->Writable && State->DirectIo)
        FileInfo->direct_io = 1;
    /* Cached reads never reach the daemon, so an access time still due needs the first read to. */
    else if (!State->Writable || State->NoAtime || (FileInfo->flags & O_ACCMODE) == O_WRONLY ||
             Open->AccessChecked || !AccessTimeDue(State, Open, Open->File, &Current))
        FileInfo->keep_cache = 1;
    else if (IsStored(State, Ino))
    {
        /* FIXME: an open whose data was pushed ahead takes the access time whether or not it reads. */
        Open->AccessChecked = true;
        Open->PendingAccessTime = Current;
        FileInfo->keep_cache = 1;
        /* No read will reach the daemon to make the kernel ask for the new time: drop its copy now. */
        if (State->Session)
            fuse_lowlevel_notify_inval_inode(State->Session, Ino, -1, 0);
    }
}

/* What a small plain file's data is made of: resident bytes, or up to four runs on the image. */
struct NtfsPrefetchItem
{
    fuse_ino_t Ino;
    ULONG Size;
    std::vector<UCHAR> Resident;
    NtfsFastExtent Runs[4];
    unsigned RunCount;
};

static bool
DescribeSmallFile(NtfsFuseState* State, PNtfsFileRecord File, NtfsPrefetchItem* Item)
{
    PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    ULONG Cluster = ClusterBytes(State);
    ULONGLONG Clusters = NtfsVolumeGetClustersInVolume(State->Volume);

    if (!Data || Data->Flags || (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) ||
        NtfsFileRecordGetAttribute(File, TypeReparsePoint, NULL))
    {
        return false;
    }
    Item->RunCount = 0;
    Item->Resident.clear();
    if (!Data->IsNonResident)
    {
        ULONG Length = Data->Resident.DataLength;
        ULONG Remaining = Length;

        Item->Size = Length;
        Item->Resident.resize(Length);
        return Length == 0 ||
               (NT_SUCCESS(NtfsFileRecordCopyData(File, TypeData, NULL, Item->Resident.data(), &Remaining, 0)) && Remaining == 0);
    }
    if (Data->NonResident.DataSize > NtfsPrefetchFileLimit ||
        Data->NonResident.InitalizedDataSize != Data->NonResident.DataSize)
    {
        return false;
    }
    Item->Size = (ULONG)Data->NonResident.DataSize;
    for (ULONGLONG Offset = 0; Offset < Item->Size;)
    {
        NtfsRetrievalExtent Extent;
        ULONGLONG Start;
        ULONG Count = 1;
        NTSTATUS Status = NtfsFileRecordQueryRetrievalPointers(File, TypeData, NULL, Offset / Cluster, &Start, &Extent, &Count);
        ULONGLONG Lcn = (ULONGLONG)Extent.Lcn;

        if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) || Count != 1 || Extent.Lcn < 0 ||
            Item->RunCount == 4 || Extent.NextVcn <= Offset / Cluster || Lcn >= Clusters ||
            Extent.NextVcn - Start > Clusters - Lcn)
        {
            return false;
        }
        ULONGLONG End = Extent.NextVcn * Cluster < Item->Size ? Extent.NextVcn * Cluster : Item->Size;

        Item->Runs[Item->RunCount++] = {Offset, End, (Lcn + (Offset / Cluster - Start)) * Cluster + Offset % Cluster};
        Offset = End;
    }
    return true;
}

static bool
PushSmallFile(NtfsFuseState* State, const NtfsPrefetchItem& Item, std::vector<UCHAR>& Buffer)
{
    struct fuse_bufvec Vector = FUSE_BUFVEC_INIT(Item.Size);

    if (Item.Size == 0)
        return true;
    if (Item.RunCount == 0)
        Vector.buf[0].mem = const_cast<UCHAR*>(Item.Resident.data());
    else
    {
        if (Buffer.size() < Item.Size)
            Buffer.resize(Item.Size);
        for (unsigned Index = 0; Index < Item.RunCount; Index++)
        {
            size_t Length = (size_t)(Item.Runs[Index].End - Item.Runs[Index].Start);
            size_t Done = 0;


            while (Done < Length)
            {
                ssize_t Result = pread(NtfsDiskFdLinux(), Buffer.data() + Item.Runs[Index].Start + Done, Length - Done,
                                       (off_t)(Item.Runs[Index].Disk + Done));

                if (Result < 0 && errno == EINTR)
                    continue;
                if (Result <= 0)
                    return false;
                Done += (size_t)Result;
            }
        }
        Vector.buf[0].mem = Buffer.data();
    }
    return fuse_lowlevel_notify_store(State->Session, Item.Ino, 0, &Vector, (fuse_buf_copy_flags)0) == 0;
}

/* The first reader of a small file gets its data with the open instead of asking for it afterwards. */
static void
StoreAtOpen(NtfsFuseState* State, fuse_ino_t Ino, PNtfsFileRecord File)
{
    static std::vector<UCHAR> Buffer;
    NtfsPrefetchItem Item;

    if (!State->Session || !State->PrefetchThread.joinable() || IsStored(State, Ino))
        return;
    Item.Ino = Ino;
    if (DescribeSmallFile(State, File, &Item) && PushSmallFile(State, Item, Buffer))
    {
        SetStored(State, Ino, true);
    }
}

static void
SchedulePrefetch(NtfsFuseState* State, fuse_ino_t Ino)
{
    auto Inode = State->Inodes.find(Ino);

    if (!State->PrefetchThread.joinable() || State->DirectIo || Inode == State->Inodes.end() ||
        !Inode->second.ListDirectory)
    {
        return;
    }
    auto Cached = State->DirectoryCache.find(Inode->second.ListDirectory);
    if (Cached == State->DirectoryCache.end())
        return;
    const std::shared_ptr<NtfsDirectoryListing>& Listing = Cached->second;
    size_t Index = Inode->second.ListIndex;
    size_t From;

    if (Index >= Listing->Entries.size() || Listing->Entries[Index].Reference != Inode->second.Reference)
        return;
    if (State->PrefetchStream != Listing.get() || Index >= State->PrefetchNext || Index + 64 < State->PrefetchNext)
    {
        State->PrefetchStream = Listing.get();
        State->PrefetchBatch = 8;
        From = Index + 1;
    }
    else if (Index + State->PrefetchBatch / 2 >= State->PrefetchNext)
    {
        if (State->PrefetchBatch < 64)
            State->PrefetchBatch *= 2;
        From = State->PrefetchNext;
    }
    else
        return;
    State->PrefetchNext = From + State->PrefetchBatch < Listing->Entries.size() ? From + State->PrefetchBatch
                                                                                 : Listing->Entries.size();
    if (From >= State->PrefetchNext)
        return;
    {
        std::lock_guard<std::mutex> Guard(State->PrefetchLock);

        State->PrefetchListing = Listing;
        State->PrefetchFrom = From;
        State->PrefetchTo = State->PrefetchNext;
        State->PrefetchJobGeneration = State->PrefetchGeneration.load();
        State->PrefetchPending = true;
    }
    State->PrefetchWake.notify_one();
}

static void
PrefetchLoop(NtfsFuseState* State)
{
    std::vector<NtfsPrefetchItem> Items;
    std::vector<UCHAR> Buffer;

    for (;;)
    {
        std::shared_ptr<NtfsDirectoryListing> Listing;
        size_t From, To;
        uint64_t Generation;

        {
            std::unique_lock<std::mutex> Guard(State->PrefetchLock);

            State->PrefetchWake.wait(Guard, [State] { return State->PrefetchPending || State->WorkerStop.load(); });
            if (State->WorkerStop.load())
                return;
            Listing = std::move(State->PrefetchListing);
            From = State->PrefetchFrom;
            To = State->PrefetchTo;
            Generation = State->PrefetchJobGeneration;
            State->PrefetchPending = false;
        }
        Items.clear();
        {
            std::lock_guard<std::mutex> Guard(State->RequestLock);
            ULONGLONG Total = 0;

            if (Generation != State->PrefetchGeneration.load())
                continue;
            for (size_t Index = From; Index < To && Total < 4 * 1024 * 1024; Index++)
            {
                const NtfsDirectoryName& Child = Listing->Entries[Index];
                fuse_ino_t Ino = InoFromReference(Child.Reference);
                auto Inode = State->Inodes.find(Ino);
                PNtfsFileRecord File = NULL;
                NtfsPrefetchItem Item;

                /* Only files the kernel knows, nobody has open and nothing was pushed for. */
                if (Inode == State->Inodes.end() || Inode->second.Reference != Child.Reference || Inode->second.Unlinked ||
                    State->OpenFiles.count(Ino) || IsStored(State, Ino) ||
                    !NT_SUCCESS(NtfsMasterFileTableGetFileRecordByReference(StateMft(State), Child.Reference, &File)))
                {
                    continue;
                }
                Item.Ino = Ino;
                if (DescribeSmallFile(State, File, &Item))
                {
                    Total += Item.Size;
                    Items.push_back(std::move(Item));
                }
                NtfsFileRecordDestroy(File);
            }
            State->PrefetchGate.lock_shared();
        }
        for (const NtfsPrefetchItem& Item : Items)
        {
            if (Generation != State->PrefetchGeneration.load())
                break;
            if (PushSmallFile(State, Item, Buffer))
            {
                SetStored(State, Item.Ino, true);
            }
        }
        State->PrefetchGate.unlock_shared();
    }
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
    Open = FindOpen(State, Ino);
    assert(Open && Open->File == File);
    FileInfo->fh = (uint64_t)(uintptr_t)Open->Fast;
    SetOpenCaching(State, Ino, Open, FileInfo);
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
    bool Reader = (FileInfo->flags & O_ACCMODE) == O_RDONLY && !(FileInfo->flags & O_TRUNC);

    if (!FindOpen(State, Ino))
    {
        bool Owned;

        Status = LoadInode(State, Ino, &File, &Owned);
        assert(!NT_SUCCESS(Status) || Owned);
        /* Nobody has it open, so the kernel holds no newer pages of it. */
        if (NT_SUCCESS(Status) && Reader && !State->DirectIo)
            StoreAtOpen(State, Ino, File);
    }
    if (NT_SUCCESS(Status))
        Status = OpenRecord(State, Ino, File, FileInfo);
    if (NT_SUCCESS(Status) && Reader)
        SchedulePrefetch(State, Ino);
    if (NT_SUCCESS(Status) && (FileInfo->flags & O_TRUNC) &&
        (FileInfo->flags & O_ACCMODE) != O_RDONLY)
    {
        Status = KillSetIdBits(State, Request, Ino, ((NtfsFastFile*)(uintptr_t)FileInfo->fh)->File,
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
NtfsLowOpenAny(fuse_req_t Request, fuse_ino_t Ino, struct fuse_file_info* FileInfo)
{
    if ((FileInfo->flags & O_ACCMODE) == O_RDONLY && !(FileInfo->flags & O_TRUNC))
        DispatchRequest<NtfsLowOpen>(Request, Ino, FileInfo);
    else
        DispatchMutation<NtfsLowOpen>(Request, Ino, FileInfo);
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

static PUCHAR
GetReplyBuffer(fuse_req_t Request, NtfsFuseState* State, size_t Size)
{
#ifdef FUSE_CAP_OVER_IO_URING
    char* Payload;
    size_t Capacity;
    if (fuse_req_get_payload(Request, &Payload, &Capacity, NULL) == 0 && Capacity >= Size)
        return (PUCHAR)Payload;
#else
    UNREFERENCED_PARAMETER(Request);
#endif
    if (State->ReadBuffer.size() < Size)
        State->ReadBuffer.resize(Size);
    return State->ReadBuffer.data();
}

static bool
ReplyMappedRead(fuse_req_t Request, NtfsFuseState* State, PNtfsFileRecord File, ULONG Length, ULONGLONG Offset)
{
    PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    if (!Data || !Data->IsNonResident || Data->Flags ||
        Offset >= Data->NonResident.DataSize || Offset >= Data->NonResident.InitalizedDataSize ||
        NtfsFileRecordGetAttribute(File, TypeReparsePoint, NULL))
        return false;

    if (Length > Data->NonResident.DataSize - Offset)
        Length = (ULONG)(Data->NonResident.DataSize - Offset);
    if (Length < State->SpliceThreshold || Length > Data->NonResident.InitalizedDataSize - Offset)
        return false;

    ULONG Cluster = ClusterBytes(State);
    ULONGLONG First = Offset / Cluster;
    ULONGLONG Last = (Offset + Length - 1) / Cluster;
    ULONGLONG Start;
    NtfsRetrievalExtent Extent;
    ULONG Count = 1;
    NTSTATUS Status = NtfsFileRecordQueryRetrievalPointers(File, TypeData, NULL, First, &Start, &Extent, &Count);
    if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) || Count != 1 ||
        Extent.Lcn < 0 || Start > First || Last >= Extent.NextVcn)
        return false;

    ULONGLONG Lcn = (ULONGLONG)Extent.Lcn;
    ULONGLONG Clusters = NtfsVolumeGetClustersInVolume(State->Volume);
    if (Lcn >= Clusters || Last - Start >= Clusters - Lcn ||
        Lcn + (Last - Start) > ((ULONGLONG)INT64_MAX - (Cluster - 1)) / Cluster)
        return false;

    struct fuse_bufvec Reply = FUSE_BUFVEC_INIT(Length);
    Reply.buf[0].flags = (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
    Reply.buf[0].fd = NtfsDiskFdLinux();
    Reply.buf[0].pos = (off_t)((Lcn + (First - Start)) * Cluster + Offset % Cluster);
    fuse_reply_data(Request, &Reply, FUSE_BUF_SPLICE_MOVE);
    return true;
}

static void
NtfsLowRead(fuse_req_t Request,
            fuse_ino_t Ino,
            size_t Size,
            off_t Offset,
            struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = RequestState(Request);
    NtfsFuseOpenFile* Open = ((NtfsFastFile*)(uintptr_t)FileInfo->fh)->Owner;
    PNtfsFileRecord File = Open->File;
    ULONG Requested = Size > MAXULONG ? MAXULONG : (ULONG)Size;
    ULONG Remaining = Requested;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Ino);
    SettleFast(State, Open);
    if (State->Writable && !State->NoAtime)
    {
        UINT64 Current;

        if (!Open->AccessChecked)
        {
            Open->AccessChecked = true;
            if (AccessTimeDue(State, Open, File, &Current))
                Open->PendingAccessTime = Current;
        }
    }
    ArmFast(State, Open, (ULONGLONG)Offset);
    if (State->SpliceThreshold && Requested >= State->SpliceThreshold &&
        ReplyMappedRead(Request, State, File, Requested, (ULONGLONG)Offset))
        return;
    PUCHAR Buffer = GetReplyBuffer(Request, State, Requested);
    Status = NtfsReadFileLinux(File, Buffer, &Remaining, (ULONGLONG)Offset);
    if (Status == STATUS_END_OF_FILE)
        fuse_reply_buf(Request, NULL, 0);
    else if (!NT_SUCCESS(Status))
        ReplyStatus(Request, Status);
    else
        fuse_reply_buf(Request, (const char*)Buffer, Requested - Remaining);
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
    PNtfsFileRecord File = ((NtfsFastFile*)(uintptr_t)FileInfo->fh)->File;
    auto Open = State->OpenFiles.find(Ino);
    LARGE_INTEGER ByteOffset;
    ULONG Length = Size > MAXULONG ? MAXULONG : (ULONG)Size;
    NTSTATUS Status;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    if (Open != State->OpenFiles.end())
        SettleFast(State, &Open->second);
    ByteOffset.QuadPart = (LONGLONG)Offset;
    if (Open != State->OpenFiles.end())
    {
        UINT32 Mask = NtfsFileRecordGetAutomaticTimestampMask(File);
        struct timespec Now;

        if (!FileInfo->writepage && !Open->second.SetIdChecked && fuse_req_ctx(Request)->uid != 0)
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
        if (NT_SUCCESS(Status) && !FileInfo->writepage)
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
    {
        ReplyStatus(Request, Status);
        return;
    }
    if (Open != State->OpenFiles.end())
        ArmFast(State, &Open->second, (ULONGLONG)Offset);
    fuse_reply_write(Request, Length);
}

/* Maps file bytes [Start, End) through the snapshot; false when any part lies outside it. */
static bool
MapFast(const NtfsFastFile* Fast, ULONGLONG Start, ULONGLONG End, NtfsFastExtent* Pieces, unsigned* Count)
{
    unsigned Capacity = *Count;
    unsigned Used = 0;

    for (const NtfsFastExtent& Extent : Fast->Extents)
    {
        if (Start >= Extent.End)
            continue;
        if (Start < Extent.Start || Used == Capacity)
            return false;
        ULONGLONG Stop = End < Extent.End ? End : Extent.End;

        Pieces[Used++] = {Start, Stop, Extent.Disk + (Start - Extent.Start)};
        Start = Stop;
        if (Start == End)
        {
            *Count = Used;
            return true;
        }
    }
    return false;
}

static PUCHAR
FastBuffer(fuse_req_t Request, size_t Size)
{
    static thread_local std::vector<UCHAR> Buffer;
#ifdef FUSE_CAP_OVER_IO_URING
    char* Payload;
    size_t Capacity;

    if (fuse_req_get_payload(Request, &Payload, &Capacity, NULL) == 0 && Capacity >= Size)
        return (PUCHAR)Payload;
#else
    UNREFERENCED_PARAMETER(Request);
#endif
    if (Buffer.size() < Size)
        Buffer.resize(Size);
    return Buffer.data();
}

static bool
TryFastRead(fuse_req_t Request, NtfsFuseState* State, NtfsFastFile* Fast, size_t Size, off_t Offset)
{
    NtfsFastExtent Pieces[8];
    unsigned Count = 8;

    if (!Fast->Armed.load(std::memory_order_acquire))
        return false;
    std::shared_lock<std::shared_mutex> Guard(Fast->Lock);
    if (!Fast->Armed.load(std::memory_order_acquire) || !Fast->ReadOk || Offset < 0 || Size == 0 ||
        (ULONGLONG)Offset >= Fast->DataSize)
    {
        return false;
    }
    ULONGLONG Start = (ULONGLONG)Offset;
    ULONGLONG Length = Size < Fast->DataSize - Start ? Size : Fast->DataSize - Start;

    if (Length > Fast->Committed || Start > Fast->Committed - Length || !MapFast(Fast, Start, Start + Length, Pieces, &Count))
        return false;
    if (State->SpliceThreshold && Length >= State->SpliceThreshold)
    {
        union
        {
            struct fuse_bufvec Vector;
            char Storage[sizeof(struct fuse_bufvec) + 7 * sizeof(struct fuse_buf)];
        } Reply = {};

        struct fuse_buf* Buffers = Reply.Vector.buf;

        Reply.Vector.count = Count;
        for (unsigned Index = 0; Index < Count; Index++)
        {
            Buffers[Index].size = (size_t)(Pieces[Index].End - Pieces[Index].Start);
            Buffers[Index].flags = (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
            Buffers[Index].fd = NtfsDiskFdLinux();
            Buffers[Index].pos = (off_t)Pieces[Index].Disk;
        }
        fuse_reply_data(Request, &Reply.Vector, FUSE_BUF_SPLICE_MOVE);
        return true;
    }

    PUCHAR Buffer = FastBuffer(Request, (size_t)Length);
    size_t Done = 0;

    for (unsigned Index = 0; Index < Count; Index++)
    {
        size_t Piece = (size_t)(Pieces[Index].End - Pieces[Index].Start);
        size_t Read = 0;

        while (Read < Piece)
        {
            ssize_t Result = pread(NtfsDiskFdLinux(), Buffer + Done + Read, Piece - Read, (off_t)(Pieces[Index].Disk + Read));

            if (Result < 0 && errno == EINTR)
                continue;
            if (Result <= 0)
            {
                fuse_reply_err(Request, EIO);
                return true;
            }
            Read += (size_t)Result;
        }
        Done += Piece;
    }
    fuse_reply_buf(Request, (const char*)Buffer, Done);
    return true;
}

static bool
TryFastWrite(fuse_req_t Request, NtfsFuseState* State, NtfsFastFile* Fast, const char* Buffer, size_t Size, off_t Offset,
             struct fuse_file_info* FileInfo)
{
    NtfsFastExtent Pieces[8];
    unsigned Count = 8;
    ULONGLONG Start = (ULONGLONG)Offset;
    ULONGLONG End = Start + Size;
    bool Commit = false;

    if (!Fast->Armed.load(std::memory_order_acquire))
        return false;
    {
        std::shared_lock<std::shared_mutex> Guard(Fast->Lock);

        if (!Fast->Armed.load(std::memory_order_acquire) || !Fast->WriteOk || Offset < 0 || Size == 0 || Size > MAXULONG ||
            (!FileInfo->writepage && !Fast->SetIdChecked && fuse_req_ctx(Request)->uid != 0) ||
            !MapFast(Fast, Start, End, Pieces, &Count))
        {
            return false;
        }
        if (End > Fast->Committed)
        {
            std::lock_guard<std::mutex> Pending(Fast->PendingLock);

            if (Start > Fast->Frontier && Start - Fast->Frontier > NtfsFastWindow)
                return false;
        }
        for (unsigned Index = 0; Index < Count; Index++)
        {
            NTSTATUS Status = NtfsWriteFileDataLinux(Pieces[Index].Disk,
                                                     (const UCHAR*)Buffer + (Pieces[Index].Start - Start),
                                                     (ULONG)(Pieces[Index].End - Pieces[Index].Start));

            if (!NT_SUCCESS(Status))
            {
                ReplyStatus(Request, Status);
                return true;
            }
        }
        if (End > Fast->Committed)
        {
            std::lock_guard<std::mutex> Pending(Fast->PendingLock);

            if (Start <= Fast->Frontier)
            {
                if (End > Fast->Frontier)
                    Fast->Frontier = End;
                while (!Fast->Ahead.empty() && Fast->Ahead.front().first <= Fast->Frontier)
                {
                    if (Fast->Ahead.front().second > Fast->Frontier)
                        Fast->Frontier = Fast->Ahead.front().second;
                    Fast->Ahead.erase(Fast->Ahead.begin());
                }
            }
            else
            {
                auto Place = Fast->Ahead.begin();

                while (Place != Fast->Ahead.end() && Place->first < Start)
                    ++Place;
                Fast->Ahead.insert(Place, {Start, End});
            }
            Commit = Fast->Frontier - Fast->Committed >= 4 * NtfsFastWindow;
        }
        Fast->Written.store(true, std::memory_order_relaxed);
        if (!FileInfo->writepage)
        {
            struct timespec Now;
            UINT64 Previous = Fast->WriteTime.load(std::memory_order_relaxed);

            clock_gettime(CLOCK_REALTIME, &Now);
            UINT64 Time = TimespecToNtfsTime(&Now);
            while (Previous < Time && !Fast->WriteTime.compare_exchange_weak(Previous, Time))
            {
            }
        }
    }
    if (Commit)
    {
        /* Store the length reached so far, so a long stream does not leave it all to the last close. */
        std::lock_guard<std::mutex> Guard(State->RequestLock);

        SettleFast(State, Fast->Owner);
        Fast->Owner->DataRequests = 1;
        ArmFast(State, Fast->Owner, End - 1);
    }
    fuse_reply_write(Request, Size);
    return true;
}

static void
NtfsLowReadFast(fuse_req_t Request, fuse_ino_t Ino, size_t Size, off_t Offset, struct fuse_file_info* FileInfo)
{
    NtfsFastFile* Fast = (NtfsFastFile*)(uintptr_t)FileInfo->fh;
    NtfsFuseState* State = RequestState(Request);
    int OldCancelState;
    bool Done;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &OldCancelState);
    Done = Fast && TryFastRead(Request, State, Fast, Size, Offset);
    if (!Done && Fast)
    {
        /* Whoever held the request lock may have armed the file again: do not take it down for nothing. */
        std::unique_lock<std::mutex> Guard(State->RequestLock);

        if (Fast->Armed.load(std::memory_order_acquire))
        {
            Guard.unlock();
            Done = TryFastRead(Request, State, Fast, Size, Offset);
        }
    }
    pthread_setcancelstate(OldCancelState, NULL);
    if (!Done)
        DispatchRequest<NtfsLowRead>(Request, Ino, Size, Offset, FileInfo);
}

static void
NtfsLowWriteFast(fuse_req_t Request, fuse_ino_t Ino, const char* Buffer, size_t Size, off_t Offset,
                 struct fuse_file_info* FileInfo)
{
    NtfsFastFile* Fast = (NtfsFastFile*)(uintptr_t)FileInfo->fh;
    NtfsFuseState* State = RequestState(Request);
    int OldCancelState;
    bool Done;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &OldCancelState);
    Done = Fast && TryFastWrite(Request, State, Fast, Buffer, Size, Offset, FileInfo);
    if (!Done && Fast)
    {
        std::unique_lock<std::mutex> Guard(State->RequestLock);

        if (Fast->Armed.load(std::memory_order_acquire))
        {
            Guard.unlock();
            Done = TryFastWrite(Request, State, Fast, Buffer, Size, Offset, FileInfo);
        }
    }
    pthread_setcancelstate(OldCancelState, NULL);
    if (!Done)
        DispatchRequest<NtfsLowWrite>(Request, Ino, Buffer, Size, Offset, FileInfo);
}

/*
 * One thread answers a caller that waits for each reply fastest. When the kernel queues requests
 * without waiting, as writeback and read-ahead do, helpers drain the queue beside it.
 */
static void
WakeWorker(NtfsFuseState* State)
{
    if (State->WorkersSleeping.load(std::memory_order_relaxed) == 0)
        return;
    {
        std::lock_guard<std::mutex> Guard(State->WorkerLock);

        State->WorkerTokens++;
    }
    State->WorkerWake.notify_one();
}

static bool
RequestWaiting(struct fuse_session* Session)
{
    struct pollfd Poll = {fuse_session_fd(Session), POLLIN, 0};

    return poll(&Poll, 1, 0) > 0 && (Poll.revents & POLLIN);
}

static void
WorkerLoop(NtfsFuseState* State)
{
    struct fuse_session* Session = State->Session;
    struct fuse_buf Buffer = {};

    for (;;)
    {
        {
            std::unique_lock<std::mutex> Guard(State->WorkerLock);

            State->WorkersSleeping++;
            State->WorkerWake.wait(Guard, [State] { return State->WorkerTokens != 0 || State->WorkerStop.load(); });
            State->WorkersSleeping--;
            if (State->WorkerStop.load())
                break;
            State->WorkerTokens--;
        }
        for (unsigned Idle = 0; Idle < 64 && !State->WorkerStop.load() && !fuse_session_exited(Session);)
        {
            if (!RequestWaiting(Session))
            {
                Idle++;
                continue;
            }
            int Result = fuse_session_receive_buf(Session, &Buffer);

            if (Result == -EINTR || Result == -EAGAIN)
                continue;
            if (Result <= 0)
                break;
            Idle = 0;
            if (RequestWaiting(Session))
                WakeWorker(State);
            fuse_session_process_buf(Session, &Buffer);
        }
    }
    free(Buffer.mem);
    State->WorkersDone++;
}

static void
WakeBlockedWorker(int Signal)
{
    UNREFERENCED_PARAMETER(Signal);
}

/* A helper that lost the race for a request sits in read(): a signal brings it back to look at the stop flag. */
static void
StopWorkers(NtfsFuseState* State)
{
    struct sigaction Action = {};

    Action.sa_handler = WakeBlockedWorker;
    sigaction(SIGUSR2, &Action, NULL);
    State->WorkerStop.store(true);
    while (State->WorkersDone.load() < State->Workers.size())
    {
        State->WorkerWake.notify_all();
        for (std::thread& Worker : State->Workers)
            pthread_kill(Worker.native_handle(), SIGUSR2);
        usleep(1000);
    }
    for (std::thread& Worker : State->Workers)
        Worker.join();
    State->Workers.clear();
    {
        std::lock_guard<std::mutex> Guard(State->PrefetchLock);
    }
    State->PrefetchWake.notify_all();
    if (State->PrefetchThread.joinable())
        State->PrefetchThread.join();
}

/*
 * The kernel lets a FUSE mount hold 1% of the dirty memory, which paces writers below what the
 * daemon can take. A privileged mount gets the share of any other filesystem; without the
 * privilege the write fails and the default stays.
 */
static void
RaiseDirtyShare(const char* Mountpoint)
{
    char* Resolved = realpath(Mountpoint, NULL);
    FILE* Mounts = fopen("/proc/self/mountinfo", "r");
    char Line[4096];
    char Device[64] = "";

    while (Resolved && Mounts && fgets(Line, sizeof(Line), Mounts))
    {
        char Found[64];
        char Path[2048];

        if (sscanf(Line, "%*u %*u %63s %*s %2047s", Found, Path) == 2 && strcmp(Path, Resolved) == 0)
            snprintf(Device, sizeof(Device), "%s", Found);
    }
    if (Mounts)
        fclose(Mounts);
    free(Resolved);
    if (Device[0])
    {
        char Name[128];

        snprintf(Name, sizeof(Name), "/sys/class/bdi/%s/max_ratio", Device);
        int File = open(Name, O_WRONLY | O_CLOEXEC);

        if (File >= 0)
        {
            (void)!write(File, "100", 3);
            close(File);
        }
    }
}

static int
SessionLoop(NtfsFuseState* State)
{
    struct fuse_session* Session = State->Session;
    struct fuse_buf Buffer = {};
    unsigned Interval = 1;
    unsigned Seen = 0;
    int Result = 0;

    while (!fuse_session_exited(Session))
    {
        Result = fuse_session_receive_buf(Session, &Buffer);
        if (Result == -EINTR)
            continue;
        if (Result <= 0)
            break;
        if (!State->Workers.empty() && Buffer.size >= 8 && !(Buffer.flags & FUSE_BUF_IS_FD))
        {
            uint32_t Opcode;

            memcpy(&Opcode, (const char*)Buffer.mem + 4, sizeof(Opcode));
            /* FUSE_READ and FUSE_WRITE; a lone caller's requests never find another one waiting. */
            if ((Opcode == 15 || Opcode == 16) && ++Seen >= Interval)
            {
                Seen = 0;
                if (RequestWaiting(Session))
                {
                    Interval = 1;
                    WakeWorker(State);
                }
                else if (Interval < 16)
                    Interval *= 2;
            }
        }
        fuse_session_process_buf(Session, &Buffer);
    }
    free(Buffer.mem);
    return Result < 0 ? Result : 0;
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
    PNtfsFileRecord File = ((NtfsFastFile*)(uintptr_t)FileInfo->fh)->File;
    auto Open = State->OpenFiles.find(Ino);
    PAttribute Data;
    ULONGLONG Target;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!State->Writable)
    {
        fuse_reply_err(Request, EROFS);
        return;
    }
    if (Open != State->OpenFiles.end())
        SettleFast(State, &Open->second);
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
AppendDirectoryName(void* Context, ULONGLONG Reference, const FileNameEx* Entry)
{
    auto* Listing = static_cast<NtfsDirectoryListing*>(Context);
    std::string Name;
    mode_t Type = 0;

    if (!Utf16ToUtf8(Entry->Name, Entry->NameLength, Name))
        return STATUS_OBJECT_NAME_INVALID;
    if (Entry->Flags & FILE_PERM_REPARSE_PT)
    {
        if (Entry->Extended.ReparseTag == NTFS_IO_REPARSE_TAG_WOF)
            Type = Entry->Flags & FN_DIRECTORY ? S_IFDIR : S_IFREG;
    }
    else if (Entry->Flags & FN_DIRECTORY)
        Type = S_IFDIR;
    else if (!Entry->Extended.EAInfo.PackedEASize)
        Type = S_IFREG;
    Listing->Entries.push_back({std::move(Name), Reference, Type});
    return STATUS_SUCCESS;
}

static NTSTATUS
GetListing(NtfsFuseState* State, fuse_ino_t Ino, std::shared_ptr<NtfsDirectoryListing>& Result)
{
    auto Cached = State->DirectoryCache.find(Ino);

    if (Cached == State->DirectoryCache.end())
    {
        auto Inode = State->Inodes.find(Ino);
        if (Inode != State->Inodes.end() && Inode->second.Unlinked)
        {
            Result.reset();
            return STATUS_SUCCESS;
        }
        PNtfsFileRecord File = NULL;
        NtfsDirectoryListing Listing;
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
            Status = NtfsDirectoryVisitNames(State->Volume, File, AppendDirectoryName, &Listing);
        }
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
    NTSTATUS Status;

    /* The kernel then stops asking: a directory costs no open and no release. */
    if (State->OpenlessDirectories)
    {
        fuse_reply_err(Request, ENOSYS);
        return;
    }
    Status = GetListing(State, Ino, Listing);
    if (!NT_SUCCESS(Status))
    {
        fuse_reply_err(Request, Status == STATUS_OBJECT_NAME_INVALID ? EILSEQ
                                                                     : NtStatusToErrno(Status));
        return;
    }
    FileInfo->keep_cache = 1;
    FileInfo->cache_readdir = 1;
    fuse_reply_open(Request, FileInfo);
}

static void
NtfsLowReleasedir(fuse_req_t Request, fuse_ino_t Ino, struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(Ino);
    UNREFERENCED_PARAMETER(FileInfo);
    fuse_reply_err(Request, 0);
}

/* The position leads and the scan number trails, so any offset below a valid one still names a position. */
static const unsigned NtfsScanBits = 20;

static void
ReadDirectory(fuse_req_t Request,
              fuse_ino_t Ino,
              size_t Size,
              off_t Offset,
              struct fuse_file_info* FileInfo,
              bool Plus)
{
    NtfsFuseState* State = RequestState(Request);
    uint64_t Cookie = Offset < 0 ? 0 : (uint64_t)Offset;
    uint32_t Scan = (uint32_t)(Cookie & ((UINT64_C(1) << NtfsScanBits) - 1));
    size_t First = (size_t)(Cookie >> NtfsScanBits);
    std::shared_ptr<NtfsDirectoryListing> Snapshot;
    auto& Scans = State->DirectoryScans[Ino];

    UNREFERENCED_PARAMETER(FileInfo);
    /* TODO: update the directory access time under relatime and strictatime. */
    for (const auto& Known : Scans)
    {
        if (Cookie != 0 && Known.first == Scan)
            Snapshot = Known.second;
    }
    /* A read from the start (opendir or rewinddir) sees names added since the last one. */
    if (!Snapshot)
    {
        NTSTATUS Status = GetListing(State, Ino, Snapshot);

        if (!NT_SUCCESS(Status))
        {
            fuse_reply_err(Request, Status == STATUS_OBJECT_NAME_INVALID ? EILSEQ
                                                                         : NtStatusToErrno(Status));
            return;
        }
        if (Scans.empty() || Scans.back().second != Snapshot)
        {
            uint32_t Next = Scans.empty() ? 1 : Scans.back().first + 1;

            if (Next >= (UINT32_C(1) << NtfsScanBits))
                Next = 1;
            Scans.emplace_back(Next, Snapshot);
            if (Scans.size() > 4)
                Scans.erase(Scans.begin());
        }
        Scan = Scans.back().first;
    }

    const NtfsDirectoryListing* Listing = Snapshot.get();
    if (!Listing)
    {
        fuse_reply_buf(Request, NULL, 0);
        return;
    }
    auto Current = State->DirectoryCache.find(Ino);
    bool IsCurrent = Current != State->DirectoryCache.end() && Current->second.get() == Listing;
    char* Buffer = (char*)GetReplyBuffer(Request, State, Size);
    size_t Used = 0;
    size_t Total = Listing->Entries.size() + 2;

    if (Plus)
    {
        State->ListedRecords.clear();
        State->ListingGeneration = NtfsDiskWriteGenerationLinux();
    }
    for (size_t Index = First; Index < Total; Index++)
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
            if (IsCurrent && (!Child.Type || (Plus && Child.Type == S_IFDIR)))
            {
                auto Open = State->OpenFiles.find(InoFromReference(Child.Reference));
                NTSTATUS Status = STATUS_SUCCESS;

                if (Open != State->OpenFiles.end())
                    SettleFast(State, &Open->second);
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
        off_t Next = (off_t)(((uint64_t)(Index + 1) << NtfsScanBits) | Scan);

        Needed = Plus ? fuse_add_direntry_plus(Request, Buffer + Used, Size - Used, Name, &Entry, Next)
                      : fuse_add_direntry(Request, Buffer + Used, Size - Used, Name, &Entry.attr, Next);
        decltype(State->LookupRecord) Record(File, NtfsFileRecordDestroy);
        if (Needed > Size - Used)
            break;
        Used += Needed;
        if (Plus && Index >= 2 && Entry.ino != 0)
        {
            NtfsFuseInode& Inode = State->Inodes[Entry.ino];
            Inode.Reference = Listing->Entries[Index - 2].Reference;
            Inode.Lookups++;
            if (IsCurrent)
            {
                Inode.ListDirectory = Ino;
                Inode.ListIndex = Index - 2;
            }
            if (Record)
                State->ListedRecords.emplace(Inode.Reference, std::move(Record));
        }
    }
    fuse_reply_buf(Request, Buffer, Used);
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
    State->Inodes[FUSE_ROOT_ID] = NtfsFuseInode{NtfsFileRecordGetReference(Root), 1, false, 0, 0};
    NtfsFileRecordDestroy(Root);

    Operations.init = NtfsLowInit;
    Operations.lookup = DispatchRequest<NtfsLowLookup>;
    Operations.forget = DispatchRequest<NtfsLowForget>;
    Operations.forget_multi = DispatchRequest<NtfsLowForgetMulti>;
    Operations.getattr = DispatchRequest<NtfsLowGetattr>;
    Operations.setattr = DispatchMutation<NtfsLowSetattr>;
    Operations.readlink = DispatchRequest<NtfsLowReadlink>;
    Operations.mknod = DispatchRequest<NtfsLowMknod>;
    Operations.mkdir = DispatchRequest<NtfsLowMkdir>;
    Operations.unlink = DispatchMutation<NtfsLowUnlink>;
    Operations.rmdir = DispatchMutation<NtfsLowRmdir>;
    Operations.symlink = DispatchRequest<NtfsLowSymlink>;
    Operations.rename = DispatchMutation<NtfsLowRename>;
    Operations.link = DispatchRequest<NtfsLowLink>;
    Operations.open = NtfsLowOpenAny;
    Operations.read = NtfsLowReadFast;
    Operations.write = NtfsLowWriteFast;
    Operations.release = DispatchRequest<NtfsLowRelease>;
    Operations.fsync = DispatchRequest<NtfsLowFsync>;
    Operations.fsyncdir = DispatchRequest<NtfsLowFsync>;
    Operations.opendir = DispatchRequest<NtfsLowOpendir>;
    Operations.readdir = DispatchRequest<NtfsLowReaddir>;
    Operations.readdirplus = DispatchRequest<NtfsLowReaddirplus>;
    Operations.releasedir = DispatchRequest<NtfsLowReleasedir>;
    Operations.statfs = DispatchRequest<NtfsLowStatfs>;
    Operations.setxattr = DispatchRequest<NtfsLowSetxattr>;
    Operations.getxattr = DispatchRequest<NtfsLowGetxattr>;
    Operations.listxattr = DispatchRequest<NtfsLowListxattr>;
    Operations.removexattr = DispatchRequest<NtfsLowRemovexattr>;
    Operations.access = DispatchRequest<NtfsLowAccess>;
    Operations.create = DispatchRequest<NtfsLowCreate>;
    Operations.fallocate = DispatchRequest<NtfsLowFallocate>;

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
                if (State->Writable)
                    RaiseDirtyShare(Options.mountpoint);
                fuse_daemonize(Options.foreground);
                if (!State->RingTransport)
                {
                    unsigned Count = std::thread::hardware_concurrency();

                    Count = Count > 2 ? 2 : Count > 1 ? 1 : 0;
                    for (unsigned Index = 0; Index < Count; Index++)
                        State->Workers.emplace_back(WorkerLoop, State);
                }
                if (!State->Workers.empty())
                    State->PrefetchThread = std::thread(PrefetchLoop, State);
                Result = SessionLoop(State) == 0 ? 0 : 1;
                fuse_session_unmount(Session);
                StopWorkers(State);
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
    for (const auto& Inode : State->Inodes)
    {
        if (Inode.second.Unlinked)
            DeleteUnlinkedRecord(State, Inode.second.Reference);
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
            "  %s --addwim IMAGE WIM IMAGE_PATH PARTITION_OFFSET DISK_SIGNATURE\n"
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
            "  %s [--show-metadata] [--writable [--direct-io] [--writeback-cache|--write-through]]"
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

    if (strcmp(Argv[First], "--addwim") == 0)
    {
        ULONGLONG PartitionOffset = 0;
        ULONGLONG DiskSignature = 0;

        if (First + 6 != Argc ||
            ShowMetadata ||
            !ParseUnsigned(Argv[First + 4], ~(ULONGLONG)0, &PartitionOffset) ||
            !ParseUnsigned(Argv[First + 5], MAXULONG, &DiskSignature))
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
        Result = AddWimImage(&State,
                             Argv[First + 2],
                             Argv[First + 3],
                             PartitionOffset,
                             (uint32_t)DiskSignature);
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

        State.WritebackCache = true;
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
            else if (strcmp(Argv[First], "--write-through") == 0)
                State.WritebackCache = false;
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

                    /* FIXME: a later remount,rw is not seen; the daemon keeps refusing writes. */
                    if (Option == "ro")
                        State.Writable = false;
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
