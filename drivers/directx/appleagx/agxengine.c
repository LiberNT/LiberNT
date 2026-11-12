/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Render and compute work on the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleagxp.h"

#define AGX_JOB_TIMEOUT_MS          2000
#define AGX_SPIN_US                 2000
#define AGX_SPIN_STEP_US            20
#define AGX_POST_TIMEOUT_MS         100
#define AGX_GROW_TIMEOUT_MS         500
#define AGX_GLOBALS_COMPUTE_MONITOR 0x9b0
#define AGX_MAX_CMDBUF              (APPLE_AGX_MAX_COMMANDS * (sizeof(DRM_ASAHI_CMD_HEADER) + 0x400))

#define FW      APPLE_AGX_CLASS_FIRMWARE
#define FWC     APPLE_AGX_CLASS_FIRMWARE_CACHED
#define SH      APPLE_AGX_CLASS_SHARED
#define SHC     APPLE_AGX_CLASS_SHARED_CACHED
#define NC      APPLE_AGX_LEAF_UNCACHED
#define WB      APPLE_AGX_LEAF_CACHED
#define PBL     APPLE_AGX_LEAF_PB_LIST

typedef struct _AGX_ITEM_SPEC
{
    ULONG Size;
    USHORT Offset;
    UCHAR Class;
    UCHAR Leaf;
    BOOLEAN Lower;
    ULONG64 Client;
} AGX_ITEM_SPEC;

static const AGX_ITEM_SPEC AgxItemSpec[AgxComputePool] =
{
    [AgxTaInfo] = { APPLE_AGX_QINFO_SIZE, 0, FWC },
    [AgxTaState] = { APPLE_AGX_QSTATE_SIZE, 0, FW },
    [AgxTaRing] = { APPLE_AGX_QUEUE_ENTRIES * APPLE_AGX_WORK_ENTRY, 0, FW },
    [AgxTaScratch] = { APPLE_AGX_QUEUE_SCRATCH_SIZE, 0, FWC },
    [AgxTaUserStamp] = { APPLE_AGX_STAMP_SIZE, 0, SH },
    [AgxTaFirmwareStamp] = { APPLE_AGX_STAMP_SIZE, 0, SH },
    [AgxFsInfo] = { APPLE_AGX_QINFO_SIZE, 0, FWC },
    [AgxFsState] = { APPLE_AGX_QSTATE_SIZE, 0, FW },
    [AgxFsRing] = { APPLE_AGX_QUEUE_ENTRIES * APPLE_AGX_WORK_ENTRY, 0, FW },
    [AgxFsScratch] = { APPLE_AGX_QUEUE_SCRATCH_SIZE, 0, FWC },
    [AgxFsUserStamp] = { APPLE_AGX_STAMP_SIZE, 0, SH },
    [AgxFsFirmwareStamp] = { APPLE_AGX_STAMP_SIZE, 0, SH },
    [AgxRenderSched] = { APPLE_AGX_SCHED_CONTEXT_SIZE, 0, FWC },
    [AgxRenderNotifier] = { APPLE_AGX_NOTIFIER_SIZE, 0, FWC },
    [AgxRenderThreshold] = { APPLE_AGX_THRESHOLD_SIZE, 0, FW },
    [AgxRenderJobList] = { APPLE_AGX_JOB_LIST_SIZE, 0, FWC },
    [AgxRenderEmpty] = { APPLE_AGX_LEGACY_EMPTY_SIZE, 0, SH },
    [AgxInitBm] = { APPLE_AGX_INIT_BM_SIZE, 0, FWC },
    [AgxComputeLegacy] = { APPLE_AGX_COMPUTE_LEGACY_SIZE, 0, SH },
    [AgxComputeFreelist] = { APPLE_AGX_FREELIST_SIZE, 0, SH, NC, FALSE, APPLE_AGX_CLIENT_FREELIST },
    [AgxComputePoolTable] = { APPLE_AGX_POOL_TABLE_SIZE, APPLE_AGX_POOL_TABLE_OFFSET, SH, NC, FALSE,
                              APPLE_AGX_CLIENT_POOL_TABLE },
    [AgxComputeManager] = { APPLE_AGX_MANAGER_SIZE, 0, SHC },
    [AgxComputeCounter] = { APPLE_AGX_COUNTER_SIZE, 0, SH },
    [AgxComputeSched] = { APPLE_AGX_SCHED_CONTEXT_SIZE, 0, FWC },
    [AgxComputeNotifier] = { APPLE_AGX_NOTIFIER_SIZE, 0, FW },
    [AgxComputeThreshold] = { APPLE_AGX_THRESHOLD_SIZE, 0, FW },
    [AgxComputeJobList] = { APPLE_AGX_JOB_LIST_SIZE, 0, FWC },
    [AgxComputeInfo] = { APPLE_AGX_QINFO_SIZE, 0, FWC },
    [AgxComputeState] = { APPLE_AGX_QSTATE_SIZE, 0, FW },
    [AgxComputeRing] = { APPLE_AGX_QUEUE_ENTRIES * APPLE_AGX_WORK_ENTRY, 0, FW },
    [AgxComputeScratch] = { APPLE_AGX_QUEUE_SCRATCH_SIZE, 0, FWC },
    [AgxComputeUserStamp] = { APPLE_AGX_STAMP_SIZE, 0, SH },
    [AgxComputeFirmwareStamp] = { APPLE_AGX_STAMP_SIZE, 0, SH },
    [AgxUma0Freelist] = { APPLE_AGX_FREELIST_SIZE, 0, SH, NC, FALSE, APPLE_AGX_CLIENT_UMA_FREELIST(0) },
    [AgxUma0Table] = { APPLE_AGX_POOL_TABLE_SIZE, APPLE_AGX_POOL_TABLE_OFFSET, SH, NC, FALSE,
                       APPLE_AGX_CLIENT_UMA_TABLE(0) },
    [AgxUma0Manager] = { APPLE_AGX_MANAGER_SIZE, 0, SHC },
    [AgxUma0Counter] = { APPLE_AGX_COUNTER_SIZE, 0, SH },
    [AgxUma1Freelist] = { APPLE_AGX_FREELIST_SIZE, 0, SH, NC, FALSE, APPLE_AGX_CLIENT_UMA_FREELIST(1) },
    [AgxUma1Table] = { APPLE_AGX_POOL_TABLE_SIZE, APPLE_AGX_POOL_TABLE_OFFSET, SH, NC, FALSE,
                       APPLE_AGX_CLIENT_UMA_TABLE(1) },
    [AgxUma1Manager] = { APPLE_AGX_MANAGER_SIZE, 0, SHC },
    [AgxUma1Counter] = { APPLE_AGX_COUNTER_SIZE, 0, SH },
    [AgxPbBlockControl] = { APPLE_AGX_BLOCK_CONTROL_SIZE, 0, FW },
    [AgxPbCounter] = { APPLE_AGX_COUNTER_SIZE, 0, FW },
    [AgxPbMisc] = { APPLE_AGX_MISC_SIZE, 0, FWC },
    [AgxPbSceneList] = { APPLE_AGX_SCENE_LIST_SIZE, 0, SH },
    [AgxPbManager] = { APPLE_AGX_PB_MANAGER_SIZE, 0, FWC },
    [AgxPbPages] = { APPLE_AGX_PB_PAGE_LIST_SIZE, 0, SH, PBL, FALSE, APPLE_AGX_CLIENT_PB_PAGE_LIST },
    [AgxPbBlocks] = { APPLE_AGX_PB_BLOCK_LIST_SIZE, 0, SH, PBL, FALSE, APPLE_AGX_CLIENT_PB_BLOCK_LIST },
    [AgxPassAux] = { APPLE_AGX_AUX_SIZE, 0, SH, NC, FALSE, APPLE_AGX_CLIENT_AUX },
    [AgxPassPre0] = { APPLE_AGX_PRE_RECORDS * APPLE_AGX_PRE0_RECORD, APPLE_AGX_PRE0_OFFSET, SH, WB, FALSE,
                      APPLE_AGX_CLIENT_PRE0 },
    [AgxPassPre1] = { APPLE_AGX_PRE_RECORDS * APPLE_AGX_PRE1_RECORD, APPLE_AGX_PRE1_OFFSET, SH, WB, FALSE,
                      APPLE_AGX_CLIENT_PRE1 },
    [AgxPassPre2] = { APPLE_AGX_PRE_RECORDS * APPLE_AGX_PRE2_RECORD, APPLE_AGX_PRE2_OFFSET, SH, WB, FALSE,
                      APPLE_AGX_CLIENT_PRE2 },
    [AgxPassTpc] = { (ULONG)APPLE_AGX_CLIENT_TPC_SIZE, 0, SH, WB, FALSE, APPLE_AGX_CLIENT_TPC },
    [AgxPassTilemap] = { (ULONG)APPLE_AGX_CLIENT_TILEMAP_SIZE, 0, SH, WB, FALSE, APPLE_AGX_CLIENT_TILEMAP },
    [AgxPassHeap] = { APPLE_AGX_HEAP_META_SIZE, APPLE_AGX_HEAP_META_OFFSET, SH, WB, FALSE,
                      APPLE_AGX_CLIENT_HEAP_META },
    [AgxPassSceneUser] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH, WB, FALSE, APPLE_AGX_CLIENT_SCENE_USER },
    [AgxPassFsScratch] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH, WB, FALSE, APPLE_AGX_CLIENT_FS_SCRATCH },
    [AgxPassFsWork] = { APPLE_AGX_FRAGMENT_WORK_SIZE, APPLE_AGX_CLIENT_FS_WORK_OFFSET, SHC, NC, TRUE,
                        APPLE_AGX_CLIENT_FS_WORK },
    [AgxPassTaWork] = { APPLE_AGX_VERTEX_WORK_SIZE, APPLE_AGX_CLIENT_TA_WORK_OFFSET, SHC, NC, TRUE,
                        APPLE_AGX_CLIENT_TA_WORK },
    [AgxPassTaSequence] = { APPLE_AGX_SEQUENCE_STORAGE, 0, FWC },
    [AgxPassFsSequence] = { APPLE_AGX_SEQUENCE_STORAGE, 0, FWC },
    [AgxPassTaScratch] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH },
    [AgxPassScene] = { APPLE_AGX_SCENE_SIZE, 0, SH },
    [AgxPassStamps] = { APPLE_AGX_STAMP_CELLS, 0, SH },
    [AgxPassUserStamps] = { APPLE_AGX_STAMP_CELLS, 0, FW },
    [AgxPassOwnBarrier] = { APPLE_AGX_BARRIER_SIZE, 0, FWC },
    [AgxPassPrevBarrier] = { APPLE_AGX_BARRIER_SIZE, 0, FWC },
    [AgxSlotStamps] = { APPLE_AGX_COMPUTE_CELLS, 0, SH },
    [AgxSlotUserStamps] = { APPLE_AGX_COMPUTE_CELLS, 0, FW },
    [AgxSlotPreempt] = { APPLE_AGX_PREEMPT_SIZE, 0, SH, NC, FALSE, APPLE_AGX_CLIENT_PREEMPT },
    [AgxSlotWork] = { APPLE_AGX_COMPUTE_WORK_SIZE, APPLE_AGX_CLIENT_WORK_OFFSET, SHC, NC, TRUE, APPLE_AGX_CLIENT_WORK },
    [AgxSlotTail] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH },
    [AgxSlotSequence] = { APPLE_AGX_SEQUENCE_STORAGE, 0, FWC },
    [AgxSlotScratch + 0] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH },
    [AgxSlotScratch + 1] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH },
    [AgxSlotScratch + 2] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH },
    [AgxSlotScratch + 3] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH },
    [AgxSlotScratch + 4] = { APPLE_AGX_PASS_BUFFER_SIZE, 0, SH },
    [AgxSlotBarrier] = { APPLE_AGX_BARRIER_SIZE, 0, FWC },
};

