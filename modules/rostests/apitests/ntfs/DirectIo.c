/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test unbuffered transfers that extend a file or span several of its runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winioctl.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"
#define CHUNK_SIZE (64 * 1024)
#define CHUNK_COUNT 8

static WCHAR VolumeRoot[] = L"?:\\";

static
BOOL
FindTestVolume(_In_ BOOL AllowSystemVolume)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR System[MAX_PATH];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            return TRUE;
        }
    }

    if (!AllowSystemVolume || GetSystemDirectoryW(System, ARRAYSIZE(System)) == 0)
        return FALSE;
    VolumeRoot[0] = System[0];
    return GetVolumeInformationW(VolumeRoot, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)) &&
           !wcscmp(FileSystem, L"NTFS");
}

static
VOID
Fill(
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG Seed)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        Buffer[Index] = (UCHAR)(Seed * 131 + Index * 7 + (Index >> 9));
}

static
BOOL
Matches(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _In_ ULONG Seed)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Buffer[Index] != (UCHAR)(Seed * 131 + Index * 7 + (Index >> 9)))
            return FALSE;
    }
    return TRUE;
}

static
VOID
CheckContents(
    _In_ PCWSTR Path,
    _In_ ULONG FirstSeed,
    _Inout_updates_bytes_(CHUNK_SIZE) PUCHAR Buffer)
{
    HANDLE File;
    DWORD Done;
    ULONG Chunk;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %ls failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    for (Chunk = 0; Chunk < CHUNK_COUNT; Chunk++)
    {
        ok(ReadFile(File, Buffer, CHUNK_SIZE, &Done, NULL) && Done == CHUNK_SIZE,
           "Reading chunk %lu of %ls returned %lu bytes, error %lu\n", Chunk, Path, Done, GetLastError());
        ok(Matches(Buffer, CHUNK_SIZE, FirstSeed + Chunk), "Chunk %lu of %ls holds the wrong data\n", Chunk, Path);
    }
    ok(ReadFile(File, Buffer, CHUNK_SIZE, &Done, NULL) && Done == 0, "Read past the end returned %lu bytes\n", Done);
    CloseHandle(File);
}

static
VOID
TestExtendingWrites(
    _Inout_updates_bytes_(CHUNK_SIZE * CHUNK_COUNT) PUCHAR Buffer)
{
    WCHAR Path[MAX_PATH];
    LARGE_INTEGER Size;
    HANDLE File;
    DWORD Done;
    ULONG Chunk;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sdirectio_extend.dat", VolumeRoot);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Creating %ls failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    for (Chunk = 0; Chunk < CHUNK_COUNT; Chunk++)
    {
        Fill(Buffer, CHUNK_SIZE, Chunk);
        ok(WriteFile(File, Buffer, CHUNK_SIZE, &Done, NULL) && Done == CHUNK_SIZE,
           "Extending write %lu wrote %lu bytes, error %lu\n", Chunk, Done, GetLastError());
        ok(GetFileSizeEx(File, &Size) && Size.QuadPart == (LONGLONG)(Chunk + 1) * CHUNK_SIZE,
           "Size after write %lu is %I64d\n", Chunk, Size.QuadPart);
    }
    CloseHandle(File);

    CheckContents(Path, 0, Buffer);
    ok(DeleteFileW(Path), "Deleting %ls failed: %lu\n", Path, GetLastError());
}

static
DWORD
CountExtents(
    _In_ HANDLE File)
{
    STARTING_VCN_INPUT_BUFFER Start = { 0 };
    UCHAR Output[sizeof(RETRIEVAL_POINTERS_BUFFER) + 32 * 2 * sizeof(LARGE_INTEGER)];
    PRETRIEVAL_POINTERS_BUFFER Pointers = (PRETRIEVAL_POINTERS_BUFFER)Output;
    DWORD Returned;

    if (!DeviceIoControl(File, FSCTL_GET_RETRIEVAL_POINTERS, &Start, sizeof(Start), Output, sizeof(Output), &Returned, NULL))
        return 0;
    return Pointers->ExtentCount;
}

