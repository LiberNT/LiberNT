/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 translation-cache invalidation
 *
 * The INVTLB operation is a 5-bit immediate and takes operands in $rj/$rk
 * ([V1-4.2.4.7] Table 13, transcribed in internal/loongarch64/ke.h):
 *
 *   0x0  every entry, any ASID              KiLa64InvalidateAllTlb
 *   0x4  G=0 and ASID == $rj                KiLa64FlushCurrentAsidTlb
 *   0x6  G=1 or (ASID == $rj and VA == $rk) KeInvalidateTlbEntry
 *
 * A shootdown has to run on every target core, so it is expressed as a
 * generic IPI (KeIpiGenericCall) whose worker performs the invalidation in
 * the target's own context. That costs a full-machine rendezvous, but it is
 * the only mechanism this tree has for carrying an address to another core:
 * Prcb->RequestSummary is a bit set, not a payload. Flushing every core is
 * also always allowed to flush more than the caller asked for.
 */

#include <ntoskrnl.h>
#include "la64ke.h"

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    /* Every entry of this core: all ASIDs, global mappings included (op 0x0).
     * This does not change PGDL/PGDH and does not synchronize another core. */
    KiLa64InvalidateAllTlb();
}

typedef struct _KI_LA64_TLB_SHOOTDOWN
{
    PVOID Address;
    ULONG Pages;
} KI_LA64_TLB_SHOOTDOWN, *PKI_LA64_TLB_SHOOTDOWN;

static
ULONG_PTR
NTAPI
KiLa64TlbFlushWorker(
    _In_ ULONG_PTR Argument)
{
    PKI_LA64_TLB_SHOOTDOWN Flush = (PKI_LA64_TLB_SHOOTDOWN)Argument;
    ULONG Index;

    if ((Flush->Pages == 0) || (Flush->Pages > FLUSH_MULTIPLE_MAXIMUM))
    {
        /* No range given, or too many single-page invalidations to be worth
         * it: drop everything on this core (op 0x0). */
        KiLa64InvalidateAllTlb();
    }
    else
    {
        for (Index = 0; Index < Flush->Pages; Index++)
        {
            KeInvalidateTlbEntry((PUCHAR)Flush->Address + (SIZE_T)Index * PAGE_SIZE);
        }
    }
    return 0;
}

VOID
NTAPI
KiIpiSendTbFlush(
    KAFFINITY Targets,
    PVOID Address,
    ULONG Pages)
{
    KI_LA64_TLB_SHOOTDOWN Flush;
    KIRQL OldIrql = KeGetCurrentIrql();
    KAFFINITY Self = KeGetCurrentPrcb()->SetMember;
    ULONG Index;

    Targets &= KeActiveProcessors;

    if (Targets & Self)
    {
        if ((Pages == 0) || (Pages > FLUSH_MULTIPLE_MAXIMUM))
        {
            KeFlushCurrentTb();
        }
        else
        {
            for (Index = 0; Index < Pages; Index++)
            {
                KeInvalidateTlbEntry((PUCHAR)Address + (SIZE_T)Index * PAGE_SIZE);
            }
        }
    }

    Targets &= ~Self;
    if (Targets == 0)
        return;

    /* The worker's payload lives on this stack, and KeIpiGenericCall does not
     * return until every target has finished running it. */
    Flush.Address = Address;
    Flush.Pages = Pages;
    if (OldIrql < SYNCH_LEVEL)
        KfRaiseIrql(SYNCH_LEVEL);
    KeIpiGenericCall(KiLa64TlbFlushWorker, (ULONG_PTR)&Flush);
    KfLowerIrql(OldIrql);
}

VOID NTAPI
KeFlushProcessTb(VOID)
{
    /* One ASID owns the process address space, and every context switch
     * enters a root through PGDL, so the whole machine must drop its
     * translation of it. Broadcast covers a core that is switching into the
     * process right now. */
    KiIpiSendTbFlush(KeActiveProcessors, NULL, 0);
}

VOID NTAPI
KeFlushEntireTb(
    _In_ BOOLEAN Invalid,
    _In_ BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(Invalid);

    if (AllProcessors)
        KiIpiSendTbFlush(KeActiveProcessors, NULL, 0);
    else
        KeFlushCurrentTb();
}
