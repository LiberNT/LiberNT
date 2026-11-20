/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple graphics firmware data host regression test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <reactos/drivers/appleagxfw.h>

static unsigned Checks, Failures;

#define CHECK(Expression)                                                                   \
    do                                                                                      \
    {                                                                                       \
        ++Checks;                                                                           \
        if (!(Expression))                                                                  \
        {                                                                                   \
            ++Failures;                                                                     \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #Expression);                         \
        }                                                                                   \
    } while (0)

#define CHECK_EQ(Got, Want)                                                                 \
    do                                                                                      \
    {                                                                                       \
        unsigned long long G_ = (unsigned long long)(Got), W_ = (unsigned long long)(Want); \
        ++Checks;                                                                           \
        if (G_ != W_)                                                                       \
        {                                                                                   \
            ++Failures;                                                                     \
            printf("%s:%d: %s = %#llx, want %#llx\n", __FILE__, __LINE__, #Got, G_, W_);    \
        }                                                                                   \
    } while (0)

static ULONG64
Get64(const void *Base, ULONG Offset)
{
    ULONG64 Value;

    memcpy(&Value, (const UCHAR *)Base + Offset, sizeof(Value));
    return Value;
}

static ULONG
Get32(const void *Base, ULONG Offset)
{
    ULONG Value;

    memcpy(&Value, (const UCHAR *)Base + Offset, sizeof(Value));
    return Value;
}

static float
GetFloat(const void *Base, ULONG Offset)
{
    float Value;

    memcpy(&Value, (const UCHAR *)Base + Offset, sizeof(Value));
    return Value;
}

static void
TestLayout(void)
{
    static const ULONG GroupA[] = { 0, 0x1880, 0x24c0, 0x3740, 0x4580, 0x8900, 0x8980, 0x8980, 0x8a40, 0x8f00 };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(GroupA); ++Index)
    {
        CHECK_EQ(AppleAgxFwGroupOf(Index), APPLE_AGX_FW_GROUP_A);
        CHECK_EQ(AppleAgxFwObjectOffset(Index), GroupA[Index]);
    }
    CHECK_EQ(AppleAgxFwGroupSize(APPLE_AGX_FW_GROUP_A), 0x10000);
    CHECK(AppleAgxFwGroupCached(APPLE_AGX_FW_GROUP_A));
    CHECK(!AppleAgxFwGroupCached(APPLE_AGX_FW_GROUP_B));
    CHECK(!AppleAgxFwGroupCached(APPLE_AGX_FW_GROUP_C));
    CHECK(!AppleAgxFwGroupCached(APPLE_AGX_FW_GROUP_CTL));
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_HWCFG), 0x1868);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_POWER), 0x4360);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_BRN), 0);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_RUNTIME), 0x490);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_DEVCTL_RING), 0x3800);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_CONTROL), 0xc3d0);
    CHECK_EQ(AppleAgxFwObjectOffset(APPLE_AGX_FW_CONTROL), 0);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_STATUS), 0x238);
    CHECK_EQ(AppleAgxFwObjectOffset(APPLE_AGX_FW_STATUS), 0xc400);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_LOG_STATE), 0x120);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_EVENT_RING), 0x3800);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_LOG_RING), 0x15000);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_KERNEL_LOG), 0x51000);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_TRACE_RING), 0x7000);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_STATS_RING), 0x4000);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_GLOBALS), 0xe00);
    CHECK_EQ(AppleAgxFwGroupOf(APPLE_AGX_FW_GLOBALS), APPLE_AGX_FW_GROUP_C);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_FWCTL_STATE), 0x20);
    CHECK_EQ(AppleAgxFwObjectSize(APPLE_AGX_FW_FWCTL_RING), 0x1800);
    CHECK_EQ(AppleAgxFwObjectOffset(APPLE_AGX_FW_FWCTL_RING), 0x40);
}

