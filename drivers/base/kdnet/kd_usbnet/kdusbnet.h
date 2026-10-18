/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled xHCI host and USB Ethernet (RNDIS, CDC-ECM, CDC-NCM, CDC-EEM) layout
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <kdnetextensibility.h>
#include <usb100.h>
#include <usb200.h>

#define XHCI_CAP_LENGTH             0x00
#define XHCI_CAP_HCSPARAMS1         0x04
#define XHCI_CAP_HCSPARAMS2         0x08
#define XHCI_CAP_HCCPARAMS1         0x10
#define XHCI_CAP_DBOFF              0x14
#define XHCI_CAP_RTSOFF             0x18

#define XHCI_OP_USBCMD              0x00
#define XHCI_OP_USBSTS              0x04
#define XHCI_OP_PAGESIZE            0x08
#define XHCI_OP_CRCR                0x18
#define XHCI_OP_DCBAAP              0x30
#define XHCI_OP_CONFIG              0x38
#define XHCI_OP_PORTSC(Port)        (0x400 + 0x10 * ((Port) - 1))

#define XHCI_RT_IMAN                0x20
#define XHCI_RT_IMOD                0x24
#define XHCI_RT_ERSTSZ              0x28
#define XHCI_RT_ERSTBA              0x30
#define XHCI_RT_ERDP                0x38

#define XHCI_USBCMD_RUN             0x00000001
#define XHCI_USBCMD_RESET           0x00000002
#define XHCI_USBSTS_HALTED          0x00000001
#define XHCI_USBSTS_NOT_READY       0x00000800
#define XHCI_HCC_CONTEXT_64         0x00000004
#define XHCI_HCC_PORT_POWER         0x00000008
#define XHCI_HCC_XECP_SHIFT         16
#define XHCI_ERDP_BUSY              0x00000008
#define XHCI_PAGESIZE_4K            0x00000001

#define XHCI_EXT_CAP_LEGACY         1
#define XHCI_LEGACY_BIOS_OWNED      0x00010000
#define XHCI_LEGACY_OS_OWNED        0x01000000
#define XHCI_LEGACY_PRESERVE        0x000E1FEE
#define XHCI_LEGACY_SMI_EVENTS      0xE0000000

#define XHCI_PORT_CONNECTED         0x00000001
#define XHCI_PORT_ENABLED           0x00000002
#define XHCI_PORT_RESET             0x00000010
#define XHCI_PORT_POWER             0x00000200
#define XHCI_PORT_SPEED_SHIFT       10
#define XHCI_PORT_SPEED_MASK        0x0000000F
#define XHCI_PORT_INDICATOR         0x0000C000
#define XHCI_PORT_CONNECT_CHANGE    0x00020000
#define XHCI_PORT_RESET_CHANGE      0x00200000
#define XHCI_PORT_CHANGE_MASK       0x00FE0000
#define XHCI_PORT_WAKE_MASK         0x0E000000

#define XHCI_SPEED_FULL             1
#define XHCI_SPEED_LOW              2
#define XHCI_SPEED_HIGH             3
#define XHCI_SPEED_SUPER            4

#define TRB_CYCLE                   0x00000001
#define TRB_TOGGLE_CYCLE            0x00000002
#define TRB_SHORT_PACKET            0x00000004
#define TRB_IOC                     0x00000020
#define TRB_IMMEDIATE               0x00000040
#define TRB_TYPE_SHIFT              10
#define TRB_TYPE_MASK               0x3F
#define TRB_DIRECTION_IN            0x00010000
#define TRB_TRANSFER_SHIFT          16
#define TRB_ENDPOINT_SHIFT          16
#define TRB_SLOT_SHIFT              24
#define TRB_CODE_SHIFT              24
#define TRB_LENGTH_MASK             0x00FFFFFF

