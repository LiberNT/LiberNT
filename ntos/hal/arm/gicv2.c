/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 GICv2 interrupt controller backend
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "intc.h"
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

static PUCHAR KiGicDistributor;
static PUCHAR KiGicCpuInterface;
static ULONG KiGicLines;
static UCHAR KiGicCpuMask[MAXIMUM_PROCESSORS];

FORCEINLINE ULONG KiGicdRead(ULONG Offset) { return READ_REGISTER_ULONG((PULONG)(KiGicDistributor + Offset)); }
FORCEINLINE VOID KiGicdWrite(ULONG Offset, ULONG Value) { WRITE_REGISTER_ULONG((PULONG)(KiGicDistributor + Offset), Value); }
FORCEINLINE ULONG KiGiccRead(ULONG Offset) { return READ_REGISTER_ULONG((PULONG)(KiGicCpuInterface + Offset)); }
FORCEINLINE VOID KiGiccWrite(ULONG Offset, ULONG Value) { WRITE_REGISTER_ULONG((PULONG)(KiGicCpuInterface + Offset), Value); }

/* Higher IRQL is a numerically lower GIC priority; 16 levels in the top nibble. */
FORCEINLINE
ULONG
KiGicIrqlToPriority(
    _In_ KIRQL Irql)
{
    return (ULONG)(HIGH_LEVEL - Irql) << 4;
}

/* The CPU interface mask GICD_ITARGETSR0-7 returns on the processor reading it (IHI 0048B 4.3.12). */
static
UCHAR
KiGicReadOwnCpuMask(VOID)
{
    UCHAR Mask = READ_REGISTER_UCHAR(KiGicDistributor + GICD_ITARGETSR);

    /* A uniprocessor implementation reads it as zero. */
    return Mask ? Mask : 1;
}

static
VOID
NTAPI
KiGicSetPriorityMask(
    _In_ KIRQL Irql)
{
    ASSERT(Irql >= DISPATCH_LEVEL);

    /* Interrupts with a priority value below PMR are signalled. */
    KiGiccWrite(GICC_PMR, KiGicIrqlToPriority(Irql));
}

static
BOOLEAN
NTAPI
KiGicInitialize(VOID)
{
    static const CHAR * const Gic[] = { "arm,cortex-a15-gic", "arm,cortex-a7-gic", "arm,gic-400", "arm,cortex-a9-gic", NULL };
    PHYSICAL_ADDRESS Distributor, CpuInterface;
    ULONG DistributorLength, CpuLength, Index;

    if (!KiArmFindDevice(Gic, 0, &Distributor, &DistributorLength) ||
        !KiArmFindDevice(Gic, 1, &CpuInterface, &CpuLength))
    {
        return FALSE;
    }

    KiGicDistributor = KiArmMapDevice(Distributor, DistributorLength);
    KiGicCpuInterface = KiArmMapDevice(CpuInterface, CpuLength);
    if (!KiGicDistributor || !KiGicCpuInterface)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D02, Distributor.LowPart, CpuInterface.LowPart, 0);

    KiGicLines = min(((KiGicdRead(GICD_TYPER) & 0x1F) + 1) * 32, KI_ARM_MAX_VECTORS);
    KiGicdWrite(GICD_CTLR, 0);
    KiGicCpuMask[0] = KiGicReadOwnCpuMask();
    for (Index = 0; Index < KiGicLines; Index += 32)
    {
        KiGicdWrite(GICD_ICENABLER + Index / 8, 0xFFFFFFFF);
        KiGicdWrite(GICD_ICPENDR + Index / 8, 0xFFFFFFFF);
        KiGicdWrite(GICD_ICACTIVER + Index / 8, 0xFFFFFFFF);
    }
    for (Index = 0; Index < KiGicLines; Index++)
        WRITE_REGISTER_UCHAR(KiGicDistributor + GICD_IPRIORITYR + Index, 0xF0);
    KiGicdWrite(GICD_CTLR, 1);
    return TRUE;
}