static void
TestCore(void)
{
    APPLE_AGX_FW_CORE Core;

    CHECK(AppleAgxFwParseCore(0x07041100, 0x00010603, 0, 0x00000011, 0x3ffff, &Core));
    CHECK_EQ(Core.CoreId, APPLE_AGX_FW_CORE_G15X);
    CHECK_EQ(Core.Revision, 4);
    CHECK_EQ(Core.Mgpus, 1);
    CHECK_EQ(Core.Clusters, 6);
    CHECK_EQ(Core.CoresPerCluster, 3);
    CHECK_EQ(Core.TotalCores, 18);
    CHECK_EQ(Core.Field24, 1);
    CHECK(Core.SramDomain && Core.Domain1 && Core.Domain2);
    CHECK(AppleAgxFwParseCore(0x07030000, 0x00010403, 0, 0, 0, &Core));
    CHECK_EQ(Core.CoreId, APPLE_AGX_FW_CORE_G15X_MAX);
    CHECK_EQ(Core.Revision, 1);
    CHECK_EQ(Core.CoresPerCluster, 10);
    CHECK(!Core.Domain1);
    CHECK(!AppleAgxFwParseCore(0x07040000, 0x00020603, 0, 0, 0, &Core));
    CHECK(!AppleAgxFwParseCore(0x06040000, 0x00010603, 0, 0, 0, &Core));
}

static void
TestRegions(void)
{
    APPLE_AGX_FW_CONFIG Config;

    AppleAgxFwDefaults(&Config);
    Config.ChipId = 0x6030;
    Config.GpuBase = 0x290000000ULL;
    Config.FenderBase = 0x290d00000ULL;
    Config.Core.CoreId = APPLE_AGX_FW_CORE_G15X;
    CHECK(AppleAgxFwRegions(&Config));
    CHECK_EQ(Config.Region[0].Physical[0], 0x290d00000ULL);
    CHECK_EQ(Config.Region[0].Size, 0x144000);
    CHECK(Config.Region[0].Writable);
    CHECK_EQ(Config.Region[1].Physical[0], 0x20e100000ULL);
    CHECK(!Config.Region[1].Writable);
    CHECK_EQ(Config.Region[2].Physical[0], 0x351014000ULL);
    CHECK_EQ(Config.Region[3].Physical[0], 0x290000000ULL);
    CHECK_EQ(Config.Region[3].Size, 0x20000);
    CHECK_EQ(Config.Region[7].Physical[0], 0x290444000ULL);
    CHECK_EQ(Config.Region[9].Physical[0], 0x290e08000ULL);
    CHECK_EQ(Config.Region[11].Count, 8);
    CHECK_EQ(Config.Region[11].Size, 0x98000);
    CHECK_EQ(Config.Region[11].Physical[0], 0x220000000ULL);
    CHECK_EQ(Config.Region[11].Physical[1], 0x222000000ULL);
    CHECK_EQ(Config.Region[11].Physical[7], 0x22e000000ULL);
    CHECK_EQ(Config.Region[12].Physical[0], 0x35104c000ULL);
    CHECK(!Config.Region[12].Writable);
    CHECK_EQ(Config.Region[19].Physical[0], 0x3503c0000ULL);
    CHECK_EQ(Config.Region[21].Physical[0], 0x290eb4000ULL);
    CHECK_EQ(Config.Region[23].Count, 0);
    CHECK_EQ(Config.Region[24].Physical[0], 0x291000000ULL);
    CHECK_EQ(Config.Region[24].Size, 0x8000);
    CHECK_EQ(Config.Region[25].Physical[0], 0x3c945c000ULL);
    CHECK_EQ(Config.Region[26].Physical[0], 0x350280000ULL);
    CHECK_EQ(Config.Region[28].Physical[0], 0x2a4dc0000ULL);
    CHECK_EQ(Config.Region[28].Size, 0x24000);
    CHECK_EQ(Config.Region[29].Physical[0], 0x290e5c000ULL);
    CHECK_EQ(Config.Region[6].Count, 0);
    CHECK(Config.Region[6].Writable);
    CHECK_EQ(Config.Region[APPLE_AGX_FW_REGION_SCRATCH].Physical[0], 0x290d60000ULL);
    CHECK_EQ(Config.Region[APPLE_AGX_FW_REGION_SCRATCH].Size, 0x20000);
    CHECK_EQ(Config.Region[APPLE_AGX_FW_REGION_SCRATCH].Window0, 0x14000);
    CHECK_EQ(Config.Region[APPLE_AGX_FW_REGION_SCRATCH].Window1, 0xc000);
    Config.ChipId = 0x6034;
    CHECK(AppleAgxFwRegions(&Config));
    CHECK_EQ(Config.Region[11].Count, 6);
    Config.Core.CoreId = 0x16;
    CHECK(!AppleAgxFwRegions(&Config));
}