static
VOID
TestFragmentedTransfers(
    _Inout_updates_bytes_(CHUNK_SIZE * CHUNK_COUNT) PUCHAR Buffer)
{
    WCHAR PathA[MAX_PATH], PathB[MAX_PATH];
    HANDLE FileA, FileB;
    DWORD Done, Extents;
    ULONG Chunk;

    StringCchPrintfW(PathA, ARRAYSIZE(PathA), L"%sdirectio_a.dat", VolumeRoot);
    StringCchPrintfW(PathB, ARRAYSIZE(PathB), L"%sdirectio_b.dat", VolumeRoot);
    FileA = CreateFileW(PathA, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING, NULL);
    FileB = CreateFileW(PathB, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING, NULL);
    ok(FileA != INVALID_HANDLE_VALUE && FileB != INVALID_HANDLE_VALUE, "Creating the interleaved files failed: %lu\n", GetLastError());
    if (FileA == INVALID_HANDLE_VALUE || FileB == INVALID_HANDLE_VALUE)
    {
        if (FileA != INVALID_HANDLE_VALUE)
            CloseHandle(FileA);
        if (FileB != INVALID_HANDLE_VALUE)
            CloseHandle(FileB);
        return;
    }

    for (Chunk = 0; Chunk < CHUNK_COUNT; Chunk++)
    {
        Fill(Buffer, CHUNK_SIZE, 100 + Chunk);
        ok(WriteFile(FileA, Buffer, CHUNK_SIZE, &Done, NULL) && Done == CHUNK_SIZE, "Writing A chunk %lu failed: %lu\n", Chunk, GetLastError());
        Fill(Buffer, CHUNK_SIZE, 200 + Chunk);
        ok(WriteFile(FileB, Buffer, CHUNK_SIZE, &Done, NULL) && Done == CHUNK_SIZE, "Writing B chunk %lu failed: %lu\n", Chunk, GetLastError());
    }
    CloseHandle(FileB);

    Extents = CountExtents(FileA);
    trace("%ls has %lu extents\n", PathA, Extents);

    ok(SetFilePointer(FileA, 0, NULL, FILE_BEGIN) == 0, "Seeking A failed: %lu\n", GetLastError());
    ok(ReadFile(FileA, Buffer, CHUNK_SIZE * CHUNK_COUNT, &Done, NULL) && Done == CHUNK_SIZE * CHUNK_COUNT,
       "Reading A in one transfer returned %lu bytes, error %lu\n", Done, GetLastError());
    for (Chunk = 0; Chunk < CHUNK_COUNT; Chunk++)
        ok(Matches(Buffer + Chunk * CHUNK_SIZE, CHUNK_SIZE, 100 + Chunk), "One-transfer read: chunk %lu of A holds the wrong data\n", Chunk);

    for (Chunk = 0; Chunk < CHUNK_COUNT; Chunk++)
        Fill(Buffer + Chunk * CHUNK_SIZE, CHUNK_SIZE, 300 + Chunk);
    ok(SetFilePointer(FileA, 0, NULL, FILE_BEGIN) == 0, "Seeking A failed: %lu\n", GetLastError());
    ok(WriteFile(FileA, Buffer, CHUNK_SIZE * CHUNK_COUNT, &Done, NULL) && Done == CHUNK_SIZE * CHUNK_COUNT,
       "Writing A in one transfer wrote %lu bytes, error %lu\n", Done, GetLastError());
    CloseHandle(FileA);

    CheckContents(PathA, 300, Buffer);
    FileA = CreateFileW(PathA, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
    ok(FileA != INVALID_HANDLE_VALUE, "reopening fragmented file: %lu\n", GetLastError());
    if (FileA != INVALID_HANDLE_VALUE)
    {
        for (Chunk = 0; Chunk < CHUNK_COUNT; ++Chunk)
            Fill(Buffer + Chunk * CHUNK_SIZE, CHUNK_SIZE, 500 + Chunk);
        ok(SetFilePointer(FileA, 1, NULL, FILE_BEGIN) == 1 &&
           WriteFile(FileA, Buffer + 1, CHUNK_SIZE * CHUNK_COUNT - 2, &Done, NULL) && Done == CHUNK_SIZE * CHUNK_COUNT - 2,
           "unaligned write over multiple extents: %lu\n", GetLastError());
        CloseHandle(FileA);
        FileA = CreateFileW(PathA, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
        ok(FileA != INVALID_HANDLE_VALUE, "reopening fragmented file for verification: %lu\n", GetLastError());
        if (FileA != INVALID_HANDLE_VALUE)
        {
            ok(ReadFile(FileA, Buffer, CHUNK_SIZE * CHUNK_COUNT, &Done, NULL) && Done == CHUNK_SIZE * CHUNK_COUNT,
               "reading fragmented data: %lu\n", GetLastError());
            ok(Buffer[0] == (UCHAR)(300 * 131), "first neighbor changed\n");
            ok(Buffer[CHUNK_SIZE * CHUNK_COUNT - 1] == (UCHAR)((300 + CHUNK_COUNT - 1) * 131 +
               (CHUNK_SIZE - 1) * 7 + ((CHUNK_SIZE - 1) >> 9)), "last neighbor changed\n");
            Buffer[0] = (UCHAR)(500 * 131);
            Buffer[CHUNK_SIZE * CHUNK_COUNT - 1] = (UCHAR)((500 + CHUNK_COUNT - 1) * 131 +
                                                    (CHUNK_SIZE - 1) * 7 + ((CHUNK_SIZE - 1) >> 9));
            for (Chunk = 0; Chunk < CHUNK_COUNT; ++Chunk)
                ok(Matches(Buffer + Chunk * CHUNK_SIZE, CHUNK_SIZE, 500 + Chunk), "fragmented payload chunk %lu\n", Chunk);
            CloseHandle(FileA);
        }
    }

    CheckContents(PathB, 200, Buffer);
    ok(DeleteFileW(PathA), "Deleting %ls failed: %lu\n", PathA, GetLastError());
    ok(DeleteFileW(PathB), "Deleting %ls failed: %lu\n", PathB, GetLastError());
}

static
BOOL
IsZero(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Buffer[Index] != 0)
            return FALSE;
    }
    return TRUE;
}

