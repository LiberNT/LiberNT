/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 IRQL, interrupt priority mask and software interrupts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#undef KeAcquireSpinLock
#undef KeRaiseIrql
#undef KeLowerIrql

BOOLEAN KiArmHalInitialized;

/*
 * IRQL lives in the PCR. Raising it is lazy: the HAL defers an interrupt that
 * arrives at or below the current level and masks it then; lowering hands the
 * new level to the HAL, which unmasks and re-raises what it deferred.
 */

FORCEINLINE
PKIPCR
KiArmPcr(VOID)
{
    return (PKIPCR)KeGetPcr();
}

FORCEINLINE
VOID
KiArmSetIrqlAndPriorityMask(
    _Inout_ PKIPCR Pcr,
    _In_ KIRQL Irql)
{
    Pcr->CurrentIrql = Irql;
    if (KiArmHalInitialized)
        HalSetGicPriorityMask(Irql);
}

/*
 * The IRQL lives in the PCR, so it is read and written with interrupts masked:
 * below DISPATCH_LEVEL the thread could otherwise move to another processor
 * between finding the PCR and using it, and act on the processor it left.
 */
KIRQL
FASTCALL
KfRaiseIrql(
    _In_ KIRQL NewIrql)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    PKIPCR Pcr = KiArmPcr();
    KIRQL OldIrql = Pcr->CurrentIrql;

    if ((NewIrql < OldIrql) || (NewIrql > HIGH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)_ReturnAddress(), 0);

    Pcr->CurrentIrql = NewIrql;
    KeRestoreInterrupts(Enabled);
    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    PKIPCR Pcr = KiArmPcr();
    KIRQL OldIrql = Pcr->CurrentIrql;
    BOOLEAN Pending;

    if (NewIrql > OldIrql)
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)_ReturnAddress(), 0);

    KiArmSetIrqlAndPriorityMask(Pcr, NewIrql);
    Pending = (NewIrql < DISPATCH_LEVEL) && Pcr->SoftwareInterruptPending && (Pcr->Prcb.NestingLevel == 0);
    KeRestoreInterrupts(Enabled);

    if (Pending)
        KiArmDispatchSoftwareInterrupts(NULL);
}

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    KIRQL Irql = KiArmPcr()->CurrentIrql;

    KeRestoreInterrupts(Enabled);
    return Irql;
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

KIRQL
NTAPI
KeGetEffectiveIrql(VOID)
{
    ULONG Cpsr;

    __asm__ __volatile__("mrs %0, cpsr" : "=r"(Cpsr));
    return (Cpsr & CPSR_IRQ_DISABLE) ? HIGH_LEVEL : KeGetCurrentIrql();
}

/*
 * Deliver the DISPATCH_LEVEL and APC_LEVEL requests the current IRQL allows.
 * Each pass runs with interrupts enabled; the caller's interrupt state is
 * restored on return. A switch inside KiDispatchInterrupt resumes here later.
 */
VOID
NTAPI
KiArmDispatchSoftwareInterrupts(
    _In_opt_ PKTRAP_FRAME TrapFrame)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    PKIPCR Pcr;
    KIRQL OldIrql;

    for (;;)
    {
        Pcr = KiArmPcr();
        OldIrql = Pcr->CurrentIrql;
        if ((OldIrql < DISPATCH_LEVEL) && Pcr->DispatchInterrupt)
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            Pcr->CurrentIrql = DISPATCH_LEVEL;
            _enable();
            KiDispatchInterrupt();
            _disable();
            KiArmSetIrqlAndPriorityMask(KiArmPcr(), OldIrql);
            continue;
        }

        if ((OldIrql < APC_LEVEL) && Pcr->ApcInterrupt)
        {
            HalClearSoftwareInterrupt(APC_LEVEL);
            Pcr->CurrentIrql = APC_LEVEL;
            _enable();
            KiDeliverApc(KernelMode, NULL, TrapFrame);
            _disable();
            KiArmSetIrqlAndPriorityMask(KiArmPcr(), OldIrql);
            continue;
        }
        break;
    }

    KeRestoreInterrupts(Enabled);
}

VOID
NTAPI
KeRaiseIrql(
    _In_ KIRQL NewIrql,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KfRaiseIrql(NewIrql);
}

VOID
NTAPI
KeLowerIrql(
    _In_ KIRQL NewIrql)
{
    KfLowerIrql(NewIrql);
}

VOID
NTAPI
KeAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KeAcquireSpinLockRaiseToDpc(SpinLock);
}

KIRQL
FASTCALL
KfAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    return KeAcquireSpinLockRaiseToDpc(SpinLock);
}

VOID
FASTCALL
KfReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    KeReleaseSpinLock(SpinLock, OldIrql);
}

/* Retire DPCs and switch threads at DISPATCH_LEVEL, entered with interrupts on. */
VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    _disable();
    if (KiDpcWorkPending(Prcb))
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
    _enable();

    KiFinishDispatchInterrupt(Prcb);
}

VOID
NTAPI
KiRetireDpcListInDpcStack(
    _In_ PKPRCB Prcb,
    _In_ PVOID DpcStack)
{
    UNREFERENCED_PARAMETER(DpcStack);
    /* TODO: run deferred routines on the processor DPC stack */
    KiRetireDpcList(Prcb);
}
