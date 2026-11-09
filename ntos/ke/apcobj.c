/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Running a routine inside a thread that did not ask for it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The environment an APC was aimed at. A thread attached to another process
 * keeps two of these: the one it is running in and the one it came from, so an
 * APC queued against either is held until that environment is current again.
 */
FORCEINLINE
PKAPC_STATE
KiApcTargetState(
    _In_ PKTHREAD Thread,
    _In_ CCHAR ApcStateIndex)
{
    ASSERT((ApcStateIndex == OriginalApcEnvironment) ||
           (ApcStateIndex == AttachedApcEnvironment));

    return Thread->ApcStatePointer[(UCHAR)ApcStateIndex];
}

/*
 * Moves one circular list of APCs to a new head. The entries are not touched
 * individually: only the head they point back to changes.
 */
static
VOID
KiRelinkApcList(
    _Inout_ PLIST_ENTRY OldHead,
    _Out_ PLIST_ENTRY NewHead)
{
    if (IsListEmpty(OldHead))
    {
        InitializeListHead(NewHead);
        return;
    }

    NewHead->Flink = OldHead->Flink;
    NewHead->Blink = OldHead->Blink;
    NewHead->Flink->Blink = NewHead;
    NewHead->Blink->Flink = NewHead;
    InitializeListHead(OldHead);
}

/*
 * Whether a normal kernel APC could run in this thread right now. A special
 * APC answers to SpecialApcDisable alone; a normal one also has to wait for
 * any normal APC already running to finish, because its routine runs at
 * PASSIVE_LEVEL and must not nest.
 */
