/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V thread context creation, switch completion and idle loop
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Initial kernel stack of a thread that has not run yet:
 *
 *   InitialStack -> +-------------------+
 *                   |   KSTART_FRAME    |  popped by KiThreadStartup
 *                   +-------------------+
 *   KernelStack  -> |   KSWITCH_FRAME   |  Ra = KiThreadStartup, s0-s11 = 0
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
        __asm__ __volatile__("mv %0, sp" : "=r"(StackPointer) :: "memory");
        if ((StackPointer < Thread->StackLimit) ||
            (StackPointer >= (ULONG_PTR)Thread->StackBase) ||
            (StackPointer & 15))
        {
            KiRiscvUnimplemented("KiInitializeContextThread/boot-stack");
        }

        Thread->PreviousMode = KernelMode;
        Thread->KernelStack = (PVOID)StackPointer;
        Thread->TrapFrame = NULL;
        return;
    }

    if (SystemRoutine == NULL)
        KiRiscvUnimplemented("KiInitializeContextThread/no-system-routine");

    if (Context)
        Thread->InitialStack = KiRiscvInitializeVectorArea(Thread, Thread->InitialStack);

    FrameSize = sizeof(KKINIT_FRAME) + (Context ? sizeof(KTRAP_FRAME) : 0);
    if (((ULONG_PTR)Thread->InitialStack & 15) ||
        ((ULONG_PTR)Thread->InitialStack - Thread->StackLimit < FrameSize))
    {
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     STATUS_BAD_INITIAL_STACK,
                     (ULONG_PTR)Thread,
                     (ULONG_PTR)Thread->InitialStack,
                     Thread->StackLimit);
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
        TrapFrame->Context.Tp = (ULONG_PTR)Thread->Teb;
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
 * root PPN changes. No ASID is allocated. */
VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    ULONG_PTR Satp;

    KAFFINITY Member = KeGetCurrentPrcb()->SetMember;
    InterlockedOr64((PLONG64)&NewProcess->ActiveProcessors, Member);
    ASSERT((NewProcess->DirectoryTableBase & (PAGE_SIZE - 1)) == 0);
    ASSERT(NewProcess->DirectoryTableBase != 0);

    Satp = RISCV64_LOADER_SATP_MODE_SV39 | (NewProcess->DirectoryTableBase >> PAGE_SHIFT);
    __asm__ __volatile__("csrw satp, %0\n\tsfence.vma zero, zero" :: "r"(Satp) : "memory");
    InterlockedAnd64((PLONG64)&OldProcess->ActiveProcessors, ~Member);
}

/* No per-thread coprocessor ownership or DPC affinity exists yet. */
VOID
NTAPI
KiRundownThread(_In_ PKTHREAD Thread)
{
    KiRiscvRundownVectorState(Thread);
}

/* Interrupt sources are enabled by the HAL alone. Until it enables one, a
 * wfi could only wake through a pending software request, so the idle hart
 * polls instead of sleeping; the interrupt enable bit stays clear so a new
 * thread inherits the boot-time masked state through KiSwapContext. */
FORCEINLINE
VOID
KiRiscvEnterFenceSleep(_Inout_ PKPCR Pcr)
{
    InterlockedOr(&Pcr->FenceState, KI_RISCV_FENCE_SLEEPING);
}

FORCEINLINE
VOID
KiRiscvLeaveFenceSleep(_Inout_ PKPCR Pcr)
{
    LONG Deferred = InterlockedExchange(&Pcr->FenceState, 0);

    if (Deferred & KI_RISCV_FENCE_TLB)
        __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
    if (Deferred & KI_RISCV_FENCE_ICACHE)
        __asm__ __volatile__("fence.i" ::: "memory");
}

FORCEINLINE
BOOLEAN
KiRiscvInterruptPending(VOID)
{
    ULONG_PTR Pending, Enabled;

    __asm__ __volatile__("csrr %0, sip\n\tcsrr %1, sie" : "=&r"(Pending), "=r"(Enabled));
    return (Pending & Enabled) != 0;
}

static
VOID
KiRiscvIdleWait(_In_ PKPCR Pcr)
{
    if (Pcr->InterruptEnable == 0)
    {
        YieldProcessor();
        return;
    }

    KiRiscvEnterFenceSleep(Pcr);
    __asm__ __volatile__("wfi" ::: "memory");
    KiRiscvLeaveFenceSleep(Pcr);
    _enable();
    _disable();
}

