/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        datalink/lan.c
 * PURPOSE:     Local Area Network media routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

#include <ntifs.h>
#include <receive.h>
#include <wait.h>

#define CCS_ROOT L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
#define TCPIP_GUID L"{4D36E972-E325-11CE-BFC1-08002BE10318}"

typedef struct _LAN_WQ_ITEM {
    LIST_ENTRY ListEntry;
    PNDIS_PACKET Packet;
    PLAN_ADAPTER Adapter;
    PIP_INTERFACE Interface;
    UINT BytesTransferred;
} LAN_WQ_ITEM, *PLAN_WQ_ITEM;

NPAGED_LOOKASIDE_LIST LanWqLookaside;

typedef struct _RECONFIGURE_CONTEXT {
    LIST_ENTRY ListEntry;
    ULONG State;
    PLAN_ADAPTER Adapter;
    PIP_INTERFACE Interface;
} RECONFIGURE_CONTEXT, *PRECONFIGURE_CONTEXT;

NDIS_HANDLE NdisProtocolHandle = (NDIS_HANDLE)NULL;
BOOLEAN ProtocolRegistered     = FALSE;
LIST_ENTRY AdapterListHead;
KSPIN_LOCK AdapterListLock;

typedef struct _LAN_REQUEST {
    NDIS_OID_REQUEST Request;
    KEVENT Event;
    NDIS_STATUS Status;
} LAN_REQUEST, *PLAN_REQUEST;

NDIS_STATUS NDISCall(
    PLAN_ADAPTER Adapter,
    NDIS_REQUEST_TYPE Type,
    NDIS_OID OID,
    PVOID Buffer,
    UINT Length)
/*
 * FUNCTION: Send a request to NDIS
 * ARGUMENTS:
 *     Adapter     = Pointer to a LAN_ADAPTER structure
 *     Type        = Type of request (Set or Query)
 *     OID         = Value to be set/queried for
 *     Buffer      = Pointer to a buffer to use
 *     Length      = Number of bytes in Buffer
 * RETURNS:
 *     Status of operation
 */
{
    LAN_REQUEST Context;
    PNDIS_OID_REQUEST Request = &Context.Request;
    NDIS_STATUS NdisStatus;
    NDIS_HANDLE NdisHandle;
    KIRQL OldIrql;

    RtlZeroMemory(&Context, sizeof(Context));
    KeInitializeEvent(&Context.Event, NotificationEvent, FALSE);
    Context.Status = NDIS_STATUS_PENDING;
    Request->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Request->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Request->Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    Request->RequestType = Type;
    if (Type == NdisRequestSetInformation) {
        Request->DATA.SET_INFORMATION.Oid                     = OID;
        Request->DATA.SET_INFORMATION.InformationBuffer       = Buffer;
        Request->DATA.SET_INFORMATION.InformationBufferLength = Length;
    } else {
        Request->DATA.QUERY_INFORMATION.Oid                     = OID;
        Request->DATA.QUERY_INFORMATION.InformationBuffer       = Buffer;
        Request->DATA.QUERY_INFORMATION.InformationBufferLength = Length;
    }

    if (!ExAcquireRundownProtection(&Adapter->RequestRundown))
        return NDIS_STATUS_ADAPTER_REMOVED;

    TcpipAcquireSpinLock(&Adapter->Lock, &OldIrql);
    NdisHandle = Adapter->NdisHandle;
    TcpipReleaseSpinLock(&Adapter->Lock, OldIrql);

    if (NdisHandle && Adapter->State != LAN_STATE_RESETTING) {
        NdisStatus = NdisOidRequest(NdisHandle, Request);
    } else {
        NdisStatus = NDIS_STATUS_NOT_ACCEPTED;
    }

    /* Wait for NDIS to complete the request */
    if (NdisStatus == NDIS_STATUS_PENDING) {
        KeWaitForSingleObject(&Context.Event,
                              UserRequest,
                              KernelMode,
                              FALSE,
                              NULL);
        NdisStatus = Context.Status;
    }

    ExReleaseRundownProtection(&Adapter->RequestRundown);
    return NdisStatus;
}

NDIS_STATUS
GetPacketTypeFromHeaderBuffer(PLAN_ADAPTER Adapter,
                              PVOID HeaderBuffer,
                              ULONG HeaderBufferSize,
                              PULONG PacketType)
{
    PETH_HEADER EthHeader = HeaderBuffer;

    if (HeaderBufferSize < Adapter->HeaderSize)
    {
        TI_DbgPrint(DEBUG_DATALINK, ("Runt frame (size %d).\n", HeaderBufferSize));
        return NDIS_STATUS_NOT_ACCEPTED;
    }

    switch (Adapter->Media)
    {
        case NdisMedium802_3:
            /* Ethernet and IEEE 802.3 frames can be distinguished by
               looking at the IEEE 802.3 length field. This field is
               less than or equal to 1500 for a valid IEEE 802.3 frame
               and larger than 1500 is it's a valid EtherType value.
               See RFC 1122, section 2.3.3 for more information */

            *PacketType = EthHeader->EType;
            break;

        default:
            TI_DbgPrint(MIN_TRACE, ("Unsupported media.\n"));

            /* FIXME: Support other medias */
            return NDIS_STATUS_NOT_ACCEPTED;
    }

    TI_DbgPrint(DEBUG_DATALINK, ("EtherType (0x%X).\n", *PacketType));

    return NDIS_STATUS_SUCCESS;
}

VOID FreeAdapter(
    PLAN_ADAPTER Adapter)
/*
 * FUNCTION: Frees memory for a LAN_ADAPTER structure
 * ARGUMENTS:
 *     Adapter = Pointer to LAN_ADAPTER structure to free
 */
{
    ExFreePoolWithTag(Adapter, LAN_ADAPTER_TAG);
}


NTSTATUS TcpipLanGetDwordOid
( PIP_INTERFACE Interface,
  NDIS_OID Oid,
  PULONG Result ) {
    /* Get maximum frame size */
    if( Interface->Context ) {
        return NDISCall((PLAN_ADAPTER)Interface->Context,
                        NdisRequestQueryInformation,
                        Oid,
                        Result,
                        sizeof(ULONG));
    } else switch( Oid ) { /* Loopback Case */
    case OID_GEN_HARDWARE_STATUS:
        *Result = NdisHardwareStatusReady;
        return STATUS_SUCCESS;
    case OID_GEN_MEDIA_CONNECT_STATUS:
        *Result = NdisMediaStateConnected;
        return STATUS_SUCCESS;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}


VOID NTAPI ProtocolOpenAdapterComplete(
    NDIS_HANDLE BindingContext,
    NDIS_STATUS Status)
/*
 * FUNCTION: Called by NDIS to complete opening of an adapter
 * ARGUMENTS:
 *     BindingContext  = Pointer to a device context (LAN_ADAPTER)
 *     Status          = Status of the operation
 */
{
    PLAN_ADAPTER Adapter = (PLAN_ADAPTER)BindingContext;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    Adapter->NdisStatus = Status;

    KeSetEvent(&Adapter->Event, 0, FALSE);
}


VOID NTAPI ProtocolCloseAdapterComplete(
    NDIS_HANDLE BindingContext)
/*
 * FUNCTION: Called by NDIS to complete closing an adapter
 * ARGUMENTS:
 *     BindingContext = Pointer to a device context (LAN_ADAPTER)
 */
{
    PLAN_ADAPTER Adapter = (PLAN_ADAPTER)BindingContext;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    Adapter->NdisStatus = NDIS_STATUS_SUCCESS;

    KeSetEvent(&Adapter->Event, 0, FALSE);
}


VOID NTAPI ProtocolRequestComplete(
    NDIS_HANDLE BindingContext,
    PNDIS_OID_REQUEST NdisRequest,
    NDIS_STATUS Status)
/*
 * FUNCTION: Called by NDIS to complete a request
 * ARGUMENTS:
 *     BindingContext = Pointer to a device context (LAN_ADAPTER)
 *     NdisRequest    = Pointer to an object describing the request
 *     Status         = Status of the operation
 */
{
    PLAN_REQUEST Context = CONTAINING_RECORD(NdisRequest, LAN_REQUEST, Request);

    UNREFERENCED_PARAMETER(BindingContext);

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    /* Concurrent OIDs must not consume each other's completion or status.
     * Adapter->Event is reserved for adapter lifecycle operations. */
    Context->Status = Status;
    KeSetEvent(&Context->Event, 0, FALSE);
}


NDIS_STATUS LANUpdateMulticastList(
    PVOID Context,
    const UCHAR *Address,
    BOOLEAN Add)
{
    PLAN_ADAPTER Adapter = Context;
    UCHAR List[LAN_MAX_MULTICAST][IEEE_802_ADDR_LENGTH];
    ULONG Count, i;
    KIRQL OldIrql;

    if (!Adapter || Adapter->Media != NdisMedium802_3)
        return NDIS_STATUS_NOT_SUPPORTED;

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);

    for (i = 0; i < Adapter->MulticastCount; i++)
    {
        if (RtlEqualMemory(Adapter->MulticastList[i], Address, IEEE_802_ADDR_LENGTH))
            break;
    }

    if (Add)
    {
        if (i < Adapter->MulticastCount)
        {
            Adapter->MulticastRefs[i]++;
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return NDIS_STATUS_SUCCESS;
        }
        if (Adapter->MulticastCount == LAN_MAX_MULTICAST)
        {
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return NDIS_STATUS_MULTICAST_FULL;
        }
        RtlCopyMemory(Adapter->MulticastList[i], Address, IEEE_802_ADDR_LENGTH);
        Adapter->MulticastRefs[i] = 1;
        Adapter->MulticastCount++;
    }
    else
    {
        if (i == Adapter->MulticastCount)
        {
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return NDIS_STATUS_SUCCESS;
        }
        if (--Adapter->MulticastRefs[i])
        {
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return NDIS_STATUS_SUCCESS;
        }
        Adapter->MulticastCount--;
        RtlMoveMemory(Adapter->MulticastList[i], Adapter->MulticastList[i + 1],
                      (Adapter->MulticastCount - i) * IEEE_802_ADDR_LENGTH);
        RtlMoveMemory(&Adapter->MulticastRefs[i], &Adapter->MulticastRefs[i + 1],
                      (Adapter->MulticastCount - i) * sizeof(ULONG));
    }

    Count = Adapter->MulticastCount;
    RtlCopyMemory(List, Adapter->MulticastList, Count * IEEE_802_ADDR_LENGTH);

    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    return NDISCall(Adapter,
                    NdisRequestSetInformation,
                    OID_802_3_MULTICAST_LIST,
                    List,
                    Count * IEEE_802_ADDR_LENGTH);
}

