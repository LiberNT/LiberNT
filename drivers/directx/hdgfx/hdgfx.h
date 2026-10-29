/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adapter state shared by the hdgfx display and submission code
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include "softgpu.h"
#include "core/hdgfx_core.h"
#include "core/hdgfx_regs.h"

#define HDGFX_TAG                   'xfDH'
#define HDGFX_RING_SIZE             0x10000UL
#define HDGFX_RING_PAGES            (HDGFX_RING_SIZE / PAGE_SIZE)
#define HDGFX_DMA_SEGMENT_ID        2UL
#define HDGFX_DMA_SIZE              0x800000UL
#define HDGFX_DMA_PAGES             (HDGFX_DMA_SIZE / PAGE_SIZE)
#define HDGFX_RING_GTT_INDEX        HDGFX_DMA_PAGES
#define HDGFX_STATUS_GTT_INDEX      (HDGFX_RING_GTT_INDEX + HDGFX_RING_PAGES)
#define HDGFX_SCRATCH_GTT_INDEX     (HDGFX_STATUS_GTT_INDEX + 1)
#define HDGFX_RENDER_GTT_PAGES      (HDGFX_SCRATCH_GTT_INDEX + 1)
#define HDGFX_RING_SEQNO_INDEX      0x20UL
#define HDGFX_RING_FENCE_INDEX      0x21UL
#define HDGFX_RING_RESERVE          512UL
#define HDGFX_BLT_OVERLAP_PITCH     64UL
#define HDGFX_QUEUE_SIZE            1024UL

typedef struct _HDGFX_SCANOUT
{
    PUCHAR Registers;
    const HDGFX_GENERATION *Generation;
    HDGFX_DTD Panel;
    HDGFX_DTD PanelModes[HDGFX_EDID_MAX_DTDS];
    ULONG PanelModeCount;
    BOOLEAN Ssc;
    ULONG Surface;
    ULONG FirmwareSurface;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    BOOLEAN DisplayOff;
    BOOLEAN PanelBacklight;
    BOOLEAN LvdsActive;
    HDGFX_DTD CrtTiming;
    BOOLEAN CrtTimingValid;
    BOOLEAN CrtActive;
    BOOLEAN CrtDisplayOff;
    ULONGLONG Bar0;
    ULONG ApertureSize;
    PULONG SlabGtt;
    ULONG SlabGttPages;
    ULONG SlabAddress;
    ULONGLONG SlabPhysical;
    ULONGLONG SlabSize;
    PULONG Cursor;
    ULONG CursorAddress;
    ULONG CursorGeneration;
    ULONG CursorControl;
    ULONG CursorPosition;
    KMUTEX Lock;
} HDGFX_SCANOUT, *PHDGFX_SCANOUT;

typedef struct _HDGFX_SUBMIT
{
    PHYSICAL_ADDRESS DmaPhysical;
    ULONG SegmentId;
    ULONG Offset;
    ULONG EndOffset;
    ULONG Fence;
} HDGFX_SUBMIT, *PHDGFX_SUBMIT;

