/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unit tests for the Apple silicon CPU cluster performance-state driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <ndk/ntndk.h>
#include <libernt/fdtlib.h>

#define UNIT_TEST

static ULONG64 SimData[0x100 / sizeof(ULONG64)];
static ULONG SimBusyAfterWrite;
static ULONG SimBusyLeft;
static BOOLEAN SimBusyForever;
static ULONG SimLevelCap;
static ULONG SimCommandWrites;
static ULONG SimStalledMicroseconds;

#define SimBase ((PUCHAR)SimData)
#define SimCommand SimData[0x20 / sizeof(ULONG64)]
#define SimStatus (*(volatile ULONG *)(SimBase + 0x50))

static
ULONG64
SimRead64(volatile ULONG64 *Address)
{
    ULONG64 Value = *Address;

    if ((PUCHAR)Address == SimBase + 0x20 && (SimBusyForever || SimBusyLeft))
    {
        if (SimBusyLeft)
            --SimBusyLeft;
        Value |= 0x80000000ULL;
    }
    return Value;
}

static
VOID
SimWrite64(volatile ULONG64 *Address, ULONG64 Value)
{
    ULONG Level = (ULONG)(Value & 0x1F);

    *Address = Value;
    if ((PUCHAR)Address != SimBase + 0x20)
        return;
    ++SimCommandWrites;
    if (!(Value & 0x02000000ULL))
        return;
    SimStatus = (((SimLevelCap && Level > SimLevelCap) ? SimLevelCap : Level) << 5) | Level;
    SimBusyLeft = SimBusyAfterWrite;
}

#define READ_REGISTER_ULONG64(Address) SimRead64(Address)
#define WRITE_REGISTER_ULONG64(Address, Value) SimWrite64(Address, Value)
#define READ_REGISTER_ULONG(Address) (*(Address))
#define KeStallExecutionProcessor(Microseconds) (SimStalledMicroseconds += (Microseconds))

#include "../../../../drivers/platform/applecpufreq/cluster.c"
#include "../hal/fdtbuild.h"

typedef struct _TEST_OPP
{
    ULONG Level;
    ULONG Hz;
    ULONG LatencyNs;
} TEST_OPP;

static const TEST_OPP J516sPerformanceStates[] =
{
    { 1, 0x297c1e00, 0x2198 }, { 2, 0x41169900, 0x490c }, { 3, 0x50d2eb00, 0x50dc }, { 4, 0x5f210700, 0x60e0 },
    { 5, 0x704b8f00, 0x77ec }, { 6, 0x81761700, 0x8660 }, { 7, 0x907b4e00, 0x9ab0 }, { 8, 0x9becfe00, 0x9c40 },
    { 9, 0xa75eae00, 0xa348 }, { 10, 0xb2194300, 0xb0f4 }, { 11, 0xbb65a200, 0xb66c }, { 12, 0xc3fae600, 0xc030 },
    { 13, 0xcbd90f00, 0xd28c }, { 14, 0xd5256e00, 0xd1c4 }, { 15, 0xd801da00, 0xd2f0 }, { 16, 0xdd039700, 0xd2f0 },
    { 17, 0xe14e3900, 0xd354 }, { 18, 0xe64ff600, 0xd41c }, { 19, 0xec08ce00, 0xe420 }, { 20, 0xf1c1a600, 0xe54c },
};

static const TEST_OPP J516sEfficiencyStates[] =
{
    { 1, 0x2c588a00, 0x1e14 }, { 2, 0x3e3a2d00, 0x52d0 }, { 3, 0x57f9f900, 0x6720 }, { 4, 0x77729d00, 0x9e34 },
    { 5, 0x872eef00, 0xad70 }, { 6, 0x91e98400, 0xb860 }, { 7, 0x9d5b3400, 0xcb20 }, { 8, 0xa3cb2700, 0xcb20 },
};

static
VOID
OpenTree(PFDT Fdt)
{
    FdtFinish();
    ok(FdtOpen(Builder.Blob, Builder.BlobSize, Fdt), "the test tree does not open\n");
}

static
ULONG
FindNode(const FDT *Fdt, const CHAR *Path)
{
    ULONG Node = FDT_NO_NODE;

    ok(FdtResolvePath(Fdt, Path, &Node, NULL, NULL), "%s is missing from the test tree\n", Path);
    return Node;
}

