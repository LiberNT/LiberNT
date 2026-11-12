/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/main.c
 * PURPOSE:     Driver entry point
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

#include <ntifs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/obfuncs.h>

#include <dispatch.h>
#include <fileobjs.h>
#include <tcpip_undoc.h>

PDEVICE_OBJECT TCPDeviceObject   = NULL;
PDEVICE_OBJECT UDPDeviceObject   = NULL;
PDEVICE_OBJECT IPDeviceObject    = NULL;
PDEVICE_OBJECT IP6DeviceObject   = NULL;
PDEVICE_OBJECT RawIPDeviceObject = NULL;
NDIS_HANDLE GlobalPacketPool     = NULL;
NDIS_HANDLE GlobalBufferPool     = NULL;
KSPIN_LOCK EntityListLock;
TDIEntityInfo *EntityList        = NULL;
ULONG EntityCount                = 0;
ULONG EntityMax                  = 0;
UDP_STATISTICS UDPStats;

/* Network timers */
KTIMER IPTimer;
KDPC IPTimeoutDpc;
KTIMER NeighborTimer;
KDPC NeighborTimeoutDpc;

VOID TiWriteErrorLog(
    PDRIVER_OBJECT DriverContext,
    NTSTATUS ErrorCode,
    ULONG UniqueErrorValue,
    NTSTATUS FinalStatus,
    PWSTR String,
    ULONG DumpDataCount,
    PULONG DumpData)
/*
 * FUNCTION: Writes an error log entry
 * ARGUMENTS:
 *     DriverContext    = Pointer to the driver or device object
 *     ErrorCode        = An error code to put in the log entry
 *     UniqueErrorValue = UniqueErrorValue in the error log packet
 *     FinalStatus      = FinalStatus in the error log packet
 *     String           = If not NULL, a pointer to a string to put in log
 *                        entry
 *     DumpDataCount    = Number of ULONGs of dump data
 *     DumpData         = Pointer to dump data for the log entry
 */
{
    PIO_ERROR_LOG_PACKET LogEntry;
    UCHAR EntrySize;
    ULONG StringSize;
    PUCHAR pString;
    static WCHAR DriverName[] = L"TCP/IP";

    EntrySize = sizeof(IO_ERROR_LOG_PACKET) +
        (DumpDataCount * sizeof(ULONG)) + sizeof(DriverName);

    if (String) {
        StringSize = (wcslen(String) * sizeof(WCHAR)) + sizeof(UNICODE_NULL);
        EntrySize += (UCHAR)StringSize;
    }

    /* Fail if the required error log entry is too large */
    if (EntrySize > ERROR_LOG_MAXIMUM_SIZE)
        return;

    LogEntry = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(DriverContext, EntrySize);
    if (!LogEntry)
        return;

    LogEntry->MajorFunctionCode = -1;
    LogEntry->RetryCount        = -1;
    LogEntry->DumpDataSize      = (USHORT)(DumpDataCount * sizeof(ULONG));
    LogEntry->NumberOfStrings   = (String == NULL) ? 1 : 2;
    LogEntry->StringOffset      = sizeof(IO_ERROR_LOG_PACKET) + (DumpDataCount * sizeof(ULONG));
    LogEntry->EventCategory     = 0;
    LogEntry->ErrorCode         = ErrorCode;
    LogEntry->UniqueErrorValue  = UniqueErrorValue;
    LogEntry->FinalStatus       = FinalStatus;
    LogEntry->SequenceNumber    = -1;
    LogEntry->IoControlCode     = 0;

    if (DumpDataCount)
        RtlCopyMemory(LogEntry->DumpData, DumpData, DumpDataCount * sizeof(ULONG));

    pString = ((PUCHAR)LogEntry) + LogEntry->StringOffset;
    RtlCopyMemory(pString, DriverName, sizeof(DriverName));
    pString += sizeof(DriverName);

    if (String)
        RtlCopyMemory(pString, String, StringSize);

    IoWriteErrorLogEntry(LogEntry);
}

static BOOLEAN TiIsDualStackAddress(
    PTA_IP_ADDRESS Address,
    ULONG Length)
{
    PTA_ADDRESS Next;
    ULONG Offset;

    if (Address->Address[0].AddressType != TDI_ADDRESS_TYPE_IP6)
        return FALSE;

    Offset = FIELD_OFFSET(TA_IP_ADDRESS, Address[0].Address) + Address->Address[0].AddressLength;
    if (Length < Offset + FIELD_OFFSET(TA_ADDRESS, Address) + TDI_ADDRESS_LENGTH_IP)
        return FALSE;

    Next = (PTA_ADDRESS)((PUCHAR)Address + Offset);
    return (Next->AddressType == TDI_ADDRESS_TYPE_IP) &&
           (Next->AddressLength >= TDI_ADDRESS_LENGTH_IP) &&
           (((PTDI_ADDRESS_IP)Next->Address)->in_addr == 0);
}

/*
 * FUNCTION: Creates a file object
 * ARGUMENTS:
 *     DeviceObject = Pointer to a device object for this driver
 *     Irp          = Pointer to a I/O request packet
 * RETURNS:
 *     Status of the operation
 */

