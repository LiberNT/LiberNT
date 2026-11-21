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
FindTestVolume(VOID)
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

    if (GetSystemDirectoryW(System, ARRAYSIZE(System)) == 0)
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
    CheckContents(PathB, 200, Buffer);
    ok(DeleteFileW(PathA), "Deleting %ls failed: %lu\n", PathA, GetLastError());
    ok(DeleteFileW(PathB), "Deleting %ls failed: %lu\n", PathB, GetLastError());
}

START_TEST(DirectIo)
{
    PUCHAR Buffer;

    if (!FindTestVolume())
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
    VirtualFree(Buffer, 0, MEM_RELEASE);
}
