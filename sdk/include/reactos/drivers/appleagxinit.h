/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Initial data the Apple silicon graphics firmware is started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              The firmware interface described here is taken, under its MIT option, from the
 *              Apple graphics driver of aurora-silicon/linux, at git commit c880586:
 *              Copyright (C) 2026 Eryk Wieliczko
 *              Copyright (C) 2026 The Gravity Linux Contributors
 *              SPDX-License-Identifier: GPL-2.0-only OR MIT
 *              See media/doc/3rd Party Apple GPU interface.txt for what was and was not taken.
 */

#pragma once

#include <reactos/drivers/appleagxuapi.h>

/* The firmware endpoint, its doorbell for the initial data and its request to drain the rings. */
#define APPLE_AGX_EP_FIRMWARE           0x20
#define APPLE_AGX_EP_DOORBELL           0x21
#define APPLE_AGX_MESSAGE_INITDATA      (0x0081ULL << 48)
#define APPLE_AGX_MESSAGE_RING_SERVICE  (0x0042ULL << 48)
#define APPLE_AGX_MESSAGE_ADDRESS_MASK  0xfffffffffffULL

/* Root object of the initial data: the magic names the layout of one firmware build. */
#define APPLE_AGX_ROOT_SIZE             0xc0
#define APPLE_AGX_ROOT_MAGIC            0x0c08e21e83800490ULL
#define APPLE_AGX_ROOT_REGION_A         0x08
#define APPLE_AGX_ROOT_RUNTIME          0x18
#define APPLE_AGX_ROOT_GLOBALS          0x20
#define APPLE_AGX_ROOT_ENABLE           0x2c
#define APPLE_AGX_ROOT_GEOMETRY         0x30
#define APPLE_AGX_ROOT_LEVELS           0x34
#define APPLE_AGX_ROOT_LEVEL_STRIDE     0x20
#define APPLE_AGX_ROOT_FLAGS            0xa8
#define APPLE_AGX_ROOT_CONTROL          0xb0
#define APPLE_AGX_ROOT_POWER            0xb8

/* Runtime object: where the firmware finds the hardware description and every ring. */
#define APPLE_AGX_RUNTIME_SIZE          0x4b3
#define APPLE_AGX_RUNTIME_HWDATA        0x00
#define APPLE_AGX_RUNTIME_PAIR          0x08
#define APPLE_AGX_RUNTIME_PAIR2         0x10
#define APPLE_AGX_RUNTIME_CHANNELS      0x18
#define APPLE_AGX_RUNTIME_CHANNEL_STRIDE 0x20
#define APPLE_AGX_RUNTIME_RX            0x1b8
#define APPLE_AGX_RUNTIME_RX_STRIDE     0x10
#define APPLE_AGX_RUNTIME_SMALL         0x1f8
#define APPLE_AGX_RUNTIME_TA_ACTIVITY   0x234
#define APPLE_AGX_RUNTIME_FS_ACTIVITY   0x23c
#define APPLE_AGX_RUNTIME_CS_ACTIVITY   0x244
#define APPLE_AGX_RUNTIME_FAULT_INFO    0x24c
#define APPLE_AGX_RUNTIME_C0            0x2a8
#define APPLE_AGX_RUNTIME_C1            0x2b0
#define APPLE_AGX_RUNTIME_C1_2          0x2b8
#define APPLE_AGX_RUNTIME_C3            0x2c0
#define APPLE_AGX_RUNTIME_C3_2          0x2c8
#define APPLE_AGX_RUNTIME_UNKNOWN_2D0   0x2d0
#define APPLE_AGX_RUNTIME_READY         0x314
#define APPLE_AGX_RUNTIME_READY_MAGIC   0xabcdabcdUL
#define APPLE_AGX_RUNTIME_UNKNOWN_3B0   0x3b0
#define APPLE_AGX_RUNTIME_HWDATA_A      0x441

/* Control object: the firmware is enabled through two words and halts itself through a third. */
#define APPLE_AGX_CONTROL_SIZE          0xc3d0
#define APPLE_AGX_CONTROL_ENABLE        0x04
#define APPLE_AGX_CONTROL_FWCTL_STATE   0x4568
#define APPLE_AGX_CONTROL_FWCTL_RING    0x4570
#define APPLE_AGX_CONTROL_HALTED        0x4590
#define APPLE_AGX_CONTROL_ENABLE2       0x45c4

#define APPLE_AGX_FLAGS_SIZE            0x40
#define APPLE_AGX_FLAGS_READY           0x04

/* Hardware description: the driver writes only the addresses and the counts into it. */
#define APPLE_AGX_HWDATA_SIZE           35332
#define APPLE_AGX_HWDATA_CACHED         0x4a04
#define APPLE_AGX_HWDATA_TIMESTAMPS     0x28
#define APPLE_AGX_HWDATA_IOMAPS         0x640
#define APPLE_AGX_HWDATA_IOMAP_STRIDE   32
#define APPLE_AGX_HWDATA_IOMAP_VA       8
#define APPLE_AGX_HWDATA_TTBAT          0xb44
#define APPLE_AGX_HWDATA_DVFS_COUNT     0xb58
#define APPLE_AGX_HWDATA_TA_ACTIVITY    0x1880
#define APPLE_AGX_HWDATA_FS_ACTIVITY    0x24c0
#define APPLE_AGX_HWDATA_CS_ACTIVITY    0x3740
#define APPLE_AGX_HWDATA_HWDATA_A       0x4580

#define APPLE_AGX_GLOBALS_SIZE          15868
#define APPLE_AGX_POWER_SIZE            2292
#define APPLE_AGX_REGION_A_SIZE         0x4000
#define APPLE_AGX_PAIR_SIZE             0x100
#define APPLE_AGX_SMALL_SIZE            0x10

/* The rings, their records and how many of each the firmware is given. */
#define APPLE_AGX_SUBMIT_CHANNELS       12
#define APPLE_AGX_STATE_SIZE            0x30
#define APPLE_AGX_SUBMIT_RING_SIZE      0x1800
#define APPLE_AGX_DEVCTL_RING_SIZE      0x3800
#define APPLE_AGX_EVENT_RING_SIZE       0x3800
#define APPLE_AGX_LOG_RINGS             6
#define APPLE_AGX_LOG_STATE_SIZE        0x120
#define APPLE_AGX_LOG_RING_SIZE         0x51000
#define APPLE_AGX_TRACE_RING_SIZE       0x7000
#define APPLE_AGX_STATS_RING_SIZE       0x4000
#define APPLE_AGX_FWCTL_RING_SIZE       0x1800
#define APPLE_AGX_TIMESTAMPS_SIZE       0x4000000
#define APPLE_AGX_TIMESTAMP_ARENA       0xfffffc2ffc000000ULL

/* How a page of the initial data may be reached. */
#define APPLE_AGX_CLASS_FIRMWARE        0
#define APPLE_AGX_CLASS_FIRMWARE_CACHED 1
#define APPLE_AGX_CLASS_SHARED          2
#define APPLE_AGX_CLASS_SHARED_CACHED   3

