/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keeping a closed key around a while in case it is opened again
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

LIST_ENTRY CmpDelayedCloseListHead;
ULONG CmpDelayedCloseElements = 0;
ULONG CmpDelayCloseIntervalInSeconds = CMP_DELAY_CLOSE_SECONDS;

KTIMER CmpDelayCloseTimer;
KDPC CmpDelayCloseDpc;
WORK_QUEUE_ITEM CmpDelayCloseWorkItem;
BOOLEAN CmpDelayCloseWorkItemActive = FALSE;

LIST_ENTRY CmpDelayDerefKCBListHead;
KGUARDED_MUTEX CmpDelayDerefKCBLock;
ULONG CmpDelayDerefKCBIntervalInSeconds = CMP_DELAY_DEREF_SECONDS;

KTIMER CmpDelayDerefKCBTimer;
KDPC CmpDelayDerefKCBDpc;
WORK_QUEUE_ITEM CmpDelayDerefKCBWorkItem;
BOOLEAN CmpDelayDerefKCBWorkItemActive = FALSE;

/* PRIVATE DATA ***************************************************************/

static KGUARDED_MUTEX CmpDelayedCloseTableLock;
static BOOLEAN CmpDelayCloseEngineRunning = FALSE;
static BOOLEAN CmpDelayDerefEngineRunning = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

static
PCM_DELAYED_CLOSE_ENTRY
CmpTakeOldestDelayedClose(VOID)
{
    PCM_DELAYED_CLOSE_ENTRY Entry;
    PLIST_ENTRY ListEntry;

    KeAcquireGuardedMutex(&CmpDelayedCloseTableLock);

    if (IsListEmpty(&CmpDelayedCloseListHead))
    {
        KeReleaseGuardedMutex(&CmpDelayedCloseTableLock);
        return NULL;
    }

    ListEntry = RemoveHeadList(&CmpDelayedCloseListHead);
    CmpDelayedCloseElements--;

    KeReleaseGuardedMutex(&CmpDelayedCloseTableLock);

    Entry = CONTAINING_RECORD(ListEntry, CM_DELAYED_CLOSE_ENTRY, DelayedLRUList);
    return Entry;
}

/* Lets go of the oldest entries until the table is back inside its budget */
static
VOID
CmpDrainDelayedClose(
    _In_ ULONG Budget)
{
    PCM_DELAYED_CLOSE_ENTRY Entry;
    PCM_KEY_CONTROL_BLOCK Kcb;
    ULONG ConvKey;

    while (CmpDelayedCloseElements > Budget)
    {
        Entry = CmpTakeOldestDelayedClose();
        if (Entry == NULL) return;

        Kcb = Entry->KeyControlBlock;
        ConvKey = Kcb->ConvKey;

        CmpAcquireKcbLockExclusiveByKey(ConvKey);

        if (Kcb->InDelayClose != 0 && Kcb->RefCount == 0)
        {
            Kcb->InDelayClose = 0;
            Kcb->DelayCloseEntry = NULL;
            CmpFreeDelayItem(Entry);
            CmpCleanUpKcbCacheWithLock(Kcb, TRUE);
            CmpReleaseKcbLockByKey(ConvKey);
            continue;
        }

        CmpReleaseKcbLockByKey(ConvKey);
        CmpFreeDelayItem(Entry);
    }
}

VOID
NTAPI
CmpDelayCloseWorker(
    _In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    CmpDrainDelayedClose(0);
    CmpDelayCloseWorkItemActive = FALSE;

    if (CmpDelayedCloseElements != 0) CmpArmDelayedCloseTimer();
}

VOID
NTAPI
CmpDelayCloseDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (CmpDelayCloseWorkItemActive) return;

    CmpDelayCloseWorkItemActive = TRUE;
    ExQueueWorkItem(&CmpDelayCloseWorkItem, DelayedWorkQueue);
}

VOID
NTAPI
CmpDelayDerefKCBWorker(
    _In_ PVOID Parameter)
{
    PCM_DELAY_DEREF_KCB_ITEM Item;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(Parameter);

    for (;;)
    {
        KeAcquireGuardedMutex(&CmpDelayDerefKCBLock);

        if (IsListEmpty(&CmpDelayDerefKCBListHead))
        {
            KeReleaseGuardedMutex(&CmpDelayDerefKCBLock);
            break;
        }

        Entry = RemoveHeadList(&CmpDelayDerefKCBListHead);
        KeReleaseGuardedMutex(&CmpDelayDerefKCBLock);

        Item = CONTAINING_RECORD(Entry, CM_DELAY_DEREF_KCB_ITEM, ListEntry);
        CmpDereferenceKeyControlBlock(Item->Kcb);
        CmpFreeDelayItem(Item);
    }

    CmpDelayDerefKCBWorkItemActive = FALSE;
}

VOID
NTAPI
CmpDelayDerefKCBDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (CmpDelayDerefKCBWorkItemActive) return;

    CmpDelayDerefKCBWorkItemActive = TRUE;
    ExQueueWorkItem(&CmpDelayDerefKCBWorkItem, DelayedWorkQueue);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeDelayedCloseTable(VOID)
{
    InitializeListHead(&CmpDelayedCloseListHead);
    KeInitializeGuardedMutex(&CmpDelayedCloseTableLock);

    ExInitializeWorkItem(&CmpDelayCloseWorkItem, CmpDelayCloseWorker, NULL);
    KeInitializeDpc(&CmpDelayCloseDpc, CmpDelayCloseDpcRoutine, NULL);
    KeInitializeTimer(&CmpDelayCloseTimer);

    CmpDelayCloseEngineRunning = TRUE;
}

