/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Getting what the registry changed onto the disk, in its own time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

BOOLEAN CmpHoldLazyFlush = TRUE;
BOOLEAN CmpLazyFlushPending = FALSE;
ULONG CmpLazyFlushCount = 1;
ULONG CmpLazyFlushHiveCount = CMP_LAZY_FLUSH_HIVES;
ULONG CmpLazyFlushIntervalInSeconds = CMP_LAZY_FLUSH_SECONDS;

KTIMER CmpLazyFlushTimer;
KDPC CmpLazyFlushDpc;
WORK_QUEUE_ITEM CmpLazyWorkItem;

KTIMER CmpEnableLazyFlushTimer;
KDPC CmpEnableLazyFlushDpc;

/* PRIVATE DATA ***************************************************************/

static BOOLEAN CmpFlusherStarted = FALSE;
static BOOLEAN CmpLazyWorkItemActive = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Writes out one hive that has something waiting, and says whether it found
 * anything to do, so the lazy flusher can spread the work over several turns.
 */
BOOLEAN
NTAPI
CmpDoFlushNextHive(
    _In_ BOOLEAN ForceFlush,
    _Out_opt_ PBOOLEAN Error,
    _Out_opt_ PULONG DirtyCount)
{
    PLIST_ENTRY Entry;
    PCMHIVE Hive = NULL;
    NTSTATUS Status;

    if (Error != NULL) *Error = FALSE;
    if (DirtyCount != NULL) *DirtyCount = 0;

    CmpLockRegistry();

    ExAcquirePushLockShared(&CmpHiveListHeadLock);

    for (Entry = CmpHiveListHead.Flink;
         Entry != &CmpHiveListHead;
         Entry = Entry->Flink)
    {
        Hive = CONTAINING_RECORD(Entry, CMHIVE, HiveList);

        if (Hive->Hive.HiveFlags & HIVE_VOLATILE) continue;
        if (Hive->Hive.HiveFlags & HIVE_NOLAZYFLUSH) continue;
        if (!ForceFlush && Hive->Hive.DirtyCount == 0) continue;

        CmpReferenceHive(Hive);
        break;
    }

    if (Entry == &CmpHiveListHead) Hive = NULL;

    ExReleasePushLockShared(&CmpHiveListHeadLock);

    if (Hive == NULL)
    {
        CmpUnlockRegistry();
        return FALSE;
    }

    if (DirtyCount != NULL) *DirtyCount = Hive->Hive.DirtyCount;

    Status = CmpFlushHiveToDisk(Hive, ForceFlush);
    if (!NT_SUCCESS(Status) && Error != NULL) *Error = TRUE;

    CmpDereferenceHive(Hive);
    CmpUnlockRegistry();
    return TRUE;
}

BOOLEAN
NTAPI
CmpDoFlushAll(
    _In_ BOOLEAN ForceFlush)
{
    BOOLEAN Result = TRUE;
    PLIST_ENTRY Entry;
    PCMHIVE Hive;
    NTSTATUS Status;

    if (CmpNoWrite) return TRUE;

    CmpBeginStarveWriters();

    ExAcquirePushLockShared(&CmpHiveListHeadLock);

    for (Entry = CmpHiveListHead.Flink;
         Entry != &CmpHiveListHead;
         Entry = Entry->Flink)
    {
        Hive = CONTAINING_RECORD(Entry, CMHIVE, HiveList);

        if (Hive->Hive.HiveFlags & HIVE_VOLATILE) continue;
        if (!ForceFlush && Hive->Hive.DirtyCount == 0) continue;

        Status = CmpFlushHiveToDisk(Hive, ForceFlush);
        if (!NT_SUCCESS(Status)) Result = FALSE;
    }

    ExReleasePushLockShared(&CmpHiveListHeadLock);

    CmpEndStarveWriters();
    return Result;
}

