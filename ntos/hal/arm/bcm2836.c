/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 BCM2836 interrupt controller backend: ARM-local and GPU controllers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "intc.h"
#define NDEBUG
#include <debug.h>

/* BCM2836 ARM-local peripherals (QA7): per-core timer, mailbox and source registers. */
#define BCM_LOCAL_GPU_ROUTING        0x0C
#define BCM_LOCAL_TIMER_CONTROL(c)   (0x40 + 4 * (c))
#define BCM_LOCAL_MAILBOX_CONTROL(c) (0x50 + 4 * (c))
#define BCM_LOCAL_IRQ_SOURCE(c)      (0x60 + 4 * (c))
#define BCM_LOCAL_MAILBOX_SET(c, m)  (0x80 + 0x10 * (c) + 4 * (m))
#define BCM_LOCAL_MAILBOX_CLEAR(c, m) (0xC0 + 0x10 * (c) + 4 * (m))
#define BCM_SOURCE_TIMERS            0x00F
#define BCM_SOURCE_MAILBOX0          0x010
#define BCM_SOURCE_GPU               0x100
#define BCM_CORES                    4

/* BCM2835 ARM peripherals 7.5: the GPU interrupt controller, from its pending register at 0x200. */
#define ARMCTRL_PENDING_BASIC        0x00
#define ARMCTRL_PENDING_1            0x04
#define ARMCTRL_PENDING_2            0x08
#define ARMCTRL_ENABLE_1             0x10
#define ARMCTRL_ENABLE_2             0x14
#define ARMCTRL_ENABLE_BASIC         0x18
#define ARMCTRL_DISABLE_1            0x1C
#define ARMCTRL_DISABLE_2            0x20
#define ARMCTRL_DISABLE_BASIC        0x24
#define ARMCTRL_BASIC_SOURCES        0xFF

/* Vectors: ARM-local source n is 16 + n, GPU interrupt n is 32 + n, basic source n is 96 + n. */
#define BCM_VECTOR_LOCAL             16
#define BCM_VECTOR_CNTV              (BCM_VECTOR_LOCAL + 3)
#define BCM_VECTOR_GPU               32
#define BCM_VECTOR_BASIC             96
#define BCM_VECTORS                  104
#define BCM_GPU_BANKS                3

static PUCHAR KiBcmLocal;
static PUCHAR KiBcmArmctrl;
static ULONG KiBcmGpuCore;
static UCHAR KiBcmCoreOfProcessor[MAXIMUM_PROCESSORS];
static KIRQL KiBcmIrql[BCM_VECTORS];
static ULONG KiBcmTimerEnabled[BCM_CORES];
static ULONG KiBcmTimerControl[BCM_CORES];
static ULONG KiBcmMailboxControl[BCM_CORES];
static BOOLEAN KiBcmMailboxReady[BCM_CORES];
static ULONG KiBcmGpuEnabled[BCM_GPU_BANKS];
static ULONG KiBcmGpuApplied[BCM_GPU_BANKS];

static const ULONG KiBcmEnableRegister[BCM_GPU_BANKS] = { ARMCTRL_ENABLE_1, ARMCTRL_ENABLE_2, ARMCTRL_ENABLE_BASIC };
static const ULONG KiBcmDisableRegister[BCM_GPU_BANKS] = { ARMCTRL_DISABLE_1, ARMCTRL_DISABLE_2, ARMCTRL_DISABLE_BASIC };
static const ULONG KiBcmPendingRegister[BCM_GPU_BANKS] = { ARMCTRL_PENDING_1, ARMCTRL_PENDING_2, ARMCTRL_PENDING_BASIC };
static const ULONG KiBcmBankVector[BCM_GPU_BANKS] = { BCM_VECTOR_GPU, BCM_VECTOR_GPU + 32, BCM_VECTOR_BASIC };

FORCEINLINE ULONG KiBcmLocalRead(ULONG Offset) { return READ_REGISTER_ULONG((PULONG)(KiBcmLocal + Offset)); }
FORCEINLINE VOID KiBcmLocalWrite(ULONG Offset, ULONG Value) { WRITE_REGISTER_ULONG((PULONG)(KiBcmLocal + Offset), Value); }
FORCEINLINE ULONG KiBcmArmctrlRead(ULONG Offset) { return READ_REGISTER_ULONG((PULONG)(KiBcmArmctrl + Offset)); }
FORCEINLINE VOID KiBcmArmctrlWrite(ULONG Offset, ULONG Value) { WRITE_REGISTER_ULONG((PULONG)(KiBcmArmctrl + Offset), Value); }

