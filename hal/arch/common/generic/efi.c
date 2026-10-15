/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI runtime variable services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/extypes.h>
#include <ndk/halfuncs.h>
#include "../include/halefi.h"

#define HALP_EFI_ERROR(Code)        (((ULONG_PTR)1 << (sizeof(ULONG_PTR) * 8 - 1)) | (Code))
#define HALP_EFI_SUCCESS            0
#define HALP_EFI_INVALID_PARAMETER  HALP_EFI_ERROR(2)
#define HALP_EFI_UNSUPPORTED        HALP_EFI_ERROR(3)
#define HALP_EFI_BUFFER_TOO_SMALL   HALP_EFI_ERROR(5)
#define HALP_EFI_OUT_OF_RESOURCES   HALP_EFI_ERROR(9)
#define HALP_EFI_NOT_FOUND          HALP_EFI_ERROR(14)

typedef ULONG_PTR (*PHALP_EFI_GET_VARIABLE)(PWSTR VariableName,
                                            LPCGUID VendorGuid,
                                            PULONG Attributes,
                                            PULONG_PTR DataSize,
                                            PVOID Data);

typedef ULONG_PTR (*PHALP_EFI_SET_VARIABLE)(PWSTR VariableName,
                                            LPCGUID VendorGuid,
                                            ULONG Attributes,
                                            ULONG_PTR DataSize,
                                            PVOID Data);

static VIRTUAL_EFI_RUNTIME_SERVICES HalpEfiRuntimeServices;
static KGUARDED_MUTEX HalpEfiRuntimeLock;

VOID
NTAPI
HalpInitializeEfiRuntime(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVIRTUAL_EFI_RUNTIME_SERVICES Services;

    KeInitializeGuardedMutex(&HalpEfiRuntimeLock);

    if (!LoaderBlock->FirmwareInformation.FirmwareTypeEfi)
        return;

    Services = LoaderBlock->FirmwareInformation.u.EfiInformation.VirtualEfiRuntimeServices;
    if (Services)
        HalpEfiRuntimeServices = *Services;
}

static
NTSTATUS
HalpEfiStatusToNtStatus(
    _In_ ULONG_PTR Status)
{
    switch (Status)
    {
        case HALP_EFI_SUCCESS:
            return STATUS_SUCCESS;
        case HALP_EFI_BUFFER_TOO_SMALL:
            return STATUS_BUFFER_TOO_SMALL;
        case HALP_EFI_NOT_FOUND:
            return STATUS_VARIABLE_NOT_FOUND;
        case HALP_EFI_INVALID_PARAMETER:
            return STATUS_INVALID_PARAMETER;
        case HALP_EFI_OUT_OF_RESOURCES:
            return STATUS_INSUFFICIENT_RESOURCES;
        case HALP_EFI_UNSUPPORTED:
            return STATUS_NOT_IMPLEMENTED;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}

NTSTATUS
NTAPI
HalGetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _Out_writes_bytes_opt_(*ValueLength) PVOID Value,
    _Inout_ PULONG ValueLength,
    _Out_opt_ PULONG Attributes)
{
    PHALP_EFI_GET_VARIABLE GetVariable = (PHALP_EFI_GET_VARIABLE)HalpEfiRuntimeServices.GetVariable;
    ULONG_PTR DataSize;
    ULONG_PTR Status;
    ULONG EfiAttributes = 0;

    PAGED_CODE();

    if (!GetVariable)
        return STATUS_NOT_IMPLEMENTED;

    if (!VariableName || !VendorGuid || !ValueLength)
        return STATUS_INVALID_PARAMETER;

    DataSize = Value ? *ValueLength : 0;

    KeAcquireGuardedMutex(&HalpEfiRuntimeLock);
    Status = GetVariable(VariableName, VendorGuid, &EfiAttributes, &DataSize, Value);
    KeReleaseGuardedMutex(&HalpEfiRuntimeLock);

    if (Status == HALP_EFI_SUCCESS || Status == HALP_EFI_BUFFER_TOO_SMALL)
    {
        if (DataSize > MAXULONG)
            return STATUS_UNSUCCESSFUL;

        *ValueLength = (ULONG)DataSize;
        if (Status == HALP_EFI_SUCCESS && Attributes)
            *Attributes = EfiAttributes;
    }

    return HalpEfiStatusToNtStatus(Status);
}

NTSTATUS
NTAPI
HalSetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_ ULONG Attributes)
{
    PHALP_EFI_SET_VARIABLE SetVariable = (PHALP_EFI_SET_VARIABLE)HalpEfiRuntimeServices.SetVariable;
    ULONG_PTR Status;

    PAGED_CODE();

    if (!SetVariable)
        return STATUS_NOT_IMPLEMENTED;

    if (!VariableName || !VendorGuid || (!Value && ValueLength))
        return STATUS_INVALID_PARAMETER;

    Attributes |= VARIABLE_ATTRIBUTE_BOOTSERVICE_ACCESS | VARIABLE_ATTRIBUTE_RUNTIME_ACCESS;

    KeAcquireGuardedMutex(&HalpEfiRuntimeLock);
    Status = SetVariable(VariableName, VendorGuid, Attributes, ValueLength, Value);
    KeReleaseGuardedMutex(&HalpEfiRuntimeLock);

    return HalpEfiStatusToNtStatus(Status);
}
