/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for object types that own their security descriptor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestKeyMaximumAllowed(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software");
    OBJECT_ATTRIBUTES ObjectAttributes;
    KEY_FULL_INFORMATION FullInfo;
    HANDLE KeyHandle = NULL;
    PVOID KeyObject = NULL;
    NTSTATUS Status;
    ULONG Length;

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, MAXIMUM_ALLOWED, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    /* A key keeps its descriptor in the hive, not in the object header. If the
     * access check skipped the type's security method the handle would carry
     * the MAXIMUM_ALLOWED bit itself instead of the rights it stands for, and
     * this query would come back denied */
    Length = 0;
    Status = ZwQueryKey(KeyHandle,
                        KeyFullInformation,
                        &FullInfo,
                        sizeof(FullInfo),
                        &Length);
    ok(Status == STATUS_SUCCESS || Status == STATUS_BUFFER_OVERFLOW,
       "ZwQueryKey with MAXIMUM_ALLOWED returned %lx\n", Status);

    /* The same handle must still resolve back to a key object */
    Status = ObReferenceObjectByHandle(KeyHandle,
                                       KEY_QUERY_VALUE,
                                       NULL,
                                       KernelMode,
                                       &KeyObject,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) ObDereferenceObject(KeyObject);

    ObCloseHandle(KeyHandle, KernelMode);
}

static
VOID
TestKeyObjectSecurity(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software");
    OBJECT_ATTRIBUTES ObjectAttributes;
    PSECURITY_DESCRIPTOR Sd = NULL;
    BOOLEAN MemoryAllocated = FALSE;
    BOOLEAN GenerateOnClose = TRUE;
    HANDLE KeyHandle = NULL;
    PVOID KeyObject = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_READ | READ_CONTROL, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObQueryObjectAuditingByHandle(KeyHandle, &GenerateOnClose);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) ok_bool_false(GenerateOnClose, "GenerateOnClose");

    Status = ObReferenceObjectByHandle(KeyHandle,
                                       0,
                                       NULL,
                                       KernelMode,
                                       &KeyObject,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(KeyHandle, KernelMode);
        return;
    }

    /* The type's own security method answers for this object, so the
     * descriptor comes back in a buffer of its own */
    Status = ObGetObjectSecurity(KeyObject, &Sd, &MemoryAllocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Sd != NULL, "no security descriptor for the key\n");
        if (Sd != NULL)
        {
            ok_bool_true(RtlValidSecurityDescriptor(Sd), "key SD valid");
            ok_bool_true(MemoryAllocated, "key SD was allocated");
        }
        ObReleaseObjectSecurity(Sd, MemoryAllocated);
    }

    ObDereferenceObject(KeyObject);
    ObCloseHandle(KeyHandle, KernelMode);
}

static
VOID
TestDirectoryObjectSecurity(VOID)
{
    UNICODE_STRING DirectoryName = RTL_CONSTANT_STRING(L"\\BaseNamedObjects");
    OBJECT_ATTRIBUTES ObjectAttributes;
    PSECURITY_DESCRIPTOR Sd = NULL, SdAgain = NULL;
    BOOLEAN MemoryAllocated = TRUE;
    BOOLEAN MemoryAllocated2 = TRUE;
    HANDLE DirectoryHandle = NULL;
    PVOID DirectoryObject = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &DirectoryName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = ZwOpenDirectoryObject(&DirectoryHandle, READ_CONTROL, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(DirectoryHandle,
                                       0,
                                       NULL,
                                       KernelMode,
                                       &DirectoryObject,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(DirectoryHandle, KernelMode);
        return;
    }

    /* A directory keeps its descriptor in the object manager's cache, so it is
     * handed out by pointer and the same entry comes back every time */
    Status = ObGetObjectSecurity(DirectoryObject, &Sd, &MemoryAllocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Sd != NULL, "no security descriptor for the directory\n");
        ok_bool_false(MemoryAllocated, "directory SD was allocated");
        if (Sd != NULL) ok_bool_true(RtlValidSecurityDescriptor(Sd), "directory SD valid");

        Status = ObGetObjectSecurity(DirectoryObject, &SdAgain, &MemoryAllocated2);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_pointer(SdAgain, Sd);
            ObReleaseObjectSecurity(SdAgain, MemoryAllocated2);
        }

        ObReleaseObjectSecurity(Sd, MemoryAllocated);
    }

    ObDereferenceObject(DirectoryObject);
    ObCloseHandle(DirectoryHandle, KernelMode);
}

START_TEST(ObSecurityMethod)
{
    TestKeyMaximumAllowed();
    TestKeyObjectSecurity();
    TestDirectoryObjectSecurity();
}
