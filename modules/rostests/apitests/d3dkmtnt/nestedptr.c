/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel addresses in the buffers a D3DKMT descriptor points to
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);

#define NESTEDPTR_USER_SHARED_DATA ((const UCHAR *)(ULONG_PTR)0x7FFE0000)

static PFN_KMT1
NestedPtrWin32u(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

/* The kernel-mode alias of the shared user data page: always mapped, never written here */
static const VOID *
NestedPtrKernelAddress(VOID)
{
    return (const VOID *)(ULONG_PTR)(sizeof(PVOID) == 8 ? 0xFFFFF78000000000ULL : 0xFFDF0000UL);
}

static NTSTATUS
NestedPtrCreateResource(
    _In_ PFN_KMT1 pCreate,
    _In_ D3DKMT_HANDLE hDevice,
    _In_ PVOID Memory,
    _In_ BOOLEAN ExistingHeap,
    _In_opt_ const VOID *RuntimeData,
    _In_ UINT RuntimeDataSize,
    _Out_ D3DKMT_CREATEALLOCATION *Create)
{
    static D3DKMT_CREATESTANDARDALLOCATION Standard;
    static D3DDDI_ALLOCATIONINFO2 Info;

    ZeroMemory(&Standard, sizeof(Standard));
    ZeroMemory(&Info, sizeof(Info));
    ZeroMemory(Create, sizeof(*Create));
    Create->hDevice = hDevice;
    Create->NumAllocations = 1;
    Create->pAllocationInfo2 = &Info;
    Create->pPrivateRuntimeData = RuntimeData;
    Create->PrivateRuntimeDataSize = RuntimeDataSize;
    Create->Flags.CreateResource = 1;
    Create->Flags.CreateShared = 1;
    if (ExistingHeap)
    {
        Info.pSystemMem = Memory;
        Standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
        Standard.ExistingHeapData.Size = 0x10000;
        Create->pStandardAllocation = &Standard;
        Create->Flags.ExistingSysMem = 1;
        Create->Flags.StandardAllocation = 1;
    }
    return pCreate(Create);
}

static VOID
NestedPtrDestroy(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hResource)
{
    PFN_KMT1 pDestroy = NestedPtrWin32u("NtGdiDdDDIDestroyAllocation");
    D3DKMT_DESTROYALLOCATION Destroy;

    if (pDestroy == NULL)
        return;
    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = hDevice;
    Destroy.hResource = hResource;
    pDestroy(&Destroy);
}

static VOID
NestedPtrRuntimeData(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ PVOID Memory)
{
    PFN_KMT1 pCreate = NestedPtrWin32u("NtGdiDdDDICreateAllocation");
    PFN_KMT1 pQuery = NestedPtrWin32u("NtGdiDdDDIQueryResourceInfo");
    UCHAR RuntimeData[16];
    D3DKMT_CREATEALLOCATION Create;
    D3DKMT_QUERYRESOURCEINFO Query;
    BOOLEAN ExistingHeap = TRUE;
    NTSTATUS Status;

    memcpy(RuntimeData, NESTEDPTR_USER_SHARED_DATA, sizeof(RuntimeData));
    Status = NestedPtrCreateResource(pCreate, hDevice, Memory, ExistingHeap, RuntimeData, sizeof(RuntimeData), &Create);
    if (!NT_SUCCESS(Status))
    {
        ExistingHeap = FALSE;
        Status = NestedPtrCreateResource(pCreate, hDevice, Memory, ExistingHeap, RuntimeData, sizeof(RuntimeData), &Create);
    }
    if (!NT_SUCCESS(Status))
    {
        skip("no shared resource with runtime data can be created (0x%08lx)\n", Status);
        return;
    }
    NestedPtrDestroy(hDevice, Create.hResource);

    Status = NestedPtrCreateResource(pCreate, hDevice, Memory, ExistingHeap, NestedPtrKernelAddress(), sizeof(RuntimeData), &Create);
    ok(!NT_SUCCESS(Status), "runtime data at a kernel address was accepted (0x%08lx)\n", Status);
    trace("runtime data at a kernel address: 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    ZeroMemory(RuntimeData, sizeof(RuntimeData));
    ZeroMemory(&Query, sizeof(Query));
    Query.hDevice = hDevice;
    Query.hGlobalShare = Create.hGlobalShare;
    Query.pPrivateRuntimeData = RuntimeData;
    Query.PrivateRuntimeDataSize = sizeof(RuntimeData);
    if (pQuery != NULL && NT_SUCCESS(pQuery(&Query)))
        ok(memcmp(RuntimeData, NESTEDPTR_USER_SHARED_DATA, sizeof(RuntimeData)) != 0, "the kernel-address runtime data was read from kernel memory\n");
    NestedPtrDestroy(hDevice, Create.hResource);
}

START_TEST(nestedptr)
{
    PFN_KMT1 pEnumAdapters2 = NestedPtrWin32u("NtGdiDdDDIEnumAdapters2");
    PFN_KMT1 pCreateDevice = NestedPtrWin32u("NtGdiDdDDICreateDevice");
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    PVOID Memory;
    UINT Adapter;

    LoadLibraryA("gdi32.dll");
    if (pEnumAdapters2 == NULL || pCreateDevice == NULL || NestedPtrWin32u("NtGdiDdDDICreateAllocation") == NULL)
    {
        skip("D3DKMT services are not available\n");
        return;
    }
    Memory = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (Memory == NULL || !NT_SUCCESS(pEnumAdapters2(&Enum)) || Enum.NumAdapters == 0)
    {
        skip("no D3DKMT adapter\n");
        return;
    }
    for (Adapter = 0; Adapter < Enum.NumAdapters; ++Adapter)
    {
        D3DKMT_CREATEDEVICE Device;

        ZeroMemory(&Device, sizeof(Device));
        Device.hAdapter = Adapters[Adapter].hAdapter;
        if (!NT_SUCCESS(pCreateDevice(&Device)))
            continue;
        NestedPtrRuntimeData(Device.hDevice, Memory);
    }
}