static VOID LanSendDone(
    PLAN_ADAPTER Adapter)
{
    if (InterlockedDecrement(&Adapter->PendingSends) == 0 &&
        InterlockedCompareExchange(&Adapter->Paused, 0, 0))
    {
        KeSetEvent(&Adapter->SendsDoneEvent, IO_NO_INCREMENT, FALSE);
    }
}

VOID NTAPI ProtocolSendComplete(
    NDIS_HANDLE BindingContext,
    PNET_BUFFER_LIST NetBufferLists,
    ULONG SendCompleteFlags)
/*
 * FUNCTION: Called by NDIS to complete sending process
 * ARGUMENTS:
 *     BindingContext = Pointer to a device context (LAN_ADAPTER)
 */
{
    PLAN_ADAPTER Adapter = (PLAN_ADAPTER)BindingContext;
    PNET_BUFFER_LIST Nbl, NextNbl;
    PVOID Buffer;
    PMDL Mdl;

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);

        Buffer = Nbl->ProtocolReserved[0];
        Mdl = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(Nbl));
        NdisFreeNetBufferList(Nbl);
        NdisFreeMdl(Mdl);
        ExFreePoolWithTag(Buffer, PACKET_BUFFER_TAG);

        LanSendDone(Adapter);
    }
}

VOID LanReceiveWorker( PVOID Context ) {
    ULONG PacketType;
    PLAN_WQ_ITEM WorkItem = (PLAN_WQ_ITEM)Context;
    PNDIS_PACKET Packet;
    PLAN_ADAPTER Adapter;
    UINT BytesTransferred;
    IP_PACKET IPPacket;
    PIP_INTERFACE Interface;

    Packet = WorkItem->Packet;
    Adapter = WorkItem->Adapter;
    Interface = WorkItem->Interface;
    BytesTransferred = WorkItem->BytesTransferred;

    ExFreeToNPagedLookasideList(&LanWqLookaside, WorkItem);

    IPInitializePacket(&IPPacket, 0);

    IPPacket.NdisPacket = Packet;
    IPPacket.Position = 0;
    IPPacket.TotalSize = BytesTransferred;

    PacketType = PC(IPPacket.NdisPacket)->PacketType;

    /* Update interface stats */
    Interface->Stats.InBytes += IPPacket.TotalSize + Adapter->HeaderSize;

    /* NDIS packet is freed in all of these cases */
    switch (PacketType) {
        case ETYPE_IPv4:
        case ETYPE_IPv6:
            IPReceive(Interface, &IPPacket);
            break;
        case ETYPE_ARP:
            ARPReceive(Interface, &IPPacket);
            break;
        default:
            IPPacket.Free(&IPPacket);
            break;
    }

    IPDereferenceInterface(Interface);
    ExReleaseRundownProtection(&Adapter->WorkRundown);
}

BOOLEAN LanSubmitReceiveWork(
    NDIS_HANDLE BindingContext,
    PNDIS_PACKET Packet,
    UINT BytesTransferred) {
    PLAN_ADAPTER Adapter = (PLAN_ADAPTER)BindingContext;
    PLAN_WQ_ITEM WQItem;
    PIP_INTERFACE Interface;

    TI_DbgPrint(DEBUG_DATALINK,("called\n"));

    if (!ExAcquireRundownProtection(&Adapter->WorkRundown))
        return FALSE;

    if (Adapter->Closing)
        goto Failure;

    Interface = Adapter->Context;
    if (!Interface || !IPReferenceInterface(Interface))
        goto Failure;

    WQItem = ExAllocateFromNPagedLookasideList(&LanWqLookaside);
    if (!WQItem)
    {
        IPDereferenceInterface(Interface);
        goto Failure;
    }

    WQItem->Packet = Packet;
    WQItem->Adapter = Adapter;
    WQItem->Interface = Interface;
    WQItem->BytesTransferred = BytesTransferred;

    if (!ChewCreate( LanReceiveWorker, WQItem ))
    {
        ExFreeToNPagedLookasideList(&LanWqLookaside, WQItem);
        IPDereferenceInterface(Interface);
        goto Failure;
    }

    return TRUE;

Failure:
    ExReleaseRundownProtection(&Adapter->WorkRundown);
    return FALSE;
}

static ULONG LanCopyNetBuffer(
    PNET_BUFFER NetBuffer,
    ULONG Offset,
    PUCHAR Destination,
    ULONG Length)
{
    PMDL Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer) + Offset;
    ULONG Copied = 0;
    ULONG MdlLength;
    PUCHAR Source;

    while (Mdl && Copied < Length)
    {
        MdlLength = MmGetMdlByteCount(Mdl);
        if (MdlOffset >= MdlLength)
        {
            MdlOffset -= MdlLength;
            Mdl = Mdl->Next;
            continue;
        }

        Source = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (!Source)
            break;

        MdlLength = min(MdlLength - MdlOffset, Length - Copied);
        RtlCopyMemory(Destination + Copied, Source + MdlOffset, MdlLength);
        Copied += MdlLength;
        MdlOffset = 0;
        Mdl = Mdl->Next;
    }

    return Copied;
}

VOID NTAPI ProtocolReceive(
    NDIS_HANDLE BindingContext,
    PNET_BUFFER_LIST NetBufferLists,
    NDIS_PORT_NUMBER PortNumber,
    ULONG NumberOfNetBufferLists,
    ULONG ReceiveFlags)