FORCEINLINE
BOOLEAN
KiIsApcDeliverableToThread(
    _In_ PKTHREAD Thread,
    _In_ PKAPC Apc)
{
    if (Thread->SpecialApcDisable != 0)
        return FALSE;

    if (Apc->NormalRoutine == NULL)
        return TRUE;

    return (Thread->KernelApcDisable == 0) &&
           !Thread->ApcState.KernelApcInProgress;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Describes a routine to run inside a thread. Nothing is queued yet: the APC
 * only records what to run, in which environment, and at which mode.
 */
VOID
NTAPI
KeInitializeApc(
    _Out_ PKAPC Apc,
    _In_ PKTHREAD Thread,
    _In_ KAPC_ENVIRONMENT TargetEnvironment,
    _In_ PKKERNEL_ROUTINE KernelRoutine,
    _In_opt_ PKRUNDOWN_ROUTINE RundownRoutine,
    _In_opt_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ KPROCESSOR_MODE Mode,
    _In_opt_ PVOID Context)
{
    ASSERT(TargetEnvironment <= InsertApcEnvironment);

    Apc->Type = ApcObject;
    Apc->Size = sizeof(KAPC);
    Apc->SpareByte0 = 0;
    Apc->SpareByte1 = 0;
    Apc->SpareLong0 = 0;
    Apc->Thread = Thread;

    /*
     * CurrentApcEnvironment is resolved now, while the caller can see which
     * environment the thread is in; InsertApcEnvironment is resolved at
     * insertion, which is what makes it mean "wherever it is by then".
     */
    if (TargetEnvironment == CurrentApcEnvironment)
        Apc->ApcStateIndex = Thread->ApcStateIndex;
    else
        Apc->ApcStateIndex = (CCHAR)TargetEnvironment;

    Apc->KernelRoutine = KernelRoutine;
    Apc->RundownRoutine = RundownRoutine;
    Apc->NormalRoutine = NormalRoutine;
    Apc->Inserted = FALSE;

    /*
     * Only a normal routine can run in user mode, and only it is given a
     * context: a special APC's kernel routine takes the APC itself.
     */
    if (NormalRoutine != NULL)
    {
        Apc->ApcMode = Mode;
        Apc->NormalContext = Context;
    }
    else
    {
        Apc->ApcMode = KernelMode;
        Apc->NormalContext = NULL;
    }

    Apc->SystemArgument1 = NULL;
    Apc->SystemArgument2 = NULL;
}

/*
 * Puts an APC on its thread's list and, where it can be delivered at once,
 * makes the thread notice.
 *
 * Entered at SYNCH_LEVEL with the thread's APC lock held.
 */
VOID
FASTCALL
KiInsertQueueApc(
    _Inout_ PKAPC Apc,
    _In_ KPRIORITY PriorityBoost)
{
    PKTHREAD Thread = Apc->Thread;
    PKAPC_STATE ApcState;
    PKAPC Queued;
    PLIST_ENTRY ListHead, Entry;
    KPROCESSOR_MODE ApcMode = Apc->ApcMode;
    BOOLEAN RequestInterrupt = FALSE;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    if (Apc->ApcStateIndex == InsertApcEnvironment)
        Apc->ApcStateIndex = Thread->ApcStateIndex;

    ApcState = KiApcTargetState(Thread, Apc->ApcStateIndex);
    ListHead = &ApcState->ApcListHead[(ULONG)ApcMode];

    if (Apc->NormalRoutine != NULL)
    {
        InsertTailList(ListHead, &Apc->ApcListEntry);
    }
    else
    {
        /*
         * A special APC goes ahead of every normal one already queued: it runs
         * at APC_LEVEL and must not be made to wait behind work that runs at
         * PASSIVE_LEVEL and may block.
         */
        Entry = ListHead->Blink;
        while (Entry != ListHead)
        {
            Queued = CONTAINING_RECORD(Entry, KAPC, ApcListEntry);
            if (Queued->NormalRoutine == NULL)
                break;
            Entry = Entry->Blink;
        }

        InsertHeadList(Entry, &Apc->ApcListEntry);
    }

    Apc->Inserted = TRUE;

    if (ApcMode == KernelMode)
    {
        ApcState->KernelApcPending = TRUE;
    }

    /*
     * An APC aimed at an environment the thread is not currently in waits
     * there; the attach and detach paths notice it when they swap states.
     */
    if (Apc->ApcStateIndex != Thread->ApcStateIndex)
        return;

    KiAcquireDispatcherLockAtSynchLevel();

    if (ApcMode == KernelMode)
    {
        if (Thread->State == Running)
        {
            RequestInterrupt = TRUE;
        }
        else if ((Thread->State == Waiting) &&
                 (Thread->WaitIrql == PASSIVE_LEVEL) &&
                 KiIsApcDeliverableToThread(Thread, Apc))
        {
            KiUnwaitThread(Thread, STATUS_KERNEL_APC, PriorityBoost);
        }
    }
    else if ((Thread->State == Waiting) &&
             (Thread->WaitMode == UserMode) &&
             (Thread->Alertable || Thread->ApcState.UserApcPending))
    {
        Thread->ApcState.UserApcPending = TRUE;
        KiUnwaitThread(Thread, STATUS_USER_APC, PriorityBoost);
    }

    KiReleaseDispatcherLockFromSynchLevel();

    KiRequestApcInterrupt(RequestInterrupt, (UCHAR)Thread->NextProcessor);
}

/*
 * Queues an APC against a thread. Answers FALSE when the thread has stopped
 * accepting them, which is how a terminating thread stops collecting work it
 * will never run.
 */
BOOLEAN
NTAPI
KeInsertQueueApc(
    _Inout_ PKAPC Apc,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2,
    _In_ KPRIORITY PriorityBoost)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PKTHREAD Thread = Apc->Thread;
    BOOLEAN Inserted = FALSE;

    ASSERT_APC(Apc);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    if (Thread->ApcQueueable && !Apc->Inserted)
    {
        Apc->SystemArgument1 = SystemArgument1;
        Apc->SystemArgument2 = SystemArgument2;
        KiInsertQueueApc(Apc, PriorityBoost);
        Inserted = TRUE;
    }

    KiReleaseApcLockFromSynchLevel(&ApcLock);

    /* Whatever the insertion readied is dispatched on the way out */
    KiExitDispatcher(ApcLock.OldIrql);

    return Inserted;
}

/*
 * Takes a queued APC back. Answers whether it was still queued: a caller that
 * gets FALSE has to assume the APC is already running or has run.
 */
BOOLEAN
NTAPI
KeRemoveQueueApc(
    _Inout_ PKAPC Apc)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PKTHREAD Thread = Apc->Thread;
    PKAPC_STATE ApcState;
    BOOLEAN Inserted;

    ASSERT_APC(Apc);

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);
    KiAcquireDispatcherLockAtSynchLevel();

    Inserted = Apc->Inserted;
    if (Inserted)
    {
        Apc->Inserted = FALSE;
        ApcState = KiApcTargetState(Thread, Apc->ApcStateIndex);

        if (RemoveEntryList(&Apc->ApcListEntry))
        {
            if (Apc->ApcMode == KernelMode)
                ApcState->KernelApcPending = FALSE;
            else
                ApcState->UserApcPending = FALSE;
        }
    }

    KiReleaseDispatcherLockFromSynchLevel();
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    return Inserted;
}

/*
 * Empties one mode's APC list and hands it to the caller as a circular list,
 * so the caller can run each rundown routine outside the lock. Nothing is
 * delivered, which is the point: this is how a dying thread discards work.
 */
