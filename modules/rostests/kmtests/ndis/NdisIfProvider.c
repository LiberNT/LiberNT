/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS interface provider and NET_LUID index tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define NDIS60 1
#include <kmt_test.h>
#include <ndis.h>
#include "NdisTestExport.h"

#define TEST_IF_TYPE IF_TYPE_PROP_VIRTUAL

typedef NDIS_STATUS (NTAPI *PFN_IF_REGISTER_PROVIDER)(PNDIS_IF_PROVIDER_CHARACTERISTICS, NDIS_HANDLE, PNDIS_HANDLE);
typedef VOID (NTAPI *PFN_IF_DEREGISTER_PROVIDER)(NDIS_HANDLE);
typedef NDIS_STATUS (NTAPI *PFN_IF_ALLOCATE_LUID_INDEX)(NET_IFTYPE, PUINT32);
typedef NDIS_STATUS (NTAPI *PFN_IF_FREE_LUID_INDEX)(NET_IFTYPE, UINT32);
typedef NDIS_STATUS (NTAPI *PFN_IF_REGISTER_INTERFACE)(NDIS_HANDLE, NET_LUID, NDIS_HANDLE, PNET_IF_INFORMATION, PNET_IFINDEX);
typedef VOID (NTAPI *PFN_IF_DEREGISTER_INTERFACE)(NET_IFINDEX);
typedef NDIS_STATUS (NTAPI *PFN_IF_INDEX_FROM_LUID)(NET_LUID, PNET_IFINDEX);
typedef NDIS_STATUS (NTAPI *PFN_IF_LUID_FROM_INDEX)(NET_IFINDEX, PNET_LUID);

static
NDIS_STATUS
TestQueryObject(
    _In_ NDIS_HANDLE ProviderIfContext,
    _In_ NET_IF_OBJECT_ID ObjectId,
    _Inout_ PULONG OutputBufferLength,
    _Out_ PVOID OutputBuffer)
{
    UNREFERENCED_PARAMETER(ProviderIfContext);
    UNREFERENCED_PARAMETER(OutputBuffer);
    trace("query object %lx length %lu\n", ObjectId, *OutputBufferLength);
    return NDIS_STATUS_NOT_SUPPORTED;
}

static
NDIS_STATUS
TestSetObject(
    _In_ NDIS_HANDLE ProviderIfContext,
    _In_ NET_IF_OBJECT_ID ObjectId,
    _In_ ULONG InputBufferLength,
    _In_ PVOID InputBuffer)
{
    UNREFERENCED_PARAMETER(ProviderIfContext);
    UNREFERENCED_PARAMETER(InputBuffer);
    trace("set object %lx length %lu\n", ObjectId, InputBufferLength);
    return NDIS_STATUS_NOT_SUPPORTED;
}