/*
 * FUNCTION: Called by NDIS when a packet has been received on the physical link
 * ARGUMENTS:
 *     BindingContext      = Pointer to a device context (LAN_ADAPTER)
 */
{
    ULONG PacketType;
    UINT PacketSize;
    PCHAR BufferData;
    NDIS_STATUS NdisStatus;
    PNDIS_PACKET NdisPacket;
    PLAN_ADAPTER Adapter = (PLAN_ADAPTER)BindingContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    ETH_HEADER HeaderStorage;
    PVOID HeaderBuffer;
    ULONG FrameSize;

    for (Nbl = NetBufferLists; Nbl; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
    {
        for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
             NetBuffer;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            if (Adapter->Closing || Adapter->State != LAN_STATE_STARTED) {
                TI_DbgPrint(DEBUG_DATALINK, ("Adapter is stopped.\n"));
                continue;
            }

            FrameSize = NET_BUFFER_DATA_LENGTH(NetBuffer);
            if (FrameSize <= Adapter->HeaderSize) {
                TI_DbgPrint(DEBUG_DATALINK, ("Runt frame received.\n"));
                continue;
            }

            HeaderBuffer = NdisGetDataBuffer(NetBuffer, Adapter->HeaderSize, &HeaderStorage, 1, 0);
            if (!HeaderBuffer)
                continue;

            NdisStatus = GetPacketTypeFromHeaderBuffer(Adapter,
                                                       HeaderBuffer,
                                                       Adapter->HeaderSize,
                                                       &PacketType);
            if (NdisStatus != NDIS_STATUS_SUCCESS)
                continue;

            PacketSize = FrameSize - Adapter->HeaderSize;

            NdisStatus = AllocatePacketWithBuffer(&NdisPacket, NULL, PacketSize);
            if (NdisStatus != NDIS_STATUS_SUCCESS)
                continue;

            PC(NdisPacket)->PacketType = PacketType;

            GetDataPtr(NdisPacket, 0, &BufferData, &PacketSize);

            if (LanCopyNetBuffer(NetBuffer, Adapter->HeaderSize, (PUCHAR)BufferData, PacketSize) != PacketSize ||
                !LanSubmitReceiveWork(BindingContext, NdisPacket, PacketSize))
            {
                FreeNdisPacket(NdisPacket);
            }
        }
    }

    if (NDIS_TEST_RECEIVE_CAN_PEND(ReceiveFlags))
    {
        NdisReturnNetBufferLists(Adapter->NdisHandle,
                                 NetBufferLists,
                                 NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags) ?
                                 NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
    }
}

static PKEY_VALUE_PARTIAL_INFORMATION ReadIpConfigurationValue(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = ZwQueryValueKey(KeyHandle, ValueName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        return NULL;

    Information = ExAllocatePoolWithTag(PagedPool, Length + 2 * sizeof(WCHAR), KEY_VALUE_TAG);
    if (!Information)
        return NULL;

    Status = ZwQueryValueKey(KeyHandle, ValueName, KeyValuePartialInformation, Information, Length, &Length);
    if (!NT_SUCCESS(Status) ||
        (Information->Type != REG_SZ && Information->Type != REG_MULTI_SZ))
    {
        ExFreePoolWithTag(Information, KEY_VALUE_TAG);
        return NULL;
    }

    RtlZeroMemory(Information->Data + Information->DataLength, 2 * sizeof(WCHAR));
    return Information;
}

static BOOLEAN ParseIpConfigurationAddress(PCWSTR String, PIPv4_RAW_ADDRESS Address)
{
    UNICODE_STRING StringU;
    ANSI_STRING StringA;

    RtlInitUnicodeString(&StringU, String);
    if (!NT_SUCCESS(RtlUnicodeStringToAnsiString(&StringA, &StringU, TRUE)))
        return FALSE;

    *Address = inet_addr(StringA.Buffer);
    RtlFreeAnsiString(&StringA);

    return *Address != 0xFFFFFFFF;
}

BOOLEAN ReadIpConfiguration(PIP_INTERFACE Interface)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ParameterHandle;
    PKEY_VALUE_PARTIAL_INFORMATION KeyValueInfo;
    ULONG KeyValueInfoLength;
    WCHAR Buffer[150];
    UNICODE_STRING IPAddress = RTL_CONSTANT_STRING(L"IPAddress");
    UNICODE_STRING Netmask = RTL_CONSTANT_STRING(L"SubnetMask");
    UNICODE_STRING Gateway = RTL_CONSTANT_STRING(L"DefaultGateway");
    UNICODE_STRING EnableDhcp = RTL_CONSTANT_STRING(L"EnableDHCP");
    UNICODE_STRING InterfaceMetric = RTL_CONSTANT_STRING(L"InterfaceMetric");
    UNICODE_STRING Prefix = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\");
    UNICODE_STRING TcpipRegistryPath;
    ULONG Unused;
    NTSTATUS Status;
    IP_ADDRESS DefaultMask, Router;

    AddrInitIPv4(&DefaultMask, 0);

    TcpipRegistryPath.MaximumLength = sizeof(WCHAR) * 150;
    TcpipRegistryPath.Length = 0;
    TcpipRegistryPath.Buffer = Buffer;

    /* Build the registry path */
    RtlAppendUnicodeStringToString(&TcpipRegistryPath, &Prefix);
    RtlAppendUnicodeStringToString(&TcpipRegistryPath, &Interface->Name);

    InitializeObjectAttributes(&ObjectAttributes,
                               &TcpipRegistryPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               0,
                               NULL);

    /* Open a handle to the adapter parameters */
    Status = ZwOpenKey(&ParameterHandle, KEY_READ, &ObjectAttributes);

    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }
    else
    {
        KeyValueInfoLength = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 16 * sizeof(WCHAR);
        KeyValueInfo = ExAllocatePoolWithTag(PagedPool,
                                             KeyValueInfoLength,
                                             KEY_VALUE_TAG);
        if (!KeyValueInfo)
        {
            ZwClose(ParameterHandle);
            return FALSE;
        }

        /* Read the InterfaceMetric value */
        Interface->Metric = 0;
        Status = ZwQueryValueKey(ParameterHandle,
                                 &InterfaceMetric,
                                 KeyValuePartialInformation,
                                 KeyValueInfo,
                                 KeyValueInfoLength,
                                 &Unused);
        if (NT_SUCCESS(Status) && KeyValueInfo->DataLength == sizeof(ULONG))
        {
            Interface->Metric = (UINT)*(PULONG)KeyValueInfo->Data;
            if (Interface->Metric > 9999)
                Interface->Metric = 9999;
        }

        /* Read the EnableDHCP entry */
        Status = ZwQueryValueKey(ParameterHandle,
                                 &EnableDhcp,
                                 KeyValuePartialInformation,
                                 KeyValueInfo,
                                 KeyValueInfoLength,
                                 &Unused);
        if (NT_SUCCESS(Status) && KeyValueInfo->DataLength == sizeof(ULONG) && (*(PULONG)KeyValueInfo->Data) == 0)
        {
            PKEY_VALUE_PARTIAL_INFORMATION AddressInfo, MaskInfo, GatewayInfo;
            PCWSTR AddressString, MaskString, AddressEnd, MaskEnd;
            IPv4_RAW_ADDRESS AddressValue, MaskValue, RouterValue;

            AddressInfo = ReadIpConfigurationValue(ParameterHandle, &IPAddress);
            MaskInfo = ReadIpConfigurationValue(ParameterHandle, &Netmask);
            if (AddressInfo && MaskInfo)
            {
                AddressString = (PCWSTR)AddressInfo->Data;
                AddressEnd = (PCWSTR)(AddressInfo->Data + AddressInfo->DataLength);
                MaskString = (PCWSTR)MaskInfo->Data;
                MaskEnd = (PCWSTR)(MaskInfo->Data + MaskInfo->DataLength);

                while (AddressString < AddressEnd && *AddressString &&
                       MaskString < MaskEnd && *MaskString)
                {
                    if (ParseIpConfigurationAddress(AddressString, &AddressValue) &&
                        ParseIpConfigurationAddress(MaskString, &MaskValue) &&
                        AddressValue != 0)
                    {
                        IPAddInterfaceAddress(Interface, AddressValue, MaskValue, NULL, NULL);
                    }

                    AddressString += wcslen(AddressString) + 1;
                    MaskString += wcslen(MaskString) + 1;
                }
            }
            if (AddressInfo)
                ExFreePoolWithTag(AddressInfo, KEY_VALUE_TAG);
            if (MaskInfo)
                ExFreePoolWithTag(MaskInfo, KEY_VALUE_TAG);

            /* Read default gateway info */
            GatewayInfo = ReadIpConfigurationValue(ParameterHandle, &Gateway);
            if (GatewayInfo)
            {
                if (ParseIpConfigurationAddress((PCWSTR)GatewayInfo->Data, &RouterValue) && RouterValue != 0)
                {
                    AddrInitIPv4(&Router, RouterValue);
                    RouterCreateRoute(&DefaultMask, &DefaultMask, &Router, Interface);
                }
                ExFreePoolWithTag(GatewayInfo, KEY_VALUE_TAG);
            }
        }

        ExFreePoolWithTag(KeyValueInfo, KEY_VALUE_TAG);
        ZwClose(ParameterHandle);
    }

    return TRUE;
}

