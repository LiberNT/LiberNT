/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The symbolic links a driver puts in the object namespace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
IopCreateSymbolicLink(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ PUNICODE_STRING DeviceName,
    _In_ BOOLEAN Protected)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (Protected)
    {
        Status = IopCreateSecurityDescriptorPerType(SystemDefault, &Descriptor);
        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        Status = IopCreateSecurityDescriptorPerType(UnrestrictedPublic,
                                                    &Descriptor);
        if (!NT_SUCCESS(Status)) return Status;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               SymbolicLinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT |
                                   OBJ_KERNEL_HANDLE,
                               NULL,
                               Descriptor);

    Status = NtCreateSymbolicLinkObject(&Handle,
                                        SYMBOLIC_LINK_ALL_ACCESS,
                                        &ObjectAttributes,
                                        DeviceName);
    if (!NT_SUCCESS(Status)) return Status;

    NtClose(Handle);

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoCreateSymbolicLink(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ PUNICODE_STRING DeviceName)
{
    return IopCreateSymbolicLink(SymbolicLinkName, DeviceName, TRUE);
}

NTSTATUS
NTAPI
IoCreateUnprotectedSymbolicLink(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ PUNICODE_STRING DeviceName)
{
    return IopCreateSymbolicLink(SymbolicLinkName, DeviceName, FALSE);
}

NTSTATUS
NTAPI
IoDeleteSymbolicLink(
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               SymbolicLinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = NtOpenSymbolicLinkObject(&Handle, DELETE, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    Status = NtMakeTemporaryObject(Handle);

    NtClose(Handle);

    return Status;
}