ULONG KiRiscvIdlePollMicroseconds = 200;

FORCEINLINE
BOOLEAN
KiRiscvIdleWorkPending(_In_ PKPRCB Prcb)
{
    return Prcb->NextThread || Prcb->DpcData[0].DpcQueueDepth || Prcb->TimerRequest ||
           Prcb->DeferredReadyListHead.Next || ReadNoFence(&Prcb->RequestSummary);
}

static
BOOLEAN
KiRiscvIdlePoll(
    _In_ PKPCR Pcr,
    _In_ PKPRCB Prcb)
{
    LARGE_INTEGER Frequency;
    ULONG64 Start, Ticks;
    KIRQL OldIrql;
    BOOLEAN Work;

    if (KiRiscvIdlePollMicroseconds == 0)
        return FALSE;

    Start = (ULONG64)KeQueryPerformanceCounter(&Frequency).QuadPart;
    Ticks = ((ULONG64)Frequency.QuadPart * KiRiscvIdlePollMicroseconds) / 1000000;

    Pcr->IdlePolling = TRUE;
    KiRiscvEnterFenceSleep(Pcr);
    for (;;)
    {
        Work = KiRiscvIdleWorkPending(Prcb);
        if (Work || ((ULONG64)KeQueryPerformanceCounter(NULL).QuadPart - Start >= Ticks))
            break;
        if (KiRiscvInterruptPending())
        {
            KiRiscvLeaveFenceSleep(Pcr);
            _enable();
            _disable();
            KiRiscvEnterFenceSleep(Pcr);
            continue;
        }
        YieldProcessor();
    }
    KiRiscvLeaveFenceSleep(Pcr);
    Pcr->IdlePolling = FALSE;
    KeMemoryBarrier();

    if (ReadNoFence(&Prcb->RequestSummary))
    {
        OldIrql = KfRaiseIrql(IPI_LEVEL);
        KiIpiProcessRequests();
        KfLowerIrql(OldIrql);
        Work = TRUE;
    }
    return Work || KiRiscvIdleWorkPending(Prcb);
}

static
VOID
KiRiscvCreditIdleTicks(_In_ ULONG Ticks)
{
    KTRAP_FRAME TrapFrame;

    TrapFrame.Sstatus = RISCV_SSTATUS_SPP;
    KiUpdateRunTime(&TrapFrame, DISPATCH_LEVEL, Ticks);
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPCR Pcr = KeGetPcr();
    PKPRCB Prcb = &Pcr->Prcb;
    PKTHREAD OldThread, NewThread;

    ASSERT(Prcb->CurrentThread == Prcb->IdleThread);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    for (;;)
    {
        _disable();
        if ((Prcb->DpcData[0].DpcQueueDepth) ||
            (Prcb->TimerRequest) ||
            (Prcb->DeferredReadyListHead.Next))
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
        }

        if (Pcr->InterruptEnable) _enable();
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
            KiSwapContext(APC_LEVEL, OldThread);
            if (KeGetCurrentIrql() > DISPATCH_LEVEL)
                KfLowerIrql(DISPATCH_LEVEL);
            continue;
        }
        InterlockedOr64((PLONG64)&KiIdleSummary, Prcb->SetMember);
        KiReleasePrcbLock(Prcb);

        /* Check work with SIE clear, then WFI before enabling it. A pending
         * enabled interrupt wakes WFI even with global SIE clear, closing the
         * check/interrupt/sleep race without losing a remote reschedule. */
        _disable();
        KeMemoryBarrier();
        if (Prcb->NextThread || Prcb->DpcData[0].DpcQueueDepth ||
            Prcb->TimerRequest || Prcb->DeferredReadyListHead.Next)
            continue;
        if (Pcr->InterruptEnable && KiRiscvIdlePoll(Pcr, Prcb))
            continue;
        Prcb->Sleeping = TRUE;
        if (Pcr->InterruptEnable)
            HalpRiscvSuspendClockTick();
        KiRiscvIdleWait(Pcr);
        if (Pcr->InterruptEnable)
        {
            ULONG Ticks = HalpRiscvResumeClockTick();

            if (Ticks)
                KiRiscvCreditIdleTicks(Ticks);
        }
        Prcb->Sleeping = FALSE;
    }
}
