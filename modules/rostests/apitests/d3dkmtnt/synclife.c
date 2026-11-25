/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared sync objects stay usable for every holder after their creator goes away
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef NTSTATUS (APIENTRY *PFN_KMTSHARE)(UINT, const D3DKMT_HANDLE *, POBJECT_ATTRIBUTES, DWORD, HANDLE *);

static PFN_KMT1
SyncLifeWin32u(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

static D3DKMT_HANDLE
SyncLifeCreateDevice(
    _In_ D3DKMT_HANDLE hAdapter)
{
    D3DKMT_CREATEDEVICE Device;

    ZeroMemory(&Device, sizeof(Device));
    Device.hAdapter = hAdapter;
    if (!NT_SUCCESS(SyncLifeWin32u("NtGdiDdDDICreateDevice")(&Device)))
        return 0;
    return Device.hDevice;
}

static NTSTATUS
SyncLifeDestroyDevice(
    _In_ D3DKMT_HANDLE hDevice)
{
    D3DKMT_DESTROYDEVICE Destroy;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = hDevice;
    return SyncLifeWin32u("NtGdiDdDDIDestroyDevice")(&Destroy);
}

static NTSTATUS
SyncLifeDestroy(
    _In_ D3DKMT_HANDLE hSyncObject)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hSyncObject = hSyncObject;
    return SyncLifeWin32u("NtGdiDdDDIDestroySynchronizationObject")(&Destroy);
}

static NTSTATUS
SyncLifeOpen(
    _In_ HANDLE Shared,
    _In_ D3DKMT_HANDLE hDevice,
    _Out_ D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 *Open)
{
    ZeroMemory(Open, sizeof(*Open));
    Open->hNtHandle = Shared;
    Open->hDevice = hDevice;
    return SyncLifeWin32u("NtGdiDdDDIOpenSyncObjectFromNtHandle2")(Open);
}

static VOID
SyncLifeUse(
    _In_ PCSTR Step,
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ volatile UINT64 *Page,
    _In_ UINT64 Value)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal;
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;
    HANDLE Event;
    NTSTATUS Status;

    ZeroMemory(&Signal, sizeof(Signal));
    Signal.hDevice = hDevice;
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &hSyncObject;
    Signal.FenceValueArray = &Value;
    Status = SyncLifeWin32u("NtGdiDdDDISignalSynchronizationObjectFromCpu")(&Signal);
    ok(Status == STATUS_SUCCESS, "%s: signal %I64u status 0x%08lx\n", Step, Value, Status);
    ok(Page != NULL && *Page == Value, "%s: fence page %I64u, expected %I64u\n", Step, Page != NULL ? *Page : 0, Value);

    Event = CreateEventA(NULL, TRUE, FALSE, NULL);
    ZeroMemory(&Wait, sizeof(Wait));
    Wait.hDevice = hDevice;
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &hSyncObject;
    Wait.FenceValueArray = &Value;
    Wait.hAsyncEvent = Event;
    Status = SyncLifeWin32u("NtGdiDdDDIWaitForSynchronizationObjectFromCpu")(&Wait);
    ok(Status == STATUS_SUCCESS, "%s: wait %I64u status 0x%08lx\n", Step, Value, Status);
    ok(Status != STATUS_SUCCESS || WaitForSingleObject(Event, 100) == WAIT_OBJECT_0, "%s: wait %I64u not signaled\n", Step, Value);
    CloseHandle(Event);
}

