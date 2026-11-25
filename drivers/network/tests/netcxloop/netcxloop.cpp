/*
 * PROJECT:     LiberNT network tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx client that answers ARP and ICMP echo as a simulated link peer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193.001@gmail.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <netadaptercx.h>
#include <net/virtualaddress.h>
#ifdef NETCXLOOP_WIFI
#include <wificx.h>
#endif

#define LOOP_MTU 1500
#define LOOP_MAX_FRAME (LOOP_MTU + 14)
#define LOOP_PEER_FRAMES 64
#define LOOP_LINK_SPEED 1000000000ULL

#ifdef NETCXLOOP_WIFI
static const UCHAR LoopAdapterAddress[6] = { 0x02, 0x00, 0x4C, 0x4F, 0x4F, 0x57 };
#else
static const UCHAR LoopAdapterAddress[6] = { 0x02, 0x00, 0x4C, 0x4F, 0x4F, 0x50 };
#endif
static const UCHAR LoopPeerAddress[6] = { 0x02, 0x00, 0x4C, 0x4F, 0x4F, 0x51 };
#ifndef NETCXLOOP_WIFI
static const UCHAR LoopLightweightAddress[6] = { 0x02, 0x00, 0x4C, 0x4F, 0x4F, 0x52 };
static const GUID LoopLightweightInterface = { 0x4c4f4f50, 0x4c57, 0x4d00, { 0x80, 0x00, 0x4c, 0x4f, 0x4f, 0x50, 0x4c, 0x57 } };
static NETADAPTER LoopLightweightAdapter;
#endif

typedef struct _LOOP_FRAME
{
    USHORT Length;
    UCHAR Data[LOOP_MAX_FRAME];
} LOOP_FRAME;

typedef struct _LOOP_ADAPTER
{
    KSPIN_LOCK Lock;
    ULONG Head;
    ULONG Count;
    NETPACKETQUEUE RxQueue;
    volatile LONG RxNotify;
    volatile LONG64 TxFrames;
    volatile LONG64 RxFrames;
    volatile LONG64 Dropped;
    LOOP_FRAME Frames[LOOP_PEER_FRAMES];
} LOOP_ADAPTER;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LOOP_ADAPTER, LoopGetAdapter);

typedef struct _LOOP_QUEUE
{
    LOOP_ADAPTER *Adapter;
    NET_RING_COLLECTION const *Rings;
    NET_EXTENSION VirtualAddress;
    volatile LONG Cancelled;
    UCHAR Scratch[LOOP_MAX_FRAME];
} LOOP_QUEUE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LOOP_QUEUE, LoopGetQueue);

extern "C" DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD LoopDeviceAdd;
static EVT_WDF_DRIVER_UNLOAD LoopDriverUnload;
static EVT_NET_ADAPTER_CREATE_TXQUEUE LoopCreateTxQueue;
static EVT_NET_ADAPTER_CREATE_RXQUEUE LoopCreateRxQueue;
static EVT_NET_ADAPTER_SET_RECEIVE_FILTER LoopSetReceiveFilter;
static EVT_PACKET_QUEUE_ADVANCE LoopTxAdvance;
static EVT_PACKET_QUEUE_SET_NOTIFICATION_ENABLED LoopTxSetNotificationEnabled;
static EVT_PACKET_QUEUE_CANCEL LoopTxCancel;
static EVT_PACKET_QUEUE_ADVANCE LoopRxAdvance;
static EVT_PACKET_QUEUE_SET_NOTIFICATION_ENABLED LoopRxSetNotificationEnabled;
static EVT_PACKET_QUEUE_CANCEL LoopRxCancel;

static
USHORT
LoopChecksum(
    _In_reads_bytes_(Length) UCHAR const *Data,
    _In_ ULONG Length)
{
    ULONG Sum = 0;

    while (Length > 1)
    {
        Sum += ((ULONG)Data[0] << 8) | Data[1];
        Data += 2;
        Length -= 2;
    }
    if (Length)
        Sum += (ULONG)Data[0] << 8;
    while (Sum >> 16)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    return (USHORT)~Sum;
}

static
void
LoopStoreChecksum(
    _Out_writes_bytes_(2) UCHAR *Field,
    _In_ USHORT Checksum)
{
    Field[0] = (UCHAR)(Checksum >> 8);
    Field[1] = (UCHAR)Checksum;
}

static
BOOLEAN
LoopBuildArpReply(
    _In_reads_bytes_(Length) UCHAR const *Request,
    _In_ ULONG Length,
    _Out_ LOOP_FRAME *Reply)
{
    UCHAR const *Arp = Request + 14;

    if (Length < 14 + 28)
        return FALSE;
    if (Arp[0] != 0 || Arp[1] != 1 || Arp[2] != 0x08 || Arp[3] != 0x00 ||
        Arp[4] != 6 || Arp[5] != 4 || Arp[6] != 0 || Arp[7] != 1)
        return FALSE;
    if (RtlCompareMemory(Arp + 14, Arp + 24, 4) == 4)
        return FALSE;
    if (Arp[14] == 0 && Arp[15] == 0 && Arp[16] == 0 && Arp[17] == 0)
        return FALSE;

    RtlCopyMemory(Reply->Data, Arp + 8, 6);
    RtlCopyMemory(Reply->Data + 6, LoopPeerAddress, 6);
    Reply->Data[12] = 0x08;
    Reply->Data[13] = 0x06;
    RtlCopyMemory(Reply->Data + 14, Arp, 6);
    Reply->Data[20] = 0;
    Reply->Data[21] = 2;
    RtlCopyMemory(Reply->Data + 22, LoopPeerAddress, 6);
    RtlCopyMemory(Reply->Data + 28, Arp + 24, 4);
    RtlCopyMemory(Reply->Data + 32, Arp + 8, 6);
    RtlCopyMemory(Reply->Data + 38, Arp + 14, 4);
    RtlZeroMemory(Reply->Data + 42, 60 - 42);
    Reply->Length = 60;
    return TRUE;
}

static
BOOLEAN
LoopBuildEchoReply(
    _In_reads_bytes_(Length) UCHAR const *Request,
    _In_ ULONG Length,
    _Out_ LOOP_FRAME *Reply)
{
    UCHAR const *Ip = Request + 14;
    ULONG HeaderLength;
    ULONG TotalLength;
    UCHAR *ReplyIp;
    UCHAR *Icmp;

    if (Length < 14 + 20)
        return FALSE;
    HeaderLength = (Ip[0] & 0x0F) * 4;
    TotalLength = ((ULONG)Ip[2] << 8) | Ip[3];
    if ((Ip[0] >> 4) != 4 || HeaderLength < 20 || TotalLength < HeaderLength + 8 ||
        14 + TotalLength > Length || Ip[9] != 1)
        return FALSE;
    if ((Ip[6] & 0x3F) != 0 || Ip[7] != 0)
        return FALSE;
    if (Ip[HeaderLength] != 8 || Ip[HeaderLength + 1] != 0)
        return FALSE;

    RtlCopyMemory(Reply->Data, Request + 6, 6);
    RtlCopyMemory(Reply->Data + 6, LoopPeerAddress, 6);
    Reply->Data[12] = 0x08;
    Reply->Data[13] = 0x00;
    RtlCopyMemory(Reply->Data + 14, Ip, TotalLength);

    ReplyIp = Reply->Data + 14;
    RtlCopyMemory(ReplyIp + 12, Ip + 16, 4);
    RtlCopyMemory(ReplyIp + 16, Ip + 12, 4);
    ReplyIp[8] = 128;
    ReplyIp[10] = 0;
    ReplyIp[11] = 0;
    LoopStoreChecksum(ReplyIp + 10, LoopChecksum(ReplyIp, HeaderLength));

    Icmp = ReplyIp + HeaderLength;
    Icmp[0] = 0;
    Icmp[2] = 0;
    Icmp[3] = 0;
    LoopStoreChecksum(Icmp + 2, LoopChecksum(Icmp, TotalLength - HeaderLength));

    Reply->Length = (USHORT)(14 + TotalLength < 60 ? 60 : 14 + TotalLength);
    if (14 + TotalLength < 60)
        RtlZeroMemory(Reply->Data + 14 + TotalLength, 60 - (14 + TotalLength));
    return TRUE;
}

static
void
LoopPeerReceive(
    _In_ LOOP_ADAPTER *Adapter,
    _In_reads_bytes_(Length) UCHAR const *Frame,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    LOOP_FRAME *Slot;
    BOOLEAN Queued = FALSE;
    USHORT EtherType;

    if (Length < 14)
        return;
    EtherType = ((USHORT)Frame[12] << 8) | Frame[13];
    if (EtherType != 0x0806 && EtherType != 0x0800)
        return;

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    if (Adapter->Count < LOOP_PEER_FRAMES)
    {
        Slot = &Adapter->Frames[(Adapter->Head + Adapter->Count) % LOOP_PEER_FRAMES];
        if (EtherType == 0x0806)
            Queued = LoopBuildArpReply(Frame, Length, Slot);
        else
            Queued = LoopBuildEchoReply(Frame, Length, Slot);
        if (Queued)
            Adapter->Count++;
    }
    else
    {
        InterlockedIncrement64(&Adapter->Dropped);
    }
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (Queued && Adapter->RxQueue && InterlockedExchange(&Adapter->RxNotify, 0))
        NetRxQueueNotifyMoreReceivedPacketsAvailable(Adapter->RxQueue);
}

static
ULONG
LoopGatherPacket(
    _In_ LOOP_QUEUE *Queue,
    _In_ NET_PACKET const *Packet)
{
    NET_RING *FragmentRing = NetRingCollectionGetFragmentRing(Queue->Rings);
    UINT32 Index = Packet->FragmentIndex;
    ULONG Length = 0;

    for (UINT16 i = 0; i < Packet->FragmentCount; i++)
    {
        NET_FRAGMENT const *Fragment = NetRingGetFragmentAtIndex(FragmentRing, Index);
        UCHAR const *Data = (UCHAR const *)NetExtensionGetFragmentVirtualAddress(&Queue->VirtualAddress, Index)->VirtualAddress +
                            Fragment->Offset;

        if (Length + Fragment->ValidLength > sizeof(Queue->Scratch))
            return 0;
        RtlCopyMemory(Queue->Scratch + Length, Data, (SIZE_T)Fragment->ValidLength);
        Length += (ULONG)Fragment->ValidLength;
        Index = NetRingIncrementIndex(FragmentRing, Index);
    }
    return Length;
}

static
void
LoopTxAdvance(
    _In_ NETPACKETQUEUE PacketQueue)
{
    LOOP_QUEUE *Queue = LoopGetQueue(PacketQueue);
    NET_RING *PacketRing = NetRingCollectionGetPacketRing(Queue->Rings);
    NET_RING *FragmentRing = NetRingCollectionGetFragmentRing(Queue->Rings);
    BOOLEAN Cancelled = Queue->Cancelled != 0;
    UINT32 Index = PacketRing->BeginIndex;
    UINT32 FragmentEnd = FragmentRing->BeginIndex;
    BOOLEAN Fragments = FALSE;

    while (Index != PacketRing->EndIndex)
    {
        NET_PACKET const *Packet = NetRingGetPacketAtIndex(PacketRing, Index);

        if (!Packet->Ignore && !Cancelled)
        {
            ULONG Length = LoopGatherPacket(Queue, Packet);

            if (Length)
            {
                InterlockedIncrement64(&Queue->Adapter->TxFrames);
                LoopPeerReceive(Queue->Adapter, Queue->Scratch, Length);
            }
        }
        if (Packet->FragmentCount)
        {
            FragmentEnd = NetRingAdvanceIndex(FragmentRing, Packet->FragmentIndex, Packet->FragmentCount);
            Fragments = TRUE;
        }
        Index = NetRingIncrementIndex(PacketRing, Index);
    }

    PacketRing->BeginIndex = Index;
    PacketRing->NextIndex = Index;
    if (Fragments)
    {
        FragmentRing->BeginIndex = FragmentEnd;
        FragmentRing->NextIndex = FragmentEnd;
    }
}

static
void
LoopTxSetNotificationEnabled(
    _In_ NETPACKETQUEUE PacketQueue,
    _In_ BOOLEAN NotificationEnabled)
{
    UNREFERENCED_PARAMETER(PacketQueue);
    UNREFERENCED_PARAMETER(NotificationEnabled);
}

static
void
LoopTxCancel(
    _In_ NETPACKETQUEUE PacketQueue)
{
    InterlockedExchange(&LoopGetQueue(PacketQueue)->Cancelled, 1);
}

static
void
LoopRxAdvance(
    _In_ NETPACKETQUEUE PacketQueue)
{
    LOOP_QUEUE *Queue = LoopGetQueue(PacketQueue);
    LOOP_ADAPTER *Adapter = Queue->Adapter;
    NET_RING *PacketRing = NetRingCollectionGetPacketRing(Queue->Rings);
    NET_RING *FragmentRing = NetRingCollectionGetFragmentRing(Queue->Rings);
    KIRQL OldIrql;

    PacketRing->NextIndex = PacketRing->EndIndex;
    FragmentRing->NextIndex = FragmentRing->EndIndex;

    if (Queue->Cancelled)
    {
        while (PacketRing->BeginIndex != PacketRing->EndIndex)
        {
            NET_PACKET *Packet = NetRingGetPacketAtIndex(PacketRing, PacketRing->BeginIndex);

            Packet->Ignore = 1;
            Packet->FragmentCount = 0;
            PacketRing->BeginIndex = NetRingIncrementIndex(PacketRing, PacketRing->BeginIndex);
        }
        FragmentRing->BeginIndex = FragmentRing->EndIndex;
        return;
    }

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    while (Adapter->Count &&
           PacketRing->BeginIndex != PacketRing->EndIndex &&
           FragmentRing->BeginIndex != FragmentRing->EndIndex)
    {
        LOOP_FRAME const *Frame = &Adapter->Frames[Adapter->Head];
        UINT32 FragmentIndex = FragmentRing->BeginIndex;
        NET_FRAGMENT *Fragment = NetRingGetFragmentAtIndex(FragmentRing, FragmentIndex);
        NET_PACKET *Packet = NetRingGetPacketAtIndex(PacketRing, PacketRing->BeginIndex);
        UCHAR *Data = (UCHAR *)NetExtensionGetFragmentVirtualAddress(&Queue->VirtualAddress, FragmentIndex)->VirtualAddress;

        if (Frame->Length <= Fragment->Capacity)
        {
            RtlCopyMemory(Data, Frame->Data, Frame->Length);
            Fragment->Offset = 0;
            Fragment->ValidLength = Frame->Length;
            Packet->FragmentIndex = FragmentIndex;
            Packet->FragmentCount = 1;
            Packet->Ignore = 0;
            RtlZeroMemory(&Packet->Layout, sizeof(Packet->Layout));
            Packet->Layout.Layer2Type = NetPacketLayer2TypeEthernet;
            FragmentRing->BeginIndex = NetRingIncrementIndex(FragmentRing, FragmentIndex);
            PacketRing->BeginIndex = NetRingIncrementIndex(PacketRing, PacketRing->BeginIndex);
            InterlockedIncrement64(&Adapter->RxFrames);
        }
        else
        {
            InterlockedIncrement64(&Adapter->Dropped);
        }
        Adapter->Head = (Adapter->Head + 1) % LOOP_PEER_FRAMES;
        Adapter->Count--;
    }
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
}

static
void
LoopRxSetNotificationEnabled(
    _In_ NETPACKETQUEUE PacketQueue,
    _In_ BOOLEAN NotificationEnabled)
{
    LOOP_ADAPTER *Adapter = LoopGetQueue(PacketQueue)->Adapter;
    BOOLEAN Pending;
    KIRQL OldIrql;

    InterlockedExchange(&Adapter->RxNotify, NotificationEnabled ? 1 : 0);
    if (!NotificationEnabled)
        return;

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    Pending = Adapter->Count != 0;
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (Pending && InterlockedExchange(&Adapter->RxNotify, 0))
        NetRxQueueNotifyMoreReceivedPacketsAvailable(PacketQueue);
}

static
void
LoopRxCancel(
    _In_ NETPACKETQUEUE PacketQueue)
{
    InterlockedExchange(&LoopGetQueue(PacketQueue)->Cancelled, 1);
}

static
NTSTATUS
LoopInitializeQueue(
    _In_ NETADAPTER NetAdapter,
    _In_ NETPACKETQUEUE PacketQueue,
    _In_ NET_RING_COLLECTION const *Rings,
    _In_ BOOLEAN Transmit)
{
    LOOP_QUEUE *Queue = LoopGetQueue(PacketQueue);
    NET_EXTENSION_QUERY Query;

    Queue->Adapter = LoopGetAdapter(NetAdapter);
    Queue->Rings = Rings;
    NET_EXTENSION_QUERY_INIT(&Query,
                             NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_NAME,
                             NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_VERSION_1,
                             NetExtensionTypeFragment);
    if (Transmit)
        NetTxQueueGetExtension(PacketQueue, &Query, &Queue->VirtualAddress);
    else
        NetRxQueueGetExtension(PacketQueue, &Query, &Queue->VirtualAddress);
    if (!Queue->VirtualAddress.Enabled)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static
NTSTATUS
LoopCreateTxQueue(
    _In_ NETADAPTER NetAdapter,
    _Inout_ NETTXQUEUE_INIT *TxQueueInit)
{
    NET_PACKET_QUEUE_CONFIG Config;
    WDF_OBJECT_ATTRIBUTES Attributes;
    NETPACKETQUEUE PacketQueue;
    NTSTATUS Status;

    NET_PACKET_QUEUE_CONFIG_INIT(&Config, LoopTxAdvance, LoopTxSetNotificationEnabled, LoopTxCancel);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, LOOP_QUEUE);
    Status = NetTxQueueCreate(TxQueueInit, &Attributes, &Config, &PacketQueue);
    if (!NT_SUCCESS(Status))
        return Status;
    return LoopInitializeQueue(NetAdapter, PacketQueue, NetTxQueueGetRingCollection(PacketQueue), TRUE);
}

static
NTSTATUS
LoopCreateRxQueue(
    _In_ NETADAPTER NetAdapter,
    _Inout_ NETRXQUEUE_INIT *RxQueueInit)
{
    NET_PACKET_QUEUE_CONFIG Config;
    WDF_OBJECT_ATTRIBUTES Attributes;
    NETPACKETQUEUE PacketQueue;
    NTSTATUS Status;

    NET_PACKET_QUEUE_CONFIG_INIT(&Config, LoopRxAdvance, LoopRxSetNotificationEnabled, LoopRxCancel);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, LOOP_QUEUE);
    Status = NetRxQueueCreate(RxQueueInit, &Attributes, &Config, &PacketQueue);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = LoopInitializeQueue(NetAdapter, PacketQueue, NetRxQueueGetRingCollection(PacketQueue), FALSE);
    if (NT_SUCCESS(Status))
        LoopGetAdapter(NetAdapter)->RxQueue = PacketQueue;
    return Status;
}

static
void
LoopSetReceiveFilter(
    _In_ NETADAPTER NetAdapter,
    _In_ NETRECEIVEFILTER ReceiveFilter)
{
    UNREFERENCED_PARAMETER(NetAdapter);
    UNREFERENCED_PARAMETER(ReceiveFilter);
}

static
NTSTATUS
LoopStartAdapter(
    _In_ NETADAPTER NetAdapter,
    _In_reads_(6) UCHAR const *LinkLayerAddress)
{
    NET_ADAPTER_LINK_LAYER_CAPABILITIES LinkLayer;
    NET_ADAPTER_LINK_LAYER_ADDRESS Address;
    NET_ADAPTER_TX_CAPABILITIES TxCapabilities;
    NET_ADAPTER_RX_CAPABILITIES RxCapabilities;
    NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES ReceiveFilter;
    NET_ADAPTER_LINK_STATE LinkState;

    KeInitializeSpinLock(&LoopGetAdapter(NetAdapter)->Lock);

    NET_ADAPTER_LINK_LAYER_CAPABILITIES_INIT(&LinkLayer, LOOP_LINK_SPEED, LOOP_LINK_SPEED);
    NetAdapterSetLinkLayerCapabilities(NetAdapter, &LinkLayer);
    NetAdapterSetLinkLayerMtuSize(NetAdapter, LOOP_MTU);

    NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&Address, 6, LinkLayerAddress);
    NetAdapterSetPermanentLinkLayerAddress(NetAdapter, &Address);
    NetAdapterSetCurrentLinkLayerAddress(NetAdapter, &Address);

    NET_ADAPTER_TX_CAPABILITIES_INIT(&TxCapabilities, 1);
    NET_ADAPTER_RX_CAPABILITIES_INIT_SYSTEM_MANAGED(&RxCapabilities, LOOP_MAX_FRAME, 1);
    NetAdapterSetDataPathCapabilities(NetAdapter, &TxCapabilities, &RxCapabilities);

    NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES_INIT(&ReceiveFilter, LoopSetReceiveFilter);
    ReceiveFilter.SupportedPacketFilters = (NET_PACKET_FILTER_FLAGS)(NetPacketFilterFlagDirected |
                                                                     NetPacketFilterFlagMulticast |
                                                                     NetPacketFilterFlagAllMulticast |
                                                                     NetPacketFilterFlagBroadcast |
                                                                     NetPacketFilterFlagPromiscuous);
    ReceiveFilter.MaximumMulticastAddresses = 32;
    NetAdapterSetReceiveFilterCapabilities(NetAdapter, &ReceiveFilter);

    NET_ADAPTER_LINK_STATE_INIT(&LinkState,
                                LOOP_LINK_SPEED,
                                MediaConnectStateConnected,
                                MediaDuplexStateFull,
                                NetAdapterPauseFunctionTypeUnsupported,
                                NetAdapterAutoNegotiationFlagNone);
    NetAdapterSetLinkState(NetAdapter, &LinkState);

    return NetAdapterStart(NetAdapter);
}

static
NTSTATUS
LoopCreateNetAdapter(
    _In_ NETADAPTER_INIT *AdapterInit,
    _Out_ NETADAPTER *NetAdapter)
{
    NET_ADAPTER_DATAPATH_CALLBACKS Datapath;
    WDF_OBJECT_ATTRIBUTES Attributes;

    NET_ADAPTER_DATAPATH_CALLBACKS_INIT(&Datapath, LoopCreateTxQueue, LoopCreateRxQueue);
    NetAdapterInitSetDatapathCallbacks(AdapterInit, &Datapath);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, LOOP_ADAPTER);
    return NetAdapterCreate(AdapterInit, &Attributes, NetAdapter);
}

#ifndef NETCXLOOP_WIFI
static
NTSTATUS
LoopCreateAdapter(
    _In_ NETADAPTER_INIT *AdapterInit,
    _In_reads_(6) UCHAR const *LinkLayerAddress,
    _Out_opt_ NETADAPTER *Created)
{
    NETADAPTER NetAdapter;
    NTSTATUS Status;

    Status = LoopCreateNetAdapter(AdapterInit, &NetAdapter);
    NetAdapterInitFree(AdapterInit);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = LoopStartAdapter(NetAdapter, LinkLayerAddress);
    if (NT_SUCCESS(Status) && Created)
        *Created = NetAdapter;
    return Status;
}
#endif

#ifdef NETCXLOOP_WIFI
typedef struct _LOOP_WIFI_DEVICE
{
    WDFDMAENABLER DmaEnabler;
} LOOP_WIFI_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LOOP_WIFI_DEVICE, LoopGetWifiDevice);

static
NTSTATUS
LoopWifiD0EntryPostInterruptsEnabled(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    LOOP_WIFI_DEVICE *Context = LoopGetWifiDevice(Device);
    WDF_DMA_ENABLER_CONFIG Config;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);

    if (Context->DmaEnabler != NULL)
        return STATUS_SUCCESS;

    WDF_DMA_ENABLER_CONFIG_INIT(&Config, WdfDmaProfileScatterGather64, LOOP_MAX_FRAME);
    Config.WdmDmaVersionOverride = 3;
    Status = WdfDmaEnablerCreate(Device, &Config, WDF_NO_OBJECT_ATTRIBUTES, &Context->DmaEnabler);
    if (!NT_SUCCESS(Status))
        DbgPrint("WIFILOOP: WdfDmaEnablerCreate failed 0x%08lx\n", Status);
    return Status;
}

static
NTSTATUS
LoopWifiPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    WIFI_DEVICE_CAPABILITIES Capabilities;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    WIFI_DEVICE_CAPABILITIES_INIT(&Capabilities);
    Capabilities.HardwareRadioState = TRUE;
    Capabilities.SoftwareRadioState = TRUE;
    Capabilities.NumRxStreams = 1;
    Capabilities.NumTxStreams = 1;
    Capabilities.NumRadios = 1;
    return WifiDeviceSetDeviceCapabilities(Device, &Capabilities);
}

static
NTSTATUS
LoopWifiCreateAdapter(
    _In_ WDFDEVICE Device,
    _Inout_ NETADAPTER_INIT *AdapterInit)
{
    NETADAPTER NetAdapter;
    NTSTATUS Status;

    if (LoopGetWifiDevice(Device)->DmaEnabler == NULL)
    {
        DbgPrint("WIFILOOP: the station adapter was requested before the device entered D0\n");
        return STATUS_DEVICE_NOT_READY;
    }

    Status = LoopCreateNetAdapter(AdapterInit, &NetAdapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WifiAdapterInitialize(NetAdapter);
    if (!NT_SUCCESS(Status))
        return Status;

    return LoopStartAdapter(NetAdapter, LoopAdapterAddress);
}

static
NTSTATUS
LoopWifiCreateWifiDirectDevice(
    _In_ WDFDEVICE Device,
    _Inout_ WIFIDIRECT_DEVICE_INIT *WifiDirectDeviceInit)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(WifiDirectDeviceInit);
    return STATUS_NOT_SUPPORTED;
}

static
VOID
LoopWifiSendCommand(
    _In_ WDFDEVICE Device,
    _In_ WIFIREQUEST Request)
{
    WDI_MESSAGE_HEADER *Header;
    WDI_MESSAGE_HEADER *Completion;
    WDFMEMORY Memory;
    UINT InputLength = 0;
    UINT OutputLength = 0;
    UINT16 MessageId;
    UINT32 TransactionId;
    WDI_PORT_ID PortId;

    Header = (WDI_MESSAGE_HEADER *)WifiRequestGetInOutBuffer(Request, &InputLength, &OutputLength);
    MessageId = WifiRequestGetMessageId(Request);
    TransactionId = Header->TransactionId;
    PortId = Header->PortId;
    DbgPrint("WIFILOOP: command %u port %u transaction %u length %u\n", MessageId, PortId, TransactionId, InputLength);

    Header->Status = NDIS_STATUS_SUCCESS;
    WifiRequestComplete(Request, STATUS_SUCCESS, sizeof(WDI_MESSAGE_HEADER));

    if (MessageId != WDI_TASK_SET_RADIO_STATE)
        return;

    if (!NT_SUCCESS(WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES, NonPagedPoolNx, 'LfiW',
                                    sizeof(WDI_MESSAGE_HEADER), &Memory, (PVOID *)&Completion)))
    {
        return;
    }
    RtlZeroMemory(Completion, sizeof(*Completion));
    Completion->PortId = PortId;
    Completion->TransactionId = TransactionId;
    Completion->Status = NDIS_STATUS_SUCCESS;
    WifiDeviceReceiveIndication(Device, MessageId, Memory);
    WdfObjectDelete(Memory);
}
#endif

static
NTSTATUS
LoopDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDFDEVICE Device;
#ifdef NETCXLOOP_WIFI
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPower;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WIFI_DEVICE_CONFIG WifiConfig;
#else
    NETADAPTER_INIT *AdapterInit;
#endif
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);

    Status = NetDeviceInitConfig(DeviceInit);
    if (!NT_SUCCESS(Status))
        return Status;

#ifdef NETCXLOOP_WIFI
    Status = WifiDeviceInitConfig(DeviceInit);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPower);
    PnpPower.EvtDevicePrepareHardware = LoopWifiPrepareHardware;
    PnpPower.EvtDeviceD0EntryPostInterruptsEnabled = LoopWifiD0EntryPostInterruptsEnabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPower);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, LOOP_WIFI_DEVICE);
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
#else
    Status = WdfDeviceCreate(&DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &Device);
#endif
    if (!NT_SUCCESS(Status))
        return Status;

#ifdef NETCXLOOP_WIFI
    WIFI_DEVICE_CONFIG_INIT(&WifiConfig, WDI_VERSION_LATEST, LoopWifiSendCommand,
                            LoopWifiCreateAdapter, LoopWifiCreateWifiDirectDevice);
    return WifiDeviceInitialize(Device, &WifiConfig);
#else
    AdapterInit = NetAdapterInitAllocate(Device);
    if (!AdapterInit)
        return STATUS_INSUFFICIENT_RESOURCES;

    return LoopCreateAdapter(AdapterInit, LoopAdapterAddress, NULL);
#endif
}

static
void
LoopDriverUnload(
    _In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);

#ifndef NETCXLOOP_WIFI
    if (LoopLightweightAdapter)
    {
        NetAdapterStop(LoopLightweightAdapter);
        LoopLightweightAdapter = NULL;
    }
#endif
}

extern "C"
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;
#ifndef NETCXLOOP_WIFI
    NETADAPTER_INIT *AdapterInit;
#endif
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, LoopDeviceAdd);
    Config.EvtDriverUnload = LoopDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(Status))
        return Status;

#ifndef NETCXLOOP_WIFI
    AdapterInit = NetAdapterLightweightInitAllocate(&LoopLightweightInterface);
    if (AdapterInit)
        (VOID)LoopCreateAdapter(AdapterInit, LoopLightweightAddress, &LoopLightweightAdapter);
#endif
    return STATUS_SUCCESS;
}
