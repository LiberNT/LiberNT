/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Data the Apple G15 graphics firmware is started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define APPLE_AGX_FW_MAGIC              0x0c08e21e83800490ULL
#define APPLE_AGX_FW_EP_MAIN            0x20
#define APPLE_AGX_FW_EP_KICK            0x21
#define APPLE_AGX_FW_MSG_INITDATA       0x0081000000000000ULL
#define APPLE_AGX_FW_MSG_ADDRESS        0x00000fffffffffffULL
#define APPLE_AGX_FW_MSG_TYPE(m)        ((ULONG)(((m) >> 48) & 0x3f))
#define APPLE_AGX_FW_MSG_TYPE_RINGS     2

#define APPLE_AGX_FW_PAGE               0x4000
#define APPLE_AGX_FW_ALIGN              0x40

#define APPLE_AGX_FW_GROUP_A            0
#define APPLE_AGX_FW_GROUP_B            1
#define APPLE_AGX_FW_GROUP_C            2
#define APPLE_AGX_FW_GROUP_CTL          3
#define APPLE_AGX_FW_GROUPS             4

#define APPLE_AGX_FW_HWCFG              0
#define APPLE_AGX_FW_TA_ACTIVITY        1
#define APPLE_AGX_FW_FS_ACTIVITY        2
#define APPLE_AGX_FW_CS_ACTIVITY        3
#define APPLE_AGX_FW_POWER              4
#define APPLE_AGX_FW_FAULT              5
#define APPLE_AGX_FW_BRN                6
#define APPLE_AGX_FW_MISC               7
#define APPLE_AGX_FW_RUNTIME            8
#define APPLE_AGX_FW_DEVCTL_RING        9
#define APPLE_AGX_FW_CONTROL            10
#define APPLE_AGX_FW_STATUS             11
#define APPLE_AGX_FW_FLAGS              12
#define APPLE_AGX_FW_EVENT_STATE        13
#define APPLE_AGX_FW_LOG_STATE          14
#define APPLE_AGX_FW_TRACE_STATE        15
#define APPLE_AGX_FW_STATS_STATE        16
#define APPLE_AGX_FW_EVENT_RING         17
#define APPLE_AGX_FW_LOG_RING           18
#define APPLE_AGX_FW_KERNEL_LOG         19
#define APPLE_AGX_FW_TRACE_RING         20
#define APPLE_AGX_FW_STATS_RING         21
#define APPLE_AGX_FW_GLOBALS            22
#define APPLE_AGX_FW_FWCTL_STATE        23
#define APPLE_AGX_FW_FWCTL_RING         24
#define APPLE_AGX_FW_OBJECTS            25

#define APPLE_AGX_FW_HWCFG_SIZE         0x1868
#define APPLE_AGX_FW_POWER_SIZE         0x4360
#define APPLE_AGX_FW_RUNTIME_SIZE       0x490
#define APPLE_AGX_FW_CONTROL_SIZE       0xc3d0
#define APPLE_AGX_FW_STATUS_SIZE        0x238
#define APPLE_AGX_FW_GLOBALS_SIZE       0xe00

#define APPLE_AGX_FW_ROOT_REGION_A      0x08
#define APPLE_AGX_FW_ROOT_RUNTIME       0x18
#define APPLE_AGX_FW_ROOT_GLOBALS       0x20
#define APPLE_AGX_FW_ROOT_ENABLE        0x28
#define APPLE_AGX_FW_ROOT_UAT           0x30
#define APPLE_AGX_FW_ROOT_UAT_SIZE      0x68
#define APPLE_AGX_FW_ROOT_FLAGS         0xa8
#define APPLE_AGX_FW_ROOT_CONTROL       0xb0
#define APPLE_AGX_FW_ROOT_STATUS        0xb8

