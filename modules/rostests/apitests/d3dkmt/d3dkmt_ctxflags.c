/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMTCreateContextVirtual creation flags
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"

#define CTXFLAGS_MAX_ADAPTERS 16
#define CTXFLAGS_FENCE_TIMEOUT_MS 2000
#define CTXFLAGS_HOLD_MS 100

typedef struct _CTXFLAGS_API
{
    PFN_D3DKMTEnumAdapters EnumAdapters;
    PFN_D3DKMTQueryAdapterInfo QueryAdapterInfo;
    PFND3DKMT_CREATECONTEXTVIRTUAL CreateContext;
    PFND3DKMT_DESTROYCONTEXT DestroyContext;
    PFND3DKMT_CREATEPAGINGQUEUE CreatePagingQueue;
    PFND3DKMT_DESTROYPAGINGQUEUE DestroyPagingQueue;
    PFND3DKMT_RESERVEGPUVIRTUALADDRESS Reserve;
    PFND3DKMT_FREEGPUVIRTUALADDRESS Free;
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
    PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU SignalFromGpu;
    PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU WaitFromGpu;
    PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU SignalFromCpu;
    PFND3DKMT_SUBMITCOMMAND Submit;
} CTXFLAGS_API;

typedef struct _CTXFLAGS_FENCE
{
    D3DKMT_HANDLE Handle;
    volatile const UINT64 *Value;
} CTXFLAGS_FENCE;

typedef enum _CTXFLAGS_EXPECT
{
    CtxFlagsAccepted,
    CtxFlagsHwQueue,
    CtxFlagsInvalid,
} CTXFLAGS_EXPECT;

static const struct
{
    UINT Value;
    const char *Name;
    CTXFLAGS_EXPECT Expect;
} CtxFlagBits[] =
{
    { 0x00000001, "NullRendering", CtxFlagsAccepted },
    { 0x00000002, "InitialData", CtxFlagsAccepted },
    { 0x00000004, "DisableGpuTimeout", CtxFlagsAccepted },
    { 0x00000008, "SynchronizationOnly", CtxFlagsAccepted },
    { 0x00000010, "HwQueueSupported", CtxFlagsHwQueue },
    { 0x00000020, "NoKmdAccess", CtxFlagsAccepted },
    { 0x00000040, "TestContext", CtxFlagsAccepted },
    { 0x00000080, "Reserved7", CtxFlagsInvalid },
    { 0x80000000, "Reserved31", CtxFlagsInvalid },
};

static BOOL
CtxFlagsLoad(CTXFLAGS_API *Api)
{
    Api->EnumAdapters = (PFN_D3DKMTEnumAdapters)LoadD3DKMTProc("D3DKMTEnumAdapters");
    Api->QueryAdapterInfo = (PFN_D3DKMTQueryAdapterInfo)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    Api->CreateContext = (PFND3DKMT_CREATECONTEXTVIRTUAL)LoadD3DKMTProc("D3DKMTCreateContextVirtual");
    Api->DestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    Api->CreatePagingQueue = (PFND3DKMT_CREATEPAGINGQUEUE)LoadD3DKMTProc("D3DKMTCreatePagingQueue");
    Api->DestroyPagingQueue = (PFND3DKMT_DESTROYPAGINGQUEUE)LoadD3DKMTProc("D3DKMTDestroyPagingQueue");
    Api->Reserve = (PFND3DKMT_RESERVEGPUVIRTUALADDRESS)LoadD3DKMTProc("D3DKMTReserveGpuVirtualAddress");
    Api->Free = (PFND3DKMT_FREEGPUVIRTUALADDRESS)LoadD3DKMTProc("D3DKMTFreeGpuVirtualAddress");
    Api->CreateSync = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    Api->DestroySync = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    Api->SignalFromGpu = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU)LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromGpu");
    Api->WaitFromGpu = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU)LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromGpu");
    Api->SignalFromCpu = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU)LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromCpu");
    Api->Submit = (PFND3DKMT_SUBMITCOMMAND)LoadD3DKMTProc("D3DKMTSubmitCommand");
    return Api->EnumAdapters && Api->QueryAdapterInfo && Api->CreateContext && Api->DestroyContext &&
           Api->CreatePagingQueue && Api->DestroyPagingQueue && Api->Reserve && Api->Free &&
           Api->CreateSync && Api->DestroySync && Api->SignalFromGpu && Api->WaitFromGpu &&
           Api->SignalFromCpu && Api->Submit;
}

