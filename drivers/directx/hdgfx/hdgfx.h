/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adapter state shared by the hdgfx display and submission code
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <ntddk.h>
#include <windef.h>
#include <dispmprt.h>
#include <reactos/drivers/directx/softgpu_2d_shared.h>

#define NDEBUG
#include <debug.h>

#include "core/hdgfx_core.h"
#include "core/hdgfx_regs.h"

#define HDGFX_TAG                   'xfDH'
#define HDGFX_ADAPTER_MAGIC         0x41584648UL
#define HDGFX_DEVICE_MAGIC          0x44584648UL
#define HDGFX_PROCESS_MAGIC         0x50584648UL
#define HDGFX_CONTEXT_MAGIC         0x43584648UL
#define HDGFX_ALLOCATION_MAGIC      0x4C584648UL
#define HDGFX_OPEN_MAGIC            0x4F584648UL
#define HDGFX_SLAB_SEGMENT_ID       1UL
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
#define HDGFX_PANEL_CHILD_UID       0UL
#define HDGFX_CRT_CHILD_UID         1UL
#define HDGFX_CRT_PIPE              1UL
#define HDGFX_CHILD_COUNT           2UL
#define HDGFX_MAX_MODES             32UL
#define HDGFX_BYTES_PER_PIXEL       4UL
#define HDGFX_STRIDE_ALIGNMENT      64UL

typedef struct _HDGFX_MODE
{
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    ULONG Timing;
    ULONG TotalWidth;
    ULONG TotalHeight;
    ULONG PixelRate;
} HDGFX_MODE, *PHDGFX_MODE;

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
    LONG PointerX;
    LONG PointerY;
    ULONG PointerHotX;
    ULONG PointerHotY;
    BOOLEAN PointerVisible;
    BOOLEAN PointerShapeValid;
    ULONG FirmwareSize;
    HDGFX_MODE Modes[HDGFX_MAX_MODES];
    ULONG ModeCount;
    ULONG Mode;
    UCHAR PanelEdid[HDGFX_EDID_SIZE];
    ULONG PanelEdidLength;
    BOOLEAN CrtSupported;
    BOOLEAN CrtConnected;
    UCHAR CrtEdid[HDGFX_EDID_SIZE];
    ULONG CrtEdidLength;
    HDGFX_MODE CrtModes[HDGFX_MAX_MODES];
    ULONG CrtModeCount;
    BOOLEAN CrtStale;
    KMUTEX OutputLock;
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
    ULONG Magic;
    PDEVICE_OBJECT PhysicalDeviceObject;
    DXGK_INTERFACE DxgkInterface;
    volatile LONG Stopped;
    BOOLEAN Started;
    PUCHAR Slab;
    PHYSICAL_ADDRESS SlabPhysical;
    SIZE_T SlabSize;
    PHDGFX_SCANOUT Scanout;
    BOOLEAN HardwarePointer;
    ULONG ChildAcpiUid;
    D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY ChildOutputTechnology;
    PEX_TIMER VsyncTimer;
    volatile LONG VsyncEnabled;
    volatile LONG VsyncPhaseEnabled;
    ULONG VsyncTargetId;
    volatile LONG64 PrimaryAddress;
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

typedef struct _HDGFX_PROCESS
{
    ULONG Magic;
    HANDLE hDxgkProcess;
    PHDGFX_ADAPTER Adapter;
} HDGFX_PROCESS, *PHDGFX_PROCESS;

typedef struct _HDGFX_DEVICE
{
    ULONG Magic;
    PHDGFX_ADAPTER Adapter;
    PHDGFX_PROCESS Process;
} HDGFX_DEVICE, *PHDGFX_DEVICE;

typedef struct _HDGFX_CONTEXT
{
    ULONG Magic;
    ULONG NodeOrdinal;
    PHDGFX_DEVICE Device;
    PHDGFX_PROCESS Process;
} HDGFX_CONTEXT, *PHDGFX_CONTEXT;

typedef struct _HDGFX_ALLOCATION
{
    ULONG Magic;
    SIZE_T Size;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    ULONG StorageHeight;
    D3DDDIFORMAT Format;
    ULONG PlaneCount;
    ULONG PlaneOffsets[SOFTGPU_ALLOCATION_MAX_PLANES];
    ULONG PlanePitches[SOFTGPU_ALLOCATION_MAX_PLANES];
} HDGFX_ALLOCATION, *PHDGFX_ALLOCATION;

