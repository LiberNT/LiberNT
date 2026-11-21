/*
 * PROJECT:     LiberNT Device Tree Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device-tree access for drivers of devices the HAL enumerates from the firmware tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#include <stdarg.h>
#include <stdio.h>
#endif
#include <libernt/drivers/fdtdrv.h>

#define FDT_DRIVER_TAG 'vDdF'

static
PVOID
NTAPI
FdtDriverSystemMap(
    _In_ ULONG64 Physical,
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = (LONGLONG)Physical;
    return MmMapIoSpace(Address, Size, MmNonCached);
}

static
VOID
NTAPI
FdtDriverSystemUnmap(
    _In_ PVOID Address,
    _In_ ULONG Size)
{
    MmUnmapIoSpace(Address, Size);
}

static
VOID
NTAPI
FdtDriverSystemStall(
    _In_ ULONG Microseconds)
{
    KeStallExecutionProcessor(Microseconds);
}

static
VOID
NTAPI
FdtDriverSystemSleep(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
VOID
__cdecl
FdtDriverSystemPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Text[256];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Text, sizeof(Text) - 1, Format, Arguments);
    va_end(Arguments);
    Text[sizeof(Text) - 1] = '\0';
    DbgPrint("%s", Text);
}

static const FDT_DRIVER_IO FdtDriverSystemIo =
{
    FdtDriverSystemMap,
    FdtDriverSystemUnmap,
    FdtDriverSystemStall,
    FdtDriverSystemSleep,
    FdtDriverSystemPrint
};

PCFDT_DRIVER_IO
NTAPI
FdtDriverGetSystemIo(VOID)
{
    return &FdtDriverSystemIo;
}

static
NTSTATUS
FdtDriverLoadBlob(
    _Out_ PFDT_DRIVER_TREE Tree)
{
    static UNICODE_STRING SystemKey = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System");
    static UNICODE_STRING BlobName = RTL_CONSTANT_STRING(L"DeviceTree");
    PKEY_VALUE_PARTIAL_INFORMATION Info = NULL;
    OBJECT_ATTRIBUTES Attributes;
    ULONG Length;
    HANDLE Key;
    NTSTATUS Status;

    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->Node = FDT_NO_NODE;
    Tree->Io = &FdtDriverSystemIo;
    InitializeObjectAttributes(&Attributes, &SystemKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW)
    {
        Info = ExAllocatePoolWithTag(PagedPool, Length, FDT_DRIVER_TAG);
        Status = Info ? ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, Info, Length, &Length) :
                        STATUS_INSUFFICIENT_RESOURCES;
    }
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    Tree->Blob = ExAllocatePoolWithTag(NonPagedPool, Info->DataLength, FDT_DRIVER_TAG);
    if (!Tree->Blob)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(Tree->Blob, Info->Data, Info->DataLength);
    if (!FdtOpen(Tree->Blob, Info->DataLength, &Tree->Fdt))
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;

Done:
    if (Info)
        ExFreePoolWithTag(Info, FDT_DRIVER_TAG);
    return Status;
}

NTSTATUS
NTAPI
FdtDriverOpen(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    static UNICODE_STRING PathName = RTL_CONSTANT_STRING(L"FdtPath");
    UCHAR PathBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 256 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION PathInfo = (PKEY_VALUE_PARTIAL_INFORMATION)PathBuffer;
    CHAR Path[256];
    ULONG Length, Index;
    PWCHAR Wide;
    HANDLE Key;
    NTSTATUS Status;

    Status = FdtDriverLoadBlob(Tree);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = IoOpenDeviceRegistryKey(PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = ZwQueryValueKey(Key, &PathName, KeyValuePartialInformation, PathInfo, sizeof(PathBuffer), &Length);
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (PathInfo->Type != REG_SZ)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    Wide = (PWCHAR)PathInfo->Data;
    for (Index = 0; Index < sizeof(Path) - 1 && Index < PathInfo->DataLength / sizeof(WCHAR) && Wide[Index]; ++Index)
        Path[Index] = (CHAR)Wide[Index];
    Path[Index] = '\0';
    if (!FdtFindNode(&Tree->Fdt, Path, &Tree->Node, NULL))
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;

Done:
    if (!NT_SUCCESS(Status))
        FdtDriverClose(Tree);
    return Status;
}

NTSTATUS
NTAPI
FdtDriverOpenByCompatible(
    _In_z_ PCSTR Compatible,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    const VOID *List;
    ULONG Offset, Length;
    NTSTATUS Status;

    Status = FdtDriverLoadBlob(Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Offset = FdtRootNode(&Tree->Fdt); Offset != FDT_NO_NODE; Offset = FdtSkipToken(&Tree->Fdt, Offset))
    {
        if (FdtReadToken(&Tree->Fdt, Offset) != FDT_BEGIN_NODE)
            continue;
        List = FdtGetProperty(&Tree->Fdt, Offset, "compatible", &Length);
        if (List && FdtStringListContains(List, Length, Compatible))
        {
            Tree->Node = Offset;
            return STATUS_SUCCESS;
        }
    }
    FdtDriverClose(Tree);
    return STATUS_NOT_FOUND;
}

NTSTATUS
NTAPI
FdtDriverOpenAlias(
    _In_z_ PCSTR PathOrAlias,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    NTSTATUS Status;

    Status = FdtDriverLoadBlob(Tree);
    if (NT_SUCCESS(Status) &&
        !FdtResolvePath(&Tree->Fdt, PathOrAlias, &Tree->Node, NULL, NULL))
    {
        Status = STATUS_NOT_FOUND;
    }
    if (!NT_SUCCESS(Status))
        FdtDriverClose(Tree);
    return Status;
}

VOID
NTAPI
FdtDriverClose(
    _Inout_ PFDT_DRIVER_TREE Tree)
{
    if (Tree->Blob)
        ExFreePoolWithTag(Tree->Blob, FDT_DRIVER_TAG);
    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->Node = FDT_NO_NODE;
}

NTSTATUS
NTAPI
FdtDriverOpenByAddress(
    _In_z_ PCSTR Compatible,
    _In_ ULONG64 Address,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    static UNICODE_STRING SystemKey = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System");
    static UNICODE_STRING BlobName = RTL_CONSTANT_STRING(L"DeviceTree");
    PKEY_VALUE_PARTIAL_INFORMATION Info = NULL;
    OBJECT_ATTRIBUTES Attributes;
    ULONG Length, Offset;
    HANDLE Key;
    NTSTATUS Status;

    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->Node = FDT_NO_NODE;
    Tree->Io = &FdtDriverSystemIo;
    InitializeObjectAttributes(&Attributes, &SystemKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW)
    {
        Info = ExAllocatePoolWithTag(PagedPool, Length, FDT_DRIVER_TAG);
        Status = Info ? ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, Info, Length, &Length) :
                        STATUS_INSUFFICIENT_RESOURCES;
    }
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    Tree->Blob = ExAllocatePoolWithTag(NonPagedPool, Info->DataLength, FDT_DRIVER_TAG);
    if (!Tree->Blob)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(Tree->Blob, Info->Data, Info->DataLength);
    if (!FdtOpen(Tree->Blob, Info->DataLength, &Tree->Fdt))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    Status = STATUS_NOT_FOUND;
    for (Offset = FdtRootNode(&Tree->Fdt); Offset != FDT_NO_NODE; Offset = FdtSkipToken(&Tree->Fdt, Offset))
    {
        const VOID *List;
        ULONG64 Base, Size;

        if (FdtReadToken(&Tree->Fdt, Offset) != FDT_BEGIN_NODE)
            continue;
        List = FdtGetProperty(&Tree->Fdt, Offset, "compatible", &Length);
        if (!List || !FdtStringListContains(List, Length, Compatible))
            continue;
        if (FdtDriverReadReg(Tree, Offset, 0, &Base, &Size) && Base == Address)
        {
            Tree->Node = Offset;
            Status = STATUS_SUCCESS;
            break;
        }
    }

Done:
    if (Info)
        ExFreePoolWithTag(Info, FDT_DRIVER_TAG);
    if (!NT_SUCCESS(Status))
        FdtDriverClose(Tree);
    return Status;
}
