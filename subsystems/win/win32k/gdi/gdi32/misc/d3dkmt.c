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
