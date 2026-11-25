/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NT-handle sharing of mutex, semaphore, fence and CPU notification sync objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef NTSTATUS (APIENTRY *PFN_KMTSHARE)(UINT, const D3DKMT_HANDLE *, POBJECT_ATTRIBUTES, DWORD, HANDLE *);

static PFN_KMT1
LegacySyncWin32u(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

static D3DKMT_HANDLE
LegacySyncCreateDevice(
    _In_ D3DKMT_HANDLE hAdapter)
{
    D3DKMT_CREATEDEVICE Device;

    ZeroMemory(&Device, sizeof(Device));
    Device.hAdapter = hAdapter;
    if (!NT_SUCCESS(LegacySyncWin32u("NtGdiDdDDICreateDevice")(&Device)))
        return 0;
    return Device.hDevice;
}

static NTSTATUS
LegacySyncCreate(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DDDI_SYNCHRONIZATIONOBJECT_TYPE Type,
    _In_ BOOLEAN Shared,
    _In_ BOOLEAN NtSecuritySharing,
    _In_ HANDLE Event,
    _Out_ D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *Create)
{
    ZeroMemory(Create, sizeof(*Create));
    Create->hDevice = hDevice;
    Create->Info.Type = Type;
    if (Type == D3DDDI_SEMAPHORE)
    {
        Create->Info.Semaphore.MaxCount = 4;
        Create->Info.Semaphore.InitialCount = 1;
    }
    if (Type == D3DDDI_CPU_NOTIFICATION)
        Create->Info.CPUNotification.Event = Event;
    Create->Info.Flags.Shared = Shared;
    Create->Info.Flags.NtSecuritySharing = NtSecuritySharing;
    return LegacySyncWin32u("NtGdiDdDDICreateSynchronizationObject")(Create);
}

static VOID
LegacySyncDestroy(
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ PCSTR What)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;
    NTSTATUS Status;

    Destroy.hSyncObject = hSyncObject;
    Status = LegacySyncWin32u("NtGdiDdDDIDestroySynchronizationObject")(&Destroy);
    ok(Status == STATUS_SUCCESS, "%s: destroy status 0x%08lx\n", What, Status);
}

static VOID
LegacySyncShared(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hDevice2,
    _In_ D3DDDI_SYNCHRONIZATIONOBJECT_TYPE Type,
    _In_ PCSTR Name)
{
    PFN_KMTSHARE pShareObjects = (PFN_KMTSHARE)LegacySyncWin32u("NtGdiDdDDIShareObjects");
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE Open;
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open2;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Shared = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);

    Status = LegacySyncCreate(hDevice, Type, FALSE, TRUE, NULL, &Create);
    ok(Status == STATUS_INVALID_PARAMETER, "%s NtSecuritySharing without Shared: status 0x%08lx\n", Name, Status);

    Status = LegacySyncCreate(hDevice, Type, TRUE, FALSE, NULL, &Create);
    ok(Status == STATUS_SUCCESS, "%s Shared: status 0x%08lx\n", Name, Status);
    if (Status == STATUS_SUCCESS)
    {
        ok(Create.Info.SharedHandle != 0, "%s Shared: no global share handle\n", Name);
        Status = pShareObjects(1, &Create.hSyncObject, &Attributes, GENERIC_ALL, &Shared);
        ok(Status == STATUS_INVALID_PARAMETER, "%s Shared: ShareObjects status 0x%08lx\n", Name, Status);
        if (NT_SUCCESS(Status))
            CloseHandle(Shared);
        LegacySyncDestroy(Create.hSyncObject, Name);
    }

    Status = LegacySyncCreate(hDevice, Type, TRUE, TRUE, NULL, &Create);
    ok(Status == STATUS_SUCCESS, "%s Shared+NtSecuritySharing: status 0x%08lx\n", Name, Status);
    if (Status != STATUS_SUCCESS)
        return;
    ok(Create.hSyncObject != 0, "%s Shared+NtSecuritySharing: no handle\n", Name);
    ok(Create.Info.SharedHandle == 0, "%s Shared+NtSecuritySharing: global share handle 0x%x\n", Name, Create.Info.SharedHandle);

    Shared = NULL;
    Status = pShareObjects(1, &Create.hSyncObject, &Attributes, GENERIC_ALL, &Shared);
    ok(Status == STATUS_SUCCESS, "%s: ShareObjects status 0x%08lx\n", Name, Status);
    if (Status == STATUS_SUCCESS)
    {
        ZeroMemory(&Open, sizeof(Open));
        Open.hNtHandle = Shared;
        Status = LegacySyncWin32u("NtGdiDdDDIOpenSyncObjectFromNtHandle")(&Open);
        ok(Status == STATUS_SUCCESS, "%s: device-less open status 0x%08lx\n", Name, Status);
        ok(Open.hSyncObject != 0 && Open.hSyncObject != Create.hSyncObject, "%s: device-less open handle 0x%x\n", Name, Open.hSyncObject);

        ZeroMemory(&Open2, sizeof(Open2));
        Open2.hNtHandle = Shared;
        Open2.hDevice = hDevice2;
        Status = LegacySyncWin32u("NtGdiDdDDIOpenSyncObjectFromNtHandle2")(&Open2);
        ok(Status == STATUS_SUCCESS, "%s: device open status 0x%08lx\n", Name, Status);
        ok(Open2.hSyncObject != 0, "%s: device open handle 0x%x\n", Name, Open2.hSyncObject);
        ok(Open2.MonitoredFence.FenceValueCPUVirtualAddress == NULL, "%s: device open CPU address %p\n", Name,
           Open2.MonitoredFence.FenceValueCPUVirtualAddress);

        if (Open.hSyncObject != 0)
            LegacySyncDestroy(Open.hSyncObject, Name);
        if (Open2.hSyncObject != 0)
            LegacySyncDestroy(Open2.hSyncObject, Name);
        /* TODO: open after the creator's destroy once the NT object keeps the sync object alive */
        CloseHandle(Shared);
    }
    LegacySyncDestroy(Create.hSyncObject, Name);
}

