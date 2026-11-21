/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Controller glue of an Apple silicon Type-C DWC3 controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"
#include <libernt/drivers/fdtdrv.h>
#include <libernt/drivers/applepmgr.h>
#include <libernt/drivers/appledart.h>
#include <libernt/drivers/appledwc3host.h>

#define USBNET_APPLE_OWNER "USB debugger"

NTSTATUS
UsbNetGlueAttach(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    return UsbNetTreeAttach(Device);
}

NTSTATUS
UsbNetGluePrepare(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    FDT_DRIVER_TREE Tree;
    ULONG64 CoreBase, Size;
    NTSTATUS Status;

    Status = UsbNetTreeOpen(KdNet->Device, &Tree);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FdtDriverReadReg(&Tree, Tree.Node, 0, &CoreBase, &Size) || Size < APPLE_DWC3_REGISTER_SPAN)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    UsbNetTrace("USB debugger: starting the Apple USB-C controller at %I64x\n", CoreBase);
    Status = ApplePmgrPowerOnDevice(&Tree, Tree.Node);
    if (!NT_SUCCESS(Status))
    {
        UsbNetTrace("USB debugger: %I64x power domains did not turn on: %lx\n", CoreBase, Status);
        return Status;
    }

    Status = AppleDartMapPhysical(&Tree, Tree.Node, USBNET_APPLE_OWNER);
    if (!NT_SUCCESS(Status))
    {
        UsbNetTrace("USB debugger: %I64x DMA translation is not set up: %lx\n", CoreBase, Status);
        return Status;
    }

    return AppleDwc3StartHost(&Tree, Tree.Node, CoreBase, USBNET_APPLE_OWNER);
}

NTSTATUS
AppleDartIdentityStream(
    _In_ PUCHAR Base,
    _In_ ULONG Sid,
    _In_ ULONG Params3,
    _In_z_ PCSTR Owner)
{
    UNREFERENCED_PARAMETER(Base);
    UNREFERENCED_PARAMETER(Params3);

    /* TODO: identity tables in the debugger's hardware context for a DART stream without bypass */
    UsbNetTrace("%s: DART stream %lu has no bypass, and the debugger builds no translation tables\n", Owner, Sid);
    return STATUS_NOT_SUPPORTED;
}