#define TRB_NORMAL                  1
#define TRB_SETUP                   2
#define TRB_DATA                    3
#define TRB_STATUS                  4
#define TRB_LINK                    6
#define TRB_ENABLE_SLOT             9
#define TRB_DISABLE_SLOT            10
#define TRB_ADDRESS_DEVICE          11
#define TRB_CONFIGURE_ENDPOINT      12
#define TRB_EVALUATE_CONTEXT        13
#define TRB_RESET_ENDPOINT          14
#define TRB_STOP_ENDPOINT           15
#define TRB_SET_DEQUEUE             16
#define TRB_TRANSFER_EVENT          32
#define TRB_COMMAND_EVENT           33

#define TRB_SETUP_NO_DATA           0
#define TRB_SETUP_OUT_DATA          2
#define TRB_SETUP_IN_DATA           3

#define XHCI_CC_SUCCESS             1
#define XHCI_CC_STALL               6
#define XHCI_CC_SHORT_PACKET        13
#define XHCI_CC_TIMEOUT             0xFF

#define EP_TYPE_BULK_OUT            2
#define EP_TYPE_CONTROL             4
#define EP_TYPE_BULK_IN             6
#define EP_ERROR_COUNT              3
#define EP_CONTROL_AVERAGE          8
#define EP_BULK_AVERAGE             3072
#define EP_STATE_MASK               7
#define EP_STATE_RUNNING            1
#define EP_STATE_HALTED             2

#define USBNET_RING_TRBS            64
#define USBNET_TRB_SIZE             16
#define USBNET_RING_SIZE            (USBNET_RING_TRBS * USBNET_TRB_SIZE)
#define USBNET_MAX_SCRATCHPAD       128
#define USBNET_MAX_SLOTS            8
#define USBNET_CONTROL_SIZE         4096
#define USBNET_RX_SIZE              4096
#define USBNET_TX_SIZE              4096
#define USBNET_TX_COUNT             2
#define USBNET_FIXED_PAGES          8
#define USBNET_MAX_FRAME            1514
#define USBNET_MIN_FRAME            14
#define USBNET_PAD_FRAME            60

#define USB_CLASS_CDC               0x02
#define USB_CLASS_CDC_DATA          0x0A
#define USB_CLASS_WIRELESS          0xE0
#define USB_CLASS_MISC              0xEF
#define CDC_SUBCLASS_ACM            0x02
#define CDC_SUBCLASS_ECM            0x06
#define CDC_SUBCLASS_EEM            0x0C
#define CDC_SUBCLASS_NCM            0x0D
#define CDC_PROTOCOL_VENDOR         0xFF
#define CDC_PROTOCOL_EEM            0x07
#define CDC_CS_INTERFACE            0x24
#define CDC_UNION_FUNCTIONAL        0x06
#define CDC_ETHERNET_FUNCTIONAL     0x0F
#define CDC_NCM_FUNCTIONAL          0x1A
#define CDC_NCM_INPUT_SIZE_8        0x20

#define CDC_SEND_ENCAPSULATED_COMMAND   0x00
#define CDC_GET_ENCAPSULATED_RESPONSE   0x01
#define CDC_SET_ETHERNET_PACKET_FILTER  0x43
#define CDC_GET_NTB_PARAMETERS          0x80
#define CDC_SET_NTB_INPUT_SIZE          0x86
#define CDC_PACKET_TYPE_DIRECTED        0x0004
#define CDC_PACKET_TYPE_BROADCAST       0x0008
#define CDC_NTB_PARAMETERS_SIZE         28

#define RNDIS_PACKET_MSG            0x00000001
#define RNDIS_INITIALIZE_MSG        0x00000002
#define RNDIS_QUERY_MSG             0x00000004
#define RNDIS_SET_MSG               0x00000005
#define RNDIS_COMPLETION            0x80000000
#define RNDIS_STATUS_SUCCESS        0x00000000
#define RNDIS_PACKET_HEADER         44
#define RNDIS_REQUEST_HEADER        28
#define RNDIS_OID_PERMANENT_ADDRESS 0x01010101
#define RNDIS_OID_PACKET_FILTER     0x0001010E
#define RNDIS_FILTER_DEFAULT        0x0000000B
#define RNDIS_RESPONSE_SIZE         1024