PLIST_ENTRY
NTAPI
KeFlushQueueApc(
    _Inout_ PKTHREAD Thread,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PLIST_ENTRY ListHead, FirstEntry, Entry;
    PKAPC Apc;

    ASSERT((PreviousMode == KernelMode) || (PreviousMode == UserMode));

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    ListHead = &Thread->ApcState.ApcListHead[(UCHAR)PreviousMode];
    if (IsListEmpty(ListHead))
    {
        KiReleaseApcLock(&ApcLock);
        return NULL;
    }

    FirstEntry = ListHead->Flink;

    /* Unlink the head itself; what is left is a circular list of the APCs */
    RemoveEntryList(ListHead);
    InitializeListHead(ListHead);

    Entry = FirstEntry;
    do
    {
        Apc = CONTAINING_RECORD(Entry, KAPC, ApcListEntry);
        Apc->Inserted = FALSE;
        Entry = Entry->Flink;
    } while (Entry != FirstEntry);

    if (PreviousMode == KernelMode)
        Thread->ApcState.KernelApcPending = FALSE;
    else
        Thread->ApcState.UserApcPending = FALSE;

    KiReleaseApcLock(&ApcLock);
    return FirstEntry;
}

/*
 * Carries one thread's APC bookkeeping from one KAPC_STATE to another. Used
 * where a thread changes which process it runs in: the state it leaves behind
 * keeps its queued work, pending flags and all.
 */
VOID
NTAPI
KiMoveApcState(
    _Inout_ PKAPC_STATE OldState,
    _Out_ PKAPC_STATE NewState)
{
    KiRelinkApcList(&OldState->ApcListHead[KernelMode],
                    &NewState->ApcListHead[KernelMode]);
    KiRelinkApcList(&OldState->ApcListHead[UserMode],
                    &NewState->ApcListHead[UserMode]);

    NewState->Process = OldState->Process;
    NewState->KernelApcInProgress = OldState->KernelApcInProgress;
    NewState->KernelApcPending = OldState->KernelApcPending;
    NewState->UserApcPending = OldState->UserApcPending;
}

/*
 * Runs whatever APCs this thread has waiting.
 *
 * Entered at APC_LEVEL. Special kernel APCs run there; a normal kernel APC's
 * own routine runs at PASSIVE_LEVEL and is allowed to block, which is why only
 * one of them may be in progress at a time. A user APC is not run here at all:
 * it is arranged on the outgoing trap frame so that it runs in user mode.
 */