BOOLEAN ReconfigureAdapter(PRECONFIGURE_CONTEXT Context)
{
    PLAN_ADAPTER Adapter = Context->Adapter;
    PIP_INTERFACE Interface = Context->Interface;

    if (Context->State == LAN_STATE_STARTED &&
        !Context->Adapter->CompletingReset)
    {
        /* A media transition keeps the interface addresses, and AddIPAddress
         * aliases have no registry counterpart: only load the configuration
         * while no address is assigned. */
        if (AddrIsUnspecified(&Interface->Unicast))
            ReadIpConfiguration(Interface);
    }
    else if (!Context->Adapter->CompletingReset)
    {
        /* Keep the addresses and routes across a disconnect; route selection
         * skips this interface until it connects again. Invalidate rather
         * than destroy neighbors, because the routes still reference them. */
        NBInvalidateNeighborsForInterface(Interface);
    }

    Context->Adapter->CompletingReset = FALSE;

    if (Context->State == LAN_STATE_STARTED)
    {
        /* Get maximum link speed */
        if (Adapter->LinkSpeed == NDIS_LINK_SPEED_UNKNOWN || Adapter->LinkSpeed == 0)
            Interface->Speed = IP_DEFAULT_LINK_SPEED;
        else
            Interface->Speed = (UINT)(Adapter->LinkSpeed / 100);

        Adapter->Speed = Interface->Speed * 100L;

        Interface->MTU = Adapter->MTU;
    }

    Adapter->State = Context->State;

    /* Update the IP and link status information cached in TCP */
    TCPUpdateInterfaceIPInformation(Interface);
    TCPUpdateInterfaceLinkStatus(Interface);

    return TRUE;
}

VOID ReconfigureAdapterWorker(PVOID Context)
{
    PLAN_ADAPTER Adapter = Context;
    PRECONFIGURE_CONTEXT ReconfigureContext;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Done;

    for (;;)
    {
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        Entry = RemoveHeadList(&Adapter->ReconfigureQueue);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        ReconfigureContext = CONTAINING_RECORD(Entry, RECONFIGURE_CONTEXT, ListEntry);

        /* Apply queued indications in arrival order. */
        if (!Adapter->Closing)
            ReconfigureAdapter(ReconfigureContext);

        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        Done = IsListEmpty(&Adapter->ReconfigureQueue);
        if (Done)
            Adapter->ReconfigureActive = FALSE;
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);

        IPDereferenceInterface(ReconfigureContext->Interface);
        ExFreePoolWithTag(ReconfigureContext, CONTEXT_TAG);
        ExReleaseRundownProtection(&Adapter->WorkRundown);

        /* Releasing the last work reference may let the adapter be torn down;
         * only a still-queued context keeps it alive. */
        if (Done)
            return;
    }
}

VOID NTAPI ProtocolStatus(
    NDIS_HANDLE BindingContext,
    NDIS_STATUS GeneralStatus,
    PVOID StatusBuffer,
    UINT StatusBufferSize)
/*
 * FUNCTION: Called by NDIS when the underlying driver has changed state
 * ARGUMENTS:
 *     BindingContext   = Pointer to a device context (LAN_ADAPTER)
 *     GeneralStatus    = A general status code
 *     StatusBuffer     = Pointer to a buffer with medium-specific data
 *     StatusBufferSize = Number of bytes in StatusBuffer
 */
{
    PLAN_ADAPTER Adapter = BindingContext;
    PRECONFIGURE_CONTEXT Context;
    PIP_INTERFACE Interface;
    KIRQL OldIrql;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    if (!ExAcquireRundownProtection(&Adapter->WorkRundown))
        return;

    /* Ignore status indications while the binding is opening or closing. */
    if (Adapter->Closing || !Adapter->Context)
        goto Failure;

    switch(GeneralStatus)
    {
        case NDIS_STATUS_MEDIA_CONNECT:
            TI_DbgPrint(MIN_TRACE, ("NDIS_STATUS_MEDIA_CONNECT\n"));

            GeneralStatus = LAN_STATE_STARTED;
            break;

        case NDIS_STATUS_MEDIA_DISCONNECT:
            TI_DbgPrint(MIN_TRACE, ("NDIS_STATUS_MEDIA_DISCONNECT\n"));

            GeneralStatus = LAN_STATE_STOPPED;
            break;

        case NDIS_STATUS_RESET_START:
            Adapter->OldState = Adapter->State;
            Adapter->State = LAN_STATE_RESETTING;
            /* Nothing else to do here */
            goto Failure;

        case NDIS_STATUS_RESET_END:
            Adapter->CompletingReset = TRUE;
            GeneralStatus = Adapter->OldState;
            break;

        default:
            TI_DbgPrint(MIN_TRACE, ("Unhandled status: %x", GeneralStatus));
            goto Failure;
    }

    Interface = Adapter->Context;
    if (!IPReferenceInterface(Interface))
        goto Failure;

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(RECONFIGURE_CONTEXT), CONTEXT_TAG);
    if (!Context)
    {
        IPDereferenceInterface(Interface);
        goto Failure;
    }

    Context->Adapter = Adapter;
    Context->Interface = Interface;
    Context->State = GeneralStatus;

    /* Adapter->State may lag behind indications still queued, so do not
     * deduplicate against it. One worker at a time drains the queue so that
     * parallel executive workers cannot reorder an adapter's changes. */
    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    InsertTailList(&Adapter->ReconfigureQueue, &Context->ListEntry);
    if (!Adapter->ReconfigureActive)
    {
        Adapter->ReconfigureActive = TRUE;
        if (!ChewCreate(ReconfigureAdapterWorker, Adapter))
        {
            Adapter->ReconfigureActive = FALSE;
            RemoveEntryList(&Context->ListEntry);
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            ExFreePoolWithTag(Context, CONTEXT_TAG);
            IPDereferenceInterface(Interface);
            goto Failure;
        }
    }
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    return;

Failure:
    ExReleaseRundownProtection(&Adapter->WorkRundown);
}

VOID NTAPI ProtocolStatusEx(
    NDIS_HANDLE BindingContext,
    PNDIS_STATUS_INDICATION StatusIndication)
{
    PLAN_ADAPTER Adapter = BindingContext;
    PNDIS_LINK_STATE LinkState;

    switch (StatusIndication->StatusCode)
    {
        case NDIS_STATUS_LINK_STATE:
            if (StatusIndication->StatusBufferSize < sizeof(NDIS_LINK_STATE))
                break;

            LinkState = StatusIndication->StatusBuffer;
            Adapter->LinkSpeed = LinkState->XmitLinkSpeed;
            ProtocolStatus(Adapter,
                           (LinkState->MediaConnectState == MediaConnectStateConnected) ?
                           NDIS_STATUS_MEDIA_CONNECT : NDIS_STATUS_MEDIA_DISCONNECT,
                           NULL, 0);
            break;

        case NDIS_STATUS_RESET_START:
        case NDIS_STATUS_RESET_END:
            ProtocolStatus(Adapter, StatusIndication->StatusCode, NULL, 0);
            break;

        default:
            break;
    }
}

