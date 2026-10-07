/*
 * PROJECT:     LiberNT Realtek Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KDNET extensibility module entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kd10ec.h"

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

static
VOID
NTAPI
RtlSetHibernateRange(VOID)
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
    if (RtlIdentifyDevice(Device) == RtlFamilyNone)
        return STATUS_NOT_SUPPORTED;

    Exports->KdInitializeController = RtlInitializeController;
    Exports->KdShutdownController = RtlShutdownController;
    Exports->KdSetHibernateRange = RtlSetHibernateRange;
    Exports->KdGetRxPacket = RtlGetRxPacket;
    Exports->KdReleaseRxPacket = RtlReleaseRxPacket;
    Exports->KdGetTxPacket = RtlGetTxPacket;
    Exports->KdSendTxPacket = RtlSendTxPacket;
    Exports->KdGetPacketAddress = RtlGetPacketAddress;
    Exports->KdGetPacketLength = RtlGetPacketLength;
    Exports->KdGetHardwareContextSize = RtlGetHardwareContextSize;
    Device->Memory.Length = RtlGetHardwareContextSize(Device);
    return STATUS_SUCCESS;
}