/* Every object of the initial data, in the order this library keeps them. */
#define APPLE_AGX_OBJECT_ROOT           0
#define APPLE_AGX_OBJECT_REGION_A       1
#define APPLE_AGX_OBJECT_RUNTIME        2
#define APPLE_AGX_OBJECT_HWDATA         3
#define APPLE_AGX_OBJECT_PAIR           4
#define APPLE_AGX_OBJECT_SMALL          5
#define APPLE_AGX_OBJECT_C0             6
#define APPLE_AGX_OBJECT_C1             7
#define APPLE_AGX_OBJECT_C3             8
#define APPLE_AGX_OBJECT_GLOBALS        9
#define APPLE_AGX_OBJECT_POWER          10
#define APPLE_AGX_OBJECT_CONTROL        11
#define APPLE_AGX_OBJECT_FLAGS          12
#define APPLE_AGX_OBJECT_TIMESTAMPS     13
#define APPLE_AGX_OBJECT_DEVCTL_STATE   14
#define APPLE_AGX_OBJECT_DEVCTL_RING    15
#define APPLE_AGX_OBJECT_EVENT_STATE    16
#define APPLE_AGX_OBJECT_EVENT_RING     17
#define APPLE_AGX_OBJECT_LOG_STATE      18
#define APPLE_AGX_OBJECT_LOG_RING       19
#define APPLE_AGX_OBJECT_TRACE_STATE    20
#define APPLE_AGX_OBJECT_TRACE_RING     21
#define APPLE_AGX_OBJECT_STATS_STATE    22
#define APPLE_AGX_OBJECT_STATS_RING     23
#define APPLE_AGX_OBJECT_FWCTL_STATE    24
#define APPLE_AGX_OBJECT_FWCTL_RING     25
#define APPLE_AGX_FIXED_OBJECTS         26
#define APPLE_AGX_OBJECTS               (APPLE_AGX_FIXED_OBJECTS + 2 * APPLE_AGX_SUBMIT_CHANNELS)

typedef struct _APPLE_AGX_LAYOUT
{
    ULONG64 Address;
    ULONG Size;
    UCHAR Class;
} APPLE_AGX_LAYOUT, *PAPPLE_AGX_LAYOUT;

/* A firmware object: its pages mapped once for the processor and once in the firmware's window,
 * and, when the graphics processor reaches it through a client address space, the address it has
 * there. */
typedef struct _APPLE_AGX_OBJECT
{
    ULONG64 Address;
    ULONG64 Client;
    PVOID Virtual;
    PMDL Mdl;
    PVOID Mapping;
    ULONG64 Page;
    ULONG64 ClientPage;
    SIZE_T Bytes;
    UCHAR Class;
    UCHAR Leaf;
    BOOLEAN Lower;
} APPLE_AGX_OBJECT, *PAPPLE_AGX_OBJECT;

typedef struct _APPLE_AGX_HEAP
{
    PAPPLE_UAT_ADOPTED Adopted;
    ULONG64 Next;
    ULONG64 End;
    ULONG Pages;
    PCSTR Owner;
} APPLE_AGX_HEAP, *PAPPLE_AGX_HEAP;

typedef struct _APPLE_AGX_INIT
{
    PAPPLE_UAT_ADOPTED Adopted;
    PVOID Virtual[APPLE_AGX_OBJECTS];
    APPLE_AGX_OBJECT Object[APPLE_AGX_OBJECTS];
    ULONG Pages;
    ULONG Allocations;
    PCSTR Owner;
} APPLE_AGX_INIT, *PAPPLE_AGX_INIT;

BOOLEAN
NTAPI
AppleAgxLayout(
    _In_ ULONG Object,
    _Out_ PAPPLE_AGX_LAYOUT Layout);

VOID
NTAPI
AppleAgxClassProtection(
    _In_ UCHAR Class,
    _Out_ PAPPLE_UAT_PROTECTION Protection);

NTSTATUS
NTAPI
AppleAgxInitCreate(
    _Out_ PAPPLE_AGX_INIT Init,
    _In_ PAPPLE_UAT_ADOPTED Adopted,
    _In_z_ PCSTR Owner);

NTSTATUS
NTAPI
AppleAgxInitWrite(
    _Inout_ PAPPLE_AGX_INIT Init);

BOOLEAN
NTAPI
AppleAgxInitReady(
    _In_ PAPPLE_AGX_INIT Init);

/* The window the host places its own firmware objects in, each followed by an unmapped page. */
#define APPLE_AGX_HEAP_BASE             0xfffffc2d00000000ULL
#define APPLE_AGX_HEAP_END              0xfffffc2e00000000ULL

/* How a client address space maps the firmware objects aliased into it. */
#define APPLE_AGX_LEAF_NONE             0
#define APPLE_AGX_LEAF_UNCACHED         1
#define APPLE_AGX_LEAF_CACHED           2
#define APPLE_AGX_LEAF_PB_LIST          3

typedef struct _APPLE_AGX_REQUEST
{
    SIZE_T Size;
    ULONG Offset;
    UCHAR Class;
    UCHAR Leaf;
    BOOLEAN Lower;
    BOOLEAN Cpu;
    BOOLEAN Hidden;
    ULONG64 At;
    ULONG64 Client;
    ULONG CachedBytes;
} APPLE_AGX_REQUEST, *PAPPLE_AGX_REQUEST;

VOID
NTAPI
AppleAgxLeafProtection(
    _In_ UCHAR Leaf,
    _Out_ PAPPLE_UAT_PROTECTION Protection);

NTSTATUS
NTAPI
AppleAgxObjectPlace(
    _Inout_ PAPPLE_AGX_HEAP Heap,
    _In_ const APPLE_AGX_REQUEST *Request,
    _Out_ PULONG64 Page,
    _Out_ PSIZE_T Bytes);

NTSTATUS
NTAPI
AppleAgxObjectCreate(
    _Inout_ PAPPLE_AGX_HEAP Heap,
    _In_ const APPLE_AGX_REQUEST *Request,
    _Out_ PAPPLE_AGX_OBJECT Object);

NTSTATUS
NTAPI
AppleAgxObjectAlias(
    _In_ const APPLE_AGX_OBJECT *Object,
    _Inout_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address);

VOID
NTAPI
AppleAgxObjectFree(
    _Inout_ PAPPLE_AGX_OBJECT Object);

VOID
NTAPI
AppleAgxObjectZero(
    _In_ const APPLE_AGX_OBJECT *Object,
    _In_ SIZE_T Offset,
    _In_ SIZE_T Length,
    _In_ UCHAR Fill);

ULONG64
NTAPI
AppleAgxInitMessage(VOID);

VOID
NTAPI
AppleAgxInitRelease(
    _Inout_ PAPPLE_AGX_INIT Init);

/* A ring and its state block: the consumer's index, the completion index and the producer's. */
#define APPLE_AGX_RING_SLOTS            256
#define APPLE_AGX_RING_AHEAD            64
#define APPLE_AGX_STATE_READ            0x00
#define APPLE_AGX_STATE_COMPLETION      0x10
#define APPLE_AGX_STATE_WRITE           0x20
#define APPLE_AGX_SUBMIT_RECORD         0x18
#define APPLE_AGX_DEVCTL_RECORD         0x38
#define APPLE_AGX_EVENT_RECORD          0x38
#define APPLE_AGX_DOORBELL_BASE         (0x0083ULL << 48)
#define APPLE_AGX_DOORBELL_DEVCTL       0x11

typedef struct _APPLE_AGX_RING
{
    PVOID State;
    PVOID Ring;
    ULONG Record;
    ULONG Producer;
} APPLE_AGX_RING, *PAPPLE_AGX_RING;

VOID
NTAPI
AppleAgxRingBind(
    _Out_ PAPPLE_AGX_RING Ring,
    _In_ PVOID State,
    _In_ PVOID Records,
    _In_ ULONG Record);

ULONG
NTAPI
AppleAgxRingIndex(
    _In_ PAPPLE_AGX_RING Ring,
    _In_ ULONG Offset);

NTSTATUS
NTAPI
AppleAgxRingPost(
    _Inout_ PAPPLE_AGX_RING Ring,
    _In_reads_bytes_(Ring->Record) const VOID *Record,
    _In_ BOOLEAN Ahead,
    _Out_opt_ PULONG Next);

BOOLEAN
NTAPI
AppleAgxRingDone(
    _In_ PAPPLE_AGX_RING Ring,
    _In_ ULONG Next);

