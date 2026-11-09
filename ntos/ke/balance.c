/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Finding the threads the scheduler has left behind
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#include "keprivate.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Where the last scan stopped, so that successive scans cover the processors
   in turn instead of always starting from the same one. */
ULONG KiReadyScanLast;

/* PRIVATE DEFINITIONS ********************************************************/

/*
 * How long a ready thread may sit at the same priority before the scan treats
 * it as starved, in default-length ticks. Four seconds at the usual period:
 * long enough that an ordinary busy system never reaches it, short enough that
 * a thread shut out by a priority inversion is found before a user notices.
 */
#define KI_STARVATION_TICKS 300

/* The priority a starved thread is lifted to for one turn. It is the top of
   the dynamic range and not the real-time range: being starved earns a thread
   a turn, not the right to preempt real-time work. */
#define KI_STARVATION_PRIORITY (LOW_REALTIME_PRIORITY - 1)

/* How many threads one scan will lift. A scan that lifted every starved
   thread on a badly inverted system would make the inversion worse by
   flooding the top of the dynamic range. */
#define KI_STARVATION_LIMIT 10

/* How many processors one scan looks at. */
#define KI_SCAN_PROCESSORS 4

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Lifts one starved thread and gives it a doubled turn, so that the lift is
 * worth something: a thread raised for a turn it then loses immediately to the
 * thread that was starving it has gained nothing.
 *
 * Entered with the thread's processor lock held; it is released here.
 */
static
VOID
KiRelieveStarvedThread(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY Priority)
{
    /* Take it out of the queue it is stuck in */
    KiAssertReadyQueue(Prcb, Priority);
    if (RemoveEntryList(&Thread->WaitListEntry))
        Prcb->ReadySummary ^= PRIORITY_MASK(Priority);

    Thread->PriorityDecrement = KI_STARVATION_PRIORITY - Thread->BasePriority;
    Thread->Priority = (SCHAR)KiApplyPriorityFloor(Thread,
                                                   KI_STARVATION_PRIORITY);
    KiSetThreadQuantum(Thread, Thread->QuantumReset * 2);
    Thread->Preempted = FALSE;

    KiReleasePrcbLock(Prcb);

    Thread->State = DeferredReady;
    Thread->AdjustReason = AdjustNone;
    KiDeferredReadyThread(Thread);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Looks for threads that have been ready for a long time without running. A
 * thread gets there by being held behind something of a higher priority that
 * is itself waiting on something the lower thread holds; the scheduler cannot
 * see that, so this scan breaks the deadlock by priority alone.
 *
 * Only part of the system is looked at each time: the scan runs often, and
 * walking every queue of every processor would cost more than the starvation
 * it is looking for.
 */
VOID
NTAPI
KiScanReadyQueues(VOID)
{
    PKPRCB Prcb;
    PKTHREAD Thread;
    PLIST_ENTRY Entry, ListHead;
    KIRQL OldIrql;
    ULONG TickNow;
    ULONG Processor, Scanned;
    KPRIORITY Priority;
    ULONG Relieved = 0;

    TickNow = KeTickCount.LowPart;

    for (Scanned = 0; Scanned < KI_SCAN_PROCESSORS; Scanned++)
    {
        Processor = (KiReadyScanLast + Scanned) % MAXIMUM_PROCESSORS;

        if ((KeActiveProcessors & AFFINITY_MASK(Processor)) == 0)
            continue;

        Prcb = KiProcessorBlock[Processor];
        if (Prcb == NULL)
            continue;

        OldIrql = KeRaiseIrqlToSynchLevel();

        for (Priority = 1; Priority < KI_STARVATION_PRIORITY; Priority++)
        {
            if ((Prcb->ReadySummary & PRIORITY_MASK(Priority)) == 0)
                continue;

            KiAcquirePrcbLock(Prcb);

            /* The queue may have emptied while the lock was taken */
            if ((Prcb->ReadySummary & PRIORITY_MASK(Priority)) == 0)
            {
                KiReleasePrcbLock(Prcb);
                continue;
            }

            ListHead = &Prcb->DispatcherReadyListHead[Priority];
            Entry = ListHead->Flink;
            Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);

            /*
             * The head of the queue is the one that has waited longest, so if
             * it is not starved nothing behind it is either.
             */
            if ((Thread->State != Ready) ||
                ((TickNow - Thread->WaitTime) < KI_STARVATION_TICKS))
            {
                KiReleasePrcbLock(Prcb);
                continue;
            }

            KiRelieveStarvedThread(Prcb, Thread, Priority);

            if (SmpDbgEnabled)
                SmpDbgBalanceEvent(Processor, Processor, SMPDBG_BALANCE_PERIODIC);

            Relieved++;
            if (Relieved >= KI_STARVATION_LIMIT)
            {
                KeLowerIrql(OldIrql);
                KiReadyScanLast = (Processor + 1) % MAXIMUM_PROCESSORS;
                return;
            }
        }

        KeLowerIrql(OldIrql);
    }

    KiReadyScanLast = (KiReadyScanLast + KI_SCAN_PROCESSORS) %
                      MAXIMUM_PROCESSORS;
}

/*
 * The thread that keeps the scheduler honest. It wakes on its own period to
 * look for starvation, and on the swap event when the memory manager has
 * something for it to do, so that neither has to poll for the other.
 */
VOID
NTAPI
KeBalanceSetManager(
    _In_ PVOID Context)
{
    KTIMER PeriodTimer;
    LARGE_INTEGER DueTime;
    PVOID WaitObjects[2];
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Context);

    /*
     * At the top of the dynamic range: the scan has to be able to run on a
     * system whose problem is precisely that nothing below real time is
     * getting a turn.
     */
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY - 1);

    KeInitializeTimerEx(&PeriodTimer, SynchronizationTimer);
    DueTime.QuadPart = Int32x32To64(1000, -10000);
    KeSetTimerEx(&PeriodTimer, DueTime, 1000, NULL);

    WaitObjects[0] = &PeriodTimer;
    WaitObjects[1] = &KiSwapEvent;

    for (;;)
    {
        Status = KeWaitForMultipleObjects(2,
                                          WaitObjects,
                                          WaitAny,
                                          WrDispatchInt,
                                          KernelMode,
                                          FALSE,
                                          NULL,
                                          NULL);

        if (Status == STATUS_WAIT_0)
        {
            KiScanReadyQueues();

            /*
             * While the watchdog is on, the timer table is checked for a
             * timer that came due and was never noticed. Nothing should ever
             * be found: what it reports is a defect in the clock path.
             */
            if (KiEnableTimerWatchdog)
            {
                ULARGE_INTEGER Now;

                Now.QuadPart = KiQueryInterruptTimePrecise(NULL, FALSE);
                KiCheckTimerTable(Now);
            }

            continue;
        }

        if (Status != STATUS_WAIT_1)
            continue;

        /*
         * Something was put on one of the swap lists. The lists belong to the
         * memory manager, which runs its own worker for them; waking here is
         * what gives that worker a thread to run on when the system is busy.
         */
        /* TODO: no stack or process swapping is implemented yet */
    }
}

/* EOF */
