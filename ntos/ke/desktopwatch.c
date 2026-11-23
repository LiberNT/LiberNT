/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Detect stalled input dispatch and presentation from the clock interrupt
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntoskrnl.h>
#include <reactos/desktopwatchdog.h>

#define KI_DESKTOP_TIMEOUT 100000000ULL

typedef struct _LNT_DESKTOP_WATCH
{
    LIST_ENTRY Link;
    LNT_DESKTOP_WATCH_KIND Kind;
    ULONG Session;
    PKEVENT ReadEvent;
    PIO_STATUS_BLOCK ReadStatus;
    ULONG Draws;
    BOOLEAN Observing;
    BOOLEAN Composed;
    BOOLEAN Occluded;
    BOOLEAN Suspended;
    ULONGLONG Pending;
    ULONGLONG Pull;
    ULONGLONG Frame;
} LNT_DESKTOP_WATCH;

static LIST_ENTRY KiDesktopWatches = { &KiDesktopWatches, &KiDesktopWatches };
static KSPIN_LOCK KiDesktopWatchLock;
extern KEVENT PopShutdownEvent;

static ULONGLONG
KiEarlierDeadline(ULONGLONG First, ULONGLONG Second)
{
    return !First || (Second && Second < First) ? Second : First;
}

static VOID
KiSetCompositorState(PLNT_DESKTOP_WATCH Watch, BOOLEAN Attached, ULONGLONG Deadline)
{
    PLIST_ENTRY Link;

    ASSERT(Watch->Kind == LntWatchCompositor);
    Watch->Composed = Attached;
    Watch->Occluded = FALSE;
    Watch->Pending = Watch->Pull = Watch->Frame = 0;
    for (Link = KiDesktopWatches.Flink; Link != &KiDesktopWatches; Link = Link->Flink)
    {
        PLNT_DESKTOP_WATCH Output = CONTAINING_RECORD(Link, LNT_DESKTOP_WATCH, Link);
        if (Output->Session == Watch->Session && Output->Kind == LntWatchFramebuffer)
        {
            Output->Composed = Attached;
            if (Output->Pending)
                Output->Pending = Deadline;
        }
    }
}

PLNT_DESKTOP_WATCH
NTAPI
LntCreateDesktopWatch(LNT_DESKTOP_WATCH_KIND Kind, PKEVENT ReadEvent, PIO_STATUS_BLOCK ReadStatus)
{
    PLNT_DESKTOP_WATCH Watch;
    PLIST_ENTRY Link;
    KIRQL Irql;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(Kind >= LntWatchInput && Kind <= LntWatchFramebuffer);
    ASSERT((Kind == LntWatchInput) == (ReadEvent != NULL));
    Watch = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Watch), 'wDsK');
    if (!Watch)
    {
        DbgPrint("Desktop watchdog registration failed for kind %u\n", (ULONG)Kind);
        return NULL;
    }
    Watch->Kind = Kind;
    Watch->Session = PsGetCurrentProcessSessionId();
    ASSERT((ReadStatus != NULL) == (ReadEvent != NULL));
    Watch->ReadStatus = ReadStatus;
    Watch->ReadEvent = ReadEvent;
    if (ReadEvent)
        ObReferenceObject(ReadEvent);
    Irql = KfRaiseIrql(HIGH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(&KiDesktopWatchLock);
    if (Kind == LntWatchFramebuffer)
    {
        for (Link = KiDesktopWatches.Flink; Link != &KiDesktopWatches; Link = Link->Flink)
        {
            PLNT_DESKTOP_WATCH Other = CONTAINING_RECORD(Link, LNT_DESKTOP_WATCH, Link);
            if (Other->Kind == LntWatchCompositor && Other->Session == Watch->Session && Other->Composed)
                Watch->Composed = TRUE;
        }
    }
    InsertTailList(&KiDesktopWatches, &Watch->Link);
    KeReleaseSpinLock(&KiDesktopWatchLock, Irql);
    return Watch;
}