BOOLEAN
NTAPI
AppleAgxRingTake(
    _Inout_ PAPPLE_AGX_RING Ring,
    _Out_writes_bytes_(Ring->Record) PVOID Record);

VOID
NTAPI
AppleAgxRingConsume(
    _Inout_ PAPPLE_AGX_RING Ring);

ULONG64
NTAPI
AppleAgxRingDoorbell(
    _In_ ULONG Channel);

/* The register windows the firmware reaches through its own address space. One record per slot in
 * the hardware description: the physical address, the address the firmware uses, the size. */
#define APPLE_AGX_IOMAPS                15
#define APPLE_AGX_IOMAP_SLOTS           32
#define APPLE_AGX_IOMAP_PHYSICAL        0x00
#define APPLE_AGX_IOMAP_ADDRESS         0x08
#define APPLE_AGX_IOMAP_TOTAL           0x10
#define APPLE_AGX_IOMAP_ELEMENT         0x14
#define APPLE_AGX_IOMAP_WRITABLE        0x18

typedef struct _APPLE_AGX_IOMAP
{
    ULONG64 Physical;
    ULONG64 Address;
    ULONG Size;
    UCHAR Slot;
} APPLE_AGX_IOMAP, *PAPPLE_AGX_IOMAP;

const APPLE_AGX_IOMAP *
NTAPI
AppleAgxIoMap(
    _In_ ULONG Index);

NTSTATUS
NTAPI
AppleAgxMapRegisters(
    _Inout_ PAPPLE_AGX_INIT Init);

/* The two operating point tables the firmware runs the graphics block by, built from the
 * performance states the machine publishes. Each table names nine of them. */
#define APPLE_AGX_DVFS_STATES           9
#define APPLE_AGX_HWDATA_CHECK_A38      0xa38
#define APPLE_AGX_HWDATA_DVFS_MHZ_A     0xb5c
#define APPLE_AGX_HWDATA_DVFS_MHZ_B     0xf9c
#define APPLE_AGX_HWDATA_DVFS_MHZ_B2    0x1350
#define APPLE_AGX_HWDATA_DVFS_CORE_MV   0xb9c
#define APPLE_AGX_HWDATA_DVFS_SRAM_MV   0xd9c
#define APPLE_AGX_HWDATA_DVFS_STRIDE    0x20
#define APPLE_AGX_HWDATA_DVFS_REPEAT    8
#define APPLE_AGX_HWDATA_TOP_MHZ        0x4dc4

const UCHAR *
NTAPI
AppleAgxDvfsStates(
    _In_ BOOLEAN Second);

NTSTATUS
NTAPI
AppleAgxWriteStates(
    _Inout_ PAPPLE_AGX_INIT Init,
    _In_ const APPLE_AGX_DESCRIPTION *Description,
    _Out_ PULONG TopKilohertz);

/* A work queue the firmware runs: its description, its cursors and a ring of work addresses.
 * A submission on a channel names the queue and the head of its ring to run to. */
#define APPLE_AGX_QINFO_SIZE            0xb4
#define APPLE_AGX_QINFO_STATE           0x00
#define APPLE_AGX_QINFO_RING            0x08
#define APPLE_AGX_QINFO_JOB_LIST        0x10
#define APPLE_AGX_QINFO_SCRATCH         0x18
#define APPLE_AGX_QINFO_UNKNOWN_2C      0x2c
#define APPLE_AGX_QINFO_CLASS           0x30
#define APPLE_AGX_QINFO_CLASS_B         0x34
#define APPLE_AGX_QINFO_MASK            0x38
#define APPLE_AGX_QINFO_MASK_VALUE      0xffff000000000000ULL
#define APPLE_AGX_QINFO_UNKNOWN_48      0x48
#define APPLE_AGX_QINFO_UNKNOWN_4C      0x4c
#define APPLE_AGX_QINFO_UUID            0x50
#define APPLE_AGX_QUEUE_CLASS           2
#define APPLE_AGX_QUEUE_ENTRIES         0x500
#define APPLE_AGX_QUEUE_SCRATCH_SIZE    0x40000
#define APPLE_AGX_QINFO_SCHED_CONTEXT   0xa4
#define APPLE_AGX_QINFO_UNKNOWN_AC      0xac
#define APPLE_AGX_QINFO_UNKNOWN_AC_VALUE 4

#define APPLE_AGX_QSTATE_SIZE           0x60
#define APPLE_AGX_QSTATE_DONE           0x00
#define APPLE_AGX_QSTATE_READ           0x30
#define APPLE_AGX_QSTATE_WRITE          0x40
#define APPLE_AGX_QSTATE_CAPACITY       0x50

#define APPLE_AGX_SCHED_CONTEXT_SIZE    0x40
#define APPLE_AGX_LEGACY_EMPTY_SIZE     0x40
#define APPLE_AGX_COMPUTE_LEGACY_SIZE   0x4000
#define APPLE_AGX_THRESHOLD_SIZE        4
#define APPLE_AGX_STAMP_SIZE            4
#define APPLE_AGX_NOTIFIER_SIZE         0x108
#define APPLE_AGX_NOTIFIER_THRESHOLD    0x00
#define APPLE_AGX_NOTIFIER_UNKNOWN_10   0x10
#define APPLE_AGX_NOTIFIER_UNKNOWN_10_VALUE 0x50
#define APPLE_AGX_NOTIFIER_MASK         0xa8
#define APPLE_AGX_JOB_LIST_SIZE         0x18
#define APPLE_AGX_JOB_LIST_SELF         0x08
#define APPLE_AGX_WORK_ENTRY            8

/* What the firmware is asked to run. */
#define APPLE_AGX_WORK_VERTEX           0
#define APPLE_AGX_WORK_FRAGMENT         1
#define APPLE_AGX_WORK_COMPUTE          3

/* The record a submission carries on a channel. */
#define APPLE_AGX_SUBMIT_QUEUE          0x08
#define APPLE_AGX_SUBMIT_KIND           0x10
#define APPLE_AGX_SUBMIT_HEAD           0x14
#define APPLE_AGX_SUBMIT_EVENT          0x16
#define APPLE_AGX_SUBMIT_NEW_QUEUE      0x17

typedef struct _APPLE_AGX_QUEUE
{
    ULONG64 Info;
    ULONG64 State;
    ULONG64 Ring;
    ULONG64 JobList;
    ULONG64 Scratch;
    ULONG64 SchedContext;
    PVOID InfoVirtual;
    PVOID StateVirtual;
    PVOID RingVirtual;
    ULONG Entries;
    ULONG Write;
    ULONG Uuid;
} APPLE_AGX_QUEUE, *PAPPLE_AGX_QUEUE;

NTSTATUS
NTAPI
AppleAgxQueueDescribe(
    _Inout_ PAPPLE_AGX_QUEUE Queue);

NTSTATUS
NTAPI
AppleAgxQueuePost(
    _Inout_ PAPPLE_AGX_QUEUE Queue,
    _In_ ULONG64 Work,
    _Out_ PUSHORT Head);

NTSTATUS
NTAPI
AppleAgxQueueAppend(
    _Inout_ PAPPLE_AGX_QUEUE Queue,
    _In_ ULONG64 Work);

VOID
NTAPI
AppleAgxQueuePublish(
    _Inout_ PAPPLE_AGX_QUEUE Queue,
    _Out_ PUSHORT Head);

ULONG
NTAPI
AppleAgxQueueDone(
    _In_ PAPPLE_AGX_QUEUE Queue);

VOID
NTAPI
AppleAgxSubmitRecord(
    _Out_writes_bytes_(APPLE_AGX_SUBMIT_RECORD) PVOID Record,
    _In_ ULONG64 Queue,
    _In_ ULONG Kind,
    _In_ USHORT Head,
    _In_ UCHAR Event,
    _In_ BOOLEAN NewQueue);

