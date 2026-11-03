/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMTReserveGpuVirtualAddress size granularity
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

static PFND3DKMT_RESERVEGPUVIRTUALADDRESS pfnReserve;
static PFND3DKMT_FREEGPUVIRTUALADDRESS pfnFree;
static PFND3DKMT_MAPGPUVIRTUALADDRESS pfnMap;

#define GPUVA_PROTECTION_WRITE 0x01
#define GPUVA_PROTECTION_EXECUTE 0x02
#define GPUVA_PROTECTION_ZERO 0x04
#define GPUVA_PROTECTION_NO_ACCESS 0x08
#define GPUVA_PROTECTION_SYSTEM_USE_ONLY 0x10

static NTSTATUS
GpuVaReserve(D3DKMT_HANDLE hPagingQueue, D3DGPU_SIZE_T Size, D3DGPU_VIRTUAL_ADDRESS *Address)
{
    D3DDDI_RESERVEGPUVIRTUALADDRESS Reserve;
    NTSTATUS Status;

    memset(&Reserve, 0, sizeof(Reserve));
    Reserve.hPagingQueue = hPagingQueue;
    Reserve.Size = Size;
    Status = pfnReserve(&Reserve);
    *Address = NT_SUCCESS(Status) ? Reserve.VirtualAddress : 0;
    return Status;
}

