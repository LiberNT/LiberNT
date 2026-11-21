/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 platform devices: GICv2, generic timer and device tree lookup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <libernt/fdtlib.h>
#define NDEBUG
#include <debug.h>

/* GICv2 architecture specification (IHI 0048B) register offsets. */
#define GICD_CTLR       0x000
#define GICD_TYPER      0x004
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_ICPENDR    0x280
#define GICD_ICACTIVER  0x380
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICD_ICFGR      0xC00
#define GICD_SGIR       0xF00
#define GICC_CTLR       0x000
#define GICC_PMR        0x004
#define GICC_BPR        0x008
#define GICC_IAR        0x00C
#define GICC_EOIR       0x010
#define GICC_INTID_MASK 0x3FF
#define GIC_SPURIOUS    1020
#define GIC_MAX_INTID   1020

/* Generic timer (ARM ARM B8): virtual timer control. */
#define CNT_CTL_ENABLE  0x1
#define CNT_CTL_IMASK   0x2
#define CNT_CTL_ISTATUS 0x4

#define KI_ARM_CLOCK_INCREMENT 100000UL

static PUCHAR KiArmGicDistributor;
static PUCHAR KiArmGicCpuInterface;
static ULONG KiArmGicLines;
static KIRQL KiArmInterruptIrql[GIC_MAX_INTID];
static ULONG KiArmTimerInterrupt = 27;
static ULONG KiArmTimerFrequency;
static ULONG KiArmTimerReload;
static BOOLEAN KiArmPlatformReady;
static ULONG KiArmInterruptSource[MAXIMUM_PROCESSORS];
static FDT KiArmDeviceTree;
static BOOLEAN KiArmDeviceTreeValid;

static
BOOLEAN
KiArmOpenDeviceTree(VOID)
{
    PARM_LOADER_BLOCK ArmBlock;

    if (KiArmDeviceTreeValid)
        return TRUE;
    if (!KeLoaderBlock)
        return FALSE;

    ArmBlock = &KeLoaderBlock->u.Arm;
    if (!ArmBlock->DeviceTree || !ArmBlock->DeviceTreeSize)
        return FALSE;

    KiArmDeviceTreeValid = FdtOpen((PVOID)(ULONG_PTR)ArmBlock->DeviceTree, ArmBlock->DeviceTreeSize, &KiArmDeviceTree);
    return KiArmDeviceTreeValid;
}

static
BOOLEAN
KiArmFindCompatibleIn(
    _In_ ULONG Parent,
    _In_ const CHAR * const *Compatible,
    _Out_ PULONG Node,
    _Out_ PULONG NodeParent)
{
    const VOID *Property;
    ULONG Child, Length, Index;

    for (Child = FdtFirstChild(&KiArmDeviceTree, Parent);
         Child != FDT_NO_NODE;
         Child = FdtNextSibling(&KiArmDeviceTree, Child))
    {
        Property = FdtGetProperty(&KiArmDeviceTree, Child, "compatible", &Length);
        for (Index = 0; Property && Compatible[Index]; Index++)
        {
            if (FdtStringListContains(Property, Length, Compatible[Index]))
            {
                *Node = Child;
                *NodeParent = Parent;
                return TRUE;
            }
        }
        if (KiArmFindCompatibleIn(Child, Compatible, Node, NodeParent))
            return TRUE;
    }
    return FALSE;
}

BOOLEAN
NTAPI
KiArmFindDevice(
    _In_ const CHAR * const *Compatible,
    _In_ ULONG RegIndex,
    _Out_ PPHYSICAL_ADDRESS Address,
    _Out_opt_ PULONG Length)
{
    ULONGLONG Base, Size;
    ULONG Node, Parent, Root;

    if (!KiArmOpenDeviceTree())
        return FALSE;
    Root = FdtRootNode(&KiArmDeviceTree);
    if ((Root == FDT_NO_NODE) || !KiArmFindCompatibleIn(Root, Compatible, &Node, &Parent) ||
        !FdtReadReg(&KiArmDeviceTree, Node, Parent, RegIndex, &Base, &Size))
    {
        return FALSE;
    }

    Address->QuadPart = (LONGLONG)Base;
    if (Length)
        *Length = (ULONG)Size;
    return TRUE;
}