static
VOID
AddOpp(const CHAR *Name, ULONG Level, ULONG Hz, ULONG LatencyNs)
{
    FdtBeginNode(Name);
    FdtCells("opp-hz", 2, 0, Hz);
    FdtCells("opp-level", 1, Level);
    FdtCells("clock-latency-ns", 1, LatencyNs);
    FdtEndNode();
}

static
VOID
BuildTable(const TEST_OPP *States, ULONG Count, BOOLEAN Reverse)
{
    CHAR Name[16];
    ULONG Index, Entry;

    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtBeginNode("opp-table");
    FdtStrings("compatible", LIST("operating-points-v2"));
    for (Index = 0; Index < Count; ++Index)
    {
        Entry = Reverse ? Count - 1 - Index : Index;
        sprintf(Name, "opp%02lu", States[Entry].Level);
        AddOpp(Name, States[Entry].Level, States[Entry].Hz, States[Entry].LatencyNs);
    }
    FdtEndNode();
    FdtEndNode();
}

static
VOID
CheckTable(const TEST_OPP *States, ULONG Count, BOOLEAN Reverse)
{
    CPUFREQ_TABLE Table;
    NTSTATUS Status;
    ULONG Index;
    FDT Fdt;

    BuildTable(States, Count, Reverse);
    OpenTree(&Fdt);
    Status = CpufreqParseTable(&Fdt, FindNode(&Fdt, "/opp-table"), &Table);
    ok(Status == STATUS_SUCCESS, "parse: %lx\n", Status);
    ok(Table.Count == Count, "%lu states instead of %lu\n", Table.Count, Count);
    for (Index = 0; Index < Count && Index < Table.Count; ++Index)
    {
        ok(Table.State[Index].Level == States[Index].Level, "state %lu has level %lu\n", Index,
           Table.State[Index].Level);
        ok(Table.State[Index].Hz == States[Index].Hz, "state %lu runs at %I64u Hz\n", Index, Table.State[Index].Hz);
        ok(Table.State[Index].LatencyNs == States[Index].LatencyNs, "state %lu switches in %lu ns\n", Index,
           Table.State[Index].LatencyNs);
    }
    ok(CpufreqFindLevel(&Table, States[Count - 1].Level) == &Table.State[Count - 1], "the top level is not found\n");
    ok(CpufreqFindLevel(&Table, 0) == NULL, "level 0 is found\n");
    ok(CpufreqFindLevel(&Table, States[Count - 1].Level + 1) == NULL, "a level above the table is found\n");
}

static
VOID
TestTable(VOID)
{
    CheckTable(J516sPerformanceStates, RTL_NUMBER_OF(J516sPerformanceStates), FALSE);
    CheckTable(J516sPerformanceStates, RTL_NUMBER_OF(J516sPerformanceStates), TRUE);
    CheckTable(J516sEfficiencyStates, RTL_NUMBER_OF(J516sEfficiencyStates), FALSE);
}

static
NTSTATUS
ParseSmallTable(ULONG Level1, ULONG Hz1, ULONG Level2, ULONG Hz2, BOOLEAN Compatible)
{
    CPUFREQ_TABLE Table;
    FDT Fdt;

    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtBeginNode("opp-table");
    if (Compatible)
        FdtStrings("compatible", LIST("operating-points-v2"));
    AddOpp("opp01", Level1, Hz1, 1000);
    AddOpp("opp02", Level2, Hz2, 1000);
    FdtEndNode();
    FdtEndNode();
    OpenTree(&Fdt);
    return CpufreqParseTable(&Fdt, FindNode(&Fdt, "/opp-table"), &Table);
}

