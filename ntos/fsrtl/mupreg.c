/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling the multiple UNC provider that a redirector is here
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_MUP_TAG 'mRsF'

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
FsRtlpOpenDev(
    _Out_ PHANDLE Handle,
    _In_ PCWSTR DeviceName)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;

    RtlInitUnicodeString(&Name, DeviceName);

    InitializeObjectAttributes(&Attributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenFile(Handle,
                      FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                      &Attributes,
                      &IoStatus,
                      0,
                      FILE_SYNCHRONOUS_IO_NONALERT);
}

static
NTSTATUS
FsRtlpRegisterProviderWithMUP(
    _In_ HANDLE MupHandle,
    _In_ PCUNICODE_STRING RedirectorDeviceName,
    _In_ BOOLEAN MailslotsSupported)
{
    PMUP_PROVIDER_REGISTRATION_INFO Registration;
    IO_STATUS_BLOCK IoStatus;
    ULONG Length;
    NTSTATUS Status;

    Length = sizeof(MUP_PROVIDER_REGISTRATION_INFO) +
             RedirectorDeviceName->Length;

    Registration = ExAllocatePoolWithTag(PagedPool, Length, FSRTL_MUP_TAG);
    if (Registration == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Registration, Length);

    Registration->RedirectorDeviceNameOffset =
        sizeof(MUP_PROVIDER_REGISTRATION_INFO);
    Registration->RedirectorDeviceNameLength = RedirectorDeviceName->Length;
    Registration->MailslotsSupported = MailslotsSupported;

    RtlCopyMemory((PUCHAR)Registration +
                      sizeof(MUP_PROVIDER_REGISTRATION_INFO),
                  RedirectorDeviceName->Buffer,
                  RedirectorDeviceName->Length);

    Status = ZwFsControlFile(MupHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatus,
                             FSCTL_MUP_REGISTER_PROVIDER,
                             Registration,
                             Length,
                             NULL,
                             0);

    ExFreePoolWithTag(Registration, FSRTL_MUP_TAG);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlRegisterUncProvider(
    _Out_ PHANDLE MupHandle,
    _In_ PCUNICODE_STRING RedirectorDeviceName,
    _In_ BOOLEAN MailslotsSupported)
{
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if ((MupHandle == NULL) || (RedirectorDeviceName == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *MupHandle = NULL;

    Status = FsRtlpOpenDev(&Handle, L"\\Device\\Mup");
    if (!NT_SUCCESS(Status)) return Status;

    Status = FsRtlpRegisterProviderWithMUP(Handle,
                                           RedirectorDeviceName,
                                           MailslotsSupported);

    if (!NT_SUCCESS(Status))
    {
        ZwClose(Handle);
        return Status;
    }

    *MupHandle = Handle;

    return STATUS_SUCCESS;
}

VOID
NTAPI
FsRtlDeregisterUncProvider(
    _In_ HANDLE Handle)
{
    PAGED_CODE();

    if (Handle != NULL) ZwClose(Handle);
}

/* EOF */