C_ASSERT(APPLE_AGX_COMPUTE_SCRATCHES == 5);

static
VOID
AgxItemRequest(
    _In_ ULONG Item,
    _Out_ PAPPLE_AGX_REQUEST Request)
{
    RtlZeroMemory(Request, sizeof(*Request));
    if (Item < AgxComputePool)
    {
        Request->Size = AgxItemSpec[Item].Size;
        Request->Offset = AgxItemSpec[Item].Offset;
        Request->Class = AgxItemSpec[Item].Class;
        Request->Leaf = AgxItemSpec[Item].Leaf;
        Request->Lower = AgxItemSpec[Item].Lower;
        Request->Client = AgxItemSpec[Item].Client;
        Request->Cpu = TRUE;
        return;
    }
    Request->Class = SH;
    if (Item < AgxUmaPool)
    {
        Request->Size = APPLE_AGX_POOL_SIZE;
        Request->Leaf = NC;
        Request->Client = APPLE_AGX_CLIENT_POOL(Item - AgxComputePool);
    }
    else if (Item < AgxPbChunk)
    {
        Request->Size = APPLE_AGX_POOL_SIZE;
        Request->Leaf = NC;
        Request->Client = APPLE_AGX_CLIENT_UMA((Item - AgxUmaPool) / APPLE_AGX_UMA_POOLS,
                                               (Item - AgxUmaPool) % APPLE_AGX_UMA_POOLS);
    }
    else
    {
        Request->Size = APPLE_AGX_PB_CHUNK_SIZE;
        Request->Leaf = WB;
        Request->Hidden = TRUE;
        Request->Client = APPLE_AGX_CLIENT_PB_HEAP + (ULONG64)(Item - AgxPbChunk) * APPLE_AGX_PB_CHUNK_SIZE;
    }
}

static
VOID
AgxWrite32(
    _In_ const APPLE_AGX_OBJECT *Object,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    ASSERT(Object->Virtual);
    WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Object->Virtual + Offset), Value);
}

static
ULONG
AgxRead32(
    _In_ const APPLE_AGX_OBJECT *Object,
    _In_ ULONG Offset)
{
    ASSERT(Object->Virtual);
    return READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Object->Virtual + Offset));
}

static
VOID
AgxDescribeQueue(
    _Inout_ PAGX_ENGINE Engine,
    _In_ ULONG Channel,
    _In_ ULONG First,
    _In_ ULONG JobList,
    _In_ ULONG Sched,
    _In_ ULONG Uuid)
{
    PAPPLE_AGX_QUEUE Queue = &Engine->Queue[Channel];
    PAPPLE_AGX_OBJECT Item = Engine->Item;

    RtlZeroMemory(Queue, sizeof(*Queue));
    Queue->Info = Item[First].Address;
    Queue->State = Item[First + 1].Address;
    Queue->Ring = Item[First + 2].Address;
    Queue->Scratch = Item[First + 3].Address;
    Queue->JobList = Item[JobList].Address;
    Queue->SchedContext = Item[Sched].Address;
    Queue->InfoVirtual = Item[First].Virtual;
    Queue->StateVirtual = Item[First + 1].Virtual;
    Queue->RingVirtual = Item[First + 2].Virtual;
    Queue->Entries = APPLE_AGX_QUEUE_ENTRIES;
    Queue->Uuid = Uuid;
}

C_ASSERT(AgxTaState == AgxTaInfo + 1 && AgxTaRing == AgxTaInfo + 2 && AgxTaScratch == AgxTaInfo + 3);
C_ASSERT(AgxFsState == AgxFsInfo + 1 && AgxFsRing == AgxFsInfo + 2 && AgxFsScratch == AgxFsInfo + 3);
C_ASSERT(AgxComputeState == AgxComputeInfo + 1 && AgxComputeRing == AgxComputeInfo + 2 &&
         AgxComputeScratch == AgxComputeInfo + 3);

/* The firmware owns everything here once a queue has been run; the host writes only the
 * thresholds, the counters and the work it appends. */
