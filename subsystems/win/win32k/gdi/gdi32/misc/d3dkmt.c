/*
 * PROJECT:     LiberNT GDI32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3DKMT entry points that read their descriptor in user mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <precomp.h>

NTSTATUS
APIENTRY
D3DKMTCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *pData)
{
    D3DKMT_CREATEALLOCATION Create = *pData;
    D3DDDI_ALLOCATIONINFO *Info = Create.pAllocationInfo;
    D3DDDI_ALLOCATIONINFO2 *Info2 = NULL;
    NTSTATUS Status;
    UINT Count = 0;
    UINT Index;

    if (Info != NULL && Create.NumAllocations != 0)
    {
        Count = Create.NumAllocations;
        if ((SIZE_T)Count > MAXSIZE_T / sizeof(*Info2))
            return STATUS_INVALID_PARAMETER;
        Info2 = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Count * sizeof(*Info2));
        if (Info2 == NULL)
            return STATUS_NO_MEMORY;
        for (Index = 0; Index < Count; ++Index)
        {
            Info2[Index].hAllocation = Info[Index].hAllocation;
            Info2[Index].pSystemMem = Info[Index].pSystemMem;
            Info2[Index].pPrivateDriverData = Info[Index].pPrivateDriverData;
            Info2[Index].PrivateDriverDataSize = Info[Index].PrivateDriverDataSize;
            Info2[Index].VidPnSourceId = Info[Index].VidPnSourceId;
            Info2[Index].Flags.Value = Info[Index].Flags.Value;
        }
    }
    Create.pAllocationInfo2 = Info2;
    Status = NtGdiDdDDICreateAllocation(&Create);
    if (NT_SUCCESS(Status))
    {
        pData->hResource = Create.hResource;
        pData->hGlobalShare = Create.hGlobalShare;
        for (Index = 0; Index < Count; ++Index)
            Info[Index].hAllocation = Info2[Index].hAllocation;
    }
    if (Info2 != NULL)
        HeapFree(GetProcessHeap(), 0, Info2);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pData)
{
    D3DKMT_OPENRESOURCE Open;
    D3DDDI_OPENALLOCATIONINFO *Info;
    D3DDDI_OPENALLOCATIONINFO2 *Info2 = NULL;
    NTSTATUS Status;
    UINT Count = 0;
    UINT Index;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Open = *pData;
    Info = Open.pOpenAllocationInfo;
    if (Info != NULL && Open.NumAllocations != 0)
    {
        Count = Open.NumAllocations;
        if ((SIZE_T)Count > MAXSIZE_T / sizeof(*Info2))
            return STATUS_INVALID_PARAMETER;
        Info2 = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Count * sizeof(*Info2));
        if (Info2 == NULL)
            return STATUS_NO_MEMORY;
        for (Index = 0; Index < Count; ++Index)
        {
            Info2[Index].hAllocation = Info[Index].hAllocation;
            Info2[Index].pPrivateDriverData = Info[Index].pPrivateDriverData;
            Info2[Index].PrivateDriverDataSize = Info[Index].PrivateDriverDataSize;
        }
    }
    Open.pOpenAllocationInfo2 = Info2;
    Status = NtGdiDdDDIOpenResource(&Open);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Open.NumAllocations && Index < Count; ++Index)
        {
            Info[Index].hAllocation = Info2[Index].hAllocation;
            Info[Index].pPrivateDriverData = Info2[Index].pPrivateDriverData;
            Info[Index].PrivateDriverDataSize = Info2[Index].PrivateDriverDataSize;
        }
        Open.pOpenAllocationInfo = Info;
        *pData = Open;
    }
    if (Info2 != NULL)
        HeapFree(GetProcessHeap(), 0, Info2);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromGdiDisplayName(
    _Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *pData)
{
    WCHAR DeviceName[RTL_NUMBER_OF(pData->DeviceName)];
    D3DKMT_OPENADAPTERFROMHDC Open;
    NTSTATUS Status;
    HDC hDC;

    if (pData == NULL)
        return STATUS_UNSUCCESSFUL;
    RtlCopyMemory(DeviceName, pData->DeviceName, sizeof(DeviceName));
    if (DeviceName[0] == UNICODE_NULL || wcsnlen(DeviceName, RTL_NUMBER_OF(DeviceName)) == RTL_NUMBER_OF(DeviceName))
        return STATUS_UNSUCCESSFUL;
    hDC = CreateDCW(DeviceName, NULL, NULL, NULL);
    if (hDC == NULL)
        return STATUS_UNSUCCESSFUL;
    RtlZeroMemory(&Open, sizeof(Open));
    Open.hDc = hDC;
    Status = NtGdiDdDDIOpenAdapterFromHdc(&Open);
    DeleteDC(hDC);
    if (NT_SUCCESS(Status))
    {
        pData->hAdapter = Open.hAdapter;
        pData->AdapterLuid = Open.AdapterLuid;
        pData->VidPnSourceId = Open.VidPnSourceId;
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    NTSTATUS Status;

    C_ASSERT(sizeof(Create.Info.Reserved) >= sizeof(pData->Info.Reserved));

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&Create, sizeof(Create));
    Create.hDevice = pData->hDevice;
    Create.Info.Type = pData->Info.Type;
    RtlCopyMemory(&Create.Info.Reserved, &pData->Info.Reserved, sizeof(pData->Info.Reserved));
    Status = NtGdiDdDDICreateSynchronizationObject(&Create);
    if (NT_SUCCESS(Status))
        pData->hSyncObject = Create.hSyncObject;
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObject(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 Signal;

    C_ASSERT(sizeof(Signal.ObjectHandleArray) == sizeof(pData->ObjectHandleArray));

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&Signal, sizeof(Signal));
    Signal.hContext = pData->hContext;
    Signal.ObjectCount = pData->ObjectCount;
    RtlCopyMemory(Signal.ObjectHandleArray, pData->ObjectHandleArray, sizeof(Signal.ObjectHandleArray));
    Signal.Flags = pData->Flags;
    return NtGdiDdDDISignalSynchronizationObject(&Signal);
}

NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObject(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 Wait;

    C_ASSERT(sizeof(Wait.ObjectHandleArray) == sizeof(pData->ObjectHandleArray));

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&Wait, sizeof(Wait));
    Wait.hContext = pData->hContext;
    Wait.ObjectCount = pData->ObjectCount;
    RtlCopyMemory(Wait.ObjectHandleArray, pData->ObjectHandleArray, sizeof(Wait.ObjectHandleArray));
    return NtGdiDdDDIWaitForSynchronizationObject(&Wait);
}
