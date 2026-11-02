/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMTCreateContextVirtual argument validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

static PFND3DKMT_CREATECONTEXTVIRTUAL pfnCreateContextVirtual;
static PFND3DKMT_DESTROYCONTEXT pfnDestroyContext;

static void
CtxValidateRefused(D3DKMT_HANDLE hDevice, LUID Luid, const char *What, UINT NodeOrdinal, UINT EngineAffinity,
                   PVOID PrivateData, UINT PrivateDataSize)
{
    D3DKMT_CREATECONTEXTVIRTUAL Create;
    D3DKMT_DESTROYCONTEXT Destroy;
    NTSTATUS Status;

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.NodeOrdinal = NodeOrdinal;
    Create.EngineAffinity = EngineAffinity;
    Create.pPrivateDriverData = PrivateData;
    Create.PrivateDriverDataSize = PrivateDataSize;
    Create.ClientHint = D3DKMT_CLIENTHINT_DX11;
    Status = pfnCreateContextVirtual(&Create);
    ok(Status == STATUS_INVALID_PARAMETER, "Adapter %08lx:%08lx: %s returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, What, (ULONG)Status);
    if (NT_SUCCESS(Status) && Create.hContext != 0)
    {
        memset(&Destroy, 0, sizeof(Destroy));
        Destroy.hContext = Create.hContext;
        pfnDestroyContext(&Destroy);
    }
}

START_TEST(ctxvalidate)
{
    PFND3DKMT_ENUMADAPTERS2 pfnEnumAdapters2 = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    PFND3DKMT_CLOSEADAPTER pfnCloseAdapter = (PFND3DKMT_CLOSEADAPTER)LoadD3DKMTProc("D3DKMTCloseAdapter");
    PFND3DKMT_CREATEDEVICE pfnCreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    PFND3DKMT_DESTROYDEVICE pfnDestroyDevice = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    D3DKMT_CLOSEADAPTER Close;
    UCHAR PrivateData[16] = { 0 };
    ULONG Index;

    pfnCreateContextVirtual = (PFND3DKMT_CREATECONTEXTVIRTUAL)LoadD3DKMTProc("D3DKMTCreateContextVirtual");
    pfnDestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    if (pfnEnumAdapters2 == NULL || pfnCloseAdapter == NULL || pfnCreateDevice == NULL || pfnDestroyDevice == NULL ||
        pfnCreateContextVirtual == NULL || pfnDestroyContext == NULL)
    {
        skip("D3DKMT adapter, device or virtual context entry points are not exported\n");
        return;
    }
    memset(&Enum, 0, sizeof(Enum));
    if (!NT_SUCCESS(pfnEnumAdapters2(&Enum)) || Enum.NumAdapters == 0)
    {
        skip("no adapters\n");
        return;
    }
    Adapters = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Enum.NumAdapters * sizeof(*Adapters));
    if (Adapters == NULL)
        return;
    Enum.pAdapters = Adapters;
    if (NT_SUCCESS(pfnEnumAdapters2(&Enum)))
    {
        for (Index = 0; Index < Enum.NumAdapters; ++Index)
        {
            D3DKMT_CREATEDEVICE Device;
            D3DKMT_DESTROYDEVICE DestroyDevice;
            LUID Luid = Adapters[Index].AdapterLuid;

            memset(&Device, 0, sizeof(Device));
            Device.hAdapter = Adapters[Index].hAdapter;
            if (NT_SUCCESS(pfnCreateDevice(&Device)))
            {
                CtxValidateRefused(Device.hDevice, Luid, "node ordinal past the last node", 0x1000, 0, NULL, 0);
                CtxValidateRefused(Device.hDevice, Luid, "engine affinity naming a second physical adapter", 0, 2, NULL, 0);
                CtxValidateRefused(Device.hDevice, Luid, "private data without a size", 0, 0, PrivateData, 0);
                CtxValidateRefused(Device.hDevice, Luid, "private data size without data", 0, 0, NULL, sizeof(PrivateData));
                memset(&DestroyDevice, 0, sizeof(DestroyDevice));
                DestroyDevice.hDevice = Device.hDevice;
                pfnDestroyDevice(&DestroyDevice);
            }
            memset(&Close, 0, sizeof(Close));
            Close.hAdapter = Adapters[Index].hAdapter;
            pfnCloseAdapter(&Close);
        }
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
}
