/*
 * PROJECT:     LiberNT Intel Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KDNET extensibility module entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kd8086.h"

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

static
VOID
NTAPI
E1kSetHibernateRange(VOID)
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

    if (E1kClassifyDevice(Device) == E1kClassNone)
        return STATUS_NOT_SUPPORTED;

    KdNetExtensibilityImports = ImportTable;
    Exports->KdInitializeController = E1kInitializeController;
    Exports->KdShutdownController = E1kShutdownController;
    Exports->KdSetHibernateRange = E1kSetHibernateRange;
    Exports->KdGetRxPacket = E1kGetRxPacket;
    Exports->KdReleaseRxPacket = E1kReleaseRxPacket;
    Exports->KdGetTxPacket = E1kGetTxPacket;
    Exports->KdSendTxPacket = E1kSendTxPacket;
    Exports->KdGetPacketAddress = E1kGetPacketAddress;
    Exports->KdGetPacketLength = E1kGetPacketLength;
    Exports->KdGetHardwareContextSize = E1kGetHardwareContextSize;
    Device->Memory.Length = E1kGetHardwareContextSize(Device);
    return STATUS_SUCCESS;
}
