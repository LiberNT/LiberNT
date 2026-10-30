/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     hdgfx user-mode built Gen batches through D3DKMTRender
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"
#include <drivers/directx/hdgfx_umd.h>

#define GEN_ALLOCATION_SIZE     0x10000UL
#define GEN_BATCH_OFFSET        64UL
#define GEN_STATE_OFFSET        0x1000UL
#define GEN_PITCH               256UL
#define GEN_MI_BATCH_END        0x05000000UL
#define GEN_XY_COLOR_BLT        0x54300004UL
#define GEN_XY_SRC_COPY_BLT     0x54F00006UL
#define GEN_BLT_FILL_32         0x03F00000UL
#define GEN_BLT_COPY_32         0x03CC0000UL
#define GEN_PIPE_CONTROL_WRITE  0x7A004002UL
#define GEN_PIPE_CONTROL_GGTT   0x4UL
#define GEN_OVERCOMMIT_COUNT    33
#define GEN_OVERCOMMIT_SIZE     0x800000UL
#define GEN_OVERCOMMIT_LAST     0x4000000UL

static PFND3DKMT_CREATEDEVICE CreateDevice;
static PFND3DKMT_DESTROYDEVICE DestroyDevice;
static PFND3DKMT_CREATECONTEXT CreateContext;
static PFND3DKMT_DESTROYCONTEXT DestroyContext;
static PFND3DKMT_RENDER Render;
static PFND3DKMT_CREATEALLOCATION CreateAlloc;
static PFND3DKMT_DESTROYALLOCATION DestroyAlloc;
static PFND3DKMT_LOCK2 Lock;
static PFND3DKMT_UNLOCK2 Unlock;
static PFND3DKMT_LOCK WaitLock;
static PFND3DKMT_UNLOCK WaitUnlock;

typedef struct _GEN_CONTEXT
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hContext;
    D3DKMT_HANDLE hAlloc;
    UCHAR *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
} GEN_CONTEXT;

typedef struct _GEN_PATCH
{
    UINT Offset;
    UINT AllocationOffset;
    UINT DriverId;
} GEN_PATCH;

static void GenTeardown(GEN_CONTEXT *Ctx)
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
    if (Ctx->hDevice)
    {
        D3DKMT_DESTROYDEVICE dd = {0};

        dd.hDevice = Ctx->hDevice;
        DestroyDevice(&dd);
    }
    if (Ctx->hAdapter)
        CloseAdapter(Ctx->hAdapter);
    memset(Ctx, 0, sizeof(*Ctx));
}

static BOOL GenSetup(GEN_CONTEXT *Ctx)
{
    HDGFX_GEN_CONTEXT_DATA data = { HDGFX_GEN_CONTEXT_MAGIC, HDGFX_GEN_CONTEXT_VERSION };
    UINT size = GEN_ALLOCATION_SIZE;
    D3DKMT_CREATEDEVICE cd = {0};
    D3DKMT_CREATECONTEXT cc = {0};
    D3DKMT_CREATEALLOCATION ca = {0};
    D3DDDI_ALLOCATIONINFO ai = {0};
    D3DKMT_LOCK2 lock = {0};
    D3DKMT_UNLOCK2 unlock = {0};
    NTSTATUS status;

    memset(Ctx, 0, sizeof(*Ctx));
    Ctx->hAdapter = OpenAdapterFromDisplay1();
    if (!Ctx->hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return FALSE;
    }
    cd.hAdapter = Ctx->hAdapter;
    status = CreateDevice(&cd);
    ok_succeeded(status, "CreateDevice: 0x%08lx\n", status);
    if (!NT_SUCCESS(status))
        goto fail;
    Ctx->hDevice = cd.hDevice;

    cc.hDevice = Ctx->hDevice;
    cc.pPrivateDriverData = &data;
    cc.PrivateDriverDataSize = sizeof(data);
    status = CreateContext(&cc);
    if (!NT_SUCCESS(status) || cc.CommandBufferSize < HDGFX_GEN_COMMAND_BUFFER_SIZE)
    {
        skip("no hdgfx Gen context (0x%08lx, %u byte command buffer)\n", status, cc.CommandBufferSize);
        goto fail;
    }
    Ctx->hContext = cc.hContext;
    Ctx->pCommandBuffer = cc.pCommandBuffer;
    Ctx->CommandBufferSize = cc.CommandBufferSize;
    Ctx->pAllocationList = cc.pAllocationList;
    Ctx->pPatchLocationList = cc.pPatchLocationList;
    ok(cc.AllocationListSize >= HDGFX_GEN_ALLOCATION_LIST_SIZE, "allocation list %u\n", cc.AllocationListSize);
    ok(cc.PatchLocationListSize >= HDGFX_GEN_PATCH_LIST_SIZE, "patch list %u\n", cc.PatchLocationListSize);

    ai.pPrivateDriverData = &size;
    ai.PrivateDriverDataSize = sizeof(size);
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
    memset(lock.pData, 0, GEN_ALLOCATION_SIZE);
    unlock.hDevice = Ctx->hDevice;
    unlock.hAllocation = Ctx->hAlloc;
    Unlock(&unlock);
    return TRUE;

fail:
    GenTeardown(Ctx);
    return FALSE;
}

