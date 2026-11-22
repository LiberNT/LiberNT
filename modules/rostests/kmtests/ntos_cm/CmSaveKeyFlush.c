/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Flushing the whole registry succeeds while a key is being saved to a file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define SAVE_ROUNDS 2

typedef struct _SAVE_CONTEXT
{
    HANDLE Key;
    volatile LONG Done;
    ULONG Saved;
    NTSTATUS Status;
} SAVE_CONTEXT, *PSAVE_CONTEXT;

static SAVE_CONTEXT SaveContext;

static
NTSTATUS
OpenKey(
    _Out_ PHANDLE Key,
    _In_ PCWSTR Path)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    return ZwOpenKey(Key, KEY_READ, &ObjectAttributes);
}

static
VOID
NTAPI
SaveThread(
    _In_ PVOID Parameter)
{
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\kmtest-savekey.hiv");
    PSAVE_CONTEXT Context = Parameter;
    FILE_DISPOSITION_INFORMATION Disposition;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status = STATUS_SUCCESS;
    HANDLE File;
    ULONG Round;

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    for (Round = 0; Round < SAVE_ROUNDS; Round++)
    {
        Status = ZwCreateFile(&File,
                              GENERIC_WRITE | DELETE | SYNCHRONIZE,
                              &ObjectAttributes,
                              &IoStatus,
                              NULL,
                              FILE_ATTRIBUTE_NORMAL,
                              0,
                              FILE_OVERWRITE_IF,
                              FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                              NULL,
                              0);
        if (!NT_SUCCESS(Status))
            break;

        Status = ZwSaveKey(Context->Key, File);

        Disposition.DeleteFile = TRUE;
        ZwSetInformationFile(File, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
        ZwClose(File);
        if (!NT_SUCCESS(Status))
            break;

        Context->Saved++;
    }

    Context->Status = Status;
    InterlockedExchange(&Context->Done, 1);
}

START_TEST(CmSaveKeyFlush)
{
    PSAVE_CONTEXT Context = &SaveContext;
    ULONG Flushes = 0, Failures = 0;
    NTSTATUS Status, FirstFailure = STATUS_SUCCESS;
    HANDLE Root = NULL;
    PKTHREAD Thread;

    RtlZeroMemory(Context, sizeof(*Context));

    /* A key of the master hive stands for every hive: flushing it writes them all */
    Status = OpenKey(&Root, L"\\Registry\\Machine");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = OpenKey(&Context->Key, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Thread = KmtStartThread(SaveThread, Context);
    if (skip(Thread != NULL, "The saving thread did not start\n"))
        goto Cleanup;

    while (!Context->Done)
    {
        Status = ZwFlushKey(Root);
        Flushes++;
        if (!NT_SUCCESS(Status))
        {
            if (Failures++ == 0)
                FirstFailure = Status;
        }
    }
    KmtFinishThread(Thread, NULL);

    ok_eq_hex(Context->Status, STATUS_SUCCESS);
    ok_eq_ulong(Context->Saved, (ULONG)SAVE_ROUNDS);
    ok(Flushes != 0, "The registry was never flushed while the key was saved\n");
    ok(Failures == 0, "%lu of %lu registry flushes failed, the first with 0x%08lx\n", Failures, Flushes, FirstFailure);

Cleanup:
    if (Context->Key)
        ZwClose(Context->Key);
    ZwClose(Root);
}
