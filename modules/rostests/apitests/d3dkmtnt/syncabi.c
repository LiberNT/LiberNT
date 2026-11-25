/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Descriptor read by the synchronization object creation system call
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);

static PFN_KMT1
SyncAbiWin32u(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

static D3DKMT_HANDLE
SyncAbiCreateDevice(VOID)
{
    PFN_KMT1 pEnumAdapters2 = SyncAbiWin32u("NtGdiDdDDIEnumAdapters2");
    PFN_KMT1 pCreateDevice = SyncAbiWin32u("NtGdiDdDDICreateDevice");
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_CREATEDEVICE Device;

    if (pEnumAdapters2 == NULL || pCreateDevice == NULL)
        return 0;
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(pEnumAdapters2(&Enum)) || Enum.NumAdapters == 0)
        return 0;
    ZeroMemory(&Device, sizeof(Device));
    Device.hAdapter = Adapters[Enum.NumAdapters - 1].hAdapter;
    if (!NT_SUCCESS(pCreateDevice(&Device)))
        return 0;
    return Device.hDevice;
}

START_TEST(syncabi)
{
    PFN_KMT1 pCreate;
    PFN_KMT1 pGdiCreateV1;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 V2;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT V1;
    D3DKMT_HANDLE hDevice;
    NTSTATUS Status;

    LoadLibraryA("gdi32.dll");
    pCreate = SyncAbiWin32u("NtGdiDdDDICreateSynchronizationObject");
    ok(pCreate != NULL, "win32u does not export NtGdiDdDDICreateSynchronizationObject\n");
    ok(SyncAbiWin32u("NtGdiDdDDICreateSynchronizationObject2") == NULL, "win32u exports NtGdiDdDDICreateSynchronizationObject2\n");
    if (pCreate == NULL)
        return;
    hDevice = SyncAbiCreateDevice();
    if (hDevice == 0)
    {
        skip("no D3DKMT device\n");
        return;
    }

    ZeroMemory(&V2, sizeof(V2));
    V2.hDevice = hDevice;
    V2.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    Status = pCreate(&V2);
    ok(Status == STATUS_SUCCESS, "v2 mutex: status 0x%08lx\n", Status);
    ok(V2.hSyncObject != 0, "v2 mutex: no handle at the v2 offset\n");

    ZeroMemory(&V2, sizeof(V2));
    V2.hDevice = hDevice;
    V2.Info.Type = D3DDDI_MONITORED_FENCE;
    V2.Info.Flags.Shared = 1;
    V2.Info.Flags.NtSecuritySharing = 1;
    Status = pCreate(&V2);
    ok(Status == STATUS_SUCCESS, "v2 shared monitored fence: status 0x%08lx\n", Status);
    ok(V2.hSyncObject != 0, "v2 shared monitored fence: no handle\n");
    ok(V2.Info.MonitoredFence.FenceValueCPUVirtualAddress != NULL, "v2 shared monitored fence: no CPU address\n");

    ZeroMemory(&V2, sizeof(V2));
    V2.hDevice = hDevice;
    V2.Info.Type = D3DDDI_MONITORED_FENCE;
    V2.Info.Flags.Shared = 1;
    Status = pCreate(&V2);
    ok(Status == STATUS_INVALID_PARAMETER, "v2 shared monitored fence without NT sharing: status 0x%08lx\n", Status);

    ZeroMemory(&V1, sizeof(V1));
    V1.hDevice = hDevice;
    V1.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    Status = pCreate(&V1);
    ok(Status == STATUS_INVALID_PARAMETER, "v1-shaped mutex: status 0x%08lx\n", Status);
    ok(V1.hSyncObject == 0, "v1-shaped mutex: handle 0x%x\n", V1.hSyncObject);

    pGdiCreateV1 = (PFN_KMT1)GetProcAddress(GetModuleHandleA("gdi32.dll"), "D3DKMTCreateSynchronizationObject");
    if (pGdiCreateV1 == NULL)
    {
        skip("gdi32 does not export D3DKMTCreateSynchronizationObject\n");
        return;
    }
    ZeroMemory(&V1, sizeof(V1));
    V1.hDevice = hDevice;
    V1.Info.Type = D3DDDI_SEMAPHORE;
    V1.Info.Semaphore.MaxCount = 2;
    V1.Info.Semaphore.InitialCount = 1;
    Status = pGdiCreateV1(&V1);
    ok(Status == STATUS_SUCCESS, "gdi32 v1 semaphore: status 0x%08lx\n", Status);
    ok(V1.hSyncObject != 0, "gdi32 v1 semaphore: no handle\n");
}