static void
TestPstates(void)
{
    static const ULONG States[] = { 0, 600, 338000000, 650, 618000000, 0, 798000000, 700, 1100000000, 800 };
    static const ULONG Sram[] = { 0, 810, 338000000, 810, 618000000, 820, 798000000, 830, 1100000000, 850 };
    ULONG Registers[16] = { 0 };
    APPLE_AGX_FW_CONFIG Config;

    AppleAgxFwDefaults(&Config);
    CHECK(!AppleAgxFwParsePstates(0, 1, States, sizeof(States), NULL, 0, &Config.Base));
    CHECK(!AppleAgxFwParsePstates(16, 1, States, sizeof(States), NULL, 0, &Config.Base));
    CHECK(!AppleAgxFwParsePstates(5, 1, States, sizeof(States), NULL, 0, &Config.Base));
    CHECK(AppleAgxFwParsePstates(4, 1, States, sizeof(States), Sram, sizeof(Sram), &Config.Base));
    CHECK_EQ(Config.Base.Count, 5);
    CHECK_EQ(Config.Base.Columns, 1);
    CHECK_EQ(Config.Base.Hertz[4], 1100000000);
    CHECK_EQ(Config.Base.Millivolts[1][0], 650);
    CHECK_EQ(Config.Base.Millivolts[1][7], 650);
    CHECK_EQ(Config.Base.Millivolts[2][3], 0);
    CHECK_EQ(Config.Base.SramMillivolts[4][5], 850);
    Registers[0] = 0x00010000;
    Registers[1] = 0x00030002;
    Registers[2] = 0x00000004;
    Registers[8] = 0x00020000;
    Registers[9] = 0x00000004;
    AppleAgxFwMapRegisters(Registers, Config.MapA, Config.MapB);
    CHECK_EQ(Config.MapA[0], 0);
    CHECK_EQ(Config.MapA[1], 1);
    CHECK_EQ(Config.MapA[2], 2);
    CHECK_EQ(Config.MapA[3], 3);
    CHECK_EQ(Config.MapA[4], 4);
    CHECK_EQ(Config.MapB[1], 2);
    CHECK_EQ(Config.MapB[2], 4);
    AppleAgxFwDeriveTables(&Config);
    CHECK_EQ(Config.TableA.Count, 5);
    CHECK_EQ(Config.TableA.Hertz[0], 0);
    CHECK_EQ(Config.TableA.Hertz[3], 798000000);
    CHECK_EQ(Config.TableB.Count, 3);
    CHECK_EQ(Config.TableB.Hertz[1], 618000000);
    CHECK_EQ(Config.TableB.Hertz[2], 1100000000);
    CHECK_EQ(Config.Cap[0], 4);
    Config.Core.CoreId = APPLE_AGX_FW_CORE_G15X;
    Config.Core.Revision = 1;
    AppleAgxFwDeriveTables(&Config);
    CHECK_EQ(Config.Cap[0], 7);
    CHECK_EQ(Config.Cap[2], 5);
}

static void
TestDomain(void)
{
    ULONG64 Blob[2 + 2 * 2 + 1] = { 1, 2, 700000, 400000000, 900000, 800000000, 750000 };
    APPLE_AGX_FW_PSTATE_TABLE Table;

    CHECK(AppleAgxFwParseDomain((const UCHAR *)Blob, sizeof(Blob), 1, 16, &Table));
    CHECK_EQ(Table.Count, 2);
    CHECK_EQ(Table.Columns, 1);
    CHECK_EQ(Table.Hertz[1], 800000000);
    CHECK_EQ(Table.Millivolts[0][0], 700);
    CHECK_EQ(Table.SramMillivolts[0][0], 750);
    CHECK_EQ(Table.SramMillivolts[1][0], 900);
    CHECK(!AppleAgxFwParseDomain((const UCHAR *)Blob, sizeof(Blob), 2, 16, &Table));
    CHECK(!AppleAgxFwParseDomain((const UCHAR *)Blob, 0x30, 1, 16, &Table));
}

