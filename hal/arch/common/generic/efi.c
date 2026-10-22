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

#define HALP_EFI_UNSPECIFIED_TIMEZONE 0x07FF

typedef struct _HALP_EFI_TIME
{
    USHORT Year;
    UCHAR Month;
    UCHAR Day;
    UCHAR Hour;
    UCHAR Minute;
    UCHAR Second;
    UCHAR Pad1;
    ULONG Nanosecond;
    SHORT TimeZone;
    UCHAR Daylight;
    UCHAR Pad2;
} HALP_EFI_TIME, *PHALP_EFI_TIME;

C_ASSERT(sizeof(HALP_EFI_TIME) == 16);

typedef ULONG_PTR (*PHALP_EFI_GET_TIME)(PHALP_EFI_TIME Time,
                                        PVOID Capabilities);

typedef ULONG_PTR (*PHALP_EFI_SET_TIME)(PHALP_EFI_TIME Time);

static VIRTUAL_EFI_RUNTIME_SERVICES HalpEfiRuntimeServices;
static KGUARDED_MUTEX HalpEfiRuntimeLock;
static KSPIN_LOCK HalpEfiTimeLock;
static BOOLEAN HalpEfiFirmwareResources;

VOID
NTAPI
HalpInitializeEfiRuntime(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVIRTUAL_EFI_RUNTIME_SERVICES Services;
    PLIST_ENTRY Resources;

    KeInitializeGuardedMutex(&HalpEfiRuntimeLock);
    KeInitializeSpinLock(&HalpEfiTimeLock);

    if (!LoaderBlock->FirmwareInformation.FirmwareTypeEfi)
        return;

    Services = LoaderBlock->FirmwareInformation.u.EfiInformation.VirtualEfiRuntimeServices;
    if (Services)
        HalpEfiRuntimeServices = *Services;

    Resources = &LoaderBlock->FirmwareInformation.u.EfiInformation.FirmwareResourceList;
    HalpEfiFirmwareResources = Resources->Flink && !IsListEmpty(Resources);
}

BOOLEAN
NTAPI
HalpEfiHasFirmwareResources(VOID)
{
    return HalpEfiFirmwareResources;
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

NTSTATUS
NTAPI
HalpEfiQueryTime(
    _Out_ PTIME_FIELDS TimeFields)
{
    PHALP_EFI_GET_TIME GetTime = (PHALP_EFI_GET_TIME)HalpEfiRuntimeServices.GetTime;
    HALP_EFI_TIME Time;
    ULONG_PTR Status;
    KIRQL OldIrql;

    if (!GetTime)
        return STATUS_NOT_IMPLEMENTED;

    RtlZeroMemory(&Time, sizeof(Time));
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpEfiTimeLock);
    Status = GetTime(&Time, NULL);
    KeReleaseSpinLockFromDpcLevel(&HalpEfiTimeLock);
    KeLowerIrql(OldIrql);

    if (Status != HALP_EFI_SUCCESS)
        return HalpEfiStatusToNtStatus(Status);

    if (Time.Year < 1900 || Time.Month < 1 || Time.Month > 12 || Time.Day < 1 || Time.Day > 31 ||
        Time.Hour > 23 || Time.Minute > 59 || Time.Second > 59)
    {
        return STATUS_UNSUCCESSFUL;
    }

    TimeFields->Year = (CSHORT)Time.Year;
    TimeFields->Month = Time.Month;
    TimeFields->Day = Time.Day;
    TimeFields->Hour = Time.Hour;
    TimeFields->Minute = Time.Minute;
    TimeFields->Second = Time.Second;
    TimeFields->Milliseconds = (CSHORT)(Time.Nanosecond / 1000000);
    TimeFields->Weekday = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpEfiSetTime(
    _In_ PTIME_FIELDS TimeFields)
{
    PHALP_EFI_SET_TIME SetTime = (PHALP_EFI_SET_TIME)HalpEfiRuntimeServices.SetTime;
    HALP_EFI_TIME Time;
    ULONG_PTR Status;
    KIRQL OldIrql;

    if (!SetTime)
        return STATUS_NOT_IMPLEMENTED;

    RtlZeroMemory(&Time, sizeof(Time));
    Time.Year = (USHORT)TimeFields->Year;
    Time.Month = (UCHAR)TimeFields->Month;
    Time.Day = (UCHAR)TimeFields->Day;
    Time.Hour = (UCHAR)TimeFields->Hour;
    Time.Minute = (UCHAR)TimeFields->Minute;
    Time.Second = (UCHAR)TimeFields->Second;
    Time.Nanosecond = (ULONG)TimeFields->Milliseconds * 1000000;
    Time.TimeZone = HALP_EFI_UNSPECIFIED_TIMEZONE;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpEfiTimeLock);
    Status = SetTime(&Time);
    KeReleaseSpinLockFromDpcLevel(&HalpEfiTimeLock);
    KeLowerIrql(OldIrql);

    return HalpEfiStatusToNtStatus(Status);
}
