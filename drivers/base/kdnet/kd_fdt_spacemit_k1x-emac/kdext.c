/*
 * PROJECT:     LiberNT SpacemiT K1 Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KDNET extensibility module entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdk1x.h"

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

static
VOID
NTAPI
K1xSetHibernateRange(VOID)
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

    if (!Device->OemData || Device->OemDataLength != sizeof(KD_FDT_DEVICE))
        return STATUS_NOT_SUPPORTED;

    KdNetExtensibilityImports = ImportTable;
    Exports->KdInitializeController = K1xInitializeController;
    Exports->KdShutdownController = K1xShutdownController;
    Exports->KdSetHibernateRange = K1xSetHibernateRange;
    Exports->KdGetRxPacket = K1xGetRxPacket;
    Exports->KdReleaseRxPacket = K1xReleaseRxPacket;
    Exports->KdGetTxPacket = K1xGetTxPacket;
    Exports->KdSendTxPacket = K1xSendTxPacket;
    Exports->KdGetPacketAddress = K1xGetPacketAddress;
    Exports->KdGetPacketLength = K1xGetPacketLength;
    Exports->KdGetHardwareContextSize = K1xGetHardwareContextSize;
    Device->Memory.Length = K1xGetHardwareContextSize(Device);
    return STATUS_SUCCESS;
}