static UCHAR GroupMemory[APPLE_AGX_FW_GROUPS][0x90000] __attribute__((aligned(0x4000)));
static UCHAR RootMemory[APPLE_AGX_FW_PAGE], RegionAMemory[APPLE_AGX_FW_PAGE];
static UCHAR ScratchMemory[0x20000];

static void
BuildFw(APPLE_AGX_FW *Fw, APPLE_AGX_FW_CONFIG *Config)
{
    static const ULONG States[] = { 0, 600, 338000000, 650, 618000000, 700, 798000000, 750, 1100000000, 800 };
    ULONG Group, Index;

    memset(Fw, 0, sizeof(*Fw));
    memset(GroupMemory, 0, sizeof(GroupMemory));
    memset(RootMemory, 0, sizeof(RootMemory));
    memset(RegionAMemory, 0xa5, sizeof(RegionAMemory));
    memset(ScratchMemory, 0, sizeof(ScratchMemory));
    for (Group = 0; Group < APPLE_AGX_FW_GROUPS; ++Group)
    {
        CHECK(AppleAgxFwGroupSize(Group) <= sizeof(GroupMemory[Group]));
        Fw->Group[Group].Virtual = GroupMemory[Group];
        Fw->Group[Group].Address = 0xfffffc2c00000000ULL + Group * 0x100000ULL;
    }
    Fw->Root.Virtual = RootMemory;
    Fw->Root.Address = 0xfffffc2c00800000ULL;
    Fw->RegionA.Virtual = RegionAMemory;
    Fw->RegionA.Address = 0xfffffc2c00804000ULL;
    AppleAgxFwPlaceObjects(Fw);
    Fw->Scratch.Window0 = ScratchMemory;
    Fw->Scratch.Window1 = ScratchMemory + 0x14000;
    Fw->Scratch.Window0Address = 0xfffffc2c01000000ULL;
    Fw->Scratch.Window1Address = 0xfffffc2c01014000ULL;
    Fw->Scratch.Window0Size = 0x14000;
    Fw->Scratch.Window1Size = 0xc000;
    for (Index = 0; Index < APPLE_AGX_FW_DM_RINGS; ++Index)
        CHECK(AppleAgxFwScratchRing(&Fw->Scratch, NULL, APPLE_AGX_FW_DM_RECORD, &Fw->DataMaster[Index]));
    CHECK(AppleAgxFwScratchRing(&Fw->Scratch, &Fw->Object[APPLE_AGX_FW_DEVCTL_RING], APPLE_AGX_FW_DEVCTL_RECORD,
                                &Fw->DeviceControl));
    for (Index = 0; Index < APPLE_AGX_FW_IOMAPS; ++Index)
        Fw->IoMapAddress[Index] = 0xfffffc2010000000ULL + Index * 0x200000ULL;
    AppleAgxFwDefaults(Config);
    Config->ChipId = 0x6030;
    Config->GpuBase = 0x290000000ULL;
    Config->FenderBase = 0x290d00000ULL;
    CHECK(AppleAgxFwParseCore(0x07041100, 0x00010603, 0, 0x00000011, 0x3ffff, &Config->Core));
    Config->Features = 0x00022831a04709c7ULL;
    CHECK(AppleAgxFwRegions(Config));
    CHECK(AppleAgxFwParsePstates(4, 1, States, sizeof(States), NULL, 0, &Config->Base));
    for (Index = 1; Index < 5; ++Index)
    {
        Config->MapA[Index] = Index;
        Config->MapB[Index] = Index;
    }
    AppleAgxFwDeriveTables(Config);
    Config->SamplePeriodClocks = 200000;
    Config->SamplePeriodMs = 8;
    Config->GpuTtbatBase = 0x10823fc000ULL;
    Config->PbTableAddress = 0xfffffc2c00900000ULL;
}

