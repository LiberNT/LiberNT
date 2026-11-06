/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Placement and lifetime of monitored fence value storage
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

#define FENCE_COUNT 6

static PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pfnCreateSync;
static PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pfnDestroySync;

static NTSTATUS
CreateFence(D3DKMT_HANDLE hDevice, BOOLEAN Shared, BOOLEAN NoGpuAccess, D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *Create)
{
    memset(Create, 0, sizeof(*Create));
    Create->hDevice = hDevice;
    Create->Info.Type = D3DDDI_MONITORED_FENCE;
    Create->Info.Flags.Shared = Shared;
    Create->Info.Flags.NoGPUAccess = NoGpuAccess;
    return pfnCreateSync(Create);
}

static void
DestroyFence(D3DKMT_HANDLE hSync)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;

    if (hSync == 0)
        return;
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hSyncObject = hSync;
    pfnDestroySync(&Destroy);
}

static void
TraceMapping(const char *What, const void *Address)
{
    MEMORY_BASIC_INFORMATION Info;

    memset(&Info, 0, sizeof(Info));
    if (VirtualQuery(Address, &Info, sizeof(Info)) == 0)
    {
        trace("%s %p: VirtualQuery failed %lu\n", What, Address, GetLastError());
        return;
    }
    trace("%s %p: base %p alloc %p size %Ix state %#lx protect %#lx type %#lx\n", What, Address, Info.BaseAddress,
          Info.AllocationBase, Info.RegionSize, Info.State, Info.Protect, Info.Type);
}

static void
TraceFence(const char *What, ULONG Index, const D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *Create,
           const D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *First)
{
    ULONG_PTR Cpu = (ULONG_PTR)Create->Info.MonitoredFence.FenceValueCPUVirtualAddress;
    ULONGLONG Gpu = Create->Info.MonitoredFence.FenceValueGPUVirtualAddress;
    ULONG_PTR FirstCpu = (ULONG_PTR)First->Info.MonitoredFence.FenceValueCPUVirtualAddress;
    ULONGLONG FirstGpu = First->Info.MonitoredFence.FenceValueGPUVirtualAddress;

    trace("%s %lu: cpu %p (page offset %#Ix, delta %Id) gpu %#I64x (page offset %#I64x, delta %I64d)\n", What, Index,
          (void *)Cpu, Cpu & 0xfff, (LONG_PTR)(Cpu - FirstCpu), Gpu, Gpu & 0xfff, (LONGLONG)(Gpu - FirstGpu));
}

static void
ProbeAdapter(ULONG AdapterIndex, D3DKMT_HANDLE hAdapter)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Fences[FENCE_COUNT];
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Reused, Other, NoGpu, Shared;
    D3DKMT_HANDLE hDevice, hOther;
    const void *Destroyed;
    NTSTATUS Status;
    ULONG Index;

    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        trace("adapter %lu: no device\n", AdapterIndex);
        return;
    }
    memset(Fences, 0, sizeof(Fences));
    for (Index = 0; Index < FENCE_COUNT; ++Index)
    {
        Status = CreateFence(hDevice, FALSE, FALSE, &Fences[Index]);
        if (!NT_SUCCESS(Status))
        {
            trace("adapter %lu: fence %lu create 0x%08lx\n", AdapterIndex, Index, Status);
            break;
        }
        TraceFence("fence", Index, &Fences[Index], &Fences[0]);
    }
    if (Index == FENCE_COUNT)
    {
        TraceMapping("fence 0 mapping", Fences[0].Info.MonitoredFence.FenceValueCPUVirtualAddress);
        Destroyed = Fences[1].Info.MonitoredFence.FenceValueCPUVirtualAddress;
        DestroyFence(Fences[1].hSyncObject);
        Fences[1].hSyncObject = 0;
        TraceMapping("destroyed fence 1 mapping", Destroyed);
        if (NT_SUCCESS(CreateFence(hDevice, FALSE, FALSE, &Reused)))
        {
            TraceFence("after destroying fence 1", 0, &Reused, &Fences[0]);
            DestroyFence(Reused.hSyncObject);
        }
        Status = CreateFence(hDevice, FALSE, TRUE, &NoGpu);
        if (NT_SUCCESS(Status))
        {
            TraceFence("no GPU access", 0, &NoGpu, &Fences[0]);
            DestroyFence(NoGpu.hSyncObject);
        }
        else
            trace("no GPU access fence 0x%08lx\n", Status);
        Status = CreateFence(hDevice, TRUE, FALSE, &Shared);
        if (NT_SUCCESS(Status))
        {
            TraceFence("shared", 0, &Shared, &Fences[0]);
            DestroyFence(Shared.hSyncObject);
        }
        else
            trace("shared fence 0x%08lx\n", Status);
        hOther = CreateTestDevice(hAdapter);
        if (hOther != 0 && NT_SUCCESS(CreateFence(hOther, FALSE, FALSE, &Other)))
        {
            TraceFence("second device", 0, &Other, &Fences[0]);
            DestroyFence(Other.hSyncObject);
        }
        if (hOther != 0)
            DestroyTestDevice(hOther);
        Destroyed = Fences[0].Info.MonitoredFence.FenceValueCPUVirtualAddress;
        for (Index = 0; Index < FENCE_COUNT; ++Index)
        {
            DestroyFence(Fences[Index].hSyncObject);
            Fences[Index].hSyncObject = 0;
        }
        TraceMapping("all fences destroyed, fence 0 mapping", Destroyed);
        DestroyTestDevice(hDevice);
        TraceMapping("device destroyed, fence 0 mapping", Destroyed);
        return;
    }
    for (Index = 0; Index < FENCE_COUNT; ++Index)
        DestroyFence(Fences[Index].hSyncObject);
    DestroyTestDevice(hDevice);
}

START_TEST(fencestorage)
{
    PFND3DKMT_ENUMADAPTERS2 pfnEnumAdapters2 = (PFND3DKMT_ENUMADAPTERS2)LoadD3DKMTProc("D3DKMTEnumAdapters2");
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    ULONG Index;

    pfnCreateSync = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pfnDestroySync = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    if (pfnEnumAdapters2 == NULL || pfnCreateSync == NULL || pfnDestroySync == NULL)
    {
        skip("monitored fence entry points are not exported\n");
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
            trace("adapter %lu luid %08lx:%08lx\n", Index, Adapters[Index].AdapterLuid.HighPart,
                  Adapters[Index].AdapterLuid.LowPart);
            ProbeAdapter(Index, Adapters[Index].hAdapter);
            CloseAdapter(Adapters[Index].hAdapter);
        }
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
}