static NTSTATUS
CtxFlagsCreate(CTXFLAGS_API *Api, D3DKMT_HANDLE hDevice, UINT Flags, D3DKMT_HANDLE *hContext)
{
    D3DKMT_CREATECONTEXTVIRTUAL Create;
    NTSTATUS Status;

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.NodeOrdinal = 0;
    Create.EngineAffinity = 1;
    Create.Flags.Value = Flags;
    Status = Api->CreateContext(&Create);
    *hContext = NT_SUCCESS(Status) ? Create.hContext : 0;
    return Status;
}

static VOID
CtxFlagsDestroy(CTXFLAGS_API *Api, D3DKMT_HANDLE hContext)
{
    D3DKMT_DESTROYCONTEXT Destroy;

    if (hContext == 0)
        return;
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hContext = hContext;
    ok_succeeded(Api->DestroyContext(&Destroy), "DestroyContext 0x%lx failed\n", (ULONG)hContext);
}

static BOOL
CtxFlagsCreateFence(CTXFLAGS_API *Api, D3DKMT_HANDLE hDevice, CTXFLAGS_FENCE *Fence)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    NTSTATUS Status;

    memset(Fence, 0, sizeof(*Fence));
    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.MonitoredFence.EngineAffinity = 1;
    Status = Api->CreateSync(&Create);
    ok_succeeded(Status, "Create monitored fence failed 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return FALSE;
    Fence->Handle = Create.hSyncObject;
    Fence->Value = Create.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    ok(Fence->Value != NULL, "Monitored fence has no CPU value address\n");
    return Fence->Value != NULL;
}

static VOID
CtxFlagsDestroyFence(CTXFLAGS_API *Api, CTXFLAGS_FENCE *Fence)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;

    if (Fence->Handle == 0)
        return;
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hSyncObject = Fence->Handle;
    Api->DestroySync(&Destroy);
    Fence->Handle = 0;
}

static BOOL
CtxFlagsWaitValue(volatile const UINT64 *Value, UINT64 Target, DWORD TimeoutMs)
{
    DWORD Start = GetTickCount();

    while (*Value < Target)
    {
        if (GetTickCount() - Start > TimeoutMs)
            return FALSE;
        Sleep(1);
    }
    return TRUE;
}

static NTSTATUS
CtxFlagsSignalFromGpu(CTXFLAGS_API *Api, D3DKMT_HANDLE hContext, CTXFLAGS_FENCE *Fence, UINT64 Value)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU Signal;

    memset(&Signal, 0, sizeof(Signal));
    Signal.hContext = hContext;
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &Fence->Handle;
    Signal.MonitoredFenceValueArray = &Value;
    return Api->SignalFromGpu(&Signal);
}

static NTSTATUS
CtxFlagsWaitFromGpu(CTXFLAGS_API *Api, D3DKMT_HANDLE hContext, CTXFLAGS_FENCE *Fence, UINT64 Value)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait;

    memset(&Wait, 0, sizeof(Wait));
    Wait.hContext = hContext;
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &Fence->Handle;
    Wait.MonitoredFenceValueArray = &Value;
    return Api->WaitFromGpu(&Wait);
}

static NTSTATUS
CtxFlagsSignalFromCpu(CTXFLAGS_API *Api, D3DKMT_HANDLE hDevice, CTXFLAGS_FENCE *Fence, UINT64 Value)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal;

    memset(&Signal, 0, sizeof(Signal));
    Signal.hDevice = hDevice;
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &Fence->Handle;
    Signal.FenceValueArray = &Value;
    return Api->SignalFromCpu(&Signal);
}

static NTSTATUS
CtxFlagsSubmitNull(CTXFLAGS_API *Api, D3DKMT_HANDLE hContext, D3DGPU_VIRTUAL_ADDRESS CommandVa)
{
    D3DKMT_SUBMITCOMMAND Submit;

    memset(&Submit, 0, sizeof(Submit));
    Submit.BroadcastContextCount = 1;
    Submit.BroadcastContext[0] = hContext;
    Submit.Commands = CommandVa;
    Submit.CommandLength = sizeof(ULONG);
    Submit.Flags.NullRendering = 1;
    return Api->Submit(&Submit);
}