#define APPLE_AGX_FW_RT_HWCFG           0x000
#define APPLE_AGX_FW_RT_BRN             0x008
#define APPLE_AGX_FW_RT_MISC            0x010
#define APPLE_AGX_FW_RT_CHANNELS        0x018
#define APPLE_AGX_FW_RT_DEVCTL          0x198
#define APPLE_AGX_FW_RT_RX              0x1b8
#define APPLE_AGX_FW_RT_KERNEL_LOG      0x1f8
#define APPLE_AGX_FW_RT_TA_ACTIVITY     0x234
#define APPLE_AGX_FW_RT_FS_ACTIVITY     0x23c
#define APPLE_AGX_FW_RT_CS_ACTIVITY     0x244
#define APPLE_AGX_FW_RT_FAULT           0x24c
#define APPLE_AGX_FW_RT_PB_TABLE        0x2b0
#define APPLE_AGX_FW_RT_UMA_TABLE       0x2c0
#define APPLE_AGX_FW_RT_POWER           0x441

#define APPLE_AGX_FW_CTL_FWCTL_STATE    0x4568
#define APPLE_AGX_FW_CTL_FWCTL_RING     0x4570
#define APPLE_AGX_FW_CTL_HALTED         0x4590
#define APPLE_AGX_FW_CTL_ENABLE         0x45c4

#define APPLE_AGX_FW_HW_IOMAPS          0x640
#define APPLE_AGX_FW_HW_IOMAP_STRIDE    0x20
#define APPLE_AGX_FW_HW_PSTATE_TOP      0xb58
#define APPLE_AGX_FW_HW_PSTATE_MHZ_A    0xb5c
#define APPLE_AGX_FW_HW_PSTATE_MV       0xb9c
#define APPLE_AGX_FW_HW_PSTATE_SRAM_MV  0xd9c
#define APPLE_AGX_FW_HW_PSTATE_MHZ_B    0xf9c
#define APPLE_AGX_FW_HW_PSTATE_MAP_A    0x10dc
#define APPLE_AGX_FW_HW_PSTATE_MAP_B    0x111c

#define APPLE_AGX_FW_DM_RINGS           12
#define APPLE_AGX_FW_RING_SLOTS         256
#define APPLE_AGX_FW_DM_RECORD          0x18
#define APPLE_AGX_FW_DEVCTL_RECORD      0x38
#define APPLE_AGX_FW_EVENT_RECORD       0x38
#define APPLE_AGX_FW_LOG_RINGS          6
#define APPLE_AGX_FW_STATE_READ         0x00
#define APPLE_AGX_FW_STATE_WRITE        0x20
#define APPLE_AGX_FW_STATE_SIZE         0x30

#define APPLE_AGX_FW_REGIONS            32
#define APPLE_AGX_FW_IOMAPS             31
#define APPLE_AGX_FW_REGION_SCRATCH     31
#define APPLE_AGX_FW_REGION_RANGES      16

#define APPLE_AGX_FW_PSTATES            16
#define APPLE_AGX_FW_PSTATE_COLUMNS     8

#define APPLE_AGX_FW_CORE_G15X_MAX      0x17
#define APPLE_AGX_FW_CORE_G15X          0x18

typedef struct _APPLE_AGX_FW_REGION
{
    BOOLEAN Writable;
    ULONG Count;
    ULONG64 Physical[APPLE_AGX_FW_REGION_RANGES];
    ULONG Size;
    ULONG Window0;
    ULONG Window1;
} APPLE_AGX_FW_REGION, *PAPPLE_AGX_FW_REGION;

typedef struct _APPLE_AGX_FW_PSTATE_TABLE
{
    ULONG Count;
    ULONG Columns;
    ULONG Hertz[APPLE_AGX_FW_PSTATES];
    ULONG Millivolts[APPLE_AGX_FW_PSTATES][APPLE_AGX_FW_PSTATE_COLUMNS];
    ULONG SramMillivolts[APPLE_AGX_FW_PSTATES][APPLE_AGX_FW_PSTATE_COLUMNS];
} APPLE_AGX_FW_PSTATE_TABLE, *PAPPLE_AGX_FW_PSTATE_TABLE;

typedef struct _APPLE_AGX_FW_CORE
{
    ULONG CoreId;
    ULONG Revision;
    ULONG Mgpus;
    ULONG Clusters;
    ULONG CoresPerCluster;
    ULONG TotalCores;
    ULONG Field24;
    ULONG Field28;
    ULONG Field2c;
    ULONG64 CoreMask;
    BOOLEAN SramDomain;
    BOOLEAN Domain1;
    BOOLEAN Domain2;
} APPLE_AGX_FW_CORE, *PAPPLE_AGX_FW_CORE;