static
VOID
CheckMapped(
    _In_ PCWSTR Path,
    _In_ ULONG ValidLength,
    _In_ ULONG Size,
    _In_ ULONG Seed)
{
    HANDLE File, Mapping;
    PUCHAR View;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %ls failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Mapping = CreateFileMappingW(File, NULL, PAGE_READONLY, 0, 0, NULL);
    ok(Mapping != NULL, "Mapping %ls failed: %lu\n", Path, GetLastError());
    if (Mapping)
    {
        View = MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
        ok(View != NULL, "Viewing %ls failed: %lu\n", Path, GetLastError());
        if (View)
        {
            ok(Matches(View, ValidLength, Seed), "The mapped valid data of %ls is wrong\n", Path);
            ok(IsZero(View + ValidLength, ((Size + 4095) & ~4095UL) - ValidLength),
               "%ls maps nonzero bytes past its valid data\n", Path);
            UnmapViewOfFile(View);
        }
        CloseHandle(Mapping);
    }
    CloseHandle(File);
}

/* What lies on the device past the valid data length is stale, so a read across it must return zeros there */
static
VOID
TestPastValidData(
    _Inout_updates_bytes_(CHUNK_SIZE * CHUNK_COUNT) PUCHAR Buffer)
{
    WCHAR Path[MAX_PATH];
    FILE_END_OF_FILE_INFO EndOfFile;
    LARGE_INTEGER Offset;
    HANDLE File;
    DWORD Done;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sdirectio_vdl.dat", VolumeRoot);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Creating %ls failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Fill(Buffer, 2 * CHUNK_SIZE, 400);
    ok(WriteFile(File, Buffer, 2 * CHUNK_SIZE, &Done, NULL) && Done == 2 * CHUNK_SIZE,
       "Writing %ls wrote %lu bytes, error %lu\n", Path, Done, GetLastError());
    EndOfFile.EndOfFile.QuadPart = 1000;
    ok(SetFileInformationByHandle(File, FileEndOfFileInfo, &EndOfFile, sizeof(EndOfFile)),
       "Truncating %ls failed: %lu\n", Path, GetLastError());

    Offset.QuadPart = 0;
    FillMemory(Buffer, CHUNK_SIZE, 0xCC);
    ok(SetFilePointerEx(File, Offset, NULL, FILE_BEGIN), "Seeking %ls failed: %lu\n", Path, GetLastError());
    ok(ReadFile(File, Buffer, CHUNK_SIZE, &Done, NULL), "Reading past the end of %ls failed: %lu\n", Path, GetLastError());
    ok_eq_ulong(Done, 1000UL);
    ok(Matches(Buffer, 1000, 400), "The data before the end of %ls is wrong\n", Path);
    CloseHandle(File);
    CheckMapped(Path, 1000, 1000, 400);

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Reopening %ls failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    EndOfFile.EndOfFile.QuadPart = 2 * CHUNK_SIZE;
    ok(SetFileInformationByHandle(File, FileEndOfFileInfo, &EndOfFile, sizeof(EndOfFile)),
       "Extending %ls failed: %lu\n", Path, GetLastError());

    Offset.QuadPart = 0;
    FillMemory(Buffer, 2 * CHUNK_SIZE, 0xCC);
    ok(SetFilePointerEx(File, Offset, NULL, FILE_BEGIN), "Seeking %ls failed: %lu\n", Path, GetLastError());
    ok(ReadFile(File, Buffer, 2 * CHUNK_SIZE, &Done, NULL), "Reading across the valid data of %ls failed: %lu\n",
       Path, GetLastError());
    ok_eq_ulong(Done, (ULONG)(2 * CHUNK_SIZE));
    ok(Matches(Buffer, 1000, 400), "The valid data of %ls is wrong\n", Path);
    ok(IsZero(Buffer + 1000, 2 * CHUNK_SIZE - 1000), "%ls reads nonzero bytes past its valid data\n", Path);
    CloseHandle(File);
    CheckMapped(Path, 1000, 2 * CHUNK_SIZE, 400);

    ok(DeleteFileW(Path), "Deleting %ls failed: %lu\n", Path, GetLastError());
}