VOID
NTAPI
KiDeliverApc(
    _In_ KPROCESSOR_MODE DeliveryMode,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_opt_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKAPC_STATE State = &Thread->ApcState;
    KLOCK_QUEUE_HANDLE ApcLock;
    PKAPC Apc;
    PLIST_ENTRY Entry;
    PKKERNEL_ROUTINE KernelRoutine;
    PKNORMAL_ROUTINE NormalRoutine;
    PVOID NormalContext, SystemArgument1, SystemArgument2;

    ASSERT(Thread->ApcState.KernelApcPending ||
           !IsListEmpty(&State->ApcListHead[KernelMode]) ||
           (DeliveryMode == UserMode));
    ASSERT(KeGetCurrentIrql() == APC_LEVEL);

    /*
     * Say the pending kernel APCs are being taken care of before looking at
     * the list: anything queued from here on sets the flag again and will be
     * noticed by the next pass.
     */
    State->KernelApcPending = FALSE;

    while (!IsListEmpty(&State->ApcListHead[KernelMode]))
    {
        KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

        if (IsListEmpty(&State->ApcListHead[KernelMode]))
        {
            KiReleaseApcLock(&ApcLock);
            break;
        }

        Entry = State->ApcListHead[KernelMode].Flink;
        Apc = CONTAINING_RECORD(Entry, KAPC, ApcListEntry);

        KernelRoutine = Apc->KernelRoutine;
        NormalRoutine = Apc->NormalRoutine;
        NormalContext = Apc->NormalContext;
        SystemArgument1 = Apc->SystemArgument1;
        SystemArgument2 = Apc->SystemArgument2;

        if (NormalRoutine != NULL)
        {
            /* A normal APC has to wait its turn behind the one in progress */
            if (State->KernelApcInProgress || (Thread->KernelApcDisable != 0))
            {
                KiReleaseApcLock(&ApcLock);
                return;
            }
        }

        RemoveEntryList(Entry);
        Apc->Inserted = FALSE;
        KiReleaseApcLock(&ApcLock);

        /*
         * The kernel routine owns the APC from here: it may free it, and it
         * may cancel the normal routine by clearing the pointer it is handed.
         */
        KernelRoutine(Apc,
                      &NormalRoutine,
                      &NormalContext,
                      &SystemArgument1,
                      &SystemArgument2);

        if (NormalRoutine == NULL)
            continue;

        State->KernelApcInProgress = TRUE;
        KeLowerIrql(PASSIVE_LEVEL);
        NormalRoutine(NormalContext, SystemArgument1, SystemArgument2);
        KfRaiseIrql(APC_LEVEL);
        State->KernelApcInProgress = FALSE;
    }

    /* The rest only happens on the way back out to user mode */
    if ((DeliveryMode != UserMode) ||
        !State->UserApcPending ||
        IsListEmpty(&State->ApcListHead[UserMode]))
    {
        return;
    }

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    if (IsListEmpty(&State->ApcListHead[UserMode]))
    {
        State->UserApcPending = FALSE;
        KiReleaseApcLock(&ApcLock);
        return;
    }

    State->UserApcPending = FALSE;

    Entry = State->ApcListHead[UserMode].Flink;
    Apc = CONTAINING_RECORD(Entry, KAPC, ApcListEntry);

    KernelRoutine = Apc->KernelRoutine;
    NormalRoutine = Apc->NormalRoutine;
    NormalContext = Apc->NormalContext;
    SystemArgument1 = Apc->SystemArgument1;
    SystemArgument2 = Apc->SystemArgument2;

    RemoveEntryList(Entry);
    Apc->Inserted = FALSE;
    KiReleaseApcLock(&ApcLock);

    KernelRoutine(Apc,
                  &NormalRoutine,
                  &NormalContext,
                  &SystemArgument1,
                  &SystemArgument2);

    if (NormalRoutine == NULL)
    {
        /* The kernel routine took it back; look for anything else waiting */
        KiCheckForKernelApcDelivery();
        return;
    }

    KiInitializeUserApc(ExceptionFrame,
                        TrapFrame,
                        NormalRoutine,
                        NormalContext,
                        SystemArgument1,
                        SystemArgument2);
}

/*
 * Called where leaving a critical or guarded region has just made kernel APCs
 * deliverable again. At PASSIVE_LEVEL the APC can run straight away; higher
 * up, an interrupt is asked for so that it runs as soon as IRQL allows.
 */
VOID
NTAPI
KiCheckForKernelApcDelivery(VOID)
{
    KIRQL OldIrql;

    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        OldIrql = KfRaiseIrql(APC_LEVEL);
        KiDeliverApc(KernelMode, NULL, NULL);
        KeLowerIrql(OldIrql);
        return;
    }

    KiRequestApcInterrupt(TRUE, (UCHAR)KeGetCurrentPrcb()->Number);
}

/*
 * Says whether APCs are blocked for the calling thread. "Disabled" here means
 * normal kernel APCs: a special APC can still arrive inside a critical region.
 */
BOOLEAN
NTAPI
KeAreApcsDisabled(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    return (Thread->CombinedApcDisable != 0) ||
           (KeGetCurrentIrql() >= APC_LEVEL);
}

/*
 * Says whether even special kernel APCs are blocked, which is what a guarded
 * region promises and a critical region does not.
 */
BOOLEAN
NTAPI
KeAreAllApcsDisabled(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    return (Thread->SpecialApcDisable != 0) ||
           (KeGetCurrentIrql() >= APC_LEVEL);
}

/*
 * Stops a thread accepting any further APCs. Answers the previous state, so a
 * caller that finds it already closed knows someone else got there first.
 */
BOOLEAN
NTAPI
KeDisableThreadApcQueueing(
    _Inout_ PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    BOOLEAN WasQueueable;

    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);
    WasQueueable = Thread->ApcQueueable;
    Thread->ApcQueueable = FALSE;
    KiReleaseApcLock(&ApcLock);

    return WasQueueable;
}

/*
 * The out-of-line forms of the region entry and exit. Drivers reach these
 * through the export table; kernel code uses the inline forms in the headers.
 */
VOID
NTAPI
_KeEnterCriticalRegion(VOID)
{
    KeEnterCriticalRegion();
}

VOID
NTAPI
_KeLeaveCriticalRegion(VOID)
{
    KeLeaveCriticalRegion();
}

VOID
NTAPI
_KeEnterGuardedRegion(VOID)
{
    KeEnterGuardedRegion();
}

VOID
NTAPI
_KeLeaveGuardedRegion(VOID)
{
    KeLeaveGuardedRegion();
}

/* EOF */
