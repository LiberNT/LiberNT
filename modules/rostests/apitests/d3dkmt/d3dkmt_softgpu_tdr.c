/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software GPU timeout detection and recovery on the render path
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <hi@ostapbrehin.com>
 */

#include "precomp.h"
#include <drivers/directx/softgpu_2d_shared.h>

#define TDR_WAIT_MS 15000

static PFND3DKMT_QUERYADAPTERINFO QueryAdapter;
static PFND3DKMT_CREATECONTEXT CreateContext;
static PFND3DKMT_DESTROYCONTEXT DestroyContext;
static PFND3DKMT_RENDER Render;
static PFND3DKMT_CREATEALLOCATION CreateAlloc;
static PFND3DKMT_DESTROYALLOCATION DestroyAlloc;
static PFND3DKMT_LOCK2 Lock;
static PFND3DKMT_UNLOCK2 Unlock;
static PFND3DKMT_LOCK WaitLock;
static PFND3DKMT_UNLOCK WaitUnlock;
static PFND3DKMT_GETDEVICESTATE GetState;

typedef struct _TDR_CONTEXT
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hContext;
    D3DKMT_HANDLE hAlloc;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
} TDR_CONTEXT;

static BOOL IsSoftGpu(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_UMDFILENAMEINFO info = {0};
    D3DKMT_QUERYADAPTERINFO query = {0};
    const WCHAR *base, *cursor;

    info.Version = KMTUMDVERSION_DX9;
    query.hAdapter = hAdapter;
    query.Type = KMTQAITYPE_UMDRIVERNAME;
    query.pPrivateDriverData = &info;
    query.PrivateDriverDataSize = sizeof(info);
    if (!NT_SUCCESS(QueryAdapter(&query)))
        return FALSE;
    info.UmdFileName[ARRAY_SIZE(info.UmdFileName) - 1] = 0;
    base = info.UmdFileName;
    for (cursor = base; *cursor; ++cursor)
        if (*cursor == L'\\' || *cursor == L'/')
            base = cursor + 1;
    return lstrcmpiW(base, L"softgpuum.dll") == 0;
}

static void TdrTeardown(TDR_CONTEXT *Ctx)
{
    if (Ctx->hAlloc)
    {
        D3DKMT_DESTROYALLOCATION da = {0};

        da.hDevice = Ctx->hDevice;
        da.AllocationCount = 1;
        da.phAllocationList = &Ctx->hAlloc;
        DestroyAlloc(&da);
    }
    if (Ctx->hContext)
    {
        D3DKMT_DESTROYCONTEXT dc = {0};

        dc.hContext = Ctx->hContext;
        DestroyContext(&dc);
    }
    DestroyTestDevice(Ctx->hDevice);
    memset(Ctx, 0, sizeof(*Ctx));
}

static BOOL TdrSetup(D3DKMT_HANDLE hAdapter, TDR_CONTEXT *Ctx)
{
    D3DKMT_CREATECONTEXT cc = {0};
    D3DKMT_CREATEALLOCATION ca = {0};
    D3DDDI_ALLOCATIONINFO ai = {0};
    D3DKMT_LOCK2 lock = {0};
    D3DKMT_UNLOCK2 unlock = {0};
    NTSTATUS status;

    memset(Ctx, 0, sizeof(*Ctx));
    Ctx->hDevice = CreateTestDevice(hAdapter);
    ok(Ctx->hDevice != 0, "CreateDevice failed\n");
    if (!Ctx->hDevice)
        return FALSE;

    cc.hDevice = Ctx->hDevice;
    status = CreateContext(&cc);
    ok_succeeded(status, "CreateContext: 0x%08lx\n", status);
    if (!NT_SUCCESS(status))
        goto fail;
    Ctx->hContext = cc.hContext;
    Ctx->pCommandBuffer = cc.pCommandBuffer;
    Ctx->CommandBufferSize = cc.CommandBufferSize;
    Ctx->pAllocationList = cc.pAllocationList;
    Ctx->pPatchLocationList = cc.pPatchLocationList;
    ok(Ctx->pCommandBuffer != NULL && Ctx->CommandBufferSize >= 2 * sizeof(SOFTGPU_CMD) &&
       Ctx->pAllocationList != NULL && Ctx->pPatchLocationList != NULL,
       "context has no usable command, allocation or patch list\n");
    if (Ctx->pCommandBuffer == NULL || Ctx->CommandBufferSize < 2 * sizeof(SOFTGPU_CMD) ||
        Ctx->pAllocationList == NULL || Ctx->pPatchLocationList == NULL)
        goto fail;

    ca.hDevice = Ctx->hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;
    status = CreateAlloc(&ca);
    ok_succeeded(status, "CreateAllocation: 0x%08lx\n", status);
    if (!NT_SUCCESS(status))
        goto fail;
    Ctx->hAlloc = ai.hAllocation;

    lock.hDevice = Ctx->hDevice;
    lock.hAllocation = Ctx->hAlloc;
    status = Lock(&lock);
    ok_succeeded(status, "Lock: 0x%08lx\n", status);
    if (!NT_SUCCESS(status))
        goto fail;
    *(volatile UINT64 *)lock.pData = 0;
    unlock.hDevice = Ctx->hDevice;
    unlock.hAllocation = Ctx->hAlloc;
    Unlock(&unlock);
    return TRUE;

fail:
    TdrTeardown(Ctx);
    return FALSE;
}

