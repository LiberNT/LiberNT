/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NT-handle sharing of monitored fences between devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022L)
#endif
#ifndef STATUS_OBJECT_TYPE_MISMATCH
#define STATUS_OBJECT_TYPE_MISMATCH ((NTSTATUS)0xC0000024L)
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008L)
#endif
#ifndef STATUS_OBJECT_NAME_COLLISION
#define STATUS_OBJECT_NAME_COLLISION ((NTSTATUS)0xC0000035L)
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif

typedef struct _TEST_OBJECT_ATTRIBUTES
{
    ULONG Length;
    HANDLE RootDirectory;
    PVOID ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} TEST_OBJECT_ATTRIBUTES;

static PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreateSync2;
static PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroySync;
static PFND3DKMT_SHAREOBJECTS pShareObjects;
static PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE pOpenFromNt;
static PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 pOpenFromNt2;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU pSignalCpu;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU pWaitCpu;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU pWaitGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 pSignalGpu2;
static PFND3DKMT_CREATECONTEXT pCreateContext;
static PFND3DKMT_CREATECONTEXTVIRTUAL pCreateContextVirtual;
static PFND3DKMT_DESTROYCONTEXT pDestroyContext;

static NTSTATUS
CreateFence(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ UINT FlagsValue,
    _In_ UINT64 InitialValue,
    _Out_ D3DKMT_HANDLE *hFence,
    _Out_ volatile UINT64 **CpuValue)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    NTSTATUS Status;

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.Value = FlagsValue;
    Create.Info.MonitoredFence.InitialFenceValue = InitialValue;
    Status = pCreateSync2(&Create);
    *hFence = NT_SUCCESS(Status) ? Create.hSyncObject : 0;
    *CpuValue = NT_SUCCESS(Status) ? (volatile UINT64 *)Create.Info.MonitoredFence.FenceValueCPUVirtualAddress : NULL;
    return Status;
}

static VOID
DestroySync(_In_ D3DKMT_HANDLE hSync)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;

    if (hSync == 0)
        return;
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hSyncObject = hSync;
    ok(NT_SUCCESS(pDestroySync(&Destroy)), "DestroySynchronizationObject(0x%lx) failed\n", (ULONG)hSync);
}

static NTSTATUS
ShareSync(_In_ D3DKMT_HANDLE hSync, _In_ DWORD Access, _Out_ HANDLE *NtHandle)
{
    TEST_OBJECT_ATTRIBUTES Attributes;

    memset(&Attributes, 0, sizeof(Attributes));
    Attributes.Length = sizeof(Attributes);
    *NtHandle = NULL;
    return pShareObjects(1, &hSync, (POBJECT_ATTRIBUTES)&Attributes, Access, NtHandle);
}

static NTSTATUS
OpenSync2(
    _In_ HANDLE NtHandle,
    _In_ D3DKMT_HANDLE hDevice,
    _In_ UINT FlagsValue,
    _Out_ D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 *Open)
{
    memset(Open, 0, sizeof(*Open));
    Open->hNtHandle = NtHandle;
    Open->hDevice = hDevice;
    Open->Flags.Value = FlagsValue;
    return pOpenFromNt2(Open);
}

static NTSTATUS
SignalCpu(_In_ D3DKMT_HANDLE hDevice, _In_ D3DKMT_HANDLE hSync, _In_ UINT64 Value)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal;

    memset(&Signal, 0, sizeof(Signal));
    Signal.hDevice = hDevice;
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &hSync;
    Signal.FenceValueArray = &Value;
    return pSignalCpu(&Signal);
}

static NTSTATUS
WaitCpuAsync(_In_ D3DKMT_HANDLE hDevice, _In_ D3DKMT_HANDLE hSync, _In_ UINT64 Value, _In_ HANDLE Event)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;

    memset(&Wait, 0, sizeof(Wait));
    Wait.hDevice = hDevice;
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &hSync;
    Wait.FenceValueArray = &Value;
    Wait.hAsyncEvent = Event;
    return pWaitCpu(&Wait);
}

