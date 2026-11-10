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