static
ULONG
KiArmFindTimerInterrupt(VOID)
{
    static const CHAR * const Timer[] = { "arm,armv7-timer", "arm,armv8-timer", NULL };
    const VOID *Property;
    ULONGLONG Type, Number;
    ULONG Node, Parent, Length;

    /* interrupts = secure phys, non-secure phys, virtual, hypervisor; PPIs start at 16. */
    if (KiArmOpenDeviceTree() &&
        KiArmFindCompatibleIn(FdtRootNode(&KiArmDeviceTree), Timer, &Node, &Parent) &&
        (Property = FdtGetProperty(&KiArmDeviceTree, Node, "interrupts", &Length)) &&
        FdtReadCells(Property, Length, 6, 1, &Type) &&
        FdtReadCells(Property, Length, 7, 1, &Number) && (Type == 1))
    {
        return (ULONG)Number + 16;
    }
    return 27;
}

FORCEINLINE ULONG KiArmGicdRead(ULONG Offset) { return READ_REGISTER_ULONG((PULONG)(KiArmGicDistributor + Offset)); }
FORCEINLINE VOID KiArmGicdWrite(ULONG Offset, ULONG Value) { WRITE_REGISTER_ULONG((PULONG)(KiArmGicDistributor + Offset), Value); }
FORCEINLINE ULONG KiArmGiccRead(ULONG Offset) { return READ_REGISTER_ULONG((PULONG)(KiArmGicCpuInterface + Offset)); }
FORCEINLINE VOID KiArmGiccWrite(ULONG Offset, ULONG Value) { WRITE_REGISTER_ULONG((PULONG)(KiArmGicCpuInterface + Offset), Value); }

/* Higher IRQL is a numerically lower GIC priority; 16 levels in the top nibble. */
FORCEINLINE
ULONG
KiArmIrqlToPriority(
    _In_ KIRQL Irql)
{
    return (ULONG)(HIGH_LEVEL - Irql) << 4;
}

VOID
NTAPI
KiArmSetPriorityMask(
    _In_ KIRQL Irql)
{
    if (!KiArmGicCpuInterface)
        return;

    /* Interrupts with a priority value below PMR are signalled. */
    KiArmGiccWrite(GICC_PMR, KiArmIrqlToPriority(max(Irql, DISPATCH_LEVEL)));
}

BOOLEAN
NTAPI
KiArmEnableInterruptLine(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode)
{
    ULONG Shift, Config;

    if (!KiArmGicDistributor || (Vector >= KiArmGicLines) || (Vector < 16) || (Irql <= DISPATCH_LEVEL))
        return FALSE;

    KiArmInterruptIrql[Vector] = Irql;
    WRITE_REGISTER_UCHAR(KiArmGicDistributor + GICD_IPRIORITYR + Vector, (UCHAR)KiArmIrqlToPriority(Irql));
    if (Vector >= 32)
    {
        WRITE_REGISTER_UCHAR(KiArmGicDistributor + GICD_ITARGETSR + Vector, 1);
        Shift = (Vector % 16) * 2;
        Config = KiArmGicdRead(GICD_ICFGR + (Vector / 16) * 4);
        Config &= ~(2UL << Shift);
        if (Mode == Latched)
            Config |= 2UL << Shift;
        KiArmGicdWrite(GICD_ICFGR + (Vector / 16) * 4, Config);
    }
    KiArmGicdWrite(GICD_ISENABLER + (Vector / 32) * 4, 1UL << (Vector % 32));
    return TRUE;
}

