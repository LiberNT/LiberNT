/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Giving a file system a fresh stack when its own has run out
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fsrtlp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_OVERFLOW_TAG 'oRsF'

#define FSRTL_OVERFLOW_QUEUE_NORMAL 0
#define FSRTL_OVERFLOW_QUEUE_PAGING 1
#define FSRTL_OVERFLOW_QUEUE_COUNT 2

typedef struct _FSRTL_OVERFLOW_REQUEST
{
    LIST_ENTRY Links;
    PFSRTL_STACK_OVERFLOW_ROUTINE Routine;
    PVOID Context;
    PKEVENT Event;
} FSRTL_OVERFLOW_REQUEST, *PFSRTL_OVERFLOW_REQUEST;

/* GLOBALS ********************************************************************/

KQUEUE FsRtlWorkerQueues[FSRTL_OVERFLOW_QUEUE_COUNT];

FSRTL_OVERFLOW_REQUEST StackOverflowFallback[FSRTL_OVERFLOW_QUEUE_COUNT];
KEVENT StackOverflowFallbackSerialEvent[FSRTL_OVERFLOW_QUEUE_COUNT];

static NPAGED_LOOKASIDE_LIST FsRtlOverflowLookaside;
static LONG FsRtlFallbackInUse[FSRTL_OVERFLOW_QUEUE_COUNT];

/* PRIVATE FUNCTIONS **********************************************************/

static
PFSRTL_OVERFLOW_REQUEST
FsRtlpClaimOverflowRequest(
    _In_ ULONG Queue)
{
    PFSRTL_OVERFLOW_REQUEST Request;

    Request = ExAllocateFromNPagedLookasideList(&FsRtlOverflowLookaside);
    if (Request != NULL) return Request;

    for (;;)
    {
        if (InterlockedCompareExchange(&FsRtlFallbackInUse[Queue],
                                       1,
                                       0) == 0)
        {
            return &StackOverflowFallback[Queue];
        }

        if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        {
            /* FIXME: with the pool empty and the one reserved request taken, a caller above PASSIVE_LEVEL cannot be given a stack and the work is dropped */
            return NULL;
        }

        KeWaitForSingleObject(&StackOverflowFallbackSerialEvent[Queue],
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}

static
VOID
FsRtlpReleaseOverflowRequest(
    _In_ ULONG Queue,
    _In_ PFSRTL_OVERFLOW_REQUEST Request)
{
    if (Request != &StackOverflowFallback[Queue])
    {
        ExFreeToNPagedLookasideList(&FsRtlOverflowLookaside, Request);
        return;
    }

    InterlockedExchange(&FsRtlFallbackInUse[Queue], 0);
    KeSetEvent(&StackOverflowFallbackSerialEvent[Queue], IO_NO_INCREMENT, FALSE);
}

static
VOID
FsRtlpPostStackOverflow(
    _In_ PVOID Context,
    _In_ PKEVENT Event,
    _In_ PFSRTL_STACK_OVERFLOW_ROUTINE StackOverflowRoutine,
    _In_ ULONG Queue)
{
    PFSRTL_OVERFLOW_REQUEST Request = FsRtlpClaimOverflowRequest(Queue);

    if (Request == NULL) return;

    Request->Routine = StackOverflowRoutine;
    Request->Context = Context;
    Request->Event = Event;

    KeInsertQueue(&FsRtlWorkerQueues[Queue], &Request->Links);
}

VOID
NTAPI
FsRtlWorkerThread(
    _In_ PVOID StartContext)
{
    ULONG Queue = (ULONG)(ULONG_PTR)StartContext;

    KeSetPriorityThread(&PsGetCurrentThread()->Tcb,
                        (Queue == FSRTL_OVERFLOW_QUEUE_PAGING) ?
                            LOW_REALTIME_PRIORITY + 1 : LOW_REALTIME_PRIORITY);

    for (;;)
    {
        PFSRTL_STACK_OVERFLOW_ROUTINE Routine;
        PFSRTL_OVERFLOW_REQUEST Request;
        PLIST_ENTRY Entry;
        PVOID Context;
        PKEVENT Event;

        Entry = KeRemoveQueue(&FsRtlWorkerQueues[Queue],
                              KernelMode,
                              NULL);

        Request = CONTAINING_RECORD(Entry, FSRTL_OVERFLOW_REQUEST, Links);
        Routine = Request->Routine;
        Context = Request->Context;
        Event = Request->Event;

        FsRtlpReleaseOverflowRequest(Queue, Request);

        Routine(Context, Event);
    }
}

/* INIT FUNCTIONS *************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeWorkerThread(VOID)
{
    ULONG Queue;

    ExInitializeNPagedLookasideList(&FsRtlOverflowLookaside,
                                    NULL,
                                    NULL,
                                    0,
                                    sizeof(FSRTL_OVERFLOW_REQUEST),
                                    FSRTL_OVERFLOW_TAG,
                                    0);

    for (Queue = 0; Queue < FSRTL_OVERFLOW_QUEUE_COUNT; Queue++)
    {
        HANDLE ThreadHandle;
        NTSTATUS Status;

        KeInitializeQueue(&FsRtlWorkerQueues[Queue], 1);
        KeInitializeEvent(&StackOverflowFallbackSerialEvent[Queue],
                          SynchronizationEvent,
                          FALSE);
        FsRtlFallbackInUse[Queue] = 0;

        Status = PsCreateSystemThread(&ThreadHandle,
                                      THREAD_ALL_ACCESS,
                                      NULL,
                                      NULL,
                                      NULL,
                                      FsRtlWorkerThread,
                                      (PVOID)(ULONG_PTR)Queue);

        if (NT_SUCCESS(Status)) ZwClose(ThreadHandle);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlPostStackOverflow(
    _In_ PVOID Context,
    _In_ PKEVENT Event,
    _In_ PFSRTL_STACK_OVERFLOW_ROUTINE StackOverflowRoutine)
{
    FsRtlpPostStackOverflow(Context,
                            Event,
                            StackOverflowRoutine,
                            FSRTL_OVERFLOW_QUEUE_NORMAL);
}

VOID
NTAPI
FsRtlPostPagingFileStackOverflow(
    _In_ PVOID Context,
    _In_ PKEVENT Event,
    _In_ PFSRTL_STACK_OVERFLOW_ROUTINE StackOverflowRoutine)
{
    FsRtlpPostStackOverflow(Context,
                            Event,
                            StackOverflowRoutine,
                            FSRTL_OVERFLOW_QUEUE_PAGING);
}

/* EOF */
