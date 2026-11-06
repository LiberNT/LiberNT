/*
 * PROJECT:     LiberNT USB Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared declarations of the USB video class driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <windef.h>
#include <ks.h>
#include <ksmedia.h>
#include <usb.h>
#include <usbioctl.h>
#include <usbdlib.h>

#define LNTUVC_TAG 'cvuL'

#define UVC_CS_INTERFACE                0x24

#define UVC_SC_VIDEOCONTROL             0x01
#define UVC_SC_VIDEOSTREAMING           0x02

#define UVC_VC_HEADER                   0x01
#define UVC_VC_INPUT_TERMINAL           0x02
#define UVC_VC_OUTPUT_TERMINAL          0x03
#define UVC_VC_SELECTOR_UNIT            0x04
#define UVC_VC_PROCESSING_UNIT          0x05
#define UVC_VC_EXTENSION_UNIT           0x06

#define UVC_VS_INPUT_HEADER             0x01
#define UVC_VS_FORMAT_UNCOMPRESSED      0x04
#define UVC_VS_FRAME_UNCOMPRESSED       0x05
#define UVC_VS_FORMAT_MJPEG             0x06
#define UVC_VS_FRAME_MJPEG              0x07
#define UVC_VS_FORMAT_FRAME_BASED       0x10
#define UVC_VS_FRAME_FRAME_BASED        0x11
#define UVC_VS_STILL_IMAGE_FRAME        0x03
#define UVC_VS_COLOR_FORMAT             0x0D

#define UVC_ITT_CAMERA                  0x0201
#define UVC_TT_STREAMING                0x0101

#define UVC_SET_CUR                     0x01
#define UVC_GET_CUR                     0x81
#define UVC_GET_MIN                     0x82
#define UVC_GET_MAX                     0x83
#define UVC_GET_RES                     0x84
#define UVC_GET_LEN                     0x85
#define UVC_GET_INFO                    0x86
#define UVC_GET_DEF                     0x87

#define UVC_VS_PROBE_CONTROL            0x01
#define UVC_VS_COMMIT_CONTROL           0x02
#define UVC_VS_STILL_PROBE_CONTROL      0x03
#define UVC_VS_STILL_COMMIT_CONTROL     0x04
#define UVC_VS_STILL_TRIGGER_CONTROL    0x05

#define UVC_STILL_METHOD_TRANSMIT       2

#define UVC_PIN_CAPTURE                 0
#define UVC_PIN_BRIDGE                  1
#define UVC_PIN_STILL                   2

#define UVC_HDR_FID                     0x01
#define UVC_HDR_EOF                     0x02
#define UVC_HDR_STI                     0x20
#define UVC_HDR_ERR                     0x40

#define UVC_MAX_ALTS                    32
#define UVC_MAX_PROBE                   48
#define UVC_TRANSFER_COUNT              5
#define UVC_ISO_PACKETS                 32
#define UVC_ISO_PACKETS_LARGE           8
#define UVC_ISO_LARGE_TRANSFER          (128 * 1024)
#define UVC_FRAME_SLOTS                 3
#define UVC_FRAMING_FRAMES              10
#define UVC_MAX_BULK_TRANSFER           (1024 * 1024)
#define UVC_MIN_BULK_TRANSFER           (16 * 1024)
#define UVC_MAX_TRANSFER_ERRORS         16

typedef struct _UVC_FRAME
{
    UCHAR Index;
    USHORT Width;
    USHORT Height;
    ULONG MinBitRate;
    ULONG MaxBitRate;
    ULONG DefaultInterval;
    ULONG MinInterval;
    ULONG MaxInterval;
    ULONG IntervalCount;
    ULONG IntervalStep;
    PULONG Intervals;
} UVC_FRAME, *PUVC_FRAME;

typedef struct _UVC_STILL_SIZE
{
    USHORT Width;
    USHORT Height;
} UVC_STILL_SIZE, *PUVC_STILL_SIZE;

typedef struct _UVC_FORMAT
{
    UCHAR Index;
    UCHAR Subtype;
    GUID SubFormat;
    USHORT BitCount;
    ULONG Compression;
    BOOLEAN FixedSize;
    ULONG FrameCount;
    PUVC_FRAME Frames;
    ULONG StillCount;
    PUVC_STILL_SIZE StillSizes;
    UCHAR StillCompressions;
    BOOLEAN HasColor;
    UCHAR ColorPrimaries;
    UCHAR ColorMatrix;
} UVC_FORMAT, *PUVC_FORMAT;

typedef struct _UVC_ENTITY
{
    UCHAR Id;
    UCHAR Subtype;
    USHORT TerminalType;
    UCHAR SourceCount;
    PUCHAR Sources;
    UCHAR ControlSize;
    PUCHAR Controls;
    GUID ExtensionCode;
    UCHAR ExtensionControlCount;
    USHORT ObjectiveFocalMin;
    USHORT ObjectiveFocalMax;
    USHORT OcularFocal;
    ULONG NodeIndex;
} UVC_ENTITY, *PUVC_ENTITY;

typedef struct _UVC_ALT
{
    UCHAR AlternateSetting;
    UCHAR EndpointAddress;
    UCHAR Attributes;
    USHORT MaxPacketSize;
    ULONG Bytes;
} UVC_ALT, *PUVC_ALT;

typedef struct _UVC_DEVICE
{
    PKSDEVICE KsDevice;
    PDEVICE_OBJECT LowerDevice;
    PUSB_CONFIGURATION_DESCRIPTOR Config;
    USBD_CONFIGURATION_HANDLE ConfigHandle;
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    USHORT UvcVersion;
    ULONG ClockFrequency;
    UCHAR ControlInterface;
    UCHAR StreamInterface;
    UCHAR StreamAltCount;
    UCHAR TerminalLink;
    UCHAR StillCaptureMethod;
    BOOLEAN HasStream;
    BOOLEAN Bulk;
    ULONG EntityCount;
    PUVC_ENTITY Entities;
    ULONG FormatCount;
    PUVC_FORMAT Formats;
    UVC_ALT Alts[UVC_MAX_ALTS];
    PKSFILTER_DESCRIPTOR FilterDescriptor;
    PKSFILTERFACTORY FilterFactory;
    KMUTEX ControlMutex;
    volatile LONG Gone;
    KSPIN_LOCK StillLock;
    struct _UVC_STILL *Still;
    volatile LONG CapturePins;
    USBD_PIPE_HANDLE StatusPipe;
    PIRP StatusIrp;
    struct _URB_BULK_OR_INTERRUPT_TRANSFER StatusUrb;
    UCHAR StatusBuffer[64];
    KEVENT StatusIdle;
    volatile LONG StatusPending;
    volatile LONG StatusWorkQueued;
    BOOLEAN StatusRunning;
    PIO_WORKITEM StatusWorkItem;
} UVC_DEVICE, *PUVC_DEVICE;

typedef struct _UVC_PIN UVC_PIN, *PUVC_PIN;

typedef struct _UVC_TRANSFER
{
    PUVC_PIN Owner;
    PIRP Irp;
    PURB Urb;
    PUCHAR Buffer;
    ULONG Size;
    LIST_ENTRY RetryEntry;
} UVC_TRANSFER, *PUVC_TRANSFER;

typedef struct _UVC_FRAME_SLOT
{
    PUCHAR Data;
    ULONG Used;
    ULONGLONG Time;
} UVC_FRAME_SLOT, *PUVC_FRAME_SLOT;

struct _UVC_PIN
{
    PKSPIN Pin;
    PUVC_DEVICE Device;
    PUVC_FORMAT Format;
    PUVC_FRAME Frame;
    ULONG Interval;
    ULONG ImageSize;
    ULONG ProbeLength;
    UCHAR Probe[UVC_MAX_PROBE];
    ULONG MaxFrameSize;
    ULONG MaxPayloadSize;
    ULONG SlotSize;

    KSPIN_LOCK Lock;
    KEVENT Idle;
    volatile LONG Pending;
    BOOLEAN Prepared;
    BOOLEAN Streaming;
    BOOLEAN PipeOpen;
    PUVC_ALT Alt;
    ULONG IsoPackets;
    USBD_PIPE_HANDLE Pipe;
    ULONG TransferCount;
    UVC_TRANSFER Transfers[UVC_TRANSFER_COUNT];
    volatile LONG TransferErrors;
    LIST_ENTRY RetryList;
    WORK_QUEUE_ITEM RetryItem;
    PKSWORKER RetryWorker;

    UVC_FRAME_SLOT Slots[UVC_FRAME_SLOTS];
    PUVC_FRAME_SLOT FillSlot;
    PUVC_FRAME_SLOT ReadySlot;
    PUVC_FRAME_SLOT BusySlot;
    BOOLEAN InFrame;
    BOOLEAN FrameError;
    BOOLEAN FrameIsStill;
    UCHAR FrameFid;
    BOOLEAN HaveLastFid;
    UCHAR LastFid;
    BOOLEAN PayloadActive;
    UCHAR PayloadFlags;
    ULONG PayloadBytes;
    BOOLEAN Delivered;
    BOOLEAN Discontinuity;
    ULONG FramesDropped;
};

extern const KSPIN_DISPATCH UvcPinDispatch;
extern const KSPIN_DISPATCH UvcStillDispatch;

VOID UvcStillFrameBegin(PUVC_DEVICE Device);
VOID UvcStillFrameData(PUVC_DEVICE Device, PUCHAR Data, ULONG Length);
VOID UvcStillFrameEnd(PUVC_DEVICE Device, BOOLEAN Error);
NTSTATUS UvcStillTrigger(PUVC_DEVICE Device);
ULONG UvcStillImageSize(PUVC_FORMAT Format, PUVC_STILL_SIZE Size);
VOID UvcStatusStart(PUVC_DEVICE Device);
VOID UvcStatusStop(PUVC_DEVICE Device);
VOID UvcStatusFree(PUVC_DEVICE Device);

PVOID UvcAllocate(PUVC_DEVICE Device, SIZE_T Size);
NTSTATUS UvcSubmitUrbSync(PUVC_DEVICE Device, PURB Urb);
NTSTATUS UvcClassRequest(PUVC_DEVICE Device, BOOLEAN In, UCHAR Request, USHORT Value, USHORT Index, PVOID Buffer, ULONG Length, PULONG Transferred);

NTSTATUS UvcParseDescriptors(PUVC_DEVICE Device);
PUVC_ENTITY UvcFindEntity(PUVC_DEVICE Device, UCHAR Subtype, USHORT TerminalType);
BOOLEAN UvcEntityHasControl(PUVC_ENTITY Entity, ULONG Bit);
ULONG UvcImageSize(PUVC_FORMAT Format, PUVC_FRAME Frame);
USHORT UvcImageBits(PUVC_FORMAT Format);

NTSTATUS UvcBuildFilter(PUVC_DEVICE Device);
const KSAUTOMATION_TABLE *UvcPinAutomationTable(PUVC_DEVICE Device);
NTSTATUS UvcBuildControlTables(PUVC_DEVICE Device, PKSFILTER_DESCRIPTOR Descriptor, PKSNODE_DESCRIPTOR Nodes);
NTSTATUS UvcFindFormat(PUVC_DEVICE Device, PKSDATAFORMAT DataFormat, PUVC_FORMAT *Format, PUVC_FRAME *Frame, PULONG Interval);
NTSTATUS NTAPI UvcPinIntersect(PVOID Context, PIRP Irp, PKSP_PIN PinInstance, PKSDATARANGE CallerRange, PKSDATARANGE DescriptorRange, ULONG BufferSize, PVOID Data, PULONG DataSize);
