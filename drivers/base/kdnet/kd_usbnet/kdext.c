/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KDNET extensibility module entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

#define USBNET_PCI_PROGIF_XHCI  0x30

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

static
VOID
NTAPI
UsbNetSetHibernateRange(VOID)
{
}

NTSTATUS
NTAPI
KdInitializeLibrary(
    _In_ PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    _In_opt_ PCHAR LoaderOptions,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    PKDNET_EXTENSIBILITY_EXPORTS Exports;

    UNREFERENCED_PARAMETER(LoaderOptions);

    if (!ImportTable || ImportTable->FunctionCount != KDNET_EXT_IMPORTS)
        return STATUS_INVALID_PARAMETER;

    Exports = ImportTable->Exports;
    if (!Exports || Exports->FunctionCount != KDNET_EXT_EXPORTS)
        return STATUS_INVALID_PARAMETER;

    if (Device->BaseClass != PCI_CLASS_SERIAL_BUS_CTLR || Device->SubClass != PCI_SUBCLASS_SB_USB ||
        Device->ProgIf != USBNET_PCI_PROGIF_XHCI)
    {
        return STATUS_NOT_SUPPORTED;
    }

    KdNetExtensibilityImports = ImportTable;
    Exports->KdInitializeController = UsbNetInitializeController;
    Exports->KdShutdownController = UsbNetShutdownController;
    Exports->KdSetHibernateRange = UsbNetSetHibernateRange;
    Exports->KdGetRxPacket = UsbNetGetRxPacket;
    Exports->KdReleaseRxPacket = UsbNetReleaseRxPacket;
    Exports->KdGetTxPacket = UsbNetGetTxPacket;
    Exports->KdSendTxPacket = UsbNetSendTxPacket;
    Exports->KdGetPacketAddress = UsbNetGetPacketAddress;
    Exports->KdGetPacketLength = UsbNetGetPacketLength;
    Exports->KdGetHardwareContextSize = UsbNetGetHardwareContextSize;
    Device->Memory.Length = UsbNetGetHardwareContextSize(Device);
    Device->Memory.Cached = FALSE;
    return STATUS_SUCCESS;
}