static VOID
CtxFlagsSyncContext(CTXFLAGS_API *Api, D3DKMT_HANDLE hDevice, UINT Flags, const char *Name,
                    D3DGPU_VIRTUAL_ADDRESS CommandVa, NTSTATUS ControlSubmit)
{
    CTXFLAGS_FENCE Gate, Done;
    D3DKMT_HANDLE hContext = 0;
    NTSTATUS Status;

    memset(&Gate, 0, sizeof(Gate));
    memset(&Done, 0, sizeof(Done));
    Status = CtxFlagsCreate(Api, hDevice, Flags, &hContext);
    ok_succeeded(Status, "%s context refused after a submission (0x%08lx)\n", Name, Status);
    if (!NT_SUCCESS(Status))
        return;
    if (!CtxFlagsCreateFence(Api, hDevice, &Gate) || !CtxFlagsCreateFence(Api, hDevice, &Done))
        goto Cleanup;

    Status = CtxFlagsSignalFromGpu(Api, hContext, &Done, 1);
    ok_succeeded(Status, "%s signal from GPU failed 0x%08lx\n", Name, Status);
    if (NT_SUCCESS(Status))
        ok(CtxFlagsWaitValue(Done.Value, 1, CTXFLAGS_FENCE_TIMEOUT_MS),
           "%s signal never completed (value %I64u)\n", Name, *Done.Value);

    Status = CtxFlagsWaitFromGpu(Api, hContext, &Gate, 1);
    ok_succeeded(Status, "%s wait from GPU failed 0x%08lx\n", Name, Status);
    if (NT_SUCCESS(Status))
    {
        Status = CtxFlagsSignalFromGpu(Api, hContext, &Done, 2);
        ok_succeeded(Status, "%s ordered signal failed 0x%08lx\n", Name, Status);
        Sleep(CTXFLAGS_HOLD_MS);
        ok(*Done.Value < 2, "%s signal behind an unsatisfied GPU wait completed early (value %I64u)\n", Name, *Done.Value);
        Status = CtxFlagsSignalFromCpu(Api, hDevice, &Gate, 1);
        ok_succeeded(Status, "CPU signal of the gate failed 0x%08lx\n", Status);
        ok(CtxFlagsWaitValue(Done.Value, 2, CTXFLAGS_FENCE_TIMEOUT_MS),
           "%s signal behind a satisfied GPU wait never completed (value %I64u)\n", Name, *Done.Value);
    }

    Status = CtxFlagsSubmitNull(Api, hContext, CommandVa);
    trace("CTXFLAGS %s submit status=0x%08lx control=0x%08lx\n", Name, Status, ControlSubmit);
    if (Flags & 0x00000008)
        ok(Status == STATUS_INVALID_PARAMETER, "%s context accepted a submission (0x%08lx)\n", Name, Status);
    else
        ok(Status == ControlSubmit, "%s submission returned 0x%08lx, control 0x%08lx\n", Name, Status, ControlSubmit);

Cleanup:
    CtxFlagsDestroyFence(Api, &Done);
    CtxFlagsDestroyFence(Api, &Gate);
    CtxFlagsDestroy(Api, hContext);
}