static
NTSTATUS
AgxInitGraphs(
    _Inout_ PAPPLE_AGX Agx)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    PAPPLE_AGX_OBJECT Item = Engine->Item;
    APPLE_AGX_PARAMETER_BUFFER Buffer;
    ULONG64 Pool[APPLE_AGX_POOLS];
    ULONG Index, Engine2, Seeds;
    NTSTATUS Status;

    AgxDescribeQueue(Engine, AGX_CHANNEL_COMPUTE, AgxComputeInfo, AgxComputeJobList, AgxComputeSched,
                     APPLE_AGX_COMPUTE_UUID);
    for (Index = 0; Index < APPLE_AGX_POOLS; ++Index)
        Pool[Index] = Item[AgxComputePool + Index].Client;
    Status = AppleAgxSeedPools(Item[AgxComputePoolTable].Virtual, Item[AgxComputeFreelist].Virtual, Pool,
                               APPLE_AGX_POOLS, &Seeds);
    if (NT_SUCCESS(Status))
    {
        Status = AppleAgxDescribeManager(Item[AgxComputeManager].Virtual, APPLE_AGX_MANAGER_COMPUTE_ID,
                                         APPLE_AGX_MANAGER_COMPUTE_INDEX, Seeds, Item[AgxComputeFreelist].Client,
                                         Item[AgxComputePoolTable].Client, Item[AgxComputeCounter].Address);
    }
    if (NT_SUCCESS(Status))
    {
        Status = AppleAgxDescribeGraph(Item[AgxComputeNotifier].Virtual, Item[AgxComputeThreshold].Address,
                                       Item[AgxComputeJobList].Virtual, Item[AgxComputeJobList].Address,
                                       Item[AgxComputeSched].Virtual);
    }
    if (NT_SUCCESS(Status))
        Status = AppleAgxQueueDescribe(&Engine->Queue[AGX_CHANNEL_COMPUTE]);
    if (!NT_SUCCESS(Status))
        return Status;
    AgxWrite32(&Item[AgxComputeCounter], 0, 1);
    AgxWrite32(&Item[AgxComputeThreshold], 0, 1);
    AgxWrite32(&Item[AgxComputeUserStamp], 0, APPLE_AGX_SEED_COMPUTE);
    AgxWrite32(&Item[AgxComputeFirmwareStamp], 0, 0);
    KeMemoryBarrier();

    AgxDescribeQueue(Engine, AGX_CHANNEL_TA, AgxTaInfo, AgxRenderJobList, AgxRenderSched, APPLE_AGX_VERTEX_UUID);
    AgxDescribeQueue(Engine, AGX_CHANNEL_FS, AgxFsInfo, AgxRenderJobList, AgxRenderSched, APPLE_AGX_FRAGMENT_UUID);
    for (Index = AGX_CHANNEL_TA; Index <= AGX_CHANNEL_FS; ++Index)
    {
        Status = AppleAgxQueueDescribe(&Engine->Queue[Index]);
        if (!NT_SUCCESS(Status))
            return Status;
        AppleAgxObjectZero(&Item[Index == AGX_CHANNEL_TA ? AgxTaRing : AgxFsRing], 0,
                           APPLE_AGX_QUEUE_ENTRIES * APPLE_AGX_WORK_ENTRY, 0xcc);
    }
    AgxWrite32(&Item[AgxTaUserStamp], 0, APPLE_AGX_SEED_VERTEX);
    AgxWrite32(&Item[AgxTaFirmwareStamp], 0, APPLE_AGX_SEED_VERTEX);
    AgxWrite32(&Item[AgxFsUserStamp], 0, APPLE_AGX_SEED_FRAGMENT);
    AgxWrite32(&Item[AgxFsFirmwareStamp], 0, APPLE_AGX_SEED_FRAGMENT);
    Status = AppleAgxDescribeGraph(Item[AgxRenderNotifier].Virtual, Item[AgxRenderThreshold].Address,
                                   Item[AgxRenderJobList].Virtual, Item[AgxRenderJobList].Address,
                                   Item[AgxRenderSched].Virtual);
    if (!NT_SUCCESS(Status))
        return Status;
    AgxWrite32(&Item[AgxRenderThreshold], 0, 2);

    for (Engine2 = 0; Engine2 < 2; ++Engine2)
    {
        ULONG Freelist = Engine2 ? AgxUma1Freelist : AgxUma0Freelist;

        for (Index = 0; Index < APPLE_AGX_UMA_POOLS; ++Index)
            Pool[Index] = Item[AgxUmaPool + Engine2 * APPLE_AGX_UMA_POOLS + Index].Client;
        Status = AppleAgxSeedPools(Item[Freelist + 1].Virtual, Item[Freelist].Virtual, Pool, APPLE_AGX_UMA_POOLS,
                                   &Seeds);
        if (NT_SUCCESS(Status))
        {
            Status = AppleAgxDescribeManager(Item[Freelist + 2].Virtual, Engine2 + 1, Engine2, Seeds,
                                             Item[Freelist].Client, Item[Freelist + 1].Client,
                                             Item[Freelist + 3].Address);
        }
        if (!NT_SUCCESS(Status))
            return Status;
        AgxWrite32(&Item[Freelist + 3], 0, 1);
    }

    RtlZeroMemory(&Engine->Growth, sizeof(Engine->Growth));
    Engine->Growth.Posted = APPLE_AGX_PB_INITIAL_BLOCKS;
    Engine->Growth.Backed = AGX_PB_CHUNKS * APPLE_AGX_PB_CHUNK_BLOCKS;
    Engine->Growth.Max = min(Engine->Growth.Backed, APPLE_AGX_PB_MAX_BLOCKS);
    Status = AppleAgxWriteBlocks(Item[AgxPbPages].Virtual, Item[AgxPbBlocks].Virtual, APPLE_AGX_CLIENT_PB_HEAP, 0,
                                 APPLE_AGX_PB_INITIAL_BLOCKS);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.PageList = Item[AgxPbPages].Client;
    Buffer.BlockList = Item[AgxPbBlocks].Client;
    Buffer.BlockControl = Item[AgxPbBlockControl].Address;
    Buffer.Counter = Item[AgxPbCounter].Address;
    Buffer.Blocks = APPLE_AGX_PB_INITIAL_BLOCKS;
    Buffer.MaxBlocks = Engine->Growth.Max;
    Status = AppleAgxDescribeParameterBuffer(Item[AgxPbManager].Virtual, &Buffer);
    if (NT_SUCCESS(Status))
        Status = AppleAgxDescribeBlockControl(Item[AgxPbBlockControl].Virtual, Item[AgxPbMisc].Virtual,
                                              APPLE_AGX_PB_INITIAL_BLOCKS);
    if (!NT_SUCCESS(Status))
        return Status;
    AgxWrite32(&Item[AgxPbCounter], 0, 1);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

C_ASSERT(AgxUma0Table == AgxUma0Freelist + 1 && AgxUma0Manager == AgxUma0Freelist + 2 &&
         AgxUma0Counter == AgxUma0Freelist + 3);
C_ASSERT(AgxUma1Table == AgxUma1Freelist + 1 && AgxUma1Manager == AgxUma1Freelist + 2 &&
         AgxUma1Counter == AgxUma1Freelist + 3);

