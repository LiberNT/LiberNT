/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Data the Apple G15 graphics firmware is started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <libernt/drivers/appleagxfw.h>

typedef struct _AGX_FW_SPEC
{
    UCHAR Group;
    ULONG Size;
} AGX_FW_SPEC;

static const AGX_FW_SPEC AgxFwSpec[APPLE_AGX_FW_OBJECTS] =
{
    { APPLE_AGX_FW_GROUP_A, APPLE_AGX_FW_HWCFG_SIZE },
    { APPLE_AGX_FW_GROUP_A, 0xc10 },
    { APPLE_AGX_FW_GROUP_A, 0x1248 },
    { APPLE_AGX_FW_GROUP_A, 0xe10 },
    { APPLE_AGX_FW_GROUP_A, APPLE_AGX_FW_POWER_SIZE },
    { APPLE_AGX_FW_GROUP_A, 0x60 },
    { APPLE_AGX_FW_GROUP_A, 0 },
    { APPLE_AGX_FW_GROUP_A, 0x88 },
    { APPLE_AGX_FW_GROUP_A, APPLE_AGX_FW_RUNTIME_SIZE },
    { APPLE_AGX_FW_GROUP_A, APPLE_AGX_FW_RING_SLOTS * APPLE_AGX_FW_DEVCTL_RECORD },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_CONTROL_SIZE },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_STATUS_SIZE },
    { APPLE_AGX_FW_GROUP_B, 0x20 },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_STATE_SIZE },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_LOG_RINGS * APPLE_AGX_FW_STATE_SIZE },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_STATE_SIZE },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_STATE_SIZE },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_RING_SLOTS * APPLE_AGX_FW_EVENT_RECORD },
    { APPLE_AGX_FW_GROUP_B, APPLE_AGX_FW_LOG_RINGS * APPLE_AGX_FW_RING_SLOTS * APPLE_AGX_FW_EVENT_RECORD },
    { APPLE_AGX_FW_GROUP_B, 0x51000 },
    { APPLE_AGX_FW_GROUP_B, 0x7000 },
    { APPLE_AGX_FW_GROUP_B, 0x4000 },
    { APPLE_AGX_FW_GROUP_C, APPLE_AGX_FW_GLOBALS_SIZE },
    { APPLE_AGX_FW_GROUP_CTL, 0x20 },
    { APPLE_AGX_FW_GROUP_CTL, APPLE_AGX_FW_RING_SLOTS * APPLE_AGX_FW_DM_RECORD },
};

static const UCHAR AgxFwUatLevelShift[3] = { 36, 25, 14 };
static const USHORT AgxFwUatLevelEntries[3] = { 64, 2048, 2048 };
static const ULONG AgxFwRevisionCap[3] = { 7, 5, 5 };

#define AGX_FW_UAT_OUTPUT_MASK      0x3ffffffc000ULL
#define AGX_FW_TIMESTAMP_BASE       0xfffffc2011800000ULL
#define AGX_FW_AIC_TIMER            0x20e101000ULL
#define AGX_FW_HZ_PER_MHZ           1000000