NTSTATUS TiCreateFileObject(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PFILE_FULL_EA_INFORMATION EaInfo;
    PTRANSPORT_CONTEXT Context;
    PIO_STACK_LOCATION IrpSp;
    PTA_IP_ADDRESS Address;
    TDI_REQUEST Request;
    PVOID ClientContext;
    NTSTATUS Status;
    ULONG Protocol;
    BOOLEAN Shared;
    BOOLEAN Exclusive;

    TI_DbgPrint(DEBUG_IRP, ("Called. DeviceObject is at (0x%X), IRP is at (0x%X).\n", DeviceObject, Irp));

    EaInfo = Irp->AssociatedIrp.SystemBuffer;

    /* Parameter check */
    /* No EA information means that we're opening for SET/QUERY_INFORMATION
    * style calls. */

    /* Allocate resources here. We release them again if something failed */
    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(TRANSPORT_CONTEXT),
                                    TRANS_CONTEXT_TAG);
    if (!Context)
    {
        TI_DbgPrint(MIN_TRACE, ("Insufficient resources.\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->CancelIrps = FALSE;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    IrpSp->FileObject->FsContext = Context;
    Request.RequestContext       = Irp;

    /* Branch to the right handler */
    if (EaInfo &&
        (EaInfo->EaNameLength == TDI_TRANSPORT_ADDRESS_LENGTH) &&
        (RtlCompareMemory(&EaInfo->EaName, TdiTransportAddress,
        TDI_TRANSPORT_ADDRESS_LENGTH) == TDI_TRANSPORT_ADDRESS_LENGTH))
    {
        /* This is a request to open an address */


        /* XXX This should probably be done in IoCreateFile() */
        /* Parameter checks */

        Address = (PTA_IP_ADDRESS)(EaInfo->EaName + EaInfo->EaNameLength + 1); //0-term

        if ((EaInfo->EaValueLength < FIELD_OFFSET(TA_IP_ADDRESS, Address[0].Address)) ||
            ((Address->TAAddressCount != 1) &&
             ((Address->TAAddressCount != 2) || !TiIsDualStackAddress(Address, EaInfo->EaValueLength))) ||
            !(((Address->Address[0].AddressType == TDI_ADDRESS_TYPE_IP) &&
               (EaInfo->EaValueLength >= sizeof(TA_IP_ADDRESS)) &&
               (Address->Address[0].AddressLength >= TDI_ADDRESS_LENGTH_IP)) ||
              ((Address->Address[0].AddressType == TDI_ADDRESS_TYPE_IP6) &&
               (EaInfo->EaValueLength >= sizeof(TA_IP6_ADDRESS)) &&
               (Address->Address[0].AddressLength >= TDI_ADDRESS_LENGTH_IP6))))
        {
            TI_DbgPrint(MIN_TRACE, ("Parameters are invalid:\n"));
            TI_DbgPrint(MIN_TRACE, ("AddressCount: %d\n", Address->TAAddressCount));
            if( Address->TAAddressCount == 1 )
            {
                TI_DbgPrint(MIN_TRACE, ("AddressLength: %u\n",
                            Address->Address[0].AddressLength));
                TI_DbgPrint(MIN_TRACE, ("AddressType: %u\n",
                            Address->Address[0].AddressType));
            }

            ExFreePoolWithTag(Context, TRANS_CONTEXT_TAG);
            return STATUS_INVALID_PARAMETER;
        }

        /* Open address file object */

        /* Protocol depends on device object so find the protocol */
        if (DeviceObject == TCPDeviceObject)
            Protocol = IPPROTO_TCP;
        else if (DeviceObject == UDPDeviceObject)
            Protocol = IPPROTO_UDP;
        else if (DeviceObject == IPDeviceObject)
            Protocol = IPPROTO_RAW;
        else if (DeviceObject == RawIPDeviceObject)
        {
            Status = TiGetProtocolNumber(&IrpSp->FileObject->FileName, &Protocol);
            if (!NT_SUCCESS(Status))
            {
                TI_DbgPrint(MIN_TRACE, ("Raw IP protocol number is invalid.\n"));
                ExFreePoolWithTag(Context, TRANS_CONTEXT_TAG);
                return STATUS_INVALID_PARAMETER;
            }
        }
        else
        {
            TI_DbgPrint(MIN_TRACE, ("Invalid device object at (0x%X).\n", DeviceObject));
            ExFreePoolWithTag(Context, TRANS_CONTEXT_TAG);
            return STATUS_INVALID_PARAMETER;
        }

        Shared = ((IrpSp->Parameters.Create.ShareAccess & FILE_SHARE_WRITE) != 0);
        Exclusive = (IrpSp->Parameters.Create.ShareAccess == 0);

        Status = FileOpenAddress(&Request, Address, Protocol, DeviceObject == RawIPDeviceObject, Shared, Exclusive, NULL);
        if (NT_SUCCESS(Status))
        {
            IrpSp->FileObject->FsContext2 = (PVOID)(ULONG_PTR)TDI_TRANSPORT_ADDRESS_FILE;
            Context->Handle.AddressHandle = Request.Handle.AddressHandle;
        }

    }
    else if (EaInfo &&
            (EaInfo->EaNameLength == TDI_CONNECTION_CONTEXT_LENGTH) &&
            (RtlCompareMemory
            (&EaInfo->EaName, TdiConnectionContext,
            TDI_CONNECTION_CONTEXT_LENGTH) ==
            TDI_CONNECTION_CONTEXT_LENGTH))
    {
        /* This is a request to open a connection endpoint */

        /* Parameter checks */

        if (EaInfo->EaValueLength < sizeof(PVOID))
        {
            TI_DbgPrint(MIN_TRACE, ("Parameters are invalid.\n"));
            ExFreePoolWithTag(Context, TRANS_CONTEXT_TAG);
            return STATUS_INVALID_PARAMETER;
        }

        /* Can only do connection oriented communication using TCP */

        if (DeviceObject != TCPDeviceObject)
        {
            TI_DbgPrint(MIN_TRACE, ("Bad device object.\n"));
            ExFreePoolWithTag(Context, TRANS_CONTEXT_TAG);
            return STATUS_INVALID_PARAMETER;
        }

        ClientContext = *((PVOID*)(EaInfo->EaName + EaInfo->EaNameLength));

        /* Open connection endpoint file object */

        Status = FileOpenConnection(&Request, ClientContext);
        if (NT_SUCCESS(Status))
        {
            IrpSp->FileObject->FsContext2 = (PVOID)(ULONG_PTR)TDI_CONNECTION_FILE;
            Context->Handle.ConnectionContext = Request.Handle.ConnectionContext;
        }
    }
    else
    {
        /* This is a request to open a control connection */
        Status = FileOpenControlChannel(&Request);
        if (NT_SUCCESS(Status))
        {
            IrpSp->FileObject->FsContext2 = (PVOID)(ULONG_PTR)TDI_CONTROL_CHANNEL_FILE;
            Context->Handle.ControlChannel = Request.Handle.ControlChannel;
        }
    }

    if (!NT_SUCCESS(Status))
        ExFreePoolWithTag(Context, TRANS_CONTEXT_TAG);

    TI_DbgPrint(DEBUG_IRP, ("Leaving. Status = (0x%X).\n", Status));

    Irp->IoStatus.Status = Status;
    return Status;
}


/*
 * FUNCTION: Releases resources used by a file object
 * ARGUMENTS:
 *     DeviceObject = Pointer to a device object for this driver
 *     Irp          = Pointer to a I/O request packet
 * RETURNS:
 *     Status of the operation
 * NOTES:
 *     This function does not pend
 */
NTSTATUS TiCloseFileObject(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PTRANSPORT_CONTEXT Context;
    TDI_REQUEST Request;
    NTSTATUS Status;

    IrpSp   = IoGetCurrentIrpStackLocation(Irp);
    Context = IrpSp->FileObject->FsContext;
    if (!Context)
    {
        TI_DbgPrint(MIN_TRACE, ("Parameters are invalid.\n"));
        return STATUS_INVALID_PARAMETER;
    }

    switch ((ULONG_PTR)IrpSp->FileObject->FsContext2)
    {
        case TDI_TRANSPORT_ADDRESS_FILE:
            Request.Handle.AddressHandle = Context->Handle.AddressHandle;
            Status = FileCloseAddress(&Request);
            break;

        case TDI_CONNECTION_FILE:
            Request.Handle.ConnectionContext = Context->Handle.ConnectionContext;
            Status = FileCloseConnection(&Request);
            break;

        case TDI_CONTROL_CHANNEL_FILE:
            IGMPLeaveAllGroups(IrpSp->FileObject);
            LibIP6LeaveAllGroups(IrpSp->FileObject);
            Request.Handle.ControlChannel = Context->Handle.ControlChannel;
            Status = FileCloseControlChannel(&Request);
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    if (NT_SUCCESS(Status))
        ExFreePoolWithTag(Context, TRANS_CONTEXT_TAG);

    Irp->IoStatus.Status = Status;

    return Irp->IoStatus.Status;
}


NTSTATUS NTAPI
TiDispatchOpenClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
/*
 * FUNCTION: Main dispath routine
 * ARGUMENTS:
 *     DeviceObject = Pointer to a device object for this driver
 *     Irp          = Pointer to a I/O request packet
 * RETURNS:
 *     Status of the operation
 */
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

//  DbgPrint("Called. DeviceObject is at (0x%X), IRP is at (0x%X).\n", DeviceObject, Irp);

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MajorFunction) {
    /* Open an address file, connection endpoint, or control connection */
    case IRP_MJ_CREATE:
        Status = TiCreateFileObject(DeviceObject, Irp);
        break;

    /* Close an address file, connection endpoint, or control connection */
    case IRP_MJ_CLOSE:
        Status = TiCloseFileObject(DeviceObject, Irp);
        break;

     default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }

    //DbgPrint("Leaving. Status is (0x%X)\n", Status);

    return IRPFinish( Irp, Status );
}


NTSTATUS NTAPI
TiDispatchInternal(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
/*
 * FUNCTION: Internal IOCTL dispatch routine
 * ARGUMENTS:
 *     DeviceObject = Pointer to a device object for this driver
 *     Irp          = Pointer to a I/O request packet
 * RETURNS:
 *     Status of the operation
 */
{
    NTSTATUS Status;
    BOOLEAN Complete = TRUE;
    PIO_STACK_LOCATION IrpSp;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    TI_DbgPrint(DEBUG_IRP, ("[TCPIP, TiDispatchInternal] Called. DeviceObject is at (0x%X), IRP is at (0x%X) MN (%d).\n",
        DeviceObject, Irp, IrpSp->MinorFunction));

    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    switch (IrpSp->MinorFunction) {
    case TDI_RECEIVE:
        Status = DispTdiReceive(Irp);
        Complete = FALSE;
        break;

    case TDI_RECEIVE_DATAGRAM:
        Status = DispTdiReceiveDatagram(Irp);
        Complete = FALSE;
        break;

    case TDI_SEND:
        Status = DispTdiSend(Irp);
        Complete = FALSE; /* Completed in DispTdiSend */
        break;

    case TDI_SEND_DATAGRAM:
        Status = DispTdiSendDatagram(Irp);
        Complete = FALSE;
        break;

  case TDI_ACCEPT:
    Status = DispTdiAccept(Irp);
    break;

    case TDI_LISTEN:
        Status = DispTdiListen(Irp);
        Complete = FALSE;
        break;

    case TDI_CONNECT:
        Status = DispTdiConnect(Irp);
        Complete = FALSE; /* Completed by the TCP event handler */
        break;

    case TDI_DISCONNECT:
        Status = DispTdiDisconnect(Irp);
        Complete = FALSE;
        break;

    case TDI_ASSOCIATE_ADDRESS:
        Status = DispTdiAssociateAddress(Irp);
        break;

    case TDI_DISASSOCIATE_ADDRESS:
        Status = DispTdiDisassociateAddress(Irp);
        break;

    case TDI_QUERY_INFORMATION:
        Status = DispTdiQueryInformation(DeviceObject, Irp);
        break;

    case TDI_SET_INFORMATION:
        Status = DispTdiSetInformation(Irp);
        break;

    case TDI_SET_EVENT_HANDLER:
        Status = DispTdiSetEventHandler(Irp);
        break;

    case TDI_ACTION:
        Status = STATUS_SUCCESS;
        break;

    /* An unsupported IOCTL code was submitted */
    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }

    TI_DbgPrint(DEBUG_IRP, ("[TCPIP, TiDispatchInternal] Leaving. Status = (0x%X).\n", Status));

    if( Complete )
        IRPFinish( Irp, Status );

    return Status;
}


/**
 * @brief Dispatch routine for IRP_MJ_DEVICE_CONTROL requests
 *
 * @param[in] DeviceObject
 *   Pointer to a device object for this driver
 * @param[in] Irp
 *   Pointer to a I/O request packet
 *
 * @return
 *   Status of the operation
 */
static LIST_ENTRY TiChangeIrps;
static KSPIN_LOCK TiChangeLock;
static ULONG TiChangeGeneration;

static
VOID
NTAPI
TiChangeCancel(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&TiChangeLock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&TiChangeLock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
TiWaitChange(
    PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PULONG Generation = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG) ||
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&TiChangeLock, &OldIrql);
    if (*Generation != TiChangeGeneration)
    {
        *Generation = TiChangeGeneration;
        KeReleaseSpinLock(&TiChangeLock, OldIrql);
        Irp->IoStatus.Information = sizeof(ULONG);
        return STATUS_SUCCESS;
    }
    IoSetCancelRoutine(Irp, TiChangeCancel);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        KeReleaseSpinLock(&TiChangeLock, OldIrql);
        return STATUS_CANCELLED;
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&TiChangeIrps, &Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&TiChangeLock, OldIrql);
    return STATUS_PENDING;
}

