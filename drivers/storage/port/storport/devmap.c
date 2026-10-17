/*
 * PROJECT:     LiberNT Storport Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SCSI port numbering and the HARDWARE\DEVICEMAP\Scsi tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include "../scsiport/scsitypes.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
PortOpenMapKey(
    _In_opt_ HANDLE Parent,
    _In_ PCWSTR Name,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;

    RtlInitUnicodeString(&KeyName, Name);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_KERNEL_HANDLE,
                               Parent,
                               NULL);
    return ZwCreateKey(Key, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
}

static
VOID
PortSetMapValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ ULONG Type,
    _In_reads_bytes_(Size) PVOID Data,
    _In_ ULONG Size)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    ZwSetValueKey(Key, &ValueName, 0, Type, Data, Size);
}

static
NTSTATUS
PortOpenAdapterMapKey(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PHANDLE Key)
{
    WCHAR NameBuffer[32];
    HANDLE ScsiKey;
    NTSTATUS Status;

    Status = PortOpenMapKey(NULL, L"\\Registry\\Machine\\Hardware\\DeviceMap\\Scsi", &ScsiKey);
    if (!NT_SUCCESS(Status))
        return Status;

    _swprintf(NameBuffer, L"Scsi Port %lu", DeviceExtension->PortNumber);
    Status = PortOpenMapKey(ScsiKey, NameBuffer, Key);
    ZwClose(ScsiKey);
    return Status;
}

VOID
PortCreateScsiPortLinks(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PCONFIGURATION_INFORMATION ConfigInfo = IoGetConfigurationInformation();
    WCHAR TargetBuffer[32], LinkBuffer[32];
    UNICODE_STRING Target, Link;
    ULONG Number;

    _swprintf(TargetBuffer, L"\\Device\\RaidPort%lu", DeviceExtension->RaidPortNumber);
    RtlInitUnicodeString(&Target, TargetBuffer);

    for (Number = 0; Number <= ConfigInfo->ScsiPortCount; Number++)
    {
        _swprintf(LinkBuffer, L"\\Device\\ScsiPort%lu", Number);
        RtlInitUnicodeString(&Link, LinkBuffer);
        if (!NT_SUCCESS(IoCreateSymbolicLink(&Link, &Target)))
            continue;

        _swprintf(LinkBuffer, L"\\DosDevices\\Scsi%lu:", Number);
        RtlInitUnicodeString(&Link, LinkBuffer);
        IoCreateSymbolicLink(&Link, &Target);

        InterlockedIncrement((PLONG)&ConfigInfo->ScsiPortCount);
        DeviceExtension->PortNumber = Number;
        DeviceExtension->ScsiPortLinked = TRUE;
        return;
    }
}

VOID
PortDeleteScsiPortLinks(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    WCHAR LinkBuffer[32];
    UNICODE_STRING Link;

    if (!DeviceExtension->ScsiPortLinked)
        return;

    _swprintf(LinkBuffer, L"\\DosDevices\\Scsi%lu:", DeviceExtension->PortNumber);
    RtlInitUnicodeString(&Link, LinkBuffer);
    IoDeleteSymbolicLink(&Link);

    _swprintf(LinkBuffer, L"\\Device\\ScsiPort%lu", DeviceExtension->PortNumber);
    RtlInitUnicodeString(&Link, LinkBuffer);
    IoDeleteSymbolicLink(&Link);

    InterlockedDecrement((PLONG)&IoGetConfigurationInformation()->ScsiPortCount);
    DeviceExtension->ScsiPortLinked = FALSE;
}

VOID
PortRegisterAdapterMap(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PUNICODE_STRING Service = &DeviceExtension->Device->DriverObject->DriverExtension->ServiceKeyName;
    WCHAR NameBuffer[32];
    PWCHAR Driver;
    HANDLE PortKey, BusKey, InitiatorKey;
    ULONG Value, Bus;

    if (!NT_SUCCESS(PortOpenAdapterMapKey(DeviceExtension, &PortKey)))
        return;

    Value = DeviceExtension->Miniport.PortConfig.BusInterruptLevel;
    PortSetMapValue(PortKey, L"Interrupt", REG_DWORD, &Value, sizeof(Value));
    Value = DeviceExtension->DataDma64 ? 1 : 0;
    PortSetMapValue(PortKey, L"Dma64BitAddresses", REG_DWORD, &Value, sizeof(Value));

    Driver = ExAllocatePoolWithTag(PagedPool, Service->Length + sizeof(UNICODE_NULL), TAG_GLOBAL_DATA);
    if (Driver)
    {
        RtlCopyMemory(Driver, Service->Buffer, Service->Length);
        Driver[Service->Length / sizeof(WCHAR)] = UNICODE_NULL;
        PortSetMapValue(PortKey, L"Driver", REG_SZ, Driver, Service->Length + sizeof(UNICODE_NULL));
        ExFreePoolWithTag(Driver, TAG_GLOBAL_DATA);
    }

    for (Bus = 0; Bus < DeviceExtension->Miniport.PortConfig.NumberOfBuses; Bus++)
    {
        _swprintf(NameBuffer, L"Scsi Bus %lu", Bus);
        if (!NT_SUCCESS(PortOpenMapKey(PortKey, NameBuffer, &BusKey)))
            continue;

        _swprintf(NameBuffer, L"Initiator Id %lu",
                  Bus < RTL_NUMBER_OF(DeviceExtension->Miniport.PortConfig.InitiatorBusId)
                      ? (ULONG)(UCHAR)DeviceExtension->Miniport.PortConfig.InitiatorBusId[Bus]
                      : (ULONG)(UCHAR)SP_UNINITIALIZED_VALUE);
        if (NT_SUCCESS(PortOpenMapKey(BusKey, NameBuffer, &InitiatorKey)))
            ZwClose(InitiatorKey);
        ZwClose(BusKey);
    }

    ZwClose(PortKey);
}

VOID
PortRegisterLunMap(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension)
{
    PINQUIRYDATA InquiryData = PdoExtension->InquiryBuffer;
    WCHAR NameBuffer[64];
    WCHAR Serial[RTL_NUMBER_OF(PdoExtension->SerialNumber)];
    PCWSTR TypeName;
    HANDLE PortKey, BusKey, TargetKey, LunKey;
    ULONG Length;

    if (!InquiryData || !NT_SUCCESS(PortOpenAdapterMapKey(PdoExtension->FdoExtension, &PortKey)))
        return;

    _swprintf(NameBuffer, L"Scsi Bus %lu", PdoExtension->Bus);
    if (NT_SUCCESS(PortOpenMapKey(PortKey, NameBuffer, &BusKey)))
    {
        _swprintf(NameBuffer, L"Target Id %lu", PdoExtension->Target);
        if (NT_SUCCESS(PortOpenMapKey(BusKey, NameBuffer, &TargetKey)))
        {
            _swprintf(NameBuffer, L"Logical Unit Id %lu", PdoExtension->Lun);
            if (NT_SUCCESS(PortOpenMapKey(TargetKey, NameBuffer, &LunKey)))
            {
                PortSetMapValue(LunKey, L"InquiryData", REG_BINARY, InquiryData, INQUIRYDATABUFFERSIZE);

                Length = (ULONG)min(strlen(PdoExtension->SerialNumber), RTL_NUMBER_OF(Serial) - 1);
                for (ULONG Index = 0; Index < Length; Index++)
                    Serial[Index] = (WCHAR)(UCHAR)PdoExtension->SerialNumber[Index];
                Serial[Length] = UNICODE_NULL;
                PortSetMapValue(LunKey, L"SerialNumber", REG_SZ, Serial, (Length + 1) * sizeof(WCHAR));

                _swprintf(NameBuffer, L"%.8hs%.16hs%.4hs",
                          InquiryData->VendorId,
                          InquiryData->ProductId,
                          InquiryData->ProductRevisionLevel);
                PortSetMapValue(LunKey, L"Identifier", REG_SZ, NameBuffer,
                                (ULONG)(wcslen(NameBuffer) + 1) * sizeof(WCHAR));

                TypeName = GetPeripheralTypeW(InquiryData);
                PortSetMapValue(LunKey, L"DeviceType", REG_SZ, (PVOID)TypeName,
                                (ULONG)(wcslen(TypeName) + 1) * sizeof(WCHAR));
                ZwClose(LunKey);
            }
            ZwClose(TargetKey);
        }
        ZwClose(BusKey);
    }
    ZwClose(PortKey);
}