#define APPLE_AGX_FW_IDLEOFF_PARAMS     10

typedef struct _APPLE_AGX_FW_CONFIG
{
    ULONG ChipId;
    ULONG64 GpuBase;
    ULONG64 FenderBase;
    ULONG BootFlags;
    ULONG64 Features;
    APPLE_AGX_FW_CORE Core;
    APPLE_AGX_FW_REGION Region[APPLE_AGX_FW_REGIONS];
    APPLE_AGX_FW_PSTATE_TABLE Base;
    APPLE_AGX_FW_PSTATE_TABLE TableA;
    APPLE_AGX_FW_PSTATE_TABLE TableB;
    APPLE_AGX_FW_PSTATE_TABLE Domain[2];
    ULONG MapA[APPLE_AGX_FW_PSTATES];
    ULONG MapB[APPLE_AGX_FW_PSTATES];
    ULONG Cap[3];
    ULONG SamplePeriodMs;
    ULONG SamplePeriodClocks;
    ULONG MaxPower;
    ULONG PowerFilter;
    ULONG PowerIntegralGain;
    LONG PowerIntegralMinClamp;
    ULONG PowerProportionalGain;
    ULONG PowerTarget[2];
    ULONG MinDutyCycle;
    ULONG BasePstate;
    ULONG PerfTargetUtilization;
    ULONG PerfFilter[2];
    ULONG PerfFilterDropThreshold;
    BOOLEAN PerfDualFilter;
    ULONG PerfIntegralGain[2];
    LONG PerfIntegralMinClamp;
    ULONG PerfIntegralScale;
    ULONG PerfProportionalGain[2];
    ULONG PerfSlot2340;
    ULONG PerfLimiterFilter;
    ULONG IdleOff[APPLE_AGX_FW_IDLEOFF_PARAMS];
    ULONG KeepAliveOffThreshold;
    ULONG KeepAlivePerfThreshold;
    ULONG McacheWrites;
    ULONG UscMaxTgmem;
    ULONG UatConfig;
    BOOLEAN UseIoUat;
    ULONG Field688;
    ULONG64 BrnMask;
    USHORT Field6c4[3];
    BOOLEAN NoiseSuppression;
    BOOLEAN PmpSwInterrupt;
    ULONG64 PbTableAddress;
    ULONG64 UmaTableAddress;
    ULONG64 GpuTtbatBase;
} APPLE_AGX_FW_CONFIG, *PAPPLE_AGX_FW_CONFIG;

typedef struct _APPLE_AGX_FW_BUFFER
{
    PVOID Virtual;
    ULONG64 Address;
} APPLE_AGX_FW_BUFFER, *PAPPLE_AGX_FW_BUFFER;

typedef struct _APPLE_AGX_FW_SCRATCH_RING
{
    PVOID Entries;
    ULONG64 EntriesAddress;
    volatile ULONG *Read;
    volatile ULONG *Completed;
    volatile ULONG *Write;
    ULONG64 ReadAddress;
    ULONG64 CompletedAddress;
    ULONG64 WriteAddress;
    ULONG Record;
    ULONG Producer;
} APPLE_AGX_FW_SCRATCH_RING, *PAPPLE_AGX_FW_SCRATCH_RING;

typedef struct _APPLE_AGX_FW_SCRATCH
{
    PVOID Window0;
    PVOID Window1;
    ULONG64 Window0Address;
    ULONG64 Window1Address;
    ULONG Window0Size;
    ULONG Window1Size;
    ULONG Window0Used;
    ULONG Window1Used;
} APPLE_AGX_FW_SCRATCH, *PAPPLE_AGX_FW_SCRATCH;

