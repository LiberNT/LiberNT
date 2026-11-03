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

static void
CtxValidateRenderFlags(D3DKMT_HANDLE hDevice, LUID Luid)
{
    PFND3DKMT_CREATECONTEXT pfnCreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    PFND3DKMT_RENDER pfnRender = (PFND3DKMT_RENDER)LoadD3DKMTProc("D3DKMTRender");
    D3DKMT_CREATECONTEXT Create;
    D3DKMT_DESTROYCONTEXT Destroy;
    D3DKMT_RENDER Render;
    NTSTATUS Status;

    if (pfnCreateContext == NULL || pfnRender == NULL)
        return;
    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    if (!NT_SUCCESS(pfnCreateContext(&Create)))
        return;
    memset(&Render, 0, sizeof(Render));
    Render.hContext = Create.hContext;
    Render.Flags.RenderKm = 1;
    Status = pfnRender(&Render);
    ok(Status == STATUS_INVALID_PARAMETER, "Adapter %08lx:%08lx: render with RenderKm returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
    memset(&Render, 0, sizeof(Render));
    Render.hContext = Create.hContext;
    Render.Flags.RenderKmReadback = 1;
    Status = pfnRender(&Render);
    ok(Status == STATUS_INVALID_PARAMETER, "Adapter %08lx:%08lx: render with RenderKmReadback returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hContext = Create.hContext;
    pfnDestroyContext(&Destroy);
}

static void
CtxValidateQueuedLimit(D3DKMT_HANDLE hDevice, LUID Luid)
{
    PFND3DKMT_SETQUEUEDLIMIT pfnSetQueuedLimit = (PFND3DKMT_SETQUEUEDLIMIT)LoadD3DKMTProc("D3DKMTSetQueuedLimit");
    static const UINT Values[] = { 1, 16, 17, 0 };
    static const UINT Expected[] = { 1, 16, 17, 3 };
    D3DKMT_SETQUEUEDLIMIT Limit;
    NTSTATUS Status;
    ULONG Index;

    if (pfnSetQueuedLimit == NULL)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(Values); ++Index)
    {
        memset(&Limit, 0, sizeof(Limit));
        Limit.hDevice = hDevice;
        Limit.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
        Limit.QueuedPresentLimit = Values[Index];
        Status = pfnSetQueuedLimit(&Limit);
        ok(Status == STATUS_SUCCESS, "Adapter %08lx:%08lx: queued present limit %u returned 0x%08lx\n",
           (ULONG)Luid.HighPart, Luid.LowPart, Values[Index], (ULONG)Status);
        memset(&Limit, 0, sizeof(Limit));
        Limit.hDevice = hDevice;
        Limit.Type = D3DKMT_GET_QUEUEDLIMIT_PRESENT;
        Status = pfnSetQueuedLimit(&Limit);
        ok(Status == STATUS_SUCCESS && Limit.QueuedPresentLimit == Expected[Index],
           "Adapter %08lx:%08lx: queued present limit after %u: 0x%08lx %u\n",
           (ULONG)Luid.HighPart, Luid.LowPart, Values[Index], (ULONG)Status, Limit.QueuedPresentLimit);
    }
    memset(&Limit, 0, sizeof(Limit));
    Limit.hDevice = hDevice;
    Limit.Type = 3;
    Status = pfnSetQueuedLimit(&Limit);
    ok(Status == STATUS_INVALID_PARAMETER, "Adapter %08lx:%08lx: queued limit type 3 returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
}

static void
CtxValidateStablePower(D3DKMT_HANDLE hAdapter, LUID Luid)
{
    PFND3DKMT_SETSTABLEPOWERSTATE pfnSet = (PFND3DKMT_SETSTABLEPOWERSTATE)LoadD3DKMTProc("D3DKMTSetStablePowerState");
    D3DKMT_SETSTABLEPOWERSTATE Stable;
    NTSTATUS Status;

    if (pfnSet == NULL)
        return;
    memset(&Stable, 0, sizeof(Stable));
    Stable.hAdapter = hAdapter;
    Stable.Enabled = TRUE;
    Status = pfnSet(&Stable);
    ok(Status == STATUS_SUCCESS, "Adapter %08lx:%08lx: stable power on returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
    Stable.Enabled = FALSE;
    Status = pfnSet(&Stable);
    ok(Status == STATUS_SUCCESS, "Adapter %08lx:%08lx: stable power off returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
    Status = pfnSet(&Stable);
    ok(Status == STATUS_SUCCESS, "Adapter %08lx:%08lx: stable power off without a request returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
}

static void
CtxValidateEnumFilter(void)
{
    PFND3DKMT_ENUMADAPTERS3 pfnEnumAdapters3 = (PFND3DKMT_ENUMADAPTERS3)LoadD3DKMTProc("D3DKMTEnumAdapters3");
    D3DKMT_ENUMADAPTERS3 Enum;
    NTSTATUS Status;

    if (pfnEnumAdapters3 == NULL)
        return;
    memset(&Enum, 0, sizeof(Enum));
    Enum.Filter.IncludeVirtualGpuOnly = 1;
    Status = pfnEnumAdapters3(&Enum);
    ok(NT_SUCCESS(Status), "EnumAdapters3 including virtual-GPU-only adapters returned 0x%08lx\n", (ULONG)Status);
    memset(&Enum, 0, sizeof(Enum));
    Enum.Filter.Value = 0x8;
    Status = pfnEnumAdapters3(&Enum);
    ok(Status == STATUS_INVALID_PARAMETER, "EnumAdapters3 with a reserved filter bit returned 0x%08lx\n", (ULONG)Status);
}

START_TEST(ctxvalidate)
{
    PFND3DKMT_ENUMADAPTERS2 pfnEnumAdapters2 = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    PFND3DKMT_CLOSEADAPTER pfnCloseAdapter = (PFND3DKMT_CLOSEADAPTER)LoadD3DKMTProc("D3DKMTCloseAdapter");
    PFND3DKMT_CREATEDEVICE pfnCreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    PFND3DKMT_DESTROYDEVICE pfnDestroyDevice = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    PFND3DKMT_EVICT pfnEvict = (PFND3DKMT_EVICT)LoadD3DKMTProc("D3DKMTEvict");
    NTSTATUS Status;
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    D3DKMT_CLOSEADAPTER Close;
    UCHAR PrivateData[16] = { 0 };
    ULONG Index;

    pfnCreateContextVirtual = (PFND3DKMT_CREATECONTEXTVIRTUAL)LoadD3DKMTProc("D3DKMTCreateContextVirtual");
    pfnDestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    CtxValidateEnumFilter();
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

            CtxValidateStablePower(Adapters[Index].hAdapter, Luid);
            memset(&Device, 0, sizeof(Device));
            Device.hAdapter = Adapters[Index].hAdapter;
            Device.Flags.TestDevice = 1;
            Status = pfnCreateDevice(&Device);
            ok(Status == STATUS_INVALID_PARAMETER, "Adapter %08lx:%08lx: test device returned 0x%08lx\n",
               (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
            if (NT_SUCCESS(Status))
            {
                memset(&DestroyDevice, 0, sizeof(DestroyDevice));
                DestroyDevice.hDevice = Device.hDevice;
                pfnDestroyDevice(&DestroyDevice);
            }
            memset(&Device, 0, sizeof(Device));
            Device.hAdapter = Adapters[Index].hAdapter;
            if (NT_SUCCESS(pfnCreateDevice(&Device)))
            {
                if (pfnEvict != NULL)
                {
                    D3DKMT_EVICT Evict;

                    memset(&Evict, 0, sizeof(Evict));
                    Evict.hDevice = Device.hDevice;
                    Status = pfnEvict(&Evict);
                    ok(Status == STATUS_INVALID_PARAMETER, "Adapter %08lx:%08lx: evict without allocations returned 0x%08lx\n",
                       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
                }
                CtxValidateRenderFlags(Device.hDevice, Luid);
                CtxValidateQueuedLimit(Device.hDevice, Luid);
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