VOID
NTAPI
LntDestroyDesktopWatch(PLNT_DESKTOP_WATCH Watch)
{
    KIRQL Irql;

    if (!Watch)
        return;
    Irql = KfRaiseIrql(HIGH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(&KiDesktopWatchLock);
    if (Watch->Kind == LntWatchCompositor && Watch->Composed)
        KiSetCompositorState(Watch, FALSE, KiDesktopInterruptTime() + KI_DESKTOP_TIMEOUT);
    RemoveEntryList(&Watch->Link);
    KeReleaseSpinLock(&KiDesktopWatchLock, Irql);
    if (Watch->ReadEvent)
        ObDereferenceObject(Watch->ReadEvent);
    ExFreePoolWithTag(Watch, 'wDsK');
}

VOID
NTAPI
LntUpdateDesktopWatch(PLNT_DESKTOP_WATCH Watch, LNT_DESKTOP_WATCH_ACTION Action)
{
    ULONGLONG Deadline;
    KIRQL Irql;

    if (!Watch)
        return;
    Irql = KfRaiseIrql(HIGH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(&KiDesktopWatchLock);
    Deadline = KiDesktopInterruptTime() + KI_DESKTOP_TIMEOUT;
    switch (Action)
    {
        case LntWatchArm:
            if (!Watch->Pending && (Watch->Kind != LntWatchCompositor || (Watch->Composed && !Watch->Occluded)))
                Watch->Pending = Deadline;
            break;
        case LntWatchBeginDraw:
            ASSERT(Watch->Kind == LntWatchFramebuffer && Watch->Draws < MAXULONG);
            if (!Watch->Draws++)
                Watch->Pending = Deadline;
            break;
        case LntWatchEndDraw:
            ASSERT(Watch->Kind == LntWatchFramebuffer && Watch->Draws);
            if (Watch->Draws)
                Watch->Pending = --Watch->Draws ? Deadline : 0;
            break;
        case LntWatchObserveRead:
            ASSERT(Watch->ReadEvent != NULL);
            Watch->Observing = TRUE;
            break;
        case LntWatchComplete:
            Watch->Observing = FALSE;
            Watch->Pending = 0;
            break;
        case LntWatchProgress:
            Watch->Pending = Deadline;
            break;
        case LntWatchSuspendOutput:
        case LntWatchResumeOutput:
            ASSERT(Watch->Kind == LntWatchFramebuffer);
            Watch->Suspended = Action == LntWatchSuspendOutput;
            if (Watch->Pending)
                Watch->Pending = Deadline;
            break;
        case LntWatchAttach:
        case LntWatchDetach:
            KiSetCompositorState(Watch, Action == LntWatchAttach, Deadline);
            break;
        case LntWatchPull:
            Watch->Occluded = FALSE;
            Watch->Pull = KiEarlierDeadline(Watch->Pull, Watch->Pending);
            Watch->Pending = 0;
            break;
        case LntWatchDirtyFrame:
            Watch->Frame = KiEarlierDeadline(Watch->Frame, Watch->Pull ? Watch->Pull : Deadline);
            Watch->Pull = 0;
            break;
        case LntWatchEmptyFrame:
            Watch->Pull = 0;
            break;
        case LntWatchPresented:
            if (Watch->Frame && Watch->Pending)
                Watch->Pending = Deadline;
            Watch->Frame = 0;
            break;
        case LntWatchOccluded:
            Watch->Occluded = TRUE;
            Watch->Pending = Watch->Pull = Watch->Frame = 0;
            break;
    }
    KeReleaseSpinLock(&KiDesktopWatchLock, Irql);
}

VOID
KiCheckDesktopWatchdogs(ULONGLONG Now)
{
    PLIST_ENTRY Link;
    ULONG Kind = 0, Session = 0;
    ULONGLONG Deadline = 0;

    if (!KeTryToAcquireSpinLockAtDpcLevel(&KiDesktopWatchLock))
        return;
    for (Link = KiDesktopWatches.Flink; Link != &KiDesktopWatches; Link = Link->Flink)
    {
        PLNT_DESKTOP_WATCH Watch = CONTAINING_RECORD(Link, LNT_DESKTOP_WATCH, Link);
        if (Watch->Kind == LntWatchFramebuffer && (Watch->Composed || Watch->Suspended))
            continue;
        if (Watch->Observing && !Watch->Pending &&
            ReadAcquire(&Watch->ReadEvent->Header.SignalState) &&
            NT_SUCCESS(Watch->ReadStatus->Status) && Watch->ReadStatus->Information != 0)
            Watch->Pending = Now + KI_DESKTOP_TIMEOUT;
        Deadline = KiEarlierDeadline(Watch->Pending, KiEarlierDeadline(Watch->Pull, Watch->Frame));
        if (Deadline && Now >= Deadline)
        {
            Kind = Watch->Kind;
            Session = Watch->Session;
            break;
        }
    }
    KeReleaseSpinLockFromDpcLevel(&KiDesktopWatchLock);
    if (Kind && !KeReadStateEvent(&PopShutdownEvent))
        KeBugCheckEx(LIBERNT_DESKTOP_WATCHDOG_TIMEOUT, Kind, Session,
                     (ULONG_PTR)((Now - Deadline + KI_DESKTOP_TIMEOUT) / 10000), 10000);
}