#define NCM_NTH16_SIGNATURE         0x484D434E
#define NCM_NDP16_SIGNATURE         0x304D434E
#define NCM_NDP16_CRC_SIGNATURE     0x314D434E
#define NCM_NTH16_SIZE              12
#define NCM_NDP16_SIZE              16
#define NCM_MIN_INPUT_SIZE          2048

#define EEM_HEADER_SIZE             2
#define EEM_CRC_SIZE                4
#define EEM_COMMAND                 0x8000
#define EEM_COMMAND_SHIFT           11
#define EEM_COMMAND_MASK            7
#define EEM_COMMAND_LENGTH          0x07FF
#define EEM_LENGTH_MASK             0x3FFF

typedef enum _USBNET_PROTOCOL
{
    UsbNetNone,
    UsbNetRndis,
    UsbNetEcm,
    UsbNetNcm,
    UsbNetEem
} USBNET_PROTOCOL;

typedef struct _USBNET_RING
{
    PULONG Trbs;
    ULONG64 Physical;
    ULONG Enqueue;
    ULONG Cycle;
} USBNET_RING, *PUSBNET_RING;

typedef struct _USBNET_ENDPOINT
{
    UCHAR Address;
    UCHAR Dci;
    UCHAR MaxBurst;
    USHORT MaxPacket;
    USBNET_RING Ring;
    ULONG64 FirstTrb;
    ULONG64 DataTrb;
    ULONG64 LastTrb;
    ULONG Residual;
    UCHAR Completion;
    BOOLEAN Done;
} USBNET_ENDPOINT, *PUSBNET_ENDPOINT;

typedef struct _USBNET_FUNCTION
{
    USBNET_PROTOCOL Protocol;
    UCHAR Configuration;
    UCHAR ControlInterface;
    UCHAR DataInterface;
    UCHAR DataAlternate;
    UCHAR MacIndex;
    UCHAR NcmCapabilities;
    UCHAR InAddress;
    UCHAR OutAddress;
    UCHAR InBurst;
    UCHAR OutBurst;
    USHORT InPacket;
    USHORT OutPacket;
} USBNET_FUNCTION, *PUSBNET_FUNCTION;

typedef struct _USBNET_ADAPTER
{
    PUCHAR Registers;
    PUCHAR Operational;
    PUCHAR Runtime;
    PUCHAR Doorbells;
    PKDNET_SHARED_DATA KdNet;
    PUCHAR Memory;
    ULONG ContextSize;
    ULONG MaxPorts;
    ULONG MaxSlots;
    ULONG Scratchpads;
    PULONG64 Dcbaa;
    PULONG64 ScratchpadArray;
    PULONG Erst;
    PUCHAR DeviceContext;
    PUCHAR InputContext;
    PUCHAR Control;
    PUCHAR RxBuffer;
    PUCHAR TxBuffers;
    PUCHAR ScratchpadPages;
    USBNET_RING Command;
    PULONG Events;
    ULONG64 EventsPhysical;
    ULONG EventDequeue;
    ULONG EventCycle;
    ULONG64 CommandTrb;
    UCHAR CommandCompletion;
    UCHAR CommandSlot;
    BOOLEAN CommandDone;
    BOOLEAN Running;
    UCHAR Slot;
    UCHAR Port;
    UCHAR Speed;
    USBNET_FUNCTION Function;
    USBNET_ENDPOINT Ep0;
    USBNET_ENDPOINT BulkIn;
    USBNET_ENDPOINT BulkOut;
    ULONG RndisRequest;
    ULONG NcmOutMaxSize;
    USHORT NcmNdpOffset;
    USHORT NcmSequence;
    ULONG TxHeader;
    ULONG TxNext;
    ULONG LinkPoll;
    ULONG RxLength;
    ULONG RxOffset;
    ULONG RxNdp;
    ULONG RxFrameLength;
    PUCHAR RxFrame;
    BOOLEAN RxActive;
    BOOLEAN RxHeld;
    BOOLEAN LinkUp;
} USBNET_ADAPTER, *PUSBNET_ADAPTER;

