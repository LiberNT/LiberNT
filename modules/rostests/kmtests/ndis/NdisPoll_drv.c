/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test miniport that records how NDIS drives a Poll object
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define NDIS_MINIPORT_DRIVER 1
#define NDIS685_MINIPORT 1
#define NDIS685 1
#include <ntddk.h>
#include <ndis.h>
#include "NdisPoll.h"

typedef enum _NDISPOLL_MODE
{
    NdisPollModeIdle,
    NdisPollModeRemaining,
    NdisPollModeRequeue,
} NDISPOLL_MODE;

static NDIS_HANDLE NdisPollDriverHandle;
static NDIS_HANDLE NdisPollAdapterHandle;
static ULONG NdisPollAdapterContext;
static UNICODE_STRING NdisPollParameters;
static PKTHREAD NdisPollWorker;
static NDIS_POLL_HANDLE NdisPollHandle;
static volatile LONG NdisPollMode;
static volatile LONG NdisPollRequeued;
static volatile LONG NdisPollEventCount;
static WCHAR NdisPollEvents[NDISPOLL_MAX_EVENTS + 1];
static volatile LONG NdisPollFirstData;
static NDIS_POLL_DATA NdisPollData;
static volatile LONG NdisPollFirstNotification;
static NDIS_OBJECT_HEADER NdisPollNotificationHeader;
static volatile LONG NdisPollIrql = -1;
static volatile LONG NdisPollNotifyIrql = -1;
static volatile LONG NdisPollProcessor = -1;

static
VOID
NdisPollWrite(
    _In_ PCWSTR Name,
    _In_ ULONG Type,
    _In_ PVOID Data,
    _In_ ULONG Size)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ValueName;
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, &NdisPollParameters, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateKey(&Key, KEY_SET_VALUE, &Attributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL)))
        return;
    RtlInitUnicodeString(&ValueName, Name);
    ZwSetValueKey(Key, &ValueName, 0, Type, Data, Size);
    ZwClose(Key);
}

static
VOID
NdisPollWriteValue(
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    NdisPollWrite(Name, REG_DWORD, &Value, sizeof(Value));
}

static
VOID
NdisPollRecord(
    _In_ WCHAR Event)
{
    LONG Index = InterlockedIncrement(&NdisPollEventCount) - 1;

    if (Index < NDISPOLL_MAX_EVENTS)
        NdisPollEvents[Index] = Event;
}

static
VOID
NdisPollRaise(
    _Inout_ volatile LONG *Maximum,
    _In_ LONG Value)
{
    LONG Current = *Maximum;

    while (Value > Current)
    {
        LONG Previous = InterlockedCompareExchange(Maximum, Value, Current);
        if (Previous == Current)
            break;
        Current = Previous;
    }
}