static NTSTATUS GenSubmit(GEN_CONTEXT *Ctx, const HDGFX_GEN_SUBMIT *Header, const ULONG *Batch, const ULONG *State,
                          const GEN_PATCH *Patches, UINT PatchCount)
{
    D3DKMT_RENDER render = {0};
    NTSTATUS status;
    UINT i;

    memset(Ctx->pCommandBuffer, 0, GEN_STATE_OFFSET + HDGFX_GEN_MAX_STATE_SIZE);
    memcpy(Ctx->pCommandBuffer, Header, sizeof(*Header));
    memcpy(Ctx->pCommandBuffer + GEN_BATCH_OFFSET, Batch, Header->BatchLength);
    if (State != NULL)
        memcpy(Ctx->pCommandBuffer + GEN_STATE_OFFSET, State, Header->StateLength);
    memset(&Ctx->pAllocationList[0], 0, sizeof(Ctx->pAllocationList[0]));
    Ctx->pAllocationList[0].hAllocation = Ctx->hAlloc;
    Ctx->pAllocationList[0].WriteOperation = 1;
    for (i = 0; i < PatchCount; i++)
    {
        memset(&Ctx->pPatchLocationList[i], 0, sizeof(Ctx->pPatchLocationList[i]));
        Ctx->pPatchLocationList[i].PatchOffset = Patches[i].Offset;
        Ctx->pPatchLocationList[i].AllocationOffset = Patches[i].AllocationOffset;
        Ctx->pPatchLocationList[i].DriverId = Patches[i].DriverId;
    }

    render.hContext = Ctx->hContext;
    render.CommandLength = GEN_STATE_OFFSET + HDGFX_GEN_MAX_STATE_SIZE;
    render.AllocationCount = 1;
    render.PatchLocationCount = PatchCount;
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

static void GenHeader(HDGFX_GEN_SUBMIT *Header, UINT BatchDwords, UINT StateDwords)
{
    memset(Header, 0, sizeof(*Header));
    Header->Magic = HDGFX_GEN_SUBMIT_MAGIC;
    Header->Version = HDGFX_GEN_SUBMIT_VERSION;
    Header->BatchOffset = GEN_BATCH_OFFSET;
    Header->BatchLength = BatchDwords * sizeof(ULONG);
    Header->StateOffset = StateDwords != 0 ? GEN_STATE_OFFSET : 0;
    Header->StateLength = StateDwords * sizeof(ULONG);
}

static BOOL GenRead(GEN_CONTEXT *Ctx, ULONG *Copy)
{
    D3DKMT_LOCK lock = {0};
    D3DKMT_UNLOCK unlock = {0};
    NTSTATUS status;

    lock.hDevice = Ctx->hDevice;
    lock.hAllocation = Ctx->hAlloc;
    status = WaitLock(&lock);
    ok_succeeded(status, "Lock for readback: 0x%08lx\n", status);
    if (!NT_SUCCESS(status))
        return FALSE;
    memcpy(Copy, lock.pData, GEN_ALLOCATION_SIZE);
    unlock.hDevice = Ctx->hDevice;
    unlock.NumAllocations = 1;
    unlock.phAllocations = &Ctx->hAlloc;
    WaitUnlock(&unlock);
    return TRUE;
}

static UINT GenCountMismatches(const ULONG *Words, UINT First, UINT Count, ULONG Expected)
{
    UINT Mismatches = 0;
    UINT i;

    for (i = First; i < First + Count; i++)
        Mismatches += Words[i] != Expected;
    return Mismatches;
}

static void Test_ColorFill(GEN_CONTEXT *Ctx, ULONG *Copy)
{
    HDGFX_GEN_SUBMIT header;
    ULONG batch[16];
    GEN_PATCH patches[2];
    NTSTATUS status;

    batch[0] = GEN_XY_COLOR_BLT;
    batch[1] = GEN_BLT_FILL_32 | GEN_PITCH;
    batch[2] = 0;
    batch[3] = (16 << 16) | 64;
    batch[4] = 0;
    batch[5] = 0x11223344;
    batch[6] = GEN_XY_COLOR_BLT;
    batch[7] = GEN_BLT_FILL_32 | GEN_PITCH;
    batch[8] = 0;
    batch[9] = (2 << 16) | 64;
    batch[10] = 0;
    batch[11] = 0x55667788;
    batch[12] = GEN_MI_BATCH_END;
    batch[13] = 0;
    patches[0].Offset = GEN_BATCH_OFFSET + 4 * sizeof(ULONG);
    patches[0].AllocationOffset = 0;
    patches[0].DriverId = HDGFX_GEN_PATCH_ALLOCATION;
    patches[1].Offset = GEN_BATCH_OFFSET + 10 * sizeof(ULONG);
    patches[1].AllocationOffset = 0x2000;
    patches[1].DriverId = HDGFX_GEN_PATCH_ALLOCATION;
    GenHeader(&header, 14, 0);
    status = GenSubmit(Ctx, &header, batch, NULL, patches, 2);
    ok_succeeded(status, "Render of the fills: 0x%08lx\n", status);
    if (!NT_SUCCESS(status) || !GenRead(Ctx, Copy))
        return;
    ok(GenCountMismatches(Copy, 0, 16 * GEN_PITCH / 4, 0x11223344) == 0,
       "first fill: word 0 is 0x%08lx, %u mismatches\n", Copy[0], GenCountMismatches(Copy, 0, 1024, 0x11223344));
    ok(GenCountMismatches(Copy, 1024, 1024, 0) == 0, "first fill wrote past its rectangle\n");
    ok(GenCountMismatches(Copy, 0x2000 / 4, 2 * GEN_PITCH / 4, 0x55667788) == 0,
       "fill at allocation offset 0x2000: word is 0x%08lx\n", Copy[0x2000 / 4]);
    ok(Copy[0x2000 / 4 + 2 * GEN_PITCH / 4] == 0, "second fill wrote past its rectangle\n");
}

static void Test_StateCopy(GEN_CONTEXT *Ctx, ULONG *Copy)
{
    HDGFX_GEN_SUBMIT header;
    ULONG batch[10];
    ULONG state[4 * GEN_PITCH / 4];
    GEN_PATCH patches[2];
    NTSTATUS status;
    UINT Mismatches = 0;
    UINT i;

    for (i = 0; i < ARRAY_SIZE(state); i++)
        state[i] = 0xA5000000 | i;
    batch[0] = GEN_XY_SRC_COPY_BLT;
    batch[1] = GEN_BLT_COPY_32 | GEN_PITCH;
    batch[2] = 0;
    batch[3] = (4 << 16) | 64;
    batch[4] = 0;
    batch[5] = 0;
    batch[6] = GEN_PITCH;
    batch[7] = 0;
    batch[8] = GEN_MI_BATCH_END;
    batch[9] = 0;
    patches[0].Offset = GEN_BATCH_OFFSET + 4 * sizeof(ULONG);
    patches[0].AllocationOffset = 0x4000;
    patches[0].DriverId = HDGFX_GEN_PATCH_ALLOCATION;
    patches[1].Offset = GEN_BATCH_OFFSET + 7 * sizeof(ULONG);
    patches[1].AllocationOffset = 0;
    patches[1].DriverId = HDGFX_GEN_PATCH_STATE;
    GenHeader(&header, 10, ARRAY_SIZE(state));
    status = GenSubmit(Ctx, &header, batch, state, patches, 2);
    ok_succeeded(status, "Render of the copy from the state section: 0x%08lx\n", status);
    if (!NT_SUCCESS(status) || !GenRead(Ctx, Copy))
        return;
    for (i = 0; i < ARRAY_SIZE(state); i++)
        Mismatches += Copy[0x4000 / 4 + i] != state[i];
    ok(Mismatches == 0, "copy from the state section: %u mismatches, word 0 is 0x%08lx\n", Mismatches,
       Copy[0x4000 / 4]);
}

static void Test_PipeControlWrite(GEN_CONTEXT *Ctx, ULONG *Copy)
{
    HDGFX_GEN_SUBMIT header;
    ULONG batch[6];
    GEN_PATCH patch;
    NTSTATUS status;

    batch[0] = GEN_PIPE_CONTROL_WRITE;
    batch[1] = 0;
    batch[2] = 0xC0FFEE01;
    batch[3] = 0xC0FFEE02;
    batch[4] = GEN_MI_BATCH_END;
    batch[5] = 0;
    patch.Offset = GEN_BATCH_OFFSET + sizeof(ULONG);
    patch.AllocationOffset = 0x8000 | GEN_PIPE_CONTROL_GGTT;
    patch.DriverId = HDGFX_GEN_PATCH_ALLOCATION;
    GenHeader(&header, 6, 0);
    status = GenSubmit(Ctx, &header, batch, NULL, &patch, 1);
    ok_succeeded(status, "Render of the PIPE_CONTROL write: 0x%08lx\n", status);
    if (!NT_SUCCESS(status) || !GenRead(Ctx, Copy))
        return;
    ok(Copy[0x8000 / 4] == 0xC0FFEE01 && Copy[0x8000 / 4 + 1] == 0xC0FFEE02,
       "PIPE_CONTROL immediate write: 0x%08lx 0x%08lx\n", Copy[0x8000 / 4], Copy[0x8000 / 4 + 1]);
}

static void Test_ApertureOvercommit(GEN_CONTEXT *Ctx, ULONG *Copy)
{
    D3DKMT_HANDLE original = Ctx->hAlloc;
    D3DKMT_HANDLE handles[GEN_OVERCOMMIT_COUNT] = {0};
    volatile ULONG *mapped[GEN_OVERCOMMIT_COUNT] = {0};
    HDGFX_GEN_SUBMIT header;
    GEN_PATCH patch;
    ULONG batch[8];
    NTSTATUS status;
    UINT count;
    UINT i;

    patch.Offset = GEN_BATCH_OFFSET + 4 * sizeof(ULONG);
    patch.AllocationOffset = 0;
    patch.DriverId = HDGFX_GEN_PATCH_ALLOCATION;
    for (count = 0; count < GEN_OVERCOMMIT_COUNT; count++)
    {
        UINT size = count == GEN_OVERCOMMIT_COUNT - 1 ? GEN_OVERCOMMIT_LAST : GEN_OVERCOMMIT_SIZE;
        D3DKMT_CREATEALLOCATION ca = {0};
        D3DDDI_ALLOCATIONINFO ai = {0};
        D3DKMT_LOCK2 lock = {0};

        ai.pPrivateDriverData = &size;
        ai.PrivateDriverDataSize = sizeof(size);
        ca.hDevice = Ctx->hDevice;
        ca.NumAllocations = 1;
        ca.pAllocationInfo = &ai;
        status = CreateAlloc(&ca);
        ok_succeeded(status, "CreateAllocation %u: 0x%08lx\n", count, status);
        if (!NT_SUCCESS(status))
            break;
        handles[count] = ai.hAllocation;

        lock.hDevice = Ctx->hDevice;
        lock.hAllocation = ai.hAllocation;
        status = Lock(&lock);
        ok_succeeded(status, "Lock %u: 0x%08lx\n", count, status);
        if (!NT_SUCCESS(status))
            break;
        mapped[count] = lock.pData;

        batch[0] = GEN_XY_COLOR_BLT;
        batch[1] = GEN_BLT_FILL_32 | GEN_PITCH;
        batch[2] = 0;
        batch[3] = (16 << 16) | 64;
        batch[4] = 0;
        batch[5] = 0xA5000000 | count;
        batch[6] = GEN_MI_BATCH_END;
        batch[7] = 0;
        GenHeader(&header, 8, 0);
        Ctx->hAlloc = ai.hAllocation;
        status = GenSubmit(Ctx, &header, batch, NULL, &patch, 1);
        ok_succeeded(status, "Render into allocation %u: 0x%08lx\n", count, status);
        if (!NT_SUCCESS(status))
            break;
    }

    for (i = 0; i < count; i++)
    {
        Ctx->hAlloc = handles[i];
        if (!GenRead(Ctx, Copy))
            break;
        ok(Copy[0] == (0xA5000000 | i), "allocation %u reads 0x%08lx\n", i, Copy[0]);
        ok(mapped[i][0] == (0xA5000000 | i), "allocation %u mapping reads 0x%08lx\n", i, mapped[i][0]);
    }

    Ctx->hAlloc = original;
    for (i = 0; i < GEN_OVERCOMMIT_COUNT; i++)
    {
        D3DKMT_UNLOCK2 unlock = {0};
        D3DKMT_DESTROYALLOCATION da = {0};

        if (handles[i] == 0)
            continue;
        if (mapped[i] != NULL)
        {
            unlock.hDevice = Ctx->hDevice;
            unlock.hAllocation = handles[i];
            Unlock(&unlock);
        }
        da.hDevice = Ctx->hDevice;
        da.phAllocationList = &handles[i];
        da.AllocationCount = 1;
        DestroyAlloc(&da);
    }
}

static void Test_MalformedSubmits(GEN_CONTEXT *Ctx)
{
    static const ULONG batch[2] = { GEN_MI_BATCH_END, 0 };
    HDGFX_GEN_SUBMIT header;
    GEN_PATCH patch;
    NTSTATUS status;

    GenHeader(&header, 2, 0);
    header.Magic = 0;
    status = GenSubmit(Ctx, &header, batch, NULL, NULL, 0);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    GenHeader(&header, 2, 0);
    header.BatchLength = 4;
    status = GenSubmit(Ctx, &header, batch, NULL, NULL, 0);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    GenHeader(&header, 2, 0);
    header.BatchOffset = 4;
    status = GenSubmit(Ctx, &header, batch, NULL, NULL, 0);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    GenHeader(&header, 2, 0);
    header.StateOffset = GEN_BATCH_OFFSET;
    header.StateLength = 4;
    status = GenSubmit(Ctx, &header, batch, NULL, NULL, 0);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    GenHeader(&header, 2, 0);
    header.BatchLength = HDGFX_GEN_MAX_BATCH_SIZE + 8;
    status = GenSubmit(Ctx, &header, batch, NULL, NULL, 0);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    GenHeader(&header, 2, 0);
    patch.Offset = GEN_BATCH_OFFSET + 2 * sizeof(ULONG);
    patch.AllocationOffset = 0;
    patch.DriverId = HDGFX_GEN_PATCH_ALLOCATION;
    status = GenSubmit(Ctx, &header, batch, NULL, &patch, 1);
    ok_hex(status, STATUS_INVALID_USER_BUFFER);

    patch.Offset = GEN_BATCH_OFFSET + 2;
    status = GenSubmit(Ctx, &header, batch, NULL, &patch, 1);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    patch.Offset = GEN_BATCH_OFFSET;
    patch.DriverId = HDGFX_GEN_PATCH_STATE;
    status = GenSubmit(Ctx, &header, batch, NULL, &patch, 1);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    patch.DriverId = 7;
    status = GenSubmit(Ctx, &header, batch, NULL, &patch, 1);
    ok_hex(status, STATUS_INVALID_PARAMETER);

    status = GenSubmit(Ctx, &header, batch, NULL, NULL, 0);
    ok_succeeded(status, "Render of a well-formed empty batch: 0x%08lx\n", status);
}

START_TEST(hdgfx_gen)
{
    GEN_CONTEXT Ctx;
    ULONG *Copy;

    CreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    DestroyDevice = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    CreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    DestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    Render = (PFND3DKMT_RENDER)LoadD3DKMTProc("D3DKMTRender");
    CreateAlloc = (PFND3DKMT_CREATEALLOCATION)LoadD3DKMTProc("D3DKMTCreateAllocation");
    DestroyAlloc = (PFND3DKMT_DESTROYALLOCATION)LoadD3DKMTProc("D3DKMTDestroyAllocation");
    Lock = (PFND3DKMT_LOCK2)LoadD3DKMTProc("D3DKMTLock2");
    Unlock = (PFND3DKMT_UNLOCK2)LoadD3DKMTProc("D3DKMTUnlock2");
    WaitLock = (PFND3DKMT_LOCK)LoadD3DKMTProc("D3DKMTLock");
    WaitUnlock = (PFND3DKMT_UNLOCK)LoadD3DKMTProc("D3DKMTUnlock");
    if (!CreateDevice || !DestroyDevice || !CreateContext || !DestroyContext || !Render || !CreateAlloc ||
        !DestroyAlloc || !Lock || !Unlock || !WaitLock || !WaitUnlock)
    {
        skip("render-path entry points not exported\n");
        return;
    }
    Copy = HeapAlloc(GetProcessHeap(), 0, GEN_ALLOCATION_SIZE);
    if (Copy == NULL)
    {
        skip("out of memory\n");
        return;
    }
    if (GenSetup(&Ctx))
    {
        Test_ColorFill(&Ctx, Copy);
        Test_StateCopy(&Ctx, Copy);
        Test_PipeControlWrite(&Ctx, Copy);
        Test_ApertureOvercommit(&Ctx, Copy);
        Test_MalformedSubmits(&Ctx);
        GenTeardown(&Ctx);
    }
    HeapFree(GetProcessHeap(), 0, Copy);
}
