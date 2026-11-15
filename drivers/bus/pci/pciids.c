/*
 * PROJECT:     LiberNT PCI Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device, instance, hardware and compatible identifiers of a PCI function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "pci.h"

#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define PCI_ID_CHARS 512
#define PCI_CAPABILITY_WALK_LIMIT 48

static
ULONG
PciIdsReadConfig(
    _In_ PPCI_DEVICE Device,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return HalGetBusDataByOffset(PCIConfiguration, Device->BusNumber, Device->SlotNumber.u.AsULONG, Buffer, Offset, Length);
}

/* A bridge names its subsystem in capability 0Dh; without that capability it has none */
static
ULONG
PciIdsBridgeSubsystem(
    _In_ PPCI_DEVICE Device)
{
    UCHAR Pointer = Device->PciConfig.u.type1.CapabilitiesPtr;
    UCHAR Header[2];
    USHORT Ids[2];
    ULONG Walked;

    if (!(Device->PciConfig.Status & PCI_STATUS_CAPABILITIES_LIST))
        return 0;

    for (Walked = 0; Walked < PCI_CAPABILITY_WALK_LIMIT && Pointer >= PCI_COMMON_HDR_LENGTH; Walked++)
    {
        Pointer &= ~3;
        if (PciIdsReadConfig(Device, Header, Pointer, sizeof(Header)) != sizeof(Header))
            return 0;
        if (Header[0] == PCI_CAPABILITY_ID_P2P_SSID)
        {
            if (PciIdsReadConfig(Device, Ids, Pointer + 4, sizeof(Ids)) != sizeof(Ids))
                return 0;
            return ((ULONG)Ids[1] << 16) | Ids[0];
        }
        Pointer = Header[1];
    }

    return 0;
}

static
ULONG
PciIdsSubsystem(
    _In_ PPCI_DEVICE Device)
{
    USHORT Ids[2];

    switch (PCI_CONFIGURATION_TYPE(&Device->PciConfig))
    {
        case PCI_DEVICE_TYPE:
            return ((ULONG)Device->PciConfig.u.type0.SubSystemID << 16) | Device->PciConfig.u.type0.SubVendorID;

        case PCI_BRIDGE_TYPE:
            return PciIdsBridgeSubsystem(Device);

        case PCI_CARDBUS_BRIDGE_TYPE:
            /* TODO: the CardBus subsystem at 40h is used without a Windows 11 measurement */
            if (PciIdsReadConfig(Device, Ids, 0x40, sizeof(Ids)) != sizeof(Ids))
                return 0;
            return ((ULONG)Ids[1] << 16) | Ids[0];

        default:
            return 0;
    }
}

static
NTSTATUS
PciIdsStore(
    _In_reads_(Chars) PCWSTR Buffer,
    _In_ SIZE_T Chars,
    _Out_ PUNICODE_STRING String)
{
    UNICODE_STRING Source;

    ASSERT(Chars < PCI_ID_CHARS);
    Source.Buffer = (PWSTR)Buffer;
    Source.Length = (USHORT)(Chars * sizeof(WCHAR));
    Source.MaximumLength = Source.Length;
    return RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE, &Source, String);
}

/* Appends one string and its terminating NUL to a multi-string under construction */
static
NTSTATUS
PciIdsAppend(
    _Inout_updates_(PCI_ID_CHARS) PWSTR Buffer,
    _Inout_ PSIZE_T Used,
    _In_ _Printf_format_string_ PCWSTR Format,
    ...)
{
    size_t Remaining;
    PWSTR End;
    NTSTATUS Status;
    va_list Arguments;

    ASSERT(*Used < PCI_ID_CHARS);
    va_start(Arguments, Format);
    Status = RtlStringCchVPrintfExW(Buffer + *Used, PCI_ID_CHARS - *Used, &End, &Remaining, 0, Format, Arguments);
    va_end(Arguments);
    if (!NT_SUCCESS(Status) || Remaining < 2)
        return STATUS_BUFFER_OVERFLOW;

    *Used = (SIZE_T)(End - Buffer) + 1;
    ASSERT(Buffer[*Used - 1] == UNICODE_NULL);
    return STATUS_SUCCESS;
}

