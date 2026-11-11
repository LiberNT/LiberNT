/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoCreateFile access checks for kernel-mode requests issued in a user thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\libernt_iocreate_access.tmp");

static
NTSTATUS
OpenWithOptions(
    _In_ ULONG Options)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = IoCreateFile(&Handle, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0,
                          CreateFileTypeNone, NULL, Options);
    if (NT_SUCCESS(Status))
        ZwClose(Handle);
    return Status;
}

static
VOID
DeleteTestFile(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&Handle, DELETE | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE | FILE_DELETE_ON_CLOSE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (NT_SUCCESS(Status))
        ZwClose(Handle);
}

START_TEST(IoCreateFileAccessCheck)
{
    SECURITY_DESCRIPTOR SecurityDescriptor;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    ACL EmptyDacl;
    HANDLE Handle;
    NTSTATUS Status;

    ok(ExGetPreviousMode() == UserMode, "The test does not run in a user thread\n");

    DeleteTestFile();

    Status = RtlCreateSecurityDescriptor(&SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlCreateAcl(&EmptyDacl, sizeof(EmptyDacl), ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlSetDaclSecurityDescriptor(&SecurityDescriptor, TRUE, &EmptyDacl, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL,
                               &SecurityDescriptor);
    Status = ZwCreateFile(&Handle, FILE_WRITE_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, NULL,
                          FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ZwClose(Handle);

    Status = OpenWithOptions(IO_NO_PARAMETER_CHECKING);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = OpenWithOptions(IO_NO_PARAMETER_CHECKING | IO_FORCE_ACCESS_CHECK);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);

    DeleteTestFile();
    Status = OpenWithOptions(IO_NO_PARAMETER_CHECKING);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
}
