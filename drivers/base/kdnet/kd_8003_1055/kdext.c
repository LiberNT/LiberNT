/*
 * PROJECT:     LiberNT LAN95xx USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KDNET extensibility module entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdlan.h"

#define LAN_DBG2_PORT_NET   0x8003
#define LAN_DBG2_SUBTYPE    0x1055

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

static
VOID
NTAPI
LanSetHibernateRange(VOID)
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

    if (Device->PortType != LAN_DBG2_PORT_NET || Device->PortSubtype != LAN_DBG2_SUBTYPE)
        return STATUS_NOT_SUPPORTED;

    KdNetExtensibilityImports = ImportTable;
    Exports->KdInitializeController = LanInitializeController;
    Exports->KdShutdownController = LanShutdownController;
    Exports->KdSetHibernateRange = LanSetHibernateRange;
    Exports->KdGetRxPacket = LanGetRxPacket;
    Exports->KdReleaseRxPacket = LanReleaseRxPacket;
    Exports->KdGetTxPacket = LanGetTxPacket;
    Exports->KdSendTxPacket = LanSendTxPacket;
    Exports->KdGetPacketAddress = LanGetPacketAddress;
    Exports->KdGetPacketLength = LanGetPacketLength;
    Exports->KdGetHardwareContextSize = LanGetHardwareContextSize;
    Device->Memory.Length = LanGetHardwareContextSize(Device);
    Device->Memory.Cached = FALSE;
    return STATUS_SUCCESS;
}
