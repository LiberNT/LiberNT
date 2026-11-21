/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test writes at the end of a file through append-only and end-of-file offsets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"
#define LINE_SIZE 128
#define HEAD_SIZE 100

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
FillLine(
    _Out_writes_(LINE_SIZE) PUCHAR Line,
    _In_ ULONG Index)
{
    ULONG i;

    for (i = 0; i < LINE_SIZE; i++)
        Line[i] = (UCHAR)(Index * 7 + i);
}

static
BOOL
ReadAt(
    _In_ HANDLE File,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Done = 0;

    Overlapped.Offset = Offset;
    return ReadFile(File, Buffer, Length, &Done, &Overlapped) && Done == Length;
}

static
LONGLONG
SizeOf(
    _In_ HANDLE File)
{
    LARGE_INTEGER Size;

    if (!GetFileSizeEx(File, &Size))
        return -1;
    return Size.QuadPart;
}

static
LONGLONG
PointerOf(
    _In_ HANDLE File)
{
    LARGE_INTEGER Zero;
    LARGE_INTEGER Position;

    Zero.QuadPart = 0;
    if (!SetFilePointerEx(File, Zero, &Position, FILE_CURRENT))
        return -1;
    return Position.QuadPart;
}

static
VOID
TestAppendOnlyHandle(
    _In_ PCWSTR Path)
{
    UCHAR Head[HEAD_SIZE];
    UCHAR Line[LINE_SIZE];
    UCHAR Read[LINE_SIZE];
    HANDLE File;
    HANDLE Append;
    HANDLE Reader;
    DWORD Done;
    ULONG i;

    memset(Head, 0x41, sizeof(Head));
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "create failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(File, Head, sizeof(Head), &Done, NULL) && Done == sizeof(Head), "head write failed: %lu\n", GetLastError());
    CloseHandle(File);

    Reader = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Reader != INVALID_HANDLE_VALUE, "reader open failed: %lu\n", GetLastError());
    ok(ReadAt(Reader, 0, Read, HEAD_SIZE), "head read failed: %lu\n", GetLastError());
    ok(!memcmp(Read, Head, HEAD_SIZE), "head mismatch\n");

    Append = CreateFileW(Path, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(Append != INVALID_HANDLE_VALUE, "append open failed: %lu\n", GetLastError());
    if (Append == INVALID_HANDLE_VALUE)
    {
        CloseHandle(Reader);
        return;
    }
    ok_eq_longlong(PointerOf(Append), 0);

    for (i = 0; i < 10; i++)
    {
        FillLine(Line, i);
        Done = 0;
        ok(WriteFile(Append, Line, LINE_SIZE, &Done, NULL), "append %lu failed: %lu\n", i, GetLastError());
        ok_long(Done, LINE_SIZE);
        ok_eq_longlong(SizeOf(Append), HEAD_SIZE + (i + 1) * LINE_SIZE);
        ok_eq_longlong(PointerOf(Append), HEAD_SIZE + (i + 1) * LINE_SIZE);
    }

    for (i = 0; i < 10; i++)
    {
        FillLine(Line, i);
        memset(Read, 0, sizeof(Read));
        ok(ReadAt(Reader, HEAD_SIZE + i * LINE_SIZE, Read, LINE_SIZE), "read of line %lu failed: %lu\n", i, GetLastError());
        ok(!memcmp(Read, Line, LINE_SIZE), "line %lu mismatch through the open reader\n", i);
    }
    ok(ReadAt(Reader, 0, Read, HEAD_SIZE), "head reread failed: %lu\n", GetLastError());
    ok(!memcmp(Read, Head, HEAD_SIZE), "head changed by the appends\n");

    CloseHandle(Append);
    CloseHandle(Reader);
}

static
VOID
TestEndOfFileOffset(
    _In_ PCWSTR Path)
{
    OVERLAPPED Overlapped = { 0 };
    UCHAR Line[LINE_SIZE];
    UCHAR Read[LINE_SIZE];
    HANDLE File;
    LONGLONG Size;
    DWORD Done;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "open failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Size = SizeOf(File);
    ok_eq_longlong(Size, HEAD_SIZE + 10 * LINE_SIZE);
    FillLine(Line, 20);
    Overlapped.Offset = 0xFFFFFFFF;
    Overlapped.OffsetHigh = 0xFFFFFFFF;
    Done = 0;
    ok(WriteFile(File, Line, LINE_SIZE, &Done, &Overlapped), "end-of-file write failed: %lu\n", GetLastError());
    ok_long(Done, LINE_SIZE);
    ok_eq_longlong(SizeOf(File), Size + LINE_SIZE);
    ok_eq_longlong(PointerOf(File), Size + LINE_SIZE);
    ok(ReadAt(File, (ULONG)Size, Read, LINE_SIZE), "read back failed: %lu\n", GetLastError());
    ok(!memcmp(Read, Line, LINE_SIZE), "end-of-file write landed elsewhere\n");

    FillLine(Line, 21);
    Overlapped.Offset = 0;
    Overlapped.OffsetHigh = 0;
    ok(WriteFile(File, Line, LINE_SIZE, &Done, &Overlapped), "overwrite failed: %lu\n", GetLastError());
    ok_eq_longlong(SizeOf(File), Size + LINE_SIZE);
    ok(ReadAt(File, 0, Read, LINE_SIZE), "overwrite read back failed: %lu\n", GetLastError());
    ok(!memcmp(Read, Line, LINE_SIZE), "overwrite at offset 0 mismatch\n");
    CloseHandle(File);
}