typedef struct _APPLE_AGX_FW
{
    APPLE_AGX_FW_BUFFER Root;
    APPLE_AGX_FW_BUFFER RegionA;
    APPLE_AGX_FW_BUFFER Group[APPLE_AGX_FW_GROUPS];
    APPLE_AGX_FW_BUFFER Object[APPLE_AGX_FW_OBJECTS];
    ULONG64 IoMapAddress[APPLE_AGX_FW_IOMAPS];
    ULONG64 TimestampAddress;
    APPLE_AGX_FW_SCRATCH Scratch;
    APPLE_AGX_FW_SCRATCH_RING DataMaster[APPLE_AGX_FW_DM_RINGS];
    APPLE_AGX_FW_SCRATCH_RING DeviceControl;
} APPLE_AGX_FW, *PAPPLE_AGX_FW;

ULONG
NTAPI
AppleAgxFwGroupOf(
    _In_ ULONG Object);

ULONG
NTAPI
AppleAgxFwObjectSize(
    _In_ ULONG Object);

ULONG
NTAPI
AppleAgxFwObjectOffset(
    _In_ ULONG Object);

ULONG
NTAPI
AppleAgxFwGroupSize(
    _In_ ULONG Group);

BOOLEAN
NTAPI
AppleAgxFwGroupCached(
    _In_ ULONG Group);

VOID
NTAPI
AppleAgxFwPlaceObjects(
    _Inout_ PAPPLE_AGX_FW Fw);

BOOLEAN
NTAPI
AppleAgxFwParseCore(
    _In_ ULONG Version,
    _In_ ULONG Counts1,
    _In_ ULONG Counts2,
    _In_ ULONG Counts3,
    _In_ ULONG64 CoreMask,
    _Out_ PAPPLE_AGX_FW_CORE Core);

BOOLEAN
NTAPI
AppleAgxFwRegions(
    _Inout_ PAPPLE_AGX_FW_CONFIG Config);

BOOLEAN
NTAPI
AppleAgxFwParsePstates(
    _In_ ULONG StateCount,
    _In_ ULONG Columns,
    _In_reads_bytes_(StatesBytes) const ULONG *States,
    _In_ ULONG StatesBytes,
    _In_reads_bytes_opt_(SramBytes) const ULONG *Sram,
    _In_ ULONG SramBytes,
    _Out_ PAPPLE_AGX_FW_PSTATE_TABLE Base);

BOOLEAN
NTAPI
AppleAgxFwParseDomain(
    _In_reads_bytes_(Bytes) const UCHAR *Data,
    _In_ ULONG Bytes,
    _In_ ULONG Rails,
    _In_ ULONG MaxStates,
    _Out_ PAPPLE_AGX_FW_PSTATE_TABLE Table);

VOID
NTAPI
AppleAgxFwMapRegisters(
    _In_reads_(16) const ULONG *Registers,
    _Out_writes_(APPLE_AGX_FW_PSTATES) PULONG MapA,
    _Out_writes_(APPLE_AGX_FW_PSTATES) PULONG MapB);

VOID
NTAPI
AppleAgxFwDeriveTables(
    _Inout_ PAPPLE_AGX_FW_CONFIG Config);

VOID
NTAPI
AppleAgxFwDefaults(
    _Out_ PAPPLE_AGX_FW_CONFIG Config);

BOOLEAN
NTAPI
AppleAgxFwScratchRing(
    _Inout_ PAPPLE_AGX_FW_SCRATCH Scratch,
    _In_opt_ const APPLE_AGX_FW_BUFFER *Entries,
    _In_ ULONG Record,
    _Out_ PAPPLE_AGX_FW_SCRATCH_RING Ring);

NTSTATUS
NTAPI
AppleAgxFwWrite(
    _Inout_ PAPPLE_AGX_FW Fw,
    _In_ const APPLE_AGX_FW_CONFIG *Config);

ULONG64
NTAPI
AppleAgxFwInitMessage(
    _In_ const APPLE_AGX_FW *Fw);

NTSTATUS
NTAPI
AppleAgxFwPost(
    _Inout_ PAPPLE_AGX_FW_SCRATCH_RING Ring,
    _In_reads_bytes_(Ring->Record) const VOID *Record,
    _Out_opt_ PULONG Slot);

BOOLEAN
NTAPI
AppleAgxFwTake(
    _In_ PVOID State,
    _In_ PVOID Records,
    _In_ ULONG Record,
    _Out_writes_bytes_(Record) PVOID Out);
