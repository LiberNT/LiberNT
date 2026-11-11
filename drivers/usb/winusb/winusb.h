/*
 * PROJECT:     LiberNT WinUSB driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Generic USB function driver for WinUSB devices: definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#include <usbbusif.h>
#include <winusbio.h>
#include <reactos/drivers/lntwinusb.h>

#define WINUSB_TAG 'BSUW'
#define WINUSB_MAX_PIPES 32
#define WINUSB_MAX_INTERFACE_GUIDS 8


typedef struct _WINUSB_DEVICE WINUSB_DEVICE, *PWINUSB_DEVICE;

typedef struct _WINUSB_PIPE
{
    UCHAR EndpointAddress;
    UCHAR Interval;
    USHORT MaximumPacketSize;
    ULONG MaximumBytesPerInterval;
    USBD_PIPE_TYPE Type;
    USBD_PIPE_HANDLE Handle;
    BOOLEAN ShortPacketTerminate;
    BOOLEAN AutoClearStall;
    BOOLEAN IgnoreShortPackets;
    BOOLEAN AllowPartialReads;
    BOOLEAN AutoFlush;
    BOOLEAN RawIo;
    BOOLEAN ResetPipeOnResume;
    BOOLEAN Halted;
    ULONG Timeout;
    LIST_ENTRY Queue;
    LIST_ENTRY ActiveList;
    ULONG Active;
    PUCHAR Saved;
    ULONG SavedLength;
    ULONG SavedOffset;
} WINUSB_PIPE, *PWINUSB_PIPE;

typedef struct _WINUSB_INTERFACE
{
    UCHAR Number;
    UCHAR Setting;
    UCHAR PipeCount;
    BOOLEAN Selecting;
    WINUSB_PIPE Pipes[WINUSB_MAX_PIPES];
} WINUSB_INTERFACE, *PWINUSB_INTERFACE;

typedef enum _WINUSB_REQUEST_STAGE
{
    WinUsbStageQueued,
    WinUsbStageActive,
    WinUsbStageDone
} WINUSB_REQUEST_STAGE;

typedef struct _WINUSB_REQUEST
{
    LIST_ENTRY Link;
    PIRP Irp;
    PWINUSB_DEVICE Device;
    PWINUSB_PIPE Pipe;
    PWINUSB_INTERFACE Interface;
    PFILE_OBJECT FileObject;
    WINUSB_REQUEST_STAGE Stage;
    BOOLEAN In;
    BOOLEAN Control;
    BOOLEAN RawIo;
    BOOLEAN TailAllowed;
    BOOLEAN ZeroLengthSent;
    BOOLEAN TimedOut;
    BOOLEAN TimerArmed;
    BOOLEAN UsedBounce;
    BOOLEAN Overflow;
    ULONG Length;
    ULONG Done;
    ULONG Chunk;
    ULONG Timeout;
    LONG References;
    NTSTATUS Status;
    PMDL PartialMdl;
    PUCHAR Bounce;
    ULONG BounceLength;
    PIO_WORKITEM WorkItem;
    KTIMER Timer;
    KDPC TimerDpc;
    union
    {
        struct _URB_BULK_OR_INTERRUPT_TRANSFER Bulk;
        struct _URB_CONTROL_TRANSFER Control;
        struct _URB_PIPE_REQUEST PipeRequest;
    } Urb;
} WINUSB_REQUEST, *PWINUSB_REQUEST;

struct _WINUSB_DEVICE
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Lower;
    PDEVICE_OBJECT Pdo;
    IO_REMOVE_LOCK RemoveLock;
    KSPIN_LOCK Lock;
    LONG Opened;
    BOOLEAN Started;
    BOOLEAN Gone;
    UCHAR Speed;
    UCHAR InterfaceCount;
    ULONG ControlTimeout;
    BOOLEAN AutoSuspend;
    BOOLEAN DefaultAutoSuspend;
    ULONG SuspendDelay;
    ULONG DefaultSuspendDelay;
    ULONG ControlActive;
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR Configuration;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    WINUSB_INTERFACE Interfaces[LNT_WINUSB_MAX_INTERFACES];
    ULONG InterfaceNameCount;
    UNICODE_STRING InterfaceNames[WINUSB_MAX_INTERFACE_GUIDS];
    KMUTEX ConfigurationMutex;
};

DRIVER_ADD_DEVICE WinUsbAddDevice;
_Dispatch_type_(IRP_MJ_PNP) DRIVER_DISPATCH WinUsbDispatchPnp;
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH WinUsbDispatchDeviceControl;

NTSTATUS
WinUsbSubmitUrbSync(
    _In_ PWINUSB_DEVICE Device,
    _Inout_ PURB Urb);

NTSTATUS
WinUsbConfigure(
    _In_ PWINUSB_DEVICE Device);

VOID
WinUsbDeconfigure(
    _In_ PWINUSB_DEVICE Device);

VOID
WinUsbLoadPipes(
    _Inout_ PWINUSB_INTERFACE Interface,
    _In_ PUSBD_INTERFACE_INFORMATION Info);

VOID
WinUsbResetPolicies(
    _In_ PWINUSB_DEVICE Device);

VOID
WinUsbResetPipePolicy(
    _Inout_ PWINUSB_PIPE Pipe);

PWINUSB_PIPE
WinUsbFindPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ UCHAR InterfaceIndex,
    _In_ UCHAR PipeId);

NTSTATUS
WinUsbQueueTransfer(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp,
    _In_ BOOLEAN In);

NTSTATUS
WinUsbControlTransfer(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp,
    _In_ BOOLEAN In);

NTSTATUS
WinUsbResetPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe);

NTSTATUS
WinUsbAbortPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe);

VOID
WinUsbFlushPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe);

VOID
WinUsbCancelFileRequests(
    _In_ PWINUSB_DEVICE Device,
    _In_opt_ PFILE_OBJECT FileObject);

BOOLEAN
WinUsbInterfaceBusy(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_INTERFACE Interface);

NTSTATUS
WinUsbSelectSetting(
    _In_ PWINUSB_DEVICE Device,
    _In_ UCHAR InterfaceIndex,
    _In_ UCHAR Setting);

NTSTATUS
WinUsbGetDescriptor(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp);

NTSTATUS
WinUsbGetFrameNumber(
    _In_ PWINUSB_DEVICE Device,
    _Out_ PLNT_WINUSB_FRAME_NUMBER FrameNumber);