static
ULONG
AgxFwAlign(
    _In_ ULONG Value,
    _In_ ULONG Alignment)
{
    ASSERT((Alignment & (Alignment - 1)) == 0);
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

static
VOID
AgxFwPut8(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ UCHAR Value)
{
    ((PUCHAR)Base)[Offset] = Value;
}

static
VOID
AgxFwPut16(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ USHORT Value)
{
    RtlCopyMemory((PUCHAR)Base + Offset, &Value, sizeof(Value));
}

static
VOID
AgxFwPut32(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    RtlCopyMemory((PUCHAR)Base + Offset, &Value, sizeof(Value));
}

static
VOID
AgxFwPutFloat(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ float Value)
{
    RtlCopyMemory((PUCHAR)Base + Offset, &Value, sizeof(Value));
}

static
VOID
AgxFwPut64(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG64 Value)
{
    RtlCopyMemory((PUCHAR)Base + Offset, &Value, sizeof(Value));
}

static
ULONG
AgxFwGet32(
    _In_ const VOID *Base,
    _In_ ULONG Offset)
{
    ULONG Value;

    RtlCopyMemory(&Value, (const UCHAR *)Base + Offset, sizeof(Value));
    return Value;
}

ULONG
NTAPI
AppleAgxFwGroupOf(
    _In_ ULONG Object)
{
    ASSERT(Object < APPLE_AGX_FW_OBJECTS);
    return AgxFwSpec[Object].Group;
}

ULONG
NTAPI
AppleAgxFwObjectSize(
    _In_ ULONG Object)
{
    ASSERT(Object < APPLE_AGX_FW_OBJECTS);
    return AgxFwSpec[Object].Size;
}

ULONG
NTAPI
AppleAgxFwObjectOffset(
    _In_ ULONG Object)
{
    ULONG Index, Offset = 0;

    ASSERT(Object < APPLE_AGX_FW_OBJECTS);
    for (Index = 0; Index < Object; ++Index)
    {
        if (AgxFwSpec[Index].Group == AgxFwSpec[Object].Group)
            Offset = AgxFwAlign(Offset + AgxFwSpec[Index].Size, APPLE_AGX_FW_ALIGN);
    }
    return Offset;
}

ULONG
NTAPI
AppleAgxFwGroupSize(
    _In_ ULONG Group)
{
    ULONG Index, End = 0;

    ASSERT(Group < APPLE_AGX_FW_GROUPS);
    for (Index = 0; Index < APPLE_AGX_FW_OBJECTS; ++Index)
    {
        if (AgxFwSpec[Index].Group == Group)
            End = AppleAgxFwObjectOffset(Index) + AgxFwSpec[Index].Size;
    }
    return AgxFwAlign(End, APPLE_AGX_FW_PAGE);
}

BOOLEAN
NTAPI
AppleAgxFwGroupCached(
    _In_ ULONG Group)
{
    ASSERT(Group < APPLE_AGX_FW_GROUPS);
    return Group == APPLE_AGX_FW_GROUP_A;
}

VOID
NTAPI
AppleAgxFwPlaceObjects(
    _Inout_ PAPPLE_AGX_FW Fw)
{
    ULONG Index, Group, Offset;

    for (Index = 0; Index < APPLE_AGX_FW_OBJECTS; ++Index)
    {
        Group = AgxFwSpec[Index].Group;
        Offset = AppleAgxFwObjectOffset(Index);
        ASSERT(Fw->Group[Group].Virtual != NULL);
        ASSERT((Fw->Group[Group].Address & (APPLE_AGX_FW_PAGE - 1)) == 0);
        Fw->Object[Index].Virtual = (PUCHAR)Fw->Group[Group].Virtual + Offset;
        Fw->Object[Index].Address = Fw->Group[Group].Address + Offset;
    }
}

BOOLEAN
NTAPI
AppleAgxFwParseCore(
    _In_ ULONG Version,
    _In_ ULONG Counts1,
    _In_ ULONG Counts2,
    _In_ ULONG Counts3,
    _In_ ULONG64 CoreMask,
    _Out_ PAPPLE_AGX_FW_CORE Core)
{
    ULONG Major = (Version >> 12) & 0xf, Minor = (Version >> 8) & 0xf;

    UNREFERENCED_PARAMETER(Counts2);
    RtlZeroMemory(Core, sizeof(*Core));
    Core->Mgpus = (Counts1 >> 16) & 0xf;
    Core->CoresPerCluster = Counts1 & 0xff;
    if ((Version >> 24) == 7)
    {
        switch ((Version >> 16) & 0xff)
        {
        case 0:
            Core->CoreId = 0x15;
            break;
        case 2:
            Core->CoreId = 0x16;
            break;
        case 3:
            Core->CoreId = APPLE_AGX_FW_CORE_G15X_MAX;
            Core->SramDomain = TRUE;
            Core->CoresPerCluster = 10;
            break;
        case 4:
            if (Core->Mgpus == 1)
                Core->CoreId = APPLE_AGX_FW_CORE_G15X;
            Core->SramDomain = TRUE;
            break;
        default:
            break;
        }
    }
    if (Major == 0 && Minor == 0)
        Core->Revision = 1;
    else if (Major == 1 && Minor == 0)
        Core->Revision = 3;
    else if (Major == 1 && Minor == 1)
        Core->Revision = 4;
    else if (Major == 2 && Minor == 0)
        Core->Revision = 5;
    Core->Clusters = ((Counts1 >> 8) & 0xff) * Core->Mgpus;
    Core->TotalCores = Core->CoresPerCluster * Core->Clusters;
    Core->Field24 = (Counts3 & 0xf) * ((Counts3 >> 4) & 0xf);
    Core->Field28 = ((Counts3 >> 16) & 0xf) * ((Counts3 >> 20) & 0xf);
    Core->Field2c = ((Counts3 >> 24) & 0xf) * ((Counts3 >> 8) & 0xf);
    Core->CoreMask = CoreMask;
    if (Core->CoreId == APPLE_AGX_FW_CORE_G15X)
    {
        Core->Domain1 = TRUE;
        Core->Domain2 = TRUE;
    }
    return Core->CoreId == APPLE_AGX_FW_CORE_G15X || Core->CoreId == APPLE_AGX_FW_CORE_G15X_MAX;
}

static
VOID
AgxFwRegion(
    _Inout_ PAPPLE_AGX_FW_CONFIG Config,
    _In_ ULONG Index,
    _In_ ULONG64 Physical,
    _In_ ULONG Size,
    _In_ BOOLEAN Writable)
{
    PAPPLE_AGX_FW_REGION Region = &Config->Region[Index];

    ASSERT(Index < APPLE_AGX_FW_REGIONS);
    ASSERT((Physical & (APPLE_AGX_FW_PAGE - 1)) == 0);
    Region->Writable = Writable;
    Region->Count = 1;
    Region->Physical[0] = Physical;
    Region->Size = Size;
}

BOOLEAN
NTAPI
AppleAgxFwRegions(
    _Inout_ PAPPLE_AGX_FW_CONFIG Config)
{
    static const UCHAR UnusedWritable[] = { 4, 6, 8, 10, 13, 14, 15, 16, 17 };
    ULONG Index, McacheCount;
    ULONG64 Gpu = Config->GpuBase, Fender = Config->FenderBase;
    BOOLEAN Pro = Config->Core.CoreId == APPLE_AGX_FW_CORE_G15X;

    RtlZeroMemory(Config->Region, sizeof(Config->Region));
    if (!Pro && Config->Core.CoreId != APPLE_AGX_FW_CORE_G15X_MAX)
        return FALSE;
    if (!Gpu || !Fender)
        return FALSE;
    for (Index = 0; Index < RTL_NUMBER_OF(UnusedWritable); ++Index)
        Config->Region[UnusedWritable[Index]].Writable = TRUE;
    AgxFwRegion(Config, 0, Fender, 0x144000, TRUE);
    AgxFwRegion(Config, 1, AGX_FW_AIC_TIMER & ~(ULONG64)(APPLE_AGX_FW_PAGE - 1), APPLE_AGX_FW_PAGE, FALSE);
    AgxFwRegion(Config, 2, 0x351014000ULL, 0x4000, TRUE);
    AgxFwRegion(Config, 3, Gpu, 0x20000, TRUE);
    AgxFwRegion(Config, 7, Pro ? 0x290444000ULL : 0x3502bc000ULL, APPLE_AGX_FW_PAGE, FALSE);
    AgxFwRegion(Config, 9, Gpu + 0xe08000, 0x8000, TRUE);
    McacheCount = Pro ? (Config->ChipId == 0x6034 ? 6 : 8) : 2;
    ASSERT(McacheCount <= APPLE_AGX_FW_REGION_RANGES);
    Config->Region[11].Writable = TRUE;
    Config->Region[11].Count = McacheCount;
    Config->Region[11].Size = 0x98000;
    for (Index = 0; Index < McacheCount; ++Index)
    {
        Config->Region[11].Physical[Index] = (Index < 8 ? 0x220000000ULL : 0x2220000000ULL) |
                                             (((ULONG64)Index * 0x2000000) & 0xe000000);
    }
    AgxFwRegion(Config, 12, 0x35104c000ULL, 0x4000, FALSE);
    AgxFwRegion(Config, 18, 0x3503d0000ULL, 0x4000, TRUE);
    AgxFwRegion(Config, 19, 0x3503c0000ULL, 0x4000, FALSE);
    AgxFwRegion(Config, 20, 0x3503d8000ULL, 0x4000, TRUE);
    if (Pro)
    {
        AgxFwRegion(Config, 21, Gpu + 0xeb4000, 0x4000, TRUE);
        AgxFwRegion(Config, 24, Gpu + 0x1000000, 0x8000, TRUE);
        AgxFwRegion(Config, 28, 0x2a4dc0000ULL, 0x24000, FALSE);
    }
    else
    {
        AgxFwRegion(Config, 23, Gpu + 0x3000000, 0x400000, TRUE);
    }
    AgxFwRegion(Config, 25, Pro ? 0x3c945c000ULL : 0x30945c000ULL, APPLE_AGX_FW_PAGE, TRUE);
    AgxFwRegion(Config, 26, 0x350280000ULL, 0x8000, FALSE);
    AgxFwRegion(Config, 29, Gpu + 0xe5c000, 0x4000, FALSE);
    AgxFwRegion(Config, APPLE_AGX_FW_REGION_SCRATCH, Fender + 0x60000, 0x20000, TRUE);
    Config->Region[APPLE_AGX_FW_REGION_SCRATCH].Window0 = 0x14000;
    Config->Region[APPLE_AGX_FW_REGION_SCRATCH].Window1 = 0x20000 - 0x14000;
    return TRUE;
}

static
VOID
AgxFwFillColumns(
    _Inout_ ULONG Row[APPLE_AGX_FW_PSTATE_COLUMNS])
{
    ULONG Column;

    for (Column = 1; Column < APPLE_AGX_FW_PSTATE_COLUMNS; ++Column)
    {
        if (!Row[Column])
            Row[Column] = Row[0];
    }
}

BOOLEAN
NTAPI
AppleAgxFwParsePstates(
    _In_ ULONG StateCount,
    _In_ ULONG Columns,
    _In_reads_bytes_(StatesBytes) const ULONG *States,
    _In_ ULONG StatesBytes,
    _In_reads_bytes_opt_(SramBytes) const ULONG *Sram,
    _In_ ULONG SramBytes,
    _Out_ PAPPLE_AGX_FW_PSTATE_TABLE Base)
{
    ULONG Count = StateCount + 1, Column, State;

    RtlZeroMemory(Base, sizeof(*Base));
    if (StateCount < 1 || StateCount > 15 || Columns < 1 || Columns > APPLE_AGX_FW_PSTATE_COLUMNS)
        return FALSE;
    if (StatesBytes / (8 * Columns) < Count)
        return FALSE;
    Base->Count = Count;
    Base->Columns = Columns;
    for (Column = 0; Column < Columns; ++Column)
    {
        for (State = 0; State < Count; ++State)
        {
            Base->Hertz[State] = States[2 * (Column * Count + State)];
            Base->Millivolts[State][Column] = States[2 * (Column * Count + State) + 1];
        }
    }
    for (State = 0; State < Count; ++State)
        AgxFwFillColumns(Base->Millivolts[State]);
    if (!Sram || SramBytes / (8 * Columns) < Count)
        return TRUE;
    for (Column = 0; Column < Columns; ++Column)
    {
        for (State = 0; State < Count; ++State)
        {
            Base->Hertz[State] = Sram[2 * (Column * Count + State)];
            Base->SramMillivolts[State][Column] = Sram[2 * (Column * Count + State) + 1];
        }
    }
    for (State = 0; State < Count; ++State)
        AgxFwFillColumns(Base->SramMillivolts[State]);
    return TRUE;
}

BOOLEAN
NTAPI
AppleAgxFwParseDomain(
    _In_reads_bytes_(Bytes) const UCHAR *Data,
    _In_ ULONG Bytes,
    _In_ ULONG Rails,
    _In_ ULONG MaxStates,
    _Out_ PAPPLE_AGX_FW_PSTATE_TABLE Table)
{
    ULONG64 Header[2], Entry[2], Minimum;
    ULONG Count, State, Rail;
    const UCHAR *Cursor;

    RtlZeroMemory(Table, sizeof(*Table));
    if (!Data || Bytes < 0x38 || MaxStates > APPLE_AGX_FW_PSTATES)
        return FALSE;
    RtlCopyMemory(Header, Data, sizeof(Header));
    Count = (ULONG)Header[1];
    if (Header[0] >= 3 || Header[0] != Rails || Header[0] > APPLE_AGX_FW_PSTATE_COLUMNS ||
        Count < 1 || Count > MaxStates ||
        (ULONG64)(Count * 16 + 8) * Header[0] + 0x10 > Bytes)
    {
        return FALSE;
    }
    Table->Count = Count;
    Table->Columns = Rails;
    Cursor = Data + sizeof(Header);
    for (Rail = 0; Rail < Rails; ++Rail)
    {
        for (State = 0; State < Count; ++State)
        {
            RtlCopyMemory(Entry, Cursor, sizeof(Entry));
            Cursor += sizeof(Entry);
            Table->Millivolts[State][Rail] = (ULONG)(Entry[0] / 1000);
            Table->Hertz[State] = (ULONG)Entry[1];
        }
    }
    for (Rail = 0; Rail < Rails; ++Rail)
    {
        RtlCopyMemory(&Minimum, Cursor, sizeof(Minimum));
        Cursor += sizeof(Minimum);
        for (State = 0; State < Count; ++State)
        {
            Table->SramMillivolts[State][Rail] = max(Table->Millivolts[State][Rail], (ULONG)(Minimum / 1000));
        }
    }
    return TRUE;
}

VOID
NTAPI
AppleAgxFwMapRegisters(
    _In_reads_(16) const ULONG *Registers,
    _Out_writes_(APPLE_AGX_FW_PSTATES) PULONG MapA,
    _Out_writes_(APPLE_AGX_FW_PSTATES) PULONG MapB)
{
    ULONG Index;

    for (Index = 0; Index < 8; ++Index)
    {
        MapA[2 * Index] = Registers[Index] & 0xf;
        MapA[2 * Index + 1] = (Registers[Index] >> 16) & 0xf;
        MapB[2 * Index] = Registers[8 + Index] & 0xf;
        MapB[2 * Index + 1] = (Registers[8 + Index] >> 16) & 0xf;
    }
}

static
VOID
AgxFwCopyRow(
    _Out_ PAPPLE_AGX_FW_PSTATE_TABLE To,
    _In_ ULONG ToRow,
    _In_ const APPLE_AGX_FW_PSTATE_TABLE *From,
    _In_ ULONG FromRow)
{
    ASSERT(ToRow < APPLE_AGX_FW_PSTATES && FromRow < APPLE_AGX_FW_PSTATES);
    To->Hertz[ToRow] = From->Hertz[FromRow];
    RtlCopyMemory(To->Millivolts[ToRow], From->Millivolts[FromRow], sizeof(To->Millivolts[ToRow]));
    RtlCopyMemory(To->SramMillivolts[ToRow], From->SramMillivolts[FromRow], sizeof(To->SramMillivolts[ToRow]));
}

VOID
NTAPI
AppleAgxFwDeriveTables(
    _Inout_ PAPPLE_AGX_FW_CONFIG Config)
{
    const APPLE_AGX_FW_PSTATE_TABLE *Base = &Config->Base;
    ULONG Index, NextA = 1, NextB = 1;

    RtlZeroMemory(&Config->TableA, sizeof(Config->TableA));
    RtlZeroMemory(&Config->TableB, sizeof(Config->TableB));
    AgxFwCopyRow(&Config->TableA, 0, Base, 0);
    AgxFwCopyRow(&Config->TableB, 0, Base, 0);
    for (Index = 1; Index < APPLE_AGX_FW_PSTATES; ++Index)
    {
        if (Config->MapA[Index] > Base->Count)
            break;
        if (Config->MapA[Index] && NextA < APPLE_AGX_FW_PSTATES)
            AgxFwCopyRow(&Config->TableA, NextA++, Base, Config->MapA[Index]);
        if (Config->MapB[Index] && NextB < APPLE_AGX_FW_PSTATES && Config->MapB[Index] < APPLE_AGX_FW_PSTATES)
            AgxFwCopyRow(&Config->TableB, NextB++, Base, Config->MapB[Index]);
    }
    Config->TableA.Count = NextA;
    Config->TableA.Columns = Base->Columns;
    Config->TableB.Count = NextB;
    Config->TableB.Columns = Base->Columns;
    Config->Cap[0] = Config->TableA.Count - 1;
    Config->Cap[1] = Config->Domain[0].Count ? Config->Domain[0].Count - 1 : 0;
    Config->Cap[2] = Config->Domain[1].Count ? Config->Domain[1].Count - 1 : 0;
    if ((Config->Core.CoreId == APPLE_AGX_FW_CORE_G15X || Config->Core.CoreId == APPLE_AGX_FW_CORE_G15X_MAX) &&
        Config->Core.Revision == 1)
    {
        RtlCopyMemory(Config->Cap, AgxFwRevisionCap, sizeof(Config->Cap));
    }
}

VOID
NTAPI
AppleAgxFwDefaults(
    _Out_ PAPPLE_AGX_FW_CONFIG Config)
{
    static const ULONG IdleOff[APPLE_AGX_FW_IDLEOFF_PARAMS] =
    {
        700, 0x3f800000, 0x3f4ccccd, 0x3e4ccccd, 0x3f666666,
        0x3dcccccd, 0x3e800000, 0x3f333333, 0x3f666666, 6
    };

    RtlZeroMemory(Config, sizeof(*Config));
    Config->BootFlags = 0x0f;
    Config->SamplePeriodMs = 0x10;
    Config->SamplePeriodClocks = 0x61a80;
    Config->MaxPower = 0xfe6;
    Config->PowerFilter = 0x139;
    Config->PowerIntegralGain = 0x3ca59586;
    Config->PowerProportionalGain = 0x40a90fdb;
    Config->MinDutyCycle = 0x28;
    Config->BasePstate = 3;
    Config->PerfTargetUtilization = 0x5f;
    Config->PerfFilter[0] = 5;
    Config->PerfFilter[1] = 0x32;
    Config->PerfDualFilter = TRUE;
    Config->PerfIntegralGain[0] = 0x40fca970;
    Config->PerfIntegralGain[1] = 0x3e4a2121;
    Config->PerfIntegralScale = 0x5f;
    Config->PerfProportionalGain[0] = 0x416b53d1;
    Config->PerfProportionalGain[1] = 0x40db53d0;
    Config->PerfLimiterFilter = 5;
    RtlCopyMemory(Config->IdleOff, IdleOff, sizeof(IdleOff));
    Config->KeepAliveOffThreshold = 100;
    Config->KeepAlivePerfThreshold = 100;
    Config->McacheWrites = 4;
    Config->UscMaxTgmem = 4;
    Config->UatConfig = 4;
    Config->UseIoUat = TRUE;
    Config->Field688 = 5;
    Config->BrnMask = 0xff;
    Config->Field6c4[0] = 0xffff;
    Config->Field6c4[1] = 0x28;
    Config->Field6c4[2] = 0xffff;
}

static
BOOLEAN
AgxFwScratchAlloc(
    _Inout_ PAPPLE_AGX_FW_SCRATCH Scratch,
    _In_ BOOLEAN Window1,
    _In_ ULONG Size,
    _Out_ PVOID *Virtual,
    _Out_ PULONG64 Address)
{
    PULONG Used = Window1 ? &Scratch->Window1Used : &Scratch->Window0Used;
    ULONG Limit = Window1 ? Scratch->Window1Size : Scratch->Window0Size;
    ULONG Offset = AgxFwAlign(*Used, 8);

    if (Offset > Limit || Size > Limit - Offset)
        return FALSE;
    *Used = Offset + Size;
    *Virtual = (PUCHAR)(Window1 ? Scratch->Window1 : Scratch->Window0) + Offset;
    *Address = (Window1 ? Scratch->Window1Address : Scratch->Window0Address) + Offset;
    return TRUE;
}

BOOLEAN
NTAPI
AppleAgxFwScratchRing(
    _Inout_ PAPPLE_AGX_FW_SCRATCH Scratch,
    _In_opt_ const APPLE_AGX_FW_BUFFER *Entries,
    _In_ ULONG Record,
    _Out_ PAPPLE_AGX_FW_SCRATCH_RING Ring)
{
    PVOID Read, Completed, Write;

    RtlZeroMemory(Ring, sizeof(*Ring));
    ASSERT(Record != 0);
    if (!AgxFwScratchAlloc(Scratch, TRUE, sizeof(ULONG), &Read, &Ring->ReadAddress) ||
        !AgxFwScratchAlloc(Scratch, TRUE, sizeof(ULONG), &Completed, &Ring->CompletedAddress) ||
        !AgxFwScratchAlloc(Scratch, FALSE, sizeof(ULONG), &Write, &Ring->WriteAddress))
    {
        return FALSE;
    }
    Ring->Read = Read;
    Ring->Completed = Completed;
    Ring->Write = Write;
    Ring->Record = Record;
    if (Entries)
    {
        Ring->Entries = Entries->Virtual;
        Ring->EntriesAddress = Entries->Address;
        return Ring->Entries != NULL;
    }
    return AgxFwScratchAlloc(Scratch, FALSE, APPLE_AGX_FW_RING_SLOTS * Record, &Ring->Entries,
                             &Ring->EntriesAddress);
}

static
VOID
AgxFwWriteUat(
    _Out_writes_bytes_(APPLE_AGX_FW_ROOT_UAT_SIZE) PUCHAR Uat)
{
    ULONG Level, Offset;

    RtlZeroMemory(Uat, APPLE_AGX_FW_ROOT_UAT_SIZE);
    AgxFwPut16(Uat, 0, APPLE_AGX_FW_PAGE);
    AgxFwPut8(Uat, 2, 14);
    AgxFwPut8(Uat, 3, 3);
    for (Level = 0; Level < 3; ++Level)
    {
        Offset = 4 + Level * 0x20;
        AgxFwPut8(Uat, Offset + 0, 8);
        AgxFwPut8(Uat, Offset + 1, 14);
        AgxFwPut8(Uat, Offset + 2, 14);
        AgxFwPut8(Uat, Offset + 3, AgxFwUatLevelShift[Level]);
        AgxFwPut16(Uat, Offset + 4, AgxFwUatLevelEntries[Level]);
        AgxFwPut16(Uat, Offset + 6, APPLE_AGX_FW_PAGE);
        AgxFwPut64(Uat, Offset + 8, 1);
        AgxFwPut64(Uat, Offset + 0x10, AGX_FW_UAT_OUTPUT_MASK);
        AgxFwPut64(Uat, Offset + 0x18, (ULONG64)(AgxFwUatLevelEntries[Level] - 1) << AgxFwUatLevelShift[Level]);
    }
}

static
ULONG
AgxFwLowMask(
    _In_ ULONG Bits)
{
    return Bits >= 32 ? 0xffffffff : (1UL << Bits) - 1;
}

static
VOID
AgxFwWriteTable(
    _In_ PVOID Hw,
    _In_ ULONG Mhz,
    _In_ ULONG Millivolts,
    _In_ ULONG Sram,
    _In_ ULONG Stride,
    _In_ ULONG Rows,
    _In_ ULONG Columns,
    _In_ const APPLE_AGX_FW_PSTATE_TABLE *Table)
{
    ULONG Row, Column;

    ASSERT(Columns <= APPLE_AGX_FW_PSTATE_COLUMNS && Columns * 4 <= Stride);
    for (Row = 0; Row < Rows && Row < APPLE_AGX_FW_PSTATES; ++Row)
    {
        AgxFwPut32(Hw, Mhz + Row * 4, Table->Hertz[Row] / AGX_FW_HZ_PER_MHZ);
        for (Column = 0; Column < Columns; ++Column)
        {
            AgxFwPut32(Hw, Millivolts + Row * Stride + Column * 4, Table->Millivolts[Row][Column]);
            AgxFwPut32(Hw, Sram + Row * Stride + Column * 4, Table->SramMillivolts[Row][Column]);
        }
    }
}

static
VOID
AgxFwWriteHardware(
    _Inout_ PAPPLE_AGX_FW Fw,
    _In_ const APPLE_AGX_FW_CONFIG *Config)
{
    PVOID Hw = Fw->Object[APPLE_AGX_FW_HWCFG].Virtual;
    PVOID Power = Fw->Object[APPLE_AGX_FW_POWER].Virtual;
    ULONG64 Features = Config->Features;
    ULONG Index, Top = Config->TableA.Count ? Config->TableA.Count - 1 : 0;
    ULONG Shift24 = (ULONG)(Features >> 32) & 0xffffff;

    AgxFwPut64(Hw, 0x00, 0x6f00000000ULL);
    AgxFwPut64(Hw, 0x08, 0xffc00000ULL);
    AgxFwPut64(Hw, 0x10, 0x1000000000ULL);
    AgxFwPut64(Hw, 0x18, 0x1000000000ULL);
    AgxFwPut64(Hw, 0x20, 0x2ffffff8000ULL);
    AgxFwPut64(Hw, 0x28, Fw->TimestampAddress ? Fw->TimestampAddress : AGX_FW_TIMESTAMP_BASE);
    for (Index = 0; Index < APPLE_AGX_FW_IOMAPS; ++Index)
    {
        const APPLE_AGX_FW_REGION *Region = &Config->Region[Index];
        ULONG Record = APPLE_AGX_FW_HW_IOMAPS + Index * APPLE_AGX_FW_HW_IOMAP_STRIDE;

        AgxFwPut64(Hw, Record + 0x00, Region->Physical[0]);
        AgxFwPut64(Hw, Record + 0x08, (Region->Physical[0] && Region->Size) ? Fw->IoMapAddress[Index] : 0);
        AgxFwPut32(Hw, Record + 0x10, Region->Count * Region->Size);
        AgxFwPut32(Hw, Record + 0x14, Region->Size);
        AgxFwPut32(Hw, Record + 0x18, Region->Writable ? 1 : 0);
    }
    AgxFwPut32(Hw, 0xa38, (Config->BootFlags >> 1) & 1);
    AgxFwPut32(Hw, 0xa3c, (ULONG)(Features >> 8) & 1);
    AgxFwPut32(Hw, 0xa40, 1);
    AgxFwPut32(Hw, 0xa44, (Config->BootFlags >> 4) & 1);
    AgxFwPut64(Hw, 0xa48, 0);
    AgxFwPut32(Hw, 0xa50, 1);
    AgxFwPut32(Hw, 0xa54, 0);
    AgxFwPut32(Hw, 0xa58, (ULONG)(Features >> 11) & 1);
    AgxFwPut32(Hw, 0xa5c, (ULONG)(Features >> 12) & 1);
    AgxFwPut32(Hw, 0xa60, 1);
    AgxFwPut32(Hw, 0xa64, 0);
    AgxFwPut32(Hw, 0xa68, 24000);
    AgxFwPut32(Hw, 0xa6c, (Config->BootFlags & 0x10) ? 200 : 1);
    AgxFwPut32(Hw, 0xa70, Config->SamplePeriodMs);
    AgxFwPut32(Hw, 0xa78, 1);
    AgxFwPut64(Hw, 0xa7c, 0x100000001ULL);
    AgxFwPut32(Hw, 0xa98, Config->PmpSwInterrupt ? 1 : 0);
    AgxFwPut32(Hw, 0xa9c, 0x1f);
    AgxFwPut32(Hw, 0xaa4, (ULONG)(Features >> 19) & 1);
    AgxFwPut64(Hw, 0xabc, Config->McacheWrites);
    AgxFwPut32(Hw, 0xac8, 0);
    AgxFwPut32(Hw, 0xacc, 1);
    AgxFwPut32(Hw, 0xad0, 1);
    AgxFwPut32(Hw, 0xad4, 0);
    AgxFwPut32(Hw, 0xadc, (ULONG)(Features >> 21) & 1);
    AgxFwPut32(Hw, 0xae0, (ULONG)(Features >> 26) & 1);
    AgxFwPut32(Hw, 0xae4, 0x31);
    AgxFwPut32(Hw, 0xb00, (Shift24 >> 12) & 1);
    AgxFwPut32(Hw, 0xb04, Config->UseIoUat ? 1 : 0);
    AgxFwPut32(Hw, 0xb18, (ULONG)(Features >> 25) & 1);
    AgxFwPut32(Hw, 0xb20, Config->Core.TotalCores);
    AgxFwPut64(Hw, 0xb24, Config->BrnMask);
    AgxFwPut32(Hw, 0xb40, Config->UatConfig ? 1 : 0);
    AgxFwPut64(Hw, 0xb44, Config->GpuTtbatBase);
    AgxFwPut64(Hw, 0xb4c, (ULONG64)Config->Core.CoreId | ((ULONG64)Config->Core.Revision << 32));
    AgxFwPut32(Hw, 0xb54, Config->Core.TotalCores);
    AgxFwPut32(Hw, APPLE_AGX_FW_HW_PSTATE_TOP, Top);
    AgxFwWriteTable(Hw, APPLE_AGX_FW_HW_PSTATE_MHZ_A, APPLE_AGX_FW_HW_PSTATE_MV, APPLE_AGX_FW_HW_PSTATE_SRAM_MV,
                    0x20, APPLE_AGX_FW_PSTATES, APPLE_AGX_FW_PSTATE_COLUMNS, &Config->TableA);
    for (Index = 0; Index < Config->TableB.Count && Index < APPLE_AGX_FW_PSTATES; ++Index)
        AgxFwPut32(Hw, APPLE_AGX_FW_HW_PSTATE_MHZ_B + Index * 4, Config->TableB.Hertz[Index] / AGX_FW_HZ_PER_MHZ);
    for (Index = 0; Index < APPLE_AGX_FW_PSTATES; ++Index)
    {
        AgxFwPut32(Hw, 0xfdc + Index * 4, AgxFwGet32(Power, 0x80 + Index * 4));
        AgxFwPut32(Hw, 0x101c + Index * 4, AgxFwGet32(Power, 0xc0 + Index * 4));
        AgxFwPut32(Hw, APPLE_AGX_FW_HW_PSTATE_MAP_A + Index * 4, Config->MapA[Index]);
        AgxFwPut32(Hw, APPLE_AGX_FW_HW_PSTATE_MAP_B + Index * 4, Config->MapB[Index]);
    }
    if (Config->Core.Domain1 && Config->Domain[0].Count)
    {
        AgxFwPut32(Hw, 0x115c, Config->Domain[0].Count - 1);
        AgxFwPut32(Hw, 0x1160, Config->Cap[1]);
        AgxFwWriteTable(Hw, 0x1164, 0x11a4, 0x1224, 8, Config->Domain[0].Count, min(Config->Domain[0].Columns, 2),
                        &Config->Domain[0]);
    }
    if (Config->Core.Domain2 && Config->Domain[1].Count)
    {
        AgxFwPut32(Hw, 0x12a4, Config->Domain[1].Count - 1);
        AgxFwPut32(Hw, 0x12a8, Config->Cap[2]);
        AgxFwWriteTable(Hw, 0x12ac, 0x12cc, 0x130c, 8, min(Config->Domain[1].Count, 8), min(Config->Domain[1].Columns, 2),
                        &Config->Domain[1]);
    }
    if (Config->Core.SramDomain && Config->TableB.Count)
    {
        RtlZeroMemory((PUCHAR)Hw + 0x134c, 0x448);
        AgxFwPut32(Hw, 0x134c, Config->TableB.Count - 1);
        AgxFwWriteTable(Hw, 0x1350, 0x1390, 0x1590, 0x20, Config->TableB.Count, Config->TableB.Columns, &Config->TableB);
    }
    AgxFwPut32(Hw, 0x17b8, Config->Field688);
    AgxFwPut32(Hw, 0x17c0, AgxFwLowMask(Config->Core.Field24));
    AgxFwPut32(Hw, 0x17c4, AgxFwLowMask(Config->Core.Field28));
    AgxFwPut32(Hw, 0x17c8, AgxFwLowMask(Config->Core.Field2c));
    AgxFwPut32(Hw, 0x17e8, 1);
    AgxFwPut32(Hw, 0x17fc, (Shift24 >> 4) & 1);
    AgxFwPut32(Hw, 0x1800, 1);
    AgxFwPut32(Hw, 0x1804, 1);
    AgxFwPut32(Hw, 0x1808, (Shift24 >> 5) & 1);
    AgxFwPut64(Hw, 0x1810, 0x100000001ULL);
    AgxFwPut32(Hw, 0x1818, (ULONG)(Features >> 7) & 1);
    RtlFillMemory((PUCHAR)Hw + 0x1820, 0x30, 0xff);
    AgxFwPut32(Hw, 0x1860, (Config->BootFlags & 0x10) ? 0 : 1);
}

static
VOID
AgxFwWritePower(
    _Inout_ PAPPLE_AGX_FW Fw,
    _In_ const APPLE_AGX_FW_CONFIG *Config)
{
    PVOID Power = Fw->Object[APPLE_AGX_FW_POWER].Virtual;
    ULONG Top = Config->TableA.Count ? Config->TableA.Count - 1 : 0;
    float Filter, Limiter;
    ULONG Index;

    AgxFwPut32(Power, 0x04, Config->SamplePeriodClocks);
    AgxFwPut32(Power, 0x08, Config->SamplePeriodClocks);
    AgxFwPut64(Power, 0x0c, 0x400000000ULL);
    AgxFwPut64(Power, 0x14, 0x3f800000ULL);
    AgxFwPut64(Power, 0x2c, 0x100000001ULL);
    AgxFwPut32(Power, 0x54, 100);
    Filter = (float)(Config->PowerFilter ? Config->PowerFilter : 1);
    AgxFwPutFloat(Power, 0x664, (Filter - 1.0f) / Filter);
    AgxFwPutFloat(Power, 0x66c, 1.0f / Filter);
    AgxFwPut32(Power, 0x674, Config->PowerIntegralGain);
    AgxFwPutFloat(Power, 0x67c, (float)Config->PowerIntegralMinClamp);
    AgxFwPutFloat(Power, 0x680, (float)(LONG)Config->MaxPower);
    AgxFwPut32(Power, 0x684, Config->PowerProportionalGain);
    AgxFwPutFloat(Power, 0x68c, Config->MaxPower ? -(float)(Top * 100) / (float)Config->MaxPower : 0.0f);
    AgxFwPut32(Power, 0x690, Config->MinDutyCycle - Top * 100);
    AgxFwPut32(Power, 0x698, Top * 100);
    AgxFwPut32(Power, 0x6a4, Config->MaxPower);
    AgxFwPut32(Power, 0x6d0, Config->PowerTarget[0]);
    AgxFwPut32(Power, 0x6d4, Top * 100);
    AgxFwPut32(Power, 0x6d8, Config->PowerTarget[1]);
    AgxFwPut32(Power, 0x7a4, Config->PerfDualFilter ? 1 : 0);
    AgxFwPut32(Power, 0x7a8, Config->PerfFilterDropThreshold);
    for (Index = 0; Index < 2; ++Index)
    {
        Filter = (float)(Config->PerfFilter[Index] ? Config->PerfFilter[Index] : 1);
        AgxFwPutFloat(Power, 0x7ac + Index * 4, (Filter - 1.0f) / Filter);
        AgxFwPutFloat(Power, 0x7b4 + Index * 4, 1.0f / Filter);
        AgxFwPut32(Power, 0x7bc + Index * 4, Config->PerfIntegralGain[Index]);
        AgxFwPut32(Power, 0x7cc + Index * 4, Config->PerfProportionalGain[Index]);
    }
    AgxFwPutFloat(Power, 0x7c4, (float)Config->PerfIntegralMinClamp);
    AgxFwPutFloat(Power, 0x7c8, (float)(LONG)Config->PerfIntegralScale);
    if (Config->PerfIntegralScale)
        AgxFwPutFloat(Power, 0x7d4, (float)((Top - min(Config->BasePstate, Top)) * 100) / (float)Config->PerfIntegralScale);
    AgxFwPut32(Power, 0x7d8, Config->BasePstate * 100);
    AgxFwPut32(Power, 0x7dc, Top * 100);
    AgxFwPut32(Power, 0x7e0, Config->BasePstate * 100);
    AgxFwPut32(Power, 0x7ec, Config->PerfTargetUtilization);
    AgxFwPut32(Power, 0x788, Config->PerfTargetUtilization);
    AgxFwPutFloat(Power, 0x818, (float)Config->PerfSlot2340);
    AgxFwPut32(Power, 0x81c, Config->BasePstate * 100);
    AgxFwPut32(Power, 0xbe0, Top * 100);
    AgxFwPut32(Power, 0x864, Top * 100);
    Limiter = (float)(Config->PerfLimiterFilter ? Config->PerfLimiterFilter : 1);
    AgxFwPutFloat(Power, 0x86c, (Limiter - 1.0f) / Limiter);
    AgxFwPutFloat(Power, 0x870, 1.0f / Limiter);
    AgxFwPut32(Power, 0x4298, (ULONG)(Config->Features >> 18) & 1);
    AgxFwPut32(Power, 0x429c, (ULONG)(Config->Features >> 31) & 1);
}

static
VOID
AgxFwWriteGlobals(
    _Inout_ PAPPLE_AGX_FW Fw,
    _In_ const APPLE_AGX_FW_CONFIG *Config)
{
    PVOID Globals = Fw->Object[APPLE_AGX_FW_GLOBALS].Virtual;
    ULONG Index;

    AgxFwPut32(Globals, 0x00, (Config->Features & 0x10000000) ? 7 : 0);
    AgxFwPut32(Globals, 0x20, 3000);
    AgxFwPut32(Globals, 0x2c, (ULONG)Config->Features & 1);
    AgxFwPut32(Globals, 0x30, (ULONG)(Config->Features >> 1) & 1);
    AgxFwPut32(Globals, 0x38, 0x78);
    AgxFwPut16(Globals, 0x50, Config->Field6c4[0]);
    AgxFwPut16(Globals, 0x52, Config->Field6c4[1]);
    AgxFwPut16(Globals, 0x54, Config->Field6c4[2]);
    AgxFwPut32(Globals, 0x5a, 1);
    AgxFwPut32(Globals, 0x5e, (Config->BootFlags & 0x10) ? 200 : 1);
    AgxFwPut32(Globals, 0x8c, Config->Cap[0] * 100);
    for (Index = 0; Index < APPLE_AGX_FW_IDLEOFF_PARAMS; ++Index)
        AgxFwPut32(Globals, 0x7a8 + Index * 4, Config->IdleOff[Index]);
    AgxFwPut32(Globals, 0x7d0, 1);
    AgxFwPut32(Globals, 0x7d4, 1);
    AgxFwPut32(Globals, 0x7d8, Config->KeepAlivePerfThreshold);
    AgxFwPut32(Globals, 0x7dc, Config->KeepAliveOffThreshold);
    AgxFwPut32(Globals, 0x9b0, 40);
    AgxFwPut32(Globals, 0x9b4, 50);
    AgxFwPut8(Globals, 0x9bc, 4);
}

static
VOID
AgxFwWriteRing(
    _In_ PVOID Runtime,
    _In_ ULONG Offset,
    _In_ const APPLE_AGX_FW_SCRATCH_RING *Ring)
{
    AgxFwPut64(Runtime, Offset + 0x00, Ring->ReadAddress);
    AgxFwPut64(Runtime, Offset + 0x08, Ring->CompletedAddress);
    AgxFwPut64(Runtime, Offset + 0x10, Ring->WriteAddress);
    AgxFwPut64(Runtime, Offset + 0x18, Ring->EntriesAddress);
}

NTSTATUS
NTAPI
AppleAgxFwWrite(
    _Inout_ PAPPLE_AGX_FW Fw,
    _In_ const APPLE_AGX_FW_CONFIG *Config)
{
    PVOID Root = Fw->Root.Virtual, Runtime, Control;
    ULONG Row, Column, Index;

    if (!Root || !Fw->RegionA.Virtual || !Fw->Object[APPLE_AGX_FW_RUNTIME].Virtual ||
        !Fw->Object[APPLE_AGX_FW_CONTROL].Virtual || !Fw->Object[APPLE_AGX_FW_HWCFG].Virtual ||
        !Fw->DeviceControl.Write)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    for (Index = 0; Index < APPLE_AGX_FW_DM_RINGS; ++Index)
    {
        if (!Fw->DataMaster[Index].Write)
            return STATUS_INVALID_DEVICE_STATE;
    }
    Runtime = Fw->Object[APPLE_AGX_FW_RUNTIME].Virtual;
    Control = Fw->Object[APPLE_AGX_FW_CONTROL].Virtual;

    RtlZeroMemory(Fw->RegionA.Virtual, 0x18);

    AgxFwPut64(Root, 0x00, APPLE_AGX_FW_MAGIC);
    AgxFwPut64(Root, APPLE_AGX_FW_ROOT_REGION_A, Fw->RegionA.Address);
    AgxFwPut64(Root, APPLE_AGX_FW_ROOT_RUNTIME, Fw->Object[APPLE_AGX_FW_RUNTIME].Address);
    AgxFwPut64(Root, APPLE_AGX_FW_ROOT_GLOBALS, Fw->Object[APPLE_AGX_FW_GLOBALS].Address);
    AgxFwPut64(Root, APPLE_AGX_FW_ROOT_ENABLE, 0x100000000ULL);
    AgxFwWriteUat((PUCHAR)Root + APPLE_AGX_FW_ROOT_UAT);
    AgxFwPut64(Root, APPLE_AGX_FW_ROOT_FLAGS, Fw->Object[APPLE_AGX_FW_FLAGS].Address);
    AgxFwPut64(Root, APPLE_AGX_FW_ROOT_CONTROL, Fw->Object[APPLE_AGX_FW_CONTROL].Address);
    AgxFwPut64(Root, APPLE_AGX_FW_ROOT_STATUS, Fw->Object[APPLE_AGX_FW_STATUS].Address);

    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_HWCFG, Fw->Object[APPLE_AGX_FW_HWCFG].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_BRN, Fw->Object[APPLE_AGX_FW_BRN].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_MISC, Fw->Object[APPLE_AGX_FW_MISC].Address);
    for (Row = 0; Row < 4; ++Row)
    {
        for (Column = 0; Column < 3; ++Column)
        {
            AgxFwWriteRing(Runtime, APPLE_AGX_FW_RT_CHANNELS + Row * 0x60 + Column * 0x20,
                           &Fw->DataMaster[Column * 4 + Row]);
        }
    }
    AgxFwWriteRing(Runtime, APPLE_AGX_FW_RT_DEVCTL, &Fw->DeviceControl);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x00, Fw->Object[APPLE_AGX_FW_EVENT_STATE].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x08, Fw->Object[APPLE_AGX_FW_EVENT_RING].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x10, Fw->Object[APPLE_AGX_FW_LOG_STATE].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x18, Fw->Object[APPLE_AGX_FW_LOG_RING].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x20, Fw->Object[APPLE_AGX_FW_TRACE_STATE].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x28, Fw->Object[APPLE_AGX_FW_TRACE_RING].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x30, Fw->Object[APPLE_AGX_FW_STATS_STATE].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_RX + 0x38, Fw->Object[APPLE_AGX_FW_STATS_RING].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_KERNEL_LOG, Fw->Object[APPLE_AGX_FW_KERNEL_LOG].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_TA_ACTIVITY, Fw->Object[APPLE_AGX_FW_TA_ACTIVITY].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_FS_ACTIVITY, Fw->Object[APPLE_AGX_FW_FS_ACTIVITY].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_CS_ACTIVITY, Fw->Object[APPLE_AGX_FW_CS_ACTIVITY].Address);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_FAULT, Fw->Object[APPLE_AGX_FW_FAULT].Address);
    if (Config->PbTableAddress)
    {
        AgxFwPut64(Runtime, APPLE_AGX_FW_RT_PB_TABLE, Config->PbTableAddress);
        AgxFwPut64(Runtime, APPLE_AGX_FW_RT_PB_TABLE + 8, Config->PbTableAddress);
    }
    if (Config->UmaTableAddress)
    {
        AgxFwPut64(Runtime, APPLE_AGX_FW_RT_UMA_TABLE, Config->UmaTableAddress);
        AgxFwPut64(Runtime, APPLE_AGX_FW_RT_UMA_TABLE + 8, Config->UmaTableAddress);
    }
    AgxFwPut32(Runtime, 0x2d0, Config->UscMaxTgmem);
    AgxFwPut8(Runtime, 0x3b0, 0xff);
    AgxFwPut64(Runtime, APPLE_AGX_FW_RT_POWER, Fw->Object[APPLE_AGX_FW_POWER].Address);

    AgxFwPut32(Control, 0x04, (ULONG)(Config->Features >> 2) & 1);
    AgxFwPut64(Control, APPLE_AGX_FW_CTL_FWCTL_STATE, Fw->Object[APPLE_AGX_FW_FWCTL_STATE].Address);
    AgxFwPut64(Control, APPLE_AGX_FW_CTL_FWCTL_RING, Fw->Object[APPLE_AGX_FW_FWCTL_RING].Address);
    AgxFwPut32(Control, APPLE_AGX_FW_CTL_ENABLE, 1);
    AgxFwPut32(Control, 0xc3cc, (ULONG)(Config->Features >> 28) & 1);

    AgxFwPut32(Fw->Object[APPLE_AGX_FW_FLAGS].Virtual, 0x04, 1);
    AgxFwPut32(Fw->Object[APPLE_AGX_FW_FS_ACTIVITY].Virtual, 0xc18, 0xffffffff);
    AgxFwPut32(Fw->Object[APPLE_AGX_FW_FS_ACTIVITY].Virtual, 0xc30, 0xffffffff);

    AgxFwWritePower(Fw, Config);
    AgxFwWriteHardware(Fw, Config);
    AgxFwWriteGlobals(Fw, Config);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