VOID
NTAPI
CmpLazyFlushWorker(
    _In_ PVOID Parameter)
{
    ULONG Flushed = 0;
    BOOLEAN Error;

    UNREFERENCED_PARAMETER(Parameter);

    CmpLazyFlushPending = FALSE;

    /* The first turn of the flusher is the first moment a hive file can be
       opened, because by then a file system is running */
    CmpInitializeHiveList();
    CmpFlushSelfHealQueue();

    while (Flushed < CmpLazyFlushHiveCount)
    {
        if (!CmpDoFlushNextHive(FALSE, &Error, NULL)) break;
        Flushed++;
    }

    CmpLazyWorkItemActive = FALSE;

    if (Flushed != 0) CmpLazyFlush();
}

VOID
NTAPI
CmpLazyFlushDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (CmpLazyWorkItemActive) return;

    CmpLazyWorkItemActive = TRUE;
    ExQueueWorkItem(&CmpLazyWorkItem, DelayedWorkQueue);
}

VOID
NTAPI
CmpEnableLazyFlushDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    CmpHoldLazyFlush = FALSE;
}

/*
 * Asked for by whoever dirtied a hive. Nothing is written here: a timer is
 * set so that a burst of changes costs one write rather than many.
 */
VOID
NTAPI
CmpLazyFlush(VOID)
{
    LARGE_INTEGER Due;

    if (!CmpFlusherStarted) return;
    if (CmpNoWrite || CmpHoldLazyFlush) return;
    if (CmpLazyFlushPending) return;

    CmpLazyFlushPending = TRUE;

    Due.QuadPart = Int32x32To64(CmpLazyFlushIntervalInSeconds, -10000000);
    KeSetTimer(&CmpLazyFlushTimer, Due, &CmpLazyFlushDpc);
}

/*
 * Setup turns lazy flushing off while it writes a great deal, and back on a
 * moment later, so the writes land in one go.
 */
VOID
NTAPI
CmSetLazyFlushState(
    _In_ BOOLEAN Enable)
{
    LARGE_INTEGER Due;

    if (Enable)
    {
        CmpHoldLazyFlush = FALSE;
        return;
    }

    CmpHoldLazyFlush = TRUE;

    if (!CmpFlusherStarted) return;

    Due.QuadPart = Int32x32To64(CmpLazyFlushIntervalInSeconds * 2, -10000000);
    KeSetTimer(&CmpEnableLazyFlushTimer, Due, &CmpEnableLazyFlushDpc);
}

CODE_SEG("INIT")
VOID
NTAPI
CmpCmdInit(
    _In_ BOOLEAN SetupBoot)
{
    KeInitializeTimer(&CmpLazyFlushTimer);
    KeInitializeDpc(&CmpLazyFlushDpc, CmpLazyFlushDpcRoutine, NULL);
    ExInitializeWorkItem(&CmpLazyWorkItem, CmpLazyFlushWorker, NULL);

    KeInitializeTimer(&CmpEnableLazyFlushTimer);
    KeInitializeDpc(&CmpEnableLazyFlushDpc,
                    CmpEnableLazyFlushDpcRoutine,
                    NULL);

    CmpFlusherStarted = TRUE;

    if (SetupBoot)
    {
        CmpWasSetupBoot = TRUE;
        CmpHoldLazyFlush = TRUE;
        return;
    }

    CmpHoldLazyFlush = FALSE;
}

VOID
NTAPI
CmpShutdownWorkers(VOID)
{
    if (!CmpFlusherStarted) return;

    CmpFlusherStarted = FALSE;
    CmpHoldLazyFlush = TRUE;

    KeCancelTimer(&CmpLazyFlushTimer);
    KeCancelTimer(&CmpEnableLazyFlushTimer);
}

/*
 * The registry's last act. Everything still waiting is written, the hives are
 * closed for business and the caches are run down.
 */
VOID
NTAPI
CmShutdownSystem(VOID)
{
    CmpShutdownWorkers();

    CmpLockRegistryExclusive();

    CmpDoFlushAll(TRUE);
    HvShutdownComplete = TRUE;

    CmpUnlockRegistry();

    CmpRunDownDelayDerefKCBEngine();
}