static NTSTATUS TdrRenderFences(TDR_CONTEXT *Ctx, const ULONG *Ops, const UINT64 *Values, UINT Count)
{
    D3DKMT_RENDER render = {0};
    SOFTGPU_CMD *Cmd = Ctx->pCommandBuffer;
    NTSTATUS status;
    UINT i;

    memset(Cmd, 0, Count * sizeof(*Cmd));
    memset(&Ctx->pAllocationList[0], 0, sizeof(Ctx->pAllocationList[0]));
    Ctx->pAllocationList[0].hAllocation = Ctx->hAlloc;
    Ctx->pAllocationList[0].WriteOperation = 1;
    for (i = 0; i < Count; i++)
    {
        Cmd[i].Magic = SOFTGPU_CMD_MAGIC;
        Cmd[i].Size = sizeof(Cmd[i]);
        Cmd[i].Op = Ops[i];
        Cmd[i].FenceValue = Values[i];
        memset(&Ctx->pPatchLocationList[i], 0, sizeof(Ctx->pPatchLocationList[i]));
        Ctx->pPatchLocationList[i].AllocationIndex = 0;
        Ctx->pPatchLocationList[i].PatchOffset =
            i * sizeof(SOFTGPU_CMD) + FIELD_OFFSET(SOFTGPU_CMD, DstAddress);
    }

    render.hContext = Ctx->hContext;
    render.CommandLength = Count * sizeof(SOFTGPU_CMD);
    render.AllocationCount = 1;
    render.PatchLocationCount = Count;
    status = Render(&render);
    if (NT_SUCCESS(status))
    {
        Ctx->pCommandBuffer = render.pNewCommandBuffer;
        Ctx->CommandBufferSize = render.NewCommandBufferSize;
        Ctx->pAllocationList = render.pNewAllocationList;
        Ctx->pPatchLocationList = render.pNewPatchLocationList;
    }
    return status;
}

static UINT64 TdrReadFence(TDR_CONTEXT *Ctx)
{
    D3DKMT_LOCK lock = {0};
    D3DKMT_UNLOCK unlock = {0};
    UINT64 value;

    lock.hDevice = Ctx->hDevice;
    lock.hAllocation = Ctx->hAlloc;
    if (!NT_SUCCESS(WaitLock(&lock)))
        return ~(UINT64)0;
    value = *(volatile UINT64 *)lock.pData;
    unlock.hDevice = Ctx->hDevice;
    unlock.NumAllocations = 1;
    unlock.phAllocations = &Ctx->hAlloc;
    WaitUnlock(&unlock);
    return value;
}

static BOOL TdrSignalAndWait(TDR_CONTEXT *Ctx, UINT64 Value)
{
    static const ULONG Ops[2] = { SOFTGPU_CMD_OP_SIGNAL_FENCE, SOFTGPU_CMD_OP_WAIT_FENCE };
    UINT64 Values[2];
    NTSTATUS status;
    UINT64 seen;

    Values[0] = Values[1] = Value;
    status = TdrRenderFences(Ctx, Ops, Values, 2);
    ok_succeeded(status, "Render signal+wait %I64u: 0x%08lx\n", Value, status);
    if (!NT_SUCCESS(status))
        return FALSE;
    seen = TdrReadFence(Ctx);
    ok(seen == Value, "fence word %I64u, expected %I64u\n", seen, Value);
    return seen == Value;
}