VOID
NTAPI
KiArmDisableInterruptLine(
    _In_ ULONG Vector)
{
    if (!KiArmGicDistributor || (Vector >= KiArmGicLines))
        return;
    KiArmGicdWrite(GICD_ICENABLER + (Vector / 32) * 4, 1UL << (Vector % 32));
}

static
VOID
KiArmProgramTimer(VOID)
{
    KiArmWriteCp15(14, 0, 3, 0, KiArmTimerReload);
    KiArmWriteCp15(14, 0, 3, 1, CNT_CTL_ENABLE);
    KiArmIsb();
}

static
BOOLEAN
NTAPI
KiArmClockInterrupt(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL OldIrql)
{
    if (!(KiArmReadCp15(14, 0, 3, 1) & CNT_CTL_ISTATUS))
        return FALSE;

    KiArmProgramTimer();
    KeUpdateSystemTime(TrapFrame, KI_ARM_CLOCK_INCREMENT, OldIrql);
    return TRUE;
}

/* Acknowledge, run and complete one GIC interrupt. Called with interrupts masked. */
BOOLEAN
NTAPI
KiArmDispatchDeviceInterrupt(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    ULONG Iar, Vector;
    KIRQL OldIrql, Irql;

    if (!KiArmGicCpuInterface)
        return FALSE;

    Iar = KiArmGiccRead(GICC_IAR);
    Vector = Iar & GICC_INTID_MASK;
    if (Vector >= GIC_SPURIOUS)
        return FALSE;

    KiArmInterruptSource[Pcr->Prcb.Number % MAXIMUM_PROCESSORS] = Vector;
    OldIrql = Pcr->CurrentIrql;
    Irql = KiArmInterruptIrql[Vector];
    ASSERT(Irql > OldIrql);
    Pcr->CurrentIrql = Irql;
    KiArmSetPriorityMask(Irql);

    if ((Vector != KiArmTimerInterrupt) || !KiArmClockInterrupt(TrapFrame, OldIrql))
        KiArmServiceInterruptVector(Vector, TrapFrame);

    KiArmGiccWrite(GICC_EOIR, Iar);
    Pcr->CurrentIrql = OldIrql;
    KiArmSetPriorityMask(OldIrql);
    return TRUE;
}

VOID
NTAPI
KiArmSendInterprocessorInterrupt(
    _In_ ULONG TargetMask,
    _In_ ULONG Sgi)
{
    if (!KiArmGicDistributor)
        return;
    KiArmDsbIsh();
    KiArmGicdWrite(GICD_SGIR, ((TargetMask & 0xFF) << 16) | (Sgi & 0xF));
}

static
PVOID
KiArmMapDevice(
    _In_ PHYSICAL_ADDRESS Address,
    _In_ ULONG Length)
{
    return MmMapIoSpace(Address, max(Length, PAGE_SIZE), MmNonCached);
}