NTSTATUS
AgxEngineStart(
    _Inout_ PAPPLE_AGX Agx)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    APPLE_AGX_REQUEST Request;
    ULONG Item, Channel;
    PULONG Monitor;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Agx->Initialised && Agx->Bound);
    ASSERT(!Engine->Ready);
    RtlZeroMemory(&Engine->Heap, sizeof(Engine->Heap));
    Engine->Heap.Adopted = &Agx->Adopted;
    Engine->Heap.Next = APPLE_AGX_HEAP_BASE;
    Engine->Heap.End = APPLE_AGX_HEAP_END;
    Engine->Heap.Owner = "appleagx";
    for (Item = 0; Item < AgxItems; ++Item)
    {
        AgxItemRequest(Item, &Request);
        Status = AppleAgxObjectCreate(&Engine->Heap, &Request, &Engine->Item[Item]);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("appleagx: object %lu of %lu bytes was not created: %lx\n", Item, (ULONG)Request.Size, Status);
            AgxEngineStop(Agx);
            return Status;
        }
    }
    for (Channel = 0; Channel < AGX_CHANNELS; ++Channel)
    {
        AppleAgxRingBind(&Engine->Channel[Channel], Agx->Init.Virtual[APPLE_AGX_FIXED_OBJECTS + 2 * Channel],
                         Agx->Init.Virtual[APPLE_AGX_FIXED_OBJECTS + 2 * Channel + 1], APPLE_AGX_SUBMIT_RECORD);
        if (Engine->Channel[Channel].Producer != 0 ||
            AppleAgxRingIndex(&Engine->Channel[Channel], APPLE_AGX_STATE_READ) != 0)
        {
            DbgPrint("appleagx: submit channel %lu does not start empty\n", Channel);
            AgxEngineStop(Agx);
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
    }
    Status = AgxInitGraphs(Agx);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the queues were not described: %lx\n", Status);
        AgxEngineStop(Agx);
        return Status;
    }
    AppleUatInvalidateAll();
    Monitor = (PULONG)((PUCHAR)Agx->Init.Virtual[APPLE_AGX_OBJECT_GLOBALS] + AGX_GLOBALS_COMPUTE_MONITOR);
    WRITE_REGISTER_ULONG((volatile ULONG *)Monitor, 0);
    KeMemoryBarrier();
    {
        ULONG Request2[APPLE_AGX_RECORD_WORDS] = { APPLE_AGX_DEVCTL_IMPORT_GLOBALS };

        Status = AgxDeviceControlPost(Agx, Request2, AGX_GROW_TIMEOUT_MS);
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the compute monitor was not turned off: %lx\n", Status);
        AgxEngineStop(Agx);
        return Status;
    }
    ExAcquireFastMutex(&Engine->Lock);
    Engine->Ready = TRUE;
    ExReleaseFastMutex(&Engine->Lock);
    DbgPrint("appleagx: %lu pages of queues and allocators, work at %I64x, parameter buffer %lu of %lu blocks\n",
             Engine->Heap.Pages, Engine->Item[AgxPassTaWork].Address, Engine->Growth.Posted, Engine->Growth.Max);
    return STATUS_SUCCESS;
}

VOID
AgxEngineStop(
    _Inout_ PAPPLE_AGX Agx)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    ULONG Item;

    Engine->Ready = FALSE;
    for (Item = 0; Item < AgxItems; ++Item)
        AppleAgxObjectFree(&Engine->Item[Item]);
    RtlZeroMemory(Engine->Channel, sizeof(Engine->Channel));
    RtlZeroMemory(Engine->Queue, sizeof(Engine->Queue));
}

/* Events are taken while the rings are drained; growth requests wait there for the submission
 * that runs, which answers them outside the drain. */
VOID
AgxEngineEvent(
    _Inout_ PAPPLE_AGX Agx,
    _In_reads_(APPLE_AGX_RECORD_WORDS) const ULONG *Record)
{
    PAGX_ENGINE Engine = &Agx->Engine;

    switch (Record[0])
    {
        case APPLE_AGX_EVENT_FAULT:
        case APPLE_AGX_EVENT_TIMEOUT:
        case APPLE_AGX_EVENT_CHANNEL_ERROR:
            ++Engine->Errors;
            RtlCopyMemory(Engine->LastError, Record, sizeof(Engine->LastError));
            DbgPrint("appleagx: event %lu: %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", Record[0], Record[1],
                     Record[2], Record[3], Record[4], Record[5], Record[6], Record[7]);
            break;
        case APPLE_AGX_EVENT_GROW:
            if (Engine->Grows == AGX_GROW_QUEUE)
            {
                ++Engine->GrowLost;
                break;
            }
            RtlCopyMemory(Engine->Grow[Engine->Grows++], Record, APPLE_AGX_RECORD_WORDS * sizeof(ULONG));
            break;
        default:
            break;
    }
}

/* A client address space gets the firmware objects it is run with, at their fixed client
 * addresses, and a context slot of its own. */
static
NTSTATUS
AgxEngineAttach(
    _Inout_ PAGX_ENGINE Engine,
    _Inout_ PAGX_VM Vm)
{
    ULONG Item, Slot;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Vm->Engine)
        return STATUS_SUCCESS;
    for (Slot = 1; Slot < APPLE_UAT_CONTEXTS && (Engine->Slots & (1ULL << Slot)); ++Slot)
        ;
    if (Slot == APPLE_UAT_CONTEXTS)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Item = 0; Item < AgxItems; ++Item)
    {
        if (!Engine->Item[Item].ClientPage)
            continue;
        Status = AppleAgxObjectAlias(&Engine->Item[Item], &Vm->Uat, Engine->Item[Item].ClientPage);
        if (!NT_SUCCESS(Status))
            break;
    }
    if (!NT_SUCCESS(Status))
    {
        while (Item--)
        {
            if (Engine->Item[Item].ClientPage)
                AppleUatUnmap(&Vm->Uat, Engine->Item[Item].ClientPage, Engine->Item[Item].Bytes);
        }
        return Status;
    }
    Engine->Slots |= 1ULL << Slot;
    Vm->Slot = Slot;
    Vm->Engine = Engine;
    return STATUS_SUCCESS;
}

VOID
AgxEngineRelease(
    _Inout_ PAGX_VM Vm)
{
    PAGX_ENGINE Engine = Vm->Engine;

    if (!Engine)
        return;
    ExAcquireFastMutex(&Engine->Lock);
    ASSERT(Vm->Slot && (Engine->Slots & (1ULL << Vm->Slot)));
    Engine->Slots &= ~(1ULL << Vm->Slot);
    ExReleaseFastMutex(&Engine->Lock);
    Vm->Engine = NULL;
    Vm->Slot = 0;
}

static
BOOLEAN
AgxArenaTest(
    _In_ PAGX_ENGINE Engine,
    _In_ ULONG Page)
{
    return (Engine->Arena[Page / 8] & (1 << (Page % 8))) != 0;
}

static
VOID
AgxArenaSet(
    _Inout_ PAGX_ENGINE Engine,
    _In_ ULONG First,
    _In_ ULONG Count,
    _In_ BOOLEAN Used)
{
    ULONG Page;

    for (Page = First; Page < First + Count; ++Page)
    {
        if (Used)
            Engine->Arena[Page / 8] |= (UCHAR)(1 << (Page % 8));
        else
            Engine->Arena[Page / 8] &= (UCHAR)~(1 << (Page % 8));
    }
}

/* A timestamp buffer is mapped into the firmware's timestamp arena, which the firmware writes the
 * requested start and end times into. */
NTSTATUS
AgxEngineMapArena(
    _Inout_ PAPPLE_AGX Agx,
    _In_ PMDL Mdl,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Range,
    _Out_ PULONG64 Address)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    PPFN_NUMBER Frames = MmGetMdlPfnArray(Mdl);
    APPLE_UAT_PROTECTION Protection;
    ULONG Pages = (ULONG)(Range / APPLE_UAT_PAGE_SIZE), First, Page;
    ULONG64 At;
    NTSTATUS Status = STATUS_NO_MEMORY;

    *Address = 0;
    if (!Range || ((Offset | Range) & (APPLE_UAT_PAGE_SIZE - 1)) || Range > APPLE_AGX_TIMESTAMPS_SIZE)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&Engine->Lock);
    if (!Engine->Ready)
    {
        ExReleaseFastMutex(&Engine->Lock);
        return STATUS_DEVICE_NOT_READY;
    }
    for (First = 0; First + Pages <= AGX_ARENA_PAGES; ++First)
    {
        for (Page = 0; Page < Pages && !AgxArenaTest(Engine, First + Page); ++Page)
            ;
        if (Page == Pages)
            break;
        First += Page;
    }
    if (First + Pages <= AGX_ARENA_PAGES)
    {
        AppleAgxClassProtection(APPLE_AGX_CLASS_SHARED, &Protection);
        At = APPLE_AGX_TIMESTAMP_ARENA + (ULONG64)First * APPLE_UAT_PAGE_SIZE;
        for (Page = 0; Page < Pages; ++Page)
        {
            Status = AppleUatAdoptedMap(&Agx->Adopted, At + (ULONG64)Page * APPLE_UAT_PAGE_SIZE,
                                        (ULONG64)Frames[(Offset + (ULONG64)Page * APPLE_UAT_PAGE_SIZE) >> PAGE_SHIFT]
                                            << PAGE_SHIFT,
                                        APPLE_UAT_PAGE_SIZE, &Protection);
            if (!NT_SUCCESS(Status))
                break;
        }
        if (NT_SUCCESS(Status))
        {
            AgxArenaSet(Engine, First, Pages, TRUE);
            *Address = At;
        }
        else if (Page)
        {
            AppleUatUnmap(&Agx->Adopted.Host, At, (SIZE_T)Page * APPLE_UAT_PAGE_SIZE);
        }
    }
    ExReleaseFastMutex(&Engine->Lock);
    return Status;
}

