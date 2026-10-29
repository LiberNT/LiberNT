/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WFP listen authorization of a WSK socket
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include <initguid.h>
#include <fwpsk.h>
#include <fwpmk.h>
#include <wsk.h>

#define NDEBUG
#include <debug.h>

#include "wfp_listen.h"

DEFINE_GUID(TEST_SUBLAYER, 0x3e8d2a61, 0x5c7b, 0x4f19, 0xa2, 0x46, 0x0b, 0x9c, 0x7d, 0x13, 0xe5, 0x21);
DEFINE_GUID(TEST_LISTEN_CALLOUT, 0x3e8d2a61, 0x5c7b, 0x4f19, 0xa2, 0x46, 0x0b, 0x9c, 0x7d, 0x13, 0xe5, 0x22);

static KMT_MESSAGE_HANDLER TestMessageHandler;

static volatile LONG ListenClassifyCount;
static volatile LONG AcceptEventCount;

static
VOID
NTAPI
ListenClassify(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _Inout_opt_ PVOID LayerData,
    _In_opt_ const void *ClassifyContext,
    _In_ const FWPS_FILTER3 *Filter,
    _In_ UINT64 FlowContext,
    _Inout_ FWPS_CLASSIFY_OUT0 *ClassifyOut)
{
    UNREFERENCED_PARAMETER(Values);
    UNREFERENCED_PARAMETER(Metadata);
    UNREFERENCED_PARAMETER(LayerData);
    UNREFERENCED_PARAMETER(ClassifyContext);
    UNREFERENCED_PARAMETER(Filter);
    UNREFERENCED_PARAMETER(FlowContext);

    InterlockedIncrement(&ListenClassifyCount);
    ClassifyOut->actionType = FWP_ACTION_BLOCK;
    ClassifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
}

static
NTSTATUS
NTAPI
CalloutNotify(
    _In_ FWPS_CALLOUT_NOTIFY_TYPE NotifyType,
    _In_ const GUID *FilterKey,
    _Inout_ FWPS_FILTER3 *Filter)
{
    UNREFERENCED_PARAMETER(NotifyType);
    UNREFERENCED_PARAMETER(FilterKey);
    UNREFERENCED_PARAMETER(Filter);

    return STATUS_SUCCESS;
}

static
NTSTATUS
WSKAPI
AcceptEvent(
    _In_opt_ PVOID SocketContext,
    _In_ ULONG Flags,
    _In_ PSOCKADDR LocalAddress,
    _In_ PSOCKADDR RemoteAddress,
    _In_opt_ PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ PVOID *AcceptSocketContext,
    _Outptr_result_maybenull_ const WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch)
{
    UNREFERENCED_PARAMETER(SocketContext);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(LocalAddress);
    UNREFERENCED_PARAMETER(RemoteAddress);
    UNREFERENCED_PARAMETER(AcceptSocket);

    InterlockedIncrement(&AcceptEventCount);
    *AcceptSocketContext = NULL;
    *AcceptSocketDispatch = NULL;
    return STATUS_REQUEST_NOT_ACCEPTED;
}

static const WSK_CLIENT_LISTEN_DISPATCH ListenDispatch = { AcceptEvent, NULL, NULL };
static const WSK_CLIENT_DISPATCH ClientDispatch = { MAKE_WSK_VERSION(1, 0), 0, NULL };

static
NTSTATUS
NTAPI
IrpComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
PIRP
NewIrp(
    _Out_ PKEVENT Event)
{
    PIRP Irp = IoAllocateIrp(1, FALSE);

    KeInitializeEvent(Event, NotificationEvent, FALSE);
    if (Irp != NULL)
    {
        IoSetCompletionRoutine(Irp, IrpComplete, Event, TRUE, TRUE, TRUE);
    }
    return Irp;
}

static
NTSTATUS
WaitIrp(
    _In_ PIRP Irp,
    _In_ PKEVENT Event,
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
    }
    Status = Irp->IoStatus.Status;
    return Status;
}

