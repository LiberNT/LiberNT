/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Making a damaged hive usable instead of refusing to boot on it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

typedef struct _CM_SELF_HEAL_ENTRY
{
    LIST_ENTRY ListEntry;
    PCMHIVE Hive;
    CM_CHECK_REGISTRY_STATUS CheckStatus;
} CM_SELF_HEAL_ENTRY, *PCM_SELF_HEAL_ENTRY;

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
CmpQueueSelfHeal(
    _In_ PCMHIVE Hive,
    _In_ CM_CHECK_REGISTRY_STATUS CheckStatus)
{
    PCM_SELF_HEAL_ENTRY Entry;

    Entry = ExAllocatePoolZero(NonPagedPool,
                               sizeof(CM_SELF_HEAL_ENTRY),
                               TAG_CM);
    if (Entry == NULL) return;

    Entry->Hive = Hive;
    Entry->CheckStatus = CheckStatus;

    KeAcquireGuardedMutex(&CmpSelfHealQueueLock);
    InsertTailList(&CmpSelfHealQueueListHead, &Entry->ListEntry);
    KeReleaseGuardedMutex(&CmpSelfHealQueueLock);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Looks a freshly read hive over. A hive that checks out is used as it is; a
 * hive that does not is repaired where self-healing is allowed, and put on
 * the queue so that the repaired form reaches the disk. Only a hive that
 * cannot be repaired is refused.
 */
CM_CHECK_REGISTRY_STATUS
NTAPI
CmpSelfHealHive(
    _In_ PCMHIVE Hive,
    _In_ ULONG CheckFlags)
{
    CM_CHECK_REGISTRY_STATUS CheckStatus;

    if (CheckFlags == 0) return CM_CHECK_REGISTRY_GOOD;

    CheckStatus = CmCheckRegistry(Hive, CheckFlags);
    if (CM_CHECK_REGISTRY_SUCCESS(CheckStatus)) return CheckStatus;

    if (Hive->Hive.ReadOnly) return CheckStatus;
    if (!CmIsSelfHealEnabled(TRUE)) return CheckStatus;

    DPRINT1("Hive check said %lu, repairing\n", CheckStatus);

    CheckStatus = CmCheckRegistry(Hive,
                                  CheckFlags |
                                      CM_CHECK_REGISTRY_FIX_HIVE |
                                      CM_CHECK_REGISTRY_PURGE_VOLATILES);
    if (!CM_CHECK_REGISTRY_SUCCESS(CheckStatus)) return CheckStatus;

    Hive->Hive.HiveFlags |= HIVE_HAS_BEEN_REPLACED;
    CmpQueueSelfHeal(Hive, CheckStatus);
    return CM_CHECK_REGISTRY_GOOD;
}

/*
 * Writes out every hive that had to be repaired, because a repair that only
 * ever lives in memory is a repair the next boot has to make again.
 */
VOID
NTAPI
CmpFlushSelfHealQueue(VOID)
{
    PCM_SELF_HEAL_ENTRY Entry;
    PLIST_ENTRY ListEntry;

    for (;;)
    {
        KeAcquireGuardedMutex(&CmpSelfHealQueueLock);

        if (IsListEmpty(&CmpSelfHealQueueListHead))
        {
            KeReleaseGuardedMutex(&CmpSelfHealQueueLock);
            return;
        }

        ListEntry = RemoveHeadList(&CmpSelfHealQueueListHead);
        KeReleaseGuardedMutex(&CmpSelfHealQueueLock);

        Entry = CONTAINING_RECORD(ListEntry, CM_SELF_HEAL_ENTRY, ListEntry);

        if (Entry->Hive != NULL)
        {
            Entry->Hive->Hive.HiveFlags &= ~HIVE_HAS_BEEN_REPLACED;
            CmpFlushHiveToDisk(Entry->Hive, TRUE);
        }

        ExFreePool(Entry);
    }
}

/* Takes a hive off the queue when it goes away before it was written out */
VOID
NTAPI
CmpRemoveFromSelfHealQueue(
    _In_ PCMHIVE Hive)
{
    PCM_SELF_HEAL_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    PLIST_ENTRY Next;

    KeAcquireGuardedMutex(&CmpSelfHealQueueLock);

    for (ListEntry = CmpSelfHealQueueListHead.Flink;
         ListEntry != &CmpSelfHealQueueListHead;
         ListEntry = Next)
    {
        Next = ListEntry->Flink;
        Entry = CONTAINING_RECORD(ListEntry, CM_SELF_HEAL_ENTRY, ListEntry);

        if (Entry->Hive != Hive) continue;

        RemoveEntryList(ListEntry);
        ExFreePool(Entry);
    }

    KeReleaseGuardedMutex(&CmpSelfHealQueueLock);
}