/* The core number is MPIDR.Aff0 on the BCM2836 cluster. */
FORCEINLINE
ULONG
KiBcmCurrentCore(VOID)
{
    ULONG Core = KiArmReadCp15(0, 0, 0, 5) & 0xFF;

    ASSERT(Core < BCM_CORES);
    return Core;
}

static
BOOLEAN
KiBcmGpuVector(
    _In_ ULONG Vector,
    _Out_ PULONG Bank,
    _Out_ PULONG Bit)
{
    if ((Vector >= BCM_VECTOR_GPU) && (Vector < BCM_VECTOR_BASIC))
    {
        *Bank = (Vector - BCM_VECTOR_GPU) / 32;
        *Bit = (Vector - BCM_VECTOR_GPU) % 32;
        return TRUE;
    }
    if ((Vector >= BCM_VECTOR_BASIC) && (Vector < BCM_VECTORS))
    {
        *Bank = 2;
        *Bit = Vector - BCM_VECTOR_BASIC;
        return TRUE;
    }
    return FALSE;
}

static
VOID
KiBcmApplyGpuMask(
    _In_ KIRQL Irql)
{
    ULONG Bank, Bit, Want, Pending;

    for (Bank = 0; Bank < BCM_GPU_BANKS; Bank++)
    {
        Want = 0;
        for (Pending = KiBcmGpuEnabled[Bank]; Pending; Pending &= Pending - 1)
        {
            Bit = 31 - _CountLeadingZeros(Pending & (~Pending + 1));
            if (KiBcmIrql[KiBcmBankVector[Bank] + Bit] > Irql)
                Want |= 1UL << Bit;
        }
        if (Want & ~KiBcmGpuApplied[Bank])
            KiBcmArmctrlWrite(KiBcmEnableRegister[Bank], Want & ~KiBcmGpuApplied[Bank]);
        if (KiBcmGpuApplied[Bank] & ~Want)
            KiBcmArmctrlWrite(KiBcmDisableRegister[Bank], KiBcmGpuApplied[Bank] & ~Want);
        KiBcmGpuApplied[Bank] = Want;
    }
}

/* Without priorities in hardware, every source at or below the IRQL is switched off at its controller. */
static
VOID
NTAPI
KiBcmSetPriorityMask(
    _In_ KIRQL Irql)
{
    ULONG Core = KiBcmCurrentCore();
    ULONG Source, Timer = 0, Mailbox;

    ASSERT(Irql >= DISPATCH_LEVEL);

    for (Source = 0; Source < 4; Source++)
    {
        if ((KiBcmTimerEnabled[Core] & (1UL << Source)) && (KiBcmIrql[BCM_VECTOR_LOCAL + Source] > Irql))
            Timer |= 1UL << Source;
    }
    if (Timer != KiBcmTimerControl[Core])
    {
        KiBcmLocalWrite(BCM_LOCAL_TIMER_CONTROL(Core), Timer);
        KiBcmTimerControl[Core] = Timer;
    }

    Mailbox = (KiBcmMailboxReady[Core] && (IPI_LEVEL > Irql)) ? 1 : 0;
    if (Mailbox != KiBcmMailboxControl[Core])
    {
        KiBcmLocalWrite(BCM_LOCAL_MAILBOX_CONTROL(Core), Mailbox);
        KiBcmMailboxControl[Core] = Mailbox;
    }

    if (KiBcmArmctrl && (Core == KiBcmGpuCore))
        KiBcmApplyGpuMask(Irql);
}

static
BOOLEAN
NTAPI
KiBcmInitialize(VOID)
{
    static const CHAR * const Local[] = { "brcm,bcm2836-l1-intc", NULL };
    static const CHAR * const Armctrl[] = { "brcm,bcm2836-armctrl-ic", NULL };
    PHYSICAL_ADDRESS LocalAddress, ArmctrlAddress;
    ULONG LocalLength, ArmctrlLength, Core, Mailbox;

    if (!KiArmFindDevice(Local, 0, &LocalAddress, &LocalLength))
        return FALSE;
    KiBcmLocal = KiArmMapDevice(LocalAddress, LocalLength);
    if (!KiBcmLocal)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D02, LocalAddress.LowPart, 0, 0);

    if (KiArmFindDevice(Armctrl, 0, &ArmctrlAddress, &ArmctrlLength))
    {
        KiBcmArmctrl = KiArmMapDevice(ArmctrlAddress, ArmctrlLength);
        if (!KiBcmArmctrl)
            KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D02, ArmctrlAddress.LowPart, 0, 0);
    }

    /* GPU interrupts go to the boot core, IRQ and FIQ alike. */
    KiBcmGpuCore = KiBcmCurrentCore();
    KiBcmLocalWrite(BCM_LOCAL_GPU_ROUTING, KiBcmGpuCore | (KiBcmGpuCore << 2));
    if (KiBcmArmctrl)
    {
        KiBcmArmctrlWrite(ARMCTRL_DISABLE_1, MAXULONG);
        KiBcmArmctrlWrite(ARMCTRL_DISABLE_2, MAXULONG);
        KiBcmArmctrlWrite(ARMCTRL_DISABLE_BASIC, ARMCTRL_BASIC_SOURCES);
    }

    for (Core = 0; Core < BCM_CORES; Core++)
    {
        KiBcmLocalWrite(BCM_LOCAL_TIMER_CONTROL(Core), 0);
        KiBcmLocalWrite(BCM_LOCAL_MAILBOX_CONTROL(Core), 0);
        for (Mailbox = 0; Mailbox < 4; Mailbox++)
            KiBcmLocalWrite(BCM_LOCAL_MAILBOX_CLEAR(Core, Mailbox), MAXULONG);
    }

    KiBcmIrql[KI_ARM_IPI_SGI] = IPI_LEVEL;
    return TRUE;
}