/* The opcodes of the sequence the firmware interprets for a piece of work. */
#define APPLE_AGX_OP_BARRIER            4
#define APPLE_AGX_OP_VERTEX_START       5
#define APPLE_AGX_OP_VERTEX_FINALIZE    6
#define APPLE_AGX_OP_FRAGMENT_START     7
#define APPLE_AGX_OP_FRAGMENT_FINALIZE  8
#define APPLE_AGX_OP_INIT_BM            6
#define APPLE_AGX_OP_COMPUTE_START      0x0b
#define APPLE_AGX_OP_COMPUTE_FINALIZE   0x0c
#define APPLE_AGX_OP_TIMESTAMP_END      3
#define APPLE_AGX_OP_TIMESTAMP          0x80000003
#define APPLE_AGX_OP_END                0x40000002

NTSTATUS
NTAPI
AppleAgxDescribeGraph(
    _In_ PVOID Notifier,
    _In_ ULONG64 Threshold,
    _In_ PVOID JobList,
    _In_ ULONG64 JobListAddress,
    _In_ PVOID SchedContext);

/* The allocator the firmware takes a compute workgroup's memory from: a table of pools and a
 * freelist seeded with every unit of the first of them. */
#define APPLE_AGX_MANAGER_SIZE          0x84
#define APPLE_AGX_MANAGER_ID            0x00
#define APPLE_AGX_MANAGER_INDEX         0x08
#define APPLE_AGX_MANAGER_ONE           0x0c
#define APPLE_AGX_MANAGER_FREELIST      0x14
#define APPLE_AGX_MANAGER_UNKNOWN_1C    0x1c
#define APPLE_AGX_MANAGER_UNKNOWN_1C_VALUE 0x80000
#define APPLE_AGX_MANAGER_SEEDS_A       0x24
#define APPLE_AGX_MANAGER_SEEDS_B       0x2c
#define APPLE_AGX_MANAGER_POOL_TABLE    0x34
#define APPLE_AGX_MANAGER_UNKNOWN_44    0x44
#define APPLE_AGX_MANAGER_UNKNOWN_44_VALUE 0x40
#define APPLE_AGX_MANAGER_COUNTER       0x48
#define APPLE_AGX_MANAGER_COMPUTE_ID    3
#define APPLE_AGX_MANAGER_COMPUTE_INDEX 2

#define APPLE_AGX_POOLS                 22
#define APPLE_AGX_POOL_SIZE             0x400000
#define APPLE_AGX_POOL_STRIDE           0x40
#define APPLE_AGX_POOL_VALID            (1ULL << 62)
#define APPLE_AGX_POOL_UNIT             0x1000
#define APPLE_AGX_POOL_UNITS            (APPLE_AGX_POOL_SIZE / APPLE_AGX_POOL_UNIT)
#define APPLE_AGX_FREELIST_SEEDS        (APPLE_AGX_POOLS * APPLE_AGX_POOL_UNITS)
#define APPLE_AGX_FREELIST_SIZE         0x400000
#define APPLE_AGX_POOL_TABLE_SIZE       0x1000
#define APPLE_AGX_POOL_TABLE_OFFSET     0x3000
#define APPLE_AGX_UMA_POOLS             8
#define APPLE_AGX_UMA_SEEDS             (APPLE_AGX_UMA_POOLS * APPLE_AGX_POOL_UNITS)

NTSTATUS
NTAPI
AppleAgxDescribeManager(
    _Out_writes_bytes_(APPLE_AGX_MANAGER_SIZE) PVOID Manager,
    _In_ ULONG Id,
    _In_ ULONG Index,
    _In_ ULONG Seeds,
    _In_ ULONG64 Freelist,
    _In_ ULONG64 PoolTable,
    _In_ ULONG64 Counter);

NTSTATUS
NTAPI
AppleAgxSeedPools(
    _Out_writes_bytes_(Pools * APPLE_AGX_POOL_STRIDE) PVOID PoolTable,
    _Out_writes_bytes_(Pools * APPLE_AGX_POOL_UNITS * 8) PVOID Freelist,
    _In_reads_(Pools) const ULONG64 *Pool,
    _In_ ULONG Pools,
    _Out_ PULONG Seeds);

/* A compute work item and the sequence the firmware interprets to run it. */
#define APPLE_AGX_COMPUTE_WORK_SIZE     0x893
#define APPLE_AGX_COMPUTE_SEQUENCE_SIZE 0x2b8
#define APPLE_AGX_COMPUTE_REGISTERS     0x20
#define APPLE_AGX_COMPUTE_HWDATA        0x3740
#define APPLE_AGX_COMPUTE_ATTACHMENTS   0x54
#define APPLE_AGX_COMPUTE_ATTACHMENT_STRIDE 16
#define APPLE_AGX_COMPUTE_FINALIZE_AT   0x238
#define APPLE_AGX_COMPUTE_END_AT        0x2b4
#define APPLE_AGX_COMPUTE_TIMESTAMP_START 0x1bc
#define APPLE_AGX_COMPUTE_TIMESTAMP_END 0x1fc
#define APPLE_AGX_COMPUTE_TAIL          0x810
#define APPLE_AGX_COMPUTE_UUID          0xc10000
#define APPLE_AGX_MAX_ATTACHMENTS       16
#define APPLE_AGX_ATTACHMENT_GRAIN      128

typedef struct _APPLE_AGX_COMPUTE_REFS
{
    ULONG64 Hwdata;
    ULONG64 Notifier;
    ULONG64 Queue;
    ULONG64 FirmwareStamp;
    ULONG64 Manager;
    ULONG64 Work;
    ULONG64 Scratch[5];
    /* What the work item itself names: its own client address, its sequence, the stamps the
     * host and the firmware watch, the internal timestamp cells, the tail and the preemption
     * buffer, and where the command stream it runs ends. */
    ULONG64 WorkClient;
    ULONG64 Sequence;
    ULONG64 UserStamp;
    ULONG64 Timestamps;
    ULONG64 Tail;
    ULONG64 Preempt;
    ULONG64 StreamEnd;
    ULONG64 UserTimestamps[2];
} APPLE_AGX_COMPUTE_REFS, *PAPPLE_AGX_COMPUTE_REFS;

typedef struct _APPLE_AGX_ATTACHMENT
{
    ULONG64 Pointer;
    ULONG64 Size;
} APPLE_AGX_ATTACHMENT, *PAPPLE_AGX_ATTACHMENT;

NTSTATUS
NTAPI
AppleAgxEncodeCompute(
    _Out_writes_bytes_(APPLE_AGX_COMPUTE_SEQUENCE_SIZE) PVOID Sequence,
    _In_ const APPLE_AGX_COMPUTE_REFS *Refs,
    _In_reads_opt_(Attachments) const APPLE_AGX_ATTACHMENT *Attachment,
    _In_ ULONG Attachments,
    _In_ ULONG Context,
    _In_ ULONG Stamp,
    _In_ BOOLEAN Faults);

#define APPLE_AGX_COMPUTE_REGISTER_COUNT 22
#define APPLE_AGX_REGISTER_RECORD       12
#define APPLE_AGX_EVENT_SLOT            2

const ULONG *
NTAPI
AppleAgxComputeSelectors(VOID);

NTSTATUS
NTAPI
AppleAgxEncodeComputeWork(
    _Out_writes_bytes_(APPLE_AGX_COMPUTE_WORK_SIZE) PVOID Work,
    _In_ const APPLE_AGX_COMPUTE_REFS *Refs,
    _In_reads_(APPLE_AGX_COMPUTE_REGISTER_COUNT) const ULONG64 *Register,
    _In_ ULONG Context,
    _In_ ULONG Ordinal,
    _In_ ULONG Stamp);