extern PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

FORCEINLINE
USHORT
UsbNetGet16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

FORCEINLINE
ULONG
UsbNetGet32(
    _In_reads_(4) const UCHAR *Data)
{
    return Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
}

FORCEINLINE
VOID
UsbNetPut16(
    _Out_writes_(2) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
}

FORCEINLINE
VOID
UsbNetPut32(
    _Out_writes_(4) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    Data[2] = (UCHAR)(Value >> 16);
    Data[3] = (UCHAR)(Value >> 24);
}

ULONG
XhciRead(
    _In_ PUCHAR Register);

VOID
XhciWrite(
    _In_ PUCHAR Register,
    _In_ ULONG Value);

VOID
XhciWrite64(
    _In_ PUCHAR Register,
    _In_ ULONG64 Value);

VOID
XhciStall(
    _In_ ULONG Microseconds);

ULONG64
XhciPhysical(
    _In_ PVOID Address);

VOID
XhciInitializeRing(
    _Out_ PUSBNET_RING Ring,
    _In_ PVOID Trbs);

ULONG64
XhciQueueTrb(
    _Inout_ PUSBNET_RING Ring,
    _In_ ULONG64 Pointer,
    _In_ ULONG Status,
    _In_ ULONG Control);

VOID
XhciRing(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Slot,
    _In_ ULONG Target);

VOID
XhciProcessEvents(
    _In_ PUSBNET_ADAPTER Adapter);

NTSTATUS
XhciCommand(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG64 Pointer,
    _In_ ULONG Status,
    _In_ ULONG Control);

NTSTATUS
XhciInitialize(
    _In_ PUSBNET_ADAPTER Adapter);

VOID
XhciHalt(
    _In_ PUSBNET_ADAPTER Adapter);

ULONG
XhciWaitForConnections(
    _In_ PUSBNET_ADAPTER Adapter);

NTSTATUS
XhciResetPort(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Port,
    _Out_ PUCHAR Speed);

NTSTATUS
XhciAddressDevice(
    _In_ PUSBNET_ADAPTER Adapter);

NTSTATUS
XhciUpdateControlPacketSize(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ USHORT MaxPacket);

NTSTATUS
XhciConfigureBulk(
    _In_ PUSBNET_ADAPTER Adapter);

VOID
XhciDisableSlot(
    _In_ PUSBNET_ADAPTER Adapter);

NTSTATUS
XhciRecoverEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint);

NTSTATUS
XhciControl(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ UCHAR RequestType,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _In_ USHORT Length,
    _Out_opt_ PULONG Transferred);

VOID
XhciQueueBulk(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN Terminate);

BOOLEAN
XhciPollEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint);

NTSTATUS
XhciWaitEndpoint(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ PUSBNET_ENDPOINT Endpoint,
    _In_ ULONG Milliseconds);

NTSTATUS
UsbFindFunction(
    _In_ PUSBNET_ADAPTER Adapter);

NTSTATUS
UsbNetStartFunction(
    _In_ PUSBNET_ADAPTER Adapter);

BOOLEAN
UsbNetNextFrame(
    _In_ PUSBNET_ADAPTER Adapter);

ULONG
UsbNetFrameTransmit(
    _In_ PUSBNET_ADAPTER Adapter,
    _Inout_ PUCHAR Buffer,
    _In_ ULONG Length);

ULONG
NTAPI
UsbNetGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
NTAPI
UsbNetInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet);

VOID
NTAPI
UsbNetShutdownController(
    _In_ PVOID Context);

NTSTATUS
NTAPI
UsbNetGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length);

VOID
NTAPI
UsbNetReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle);

NTSTATUS
NTAPI
UsbNetGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle);

NTSTATUS
NTAPI
UsbNetSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length);

PVOID
NTAPI
UsbNetGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle);

ULONG
NTAPI
UsbNetGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle);
