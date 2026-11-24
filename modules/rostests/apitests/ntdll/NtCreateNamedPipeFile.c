/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests named pipes created and opened relative to the pipe root directory
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

static NTSTATUS
OpenPipe(PHANDLE Handle, HANDLE Root, PCWSTR Name, PIO_STATUS_BLOCK IoStatus)
{
    UNICODE_STRING String;
    OBJECT_ATTRIBUTES Attributes;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Root, NULL);
    *Handle = NULL;
    return NtOpenFile(Handle, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &Attributes, IoStatus,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
}

START_TEST(NtCreateNamedPipeFile)
{
    UNICODE_STRING String;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Timeout;
    HANDLE Root, Server, Client, Other;
    WCHAR Name[64], FullName[96], Missing[80];
    CHAR Buffer[8];
    NTSTATUS Status;

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"lntrel_%lu", (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess);
    StringCchPrintfW(FullName, ARRAYSIZE(FullName), L"\\Device\\NamedPipe\\%ls", Name);
    StringCchPrintfW(Missing, ARRAYSIZE(Missing), L"%ls_missing", Name);

    RtlInitUnicodeString(&String, L"\\Device\\NamedPipe\\");
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Root, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE, Root, NULL);
    Timeout.QuadPart = -500000;
    IoStatus.Information = 0xdead;
    Status = NtCreateNamedPipeFile(&Server, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &Attributes, &IoStatus,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, FILE_SYNCHRONOUS_IO_NONALERT,
                                   FILE_PIPE_BYTE_STREAM_TYPE, FILE_PIPE_BYTE_STREAM_MODE, FILE_PIPE_QUEUE_OPERATION,
                                   1, 4096, 4096, &Timeout);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Root);
        return;
    }
    ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)FILE_CREATED);

    IoStatus.Information = 0xdead;
    Status = OpenPipe(&Client, Root, Name, &IoStatus);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)FILE_OPENED);

        Status = NtWriteFile(Client, NULL, NULL, NULL, &IoStatus, "relpipe", 7, NULL, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)7);
        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = NtReadFile(Server, NULL, NULL, NULL, &IoStatus, Buffer, 7, NULL, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)7);
        ok(!memcmp(Buffer, "relpipe", 7), "read '%.7s'\n", Buffer);

        Status = OpenPipe(&Other, NULL, FullName, &IoStatus);
        ok_ntstatus(Status, STATUS_PIPE_NOT_AVAILABLE);
        if (NT_SUCCESS(Status))
            NtClose(Other);
        Status = OpenPipe(&Other, Root, Name, &IoStatus);
        ok_ntstatus(Status, STATUS_PIPE_NOT_AVAILABLE);
        if (NT_SUCCESS(Status))
            NtClose(Other);
        NtClose(Client);
    }

    Status = OpenPipe(&Other, Root, Missing, &IoStatus);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
        NtClose(Other);

    NtClose(Server);
    Status = OpenPipe(&Other, Root, Name, &IoStatus);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
        NtClose(Other);
    NtClose(Root);
}
