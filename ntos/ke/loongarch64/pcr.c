/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 per-processor state
 *
 * $r21 is the psABI's one non-allocatable GPR ([ABI]: "Reserved
 * (Non-allocatable)"), so the kernel owns it outright and keeps the KPCR
 * base there. Handoff decision D-4 fixes the value:
 *
 *     $r21 = LoaderBlock->Prcb - FIELD_OFFSET(KPCR, Prcb)
 *
 * and the loader deliberately puts nothing in $r21 (§2.2 of the handoff
 * contract). This file is where that becomes true for every processor; the
 * boot processor does it in KiLa64InitializeBootPcr, an application
 * processor in its own startup stub.
 */

#include <ntoskrnl.h>
#include "la64ke.h"

static DECLSPEC_ALIGN(16) UCHAR KiLa64PanicStack[KERNEL_STACK_SIZE];
KSCHEDULER_SUBNODE KiNode0SubNode;
PCONTEXT KiProcessorContext[MAXIMUM_PROCESSORS];

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    return KeGetCurrentPrcb()->Number;
}

/* internal/loongarch64/ke.h maps KeGetCurrentIrql to the KD-free inline; the
 * kernel still exports the vector-backed symbol every caller outside this
 * directory links against. */
#undef KeGetCurrentIrql
KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return KiLa64GetCurrentIrql();
}

static
VOID
KiLa64PublishPcrBase(
    _In_ PKPCR Pcr)
{
    /* From this instruction onward KeGetPcr() and the two inline fast paths
     * in ke.h are valid on this processor. The register is kernel-owned and
     * never saved or restored by a context switch. */
    __asm__ __volatile__("move $r21, %0" :: "r"(Pcr) : "memory");
}

BOOLEAN
NTAPI
KiLa64InitializeBootPcr(
    _Out_ PKPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ ULONG_PTR CpuId,
    _In_ PVOID DpcStack)
{
    ULONG Crmd;
    PKPRCB Prcb;

    /* Only the boot processor may enter here and only once. Failure must not
     * need an initialized PCR or the bugcheck path. */
    Crmd = KiLa64ReadCrmd();
    if ((Pcr == NULL) || (Thread == NULL) || (DpcStack == NULL) ||
        ((ULONG_PTR)DpcStack & 15) || (Crmd & LA64_CRMD_IE) ||
        (KeNumberProcessors != 0) || (KiProcessorBlock[0] != NULL))
    {
        return FALSE;
    }

    KiLa64InitializePcr(Pcr, Thread, CpuId, 0, DpcStack,
                        KiLa64PanicStack + sizeof(KiLa64PanicStack));
    Prcb = &Pcr->Prcb;

    /* Publish only initialized queues and processor identity. */
    KiLa64SetInterruptMask(0);
    KiLa64PublishPcrBase(Pcr);
    KiProcessorBlock[0] = Prcb;
    KeActiveProcessors = 1;
    KeMemoryBarrier();
    KeNumberProcessors = 1;
    return TRUE;
}

VOID
NTAPI
KiLa64InitializePcr(
    _Inout_ PKPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ ULONG_PTR CpuId,
    _In_ ULONG Number,
    _In_ PVOID DpcStack,
    _In_ PVOID PanicStack)
{
    PKPRCB Prcb;

    /* The caller owns resident PCR and bootstrap-thread storage. This does
     * not replace KeInitializeThread or initialize the idle process. */
    RtlZeroMemory(Pcr, sizeof(*Pcr));
    Pcr->CpuId = CpuId;
    Pcr->CurrentIrql = HIGH_LEVEL;
    Pcr->PanicStack = PanicStack;
    Prcb = &Pcr->Prcb;
    Prcb->CurrentThread = Thread;
    Prcb->IdleThread = Thread;
    Prcb->DpcStack = DpcStack;
    Prcb->Number = Number;
    /* PCONTEXT scratch the crash path expects per processor; KPRCB carries
     * the scheduler-visible processor state in ProcessorState. */
    KiProcessorContext[Number] = &Prcb->ProcessorState.ContextFrame;
#ifndef CONFIG_SMP
    Prcb->BuildType |= PRCB_BUILD_UNIPROCESSOR;
#endif
#if DBG
    Prcb->BuildType |= PRCB_BUILD_DEBUG;
#endif
    Prcb->SetMember = AFFINITY_MASK(Number);
    Prcb->ParentNode = &KiNode0;
    Prcb->MultiThreadProcessorSet = Prcb->SetMember;
    Prcb->MultiThreadSetMaster = Prcb;
    Prcb->GroupIndex = (UCHAR)Number;
    Prcb->CoresPerPhysicalProcessor = 1;
    Prcb->LogicalProcessorsPerCore = 1;
    Prcb->CoreProcessorSet = Prcb->SetMember;
    Prcb->SchedulerSubNode = &KiNode0SubNode;
    KiInitSpinLocks(Prcb, Number);

    Pcr->KdVersionBlock = Number ? KeGetPcr()->KdVersionBlock : NULL;
}

VOID
NTAPI
KiLa64JoinSchedulerSubNode(
    _In_ PKPRCB Prcb)
{
    PKSCHEDULER_SUBNODE SubNode = Prcb->SchedulerSubNode;

    InterlockedOr64((PLONG64)&SubNode->Affinity, (LONG64)Prcb->SetMember);
    InterlockedOr64((PLONG64)&SubNode->SiblingMask, (LONG64)Prcb->SetMember);
    InterlockedOr64((PLONG64)&SubNode->NonParkedSet, (LONG64)Prcb->SetMember);
    if (Prcb->Number > SubNode->Highest)
        SubNode->Highest = Prcb->Number;
}