NDIS_STATUS NTAPI
ProtocolPnPEvent(
    NDIS_HANDLE NdisBindingContext,
    PNET_PNP_EVENT_NOTIFICATION PnPEventNotification)
{
    PLAN_ADAPTER Adapter = NdisBindingContext;
    PNET_PNP_EVENT PnPEvent = &PnPEventNotification->NetPnPEvent;

    switch(PnPEvent->NetEvent)
    {
      case NetEventPause:
         KeClearEvent(&Adapter->SendsDoneEvent);
         InterlockedExchange(&Adapter->Paused, TRUE);
         if (InterlockedCompareExchange(&Adapter->PendingSends, 0, 0) != 0)
         {
             KeWaitForSingleObject(&Adapter->SendsDoneEvent,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
         }
         return NDIS_STATUS_SUCCESS;

      case NetEventRestart:
         InterlockedExchange(&Adapter->Paused, FALSE);
         return NDIS_STATUS_SUCCESS;

      case NetEventSetPower:
         TI_DbgPrint(MIN_TRACE, ("Device transitioned to power state %ld\n", PnPEvent->Buffer));
         return NDIS_STATUS_SUCCESS;

      case NetEventQueryPower:
         TI_DbgPrint(MIN_TRACE, ("Device wants to go into power state %ld\n", PnPEvent->Buffer));
         return NDIS_STATUS_SUCCESS;

      case NetEventQueryRemoveDevice:
         TI_DbgPrint(MIN_TRACE, ("Device is about to be removed\n"));
         return NDIS_STATUS_SUCCESS;

      case NetEventCancelRemoveDevice:
         TI_DbgPrint(MIN_TRACE, ("Device removal cancelled\n"));
         return NDIS_STATUS_SUCCESS;

      default:
         TI_DbgPrint(MIN_TRACE, ("Unhandled event type: %ld\n", PnPEvent->NetEvent));
         return NDIS_STATUS_SUCCESS;
    }
}

NDIS_STATUS NTAPI ProtocolBindAdapter(
    IN  NDIS_HANDLE    ProtocolDriverContext,
    IN  NDIS_HANDLE    BindContext,
    IN  PNDIS_BIND_PARAMETERS BindParameters)
/*
 * FUNCTION: Called by NDIS during NdisRegisterProtocol to set up initial
 *           bindings, and periodically thereafter as new adapters come online
 * ARGUMENTS:
 *     BindContext: Handle provided by NDIS to track pending binding operations
 */
{
    TI_DbgPrint(DEBUG_DATALINK, ("Called with registry path %wZ for %wZ\n", BindParameters->ProtocolSection, BindParameters->AdapterName));
    return LANRegisterAdapter(BindContext, BindParameters);
}


VOID LANTransmit(
    PVOID Context,
    PNDIS_PACKET NdisPacket,
    UINT Offset,
    PVOID LinkAddress,
    USHORT Type)
/*
 * FUNCTION: Transmits a packet
 * ARGUMENTS:
 *     Context     = Pointer to context information (LAN_ADAPTER)
 *     NdisPacket  = Pointer to NDIS packet to send
 *     Offset      = Offset in packet where data starts
 *     LinkAddress = Pointer to link address of destination (NULL = broadcast)
 *     Type        = LAN protocol type (LAN_PROTO_*)
 */
{
    PETH_HEADER EHeader;
    PCHAR Data, OldData;
    UINT Size, OldSize;
    PLAN_ADAPTER Adapter = (PLAN_ADAPTER)Context;
    PNET_BUFFER_LIST Nbl;
    PMDL Mdl;
    PIP_INTERFACE Interface = Adapter->Context;

    TI_DbgPrint(DEBUG_DATALINK,
		("Called( NdisPacket %x, Offset %d, Adapter %x )\n",
		 NdisPacket, Offset, Adapter));

    InterlockedIncrement(&Adapter->PendingSends);

    if (Adapter->State != LAN_STATE_STARTED ||
        InterlockedCompareExchange(&Adapter->Paused, 0, 0)) {
        LanSendDone(Adapter);
        (*PC(NdisPacket)->DLComplete)(PC(NdisPacket)->Context, NdisPacket, NDIS_STATUS_NOT_ACCEPTED);
        return;
    }

    TI_DbgPrint(DEBUG_DATALINK,
		("Adapter Address [%02x %02x %02x %02x %02x %02x]\n",
		 Adapter->HWAddress[0] & 0xff,
		 Adapter->HWAddress[1] & 0xff,
		 Adapter->HWAddress[2] & 0xff,
		 Adapter->HWAddress[3] & 0xff,
		 Adapter->HWAddress[4] & 0xff,
		 Adapter->HWAddress[5] & 0xff));

    GetDataPtr( NdisPacket, 0, &OldData, &OldSize );

    Size = OldSize + Adapter->HeaderSize;
    Data = ExAllocatePoolWithTag(NonPagedPool, Size, PACKET_BUFFER_TAG);
    Mdl = Data ? NdisAllocateMdl(Adapter->NdisHandle, Data, Size) : NULL;
    Nbl = Mdl ? NdisAllocateNetBufferAndNetBufferList(Adapter->NblPool, 0, 0, Mdl, 0, Size) : NULL;
    if (!Nbl) {
        if (Mdl)
            NdisFreeMdl(Mdl);
        if (Data)
            ExFreePoolWithTag(Data, PACKET_BUFFER_TAG);
        LanSendDone(Adapter);
        (*PC(NdisPacket)->DLComplete)(PC(NdisPacket)->Context, NdisPacket, NDIS_STATUS_RESOURCES);
        return;
    }

    Nbl->ProtocolReserved[0] = Data;
    Nbl->SourceHandle = Adapter->NdisHandle;

    RtlCopyMemory(Data + Adapter->HeaderSize, OldData, OldSize);

    (*PC(NdisPacket)->DLComplete)(PC(NdisPacket)->Context, NdisPacket, NDIS_STATUS_SUCCESS);

    switch (Adapter->Media) {
        case NdisMedium802_3:
            EHeader = (PETH_HEADER)Data;

            if (LinkAddress) {
                /* Unicast address */
                RtlCopyMemory(EHeader->DstAddr, LinkAddress, IEEE_802_ADDR_LENGTH);
            } else {
                /* Broadcast address */
                RtlFillMemory(EHeader->DstAddr, IEEE_802_ADDR_LENGTH, 0xFF);
            }

            RtlCopyMemory(EHeader->SrcAddr, Adapter->HWAddress, IEEE_802_ADDR_LENGTH);

            switch (Type) {
                case LAN_PROTO_IPv4:
                    EHeader->EType = ETYPE_IPv4;
                    break;
                case LAN_PROTO_ARP:
                    EHeader->EType = ETYPE_ARP;
                    break;
                case LAN_PROTO_IPv6:
                    EHeader->EType = ETYPE_IPv6;
                    break;
                default:
                    ASSERT(FALSE);
                    NdisFreeNetBufferList(Nbl);
                    NdisFreeMdl(Mdl);
                    ExFreePoolWithTag(Data, PACKET_BUFFER_TAG);
                    LanSendDone(Adapter);
                    return;
            }
            break;

        default:
            /* FIXME: Support other medias */
            break;
    }

	TI_DbgPrint( MID_TRACE, ("LinkAddress: %x\n", LinkAddress));
	if( LinkAddress ) {
	    TI_DbgPrint
		( MID_TRACE,
		  ("Link Address [%02x %02x %02x %02x %02x %02x]\n",
		   ((PCHAR)LinkAddress)[0] & 0xff,
		   ((PCHAR)LinkAddress)[1] & 0xff,
		   ((PCHAR)LinkAddress)[2] & 0xff,
		   ((PCHAR)LinkAddress)[3] & 0xff,
		   ((PCHAR)LinkAddress)[4] & 0xff,
		   ((PCHAR)LinkAddress)[5] & 0xff));
	}

    /* Update interface stats */
    Interface->Stats.OutBytes += Size;

	TI_DbgPrint(MID_TRACE, ("NdisSend\n"));
	NdisSendNetBufferLists(Adapter->NdisHandle,
	                       Nbl,
	                       NDIS_DEFAULT_PORT_NUMBER,
	                       KeGetCurrentIrql() == DISPATCH_LEVEL ? NDIS_SEND_FLAGS_DISPATCH_LEVEL : 0);
}

static NTSTATUS
OpenRegistryKey( PNDIS_STRING RegistryPath, PHANDLE RegHandle ) {
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, RegistryPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, 0, 0);
    Status = ZwOpenKey(RegHandle, KEY_ALL_ACCESS, &Attributes);
    return Status;
}

static NTSTATUS ReadStringFromRegistry( HANDLE RegHandle,
					PWCHAR RegistryValue,
					PUNICODE_STRING String ) {
    UNICODE_STRING ValueName;
    UNICODE_STRING UnicodeString;
    NTSTATUS Status;
    ULONG ResultLength;
    UCHAR buf[1024];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)buf;

    RtlInitUnicodeString(&ValueName, RegistryValue);
    Status =
	ZwQueryValueKey(RegHandle,
			&ValueName,
			KeyValuePartialInformation,
			Information,
			sizeof(buf),
			&ResultLength);

    if (!NT_SUCCESS(Status))
	return Status;
    /* IP address is stored as a REG_MULTI_SZ - we only pay attention to the first one though */
    TI_DbgPrint(MIN_TRACE, ("Information DataLength: 0x%x\n", Information->DataLength));

    UnicodeString.Buffer = (PWCHAR)&Information->Data;
    UnicodeString.Length = 0;
    while (UnicodeString.Length + sizeof(WCHAR) <= Information->DataLength &&
           UnicodeString.Buffer[UnicodeString.Length / sizeof(WCHAR)] != UNICODE_NULL)
    {
        UnicodeString.Length += sizeof(WCHAR);
    }
    UnicodeString.MaximumLength = UnicodeString.Length + sizeof(WCHAR);

    String->Buffer =
	(PWCHAR)ExAllocatePoolWithTag( NonPagedPool,
				UnicodeString.MaximumLength + sizeof(WCHAR), REG_STR_TAG );

    if( !String->Buffer ) return STATUS_NO_MEMORY;

    String->MaximumLength = UnicodeString.MaximumLength;
    RtlCopyUnicodeString( String, &UnicodeString );

    return STATUS_SUCCESS;
}