/* Nothing the firmware may still write is unmapped: every submission has completed before the
 * client lock that guards an unbind is released. */
VOID
AgxEngineUnmapArena(
    _Inout_ PAPPLE_AGX Agx,
    _In_ ULONG64 Address,
    _In_ ULONG64 Range)
{
    PAGX_ENGINE Engine = &Agx->Engine;

    ASSERT(Address >= APPLE_AGX_TIMESTAMP_ARENA && Range &&
           Address - APPLE_AGX_TIMESTAMP_ARENA + Range <= APPLE_AGX_TIMESTAMPS_SIZE);
    ExAcquireFastMutex(&Engine->Lock);
    if (Engine->Ready)
    {
        AppleUatUnmap(&Agx->Adopted.Host, Address, (SIZE_T)Range);
        AppleUatInvalidateAll();
        AgxArenaSet(Engine, (ULONG)((Address - APPLE_AGX_TIMESTAMP_ARENA) / APPLE_UAT_PAGE_SIZE),
                    (ULONG)(Range / APPLE_UAT_PAGE_SIZE), FALSE);
    }
    ExReleaseFastMutex(&Engine->Lock);
}

static
NTSTATUS
AgxResolveTimestamp(
    _In_ PAGX_CLIENT Client,
    _In_ const DRM_ASAHI_TIMESTAMP *Timestamp,
    _Out_ PULONG64 Address)
{
    const AGX_BOUND *Bound;

    *Address = 0;
    if (!Timestamp->Handle)
        return STATUS_SUCCESS;
    if (Timestamp->Handle > AGX_MAX_BOUND)
        return STATUS_INVALID_PARAMETER;
    Bound = &Client->Bound[Timestamp->Handle - 1];
    if (!Bound->Object || (Timestamp->Offset % 8) || Timestamp->Offset >= Bound->Range ||
        Bound->Range - Timestamp->Offset < 8)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *Address = Bound->Address + Timestamp->Offset;
    return STATUS_SUCCESS;
}

static
VOID
AgxSleep(VOID)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
BOOLEAN
AgxHalted(
    _In_ PAPPLE_AGX Agx)
{
    return *(volatile ULONG *)((PUCHAR)Agx->Init.Virtual[APPLE_AGX_OBJECT_CONTROL] + APPLE_AGX_CONTROL_HALTED) != 0;
}

/* A request for more parameter buffer is granted by writing its blocks, moving the producer and
 * replying on the device control ring with what the request named. */
static
VOID
AgxAnswerGrowth(
    _Inout_ PAPPLE_AGX Agx)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    ULONG Request[APPLE_AGX_RECORD_WORDS], Reply[APPLE_AGX_RECORD_WORDS];
    ULONG First, Count, Index;
    NTSTATUS Status;

    while (Engine->Grows)
    {
        RtlCopyMemory(Request, Engine->Grow[0], sizeof(Request));
        for (Index = 1; Index < Engine->Grows; ++Index)
            RtlCopyMemory(Engine->Grow[Index - 1], Engine->Grow[Index], sizeof(Request));
        --Engine->Grows;
        Status = AppleAgxAnswerGrowth(&Engine->Growth, Request, Engine->Context, Reply, &First, &Count);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("appleagx: growth request %08lx %08lx %08lx %08lx left unanswered\n", Request[1], Request[2],
                     Request[3], Request[4]);
            continue;
        }
        if (Count)
        {
            Status = AppleAgxWriteBlocks(NULL, Engine->Item[AgxPbBlocks].Virtual, APPLE_AGX_CLIENT_PB_HEAP, First,
                                         Count);
            ASSERT(NT_SUCCESS(Status));
            KeMemoryBarrier();
            AgxWrite32(&Engine->Item[AgxPbBlockControl], APPLE_AGX_BLOCK_CONTROL_PRODUCER, Engine->Growth.Posted);
            KeMemoryBarrier();
        }
        Status = AgxDeviceControlPost(Agx, Reply, AGX_GROW_TIMEOUT_MS);
        DbgPrint("appleagx: parameter buffer grown by %lu blocks to %lu: %lx\n", Count, Engine->Growth.Posted,
                 Status);
    }
}

static
NTSTATUS
AgxPostRecord(
    _Inout_ PAPPLE_AGX Agx,
    _In_ ULONG Channel,
    _In_ USHORT Head,
    _In_ UCHAR Event,
    _In_ BOOLEAN NewQueue)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    UCHAR Record[APPLE_AGX_SUBMIT_RECORD];
    ULONG Waited;
    NTSTATUS Status;

    AppleAgxSubmitRecord(Record, Engine->Queue[Channel].Info, Channel, Head, Event, NewQueue);
    for (Waited = 0;; ++Waited)
    {
        Status = AppleAgxRingPost(&Engine->Channel[Channel], Record, FALSE, NULL);
        if (Status != STATUS_DEVICE_BUSY || Waited >= AGX_POST_TIMEOUT_MS)
            break;
        AppleRtkitPoll(&Agx->Rtkit);
        AgxSleep();
    }
    if (!NT_SUCCESS(Status))
        return Status;
    return AppleRtkitSend(&Agx->Rtkit, APPLE_AGX_EP_DOORBELL, AppleAgxRingDoorbell(Channel)) ? STATUS_SUCCESS
                                                                                            : STATUS_IO_TIMEOUT;
}

/* Work is complete once both of a queue's stamps, the one the work writes and the one the
 * firmware writes after it, have reached its value. */
static
NTSTATUS
AgxWait(
    _Inout_ PAPPLE_AGX Agx,
    _In_reads_(Count) const ULONG *Stamp,
    _In_reads_(Count) const ULONG *Target,
    _In_ ULONG Count)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    ULONG64 Start = KeQueryInterruptTime(), Elapsed = 0;
    ULONG Index, Errors = Engine->Errors;
    BOOLEAN Done;

    while (Elapsed < AGX_JOB_TIMEOUT_MS * 10000ULL)
    {
        AppleRtkitPoll(&Agx->Rtkit);
        AgxDrainRings(Agx);
        AgxAnswerGrowth(Agx);
        KeMemoryBarrier();
        Done = TRUE;
        for (Index = 0; Index < Count; ++Index)
        {
            if (AgxRead32(&Engine->Item[Stamp[Index]], 0) != Target[Index])
                Done = FALSE;
        }
        if (Done)
            return STATUS_SUCCESS;
        if (Engine->Errors != Errors)
            return STATUS_DEVICE_HARDWARE_ERROR;
        if (AgxHalted(Agx))
            return STATUS_DEVICE_POWERED_OFF;
        if (Elapsed < AGX_SPIN_US * 10ULL)
            KeStallExecutionProcessor(AGX_SPIN_STEP_US);
        else
            AgxSleep();
        Elapsed = KeQueryInterruptTime() - Start;
    }
    for (Index = 0; Index < Count; ++Index)
    {
        DbgPrint("appleagx: stamp %lu is %08lx, waiting for %08lx\n", Stamp[Index],
                 AgxRead32(&Engine->Item[Stamp[Index]], 0), Target[Index]);
    }
    return STATUS_IO_TIMEOUT;
}

