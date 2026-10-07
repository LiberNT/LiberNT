/*
 * PROJECT:     LiberNT VirtIO Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KDNET extensibility module entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kd1af4.h"

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

static
VOID
NTAPI
VioSetHibernateRange(VOID)
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

    KdNetExtensibilityImports = ImportTable;
    if (!VioIsSupportedDevice(Device))
        return STATUS_NOT_SUPPORTED;

    Exports->KdInitializeController = VioInitializeController;
    Exports->KdShutdownController = VioShutdownController;
    Exports->KdSetHibernateRange = VioSetHibernateRange;
    Exports->KdGetRxPacket = VioGetRxPacket;
    Exports->KdReleaseRxPacket = VioReleaseRxPacket;
    Exports->KdGetTxPacket = VioGetTxPacket;
    Exports->KdSendTxPacket = VioSendTxPacket;
    Exports->KdGetPacketAddress = VioGetPacketAddress;
    Exports->KdGetPacketLength = VioGetPacketLength;
    Exports->KdGetHardwareContextSize = VioGetHardwareContextSize;
    Device->Memory.Length = VioGetHardwareContextSize(Device);
    return STATUS_SUCCESS;
}