/*
 * Utility to copy and append two unicode strings.
 *
 * IN OUT PUNICODE_STRING ResultFirst -> First string and result
 * IN     PUNICODE_STRING Second      -> Second string to append
 * IN     BOOL            Deallocate  -> TRUE: Deallocate First string before
 *                                       overwriting.
 *
 * Returns NTSTATUS.
 */

NTSTATUS NTAPI AppendUnicodeString(PUNICODE_STRING ResultFirst,
				   PUNICODE_STRING Second,
				   BOOLEAN Deallocate) {
    NTSTATUS Status;
    UNICODE_STRING Ustr = *ResultFirst;
    PWSTR new_string = ExAllocatePoolWithTag
        (PagedPool,
         (ResultFirst->Length + Second->Length + sizeof(WCHAR)), TEMP_STRING_TAG);
    if( !new_string ) {
	return STATUS_NO_MEMORY;
    }
    memcpy( new_string, ResultFirst->Buffer, ResultFirst->Length );
    memcpy( new_string + ResultFirst->Length / sizeof(WCHAR),
	    Second->Buffer, Second->Length );
    if( Deallocate ) RtlFreeUnicodeString(ResultFirst);
    ResultFirst->Length = Ustr.Length + Second->Length;
    ResultFirst->MaximumLength = ResultFirst->Length;
    new_string[ResultFirst->Length / sizeof(WCHAR)] = 0;
    Status = RtlCreateUnicodeString(ResultFirst,new_string) ?
	STATUS_SUCCESS : STATUS_NO_MEMORY;
    ExFreePoolWithTag(new_string, TEMP_STRING_TAG);
    return Status;
}

static NTSTATUS CheckForDeviceDesc( PUNICODE_STRING EnumKeyName,
                                    PUNICODE_STRING TargetKeyName,
                                    PUNICODE_STRING Name,
                                    PUNICODE_STRING DeviceDesc ) {
    UNICODE_STRING RootDevice = { 0, 0, NULL }, LinkageKeyName = { 0, 0, NULL };
    UNICODE_STRING DescKeyName = { 0, 0, NULL }, Linkage = { 0, 0, NULL };
    UNICODE_STRING BackSlash = { 0, 0, NULL };
    HANDLE DescKey = NULL, LinkageKey = NULL;
    NTSTATUS Status;

    TI_DbgPrint(DEBUG_DATALINK,("EnumKeyName %wZ\n", EnumKeyName));

    RtlInitUnicodeString(&BackSlash, L"\\");
    RtlInitUnicodeString(&Linkage, L"\\Linkage");

    RtlInitUnicodeString(&DescKeyName, L"");
    AppendUnicodeString( &DescKeyName, EnumKeyName, FALSE );
    AppendUnicodeString( &DescKeyName, &BackSlash, TRUE );
    AppendUnicodeString( &DescKeyName, TargetKeyName, TRUE );

    RtlInitUnicodeString(&LinkageKeyName, L"");
    AppendUnicodeString( &LinkageKeyName, &DescKeyName, FALSE );
    AppendUnicodeString( &LinkageKeyName, &Linkage, TRUE );

    Status = OpenRegistryKey( &LinkageKeyName, &LinkageKey );
    if( !NT_SUCCESS(Status) ) goto cleanup;

    Status = ReadStringFromRegistry( LinkageKey, L"RootDevice", &RootDevice );
    if( !NT_SUCCESS(Status) ) goto cleanup;

    if( RtlCompareUnicodeString( &RootDevice, Name, TRUE ) == 0 ) {
        Status = OpenRegistryKey( &DescKeyName, &DescKey );
        if( !NT_SUCCESS(Status) ) goto cleanup;

        Status = ReadStringFromRegistry( DescKey, L"DriverDesc", DeviceDesc );
        if( !NT_SUCCESS(Status) ) goto cleanup;

        TI_DbgPrint(DEBUG_DATALINK,("ADAPTER DESC: %wZ\n", DeviceDesc));
    } else Status = STATUS_UNSUCCESSFUL;

cleanup:
    RtlFreeUnicodeString( &RootDevice );
    RtlFreeUnicodeString( &LinkageKeyName );
    RtlFreeUnicodeString( &DescKeyName );
    if( LinkageKey ) ZwClose( LinkageKey );
    if( DescKey ) ZwClose( DescKey );

    TI_DbgPrint(DEBUG_DATALINK,("Returning %x\n", Status));

    return Status;
}

static NTSTATUS FindDeviceDescForAdapter( PUNICODE_STRING Name,
                                          PUNICODE_STRING DeviceDesc ) {
    UNICODE_STRING EnumKeyName, TargetKeyName;
    HANDLE EnumKey;
    NTSTATUS Status;
    ULONG i;
    KEY_BASIC_INFORMATION *Kbio =
        ExAllocatePoolWithTag(NonPagedPool, sizeof(KEY_BASIC_INFORMATION), KBIO_TAG);
    ULONG KbioLength = sizeof(KEY_BASIC_INFORMATION), ResultLength;

    RtlInitUnicodeString( DeviceDesc, NULL );

    if( !Kbio ) return STATUS_INSUFFICIENT_RESOURCES;

    RtlInitUnicodeString
        (&EnumKeyName, CCS_ROOT L"\\Control\\Class\\" TCPIP_GUID);

    Status = OpenRegistryKey( &EnumKeyName, &EnumKey );

    if( !NT_SUCCESS(Status) ) {
        TI_DbgPrint(DEBUG_DATALINK,("Couldn't open Enum key %wZ: %x\n",
                                    &EnumKeyName, Status));
        ExFreePoolWithTag( Kbio, KBIO_TAG );
        return Status;
    }

    for( i = 0; NT_SUCCESS(Status); i++ ) {
        Status = ZwEnumerateKey( EnumKey, i, KeyBasicInformation,
                                 Kbio, KbioLength, &ResultLength );

        if( Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW ) {
            ExFreePoolWithTag( Kbio, KBIO_TAG );
            KbioLength = ResultLength;
            Kbio = ExAllocatePoolWithTag( NonPagedPool, KbioLength, KBIO_TAG );
            if( !Kbio ) {
                TI_DbgPrint(DEBUG_DATALINK,("Failed to allocate memory\n"));
                ZwClose( EnumKey );
                return STATUS_NO_MEMORY;
            }

            Status = ZwEnumerateKey( EnumKey, i, KeyBasicInformation,
                                     Kbio, KbioLength, &ResultLength );

            if( !NT_SUCCESS(Status) ) {
                TI_DbgPrint(DEBUG_DATALINK,("Couldn't enum key child %d\n", i));
                ZwClose( EnumKey );
                ExFreePoolWithTag( Kbio, KBIO_TAG );
                return Status;
            }
        }

        if( NT_SUCCESS(Status) ) {
            TargetKeyName.Length = TargetKeyName.MaximumLength =
                Kbio->NameLength;
            TargetKeyName.Buffer = Kbio->Name;

            Status = CheckForDeviceDesc
                ( &EnumKeyName, &TargetKeyName, Name, DeviceDesc );
            if( NT_SUCCESS(Status) ) {
                ZwClose( EnumKey );
                ExFreePoolWithTag( Kbio, KBIO_TAG );
                return Status;
            } else Status = STATUS_SUCCESS;
        }
    }

    ZwClose( EnumKey );
    ExFreePoolWithTag( Kbio, KBIO_TAG );
    return STATUS_UNSUCCESSFUL;
}

VOID GetName( PUNICODE_STRING RegistryKey,
              PUNICODE_STRING OutName ) {
    PWCHAR Ptr;
    UNICODE_STRING PartialRegistryKey;

    PartialRegistryKey.Buffer =
        RegistryKey->Buffer + wcslen(CCS_ROOT L"\\Services\\");
    Ptr = PartialRegistryKey.Buffer;

    while( *Ptr != L'\\' &&
           ((PCHAR)Ptr) < ((PCHAR)RegistryKey->Buffer) + RegistryKey->Length )
        Ptr++;

    PartialRegistryKey.Length = PartialRegistryKey.MaximumLength =
        (Ptr - PartialRegistryKey.Buffer) * sizeof(WCHAR);

    RtlInitUnicodeString( OutName, L"" );
    AppendUnicodeString( OutName, &PartialRegistryKey, FALSE );
}