START_TEST(NdisIfProvider)
{
    PFN_IF_REGISTER_PROVIDER pRegisterProvider = NdisTestFindExport("NdisIfRegisterProvider");
    PFN_IF_DEREGISTER_PROVIDER pDeregisterProvider = NdisTestFindExport("NdisIfDeregisterProvider");
    PFN_IF_ALLOCATE_LUID_INDEX pAllocate = NdisTestFindExport("NdisIfAllocateNetLuidIndex");
    PFN_IF_FREE_LUID_INDEX pFree = NdisTestFindExport("NdisIfFreeNetLuidIndex");
    PFN_IF_REGISTER_INTERFACE pRegisterInterface = NdisTestFindExport("NdisIfRegisterInterface");
    PFN_IF_DEREGISTER_INTERFACE pDeregisterInterface = NdisTestFindExport("NdisIfDeregisterInterface");
    PFN_IF_INDEX_FROM_LUID pIndexFromLuid = NdisTestFindExport("NdisIfGetInterfaceIndexFromNetLuid");
    PFN_IF_LUID_FROM_INDEX pLuidFromIndex = NdisTestFindExport("NdisIfGetNetLuidFromInterfaceIndex");
    NDIS_IF_PROVIDER_CHARACTERISTICS Characteristics;
    NET_IF_INFORMATION Info;
    NDIS_HANDLE Provider = NULL;
    NET_IFINDEX IfIndex = 0, IfIndex2 = 0, Found;
    NET_LUID Luid, FoundLuid;
    UINT32 Index1 = 0, Index2 = 0, Index3 = 0;
    NDIS_STATUS Status;

    if (!pRegisterProvider || !pDeregisterProvider || !pAllocate || !pFree ||
        !pRegisterInterface || !pDeregisterInterface || !pIndexFromLuid || !pLuidFromIndex)
    {
        skip(FALSE, "NDIS interface provider exports not available\n");
        return;
    }

    Status = pAllocate(TEST_IF_TYPE, &Index1);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    Status = pAllocate(TEST_IF_TYPE, &Index2);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    ok(Index2 != Index1, "Index2 = %lu, Index1 = %lu\n", Index2, Index1);
    Status = pFree(TEST_IF_TYPE, Index1);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    Status = pAllocate(TEST_IF_TYPE, &Index3);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    ok_eq_ulong(Index3, Index1);
    Status = pFree(TEST_IF_TYPE, 0x7FF0);
    ok_eq_hex(Status, NDIS_STATUS_INVALID_PARAMETER);

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Characteristics.Header.Revision = 1;
    Characteristics.Header.Size = NDIS_SIZEOF_IF_PROVIDER_CHARACTERISTICS_REVISION_1;
    Characteristics.QueryObjectHandler = TestQueryObject;
    Characteristics.SetObjectHandler = TestSetObject;
    Status = pRegisterProvider(&Characteristics, (NDIS_HANDLE)0x1234, &Provider);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    if (Status != NDIS_STATUS_SUCCESS)
        goto FreeIndexes;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Info.Header.Revision = 1;
    Info.Header.Size = NDIS_SIZEOF_NET_IF_INFORMATION_REVISION_1;
    Info.AccessType = NET_IF_ACCESS_BROADCAST;
    Info.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    Info.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    Info.MediaType = NdisMedium802_3;
    Info.PhysicalMediumType = NdisPhysicalMediumUnspecified;

    Luid.Value = 0;
    Luid.Info.IfType = TEST_IF_TYPE;
    Luid.Info.NetLuidIndex = Index3;
    Status = pRegisterInterface(Provider, Luid, (NDIS_HANDLE)0x5678, &Info, &IfIndex);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    ok(IfIndex != 0, "IfIndex = %lu\n", IfIndex);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        Found = 0;
        Status = pIndexFromLuid(Luid, &Found);
        ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
        ok_eq_ulong(Found, IfIndex);

        FoundLuid.Value = 0;
        Status = pLuidFromIndex(IfIndex, &FoundLuid);
        ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
        ok_eq_ulonglong(FoundLuid.Value, Luid.Value);

        Status = pRegisterInterface(Provider, Luid, (NDIS_HANDLE)0x5679, &Info, &IfIndex2);
        ok_eq_hex(Status, (NDIS_STATUS)STATUS_DUPLICATE_OBJECTID);
        ok_eq_ulong(IfIndex2, 0);
        if (Status == NDIS_STATUS_SUCCESS)
            pDeregisterInterface(IfIndex2);

        pDeregisterInterface(IfIndex);
        Found = 0;
        Status = pIndexFromLuid(Luid, &Found);
        ok_eq_hex(Status, NDIS_STATUS_INTERFACE_NOT_FOUND);
        Status = pLuidFromIndex(IfIndex, &FoundLuid);
        ok_eq_hex(Status, NDIS_STATUS_INTERFACE_NOT_FOUND);

        IfIndex2 = 0;
        Status = pRegisterInterface(Provider, Luid, (NDIS_HANDLE)0x5678, &Info, &IfIndex2);
        ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
        ok_eq_ulong(IfIndex2, IfIndex);
        if (Status == NDIS_STATUS_SUCCESS)
            pDeregisterInterface(IfIndex2);
    }

    Status = pLuidFromIndex(0x00FFFFF0, &FoundLuid);
    ok_eq_hex(Status, NDIS_STATUS_INTERFACE_NOT_FOUND);

    pDeregisterProvider(Provider);

FreeIndexes:
    Status = pFree(TEST_IF_TYPE, Index2);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
    Status = pFree(TEST_IF_TYPE, Index3);
    ok_eq_hex(Status, NDIS_STATUS_SUCCESS);
}
