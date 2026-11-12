/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test growing a fragmented file whose base file record is full
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <winioctl.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"
#define FRAME_SIZE 4120
#define HOLE_FILES 2400
#define MIN_EXTENTS 200
#define REGROW_FRAMES 64
#define ATTR_ATTRIBUTE_LIST 0x20
#define ATTR_DATA 0x80
#define ATTR_END 0xFFFFFFFF

static WCHAR VolumeRoot[] = L"?:\\";
static WCHAR VolumeDevice[] = L"\\\\.\\?:";
static WCHAR TestDir[MAX_PATH];
static UCHAR Frame[FRAME_SIZE];
static UCHAR ReadBuffer[65536];

static PCWSTR
TestPath(PCWSTR Name)
{
    static WCHAR Path[4][MAX_PATH];
    static ULONG Next;
    PWSTR Buffer = Path[Next++ % ARRAYSIZE(Path)];

    StringCchPrintfW(Buffer, MAX_PATH, L"%s\\%s", TestDir, Name);
    return Buffer;
}

static BOOL
FindTestVolume(VOID)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            VolumeDevice[4] = Letter;
            StringCchPrintfW(TestDir, ARRAYSIZE(TestDir), L"%c:\\FullBaseGrowth", Letter);
            return TRUE;
        }
    }
    return FALSE;
}

static UCHAR
PatternByte(ULONGLONG Offset)
{
    return (UCHAR)(Offset * 131 + (Offset >> 12));
}

static ULONG
CountExtents(HANDLE File)
{
    static UCHAR Buffer[16384];
    PRETRIEVAL_POINTERS_BUFFER Pointers = (PRETRIEVAL_POINTERS_BUFFER)Buffer;
    STARTING_VCN_INPUT_BUFFER Input;
    ULONG Extents = 0;
    DWORD Returned;
    BOOL More;

    Input.StartingVcn.QuadPart = 0;
    do
    {
        More = !DeviceIoControl(File, FSCTL_GET_RETRIEVAL_POINTERS, &Input, sizeof(Input),
                                Buffer, sizeof(Buffer), &Returned, NULL);
        if (More && GetLastError() != ERROR_MORE_DATA)
            return Extents;
        if (Pointers->ExtentCount == 0)
            break;
        Extents += Pointers->ExtentCount;
        Input.StartingVcn = Pointers->Extents[Pointers->ExtentCount - 1].NextVcn;
    } while (More);
    return Extents;
}

static BOOL
AppendFrames(HANDLE File, PULONGLONG Size, ULONG Count, ULONG StopAtExtents)
{
    LARGE_INTEGER Position;
    ULONG Index, Byte;
    DWORD Written;
    BOOL Result;

    for (Index = 0; Index < Count; Index++)
    {
        for (Byte = 0; Byte < FRAME_SIZE; Byte++)
            Frame[Byte] = PatternByte(*Size + Byte);
        Position.QuadPart = (LONGLONG)*Size;
        Result = SetFilePointerEx(File, Position, NULL, FILE_BEGIN) &&
                 WriteFile(File, Frame, FRAME_SIZE, &Written, NULL);
        ok(Result && Written == FRAME_SIZE, "Frame %lu at %I64u: error %lu, written %lu\n",
           Index, *Size, GetLastError(), Written);
        if (!Result || Written != FRAME_SIZE)
            return FALSE;
        *Size += FRAME_SIZE;
        if ((Index & 15) == 15)
        {
            Result = FlushFileBuffers(File);
            ok(Result, "FlushFileBuffers after frame %lu failed: %lu\n", Index, GetLastError());
            if (!Result)
                return FALSE;
            if (StopAtExtents && CountExtents(File) >= StopAtExtents)
                break;
        }
    }
    Result = FlushFileBuffers(File);
    ok(Result, "FlushFileBuffers failed: %lu\n", GetLastError());
    return Result;
}

static VOID
CheckContent(HANDLE File, ULONGLONG Size, ULONGLONG PatternEnd, PCSTR Step)
{
    LARGE_INTEGER Position, FileSize;
    ULONGLONG Offset = 0;
    DWORD Read, Index;

    ok(GetFileSizeEx(File, &FileSize) && (ULONGLONG)FileSize.QuadPart == Size,
       "%s: size %I64d, expected %I64u\n", Step, FileSize.QuadPart, Size);
    Position.QuadPart = 0;
    if (!SetFilePointerEx(File, Position, NULL, FILE_BEGIN))
        return;
    while (Offset < Size)
    {
        if (!ReadFile(File, ReadBuffer, sizeof(ReadBuffer), &Read, NULL) || Read == 0)
        {
            ok(0, "%s: read at %I64u failed: %lu\n", Step, Offset, GetLastError());
            return;
        }
        for (Index = 0; Index < Read; Index++, Offset++)
        {
            UCHAR Expected = Offset < PatternEnd ? PatternByte(Offset) : 0;

            if (ReadBuffer[Index] != Expected)
            {
                ok(0, "%s: byte %I64u is 0x%02x, expected 0x%02x\n", Step, Offset,
                   ReadBuffer[Index], Expected);
                return;
            }
        }
    }
    ok(Offset == Size, "%s: read %I64u bytes, expected %I64u\n", Step, Offset, Size);
}

