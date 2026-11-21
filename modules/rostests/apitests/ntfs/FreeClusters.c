/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The free space NTFS reports follows every allocation and release and matches a fresh count after a remount
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <winioctl.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"
#define TEST_CLUSTERS 64

static WCHAR VolumeRoot[] = L"?:\\";
static WCHAR VolumeDevice[] = L"\\\\.\\?:";

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
            return TRUE;
        }
    }
    return FALSE;
}

static ULONGLONG
FreeClusters(_Out_opt_ PULONG ClusterBytes)
{
    DWORD SectorsPerCluster = 0, BytesPerSector = 0, Free = 0, Total = 0;

    ok(GetDiskFreeSpaceW(VolumeRoot, &SectorsPerCluster, &BytesPerSector, &Free, &Total),
       "GetDiskFreeSpaceW failed: %lu\n", GetLastError());
    if (ClusterBytes)
        *ClusterBytes = SectorsPerCluster * BytesPerSector;
    return Free;
}

static BOOL
SetAllocation(_In_ HANDLE File, _In_ LONGLONG Bytes)
{
    FILE_ALLOCATION_INFO Allocation;

    Allocation.AllocationSize.QuadPart = Bytes;
    return SetFileInformationByHandle(File, FileAllocationInfo, &Allocation, sizeof(Allocation));
}

static BOOL
Remount(VOID)
{
    DWORD Returned;
    HANDLE Volume;
    BOOL Done;

    Volume = CreateFileW(VolumeDevice, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        return FALSE;
    Done = DeviceIoControl(Volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &Returned, NULL) &&
           DeviceIoControl(Volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &Returned, NULL);
    CloseHandle(Volume);
    return Done;
}

START_TEST(FreeClusters)
{
    WCHAR Path[MAX_PATH];
    FILE_STANDARD_INFO Standard;
    ULONGLONG Before, Grown, Shrunk, Remounted;
    LARGE_INTEGER Position;
    DWORD Written;
    BYTE Marker = 0xa5;
    ULONG ClusterBytes;
    HANDLE File, Reader;

    if (!FindTestVolume())
    {
        skip("no NTFS volume labelled %ls\n", TEST_VOLUME_LABEL);
        return;
    }

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sFreeClusters.bin", VolumeRoot);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%ls) failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Before = FreeClusters(&ClusterBytes);
    ok(SetAllocation(File, (LONGLONG)TEST_CLUSTERS * ClusterBytes), "growing the allocation failed: %lu\n", GetLastError());
    ok(GetFileInformationByHandleEx(File, FileStandardInfo, &Standard, sizeof(Standard)),
       "FileStandardInfo failed: %lu\n", GetLastError());
    Grown = FreeClusters(NULL);
    ok(Before - Grown == (ULONGLONG)Standard.AllocationSize.QuadPart / ClusterBytes,
       "free clusters went from %I64u to %I64u for %I64u allocated bytes\n",
       Before, Grown, Standard.AllocationSize.QuadPart);

    Position.QuadPart = ClusterBytes + 2;
    ok(SetFilePointerEx(File, Position, NULL, FILE_BEGIN) && WriteFile(File, &Marker, 1, &Written, NULL) && Written == 1,
       "growing EOF within reserved space failed: %lu\n", GetLastError());
    ok(FlushFileBuffers(File), "flushing reserved file failed: %lu\n", GetLastError());
    ok(GetFileInformationByHandleEx(File, FileStandardInfo, &Standard, sizeof(Standard)),
       "querying grown file failed: %lu\n", GetLastError());
    ok(Standard.EndOfFile.QuadPart == Position.QuadPart + 1, "EOF %I64d\n", Standard.EndOfFile.QuadPart);
    ok(Standard.AllocationSize.QuadPart == (LONGLONG)TEST_CLUSTERS * ClusterBytes,
       "growing EOF trimmed the reserved allocation to %I64d bytes\n", Standard.AllocationSize.QuadPart);
    ok(FreeClusters(NULL) == Grown, "growing EOF released reserved clusters\n");

    Reader = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Reader != INVALID_HANDLE_VALUE, "opening a second handle failed: %lu\n", GetLastError());
    CloseHandle(File);
    if (Reader != INVALID_HANDLE_VALUE)
    {
        ok(GetFileInformationByHandleEx(Reader, FileStandardInfo, &Standard, sizeof(Standard)) &&
           Standard.AllocationSize.QuadPart == (LONGLONG)TEST_CLUSTERS * ClusterBytes,
           "first close trimmed the reservation\n");
        CloseHandle(Reader);
    }
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopening reserved file failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(Path);
        return;
    }
    ok(GetFileInformationByHandleEx(File, FileStandardInfo, &Standard, sizeof(Standard)) &&
       Standard.AllocationSize.QuadPart == 2 * (LONGLONG)ClusterBytes && Standard.EndOfFile.QuadPart == Position.QuadPart + 1,
       "after last close allocation %I64d, EOF %I64d\n", Standard.AllocationSize.QuadPart, Standard.EndOfFile.QuadPart);
    Marker = 0;
    ok(SetFilePointerEx(File, Position, NULL, FILE_BEGIN) && ReadFile(File, &Marker, 1, &Written, NULL) &&
       Written == 1 && Marker == 0xa5, "last close lost the written byte\n");

    ok(SetAllocation(File, 0), "releasing the allocation failed: %lu\n", GetLastError());
    Shrunk = FreeClusters(NULL);
    ok(Shrunk == Before, "free clusters are %I64u after the release, %I64u before the growth\n", Shrunk, Before);
    CloseHandle(File);

    Before = FreeClusters(NULL);
    if (!Remount())
    {
        skip("the test volume could not be dismounted: %lu\n", GetLastError());
        return;
    }
    Remounted = FreeClusters(NULL);
    ok(Remounted == Before, "a fresh count found %I64u free clusters, the volume reported %I64u\n", Remounted, Before);
}

