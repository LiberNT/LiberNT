/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the D3DKMT services an application runtime needs, in the caller's bitness
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"
#include <d3dkmthk.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

typedef NTSTATUS (APIENTRY *KMT_CALL)(void *);

typedef struct _APPSMOKE_ADAPTER
{
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    D3DKMT_DRIVERVERSION DriverVersion;
} APPSMOKE_ADAPTER;

static FARPROC AppSmokeKmtProc(const char *Name)
{
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");

    return Gdi32 ? GetProcAddress(Gdi32, Name) : NULL;
}

static NTSTATUS KmtCall(const char *Name, void *Arguments)
{
    KMT_CALL Call = (KMT_CALL)AppSmokeKmtProc(Name);

    return Call ? Call(Arguments) : STATUS_PROCEDURE_NOT_FOUND;
}

static NTSTATUS QueryAdapter(D3DKMT_HANDLE hAdapter, KMTQUERYADAPTERINFOTYPE Type, void *Data, UINT Size)
{
    D3DKMT_QUERYADAPTERINFO Query;

    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = Type;
    Query.pPrivateDriverData = Data;
    Query.PrivateDriverDataSize = Size;
    return KmtCall("D3DKMTQueryAdapterInfo", &Query);
}

static BOOL AppSmokeOpenAdapter(APPSMOKE_ADAPTER *Adapter)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    DISPLAY_DEVICEW Device;
    UINT Index;

    memset(Adapter, 0, sizeof(*Adapter));
    memset(&Open, 0, sizeof(Open));
    for (Index = 0; ; ++Index)
    {
        memset(&Device, 0, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            return FALSE;
        if (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            break;
    }

    lstrcpynW(Open.DeviceName, Device.DeviceName, ARRAYSIZE(Open.DeviceName));
    if (!NT_SUCCESS(KmtCall("D3DKMTOpenAdapterFromGdiDisplayName", &Open)) || !Open.hAdapter)
        return FALSE;

    Adapter->hAdapter = Open.hAdapter;
    Adapter->Luid = Open.AdapterLuid;
    if (!NT_SUCCESS(QueryAdapter(Open.hAdapter, KMTQAITYPE_DRIVERVERSION,
                                 &Adapter->DriverVersion, sizeof(Adapter->DriverVersion))))
    {
        Adapter->DriverVersion = KMT_DRIVERVERSION_WDDM_1_0;
    }
    return TRUE;
}

static void AppSmokeCloseAdapter(APPSMOKE_ADAPTER *Adapter)
{
    D3DKMT_CLOSEADAPTER Close;

    if (!Adapter->hAdapter)
        return;
    Close.hAdapter = Adapter->hAdapter;
    KmtCall("D3DKMTCloseAdapter", &Close);
    Adapter->hAdapter = 0;
}

static BOOL AppSmokeUmdName(KMTUMDVERSION Version, WCHAR *Name, UINT Count)
{
    D3DKMT_UMDFILENAMEINFO Info;
    APPSMOKE_ADAPTER Adapter;
    NTSTATUS Status;

    Name[0] = 0;
    if (!AppSmokeOpenAdapter(&Adapter))
        return FALSE;

    memset(&Info, 0, sizeof(Info));
    Info.Version = Version;
    Status = QueryAdapter(Adapter.hAdapter, KMTQAITYPE_UMDRIVERNAME, &Info, sizeof(Info));
    AppSmokeCloseAdapter(&Adapter);
    if (!NT_SUCCESS(Status) || !Info.UmdFileName[0])
        return FALSE;

    lstrcpynW(Name, Info.UmdFileName, Count);
    return TRUE;
}

BOOL AppSmokeD3D11DriverName(WCHAR *Name, UINT Count)
{
    return AppSmokeUmdName(KMTUMDVERSION_DX11, Name, Count);
}

BOOL AppSmokeD3D12DriverName(WCHAR *Name, UINT Count)
{
    return AppSmokeUmdName(KMTUMDVERSION_DX12, Name, Count);
}

BOOL AppSmokeOpenGlIcdName(WCHAR *Name, UINT Count)
{
    APPSMOKE_ADAPTER Adapter;
    D3DKMT_OPENGLINFO Info;
    NTSTATUS Status;

    Name[0] = 0;
    if (!AppSmokeOpenAdapter(&Adapter))
        return FALSE;

    memset(&Info, 0, sizeof(Info));
    Status = QueryAdapter(Adapter.hAdapter, KMTQAITYPE_UMOPENGLINFO, &Info, sizeof(Info));
    AppSmokeCloseAdapter(&Adapter);
    if (!NT_SUCCESS(Status) || !Info.UmdOpenGlIcdFileName[0])
        return FALSE;

    lstrcpynW(Name, Info.UmdOpenGlIcdFileName, Count);
    return TRUE;
}

BOOL AppSmokeModuleLoaded(const WCHAR *Path)
{
    const WCHAR *Name = wcsrchr(Path, L'\\');

    return GetModuleHandleW(Name ? Name + 1 : Path) != NULL;
}

static VOID APIENTRY TrimCallback(D3DKMT_TRIMNOTIFICATION *Notification)
{
}

static void TestEnumeration(const APPSMOKE_ADAPTER *Adapter)
{
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    D3DKMT_CLOSEADAPTER Close;
    NTSTATUS Status;
    BOOL Found = FALSE;
    ULONG Index;

    if (!AppSmokeKmtProc("D3DKMTEnumAdapters2"))
    {
        skip("D3DKMTEnumAdapters2 is unavailable\n");
        return;
    }

    memset(&Enum, 0, sizeof(Enum));
    Status = KmtCall("D3DKMTEnumAdapters2", &Enum);
    ok(NT_SUCCESS(Status) && Enum.NumAdapters >= 1, "Adapter count query: %#lx, %lu adapters\n", Status, Enum.NumAdapters);
    if (!NT_SUCCESS(Status) || !Enum.NumAdapters)
        return;

    Adapters = (D3DKMT_ADAPTERINFO *)calloc(Enum.NumAdapters, sizeof(*Adapters));
    Enum.pAdapters = Adapters;
    Status = KmtCall("D3DKMTEnumAdapters2", &Enum);
    ok(NT_SUCCESS(Status), "Adapter enumeration: %#lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Enum.NumAdapters; ++Index)
        {
            if (Adapters[Index].AdapterLuid.LowPart == Adapter->Luid.LowPart &&
                Adapters[Index].AdapterLuid.HighPart == Adapter->Luid.HighPart)
            {
                Found = TRUE;
            }
            ok(Adapters[Index].hAdapter != 0, "Adapter %lu has no handle\n", Index);
            Close.hAdapter = Adapters[Index].hAdapter;
            KmtCall("D3DKMTCloseAdapter", &Close);
        }
        ok(Found, "The primary display adapter is not enumerated\n");
    }
    free(Adapters);
}

static void TestFence(D3DKMT_HANDLE hDevice)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    volatile UINT64 *Value;
    UINT64 Target = 9;
    NTSTATUS Status;

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.MonitoredFence.InitialFenceValue = 5;
    Status = KmtCall("D3DKMTCreateSynchronizationObject2", &Create);
    ok(NT_SUCCESS(Status) && Create.hSyncObject, "Monitored fence creation: %#lx\n", Status);
    if (!NT_SUCCESS(Status) || !Create.hSyncObject)
        return;

    Value = (volatile UINT64 *)Create.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    ok(Value != NULL, "The fence has no CPU mapping\n");
    if (Value)
        ok(*Value == 5, "The mapped fence value is %I64u, expected 5\n", *Value);

    memset(&Signal, 0, sizeof(Signal));
    Signal.hDevice = hDevice;
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &Create.hSyncObject;
    Signal.FenceValueArray = &Target;
    Status = KmtCall("D3DKMTSignalSynchronizationObjectFromCpu", &Signal);
    ok(NT_SUCCESS(Status), "Fence signal from the CPU: %#lx\n", Status);
    if (Value && NT_SUCCESS(Status))
        ok(*Value == 9, "The mapped fence value is %I64u, expected 9\n", *Value);

    memset(&Wait, 0, sizeof(Wait));
    Wait.hDevice = hDevice;
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &Create.hSyncObject;
    Wait.FenceValueArray = &Target;
    Status = KmtCall("D3DKMTWaitForSynchronizationObjectFromCpu", &Wait);
    ok(NT_SUCCESS(Status), "Wait for a signaled fence: %#lx\n", Status);

    Destroy.hSyncObject = Create.hSyncObject;
    Status = KmtCall("D3DKMTDestroySynchronizationObject", &Destroy);
    ok(NT_SUCCESS(Status), "Fence destruction: %#lx\n", Status);
}

