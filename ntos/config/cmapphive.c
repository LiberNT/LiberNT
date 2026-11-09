/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hives that belong to one process, and waiting for one to go
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

/*
 * Which process a private application hive belongs to. Windows 11 has no
 * published CMHIVE field for this and a private field may not be added to a
 * Windows structure, so the pairing is kept beside the hives instead.
 */
typedef struct _CM_APP_HIVE_OWNER
{
    LIST_ENTRY ListEntry;
    PCMHIVE Hive;
    PEPROCESS Process;
    PCMHIVE TrustClass;
} CM_APP_HIVE_OWNER, *PCM_APP_HIVE_OWNER;

static LIST_ENTRY CmpAppHiveOwnerListHead;
static EX_PUSH_LOCK CmpAppHiveOwnerLock;
static BOOLEAN CmpAppHiveOwnersInited = FALSE;

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitAppHives(VOID)
{
    InitializeListHead(&CmpAppHiveOwnerListHead);
    ExInitializePushLock(&CmpAppHiveOwnerLock);
    CmpAppHiveOwnersInited = TRUE;
}

/*
 * Records who a hive belongs to. A hive loaded against a trust class holds a
 * reference on that class's hive, so the class cannot be unloaded out from
 * under what was loaded against it.
 */
NTSTATUS
NTAPI
CmpRegisterAppHive(
    _In_ PCMHIVE Hive,
    _In_opt_ PEPROCESS Process,
    _In_opt_ PCMHIVE TrustClass)
{
    PCM_APP_HIVE_OWNER Owner;

    if (!CmpAppHiveOwnersInited) return STATUS_UNSUCCESSFUL;

    Owner = ExAllocatePoolZero(NonPagedPool,
                               sizeof(CM_APP_HIVE_OWNER),
                               TAG_CMHIVE);
    if (Owner == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Owner->Hive = Hive;
    Owner->Process = Process;
    Owner->TrustClass = TrustClass;

    if (TrustClass != NULL) CmpReferenceHive(TrustClass);

    ExAcquirePushLockExclusive(&CmpAppHiveOwnerLock);
    InsertTailList(&CmpAppHiveOwnerListHead, &Owner->ListEntry);
    ExReleasePushLockExclusive(&CmpAppHiveOwnerLock);

    return STATUS_SUCCESS;
}

VOID
NTAPI
CmpUnregisterAppHive(
    _In_ PCMHIVE Hive)
{
    PCM_APP_HIVE_OWNER Owner;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    if (!CmpAppHiveOwnersInited) return;

    ExAcquirePushLockExclusive(&CmpAppHiveOwnerLock);

    for (Entry = CmpAppHiveOwnerListHead.Flink;
         Entry != &CmpAppHiveOwnerListHead;
         Entry = Next)
    {
        Next = Entry->Flink;
        Owner = CONTAINING_RECORD(Entry, CM_APP_HIVE_OWNER, ListEntry);

        if (Owner->Hive != Hive) continue;

        RemoveEntryList(Entry);

        if (Owner->TrustClass != NULL) CmpDereferenceHive(Owner->TrustClass);

        ExFreePool(Owner);
    }

    ExReleasePushLockExclusive(&CmpAppHiveOwnerLock);
}

PEPROCESS
NTAPI
CmpAppHiveOwner(
    _In_ PCMHIVE Hive)
{
    PCM_APP_HIVE_OWNER Owner;
    PEPROCESS Process = NULL;
    PLIST_ENTRY Entry;

    if (!CmpAppHiveOwnersInited) return NULL;

    ExAcquirePushLockShared(&CmpAppHiveOwnerLock);

    for (Entry = CmpAppHiveOwnerListHead.Flink;
         Entry != &CmpAppHiveOwnerListHead;
         Entry = Entry->Flink)
    {
        Owner = CONTAINING_RECORD(Entry, CM_APP_HIVE_OWNER, ListEntry);

        if (Owner->Hive != Hive) continue;

        Process = Owner->Process;
        break;
    }

    ExReleasePushLockShared(&CmpAppHiveOwnerLock);
    return Process;
}

/*
 * A caller that asked to be told when a hive finally goes gets its event put
 * on the hive, and the hive signals every one of them on its way out.
 */
NTSTATUS
NTAPI
CmpAddUnloadEvent(
    _Inout_ PCMHIVE Hive,
    _In_ PKEVENT Event)
{
    struct _KEVENT **Array;
    ULONG Count = Hive->UnloadEventCount;

    Array = ExAllocatePoolZero(NonPagedPool,
                               (Count + 1) * sizeof(PVOID),
                               TAG_CMHIVE);
    if (Array == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Hive->UnloadEventArray != NULL)
    {
        RtlCopyMemory(Array,
                      Hive->UnloadEventArray,
                      Count * sizeof(PVOID));
        ExFreePool(Hive->UnloadEventArray);
    }

    Array[Count] = (struct _KEVENT *)Event;

    Hive->UnloadEventArray = Array;
    Hive->UnloadEventCount = Count + 1;
    return STATUS_SUCCESS;
}

/* Gives the events back unsignalled, for an unload that did not happen */
VOID
NTAPI
CmpDiscardUnloadEvents(
    _Inout_ PCMHIVE Hive)
{
    struct _KEVENT **Array = Hive->UnloadEventArray;
    ULONG Count = Hive->UnloadEventCount;
    ULONG i;

    Hive->UnloadEventArray = NULL;
    Hive->UnloadEventCount = 0;

    if (Array == NULL) return;

    for (i = 0; i < Count; i++)
    {
        if (Array[i] != NULL) ObDereferenceObject(Array[i]);
    }

    ExFreePool(Array);
}

VOID
NTAPI
CmpSignalUnloadEvents(
    _Inout_ PCMHIVE Hive)
{
    struct _KEVENT **Array = Hive->UnloadEventArray;
    ULONG Count = Hive->UnloadEventCount;
    ULONG i;

    Hive->UnloadEventArray = NULL;
    Hive->UnloadEventCount = 0;

    if (Array == NULL) return;

    for (i = 0; i < Count; i++)
    {
        if (Array[i] == NULL) continue;

        KeSetEvent((PKEVENT)Array[i], EVENT_INCREMENT, FALSE);
        ObDereferenceObject(Array[i]);
    }

    ExFreePool(Array);
}
