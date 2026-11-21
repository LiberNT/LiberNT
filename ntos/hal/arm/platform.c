/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 platform devices: interrupt controller, generic timer and device tree lookup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <libernt/fdtlib.h>
#include "intc.h"
#define NDEBUG
#include <debug.h>

/* Generic timer (ARM ARM B8): virtual timer control. */
#define CNT_CTL_ENABLE  0x1
#define CNT_CTL_IMASK   0x2
#define CNT_CTL_ISTATUS 0x4

#define KI_ARM_CLOCK_INCREMENT 100000UL

static const KI_ARM_INTERRUPT_CONTROLLER *KiArmController;
static KIRQL KiArmInterruptIrql[KI_ARM_MAX_VECTORS];
static ULONG KiArmTimerInterrupt;
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
        !FdtReadReg(&KiArmDeviceTree, Node, Parent, RegIndex, &Base, &Size) ||
        !FdtTranslateAddress(&KiArmDeviceTree, Parent, &Base))
    {
        return FALSE;
    }

    Address->QuadPart = (LONGLONG)Base;
    if (Length)
        *Length = (ULONG)Size;
    return TRUE;
}

/* The virtual timer's GIC PPI: interrupts = secure phys, non-secure phys, virtual, hypervisor; PPIs start at 16. */
BOOLEAN
NTAPI
KiArmFindTimerPpi(
    _Out_ PULONG Vector)
{
    static const CHAR * const Timer[] = { "arm,armv7-timer", "arm,armv8-timer", NULL };
    const VOID *Property;
    ULONGLONG Type, Number;
    ULONG Node, Parent, Length;

    if (KiArmOpenDeviceTree() &&
        KiArmFindCompatibleIn(FdtRootNode(&KiArmDeviceTree), Timer, &Node, &Parent) &&
        (Property = FdtGetProperty(&KiArmDeviceTree, Node, "interrupts", &Length)) &&
        FdtReadCells(Property, Length, 6, 1, &Type) &&
        FdtReadCells(Property, Length, 7, 1, &Number) && (Type == 1))
    {
        *Vector = (ULONG)Number + 16;
        return TRUE;
    }
    return FALSE;
}

VOID
NTAPI
KiArmSetPriorityMask(
    _In_ KIRQL Irql)
{
    if (!KiArmPlatformReady)
        return;
    KiArmController->SetPriorityMask(max(Irql, DISPATCH_LEVEL));
}

BOOLEAN
NTAPI
KiArmEnableInterruptLine(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode)
{
    if (!KiArmController || (Vector >= KI_ARM_MAX_VECTORS) || (Vector == KI_ARM_IPI_SGI) || (Irql <= DISPATCH_LEVEL))
        return FALSE;

    KiArmInterruptIrql[Vector] = Irql;
    return KiArmController->EnableLine(Vector, Irql, Mode);
}

