/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Flush and byte-range locks accept a handle holding any one of their access rights
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
HANDLE
OpenWith(
    _In_ PCWSTR Path,
    _In_ ACCESS_MASK Access)
{
    HANDLE File = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "open with 0x%lx failed with %lu\n", Access, GetLastError());
    return File;
}

static
NTSTATUS
Flush(
    _In_ HANDLE File)
{
    IO_STATUS_BLOCK IoStatus;

    return NtFlushBuffersFile(File, &IoStatus);
}

static
NTSTATUS
LockAndUnlock(
    _In_ HANDLE File,
    _Out_ PNTSTATUS UnlockStatus)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset, Length;
    NTSTATUS Status;

    Offset.QuadPart = 0;
    Length.QuadPart = 1;
    *UnlockStatus = STATUS_NOT_LOCKED;
    Status = NtLockFile(File, NULL, NULL, NULL, &IoStatus, &Offset, &Length, 0, TRUE, TRUE);
    if (NT_SUCCESS(Status))
        *UnlockStatus = NtUnlockFile(File, &IoStatus, &Offset, &Length, 0);
    return Status;
}

START_TEST(IoHandleAccess)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    NTSTATUS Status, UnlockStatus;
    DWORD Written;
    HANDLE File;

    ok(GetTempPathW(RTL_NUMBER_OF(Directory), Directory) != 0, "GetTempPathW failed\n");
    ok(GetTempFileNameW(Directory, L"kha", 0, Path) != 0, "GetTempFileNameW failed\n");

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "create failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE) return;
    ok(WriteFile(File, "data", 4, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
    CloseHandle(File);

    File = OpenWith(Path, FILE_WRITE_DATA | SYNCHRONIZE);
    ok_eq_hex(Flush(File), STATUS_SUCCESS);
    CloseHandle(File);

    File = OpenWith(Path, FILE_APPEND_DATA | SYNCHRONIZE);
    ok_eq_hex(Flush(File), STATUS_SUCCESS);
    CloseHandle(File);

    File = OpenWith(Path, FILE_READ_DATA | SYNCHRONIZE);
    ok_eq_hex(Flush(File), STATUS_ACCESS_DENIED);
    CloseHandle(File);

    File = OpenWith(Path, FILE_READ_DATA | SYNCHRONIZE);
    Status = LockAndUnlock(File, &UnlockStatus);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(UnlockStatus, STATUS_SUCCESS);
    CloseHandle(File);

    File = OpenWith(Path, FILE_WRITE_DATA | SYNCHRONIZE);
    Status = LockAndUnlock(File, &UnlockStatus);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(UnlockStatus, STATUS_SUCCESS);
    CloseHandle(File);

    File = OpenWith(Path, FILE_READ_ATTRIBUTES | SYNCHRONIZE);
    Status = LockAndUnlock(File, &UnlockStatus);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    CloseHandle(File);

    ok(DeleteFileW(Path), "DeleteFileW failed with %lu\n", GetLastError());
}