CODE_SEG("INIT")
VOID
NTAPI
CmpInitDelayDerefKCBEngine(VOID)
{
    InitializeListHead(&CmpDelayDerefKCBListHead);
    KeInitializeGuardedMutex(&CmpDelayDerefKCBLock);

    ExInitializeWorkItem(&CmpDelayDerefKCBWorkItem,
                         CmpDelayDerefKCBWorker,
                         NULL);
    KeInitializeDpc(&CmpDelayDerefKCBDpc, CmpDelayDerefKCBDpcRoutine, NULL);
    KeInitializeTimer(&CmpDelayDerefKCBTimer);

    CmpDelayDerefEngineRunning = TRUE;
}

VOID
NTAPI
CmpArmDelayedCloseTimer(VOID)
{
    LARGE_INTEGER Due;

    if (!CmpDelayCloseEngineRunning) return;

    Due.QuadPart = Int32x32To64(CmpDelayCloseIntervalInSeconds, -10000000);
    KeSetTimer(&CmpDelayCloseTimer, Due, &CmpDelayCloseDpc);
}

VOID
NTAPI
CmpAddToDelayedClose(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeldExclusively)
{
    PCM_DELAYED_CLOSE_ENTRY Entry;

    ASSERT_KCB_VALID(Kcb);
    ASSERT(Kcb->RefCount == 0);

    if (Kcb->InDelayClose != 0) return;

    if (!CmpDelayCloseEngineRunning)
    {
        CmpCleanUpKcbCacheWithLock(Kcb, LockHeldExclusively);
        return;
    }

    Entry = CmpAllocateDelayItem();
    if (Entry == NULL)
    {
        CmpCleanUpKcbCacheWithLock(Kcb, LockHeldExclusively);
        return;
    }

    Entry->KeyControlBlock = Kcb;
    Kcb->DelayCloseEntry = Entry;
    Kcb->InDelayClose = 1;

    KeAcquireGuardedMutex(&CmpDelayedCloseTableLock);
    InsertTailList(&CmpDelayedCloseListHead, &Entry->DelayedLRUList);
    CmpDelayedCloseElements++;
    KeReleaseGuardedMutex(&CmpDelayedCloseTableLock);

    if (CmpDelayedCloseElements <= CmpDelayedCloseSize)
    {
        CmpArmDelayedCloseTimer();
        return;
    }

    if (!CmpDelayCloseWorkItemActive)
    {
        CmpDelayCloseWorkItemActive = TRUE;
        ExQueueWorkItem(&CmpDelayCloseWorkItem, DelayedWorkQueue);
    }
}

VOID
NTAPI
CmpRemoveFromDelayedClose(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    PCM_DELAYED_CLOSE_ENTRY Entry;

    if (Kcb->InDelayClose == 0) return;

    Entry = Kcb->DelayCloseEntry;
    Kcb->InDelayClose = 0;
    Kcb->DelayCloseEntry = NULL;

    if (Entry == NULL) return;

    KeAcquireGuardedMutex(&CmpDelayedCloseTableLock);
    RemoveEntryList(&Entry->DelayedLRUList);
    CmpDelayedCloseElements--;
    KeReleaseGuardedMutex(&CmpDelayedCloseTableLock);

    CmpFreeDelayItem(Entry);
}

/*
 * Used where a reference has to go away but the caller holds a lock the
 * dereference would need, so the drop is handed to a worker instead.
 */
VOID
NTAPI
CmpDelayDerefKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    PCM_DELAY_DEREF_KCB_ITEM Item;
    LARGE_INTEGER Due;

    if (!CmpDelayDerefEngineRunning)
    {
        CmpDereferenceKeyControlBlock(Kcb);
        return;
    }

    Item = CmpAllocateDelayItem();
    if (Item == NULL)
    {
        CmpDereferenceKeyControlBlock(Kcb);
        return;
    }

    Item->Kcb = Kcb;

    KeAcquireGuardedMutex(&CmpDelayDerefKCBLock);
    InsertTailList(&CmpDelayDerefKCBListHead, &Item->ListEntry);
    KeReleaseGuardedMutex(&CmpDelayDerefKCBLock);

    Due.QuadPart = Int32x32To64(CmpDelayDerefKCBIntervalInSeconds, -10000000);
    KeSetTimer(&CmpDelayDerefKCBTimer, Due, &CmpDelayDerefKCBDpc);
}

VOID
NTAPI
CmpRunDownDelayDerefKCBEngine(VOID)
{
    if (!CmpDelayDerefEngineRunning) return;

    KeCancelTimer(&CmpDelayDerefKCBTimer);
    CmpDelayDerefEngineRunning = FALSE;
    CmpDelayDerefKCBWorker(NULL);

    KeCancelTimer(&CmpDelayCloseTimer);
    CmpDelayCloseEngineRunning = FALSE;
    CmpDrainDelayedClose(0);
}