static
NTSTATUS
TiChangeMembership(
    PVOID Owner,
    PIP_MEMBERSHIP_REQUEST Request)
{
    PIP_INTERFACE Interface;
    IPv4_RAW_ADDRESS Group;
    ULONG Index;

    if (Request->Family == TDI_ADDRESS_TYPE_IP6)
        return LibIP6ChangeMembership(Owner, Request->Interface, Request->Group, Request->Join != 0);
    if (Request->Family != TDI_ADDRESS_TYPE_IP)
        return STATUS_INVALID_PARAMETER;

    Interface = GetMulticastInterface(Request->Interface);
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    Index = Interface->Index;
    IPDereferenceInterface(Interface);
    RtlCopyMemory(&Group, Request->Group, sizeof(Group));
    return Request->Join ? IGMPJoinGroup(Owner, Index, Group) : IGMPLeaveGroup(Owner, Index, Group);
}

VOID
TiSignalChange(VOID)
{
    LIST_ENTRY Completed;
    PLIST_ENTRY Entry, Next;
    KIRQL OldIrql;

    InitializeListHead(&Completed);
    KeAcquireSpinLock(&TiChangeLock, &OldIrql);
    TiChangeGeneration++;
    for (Entry = TiChangeIrps.Flink; Entry != &TiChangeIrps; Entry = Next)
    {
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        Next = Entry->Flink;
        if (!IoSetCancelRoutine(Irp, NULL))
            continue;
        RemoveEntryList(Entry);
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = TiChangeGeneration;
        InsertTailList(&Completed, Entry);
    }
    KeReleaseSpinLock(&TiChangeLock, OldIrql);

    while (!IsListEmpty(&Completed))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Completed), IRP, Tail.Overlay.ListEntry);

        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = sizeof(ULONG);
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    }
}