static D3DKMT_DEVICEEXECUTION_STATE TdrExecutionState(D3DKMT_HANDLE hDevice)
{
    D3DKMT_GETDEVICESTATE state = {0};

    state.hDevice = hDevice;
    state.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    if (!NT_SUCCESS(GetState(&state)))
        return D3DKMT_DEVICEEXECUTION_ERROR_DMAFAULT;
    return state.ExecutionState;
}

START_TEST(softgpu_tdr)
{
    static const ULONG HangOp = SOFTGPU_CMD_OP_WAIT_FENCE;
    static const UINT64 HangValue = 2;
    D3DKMT_HANDLE hAdapter;
    TDR_CONTEXT Hung = {0}, Fresh = {0};
    D3DKMT_DEVICEEXECUTION_STATE state = D3DKMT_DEVICEEXECUTION_ACTIVE;
    DWORD start, elapsed;
    NTSTATUS status;

    QueryAdapter = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    CreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    DestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    Render = (PFND3DKMT_RENDER)LoadD3DKMTProc("D3DKMTRender");
    CreateAlloc = (PFND3DKMT_CREATEALLOCATION)LoadD3DKMTProc("D3DKMTCreateAllocation");
    DestroyAlloc = (PFND3DKMT_DESTROYALLOCATION)LoadD3DKMTProc("D3DKMTDestroyAllocation");
    Lock = (PFND3DKMT_LOCK2)LoadD3DKMTProc("D3DKMTLock2");
    Unlock = (PFND3DKMT_UNLOCK2)LoadD3DKMTProc("D3DKMTUnlock2");
    WaitLock = (PFND3DKMT_LOCK)LoadD3DKMTProc("D3DKMTLock");
    WaitUnlock = (PFND3DKMT_UNLOCK)LoadD3DKMTProc("D3DKMTUnlock");
    GetState = (PFND3DKMT_GETDEVICESTATE)LoadD3DKMTProc("D3DKMTGetDeviceState");
    if (!QueryAdapter || !CreateContext || !DestroyContext || !Render || !CreateAlloc ||
        !DestroyAlloc || !Lock || !Unlock || !WaitLock || !WaitUnlock || !GetState)
    {
        skip("render-path entry points not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    if (!IsSoftGpu(hAdapter))
    {
        skip("This test requires the LiberNT software GPU\n");
        goto cleanup;
    }

    if (!TdrSetup(hAdapter, &Hung))
        goto cleanup;
    if (!TdrSignalAndWait(&Hung, 1))
        goto cleanup;

    status = TdrRenderFences(&Hung, &HangOp, &HangValue, 1);
    ok_succeeded(status, "Render of the unsatisfiable wait: 0x%08lx\n", status);
    if (!NT_SUCCESS(status))
        goto cleanup;

    start = GetTickCount();
    do
    {
        Sleep(100);
        state = TdrExecutionState(Hung.hDevice);
        elapsed = GetTickCount() - start;
    } while ((state == D3DKMT_DEVICEEXECUTION_ACTIVE || state == D3DKMT_DEVICEEXECUTION_RESET) &&
             elapsed < TDR_WAIT_MS);
    trace("execution state %d after %lu ms\n", state, elapsed);
    ok(state == D3DKMT_DEVICEEXECUTION_HUNG,
       "device with a never-completing packet is in state %d after %lu ms\n", state, elapsed);
    if (state != D3DKMT_DEVICEEXECUTION_HUNG)
        goto cleanup;

    if (!TdrSetup(hAdapter, &Fresh))
        goto cleanup;
    ok(TdrExecutionState(Fresh.hDevice) == D3DKMT_DEVICEEXECUTION_ACTIVE,
       "a device created after recovery is not active\n");
    TdrSignalAndWait(&Fresh, 3);
    TdrSignalAndWait(&Fresh, 4);

cleanup:
    TdrTeardown(&Fresh);
    TdrTeardown(&Hung);
    CloseAdapter(hAdapter);
}