/* The vertex and fragment halves of the render path: their work images, the sequences that run
 * them and the registers each is given. */
#define APPLE_AGX_VERTEX_WORK_SIZE      0x92b
#define APPLE_AGX_VERTEX_SEQUENCE_SIZE  0x2d8
#define APPLE_AGX_VERTEX_REGISTERS      0x40
#define APPLE_AGX_VERTEX_REGISTER_COUNT 72
#define APPLE_AGX_VERTEX_HWDATA         0x1884
#define APPLE_AGX_VERTEX_TAIL           0x8a8
#define APPLE_AGX_VERTEX_FINALIZE_AT    0x248
#define APPLE_AGX_VERTEX_END_AT         0x2d4
#define APPLE_AGX_VERTEX_PARAMETER_REQUEST 32

#define APPLE_AGX_FRAGMENT_WORK_SIZE    0xc73
#define APPLE_AGX_FRAGMENT_SEQUENCE_SIZE 0x310
#define APPLE_AGX_FRAGMENT_REGISTERS    0x80
#define APPLE_AGX_FRAGMENT_REGISTER_COUNT 79
#define APPLE_AGX_FRAGMENT_HWDATA       0x24c8
#define APPLE_AGX_FRAGMENT_TAIL         0xbf0
#define APPLE_AGX_FRAGMENT_FINALIZE_AT  0x258
#define APPLE_AGX_FRAGMENT_END_AT       0x30c
#define APPLE_AGX_TIMESTAMP_RECORD      0x3c

const ULONG *
NTAPI
AppleAgxSelectors(
    _In_ ULONG WorkType,
    _Out_ PULONG Count);

NTSTATUS
NTAPI
AppleAgxWriteRegisters(
    _Inout_ PVOID Work,
    _In_ ULONG WorkType,
    _In_ const ULONG64 *Value);

#define APPLE_AGX_SEQUENCE_STORAGE          0x1000
#define APPLE_AGX_VERTEX_TIMESTAMP_START    0x1cc
#define APPLE_AGX_VERTEX_TIMESTAMP_END      0x20c
#define APPLE_AGX_VERTEX_POOL               0x18c
#define APPLE_AGX_VERTEX_STATE              0x784
#define APPLE_AGX_VERTEX_SAMPLER            0x84c
#define APPLE_AGX_VERTEX_SCRATCH_STATE      0x890
#define APPLE_AGX_VERTEX_UUID               0x7a0000
#define APPLE_AGX_FRAGMENT_TIMESTAMP_START  0x1dc
#define APPLE_AGX_FRAGMENT_TIMESTAMP_END    0x21c
#define APPLE_AGX_FRAGMENT_POOL             0x1a4
#define APPLE_AGX_FRAGMENT_BUSY             0xb70
#define APPLE_AGX_FRAGMENT_STRUCT6          0xb74
#define APPLE_AGX_FRAGMENT_STRUCT7          0xba0
#define APPLE_AGX_FRAGMENT_SCRATCH_FLAG     0xa58
#define APPLE_AGX_FRAGMENT_SCRATCH_STATE    0xbd8
#define APPLE_AGX_FRAGMENT_UUID             0x3d0000
#define APPLE_AGX_FRAGMENT_SCRATCH_MIN      0x10000
#define APPLE_AGX_TAIL_PAIR                 0x08
#define APPLE_AGX_TAIL_END                  0x10
#define APPLE_AGX_TAIL_USER                 0x18
#define APPLE_AGX_TAIL_REQUEST              0x50
#define APPLE_AGX_TAIL_COMPLETE             0x58

typedef struct _APPLE_AGX_RENDER_REFS
{
    ULONG64 Work;
    ULONG64 Sequence;
    ULONG64 Hwdata;
    ULONG64 Notifier;
    ULONG64 Queue;
    ULONG64 Scene;
    ULONG64 Manager;
    ULONG64 Pool;
    ULONG64 FirmwareStamp;
    ULONG64 Scratch;
    ULONG64 ScratchBytes;
    BOOLEAN Requested;
} APPLE_AGX_RENDER_REFS, *PAPPLE_AGX_RENDER_REFS;

NTSTATUS
NTAPI
AppleAgxEncodeVertexSequence(
    _Out_writes_bytes_(APPLE_AGX_SEQUENCE_STORAGE) PVOID Sequence,
    _In_ const APPLE_AGX_RENDER_REFS *Refs,
    _In_ ULONG Context,
    _In_ ULONG Stamp);

NTSTATUS
NTAPI
AppleAgxEncodeFragmentSequence(
    _Out_writes_bytes_(APPLE_AGX_SEQUENCE_STORAGE) PVOID Sequence,
    _In_ const APPLE_AGX_RENDER_REFS *Refs,
    _In_ ULONG Context,
    _In_ ULONG Stamp,
    _In_ ULONG QueueCommands);

#define APPLE_AGX_TILE_PIXELS               32
#define APPLE_AGX_MACROTILES                4
#define APPLE_AGX_MAX_DIMENSION             16384
#define APPLE_AGX_MAX_LAYERS                2048
#define APPLE_AGX_REGION_ENTRY              5
#define APPLE_AGX_TPC_ENTRY                 8

#define APPLE_AGX_COMPACT_BASE              0x1000000000ULL
#define APPLE_AGX_COMPACT_END               0x9000000000ULL
#define APPLE_AGX_VDM_LIMIT                 0x8000000000ULL
#define APPLE_AGX_GPU_ADDRESS_BITS          42

typedef struct _APPLE_AGX_GEOMETRY
{
    ULONG Layers;
    ULONG Tiles;
    ULONG Pixels;
    ULONG Screen;
    ULONG XBlocks;
    ULONG YBlocks;
    ULONG Blocks;
    ULONG RegionStride;
    ULONG TpcStride;
    ULONG IspBlocks;
    ULONG64 UtileConfig;
    ULONG64 TilemapBytes;
    ULONG64 TpcBytes;
} APPLE_AGX_GEOMETRY, *PAPPLE_AGX_GEOMETRY;

NTSTATUS
NTAPI
AppleAgxDescribeGeometry(
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG UtileWidth,
    _In_ ULONG UtileHeight,
    _In_ ULONG Samples,
    _In_ ULONG Layers,
    _In_ ULONG Clusters,
    _Out_ PAPPLE_AGX_GEOMETRY Geometry);

typedef struct _APPLE_AGX_VERTEX_INPUT
{
    ULONG64 Notifier;
    ULONG64 Manager;
    ULONG64 Scene;
    ULONG64 Empty;
    ULONG64 WorkClient;
    ULONG64 Sequence;
    ULONG64 Pool;
    ULONG64 Scratch;
    ULONG64 Stamp;
    ULONG64 FirmwareStamp;
    ULONG64 Timestamps[2];
    ULONG64 UserTimestamps[2];
    ULONG64 Preempt[3];
    ULONG64 Tilemap;
    ULONG64 Tpc;
    ULONG64 Heap;
    ULONG64 SceneUser;
    ULONG64 InitialSceneEntry;
    ULONG64 Vdm;
    ULONG64 Multisample;
    ULONG Ppp;
    ULONG PageCount;
    BOOLEAN ProcessEmptyTiles;
} APPLE_AGX_VERTEX_INPUT, *PAPPLE_AGX_VERTEX_INPUT;

typedef struct _APPLE_AGX_PROGRAM
{
    ULONG64 Address;
    ULONG64 Resources;
} APPLE_AGX_PROGRAM, *PAPPLE_AGX_PROGRAM;

