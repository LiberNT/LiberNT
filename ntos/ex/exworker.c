/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The system worker threads and the queues they take work from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

EX_WORK_QUEUE ExWorkerQueue[MaximumWorkQueue];

ULONG ExCriticalWorkerThreads = 0;
ULONG ExDelayedWorkerThreads = 0;

ULONG ExpAdditionalCriticalWorkerThreads = 0;
ULONG ExpAdditionalDelayedWorkerThreads = 0;

LIST_ENTRY ExpWorkerListHead;

KEVENT ExpThreadSetManagerEvent;
KEVENT ExpThreadSetManagerShutdownEvent;

PETHREAD ExpWorkerThreadBalanceManagerPtr = NULL;
PETHREAD ExpLastWorkerThread = NULL;

BOOLEAN ExpWorkersCanSwap = TRUE;

FAST_MUTEX ExpWorkerSwapinMutex;

/* PRIVATE DATA ***************************************************************/

#define EXP_WORKER_TAG 'rkoW'

/* The threads each queue starts with */
#define EXP_CRITICAL_WORKERS_BASE 5
#define EXP_DELAYED_WORKERS_BASE  3
#define EXP_HYPERCRITICAL_WORKERS 1

/* The most threads one queue may grow to */
#define EXP_MAXIMUM_DYNAMIC_WORKERS 16

/* How often the balance manager looks at the queues */
#define EXP_BALANCE_INTERVAL (-1 * 10 * 1000 * 1000)

/* How long a work item may sit unstarted before the queue is called starved */
#define EXP_DEADLOCK_INTERVAL (-30 * 10 * 1000 * 1000)

/* How many passes of no progress make a queue stuck rather than merely busy */
#define EXP_WORKER_STUCK_PASSES 3

/* The priority each queue's threads run at */
#define EXP_CRITICAL_PRIORITY      13
#define EXP_DELAYED_PRIORITY       12
#define EXP_HYPERCRITICAL_PRIORITY 15

typedef struct _EXP_WORKER_THREAD_CONTEXT
{
    WORK_QUEUE_TYPE QueueType;
    BOOLEAN Dynamic;
} EXP_WORKER_THREAD_CONTEXT, *PEXP_WORKER_THREAD_CONTEXT;

/*
 * What the system keeps about one worker thread. The thread is listed from the
 * moment it starts until it goes away, so the whole set of them can be reached
 * without walking the system's threads.
 */
typedef struct _EXP_WORKER_THREAD
{
    LIST_ENTRY Link;
    PETHREAD Thread;
    WORK_QUEUE_TYPE QueueType;
    BOOLEAN Dynamic;
    BOOLEAN SwapAllowed;
    volatile LONG SwapRequest;
    PLONG Outstanding;
    PKEVENT Done;
    KAPC SwapApc;
} EXP_WORKER_THREAD, *PEXP_WORKER_THREAD;

static KSPIN_LOCK ExpWorkerListLock;

/*
 * How many passes each queue has gone without getting through anything. It is
 * kept here because EX_WORK_QUEUE is a structure Windows defines and has no
 * field for it.
 */
static ULONG ExpWorkerStuckPasses[MaximumWorkQueue];

/* PRIVATE FUNCTIONS **********************************************************/

static
KPRIORITY
ExpWorkerPriority(
    _In_ WORK_QUEUE_TYPE QueueType)
{
    if (QueueType == HyperCriticalWorkQueue)
    {
        return EXP_HYPERCRITICAL_PRIORITY;
    }

    if (QueueType == DelayedWorkQueue)
    {
        return EXP_DELAYED_PRIORITY;
    }

    return EXP_CRITICAL_PRIORITY;
}

/*
 * Runs in the worker thread itself, because only a thread may say whether its
 * own stack can be swapped out. The thread that asked is woken once the last
 * worker has run this.
 */
