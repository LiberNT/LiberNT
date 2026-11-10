/*
 * PROJECT:     LiberNT USB CCID Smart Card Reader Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Definitions shared by the PnP and CCID protocol halves
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <usbdi.h>
#include <usbdlib.h>
#include <smclib.h>

#define CCID_TAG 'dicC'

#define CCID_INTERFACE_CLASS            0x0B
#define CCID_CLASS_DESCRIPTOR_TYPE      0x21
#define CCID_CLASS_DESCRIPTOR_LENGTH    0x36

#define CCID_HEADER_SIZE                10
#define CCID_MIN_MESSAGE                (CCID_HEADER_SIZE + 271)
#define CCID_MAX_MESSAGE                (CCID_HEADER_SIZE + 65544)

#define PC_TO_RDR_SET_PARAMETERS        0x61
#define PC_TO_RDR_ICC_POWER_ON          0x62
#define PC_TO_RDR_ICC_POWER_OFF         0x63
#define PC_TO_RDR_GET_SLOT_STATUS       0x65
#define PC_TO_RDR_XFR_BLOCK             0x6F
#define RDR_TO_PC_DATA_BLOCK            0x80
#define RDR_TO_PC_SLOT_STATUS           0x81
#define RDR_TO_PC_PARAMETERS            0x82
#define RDR_TO_PC_NOTIFY_SLOT_CHANGE    0x50
#define RDR_TO_PC_HARDWARE_ERROR        0x51

#define CCID_ICC_STATUS(Status)         ((Status) & 0x03)
#define CCID_ICC_ACTIVE                 0
#define CCID_ICC_INACTIVE               1
#define CCID_ICC_ABSENT                 2
#define CCID_COMMAND_STATUS(Status)     (((Status) >> 6) & 0x03)
#define CCID_COMMAND_OK                 0
#define CCID_COMMAND_FAILED             1
#define CCID_COMMAND_TIME_EXTENSION     2

#define CCID_ERROR_COMMAND_NOT_SUPPORTED 0x00
#define CCID_ERROR_ICC_MUTE             0xFE

#define CCID_FEATURE_AUTO_PARAMETERS    0x00000002
#define CCID_FEATURE_AUTO_VOLTAGE       0x00000008
#define CCID_FEATURE_AUTO_PPS_PROP      0x00000040
#define CCID_FEATURE_AUTO_PPS_CUR       0x00000080
#define CCID_FEATURE_EXCHANGE_MASK      0x00070000
#define CCID_FEATURE_TPDU               0x00010000
#define CCID_FEATURE_SHORT_APDU         0x00020000
#define CCID_FEATURE_EXTENDED_APDU      0x00040000

#define CCID_VOLTAGE_5V                 0x01
#define CCID_VOLTAGE_3V                 0x02
#define CCID_VOLTAGE_1V8                0x04

#define CCID_TRANSFER_TIMEOUT_MS        10000

typedef struct _READER_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    PSMARTCARD_EXTENSION Smartcard;
    USBD_PIPE_HANDLE BulkIn;
    USBD_PIPE_HANDLE BulkOut;
    USBD_PIPE_HANDLE Interrupt;
    ULONG InterruptLength;
    ULONG Features;
    ULONG MaxMessage;
    UCHAR VoltageSupport;
    UCHAR Sequence;
    PUCHAR Command;
    PUCHAR Response;
    PIRP InterruptIrp;
    URB InterruptUrb;
    UCHAR InterruptBuffer[8];
    KEVENT InterruptStopped;
    volatile LONG InterruptStopping;
} READER_EXTENSION;

typedef struct _CCID_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    IO_REMOVE_LOCK RemoveLock;
    UNICODE_STRING InterfaceName;
    volatile LONG OpenCount;
    BOOLEAN Started;
    BOOLEAN SmartcardInitialized;
    PUSB_CONFIGURATION_DESCRIPTOR Configuration;
    PURB SelectUrb;
    SMARTCARD_EXTENSION Smartcard;
    READER_EXTENSION Reader;
} CCID_DEVICE, *PCCID_DEVICE;

NTSTATUS
CcidStartReader(
    _In_ PCCID_DEVICE Device,
    _In_ PUSB_DEVICE_DESCRIPTOR DeviceDescriptor,
    _In_ PUCHAR ClassDescriptor);

VOID
CcidStopReader(
    _In_ PCCID_DEVICE Device);

VOID
CcidReleaseReader(
    _In_ PCCID_DEVICE Device);

VOID
CcidCompleteTracking(
    _In_ PSMARTCARD_EXTENSION Smartcard,
    _In_ NTSTATUS Status);

NTSTATUS
CcidGetString(
    _In_ PCCID_DEVICE Device,
    _In_ UCHAR Index,
    _Out_writes_(Size) PUCHAR Buffer,
    _In_ USHORT Size,
    _Out_ PUSHORT Length);
