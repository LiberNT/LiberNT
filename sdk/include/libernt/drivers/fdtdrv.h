/*
 * PROJECT:     LiberNT Device Tree Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device-tree access for drivers of devices the HAL enumerates from the firmware tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/fdtlib.h>

typedef struct _FDT_DRIVER_IO
{
    PVOID (NTAPI *Map)(_In_ ULONG64 Physical, _In_ ULONG Size);
    VOID (NTAPI *Unmap)(_In_ PVOID Address, _In_ ULONG Size);
    VOID (NTAPI *Stall)(_In_ ULONG Microseconds);
    VOID (NTAPI *Sleep)(_In_ ULONG Milliseconds);
    VOID (__cdecl *Print)(_In_z_ _Printf_format_string_ PCSTR Format, ...);
} FDT_DRIVER_IO, *PFDT_DRIVER_IO;

typedef const FDT_DRIVER_IO *PCFDT_DRIVER_IO;

typedef struct _FDT_DRIVER_TREE
{
    PUCHAR Blob;
    FDT Fdt;
    ULONG Node;
    PCFDT_DRIVER_IO Io;
} FDT_DRIVER_TREE, *PFDT_DRIVER_TREE;

#define FdtDriverIoPrint(Io, ...) ((Io)->Print(__VA_ARGS__))
#define FdtDriverPrint(Tree, ...) FdtDriverIoPrint((Tree)->Io, __VA_ARGS__)

PCFDT_DRIVER_IO
NTAPI
FdtDriverGetSystemIo(VOID);

NTSTATUS
NTAPI
FdtDriverOpen(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PFDT_DRIVER_TREE Tree);

NTSTATUS
NTAPI
FdtDriverOpenByAddress(
    _In_z_ PCSTR Compatible,
    _In_ ULONG64 Address,
    _Out_ PFDT_DRIVER_TREE Tree);

NTSTATUS
NTAPI
FdtDriverOpenByCompatible(
    _In_z_ PCSTR Compatible,
    _Out_ PFDT_DRIVER_TREE Tree);

NTSTATUS
NTAPI
FdtDriverOpenAlias(
    _In_z_ PCSTR PathOrAlias,
    _Out_ PFDT_DRIVER_TREE Tree);

VOID
NTAPI
FdtDriverClose(
    _Inout_ PFDT_DRIVER_TREE Tree);

ULONG
NTAPI
FdtDriverFindPhandle(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Phandle);

ULONG
NTAPI
FdtDriverFindParentNode(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node);

ULONG
NTAPI
FdtDriverFindChild(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Compatible);

BOOLEAN
NTAPI
FdtDriverReadReg(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_ PULONG64 Address,
    _Out_ PULONG64 Size);

PUCHAR
NTAPI
FdtDriverMapReg(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_opt_ PULONG64 Address,
    _Out_ PULONG Size);

ULONG
NTAPI
FdtDriverFindRegIndex(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Name);
