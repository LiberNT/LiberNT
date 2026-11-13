/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 local interrupt priority
 *
 * NT's IRQL ladder is implemented with one hardware facility: ECFG.LIE
 * masks individual interrupt lines, and the core delivers the highest
 * pending line whose LIE bit is set ([V1-6.1.4]: int_vec = ESTAT.IS &
 * ECFG.LIE, and a higher int number wins, [V1-6.1.2]). Raising the IRQL
 * therefore means clearing the LIE bits of every line whose importance is
 * at or below the new IRQL; lowering it means setting them again.
 *
 * The line-to-IRQL assignment is the HAL's, agreed as:
 *
 *   SWI0        APC_LEVEL        local APC request       (writable IS bit 0)
 *   SWI1        DISPATCH_LEVEL   local DPC request       (writable IS bit 1)
 *   HWI0-HWI7   IRQL 4+n         external controller group n
 *   PMCOV       PROFILE_LEVEL    no consumer on this port
 *   TI          CLOCK_LEVEL      the stable timer, [V1-7.6]
 *   IPI         IPI_LEVEL        interprocessor requests, ESTAT.IS[12]
 *
 * The PASSIVE-level image is therefore all 13 bits set, which is what the
 * HAL writes once at startup; this file always writes only the 12:0 field
 * with csrxchg, so ECFG.VS and any compatibility bits the loader left are
 * preserved ([V1-7.4.5]).
 */

#include <ntoskrnl.h>
#include "la64ke.h"

static
ULONG
KiLa64ComputeInterruptMask(
    _In_ KIRQL Irql)
{
    ULONG Lines = LA64_ECFG_LIE_MASK;
    ULONG Group;

    /* External controller group n is wired to HWI n and runs at IRQL 4+n. */
    for (Group = 0; Group < 8; Group++)
    {
        if (Irql >= (KIRQL)(4 + Group))
            Lines &= ~LA64_IRQ_BIT(LA64_IRQ_HWI(Group));
    }

    if (Irql >= APC_LEVEL)
        Lines &= ~LA64_IRQ_BIT(LA64_IRQ_SWI0);
    if (Irql >= DISPATCH_LEVEL)
        Lines &= ~LA64_IRQ_BIT(LA64_IRQ_SWI1);
    if (Irql >= CLOCK_LEVEL)
        Lines &= ~LA64_IRQ_BIT(LA64_IRQ_TIMER);
    if (Irql >= IPI_LEVEL)
        Lines &= ~LA64_IRQ_BIT(LA64_IRQ_IPI);
    if (Irql >= PROFILE_LEVEL)
        Lines &= ~LA64_IRQ_BIT(LA64_IRQ_PMCOV);

    return Lines;
}

static
VOID
KiLa64UpdateInterruptMask(VOID)
{
    KiLa64SetInterruptMask(KiLa64ComputeInterruptMask(KeGetCurrentIrql()));
}

VOID
NTAPI
KiLa64SynchronizeInterruptMask(VOID)
{
    KiLa64UpdateInterruptMask();
}

/* Recompute the mask with interrupts masked. Only the 12:0 LIE field is
 * written, so nothing else in ECFG is disturbed. */
static
DECLSPEC_NOINLINE
VOID
KiLa64ChangeIrqlMasked(_In_ KIRQL NewIrql)
{
    BOOLEAN Interrupts = KeDisableInterrupts();

    KiLa64SetCurrentIrql(NewIrql);
    KiLa64UpdateInterruptMask();
    KeRestoreInterrupts(Interrupts);
}

KIRQL
FASTCALL
KfRaiseIrql(_In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KeGetCurrentIrql();

    if ((NewIrql < OldIrql) || (NewIrql > HIGH_LEVEL))
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     NewIrql,
                     OldIrql,
                     (ULONG_PTR)_ReturnAddress(),
                     0);
    }

    if (NewIrql == OldIrql)
        return OldIrql;

    /* A raise only has to remove lines; it cannot make a pending line
     * deliverable, so it needs no interrupt disable around the update. */
    KiLa64SetCurrentIrql(NewIrql);
    KiLa64UpdateInterruptMask();
    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(_In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KeGetCurrentIrql();

    if (NewIrql > OldIrql)
    {
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL,
                     NewIrql,
                     OldIrql,
                     (ULONG_PTR)_ReturnAddress(),
                     0);
    }

    if (NewIrql == OldIrql)
        return;

    /* Lowering can make a pending line deliverable immediately; do the store
     * and the mask update with interrupts off so the interrupt cannot be
     * taken between them with a stale CurrentIrql. */
    KiLa64ChangeIrqlMasked(NewIrql);
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

/* Internal entry points behind the exported IRQL routines, as on the other
 * 64-bit architectures. */
NTKERNELAPI
VOID
KxLowerIrql(_In_ KIRQL NewIrql)
{
    KfLowerIrql(NewIrql);
}

NTKERNELAPI
KIRQL
KxRaiseIrql(_In_ KIRQL NewIrql)
{
    return KfRaiseIrql(NewIrql);
}

NTKERNELAPI
KIRQL
KxRaiseIrqlToDpcLevel(VOID)
{
    return KeRaiseIrqlToDpcLevel();
}

KIRQL
NTAPI
KeGetEffectiveIrql(VOID)
{
    ULONG Crmd = KiLa64ReadCrmd();

    /* Nothing can preempt code running with interrupts masked. */
    if (!(Crmd & LA64_CRMD_IE))
        return HIGH_LEVEL;

    return KeGetCurrentIrql();
}

/* The HAL forwards its KeRaiseIrql export here, as on ARM64. */
#undef KeRaiseIrql
VOID
NTAPI
KeRaiseIrql(
    _In_ KIRQL NewIrql,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KfRaiseIrql(NewIrql);
}
