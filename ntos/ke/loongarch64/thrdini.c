/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 thread context creation, switch completion and
 *              idle loop
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

/*
 * Initial kernel stack of a thread that has not run yet:
 *
 *   InitialStack -> +-------------------+
 *                   |   KSTART_FRAME    |  popped by KiThreadStartup
 *                   +-------------------+
 *   KernelStack  -> |   KSWITCH_FRAME   |  Ra = KiThreadStartup, s0-s8 = 0
 *                   +-------------------+
 */
typedef struct _KKINIT_FRAME
{
    KSWITCH_FRAME SwitchFrame;
    KSTART_FRAME StartFrame;
} KKINIT_FRAME, *PKKINIT_FRAME;

C_ASSERT((sizeof(KKINIT_FRAME) & 15) == 0);

VOID
NTAPI
KiInitializeContextThread(
    _Inout_ PKTHREAD Thread,
    _In_opt_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_opt_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PCONTEXT Context)
{
    PKKINIT_FRAME InitFrame;
    ULONG_PTR StackPointer;
    SIZE_T FrameSize;
    PKTRAP_FRAME TrapFrame = NULL;

    Thread->CallbackStack = NULL;

    /* The boot thread is already executing on the loader-owned stack. It is
     * adopted in place: its first KSWITCH_FRAME is built when it switches. */
    if ((Thread == KeGetCurrentThread()) && (SystemRoutine == NULL) &&
        (StartRoutine == NULL) && (StartContext == NULL) && (Context == NULL))
    {
        __asm__ __volatile__("move %0, $sp" : "=r"(StackPointer) :: "memory");
        if ((StackPointer < (ULONG_PTR)Thread->StackLimit) ||
            (StackPointer >= (ULONG_PTR)Thread->StackBase) ||
            (StackPointer & 15))
        {
            KiLa64Unimplemented("KiInitializeContextThread/boot-stack");
        }

        Thread->PreviousMode = KernelMode;
        Thread->KernelStack = (PVOID)StackPointer;
        Thread->TrapFrame = NULL;
        return;
    }

    if (SystemRoutine == NULL)
        KiLa64Unimplemented("KiInitializeContextThread/no-system-routine");

    FrameSize = sizeof(KKINIT_FRAME) + (Context ? sizeof(KTRAP_FRAME) : 0);
    if (((ULONG_PTR)Thread->InitialStack & 15) ||
        ((ULONG_PTR)Thread->InitialStack - Thread->StackLimit < FrameSize))
    {
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     STATUS_BAD_INITIAL_STACK,
                     (ULONG_PTR)Thread,
                     (ULONG_PTR)Thread->InitialStack,
                     (ULONG_PTR)Thread->StackLimit);
    }

    if (Context)
    {
        /* Keep the complete user register bank above the startup frames.
         * Zero FP state even for an integer-only initial context, so the
         * eventual first return cannot expose another thread's registers. */
        TrapFrame = ((PKTRAP_FRAME)Thread->InitialStack) - 1;
        RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));
        TrapFrame->Context.ContextFlags = CONTEXT_FULL;
        KeContextToTrapFrame(Context, NULL, TrapFrame, Context->ContextFlags, UserMode);
        /* $tp is the thread's TEB for the frame the kernel returns through. */
        TrapFrame->Context.Tp = (ULONG_PTR)Thread->Teb;
        TrapFrame->Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
        TrapFrame->PreviousIrql = PASSIVE_LEVEL;
        InitFrame = ((PKKINIT_FRAME)TrapFrame) - 1;
    }
    else
    {
        InitFrame = ((PKKINIT_FRAME)Thread->InitialStack) - 1;
    }
    RtlZeroMemory(InitFrame, sizeof(*InitFrame));
    InitFrame->StartFrame.SystemRoutine = (ULONG64)SystemRoutine;
    InitFrame->StartFrame.StartRoutine = (ULONG64)StartRoutine;
    InitFrame->StartFrame.StartContext = (ULONG64)StartContext;
    InitFrame->SwitchFrame.Ra = (ULONG64)KiThreadStartup;
    InitFrame->SwitchFrame.ApcBypass = APC_LEVEL;

    Thread->PreviousMode = Context ? UserMode : KernelMode;
    Thread->KernelStack = &InitFrame->SwitchFrame;
    Thread->TrapFrame = TrapFrame;
}