/* Bring up the interrupt controller and the clock once device mappings exist. */
VOID
NTAPI
KiArmInitializePlatform(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    static const CHAR * const Gic[] = { "arm,cortex-a15-gic", "arm,cortex-a7-gic", "arm,gic-400", "arm,cortex-a9-gic", NULL };
    PHYSICAL_ADDRESS Distributor, CpuInterface;
    ULONG DistributorLength, CpuLength, Index;

    UNREFERENCED_PARAMETER(LoaderBlock);

    if (KiArmPlatformReady)
        return;

    if (!KiArmFindDevice(Gic, 0, &Distributor, &DistributorLength) ||
        !KiArmFindDevice(Gic, 1, &CpuInterface, &CpuLength))
    {
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D01, 0, 0, 0);
    }

    KiArmGicDistributor = KiArmMapDevice(Distributor, DistributorLength);
    KiArmGicCpuInterface = KiArmMapDevice(CpuInterface, CpuLength);
    if (!KiArmGicDistributor || !KiArmGicCpuInterface)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D02, Distributor.LowPart, CpuInterface.LowPart, 0);

    KiArmGicLines = min(((KiArmGicdRead(GICD_TYPER) & 0x1F) + 1) * 32, GIC_MAX_INTID);
    KiArmGicdWrite(GICD_CTLR, 0);
    for (Index = 0; Index < KiArmGicLines; Index += 32)
    {
        KiArmGicdWrite(GICD_ICENABLER + Index / 8, 0xFFFFFFFF);
        KiArmGicdWrite(GICD_ICPENDR + Index / 8, 0xFFFFFFFF);
        KiArmGicdWrite(GICD_ICACTIVER + Index / 8, 0xFFFFFFFF);
    }
    for (Index = 0; Index < KiArmGicLines; Index++)
        WRITE_REGISTER_UCHAR(KiArmGicDistributor + GICD_IPRIORITYR + Index, 0xF0);
    KiArmGicdWrite(GICD_CTLR, 1);

    KiArmGiccWrite(GICC_BPR, 0);
    KiArmSetPriorityMask(KeGetCurrentIrql());
    KiArmGiccWrite(GICC_CTLR, 1);

    ASSERT(KiArmTimerFrequency != 0 && KiArmTimerReload != 0);
    KiArmTimerInterrupt = KiArmFindTimerInterrupt();

    KiArmPlatformReady = TRUE;
    KiArmEnableInterruptLine(KiArmTimerInterrupt, CLOCK_LEVEL, LevelSensitive);
    KiArmProgramTimer();
}

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    _Out_opt_ PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Counter;

    if (PerformanceFrequency)
        PerformanceFrequency->QuadPart = KiArmReadCp15(14, 0, 0, 0);
    Counter.QuadPart = (LONGLONG)ReadTimeStampCounter();
    return Counter;
}

VOID
NTAPI
KeStallExecutionProcessor(
    _In_ ULONG MicroSeconds)
{
    ULONG Frequency = KiArmReadCp15(14, 0, 0, 0);
    ULONG64 Start = ReadTimeStampCounter();
    ULONG64 Ticks = ((ULONG64)Frequency * MicroSeconds + 999999) / 1000000;

    while (ReadTimeStampCounter() - Start < Ticks)
        YieldProcessor();
}

BOOLEAN
NTAPI
KiArmPlatformInitialized(VOID)
{
    return KiArmPlatformReady;
}

ULONG
FASTCALL
HalGetInterruptSource(VOID)
{
    return KiArmInterruptSource[KeGetCurrentProcessorNumber() % MAXIMUM_PROCESSORS];
}

/* PSCI conduit from the device tree "method" property; HVC unless it says "smc". */
BOOLEAN
NTAPI
KiArmPsciConduitIsSmc(VOID)
VOID
NTAPI
KiArmInitializeTimeBase(VOID)
{
    KiArmTimerFrequency = KiArmReadCp15(14, 0, 0, 0);
    if (!KiArmTimerFrequency)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D03, 0, 0, 0);
    KiArmTimerReload = (ULONG)(((ULONGLONG)KiArmTimerFrequency * KI_ARM_CLOCK_INCREMENT) / 10000000ULL);
    KeSetTimeIncrement(KI_ARM_CLOCK_INCREMENT, KI_ARM_CLOCK_INCREMENT);
}

{
    static const CHAR * const Psci[] = { "arm,psci-1.0", "arm,psci-0.2", "arm,psci", NULL };
    const CHAR *Method;
    ULONG Node, Parent, Length;

    if (!KiArmOpenDeviceTree() ||
        !KiArmFindCompatibleIn(FdtRootNode(&KiArmDeviceTree), Psci, &Node, &Parent))
    {
        return FALSE;
    }
    Method = FdtGetProperty(&KiArmDeviceTree, Node, "method", &Length);
    return Method && (Length >= 4) && !strncmp(Method, "smc", 3);
}
