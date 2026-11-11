/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        transport/datagram/datagram.c
 * PURPOSE:     Routines for sending and receiving datagrams
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

/* TODO: bound the queue by the socket's SO_RCVBUF; AFD enforces it on what it takes, this caps what waits for its next receive */
#define DG_QUEUE_LIMIT 0x10000

typedef struct _DATAGRAM_INFO
{
    IP_ADDRESS SrcAddress;
    IP_ADDRESS DstAddress;
    USHORT SrcPort;
    BOOLEAN PacketIsV4;
    UCHAR Ttl;
    UCHAR Tos;
    ULONG InterfaceIndex;
} DATAGRAM_INFO, *PDATAGRAM_INFO;

typedef struct _DATAGRAM_QUEUED_DATA
{
    LIST_ENTRY ListEntry;
    DATAGRAM_INFO Info;
    ULONG DataSize;
    UCHAR Data[ANYSIZE_ARRAY];
} DATAGRAM_QUEUED_DATA, *PDATAGRAM_QUEUED_DATA;

BOOLEAN DGRemoveIRP(
    PADDRESS_FILE AddrFile,
    PIRP Irp)
{
    PLIST_ENTRY ListEntry;
    PDATAGRAM_RECEIVE_REQUEST ReceiveRequest;
    BOOLEAN Found = FALSE;

    TI_DbgPrint(MAX_TRACE, ("Called (Cancel IRP %08x for file %08x).\n",
                            Irp, AddrFile));

    LockObject(AddrFile);

    for( ListEntry = AddrFile->ReceiveQueue.Flink;
         ListEntry != &AddrFile->ReceiveQueue;
         ListEntry = ListEntry->Flink )
    {
        ReceiveRequest = CONTAINING_RECORD
            (ListEntry, DATAGRAM_RECEIVE_REQUEST, ListEntry);

        TI_DbgPrint(MAX_TRACE, ("Request: %08x?\n", ReceiveRequest));

        if (ReceiveRequest->Irp == Irp)
        {
            RemoveEntryList(&ReceiveRequest->ListEntry);
            ExFreePoolWithTag(ReceiveRequest, DATAGRAM_RECV_TAG);
            Found = TRUE;
            break;
        }
    }

    UnlockObject(AddrFile);

    TI_DbgPrint(MAX_TRACE, ("Done.\n"));

    return Found;
}

static ULONG
DGAppendControl(
    PUCHAR Buffer,
    ULONG BufferLength,
    LONG Level,
    LONG Type,
    PVOID Data,
    ULONG DataLength)
{
    if (TDI_CMSG_SPACE(DataLength) > BufferLength)
        return 0;

    RtlZeroMemory(Buffer, TDI_CMSG_SPACE(DataLength));
    TDI_INIT_CMSGHDR(Buffer, Level, Type, DataLength);
    RtlCopyMemory(TDI_CMSG_DATA(Buffer), Data, DataLength);
    return TDI_CMSG_SPACE(DataLength);
}

static ULONG
DGBuildControlData(
    PADDRESS_FILE AddrFile,
    PDATAGRAM_INFO Info,
    PUCHAR Buffer,
    ULONG BufferLength)
{
    PIP_ADDRESS DstAddress = &Info->DstAddress;
    ULONG Written = 0;
    ULONG PktInfo[2];
    INT Value;

    if (!Buffer || !Info->PacketIsV4 || DstAddress->Type != IP_ADDRESS_V4)
        return 0;

    if (AddrFile->RecvPktInfo)
    {
        PktInfo[0] = DstAddress->Address.IPv4Address;
        PktInfo[1] = Info->InterfaceIndex;
        Written += DGAppendControl(Buffer + Written, BufferLength - Written,
                                   WS_IPPROTO_IP, WS_IP_PKTINFO, PktInfo, sizeof(PktInfo));
    }

    if (AddrFile->RecvTtl)
    {
        Value = Info->Ttl;
        Written += DGAppendControl(Buffer + Written, BufferLength - Written,
                                   WS_IPPROTO_IP, WS_IP_TTL, &Value, sizeof(Value));
    }

    if (AddrFile->RecvTos)
    {
        Value = Info->Tos;
        Written += DGAppendControl(Buffer + Written, BufferLength - Written,
                                   WS_IPPROTO_IP, WS_IP_TOS, &Value, sizeof(Value));
    }

    if (AddrFile->RecvEcn)
    {
        Value = Info->Tos & 3;
        Written += DGAppendControl(Buffer + Written, BufferLength - Written,
                                   WS_IPPROTO_IP, WS_IP_ECN, &Value, sizeof(Value));
    }

    if (AddrFile->RecvDstAddr)
    {
        Written += DGAppendControl(Buffer + Written, BufferLength - Written,
                                   WS_IPPROTO_IP, WS_IP_RECVDSTADDR,
                                   &DstAddress->Address.IPv4Address, sizeof(IPv4_RAW_ADDRESS));
    }

    return Written;
}