static
VOID
NTAPI
KiBcmInitializeProcessor(
    _In_ ULONG Number)
{
    ULONG Core = KiBcmCurrentCore();
    BOOLEAN Enabled;

    ASSERT(Number < MAXIMUM_PROCESSORS);
    Enabled = KeDisableInterrupts();
    KiBcmCoreOfProcessor[Number] = (UCHAR)Core;
    KiBcmLocalWrite(BCM_LOCAL_MAILBOX_CLEAR(Core, 0), MAXULONG);
    KiBcmMailboxReady[Core] = TRUE;
    KiBcmSetPriorityMask(max(KeGetCurrentIrql(), DISPATCH_LEVEL));
    KeRestoreInterrupts(Enabled);
}

static
BOOLEAN
NTAPI
KiBcmEnableLine(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode)
{
    ULONG Bank, Bit;
    BOOLEAN Enabled;

    UNREFERENCED_PARAMETER(Mode);

    if ((Vector >= BCM_VECTOR_LOCAL) && (Vector < BCM_VECTOR_LOCAL + 4))
    {
        Enabled = KeDisableInterrupts();
        KiBcmIrql[Vector] = Irql;
        KiBcmTimerEnabled[KiBcmCurrentCore()] |= 1UL << (Vector - BCM_VECTOR_LOCAL);
        KiBcmSetPriorityMask(max(KeGetCurrentIrql(), DISPATCH_LEVEL));
        KeRestoreInterrupts(Enabled);
        return TRUE;
    }

    if (!KiBcmArmctrl || !KiBcmGpuVector(Vector, &Bank, &Bit))
        return FALSE;

    /* A source that fires while its core is too high is masked then and reopened by the next lowering. */
    Enabled = KeDisableInterrupts();
    KiBcmIrql[Vector] = Irql;
    KiBcmGpuEnabled[Bank] |= 1UL << Bit;
    KiBcmGpuApplied[Bank] |= 1UL << Bit;
    KiBcmArmctrlWrite(KiBcmEnableRegister[Bank], 1UL << Bit);
    KeRestoreInterrupts(Enabled);
    return TRUE;
}

static
VOID
NTAPI
KiBcmDisableLine(
    _In_ ULONG Vector)
{
    ULONG Bank, Bit, Core;
    BOOLEAN Enabled;

    Enabled = KeDisableInterrupts();
    if ((Vector >= BCM_VECTOR_LOCAL) && (Vector < BCM_VECTOR_LOCAL + 4))
    {
        Core = KiBcmCurrentCore();
        KiBcmTimerEnabled[Core] &= ~(1UL << (Vector - BCM_VECTOR_LOCAL));
        KiBcmSetPriorityMask(max(KeGetCurrentIrql(), DISPATCH_LEVEL));
    }
    else if (KiBcmArmctrl && KiBcmGpuVector(Vector, &Bank, &Bit))
    {
        KiBcmGpuEnabled[Bank] &= ~(1UL << Bit);
        KiBcmGpuApplied[Bank] &= ~(1UL << Bit);
        KiBcmArmctrlWrite(KiBcmDisableRegister[Bank], 1UL << Bit);
    }
    KeRestoreInterrupts(Enabled);
}

