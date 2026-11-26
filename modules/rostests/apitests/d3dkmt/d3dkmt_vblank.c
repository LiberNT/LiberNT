/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     V-blank waits and scan lines on every source, including sources that scan out to no target
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _VBLANK_WAIT
{
    PFND3DKMT_WAITFORVERTICALBLANKEVENT Wait;
    D3DKMT_WAITFORVERTICALBLANKEVENT Args;
    NTSTATUS Status;
} VBLANK_WAIT;

static DWORD WINAPI VBlankWaitThread(LPVOID Param)
{
    VBLANK_WAIT *Ctx = Param;

    Ctx->Status = Ctx->Wait(&Ctx->Args);
    return 0;
}

static void TestSource(PFND3DKMT_WAITFORVERTICALBLANKEVENT pfnWait, PFND3DKMT_GETSCANLINE pfnScan,
                       D3DKMT_HANDLE hAdapter, UINT Source)
{
    D3DKMT_GETSCANLINE Scan;
    VBLANK_WAIT *Ctx;
    HANDLE Thread;
    NTSTATUS Status;
    DWORD Wait;

    memset(&Scan, 0, sizeof(Scan));
    Scan.hAdapter = hAdapter;
    Scan.VidPnSourceId = Source;
    Scan.ScanLine = 0xDEAD;
    Status = pfnScan(&Scan);
    ok(Status != STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY,
       "source %u: GetScanLine refused a source of the adapter (0x%lx)\n", Source, Status);

    /* A wait that never returns must not hang the test; the thread is left behind. */
    Ctx = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Ctx));
    if (Ctx == NULL)
    {
        skip("out of memory\n");
        return;
    }
    Ctx->Wait = pfnWait;
    Ctx->Args.hAdapter = hAdapter;
    Ctx->Args.VidPnSourceId = Source;
    Thread = CreateThread(NULL, 0, VBlankWaitThread, Ctx, 0, NULL);
    ok(Thread != NULL, "CreateThread failed %lu\n", GetLastError());
    if (Thread == NULL)
        return;
    Wait = WaitForSingleObject(Thread, 2000);
    CloseHandle(Thread);
    ok(Wait == WAIT_OBJECT_0, "source %u: the v-blank wait did not return\n", Source);
    if (Wait != WAIT_OBJECT_0)
        return;
    trace("source %u: scanline 0x%lx inVB %u line %u, wait 0x%lx\n",
          Source, Status, Scan.InVerticalBlank, Scan.ScanLine, Ctx->Status);
    ok(Ctx->Status == STATUS_SUCCESS || Ctx->Status == STATUS_GRAPHICS_PRESENT_OCCLUDED,
       "source %u: v-blank wait returned 0x%lx\n", Source, Ctx->Status);
    if (Ctx->Status == STATUS_GRAPHICS_PRESENT_OCCLUDED)
    {
        ok(Status == STATUS_SUCCESS, "source %u: GetScanLine of a source with no v-blank returned 0x%lx\n", Source, Status);
        ok(Scan.InVerticalBlank == TRUE, "source %u: a source with no v-blank is not in vertical blank\n", Source);
        ok(Scan.ScanLine == 0, "source %u: a source with no v-blank reports scan line %u\n", Source, Scan.ScanLine);
    }
    HeapFree(GetProcessHeap(), 0, Ctx);
}

START_TEST(D3dkmtVBlank)
{
    PFND3DKMT_ENUMADAPTERS2 pfnEnum = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    PFND3DKMT_CLOSEADAPTER pfnClose = (PFND3DKMT_CLOSEADAPTER)LoadD3DKMTProc("D3DKMTCloseAdapter");
    PFND3DKMT_WAITFORVERTICALBLANKEVENT pfnWait = (PFND3DKMT_WAITFORVERTICALBLANKEVENT)LoadD3DKMTProc("D3DKMTWaitForVerticalBlankEvent");
    PFND3DKMT_GETSCANLINE pfnScan = (PFND3DKMT_GETSCANLINE)LoadD3DKMTProc("D3DKMTGetScanLine");
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    ULONG i, s;

    if (!pfnEnum || !pfnClose || !pfnWait || !pfnScan)
    {
        skip("D3DKMT entry points not exported by gdi32.dll\n");
        return;
    }
    memset(&Enum, 0, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(pfnEnum(&Enum)))
    {
        skip("D3DKMTEnumAdapters2 failed\n");
        return;
    }
    for (i = 0; i < Enum.NumAdapters; i++)
    {
        D3DKMT_CLOSEADAPTER Close;

        for (s = 0; s < Adapters[i].NumOfSources && s < 4; s++)
            TestSource(pfnWait, pfnScan, Adapters[i].hAdapter, s);
        Close.hAdapter = Adapters[i].hAdapter;
        pfnClose(&Close);
    }
}