static
VOID
AgxScrubPass(
    _Inout_ PAGX_ENGINE Engine)
{
    static const ULONG Whole[] =
    {
        AgxPassTaWork, AgxPassFsWork, AgxPassTaSequence, AgxPassFsSequence, AgxPassTaScratch, AgxPassFsScratch,
        AgxPassStamps, AgxPassAux, AgxPassHeap, AgxPassSceneUser, AgxPassPre0, AgxPassPre1, AgxPassPre2,
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Whole); ++Index)
        AppleAgxObjectZero(&Engine->Item[Whole[Index]], 0, AgxItemSpec[Whole[Index]].Size, 0);
    if (Engine->TpcDirty)
        AppleAgxObjectZero(&Engine->Item[AgxPassTpc], 0, (SIZE_T)Engine->TpcDirty, 0);
    if (Engine->TilemapDirty)
        AppleAgxObjectZero(&Engine->Item[AgxPassTilemap], 0, (SIZE_T)Engine->TilemapDirty, 0);
    Engine->TpcDirty = 0;
    Engine->TilemapDirty = 0;
}

static
NTSTATUS
AgxRunRender(
    _Inout_ PAPPLE_AGX Agx,
    _In_ const DRM_ASAHI_CMD_RENDER *Command,
    _In_ ULONG64 UscBase,
    _In_reads_(4) const ULONG64 *UserTimestamps)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    PAPPLE_AGX_OBJECT Item = Engine->Item;
    APPLE_AGX_RENDER_STATE Render;
    APPLE_AGX_RENDER_JOB Job;
    ULONG64 Ordinal = Engine->Draws + 1;
    ULONG VertexStamp = AppleAgxStamp(APPLE_AGX_SEED_VERTEX, Ordinal);
    ULONG FragmentStamp = AppleAgxStamp(APPLE_AGX_SEED_FRAGMENT, Ordinal);
    ULONG Stamps[4] = { AgxTaUserStamp, AgxTaFirmwareStamp, AgxFsUserStamp, AgxFsFirmwareStamp };
    ULONG Targets[4] = { VertexStamp, VertexStamp, FragmentStamp, FragmentStamp };
    UCHAR C1[APPLE_AGX_C1_RECORD];
    USHORT VertexHead, FragmentHead;
    NTSTATUS Status;

    Status = AppleAgxTranslateRender(Command, UscBase, Agx->Reported.Clusters, &Render);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Render.Geometry.TpcBytes > Item[AgxPassTpc].Bytes || Render.Geometry.TilemapBytes > Item[AgxPassTilemap].Bytes)
        return STATUS_INVALID_PARAMETER;
    if (Engine->Draws)
        AgxScrubPass(Engine);
    Engine->TpcDirty = Render.Geometry.TpcBytes;
    Engine->TilemapDirty = Render.Geometry.TilemapBytes;

    RtlZeroMemory(&Job, sizeof(Job));
    Job.VertexWork = Item[AgxPassTaWork].Virtual;
    Job.VertexSequence = Item[AgxPassTaSequence].Virtual;
    Job.FragmentWork = Item[AgxPassFsWork].Virtual;
    Job.FragmentSequence = Item[AgxPassFsSequence].Virtual;
    Job.Scene = Item[AgxPassScene].Virtual;
    Job.FragmentBarrier = Item[AgxPassOwnBarrier].Virtual;
    Job.VertexWorkAddress = Item[AgxPassTaWork].Address;
    Job.VertexWorkClient = Item[AgxPassTaWork].Client;
    Job.VertexSequenceAddress = Item[AgxPassTaSequence].Address;
    Job.FragmentWorkAddress = Item[AgxPassFsWork].Address;
    Job.FragmentWorkClient = Item[AgxPassFsWork].Client;
    Job.FragmentSequenceAddress = Item[AgxPassFsSequence].Address;
    Job.SceneAddress = Item[AgxPassScene].Address;
    Job.Hwdata = Agx->Init.Object[APPLE_AGX_OBJECT_HWDATA].Address;
    Job.Notifier = Item[AgxRenderNotifier].Address;
    Job.Empty = Item[AgxRenderEmpty].Address;
    Job.Manager = Item[AgxPbManager].Address;
    Job.SceneList = Item[AgxPbSceneList].Address;
    Job.ManagerMisc = Item[AgxPbMisc].Address;
    Job.Pool[0] = Item[AgxUma0Manager].Address;
    Job.Pool[1] = Item[AgxUma1Manager].Address;
    Job.Queue[0] = Engine->Queue[AGX_CHANNEL_TA].Info;
    Job.Queue[1] = Engine->Queue[AGX_CHANNEL_FS].Info;
    Job.UserStamp[0] = Item[AgxTaUserStamp].Address;
    Job.UserStamp[1] = Item[AgxFsUserStamp].Address;
    Job.FirmwareStamp[0] = Item[AgxTaFirmwareStamp].Address;
    Job.FirmwareStamp[1] = Item[AgxFsFirmwareStamp].Address;
    Job.Timestamps[0][0] = Item[AgxPassStamps].Address;
    Job.Timestamps[0][1] = Item[AgxPassStamps].Address + 0x08;
    Job.Timestamps[1][0] = Item[AgxPassStamps].Address + 0x10;
    Job.Timestamps[1][1] = Item[AgxPassStamps].Address + 0x18;
    Job.VertexScratch = Item[AgxPassTaScratch].Address;
    Job.FragmentScratch = Item[AgxPassFsScratch].Client;
    Job.FragmentScratchBytes = APPLE_AGX_PASS_BUFFER_SIZE;
    Job.Tilemap = Item[AgxPassTilemap].Client;
    Job.Tpc = Item[AgxPassTpc].Client;
    Job.Heap = Item[AgxPassHeap].Client;
    Job.Auxiliary = Item[AgxPassAux].Client;
    Job.SceneUser = Item[AgxPassSceneUser].Client;
    Job.SceneUserBytes = APPLE_AGX_PASS_BUFFER_SIZE;
    Job.Preempt[0] = Item[AgxPassPre0].Client;
    Job.Preempt[1] = Item[AgxPassPre1].Client;
    Job.Preempt[2] = Item[AgxPassPre2].Client;
    Job.PageCount = APPLE_AGX_PB_PAGES_PER_BLOCK * APPLE_AGX_PB_INITIAL_BLOCKS;
    Job.QueueCommands = (ULONG)(Ordinal - 1);
    Job.UserTimestamps[0][0] = UserTimestamps[0];
    Job.UserTimestamps[0][1] = UserTimestamps[1];
    Job.UserTimestamps[1][0] = UserTimestamps[2];
    Job.UserTimestamps[1][1] = UserTimestamps[3];
    Status = AppleAgxBuildRender(&Job, &Render, Engine->Context, Ordinal);
    if (NT_SUCCESS(Status))
    {
        Status = AppleAgxEncodeBarrier(Item[AgxPassPrevBarrier].Virtual, Item[AgxFsFirmwareStamp].Address,
                                       AppleAgxStamp(APPLE_AGX_SEED_FRAGMENT, Ordinal - 1), APPLE_AGX_EVENT_FRAGMENT,
                                       VertexStamp, APPLE_AGX_RENDER_WAIT_UUID, TRUE);
    }
    if (NT_SUCCESS(Status) && Ordinal == 1)
    {
        Status = AppleAgxEncodeInitBm(Item[AgxInitBm].Virtual, Engine->Context, APPLE_AGX_PB_INITIAL_BLOCKS,
                                      Item[AgxPbManager].Address, VertexStamp);
        if (NT_SUCCESS(Status))
            Status = AppleAgxQueueAppend(&Engine->Queue[AGX_CHANNEL_TA], Item[AgxInitBm].Address);
    }
    if (NT_SUCCESS(Status))
        Status = AppleAgxQueueAppend(&Engine->Queue[AGX_CHANNEL_TA], Item[AgxPassPrevBarrier].Address);
    if (NT_SUCCESS(Status))
        Status = AppleAgxQueueAppend(&Engine->Queue[AGX_CHANNEL_TA], Item[AgxPassTaWork].Address);
    if (NT_SUCCESS(Status))
        Status = AppleAgxQueueAppend(&Engine->Queue[AGX_CHANNEL_FS], Item[AgxPassOwnBarrier].Address);
    if (NT_SUCCESS(Status))
        Status = AppleAgxQueueAppend(&Engine->Queue[AGX_CHANNEL_FS], Item[AgxPassFsWork].Address);
    if (!NT_SUCCESS(Status))
    {
        Engine->Broken = TRUE;
        return Status;
    }

    AgxWrite32(&Item[AgxRenderThreshold], 0, (ULONG)(2 * Ordinal));
    AgxWrite32(&Item[AgxPbCounter], 0, (ULONG)Ordinal);
    if (!Engine->C1Written)
    {
        AppleAgxC1Record(C1, APPLE_AGX_PB_INITIAL_BLOCKS);
        RtlCopyMemory(Agx->Init.Virtual[APPLE_AGX_OBJECT_C1], C1, sizeof(C1));
        Engine->C1Written = TRUE;
    }
    KeMemoryBarrier();
    AppleAgxQueuePublish(&Engine->Queue[AGX_CHANNEL_TA], &VertexHead);
    AppleAgxQueuePublish(&Engine->Queue[AGX_CHANNEL_FS], &FragmentHead);
    Status = AgxPostRecord(Agx, AGX_CHANNEL_FS, FragmentHead, APPLE_AGX_EVENT_FRAGMENT, !Engine->RenderRegistered);
    if (NT_SUCCESS(Status))
        Status = AgxPostRecord(Agx, AGX_CHANNEL_TA, VertexHead, APPLE_AGX_EVENT_VERTEX, !Engine->RenderRegistered);
    Engine->RenderRegistered = TRUE;
    Engine->Draws = Ordinal;
    if (NT_SUCCESS(Status))
        Status = AgxWait(Agx, Stamps, Targets, RTL_NUMBER_OF(Stamps));
    DbgPrint("appleagx: render %I64u %ux%u in context %lu: %lx\n", Ordinal, Command->WidthPx, Command->HeightPx,
             Engine->Context, Status);
    if (!NT_SUCCESS(Status))
        Engine->Broken = TRUE;
    return Status;
}