static VOID
SyncLifeMonitoredFence(
    _In_ D3DKMT_HANDLE hAdapter)
{
    PFN_KMTSHARE pShareObjects = (PFN_KMTSHARE)SyncLifeWin32u("NtGdiDdDDIShareObjects");
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open, Reopen;
    OBJECT_ATTRIBUTES Attributes;
    D3DKMT_HANDLE hCreator, hHolder;
    HANDLE Shared = NULL;
    NTSTATUS Status;

    hCreator = SyncLifeCreateDevice(hAdapter);
    hHolder = SyncLifeCreateDevice(hAdapter);
    ok(hCreator != 0 && hHolder != 0, "devices 0x%x 0x%x\n", hCreator, hHolder);
    if (hCreator == 0 || hHolder == 0)
        return;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = hCreator;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.Shared = 1;
    Create.Info.Flags.NtSecuritySharing = 1;
    Status = SyncLifeWin32u("NtGdiDdDDICreateSynchronizationObject")(&Create);
    ok(Status == STATUS_SUCCESS, "create status 0x%08lx\n", Status);
    if (Status != STATUS_SUCCESS)
        return;
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    Status = pShareObjects(1, &Create.hSyncObject, &Attributes, GENERIC_ALL, &Shared);
    ok(Status == STATUS_SUCCESS, "share status 0x%08lx\n", Status);
    if (Status != STATUS_SUCCESS)
        return;
    Status = SyncLifeOpen(Shared, hHolder, &Open);
    ok(Status == STATUS_SUCCESS, "open status 0x%08lx\n", Status);
    ok(Open.MonitoredFence.FenceValueCPUVirtualAddress != NULL, "open: no CPU address\n");
    if (Status != STATUS_SUCCESS)
        return;
    SyncLifeUse("before", hHolder, Open.hSyncObject, Open.MonitoredFence.FenceValueCPUVirtualAddress, 3);

    ok_hex(SyncLifeDestroy(Create.hSyncObject), STATUS_SUCCESS);
    SyncLifeUse("creator destroyed", hHolder, Open.hSyncObject, Open.MonitoredFence.FenceValueCPUVirtualAddress, 5);

    Status = SyncLifeOpen(Shared, hHolder, &Reopen);
    ok(Status == STATUS_SUCCESS, "open after the creator's destroy status 0x%08lx\n", Status);
    ok(Status != STATUS_SUCCESS || Reopen.MonitoredFence.FenceValueCPUVirtualAddress != NULL, "reopen: no CPU address\n");
    ok(Status != STATUS_SUCCESS || Reopen.MonitoredFence.FenceValueCPUVirtualAddress == NULL ||
       *(volatile UINT64 *)Reopen.MonitoredFence.FenceValueCPUVirtualAddress == 5, "reopen: wrong fence value\n");

    ok_hex(SyncLifeDestroyDevice(hCreator), STATUS_SUCCESS);
    SyncLifeUse("creator device destroyed", hHolder, Open.hSyncObject, Open.MonitoredFence.FenceValueCPUVirtualAddress, 7);

    CloseHandle(Shared);
    SyncLifeUse("NT handle closed", hHolder, Open.hSyncObject, Open.MonitoredFence.FenceValueCPUVirtualAddress, 9);
    if (Status == STATUS_SUCCESS)
    {
        SyncLifeUse("second view", hHolder, Reopen.hSyncObject, Reopen.MonitoredFence.FenceValueCPUVirtualAddress, 11);
        ok_hex(SyncLifeDestroy(Open.hSyncObject), STATUS_SUCCESS);
        SyncLifeUse("first view destroyed", hHolder, Reopen.hSyncObject, Reopen.MonitoredFence.FenceValueCPUVirtualAddress, 13);
        ok_hex(SyncLifeDestroy(Reopen.hSyncObject), STATUS_SUCCESS);
    }
    else
    {
        ok_hex(SyncLifeDestroy(Open.hSyncObject), STATUS_SUCCESS);
    }
    ok_hex(SyncLifeDestroyDevice(hHolder), STATUS_SUCCESS);
}

static VOID
SyncLifeGlobalShare(
    _In_ D3DKMT_HANDLE hAdapter)
{
    PFN_KMT1 pOpen = SyncLifeWin32u("NtGdiDdDDIOpenSynchronizationObject");
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    D3DKMT_OPENSYNCHRONIZATIONOBJECT Open;
    D3DKMT_HANDLE hDevice;
    NTSTATUS Status;

    if (pOpen == NULL)
    {
        skip("global: no NtGdiDdDDIOpenSynchronizationObject\n");
        return;
    }
    hDevice = SyncLifeCreateDevice(hAdapter);
    ok(hDevice != 0, "global: no device\n");
    if (hDevice == 0)
        return;
    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_SEMAPHORE;
    Create.Info.Semaphore.MaxCount = 4;
    Create.Info.Semaphore.InitialCount = 1;
    Create.Info.Flags.Shared = 1;
    Status = SyncLifeWin32u("NtGdiDdDDICreateSynchronizationObject")(&Create);
    ok(Status == STATUS_SUCCESS, "global: create status 0x%08lx\n", Status);
    if (Status != STATUS_SUCCESS)
        return;
    ok(Create.Info.SharedHandle != 0, "global: no share handle\n");

    ZeroMemory(&Open, sizeof(Open));
    Open.hSharedHandle = Create.Info.SharedHandle;
    Status = pOpen(&Open);
    ok(Status == STATUS_SUCCESS, "global: open status 0x%08lx\n", Status);
    ok_hex(SyncLifeDestroy(Create.hSyncObject), STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        D3DKMT_OPENSYNCHRONIZATIONOBJECT Reopen;

        ZeroMemory(&Reopen, sizeof(Reopen));
        Reopen.hSharedHandle = Create.Info.SharedHandle;
        Status = pOpen(&Reopen);
        ok(Status == STATUS_SUCCESS, "global: open after the creator's destroy status 0x%08lx\n", Status);
        if (Status == STATUS_SUCCESS)
            ok_hex(SyncLifeDestroy(Reopen.hSyncObject), STATUS_SUCCESS);
        ok_hex(SyncLifeDestroy(Open.hSyncObject), STATUS_SUCCESS);
    }
    ok_hex(SyncLifeDestroyDevice(hDevice), STATUS_SUCCESS);
}

START_TEST(synclife)
{
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;

    LoadLibraryA("gdi32.dll");
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(SyncLifeWin32u("NtGdiDdDDIEnumAdapters2")(&Enum)) || Enum.NumAdapters == 0)
    {
        skip("no adapter\n");
        return;
    }
    SyncLifeMonitoredFence(Adapters[Enum.NumAdapters - 1].hAdapter);
    SyncLifeGlobalShare(Adapters[Enum.NumAdapters - 1].hAdapter);
}
