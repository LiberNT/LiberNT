/*
 * PROJECT:     LiberNT Raspberry Pi RP1 Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KDNET extensibility module entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdrp1.h"

#define RP1_DBG2_PORT_NET   0x8003
#define RP1_DBG2_SUBTYPE    0x1DE4

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

static
VOID
NTAPI
GemSetHibernateRange(VOID)
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

    if (Device->PortType != RP1_DBG2_PORT_NET || Device->PortSubtype != RP1_DBG2_SUBTYPE)
        return STATUS_NOT_SUPPORTED;

    KdNetExtensibilityImports = ImportTable;
    Exports->KdInitializeController = GemInitializeController;
    Exports->KdShutdownController = GemShutdownController;
    Exports->KdSetHibernateRange = GemSetHibernateRange;
    Exports->KdGetRxPacket = GemGetRxPacket;
    Exports->KdReleaseRxPacket = GemReleaseRxPacket;
    Exports->KdGetTxPacket = GemGetTxPacket;
    Exports->KdSendTxPacket = GemSendTxPacket;
    Exports->KdGetPacketAddress = GemGetPacketAddress;
    Exports->KdGetPacketLength = GemGetPacketLength;
    Exports->KdGetHardwareContextSize = GemGetHardwareContextSize;
    Device->Memory.Length = GemGetHardwareContextSize(Device);
    Device->Memory.Cached = FALSE;
    Device->Memory.MaxEnd.QuadPart = MAXLONGLONG;
    return STATUS_SUCCESS;
}
