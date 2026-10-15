/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        include/udp.h
 * PURPOSE:     User Datagram Protocol definitions
 */

#pragma once

#define UDP_STARTING_PORT 0x8000
#define UDP_DYNAMIC_PORTS 0x8000

/* UDPv4 header structure */
#pragma pack(push, 1)
typedef struct UDP_HEADER {
  USHORT SourcePort; /* Source port */
  USHORT DestPort;   /* Destination port */
  USHORT Length;     /* Size of header and data */
  USHORT Checksum;   /* Checksum of datagram */
} UDP_HEADER, *PUDP_HEADER;

/* UDPv4 pseudo header */
typedef struct UDP_PSEUDO_HEADER {
  ULONG SourceAddress; /* Source address */
  ULONG DestAddress;   /* Destination address */
  UCHAR Zero;          /* Reserved */
  UCHAR Protocol;      /* Protocol */
  USHORT UDPLength;    /* Size of UDP datagram */
} UDP_PSEUDO_HEADER, *PUDP_PSEUDO_HEADER;
#pragma pack(pop)

typedef struct UDP_STATISTICS {
  ULONG InputDatagrams;
  ULONG NumPorts;
  ULONG InputErrors;
  ULONG OutputDatagrams;
  ULONG NumAddresses;
} UDP_STATISTICS, *PUDP_STATISTICS;

extern UDP_STATISTICS UDPStats;

VOID UDPSend(
  PVOID Context,
  PDATAGRAM_SEND_REQUEST SendRequest);

NTSTATUS UDPSendDatagram(
    PADDRESS_FILE AddrFile,
    PTDI_CONNECTION_INFORMATION ConnInfo,
    PCHAR BufferData,
    ULONG DataSize,
    PULONG DataUsed );

VOID UDPReceive(
    PIP_INTERFACE Interface,
    PIP_PACKET IPPacket);

NTSTATUS UDPStartup(
  VOID);

NTSTATUS UDPShutdown(
  VOID);
UINT UDPAllocatePort( UINT HintPort );
VOID UDPFreePort( UINT Port );

NTSTATUS LibIP6QueryTable(
    ULONG TableId,
    PVOID Buffer,
    ULONG Length,
    PULONG Count,
    PULONG Needed,
    PVOID Statistics);

NTSTATUS LibICMP6EchoRequest(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PIO_STACK_LOCATION IrpSp);

NTSTATUS LibUDPOpen(
    PADDRESS_FILE AddrFile,
    USHORT Port,
    PUSHORT AllocatedPort);

VOID LibUDPClose(
    PADDRESS_FILE AddrFile);

NTSTATUS LibUDPSendDatagram(
    PADDRESS_FILE AddrFile,
    PTDI_CONNECTION_INFORMATION ConnInfo,
    PCHAR BufferData,
    ULONG DataSize,
    PULONG DataUsed);

/* EOF */
