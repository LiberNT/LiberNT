/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Controller glue of an xHCI controller found on PCI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

NTSTATUS
UsbNetGlueAttach(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    return Device->NameSpace == KdNameSpacePCI ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

NTSTATUS
UsbNetGluePrepare(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    UNREFERENCED_PARAMETER(KdNet);

    return STATUS_SUCCESS;
}