static void
TestWrite(void)
{
    static APPLE_AGX_FW Fw;
    static APPLE_AGX_FW_CONFIG Config;
    PVOID Root, Runtime, Hw, Globals, Control, Power;
    ULONG Row, Column;

    BuildFw(&Fw, &Config);
    CHECK(Fw.Scratch.Window0Used <= Fw.Scratch.Window0Size);
    CHECK_EQ(Fw.DataMaster[0].ReadAddress, 0xfffffc2c01014000ULL);
    CHECK_EQ(Fw.DataMaster[0].CompletedAddress, 0xfffffc2c01014008ULL);
    CHECK_EQ(Fw.DataMaster[0].WriteAddress, 0xfffffc2c01000000ULL);
    CHECK_EQ(Fw.DataMaster[0].EntriesAddress, 0xfffffc2c01000008ULL);
    CHECK_EQ(Fw.DeviceControl.EntriesAddress, Fw.Object[APPLE_AGX_FW_DEVCTL_RING].Address);
    CHECK_EQ(AppleAgxFwWrite(&Fw, &Config), STATUS_SUCCESS);
    Root = Fw.Root.Virtual;
    Runtime = Fw.Object[APPLE_AGX_FW_RUNTIME].Virtual;
    Hw = Fw.Object[APPLE_AGX_FW_HWCFG].Virtual;
    Globals = Fw.Object[APPLE_AGX_FW_GLOBALS].Virtual;
    Control = Fw.Object[APPLE_AGX_FW_CONTROL].Virtual;
    Power = Fw.Object[APPLE_AGX_FW_POWER].Virtual;

    CHECK_EQ(Get64(Root, 0x00), 0x0c08e21e83800490ULL);
    CHECK_EQ(Get64(Root, 0x08), Fw.RegionA.Address);
    CHECK_EQ(Get64(Root, 0x18), Fw.Object[APPLE_AGX_FW_RUNTIME].Address);
    CHECK_EQ(Get64(Root, 0x20), Fw.Object[APPLE_AGX_FW_GLOBALS].Address);
    CHECK_EQ(Get64(Root, 0x28), 0x100000000ULL);
    CHECK_EQ(Get32(Root, 0x30), 0x030e4000);
    CHECK_EQ(Get32(Root, 0x34), 0x240e0e08);
    CHECK_EQ(Get32(Root, 0x38), 0x40000040);
    CHECK_EQ(Get64(Root, 0x3c), 1);
    CHECK_EQ(Get64(Root, 0x44), 0x3ffffffc000ULL);
    CHECK_EQ(Get64(Root, 0x4c), 0x3f000000000ULL);
    CHECK_EQ(Get32(Root, 0x54), 0x190e0e08);
    CHECK_EQ(Get64(Root, 0x6c), 0xffe000000ULL);
    CHECK_EQ(Get32(Root, 0x74), 0x0e0e0e08);
    CHECK_EQ(Get64(Root, 0x8c), 0x1ffc000ULL);
    CHECK_EQ(Get32(Root, 0x94), 0);
    CHECK_EQ(Get64(Root, 0xa8), Fw.Object[APPLE_AGX_FW_FLAGS].Address);
    CHECK_EQ(Get64(Root, 0xb0), Fw.Object[APPLE_AGX_FW_CONTROL].Address);
    CHECK_EQ(Get64(Root, 0xb8), Fw.Object[APPLE_AGX_FW_STATUS].Address);
    CHECK_EQ(Get64(RegionAMemory, 0), 0);
    CHECK_EQ(Get64(RegionAMemory, 8), 0);
    CHECK_EQ(Get64(RegionAMemory, 0x10), 0);
    CHECK_EQ(AppleAgxFwInitMessage(&Fw), 0x0081000000000000ULL | (Fw.Root.Address & 0xfffffffffffULL));

    CHECK_EQ(Get64(Runtime, 0x000), Fw.Object[APPLE_AGX_FW_HWCFG].Address);
    CHECK_EQ(Get64(Runtime, 0x008), Fw.Object[APPLE_AGX_FW_BRN].Address);
    CHECK_EQ(Get64(Runtime, 0x010), Fw.Object[APPLE_AGX_FW_MISC].Address);
    for (Row = 0; Row < 4; ++Row)
    {
        for (Column = 0; Column < 3; ++Column)
        {
            ULONG Base = 0x18 + Row * 0x60 + Column * 0x20;
            const APPLE_AGX_FW_SCRATCH_RING *Ring = &Fw.DataMaster[Column * 4 + Row];

            CHECK_EQ(Get64(Runtime, Base + 0x00), Ring->ReadAddress);
            CHECK_EQ(Get64(Runtime, Base + 0x08), Ring->CompletedAddress);
            CHECK_EQ(Get64(Runtime, Base + 0x10), Ring->WriteAddress);
            CHECK_EQ(Get64(Runtime, Base + 0x18), Ring->EntriesAddress);
        }
    }
    CHECK_EQ(Get64(Runtime, 0x198), Fw.DeviceControl.ReadAddress);
    CHECK_EQ(Get64(Runtime, 0x1b0), Fw.Object[APPLE_AGX_FW_DEVCTL_RING].Address);
    CHECK_EQ(Get64(Runtime, 0x1b8), Fw.Object[APPLE_AGX_FW_EVENT_STATE].Address);
    CHECK_EQ(Get64(Runtime, 0x1c0), Fw.Object[APPLE_AGX_FW_EVENT_RING].Address);
    CHECK_EQ(Get64(Runtime, 0x1c8), Fw.Object[APPLE_AGX_FW_LOG_STATE].Address);
    CHECK_EQ(Get64(Runtime, 0x1d0), Fw.Object[APPLE_AGX_FW_LOG_RING].Address);
    CHECK_EQ(Get64(Runtime, 0x1e8), Fw.Object[APPLE_AGX_FW_STATS_STATE].Address);
    CHECK_EQ(Get64(Runtime, 0x1f0), Fw.Object[APPLE_AGX_FW_STATS_RING].Address);
    CHECK_EQ(Get64(Runtime, 0x1f8), Fw.Object[APPLE_AGX_FW_KERNEL_LOG].Address);
    CHECK_EQ(Get64(Runtime, 0x234), Fw.Object[APPLE_AGX_FW_TA_ACTIVITY].Address);
    CHECK_EQ(Get64(Runtime, 0x23c), Fw.Object[APPLE_AGX_FW_FS_ACTIVITY].Address);
    CHECK_EQ(Get64(Runtime, 0x244), Fw.Object[APPLE_AGX_FW_CS_ACTIVITY].Address);
    CHECK_EQ(Get64(Runtime, 0x24c), Fw.Object[APPLE_AGX_FW_FAULT].Address);
    CHECK_EQ(Get64(Runtime, 0x2b0), Config.PbTableAddress);
    CHECK_EQ(Get64(Runtime, 0x2c0), 0);
    CHECK_EQ(Get32(Runtime, 0x2d0), 4);
    CHECK_EQ(((PUCHAR)Runtime)[0x3b0], 0xff);
    CHECK_EQ(Get64(Runtime, 0x441), Fw.Object[APPLE_AGX_FW_POWER].Address);

    CHECK_EQ(Get32(Control, 0x04), 1);
    CHECK_EQ(Get64(Control, 0x4568), Fw.Object[APPLE_AGX_FW_FWCTL_STATE].Address);
    CHECK_EQ(Get64(Control, 0x4570), Fw.Object[APPLE_AGX_FW_FWCTL_RING].Address);
    CHECK_EQ(Get32(Control, 0x45c4), 1);
    CHECK_EQ(Get32(Fw.Object[APPLE_AGX_FW_FLAGS].Virtual, 4), 1);
    CHECK_EQ(Get32(Fw.Object[APPLE_AGX_FW_FS_ACTIVITY].Virtual, 0xc18), 0xffffffff);

    CHECK_EQ(Get64(Hw, 0x00), 0x6f00000000ULL);
    CHECK_EQ(Get64(Hw, 0x20), 0x2ffffff8000ULL);
    CHECK_EQ(Get64(Hw, 0x28), 0xfffffc2011800000ULL);
    CHECK_EQ(Get64(Hw, 0x640 + 11 * 0x20), 0x220000000ULL);
    CHECK_EQ(Get64(Hw, 0x648 + 11 * 0x20), Fw.IoMapAddress[11]);
    CHECK_EQ(Get32(Hw, 0x650 + 11 * 0x20), 8 * 0x98000);
    CHECK_EQ(Get32(Hw, 0x654 + 11 * 0x20), 0x98000);
    CHECK_EQ(Get32(Hw, 0x658 + 11 * 0x20), 1);
    CHECK_EQ(Get64(Hw, 0x648 + 6 * 0x20), 0);
    CHECK_EQ(Get32(Hw, 0x658 + 6 * 0x20), 1);
    CHECK_EQ(Get32(Hw, 0x658 + 12 * 0x20), 0);
    CHECK_EQ(Get32(Hw, 0xa38), 1);
    CHECK_EQ(Get32(Hw, 0xa3c), 1);
    CHECK_EQ(Get32(Hw, 0xa44), 0);
    CHECK_EQ(Get32(Hw, 0xa58), 1);
    CHECK_EQ(Get32(Hw, 0xa5c), 0);
    CHECK_EQ(Get32(Hw, 0xa68), 24000);
    CHECK_EQ(Get32(Hw, 0xa6c), 1);
    CHECK_EQ(Get32(Hw, 0xa70), 8);
    CHECK_EQ(Get32(Hw, 0xa9c), 0x1f);
    CHECK_EQ(Get64(Hw, 0xabc), 4);
    CHECK_EQ(Get32(Hw, 0xacc), 1);
    CHECK_EQ(Get32(Hw, 0xae4), 0x31);
    CHECK_EQ(Get32(Hw, 0xb04), 1);
    CHECK_EQ(Get32(Hw, 0xb20), 18);
    CHECK_EQ(Get64(Hw, 0xb24), 0xff);
    CHECK_EQ(Get32(Hw, 0xb40), 1);
    CHECK_EQ(Get64(Hw, 0xb44), 0x10823fc000ULL);
    CHECK_EQ(Get64(Hw, 0xb4c), 0x400000018ULL);
    CHECK_EQ(Get32(Hw, 0xb58), 4);
    CHECK_EQ(Get32(Hw, 0xb5c + 4 * 4), 1100);
    CHECK_EQ(Get32(Hw, 0xb9c + 4 * 0x20 + 7 * 4), 800);
    CHECK_EQ(Get32(Hw, 0xf9c + 1 * 4), 338);
    CHECK_EQ(Get32(Hw, 0x10dc + 2 * 4), 2);
    CHECK_EQ(Get32(Hw, 0x134c), 4);
    CHECK_EQ(Get32(Hw, 0x1350 + 2 * 4), 618);
    CHECK_EQ(Get32(Hw, 0x115c), 0);
    CHECK_EQ(Get32(Hw, 0x17b8), 5);
    CHECK_EQ(Get32(Hw, 0x17c0), 1);
    CHECK_EQ(Get32(Hw, 0x17fc), 1);
    CHECK_EQ(Get32(Hw, 0x1808), 1);
    CHECK_EQ(Get64(Hw, 0x1810), 0x100000001ULL);
    CHECK_EQ(Get32(Hw, 0x1818), 1);
    CHECK_EQ(Get64(Hw, 0x1820), 0xffffffffffffffffULL);
    CHECK_EQ(Get64(Hw, 0x1848), 0xffffffffffffffffULL);
    CHECK_EQ(Get32(Hw, 0x1850), 0);
    CHECK_EQ(Get32(Hw, 0x1860), 1);

    CHECK_EQ(Get32(Globals, 0x00), 0);
    CHECK_EQ(Get32(Globals, 0x20), 3000);
    CHECK_EQ(Get32(Globals, 0x2c), 1);
    CHECK_EQ(Get32(Globals, 0x30), 1);
    CHECK_EQ(Get32(Globals, 0x38), 0x78);
    CHECK_EQ(Get32(Globals, 0x50), 0x0028ffff);
    CHECK_EQ(Get32(Globals, 0x5a), 1);
    CHECK_EQ(Get32(Globals, 0x8c), 400);
    CHECK_EQ(Get32(Globals, 0x7a8), 700);
    CHECK_EQ(Get32(Globals, 0x7cc), 6);
    CHECK_EQ(Get32(Globals, 0x7d8), 100);
    CHECK_EQ(Get32(Globals, 0x9b0), 40);
    CHECK_EQ(Get32(Globals, 0x9b4), 50);
    CHECK_EQ(((PUCHAR)Globals)[0x9bc], 4);

    CHECK_EQ(Get32(Power, 0x04), 200000);
    CHECK_EQ(Get64(Power, 0x0c), 0x400000000ULL);
    CHECK(GetFloat(Power, 0x66c) > 0.0031f && GetFloat(Power, 0x66c) < 0.0032f);
    CHECK_EQ(Get32(Power, 0x6a4), 0xfe6);
    CHECK_EQ(Get32(Power, 0x698), 400);
    CHECK_EQ(Get32(Power, 0x7a4), 1);
    CHECK_EQ(Get32(Power, 0x7bc), 0x40fca970);
    CHECK_EQ(Get32(Power, 0x7d0), 0x40db53d0);
    CHECK_EQ(Get32(Power, 0x7d8), 300);
    CHECK_EQ(Get32(Power, 0x7dc), 400);
    CHECK_EQ(Get32(Power, 0x4298), 1);
    CHECK_EQ(Get32(Power, 0x429c), 1);
}