NTSTATUS NTAPI
TiDispatch(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    NTSTATUS Status;
    PIO_STACK_LOCATION IrpSp;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    TI_DbgPrint(DEBUG_IRP, ("[TCPIP, TiDispatch] Called. IRP is at (0x%X).\n", Irp));

    Irp->IoStatus.Information = 0;

    if (Irp->RequestorMode == UserMode &&
        (ULONG_PTR)IrpSp->FileObject->FsContext2 != TDI_CONTROL_CHANNEL_FILE &&
        IrpSp->Parameters.DeviceIoControl.IoControlCode != IOCTL_TCP_QUERY_INFORMATION_EX &&
        IrpSp->Parameters.DeviceIoControl.IoControlCode != IOCTL_TCP_SET_INFORMATION_EX)
    {
        return IRPFinish(Irp, STATUS_ACCESS_DENIED);
    }

#if 0
    Status = TdiMapUserRequest(DeviceObject, Irp, IrpSp);
    if (NT_SUCCESS(Status))
    {
        TiDispatchInternal(DeviceObject, Irp);
        Status = STATUS_PENDING;
    }
    else
    {
#else
    if (TRUE) {
#endif
        /* See if this request is TCP/IP specific */
        switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_TCP_QUERY_INFORMATION_EX:
                TI_DbgPrint(MIN_TRACE, ("TCP_QUERY_INFORMATION_EX\n"));
                Status = DispTdiQueryInformationEx(Irp, IrpSp);
                break;

            case IOCTL_TCP_SET_INFORMATION_EX:
                TI_DbgPrint(MIN_TRACE, ("TCP_SET_INFORMATION_EX\n"));
                Status = DispTdiSetInformationEx(Irp, IrpSp);
                break;

            case IOCTL_SET_IP_ADDRESS:
                TI_DbgPrint(MIN_TRACE, ("SET_IP_ADDRESS\n"));
                Status = DispTdiSetIPAddress(Irp, IrpSp);
                break;

            case IOCTL_DELETE_IP_ADDRESS:
                TI_DbgPrint(MIN_TRACE, ("DELETE_IP_ADDRESS\n"));
                Status = DispTdiDeleteIPAddress(Irp, IrpSp);
                break;

            case IOCTL_IP_GET_BEST_SOURCE:
                Status = DispTdiGetBestSource(Irp, IrpSp);
                break;

            case IOCTL_IP_RESOLVE_NEIGHBOR:
                TI_DbgPrint(MIN_TRACE, ("QUERY_IP_HW_ADDRESS\n"));
                Status = DispTdiQueryIpHwAddress(DeviceObject, Irp, IrpSp);
                break;

            case IOCTL_ICMP_ECHO_REQUEST:
                TI_DbgPrint(MIN_TRACE, ("ICMP_ECHO_REQUEST\n"));
                if (DeviceObject == IP6DeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else
                    Status = DispEchoRequest(DeviceObject, Irp, IrpSp);
                break;

            case IOCTL_ICMPV6_ECHO_REQUEST:
                if (DeviceObject == IP6DeviceObject)
                    Status = LibICMP6EchoRequest(DeviceObject, Irp, IrpSp);
                else
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                break;

            case IOCTL_IP_INTERFACE_INFO:
                Status = STATUS_INVALID_PARAMETER;
                break;

            case IOCTL_IP6_SET_ADDRESS:
                if (DeviceObject != IP6DeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP6_ADDRESS_SET))
                    Status = STATUS_INVALID_PARAMETER;
                else
                    Status = LibIP6SetAddress(Irp->AssociatedIrp.SystemBuffer);
                break;

            case IOCTL_IP6_SET_ROUTE:
                if (DeviceObject != IP6DeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP6_ROUTE_SET))
                    Status = STATUS_INVALID_PARAMETER;
                else
                    Status = LibIP6SetRoute(Irp->AssociatedIrp.SystemBuffer);
                break;

            case IOCTL_IP6_SET_INTERFACE:
                if (DeviceObject != IP6DeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP6_INTERFACE_SET))
                    Status = STATUS_INVALID_PARAMETER;
                else
                    Status = LibIP6SetInterface(Irp->AssociatedIrp.SystemBuffer);
                break;

            case IOCTL_IP6_SET_NEIGHBOR:
                if (DeviceObject != IP6DeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP6_NEIGHBOR_SET))
                    Status = STATUS_INVALID_PARAMETER;
                else
                    Status = LibIP6SetNeighbor(Irp->AssociatedIrp.SystemBuffer);
                break;

            case IOCTL_IP_QUERY_INTERFACE:
            case IOCTL_IP_SET_INTERFACE:
                if (DeviceObject != IPDeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP4_INTERFACE_INFO) ||
                         (IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_IP_QUERY_INTERFACE &&
                          IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(IP4_INTERFACE_INFO)))
                    Status = STATUS_INVALID_PARAMETER;
                else if (IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_IP_SET_INTERFACE)
                    Status = IPSetInterfaceV4(Irp->AssociatedIrp.SystemBuffer);
                else
                {
                    Status = IPQueryInterfaceV4(Irp->AssociatedIrp.SystemBuffer);
                    if (NT_SUCCESS(Status))
                        Irp->IoStatus.Information = sizeof(IP4_INTERFACE_INFO);
                }
                break;

            case IOCTL_IP_WAIT_CHANGE:
                if (DeviceObject != IPDeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else
                    Status = TiWaitChange(Irp, IrpSp);
                break;

            case IOCTL_IP_CHANGE_MEMBERSHIP:
                if (DeviceObject != IPDeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP_MEMBERSHIP_REQUEST))
                    Status = STATUS_INVALID_PARAMETER;
                else
                    Status = TiChangeMembership(IrpSp->FileObject, Irp->AssociatedIrp.SystemBuffer);
                break;

            case IOCTL_IP_QUERY_PATHS:
                if (DeviceObject == IPDeviceObject)
                    Status = PathQuery(Irp->AssociatedIrp.SystemBuffer,
                                       IrpSp->Parameters.DeviceIoControl.OutputBufferLength,
                                       (PULONG)&Irp->IoStatus.Information);
                else if (DeviceObject == IP6DeviceObject)
                    Status = LibIP6QueryPaths(Irp->AssociatedIrp.SystemBuffer,
                                              IrpSp->Parameters.DeviceIoControl.OutputBufferLength,
                                              (PULONG)&Irp->IoStatus.Information);
                else
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                break;

            case IOCTL_IP_FLUSH_PATHS:
                if (DeviceObject == IPDeviceObject)
                {
                    PathFlush();
                    Status = STATUS_SUCCESS;
                }
                else if (DeviceObject == IP6DeviceObject)
                    Status = LibIP6FlushPaths();
                else
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                break;

            case IOCTL_IP_QUERY_ROUTE:
                if (DeviceObject != IPDeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP4_ROUTE_INFO) ||
                         IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(IP4_ROUTE_INFO))
                    Status = STATUS_INVALID_PARAMETER;
                else
                {
                    Status = RouterQueryRouteV4(Irp->AssociatedIrp.SystemBuffer);
                    if (NT_SUCCESS(Status))
                        Irp->IoStatus.Information = sizeof(IP4_ROUTE_INFO);
                }
                break;

            case IOCTL_IP_SET_ROUTE:
                if (DeviceObject != IPDeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP4_ROUTE_INFO))
                    Status = STATUS_INVALID_PARAMETER;
                else
                    Status = RouterSetRouteV4(Irp->AssociatedIrp.SystemBuffer);
                break;

            case IOCTL_IP_SET_ADDRESS:
                if (DeviceObject != IPDeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP4_ADDRESS_INFO) ||
                         IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(IP4_ADDRESS_INFO))
                    Status = STATUS_INVALID_PARAMETER;
                else
                {
                    Status = IPSetAddressV4(Irp->AssociatedIrp.SystemBuffer);
                    if (NT_SUCCESS(Status))
                        Irp->IoStatus.Information = sizeof(IP4_ADDRESS_INFO);
                }
                break;

            case IOCTL_IP_QUERY_ADDRESS:
                if (DeviceObject != IPDeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP4_ADDRESS_INFO) ||
                         IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(IP4_ADDRESS_INFO))
                    Status = STATUS_INVALID_PARAMETER;
                else
                {
                    Status = IPQueryAddressV4(Irp->AssociatedIrp.SystemBuffer);
                    if (NT_SUCCESS(Status))
                        Irp->IoStatus.Information = sizeof(IP4_ADDRESS_INFO);
                }
                break;

            case IOCTL_IP6_GET_BEST_ROUTE:
                if (DeviceObject != IP6DeviceObject)
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                else if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(IP6_BEST_ROUTE) ||
                         IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(IP6_BEST_ROUTE))
                    Status = STATUS_INVALID_PARAMETER;
                else
                {
                    Status = LibIP6GetBestRoute(Irp->AssociatedIrp.SystemBuffer);
                    if (NT_SUCCESS(Status))
                        Irp->IoStatus.Information = sizeof(IP6_BEST_ROUTE);
                }
                break;

            case IOCTL_IP6_QUERY_TABLE:
            {
                ULONG TableId;

                if (DeviceObject != IP6DeviceObject)
                {
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
                }
                if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG))
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                TableId = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
                Status = InfoIp6QueryTable(TableId,
                                           Irp->AssociatedIrp.SystemBuffer,
                                           IrpSp->Parameters.DeviceIoControl.OutputBufferLength,
                                           (PULONG)&Irp->IoStatus.Information);
                break;
            }

            default:
                TI_DbgPrint(MIN_TRACE, ("Unknown IOCTL 0x%X\n",
                    IrpSp->Parameters.DeviceIoControl.IoControlCode));
                Status = STATUS_NOT_IMPLEMENTED;
                break;
        }
    }

    TI_DbgPrint(DEBUG_IRP, ("[TCPIP, TiDispatch] Leaving. Status = (0x%X).\n", Status));

    if (Status == STATUS_SUCCESS)
    {
        switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_IP6_SET_ADDRESS:
            case IOCTL_IP6_SET_ROUTE:
            case IOCTL_IP6_SET_INTERFACE:
            case IOCTL_IP6_SET_NEIGHBOR:
            case IOCTL_IP_SET_INTERFACE:
                TiSignalChange();
                break;
        }
    }

    return IRPFinish(Irp, Status);
}