static
NTSTATUS
AgxRunCompute(
    _Inout_ PAPPLE_AGX Agx,
    _In_ const DRM_ASAHI_CMD_COMPUTE *Command,
    _In_reads_(Attachments) const APPLE_AGX_ATTACHMENT *Attachment,
    _In_ ULONG Attachments,
    _In_reads_(2) const ULONG64 *UserTimestamps)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    PAPPLE_AGX_OBJECT Item = Engine->Item;
    APPLE_AGX_COMPUTE_REFS Refs;
    ULONG64 Register[APPLE_AGX_COMPUTE_REGISTER_COUNT];
    ULONG64 Ordinal = Engine->Computes + 1;
    ULONG Stamp = AppleAgxStamp(APPLE_AGX_SEED_COMPUTE, Ordinal);
    ULONG Stamps[2] = { AgxComputeUserStamp, AgxComputeFirmwareStamp };
    ULONG Targets[2] = { Stamp, Stamp };
    ULONG Index;
    USHORT Head;
    NTSTATUS Status;

    Status = AppleAgxComputeRegisters(Command, Item[AgxSlotPreempt].Client, Register);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Engine->Computes)
    {
        AppleAgxObjectZero(&Item[AgxSlotStamps], 0, APPLE_AGX_COMPUTE_CELLS, 0);
        AppleAgxObjectZero(&Item[AgxSlotTail], 0, APPLE_AGX_PASS_BUFFER_SIZE, 0);
        for (Index = 0; Index < APPLE_AGX_COMPUTE_SCRATCHES; ++Index)
            AppleAgxObjectZero(&Item[AgxSlotScratch + Index], 0, APPLE_AGX_PASS_BUFFER_SIZE, 0);
        AppleAgxObjectZero(&Item[AgxSlotPreempt], 0, APPLE_AGX_PREEMPT_SIZE, APPLE_AGX_PREEMPT_FILL);
        AppleAgxObjectZero(&Item[AgxSlotWork], 0, APPLE_AGX_COMPUTE_WORK_SIZE, 0);
        AppleAgxObjectZero(&Item[AgxSlotSequence], 0, APPLE_AGX_SEQUENCE_STORAGE, 0);
    }
    RtlZeroMemory(&Refs, sizeof(Refs));
    Refs.Hwdata = Agx->Init.Object[APPLE_AGX_OBJECT_HWDATA].Address;
    Refs.Notifier = Item[AgxComputeNotifier].Address;
    Refs.Queue = Engine->Queue[AGX_CHANNEL_COMPUTE].Info;
    Refs.FirmwareStamp = Item[AgxComputeFirmwareStamp].Address;
    Refs.Manager = Item[AgxComputeManager].Address;
    Refs.Work = Item[AgxSlotWork].Address;
    for (Index = 0; Index < APPLE_AGX_COMPUTE_SCRATCHES; ++Index)
        Refs.Scratch[Index] = Item[AgxSlotScratch + Index].Address;
    Refs.WorkClient = Item[AgxSlotWork].Client;
    Refs.Sequence = Item[AgxSlotSequence].Address;
    Refs.UserStamp = Item[AgxComputeUserStamp].Address;
    Refs.Timestamps = Item[AgxSlotStamps].Address;
    Refs.Tail = Item[AgxSlotTail].Address;
    Refs.Preempt = Item[AgxSlotPreempt].Client;
    Refs.StreamEnd = Command->CdmCtrlStreamEnd;
    Refs.UserTimestamps[0] = UserTimestamps[0];
    Refs.UserTimestamps[1] = UserTimestamps[1];
    Status = AppleAgxEncodeComputeWork(Item[AgxSlotWork].Virtual, &Refs, Register, Engine->Context, (ULONG)Ordinal,
                                       Stamp);
    if (NT_SUCCESS(Status))
    {
        Status = AppleAgxEncodeCompute(Item[AgxSlotSequence].Virtual, &Refs, Attachment, Attachments, Engine->Context,
                                       Stamp, TRUE);
    }
    if (NT_SUCCESS(Status))
        Status = AppleAgxQueueAppend(&Engine->Queue[AGX_CHANNEL_COMPUTE], Item[AgxSlotWork].Address);
    if (!NT_SUCCESS(Status))
    {
        Engine->Broken = TRUE;
        return Status;
    }
    AgxWrite32(&Item[AgxComputeThreshold], 0, (ULONG)Ordinal);
    AgxWrite32(&Item[AgxComputeCounter], 0, (ULONG)Ordinal);
    KeMemoryBarrier();
    AppleAgxQueuePublish(&Engine->Queue[AGX_CHANNEL_COMPUTE], &Head);
    Status = AgxPostRecord(Agx, AGX_CHANNEL_COMPUTE, Head, APPLE_AGX_EVENT_SLOT, !Engine->ComputeRegistered);
    Engine->ComputeRegistered = TRUE;
    Engine->Computes = Ordinal;
    if (NT_SUCCESS(Status))
        Status = AgxWait(Agx, Stamps, Targets, RTL_NUMBER_OF(Stamps));
    DbgPrint("appleagx: compute %I64u in context %lu: %lx\n", Ordinal, Engine->Context, Status);
    if (!NT_SUCCESS(Status))
        Engine->Broken = TRUE;
    return Status;
}

