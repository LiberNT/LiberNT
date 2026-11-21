/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Processors the device tree describes, and their spin-table release
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <halacpi_arm64.h>
#include <reactos/arm64/sysreg.h>
#include "devtree.h"

#define NDEBUG
#include <debug.h>

#define ARM64_DCACHE_LINE_SIZE 64

ULONG HalpStartedProcessorCount = 1;
ULONG64 HalpArm64SpinTableRelease[MAXIMUM_PROCESSORS];

VOID
HalpArm64SmpCleanDcacheRange(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Start = (ULONG_PTR)Base & ~(ARM64_DCACHE_LINE_SIZE - 1);
    ULONG_PTR End = ((ULONG_PTR)Base + Length + ARM64_DCACHE_LINE_SIZE - 1) &
                    ~(ARM64_DCACHE_LINE_SIZE - 1);

    for (ULONG_PTR Addr = Start; Addr < End; Addr += ARM64_DCACHE_LINE_SIZE)
        ARM64_DC(CVAC, Addr);

    ARM64_DSB(SY);
}

static
BOOLEAN
HalpArm64ReadDeviceTreeCpu(
    _In_ PFDT Fdt,
    _In_ ULONG Node,
    _In_ ULONG AddressCells,
    _Out_ PULONG64 Mpidr,
    _Out_ PULONG64 Release)
{
    const VOID *Property;
    ULONG Length;

    *Release = 0;
    Property = FdtGetProperty(Fdt, Node, "device_type", &Length);
    if (!FdtStringListContains(Property, Length, "cpu"))
        return FALSE;
    Property = FdtGetProperty(Fdt, Node, "status", &Length);
    if (Property && !FdtStringListContains(Property, Length, "okay") &&
        !FdtStringListContains(Property, Length, "ok"))
        return FALSE;
    Property = FdtGetProperty(Fdt, Node, "reg", &Length);
    if (!FdtReadCells(Property, Length, 0, AddressCells, Mpidr))
        return FALSE;
    Property = FdtGetProperty(Fdt, Node, "enable-method", &Length);
    if (FdtStringListContains(Property, Length, "spin-table"))
    {
        Property = FdtGetProperty(Fdt, Node, "cpu-release-addr", &Length);
        if (!FdtReadCells(Property, Length, 0, (Length >= 8) ? 2 : 1, Release))
            *Release = 0;
    }
    return TRUE;
}

static
VOID
HalpArm64DiscoverDeviceTreePsci(
    _In_ PFDT Fdt)
{
    static const PCSTR Compatibles[] = { "arm,psci-1.0", "arm,psci-0.2" };
    const VOID *Method;
    ULONG Node, Parent, Match, Length;

    if (HalpArm64PsciInfo.Present ||
        !HalpArm64FindDeviceTreeNode(Fdt, Compatibles, RTL_NUMBER_OF(Compatibles), &Node, &Parent, &Match))
        return;
    Method = FdtGetProperty(Fdt, Node, "method", &Length);
    if (FdtStringListContains(Method, Length, "hvc"))
        HalpArm64PsciInfo.UseHvc = TRUE;
    else if (!FdtStringListContains(Method, Length, "smc"))
        return;
    HalpArm64PsciInfo.Present = TRUE;
    DPRINT1("[arm64][HAL] Device tree: PSCI through %s\n", HalpArm64PsciInfo.UseHvc ? "HVC" : "SMC");
}

VOID
HalpArm64DiscoverDeviceTreeCpus(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    FDT Fdt;
    ULONG Cpus, Node, AddressCells, SizeCells, Count = 1, SpinTable = 0;
    ULONG64 Self = ARM64_READ_SYSREG(MPIDR_EL1) & 0xFF00FFFFFFULL, Mpidr, Release;

    if (HalpArm64GicInfo.GiccEntryCount || !HalpArm64OpenDeviceTree(LoaderBlock, &Fdt) ||
        !FdtFindNode(&Fdt, "/cpus", &Cpus, NULL))
        return;
    HalpArm64DiscoverDeviceTreePsci(&Fdt);
    FdtGetCellCounts(&Fdt, Cpus, &AddressCells, &SizeCells);
    if (AddressCells == 0 || AddressCells > 2)
        return;
    RtlZeroMemory(&HalpArm64GicInfo.GiccEntries[0], sizeof(HalpArm64GicInfo.GiccEntries[0]));
    HalpArm64GicInfo.GiccEntries[0].Mpidr = Self;
    HalpArm64GicInfo.GiccEntries[0].Flags = 1;
    for (Node = FdtFirstChild(&Fdt, Cpus); Node != FDT_NO_NODE && Count < MAXIMUM_PROCESSORS;
         Node = FdtNextSibling(&Fdt, Node))
    {
        if (!HalpArm64ReadDeviceTreeCpu(&Fdt, Node, AddressCells, &Mpidr, &Release) ||
            (Mpidr & 0xFF00FFFFFFULL) == Self)
            continue;
        RtlZeroMemory(&HalpArm64GicInfo.GiccEntries[Count], sizeof(HalpArm64GicInfo.GiccEntries[Count]));
        HalpArm64GicInfo.GiccEntries[Count].Mpidr = Mpidr;
        HalpArm64GicInfo.GiccEntries[Count].Flags = (HalpArm64PsciInfo.Present || Release) ? 1 : 0;
        HalpArm64SpinTableRelease[Count] = Release;
        SpinTable += Release ? 1 : 0;
        Count++;
    }
    HalpArm64GicInfo.GiccEntryCount = Count;
    DPRINT1("[arm64][HAL] Device tree: %lu CPUs, %lu released through spin tables\n", Count, SpinTable);
}

BOOLEAN
HalpArm64ReleaseSpinTableCpu(
    _In_ ULONG ProcessorNumber,
    _In_ UINT64 EntryPoint)
{
    PHYSICAL_ADDRESS Physical;
    volatile UINT64 *Release;

    if (ProcessorNumber >= MAXIMUM_PROCESSORS || !HalpArm64SpinTableRelease[ProcessorNumber])
        return FALSE;
    Physical.QuadPart = (LONGLONG)HalpArm64SpinTableRelease[ProcessorNumber];
    Release = MmMapIoSpace(Physical, sizeof(UINT64), MmCached);
    if (!Release)
        return FALSE;
    *Release = EntryPoint;
    HalpArm64SmpCleanDcacheRange((PVOID)Release, sizeof(UINT64));
    ARM64_DSB(SY);
    ARM64_SEV();
    MmUnmapIoSpace((PVOID)Release, sizeof(UINT64));
    DPRINT1("[arm64][HAL] Released CPU %lu (MPIDR 0x%llx) at %llx into the trampoline\n", ProcessorNumber,
            HalpArm64GicInfo.GiccEntries[ProcessorNumber].Mpidr, HalpArm64SpinTableRelease[ProcessorNumber]);
    return TRUE;
}