static
VOID
TestTableErrors(VOID)
{
    CPUFREQ_TABLE Table;
    NTSTATUS Status;
    FDT Fdt;

    Status = ParseSmallTable(1, 1000000, 2, 2000000, TRUE);
    ok(Status == STATUS_SUCCESS, "a valid table: %lx\n", Status);
    Status = ParseSmallTable(1, 1000000, 2, 2000000, FALSE);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a table that is not operating-points-v2: %lx\n", Status);
    Status = ParseSmallTable(2, 1000000, 2, 2000000, TRUE);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a repeated level: %lx\n", Status);
    Status = ParseSmallTable(0, 1000000, 2, 2000000, TRUE);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "level 0: %lx\n", Status);
    Status = ParseSmallTable(1, 1000000, 32, 2000000, TRUE);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "level 32: %lx\n", Status);
    Status = ParseSmallTable(1, 2000000, 2, 1000000, TRUE);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a higher level that runs slower: %lx\n", Status);
    Status = ParseSmallTable(1, 0, 2, 1000000, TRUE);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a state at 0 Hz: %lx\n", Status);

    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtBeginNode("opp-table");
    FdtStrings("compatible", LIST("operating-points-v2"));
    FdtEndNode();
    FdtBeginNode("opp-narrow");
    FdtStrings("compatible", LIST("operating-points-v2"));
    FdtBeginNode("opp01");
    FdtCells("opp-hz", 1, 1000000);
    FdtCells("opp-level", 1, 1);
    FdtEndNode();
    FdtEndNode();
    FdtEndNode();
    OpenTree(&Fdt);
    Status = CpufreqParseTable(&Fdt, FindNode(&Fdt, "/opp-table"), &Table);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "an empty table: %lx\n", Status);
    Status = CpufreqParseTable(&Fdt, FindNode(&Fdt, "/opp-narrow"), &Table);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a 32-bit opp-hz: %lx\n", Status);
}

static
VOID
AddCpu(const CHAR *Name, ULONG Mpidr, ULONG Table, ULONG Domain)
{
    FdtBeginNode(Name);
    FdtStrings("device_type", LIST("cpu"));
    FdtCells("reg", 2, 0, Mpidr);
    FdtCells("operating-points-v2", 1, Table);
    FdtCells("performance-domains", 1, Domain);
    FdtEndNode();
}

static
VOID
AddCluster(const CHAR *Name, ULONG Phandle, ULONG DomainCells)
{
    FdtBeginNode(Name);
    FdtStrings("compatible", LIST("apple,t6030-cluster-cpufreq\0apple,t8112-cluster-cpufreq"));
    FdtCells("#performance-domain-cells", 1, DomainCells);
    FdtCells("phandle", 1, Phandle);
    FdtEndNode();
}

static
VOID
TestCpus(VOID)
{
    CPUFREQ_CPUS Cpus;
    NTSTATUS Status;
    FDT Fdt;

    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 2);
    FdtBeginNode("cpus");
    FdtCells("#address-cells", 1, 2);
    FdtCells("#size-cells", 1, 0);
    FdtBeginNode("cpu-map");
    FdtEndNode();
    AddCpu("cpu@0", 0x0, 0x0e, 0x0f);
    AddCpu("cpu@1", 0x1, 0x0e, 0x0f);
    AddCpu("cpu@10100", 0x10100, 0x11, 0x12);
    AddCpu("cpu@10101", 0x10101, 0x11, 0x12);
    AddCpu("cpu@20000", 0x20000, 0x15, 0x14);
    AddCpu("cpu@20001", 0x20001, 0x16, 0x14);
    FdtEndNode();
    FdtBeginNode("soc");
    AddCluster("cpufreq@210e20000", 0x0f, 0);
    AddCluster("cpufreq@211e20000", 0x12, 0);
    AddCluster("cpufreq@212e20000", 0x13, 0);
    AddCluster("cpufreq@213e20000", 0x14, 0);
    AddCluster("cpufreq@214e20000", 0x0f, 1);
    FdtEndNode();
    FdtEndNode();
    OpenTree(&Fdt);

    Status = CpufreqFindCpus(&Fdt, FindNode(&Fdt, "/soc/cpufreq@210e20000"), &Cpus);
    ok(Status == STATUS_SUCCESS, "efficiency cluster: %lx\n", Status);
    ok(Cpus.Count == 2 && Cpus.First == 0 && Cpus.Last == 1 && Cpus.TablePhandle == 0x0e,
       "efficiency cluster: %lu CPUs %I64x..%I64x, table %lx\n", Cpus.Count, Cpus.First, Cpus.Last, Cpus.TablePhandle);
    Status = CpufreqFindCpus(&Fdt, FindNode(&Fdt, "/soc/cpufreq@211e20000"), &Cpus);
    ok(Status == STATUS_SUCCESS, "performance cluster: %lx\n", Status);
    ok(Cpus.Count == 2 && Cpus.First == 0x10100 && Cpus.Last == 0x10101 && Cpus.TablePhandle == 0x11,
       "performance cluster: %lu CPUs %I64x..%I64x, table %lx\n", Cpus.Count, Cpus.First, Cpus.Last,
       Cpus.TablePhandle);
    Status = CpufreqFindCpus(&Fdt, FindNode(&Fdt, "/soc/cpufreq@212e20000"), &Cpus);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a cluster without CPUs: %lx\n", Status);
    Status = CpufreqFindCpus(&Fdt, FindNode(&Fdt, "/soc/cpufreq@213e20000"), &Cpus);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a cluster whose CPUs disagree on the table: %lx\n", Status);
    Status = CpufreqFindCpus(&Fdt, FindNode(&Fdt, "/soc/cpufreq@214e20000"), &Cpus);
    ok(Status == STATUS_NOT_SUPPORTED, "a cluster with domain arguments: %lx\n", Status);
}