BOOLEAN BindAdapter(
    PLAN_ADAPTER Adapter,
    PNDIS_BIND_PARAMETERS BindParameters)
/*
 * FUNCTION: Binds a LAN adapter to IP layer
 * ARGUMENTS:
 *     Adapter = Pointer to LAN_ADAPTER structure
 * NOTES:
 *    We set the lookahead buffer size, set the packet filter and
 *    bind the adapter to IP layer
 */
{
    PIP_INTERFACE IF;
    NDIS_STATUS NdisStatus;
    LLIP_BIND_INFO BindInfo;
    ULONG Lookahead = LOOKAHEAD_SIZE;
    NTSTATUS Status;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    Adapter->State = LAN_STATE_OPENING;

    NdisStatus = NDISCall(Adapter,
                          NdisRequestSetInformation,
                          OID_GEN_CURRENT_LOOKAHEAD,
                          &Lookahead,
                          sizeof(ULONG));
    if (NdisStatus != NDIS_STATUS_SUCCESS) {
        TI_DbgPrint(DEBUG_DATALINK, ("Could not set lookahead buffer size (0x%X).\n", NdisStatus));
        return FALSE;
    }

    /* Addresses can be assigned and packets sent before the first media
     * connect indication, so the interface needs its MTU from the start. */
    Adapter->MTU = BindParameters->MtuSize;
    if (Adapter->MTU == 0) {
        TI_DbgPrint(MIN_TRACE, ("Could not obtain a valid MTU (%u).\n", Adapter->MTU));
        return FALSE;
    }
    Adapter->MaxPacketSize = Adapter->MTU + Adapter->HeaderSize;

    /* Bind the adapter to IP layer */
    BindInfo.Context       = Adapter;
    BindInfo.HeaderSize    = Adapter->HeaderSize;
    BindInfo.MinFrameSize  = Adapter->MinFrameSize;
    BindInfo.MTU           = Adapter->MTU;
    BindInfo.Address       = (PUCHAR)&Adapter->HWAddress;
    BindInfo.AddressLength = Adapter->HWAddressLength;
    BindInfo.Transmit      = LANTransmit;

    IF = IPCreateInterface(&BindInfo);

    if (!IF) {
        TI_DbgPrint(MIN_TRACE, ("Insufficient resources.\n"));
        return FALSE;
    }

    /*
     * Query per-adapter configuration from the registry
     * In case anyone is curious:  there *is* an Ndis configuration api
     * for this sort of thing, but it doesn't really support things like
     * REG_MULTI_SZ very well, and there is a note in the DDK that says that
     * protocol drivers developed for win2k and above just use the native
     * services (ZwOpenKey, etc).
     */

    GetName( BindParameters->ProtocolSection, &IF->Name );

    Status = FindDeviceDescForAdapter( &IF->Name, &IF->Description );
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Failed to get device description.\n"));
        IPDestroyInterface(IF);
        return FALSE;
    }

    TI_DbgPrint(DEBUG_DATALINK,("Adapter Description: %wZ\n",
                &IF->Description));

    /* Register interface with IP layer */
    IPRegisterInterface(IF);

    /* Store adapter context */
    Adapter->Context = IF;

    /* Indicate the current media state */
    Adapter->LinkSpeed = BindParameters->XmitLinkSpeed;
    ProtocolStatus(Adapter,
                   (BindParameters->MediaConnectState == MediaConnectStateConnected) ? NDIS_STATUS_MEDIA_CONNECT : NDIS_STATUS_MEDIA_DISCONNECT,
                   NULL, 0);

    /* Set packet filter so we can send and receive packets */
    NdisStatus = NDISCall(Adapter,
                          NdisRequestSetInformation,
                          OID_GEN_CURRENT_PACKET_FILTER,
                          &Adapter->PacketFilter,
                          sizeof(UINT));

    if (NdisStatus != NDIS_STATUS_SUCCESS) {
        TI_DbgPrint(DEBUG_DATALINK, ("Could not set packet filter (0x%X).\n", NdisStatus));
        IPUnregisterInterface(IF);
        IPDestroyInterface(IF);
        return FALSE;
    }

    return TRUE;
}


VOID UnbindAdapter(
    PLAN_ADAPTER Adapter)
/*
 * FUNCTION: Unbinds a LAN adapter from IP layer
 * ARGUMENTS:
 *     Adapter = Pointer to LAN_ADAPTER structure
 */
{
    PIP_INTERFACE IF = Adapter->Context;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    if (IF) {
        Adapter->Context = NULL;

        IPUnregisterInterface(IF);

        IPDestroyInterface(IF);
    }
}


static VOID LanCloseBinding(
    PLAN_ADAPTER Adapter)
{
    InterlockedExchange(&Adapter->Closing, TRUE);

    if (NdisCloseAdapterEx(Adapter->NdisHandle) == NDIS_STATUS_PENDING)
        KeWaitForSingleObject(&Adapter->Event, UserRequest, KernelMode, FALSE, NULL);

    ExWaitForRundownProtectionRelease(&Adapter->WorkRundown);

    NdisFreeNetBufferListPool(Adapter->NblPool);
    ExFreePoolWithTag(Adapter, LAN_ADAPTER_TAG);
}

NDIS_STATUS LANRegisterAdapter(
    NDIS_HANDLE BindContext,
    PNDIS_BIND_PARAMETERS BindParameters)