VOID
NTAPI
ExpSetSwappingKernelApc(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PEXP_WORKER_THREAD Worker = CONTAINING_RECORD(Apc,
                                                  EXP_WORKER_THREAD,
                                                  SwapApc);
    PKEVENT Done = Worker->Done;
    PLONG Outstanding = Worker->Outstanding;

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeSetKernelStackSwapEnable(Worker->SwapAllowed);

    InterlockedExchange(&Worker->SwapRequest, 0);

    if (InterlockedDecrement(Outstanding) == 0)
    {
        KeSetEvent(Done, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * Puts a worker on the list of them, so a caller that has something to say to
 * every worker can find it. A worker that could not be listed still works; it
 * simply cannot be told to stay resident.
 */
static
PEXP_WORKER_THREAD
ExpRegisterWorkerThread(
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_ BOOLEAN Dynamic)
{
    PEXP_WORKER_THREAD Worker;
    KIRQL OldIrql;

    Worker = ExAllocatePoolZero(NonPagedPool,
                                sizeof(EXP_WORKER_THREAD),
                                EXP_WORKER_TAG);
    if (Worker == NULL)
    {
        return NULL;
    }

    Worker->Thread = PsGetCurrentThread();
    Worker->QueueType = QueueType;
    Worker->Dynamic = Dynamic;
    Worker->SwapAllowed = ExpWorkersCanSwap;
    Worker->SwapRequest = 0;

    KeAcquireSpinLock(&ExpWorkerListLock, &OldIrql);
    InsertTailList(&ExpWorkerListHead, &Worker->Link);
    ExpLastWorkerThread = Worker->Thread;
    KeReleaseSpinLock(&ExpWorkerListLock, OldIrql);

    return Worker;
}

/*
 * Takes a worker off the list as it goes away. A request that was queued to it
 * and never ran is accounted for here, so whoever is waiting is not left
 * waiting for a thread that no longer exists.
 */
static
VOID
ExpDeregisterWorkerThread(
    _In_ PEXP_WORKER_THREAD Worker)
{
    PLONG Outstanding;
    PKEVENT Done;
    KIRQL OldIrql;

    KeAcquireSpinLock(&ExpWorkerListLock, &OldIrql);
    RemoveEntryList(&Worker->Link);
    KeReleaseSpinLock(&ExpWorkerListLock, OldIrql);

    if (InterlockedCompareExchange(&Worker->SwapRequest, 2, 1) == 1)
    {
        Outstanding = Worker->Outstanding;
        Done = Worker->Done;

        if (KeRemoveQueueApc(&Worker->SwapApc) &&
            (InterlockedDecrement(Outstanding) == 0))
        {
            KeSetEvent(Done, IO_NO_INCREMENT, FALSE);
        }
    }

    ExFreePoolWithTag(Worker, EXP_WORKER_TAG);
}

VOID
NTAPI
ExpWorkerThreadEntryPoint(
    _In_ PVOID Context)
{
    EXP_WORKER_THREAD_CONTEXT Parameters =
        *(PEXP_WORKER_THREAD_CONTEXT)Context;
    PEX_WORK_QUEUE Queue = &ExWorkerQueue[Parameters.QueueType];
    PEXP_WORKER_THREAD Worker;
    PWORK_QUEUE_ITEM WorkItem;
    PLIST_ENTRY Entry;
    LARGE_INTEGER Timeout;
    PKTHREAD Thread = KeGetCurrentThread();

    ExFreePoolWithTag(Context, EXP_WORKER_TAG);

    KeSetBasePriorityThread(Thread, ExpWorkerPriority(Parameters.QueueType));

    Worker = ExpRegisterWorkerThread(Parameters.QueueType,
                                     Parameters.Dynamic);

    Timeout.QuadPart = EXP_DEADLOCK_INTERVAL;

    for (;;)
    {
        Entry = KeRemoveQueue(&Queue->WorkerQueue,
                              KernelMode,
                              Parameters.Dynamic ? &Timeout : NULL);

        /*
         * A thread the balance manager added goes away again once the work it
         * was added for has dried up.
         */
        if ((NTSTATUS)(ULONG_PTR)Entry == STATUS_TIMEOUT)
        {
            break;
        }

        if ((NTSTATUS)(ULONG_PTR)Entry == STATUS_USER_APC)
        {
            continue;
        }

        WorkItem = CONTAINING_RECORD(Entry, WORK_QUEUE_ITEM, List);

        Queue->WorkItemsProcessed++;

        /*
         * The item is handed back to whoever queued it before it runs, because
         * the routine is free to reuse or free the item itself.
         */
        WorkItem->WorkerRoutine(WorkItem->Parameter);

        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        {
            KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL,
                         (ULONG_PTR)WorkItem->WorkerRoutine,
                         KeGetCurrentIrql(),
                         (ULONG_PTR)WorkItem->Parameter,
                         (ULONG_PTR)WorkItem);
        }

        if (Thread->ApcStateIndex != 0)
        {
            KeBugCheckEx(WORKER_THREAD_RETURNED_AT_BAD_IRQL,
                         (ULONG_PTR)WorkItem->WorkerRoutine,
                         0,
                         (ULONG_PTR)WorkItem->Parameter,
                         (ULONG_PTR)WorkItem);
        }
    }

    if (Worker != NULL)
    {
        ExpDeregisterWorkerThread(Worker);
    }

    InterlockedDecrement(&Queue->DynamicThreadCount);

    if (Parameters.QueueType == CriticalWorkQueue)
    {
        InterlockedDecrement((PLONG)&ExpAdditionalCriticalWorkerThreads);
    }
    else if (Parameters.QueueType == DelayedWorkQueue)
    {
        InterlockedDecrement((PLONG)&ExpAdditionalDelayedWorkerThreads);
    }
}

static
NTSTATUS
ExpCreateWorkerThreadWorker(
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_ BOOLEAN Dynamic)
{
    PEXP_WORKER_THREAD_CONTEXT Context;
    HANDLE Handle;
    NTSTATUS Status;

    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(EXP_WORKER_THREAD_CONTEXT),
                                    EXP_WORKER_TAG);
    if (Context == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->QueueType = QueueType;
    Context->Dynamic = Dynamic;

    Status = PsCreateSystemThread(&Handle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  ExpWorkerThreadEntryPoint,
                                  Context);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Context, EXP_WORKER_TAG);
        return Status;
    }

    ObCloseHandle(Handle, KernelMode);

    if (Dynamic)
    {
        InterlockedIncrement(&ExWorkerQueue[QueueType].DynamicThreadCount);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
ExpCreateWorkerThread(
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_ BOOLEAN Dynamic)
{
    ExpCreateWorkerThreadWorker(QueueType, Dynamic);
}

/*
 * Adds a thread to a queue that has more work waiting than it is getting
 * through, and only while the queue is allowed to grow.
 */
static
VOID
ExpCheckDynamicThreadCount(VOID)
{
    PEX_WORK_QUEUE Queue;
    ULONG Index;
    ULONG Depth;

    for (Index = 0; Index < MaximumWorkQueue; Index++)
    {
        Queue = &ExWorkerQueue[Index];

        if (!Queue->Info.MakeThreadsAsNecessary)
        {
            continue;
        }

        if (Queue->DynamicThreadCount >= EXP_MAXIMUM_DYNAMIC_WORKERS)
        {
            continue;
        }

        Depth = KeReadStateQueue(&Queue->WorkerQueue);

        /*
         * The queue is only given another thread when it still has work in hand
         * and got through no more of it than it did last time round.
         */
        if ((Depth != 0) &&
            (Depth >= Queue->QueueDepthLastPass) &&
            (Queue->WorkItemsProcessed == Queue->WorkItemsProcessedLastPass))
        {
            ExpCreateWorkerThread((WORK_QUEUE_TYPE)Index, TRUE);
        }

        Queue->QueueDepthLastPass = Depth;
        Queue->WorkItemsProcessedLastPass = Queue->WorkItemsProcessed;
    }
}

/*
 * A queue that has work waiting and has got through none of it since the last
 * pass is stuck: every thread it has is inside a work routine that is waiting
 * for something. A thread is added so the queue can get going again, which is
 * the only thing that helps when the thing being waited for is itself queued
 * behind the work that is stuck.
 */
static
VOID
ExpDetectWorkerThreadDeadlock(VOID)
{
    PEX_WORK_QUEUE Queue;
    ULONG Index;

    for (Index = 0; Index < MaximumWorkQueue; Index++)
    {
        Queue = &ExWorkerQueue[Index];

        if ((KeReadStateQueue(&Queue->WorkerQueue) == 0) ||
            (Queue->WorkItemsProcessed != Queue->WorkItemsProcessedLastPass))
        {
            ExpWorkerStuckPasses[Index] = 0;
            continue;
        }

        ExpWorkerStuckPasses[Index]++;

        if (ExpWorkerStuckPasses[Index] < EXP_WORKER_STUCK_PASSES)
        {
            continue;
        }

        DPRINT1("Work queue %lu has made no progress for %lu passes\n",
                Index,
                ExpWorkerStuckPasses[Index]);

        ExpWorkerStuckPasses[Index] = 0;

        if (Queue->DynamicThreadCount < EXP_MAXIMUM_DYNAMIC_WORKERS)
        {
            ExpCreateWorkerThread((WORK_QUEUE_TYPE)Index, TRUE);
        }
    }
}

VOID
NTAPI
ExpWorkerThreadBalanceManager(
    _In_ PVOID Context)
{
    LARGE_INTEGER Interval;
    PVOID Objects[2];
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Context);

    KeSetBasePriorityThread(KeGetCurrentThread(), EXP_HYPERCRITICAL_PRIORITY);

    Interval.QuadPart = EXP_BALANCE_INTERVAL;

    Objects[0] = &ExpThreadSetManagerShutdownEvent;
    Objects[1] = &ExpThreadSetManagerEvent;

    for (;;)
    {
        Status = KeWaitForMultipleObjects(2,
                                          Objects,
                                          WaitAny,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          &Interval,
                                          NULL);
        if (Status == STATUS_WAIT_0)
        {
            break;
        }

        if (Status == STATUS_TIMEOUT)
        {
            ExpDetectWorkerThreadDeadlock();
            ExAdjustLookasideDepth();
        }

        ExpCheckDynamicThreadCount();
    }

    ExpWorkerThreadBalanceManagerPtr = NULL;

    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeWorkerThreads(VOID)
{
    ULONG Index;
    ULONG Count;
    HANDLE Handle;

    InitializeListHead(&ExpWorkerListHead);
    KeInitializeSpinLock(&ExpWorkerListLock);
    ExInitializeFastMutex(&ExpWorkerSwapinMutex);

    KeInitializeEvent(&ExpThreadSetManagerEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&ExpThreadSetManagerShutdownEvent,
                      NotificationEvent,
                      FALSE);

    for (Index = 0; Index < MaximumWorkQueue; Index++)
    {
        KeInitializeQueue(&ExWorkerQueue[Index].WorkerQueue, 0);
        ExWorkerQueue[Index].DynamicThreadCount = 0;
        ExWorkerQueue[Index].WorkItemsProcessed = 0;
        ExWorkerQueue[Index].WorkItemsProcessedLastPass = 0;
        ExWorkerQueue[Index].QueueDepthLastPass = 0;
    }

    /* Only the two queues that take work from anywhere are allowed to grow. */
    ExWorkerQueue[CriticalWorkQueue].Info.MakeThreadsAsNecessary = TRUE;
    ExWorkerQueue[DelayedWorkQueue].Info.MakeThreadsAsNecessary = TRUE;

    ExCriticalWorkerThreads = EXP_CRITICAL_WORKERS_BASE;
    ExDelayedWorkerThreads = EXP_DELAYED_WORKERS_BASE;

    for (Count = 0; Count < ExCriticalWorkerThreads; Count++)
    {
        ExpCreateWorkerThread(CriticalWorkQueue, FALSE);
    }

    for (Count = 0; Count < ExDelayedWorkerThreads; Count++)
    {
        ExpCreateWorkerThread(DelayedWorkQueue, FALSE);
    }

    for (Count = 0; Count < EXP_HYPERCRITICAL_WORKERS; Count++)
    {
        ExpCreateWorkerThread(HyperCriticalWorkQueue, FALSE);
    }

    /* Every other queue gets a thread too, so nothing queued to one is lost. */
    for (Index = HyperCriticalWorkQueue + 1; Index < MaximumWorkQueue; Index++)
    {
        ExpCreateWorkerThread((WORK_QUEUE_TYPE)Index, FALSE);
    }

    if (NT_SUCCESS(PsCreateSystemThread(&Handle,
                                        THREAD_ALL_ACCESS,
                                        NULL,
                                        NULL,
                                        NULL,
                                        ExpWorkerThreadBalanceManager,
                                        NULL)))
    {
        ObReferenceObjectByHandle(Handle,
                                  0,
                                  PsThreadType,
                                  KernelMode,
                                  (PVOID *)&ExpWorkerThreadBalanceManagerPtr,
                                  NULL);
        ObCloseHandle(Handle, KernelMode);
    }
}

VOID
NTAPI
ExQueueWorkItem(
    _Inout_ PWORK_QUEUE_ITEM WorkItem,
    _In_ WORK_QUEUE_TYPE QueueType)
{
    ASSERT(QueueType < MaximumWorkQueue);
    ASSERT(WorkItem->List.Flink == NULL);

    KeInsertQueue(&ExWorkerQueue[QueueType].WorkerQueue, &WorkItem->List);
}

/*
 * Says whether the worker threads may be swapped out while they wait. A thread
 * can only change its own swap setting, so each of them is sent an APC that
 * does it, and the caller waits until every one of them has run it.
 */
VOID
NTAPI
ExSwapinWorkerThreads(
    _In_ BOOLEAN AllowSwap)
{
    PLIST_ENTRY Entry;
    PEXP_WORKER_THREAD Worker;
    KEVENT Done;
    LONG Outstanding = 1;
    KIRQL OldIrql;

    PAGED_CODE();

    ExAcquireFastMutex(&ExpWorkerSwapinMutex);

    if (ExpWorkersCanSwap == AllowSwap)
    {
        ExReleaseFastMutex(&ExpWorkerSwapinMutex);
        return;
    }

    ExpWorkersCanSwap = AllowSwap;

    KeInitializeEvent(&Done, NotificationEvent, FALSE);

    KeAcquireSpinLock(&ExpWorkerListLock, &OldIrql);

    for (Entry = ExpWorkerListHead.Flink;
         Entry != &ExpWorkerListHead;
         Entry = Entry->Flink)
    {
        Worker = CONTAINING_RECORD(Entry, EXP_WORKER_THREAD, Link);

        /*
         * A worker whose last request has not been taken up yet is left alone:
         * the one already queued will read the setting as it now stands.
         */
        if (InterlockedCompareExchange(&Worker->SwapRequest, 1, 0) != 0)
        {
            continue;
        }

        Worker->SwapAllowed = AllowSwap;
        Worker->Outstanding = &Outstanding;
        Worker->Done = &Done;

        InterlockedIncrement(&Outstanding);

        KeInitializeApc(&Worker->SwapApc,
                        &Worker->Thread->Tcb,
                        OriginalApcEnvironment,
                        ExpSetSwappingKernelApc,
                        NULL,
                        NULL,
                        KernelMode,
                        Worker);

        if (!KeInsertQueueApc(&Worker->SwapApc, NULL, NULL, IO_NO_INCREMENT))
        {
            InterlockedDecrement(&Outstanding);
            InterlockedExchange(&Worker->SwapRequest, 0);
        }
    }

    KeReleaseSpinLock(&ExpWorkerListLock, OldIrql);

    if (InterlockedDecrement(&Outstanding) != 0)
    {
        KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
    }

    ExReleaseFastMutex(&ExpWorkerSwapinMutex);
}

/* EOF */