static
VOID
TestInterleavedAppenders(
    _In_ PCWSTR Path)
{
    UCHAR Line[LINE_SIZE];
    UCHAR Read[LINE_SIZE];
    HANDLE First;
    HANDLE Second;
    HANDLE Reader;
    DWORD Done;
    ULONG i;

    First = CreateFileW(Path, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(First != INVALID_HANDLE_VALUE, "first appender failed: %lu\n", GetLastError());
    Second = CreateFileW(Path, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    ok(Second != INVALID_HANDLE_VALUE, "second appender failed: %lu\n", GetLastError());
    if (First == INVALID_HANDLE_VALUE || Second == INVALID_HANDLE_VALUE)
    {
        if (First != INVALID_HANDLE_VALUE)
            CloseHandle(First);
        if (Second != INVALID_HANDLE_VALUE)
            CloseHandle(Second);
        return;
    }

    for (i = 0; i < 64; i++)
    {
        FillLine(Line, 100 + i);
        Done = 0;
        ok(WriteFile((i & 1) ? Second : First, Line, LINE_SIZE, &Done, NULL), "append %lu failed: %lu\n", i, GetLastError());
        ok_long(Done, LINE_SIZE);
    }
    ok_eq_longlong(SizeOf(First), 64 * LINE_SIZE);
    ok_eq_longlong(SizeOf(Second), 64 * LINE_SIZE);
    ok_eq_longlong(PointerOf(First), 63 * LINE_SIZE);
    ok_eq_longlong(PointerOf(Second), 64 * LINE_SIZE);
    CloseHandle(First);
    CloseHandle(Second);

    Reader = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(Reader != INVALID_HANDLE_VALUE, "reader open failed: %lu\n", GetLastError());
    if (Reader == INVALID_HANDLE_VALUE)
        return;
    for (i = 0; i < 64; i++)
    {
        FillLine(Line, 100 + i);
        memset(Read, 0, sizeof(Read));
        ok(ReadAt(Reader, i * LINE_SIZE, Read, LINE_SIZE), "read of line %lu failed: %lu\n", i, GetLastError());
        ok(!memcmp(Read, Line, LINE_SIZE), "interleaved line %lu mismatch\n", i);
    }
    CloseHandle(Reader);
}

static
VOID
TestAppendOnlyFlushReachesDisk(
    _In_ PCWSTR Path)
{
    UCHAR Line[LINE_SIZE];
    PUCHAR Disk;
    HANDLE Append;
    HANDLE Raw;
    DWORD Done;
    ULONG i;

    Append = CreateFileW(Path, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Append != INVALID_HANDLE_VALUE, "append create failed: %lu\n", GetLastError());
    if (Append == INVALID_HANDLE_VALUE)
        return;

    for (i = 0; i < 40; i++)
    {
        FillLine(Line, 200 + i);
        Done = 0;
        ok(WriteFile(Append, Line, LINE_SIZE, &Done, NULL), "append %lu failed: %lu\n", i, GetLastError());
        ok_long(Done, LINE_SIZE);
    }
    ok(FlushFileBuffers(Append), "flush through the append-only handle failed: %lu\n", GetLastError());
    ok_eq_longlong(SizeOf(Append), 40 * LINE_SIZE);
    CloseHandle(Append);

    Disk = VirtualAlloc(NULL, 8192, MEM_COMMIT, PAGE_READWRITE);
    ok(Disk != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    Raw = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    ok(Raw != INVALID_HANDLE_VALUE, "unbuffered open failed: %lu\n", GetLastError());
    if (Disk == NULL || Raw == INVALID_HANDLE_VALUE)
    {
        if (Raw != INVALID_HANDLE_VALUE)
            CloseHandle(Raw);
        if (Disk != NULL)
            VirtualFree(Disk, 0, MEM_RELEASE);
        return;
    }

    ok_eq_longlong(SizeOf(Raw), 40 * LINE_SIZE);
    Done = 0;
    ok(ReadFile(Raw, Disk, 8192, &Done, NULL), "unbuffered read failed: %lu\n", GetLastError());
    ok_long(Done, 40 * LINE_SIZE);
    for (i = 0; i < 40; i++)
    {
        FillLine(Line, 200 + i);
        ok(!memcmp(Disk + i * LINE_SIZE, Line, LINE_SIZE), "line %lu on disk differs after the flush\n", i);
    }
    CloseHandle(Raw);
    VirtualFree(Disk, 0, MEM_RELEASE);
}

START_TEST(AppendWrite)
{
    WCHAR Path[MAX_PATH];
    WCHAR Path2[MAX_PATH];
    WCHAR Path3[MAX_PATH];

    if (!FindTestVolume())
    {
        skip("No NTFS volume\n");
        return;
    }

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sAppendWrite.tmp", VolumeRoot);
    StringCchPrintfW(Path2, ARRAYSIZE(Path2), L"%sAppendWrite2.tmp", VolumeRoot);
    StringCchPrintfW(Path3, ARRAYSIZE(Path3), L"%sAppendWrite3.tmp", VolumeRoot);

    TestAppendOnlyHandle(Path);
    TestEndOfFileOffset(Path);
    TestInterleavedAppenders(Path2);
    TestAppendOnlyFlushReachesDisk(Path3);

    ok(DeleteFileW(Path), "delete failed: %lu\n", GetLastError());
    ok(DeleteFileW(Path2), "delete failed: %lu\n", GetLastError());
    ok(DeleteFileW(Path3), "delete failed: %lu\n", GetLastError());
}