static
VOID
NdisPollDelay(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
VOID
NdisPollBegin(
    _In_ NDISPOLL_MODE Mode)
{
    RtlZeroMemory(NdisPollEvents, sizeof(NdisPollEvents));
    InterlockedExchange(&NdisPollEventCount, 0);
    InterlockedExchange(&NdisPollRequeued, 0);
    InterlockedExchange(&NdisPollMode, Mode);
}

static
VOID
NdisPollEnd(
    _In_ PCWSTR Name,
    _In_ ULONG Delay)
{
    ULONG Count;

    NdisPollDelay(Delay);
    Count = min((ULONG)NdisPollEventCount, NDISPOLL_MAX_EVENTS);
    NdisPollEvents[Count] = UNICODE_NULL;
    NdisPollWrite(Name, REG_SZ, NdisPollEvents, (Count + 1) * sizeof(WCHAR));
}

static
VOID
NdisPollPhase(
    _In_ PCWSTR Name,
    _In_ NDISPOLL_MODE Mode)
{
    NdisPollBegin(Mode);
    NdisRequestPoll(NdisPollHandle, NULL);
    NdisPollEnd(Name, 300);
}

static
VOID
NTAPI
NdisPollHandler(
    _In_ PVOID Context,
    _Inout_ NDIS_POLL_DATA *PollData)
{
    UNREFERENCED_PARAMETER(Context);

    NdisPollRecord(L'P');
    NdisPollRaise(&NdisPollIrql, KeGetCurrentIrql());
    InterlockedExchange(&NdisPollProcessor, (LONG)KeGetCurrentProcessorNumberEx(NULL));
    if (InterlockedCompareExchange(&NdisPollFirstData, 1, 0) == 0)
        RtlCopyMemory(&NdisPollData, PollData, sizeof(NdisPollData));

    switch (NdisPollMode)
    {
        case NdisPollModeRemaining:
            PollData->Receive.NumberOfRemainingNbls = NDIS_ANY_NUMBER_OF_NBLS;
            PollData->Transmit.NumberOfRemainingNbls = NDIS_ANY_NUMBER_OF_NBLS;
            break;

        case NdisPollModeRequeue:
            if (InterlockedCompareExchange(&NdisPollRequeued, 1, 0) == 0)
                NdisRequestPoll(NdisPollHandle, NULL);
            break;

        default:
            break;
    }
}

static
VOID
NTAPI
NdisPollSetNotification(
    _In_ PVOID Context,
    _Inout_ NDIS_POLL_NOTIFICATION *Notification)
{
    UNREFERENCED_PARAMETER(Context);

    NdisPollRecord(Notification->Enabled ? L'E' : L'D');
    NdisPollRaise(&NdisPollNotifyIrql, KeGetCurrentIrql());
    if (InterlockedCompareExchange(&NdisPollFirstNotification, 1, 0) == 0)
        NdisPollNotificationHeader = Notification->Header;
}

static
BOOLEAN
NdisPollReceiveInputClear(
    _In_ const NDIS_POLL_RECEIVE_DATA *Receive)
{
    return Receive->Reserved1[0] == 0 && Receive->Reserved1[1] == 0 && Receive->Reserved1[2] == 0 &&
           Receive->IndicatedNblChain == NULL && Receive->NumberOfIndicatedNbls == 0 &&
           Receive->NumberOfRemainingNbls == 0 && Receive->Flags == 0 && Receive->Reserved2 == 0 &&
           Receive->Reserved3[0] == NULL && Receive->Reserved3[1] == NULL &&
           Receive->Reserved3[2] == NULL && Receive->Reserved3[3] == NULL;
}

static
BOOLEAN
NdisPollTransmitInputClear(
    _In_ const NDIS_POLL_TRANSMIT_DATA *Transmit)
{
    return Transmit->Reserved1[0] == 0 && Transmit->Reserved1[1] == 0 && Transmit->Reserved1[2] == 0 &&
           Transmit->CompletedNblChain == NULL && Transmit->NumberOfCompletedNbls == 0 &&
           Transmit->NumberOfRemainingNbls == 0 && Transmit->SendCompleteFlags == 0 &&
           Transmit->Reserved2 == 0 && Transmit->Reserved3[0] == NULL && Transmit->Reserved3[1] == NULL &&
           Transmit->Reserved3[2] == NULL && Transmit->Reserved3[3] == NULL;
}

static
VOID
NTAPI
NdisPollWorkerRoutine(
    _In_ PVOID StartContext)
{
    NDIS_POLL_CHARACTERISTICS Characteristics;
    PROCESSOR_NUMBER Processor;
    NDIS_STATUS Status;
    ULONG Processors, Target;

    UNREFERENCED_PARAMETER(StartContext);

    NdisPollDelay(500);

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Characteristics.Header.Revision = NDIS_POLL_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_NDIS_POLL_CHARACTERISTICS_REVISION_1;
    Characteristics.SetPollNotificationHandler = NdisPollSetNotification;
    Characteristics.PollHandler = NdisPollHandler;
    NdisPollBegin(NdisPollModeIdle);
    Status = NdisRegisterPoll(NdisPollAdapterHandle, &NdisPollAdapterContext, &Characteristics, &NdisPollHandle);
    NdisPollWriteValue(L"Register", Status);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisPollWriteValue(L"Done", 1);
        PsTerminateSystemThread(STATUS_SUCCESS);
    }
    NdisPollEnd(L"Registered", 300);

    NdisPollPhase(L"Requested", NdisPollModeIdle);
    NdisPollPhase(L"Rearmed", NdisPollModeIdle);
    NdisPollPhase(L"Remaining", NdisPollModeRemaining);
    NdisPollPhase(L"Requeue", NdisPollModeRequeue);

    NdisPollWriteValue(L"DataHeader", NdisPollData.Header.Type | (NdisPollData.Header.Revision << 8) |
                                      ((ULONG)NdisPollData.Header.Size << 16));
    NdisPollWriteValue(L"MaxNblsToIndicate", NdisPollData.Receive.MaxNblsToIndicate);
    NdisPollWriteValue(L"MaxNblsToComplete", NdisPollData.Transmit.MaxNblsToComplete);
    NdisPollWriteValue(L"ReceiveInputClear", NdisPollReceiveInputClear(&NdisPollData.Receive));
    NdisPollWriteValue(L"TransmitInputClear", NdisPollTransmitInputClear(&NdisPollData.Transmit));
    NdisPollWriteValue(L"NotificationHeader", NdisPollNotificationHeader.Type |
                                              (NdisPollNotificationHeader.Revision << 8) |
                                              ((ULONG)NdisPollNotificationHeader.Size << 16));
    NdisPollWriteValue(L"PollIrql", (ULONG)NdisPollIrql);
    NdisPollWriteValue(L"NotificationIrql", (ULONG)NdisPollNotifyIrql);

    Processors = KeQueryActiveProcessorCountEx(0);
    NdisPollWriteValue(L"Processors", Processors);
    Target = Processors - 1;
    if (NT_SUCCESS(KeGetProcessorNumberFromIndex(Target, &Processor)))
    {
        NdisSetPollAffinity(NdisPollHandle, &Processor);
        NdisPollPhase(L"AffinityFirst", NdisPollModeIdle);
        NdisPollPhase(L"AffinitySecond", NdisPollModeIdle);
        NdisPollWriteValue(L"AffinityTarget", Target);
        NdisPollWriteValue(L"AffinityProcessor", (ULONG)NdisPollProcessor);
    }

    NdisPollBegin(NdisPollModeIdle);
    NdisDeregisterPoll(NdisPollHandle);
    NdisPollHandle = NULL;
    NdisPollEnd(L"Deregister", 0);
    NdisPollWriteValue(L"Done", 1);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NDIS_STATUS
NTAPI
NdisPollInitialize(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS InitParameters)
{
    static const UCHAR MacAddress[6] = { 0x02, 0x4B, 0x4D, 0x54, 0x50, 0x4C };
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;
    OBJECT_ATTRIBUTES Attributes;
    NDIS_STATUS Status;
    HANDLE Thread;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(InitParameters);

    NdisPollAdapterHandle = MiniportAdapterHandle;
    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = &NdisPollAdapterContext;
    Registration.InterfaceType = NdisInterfaceInternal;
    Status = NdisMSetMiniportAttributes(MiniportAdapterHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
    NdisPollWriteValue(L"Registration", Status);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    RtlZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.MediaType = NdisMedium802_3;
    General.PhysicalMediumType = NdisPhysicalMediumUnspecified;
    General.MtuSize = 1500;
    General.MaxXmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.MaxRcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.MediaConnectState = MediaConnectStateDisconnected;
    General.MediaDuplexState = MediaDuplexStateUnknown;
    General.LookaheadSize = 1500;
    General.MacOptions = NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED;
    General.MaxMulticastListSize = 32;
    General.MacAddressLength = sizeof(MacAddress);
    RtlCopyMemory(General.PermanentMacAddress, MacAddress, sizeof(MacAddress));
    RtlCopyMemory(General.CurrentMacAddress, MacAddress, sizeof(MacAddress));
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_ETHERNET_CSMACD;
    Status = NdisMSetMiniportAttributes(MiniportAdapterHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
    NdisPollWriteValue(L"General", Status);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, NdisPollWorkerRoutine, NULL);
    if (!NT_SUCCESS(Status))
        return NDIS_STATUS_RESOURCES;
    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&NdisPollWorker, NULL);
    ZwClose(Thread);
    if (!NT_SUCCESS(Status))
        NdisPollWorker = NULL;
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
NdisPollHalt(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(HaltAction);

    if (NdisPollWorker)
    {
        KeWaitForSingleObject(NdisPollWorker, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(NdisPollWorker);
        NdisPollWorker = NULL;
    }
}

static
VOID
NTAPI
NdisPollUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    NdisMDeregisterMiniportDriver(NdisPollDriverHandle);
    ExFreePoolWithTag(NdisPollParameters.Buffer, 'lPdN');
}

static
NDIS_STATUS
NTAPI
NdisPollPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
NdisPollRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RestartParameters);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
NdisPollOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(OidRequest);
    return NDIS_STATUS_NOT_SUPPORTED;
}

