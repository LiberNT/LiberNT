/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Page-packed room for key control blocks and their delay items
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

BOOLEAN CmpAllocInited = FALSE;

KGUARDED_MUTEX CmpAllocBucketLock;
KGUARDED_MUTEX CmpDelayAllocBucketLock;

LIST_ENTRY CmpFreeKCBListHead;
LIST_ENTRY CmpFreeDelayItemsListHead;

/* PRIVATE FUNCTIONS **********************************************************/

/* A page only ever holds items of one kind, so one carver serves both lists */
static
BOOLEAN
CmpCarveAllocPage(
    _In_ PLIST_ENTRY FreeList,
    _In_ ULONG ItemSize,
    _In_ ULONG ItemsPerPage)
{
    PCM_ALLOC_PAGE Page;
    PLIST_ENTRY Item;
    ULONG i;

    Page = ExAllocatePoolZero(NonPagedPool, PAGE_SIZE, TAG_KCB);
    if (Page == NULL) return FALSE;

    if (((ULONG_PTR)Page & (PAGE_SIZE - 1)) != 0)
    {
        ExFreePool(Page);
        return FALSE;
    }

    Page->FreeCount = ItemsPerPage;

    for (i = 0; i < ItemsPerPage; i++)
    {
        Item = (PLIST_ENTRY)((PUCHAR)&Page->AllocPage + i * ItemSize);
        InsertTailList(FreeList, Item);
    }

    return TRUE;
}

/* Hands the page back once nothing in it is in use any more */
static
VOID
CmpReleaseAllocPage(
    _In_ PCM_ALLOC_PAGE Page,
    _In_ ULONG ItemSize,
    _In_ ULONG ItemsPerPage)
{
    PLIST_ENTRY Item;
    ULONG i;

    for (i = 0; i < ItemsPerPage; i++)
    {
        Item = (PLIST_ENTRY)((PUCHAR)&Page->AllocPage + i * ItemSize);
        RemoveEntryList(Item);
    }

    ExFreePool(Page);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCmPrivateAlloc(VOID)
{
    if (CM_KCBS_PER_PAGE < 1)
    {
        CmpAllocInited = FALSE;
        return;
    }

    KeInitializeGuardedMutex(&CmpAllocBucketLock);
    InitializeListHead(&CmpFreeKCBListHead);
    CmpAllocInited = TRUE;
}

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCmPrivateDelayAlloc(VOID)
{
    KeInitializeGuardedMutex(&CmpDelayAllocBucketLock);
    InitializeListHead(&CmpFreeDelayItemsListHead);
}

PCM_KEY_CONTROL_BLOCK
NTAPI
CmpAllocateKeyControlBlock(VOID)
{
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_ALLOC_PAGE Page;
    PLIST_ENTRY Entry;

    if (!CmpAllocInited)
    {
        Kcb = ExAllocatePoolZero(NonPagedPool,
                                 sizeof(CM_KEY_CONTROL_BLOCK),
                                 TAG_KCB);
        if (Kcb != NULL) Kcb->PrivateAlloc = FALSE;
        return Kcb;
    }

    KeAcquireGuardedMutex(&CmpAllocBucketLock);

    if (IsListEmpty(&CmpFreeKCBListHead) &&
        !CmpCarveAllocPage(&CmpFreeKCBListHead,
                           sizeof(CM_KEY_CONTROL_BLOCK),
                           CM_KCBS_PER_PAGE))
    {
        KeReleaseGuardedMutex(&CmpAllocBucketLock);
        return NULL;
    }

    Entry = RemoveHeadList(&CmpFreeKCBListHead);
    Kcb = CONTAINING_RECORD(Entry, CM_KEY_CONTROL_BLOCK, FreeListEntry);

    Page = CmpGetAllocPageFromKcb(Kcb);
    Page->FreeCount--;

    KeReleaseGuardedMutex(&CmpAllocBucketLock);

    RtlZeroMemory(Kcb, sizeof(CM_KEY_CONTROL_BLOCK));
    Kcb->PrivateAlloc = TRUE;
    return Kcb;
}

VOID
NTAPI
CmpFreeKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    PCM_ALLOC_PAGE Page;

    if (!Kcb->PrivateAlloc)
    {
        ExFreePool(Kcb);
        return;
    }

    KeAcquireGuardedMutex(&CmpAllocBucketLock);

    Page = CmpGetAllocPageFromKcb(Kcb);
    InsertTailList(&CmpFreeKCBListHead, &Kcb->FreeListEntry);
    Page->FreeCount++;

    if (Page->FreeCount == CM_KCBS_PER_PAGE)
    {
        CmpReleaseAllocPage(Page,
                            sizeof(CM_KEY_CONTROL_BLOCK),
                            CM_KCBS_PER_PAGE);
    }

    KeReleaseGuardedMutex(&CmpAllocBucketLock);
}

PVOID
NTAPI
CmpAllocateDelayItem(VOID)
{
    PCM_DELAY_ALLOC Item;
    PCM_ALLOC_PAGE Page;
    PLIST_ENTRY Entry;

    KeAcquireGuardedMutex(&CmpDelayAllocBucketLock);

    if (IsListEmpty(&CmpFreeDelayItemsListHead) &&
        !CmpCarveAllocPage(&CmpFreeDelayItemsListHead,
                           sizeof(CM_DELAY_ALLOC),
                           CM_DELAYS_PER_PAGE))
    {
        KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);
        return NULL;
    }

    Entry = RemoveHeadList(&CmpFreeDelayItemsListHead);
    Item = CONTAINING_RECORD(Entry, CM_DELAY_ALLOC, ListEntry);

    Page = CmpGetAllocPageFromDelayAlloc(Item);
    Page->FreeCount--;

    KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);

    RtlZeroMemory(Item, sizeof(CM_DELAY_ALLOC));
    return Item;
}

VOID
NTAPI
CmpFreeDelayItem(
    _In_ PVOID Entry)
{
    PCM_DELAY_ALLOC Item = Entry;
    PCM_ALLOC_PAGE Page;

    KeAcquireGuardedMutex(&CmpDelayAllocBucketLock);

    Page = CmpGetAllocPageFromDelayAlloc(Item);
    InsertTailList(&CmpFreeDelayItemsListHead, &Item->ListEntry);
    Page->FreeCount++;

    if (Page->FreeCount == CM_DELAYS_PER_PAGE)
    {
        CmpReleaseAllocPage(Page,
                            sizeof(CM_DELAY_ALLOC),
                            CM_DELAYS_PER_PAGE);
    }

    KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);
}
