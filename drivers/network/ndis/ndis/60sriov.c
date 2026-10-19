/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6.30 SR-IOV entry points; virtualization is not supported
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

NDIS_STATUS
EXPORT
NdisMEnableVirtualization(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ USHORT NumVFs,
    _In_ BOOLEAN EnableVFMigration,
    _In_ BOOLEAN EnableMigrationInterrupt,
    _In_ BOOLEAN EnableVirtualization)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(NumVFs);
    UNREFERENCED_PARAMETER(EnableVFMigration);
    UNREFERENCED_PARAMETER(EnableMigrationInterrupt);
    return EnableVirtualization ? NDIS_STATUS_NOT_SUPPORTED : NDIS_STATUS_SUCCESS;
}

ULONG
EXPORT
NdisMGetVirtualFunctionBusData(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_SRIOV_FUNCTION_ID VFId,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(VFId);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}

ULONG
EXPORT
NdisMSetVirtualFunctionBusData(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_SRIOV_FUNCTION_ID VFId,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(VFId);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}

VOID
EXPORT
NdisMGetVirtualFunctionLocation(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_SRIOV_FUNCTION_ID VFId,
    _Out_ PUSHORT SegmentNumber,
    _Out_ PUCHAR BusNumber,
    _Out_ PUCHAR FunctionNumber)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(VFId);
    *SegmentNumber = 0;
    *BusNumber = 0;
    *FunctionNumber = 0;
}

NDIS_STATUS
EXPORT
NdisMQueryProbedBars(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _Out_writes_(PCI_TYPE0_ADDRESSES) PULONG BaseRegisterValues)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    RtlZeroMemory(BaseRegisterValues, PCI_TYPE0_ADDRESSES * sizeof(ULONG));
    return NDIS_STATUS_NOT_SUPPORTED;
}

/* EOF */