typedef struct _HDGFX_ADAPTER
{
    PSOFTGPU_DEVICE Device;
    PUCHAR Registers;
    const HDGFX_GENERATION *Generation;
    HDGFX_VBT Vbt;
    BOOLEAN VbtPresent;
    BOOLEAN InternalPanel;
    BOOLEAN AsleReady;
    BOOLEAN AsleInterrupt;
    PUCHAR OpRegion;
    ULONG SavedDeimr;
    ULONG SavedDeier;
    ULONG SavedSdeimr;
    ULONG SavedSdeier;
    ULONG InterruptMask;
    BOOLEAN HotplugInterrupt;
    volatile LONG AslePending;
    volatile LONG HotplugPending;
    volatile LONG HotplugQueued;
    WORK_QUEUE_ITEM HotplugWork;
    ULONG BacklightMaximum;
    ULONG BacklightMinimum;
    EX_RUNDOWN_REF Rundown;
    PVOID RingMemory;
    PVOID StatusPage;
    PVOID ScratchPage;
    PUCHAR DmaMemory;
    ULONGLONG DmaPhysical;
    ULONGLONG DmaBase;
    PULONG GttWindow;
    ULONG SavedGtt[HDGFX_RENDER_GTT_PAGES];
    KSPIN_LOCK RingLock;
    HDGFX_RING Ring;
    ULONG ScratchAddress;
    ULONG Seqno;
    BOOLEAN RingStarted;
    BOOLEAN RingInterrupt;
    BOOLEAN RingUnfenced;
    BOOLEAN RingResetting;
    volatile LONG RingResets;
    volatile LONG RingPending;
    ULONG GtInterruptMask;
    ULONG SavedGtimr;
    ULONG SavedGtier;
    volatile LONG RingInterrupts;
    KSPIN_LOCK QueueLock;
    KDPC QueueDpc;
    KTIMER QueueTimer;
    HDGFX_SUBMIT Queue[HDGFX_QUEUE_SIZE];
    ULONG QueueHead;
    ULONG QueueTail;
    BOOLEAN QueueStopped;
    volatile BOOLEAN QueueResetting;
    ULONG SubmittedFence;
    volatile ULONG CompletedFence;
    ULONG SoftwareFence;
    volatile LONG FenceNotified;
} HDGFX_ADAPTER, *PHDGFX_ADAPTER;

BOOLEAN
HdgfxRingFence(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ const HDGFX_MMIO *Mmio,
    _In_ ULONG Head);

VOID
HdgfxMmioInit(
    _In_ PVOID Base,
    _Out_ HDGFX_MMIO *Mmio);

NTSTATUS
HdgfxResetRender(
    _Inout_ PHDGFX_ADAPTER Adapter);

VOID
HdgfxRestartRender(
    _Inout_ PHDGFX_ADAPTER Adapter);

VOID
HdgfxQueueStart(
    _Inout_ PHDGFX_ADAPTER Adapter);

VOID
HdgfxQueueStop(
    _Inout_ PHDGFX_ADAPTER Adapter);

VOID
HdgfxQueueInterrupt(
    _Inout_ PHDGFX_ADAPTER Adapter);

VOID
HdgfxQueueDpc(
    _Inout_ PHDGFX_ADAPTER Adapter);

DXGKDDI_QUERYADAPTERINFO HdgfxDdiQueryAdapterInfo;
DXGKDDI_QUERY_INTERFACE HdgfxDdiQueryInterface;
DXGKDDI_CREATEDEVICE HdgfxDdiCreateDevice;
DXGKDDI_CREATEALLOCATION HdgfxDdiCreateAllocation;
DXGKDDI_DESTROYALLOCATION HdgfxDdiDestroyAllocation;
DXGKDDI_OPENALLOCATIONINFO HdgfxDdiOpenAllocation;
DXGKDDI_CLOSEALLOCATION HdgfxDdiCloseAllocation;
DXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA HdgfxDdiGetStandardAllocationDriverData;
DXGKDDI_VALIDATEUPDATEALLOCATIONPROPERTY HdgfxDdiValidateUpdateAllocationProperty;
DXGKDDI_RENDER HdgfxDdiRender;
DXGKDDI_PRESENT HdgfxDdiPresent;
DXGKDDI_BUILDPAGINGBUFFER HdgfxDdiBuildPagingBuffer;
DXGKDDI_PATCH HdgfxDdiPatch;
DXGKDDI_SUBMITCOMMAND HdgfxDdiSubmitCommand;
DXGKDDI_QUERYCURRENTFENCE HdgfxDdiQueryCurrentFence;
DXGKDDI_RESETFROMTIMEOUT HdgfxDdiResetFromTimeout;
DXGKDDI_RESTARTFROMTIMEOUT HdgfxDdiRestartFromTimeout;