static void TestPagingQueue(D3DKMT_HANDLE hDevice)
{
    D3DDDI_DESTROYPAGINGQUEUE Destroy;
    D3DKMT_CREATEPAGINGQUEUE Create;
    volatile UINT64 *Value;
    NTSTATUS Status;

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    Status = KmtCall("D3DKMTCreatePagingQueue", &Create);
    ok(NT_SUCCESS(Status) && Create.hPagingQueue, "Paging queue creation: %#lx\n", Status);
    if (!NT_SUCCESS(Status) || !Create.hPagingQueue)
        return;

    Value = (volatile UINT64 *)Create.FenceValueCPUVirtualAddress;
    ok(Create.hSyncObject != 0 && Value != NULL, "The paging queue has no fence: %#x, %p\n", Create.hSyncObject, Value);
    if (Value)
        trace("Paging fence value %I64u\n", *Value);

    Destroy.hPagingQueue = Create.hPagingQueue;
    Status = KmtCall("D3DKMTDestroyPagingQueue", &Destroy);
    ok(NT_SUCCESS(Status), "Paging queue destruction: %#lx\n", Status);
}

static void TestTrimNotification(const APPSMOKE_ADAPTER *Adapter)
{
    D3DKMT_UNREGISTERTRIMNOTIFICATION Unregister;
    D3DKMT_REGISTERTRIMNOTIFICATION Register;
    NTSTATUS Status;

    memset(&Register, 0, sizeof(Register));
    Register.AdapterLuid = Adapter->Luid;
    Register.Callback = TrimCallback;
    Register.Context = &Register;
    Status = KmtCall("D3DKMTRegisterTrimNotification", &Register);
    ok(NT_SUCCESS(Status) && Register.Handle, "Trim notification registration: %#lx\n", Status);
    if (!NT_SUCCESS(Status) || !Register.Handle)
        return;

    memset(&Unregister, 0, sizeof(Unregister));
    Unregister.Handle = Register.Handle;
    Unregister.Callback = TrimCallback;
    Status = KmtCall("D3DKMTUnregisterTrimNotification", &Unregister);
    ok(NT_SUCCESS(Status), "Trim notification removal: %#lx\n", Status);
}

