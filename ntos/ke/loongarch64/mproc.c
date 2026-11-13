/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 application-processor startup
 *
 * HalStartNextProcessor on this port reports failure until the firmware or
 * platform interface that releases a secondary core exists (see the comment
 * at the top of hal/halloongarch64/smp.c). Everything the kernel side owes a
 * new processor is nevertheless here, so the day the HAL can start one this
 * path is what runs on it.
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

#define KI_LA64_AP_WAITING      0
#define KI_LA64_AP_ONLINE       1
#define KI_LA64_AP_ENTERED      2
#define KI_LA64_AP_ABANDONED    (-1)
#define KI_LA64_AP_ENTRY_TIMEOUT 2
#define KI_LA64_AP_START_TIMEOUT 10

typedef struct _KI_LA64_AP
{
    KPCR Pcr;
    ETHREAD Thread;
    PVOID Stack;
    volatile LONG Online;
    volatile LONG Stage;
    DECLSPEC_ALIGN(16) UCHAR PanicStack[KERNEL_STACK_SIZE];
} KI_LA64_AP;

static DECLSPEC_NORETURN VOID NTAPI
KiLa64StartProcessor(KI_LA64_AP *Ap)
{
    PKPCR Pcr = &Ap->Pcr;
    PKPRCB Prcb = &Pcr->Prcb;
    PKTHREAD Thread = &Ap->Thread.Tcb;
    PKPROCESS Process = (PKPROCESS)KeLoaderBlock->Process;

    /* Decision D-4: $r21 holds the KPCR base and nothing the compiler emits
     * touches it, so the first instruction on this core publishes it. */
    __asm__ __volatile__("move $r21, %0" :: "r"(Pcr) : "memory");

    if (InterlockedCompareExchange(&Ap->Online, KI_LA64_AP_ENTERED, KI_LA64_AP_WAITING) !=
        KI_LA64_AP_WAITING)
    {
        for (;;)
            __asm__ __volatile__("idle 0" ::: "memory");
    }
    if (!KiLa64InitializeTrapVector())
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, Prcb->Number, Pcr->CpuId, 0, 0);
    Ap->Stage = 1;
    Thread->ApcState.Process = Process;
    PoInitializePrcb(Prcb);
    KiLa64SaveProcessorClock(Prcb);
    ExInitPoolLookasidePointers();
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    KfLowerIrql(APC_LEVEL);
    KeInitializeThread(Process, Thread, NULL, NULL, NULL, NULL, NULL, Ap->Stack);
    Thread->NextProcessor = Thread->IdealProcessor = Prcb->Number;
    Thread->Priority = 0;
    Thread->State = Running;
    Thread->Running = TRUE;
    KiThreadAffinityMask(Thread) = Prcb->SetMember;
    Thread->WaitIrql = DISPATCH_LEVEL;
    InterlockedOr64((PLONG64)&Process->ActiveProcessors, Prcb->SetMember);
    Ap->Stage = 2;
    ExpInitializeExecutive(Prcb->Number, KeLoaderBlock);
    Ap->Stage = 3;
    KfRaiseIrql(HIGH_LEVEL);
    HalInitializeProcessor(Prcb->Number, KeLoaderBlock);
    Ap->Stage = 4;
    KiProcessorBlock[Prcb->Number] = Prcb;
    InterlockedOr64((PLONG64)&KiNode0.ProcessorMask, Prcb->SetMember);
    KiLa64JoinSchedulerSubNode(Prcb);
    InterlockedOr64((PLONG64)&KiIdleSummary, Prcb->SetMember);
    KeNumberProcessors = Prcb->Number + 1;
    InterlockedOr64((PLONG64)&KeActiveProcessors, Prcb->SetMember);
    /* Shootdowns target active processors only: drop whatever this core
     * cached while it initialized outside that set. */
    KeFlushCurrentTb();
    /* The initiator may recycle the trampoline only after this point. */
    InterlockedExchange(&Ap->Online, KI_LA64_AP_ONLINE);
    KfLowerIrql(DISPATCH_LEVEL);
    KeRestoreInterrupts(TRUE);
    KiIdleLoop();
}