#define DISMOUNT_WRITE_LENGTH (1024 * 1024)
#define DISMOUNT_WAIT_MS 10000

static BOOL
FinishOverlapped(HANDLE File, OVERLAPPED *Io, PDWORD Bytes)
{
    *Bytes = 0;
    if (WaitForSingleObject(Io->hEvent, DISMOUNT_WAIT_MS) != WAIT_OBJECT_0)
    {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    return GetOverlappedResult(File, Io, Bytes, FALSE);
}

static VOID
CheckDismountedData(PCWSTR Path, PUCHAR Data)
{
    HANDLE File;
    DWORD Bytes;
    DWORD i;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopening flushed file failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    memset(Data, 0, DISMOUNT_WRITE_LENGTH);
    if (ReadFile(File, Data, DISMOUNT_WRITE_LENGTH, &Bytes, NULL) && Bytes == DISMOUNT_WRITE_LENGTH)
    {
        for (i = 0; i < DISMOUNT_WRITE_LENGTH && Data[i] == 0x39; ++i) {}
        ok(i == DISMOUNT_WRITE_LENGTH, "dismount lost data at byte %lu\n", i);
    }
    else
    {
        ok(FALSE, "short read after dismount: %lu bytes, error %lu\n", Bytes, GetLastError());
    }
    CloseHandle(File);
}

static BOOL
WriteAcrossDismount(HANDLE Volume, HANDLE File, OVERLAPPED *Io, PUCHAR Data)
{
    BOOL Submitted;
    BOOL Dismounted;
    DWORD Error;
    DWORD Bytes;

    if (!SetAllocation(File, DISMOUNT_WRITE_LENGTH))
    {
        ok(FALSE, "reserving write space failed: %lu\n", GetLastError());
        return FALSE;
    }
    memset(Data, 0x39, DISMOUNT_WRITE_LENGTH);
    Submitted = WriteFile(File, Data, DISMOUNT_WRITE_LENGTH, &Bytes, Io);
    Error = Submitted ? ERROR_SUCCESS : GetLastError();
    ok(Submitted || Error == ERROR_IO_PENDING, "submitting write failed: %lu\n", Error);
    if (!Submitted && Error != ERROR_IO_PENDING)
        return FALSE;
    Dismounted = DeviceIoControl(Volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &Bytes, NULL);
    ok(Dismounted, "dismount failed: %lu\n", GetLastError());
    Submitted = FinishOverlapped(File, Io, &Bytes);
    ok(Submitted && Bytes == DISMOUNT_WRITE_LENGTH, "pending write completed with %lu bytes, error %lu\n", Bytes,
       GetLastError());
    if (!Dismounted || !Submitted)
        return FALSE;
    ResetEvent(Io->hEvent);
    Data[0] = 0x70;
    Submitted = WriteFile(File, Data, 4096, &Bytes, Io);
    if (!Submitted && GetLastError() == ERROR_IO_PENDING)
        Submitted = FinishOverlapped(File, Io, &Bytes);
    ok(!Submitted, "old handle wrote through a dismounted volume\n");
    return TRUE;
}

static VOID
TestDismountWrite(_In_ HANDLE Volume, _In_ PCWSTR Path)
{
    OVERLAPPED Io = {0};
    HANDLE File;
    PUCHAR Data;
    BOOL Written = FALSE;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
    Data = VirtualAlloc(NULL, DISMOUNT_WRITE_LENGTH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Io.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(File != INVALID_HANDLE_VALUE && Data && Io.hEvent, "setting up overlapped write failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE && Data && Io.hEvent)
        Written = WriteAcrossDismount(Volume, File, &Io, Data);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    CloseHandle(Volume);
    if (Written)
        CheckDismountedData(Path, Data);
    if (Io.hEvent)
        CloseHandle(Io.hEvent);
    if (Data)
        VirtualFree(Data, 0, MEM_RELEASE);
}

START_TEST(DismountWrite)
{
    WCHAR Path[MAX_PATH];
    HANDLE Volume;

    if (!FindTestVolume())
    {
        skip("no NTFS volume labelled %ls\n", TEST_VOLUME_LABEL);
        return;
    }
    Volume = CreateFileW(VolumeDevice, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, 0, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
    {
        skip("opening test volume failed: %lu\n", GetLastError());
        return;
    }
    if (!GetTempFileNameW(VolumeRoot, L"dis", 0, Path))
    {
        ok(FALSE, "creating test file failed: %lu\n", GetLastError());
        CloseHandle(Volume);
        return;
    }
    TestDismountWrite(Volume, Path);
    DeleteFileW(Path);
}