START_TEST(d3dkmt)
{
    D3DKMT_QUERYVIDEOMEMORYINFO Memory;
    D3DKMT_UMDFILENAMEINFO UmdName;
    D3DKMT_DESTROYDEVICE Destroy;
    D3DKMT_GETDEVICESTATE State;
    D3DKMT_CREATEDEVICE Create;
    APPSMOKE_ADAPTER Adapter;
    NTSTATUS Status;

    if (!AppSmokeOpenAdapter(&Adapter))
    {
        skip("The primary display has no D3DKMT adapter\n");
        return;
    }
    trace("Adapter driver model %u\n", (UINT)Adapter.DriverVersion);

    TestEnumeration(&Adapter);

    memset(&UmdName, 0, sizeof(UmdName));
    UmdName.Version = KMTUMDVERSION_DX11;
    Status = QueryAdapter(Adapter.hAdapter, KMTQAITYPE_UMDRIVERNAME, &UmdName, sizeof(UmdName));
    if (NT_SUCCESS(Status))
    {
        ok(UmdName.UmdFileName[0] != 0, "The Direct3D 11 driver name is empty\n");
        trace("Direct3D 11 driver %ls\n", UmdName.UmdFileName);
    }
    else
    {
        trace("No Direct3D 11 driver name: %#lx\n", Status);
    }

    memset(&Create, 0, sizeof(Create));
    Create.hAdapter = Adapter.hAdapter;
    Status = KmtCall("D3DKMTCreateDevice", &Create);
    ok(NT_SUCCESS(Status) && Create.hDevice, "Device creation: %#lx\n", Status);
    if (NT_SUCCESS(Status) && Create.hDevice)
    {
        memset(&State, 0, sizeof(State));
        State.hDevice = Create.hDevice;
        State.StateType = D3DKMT_DEVICESTATE_EXECUTION;
        Status = KmtCall("D3DKMTGetDeviceState", &State);
        ok(NT_SUCCESS(Status) && State.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE,
           "Device execution state: %#lx, %u\n", Status, (UINT)State.ExecutionState);

        if (Adapter.DriverVersion >= KMT_DRIVERVERSION_WDDM_2_0)
        {
            TestFence(Create.hDevice);
            TestPagingQueue(Create.hDevice);
            TestTrimNotification(&Adapter);

            memset(&Memory, 0, sizeof(Memory));
            Memory.hAdapter = Adapter.hAdapter;
            Memory.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;
            Status = KmtCall("D3DKMTQueryVideoMemoryInfo", &Memory);
            ok(NT_SUCCESS(Status) && Memory.Budget != 0, "Video memory budget: %#lx, %I64u bytes\n", Status, Memory.Budget);
        }
        else
        {
            skip("The adapter driver model predates WDDM 2.0\n");
        }

        Destroy.hDevice = Create.hDevice;
        Status = KmtCall("D3DKMTDestroyDevice", &Destroy);
        ok(NT_SUCCESS(Status), "Device destruction: %#lx\n", Status);
    }

    AppSmokeCloseAdapter(&Adapter);
}