static
NTSTATUS TiCreateSecurityDescriptor(
    _In_ ACCESS_MASK WorldAccess,
    _In_ ACCESS_MASK RestrictedAccess,
    _Out_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    NTSTATUS Status;
    SECURITY_DESCRIPTOR AbsSD;
    ULONG DaclSize, RelSDSize = 0;
    PSECURITY_DESCRIPTOR RelSD = NULL;
    PACL Dacl = NULL;

    /* Setup a SD */
    Status = RtlCreateSecurityDescriptor(&AbsSD,
                                         SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to create the absolute SD (0x%X)\n", Status));
        goto Quit;
    }

    /* Setup a DACL */
    DaclSize = sizeof(ACL) +
               sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(SeExports->SeWorldSid) +
               sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(SeExports->SeLocalSystemSid) +
               sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(SeExports->SeAliasAdminsSid) +
               sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(SeExports->SeRestrictedSid);
    Dacl = ExAllocatePoolWithTag(PagedPool,
                                 DaclSize,
                                 DEVICE_OBJ_SECURITY_TAG);
    if (Dacl == NULL)
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to allocate buffer heap for a DACL\n"));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }

    Status = RtlCreateAcl(Dacl,
                          DaclSize,
                          ACL_REVISION);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to create a DACL (0x%X)\n", Status));
        goto Quit;
    }

    /* Setup access */
    Status = RtlAddAccessAllowedAce(Dacl,
                                    ACL_REVISION,
                                    WorldAccess,
                                    SeExports->SeWorldSid);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to add access allowed ACE for World SID (0x%X)\n", Status));
        goto Quit;
    }

    Status = RtlAddAccessAllowedAce(Dacl,
                                    ACL_REVISION,
                                    GENERIC_ALL,
                                    SeExports->SeLocalSystemSid);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to add access allowed ACE for System SID (0x%X)\n", Status));
        goto Quit;
    }

    Status = RtlAddAccessAllowedAce(Dacl,
                                    ACL_REVISION,
                                    GENERIC_ALL,
                                    SeExports->SeAliasAdminsSid);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to add access allowed ACE for Admins SID (0x%X)\n", Status));
        goto Quit;
    }

    Status = RtlAddAccessAllowedAce(Dacl,
                                    ACL_REVISION,
                                    RestrictedAccess,
                                    SeExports->SeRestrictedSid);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to add access allowed ACE for Restricted SID (0x%X)\n", Status));
        goto Quit;
    }

    /* Assign security data to SD */
    Status = RtlSetDaclSecurityDescriptor(&AbsSD,
                                          TRUE,
                                          Dacl,
                                          FALSE);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to set DACL to security descriptor (0x%X)\n", Status));
        goto Quit;
    }

    Status = RtlSetGroupSecurityDescriptor(&AbsSD,
                                           SeExports->SeLocalSystemSid,
                                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to set group to security descriptor (0x%X)\n", Status));
        goto Quit;
    }

    Status = RtlSetOwnerSecurityDescriptor(&AbsSD,
                                           SeExports->SeAliasAdminsSid,
                                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to set owner to security descriptor (0x%X)\n", Status));
        goto Quit;
    }

    /* Get the required buffer size for the self-relative SD */
    Status = RtlAbsoluteToSelfRelativeSD(&AbsSD,
                                         NULL,
                                         &RelSDSize);
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        TI_DbgPrint(MIN_TRACE, ("Expected STATUS_BUFFER_TOO_SMALL but got something else (0x%X)\n", Status));
        goto Quit;
    }

    RelSD = ExAllocatePoolWithTag(PagedPool,
                                  RelSDSize,
                                  DEVICE_OBJ_SECURITY_TAG);
    if (RelSD == NULL)
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to allocate buffer heap for relative SD\n"));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }

    /* Convert it now */
    Status = RtlAbsoluteToSelfRelativeSD(&AbsSD,
                                         RelSD,
                                         &RelSDSize);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to convert absolute SD into a relative SD (0x%X)\n", Status));
        goto Quit;
    }

    /* Give the buffer to caller */
    *SecurityDescriptor = RelSD;

