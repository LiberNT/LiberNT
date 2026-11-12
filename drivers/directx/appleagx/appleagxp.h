/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     State shared by the parts of the Apple silicon graphics driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <reactos/drivers/appleadt.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appleuat.h>
#include <reactos/drivers/appleagx.h>
#include <reactos/drivers/appleagxinit.h>
#include <reactos/drivers/fdtdrv.h>

#define AGX_REGION_CONTEXTS         0
#define AGX_REGION_KERNEL_ROOT      1
#define AGX_REGION_HANDOFF          2
#define AGX_REGION_HOST_LEVEL       3
#define AGX_REGION_FIRMWARE_TEXT    4
#define AGX_REGION_FIRMWARE_DATA    5
#define AGX_REGIONS                 6
#define AGX_MAX_SHARED              8
#define AGX_MAX_VMS                 16
#define AGX_MAX_OBJECTS             4096
#define AGX_MAX_QUEUES              58
#define AGX_GROW_QUEUE              8
#define AGX_PB_CHUNKS               4
#define AGX_CHANNEL_TA              0
#define AGX_CHANNEL_FS              1
#define AGX_CHANNEL_COMPUTE         2
#define AGX_CHANNELS                3
#define AGX_MAX_BOUND               64
#define AGX_ARENA_PAGES             (APPLE_AGX_TIMESTAMPS_SIZE / APPLE_UAT_PAGE_SIZE)

typedef struct _AGX_OBJECT
{
    LONG References;
    ULONG Flags;
    ULONG VmId;
    ULONG64 Size;
    PMDL Mdl;
    PVOID UserAddress;
} AGX_OBJECT, *PAGX_OBJECT;

typedef struct _AGX_ENGINE AGX_ENGINE, *PAGX_ENGINE;

typedef struct _AGX_VM
{
    APPLE_AGX_VM_RANGES Ranges;
    APPLE_UAT Uat;
    APPLE_AGX_BINDINGS Bindings;
    PAGX_ENGINE Engine;
    ULONG Slot;
} AGX_VM, *PAGX_VM;

typedef struct _AGX_QUEUE
{
    ULONG VmId;
    ULONG Priority;
    ULONG64 UscBase;
} AGX_QUEUE, *PAGX_QUEUE;

typedef struct _AGX_BOUND
{
    PAGX_OBJECT Object;
    ULONG64 Address;
    ULONG64 Range;
} AGX_BOUND, *PAGX_BOUND;

typedef struct _AGX_CLIENT
{
    FAST_MUTEX Lock;
    PEPROCESS Process;
    struct _APPLE_AGX *Agx;
    PAGX_VM Vm[AGX_MAX_VMS];
    PAGX_OBJECT Object[AGX_MAX_OBJECTS];
    PAGX_QUEUE Queue[AGX_MAX_QUEUES];
    AGX_BOUND Bound[AGX_MAX_BOUND];
} AGX_CLIENT, *PAGX_CLIENT;

typedef struct _AGX_SHARED
{
    PVOID Virtual;
    ULONG64 Physical;
    ULONG Size;
} AGX_SHARED, *PAGX_SHARED;

/* The firmware objects the host builds once the firmware is ready: the render and compute
 * queues, their allocators, one render pass slot and one compute slot. */
typedef enum _AGX_ITEM
{
    AgxTaInfo,
    AgxTaState,
    AgxTaRing,
    AgxTaScratch,
    AgxTaUserStamp,
    AgxTaFirmwareStamp,
    AgxFsInfo,
    AgxFsState,
    AgxFsRing,
    AgxFsScratch,
    AgxFsUserStamp,
    AgxFsFirmwareStamp,
    AgxRenderSched,
    AgxRenderNotifier,
    AgxRenderThreshold,
    AgxRenderJobList,
    AgxRenderEmpty,
    AgxInitBm,
    AgxComputeLegacy,
    AgxComputeFreelist,
    AgxComputePoolTable,
    AgxComputeManager,
    AgxComputeCounter,
    AgxComputeSched,
    AgxComputeNotifier,
    AgxComputeThreshold,
    AgxComputeJobList,
    AgxComputeInfo,
    AgxComputeState,
    AgxComputeRing,
    AgxComputeScratch,
    AgxComputeUserStamp,
    AgxComputeFirmwareStamp,
    AgxUma0Freelist,
    AgxUma0Table,
    AgxUma0Manager,
    AgxUma0Counter,
    AgxUma1Freelist,
    AgxUma1Table,
    AgxUma1Manager,
    AgxUma1Counter,
    AgxPbBlockControl,
    AgxPbCounter,
    AgxPbMisc,
    AgxPbSceneList,
    AgxPbManager,
    AgxPbPages,
    AgxPbBlocks,
    AgxPassAux,
    AgxPassPre0,
    AgxPassPre1,
    AgxPassPre2,
    AgxPassTpc,
    AgxPassTilemap,
    AgxPassHeap,
    AgxPassSceneUser,
    AgxPassFsScratch,
    AgxPassFsWork,
    AgxPassTaWork,
    AgxPassTaSequence,
    AgxPassFsSequence,
    AgxPassTaScratch,
    AgxPassScene,
    AgxPassStamps,
    AgxPassUserStamps,
    AgxPassOwnBarrier,
    AgxPassPrevBarrier,
    AgxSlotStamps,
    AgxSlotUserStamps,
    AgxSlotPreempt,
    AgxSlotWork,
    AgxSlotTail,
    AgxSlotSequence,
    AgxSlotScratch,
    AgxSlotBarrier = AgxSlotScratch + APPLE_AGX_COMPUTE_SCRATCHES,
    AgxComputePool,
    AgxUmaPool = AgxComputePool + APPLE_AGX_POOLS,
    AgxPbChunk = AgxUmaPool + 2 * APPLE_AGX_UMA_POOLS,
    AgxItems = AgxPbChunk + AGX_PB_CHUNKS
} AGX_ITEM;

