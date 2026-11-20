/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start data of the Apple G15 graphics firmware
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleagxp.h"

#define AGX_FW_HEAP_BASE            0xfffffc2c00000000ULL
#define AGX_FW_HEAP_END             0xfffffc2d00000000ULL
#define AGX_FW_IOMAP_BASE           0xfffffc2010000000ULL
#define AGX_FW_IOMAP_END            0xfffffc2011400000ULL
#define AGX_FW_TIMESTAMPS           0xfffffc2011800000ULL
#define AGX_FENDER_ID               0x4000
#define AGX_FENDER_TTBAT            0x802c
#define AGX_FENDER_CORE_MASK        0x101500
#define AGX_FENDER_PSTATE_MAP_A     0x141400
#define AGX_FENDER_PSTATE_MAP_B     0x141480
#define AGX_FEATURES_BASE           0x0000280000000107ULL
#define AGX_FEATURES_GROUPS         0x0000000000400000ULL
#define AGX_FEATURES_G15X           0x00020031a00709c0ULL
#define AGX_STATS_RECORD            0x40
#define AGX_TRACE_RECORD            0x70
#define AGX_LOG_DUMP                16

static
ULONG
AgxFenderRead(
    _In_ PAPPLE_AGX Agx,
    _In_ ULONG Offset)
{
    ASSERT(Agx->Fender != NULL);
    return READ_REGISTER_ULONG((volatile ULONG *)(Agx->Fender + Offset));
}

static
VOID
AgxProp(
    _In_ PAPPLE_AGX Agx,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Inout_ PULONG Value)
{
    ULONG Read;

    if (Node && AppleAdtReadU32(&Agx->Adt, Node, Name, &Read))
        *Value = Read;
}

static
BOOLEAN
AgxHasProp(
    _In_ PAPPLE_AGX Agx,
    _In_ ULONG Node,
    _In_z_ PCSTR Name)
{
    return Node && AppleAdtGetProperty(&Agx->Adt, Node, Name, NULL) != NULL;
}

static
VOID
AgxReadDomain(
    _In_ PAPPLE_AGX Agx,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _In_ BOOLEAN Required,
    _In_ ULONG MaxStates,
    _Out_ PAPPLE_AGX_FW_PSTATE_TABLE Table)
{
    const UCHAR *Data;
    ULONG Length = 0;

    RtlZeroMemory(Table, sizeof(*Table));
    if (!Required)
        return;
    Data = Node ? AppleAdtGetProperty(&Agx->Adt, Node, Name, &Length) : NULL;
    if (!Data)
    {
        DbgPrint("appleagx: %s is not in the device tree, its domain table stays empty\n", Name);
        return;
    }
    if (!AppleAgxFwParseDomain(Data, Length, Agx->FwConfig.Core.Mgpus, MaxStates, Table))
        DbgPrint("appleagx: %s (%lu bytes) does not describe %lu rails\n", Name, Length, Agx->FwConfig.Core.Mgpus);
}