static
VOID
NTAPI
NdisPollCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static
VOID
NTAPI
NdisPollSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNET_BUFFER_LIST List;

    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PortNumber);
    for (List = NetBufferLists; List; List = NET_BUFFER_LIST_NEXT_NBL(List))
        NET_BUFFER_LIST_STATUS(List) = NDIS_STATUS_MEDIA_DISCONNECTED;
    NdisMSendNetBufferListsComplete(NdisPollAdapterHandle, NetBufferLists,
                                    (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL) ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}

static
VOID
NTAPI
NdisPollReturn(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

static
VOID
NTAPI
NdisPollCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static
VOID
NTAPI
NdisPollPnpEvent(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static
VOID
NTAPI
NdisPollShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;
    static const WCHAR Suffix[] = L"\\Parameters";
    NDIS_STATUS Status;

    NdisPollParameters.MaximumLength = RegistryPath->Length + sizeof(Suffix);
    NdisPollParameters.Buffer = ExAllocatePoolWithTag(NonPagedPool, NdisPollParameters.MaximumLength, 'lPdN');
    if (!NdisPollParameters.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    NdisPollParameters.Length = 0;
    RtlAppendUnicodeStringToString(&NdisPollParameters, RegistryPath);
    RtlAppendUnicodeToString(&NdisPollParameters, Suffix);

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 85;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.InitializeHandlerEx = NdisPollInitialize;
    Characteristics.HaltHandlerEx = NdisPollHalt;
    Characteristics.UnloadHandler = NdisPollUnload;
    Characteristics.PauseHandler = NdisPollPause;
    Characteristics.RestartHandler = NdisPollRestart;
    Characteristics.OidRequestHandler = NdisPollOidRequest;
    Characteristics.CancelOidRequestHandler = NdisPollCancelOidRequest;
    Characteristics.SendNetBufferListsHandler = NdisPollSend;
    Characteristics.ReturnNetBufferListsHandler = NdisPollReturn;
    Characteristics.CancelSendHandler = NdisPollCancelSend;
    Characteristics.DevicePnPEventNotifyHandler = NdisPollPnpEvent;
    Characteristics.ShutdownHandlerEx = NdisPollShutdown;
    Status = NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &NdisPollDriverHandle);
    NdisPollWriteValue(L"DriverRegister", Status);
    if (Status != NDIS_STATUS_SUCCESS)
        ExFreePoolWithTag(NdisPollParameters.Buffer, 'lPdN');
    return Status;
}