VOID
NTAPI
KiArmDisableInterruptLine(
    _In_ ULONG Vector)
{
    if (KiArmController && (Vector < KI_ARM_MAX_VECTORS))
        KiArmController->DisableLine(Vector);
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

/* Acknowledge, run and complete one interrupt. Called with interrupts masked. */
BOOLEAN
NTAPI
KiArmDispatchDeviceInterrupt(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    ULONG Vector, Token;
    KIRQL OldIrql, Irql;

    if (!KiArmPlatformReady || !KiArmController->Acknowledge(&Vector, &Token))
        return FALSE;

    ASSERT(Vector < KI_ARM_MAX_VECTORS);
    KiArmInterruptSource[Pcr->Prcb.Number % MAXIMUM_PROCESSORS] = Vector;
    OldIrql = Pcr->CurrentIrql;
    Irql = KiArmInterruptIrql[Vector];
    ASSERT(Irql > OldIrql);
    Pcr->CurrentIrql = Irql;
    KiArmSetPriorityMask(Irql);

    if (Vector == KI_ARM_IPI_SGI)
        KiIpiProcessRequests();
    else if ((Vector != KiArmTimerInterrupt) || !KiArmClockInterrupt(TrapFrame, OldIrql))
        KiArmServiceInterruptVector(Vector, TrapFrame);

    KiArmController->EndOfInterrupt(Token);
    Pcr->CurrentIrql = OldIrql;
    KiArmSetPriorityMask(OldIrql);
    return TRUE;
}

VOID
NTAPI
KiArmSendInterprocessorInterrupt(
    _In_ KAFFINITY TargetProcessors)
{
    if (KiArmPlatformReady && TargetProcessors)
        KiArmController->SendInterprocessorInterrupt(TargetProcessors);
}

VOID
NTAPI
KiArmInitializeProcessorInterrupts(VOID)
{
    ASSERT(KiArmPlatformReady);
    ASSERT(KeGetCurrentProcessorNumber() != 0);

    KiArmController->InitializeProcessor(KeGetCurrentProcessorNumber());
    KiArmEnableInterruptLine(KiArmTimerInterrupt, CLOCK_LEVEL, LevelSensitive);
    KiArmProgramTimer();
}

/* MPIDR affinity of the Index-th processor node under /cpus, in device tree order. */
BOOLEAN
NTAPI
KiArmEnumerateProcessor(
    _In_ ULONG Index,
    _Out_ PULONG Mpidr)
{
    const VOID *Property;
    const CHAR *Type;
    ULONG Cpus, Child, AddressCells, SizeCells, Length, Found = 0;
    ULONGLONG Value;

    if (!KiArmOpenDeviceTree() || !FdtFindNode(&KiArmDeviceTree, "/cpus", &Cpus, NULL))
        return FALSE;

    FdtGetCellCounts(&KiArmDeviceTree, Cpus, &AddressCells, &SizeCells);
    for (Child = FdtFirstChild(&KiArmDeviceTree, Cpus);
         Child != FDT_NO_NODE;
         Child = FdtNextSibling(&KiArmDeviceTree, Child))
    {
        Type = FdtGetProperty(&KiArmDeviceTree, Child, "device_type", &Length);
        if (!Type || (Length < 4) || strncmp(Type, "cpu", 4))
            continue;
        Property = FdtGetProperty(&KiArmDeviceTree, Child, "reg", &Length);
        if (!Property || !FdtReadCells(Property, Length, 0, AddressCells, &Value))
            continue;
        if (Found++ == Index)
        {
            *Mpidr = (ULONG)Value;
            return TRUE;
        }
    }
    return FALSE;
}

/* TRUE when the Index-th processor node, or failing that /cpus, names Method as its enable-method. */
BOOLEAN
NTAPI
KiArmProcessorEnableMethodIs(
    _In_ ULONG Index,
    _In_z_ const CHAR *Method)
{
    const VOID *Property;
    const CHAR *Type;
    ULONG Cpus, Child, Length, Found = 0;

    if (!KiArmOpenDeviceTree() || !FdtFindNode(&KiArmDeviceTree, "/cpus", &Cpus, NULL))
        return FALSE;

    for (Child = FdtFirstChild(&KiArmDeviceTree, Cpus);
         Child != FDT_NO_NODE;
         Child = FdtNextSibling(&KiArmDeviceTree, Child))
    {
        Type = FdtGetProperty(&KiArmDeviceTree, Child, "device_type", &Length);
        if (!Type || (Length < 4) || strncmp(Type, "cpu", 4))
            continue;
        if (Found++ != Index)
            continue;
        Property = FdtGetProperty(&KiArmDeviceTree, Child, "enable-method", &Length);
        if (!Property)
            Property = FdtGetProperty(&KiArmDeviceTree, Cpus, "enable-method", &Length);
        return Property && FdtStringListContains(Property, Length, Method);
    }
    return FALSE;
}

PVOID
NTAPI
KiArmMapDevice(
    _In_ PHYSICAL_ADDRESS Address,
    _In_ ULONG Length)
{
    return MmMapIoSpace(Address, max(Length, PAGE_SIZE), MmNonCached);
}

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

/* Bring up the interrupt controller and the clock once device mappings exist. */
VOID
NTAPI
KiArmInitializePlatform(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    static const KI_ARM_INTERRUPT_CONTROLLER * const Controllers[] = { &KiArmGicV2Controller, &KiArmBcm2836Controller };
    ULONG Index;

    UNREFERENCED_PARAMETER(LoaderBlock);

    if (KiArmPlatformReady)
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(Controllers); Index++)
    {
        if (Controllers[Index]->Initialize())
        {
            KiArmController = Controllers[Index];
            break;
        }
    }
    if (!KiArmController)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D01, 0, 0, 0);

    ASSERT(KiArmTimerFrequency != 0 && KiArmTimerReload != 0);
    KiArmInterruptIrql[KI_ARM_IPI_SGI] = IPI_LEVEL;
    KiArmTimerInterrupt = KiArmController->TimerVector();
    ASSERT(KiArmTimerInterrupt < KI_ARM_MAX_VECTORS);

    KiArmPlatformReady = TRUE;
    KiArmController->InitializeProcessor(0);
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