static
NTSTATUS
AddListenBlock(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ HANDLE Engine,
    _Out_ PUINT32 CalloutId)
{
    FWPM_FILTER_CONDITION0 Condition;
    FWPM_SUBLAYER0 SubLayer;
    FWPM_CALLOUT0 Management;
    FWPS_CALLOUT3 Callout;
    FWPM_FILTER0 Filter;
    NTSTATUS Status;

    RtlZeroMemory(&Callout, sizeof(Callout));
    Callout.calloutKey = TEST_LISTEN_CALLOUT;
    Callout.classifyFn = ListenClassify;
    Callout.notifyFn = CalloutNotify;
    Status = FwpsCalloutRegister3(DeviceObject, &Callout, CalloutId);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&SubLayer, sizeof(SubLayer));
    SubLayer.subLayerKey = TEST_SUBLAYER;
    SubLayer.displayData.name = L"LiberNT WFP listen test sublayer";
    SubLayer.weight = 0x100;
    Status = FwpmSubLayerAdd0(Engine, &SubLayer, NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&Management, sizeof(Management));
    Management.calloutKey = TEST_LISTEN_CALLOUT;
    Management.displayData.name = L"LiberNT WFP listen test callout";
    Management.applicableLayer = FWPM_LAYER_ALE_AUTH_LISTEN_V4;
    Status = FwpmCalloutAdd0(Engine, &Management, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&Condition, sizeof(Condition));
    Condition.fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
    Condition.matchType = FWP_MATCH_EQUAL;
    Condition.conditionValue.type = FWP_UINT16;
    Condition.conditionValue.uint16 = WFPLISTEN_PORT;

    RtlZeroMemory(&Filter, sizeof(Filter));
    Filter.layerKey = FWPM_LAYER_ALE_AUTH_LISTEN_V4;
    Filter.subLayerKey = TEST_SUBLAYER;
    Filter.displayData.name = L"LiberNT WFP listen test filter";
    Filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
    Filter.action.calloutKey = TEST_LISTEN_CALLOUT;
    Filter.weight.type = FWP_EMPTY;
    Filter.numFilterConditions = 1;
    Filter.filterCondition = &Condition;
    return FwpmFilterAdd0(Engine, &Filter, NULL, NULL);
}

static
VOID
TestRun(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    const WSK_PROVIDER_LISTEN_DISPATCH *Dispatch;
    WSK_EVENT_CALLBACK_CONTROL Control;
    WSK_CLIENT_NPI ClientNpi;
    WSK_PROVIDER_NPI ProviderNpi;
    WSK_REGISTRATION Registration;
    FWPM_SESSION0 Session;
    PWSK_SOCKET Socket = NULL;
    SOCKADDR_IN Address;
    LARGE_INTEGER Delay;
    HANDLE Engine;
    UINT32 CalloutId = 0;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    RtlZeroMemory(&Session, sizeof(Session));
    Session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    Status = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &Session, &Engine);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }
    Status = AddListenBlock(DeviceObject, Engine, &CalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ClientNpi.ClientContext = NULL;
    ClientNpi.Dispatch = &ClientDispatch;
    Status = WskRegister(&ClientNpi, &Registration);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        goto engine;
    }
    Status = WskCaptureProviderNPI(&Registration, WSK_INFINITE_WAIT, &ProviderNpi);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        goto deregister;
    }

    Irp = NewIrp(&Event);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (Irp == NULL)
    {
        goto release;
    }
    Status = ProviderNpi.Dispatch->WskSocket(ProviderNpi.Client, AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                             WSK_FLAG_LISTEN_SOCKET, NULL, &ListenDispatch,
                                             NULL, NULL, NULL, Irp);
    Status = WaitIrp(Irp, &Event, Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Socket = (PWSK_SOCKET)Irp->IoStatus.Information;
    }
    IoFreeIrp(Irp);
    if (Socket == NULL)
    {
        goto release;
    }
    Dispatch = Socket->Dispatch;

    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = RtlUshortByteSwap(WFPLISTEN_PORT);
    Address.sin_addr.s_addr = RtlUlongByteSwap(0x7f000001);
    Irp = NewIrp(&Event);
    if (Irp != NULL)
    {
        Status = Dispatch->WskBind(Socket, (PSOCKADDR)&Address, 0, Irp);
        Status = WaitIrp(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_SUCCESS);
        IoFreeIrp(Irp);
    }

    Control.NpiId = (PNPIID)&NPI_WSK_INTERFACE_ID;
    Control.EventMask = WSK_EVENT_ACCEPT;
    Irp = NewIrp(&Event);
    if (Irp != NULL)
    {
        Status = Dispatch->WskControlSocket(Socket, WskSetOption, SO_WSK_EVENT_CALLBACK, SOL_SOCKET,
                                            sizeof(Control), &Control, 0, NULL, NULL, Irp);
        Status = WaitIrp(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_SUCCESS);
        IoFreeIrp(Irp);
    }

    Delay.QuadPart = -10 * 1000 * 1000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok_eq_long(ListenClassifyCount, 1L);
    ok_eq_long(AcceptEventCount, 0L);

    Irp = NewIrp(&Event);
    if (Irp != NULL)
    {
        Status = Dispatch->WskCloseSocket(Socket, Irp);
        Status = WaitIrp(Irp, &Event, Status);
        ok_eq_hex(Status, STATUS_SUCCESS);
        IoFreeIrp(Irp);
    }

release:
    WskReleaseProviderNPI(&Registration);
deregister:
    WskDeregister(&Registration);
engine:
    Status = FwpmEngineClose0(Engine);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (CalloutId != 0)
    {
        Status = FwpsCalloutUnregisterById0(CalloutId);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
}

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    *DeviceName = L"WfpListen";
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    if (ControlCode != IOCTL_WFPLISTEN_RUN)
    {
        return STATUS_NOT_SUPPORTED;
    }

    TestRun(DeviceObject);
    return STATUS_SUCCESS;
}
