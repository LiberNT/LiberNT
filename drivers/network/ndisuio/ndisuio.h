/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS User I/O driver
 * FILE:        ndisuio.h
 * PURPOSE:     NDISUIO definitions
 */

#ifndef __NDISUIO_H
#define __NDISUIO_H

#include <ndis.h>
#include <nuiouser.h>

extern PDEVICE_OBJECT GlobalDeviceObject;
extern NDIS_HANDLE GlobalProtocolHandle;
extern LIST_ENTRY GlobalAdapterList;
extern KSPIN_LOCK GlobalAdapterListLock;

typedef struct _NDISUIO_ADAPTER_CONTEXT
{
    /* Asynchronous completion */
    NDIS_STATUS AsyncStatus;
    KEVENT AsyncEvent;

    /* NDIS binding information */
    NDIS_HANDLE BindingHandle;

    /* Reference count information */
    ULONG OpenCount;
    LIST_ENTRY OpenEntryList;

    /* NDIS pools */
    NDIS_HANDLE NblPoolHandle;

    /* Receive packet list */
    LIST_ENTRY PacketList;
    KEVENT PacketReadEvent;

    BOOLEAN Paused;
    LONG PendingSends;
    KEVENT SendsDoneEvent;

    /* Device name */
    UNICODE_STRING DeviceName;

    /* Global list entry */
    LIST_ENTRY ListEntry;

    /* Spin lock */
    KSPIN_LOCK Spinlock;
} NDISUIO_ADAPTER_CONTEXT, *PNDISUIO_ADAPTER_CONTEXT;

typedef struct _NDISUIO_OPEN_ENTRY
{
    /* File object */
    PFILE_OBJECT FileObject;

    /* Tracks how this adapter was opened (write-only or read-write) */
    BOOLEAN WriteOnly;

    /* List entry */
    LIST_ENTRY ListEntry;
} NDISUIO_OPEN_ENTRY, *PNDISUIO_OPEN_ENTRY;

typedef struct _NDISUIO_PACKET_ENTRY
{
    /* Length of data at the end of the struct */
    ULONG PacketLength;

    /* Entry on the packet list */
    LIST_ENTRY ListEntry;

    /* Packet data */
    UCHAR PacketData[1];
} NDISUIO_PACKET_ENTRY, *PNDISUIO_PACKET_ENTRY;

typedef struct _NDISUIO_REQUEST
{
    NDIS_OID_REQUEST Request;
    KEVENT Event;
    NDIS_STATUS Status;
} NDISUIO_REQUEST, *PNDISUIO_REQUEST;

/* NDIS version info */
#define NDIS_MAJOR_VERSION 6
#define NDIS_MINOR_VERSION 0

/* createclose.c */
NTSTATUS
NTAPI
NduDispatchCreate(PDEVICE_OBJECT DeviceObject,
                  PIRP Irp);

NTSTATUS
NTAPI
NduDispatchClose(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp);

/* ioctl.c */
NTSTATUS
NTAPI
NduDispatchDeviceControl(PDEVICE_OBJECT DeviceObject,
                         PIRP Irp);

/* misc.c */
NDIS_STATUS
NduDoRequest(PNDISUIO_ADAPTER_CONTEXT AdapterContext,
             PNDISUIO_REQUEST Request);

PNDISUIO_ADAPTER_CONTEXT
FindAdapterContextByName(PNDIS_STRING DeviceName);

VOID
ReferenceAdapterContext(PNDISUIO_ADAPTER_CONTEXT AdapterContext);

VOID
DereferenceAdapterContextWithOpenEntry(PNDISUIO_ADAPTER_CONTEXT AdapterContext,
                                       PNDISUIO_OPEN_ENTRY OpenEntry);

/* protocol.c */
PROTOCOL_OPEN_ADAPTER_COMPLETE_EX NduOpenAdapterComplete;
PROTOCOL_CLOSE_ADAPTER_COMPLETE_EX NduCloseAdapterComplete;
PROTOCOL_NET_PNP_EVENT NduNetPnPEvent;
PROTOCOL_SEND_NET_BUFFER_LISTS_COMPLETE NduSendComplete;
PROTOCOL_OID_REQUEST_COMPLETE NduRequestComplete;
PROTOCOL_RECEIVE_NET_BUFFER_LISTS NduReceive;
PROTOCOL_STATUS_EX NduStatus;
PROTOCOL_BIND_ADAPTER_EX NduBindAdapter;
PROTOCOL_UNBIND_ADAPTER_EX NduUnbindAdapter;

/* readwrite.c */
NTSTATUS
NTAPI
NduDispatchRead(PDEVICE_OBJECT DeviceObject,
                PIRP Irp);

NTSTATUS
NTAPI
NduDispatchWrite(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp);

#endif /* __NDISUIO_H */
