/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 ABIOS selector table and its GDT entries
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
KeI386FlatToGdtSelector(
    _In_ ULONG Base,
    _In_ USHORT Length,
    _In_ USHORT Selector)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KeI386ReleaseGdtSelectors(
    _Out_ PULONG SelArray,
    _In_ ULONG NumOfSelectors)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KeI386AllocateGdtSelectors(
    _Out_ PULONG SelArray,
    _In_ ULONG NumOfSelectors)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KeI386SetGdtSelector(
    _In_ ULONG Selector,
    _In_ PKGDTENTRY GdtValue)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KeI386AbiosCall(
    _In_ USHORT CallId,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PULONG RequestBlock,
    _In_ USHORT EntryPoint)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KeI386GetLid(
    _In_ USHORT DeviceId,
    _In_ USHORT RelativeLid,
    _In_ BOOLEAN SharedLid,
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_ PUSHORT LogicalId)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KeI386ReleaseLid(
    _In_ USHORT LogicalId,
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

USHORT
NTAPI
KeI386Call16BitCStyleFunction(
    _In_ ULONG EntryOffset,
    _In_ ULONG EntrySelector,
    _In_ PULONG Parameters,
    _In_ ULONG Size)
{
    UNIMPLEMENTED;
    return 0;
}

VOID
NTAPI
KeI386Call16BitFunction(
    _Inout_ PCONTEXT Context)
{
    UNIMPLEMENTED;
}
