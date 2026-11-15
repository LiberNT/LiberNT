/*

#ifndef KI_ARCH_THREAD_HANDOFF_USES_RUNNING
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 0
#endif
#define KI_THREAD_HANDOFF_USES_RUNNING \
    ((NTDDI_VERSION >= NTDDI_WIN7) && KI_ARCH_THREAD_HANDOFF_USES_RUNNING)

FORCEINLINE
KPROCESSOR_MODE
KeGetPreviousMode(VOID)
{
    return KeGetCurrentThread()->PreviousMode;
}
#endif

FORCEINLINE
VOID
KiSetTargetProcessorDpc(
    _Inout_ PKDPC Dpc,
    _In_ CCHAR Number)
{
    Dpc->Number = (USHORT)(KI_DPC_TARGET_PROCESSOR_OFFSET + Number);
}

FORCEINLINE
VOID
KiRequestDispatchInterrupt(
    _In_ PKPRCB Prcb)
{
#ifdef _M_AMD64
    if (Prcb->CurrentThread == Prcb->IdleThread)
        return;
#else
    UNREFERENCED_PARAMETER(Prcb);
#endif
    HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
}

#ifdef KTHREAD_GROUP_AFFINITY
#define KiThreadAffinityMask(Thread) ((Thread)->Affinity.Mask)
#define KiThreadUserAffinityMask(Thread) ((Thread)->UserAffinity.Mask)
#else
#define KiThreadAffinityMask(Thread) ((Thread)->Affinity)
#define KiThreadUserAffinityMask(Thread) ((Thread)->UserAffinity)
#endif

#ifdef KI_CYCLE_QUANTUM
extern ULONG KiCyclesPerClockQuantum;

FORCEINLINE
ULONGLONG
KiQuantumUnitsToCycles(
    _In_ ULONG Quantum)
{
    return (ULONGLONG)Quantum * KiCyclesPerClockQuantum;
}

#define KiGetQuantumUnitCycles() ((ULONG)KiCyclesPerClockQuantum)

FORCEINLINE
BOOLEAN
KiIsThreadQuantumExpired(
    _In_ PKTHREAD Thread)
{
    return Thread->CycleTime >= Thread->QuantumTarget;
}

FORCEINLINE
LONG
KiGetThreadQuantum(
    _In_ PKTHREAD Thread)
{
    ULONGLONG Remaining;

    if (KiIsThreadQuantumExpired(Thread))
        return 0;

    Remaining = Thread->QuantumTarget - Thread->CycleTime;
    Remaining = (Remaining / KiCyclesPerClockQuantum) +
                ((Remaining % KiCyclesPerClockQuantum) != 0);
    return (Remaining > MAXLONG) ? MAXLONG : (LONG)Remaining;
}

FORCEINLINE
VOID
KiSetThreadQuantum(
    _Inout_ PKTHREAD Thread,
    _In_ LONG Quantum)
{
    ULONGLONG Cycles;

    Cycles = (Quantum > 0) ? KiQuantumUnitsToCycles((ULONG)Quantum) : 0;
    if (Cycles > (MAXULONGLONG - Thread->CycleTime))
        Thread->QuantumTarget = MAXULONGLONG;
    else
        Thread->QuantumTarget = Thread->CycleTime + Cycles;
}

FORCEINLINE
BOOLEAN
KiDecrementThreadQuantum(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG Decrement)
{
    ULONGLONG Cycles, Remaining;

    if (KiIsThreadQuantumExpired(Thread))
        return TRUE;

    Cycles = KiQuantumUnitsToCycles(Decrement);
    Remaining = Thread->QuantumTarget - Thread->CycleTime;
    if (Remaining <= Cycles)
    {
        Thread->QuantumTarget = Thread->CycleTime;
        return TRUE;
    }

    Thread->QuantumTarget -= Cycles;
    return FALSE;
}
#else
FORCEINLINE
BOOLEAN
KiIsThreadQuantumExpired(
    _In_ PKTHREAD Thread)
{
    return Thread->Quantum <= 0;
}

FORCEINLINE
LONG
KiGetThreadQuantum(
    _In_ PKTHREAD Thread)
{
    return Thread->Quantum;
}

FORCEINLINE
VOID
KiSetThreadQuantum(
    _Inout_ PKTHREAD Thread,
    _In_ LONG Quantum)
{
    Thread->Quantum = (SCHAR)Quantum;
}

FORCEINLINE
BOOLEAN
KiDecrementThreadQuantum(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG Decrement)
{
    LONG Quantum = (LONG)Thread->Quantum - (LONG)Decrement;

    Thread->Quantum = (SCHAR)max(Quantum, 0);
    return Quantum <= 0;
}

#define KiGetQuantumUnitCycles() 0UL
#endif

FORCEINLINE
VOID
{

}

FORCEINLINE
VOID
{

}

FORCEINLINE
VOID
{

}

FORCEINLINE
VOID
{

}







FORCEINLINE
VOID
KiAcquireDispatcherObject(IN DISPATCHER_HEADER* Object)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    while (InterlockedOr(&Object->Lock, KOBJECT_LOCK_BIT) & KOBJECT_LOCK_BIT)
    {
        do
        {
            YieldProcessor();
        } while (Object->Lock & KOBJECT_LOCK_BIT);
    }
}

FORCEINLINE
VOID
KiReleaseDispatcherObject(IN DISPATCHER_HEADER* Object)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    InterlockedAnd(&Object->Lock, ~KOBJECT_LOCK_BIT);
}

FORCEINLINE
VOID
KiAcquireWaitLock(IN PKPRCB Prcb)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

#if defined(_M_IX86)
    while (InterlockedExchange((PLONG)&Prcb->PrcbLock, 1) != 0)
    {
        while (*(volatile LONG*)&Prcb->PrcbLock != 0) YieldProcessor();
    }
#else
    C_ASSERT(sizeof(Prcb->WaitLock) == sizeof(PVOID));
    while (InterlockedExchangePointer((PVOID*)&Prcb->WaitLock, (PVOID)1) != NULL)
    {
        while (*(volatile ULONG_PTR*)&Prcb->WaitLock != 0) YieldProcessor();
    }
#endif
}

FORCEINLINE
VOID
KiReleaseWaitLock(IN PKPRCB Prcb)
{
#if defined(_M_IX86)
    InterlockedAnd((PLONG)&Prcb->PrcbLock, 0);
#else
    InterlockedExchangePointer((PVOID*)&Prcb->WaitLock, NULL);
#endif
}

FORCEINLINE
KIRQL
KiAcquireDispatcherLock(VOID)
{
    return KeAcquireQueuedSpinLockRaiseToSynch(LockQueueDispatcherLock);
}

FORCEINLINE
VOID
KiReleaseDispatcherLock(IN KIRQL OldIrql)
{
    KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->
                                        LockQueue[LockQueueDispatcherLock]);

    KiExitDispatcher(OldIrql);
}

FORCEINLINE
VOID
KiAcquireDispatcherLockAtSynchLevel(VOID)
{
    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);
    KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->
                                      LockQueue[LockQueueDispatcherLock]);
}

FORCEINLINE
VOID
KiReleaseDispatcherLockFromSynchLevel(VOID)
{
    KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->
                                        LockQueue[LockQueueDispatcherLock]);
}

FORCEINLINE
VOID
KiInsertDeferredReadyList(IN PKTHREAD Thread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    Thread->State = DeferredReady;

    PushEntryList(&Prcb->DeferredReadyListHead, &Thread->SwapListEntry);
}

FORCEINLINE
VOID
KiRescheduleThread(IN BOOLEAN NewThread,
                   IN ULONG Cpu)
{
    if ((NewThread) && !(KeGetCurrentPrcb()->Number == Cpu))
    {
        KiIpiSend(AFFINITY_MASK(Cpu), IPI_DPC);
    }
}

FORCEINLINE
VOID
KiSetThreadSwapBusy(IN PKTHREAD Thread)
{

    UNREFERENCED_PARAMETER(Thread);
}

FORCEINLINE
VOID
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    {
            YieldProcessor();
    }
}

FORCEINLINE
VOID
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{

}

FORCEINLINE
{
}

FORCEINLINE
VOID
KiCheckDeferredReadyList(IN PKPRCB Prcb)
{
    if (Prcb->DeferredReadyListHead.Next) KiProcessDeferredReadyList(Prcb);
}

FORCEINLINE
VOID
KiRequestApcInterrupt(IN BOOLEAN NeedApc,
                      IN UCHAR Processor)
{
    if (NeedApc)
    {
        if (KeGetCurrentPrcb()->Number != Processor)
        {
            KiIpiSend(AFFINITY_MASK(Processor), IPI_APC);
        }
        else
        {
            HalRequestSoftwareInterrupt(APC_LEVEL);
        }
    }
}

FORCEINLINE
PKSPIN_LOCK_QUEUE
KiAcquireTimerLock(IN ULONG Hand)
{
    PKSPIN_LOCK_QUEUE LockQueue;
    ULONG LockIndex;
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    LockIndex = Hand >> LOCK_QUEUE_TIMER_LOCK_SHIFT;
    LockIndex &= (LOCK_QUEUE_TIMER_TABLE_LOCKS - 1);

    LockQueue = KiPrcbTimerTableLockQueue(KeGetCurrentPrcb(), LockIndex);

    KeAcquireQueuedSpinLockAtDpcLevel(LockQueue);
    return LockQueue;
}

FORCEINLINE
VOID
KiReleaseTimerLock(IN PKSPIN_LOCK_QUEUE LockQueue)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KeReleaseQueuedSpinLockFromDpcLevel(LockQueue);
}

FORCEINLINE
VOID
KiAcquireApcLockRaiseToSynch(IN PKTHREAD Thread,
                 IN PKLOCK_QUEUE_HANDLE Handle)
{
    KeAcquireInStackQueuedSpinLockRaiseToSynch(&Thread->ApcQueueLock, Handle);
}

FORCEINLINE
VOID
KiAcquireApcLockAtSynchLevel(IN PKTHREAD Thread,
                           IN PKLOCK_QUEUE_HANDLE Handle)
{
    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);
    KeAcquireInStackQueuedSpinLockAtDpcLevel(&Thread->ApcQueueLock, Handle);
}

FORCEINLINE
VOID
KiReleaseApcLock(IN PKLOCK_QUEUE_HANDLE Handle)
{
    KeReleaseInStackQueuedSpinLock(Handle);
}

FORCEINLINE
VOID
KiReleaseApcLockFromSynchLevel(IN PKLOCK_QUEUE_HANDLE Handle)
{
    KeReleaseInStackQueuedSpinLockFromDpcLevel(Handle);
}

FORCEINLINE
VOID
KiAcquireProcessLockRaiseToSynch(IN PKPROCESS Process,
                     IN PKLOCK_QUEUE_HANDLE Handle)
{
    KeAcquireInStackQueuedSpinLockRaiseToSynch(&Process->ProcessLock, Handle);
}

FORCEINLINE
VOID
KiReleaseProcessLock(IN PKLOCK_QUEUE_HANDLE Handle)
{
    KeReleaseInStackQueuedSpinLock(Handle);
}

FORCEINLINE
VOID
KiReleaseProcessLockFromSynchLevel(IN PKLOCK_QUEUE_HANDLE Handle)
{
    KeReleaseInStackQueuedSpinLockFromDpcLevel(Handle);
}

FORCEINLINE
VOID
KiAcquireDeviceQueueLock(IN PKDEVICE_QUEUE DeviceQueue,
                         IN PKLOCK_QUEUE_HANDLE DeviceLock)
{
    if (KiIsDpcThreadActive(KeGetCurrentPrcb()))
    {
        KeAcquireInStackQueuedSpinLock(&DeviceQueue->Lock, DeviceLock);
    }
    else
    {
        ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
        KeAcquireInStackQueuedSpinLockAtDpcLevel(&DeviceQueue->Lock,
                                                 DeviceLock);
    }
}

FORCEINLINE
VOID
KiReleaseDeviceQueueLock(IN PKLOCK_QUEUE_HANDLE DeviceLock)
{
    if (KiIsDpcThreadActive(KeGetCurrentPrcb()))
    {
        KeReleaseInStackQueuedSpinLock(DeviceLock);
    }
    else
    {
        ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
        KeReleaseInStackQueuedSpinLockFromDpcLevel(DeviceLock);
    }
}

}

}

}

FORCEINLINE
PLARGE_INTEGER
KiRecalculateDueTime(IN PLARGE_INTEGER OriginalDueTime,
                     IN PLARGE_INTEGER DueTime,
                     IN OUT PLARGE_INTEGER NewDueTime)
{
    if (OriginalDueTime->QuadPart >= 0) return OriginalDueTime;

    NewDueTime->QuadPart = (LONGLONG)((ULONGLONG)KiQueryInterruptTimePrecise(NULL, FALSE) -
                                      (ULONGLONG)DueTime->QuadPart);
    return NewDueTime;
}

FORCEINLINE
BOOLEAN
{
}

#define KiAddThreadToWaitList(Thread, Swappable)                            \
{                                                                           \
    if (Swappable)                                                          \
    {                                                                       \
        PKPRCB _WaitPrcb = KeGetCurrentPrcb();                              \
        KiAcquireWaitLock(_WaitPrcb);                                       \
        Thread->WaitPrcb = _WaitPrcb;                                       \
        InsertTailList(&_WaitPrcb->WaitListHead,                            \
                       &Thread->WaitListEntry);                             \
        KiReleaseWaitLock(_WaitPrcb);                                       \
    }                                                                       \
}

FORCEINLINE
NTSTATUS
{
    NTSTATUS Status = STATUS_WAIT_0;

    KiAcquireThreadLock(Thread);

    {
            Status = STATUS_USER_APC;
    }
    {
        Status = STATUS_USER_APC;
    }

    KiReleaseThreadLock(Thread);
    return Status;
}

FORCEINLINE
ULONG
KiComputeTimerTableIndex(IN ULONGLONG DueTime)
{
    return (DueTime / KeMaximumIncrement) & (TIMER_TABLE_SIZE - 1);
}

FORCEINLINE
VOID
{

    ASSERT(Timer->TimerListEntry.Flink->Blink == &Timer->TimerListEntry);
    ASSERT(Timer->TimerListEntry.Blink->Flink == &Timer->TimerListEntry);
    if (RemoveEntryList(&Timer->TimerListEntry))
    {
    }
    {
        TableEntry->Time.QuadPart =
            CONTAINING_RECORD(TableEntry->Entry.Flink, KTIMER, TimerListEntry)->DueTime.QuadPart;
    }

}

FORCEINLINE
VOID
{
    PKSPIN_LOCK_QUEUE LockQueue;
    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);

    LockQueue = KiAcquireTimerLock(Hand);
    {
        return;
    }

}

FORCEINLINE
BOOLEAN
KxInsertThreadWaitTimer(IN PKTIMER Timer,
                        IN ULONG Hand)
{
    PKSPIN_LOCK_QUEUE LockQueue;
    BOOLEAN Armed = TRUE;

    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);
    ASSERT(Timer->Header.Inserted);

    LockQueue = KiAcquireTimerLock(Hand);

    if (KiInsertTimerTable(Timer, Hand))
    {
        KiRemoveEntryTimer(Timer);
        Timer->Header.Inserted = FALSE;
        Armed = FALSE;
    }

    KiReleaseTimerLock(LockQueue);
    KiReleaseDispatcherObject(&Timer->Header);
    return Armed;
}

FORCEINLINE
{

    {
    }

}

FORCEINLINE
BOOLEAN
{

    {
        Timer->Header.Absolute = TRUE;
    }

    Timer->Header.Hand = (UCHAR)*Hand;
    Timer->TimerListEntry.Flink = NULL;
    Timer->TimerListEntry.Blink = NULL;
    Timer->Header.Inserted = TRUE;
    return TRUE;
}

FORCEINLINE
BOOLEAN
{
    ULONG Hand = KiComputeTimerTableIndex(Timer->DueTime.QuadPart);

    {
        ASSERT(Timer->Header.Hand == (UCHAR)Hand);
        ASSERT((Timer->TimerListEntry.Flink == NULL) == (Timer->TimerListEntry.Blink == NULL));
        if (Timer->TimerListEntry.Flink != NULL)
    }

    KiReleaseTimerLock(LockQueue);
    return Removed;
}

FORCEINLINE
ULONGLONG
KiRoundToDefaultTimerResolution(IN ULONGLONG DueTime)
{
    ULONGLONG Phase = KiTickPhase;
    ULONGLONG Tick;

    if (DueTime <= Phase)
        return DueTime;

    Tick = (DueTime - Phase + KeMaximumIncrement - 1) / KeMaximumIncrement;
    Tick = Tick * KeMaximumIncrement + Phase - KeTimeIncrement / 16;
    return max(DueTime, Tick);
}

FORCEINLINE
VOID
{

    {
        Timer->Header.Absolute = TRUE;
    }


    Timer->Header.Hand = (UCHAR)*Hand;
}

FORCEINLINE
BOOLEAN
KiIsKernelApcDeliverable(IN PKTHREAD Thread,
                         IN KIRQL WaitIrql)
{
    return ((Thread->ApcState.KernelApcPending) &&
            !(Thread->SpecialApcDisable) &&
            (WaitIrql < APC_LEVEL));
}

FORCEINLINE
LONG
KiIncrementQueueCurrentCount(IN PKQUEUE Queue)
{
    return InterlockedIncrement((volatile LONG *)&Queue->CurrentCount);
}

FORCEINLINE
LONG
KiDecrementQueueCurrentCount(IN PKQUEUE Queue)
{
    return InterlockedDecrement((volatile LONG *)&Queue->CurrentCount);
}

FORCEINLINE
VOID
KxActivateWaiterQueue(IN PKTHREAD Thread)
{
    PKQUEUE Queue = Thread->Queue;

    if (Queue)
    {
        KiAcquireDispatcherObject(&Queue->Header);
        KiActivateWaiterQueue(Queue);
        KiReleaseDispatcherObject(&Queue->Header);
    }
}

FORCEINLINE
BOOLEAN
KxIsThreadWaitInterrupted(IN PKTHREAD Thread)
{
    if (Thread->Alertable)
    {
        return Thread->Alerted[(UCHAR)Thread->WaitMode] ||
               ((Thread->WaitMode != KernelMode) &&
                !IsListEmpty(&Thread->ApcState.ApcListHead[UserMode])) ||
               Thread->Alerted[KernelMode];
    }

    return (Thread->WaitMode != KernelMode) && Thread->ApcState.UserApcPending;
}

FORCEINLINE
BOOLEAN
KxTryBeginThreadWait(IN PKTHREAD Thread)
{
    KiAcquireThreadLock(Thread);
    if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql) ||
        KxIsThreadWaitInterrupted(Thread))
    {
        KiReleaseThreadLock(Thread);
        return FALSE;
    }

    return TRUE;
}

FORCEINLINE
VOID
KxExitDispatcherForRetry(IN PKTHREAD Thread)
{
    KIRQL WaitIrql = Thread->WaitIrql;

    KiReleaseDispatcherLock(WaitIrql);
    if (KiIsKernelApcDeliverable(Thread, WaitIrql))
        HalRequestSoftwareInterrupt(APC_LEVEL);
}

FORCEINLINE
VOID
KxCommitThreadWait(IN PKTHREAD Thread,
                   IN BOOLEAN Swappable)
{
    Thread->State = Waiting;
    KiAddThreadToWaitList(Thread, Swappable);
    ASSERT(Thread->WaitIrql <= DISPATCH_LEVEL);
    KiSetThreadSwapBusy(Thread);
    KiReleaseThreadLock(Thread);
}

#define KxChainTimerOnly()                                                  \
    Thread->WaitBlockCount = 1;                                             \
    Thread->TimerActive = FALSE
#define KxChainNextBlock(Array, Index)                                      \
    NOTHING
#define KxChainLastBlock(Array, Count)                                      \
    Thread->WaitBlockCount = (UCHAR)(Count);                                \
    Thread->TimerActive = FALSE
#define KxChainTimerToArray(Array)                                          \
    Thread->TimerActive = TRUE
#define KxChainSingleWithTimer()                                            \
    Thread->WaitBlockCount = 1;                                             \
    Thread->TimerActive = TRUE
#define KxChainSingleOnly()                                                 \
    Thread->WaitBlockCount = 1;                                             \
    Thread->TimerActive = FALSE

#define KxPublishTimerWaitBlock()                                           \
    InsertTailList(&Timer->Header.WaitListHead,                             \
                   &TimerBlock->WaitListEntry)

FORCEINLINE
VOID
{
    PKWAIT_BLOCK WaitBlock;


    do
    {
        if (WaitBlock->WaitType == WaitDpc)
            IopSignalWaitCompletionPacket(WaitBlock);
}

FORCEINLINE
BOOLEAN
KiConsumeSelfNextThread(IN PKPRCB Prcb,
                        IN PKTHREAD Thread)
{
    if (Prcb->NextThread != Thread) return FALSE;

    Prcb->NextThread = NULL;
    if (Thread->State == Standby) Thread->State = Running;
    return TRUE;
}

FORCEINLINE
VOID
KiWaitForThreadSwapOut(IN PKTHREAD Thread)
{
    while (*(volatile BOOLEAN *)&Thread->Running)
    {
        YieldProcessor();
    }
#else
    UNREFERENCED_PARAMETER(Thread);
#endif
    KeMemoryBarrier();
}

#if KI_THREAD_HANDOFF_USES_RUNNING
FORCEINLINE
VOID
KiCompleteThreadSwitch(
    _In_ PKTHREAD OldThread,
    _In_ PKTHREAD NewThread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    BOOLEAN ReadyTransition, ReapThread;

    NewThread->Running = TRUE;
    KeMemoryBarrier();

    KiAcquireThreadLock(OldThread);
    KiAcquirePrcbLock(Prcb);
    ASSERT(OldThread->NextProcessor == Prcb->Number);
    ReadyTransition = OldThread->ReadyTransition;
    if (ReadyTransition)
        OldThread->ReadyTransition = FALSE;
    ReapThread = (OldThread->State == Terminated);
    ASSERT(!ReadyTransition || (OldThread->State == DeferredReady));
    ASSERT(!ReadyTransition || !ReapThread);
    KeMemoryBarrier();
    OldThread->Running = FALSE;
    KiReleasePrcbLock(Prcb);
    KiReleaseThreadLock(OldThread);

    if (ReadyTransition)
    {
        KiDeferredReadyThread(OldThread);
        if (Prcb->NextThread != NULL)
            KiRequestDispatchInterrupt(Prcb);
    }
    else if (ReapThread)
        KiQueueThreadForReaping(OldThread);
}
#endif

FORCEINLINE
BOOLEAN
KiThreadLeavingProcessor(
    _In_ PKTHREAD Thread)
{
#if KI_THREAD_HANDOFF_USES_RUNNING
    return *(volatile BOOLEAN *)&Thread->Running;
#else
    UNREFERENCED_PARAMETER(Thread);
    return FALSE;
#endif
}

FORCEINLINE
BOOLEAN
KiDeferReadyWhileLeavingProcessor(
    _Inout_ PKTHREAD Thread)
{
#if KI_THREAD_HANDOFF_USES_RUNNING
    PKPRCB Prcb;
    BOOLEAN Leaving;

    ASSERT(Thread->State == DeferredReady);
    if (!*(volatile BOOLEAN *)&Thread->Running)
    {
        KeMemoryBarrier();
        return FALSE;
    }

    ASSERT(Thread->NextProcessor < (ULONG)KeNumberProcessors);
    Prcb = KiProcessorBlock[Thread->NextProcessor];
    KiAcquirePrcbLock(Prcb);
    Leaving = Thread->Running;
    if (Leaving)
    {
        ASSERT(!Thread->ReadyTransition);
        Thread->ReadyTransition = TRUE;
    }
    KiReleasePrcbLock(Prcb);
    return Leaving;
#else
    UNREFERENCED_PARAMETER(Thread);
    return FALSE;
#endif
}

FORCEINLINE
VOID
KiAssertReadyQueue(IN PKPRCB Prcb,
                   IN KPRIORITY Priority)
{
    UNREFERENCED_PARAMETER(Prcb);
    UNREFERENCED_PARAMETER(Priority);
    ASSERT(Priority >= 0 && Priority <= HIGH_PRIORITY);
    ASSERT(((Prcb->ReadySummary & PRIORITY_MASK(Priority)) == 0) ==
           IsListEmpty(&Prcb->DispatcherReadyListHead[Priority]));
    ASSERT(Prcb->DispatcherReadyListHead[Priority].Flink->Blink == &Prcb->DispatcherReadyListHead[Priority]);
    ASSERT(Prcb->DispatcherReadyListHead[Priority].Blink->Flink == &Prcb->DispatcherReadyListHead[Priority]);
}

_Requires_lock_held_(Prcb->PrcbLock)
_Releases_lock_(Prcb->PrcbLock)
FORCEINLINE
VOID
{

    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(Prcb->PrcbLock != 0);

    if (Thread == Prcb->IdleThread)
    {
        KiReleasePrcbLock(Prcb);
        return;
    }

    ASSERT(Thread->State == Running);
    ASSERT(Thread->NextProcessor == Prcb->Number);

    {
        Thread->State = DeferredReady;
        KiReleasePrcbLock(Prcb);
        KiDeferredReadyThread(Thread);
    }
}

FORCEINLINE
PKTHREAD
{
    PKTHREAD Thread;

    if (Priority > HIGH_PRIORITY)
        return NULL;

    for (;;)
    {



        ASSERT(Thread != Prcb->IdleThread);
        ASSERT(Thread->State == Ready && !Thread->ProcessReadyQueue);
        ASSERT(Thread->NextProcessor == Prcb->Number);


        {
        }

    }
}

FORCEINLINE
KPRIORITY
KiGetUnflooredPriority(IN PKTHREAD Thread)
{
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_WIN10)
    if (Thread->PriorityFloorSummary)
    {
        if (Thread->BasePriority >= LOW_REALTIME_PRIORITY)
            return Thread->BasePriority;
        return min(Thread->BasePriority + Thread->DecayBoost,
                   LOW_REALTIME_PRIORITY - 1);
    }
#endif
    return Thread->Priority;
}

FORCEINLINE
VOID
KiUpdatePriorityFloor(IN PKTHREAD Thread,
                     IN KPRIORITY OldFloor,
                     IN KPRIORITY NewFloor)
{
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_WIN10)
    if (OldFloor == NewFloor)
        return;
    if (NewFloor)
    {
        ASSERT(NewFloor <= HIGH_PRIORITY);
        ASSERT(Thread->PriorityFloorCounts[NewFloor] != MAXUCHAR);
        ++Thread->PriorityFloorCounts[NewFloor];
        Thread->PriorityFloorSummary |= PRIORITY_MASK(NewFloor);
    }
    if (OldFloor)
    {
        ASSERT(OldFloor <= HIGH_PRIORITY);
        ASSERT(Thread->PriorityFloorCounts[OldFloor] != 0);
        if (--Thread->PriorityFloorCounts[OldFloor] == 0)
            Thread->PriorityFloorSummary &= ~PRIORITY_MASK(OldFloor);
    }
#else
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(OldFloor);
    UNREFERENCED_PARAMETER(NewFloor);
#endif
}

FORCEINLINE
KPRIORITY
KiApplyPriorityFloor(IN PKTHREAD Thread,
                     IN KPRIORITY Priority)
{
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_WIN10)
    ULONG Floor;

    Thread->DecayBoost = (Priority < LOW_REALTIME_PRIORITY) ?
                        (CHAR)max(Priority - Thread->BasePriority, 0) : 0;
    if (Thread->PriorityFloorSummary)
    {
        BitScanReverse(&Floor, Thread->PriorityFloorSummary);
        Priority = max(Priority, (KPRIORITY)Floor);
    }
#else
    UNREFERENCED_PARAMETER(Thread);
#endif
    return Priority;
}

FORCEINLINE
SCHAR
{


    if (Priority < LOW_REALTIME_PRIORITY)
    {
        Thread->PriorityDecrement = 0;
    }

}

FORCEINLINE
VOID
{
    GuardedMutex->Count = GM_LOCK_BIT;
    GuardedMutex->Owner = NULL;
    GuardedMutex->Contention = 0;
    KeInitializeGate(&GuardedMutex->Gate);
}

FORCEINLINE
{

    ASSERT(GuardedMutex->Owner != Thread);

    if (!InterlockedBitTestAndReset(&GuardedMutex->Count, GM_LOCK_BIT_V))
        KiAcquireGuardedMutex(GuardedMutex);

    ASSERT((GuardedMutex->Count & GM_LOCK_BIT) == 0);
    ASSERT(GuardedMutex->Owner == NULL);
    GuardedMutex->Owner = Thread;
}

FORCEINLINE
VOID
{

    GuardedMutex->Owner = NULL;



    }
}

FORCEINLINE
VOID
{
    PKTHREAD Thread = KeGetCurrentThread();





    GuardedMutex->SpecialApcDisable = Thread->SpecialApcDisable;
}

FORCEINLINE
VOID
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(GuardedMutex->Owner == Thread);
    ASSERT(Thread->SpecialApcDisable == GuardedMutex->SpecialApcDisable);

}

FORCEINLINE
BOOLEAN
{
    PKTHREAD Thread = KeGetCurrentThread();


    if (!InterlockedBitTestAndReset(&GuardedMutex->Count, GM_LOCK_BIT_V))
    {
        YieldProcessor();
        return FALSE;
    }

    ASSERT((GuardedMutex->Count & GM_LOCK_BIT) == 0);
    ASSERT(GuardedMutex->Owner == NULL);

    GuardedMutex->Owner = Thread;
    GuardedMutex->SpecialApcDisable = Thread->SpecialApcDisable;
    return TRUE;
}

#if defined(_M_IX86) || defined(_M_AMD64)
FORCEINLINE
VOID
KiCpuId(
    PCPU_INFO CpuInfo,
    ULONG Function)
{
    __cpuid((INT*)CpuInfo->AsUINT32, Function);
}

FORCEINLINE
VOID
KiCpuIdEx(
    PCPU_INFO CpuInfo,
    ULONG Function,
    ULONG SubFunction)
{
    __cpuidex((INT*)CpuInfo->AsUINT32, Function, SubFunction);
}
#endif
