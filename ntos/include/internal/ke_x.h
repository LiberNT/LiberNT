/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Dispatcher, scheduler, timer and lock inline helpers
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef KI_ARCH_THREAD_HANDOFF_USES_RUNNING
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 0
#endif

#define KI_THREAD_HANDOFF_USES_RUNNING \
    ((NTDDI_VERSION >= NTDDI_WIN7) && KI_ARCH_THREAD_HANDOFF_USES_RUNNING)

#ifndef _KeGetPreviousMode
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
    if (KiArchIdleLoopPollsForWork() && (Prcb->CurrentThread == Prcb->IdleThread))
        return;

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
KiEnterGuardedRegionThread(
    _In_ PKTHREAD Thread)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(Thread == KeGetCurrentThread());
    ASSERT(Thread->SpecialApcDisable <= 0 && Thread->SpecialApcDisable != (SHORT)MINSHORT);

    Thread->SpecialApcDisable--;
    _ReadWriteBarrier();
}

FORCEINLINE
VOID
KiLeaveGuardedRegionThread(
    _In_ PKTHREAD Thread)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(Thread == KeGetCurrentThread());
    ASSERT(Thread->SpecialApcDisable < 0);

    _ReadWriteBarrier();
    if (++Thread->SpecialApcDisable == 0 && !IsListEmpty(&Thread->ApcState.ApcListHead[KernelMode]))
        KiCheckForKernelApcDelivery();
}

FORCEINLINE
VOID
KiEnterCriticalRegionThread(
    _In_ PKTHREAD Thread)
{
    ASSERT(Thread == KeGetCurrentThread());
    ASSERT(Thread->KernelApcDisable <= 0 && Thread->KernelApcDisable != (SHORT)MINSHORT);

    Thread->KernelApcDisable--;
    _ReadWriteBarrier();
}

FORCEINLINE
VOID
KiLeaveCriticalRegionThread(
    _In_ PKTHREAD Thread)
{
    ASSERT(Thread == KeGetCurrentThread());
    ASSERT(Thread->KernelApcDisable < 0);

    _ReadWriteBarrier();
    if (++Thread->KernelApcDisable == 0 && Thread->SpecialApcDisable == 0 &&
        !IsListEmpty(&Thread->ApcState.ApcListHead[KernelMode]))
    {
        KiCheckForKernelApcDelivery();
    }
}

#define KeEnterGuardedRegionThread(Thread) KiEnterGuardedRegionThread(Thread)

#define KeLeaveGuardedRegionThread(Thread) KiLeaveGuardedRegionThread(Thread)

#define KeEnterGuardedRegion() KiEnterGuardedRegionThread(KeGetCurrentThread())

#define KeLeaveGuardedRegion() KiLeaveGuardedRegionThread(KeGetCurrentThread())

#define KeEnterCriticalRegion() KiEnterCriticalRegionThread(KeGetCurrentThread())

#define KeLeaveCriticalRegion() KiLeaveCriticalRegionThread(KeGetCurrentThread())

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
KiAcquireFlagLock(
    _Inout_ volatile KSPIN_LOCK *Lock)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    while (InterlockedExchange((PLONG)Lock, 1) != 0)
    {
        while (*Lock != 0)
            YieldProcessor();
    }
}

FORCEINLINE
VOID
KiReleaseFlagLock(
    _Inout_ volatile KSPIN_LOCK *Lock)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(*Lock != 0);

    InterlockedAnd((PLONG)Lock, 0);
}

FORCEINLINE
VOID
KiAcquirePrcbLock(
    _Inout_ PKPRCB Prcb)
{
    KiAcquireFlagLock(&Prcb->PrcbLock);
}

FORCEINLINE
VOID
KiReleasePrcbLock(
    _Inout_ PKPRCB Prcb)
{
    KiReleaseFlagLock(&Prcb->PrcbLock);
}

FORCEINLINE
VOID
KiAcquireThreadLock(
    _Inout_ PKTHREAD Thread)
{
    KiAcquireFlagLock(&Thread->ThreadLock);
}