/*
 * FUNCTION: Registers protocol with an NDIS adapter
 * ARGUMENTS:
 *     AdapterName = Pointer to string with name of adapter to register
 *     Adapter     = Address of pointer to a LAN_ADAPTER structure
 * RETURNS:
 *     Status of operation
 */
{
    PLAN_ADAPTER IF;
    NDIS_STATUS NdisStatus;
    UINT MediaIndex;
    NDIS_MEDIUM MediaArray[MAX_MEDIA];
    NDIS_OPEN_PARAMETERS OpenParameters;
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PNDIS_STRING AdapterName = BindParameters->AdapterName;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    if (BindParameters->MediaType != NdisMedium802_3 ||
        BindParameters->MacAddressLength != IEEE_802_ADDR_LENGTH)
        return NDIS_STATUS_UNSUPPORTED_MEDIA;

    IF = ExAllocatePoolWithTag(NonPagedPool, sizeof(LAN_ADAPTER), LAN_ADAPTER_TAG);
    if (!IF) {
        TI_DbgPrint(MIN_TRACE, ("Insufficient resources.\n"));
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(IF, sizeof(LAN_ADAPTER));
    ExInitializeRundownProtection(&IF->WorkRundown);
    ExInitializeRundownProtection(&IF->RequestRundown);

    /* Put adapter in stopped state */
    IF->State = LAN_STATE_STOPPED;

    /* Initialize protecting spin lock */
    KeInitializeSpinLock(&IF->Lock);
    InitializeListHead(&IF->ReconfigureQueue);

    KeInitializeEvent(&IF->Event, SynchronizationEvent, FALSE);
    KeInitializeEvent(&IF->SendsDoneEvent, NotificationEvent, FALSE);
    IF->Paused = TRUE;

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_TCP_IP;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = PACKET_BUFFER_TAG;

    IF->NblPool = NdisAllocateNetBufferListPool(NdisProtocolHandle, &PoolParameters);
    if (!IF->NblPool) {
        ExFreePoolWithTag(IF, LAN_ADAPTER_TAG);
        return NDIS_STATUS_RESOURCES;
    }

    /* Initialize array with media IDs we support */
    MediaArray[MEDIA_ETH] = NdisMedium802_3;

    RtlZeroMemory(&OpenParameters, sizeof(OpenParameters));
    OpenParameters.Header.Type = NDIS_OBJECT_TYPE_OPEN_PARAMETERS;
    OpenParameters.Header.Revision = NDIS_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.Header.Size = NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.AdapterName = AdapterName;
    OpenParameters.MediumArray = MediaArray;
    OpenParameters.MediumArraySize = MAX_MEDIA;
    OpenParameters.SelectedMediumIndex = &MediaIndex;

    TI_DbgPrint(DEBUG_DATALINK,("opening adapter %wZ\n", AdapterName));
    /* Open the adapter. */
    NdisStatus = NdisOpenAdapterEx(NdisProtocolHandle,
                                   IF,
                                   &OpenParameters,
                                   BindContext,
                                   &IF->NdisHandle);

    /* Wait until the adapter is opened */
    if (NdisStatus == NDIS_STATUS_PENDING) {
        KeWaitForSingleObject(&IF->Event, UserRequest, KernelMode, FALSE, NULL);
        NdisStatus = IF->NdisStatus;
    }
    if (NdisStatus != NDIS_STATUS_SUCCESS) {
	TI_DbgPrint(DEBUG_DATALINK,("denying adapter %wZ\n", AdapterName));
	NdisFreeNetBufferListPool(IF->NblPool);
	ExFreePoolWithTag(IF, LAN_ADAPTER_TAG);
        return NdisStatus;
    }

    IF->Media = MediaArray[MediaIndex];

    /* Fill LAN_ADAPTER structure with some adapter specific information */
    switch (IF->Media) {
    case NdisMedium802_3:
        IF->HWAddressLength = IEEE_802_ADDR_LENGTH;
        IF->BCastMask       = BCAST_ETH_MASK;
        IF->BCastCheck      = BCAST_ETH_CHECK;
        IF->BCastOffset     = BCAST_ETH_OFFSET;
        IF->HeaderSize      = sizeof(ETH_HEADER);
        IF->MinFrameSize    = 60;
        IF->PacketFilter    =
            NDIS_PACKET_TYPE_BROADCAST |
            NDIS_PACKET_TYPE_DIRECTED  |
            NDIS_PACKET_TYPE_MULTICAST;
        break;

    default:
        /* Unsupported media */
        TI_DbgPrint(MIN_TRACE, ("Unsupported media.\n"));
        LanCloseBinding(IF);
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Get current hardware address */
    RtlCopyMemory(IF->HWAddress, BindParameters->CurrentMacAddress, IF->HWAddressLength);

    /* Bind adapter to IP layer */
    if( !BindAdapter(IF, BindParameters) ) {
	TI_DbgPrint(DEBUG_DATALINK,("denying adapter %wZ (BindAdapter)\n", AdapterName));
	LanCloseBinding(IF);
	return NDIS_STATUS_NOT_ACCEPTED;
    }

    /* Add adapter to the adapter list */
    IF->Listed = TRUE;
    ExInterlockedInsertTailList(&AdapterListHead,
                                &IF->ListEntry,
                                &AdapterListLock);

    TI_DbgPrint(DEBUG_DATALINK, ("Leaving.\n"));

    return NDIS_STATUS_SUCCESS;
}


NDIS_STATUS LANUnregisterAdapter(
    PLAN_ADAPTER Adapter)
/*
 * FUNCTION: Unregisters protocol with NDIS adapter
 * ARGUMENTS:
 *     Adapter = Pointer to a LAN_ADAPTER structure
 * RETURNS:
 *     Status of operation
 */
{
    KIRQL OldIrql;
    NDIS_HANDLE NdisHandle;
    NDIS_STATUS NdisStatus = NDIS_STATUS_SUCCESS;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    InterlockedExchange(&Adapter->Closing, TRUE);

    /* Unlink the adapter from the list */
    TcpipAcquireSpinLock(&AdapterListLock, &OldIrql);
    if (Adapter->Listed)
    {
        RemoveEntryList(&Adapter->ListEntry);
        InitializeListHead(&Adapter->ListEntry);
        Adapter->Listed = FALSE;
    }
    TcpipReleaseSpinLock(&AdapterListLock, OldIrql);

    ExWaitForRundownProtectionRelease(&Adapter->RequestRundown);

    TcpipAcquireSpinLock(&Adapter->Lock, &OldIrql);
    NdisHandle = Adapter->NdisHandle;
    if (NdisHandle) {
        Adapter->NdisHandle = NULL;
        TcpipReleaseSpinLock(&Adapter->Lock, OldIrql);

        NdisStatus = NdisCloseAdapterEx(NdisHandle);
        if (NdisStatus == NDIS_STATUS_PENDING) {
            TcpipWaitForSingleObject(&Adapter->Event,
                                     UserRequest,
                                     KernelMode,
                                     FALSE,
                                     NULL);
            NdisStatus = Adapter->NdisStatus;
        }
    } else
        TcpipReleaseSpinLock(&Adapter->Lock, OldIrql);

    ExWaitForRundownProtectionRelease(&Adapter->WorkRundown);

    /* Unbind only after every receive/status worker has dropped its adapter
     * and interface references. */
    UnbindAdapter(Adapter);

    if (Adapter->NblPool)
        NdisFreeNetBufferListPool(Adapter->NblPool);

    FreeAdapter(Adapter);

    return NdisStatus;
}

VOID
NTAPI
LANUnregisterProtocol(VOID)
/*
 * FUNCTION: Unregisters this protocol driver with NDIS
 * NOTES: Does not care wether we are already registered
 */
{
    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    if (ProtocolRegistered) {
        PLAN_ADAPTER Current;
        KIRQL OldIrql;

        for (;;) {
            TcpipAcquireSpinLock(&AdapterListLock, &OldIrql);
            if (IsListEmpty(&AdapterListHead)) {
                TcpipReleaseSpinLock(&AdapterListLock, OldIrql);
                break;
            }

            Current = CONTAINING_RECORD(RemoveHeadList(&AdapterListHead), LAN_ADAPTER, ListEntry);
            InitializeListHead(&Current->ListEntry);
            Current->Listed = FALSE;
            TcpipReleaseSpinLock(&AdapterListLock, OldIrql);

            LANUnregisterAdapter(Current);
        }

        NdisDeregisterProtocolDriver(NdisProtocolHandle);
        ProtocolRegistered = FALSE;

        ExDeleteNPagedLookasideList(&LanWqLookaside);
    }
}

NDIS_STATUS
NTAPI
ProtocolUnbindAdapter(
    NDIS_HANDLE UnbindContext,
    NDIS_HANDLE ProtocolBindingContext)
{
    /* We don't pend any unbinding so we can just ignore UnbindContext */
    return LANUnregisterAdapter((PLAN_ADAPTER)ProtocolBindingContext);
}

NTSTATUS LANRegisterProtocol(
    PNDIS_STRING Name)
/*
 * FUNCTION: Registers this protocol driver with NDIS
 * ARGUMENTS:
 *     Name = Name of this protocol driver
 * RETURNS:
 *     Status of operation
 */
{
    NDIS_STATUS NdisStatus;
    NDIS_PROTOCOL_DRIVER_CHARACTERISTICS ProtChars;

    TI_DbgPrint(DEBUG_DATALINK, ("Called.\n"));

    InitializeListHead(&AdapterListHead);
    KeInitializeSpinLock(&AdapterListLock);

    ExInitializeNPagedLookasideList(&LanWqLookaside, NULL, NULL, 0,
                                    sizeof(LAN_WQ_ITEM), WQ_CONTEXT_TAG, 0);

    /* Set up protocol characteristics */
    RtlZeroMemory(&ProtChars, sizeof(ProtChars));
    ProtChars.Header.Type                    = NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
    ProtChars.Header.Revision                = NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    ProtChars.Header.Size                    = NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    ProtChars.MajorNdisVersion               = NDIS_VERSION_MAJOR;
    ProtChars.MinorNdisVersion               = NDIS_VERSION_MINOR;
    ProtChars.Name.Length                    = Name->Length;
    ProtChars.Name.Buffer                    = Name->Buffer;
    ProtChars.Name.MaximumLength             = Name->MaximumLength;
    ProtChars.OpenAdapterCompleteHandlerEx   = ProtocolOpenAdapterComplete;
    ProtChars.CloseAdapterCompleteHandlerEx  = ProtocolCloseAdapterComplete;
    ProtChars.OidRequestCompleteHandler      = ProtocolRequestComplete;
    ProtChars.SendNetBufferListsCompleteHandler = ProtocolSendComplete;
    ProtChars.ReceiveNetBufferListsHandler   = ProtocolReceive;
    ProtChars.StatusHandlerEx                = ProtocolStatusEx;
    ProtChars.BindAdapterHandlerEx           = ProtocolBindAdapter;
    ProtChars.NetPnPEventHandler             = ProtocolPnPEvent;
    ProtChars.UnbindAdapterHandlerEx         = ProtocolUnbindAdapter;

    /* Try to register protocol */
    NdisStatus = NdisRegisterProtocolDriver(NULL,
                                            &ProtChars,
                                            &NdisProtocolHandle);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        TI_DbgPrint(DEBUG_DATALINK, ("NdisRegisterProtocol failed, status 0x%x\n", NdisStatus));
        return (NTSTATUS)NdisStatus;
    }

    ProtocolRegistered = TRUE;

    return STATUS_SUCCESS;
}

/* EOF */