typedef struct _APPLE_AGX_SURFACE
{
    ULONG64 Base;
    ULONG64 Stride;
} APPLE_AGX_SURFACE, *PAPPLE_AGX_SURFACE;

typedef struct _APPLE_AGX_FRAGMENT_STATE
{
    ULONG64 Multisample;
    ULONG MergeUpper[2];
    APPLE_AGX_PROGRAM EndOfTile;
    APPLE_AGX_PROGRAM Background;
    APPLE_AGX_PROGRAM PartialEndOfTile;
    APPLE_AGX_PROGRAM PartialBackground;
    ULONG64 Scissor;
    ULONG64 DepthBias;
    ULONG64 Query;
    ULONG IspControl;
    ULONG TilebufferControl;
    ULONG MirrorTilebufferControl;
    ULONG MirrorTileMode;
    ULONG64 TileMode;
    APPLE_AGX_SURFACE Depth;
    APPLE_AGX_SURFACE Stencil;
    APPLE_AGX_SURFACE DepthCompression;
    APPLE_AGX_SURFACE StencilCompression;
    ULONG64 ZlsControl;
    ULONG DepthDimensions;
    ULONG DepthClear;
    ULONG StencilClear;
    ULONG SampleSize;
    BOOLEAN ProcessEmptyTiles;
} APPLE_AGX_FRAGMENT_STATE, *PAPPLE_AGX_FRAGMENT_STATE;

typedef struct _APPLE_AGX_FRAGMENT_INPUT
{
    ULONG64 Notifier;
    ULONG64 Manager;
    ULONG64 Scene;
    ULONG64 Empty;
    ULONG64 Sequence;
    ULONG64 Pool;
    ULONG64 Stamp;
    ULONG64 FirmwareStamp;
    ULONG64 Timestamps[2];
    ULONG64 UserTimestamps[2];
    ULONG64 WorkClient;
    ULONG64 Tilemap;
    ULONG64 Heap;
    ULONG64 Auxiliary;
    ULONG64 SceneUser;
    ULONG64 Scratch;
    APPLE_AGX_FRAGMENT_STATE State;
} APPLE_AGX_FRAGMENT_INPUT, *PAPPLE_AGX_FRAGMENT_INPUT;

NTSTATUS
NTAPI
AppleAgxEncodeFragmentWork(
    _Out_writes_bytes_(APPLE_AGX_FRAGMENT_WORK_SIZE) PVOID Work,
    _In_ const APPLE_AGX_FRAGMENT_INPUT *Input,
    _In_ const APPLE_AGX_GEOMETRY *Geometry,
    _In_ ULONG Context,
    _In_ ULONG StampValue);

NTSTATUS
NTAPI
AppleAgxEncodeVertexWork(
    _Out_writes_bytes_(APPLE_AGX_VERTEX_WORK_SIZE) PVOID Work,
    _In_ const APPLE_AGX_VERTEX_INPUT *Input,
    _In_ const APPLE_AGX_GEOMETRY *Geometry,
    _In_ ULONG Context,
    _In_ ULONG StampValue);

/* The tiler's parameter buffer: pages the firmware hands out in blocks, the list it records them
 * on and the control it keeps the live producer in. */
#define APPLE_AGX_PB_PAGES              16384
#define APPLE_AGX_PB_BLOCKS             4096
#define APPLE_AGX_PB_LIST_RECORDS       4096
#define APPLE_AGX_PB_RECORD             8
#define APPLE_AGX_PB_PAGE_BYTES         0x8000
#define APPLE_AGX_PB_PAGES_PER_BLOCK    4
#define APPLE_AGX_PB_BLOCK_SIZE         0x20000
#define APPLE_AGX_PB_PAGE_LIST_SIZE     0x28000
#define APPLE_AGX_PB_BLOCK_LIST_SIZE    0x8000
#define APPLE_AGX_PB_GROW_STEP          10
#define APPLE_AGX_PB_INITIAL_BLOCKS     64
#define APPLE_AGX_PB_MAX_BLOCKS         (APPLE_AGX_PB_BLOCKS - 1)
#define APPLE_AGX_PB_CHUNK_BLOCKS       128
#define APPLE_AGX_PB_CHUNK_SIZE         (APPLE_AGX_PB_CHUNK_BLOCKS * APPLE_AGX_PB_BLOCK_SIZE)
#define APPLE_AGX_SCENE_LIST_SIZE       0xc0
#define APPLE_AGX_PB_MANAGER_SIZE       0xac
#define APPLE_AGX_PB_PAGE_LIST          0x1c
#define APPLE_AGX_PB_PAGE_LIST_BYTES    0x24
#define APPLE_AGX_PB_PAGE_COUNT         0x28
#define APPLE_AGX_PB_RECORDS            0x2c
#define APPLE_AGX_PB_BLOCK_COUNT        0x30
#define APPLE_AGX_PB_BLOCK_LIST         0x38
#define APPLE_AGX_PB_BLOCK_CONTROL      0x40
#define APPLE_AGX_PB_LAST_PAGE          0x48
#define APPLE_AGX_PB_BLOCK_BYTES        0x4c
#define APPLE_AGX_PB_COUNTER            0x58
#define APPLE_AGX_PB_MAX_PAGES_A        0x70
#define APPLE_AGX_PB_MAX_PAGES_B        0x74

#define APPLE_AGX_BLOCK_CONTROL_SIZE    0x40
#define APPLE_AGX_BLOCK_CONTROL_BLOCKS  0x00
#define APPLE_AGX_BLOCK_CONTROL_PRODUCER 0x04
#define APPLE_AGX_MISC_SIZE             0x40
#define APPLE_AGX_MISC_ENABLE           0x20
#define APPLE_AGX_COUNTER_SIZE          0x40
#define APPLE_AGX_SCENE_SIZE            0x80
#define APPLE_AGX_SCENE_LIST_SLOTS      48
#define APPLE_AGX_SCENE_USER_HEADER     0x40
#define APPLE_AGX_SCENE_USER_AUXILIARY  0x900
#define APPLE_AGX_SCENE_USER_MIN        0x904
#define APPLE_AGX_RENDER_POOLS          8

typedef struct _APPLE_AGX_PARAMETER_BUFFER
{
    ULONG64 PageList;
    ULONG64 BlockList;
    ULONG64 BlockControl;
    ULONG64 Counter;
    ULONG Blocks;
    ULONG MaxBlocks;
} APPLE_AGX_PARAMETER_BUFFER, *PAPPLE_AGX_PARAMETER_BUFFER;

ULONG
NTAPI
AppleAgxBlockRecord(
    _In_ ULONG Block);

NTSTATUS
NTAPI
AppleAgxDescribeParameterBuffer(
    _Out_writes_bytes_(APPLE_AGX_PB_MANAGER_SIZE) PVOID Manager,
    _In_ const APPLE_AGX_PARAMETER_BUFFER *Buffer);

NTSTATUS
NTAPI
AppleAgxDescribeBlockControl(
    _Out_writes_bytes_(APPLE_AGX_BLOCK_CONTROL_SIZE) PVOID Control,
    _Out_writes_bytes_(APPLE_AGX_MISC_SIZE) PVOID Misc,
    _In_ ULONG Blocks);

NTSTATUS
NTAPI
AppleAgxWriteBlocks(
    _Inout_updates_bytes_opt_(APPLE_AGX_PB_PAGE_LIST_SIZE) PVOID PageList,
    _Inout_updates_bytes_(APPLE_AGX_PB_BLOCK_LIST_SIZE) PVOID BlockList,
    _In_ ULONG64 Heap,
    _In_ ULONG First,
    _In_ ULONG Count);

#define APPLE_AGX_C1_RECORD             16
#define APPLE_AGX_C1_TAG                0x07400000