static VOID
CtxFlagsAdapter(CTXFLAGS_API *Api, D3DKMT_HANDLE hAdapter, LUID Luid, BOOL Software)
{
    D3DKMT_CREATEPAGINGQUEUE CreateQueue;
    D3DDDI_DESTROYPAGINGQUEUE DestroyQueue;
    D3DDDI_RESERVEGPUVIRTUALADDRESS Reserve;
    D3DKMT_FREEGPUVIRTUALADDRESS Free;
    D3DKMT_HANDLE hDevice, hContext;
    NTSTATUS Status, Control;
    ULONG Index;

    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        skip("Adapter %08lx:%08lx: CreateDevice refused\n", (ULONG)Luid.HighPart, Luid.LowPart);
        return;
    }

    memset(&CreateQueue, 0, sizeof(CreateQueue));
    CreateQueue.hDevice = hDevice;
    CreateQueue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    Status = Api->CreatePagingQueue(&CreateQueue);
    if (!NT_SUCCESS(Status) || CreateQueue.hPagingQueue == 0)
    {
        skip("Adapter %08lx:%08lx: no GPU virtual addressing (0x%08lx)\n",
             (ULONG)Luid.HighPart, Luid.LowPart, Status);
        DestroyTestDevice(hDevice);
        return;
    }

    memset(&Reserve, 0, sizeof(Reserve));
    Reserve.hAdapter = hAdapter;
    Reserve.Size = 0x10000;
    Status = Api->Reserve(&Reserve);
    trace("CTXFLAGS reserve status=0x%08lx va=0x%I64x\n", Status, Reserve.VirtualAddress);

    Status = CtxFlagsCreate(Api, hDevice, 0, &hContext);
    trace("CTXFLAGS adapter=%08lx:%08lx software=%u flags=0x00000000 status=0x%08lx\n",
          (ULONG)Luid.HighPart, Luid.LowPart, Software, Status);
    if (!NT_SUCCESS(Status))
    {
        skip("Adapter %08lx:%08lx: CreateContextVirtual refused (0x%08lx)\n",
             (ULONG)Luid.HighPart, Luid.LowPart, Status);
        goto Cleanup;
    }
    CtxFlagsDestroy(Api, hContext);

    for (Index = 0; Index < RTL_NUMBER_OF(CtxFlagBits); ++Index)
    {
        Status = CtxFlagsCreate(Api, hDevice, CtxFlagBits[Index].Value, &hContext);
        trace("CTXFLAGS adapter=%08lx:%08lx software=%u flags=0x%08x %s status=0x%08lx\n",
              (ULONG)Luid.HighPart, Luid.LowPart, Software, CtxFlagBits[Index].Value,
              CtxFlagBits[Index].Name, Status);
        if (CtxFlagBits[Index].Expect == CtxFlagsAccepted)
            ok(NT_SUCCESS(Status), "%s context refused (0x%08lx)\n", CtxFlagBits[Index].Name, Status);
        else if (CtxFlagBits[Index].Expect == CtxFlagsInvalid)
            ok(Status == STATUS_INVALID_PARAMETER, "Reserved context flag 0x%08x returned 0x%08lx\n",
               CtxFlagBits[Index].Value, Status);
        else
            ok(NT_SUCCESS(Status) || Status == STATUS_UNSUCCESSFUL, "%s context returned 0x%08lx\n",
               CtxFlagBits[Index].Name, Status);
        CtxFlagsDestroy(Api, hContext);
    }

    Control = STATUS_UNSUCCESSFUL;
    Status = CtxFlagsCreate(Api, hDevice, 0x00000001, &hContext);
    ok_succeeded(Status, "NullRendering control context refused 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Control = CtxFlagsSubmitNull(Api, hContext, 0);
        ok(Control == STATUS_INVALID_USER_BUFFER, "Submission without commands returned 0x%08lx\n", Control);
        CtxFlagsDestroy(Api, hContext);
    }
    CtxFlagsSyncContext(Api, hDevice, 0x00000008, "SynchronizationOnly", 0, Control);
    CtxFlagsSyncContext(Api, hDevice, 0x00000020, "NoKmdAccess", 0, Control);

Cleanup:
    if (Reserve.VirtualAddress != 0)
    {
        memset(&Free, 0, sizeof(Free));
        Free.hAdapter = hAdapter;
        Free.BaseAddress = Reserve.VirtualAddress;
        Free.Size = Reserve.Size;
        Api->Free(&Free);
    }
    memset(&DestroyQueue, 0, sizeof(DestroyQueue));
    DestroyQueue.hPagingQueue = CreateQueue.hPagingQueue;
    Api->DestroyPagingQueue(&DestroyQueue);
    DestroyTestDevice(hDevice);
}

START_TEST(ctxflags)
{
    CTXFLAGS_API Api;
    D3DKMT_ENUMADAPTERS Enum;
    ULONG Index;
    ULONG Tested = 0;

    memset(&Api, 0, sizeof(Api));
    if (!CtxFlagsLoad(&Api))
    {
        skip("WDDM 2.0 virtual context entry points are not exported\n");
        return;
    }

    memset(&Enum, 0, sizeof(Enum));
    if (!NT_SUCCESS(Api.EnumAdapters(&Enum)))
    {
        skip("EnumAdapters failed\n");
        return;
    }

    for (Index = 0; Index < Enum.NumAdapters && Index < CTXFLAGS_MAX_ADAPTERS; ++Index)
    {
        D3DKMT_QUERYADAPTERINFO Query;
        D3DKMT_ADAPTERTYPE Type;

        memset(&Type, 0, sizeof(Type));
        memset(&Query, 0, sizeof(Query));
        Query.hAdapter = Enum.Adapters[Index].hAdapter;
        Query.Type = KMTQAITYPE_ADAPTERTYPE;
        Query.pPrivateDriverData = &Type;
        Query.PrivateDriverDataSize = sizeof(Type);
        if (!NT_SUCCESS(Api.QueryAdapterInfo(&Query)) || !Type.RenderSupported)
            continue;
        CtxFlagsAdapter(&Api, Enum.Adapters[Index].hAdapter, Enum.Adapters[Index].AdapterLuid, Type.SoftwareDevice);
        ++Tested;
    }

    for (Index = 0; Index < Enum.NumAdapters; ++Index)
        CloseAdapter(Enum.Adapters[Index].hAdapter);

    if (Tested == 0)
        skip("No render-capable adapter\n");
}