static D3DKMT_HANDLE
CreateAnyContext(_In_ D3DKMT_HANDLE hDevice)
{
    D3DKMT_CREATECONTEXTVIRTUAL Virtual;
    D3DKMT_CREATECONTEXT Context;

    if (pCreateContextVirtual != NULL)
    {
        memset(&Virtual, 0, sizeof(Virtual));
        Virtual.hDevice = hDevice;
        Virtual.ClientHint = D3DKMT_CLIENTHINT_DX12;
        if (NT_SUCCESS(pCreateContextVirtual(&Virtual)))
            return Virtual.hContext;
    }
    if (pCreateContext != NULL)
    {
        memset(&Context, 0, sizeof(Context));
        Context.hDevice = hDevice;
        if (NT_SUCCESS(pCreateContext(&Context)))
            return Context.hContext;
    }
    return 0;
}

static VOID
DestroyAnyContext(_In_ D3DKMT_HANDLE hContext)
{
    D3DKMT_DESTROYCONTEXT Destroy;

    if (hContext == 0 || pDestroyContext == NULL)
        return;
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hContext = hContext;
    pDestroyContext(&Destroy);
}

/* A fence created without NtSecuritySharing has no NT-handle form. */
static VOID
Test_ShareNeedsNtSecuritySharing(_In_ D3DKMT_HANDLE hDevice)
{
    volatile UINT64 *CpuValue;
    D3DKMT_HANDLE hFence;
    HANDLE NtHandle;
    NTSTATUS Status;

    Status = CreateFence(hDevice, 0, 0, &hFence, &CpuValue);
    ok(Status == STATUS_SUCCESS, "CreateFence(plain) 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Status = ShareSync(hFence, D3DDDI_SYNC_OBJECT_ALL_ACCESS, &NtHandle);
    ok(Status == STATUS_INVALID_PARAMETER, "ShareObjects(plain fence) 0x%lx\n", Status);
    ok(NtHandle == NULL, "ShareObjects(plain fence) returned handle %p\n", NtHandle);
    if (NtHandle != NULL)
        CloseHandle(NtHandle);
    DestroySync(hFence);

    Status = CreateFence(hDevice, 1, 0, &hFence, &CpuValue);
    ok(Status == STATUS_INVALID_PARAMETER, "CreateFence(Shared without NtSecuritySharing) 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
        DestroySync(hFence);
}

/* The open refuses what cannot name a device sync object. */
static VOID
Test_OpenRefusals(_In_ D3DKMT_HANDLE hDevice, _In_ HANDLE NtHandle)
{
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open;
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE OpenV1;
    HANDLE Event;
    NTSTATUS Status;

    Status = OpenSync2(NtHandle, 0, 0, &Open);
    ok(Status == STATUS_INVALID_PARAMETER, "OpenSyncObjectFromNtHandle2(no device) 0x%lx\n", Status);
    ok(Open.hSyncObject == 0, "no-device open returned 0x%lx\n", (ULONG)Open.hSyncObject);

    Status = OpenSync2(NtHandle, hDevice, 1u << 20, &Open);
    ok(Status == STATUS_INVALID_PARAMETER, "OpenSyncObjectFromNtHandle2(reserved flag) 0x%lx\n", Status);

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Status = OpenSync2(Event, hDevice, 0, &Open);
    ok(Status == STATUS_OBJECT_TYPE_MISMATCH, "OpenSyncObjectFromNtHandle2(event handle) 0x%lx\n", Status);
    CloseHandle(Event);

    if (pOpenFromNt != NULL)
    {
        memset(&OpenV1, 0, sizeof(OpenV1));
        OpenV1.hNtHandle = NtHandle;
        Status = pOpenFromNt(&OpenV1);
        ok(Status == STATUS_INVALID_PARAMETER, "OpenSyncObjectFromNtHandle(monitored fence) 0x%lx\n", Status);
        ok(OpenV1.hSyncObject == 0, "v1 open returned 0x%lx\n", (ULONG)OpenV1.hSyncObject);
    }
}

/* Both views read and move one fence value, from either device. */
static VOID
Test_OpenedViewSharesTheFence(
    _In_ D3DKMT_HANDLE hDeviceA,
    _In_ D3DKMT_HANDLE hFenceA,
    _In_ volatile UINT64 *CpuValueA,
    _In_ D3DKMT_HANDLE hDeviceB,
    _In_ D3DKMT_HANDLE hFenceB,
    _In_ volatile UINT64 *CpuValueB)
{
    HANDLE Event;
    NTSTATUS Status;

    ok(*CpuValueB == 3, "opened view reads %I64u, creator initialised 3\n", *CpuValueB);

    Status = SignalCpu(hDeviceA, hFenceA, 5);
    ok(Status == STATUS_SUCCESS, "creator SignalFromCpu(5) 0x%lx\n", Status);
    ok(*CpuValueA == 5, "creator view reads %I64u after 5\n", *CpuValueA);
    ok(*CpuValueB == 5, "opened view reads %I64u after the creator signalled 5\n", *CpuValueB);

    Status = SignalCpu(hDeviceB, hFenceB, 6);
    ok(Status == STATUS_SUCCESS, "opener SignalFromCpu(6) 0x%lx\n", Status);
    ok(*CpuValueA == 6, "creator view reads %I64u after the opener signalled 6\n", *CpuValueA);

    Status = SignalCpu(hDeviceA, hFenceB, 7);
    ok(Status == STATUS_INVALID_PARAMETER, "SignalFromCpu(opened handle on the creator device) 0x%lx\n", Status);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Status = WaitCpuAsync(hDeviceB, hFenceB, 6, Event);
    ok(Status == STATUS_SUCCESS, "opener WaitFromCpu(6) 0x%lx\n", Status);
    ok(WaitForSingleObject(Event, 2000) == WAIT_OBJECT_0, "opener wait for a reached value did not complete\n");

    ResetEvent(Event);
    Status = WaitCpuAsync(hDeviceB, hFenceB, 8, Event);
    ok(Status == STATUS_SUCCESS, "opener WaitFromCpu(8) 0x%lx\n", Status);
    ok(WaitForSingleObject(Event, 0) == WAIT_TIMEOUT, "opener wait for 8 completed at 6\n");
    Status = SignalCpu(hDeviceA, hFenceA, 8);
    ok(Status == STATUS_SUCCESS, "creator SignalFromCpu(8) 0x%lx\n", Status);
    ok(WaitForSingleObject(Event, 2000) == WAIT_OBJECT_0, "creator signal did not release the opener's wait\n");

    ResetEvent(Event);
    Status = WaitCpuAsync(hDeviceA, hFenceA, 9, Event);
    ok(Status == STATUS_SUCCESS, "creator WaitFromCpu(9) 0x%lx\n", Status);
    Status = SignalCpu(hDeviceB, hFenceB, 9);
    ok(Status == STATUS_SUCCESS, "opener SignalFromCpu(9) 0x%lx\n", Status);
    ok(WaitForSingleObject(Event, 2000) == WAIT_OBJECT_0, "opener signal did not release the creator's wait\n");
    CloseHandle(Event);
}

/* Access granted to the NT handle decides what the opened view may do. */
static VOID
Test_AccessBecomesViewFlags(
    _In_ D3DKMT_HANDLE hDeviceA,
    _In_ D3DKMT_HANDLE hFenceA,
    _In_ D3DKMT_HANDLE hDeviceB)
{
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open;
    HANDLE NtHandle;
    HANDLE Event;
    NTSTATUS Status;

    Status = ShareSync(hFenceA, STANDARD_RIGHTS_READ | SYNCHRONIZE | D3DDDI_SYNC_OBJECT_WAIT, &NtHandle);
    ok(Status == STATUS_SUCCESS, "ShareObjects(wait access) 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Status = OpenSync2(NtHandle, hDeviceB, 0, &Open);
    ok(Status == STATUS_SUCCESS, "OpenSyncObjectFromNtHandle2(wait access) 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = SignalCpu(hDeviceB, Open.hSyncObject, 100);
        ok(Status == STATUS_ACCESS_DENIED, "SignalFromCpu through a wait-only view 0x%lx\n", Status);
        Event = CreateEventW(NULL, TRUE, FALSE, NULL);
        Status = WaitCpuAsync(hDeviceB, Open.hSyncObject, 1, Event);
        ok(Status == STATUS_SUCCESS, "WaitFromCpu through a wait-only view 0x%lx\n", Status);
        ok(WaitForSingleObject(Event, 2000) == WAIT_OBJECT_0, "wait-only view wait did not complete\n");
        CloseHandle(Event);
        DestroySync(Open.hSyncObject);
    }
    CloseHandle(NtHandle);

    Status = ShareSync(hFenceA, STANDARD_RIGHTS_READ | D3DDDI_SYNC_OBJECT_SIGNAL, &NtHandle);
    ok(Status == STATUS_SUCCESS, "ShareObjects(signal access) 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Status = OpenSync2(NtHandle, hDeviceB, 0, &Open);
    ok(Status == STATUS_SUCCESS, "OpenSyncObjectFromNtHandle2(signal access) 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Event = CreateEventW(NULL, TRUE, FALSE, NULL);
        Status = WaitCpuAsync(hDeviceB, Open.hSyncObject, 1, Event);
        ok(Status == STATUS_ACCESS_DENIED, "WaitFromCpu through a signal-only view 0x%lx\n", Status);
        CloseHandle(Event);
        DestroySync(Open.hSyncObject);
    }
    CloseHandle(NtHandle);
    (VOID)hDeviceA;
}

static VOID
ProbeGenericAccess(
    _In_ D3DKMT_HANDLE hFenceA,
    _In_ D3DKMT_HANDLE hDeviceB,
    _In_ DWORD Access,
    _In_ PCSTR Name,
    _Out_ NTSTATUS *OpenStatus,
    _Out_ NTSTATUS *SignalStatus,
    _Out_ NTSTATUS *WaitStatus)
{
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open;
    HANDLE NtHandle;
    HANDLE Event;
    NTSTATUS Status;

    *OpenStatus = *SignalStatus = *WaitStatus = (NTSTATUS)0xFFFFFFFF;
    Status = ShareSync(hFenceA, Access, &NtHandle);
    ok(Status == STATUS_SUCCESS, "ShareObjects(%s) 0x%lx\n", Name, Status);
    if (!NT_SUCCESS(Status))
        return;
    *OpenStatus = OpenSync2(NtHandle, hDeviceB, 0, &Open);
    if (NT_SUCCESS(*OpenStatus))
    {
        *SignalStatus = SignalCpu(hDeviceB, Open.hSyncObject, *(volatile UINT64 *)Open.MonitoredFence.FenceValueCPUVirtualAddress + 1);
        Event = CreateEventW(NULL, TRUE, FALSE, NULL);
        *WaitStatus = WaitCpuAsync(hDeviceB, Open.hSyncObject, 1, Event);
        if (NT_SUCCESS(*WaitStatus))
            ok(WaitForSingleObject(Event, 2000) == WAIT_OBJECT_0, "%s view wait did not complete\n", Name);
        CloseHandle(Event);
        DestroySync(Open.hSyncObject);
    }
    CloseHandle(NtHandle);
}

/* Generic rights on the NT handle map onto the sync-object rights. */
static VOID
Test_GenericAccessMapping(
    _In_ D3DKMT_HANDLE hFenceA,
    _In_ D3DKMT_HANDLE hDeviceB)
{
    static const struct
    {
        DWORD Access;
        PCSTR Name;
        NTSTATUS Signal;
        NTSTATUS Wait;
    } Cases[] =
    {
        { GENERIC_ALL, "GENERIC_ALL", STATUS_SUCCESS, STATUS_SUCCESS },
        { GENERIC_READ, "GENERIC_READ", STATUS_ACCESS_DENIED, STATUS_SUCCESS },
        { GENERIC_WRITE, "GENERIC_WRITE", STATUS_SUCCESS, STATUS_ACCESS_DENIED },
        { GENERIC_EXECUTE, "GENERIC_EXECUTE", STATUS_ACCESS_DENIED, STATUS_ACCESS_DENIED },
        { GENERIC_READ | GENERIC_WRITE, "GENERIC_READ|WRITE", STATUS_SUCCESS, STATUS_SUCCESS },
    };
    NTSTATUS Open, Signal, Wait;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); ++Index)
    {
        ProbeGenericAccess(hFenceA, hDeviceB, Cases[Index].Access, Cases[Index].Name, &Open, &Signal, &Wait);
        ok(Open == STATUS_SUCCESS, "%s: open 0x%lx\n", Cases[Index].Name, Open);
        ok(Signal == Cases[Index].Signal, "%s: signal 0x%lx, expected 0x%lx\n", Cases[Index].Name, Signal, Cases[Index].Signal);
        ok(Wait == Cases[Index].Wait, "%s: wait 0x%lx, expected 0x%lx\n", Cases[Index].Name, Wait, Cases[Index].Wait);
    }
}

/* A context of the opening device waits on the shared fence on the GPU. */
static VOID
Test_GpuWaitThroughOpenedView(
    _In_ D3DKMT_HANDLE hDeviceA,
    _In_ D3DKMT_HANDLE hFenceA,
    _In_ D3DKMT_HANDLE hDeviceB,
    _In_ D3DKMT_HANDLE hFenceB,
    _In_ UINT64 Target)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait;
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 Signal;
    volatile UINT64 *MarkerValue;
    D3DKMT_HANDLE hContext;
    D3DKMT_HANDLE hMarker;
    UINT64 MarkerTarget = 1;
    HANDLE Event;
    NTSTATUS Status;

    if (pWaitGpu == NULL || pSignalGpu2 == NULL)
    {
        skip("GPU-side wait/signal not exported\n");
        return;
    }
    hContext = CreateAnyContext(hDeviceB);
    if (hContext == 0)
    {
        skip("no context on the opening device\n");
        return;
    }
    Status = CreateFence(hDeviceB, 0, 0, &hMarker, &MarkerValue);
    ok(Status == STATUS_SUCCESS, "CreateFence(marker) 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
    {
        DestroyAnyContext(hContext);
        return;
    }

    memset(&Wait, 0, sizeof(Wait));
    Wait.hContext = hContext;
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &hFenceB;
    Wait.MonitoredFenceValueArray = &Target;
    Status = pWaitGpu(&Wait);
    ok(Status == STATUS_SUCCESS, "WaitFromGpu(opened view) 0x%lx\n", Status);

    memset(&Signal, 0, sizeof(Signal));
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &hMarker;
    Signal.BroadcastContextCount = 1;
    Signal.BroadcastContextArray = &hContext;
    Signal.MonitoredFenceValueArray = &MarkerTarget;
    Status = pSignalGpu2(&Signal);
    ok(Status == STATUS_SUCCESS, "SignalFromGpu2(marker) 0x%lx\n", Status);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Status = WaitCpuAsync(hDeviceB, hMarker, 1, Event);
    ok(Status == STATUS_SUCCESS, "WaitFromCpu(marker) 0x%lx\n", Status);
    ok(WaitForSingleObject(Event, 500) == WAIT_TIMEOUT, "marker passed a GPU wait that was not satisfied\n");
    Status = SignalCpu(hDeviceA, hFenceA, Target);
    ok(Status == STATUS_SUCCESS, "creator SignalFromCpu(target) 0x%lx\n", Status);
    ok(WaitForSingleObject(Event, 5000) == WAIT_OBJECT_0, "creator signal did not release the opener's GPU wait\n");
    ok(*MarkerValue == 1, "marker reads %I64u\n", *MarkerValue);
    CloseHandle(Event);

    DestroyAnyContext(hContext);
    DestroySync(hMarker);
}

typedef struct _TEST_UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} TEST_UNICODE_STRING;

typedef struct _TEST_OPEN_FROM_NAME
{
    DWORD dwDesiredAccess;
    TEST_OBJECT_ATTRIBUTES *pObjAttrib;
    HANDLE hNtHandle;
} TEST_OPEN_FROM_NAME;

typedef NTSTATUS (APIENTRY *PTEST_OPEN_FROM_NAME)(TEST_OPEN_FROM_NAME *);

static VOID
InitName(_Out_ TEST_OBJECT_ATTRIBUTES *Attributes, _Out_ TEST_UNICODE_STRING *String, _In_ PWSTR Buffer)
{
    String->Buffer = Buffer;
    String->Length = (USHORT)(wcslen(Buffer) * sizeof(WCHAR));
    String->MaximumLength = String->Length + sizeof(WCHAR);
    memset(Attributes, 0, sizeof(*Attributes));
    Attributes->Length = sizeof(*Attributes);
    Attributes->ObjectName = String;
}

/* A shared fence exported under a name is found by that name, and only as a sync object. */
static VOID
Test_NamedShare(_In_ D3DKMT_HANDLE hFenceA, _In_ D3DKMT_HANDLE hDeviceB)
{
    PTEST_OPEN_FROM_NAME pOpenSyncByName, pOpenResourceByName;
    TEST_OBJECT_ATTRIBUTES Attributes;
    TEST_UNICODE_STRING String;
    TEST_OPEN_FROM_NAME OpenByName;
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open;
    WCHAR Name[128], Missing[128];
    HANDLE NtHandle = NULL, Second = NULL;
    DWORD Session = 0;
    NTSTATUS Status;

    pOpenSyncByName = (PTEST_OPEN_FROM_NAME)LoadD3DKMTProc("D3DKMTOpenSyncObjectNtHandleFromName");
    pOpenResourceByName = (PTEST_OPEN_FROM_NAME)LoadD3DKMTProc("D3DKMTOpenNtHandleFromName");
    if (pOpenSyncByName == NULL || pOpenResourceByName == NULL)
    {
        skip("named NT-handle opens are not exported\n");
        return;
    }
    ProcessIdToSessionId(GetCurrentProcessId(), &Session);
    _snwprintf(Name, RTL_NUMBER_OF(Name), L"\\Sessions\\%lu\\BaseNamedObjects\\lnt-syncshare-%lu", Session, GetCurrentProcessId());
    _snwprintf(Missing, RTL_NUMBER_OF(Missing), L"\\Sessions\\%lu\\BaseNamedObjects\\lnt-syncshare-missing-%lu", Session, GetCurrentProcessId());

    InitName(&Attributes, &String, Name);
    Status = pShareObjects(1, &hFenceA, (POBJECT_ATTRIBUTES)&Attributes, D3DDDI_SYNC_OBJECT_ALL_ACCESS, &NtHandle);
    ok(Status == STATUS_SUCCESS, "ShareObjects(named) 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Status = pShareObjects(1, &hFenceA, (POBJECT_ATTRIBUTES)&Attributes, D3DDDI_SYNC_OBJECT_ALL_ACCESS, &Second);
    ok(Status == STATUS_OBJECT_NAME_COLLISION, "ShareObjects(same name) 0x%lx\n", Status);
    if (NT_SUCCESS(Status) && Second != NULL)
        CloseHandle(Second);

    memset(&OpenByName, 0, sizeof(OpenByName));
    OpenByName.dwDesiredAccess = D3DDDI_SYNC_OBJECT_ALL_ACCESS;
    OpenByName.pObjAttrib = &Attributes;
    Status = pOpenSyncByName(&OpenByName);
    ok(Status == STATUS_SUCCESS, "OpenSyncObjectNtHandleFromName 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = OpenSync2(OpenByName.hNtHandle, hDeviceB, 0, &Open);
        ok(Status == STATUS_SUCCESS, "OpenSyncObjectFromNtHandle2(handle opened by name) 0x%lx\n", Status);
        if (NT_SUCCESS(Status))
            DestroySync(Open.hSyncObject);
        CloseHandle(OpenByName.hNtHandle);
    }

    OpenByName.hNtHandle = NULL;
    Status = pOpenResourceByName(&OpenByName);
    ok(Status == STATUS_OBJECT_TYPE_MISMATCH, "OpenNtHandleFromName(sync object name) 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
        CloseHandle(OpenByName.hNtHandle);

    InitName(&Attributes, &String, Missing);
    OpenByName.hNtHandle = NULL;
    OpenByName.pObjAttrib = &Attributes;
    Status = pOpenSyncByName(&OpenByName);
    ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "OpenSyncObjectNtHandleFromName(missing) 0x%lx\n", Status);

    CloseHandle(NtHandle);
    InitName(&Attributes, &String, Name);
    OpenByName.hNtHandle = NULL;
    Status = pOpenSyncByName(&OpenByName);
    ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "OpenSyncObjectNtHandleFromName after the last handle closed 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
        CloseHandle(OpenByName.hNtHandle);
}

START_TEST(syncshare)
{
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open;
    volatile UINT64 *CpuValueA;
    D3DKMT_HANDLE hAdapter, hDeviceA, hDeviceB, hFenceA;
    HANDLE NtHandle;
    NTSTATUS Status;

    pCreateSync2 = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pDestroySync = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    pShareObjects = (PFND3DKMT_SHAREOBJECTS)LoadD3DKMTProc("D3DKMTShareObjects");
    pOpenFromNt = (PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE)LoadD3DKMTProc("D3DKMTOpenSyncObjectFromNtHandle");
    pOpenFromNt2 = (PFND3DKMT_OPENSYNCOBJECTFROMNTHANDLE2)LoadD3DKMTProc("D3DKMTOpenSyncObjectFromNtHandle2");
    pSignalCpu = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU)LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromCpu");
    pWaitCpu = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU)LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromCpu");
    pWaitGpu = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU)LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromGpu");
    pSignalGpu2 = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2)LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromGpu2");
    pCreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    pCreateContextVirtual = (PFND3DKMT_CREATECONTEXTVIRTUAL)LoadD3DKMTProc("D3DKMTCreateContextVirtual");
    pDestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    if (!pCreateSync2 || !pDestroySync || !pShareObjects || !pOpenFromNt2 || !pSignalCpu || !pWaitCpu)
    {
        skip("monitored-fence sharing entry points are not exported\n");
        return;
    }

    hAdapter = OpenRenderAdapter();
    if (hAdapter == 0)
    {
        skip("no render adapter\n");
        return;
    }
    hDeviceA = CreateTestDevice(hAdapter);
    hDeviceB = CreateTestDevice(hAdapter);
    if (hDeviceA == 0 || hDeviceB == 0)
    {
        skip("two devices are needed\n");
        DestroyTestDevice(hDeviceA);
        DestroyTestDevice(hDeviceB);
        CloseAdapter(hAdapter);
        return;
    }

    Test_ShareNeedsNtSecuritySharing(hDeviceA);

    Status = CreateFence(hDeviceA, 3, 3, &hFenceA, &CpuValueA);
    ok(Status == STATUS_SUCCESS, "CreateFence(Shared|NtSecuritySharing) 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = ShareSync(hFenceA, D3DDDI_SYNC_OBJECT_ALL_ACCESS, &NtHandle);
        ok(Status == STATUS_SUCCESS, "ShareObjects(fence) 0x%lx\n", Status);
        ok(NtHandle != NULL, "ShareObjects returned no handle\n");
        if (NT_SUCCESS(Status) && NtHandle != NULL)
        {
            Test_OpenRefusals(hDeviceB, NtHandle);
            Status = OpenSync2(NtHandle, hDeviceB, 0, &Open);
            ok(Status == STATUS_SUCCESS, "OpenSyncObjectFromNtHandle2 0x%lx\n", Status);
            ok(Open.hSyncObject != 0 && Open.hSyncObject != hFenceA, "opened handle 0x%lx, creator 0x%lx\n",
               (ULONG)Open.hSyncObject, (ULONG)hFenceA);
            ok(Open.MonitoredFence.FenceValueCPUVirtualAddress != NULL, "opened view has no CPU value address\n");
            if (NT_SUCCESS(Status) && Open.MonitoredFence.FenceValueCPUVirtualAddress != NULL)
            {
                Test_OpenedViewSharesTheFence(hDeviceA, hFenceA, CpuValueA, hDeviceB, Open.hSyncObject,
                                              (volatile UINT64 *)Open.MonitoredFence.FenceValueCPUVirtualAddress);
                Test_GpuWaitThroughOpenedView(hDeviceA, hFenceA, hDeviceB, Open.hSyncObject, 20);
                DestroySync(Open.hSyncObject);
            }
            CloseHandle(NtHandle);
        }
        Test_AccessBecomesViewFlags(hDeviceA, hFenceA, hDeviceB);
        Test_GenericAccessMapping(hFenceA, hDeviceB);
        Test_NamedShare(hFenceA, hDeviceB);
        DestroySync(hFenceA);
    }

    DestroyTestDevice(hDeviceB);
    DestroyTestDevice(hDeviceA);
    CloseAdapter(hAdapter);
}