typedef struct _HDGFX_OPEN
{
    ULONG Magic;
    PHDGFX_DEVICE Device;
    D3DKMT_HANDLE hAllocation;
    SIZE_T Size;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    ULONG StorageHeight;
    D3DDDIFORMAT Format;
    ULONG PlaneCount;
    ULONG PlaneOffsets[SOFTGPU_ALLOCATION_MAX_PLANES];
    ULONG PlanePitches[SOFTGPU_ALLOCATION_MAX_PLANES];
} HDGFX_OPEN, *PHDGFX_OPEN;

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

VOID
HdgfxLock(
    _In_ PHDGFX_SCANOUT Scanout);

VOID
HdgfxUnlock(
    _In_ PHDGFX_SCANOUT Scanout);

VOID
HdgfxCursorCommit(
    _In_ PHDGFX_SCANOUT Scanout);

VOID
HdgfxPlaneCommit(
    _In_ PHDGFX_SCANOUT Scanout);

NTSTATUS
HdgfxSetOutputs(
    _Inout_ PHDGFX_SCANOUT Scanout,
    _In_ const HDGFX_MODE *Source,
    _In_ BOOLEAN Panel,
    _In_ BOOLEAN Crt);

NTSTATUS
HdgfxSetMonitorPower(
    _Inout_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG ChildUid,
    _In_ BOOLEAN Off);

ULONG
HdgfxScanLine(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG Pipe);

NTSTATUS
HdgfxSetVsync(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ DXGK_CRTC_VSYNC_STATE VsyncState);

DXGKDDI_ADD_DEVICE HdgfxDdiAddDevice;
DXGKDDI_START_DEVICE HdgfxDdiStartDevice;
DXGKDDI_STOP_DEVICE HdgfxDdiStopDevice;
DXGKDDI_REMOVE_DEVICE HdgfxDdiRemoveDevice;
DXGKDDI_NOTIFY_ACPI_EVENT HdgfxDdiNotifyAcpiEvent;
DXGKDDI_INTERRUPT_ROUTINE HdgfxDdiInterruptRoutine;
DXGKDDI_DPC_ROUTINE HdgfxDdiDpcRoutine;
DXGKDDI_CREATECONTEXT HdgfxDdiCreateContext;
DXGKDDI_DESTROYCONTEXT HdgfxDdiDestroyContext;
DXGKDDI_RESET_DEVICE HdgfxDdiResetDevice;
DXGKDDI_COLLECTDIAGNOSTICINFO HdgfxDdiCollectDiagnosticInfo;
DXGKDDI_GETNODEMETADATA HdgfxDdiGetNodeMetadata;
DXGKDDI_CONTROLINTERRUPT HdgfxDdiControlInterrupt;
DXGKDDI_CONTROLINTERRUPT2 HdgfxDdiControlInterrupt2;
DXGKDDI_CONTROLINTERRUPT3 HdgfxDdiControlInterrupt3;
DXGKDDI_CREATEPROCESS HdgfxDdiCreateProcess;
DXGKDDI_DESTROYPROCESS HdgfxDdiDestroyProcess;
DXGKDDI_DESTROYDEVICE HdgfxDdiDestroyDevice;
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
DXGKDDI_SET_POWER_STATE HdgfxDdiSetPowerState;
DXGKDDI_QUERY_CHILD_RELATIONS HdgfxDdiQueryChildRelations;
DXGKDDI_QUERY_CHILD_STATUS HdgfxDdiQueryChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR HdgfxDdiQueryDeviceDescriptor;
DXGKDDI_ISSUPPORTEDVIDPN HdgfxDdiIsSupportedVidPn;
DXGKDDI_RECOMMENDFUNCTIONALVIDPN HdgfxDdiRecommendFunctionalVidPn;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY HdgfxDdiEnumVidPnCofuncModality;
DXGKDDI_SETVIDPNSOURCEADDRESS HdgfxDdiSetVidPnSourceAddress;
DXGKDDI_SETVIDPNSOURCEVISIBILITY HdgfxDdiSetVidPnSourceVisibility;
DXGKDDI_COMMITVIDPN HdgfxDdiCommitVidPn;
DXGKDDI_SETTIMINGSFROMVIDPN HdgfxDdiSetTimingsFromVidPn;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH HdgfxDdiUpdateActiveVidPnPresentPath;
DXGKDDI_RECOMMENDMONITORMODES HdgfxDdiRecommendMonitorModes;
DXGKDDI_RECOMMENDVIDPNTOPOLOGY HdgfxDdiRecommendVidPnTopology;
DXGKDDI_SETPOINTERPOSITION HdgfxDdiSetPointerPosition;
DXGKDDI_SETPOINTERSHAPE HdgfxDdiSetPointerShape;
DXGKDDI_SETPALETTE HdgfxDdiSetPalette;
DXGKDDI_GETSCANLINE HdgfxDdiGetScanLine;