static VOID
CheckBaseRecord(HANDLE File)
{
    static UCHAR Buffer[sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) + 4096];
    PNTFS_FILE_RECORD_OUTPUT_BUFFER Output = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)Buffer;
    NTFS_FILE_RECORD_INPUT_BUFFER Input;
    BY_HANDLE_FILE_INFORMATION Info;
    BOOL HasList = FALSE, HasData = FALSE;
    ULONG Offset;
    HANDLE Volume;
    DWORD Returned;

    if (!GetFileInformationByHandle(File, &Info))
    {
        ok(0, "GetFileInformationByHandle failed: %lu\n", GetLastError());
        return;
    }
    Volume = CreateFileW(VolumeDevice, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    ok(Volume != INVALID_HANDLE_VALUE, "Volume open failed: %lu\n", GetLastError());
    if (Volume == INVALID_HANDLE_VALUE)
        return;
    Input.FileReferenceNumber.HighPart = Info.nFileIndexHigh & 0xFFFF;
    Input.FileReferenceNumber.LowPart = Info.nFileIndexLow;
    if (!DeviceIoControl(Volume, FSCTL_GET_NTFS_FILE_RECORD, &Input, sizeof(Input),
                         Buffer, sizeof(Buffer), &Returned, NULL))
    {
        ok(0, "FSCTL_GET_NTFS_FILE_RECORD failed: %lu\n", GetLastError());
        CloseHandle(Volume);
        return;
    }
    CloseHandle(Volume);

    Offset = *(PUSHORT)(Output->FileRecordBuffer + 0x14);
    while (Offset + 8 <= Output->FileRecordLength)
    {
        ULONG Type = *(PULONG)(Output->FileRecordBuffer + Offset);
        ULONG Length = *(PULONG)(Output->FileRecordBuffer + Offset + 4);

        if (Type == ATTR_END || Length == 0)
            break;
        if (Type == ATTR_ATTRIBUTE_LIST)
            HasList = TRUE;
        else if (Type == ATTR_DATA)
            HasData = TRUE;
        Offset += Length;
    }
    ok(HasList, "Base record has no $ATTRIBUTE_LIST\n");
    ok(!HasData, "Base record still holds $DATA\n");
}

static VOID
FragmentVolume(PULONG Holes)
{
    ULARGE_INTEGER FreeBytes;
    LARGE_INTEGER FillerSize;
    DWORD SectorsPerCluster, BytesPerSector, FreeClusters, TotalClusters, Written;
    ULONG ClusterSize, Index, Created = 0;
    WCHAR Name[32];
    HANDLE File;
    PUCHAR Cluster;

    *Holes = 0;
    if (!GetDiskFreeSpaceW(VolumeRoot, &SectorsPerCluster, &BytesPerSector, &FreeClusters,
                           &TotalClusters) ||
        !GetDiskFreeSpaceExW(VolumeRoot, &FreeBytes, NULL, NULL))
    {
        ok(0, "GetDiskFreeSpace failed: %lu\n", GetLastError());
        return;
    }
    ClusterSize = SectorsPerCluster * BytesPerSector;
    FillerSize.QuadPart = (LONGLONG)FreeBytes.QuadPart - (LONGLONG)(HOLE_FILES + 512) * ClusterSize;
    ok(FillerSize.QuadPart > 0, "Volume too small: %I64u bytes free\n", FreeBytes.QuadPart);
    if (FillerSize.QuadPart <= 0)
        return;

    File = CreateFileW(TestPath(L"filler.bin"), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Create filler failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(SetFilePointerEx(File, FillerSize, NULL, FILE_BEGIN) && SetEndOfFile(File),
       "Filler of %I64d bytes failed: %lu\n", FillerSize.QuadPart, GetLastError());
    CloseHandle(File);

    Cluster = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ClusterSize);
    if (!Cluster)
        return;
    memset(Cluster, 'C', ClusterSize);
    for (Index = 0; Index < HOLE_FILES; Index++)
    {
        BOOL Result;

        StringCchPrintfW(Name, ARRAYSIZE(Name), L"h%04lu.bin", Index);
        File = CreateFileW(TestPath(Name), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (File == INVALID_HANDLE_VALUE)
            break;
        Result = WriteFile(File, Cluster, ClusterSize, &Written, NULL) && Written == ClusterSize;
        CloseHandle(File);
        if (!Result)
        {
            DeleteFileW(TestPath(Name));
            break;
        }
        Created++;
    }
    HeapFree(GetProcessHeap(), 0, Cluster);

    for (Index = 0; Index < Created; Index += 2)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"h%04lu.bin", Index);
        if (DeleteFileW(TestPath(Name)))
            (*Holes)++;
    }
    trace("Cluster size %lu, %lu hole files, %lu holes\n", ClusterSize, Created, *Holes);
}