/* Runs on the incoming thread's stack with the outgoing frame published. */
BOOLEAN
NTAPI
KiSwapContextResume(
    _In_ BOOLEAN ApcBypass,
    _In_ PKTHREAD OldThread,
    _In_ PKTHREAD NewThread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKPROCESS OldProcess, NewProcess;

    ASSERT(Prcb->CurrentThread == NewThread);
    if (Prcb->DpcRoutineActive)
    {
        KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC,
                     (ULONG_PTR)OldThread,
                     (ULONG_PTR)NewThread,
                     (ULONG_PTR)OldThread->InitialStack,
                     0);
    }

    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;
    if (OldProcess != NewProcess)
        KiSwapProcess(NewProcess, OldProcess);

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    /* Charge the old thread before another processor can run it. */
    KiChargeThreadCycleTime(Prcb, OldThread);
#endif
    Prcb->KeContextSwitches++;
    NewThread->ContextSwitches++;
#if KI_THREAD_HANDOFF_USES_RUNNING
    KiCompleteThreadSwitch(OldThread, NewThread);
#endif

    if (NewThread->ApcState.KernelApcPending)
    {
        if ((NewThread->SpecialApcDisable == 0) && !ApcBypass)
            return TRUE;
        HalRequestSoftwareInterrupt(APC_LEVEL);
    }
    return FALSE;
}

/* Every process root already carries the shared kernel half, so only the
 * root page changes. No ASID is allocated: kernel mappings are global. */
VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    KAFFINITY Member = KeGetCurrentPrcb()->SetMember;
    ULONG64 Root;

    InterlockedOr64((PLONG64)&NewProcess->ActiveProcessors, Member);
    ASSERT((NewProcess->DirectoryTableBase & (PAGE_SIZE - 1)) == 0);
    ASSERT(NewProcess->DirectoryTableBase != 0);

    Root = NewProcess->DirectoryTableBase;
    __asm__ __volatile__("csrwr %0, 0x19" :: "r"(Root) : "memory");
    __asm__ __volatile__("csrwr %0, 0x1a" :: "r"(Root) : "memory");
    /* Every entry of the outgoing address space is stale now. */
    KiLa64FlushCurrentAsidTlb();
    KiLa64MemoryBarrier();
    InterlockedAnd64((PLONG64)&OldProcess->ActiveProcessors, ~Member);
}

/* No per-thread coprocessor ownership exists yet: EUEN keeps FPE from the
 * handoff and there is no vector or LBT state to drop. */
VOID
NTAPI
KiRundownThread(_In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
}

static
VOID
KiLa64EnterIdleSubNode(_In_ PKPRCB Prcb)
{
    PKSCHEDULER_SUBNODE SubNode = Prcb->SchedulerSubNode;

    InterlockedOr64((PLONG64)&SubNode->IdleCpuSet, (LONG64)Prcb->SetMember);
    if ((Prcb->MultiThreadProcessorSet & ~SubNode->IdleCpuSet) == 0)
    {
        InterlockedOr64((PLONG64)&SubNode->IdleSmtSet, (LONG64)Prcb->MultiThreadProcessorSet);
        if ((Prcb->MultiThreadProcessorSet & ~SubNode->IdleCpuSet) != 0)
            InterlockedAnd64((PLONG64)&SubNode->IdleSmtSet, ~(LONG64)Prcb->MultiThreadProcessorSet);
    }
}

static
VOID
KiLa64LeaveIdleSubNode(_In_ PKPRCB Prcb)
{
    PKSCHEDULER_SUBNODE SubNode = Prcb->SchedulerSubNode;

    InterlockedAnd64((PLONG64)&SubNode->IdleCpuSet, ~(LONG64)Prcb->SetMember);
    InterlockedAnd64((PLONG64)&SubNode->IdleSmtSet, ~(LONG64)Prcb->MultiThreadProcessorSet);
}

static
VOID
KiLa64CreditIdleTicks(_In_ ULONG Ticks)
{
    KTRAP_FRAME TrapFrame;

    /* A zeroed frame names kernel mode (Pc 0), which is what idle time is. */
    RtlZeroMemory(&TrapFrame, sizeof(TrapFrame));
    KiUpdateRunTime(&TrapFrame, DISPATCH_LEVEL, Ticks);
}

