/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Device-tree controllers: the tree kdnet found and the I/O the platform glue runs on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"
#include <kdfdt.h>
#include <reactos/drivers/fdtdrv.h>

static
PVOID
NTAPI
UsbNetTreeMap(
    _In_ ULONG64 Physical,
    _In_ ULONG Size)
{
    ULONG Offset = (ULONG)(Physical & (PAGE_SIZE - 1));
    PHYSICAL_ADDRESS Address;
    PUCHAR Mapping;

    if (!Size)
        return NULL;
    Address.QuadPart = (LONGLONG)(Physical - Offset);
    Mapping = KdNetExtensibilityImports->MapPhysicalMemory64(Address, (ULONG)BYTES_TO_PAGES(Offset + Size), FALSE);
    return Mapping ? Mapping + Offset : NULL;
}

static
VOID
NTAPI
UsbNetTreeUnmap(
    _In_ PVOID Address,
    _In_ ULONG Size)
{
    ULONG Offset = (ULONG)((ULONG_PTR)Address & (PAGE_SIZE - 1));

    KdNetExtensibilityImports->UnmapVirtualAddress((PUCHAR)Address - Offset, (ULONG)BYTES_TO_PAGES(Offset + Size),
                                                   FALSE);
}

static
VOID
NTAPI
UsbNetTreeStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

static
VOID
NTAPI
UsbNetTreeSleep(
    _In_ ULONG Milliseconds)
{
    ULONG Waited;

    for (Waited = 0; Waited < Milliseconds; Waited++)
        KdNetExtensibilityImports->StallExecutionProcessor(1000);
}

static FDT_DRIVER_IO UsbNetTreeIo =
{
    UsbNetTreeMap,
    UsbNetTreeUnmap,
    UsbNetTreeStall,
    UsbNetTreeSleep,
    NULL
};

NTSTATUS
UsbNetTreeAttach(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    if (Device->NameSpace != KdNameSpaceNone || !Device->OemData || Device->OemDataLength != sizeof(KD_FDT_DEVICE))
        return STATUS_NOT_SUPPORTED;

    Device->Memory.MaxEnd.QuadPart = MAXLONGLONG;
    return STATUS_SUCCESS;
}

NTSTATUS
UsbNetTreeOpen(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    PKD_FDT_DEVICE Platform = Device->OemData;

    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->Node = FDT_NO_NODE;
    if (Device->NameSpace != KdNameSpaceNone || !Platform || Device->OemDataLength != sizeof(*Platform))
        return STATUS_NOT_SUPPORTED;

    if (!FdtOpen(Platform->Blob, Platform->Size, &Tree->Fdt))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    UsbNetTreeIo.Print = (VOID (__cdecl *)(PCSTR, ...))KdNetExtensibilityImports->KdNetDbgPrintf;
    Tree->Blob = (PUCHAR)Platform->Blob;
    Tree->Node = Platform->Node;
    Tree->Io = &UsbNetTreeIo;
    return STATUS_SUCCESS;
}