static ULONG
DGCopyDatagram(
    PADDRESS_FILE AddrFile,
    PDATAGRAM_INFO Info,
    PVOID Data,
    ULONG DataSize,
    PCHAR Buffer,
    ULONG BufferSize,
    PTDI_CONNECTION_INFORMATION ReturnInfo)
{
    PTA_IP_ADDRESS RTAIPAddress;
    ULONG Copied = MIN(BufferSize, DataSize);

    RtlCopyMemory(Buffer, Data, Copied);

    RTAIPAddress = (PTA_IP_ADDRESS)ReturnInfo->RemoteAddress;
    if (Info->SrcAddress.Type == IP_ADDRESS_V6)
    {
        AddrBuildTransportAddress(&Info->SrcAddress, Info->SrcPort,
                                  (PTRANSPORT_ADDRESS)RTAIPAddress,
                                  ReturnInfo->RemoteAddressLength);
    }
    else if (AddrFile->Address.Type == IP_ADDRESS_V6)
    {
        IP_ADDRESS MappedSource = Info->SrcAddress;

        TCPMapToDualStack(&MappedSource);
        AddrBuildTransportAddress(&MappedSource, Info->SrcPort,
                                  (PTRANSPORT_ADDRESS)RTAIPAddress,
                                  ReturnInfo->RemoteAddressLength);
    }
    else
    {
        RTAIPAddress->TAAddressCount = 1;
        RTAIPAddress->Address->AddressType = TDI_ADDRESS_TYPE_IP;
        RTAIPAddress->Address->AddressLength = TDI_ADDRESS_LENGTH_IP;
        RTAIPAddress->Address->Address->sin_port = Info->SrcPort;
        RTAIPAddress->Address->Address->in_addr = Info->SrcAddress.Address.IPv4Address;
        RtlZeroMemory(RTAIPAddress->Address->Address->sin_zero, 8);
    }

    if (ReturnInfo->Options)
    {
        ReturnInfo->OptionsLength =
            DGBuildControlData(AddrFile,
                               Info,
                               ReturnInfo->Options,
                               ReturnInfo->OptionsLength > 0 ? ReturnInfo->OptionsLength : 0);
    }

    TI_DbgPrint(MAX_TRACE, ("(A: %08x) Addr %08x Port %04x\n",
                RTAIPAddress,
                Info->SrcAddress.Address.IPv4Address, Info->SrcPort));

    return Copied;
}

static VOID
DGQueueDatagram(
    PADDRESS_FILE AddrFile,
    PDATAGRAM_INFO Info,
    PVOID Data,
    ULONG DataSize)
{
    PDATAGRAM_QUEUED_DATA Queued;

    ASSERT_TCPIP_OBJECT_LOCKED(AddrFile);

    if (DataSize > DG_QUEUE_LIMIT - AddrFile->QueuedBytes)
    {
        TI_DbgPrint(MAX_TRACE, ("Receive queue full, discarding datagram.\n"));
        return;
    }

    Queued = ExAllocatePoolWithTag(NonPagedPool,
                                   FIELD_OFFSET(DATAGRAM_QUEUED_DATA, Data) + DataSize,
                                   DATAGRAM_QUEUE_TAG);
    if (!Queued)
        return;

    Queued->Info = *Info;
    Queued->DataSize = DataSize;
    RtlCopyMemory(Queued->Data, Data, DataSize);
    AddrFile->QueuedBytes += DataSize;
    InsertTailList(&AddrFile->DatagramQueue, &Queued->ListEntry);
}