VOID
NTAPI
AppleAgxC1Record(
    _Out_writes_bytes_(APPLE_AGX_C1_RECORD) PVOID Record,
    _In_ ULONG Blocks);

#define APPLE_AGX_INIT_BM_SIZE          0x20

NTSTATUS
NTAPI
AppleAgxEncodeInitBm(
    _Out_writes_bytes_(APPLE_AGX_INIT_BM_SIZE) PVOID Record,
    _In_ ULONG Context,
    _In_ ULONG Blocks,
    _In_ ULONG64 Manager,
    _In_ ULONG Stamp);

/* What the firmware reports on its event ring, and the growth of the parameter buffer it asks
 * for there. */
#define APPLE_AGX_EVENT_FAULT           0
#define APPLE_AGX_EVENT_STAGE           1
#define APPLE_AGX_EVENT_TIMEOUT         4
#define APPLE_AGX_EVENT_GROW            7
#define APPLE_AGX_EVENT_CHANNEL_ERROR   8
#define APPLE_AGX_DEVCTL_GROW_REPLY     7
#define APPLE_AGX_DEVCTL_IMPORT_GLOBALS 0x13
#define APPLE_AGX_DEVCTL_START          9
#define APPLE_AGX_RECORD_WORDS          14

typedef struct _APPLE_AGX_GROWTH
{
    ULONG Posted;
    ULONG Backed;
    ULONG Max;
    ULONG Counter;
    ULONG Refused;
} APPLE_AGX_GROWTH, *PAPPLE_AGX_GROWTH;

NTSTATUS
NTAPI
AppleAgxAnswerGrowth(
    _Inout_ PAPPLE_AGX_GROWTH Growth,
    _In_reads_(APPLE_AGX_RECORD_WORDS) const ULONG *Request,
    _In_ ULONG Context,
    _Out_writes_(APPLE_AGX_RECORD_WORDS) PULONG Reply,
    _Out_ PULONG First,
    _Out_ PULONG Count);

NTSTATUS
NTAPI
AppleAgxDescribeScene(
    _Out_writes_bytes_(APPLE_AGX_SCENE_SIZE) PVOID Scene,
    _In_ ULONG64 ManagerEntry,
    _In_ ULONG64 ManagerMisc,
    _In_ ULONG64 UserBuffer,
    _In_ ULONG64 UserBytes);

typedef struct _APPLE_AGX_RENDER_STATE
{
    APPLE_AGX_GEOMETRY Geometry;
    APPLE_AGX_FRAGMENT_STATE Fragment;
    ULONG64 Vdm;
    ULONG64 Multisample;
    ULONG Ppp;
    BOOLEAN ProcessEmptyTiles;
} APPLE_AGX_RENDER_STATE, *PAPPLE_AGX_RENDER_STATE;

NTSTATUS
NTAPI
AppleAgxTranslateRender(
    _In_ const DRM_ASAHI_CMD_RENDER *Command,
    _In_ ULONG64 UscBase,
    _In_ ULONG Clusters,
    _Out_ PAPPLE_AGX_RENDER_STATE Render);

#define APPLE_AGX_BARRIER_SIZE          0x3e
#define APPLE_AGX_SEED_COMPUTE          0xc1000000
#define APPLE_AGX_SEED_VERTEX           0x7a000000
#define APPLE_AGX_SEED_FRAGMENT         0x3d000000
#define APPLE_AGX_STAMP_STEP            0x100
#define APPLE_AGX_EVENT_VERTEX          0
#define APPLE_AGX_EVENT_FRAGMENT        1
#define APPLE_AGX_EVENT_COMPUTE         2
#define APPLE_AGX_RENDER_WAIT_UUID      0x3d420

ULONG
NTAPI
AppleAgxStamp(
    _In_ ULONG Seed,
    _In_ ULONG64 Ordinal);

NTSTATUS
NTAPI
AppleAgxEncodeBarrier(
    _Out_writes_bytes_(APPLE_AGX_BARRIER_SIZE) PVOID Barrier,
    _In_ ULONG64 StampAddress,
    _In_ ULONG Wait,
    _In_ ULONG Event,
    _In_ ULONG Own,
    _In_ ULONG Uuid,
    _In_ BOOLEAN Hold);

typedef struct _APPLE_AGX_RENDER_JOB
{
    PVOID VertexWork;
    PVOID VertexSequence;
    PVOID FragmentWork;
    PVOID FragmentSequence;
    PVOID Scene;
    PVOID FragmentBarrier;
    ULONG64 VertexWorkAddress;
    ULONG64 VertexWorkClient;
    ULONG64 VertexSequenceAddress;
    ULONG64 FragmentWorkAddress;
    ULONG64 FragmentWorkClient;
    ULONG64 FragmentSequenceAddress;
    ULONG64 SceneAddress;
    ULONG64 Hwdata;
    ULONG64 Notifier;
    ULONG64 Empty;
    ULONG64 Manager;
    ULONG64 SceneList;
    ULONG64 ManagerMisc;
    ULONG64 Pool[2];
    ULONG64 Queue[2];
    ULONG64 UserStamp[2];
    ULONG64 FirmwareStamp[2];
    ULONG64 Timestamps[2][2];
    ULONG64 UserTimestamps[2][2];
    ULONG64 VertexScratch;
    ULONG64 FragmentScratch;
    ULONG64 FragmentScratchBytes;
    ULONG64 Tilemap;
    ULONG64 Tpc;
    ULONG64 Heap;
    ULONG64 Auxiliary;
    ULONG64 SceneUser;
    ULONG64 SceneUserBytes;
    ULONG64 Preempt[3];
    ULONG PageCount;
    ULONG QueueCommands;
} APPLE_AGX_RENDER_JOB, *PAPPLE_AGX_RENDER_JOB;

NTSTATUS
NTAPI
AppleAgxBuildRender(
    _In_ const APPLE_AGX_RENDER_JOB *Job,
    _In_ const APPLE_AGX_RENDER_STATE *Render,
    _In_ ULONG Context,
    _In_ ULONG64 Ordinal);

#define APPLE_AGX_G15_VERSION_CODE      0x07
#define APPLE_AGX_VARIANT_S_CODE        0x03
#define APPLE_AGX_CLUSTER_CORE_BITS     10
#define APPLE_AGX_USER_VA_START         0x4000ULL
#define APPLE_AGX_USER_VA_END           0x2ffffff8000ULL
#define APPLE_AGX_KERNEL_VA_MIN         0x20000000ULL
#define APPLE_AGX_MAX_COMMANDS          64
#define APPLE_AGX_TIMESTAMP_HZ          1000000000ULL

NTSTATUS
NTAPI
AppleAgxDescribeParams(
    _In_ ULONG Version,
    _In_ ULONG Clusters,
    _In_ ULONG64 CoreWord,
    _In_ ULONG ChipId,
    _In_ ULONG MaxKilohertz,
    _Out_ PDRM_ASAHI_PARAMS_GLOBAL Params);

#define APPLE_AGX_BUFFER_PAGE           0x8000ULL

typedef struct _APPLE_AGX_VM_RANGES
{
    ULONG64 UserStart;
    ULONG64 UserEnd;
    ULONG64 KernelStart;
    ULONG64 KernelGpuEnd;
    ULONG64 KernelEnd;
} APPLE_AGX_VM_RANGES, *PAPPLE_AGX_VM_RANGES;

NTSTATUS
NTAPI
AppleAgxDescribeVm(
    _In_ const DRM_ASAHI_VM_CREATE *Create,
    _Out_ PAPPLE_AGX_VM_RANGES Ranges);

NTSTATUS
NTAPI
AppleAgxGemSize(
    _In_ const DRM_ASAHI_GEM_CREATE *Create,
    _Out_ PULONG64 Size);

