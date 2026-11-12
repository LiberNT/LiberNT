/*
 * PROJECT:     LiberNT Apple ISP Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared declarations of the Apple silicon ISP camera driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <windef.h>
#include <ks.h>
#include <ksmedia.h>
#include <ntstrsafe.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/applepmgr.h>
#include "ispwire.h"

#define ISP_TAG                     'pIpA'
#define ISP_MAX_PRESETS             16
#define ISP_MAX_IPC_CHANNELS        8
#define ISP_MAX_REGIONS             4
#define ISP_COPROC_WINDOWS          4
#define ISP_FRAME_COUNT             4
#define ISP_META_COUNT              16
#define ISP_CAMERA_CHANNEL          0
#define ISP_STRIDE_ALIGNMENT        64
#define ISP_SHARED_WINDOW_END       0x10000000ULL
#define ISP_STAGE_TIMEOUT_MS        1000
#define ISP_COMMAND_TIMEOUT_MS      3000
#define ISP_BUFFER_TIMEOUT_MS       1500
#define ISP_RESET_POLLS             128
#define ISP_MAX_SURFACE             (64 * 1024 * 1024)
#define ISP_COMMAND_SPACE           0x800
#define ISP_FRAME_INTERVAL          333333
#define ISP_FRAME_RATE              30

typedef struct _ISP_HARDWARE
{
    PCSTR Compatible;
    ULONG64 PmuBase;
    ULONG64 DsidClearBase;
    ULONG DsidClearRange;
    ULONG64 ClockScratch;
    ULONG64 ClockBase;
    UCHAR ClockBit;
    UCHAR ClockSize;
    ULONG64 BandwidthScratch;
    ULONG64 BandwidthBase;
    UCHAR BandwidthBit;
    UCHAR BandwidthSize;
    ULONG MailboxIrqEnable;
    ULONG MetaSize;
} ISP_HARDWARE, *PISP_HARDWARE;

typedef struct _ISP_SENSOR
{
    ULONG Version;
    ULONG Magic;
    PCWSTR SetFile;
    ULONG SetFileSize;
} ISP_SENSOR, *PISP_SENSOR;

typedef struct _ISP_PRESET
{
    ULONG ConfigIndex;
    ULONG InputWidth;
    ULONG InputHeight;
    ULONG OutputWidth;
    ULONG OutputHeight;
    ULONG CropX;
    ULONG CropY;
    ULONG CropWidth;
    ULONG CropHeight;
    ULONG Stride;
    ULONG LumaSize;
    ULONG FrameSize;
    ULONG ImageSize;
} ISP_PRESET, *PISP_PRESET;

typedef struct _ISP_REGION
{
    ULONG64 Physical;
    ULONG64 Iova;
    ULONG64 Size;
    BOOLEAN Mapped;
    CHAR Name[16];
} ISP_REGION, *PISP_REGION;

typedef struct _ISP_SURFACE
{
    LIST_ENTRY Link;
    PMDL Mdl;
    PUCHAR Virtual;
    ULONG64 Iova;
    SIZE_T Size;
    ULONG Type;
} ISP_SURFACE, *PISP_SURFACE;

typedef struct _ISP_CHANNEL
{
    CHAR Name[32];
    ULONG Type;
    ULONG Source;
    ULONG Count;
    ULONG Doorbell;
    ULONG Cursor;
    ULONG64 Iova;
    ULONG64 Failed;
    volatile ISP_MESSAGE *Ring;
    ISP_MESSAGE Request;
    ISP_MESSAGE Reply;
} ISP_CHANNEL, *PISP_CHANNEL;

typedef enum _ISP_FIRMWARE_STATE
{
    IspFirmwareOff = 0,
    IspFirmwareStarting,
    IspFirmwareRunning,
    IspFirmwareStopping
} ISP_FIRMWARE_STATE;

typedef enum _ISP_FRAME_STATE
{
    IspFrameFree = 0,
    IspFrameSubmitted,
    IspFrameReady,
    IspFrameCopying
} ISP_FRAME_STATE;

typedef struct _ISP_FRAME
{
    PISP_SURFACE Surface;
    ISP_FRAME_STATE State;
    ULONGLONG Time;
} ISP_FRAME, *PISP_FRAME;

typedef struct _ISP_META
{
    PISP_SURFACE Surface;
    BOOLEAN Submitted;
} ISP_META, *PISP_META;

typedef struct _ISP_DEVICE
{
    PKSDEVICE KsDevice;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    BOOLEAN HardwareStarted;
    const ISP_HARDWARE *Hardware;
    const ISP_SENSOR *Sensor;

    ULONG PlatformId;
    ULONG TemporalFilter;
    ULONG64 DartVmSize;
    ULONG FirmwareVersion[3];
    ULONG FirmwareCompat[3];
    ULONG PresetCount;
    ISP_PRESET Presets[ISP_MAX_PRESETS];
    ULONG RegionCount;
    ISP_REGION Regions[ISP_MAX_REGIONS];
    ULONG64 HeapTop;

    PUCHAR CoprocWindow[ISP_COPROC_WINDOWS];
    PUCHAR Mailbox;
    ULONG MailboxSize;
    PUCHAR Gpio;
    ULONG GpioSize;
    PUCHAR Mailbox2;
    ULONG Mailbox2Size;
    ULONG Revision;

    APPLE_DART_DOMAIN Dart;
    BOOLEAN DartReady;

    KMUTEX SurfaceLock;
    RTL_BITMAP IovaMap;
    PULONG IovaBits;
    ULONG64 IovaBase;
    ULONG IovaPages;
    LIST_ENTRY FirmwareSurfaces;
    PISP_SURFACE Ipc;
    PISP_SURFACE Data;
    PISP_SURFACE Extra;
    PISP_SURFACE LogSurface;
    PISP_SURFACE BufferListSurface;

    PKINTERRUPT Interrupt;
    CM_PARTIAL_RESOURCE_DESCRIPTOR InterruptResource;
    BOOLEAN HaveInterrupt;
    KDPC Dpc;
    KEVENT ReplyEvent;
    KEVENT WorkEvent;
    PKTHREAD Worker;
    volatile LONG StopWorker;
    volatile LONG Interrupts;
    volatile LONG InterruptLogged;

    KMUTEX CommandLock;
    KMUTEX ChannelLock;
    volatile LONG FirmwareState;
    BOOLEAN CorePowered;
    BOOLEAN IrqEnabled;
    BOOLEAN ChannelsReady;
    ULONG ChannelCount;
    ISP_CHANNEL Channels[ISP_MAX_IPC_CHANNELS];
    PISP_CHANNEL Terminal;
    PISP_CHANNEL Io;
    PISP_CHANNEL Debug;
    PISP_CHANNEL BufferToFirmware;
    PISP_CHANNEL BufferToHost;
    PISP_CHANNEL SharedMalloc;
    PISP_CHANNEL IoToHost;
    ULONG64 CommandIova;
    PUCHAR Command;
    ULONG CommandSpace;

    ULONG SensorChannels;
    ULONG SensorVersion;
    ULONG SensorModes;
    CHAR ModuleSerial[24];

    KSPIN_LOCK FrameLock;
    volatile LONG Streaming;
    PKSPIN Pin;
    PISP_PRESET StreamPreset;
    ISP_FRAME Frames[ISP_FRAME_COUNT];
    ISP_META Meta[ISP_META_COUNT];
    ULONG FramesDelivered;
    ULONG FramesDropped;
    BOOLEAN Discontinuity;

    PKSFILTER_DESCRIPTOR FilterDescriptor;
    PKSFILTERFACTORY FilterFactory;
} ISP_DEVICE, *PISP_DEVICE;

NTSTATUS IspHardwareStart(PISP_DEVICE Isp, PCM_RESOURCE_LIST Translated);
VOID IspHardwareStop(PISP_DEVICE Isp);
PISP_SURFACE IspAllocateSurface(PISP_DEVICE Isp, SIZE_T Size, ULONG Type);
VOID IspFreeSurface(PISP_DEVICE Isp, PISP_SURFACE Surface);
PVOID IspTranslate(PISP_SURFACE Surface, ULONG64 Iova, ULONG64 Length);
NTSTATUS IspFirmwareBoot(PISP_DEVICE Isp);
VOID IspFirmwareShutdown(PISP_DEVICE Isp);
NTSTATUS IspSend(PISP_DEVICE Isp, PISP_CHANNEL Channel, ULONG TimeoutMs);
NTSTATUS IspDetect(PISP_DEVICE Isp);

NTSTATUS IspCommand(PISP_DEVICE Isp, PVOID Arguments, ULONG InSize, ULONG OutSize);
NTSTATUS IspStartCommandProcessor(PISP_DEVICE Isp);
NTSTATUS IspDetectSensor(PISP_DEVICE Isp);
NTSTATUS IspConfigureCapture(PISP_DEVICE Isp, PISP_PRESET Preset);
NTSTATUS IspStartCapture(PISP_DEVICE Isp);
VOID IspStopCapture(PISP_DEVICE Isp);
NTSTATUS IspSubmitBuffers(PISP_DEVICE Isp);
NTSTATUS IspHandleBufferReturn(PISP_DEVICE Isp, PISP_CHANNEL Channel);

NTSTATUS IspBuildFilter(PISP_DEVICE Isp);