FORCEINLINE
BOOLEAN
KiLa64IdleWorkPending(_In_ PKPRCB Prcb)
{
    return Prcb->NextThread || Prcb->DpcData[0].DpcQueueDepth || Prcb->TimerRequest ||
           Prcb->DeferredReadyListHead.Next || ReadNoFence(&Prcb->RequestSummary) ||
           KiIsDpcInterruptRequested(Prcb);
}

/* Whether any line interrupt is enabled at all. With ECFG.LIE empty no line
 * can wake the core, so sleeping would be forever. */
FORCEINLINE
BOOLEAN
KiLa64IdleInterruptsEnabled(VOID)
{
    return (KiLa64ReadEcfg() & LA64_ECFG_LIE_MASK) != 0;
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPCR Pcr = KeGetPcr();
    PKPRCB Prcb = &Pcr->Prcb;
    PKTHREAD OldThread, NewThread;
    BOOLEAN Lines;

    ASSERT(Prcb->CurrentThread == Prcb->IdleThread);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    for (;;)
    {
        KeDisableInterrupts();
        if ((Prcb->DpcData[0].DpcQueueDepth) ||
            (Prcb->TimerRequest) ||
            (Prcb->DeferredReadyListHead.Next) ||
            KiIsDpcInterruptRequested(Prcb))
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
        }

        Lines = KiLa64IdleInterruptsEnabled();
        if (Lines)
            KeRestoreInterrupts(TRUE);
        KiAcquirePrcbLock(Prcb);
        if (!Prcb->NextThread)
        {
            NewThread = KiIdleSchedule(Prcb);
            if (NewThread != Prcb->IdleThread)
            {
                NewThread->State = Standby;
                Prcb->NextThread = NewThread;
            }
        }
        NewThread = Prcb->NextThread;
        if (NewThread)
        {
            InterlockedAnd64((PLONG64)&KiIdleSummary, ~Prcb->SetMember);
            Prcb->NextThread = NULL;
            OldThread = Prcb->CurrentThread;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;
            KiReleasePrcbLock(Prcb);
            KiWaitForThreadSwapOut(NewThread);
            KiSwapContext(APC_LEVEL, OldThread);
            if (KeGetCurrentIrql() > DISPATCH_LEVEL)
                KfLowerIrql(DISPATCH_LEVEL);
            continue;
        }
        InterlockedOr64((PLONG64)&KiIdleSummary, Prcb->SetMember);
        KiReleasePrcbLock(Prcb);

        /* Check work with interrupts clear, then idle before enabling them.
         * A pending enabled line wakes `idle 0` even with CRMD.IE clear,
         * which closes the check/interrupt/sleep race without losing a
         * remote reschedule. */
        KeDisableInterrupts();
        KeMemoryBarrier();
        if (KiLa64IdleWorkPending(Prcb))
            continue;
        Prcb->Sleeping = TRUE;
        KeMemoryBarrier();
        if (KiLa64IdleWorkPending(Prcb))
        {
            Prcb->Sleeping = FALSE;
            continue;
        }
        KiLa64EnterIdleSubNode(Prcb);
        if (Lines)
            HalpLa64SuspendClockTick();
        __asm__ __volatile__("idle 0" ::: "memory");
        if (Lines)
        {
            ULONG Ticks = HalpLa64ResumeClockTick();

            if (Ticks)
                KiLa64CreditIdleTicks(Ticks);
        }
        Prcb->Sleeping = FALSE;
        KiLa64LeaveIdleSubNode(Prcb);
    }
}

/* First entry of a thread whose system routine returned: the prepared user
 * trap frame is the state to resume. */
DECLSPEC_NORETURN
VOID
NTAPI
KiLa64StartUserThread(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    if ((!Thread->Header.Minimal && !Thread->Teb) || !Thread->TrapFrame ||
        (Thread->TrapFrame != ((PKTRAP_FRAME)Thread->InitialStack) - 1))
    {
        KiLa64Unimplemented("KiLa64StartUserThread/invalid-frame");
    }
    KiLa64ReturnToUser(Thread->TrapFrame);
}