ULONG64
NTAPI
AppleAgxFwInitMessage(
    _In_ const APPLE_AGX_FW *Fw)
{
    ASSERT(Fw->Root.Address != 0);
    return APPLE_AGX_FW_MSG_INITDATA | (Fw->Root.Address & APPLE_AGX_FW_MSG_ADDRESS);
}

NTSTATUS
NTAPI
AppleAgxFwPost(
    _Inout_ PAPPLE_AGX_FW_SCRATCH_RING Ring,
    _In_reads_bytes_(Ring->Record) const VOID *Record,
    _Out_opt_ PULONG Slot)
{
    ULONG Read, Next;

    ASSERT(Ring->Entries && Ring->Write && Ring->Read);
    if (*Ring->Write != Ring->Producer)
        return STATUS_DATA_ERROR;
    Next = (Ring->Producer + 1) % APPLE_AGX_FW_RING_SLOTS;
    Read = *Ring->Read;
    if (Read >= APPLE_AGX_FW_RING_SLOTS)
        return STATUS_DATA_ERROR;
    if (Next == Read)
        return STATUS_DEVICE_BUSY;
    RtlCopyMemory((PUCHAR)Ring->Entries + Ring->Producer * Ring->Record, Record, Ring->Record);
    KeMemoryBarrier();
    *Ring->Write = Next;
    Ring->Producer = Next;
    if (Slot)
        *Slot = Next;
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
AppleAgxFwTake(
    _In_ PVOID State,
    _In_ PVOID Records,
    _In_ ULONG Record,
    _Out_writes_bytes_(Record) PVOID Out)
{
    ULONG Read = AgxFwGet32(State, APPLE_AGX_FW_STATE_READ);
    ULONG Write = AgxFwGet32(State, APPLE_AGX_FW_STATE_WRITE);

    if (Read == Write || Read >= APPLE_AGX_FW_RING_SLOTS)
        return FALSE;
    KeMemoryBarrier();
    RtlCopyMemory(Out, (PUCHAR)Records + Read * Record, Record);
    KeMemoryBarrier();
    AgxFwPut32(State, APPLE_AGX_FW_STATE_READ, (Read + 1) % APPLE_AGX_FW_RING_SLOTS);
    return TRUE;
}
