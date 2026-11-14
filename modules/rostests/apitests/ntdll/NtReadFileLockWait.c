/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that an alerted wait for a busy synchronous file fails the read instead of running it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _LOCK_WAIT_READ
{
    HANDLE File;
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    CHAR Byte;
} LOCK_WAIT_READ;

static
DWORD
WINAPI
ReadThread(
    _In_ PVOID Parameter)
{
    LOCK_WAIT_READ *Read = Parameter;

    Read->Status = NtReadFile(Read->File, NULL, NULL, NULL, &Read->IoStatus, &Read->Byte, 1, NULL, NULL);
    return 0;
}

START_TEST(NtReadFileLockWait)
{
    static const WCHAR PipeName[] = L"\\\\.\\pipe\\NtReadFileLockWait";
    UNICODE_STRING NtName = RTL_CONSTANT_STRING(L"\\??\\pipe\\NtReadFileLockWait");
    LOCK_WAIT_READ Holder = { 0 }, Waiter = { 0 };
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Server, Client, HolderThread, WaiterThread;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    DWORD Written;
    BOOL Success;

    Server = CreateNamedPipeW(PipeName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 16, 16, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateNamedPipeW failed with error %lu\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE)
        return;

    InitializeObjectAttributes(&ObjectAttributes, &NtName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateFile(&Client, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &ObjectAttributes, &IoStatus, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_ALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        CloseHandle(Server);
        return;
    }

    Holder.File = Client;
    Holder.Status = STATUS_PENDING;
    HolderThread = CreateThread(NULL, 0, ReadThread, &Holder, 0, NULL);
    ok(HolderThread != NULL, "CreateThread failed with error %lu\n", GetLastError());
    ok_long(WaitForSingleObject(HolderThread, 50), WAIT_TIMEOUT);

    Waiter.File = Client;
    Waiter.Status = STATUS_PENDING;
    WaiterThread = CreateThread(NULL, 0, ReadThread, &Waiter, 0, NULL);
    ok(WaiterThread != NULL, "CreateThread failed with error %lu\n", GetLastError());
    ok_long(WaitForSingleObject(WaiterThread, 50), WAIT_TIMEOUT);

    Status = NtAlertThread(WaiterThread);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(WaitForSingleObject(WaiterThread, 1000) == WAIT_OBJECT_0, "The alerted read did not return\n");
    ok_ntstatus(Waiter.Status, STATUS_ALERTED);
    ok_long(WaitForSingleObject(HolderThread, 0), WAIT_TIMEOUT);

    Written = 0;
    Success = WriteFile(Server, "x", 1, &Written, NULL);
    ok(Success && Written == 1, "WriteFile wrote %lu bytes, error %lu\n", Written, GetLastError());
    ok_long(WaitForSingleObject(HolderThread, 1000), WAIT_OBJECT_0);
    ok_ntstatus(Holder.Status, STATUS_SUCCESS);
    ok(Holder.IoStatus.Information == 1 && Holder.Byte == 'x', "The holder read %Iu bytes, first 0x%x\n",
       Holder.IoStatus.Information, Holder.Byte);
    ok_long(WaitForSingleObject(WaiterThread, 1000), WAIT_OBJECT_0);

    CloseHandle(WaiterThread);
    CloseHandle(HolderThread);
    NtClose(Client);
    CloseHandle(Server);
}