static
BOOLEAN
AgxAttachmentsValid(
    _In_ const APPLE_AGX_COMMAND *Command)
{
    DRM_ASAHI_ATTACHMENT Attachment;
    ULONG Index;

    if (Command->Header.VdmBarrier != DRM_ASAHI_BARRIER_NONE || Command->Header.CdmBarrier != DRM_ASAHI_BARRIER_NONE ||
        Command->Size % sizeof(Attachment) || Command->Size / sizeof(Attachment) > APPLE_AGX_MAX_ATTACHMENTS)
    {
        return FALSE;
    }
    for (Index = 0; Index < Command->Size / sizeof(Attachment); ++Index)
    {
        RtlCopyMemory(&Attachment, Command->Payload + Index * sizeof(Attachment), sizeof(Attachment));
        if (Attachment.Pad || Attachment.Flags || !Attachment.Size || Attachment.Pointer < APPLE_AGX_USER_VA_START ||
            Attachment.Pointer > APPLE_AGX_USER_VA_END || Attachment.Size > APPLE_AGX_USER_VA_END - Attachment.Pointer)
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* The whole command buffer is checked before anything of it runs. Each command then runs to
 * completion before the next starts, which satisfies every barrier a command can name. */
static
NTSTATUS
AgxCheckStream(
    _In_reads_bytes_(Length) const VOID *Stream,
    _In_ ULONG Length)
{
    APPLE_AGX_COMMAND Command;
    ULONG Offset = 0, Renders = 0, Computes = 0;
    NTSTATUS Status;

    if (!Length || Length > AGX_MAX_CMDBUF || Length % 8)
        return STATUS_INVALID_PARAMETER;
    while ((Status = AppleAgxNextCommand(Stream, Length, &Offset, &Command)) == STATUS_SUCCESS)
    {
        switch (Command.Header.CmdType)
        {
            case DRM_ASAHI_SET_VERTEX_ATTACHMENTS:
            case DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS:
            case DRM_ASAHI_SET_COMPUTE_ATTACHMENTS:
                if (!AgxAttachmentsValid(&Command))
                    return STATUS_INVALID_PARAMETER;
                continue;
            case DRM_ASAHI_CMD_TYPE_RENDER:
            case DRM_ASAHI_CMD_TYPE_COMPUTE:
                break;
            default:
                return STATUS_INVALID_PARAMETER;
        }
        if ((Command.Header.VdmBarrier != DRM_ASAHI_BARRIER_NONE && Command.Header.VdmBarrier > Renders) ||
            (Command.Header.CdmBarrier != DRM_ASAHI_BARRIER_NONE && Command.Header.CdmBarrier > Computes))
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (Command.Header.CmdType == DRM_ASAHI_CMD_TYPE_RENDER)
            ++Renders;
        else
            ++Computes;
        if (Renders + Computes > APPLE_AGX_MAX_COMMANDS)
            return STATUS_INVALID_PARAMETER;
    }
    if (Status != STATUS_NO_MORE_ENTRIES)
        return Status;
    return (Renders + Computes) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static
NTSTATUS
AgxRunStream(
    _Inout_ PAPPLE_AGX Agx,
    _In_ PAGX_CLIENT Client,
    _In_reads_bytes_(Length) const VOID *Stream,
    _In_ ULONG Length,
    _In_ ULONG64 UscBase)
{
    APPLE_AGX_ATTACHMENT Attachment[APPLE_AGX_MAX_ATTACHMENTS];
    ULONG64 Timestamps[4];
    DRM_ASAHI_ATTACHMENT Source;
    DRM_ASAHI_CMD_RENDER Render;
    DRM_ASAHI_CMD_COMPUTE Compute;
    APPLE_AGX_COMMAND Command;
    ULONG Offset = 0, Attachments = 0, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    while (NT_SUCCESS(Status) && AppleAgxNextCommand(Stream, Length, &Offset, &Command) == STATUS_SUCCESS)
    {
        switch (Command.Header.CmdType)
        {
            case DRM_ASAHI_SET_COMPUTE_ATTACHMENTS:
                Attachments = Command.Size / sizeof(Source);
                for (Index = 0; Index < Attachments; ++Index)
                {
                    RtlCopyMemory(&Source, Command.Payload + Index * sizeof(Source), sizeof(Source));
                    Attachment[Index].Pointer = Source.Pointer;
                    Attachment[Index].Size = Source.Size;
                }
                break;
            case DRM_ASAHI_CMD_TYPE_RENDER:
                RtlZeroMemory(&Render, sizeof(Render));
                RtlCopyMemory(&Render, Command.Payload, min(Command.Size, sizeof(Render)));
                Status = AgxResolveTimestamp(Client, &Render.TsVtx.Start, &Timestamps[0]);
                if (NT_SUCCESS(Status))
                    Status = AgxResolveTimestamp(Client, &Render.TsVtx.End, &Timestamps[1]);
                if (NT_SUCCESS(Status))
                    Status = AgxResolveTimestamp(Client, &Render.TsFrag.Start, &Timestamps[2]);
                if (NT_SUCCESS(Status))
                    Status = AgxResolveTimestamp(Client, &Render.TsFrag.End, &Timestamps[3]);
                if (NT_SUCCESS(Status))
                    Status = AgxRunRender(Agx, &Render, UscBase, Timestamps);
                break;
            case DRM_ASAHI_CMD_TYPE_COMPUTE:
                RtlZeroMemory(&Compute, sizeof(Compute));
                RtlCopyMemory(&Compute, Command.Payload, min(Command.Size, sizeof(Compute)));
                Status = AgxResolveTimestamp(Client, &Compute.Ts.Start, &Timestamps[0]);
                if (NT_SUCCESS(Status))
                    Status = AgxResolveTimestamp(Client, &Compute.Ts.End, &Timestamps[1]);
                if (NT_SUCCESS(Status))
                    Status = AgxRunCompute(Agx, &Compute, Attachment, Attachments, Timestamps);
                break;
            default:
                break;
        }
    }
    return Status;
}

/* A submission runs in its queue's address space, bound to the client's context slot for as
 * long as its work runs. */
NTSTATUS
AgxSubmitCommands(
    _Inout_ PAPPLE_AGX Agx,
    _Inout_ PAGX_CLIENT Client,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    PAGX_ENGINE Engine = &Agx->Engine;
    const DRM_ASAHI_SUBMIT *Submit = Buffer;
    const UCHAR *Stream = (const UCHAR *)(Submit + 1);
    PAGX_QUEUE Queue;
    PAGX_VM Vm;
    NTSTATUS Status, Unbound;

    ASSERT(KeGetCurrentIrql() == APC_LEVEL);
    if (Length < sizeof(*Submit) || Submit->Syncs || Submit->Cmdbuf || Submit->Flags || Submit->Pad ||
        Submit->InSyncCount || Submit->OutSyncCount || Submit->CmdbufSize > Length - sizeof(*Submit) ||
        !Submit->QueueId || Submit->QueueId > AGX_MAX_QUEUES)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Queue = Client->Queue[Submit->QueueId - 1];
    Vm = Queue ? ((Queue->VmId && Queue->VmId <= AGX_MAX_VMS) ? Client->Vm[Queue->VmId - 1] : NULL) : NULL;
    if (!Vm)
        return STATUS_INVALID_PARAMETER;
    Status = AgxCheckStream(Stream, Submit->CmdbufSize);
    if (!NT_SUCCESS(Status))
        return Status;
    ExAcquireFastMutex(&Engine->Lock);
    /* FIXME: work that failed or timed out stops the engine until the driver restarts; nothing recovers the firmware. */
    if (!Engine->Ready || Engine->Broken)
    {
        ExReleaseFastMutex(&Engine->Lock);
        return STATUS_DEVICE_NOT_READY;
    }
    Status = AgxEngineAttach(Engine, Vm);
    if (NT_SUCCESS(Status))
        Status = AppleUatAdoptedBindSlot(&Agx->Adopted, Vm->Slot, Vm->Uat.TopPhysical);
    if (NT_SUCCESS(Status))
    {
        Engine->Context = Vm->Slot;
        Status = AgxRunStream(Agx, Client, Stream, Submit->CmdbufSize, Queue->UscBase);
        Unbound = AppleUatAdoptedUnbindSlot(&Agx->Adopted, Vm->Slot);
        ASSERT(NT_SUCCESS(Unbound));
        Engine->Context = 0;
    }
    ExReleaseFastMutex(&Engine->Lock);
    return Status;
}