FORCEINLINE
VOID
KiReleaseThreadLock(
    _Inout_ PKTHREAD Thread)
{
    KiReleaseFlagLock(&Thread->ThreadLock);
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

FORCEINLINE
VOID
KiSatisfyMutantWait(
    _Inout_ PKMUTANT Mutant,
    _Inout_ PKTHREAD Thread)
{
    if (--Mutant->Header.SignalState != 0)
        return;

    Mutant->OwnerThread = Thread;
    Thread->KernelApcDisable -= Mutant->ApcDisable;
    if (Mutant->Abandoned)
    {
        Mutant->Abandoned = FALSE;
        Thread->WaitStatus = STATUS_ABANDONED;
    }

    InsertHeadList(Thread->MutantListHead.Blink, &Mutant->MutantListEntry);
}

FORCEINLINE
VOID
KiSatisfyNonMutantWait(
    _Inout_ PVOID Object)
{
    PDISPATCHER_HEADER Header = Object;

    if ((Header->Type & TIMER_OR_EVENT_TYPE) == EventSynchronizationObject)
        Header->SignalState = 0;
    else if ((Header->Type & KOBJECT_TYPE_MASK) == SemaphoreObject)
        Header->SignalState--;
}

FORCEINLINE
VOID
KiSatisfyObjectWait(
    _Inout_ PVOID Object,
    _Inout_ PKTHREAD Thread)
{
    if ((((PDISPATCHER_HEADER)Object)->Type & KOBJECT_TYPE_MASK) == MutantObject)
        KiSatisfyMutantWait(Object, Thread);
    else
        KiSatisfyNonMutantWait(Object);
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
KiCheckThreadStackSwap(
    _In_ PKTHREAD Thread,
    _In_ KPROCESSOR_MODE WaitMode)
{
    return (BOOLEAN)(WaitMode != KernelMode && Thread->EnableStackSwap &&
                     Thread->Priority >= LOW_REALTIME_PRIORITY + 9);
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
KiCheckAlertability(
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN Alertable,
    _In_ KPROCESSOR_MODE WaitMode)
{
    NTSTATUS Status = STATUS_WAIT_0;

    ASSERT(WaitMode == KernelMode || WaitMode == UserMode);

    if (!Alertable && WaitMode == KernelMode)
        return STATUS_WAIT_0;

    KiAcquireThreadLock(Thread);

    if (!Alertable)
    {
        if (Thread->ApcState.UserApcPending)
            Status = STATUS_USER_APC;
    }
    else if (Thread->Alerted[(UCHAR)WaitMode])
    {
        Thread->Alerted[(UCHAR)WaitMode] = FALSE;
        Status = STATUS_ALERTED;
    }
    else if (WaitMode == UserMode && !IsListEmpty(&Thread->ApcState.ApcListHead[UserMode]))
    {
        Thread->ApcState.UserApcPending = TRUE;
        Status = STATUS_USER_APC;
    }
    else if (Thread->Alerted[KernelMode])
    {
        Thread->Alerted[KernelMode] = FALSE;
        Status = STATUS_ALERTED;
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
KiUnlinkTimerFromTable(
    _Inout_ PKTIMER Timer,
    _Inout_ PKTIMER_TABLE_ENTRY TableEntry)
{
    BOOLEAN WasFirst = (BOOLEAN)(TableEntry->Entry.Flink == &Timer->TimerListEntry);

    ASSERT(Timer->TimerListEntry.Flink->Blink == &Timer->TimerListEntry);
    ASSERT(Timer->TimerListEntry.Blink->Flink == &Timer->TimerListEntry);

    if (RemoveEntryList(&Timer->TimerListEntry))
    {
        TableEntry->Time.HighPart = 0xFFFFFFFF;
    }
    else if (WasFirst)
    {
        TableEntry->Time.QuadPart =
            CONTAINING_RECORD(TableEntry->Entry.Flink, KTIMER, TimerListEntry)->DueTime.QuadPart;
    }

    Timer->TimerListEntry.Flink = NULL;
    Timer->TimerListEntry.Blink = NULL;
}

FORCEINLINE
VOID
KiRemoveEntryTimer(
    _Inout_ PKTIMER Timer)
{
    ULONG Hand = KiComputeTimerTableIndex(Timer->DueTime.QuadPart);

    ASSERT(Hand < TIMER_TABLE_SIZE);
    ASSERT(Timer->Header.Hand == (UCHAR)Hand);
    ASSERT(Timer->TimerListEntry.Flink != NULL && Timer->TimerListEntry.Blink != NULL);

    KiUnlinkTimerFromTable(Timer, &KiTimerTableListHead[Hand]);
}

FORCEINLINE
VOID
KxInsertTimer(
    _Inout_ PKTIMER Timer,
    _In_ ULONG Hand)
{
    PKSPIN_LOCK_QUEUE LockQueue;

    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);

    LockQueue = KiAcquireTimerLock(Hand);
    if (Timer->Header.Inserted && KiInsertTimerTable(Timer, Hand))
    {
        KiCompleteTimer(Timer, LockQueue);
        return;
    }

    KiReleaseTimerLock(LockQueue);
    KiReleaseDispatcherObject(&Timer->Header);
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
BOOLEAN
KiDueTimeToDeadline(
    _In_ LARGE_INTEGER DueTime,
    _Out_ PBOOLEAN Absolute,
    _Out_ PULONGLONG Deadline)
{
    LARGE_INTEGER Now;

    /* A non-negative due time is an absolute system time, a negative one an interval */
    *Absolute = (BOOLEAN)(DueTime.HighPart >= 0);
    if (*Absolute)
    {
        KeQuerySystemTimePrecise(&Now);
        DueTime.QuadPart = Now.QuadPart - DueTime.QuadPart;
        if (DueTime.HighPart >= 0)
            return FALSE;
    }

    *Deadline = (ULONGLONG)KiQueryInterruptTimePrecise(NULL, FALSE) - (ULONGLONG)DueTime.QuadPart;
    return TRUE;
}

FORCEINLINE
BOOLEAN
KiComputeDueTime(
    _Inout_ PKTIMER Timer,
    _In_ LARGE_INTEGER DueTime,
    _Out_ PULONG Hand)
{
    ULONGLONG Deadline = 0;
    BOOLEAN Absolute;

    if (!KiDueTimeToDeadline(DueTime, &Absolute, &Deadline))
    {
        Timer->Header.Absolute = TRUE;
        Timer->Header.SignalState = TRUE;
        Timer->Header.Hand = 0;
        Timer->DueTime.QuadPart = 0;
        *Hand = 0;
        return FALSE;
    }

    Timer->Header.Absolute = Absolute;
    Timer->DueTime.QuadPart = Deadline;
    *Hand = KiComputeTimerTableIndex(Deadline);
    Timer->Header.Hand = (UCHAR)*Hand;
    Timer->TimerListEntry.Flink = NULL;
    Timer->TimerListEntry.Blink = NULL;
    Timer->Header.Inserted = TRUE;
    return TRUE;
}

FORCEINLINE
BOOLEAN
KxRemoveTreeTimer(
    _Inout_ PKTIMER Timer)
{
    ULONG Hand = KiComputeTimerTableIndex(Timer->DueTime.QuadPart);
    PKSPIN_LOCK_QUEUE LockQueue = KiAcquireTimerLock(Hand);
    BOOLEAN Removed = Timer->Header.Inserted;

    if (Removed)
    {
        ASSERT(Timer->Header.Hand == (UCHAR)Hand);
        ASSERT((Timer->TimerListEntry.Flink == NULL) == (Timer->TimerListEntry.Blink == NULL));

        Timer->Header.Inserted = FALSE;
        if (Timer->TimerListEntry.Flink != NULL)
            KiUnlinkTimerFromTable(Timer, &KiTimerTableListHead[Hand]);
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
KxSetTimerForThreadWait(
    _Inout_ PKTIMER Timer,
    _In_ LARGE_INTEGER Interval,
    _In_ KPROCESSOR_MODE WaitMode,
    _Out_ PULONG Hand)
{
    ULONGLONG Deadline = 0;
    BOOLEAN Absolute;

    if (!KiDueTimeToDeadline(Interval, &Absolute, &Deadline))
    {
        Timer->Header.Absolute = TRUE;
        Timer->DueTime.QuadPart = 0;
        Timer->Header.Hand = 0;
        *Hand = 0;
        return;
    }

    if (WaitMode == UserMode && !((PEPROCESS)KeGetCurrentThread()->ApcState.Process)->SetTimerResolution)
        Deadline = KiRoundToDefaultTimerResolution(Deadline);

    Timer->Header.Absolute = Absolute;
    Timer->DueTime.QuadPart = Deadline;
    *Hand = KiComputeTimerTableIndex(Deadline);
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
KxUnwaitThread(
    _Inout_ DISPATCHER_HEADER *Object,
    _In_ KPRIORITY Increment)
{
    PLIST_ENTRY WaitList = &Object->WaitListHead;
    PKWAIT_BLOCK WaitBlock;

    ASSERT(!IsListEmpty(WaitList));

    do
    {
        WaitBlock = CONTAINING_RECORD(WaitList->Flink, KWAIT_BLOCK, WaitListEntry);
        if (WaitBlock->WaitType == WaitDpc)
            IopSignalWaitCompletionPacket(WaitBlock);
        else
            KiUnwaitThread(WaitBlock->Thread,
                           (WaitBlock->WaitType == WaitAny) ? WaitBlock->WaitKey : STATUS_KERNEL_APC,
                           Increment);
    } while (!IsListEmpty(WaitList));
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
#if KI_THREAD_HANDOFF_USES_RUNNING
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

FORCEINLINE
VOID
KiReadyQueueInsert(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY Priority,
    _In_ BOOLEAN AtHead)
{
    PLIST_ENTRY Queue = &Prcb->DispatcherReadyListHead[Priority];

    ASSERT(Priority >= 0 && Priority <= HIGH_PRIORITY);
    KiAssertReadyQueue(Prcb, Priority);

    if (AtHead)
        InsertHeadList(Queue, &Thread->WaitListEntry);
    else
        InsertTailList(Queue, &Thread->WaitListEntry);

    Prcb->ReadySummary |= PRIORITY_MASK(Priority);
    KiAssertReadyQueue(Prcb, Priority);
}

FORCEINLINE
VOID
KiReadyQueueRemove(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY Priority)
{
    ASSERT(Thread->WaitListEntry.Flink->Blink == &Thread->WaitListEntry);
    ASSERT(Thread->WaitListEntry.Blink->Flink == &Thread->WaitListEntry);

    if (KiEevdfEnabled)
        KiEevdfUnlist(Prcb, Thread);

    if (RemoveEntryList(&Thread->WaitListEntry))
        Prcb->ReadySummary &= ~PRIORITY_MASK(Priority);

    KiAssertReadyQueue(Prcb, Priority);
}

_Requires_lock_held_(Prcb->PrcbLock)
_Releases_lock_(Prcb->PrcbLock)
FORCEINLINE
VOID
KxQueueReadyThread(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKPRCB Prcb)
{
    BOOLEAN AtHead;

    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(Prcb->PrcbLock != 0);

    if (Thread == Prcb->IdleThread)
    {
        KiReleasePrcbLock(Prcb);
        return;
    }

    ASSERT(Thread->State == Running);
    ASSERT(Thread->NextProcessor == Prcb->Number);

    if (!(KiThreadAffinityMask(Thread) & Prcb->SetMember) || KiShouldVacateProcessor(Thread, Prcb))
    {
        Thread->State = DeferredReady;
        KiReleasePrcbLock(Prcb);
        KiDeferredReadyThread(Thread);
        return;
    }

    /* A thread that lost the processor to preemption goes back to the front of its queue */
    AtHead = Thread->Preempted;
    Thread->Preempted = FALSE;
    Thread->State = Ready;
    Thread->WaitTime = KeTickCount.LowPart;
    KiReadyQueueInsert(Prcb, Thread, Thread->Priority, AtHead);

    if (KiEevdfEnabled)
        KiEevdfEnqueue(Prcb, Thread, TRUE);

    KiReleasePrcbLock(Prcb);
}

FORCEINLINE
PKTHREAD
KiSelectReadyThread(
    _In_ KPRIORITY Priority,
    _Inout_ PKPRCB Prcb)
{
    ULONG Above, Highest;
    PKTHREAD Thread;

    if (KiEevdfEnabled)
        return KiEevdfSelectReadyThread(Priority, Prcb, NULL);

    if (Priority > HIGH_PRIORITY)
        return NULL;

    for (;;)
    {
        Above = Prcb->ReadySummary >> Priority;
        if (Above == 0)
            return NULL;

        BitScanReverse(&Highest, Above);
        Highest += Priority;

        ASSERT(!IsListEmpty(&Prcb->DispatcherReadyListHead[Highest]));
        Thread = CONTAINING_RECORD(Prcb->DispatcherReadyListHead[Highest].Flink, KTHREAD, WaitListEntry);

        ASSERT(Thread != Prcb->IdleThread);
        ASSERT(Thread->State == Ready && !Thread->ProcessReadyQueue);
        ASSERT((KPRIORITY)Highest == Thread->Priority);
        ASSERT(Thread->NextProcessor == Prcb->Number);

        KiReadyQueueRemove(Prcb, Thread, (KPRIORITY)Highest);

        if (KiThreadAffinityMask(Thread) & AFFINITY_MASK(Prcb->Number))
        {
            ASSERT(Thread->BasePriority == 0 || Thread->Priority != 0);
            return Thread;
        }

        KiInsertDeferredReadyList(Thread);
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
KiComputeNewPriority(
    _Inout_ PKTHREAD Thread,
    _In_ SCHAR Adjustment)
{
    LONG Priority = (SCHAR)KiGetUnflooredPriority(Thread);

    ASSERT(Thread->PriorityDecrement >= 0 && Thread->PriorityDecrement <= Priority);
    ASSERT(Priority < LOW_REALTIME_PRIORITY || Thread->PriorityDecrement == 0);

    /* Only the variable range decays toward the base priority */
    if (Priority < LOW_REALTIME_PRIORITY)
    {
        Priority = max(Priority - Thread->PriorityDecrement - Adjustment, (LONG)Thread->BasePriority);
        Thread->PriorityDecrement = 0;
    }

    ASSERT(Thread->BasePriority == 0 || Priority != 0);
    return (SCHAR)Priority;
}

FORCEINLINE
VOID
_KeInitializeGuardedMutex(
    _Out_ PKGUARDED_MUTEX GuardedMutex)
{
    GuardedMutex->Count = GM_LOCK_BIT;
    GuardedMutex->Owner = NULL;
    GuardedMutex->Contention = 0;
    KeInitializeGate(&GuardedMutex->Gate);
}

FORCEINLINE
BOOLEAN
KiGuardedMutexUnsafeContext(
    _In_ PKTHREAD Thread)
{
    return (BOOLEAN)(KeGetCurrentIrql() == APC_LEVEL || Thread->SpecialApcDisable < 0 ||
                     Thread->Teb == NULL || Thread->Teb >= (PTEB)MM_SYSTEM_RANGE_START);
}

FORCEINLINE
VOID
KiTakeGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex,
    _In_ PKTHREAD Thread)
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
KiGiveGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    LONG Before, Expected;

    GuardedMutex->Owner = NULL;

    Before = InterlockedExchangeAdd(&GuardedMutex->Count, GM_LOCK_BIT);
    ASSERT((Before & GM_LOCK_BIT) == 0);

    /* Waiters are counted below the lock bit; wake one unless one is already on its way */
    if (Before == 0 || (Before & GM_LOCK_WAITER_WOKEN))
        return;

    Expected = Before + GM_LOCK_BIT;
    if (InterlockedCompareExchange(&GuardedMutex->Count, Expected + GM_LOCK_WAITER_WOKEN - GM_LOCK_WAITER_INC,
                                   Expected) == Expected)
    {
        KeSignalGateBoostPriority(&GuardedMutex->Gate);
    }
}

FORCEINLINE
VOID
_KeAcquireGuardedMutexUnsafe(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(KiGuardedMutexUnsafeContext(Thread));
    KiTakeGuardedMutex(GuardedMutex, Thread);
}

FORCEINLINE
VOID
_KeReleaseGuardedMutexUnsafe(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    ASSERT(KiGuardedMutexUnsafeContext(KeGetCurrentThread()));
    ASSERT(GuardedMutex->Owner == KeGetCurrentThread());
    KiGiveGuardedMutex(GuardedMutex);
}

FORCEINLINE
VOID
_KeAcquireGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    KiEnterGuardedRegionThread(Thread);
    KiTakeGuardedMutex(GuardedMutex, Thread);
    GuardedMutex->SpecialApcDisable = Thread->SpecialApcDisable;
}

FORCEINLINE
VOID
_KeReleaseGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(GuardedMutex->Owner == Thread);
    ASSERT(Thread->SpecialApcDisable == GuardedMutex->SpecialApcDisable);

    KiGiveGuardedMutex(GuardedMutex);
    KiLeaveGuardedRegionThread(Thread);
}

FORCEINLINE
BOOLEAN
_KeTryToAcquireGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    PKTHREAD Thread = KeGetCurrentThread();

    KiEnterGuardedRegionThread(Thread);

    if (!InterlockedBitTestAndReset(&GuardedMutex->Count, GM_LOCK_BIT_V))
    {
        KiLeaveGuardedRegionThread(Thread);
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