VOID
DGFreeQueuedDatagrams(
    PADDRESS_FILE AddrFile)
{
    PDATAGRAM_QUEUED_DATA Queued;

    while (!IsListEmpty(&AddrFile->DatagramQueue))
    {
        Queued = CONTAINING_RECORD(RemoveHeadList(&AddrFile->DatagramQueue),
                                   DATAGRAM_QUEUED_DATA, ListEntry);
        AddrFile->QueuedBytes -= Queued->DataSize;
        ExFreePoolWithTag(Queued, DATAGRAM_QUEUE_TAG);
    }
}

VOID
DGDeliverData(
    PADDRESS_FILE AddrFile,
    PIP_ADDRESS SrcAddress,
    PIP_ADDRESS DstAddress,
    USHORT SrcPort,
    USHORT DstPort,
    PIP_PACKET IPPacket,
    UINT DataSize,
    PIP_INTERFACE Interface)
/*
 * FUNCTION: Delivers datagram data to a user
 * ARGUMENTS:
 *     AddrFile = Address file to deliver data to
 *     Address  = Remote address the packet came from
 *     IPPacket = Pointer to IP packet to deliver
 *     DataSize = Number of bytes in data area
 *                (incl. IP header for raw IP file objects)
 * NOTES:
 *     If there is a receive request, then we copy the data to the
 *     buffer supplied by the user and complete the receive request.
 *     If no suitable receive request exists, then we call the event
 *     handler if it exists, otherwise we drop the packet.
 */
{
    LONG AddressLength;
    PVOID SourceAddress;
    ULONG BytesTaken;
    NTSTATUS Status;
    PVOID DataBuffer;
    DATAGRAM_INFO Info;

    TI_DbgPrint(MIN_TRACE, ("Called.\n"));

    Info.SrcAddress = *SrcAddress;
    Info.DstAddress = *DstAddress;
    Info.SrcPort = SrcPort;
    Info.PacketIsV4 = IPPacket->Type == IP_ADDRESS_V4;
    Info.Ttl = Info.PacketIsV4 ? ((PIPv4_HEADER)IPPacket->Header)->Ttl : 0;
    Info.Tos = Info.PacketIsV4 ? ((PIPv4_HEADER)IPPacket->Header)->Tos : 0;
    Info.InterfaceIndex = Interface ? Interface->Index : 0;

    LockObject(AddrFile);

    if (AddrFile->Protocol == IPPROTO_UDP && !AddrFile->Raw)
        DataBuffer = IPPacket->Data;
    else
        DataBuffer = IPPacket->Header;

    if (!IsListEmpty(&AddrFile->ReceiveQueue))
    {
        PLIST_ENTRY CurrentEntry;
        PDATAGRAM_RECEIVE_REQUEST Current = NULL;

        /* Search receive request list to find a match */
        CurrentEntry = AddrFile->ReceiveQueue.Flink;
        while (CurrentEntry != &AddrFile->ReceiveQueue)
        {
            Current = CONTAINING_RECORD(CurrentEntry, DATAGRAM_RECEIVE_REQUEST, ListEntry);
            CurrentEntry = CurrentEntry->Flink;
            if (DstPort == AddrFile->Port && AddrFileReceiveMatch(AddrFile, DstAddress))
            {
                /* Remove the request from the queue */
                RemoveEntryList(&Current->ListEntry);

                TI_DbgPrint(MAX_TRACE, ("Suitable receive request found.\n"));

                TI_DbgPrint(MAX_TRACE,
                           ("Target Buffer: %x, Source Buffer: %x, Size %d\n",
                            Current->Buffer, DataBuffer, DataSize));

                DGCopyDatagram(AddrFile, &Info, DataBuffer, DataSize,
                               Current->Buffer, Current->BufferSize, Current->ReturnInfo);

                ReferenceObject(AddrFile);
                UnlockObject(AddrFile);

                /* Complete the receive request */
                if (Current->BufferSize < DataSize)
                    Current->Complete(Current->Context, STATUS_BUFFER_OVERFLOW, Current->BufferSize);
                else
                    Current->Complete(Current->Context, STATUS_SUCCESS, DataSize);

                LockObject(AddrFile);
                DereferenceObject(AddrFile);
                break;
            }
        }

        UnlockObject(AddrFile);
    }
    else if (AddrFile->RegisteredReceiveDatagramHandler &&
             ExAcquireRundownProtection(&AddrFile->ReceiveDatagramRundown))
    {
        PTDI_IND_RECEIVE_DATAGRAM ReceiveHandler = AddrFile->ReceiveDatagramHandler;
        PVOID HandlerContext = AddrFile->ReceiveDatagramHandlerContext;
        PVOID OptionsData = NULL;
        INT32 OptionsSize = 0;

        TI_DbgPrint(MAX_TRACE, ("Calling receive event handler.\n"));

        if (SrcAddress->Type == IP_ADDRESS_V4)
        {
            AddressLength = sizeof(IPv4_RAW_ADDRESS);
            SourceAddress = &SrcAddress->Address.IPv4Address;
            OptionsSize = IPPacket->HeaderSize - sizeof(IPv4_HEADER);
            if (OptionsSize > 0)
            {
                OptionsData = (PUCHAR)IPPacket->Header + sizeof(IPv4_HEADER);
            }
        }
        else /* (Address->Type == IP_ADDRESS_V6) */
        {
            AddressLength = sizeof(IPv6_RAW_ADDRESS);
            SourceAddress = SrcAddress->Address.IPv6Address;
        }

        ReferenceObject(AddrFile);
        UnlockObject(AddrFile);

        TI_DbgPrint(MIN_TRACE, ("OptionsSize %d DataSize: %u\n", OptionsSize, DataSize));

        Status = (*ReceiveHandler)(HandlerContext,
            AddressLength,
            SourceAddress,
            OptionsSize,
            OptionsData,
            TDI_RECEIVE_ENTIRE_MESSAGE,
            DataSize,
            DataSize,
            &BytesTaken,
            DataBuffer,
            NULL);

        if (STATUS_SUCCESS != Status)
            TI_DbgPrint(MAX_TRACE, ("receive handler signaled failure with Status 0x%x\n", Status));

        ExReleaseRundownProtection(&AddrFile->ReceiveDatagramRundown);
        DereferenceObject(AddrFile);
    }
    else if (DstPort == AddrFile->Port && AddrFileReceiveMatch(AddrFile, DstAddress))
    {
        DGQueueDatagram(AddrFile, &Info, DataBuffer, DataSize);
        UnlockObject(AddrFile);
    }
    else
    {
        UnlockObject(AddrFile);
        TI_DbgPrint(MAX_TRACE, ("Discarding datagram.\n"));
    }

    TI_DbgPrint(MAX_TRACE, ("Leaving.\n"));
}