Quit:
    if (!NT_SUCCESS(Status))
    {
        if (RelSD != NULL)
        {
            ExFreePoolWithTag(RelSD, DEVICE_OBJ_SECURITY_TAG);
        }
    }

    if (Dacl != NULL)
    {
        ExFreePoolWithTag(Dacl, DEVICE_OBJ_SECURITY_TAG);
    }

    return Status;
}

static
NTSTATUS TiSetupTcpDeviceSD(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ACCESS_MASK WorldAccess,
    _In_ ACCESS_MASK RestrictedAccess)
{
    NTSTATUS Status;
    PSECURITY_DESCRIPTOR Sd = NULL;
    SECURITY_INFORMATION Info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;

    /* Obtain a security descriptor */
    Status = TiCreateSecurityDescriptor(WorldAccess, RestrictedAccess, &Sd);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to create a security descriptor for the device object\n"));
        return Status;
    }

    /* Whack the new descriptor into the TCP device object */
    Status = ObSetSecurityObjectByPointer(DeviceObject,
                                          Info,
                                          Sd);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to set new security information to the device object\n"));
    }

    ExFreePoolWithTag(Sd, DEVICE_OBJ_SECURITY_TAG);
    return Status;
}

static
NTSTATUS TiSecurityStartup(
    VOID)
{
    NTSTATUS Status;

    /* Set security data for the TCP and IP device objects */
    Status = TiSetupTcpDeviceSD(TCPDeviceObject,
                                FILE_GENERIC_EXECUTE,
                                FILE_GENERIC_EXECUTE);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to set security data for TCP device object\n"));
        return Status;
    }

    Status = TiSetupTcpDeviceSD(IPDeviceObject,
                                FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE,
                                FILE_GENERIC_READ | FILE_GENERIC_EXECUTE);
    if (!NT_SUCCESS(Status))
    {
        TI_DbgPrint(MIN_TRACE, ("Failed to set security data for IP device object\n"));
        return Status;
    }

    return STATUS_SUCCESS;
}


VOID NTAPI TiUnload(
  PDRIVER_OBJECT DriverObject)