static VOID
Cleanup(PCWSTR Target)
{
    WCHAR Name[32];
    ULONG Index;

    DeleteFileW(Target);
    DeleteFileW(TestPath(L"filler.bin"));
    for (Index = 0; Index < HOLE_FILES; Index++)
    {
        StringCchPrintfW(Name, ARRAYSIZE(Name), L"h%04lu.bin", Index);
        DeleteFileW(TestPath(Name));
    }
    RemoveDirectoryW(TestDir);
}

START_TEST(FullBaseGrowth)
{
    WCHAR Target[MAX_PATH], ShortPath[MAX_PATH];
    PCWSTR ShortName;
    LARGE_INTEGER Position;
    ULONGLONG Size = 0, Half;
    ULONG Holes, Extents;
    HANDLE File;
    BOOL Result;

    if (!FindTestVolume())
    {
        skip("No NTFS volume labelled %ls\n", TEST_VOLUME_LABEL);
        return;
    }
    if (!CreateDirectoryW(TestDir, NULL))
    {
        skip("Cannot create %ls: %lu\n", TestDir, GetLastError());
        return;
    }

    StringCchCopyW(Target, ARRAYSIZE(Target), TestPath(L"transaction-log.db-wal"));

    FragmentVolume(&Holes);
    if (Holes < MIN_EXTENTS + REGROW_FRAMES)
    {
        skip("Only %lu holes on %ls\n", Holes, VolumeRoot);
        Cleanup(Target);
        return;
    }

    File = CreateFileW(Target, GENERIC_READ | GENERIC_WRITE | DELETE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Create target failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
    {
        Cleanup(Target);
        return;
    }
    /* The full base record needs the 8.3 alias, which Windows 11 does not generate on data volumes. */
    if (!GetShortPathNameW(Target, ShortPath, ARRAYSIZE(ShortPath)) ||
        !wcscmp(wcsrchr(ShortPath, L'\\') + 1, wcsrchr(Target, L'\\') + 1))
    {
        ok(SetFileShortNameW(File, L"TRANSA~1.DB-"), "SetFileShortName failed: %lu\n", GetLastError());
    }
    ShortName = GetShortPathNameW(Target, ShortPath, ARRAYSIZE(ShortPath))
        ? wcsrchr(ShortPath, L'\\') + 1
        : L"";
    ok(wcscmp(ShortName, wcsrchr(Target, L'\\') + 1) != 0, "No short name: %ls\n", ShortName);

    Result = AppendFrames(File, &Size, Holes - REGROW_FRAMES, MIN_EXTENTS);
    Extents = CountExtents(File);
    ok(Extents >= MIN_EXTENTS, "%lu extents after %I64u bytes\n", Extents, Size);
    trace("%lu extents after %I64u bytes\n", Extents, Size);
    if (Result)
    {
        CheckContent(File, Size, Size, "Grown");
        CheckBaseRecord(File);

        Half = (Size / 2) & ~(ULONGLONG)511;
        Position.QuadPart = (LONGLONG)Half;
        ok(SetFilePointerEx(File, Position, NULL, FILE_BEGIN) && SetEndOfFile(File),
           "Shrink to %I64u failed: %lu\n", Half, GetLastError());
        ok(FlushFileBuffers(File), "Flush after shrink failed: %lu\n", GetLastError());
        CheckContent(File, Half, Half, "Shrunk");

        Position.QuadPart = (LONGLONG)Half + 8192;
        ok(SetFilePointerEx(File, Position, NULL, FILE_BEGIN) && SetEndOfFile(File),
           "Grow to %I64d failed: %lu\n", Position.QuadPart, GetLastError());
        ok(FlushFileBuffers(File), "Flush after grow failed: %lu\n", GetLastError());
        CheckContent(File, Half + 8192, Half, "Regrown");

        Position.QuadPart = 0;
        ok(SetFilePointerEx(File, Position, NULL, FILE_BEGIN) && SetEndOfFile(File),
           "Truncate to 0 failed: %lu\n", GetLastError());
        ok(FlushFileBuffers(File), "Flush after truncate failed: %lu\n", GetLastError());
        Size = 0;
        if (AppendFrames(File, &Size, REGROW_FRAMES, 0))
            CheckContent(File, Size, Size, "Rewritten");
    }

    CloseHandle(File);
    Cleanup(Target);
}
