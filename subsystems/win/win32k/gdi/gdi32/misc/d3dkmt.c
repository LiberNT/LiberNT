/*
 * PROJECT:     LiberNT GDI32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3DKMT entry points that read their descriptor in user mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <precomp.h>

NTSTATUS
APIENTRY
D3DKMTOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pData)
{
    const volatile UCHAR *Descriptor = (const volatile UCHAR *)pData;

    (VOID)Descriptor[0];
    (VOID)Descriptor[sizeof(*pData) - 1];
    return NtGdiDdDDIOpenResource(pData);
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