static void
TestRings(void)
{
    static APPLE_AGX_FW Fw;
    static APPLE_AGX_FW_CONFIG Config;
    UCHAR Record[APPLE_AGX_FW_DM_RECORD] = { 0x11 }, Event[APPLE_AGX_FW_EVENT_RECORD];
    PAPPLE_AGX_FW_SCRATCH_RING Ring;
    PVOID State, Records;
    ULONG Slot, Index;

    BuildFw(&Fw, &Config);
    Ring = &Fw.DataMaster[0];
    CHECK_EQ(AppleAgxFwPost(Ring, Record, &Slot), STATUS_SUCCESS);
    CHECK_EQ(Slot, 1);
    CHECK_EQ(*Ring->Write, 1);
    CHECK_EQ(((PUCHAR)Ring->Entries)[0], 0x11);
    *Ring->Write = 7;
    CHECK_EQ(AppleAgxFwPost(Ring, Record, &Slot), STATUS_DATA_ERROR);
    *Ring->Write = 1;
    for (Index = 1; Index < APPLE_AGX_FW_RING_SLOTS - 1; ++Index)
        CHECK_EQ(AppleAgxFwPost(Ring, Record, NULL), STATUS_SUCCESS);
    CHECK_EQ(AppleAgxFwPost(Ring, Record, NULL), STATUS_DEVICE_BUSY);
    *Ring->Read = 5;
    CHECK_EQ(AppleAgxFwPost(Ring, Record, NULL), STATUS_SUCCESS);
    CHECK_EQ(*Ring->Write, 0);

    State = Fw.Object[APPLE_AGX_FW_EVENT_STATE].Virtual;
    Records = Fw.Object[APPLE_AGX_FW_EVENT_RING].Virtual;
    CHECK(!AppleAgxFwTake(State, Records, APPLE_AGX_FW_EVENT_RECORD, Event));
    ((PUCHAR)Records)[0] = 0x42;
    memcpy((PUCHAR)State + APPLE_AGX_FW_STATE_WRITE, &(ULONG){ 1 }, sizeof(ULONG));
    CHECK(AppleAgxFwTake(State, Records, APPLE_AGX_FW_EVENT_RECORD, Event));
    CHECK_EQ(Event[0], 0x42);
    CHECK_EQ(Get32(State, APPLE_AGX_FW_STATE_READ), 1);
    CHECK(!AppleAgxFwTake(State, Records, APPLE_AGX_FW_EVENT_RECORD, Event));
}

int
main(void)
{
    TestLayout();
    TestCore();
    TestRegions();
    TestPstates();
    TestDomain();
    TestWrite();
    TestRings();
    printf("appleagx_fw: %u checks, %u failures\n", Checks, Failures);
    return Failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