NTSTATUS
NTAPI
AppleAgxCheckBind(
    _In_ const DRM_ASAHI_GEM_BIND_OP *Op,
    _In_ const APPLE_AGX_VM_RANGES *Ranges,
    _In_ ULONG64 ObjectSize,
    _Out_ PAPPLE_UAT_PROTECTION Protection);

typedef VOID (NTAPI *PAPPLE_AGX_REFERENCE)(_In_ PVOID Object, _In_ LONG Delta);

typedef struct _APPLE_AGX_BINDING
{
    ULONG64 Address;
    ULONG64 Range;
    ULONG64 Offset;
    PVOID Object;
    BOOLEAN SinglePage;
} APPLE_AGX_BINDING, *PAPPLE_AGX_BINDING;

typedef struct _APPLE_AGX_BINDINGS
{
    PAPPLE_AGX_BINDING Entry;
    ULONG Count;
    ULONG Capacity;
    PAPPLE_AGX_REFERENCE Reference;
} APPLE_AGX_BINDINGS, *PAPPLE_AGX_BINDINGS;

NTSTATUS
NTAPI
AppleAgxBindingInsert(
    _Inout_ PAPPLE_AGX_BINDINGS Bindings,
    _In_ const APPLE_AGX_BINDING *Binding);

NTSTATUS
NTAPI
AppleAgxBindingRemove(
    _Inout_ PAPPLE_AGX_BINDINGS Bindings,
    _In_ ULONG64 Address,
    _In_ ULONG64 Range);

const APPLE_AGX_BINDING *
NTAPI
AppleAgxBindingFind(
    _In_ const APPLE_AGX_BINDINGS *Bindings,
    _In_ ULONG64 Address);

/* Where the firmware objects the graphics processor reaches through a client address space are
 * aliased in every one of them. Clients bind nothing there. */
#define APPLE_AGX_CLIENT_APERTURE       0x7000000000ULL
#define APPLE_AGX_CLIENT_APERTURE_END   0x7100000000ULL
#define APPLE_AGX_CLIENT_POOL(k)        (0x7000000000ULL + (ULONG64)(k) * 0x404000ULL)
#define APPLE_AGX_CLIENT_FREELIST       0x7005858000ULL
#define APPLE_AGX_CLIENT_POOL_TABLE     0x7005c5c000ULL
#define APPLE_AGX_CLIENT_PREEMPT        0x7005c64000ULL
#define APPLE_AGX_CLIENT_PREEMPT_STRIDE 0x10000ULL
#define APPLE_AGX_CLIENT_WORK           0x7021000000ULL
#define APPLE_AGX_CLIENT_WORK_OFFSET    0x3760
#define APPLE_AGX_CLIENT_WORK_STRIDE    0x4000ULL
#define APPLE_AGX_CLIENT_FS_WORK        0x7020000000ULL
#define APPLE_AGX_CLIENT_FS_WORK_OFFSET 0x3380
#define APPLE_AGX_CLIENT_TA_WORK        0x7020010000ULL
#define APPLE_AGX_CLIENT_TA_WORK_OFFSET 0x36c0
#define APPLE_AGX_CLIENT_PASS_WORK_STRIDE 0x20000ULL
#define APPLE_AGX_CLIENT_UMA(e, k)      (0x7010000000ULL + (ULONG64)(e) * 0x2450000ULL + (ULONG64)(k) * 0x408000ULL)
#define APPLE_AGX_CLIENT_UMA_FREELIST(e) (0x7012040000ULL + (ULONG64)(e) * 0x2450000ULL)
#define APPLE_AGX_CLIENT_UMA_TABLE(e)   (0x7012448000ULL + (ULONG64)(e) * 0x2450000ULL)
#define APPLE_AGX_CLIENT_AUX            0x70148a0000ULL
#define APPLE_AGX_CLIENT_AUX_STRIDE     0x98000ULL
#define APPLE_AGX_CLIENT_TPC            0x7040000000ULL
#define APPLE_AGX_CLIENT_TPC_SIZE       0x1000000ULL
#define APPLE_AGX_CLIENT_TILEMAP        0x7042000000ULL
#define APPLE_AGX_CLIENT_TILEMAP_SIZE   0x800000ULL
#define APPLE_AGX_CLIENT_TPC_STRIDE     0x4000000ULL
#define APPLE_AGX_CLIENT_PB_HEAP        0x70c0000000ULL
#define APPLE_AGX_CLIENT_PB_HEAP_SIZE   0x20000000ULL
#define APPLE_AGX_CLIENT_PB_BLOCK_LIST  0x70f0000000ULL
#define APPLE_AGX_CLIENT_PRE0           0x1006080000ULL
#define APPLE_AGX_CLIENT_PRE1           0x1006098000ULL
#define APPLE_AGX_CLIENT_PRE2           0x10060b0000ULL
#define APPLE_AGX_CLIENT_HEAP_META      0x1006140000ULL
#define APPLE_AGX_CLIENT_SCENE_USER     0x1006158000ULL
#define APPLE_AGX_CLIENT_FS_SCRATCH     0x1006178000ULL
#define APPLE_AGX_CLIENT_PASS_STRIDE    0x118000ULL
#define APPLE_AGX_CLIENT_PASS_SLOTS     16
#define APPLE_AGX_CLIENT_PB_PAGE_LIST   0x1074000000ULL

#define APPLE_AGX_PRE_RECORDS           2
#define APPLE_AGX_PRE0_RECORD           0x540
#define APPLE_AGX_PRE1_RECORD           0x280
#define APPLE_AGX_PRE2_RECORD           0x40
#define APPLE_AGX_PRE0_OFFSET           0x3580
#define APPLE_AGX_PRE1_OFFSET           0x3b00
#define APPLE_AGX_PRE2_OFFSET           0x3f80
#define APPLE_AGX_HEAP_META_SIZE        0x1000
#define APPLE_AGX_HEAP_META_OFFSET      0x3000
#define APPLE_AGX_AUX_SIZE              0x20000
#define APPLE_AGX_PASS_BUFFER_SIZE      0x10000
#define APPLE_AGX_PREEMPT_SIZE          0x10000
#define APPLE_AGX_PREEMPT_FILL          0xcc
#define APPLE_AGX_COMPUTE_SCRATCHES     5
#define APPLE_AGX_STAMP_CELLS           32
#define APPLE_AGX_COMPUTE_CELLS         16

BOOLEAN
NTAPI
AppleAgxClientReserved(
    _In_ ULONG64 Address,
    _In_ ULONG64 Range);

/* What the compute work of this processor is given: its preemption buffer, the stream it runs and
 * the saved state the firmware keeps at the tail of that buffer. */
#define APPLE_AGX_COMPUTE_PREEMPT_TAIL  0x1480
#define APPLE_AGX_COMPUTE_PTC           0x13200400020ULL
#define APPLE_AGX_COMPUTE_DUPM          0x200000001ULL
#define APPLE_AGX_COMPUTE_REG_1A0E9     8

NTSTATUS
NTAPI
AppleAgxComputeRegisters(
    _In_ const DRM_ASAHI_CMD_COMPUTE *Command,
    _In_ ULONG64 Preempt,
    _Out_writes_(APPLE_AGX_COMPUTE_REGISTER_COUNT) PULONG64 Register);

/* A submission's command buffer is walked one header at a time; the payload of each command is
 * copied out, shorter payloads zero-extended. */
typedef struct _APPLE_AGX_COMMAND
{
    DRM_ASAHI_CMD_HEADER Header;
    const UCHAR *Payload;
    ULONG Size;
} APPLE_AGX_COMMAND, *PAPPLE_AGX_COMMAND;

NTSTATUS
NTAPI
AppleAgxNextCommand(
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _Inout_ PULONG Offset,
    _Out_ PAPPLE_AGX_COMMAND Command);