struct _AGX_ENGINE
{
    FAST_MUTEX Lock;
    BOOLEAN Ready;
    BOOLEAN Broken;
    BOOLEAN RenderRegistered;
    BOOLEAN ComputeRegistered;
    BOOLEAN C1Written;
    APPLE_AGX_HEAP Heap;
    APPLE_AGX_OBJECT Item[AgxItems];
    APPLE_AGX_QUEUE Queue[AGX_CHANNELS];
    APPLE_AGX_RING Channel[AGX_CHANNELS];
    APPLE_AGX_GROWTH Growth;
    ULONG64 Draws;
    ULONG64 Computes;
    ULONG64 Slots;
    ULONG64 TpcDirty;
    ULONG64 TilemapDirty;
    ULONG Grow[AGX_GROW_QUEUE][APPLE_AGX_RECORD_WORDS];
    ULONG Grows;
    ULONG GrowLost;
    ULONG Errors;
    ULONG LastError[APPLE_AGX_RECORD_WORDS];
    ULONG Context;
    UCHAR Arena[AGX_ARENA_PAGES / 8];
};

typedef struct _APPLE_AGX
{
    PFILE_OBJECT PmpFile;
    PDEVICE_OBJECT Pmp;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    PUCHAR Asc;
    ULONG AscSize;
    PUCHAR Id;
    ULONG IdSize;
    PUCHAR Core;
    ULONG CoreSize;
    ULONG64 RegionBase[AGX_REGIONS];
    ULONG64 RegionSize[AGX_REGIONS];
    APPLE_UAT_ADOPTED Adopted;
    BOOLEAN Owned;
    APPLE_AGX_INIT Init;
    BOOLEAN Initialised;
    APPLE_AGX_RING Event;
    APPLE_AGX_RING Trace;
    APPLE_AGX_RING Stats;
    APPLE_AGX_RING DevCtl;
    BOOLEAN Bound;
    ULONG Drained;
    APPLE_AGX_REPORT Reported;
    ULONG IdVersion;
    ULONG64 CoreWord;
    ULONG ChipId;
    KEVENT StopEvent;
    PKTHREAD StartThread;
    PVOID AdtCopy;
    ULONG AdtLength;
    APPLE_ADT Adt;
    APPLE_AGX_DESCRIPTION Description;
    BOOLEAN Described;
    PUCHAR Mailbox;
    ULONG MailboxSize;
    APPLE_RTKIT Rtkit;
    AGX_SHARED Shared[AGX_MAX_SHARED];
    ULONG SharedCount;
    AGX_ENGINE Engine;
} APPLE_AGX, *PAPPLE_AGX;

NTSTATUS
AgxDeviceControlPost(
    _Inout_ PAPPLE_AGX Agx,
    _In_ const VOID *Record,
    _In_ ULONG Timeout);

VOID
AgxDrainRings(
    _Inout_ PAPPLE_AGX Agx);

NTSTATUS
AgxEngineStart(
    _Inout_ PAPPLE_AGX Agx);

VOID
AgxEngineStop(
    _Inout_ PAPPLE_AGX Agx);

VOID
AgxEngineEvent(
    _Inout_ PAPPLE_AGX Agx,
    _In_reads_(APPLE_AGX_RECORD_WORDS) const ULONG *Record);

VOID
AgxEngineRelease(
    _Inout_ PAGX_VM Vm);

NTSTATUS
AgxEngineMapArena(
    _Inout_ PAPPLE_AGX Agx,
    _In_ PMDL Mdl,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Range,
    _Out_ PULONG64 Address);

VOID
AgxEngineUnmapArena(
    _Inout_ PAPPLE_AGX Agx,
    _In_ ULONG64 Address,
    _In_ ULONG64 Range);

NTSTATUS
AgxSubmitCommands(
    _Inout_ PAPPLE_AGX Agx,
    _Inout_ PAGX_CLIENT Client,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length);
