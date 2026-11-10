/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Initial data the Apple silicon graphics firmware is started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

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

/* How a page of the initial data may be reached. */
#define APPLE_AGX_CLASS_FIRMWARE        0
#define APPLE_AGX_CLASS_FIRMWARE_CACHED 1
#define APPLE_AGX_CLASS_SHARED          2

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

typedef struct _APPLE_AGX_INIT
{
    PAPPLE_UAT_ADOPTED Adopted;
    PVOID Virtual[APPLE_AGX_OBJECTS];
    ULONG Pages;
    PVOID *Allocation;
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
#define APPLE_AGX_QINFO_UUID            0x50
#define APPLE_AGX_QINFO_SCHED_CONTEXT   0xa4
#define APPLE_AGX_QINFO_UNKNOWN_AC      0xac
#define APPLE_AGX_QINFO_UNKNOWN_AC_VALUE 4

#define APPLE_AGX_QSTATE_SIZE           0x60
#define APPLE_AGX_QSTATE_DONE           0x00
#define APPLE_AGX_QSTATE_READ           0x30
#define APPLE_AGX_QSTATE_WRITE          0x40
#define APPLE_AGX_QSTATE_CAPACITY       0x50

#define APPLE_AGX_SCHED_CONTEXT_SIZE    0x40
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
#define APPLE_AGX_SEEDED_POOLS          8
#define APPLE_AGX_FREELIST_SEEDS        8192

NTSTATUS
NTAPI
AppleAgxDescribeManager(
    _Out_writes_bytes_(APPLE_AGX_MANAGER_SIZE) PVOID Manager,
    _In_ ULONG64 Freelist,
    _In_ ULONG64 PoolTable,
    _In_ ULONG64 Counter);

NTSTATUS
NTAPI
AppleAgxSeedPools(
    _Out_writes_bytes_(APPLE_AGX_POOLS *APPLE_AGX_POOL_STRIDE) PVOID PoolTable,
    _Out_writes_bytes_(APPLE_AGX_FREELIST_SEEDS * 8) PVOID Freelist,
    _In_ ULONG64 FirstPool,
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