/*
 * FUNCTION: Unloads the driver
 * ARGUMENTS:
 *     DriverObject = Pointer to driver object created by the system
 */
{
#if DBG
  KIRQL OldIrql;

    TcpipAcquireSpinLock(&AddressFileListLock, &OldIrql);
    if (!IsListEmpty(&AddressFileListHead)) {
        TI_DbgPrint(MIN_TRACE, ("[TCPIP, TiUnload] Called. Open address file objects exists.\n"));
    }
    TcpipReleaseSpinLock(&AddressFileListLock, OldIrql);
#endif
    /* Cancel timer */
    KeCancelTimer(&IPTimer);
    KeCancelTimer(&NeighborTimer);

    /* Unregister loopback adapter */
    LoopUnregisterAdapter(NULL);

    /* Unregister protocol with NDIS */
    LANUnregisterProtocol();

    /* Shutdown transport level protocol subsystems */
    TCPShutdown();
    UDPShutdown();
    RawIPShutdown();
    ICMPShutdown();
    IGMPShutdown();

    /* Shutdown network level protocol subsystem */
    IPShutdown();

    /* Free NDIS buffer descriptors */
    if (GlobalBufferPool)
        NdisFreeBufferPool(GlobalBufferPool);

    /* Free NDIS packet descriptors */
    if (GlobalPacketPool)
        NdisFreePacketPool(GlobalPacketPool);

    /* Release all device objects */

    if (TCPDeviceObject)
    {
        UNICODE_STRING strTcpLinkName;
        UNICODE_STRING strTcp6DeviceName;

        RtlInitUnicodeString(&strTcpLinkName, L"\\DosDevices\\Tcp");
        RtlInitUnicodeString(&strTcp6DeviceName, DD_TCPV6_DEVICE_NAME);
        IoDeleteSymbolicLink(&strTcpLinkName);
        IoDeleteSymbolicLink(&strTcp6DeviceName);
        IoDeleteDevice(TCPDeviceObject);
    }

    if (UDPDeviceObject)
    {
        UNICODE_STRING strUdp6DeviceName;

        RtlInitUnicodeString(&strUdp6DeviceName, DD_UDPV6_DEVICE_NAME);
        IoDeleteSymbolicLink(&strUdp6DeviceName);
        IoDeleteDevice(UDPDeviceObject);
    }

    if (RawIPDeviceObject)
    {
        UNICODE_STRING strRaw6DeviceName;

        RtlInitUnicodeString(&strRaw6DeviceName, DD_RAWIPV6_DEVICE_NAME);
        IoDeleteSymbolicLink(&strRaw6DeviceName);
        IoDeleteDevice(RawIPDeviceObject);
    }

    if (IP6DeviceObject)
        IoDeleteDevice(IP6DeviceObject);

    if (IPDeviceObject) {
        ChewShutdown();
        IoDeleteDevice(IPDeviceObject);
    }

    if (EntityList)
        ExFreePoolWithTag(EntityList, TDI_ENTITY_TAG);

    TI_DbgPrint(MAX_TRACE, ("[TCPIP, TiUnload] Leaving.\n"));
}

