/*
 * PROJECT:     LiberNT SpacemiT K1 display miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     K1 display controller planes and PowerVR GPU hosted by the software GPU engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/powervr_umd.h>

#include "glue/powervr_glue.h"
#include "pvr_k1.h"

#define K1XDPU_TAG                       'pDiK'
#define K1XDPU_WORKING_SURFACE_COUNT     12UL

#define K1X_HDMI_BASE                    0xC0400500ULL
#define K1X_HDMI_SIZE                    0x200
#define K1X_HDMI_DDC_TX                  0x00
#define K1X_HDMI_DDC_RX                  0x04
#define K1X_HDMI_DDC_COMMAND             0x08
#define K1X_HDMI_STATUS                  0x0C
#define K1X_HDMI_STATUS_RX_COUNT_SHIFT   4
#define K1X_HDMI_STATUS_RX_COUNT_MASK    0x1F0
#define K1X_HDMI_STATUS_HPD              (1UL << 12)
#define K1X_HDMI_STATUS_DDC_DONE         (1UL << 14)
#define K1X_HDMI_STATUS_DDC_NACK         (1UL << 15)
#define K1X_HDMI_STATUS_DDC_LOST         (1UL << 16)
#define K1X_HDMI_DDC_CHUNK               16
#define K1X_EDID_ADDRESS                 0x50
#define K1X_EDID_BLOCK                   128
#define K1X_EDID_MAX                     256
#define K1X_DDC_TIMEOUT_US               50000
#define K1X_DDC_POLL_US                  20

#define K1X_APMU_BASE                    0xD4282800ULL
#define K1X_APMU_SIZE                    0x100
#define K1X_APMU_POWER_STATUS            0xF0
#define K1X_APMU_POWER_LCD               (1UL << 12)
#define K1X_APMU_POWER_HDMI              (1UL << 15)

#define K1X_DPU_SIZE                     0x2A000
#define K1X_DPU_CTL2_CHANNELS            0x560
#define K1X_DPU_CTL2_CHANNEL_MASK        0xFFFUL
#define K1X_DPU_CTL2_COMMIT              0x56C
#define K1X_DPU_CTL2_COMMIT_READY        (1UL << 0)
#define K1X_DPU_INT_ONLINE2_MASK         0x910
#define K1X_DPU_INT_ONLINE2_STATUS       0x938
#define K1X_DPU_INT_ONLINE2_RAW          0x960
#define K1X_DPU_INT_VSYNC                (1UL << 0)
#define K1X_DPU_INT_COMMIT_TAKEN         (1UL << 4)
#define K1X_DPU_INT_UNDERFLOW            (1UL << 9)

#define K1X_DPU_CHANNEL_COUNT            4
#define K1X_DPU_CHANNEL(Index)           (0xA80 + (Index) * 0x100)
#define K1X_CHANNEL_CONTROL              0x00
#define K1X_CHANNEL_COMPOSER_Y           0x04
#define K1X_CHANNEL_SCALE_RATIO_V        0x08
#define K1X_CHANNEL_BASE_LOW             0x20
#define K1X_CHANNEL_BASE_HIGH            0x24
#define K1X_CHANNEL_STRIDE               0x38
#define K1X_CHANNEL_IMAGE_SIZE           0x3C
#define K1X_CHANNEL_CROP_START           0x40
#define K1X_CHANNEL_CROP_END             0x44
#define K1X_CHANNEL_FORMAT               0x70
#define K1X_CHANNEL_LINE_MEMORY          0x78
#define K1X_CHANNEL_ALPHA01              0x98
#define K1X_CHANNEL_ALPHA23              0x9C
#define K1X_CHANNEL_CONTROL_OUTSTANDING  (16UL << 2)
#define K1X_CHANNEL_CONTROL_COMPOSER2    (2UL << 12)
#define K1X_CHANNEL_CONTROL_BURST        (16UL << 17)
#define K1X_CHANNEL_FORMAT_ARGB8888      4UL
#define K1X_CHANNEL_FORMAT_XRGB8888      8UL
#define K1X_CHANNEL_BASE_HIGH_MASK       3UL
#define K1X_LINE_MEMORY_UNIT             32UL
#define K1X_LINE_MEMORY_ALIGNMENT        64UL
#define K1X_LINE_MEMORY_PAIR0_BYTES      44544UL
#define K1X_LINE_MEMORY_START_SHIFT      16
#define K1X_LINE_MEMORY_MAPPED           (1UL << 28)
#define K1X_DPU_TRANSLATION_CONTROL(Channel) (0x1780 + (Channel) * 2 * 0x40)

#define K1X_DPU_COMPOSER2                0x4C00
#define K1X_COMPOSER_SLOT_COUNT          8
#define K1X_COMPOSER_SLOT(Index)         (K1X_DPU_COMPOSER2 + 0x38 + (Index) * 0x20)
#define K1X_SLOT_CONTROL                 0x00
#define K1X_SLOT_LEFT                    0x10
#define K1X_SLOT_TOP_RIGHT               0x14
#define K1X_SLOT_BOTTOM_BLEND            0x18
#define K1X_SLOT_ALPHA                   0x1C
#define K1X_SLOT_CONTROL_ENABLE          (1UL << 0)
#define K1X_SLOT_CONTROL_CHANNEL_SHIFT   1
#define K1X_SLOT_CONTROL_CHANNEL_MASK    (0xFUL << K1X_SLOT_CONTROL_CHANNEL_SHIFT)
#define K1X_SLOT_LEFT_SHIFT              8
#define K1X_SLOT_RIGHT_SHIFT             16
#define K1X_SLOT_PIXEL_ALPHA             (1UL << 18)
#define K1X_SLOT_ALPHA_OPAQUE            (0xFFUL << 16)

#define K1X_CURSOR_CHANNEL               0
#define K1X_OVERLAY_CHANNEL              1
#define K1X_CURSOR_SLOT                  0
#define K1X_OVERLAY_SLOT                 6
#define K1X_PRIMARY_SLOT                 7
#define K1X_FIRMWARE_SLOT                0
#define K1X_CURSOR_SIZE                  64UL
#define K1X_CURSOR_BYTES                 (K1X_CURSOR_SIZE * K1X_CURSOR_SIZE * sizeof(ULONG))

#define K1X_COMMIT_TIMEOUT_US            100000
#define K1X_COMMIT_POLL_US               50
#define K1X_VSYNC_TIMEOUT_MS             50
#define K1X_OVERLAY_WAIT_ROUNDS          4
#define K1X_SCANOUT_HIGHEST_ADDRESS      0x7FFFFFFFULL
#define K1X_SCANOUT_MAGIC                0x4F53314BUL
#define K1X_SCANOUT_LIMIT                 16
#define K1X_CHANNEL_STRIDE_MAX            0xFFFFUL
#define K1X_GPU_DEVICE_MAGIC             0x4447314BUL

struct _K1XDPU_CONTEXT;

typedef struct _K1X_SCANOUT
{
    ULONG Magic;
    LONG References;
    struct _K1XDPU_CONTEXT *Context;
    PVOID Address;
    PHYSICAL_ADDRESS Physical;
    SIZE_T Size;
    WORK_QUEUE_ITEM FreeItem;
} K1X_SCANOUT, *PK1X_SCANOUT;

typedef struct _K1X_FLIP
{
    LIST_ENTRY Entry;
    struct _K1XDPU_CONTEXT *Context;
    PK1X_SCANOUT Buffer;
    PVOID Owner;
    PVOID Fence;
    PVOID Wait;
    ULONG Handle;
    LONG Left;
    LONG Top;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    BOOLEAN Ready;
} K1X_FLIP, *PK1X_FLIP;

typedef struct _K1X_GPU_DEVICE
{
    ULONG Magic;
    PVOID File;
} K1X_GPU_DEVICE, *PK1X_GPU_DEVICE;

typedef enum _K1X_OVERLAY_COMMIT
{
    K1xOverlayCommitNone,
    K1xOverlayCommitFlip,
    K1xOverlayCommitOff
} K1X_OVERLAY_COMMIT;

typedef struct _K1XDPU_CONTEXT
{
    PSOFTGPU_DEVICE Device;
    DXGK_INTERFACE Dxgk;
    PUCHAR Hdmi;
    PUCHAR Dpu;
    const CHAR *Output;
    ULONG EdidLength;
    UCHAR Edid[K1X_EDID_MAX];

    BOOLEAN PlanesReady;
    BOOLEAN InterruptConnected;
    PKINTERRUPT Interrupt;
    KDPC InterruptDpc;
    volatile LONG InterruptBits;
    ULONG InterruptMask;
    KSPIN_LOCK Lock;
    KEVENT VsyncEvent;
    KEVENT LatchEvent;
    ULONG VsyncWaiters;
    ULONG Channels;
    BOOLEAN CommitPending;
    BOOLEAN UnderflowReported;
    KBUGCHECK_CALLBACK_RECORD BugCheckRecord;
    BOOLEAN BugCheckRegistered;
    ULONG PrimaryChannel;
    ULONG FirmwareSlot[4];

    PVOID CursorBuffer;
    PHYSICAL_ADDRESS CursorPhysical;
    ULONG CursorShapeGeneration;
    BOOLEAN CursorDirty;
    BOOLEAN CursorVisible;
    RECT CursorDestination;
    ULONG CursorSourceX;
    ULONG CursorSourceY;

    FAST_MUTEX OverlayMutex;
    PK1X_FLIP OverlayFront;
    PK1X_FLIP OverlayPending;
    PK1X_FLIP OverlayNext;
    K1X_OVERLAY_COMMIT OverlayCommit;
    BOOLEAN OverlayShown;
    BOOLEAN OverlayHide;
    LIST_ENTRY RetiredFlips;
    WORK_QUEUE_ITEM RetireItem;
    volatile LONG RetireQueued;
    KEVENT RetireIdle;

    FAST_MUTEX GpuMutex;
    PVOID GpuPlatformRegisters;
    PVOID GpuCore;
    PHYSICAL_ADDRESS GpuRegistersPhysical;
    ULONG GpuRegistersLength;
    PUCHAR GpuRegisters;
    ULONG GpuInterruptVector;
    KIRQL GpuInterruptIrql;
    BOOLEAN GpuInterruptLevelSensitive;
    BOOLEAN GpuInterruptFound;
    BOOLEAN GpuDriverInitialized;
    BOOLEAN GpuPowered;
    USHORT GpuBranch;
    USHORT GpuVersion;
    USHORT GpuScalableUnits;
    USHORT GpuConfig;
    volatile LONG ScanoutCount;
} K1XDPU_CONTEXT, *PK1XDPU_CONTEXT;

FORCEINLINE
ULONG
K1xRead(_In_ PUCHAR Base, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Base + Offset));
}

FORCEINLINE
VOID
K1xWrite(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Base + Offset), Value);
}

ULONG
K1xFindName(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PCWSTR Property,
    _In_z_ const CHAR *Wanted);

PCM_PARTIAL_RESOURCE_DESCRIPTOR
K1xFindResource(
    _In_ const DXGK_DEVICE_INFO *Information,
    _In_ UCHAR Type,
    _In_ ULONG Ordinal);

NTSTATUS
K1xPlanesStart(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ const DXGK_DEVICE_INFO *Information,
    _In_z_ const CHAR *Output);

VOID
K1xPlanesStop(_Inout_ PK1XDPU_CONTEXT Context);

VOID
K1xPlanesKick(_Inout_ PK1XDPU_CONTEXT Context);

BOOLEAN
K1xPlanesWaitVsync(_Inout_ PK1XDPU_CONTEXT Context);

NTSTATUS
K1xPlanesUpdatePointer(_Inout_ PK1XDPU_CONTEXT Context);

BOOLEAN
K1xOverlayQueue(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ PK1X_FLIP Flip);

VOID
K1xOverlayHide(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_opt_ PVOID Owner);

VOID
K1xOverlayBusy(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ PVOID Owner,
    _Out_writes_(POWERVR_SCANOUT_BUSY_COUNT) PULONG Handles);

VOID
K1xOverlayWait(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ const VOID *Flip);

VOID
K1xOverlayDrain(_Inout_ PK1XDPU_CONTEXT Context);

VOID
K1xFlipDestroy(_In_ PK1X_FLIP Flip);

VOID
K1xScanoutReference(_Inout_ PK1X_SCANOUT Buffer);

VOID
K1xScanoutDereference(_Inout_ PK1X_SCANOUT Buffer);

NTSTATUS
K1xGpuStart(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ const DXGK_DEVICE_INFO *Information);

VOID
K1xGpuStop(_Inout_ PK1XDPU_CONTEXT Context);

NTSTATUS
K1xGpuEscape(
    _Inout_ PK1XDPU_CONTEXT Context,
    _In_ CONST DXGKARG_ESCAPE *Escape);