static
VOID
ResetSim(ULONG64 Command, ULONG Current, ULONG Target)
{
    memset(SimData, 0, sizeof(SimData));
    SimCommand = Command;
    SimStatus = (Current << 5) | Target;
    SimBusyAfterWrite = 0;
    SimBusyLeft = 0;
    SimBusyForever = FALSE;
    SimLevelCap = 0;
    SimCommandWrites = 0;
    SimStalledMicroseconds = 0;
}

static
VOID
TestRequest(VOID)
{
    CPUFREQ_READING Reading;
    NTSTATUS Status;
    ULONG Polls;

    ResetSim((1ULL << 42) | 5, 5, 5);
    SimStatus |= 0xFFFFFC00;
    CpufreqRead(SimBase, &Reading);
    ok(Reading.Command == ((1ULL << 42) | 5), "command %I64x\n", Reading.Command);
    ok(Reading.Current == 5 && Reading.Target == 5, "state %lu, target %lu\n", Reading.Current, Reading.Target);
    SimStatus = (7 << 5) | 31;
    CpufreqRead(SimBase, &Reading);
    ok(Reading.Current == 7 && Reading.Target == 31, "state %lu, target %lu\n", Reading.Current, Reading.Target);

    ResetSim((1ULL << 42) | 5, 5, 5);
    SimBusyAfterWrite = 3;
    Status = CpufreqRequest(SimBase, 20, &Reading, &Polls);
    ok(Status == STATUS_SUCCESS, "request: %lx\n", Status);
    ok(SimCommandWrites == 1, "%lu command writes\n", SimCommandWrites);
    ok(SimCommand == ((1ULL << 42) | 0x02000000ULL | 20), "command %I64x\n", SimCommand);
    ok(Reading.Current == 20 && Reading.Target == 20, "state %lu, target %lu\n", Reading.Current, Reading.Target);
    ok(Polls == 3 && SimStalledMicroseconds == 6, "%lu polls, %lu us\n", Polls, SimStalledMicroseconds);

    Status = CpufreqRequest(SimBase, 1, &Reading, &Polls);
    ok(Status == STATUS_SUCCESS, "request: %lx\n", Status);
    ok(SimCommand == ((1ULL << 42) | 0x02000000ULL | 1), "command %I64x\n", SimCommand);
    ok(Reading.Current == 1 && Reading.Target == 1, "state %lu, target %lu\n", Reading.Current, Reading.Target);

    ResetSim(5, 5, 5);
    SimBusyForever = TRUE;
    Status = CpufreqRequest(SimBase, 20, &Reading, &Polls);
    ok(Status == STATUS_DEVICE_BUSY, "request while busy: %lx\n", Status);
    ok(SimCommandWrites == 0, "%lu command writes while busy\n", SimCommandWrites);
    ok(SimStalledMicroseconds == 400, "waited %lu us\n", SimStalledMicroseconds);
    ok(Reading.Current == 5, "state %lu\n", Reading.Current);

    ResetSim(5, 5, 5);
    SimBusyAfterWrite = MAXULONG;
    Status = CpufreqRequest(SimBase, 20, &Reading, &Polls);
    ok(Status == STATUS_IO_TIMEOUT, "request that never completes: %lx\n", Status);
    ok(SimCommandWrites == 1 && Polls == 200, "%lu command writes, %lu polls\n", SimCommandWrites, Polls);

    ResetSim(5, 5, 5);
    SimLevelCap = 18;
    Status = CpufreqRequest(SimBase, 20, &Reading, &Polls);
    ok(Status == STATUS_SUCCESS, "capped request: %lx\n", Status);
    ok(Reading.Current == 18 && Reading.Target == 20, "state %lu, target %lu\n", Reading.Current, Reading.Target);
}

START_TEST(AppleCpufreq)
{
    TestTable();
    TestTableErrors();
    TestCpus();
    TestRequest();
}