/* Banked SGI and PPI state and the CPU interface of the current processor. */
static
VOID
NTAPI
KiGicInitializeProcessor(
    _In_ ULONG Number)
{
    ULONG Index;

    ASSERT(Number < MAXIMUM_PROCESSORS);
    KiGicCpuMask[Number] = KiGicReadOwnCpuMask();

    KiGicdWrite(GICD_ICENABLER, 0xFFFFFFFF);
    KiGicdWrite(GICD_ICPENDR, 0xFFFFFFFF);
    KiGicdWrite(GICD_ICACTIVER, 0xFFFFFFFF);
    for (Index = 0; Index < 32; Index++)
        WRITE_REGISTER_UCHAR(KiGicDistributor + GICD_IPRIORITYR + Index, 0xF0);

    WRITE_REGISTER_UCHAR(KiGicDistributor + GICD_IPRIORITYR + KI_ARM_IPI_SGI, (UCHAR)KiGicIrqlToPriority(IPI_LEVEL));
    KiGicdWrite(GICD_ISENABLER, 1UL << KI_ARM_IPI_SGI);

    KiGiccWrite(GICC_BPR, 0);
    KiGicSetPriorityMask(max(KeGetCurrentIrql(), DISPATCH_LEVEL));
    KiGiccWrite(GICC_CTLR, 1);
}

static
BOOLEAN
NTAPI
KiGicEnableLine(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode)
{
    ULONG Shift, Config;

    if ((Vector >= KiGicLines) || (Vector < 16))
        return FALSE;

    WRITE_REGISTER_UCHAR(KiGicDistributor + GICD_IPRIORITYR + Vector, (UCHAR)KiGicIrqlToPriority(Irql));
    if (Vector >= 32)
    {
        WRITE_REGISTER_UCHAR(KiGicDistributor + GICD_ITARGETSR + Vector, KiGicCpuMask[0]);
        Shift = (Vector % 16) * 2;
        Config = KiGicdRead(GICD_ICFGR + (Vector / 16) * 4);
        Config &= ~(2UL << Shift);
        if (Mode == Latched)
            Config |= 2UL << Shift;
        KiGicdWrite(GICD_ICFGR + (Vector / 16) * 4, Config);
    }
    KiGicdWrite(GICD_ISENABLER + (Vector / 32) * 4, 1UL << (Vector % 32));
    return TRUE;
}

static
VOID
NTAPI
KiGicDisableLine(
    _In_ ULONG Vector)
{
    if (Vector < KiGicLines)
        KiGicdWrite(GICD_ICENABLER + (Vector / 32) * 4, 1UL << (Vector % 32));
}

static
BOOLEAN
NTAPI
KiGicAcknowledge(
    _Out_ PULONG Vector,
    _Out_ PULONG Token)
{
    ULONG Iar = KiGiccRead(GICC_IAR);

    if ((Iar & GICC_INTID_MASK) >= GIC_SPURIOUS)
        return FALSE;
    *Vector = Iar & GICC_INTID_MASK;
    *Token = Iar;
    return TRUE;
}

static
VOID
NTAPI
KiGicEndOfInterrupt(
    _In_ ULONG Token)
{
    KiGiccWrite(GICC_EOIR, Token);
}

static
VOID
NTAPI
KiGicSendInterprocessorInterrupt(
    _In_ KAFFINITY TargetProcessors)
{
    ULONG Number, TargetList = 0;
    KAFFINITY Remaining;

    /* SGIR names GIC CPU interfaces, which need not follow the processor numbers. */
    for (Remaining = TargetProcessors, Number = 0; Remaining && (Number < MAXIMUM_PROCESSORS); Remaining >>= 1, Number++)
    {
        if (Remaining & 1)
            TargetList |= KiGicCpuMask[Number];
    }
    if (!TargetList)
        return;

    KiArmDsbIsh();
    KiGicdWrite(GICD_SGIR, ((TargetList & 0xFF) << 16) | KI_ARM_IPI_SGI);
}

static
ULONG
NTAPI
KiGicTimerVector(VOID)
{
    ULONG Vector;

    return KiArmFindTimerPpi(&Vector) ? Vector : 27;
}

const KI_ARM_INTERRUPT_CONTROLLER KiArmGicV2Controller =
{
    KiGicInitialize,
    KiGicInitializeProcessor,
    KiGicSetPriorityMask,
    KiGicEnableLine,
    KiGicDisableLine,
    KiGicAcknowledge,
    KiGicEndOfInterrupt,
    KiGicSendInterprocessorInterrupt,
    KiGicTimerVector
};