NTSTATUS
PciCreateDeviceIDString(
    PUNICODE_STRING DeviceID,
    PPCI_DEVICE Device)
{
    WCHAR Buffer[PCI_ID_CHARS];
    SIZE_T Used = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X",
                          Device->PciConfig.VendorID, Device->PciConfig.DeviceID,
                          PciIdsSubsystem(Device), Device->PciConfig.RevisionID);
    if (!NT_SUCCESS(Status))
        return Status;
    return PciIdsStore(Buffer, Used - 1, DeviceID);
}

/* The DevFn of the function, then of every bridge above it up to the root bus */
NTSTATUS
PciCreateInstanceIDString(
    PUNICODE_STRING InstanceID,
    PPCI_DEVICE Device)
{
    WCHAR Buffer[PCI_ID_CHARS];
    PPDO_DEVICE_EXTENSION PdoExtension;
    PFDO_DEVICE_EXTENSION FdoExtension;
    PPCI_DEVICE Current = Device;
    SIZE_T Used = 0;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Device->Pdo != NULL);

    Buffer[0] = UNICODE_NULL;
    while (Current != NULL)
    {
        Status = PciIdsAppend(Buffer, &Used, L"%02X",
                              (Current->SlotNumber.u.bits.DeviceNumber << 3) | Current->SlotNumber.u.bits.FunctionNumber);
        if (!NT_SUCCESS(Status))
            return Status;
        Used--;

        PdoExtension = Current->Pdo ? (PPDO_DEVICE_EXTENSION)Current->Pdo->DeviceExtension : NULL;
        if (PdoExtension == NULL || PdoExtension->Fdo == NULL)
            break;
        FdoExtension = (PFDO_DEVICE_EXTENSION)PdoExtension->Fdo->DeviceExtension;
        Current = FdoExtension->ParentPdo ? FdoExtension->ParentPdo->PciDevice : NULL;
    }

    return PciIdsStore(Buffer, Used, InstanceID);
}

/* Windows 11 lists VEN&DEV&REV and VEN&DEV as hardware IDs only when the subsystem is zero */
NTSTATUS
PciCreateHardwareIDsString(
    PUNICODE_STRING HardwareIDs,
    PPCI_DEVICE Device)
{
    PPCI_COMMON_CONFIG Config = &Device->PciConfig;
    WCHAR Buffer[PCI_ID_CHARS];
    ULONG Subsystem = PciIdsSubsystem(Device);
    SIZE_T Used = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X",
                          Config->VendorID, Config->DeviceID, Subsystem, Config->RevisionID);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X",
                              Config->VendorID, Config->DeviceID, Subsystem);
    if (NT_SUCCESS(Status) && Subsystem == 0)
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X&REV_%02X",
                              Config->VendorID, Config->DeviceID, Config->RevisionID);
    if (NT_SUCCESS(Status) && Subsystem == 0)
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X", Config->VendorID, Config->DeviceID);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X&CC_%02X%02X%02X",
                              Config->VendorID, Config->DeviceID, Config->BaseClass, Config->SubClass, Config->ProgIf);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X&CC_%02X%02X",
                              Config->VendorID, Config->DeviceID, Config->BaseClass, Config->SubClass);
    if (!NT_SUCCESS(Status))
        return Status;
    return PciIdsStore(Buffer, Used, HardwareIDs);
}

NTSTATUS
PciCreateCompatibleIDsString(
    PUNICODE_STRING CompatibleIDs,
    PPCI_DEVICE Device)
{
    PPCI_COMMON_CONFIG Config = &Device->PciConfig;
    WCHAR Buffer[PCI_ID_CHARS];
    ULONG Subsystem = PciIdsSubsystem(Device);
    SIZE_T Used = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Subsystem != 0)
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X&REV_%02X",
                              Config->VendorID, Config->DeviceID, Config->RevisionID);
    if (NT_SUCCESS(Status) && Subsystem != 0)
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&DEV_%04X", Config->VendorID, Config->DeviceID);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&CC_%02X%02X%02X",
                              Config->VendorID, Config->BaseClass, Config->SubClass, Config->ProgIf);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X&CC_%02X%02X",
                              Config->VendorID, Config->BaseClass, Config->SubClass);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\VEN_%04X", Config->VendorID);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\CC_%02X%02X%02X", Config->BaseClass, Config->SubClass, Config->ProgIf);
    if (NT_SUCCESS(Status))
        Status = PciIdsAppend(Buffer, &Used, L"PCI\\CC_%02X%02X", Config->BaseClass, Config->SubClass);
    if (!NT_SUCCESS(Status))
        return Status;
    return PciIdsStore(Buffer, Used, CompatibleIDs);
}