/* Startup storage a processor that never came online no longer owns. */
static
VOID
KiLa64FreeAp(
    _In_ KI_LA64_AP *Ap,
    _In_opt_ PVOID DpcStack)
{
    if (DpcStack)
        MmDeleteKernelStack(DpcStack, FALSE);
    if (Ap->Stack)
        MmDeleteKernelStack(Ap->Stack, FALSE);
    ExFreePoolWithTag(Ap, TAG_KERNEL);
}

CODE_SEG("INIT") VOID NTAPI KeStartAllProcessors(VOID)
{
    ULONG Number, Maximum = KeMaximumProcessors;
    ULONG CpuId;
    KI_LA64_AP *Ap;
    LONG Progress;
    PVOID DpcStack;
    KPROCESSOR_STATE State;
    LARGE_INTEGER Start, Now, Frequency;

    if (KeNumprocSpecified)
        Maximum = min(Maximum, KeNumprocSpecified);
    if (KeBootprocSpecified)
        Maximum = min(Maximum, KeBootprocSpecified);
    for (Number = 1; Number < Maximum; ++Number)
    {
        CpuId = HalpLa64QueryProcessorId(Number);
        if (CpuId == MAXULONG)
            break;
        Ap = ExAllocatePoolZero(NonPagedPool, sizeof(*Ap), TAG_KERNEL);
        if (!Ap)
            break;
        Ap->Stack = MmCreateKernelStack(FALSE, 0);
        DpcStack = MmCreateKernelStack(FALSE, 0);
        if (!Ap->Stack || !DpcStack || !KiLa64AllocateInterruptTable(Number))
        {
            KiLa64FreeAp(Ap, DpcStack);
            break;
        }
        KiLa64InitializePcr(&Ap->Pcr, &Ap->Thread.Tcb, CpuId, Number, DpcStack,
                            Ap->PanicStack + sizeof(Ap->PanicStack));
        RtlZeroMemory(&State, sizeof(State));
        KiSaveProcessorControlState(&State);
        /* The new core adopts the system address space: both roots carry the
         * kernel half, and PGDL is the process root the boot processor uses. */
        State.SpecialRegisters.Pgdl = ((PKPROCESS)KeLoaderBlock->Process)->DirectoryTableBase;
        State.SpecialRegisters.Pgdh = State.SpecialRegisters.Pgdl;
        State.SpecialRegisters.Asid = 0;
        State.ContextFrame.Pc = (ULONG_PTR)KiLa64StartProcessor;
        /* Leave the common thread initializer's stack-control area untouched. */
        State.ContextFrame.Sp = (ULONG_PTR)Ap->Stack - PAGE_SIZE;
        State.ContextFrame.A0 = (ULONG_PTR)Ap;
        if (!HalStartNextProcessor(KeLoaderBlock, &State))
        {
            KiLa64FreeAp(Ap, DpcStack);
            break;
        }
        Start = KeQueryPerformanceCounter(&Frequency);
        while ((Progress = InterlockedCompareExchange(&Ap->Online, 0, 0)) != KI_LA64_AP_ONLINE)
        {
            Now = KeQueryPerformanceCounter(NULL);
            if (Progress == KI_LA64_AP_WAITING &&
                Now.QuadPart - Start.QuadPart > Frequency.QuadPart * KI_LA64_AP_ENTRY_TIMEOUT &&
                InterlockedCompareExchange(&Ap->Online, KI_LA64_AP_ABANDONED,
                                           KI_LA64_AP_WAITING) == KI_LA64_AP_WAITING)
            {
                break;
            }
            if (Now.QuadPart - Start.QuadPart > Frequency.QuadPart * KI_LA64_AP_START_TIMEOUT)
            {
                /* The platform accepted the start: the new core may still
                 * reference all startup storage. Never free it or continue a
                 * partial boot. */
                KeBugCheckEx(HAL_INITIALIZATION_FAILED, Number, CpuId,
                             (ULONG_PTR)Ap, Ap->Stage);
            }
            YieldProcessor();
        }
        if (Progress != KI_LA64_AP_ONLINE)
        {
            KiLa64FreeAp(Ap, DpcStack);
            break;
        }
        DPRINT1("LoongArch64: CPU %lu (id %lu) online\n", Number, CpuId);
    }
    DPRINT1("LoongArch64: %u processors online, affinity %Ix\n",
            (ULONG)(UCHAR)KeNumberProcessors, KeActiveProcessors);
}