static void
GpuVaFree(D3DKMT_HANDLE hAdapter, D3DGPU_VIRTUAL_ADDRESS Address, D3DGPU_SIZE_T Size, LUID Luid)
{
    D3DKMT_FREEGPUVIRTUALADDRESS Free;
    NTSTATUS Status;

    memset(&Free, 0, sizeof(Free));
    Free.hAdapter = hAdapter;
    Free.BaseAddress = Address;
    Free.Size = Size;
    Status = pfnFree(&Free);
    ok(NT_SUCCESS(Status), "Adapter %08lx:%08lx: free of 0x%I64x bytes at 0x%I64x returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG64)Size, (ULONG64)Address, (ULONG)Status);
}

static NTSTATUS
GpuVaMapState(D3DKMT_HANDLE hPagingQueue, D3DGPU_VIRTUAL_ADDRESS Address, UINT64 Protection)
{
    D3DDDI_MAPGPUVIRTUALADDRESS Map;

    memset(&Map, 0, sizeof(Map));
    Map.hPagingQueue = hPagingQueue;
    Map.BaseAddress = Address;
    Map.SizeInPages = 1;
    Map.Protection.Value = Protection;
    return pfnMap(&Map);
}

static void
GpuVaMapStates(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hPagingQueue, LUID Luid)
{
    static const struct
    {
        UINT64 Protection;
        BOOLEAN Accepted;
    } Cases[] =
    {
        { GPUVA_PROTECTION_ZERO, TRUE },
        { GPUVA_PROTECTION_ZERO | GPUVA_PROTECTION_WRITE, TRUE },
        { GPUVA_PROTECTION_ZERO | GPUVA_PROTECTION_WRITE | GPUVA_PROTECTION_EXECUTE, TRUE },
        { GPUVA_PROTECTION_NO_ACCESS, TRUE },
        { GPUVA_PROTECTION_NO_ACCESS | GPUVA_PROTECTION_WRITE, FALSE },
        { GPUVA_PROTECTION_NO_ACCESS | GPUVA_PROTECTION_EXECUTE, FALSE },
        { GPUVA_PROTECTION_NO_ACCESS | GPUVA_PROTECTION_ZERO, FALSE },
        { GPUVA_PROTECTION_ZERO | GPUVA_PROTECTION_SYSTEM_USE_ONLY, FALSE },
        { GPUVA_PROTECTION_ZERO | 0x20, FALSE },
    };
    D3DGPU_VIRTUAL_ADDRESS Address;
    NTSTATUS Status;
    ULONG Index;

    Status = GpuVaReserve(hPagingQueue, 0x10000, &Address);
    ok(NT_SUCCESS(Status), "Adapter %08lx:%08lx: reservation returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
    if (!NT_SUCCESS(Status))
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(Cases); ++Index)
    {
        Status = GpuVaMapState(hPagingQueue, Address, Cases[Index].Protection);
        ok(Cases[Index].Accepted ? NT_SUCCESS(Status) : Status == STATUS_INVALID_PARAMETER,
           "Adapter %08lx:%08lx: map with protection 0x%I64x returned 0x%08lx\n",
           (ULONG)Luid.HighPart, Luid.LowPart, Cases[Index].Protection, (ULONG)Status);
    }
    GpuVaFree(hAdapter, Address, 0x10000, Luid);
}

static void
GpuVaReserveQueue(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hPagingQueue, LUID Luid)
{
    D3DGPU_VIRTUAL_ADDRESS Address;
    NTSTATUS Status;

    Status = GpuVaReserve(hPagingQueue, 0x10000, &Address);
    if (!NT_SUCCESS(Status))
    {
        trace("Adapter %08lx:%08lx: no GPU virtual address space (0x%08lx)\n", (ULONG)Luid.HighPart, Luid.LowPart,
              (ULONG)Status);
        return;
    }
    GpuVaFree(hAdapter, Address, 0x10000, Luid);

    Status = GpuVaReserve(hPagingQueue, 0x1000, &Address);
    ok(NT_SUCCESS(Status) && Address != 0 && (Address & 0xFFFF) == 0,
       "Adapter %08lx:%08lx: one-page reservation returned 0x%08lx at 0x%I64x\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status, (ULONG64)Address);
    if (NT_SUCCESS(Status))
        GpuVaFree(hAdapter, Address, 0x1000, Luid);

    Status = GpuVaReserve(hPagingQueue, 0x1800, &Address);
    ok(Status == STATUS_INVALID_PARAMETER, "Adapter %08lx:%08lx: reservation of 0x1800 bytes returned 0x%08lx\n",
       (ULONG)Luid.HighPart, Luid.LowPart, (ULONG)Status);
    if (NT_SUCCESS(Status))
        GpuVaFree(hAdapter, Address, 0x1800, Luid);

    GpuVaMapStates(hAdapter, hPagingQueue, Luid);
}

static void
GpuVaReserveAdapter(D3DKMT_HANDLE hAdapter, LUID Luid)
{
    PFND3DKMT_CREATEDEVICE pfnCreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    PFND3DKMT_DESTROYDEVICE pfnDestroyDevice = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    PFND3DKMT_CREATEPAGINGQUEUE pfnCreateQueue = (PFND3DKMT_CREATEPAGINGQUEUE)LoadD3DKMTProc("D3DKMTCreatePagingQueue");
    PFND3DKMT_DESTROYPAGINGQUEUE pfnDestroyQueue = (PFND3DKMT_DESTROYPAGINGQUEUE)LoadD3DKMTProc("D3DKMTDestroyPagingQueue");
    D3DKMT_CREATEDEVICE Device;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    D3DKMT_CREATEPAGINGQUEUE Queue;
    D3DDDI_DESTROYPAGINGQUEUE DestroyQueue;

    if (pfnCreateDevice == NULL || pfnDestroyDevice == NULL || pfnCreateQueue == NULL || pfnDestroyQueue == NULL)
        return;
    memset(&Device, 0, sizeof(Device));
    Device.hAdapter = hAdapter;
    if (!NT_SUCCESS(pfnCreateDevice(&Device)))
        return;
    memset(&Queue, 0, sizeof(Queue));
    Queue.hDevice = Device.hDevice;
    Queue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    if (NT_SUCCESS(pfnCreateQueue(&Queue)) && Queue.hPagingQueue != 0)
    {
        GpuVaReserveQueue(hAdapter, Queue.hPagingQueue, Luid);
        memset(&DestroyQueue, 0, sizeof(DestroyQueue));
        DestroyQueue.hPagingQueue = Queue.hPagingQueue;
        pfnDestroyQueue(&DestroyQueue);
    }
    memset(&DestroyDevice, 0, sizeof(DestroyDevice));
    DestroyDevice.hDevice = Device.hDevice;
    pfnDestroyDevice(&DestroyDevice);
}

START_TEST(gpuvareserve)
{
    PFND3DKMT_ENUMADAPTERS2 pfnEnumAdapters2 = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    PFND3DKMT_CLOSEADAPTER pfnCloseAdapter = (PFND3DKMT_CLOSEADAPTER)LoadD3DKMTProc("D3DKMTCloseAdapter");
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    D3DKMT_CLOSEADAPTER Close;
    ULONG Index;

    pfnReserve = (PFND3DKMT_RESERVEGPUVIRTUALADDRESS)LoadD3DKMTProc("D3DKMTReserveGpuVirtualAddress");
    pfnFree = (PFND3DKMT_FREEGPUVIRTUALADDRESS)LoadD3DKMTProc("D3DKMTFreeGpuVirtualAddress");
    pfnMap = (PFND3DKMT_MAPGPUVIRTUALADDRESS)LoadD3DKMTProc("D3DKMTMapGpuVirtualAddress");
    if (pfnEnumAdapters2 == NULL || pfnCloseAdapter == NULL || pfnReserve == NULL || pfnFree == NULL || pfnMap == NULL)
    {
        skip("D3DKMT adapter or GPU virtual address entry points are not exported\n");
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
            GpuVaReserveAdapter(Adapters[Index].hAdapter, Adapters[Index].AdapterLuid);
            memset(&Close, 0, sizeof(Close));
            Close.hAdapter = Adapters[Index].hAdapter;
            pfnCloseAdapter(&Close);
        }
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
}