NTSTATUS
AgxReadFirmwareConfig(
    _Inout_ PAPPLE_AGX Agx)
{
    PAPPLE_AGX_FW_CONFIG Config = &Agx->FwConfig;
    ULONG Node = 0, States = 0, Columns = 1, Length, SramLength = 0, Value, Index, Registers[16];
    const ULONG *Perf, *Sram;
    ULONG64 Mask;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Agx->Fender || !Agx->GpuBase || !Agx->FenderBase)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    AppleAgxFwDefaults(Config);
    Config->ChipId = Agx->ChipId;
    Config->GpuBase = Agx->GpuBase;
    Config->FenderBase = Agx->FenderBase;
    Mask = (ULONG64)AgxFenderRead(Agx, AGX_FENDER_CORE_MASK) |
           ((ULONG64)AgxFenderRead(Agx, AGX_FENDER_CORE_MASK + 4) << 32);
    if (!AppleAgxFwParseCore(AgxFenderRead(Agx, AGX_FENDER_ID), AgxFenderRead(Agx, AGX_FENDER_ID + 0x10),
                             AgxFenderRead(Agx, AGX_FENDER_ID + 0x14), AgxFenderRead(Agx, AGX_FENDER_ID + 0x18), Mask,
                             &Config->Core))
    {
        DbgPrint("appleagx: core %#lx revision %lu is not a G15X part\n", Config->Core.CoreId, Config->Core.Revision);
        return STATUS_NOT_SUPPORTED;
    }
    Config->Features = AGX_FEATURES_BASE | AGX_FEATURES_G15X;
    if (Config->Core.Clusters > 1)
        Config->Features |= AGX_FEATURES_GROUPS;
    if (Agx->AdtCopy)
        Node = AppleAdtPath(&Agx->Adt, "/arm-io/sgx");
    if (!Node)
    {
        DbgPrint("appleagx: the device tree has no /arm-io/sgx\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    AgxProp(Agx, Node, "gpu-num-perf-states", &States);
    AgxProp(Agx, Node, "perf-state-table-count", &Columns);
    Perf = AppleAdtGetProperty(&Agx->Adt, Node, "perf-states", &Length);
    Sram = AppleAdtGetProperty(&Agx->Adt, Node, "perf-states-sram", &SramLength);
    if (!Perf || !AppleAgxFwParsePstates(States, Columns, Perf, Length, Sram, SramLength, &Config->Base))
    {
        DbgPrint("appleagx: %lu performance states in %lu columns are not usable\n", States, Columns);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (Index = 0; Index < 8; ++Index)
    {
        Registers[Index] = AgxFenderRead(Agx, AGX_FENDER_PSTATE_MAP_A + Index * 4);
        Registers[8 + Index] = AgxFenderRead(Agx, AGX_FENDER_PSTATE_MAP_B + Index * 4);
    }
    AppleAgxFwMapRegisters(Registers, Config->MapA, Config->MapB);
    AgxReadDomain(Agx, Node, "cs-perf-states", Config->Core.Domain1, APPLE_AGX_FW_PSTATES, &Config->Domain[0]);
    AgxReadDomain(Agx, Node, "afr-perf-states", Config->Core.Domain2, 8, &Config->Domain[1]);
    AppleAgxFwDeriveTables(Config);

    Value = 0;
    AgxProp(Agx, Node, "gpu-pwr-sample-period-aic-clks", &Value);
    if (Value)
    {
        Config->SamplePeriodClocks = Value;
        Config->SamplePeriodMs = Value / 24000;
    }
    else
    {
        AgxProp(Agx, Node, "gpu-power-sample-period", &Config->SamplePeriodMs);
        Config->SamplePeriodClocks = Config->SamplePeriodMs * 24000;
    }
    AgxProp(Agx, Node, "gpu-pwr-min-duty-cycle", &Config->MinDutyCycle);
    if (AgxHasProp(Agx, Node, "gpu-device-max-power"))
        AgxProp(Agx, Node, "gpu-device-max-power", &Config->MaxPower);
    else
        AgxProp(Agx, Node, "gpu-max-power", &Config->MaxPower);
    AgxProp(Agx, Node, "gpu-pwr-filter-time-constant", &Config->PowerFilter);
    AgxProp(Agx, Node, "gpu-pwr-integral-gain", &Config->PowerIntegralGain);
    AgxProp(Agx, Node, "gpu-pwr-integral-min-clamp", (PULONG)&Config->PowerIntegralMinClamp);
    AgxProp(Agx, Node, "gpu-pwr-proportional-gain", &Config->PowerProportionalGain);
    AgxProp(Agx, Node, "gpu-perf-base-pstate", &Config->BasePstate);
    if (!Config->BasePstate || Config->BasePstate > Config->Cap[0])
    {
        DbgPrint("appleagx: base performance state %lu is outside 1..%lu\n", Config->BasePstate, Config->Cap[0]);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    AgxProp(Agx, Node, "gpu-perf-tgt-utilization", &Config->PerfTargetUtilization);
    AgxProp(Agx, Node, "gpu-perf-filter-time-constant", &Config->PerfFilter[0]);
    AgxProp(Agx, Node, "gpu-perf-filter-time-constant2", &Config->PerfFilter[1]);
    AgxProp(Agx, Node, "gpu-perf-filter-drop-threshold", &Config->PerfFilterDropThreshold);
    AgxProp(Agx, Node, "gpu-perf-integral-gain", &Config->PerfIntegralGain[0]);
    AgxProp(Agx, Node, "gpu-perf-integral-gain2", &Config->PerfIntegralGain[1]);
    AgxProp(Agx, Node, "gpu-perf-integral-min-clamp", (PULONG)&Config->PerfIntegralMinClamp);
    AgxProp(Agx, Node, "gpu-perf-proportional-gain", &Config->PerfProportionalGain[0]);
    AgxProp(Agx, Node, "gpu-perf-proportional-gain2", &Config->PerfProportionalGain[1]);
    AgxProp(Agx, Node, "gpu-perf-limiter-filter-time-constant", &Config->PerfLimiterFilter);
    AgxProp(Agx, Node, "gpu-idleoff-standby-timer", &Config->IdleOff[0]);
    AgxProp(Agx, Node, "gpu-idleoff-prob-init-val", &Config->IdleOff[1]);
    AgxProp(Agx, Node, "gpu-idleoff-fn-hit", &Config->IdleOff[2]);
    AgxProp(Agx, Node, "gpu-idleoff-fi-hit", &Config->IdleOff[3]);
    AgxProp(Agx, Node, "gpu-idleoff-fn-miss", &Config->IdleOff[4]);
    AgxProp(Agx, Node, "gpu-idleoff-fi-miss", &Config->IdleOff[5]);
    AgxProp(Agx, Node, "gpu-idleoff-nei-hit", &Config->IdleOff[6]);
    AgxProp(Agx, Node, "gpu-idleoff-min_conf", &Config->IdleOff[7]);
    AgxProp(Agx, Node, "gpu-idleoff-high-conf", &Config->IdleOff[8]);
    AgxProp(Agx, Node, "gpu-idleoff-reset-iter", &Config->IdleOff[9]);
    AgxProp(Agx, Node, "gpu-keepalive-perf-mode-threshold", &Config->KeepAlivePerfThreshold);
    AgxProp(Agx, Node, "gpu-keepalive-off-mode-threshold", &Config->KeepAliveOffThreshold);
    Value = 0;
    AgxProp(Agx, Node, "pmp-sw-interrupt", &Value);
    Config->PmpSwInterrupt = Value != 0;
    Value = 0;
    AgxProp(Agx, Node, "gpu-noise-suppression", &Value);
    Config->NoiseSuppression = Value != 0;
    if (AgxHasProp(Agx, Node, "gptbat-ready"))
        Config->GpuTtbatBase = (ULONG64)AgxFenderRead(Agx, AGX_FENDER_TTBAT) << 14;
    if (!AppleAgxFwRegions(Config))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DbgPrint("appleagx: core %#lx rev %lu, %lu mgpu %lu clusters x %lu cores, %lu+%lu performance states up to %lu MHz, "
             "sample %lu ms, TTBAT %I64x\n",
             Config->Core.CoreId, Config->Core.Revision, Config->Core.Mgpus, Config->Core.Clusters,
             Config->Core.CoresPerCluster, Config->TableA.Count, Config->TableB.Count,
             Config->TableA.Hertz[Config->Cap[0]] / 1000000, Config->SamplePeriodMs, Config->GpuTtbatBase);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxFwAllocate(
    _Inout_ PAPPLE_AGX Agx,
    _In_ SIZE_T Size,
    _In_ UCHAR Class,
    _Out_ PAPPLE_AGX_OBJECT Object,
    _Out_opt_ PAPPLE_AGX_FW_BUFFER Buffer)
{
    APPLE_AGX_REQUEST Request;
    NTSTATUS Status;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Size = Size;
    Request.Class = Class;
    Request.Cpu = TRUE;
    Status = AppleAgxObjectCreate(&Agx->FwHeap, &Request, Object);
    if (NT_SUCCESS(Status) && Buffer)
    {
        Buffer->Virtual = Object->Virtual;
        Buffer->Address = Object->Address;
    }
    return Status;
}

static
NTSTATUS
AgxFwMapIo(
    _Inout_ PAPPLE_AGX Agx,
    _In_ ULONG64 Address,
    _In_ ULONG64 Physical,
    _In_ ULONG Size,
    _In_ BOOLEAN Writable)
{
    APPLE_UAT_PROTECTION Protection;

    ASSERT(((Address | Physical | Size) & (APPLE_UAT_PAGE_SIZE - 1)) == 0);
    Protection.Access = APPLE_UAT_ACCESS_FIRMWARE;
    Protection.Memory = APPLE_UAT_MEMORY_DEVICE;
    Protection.Read = FALSE;
    Protection.Write = Writable;
    return AppleUatAdoptedMap(&Agx->Adopted, Address, Physical, Size, &Protection);
}

static
NTSTATUS
AgxFwMapRegions(
    _Inout_ PAPPLE_AGX Agx)
{
    PAPPLE_AGX_FW_CONFIG Config = &Agx->FwConfig;
    ULONG64 Address = AGX_FW_IOMAP_BASE;
    ULONG Index, Range;
    NTSTATUS Status;

    for (Index = 0; Index < APPLE_AGX_FW_IOMAPS; ++Index)
    {
        const APPLE_AGX_FW_REGION *Region = &Config->Region[Index];
        ULONG64 Length = (ULONG64)Region->Count * Region->Size;

        if (!Region->Count || !Region->Size || !Region->Physical[0])
            continue;
        if (Address + Length + APPLE_UAT_PAGE_SIZE > AGX_FW_IOMAP_END)
            return STATUS_NO_MEMORY;
        for (Range = 0; Range < Region->Count; ++Range)
        {
            Status = AgxFwMapIo(Agx, Address + (ULONG64)Range * Region->Size, Region->Physical[Range], Region->Size,
                                Region->Writable);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        Agx->Fw.IoMapAddress[Index] = Address;
        Address += Length + APPLE_UAT_PAGE_SIZE;
    }
    Agx->FwIoMapEnd = Address;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxFwMapScratch(
    _Inout_ PAPPLE_AGX Agx)
{
    const APPLE_AGX_FW_REGION *Region = &Agx->FwConfig.Region[APPLE_AGX_FW_REGION_SCRATCH];
    PAPPLE_AGX_FW_SCRATCH Scratch = &Agx->Fw.Scratch;
    APPLE_AGX_REQUEST Request;
    PHYSICAL_ADDRESS Physical;
    ULONG64 Page;
    SIZE_T Bytes;
    NTSTATUS Status;

    if (!Region->Size || Region->Window0 + Region->Window1 != Region->Size)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    RtlZeroMemory(&Request, sizeof(Request));
    Request.Size = Region->Size;
    Status = AppleAgxObjectPlace(&Agx->FwHeap, &Request, &Page, &Bytes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AgxFwMapIo(Agx, Page, Region->Physical[0], Region->Size, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;
    Physical.QuadPart = (LONGLONG)Region->Physical[0];
    Agx->FwScratch = MmMapIoSpace(Physical, Region->Size, MmNonCached);
    if (!Agx->FwScratch)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Scratch, sizeof(*Scratch));
    Scratch->Window0 = Agx->FwScratch;
    Scratch->Window1 = (PUCHAR)Agx->FwScratch + Region->Window0;
    Scratch->Window0Address = Page;
    Scratch->Window1Address = Page + Region->Window0;
    Scratch->Window0Size = Region->Window0;
    Scratch->Window1Size = Region->Window1;
    return STATUS_SUCCESS;
}

NTSTATUS
AgxBuildFirmwareData(
    _Inout_ PAPPLE_AGX Agx)
{
    PAPPLE_AGX_FW Fw = &Agx->Fw;
    ULONG Group, Index;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Agx->Owned);
    RtlZeroMemory(Fw, sizeof(*Fw));
    RtlZeroMemory(&Agx->FwHeap, sizeof(Agx->FwHeap));
    Agx->FwHeap.Adopted = &Agx->Adopted;
    Agx->FwHeap.Next = AGX_FW_HEAP_BASE;
    Agx->FwHeap.End = AGX_FW_HEAP_END;
    Agx->FwHeap.Owner = "appleagx";
    Agx->Initialised = TRUE;
    for (Group = 0; Group < APPLE_AGX_FW_GROUPS; ++Group)
    {
        Status = AgxFwAllocate(Agx, AppleAgxFwGroupSize(Group),
                               AppleAgxFwGroupCached(Group) ? APPLE_AGX_CLASS_FIRMWARE_CACHED : APPLE_AGX_CLASS_FIRMWARE,
                               &Agx->FwGroup[Group], &Fw->Group[Group]);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Status = AgxFwAllocate(Agx, APPLE_AGX_FW_PAGE, APPLE_AGX_CLASS_FIRMWARE, &Agx->FwRoot, &Fw->Root);
    if (NT_SUCCESS(Status))
        Status = AgxFwAllocate(Agx, APPLE_AGX_FW_PAGE, APPLE_AGX_CLASS_FIRMWARE, &Agx->FwRegionA, &Fw->RegionA);
    if (NT_SUCCESS(Status))
        Status = AgxFwAllocate(Agx, APPLE_AGX_FW_PAGE, APPLE_AGX_CLASS_SHARED, &Agx->FwPbTable, NULL);
    if (NT_SUCCESS(Status))
        Status = AgxFwAllocate(Agx, APPLE_AGX_FW_PAGE, APPLE_AGX_CLASS_SHARED, &Agx->FwUmaTable, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Agx->FwConfig.PbTableAddress = Agx->FwPbTable.Address;
    Agx->FwConfig.UmaTableAddress = Agx->FwUmaTable.Address;
    AppleAgxFwPlaceObjects(Fw);
    Status = AgxFwMapRegions(Agx);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the firmware register windows were not mapped: %lx\n", Status);
        return Status;
    }
    Status = AgxFwMapScratch(Agx);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the scratch memory was not mapped: %lx\n", Status);
        return Status;
    }
    for (Index = 0; Index < APPLE_AGX_FW_DM_RINGS; ++Index)
    {
        if (!AppleAgxFwScratchRing(&Fw->Scratch, NULL, APPLE_AGX_FW_DM_RECORD, &Fw->DataMaster[Index]))
            return STATUS_NO_MEMORY;
        *Fw->DataMaster[Index].Write = 0;
    }
    if (!AppleAgxFwScratchRing(&Fw->Scratch, &Fw->Object[APPLE_AGX_FW_DEVCTL_RING], APPLE_AGX_FW_DEVCTL_RECORD,
                               &Fw->DeviceControl))
    {
        return STATUS_NO_MEMORY;
    }
    *Fw->DeviceControl.Write = 0;
    Fw->TimestampAddress = AGX_FW_TIMESTAMPS;
    AppleUatInvalidateAll();
    Status = AppleAgxFwWrite(Fw, &Agx->FwConfig);
    if (!NT_SUCCESS(Status))
        return Status;
    Agx->Bound = TRUE;
    DbgPrint("appleagx: firmware data built: root %I64x, groups %I64x %I64x %I64x %I64x, scratch %I64x, "
             "%lu windows up to %I64x, scratch used %lu+%lu\n",
             Fw->Root.Address, Fw->Group[0].Address, Fw->Group[1].Address, Fw->Group[2].Address, Fw->Group[3].Address,
             Fw->Scratch.Window0Address, APPLE_AGX_FW_IOMAPS, Agx->FwIoMapEnd, Fw->Scratch.Window0Used,
             Fw->Scratch.Window1Used);
    return STATUS_SUCCESS;
}

VOID
AgxFreeFirmwareData(
    _Inout_ PAPPLE_AGX Agx)
{
    ULONG Group;

    Agx->Bound = FALSE;
    if (Agx->FwScratch)
        MmUnmapIoSpace(Agx->FwScratch, Agx->FwConfig.Region[APPLE_AGX_FW_REGION_SCRATCH].Size);
    Agx->FwScratch = NULL;
    for (Group = 0; Group < APPLE_AGX_FW_GROUPS; ++Group)
        AppleAgxObjectFree(&Agx->FwGroup[Group]);
    AppleAgxObjectFree(&Agx->FwRoot);
    AppleAgxObjectFree(&Agx->FwRegionA);
    AppleAgxObjectFree(&Agx->FwPbTable);
    AppleAgxObjectFree(&Agx->FwUmaTable);
    RtlZeroMemory(&Agx->Fw, sizeof(Agx->Fw));
    Agx->Initialised = FALSE;
}

static
ULONG
AgxDrainRing(
    _In_ PVOID State,
    _In_ PVOID Records,
    _In_ ULONG Record,
    _In_z_ PCSTR Name,
    _In_ ULONG Dump)
{
    UCHAR Entry[AGX_TRACE_RECORD];
    ULONG Taken = 0;

    ASSERT(Record <= sizeof(Entry));
    while (Taken < APPLE_AGX_FW_RING_SLOTS && AppleAgxFwTake(State, Records, Record, Entry))
    {
        if (Taken < Dump)
        {
            PULONG Words = (PULONG)Entry;

            DbgPrint("appleagx: %s %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", Name, Words[0], Words[1],
                     Words[2], Words[3], Words[4], Words[5], Words[6], Words[7]);
        }
        ++Taken;
    }
    return Taken;
}

VOID
AgxDrainFirmware(
    _Inout_ PAPPLE_AGX Agx)
{
    PAPPLE_AGX_FW Fw = &Agx->Fw;
    UCHAR Record[APPLE_AGX_FW_EVENT_RECORD];
    ULONG Taken = 0, Index;

    if (!Agx->Bound)
        return;
    while (Taken < APPLE_AGX_FW_RING_SLOTS &&
           AppleAgxFwTake(Fw->Object[APPLE_AGX_FW_EVENT_STATE].Virtual, Fw->Object[APPLE_AGX_FW_EVENT_RING].Virtual,
                          APPLE_AGX_FW_EVENT_RECORD, Record))
    {
        ULONG Words[APPLE_AGX_RECORD_WORDS];

        RtlCopyMemory(Words, Record, sizeof(Words));
        if (Agx->FwEvents < AGX_LOG_DUMP)
        {
            DbgPrint("appleagx: event %08lx %08lx %08lx %08lx %08lx %08lx\n", Words[0], Words[1], Words[2],
                     Words[3], Words[4], Words[5]);
        }
        AgxEngineEvent(Agx, Words);
        ++Agx->FwEvents;
        ++Taken;
    }
    for (Index = 0; Index < APPLE_AGX_FW_LOG_RINGS; ++Index)
    {
        Agx->FwLogs += AgxDrainRing((PUCHAR)Fw->Object[APPLE_AGX_FW_LOG_STATE].Virtual + Index * APPLE_AGX_FW_STATE_SIZE,
                                    (PUCHAR)Fw->Object[APPLE_AGX_FW_LOG_RING].Virtual +
                                        Index * APPLE_AGX_FW_RING_SLOTS * APPLE_AGX_FW_EVENT_RECORD,
                                    APPLE_AGX_FW_EVENT_RECORD, "log", Agx->FwLogs < AGX_LOG_DUMP ? AGX_LOG_DUMP : 0);
    }
    Taken += AgxDrainRing(Fw->Object[APPLE_AGX_FW_TRACE_STATE].Virtual, Fw->Object[APPLE_AGX_FW_TRACE_RING].Virtual,
                          AGX_TRACE_RECORD, "trace", 0);
    Taken += AgxDrainRing(Fw->Object[APPLE_AGX_FW_STATS_STATE].Virtual, Fw->Object[APPLE_AGX_FW_STATS_RING].Virtual,
                          AGX_STATS_RECORD, "statistic", 0);
    Agx->Drained += Taken;
}