START_TEST(legacysync)
{
    PFN_KMT1 pEnumAdapters2;
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hDevice2;
    HANDLE Event;
    NTSTATUS Status;

    LoadLibraryA("gdi32.dll");
    pEnumAdapters2 = LegacySyncWin32u("NtGdiDdDDIEnumAdapters2");
    if (pEnumAdapters2 == NULL)
    {
        skip("no NtGdiDdDDIEnumAdapters2\n");
        return;
    }
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(pEnumAdapters2(&Enum)) || Enum.NumAdapters == 0)
    {
        skip("no D3DKMT adapter\n");
        return;
    }
    hDevice = LegacySyncCreateDevice(Adapters[Enum.NumAdapters - 1].hAdapter);
    hDevice2 = LegacySyncCreateDevice(Adapters[Enum.NumAdapters - 1].hAdapter);
    if (hDevice == 0 || hDevice2 == 0)
    {
        skip("no D3DKMT device\n");
        return;
    }

    LegacySyncShared(hDevice, hDevice2, D3DDDI_SYNCHRONIZATION_MUTEX, "mutex");
    LegacySyncShared(hDevice, hDevice2, D3DDDI_SEMAPHORE, "semaphore");
    LegacySyncShared(hDevice, hDevice2, D3DDDI_FENCE, "fence");

    Event = CreateEventA(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEvent failed\n");
    Status = LegacySyncCreate(hDevice, D3DDDI_CPU_NOTIFICATION, TRUE, FALSE, Event, &Create);
    ok(Status == STATUS_INVALID_PARAMETER, "cpu notification Shared: status 0x%08lx\n", Status);
    Status = LegacySyncCreate(hDevice, D3DDDI_CPU_NOTIFICATION, TRUE, TRUE, Event, &Create);
    ok(Status == STATUS_INVALID_PARAMETER, "cpu notification Shared+NtSecuritySharing: status 0x%08lx\n", Status);
    Status = LegacySyncCreate(hDevice, D3DDDI_CPU_NOTIFICATION, FALSE, TRUE, Event, &Create);
    ok(Status == STATUS_INVALID_PARAMETER, "cpu notification NtSecuritySharing: status 0x%08lx\n", Status);
    Status = LegacySyncCreate(hDevice, D3DDDI_CPU_NOTIFICATION, FALSE, FALSE, Event, &Create);
    ok(Status == STATUS_SUCCESS, "cpu notification: status 0x%08lx\n", Status);
    if (Status == STATUS_SUCCESS)
        LegacySyncDestroy(Create.hSyncObject, "cpu notification");
    CloseHandle(Event);
}