static
BOOLEAN
KiBcmAcknowledgeGpu(
    _Out_ PULONG Vector)
{
    KIRQL Irql = KeGetCurrentIrql();
    ULONG Bank, Bit, Pending;

    for (Bank = 0; Bank < BCM_GPU_BANKS; Bank++)
    {
        Pending = KiBcmArmctrlRead(KiBcmPendingRegister[Bank]) & KiBcmGpuApplied[Bank];
        while (Pending)
        {
            Bit = 31 - _CountLeadingZeros(Pending & (~Pending + 1));
            Pending &= Pending - 1;
            if (KiBcmIrql[KiBcmBankVector[Bank] + Bit] > Irql)
            {
                *Vector = KiBcmBankVector[Bank] + Bit;
                return TRUE;
            }
            KiBcmGpuApplied[Bank] &= ~(1UL << Bit);
            KiBcmArmctrlWrite(KiBcmDisableRegister[Bank], 1UL << Bit);
        }
    }
    return FALSE;
}

static
BOOLEAN
NTAPI
KiBcmAcknowledge(
    _Out_ PULONG Vector,
    _Out_ PULONG Token)
{
    ULONG Core = KiBcmCurrentCore();
    ULONG Source = KiBcmLocalRead(BCM_LOCAL_IRQ_SOURCE(Core));
    ULONG Timers = Source & KiBcmTimerControl[Core] & BCM_SOURCE_TIMERS;
    ULONG Pending;

    *Token = 0;
    if ((Source & BCM_SOURCE_MAILBOX0) && (KiBcmMailboxControl[Core] & 1))
    {
        /* Cleared before the requests are read, so one sent meanwhile raises the line again. */
        Pending = KiBcmLocalRead(BCM_LOCAL_MAILBOX_CLEAR(Core, 0));
        KiBcmLocalWrite(BCM_LOCAL_MAILBOX_CLEAR(Core, 0), Pending);
        KiArmDsbIsh();
        *Vector = KI_ARM_IPI_SGI;
        return TRUE;
    }

    if (Timers)
    {
        *Vector = BCM_VECTOR_LOCAL + (31 - _CountLeadingZeros(Timers));
        return TRUE;
    }

    if ((Source & BCM_SOURCE_GPU) && KiBcmArmctrl && (Core == KiBcmGpuCore))
        return KiBcmAcknowledgeGpu(Vector);
    return FALSE;
}

static
VOID
NTAPI
KiBcmEndOfInterrupt(
    _In_ ULONG Token)
{
    UNREFERENCED_PARAMETER(Token);
}

static
VOID
NTAPI
KiBcmSendInterprocessorInterrupt(
    _In_ KAFFINITY TargetProcessors)
{
    KAFFINITY Remaining;
    ULONG Number;

    KiArmDsbIsh();
    for (Remaining = TargetProcessors, Number = 0; Remaining && (Number < MAXIMUM_PROCESSORS); Remaining >>= 1, Number++)
    {
        if (Remaining & 1)
            KiBcmLocalWrite(BCM_LOCAL_MAILBOX_SET(KiBcmCoreOfProcessor[Number], 0), 1);
    }
}

/* The kernel ticks on the virtual timer, ARM-local source 3 (nCNTVIRQ). */
static
ULONG
NTAPI
KiBcmTimerVector(VOID)
{
    return BCM_VECTOR_CNTV;
}

/*
 * "brcm,bcm2836-smp": a parked core polls its mailbox 3 and branches to the
 * address written there with its core number in r0, so the startup block is
 * published by core number for KiArmSecondarySpinEntry.
 */
BOOLEAN
NTAPI
KiArmBcm2836StartProcessor(
    _In_ ULONG Mpidr,
    _In_ ULONG Context)
{
    ULONG Core = Mpidr & 0xFF;
    PHYSICAL_ADDRESS Entry;

    if (!KiBcmLocal || (Core >= BCM_CORES) || (Core >= KI_ARM_SPIN_STARTUP_BLOCKS))
        return FALSE;

    KiArmSpinStartupBlocks[Core] = Context;
    KiArmDsb();
    KiArmWriteCp15(7, 0, 10, 1, (ULONG_PTR)&KiArmSpinStartupBlocks[Core]);
    KiArmDsb();

    Entry = MmGetPhysicalAddress((PVOID)((ULONG_PTR)KiArmSecondarySpinEntry & ~1UL));
    ASSERT(Entry.HighPart == 0);
    KiBcmLocalWrite(BCM_LOCAL_MAILBOX_SET(Core, 3), Entry.LowPart | 1);
    KiArmDsb();
    __sev();
    return TRUE;
}

const KI_ARM_INTERRUPT_CONTROLLER KiArmBcm2836Controller =
{
    KiBcmInitialize,
    KiBcmInitializeProcessor,
    KiBcmSetPriorityMask,
    KiBcmEnableLine,
    KiBcmDisableLine,
    KiBcmAcknowledge,
    KiBcmEndOfInterrupt,
    KiBcmSendInterprocessorInterrupt,
    KiBcmTimerVector
};