START_TEST(DirectIo)
{
    PUCHAR Buffer;

    if (!FindTestVolume(TRUE))
    {
        skip("No NTFS volume to test on\n");
        return;
    }

    Buffer = VirtualAlloc(NULL, CHUNK_SIZE * CHUNK_COUNT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Buffer != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (!Buffer)
        return;

    TestExtendingWrites(Buffer);
    TestFragmentedTransfers(Buffer);
    TestPastValidData(Buffer);
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

#define WRITE_WAIT_MS 10000
#define THROUGH_FILE_SIZE (3 * 1024 * 1024)

typedef struct
{
    HANDLE File;
    PUCHAR Data;
    DWORD Length;
} TEST_WRITE;

static DWORD WINAPI
WriteTestData(PVOID Parameter)
{
    TEST_WRITE *Write = Parameter;
    DWORD Done;

    if (!WriteFile(Write->File, Write->Data, Write->Length, &Done, NULL))
        return GetLastError();
    return Done == Write->Length ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}

static BOOL
WriteAt(HANDLE File, DWORD Offset, PUCHAR Data, DWORD Length)
{
    DWORD Done;

    return SetFilePointer(File, Offset, NULL, FILE_BEGIN) == Offset &&
           WriteFile(File, Data, Length, &Done, NULL) && Done == Length;
}

static VOID
WriteUnalignedRanges(HANDLE File, PUCHAR Expected, PUCHAR Data)
{
    static const struct { DWORD Offset, Length; } Ranges[] =
    {
        {0x583c4, 65536}, {511, 1}, {513, 13}, {1024, 700}, {2047, 1025}, {0x583c4, 2 * 1024 * 1024}
    };
    DWORD Index;

    for (Index = 0; Index < ARRAYSIZE(Ranges); ++Index)
    {
        Fill(Data, Ranges[Index].Length, 87 + Index);
        ok(WriteAt(File, Ranges[Index].Offset, Data, Ranges[Index].Length), "range %lu failed: %lu\n",
           Index, GetLastError());
        memcpy(Expected + Ranges[Index].Offset, Data, Ranges[Index].Length);
    }
}

static VOID
RaceSameSector(PCWSTR Path, HANDLE File, PUCHAR Expected, PUCHAR Data)
{
    TEST_WRITE Write = { File, Data, 127 };
    HANDLE Second;
    HANDLE Thread;
    DWORD Error;
    DWORD Base;
    DWORD Index;

    Second = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_FLAG_WRITE_THROUGH, NULL);
    ok(Second != INVALID_HANDLE_VALUE, "opening second writer: %lu\n", GetLastError());
    if (Second == INVALID_HANDLE_VALUE)
        return;
    for (Index = 0; Index < 4; ++Index)
    {
        Base = 16384 + Index * 4096;
        Fill(Data, 256, 211 + Index);
        if (SetFilePointer(File, Base + 19, NULL, FILE_BEGIN) != Base + 19)
        {
            ok(FALSE, "seeking first writer: %lu\n", GetLastError());
            break;
        }
        Thread = CreateThread(NULL, 0, WriteTestData, &Write, 0, NULL);
        ok(Thread != NULL, "starting same-sector writer: %lu\n", GetLastError());
        if (!Thread)
            break;
        ok(WriteAt(Second, Base + 321, Data + 129, 127), "second writer: %lu\n", GetLastError());
        Error = ERROR_TIMEOUT;
        ok(WaitForSingleObject(Thread, WRITE_WAIT_MS) == WAIT_OBJECT_0, "first writer did not finish\n");
        GetExitCodeThread(Thread, &Error);
        CloseHandle(Thread);
        ok(Error == ERROR_SUCCESS, "first writer: %lu\n", Error);
        memcpy(Expected + Base + 19, Data, 127);
        memcpy(Expected + Base + 321, Data + 129, 127);
    }
    CloseHandle(Second);
}

static VOID
CheckPersisted(PCWSTR Path, const UCHAR *Expected, PUCHAR Data, DWORD Length)
{
    LARGE_INTEGER Size;
    HANDLE File;
    DWORD Done;

    File = CreateFileW(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopening file: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(GetFileSizeEx(File, &Size) && Size.QuadPart == Length, "wrong size on reopen\n");
    ok(ReadFile(File, Data, Length, &Done, NULL) && Done == Length, "reading persisted data: %lu\n", GetLastError());
    ok(!memcmp(Expected, Data, Length), "persisted payload or neighboring bytes differ\n");
    CloseHandle(File);
}

static VOID
TestUnalignedWriteThrough(PCWSTR Path, PUCHAR Expected, PUCHAR Data)
{
    HANDLE File;
    DWORD Done;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_WRITE_THROUGH, NULL);
    ok(File != INVALID_HANDLE_VALUE, "opening write-through file: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Fill(Expected, THROUGH_FILE_SIZE, 17);
    if (!WriteFile(File, Expected, THROUGH_FILE_SIZE, &Done, NULL) || Done != THROUGH_FILE_SIZE || !FlushFileBuffers(File))
    {
        ok(FALSE, "initializing test file: %lu\n", GetLastError());
        CloseHandle(File);
        return;
    }
    WriteUnalignedRanges(File, Expected, Data + 1);
    RaceSameSector(Path, File, Expected, Data + 1);
    CloseHandle(File);
    CheckPersisted(Path, Expected, Data, THROUGH_FILE_SIZE);
}

START_TEST(UnalignedWriteThrough)
{
    WCHAR Path[MAX_PATH];
    PUCHAR Expected;
    PUCHAR Data;

    if (!FindTestVolume(FALSE))
    {
        skip("No NTFSTEST volume\n");
        return;
    }
    if (!GetTempFileNameW(VolumeRoot, L"wt", 0, Path))
    {
        ok(FALSE, "creating test file: %lu\n", GetLastError());
        return;
    }
    Expected = VirtualAlloc(NULL, THROUGH_FILE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Data = VirtualAlloc(NULL, THROUGH_FILE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Expected && Data, "allocating buffers: %lu\n", GetLastError());
    if (Expected && Data)
        TestUnalignedWriteThrough(Path, Expected, Data);
    if (Data)
        VirtualFree(Data, 0, MEM_RELEASE);
    if (Expected)
        VirtualFree(Expected, 0, MEM_RELEASE);
    DeleteFileW(Path);
}

static VOID
CheckPromoted(PCWSTR Path, DWORD Offset, DWORD Length, PUCHAR Data)
{
    LARGE_INTEGER Size;
    HANDLE File;
    DWORD Done;

    File = CreateFileW(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    ok(File != INVALID_HANDLE_VALUE && GetFileSizeEx(File, &Size) && Size.QuadPart == Offset + Length,
       "wrong EOF on reopen\n");
    if (File == INVALID_HANDLE_VALUE)
        return;
    memset(Data, 0xcc, Offset + Length);
    ok(ReadFile(File, Data, Offset + Length, &Done, NULL) && Done == Offset + Length, "reading promoted file: %lu\n",
       GetLastError());
    if (Offset)
        ok(Matches(Data, 137, 5) && IsZero(Data + 137, Offset - 137), "resident prefix or gap changed\n");
    ok(Matches(Data + Offset, Length, 23), "promoted data differs\n");
    CloseHandle(File);
}

static VOID
TestResidentWrite(_In_ PCWSTR Path, _In_ DWORD Offset, _In_ DWORD Length, _In_ DWORD Flags, _Inout_ PUCHAR Data)
{
    HANDLE File;
    DWORD Done;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
    ok(File != INVALID_HANDLE_VALUE, "creating resident file: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    if (Offset)
    {
        Fill(Data, 137, 5);
        ok(WriteFile(File, Data, 137, &Done, NULL) && Done == 137, "seeding resident prefix: %lu\n", GetLastError());
    }
    CloseHandle(File);
    File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | Flags, NULL);
    ok(File != INVALID_HANDLE_VALUE, "opening unbuffered writer: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Fill(Data, Length, 23);
    ok(WriteAt(File, Offset, Data, Length), "first write at %lu with flags %#lx failed: %lu\n", Offset, Flags,
       GetLastError());
    CloseHandle(File);
    CheckPromoted(Path, Offset, Length, Data);
}

START_TEST(ResidentWrite)
{
    DWORD Sectors, SectorSize, Free, Total, Length, Offset;
    WCHAR Path[MAX_PATH];
    PUCHAR Data;

    if (!FindTestVolume(FALSE))
    {
        skip("No NTFSTEST volume\n");
        return;
    }
    if (!GetDiskFreeSpaceW(VolumeRoot, &Sectors, &SectorSize, &Free, &Total))
    {
        ok(FALSE, "reading sector size: %lu\n", GetLastError());
        return;
    }
    Length = 64 * 1024 + 3 * SectorSize;
    Offset = max(4096UL, SectorSize);
    if (!GetTempFileNameW(VolumeRoot, L"rp", 0, Path))
    {
        ok(FALSE, "creating test file: %lu\n", GetLastError());
        return;
    }
    Data = VirtualAlloc(NULL, Length + Offset, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Data != NULL, "allocating aligned buffer: %lu\n", GetLastError());
    if (Data)
    {
        TestResidentWrite(Path, 0, Length, FILE_FLAG_WRITE_THROUGH, Data);
        TestResidentWrite(Path, Offset, Length, FILE_FLAG_WRITE_THROUGH, Data);
        TestResidentWrite(Path, Offset, Length, 0, Data);
        VirtualFree(Data, 0, MEM_RELEASE);
    }
    DeleteFileW(Path);
}