VOID DGReceiveComplete(PVOID Context, NTSTATUS Status, ULONG Count) {
    PDATAGRAM_RECEIVE_REQUEST ReceiveRequest =
	(PDATAGRAM_RECEIVE_REQUEST)Context;
    TI_DbgPrint(MAX_TRACE,("Called (%08x:%08x)\n", Status, Count));
    ReceiveRequest->UserComplete( ReceiveRequest->UserContext, Status, Count );
    ExFreePoolWithTag( ReceiveRequest, DATAGRAM_RECV_TAG );
    TI_DbgPrint(MAX_TRACE,("Done\n"));
}

NTSTATUS DGReceiveDatagram(
    PADDRESS_FILE AddrFile,
    PTDI_CONNECTION_INFORMATION ConnInfo,
    PCHAR BufferData,
    ULONG ReceiveLength,
    ULONG ReceiveFlags,
    PTDI_CONNECTION_INFORMATION ReturnInfo,
    PULONG BytesReceived,
    PDATAGRAM_COMPLETION_ROUTINE Complete,
    PVOID Context,
    PIRP Irp)
/*
 * FUNCTION: Attempts to receive an DG datagram from a remote address
 * ARGUMENTS:
 *     Request       = Pointer to TDI request
 *     ConnInfo      = Pointer to connection information
 *     Buffer        = Pointer to NDIS buffer chain to store received data
 *     ReceiveLength = Maximum size to use of buffer, 0 if all can be used
 *     ReceiveFlags  = Receive flags (None, Normal, Peek)
 *     ReturnInfo    = Pointer to structure for return information
 *     BytesReceive  = Pointer to structure for number of bytes received
 * RETURNS:
 *     Status of operation
 * NOTES:
 *     This is the high level interface for receiving DG datagrams
 */
{
    NTSTATUS Status;
    PDATAGRAM_RECEIVE_REQUEST ReceiveRequest;
    PDATAGRAM_QUEUED_DATA Queued;

    TI_DbgPrint(MAX_TRACE, ("Called.\n"));

    LockObject(AddrFile);

    if (!IsListEmpty(&AddrFile->DatagramQueue))
    {
        Queued = CONTAINING_RECORD(RemoveHeadList(&AddrFile->DatagramQueue),
                                   DATAGRAM_QUEUED_DATA, ListEntry);
        AddrFile->QueuedBytes -= Queued->DataSize;
        *BytesReceived = DGCopyDatagram(AddrFile, &Queued->Info, Queued->Data, Queued->DataSize,
                                        BufferData, ReceiveLength, ReturnInfo);
        UnlockObject(AddrFile);

        Status = Queued->DataSize > ReceiveLength ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
        ExFreePoolWithTag(Queued, DATAGRAM_QUEUE_TAG);
        return Status;
    }

    ReceiveRequest = ExAllocatePoolWithTag(NonPagedPool, sizeof(DATAGRAM_RECEIVE_REQUEST),
                                           DATAGRAM_RECV_TAG);
    if (ReceiveRequest)
    {
	/* Initialize a receive request */

	/* Extract the remote address filter from the request (if any) */
	if ((ConnInfo->RemoteAddressLength != 0) &&
	    (ConnInfo->RemoteAddress))
        {
	    Status = AddrGetAddress(ConnInfo->RemoteAddress,
				    &ReceiveRequest->RemoteAddress,
				    &ReceiveRequest->RemotePort);
	    if (!NT_SUCCESS(Status))
            {
		ExFreePoolWithTag(ReceiveRequest, DATAGRAM_RECV_TAG);
	        UnlockObject(AddrFile);
		return Status;
            }
	}
	else
        {
	    ReceiveRequest->RemotePort = 0;
	    AddrInitIPv4(&ReceiveRequest->RemoteAddress, 0);
        }

	IoMarkIrpPending(Irp);

	ReceiveRequest->ReturnInfo = ReturnInfo;
	ReceiveRequest->Buffer = BufferData;
	ReceiveRequest->BufferSize = ReceiveLength;
	ReceiveRequest->UserComplete = Complete;
	ReceiveRequest->UserContext = Context;
	ReceiveRequest->Complete =
		(PDATAGRAM_COMPLETION_ROUTINE)DGReceiveComplete;
	ReceiveRequest->Context = ReceiveRequest;
        ReceiveRequest->AddressFile = AddrFile;
        ReceiveRequest->Irp = Irp;

	/* Queue receive request */
	InsertTailList(&AddrFile->ReceiveQueue, &ReceiveRequest->ListEntry);

	TI_DbgPrint(MAX_TRACE, ("Leaving (pending %08x).\n", ReceiveRequest));

	UnlockObject(AddrFile);

	return STATUS_PENDING;
    }
    else
    {
	UnlockObject(AddrFile);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    TI_DbgPrint(MAX_TRACE, ("Leaving with errors (0x%X).\n", Status));

    return Status;
}