NTSTATUS NTAPI
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
/*
 * FUNCTION: Main driver entry point
 * ARGUMENTS:
 *     DriverObject = Pointer to a driver object for this driver
 *     RegistryPath = Registry node for configuration parameters
 * RETURNS:
 *     Status of driver initialization
 */
{
    NTSTATUS Status;
    UNICODE_STRING strIpDeviceName;
    UNICODE_STRING strIp6DeviceName;
    UNICODE_STRING strRawDeviceName;
    UNICODE_STRING strUdpDeviceName;
    UNICODE_STRING strTcpDeviceName;
    UNICODE_STRING strTcpLinkName;
    UNICODE_STRING strTcp6DeviceName;
    UNICODE_STRING strUdp6DeviceName;
    UNICODE_STRING strRaw6DeviceName;
    UNICODE_STRING strNdisDeviceName;
    NDIS_STATUS NdisStatus;
    LARGE_INTEGER DueTime;

    RtlInitUnicodeString(&strIpDeviceName, DD_IP_DEVICE_NAME);
    RtlInitUnicodeString(&strIp6DeviceName, DD_IPV6_DEVICE_NAME);
    RtlInitUnicodeString(&strRawDeviceName, DD_RAWIP_DEVICE_NAME);
    RtlInitUnicodeString(&strUdpDeviceName, DD_UDP_DEVICE_NAME);
    RtlInitUnicodeString(&strTcpDeviceName, DD_TCP_DEVICE_NAME);
    RtlInitUnicodeString(&strTcpLinkName, L"\\DosDevices\\Tcp");
    RtlInitUnicodeString(&strTcp6DeviceName, DD_TCPV6_DEVICE_NAME);
    RtlInitUnicodeString(&strUdp6DeviceName, DD_UDPV6_DEVICE_NAME);
    RtlInitUnicodeString(&strRaw6DeviceName, DD_RAWIPV6_DEVICE_NAME);
    RtlInitUnicodeString(&strNdisDeviceName, TCPIP_PROTOCOL_NAME);
    InitializeListHead(&TiChangeIrps);
    KeInitializeSpinLock(&TiChangeLock);

    TI_DbgPrint(MAX_TRACE, ("[TCPIP, DriverEntry] Called\n"));

    /* TdiInitialize() ? */

    /* FIXME: Create symbolic links in Win32 namespace */

    /* Initialize our periodic timer and its associated DPC object. When the
        timer expires, the IPTimeout deferred procedure call (DPC) is queued */
    KeInitializeDpc(&IPTimeoutDpc, IPTimeoutDpcFn, NULL);
    KeInitializeTimer(&IPTimer);
    KeInitializeDpc(&NeighborTimeoutDpc, NeighborTimeoutDpcFn, NULL);
    KeInitializeTimer(&NeighborTimer);

    /* Create IP device object */
    Status = IoCreateDevice(DriverObject, 0, &strIpDeviceName,
        FILE_DEVICE_NETWORK, 0, FALSE, &IPDeviceObject);
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Failed to create IP device object. Status (0x%X).\n", Status));
        TiUnload(DriverObject);
        return Status;
    }

    ChewInit( IPDeviceObject );

    Status = IoCreateDevice(DriverObject, 0, &strIp6DeviceName,
        FILE_DEVICE_NETWORK, 0, FALSE, &IP6DeviceObject);
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Failed to create IPv6 device object. Status (0x%X).\n", Status));
        TiUnload(DriverObject);
        return Status;
    }

    /* Create RawIP device object */
    Status = IoCreateDevice(DriverObject, 0, &strRawDeviceName,
        FILE_DEVICE_NETWORK, 0, FALSE, &RawIPDeviceObject);
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Failed to create RawIP device object. Status (0x%X).\n", Status));
        TiUnload(DriverObject);
        return Status;
    }

    /* Create UDP device object */
    Status = IoCreateDevice(DriverObject, 0, &strUdpDeviceName,
        FILE_DEVICE_NETWORK, 0, FALSE, &UDPDeviceObject);
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Failed to create UDP device object. Status (0x%X).\n", Status));
        TiUnload(DriverObject);
        return Status;
    }

    /* Create TCP device object */
    Status = IoCreateDevice(DriverObject, 0, &strTcpDeviceName,
        FILE_DEVICE_NETWORK, 0, FALSE, &TCPDeviceObject);
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Failed to create TCP device object. Status (0x%X).\n", Status));
        TiUnload(DriverObject);
        return Status;
    }

    Status = IoCreateSymbolicLink(&strTcpLinkName, &strTcpDeviceName);
    if (!NT_SUCCESS(Status))
        TI_DbgPrint(MIN_TRACE, ("Failed to create the Tcp symbolic link. Status (0x%X).\n", Status));

    Status = IoCreateSymbolicLink(&strTcp6DeviceName, &strTcpDeviceName);
    if (!NT_SUCCESS(Status))
        TI_DbgPrint(MIN_TRACE, ("Failed to create the Tcp6 device link. Status (0x%X).\n", Status));

    Status = IoCreateSymbolicLink(&strUdp6DeviceName, &strUdpDeviceName);
    if (!NT_SUCCESS(Status))
        TI_DbgPrint(MIN_TRACE, ("Failed to create the Udp6 device link. Status (0x%X).\n", Status));

    Status = IoCreateSymbolicLink(&strRaw6DeviceName, &strRawDeviceName);
    if (!NT_SUCCESS(Status))
        TI_DbgPrint(MIN_TRACE, ("Failed to create the RawIp6 device link. Status (0x%X).\n", Status));

    /* Setup network layer and transport layer entities */
    KeInitializeSpinLock(&EntityListLock);
    EntityList = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(TDIEntityID) * MAX_TDI_ENTITIES,
                                       TDI_ENTITY_TAG );
    if (!EntityList) {
        TI_DbgPrint(MIN_TRACE, ("Insufficient resources.\n"));
        TiUnload(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    EntityCount = 0;
    EntityMax   = MAX_TDI_ENTITIES;

    /* Allocate NDIS packet descriptors */
    NdisAllocatePacketPoolEx(&NdisStatus, &GlobalPacketPool, 500, 1500, sizeof(PACKET_CONTEXT));
    if (NdisStatus != NDIS_STATUS_SUCCESS) {
        TiUnload(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Allocate NDIS buffer descriptors */
    NdisAllocateBufferPool(&NdisStatus, &GlobalBufferPool, 2000);
    if (NdisStatus != NDIS_STATUS_SUCCESS) {
        TiUnload(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize address file list and protecting spin lock */
    InitializeListHead(&AddressFileListHead);
    KeInitializeSpinLock(&AddressFileListLock);

    /* Initialize connection endpoint list and protecting spin lock */
    InitializeListHead(&ConnectionEndpointListHead);
    KeInitializeSpinLock(&ConnectionEndpointListLock);

    /* Initialize interface list and protecting spin lock */
    InitializeListHead(&InterfaceListHead);
    KeInitializeSpinLock(&InterfaceListLock);

    /* Initialize network level protocol subsystem */
    IPStartup(RegistryPath);

    /* Initialize transport level protocol subsystems */
    Status = RawIPStartup();
    if( !NT_SUCCESS(Status) ) {
        TiUnload(DriverObject);
        return Status;
    }

    Status = UDPStartup();
    if( !NT_SUCCESS(Status) ) {
        TiUnload(DriverObject);
        return Status;
    }

    Status = TCPStartup();
    if( !NT_SUCCESS(Status) ) {
        TiUnload(DriverObject);
        return Status;
    }

    Status = ICMPStartup();
    if( !NT_SUCCESS(Status) ) {
        TiUnload(DriverObject);
        return Status;
    }

    Status = IGMPStartup();
    if( !NT_SUCCESS(Status) ) {
        TiUnload(DriverObject);
        return Status;
    }

    /* Initialize security */
    Status = TiSecurityStartup();
    if (!NT_SUCCESS(Status))
    {
        TiUnload(DriverObject);
        return Status;
    }

    /* Use direct I/O */
    IPDeviceObject->Flags    |= DO_DIRECT_IO;
    RawIPDeviceObject->Flags |= DO_DIRECT_IO;
    UDPDeviceObject->Flags   |= DO_DIRECT_IO;
    TCPDeviceObject->Flags   |= DO_DIRECT_IO;

    /* Initialize the driver object with this driver's entry points */
    DriverObject->MajorFunction[IRP_MJ_CREATE]  = TiDispatchOpenClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]   = TiDispatchOpenClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = TiDispatchInternal;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = TiDispatch;

    DriverObject->DriverUnload = TiUnload;

    /* Open loopback adapter */
    Status = LoopRegisterAdapter(NULL, NULL);
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Failed to create loopback adapter. Status (0x%X).\n", Status));
        TiUnload(DriverObject);
        return Status;
    }

    /* Register protocol with NDIS */
    /* This used to be IP_DEVICE_NAME but the DDK says it has to match your entry in the SCM */
    Status = LANRegisterProtocol(&strNdisDeviceName);
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE,("Failed to register protocol with NDIS; status 0x%x\n", Status));
        TiWriteErrorLog(
        DriverObject,
        EVENT_TRANSPORT_REGISTER_FAILED,
        TI_ERROR_DRIVERENTRY,
        Status,
        NULL,
        0,
        NULL);
        TiUnload(DriverObject);
        return Status;
    }

    /* Start the periodic timer with an initial and periodic
        relative expiration time of IP_TIMEOUT milliseconds */
    DueTime.QuadPart = -(LONGLONG)IP_TIMEOUT * 10000;
    KeSetTimerEx(&IPTimer, DueTime, IP_TIMEOUT, &IPTimeoutDpc);
    DueTime.QuadPart = -(LONGLONG)NB_FAST_TIMEOUT * 10000;
    KeSetTimerEx(&NeighborTimer, DueTime, NB_FAST_TIMEOUT, &NeighborTimeoutDpc);

    TI_DbgPrint(MAX_TRACE, ("[TCPIP, DriverEntry] Finished\n"));


    return STATUS_SUCCESS;
}

VOID NTAPI
IPAddInterface(
    ULONG   Unknown0,
    ULONG   Unknown1,
    ULONG   Unknown2,
    ULONG   Unknown3,
    ULONG   Unknown4)
{
    UNIMPLEMENTED;
}


VOID NTAPI
IPDelInterface(
    ULONG   Unknown0)
{
    UNIMPLEMENTED;
}


VOID NTAPI
LookupRoute(
    ULONG   Unknown0,
    ULONG   Unknown1)
{
    UNIMPLEMENTED;
}

/* EOF */
