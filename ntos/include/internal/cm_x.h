/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Configuration manager inline helpers: hashing, cached cells, KCB locks
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COMPUTE_HASH_CHAR(ConvKey, Char) (37 * (ConvKey) + RtlUpcaseUnicodeChar(Char))
#define GET_HASH_INDEX(ConvKey) (((CMP_HASH_IRRATIONAL * (ConvKey)) % CMP_HASH_PRIME) % CmpHashTableSize)
#define GET_HASH_ENTRY(Table, ConvKey) (&(Table)[GET_HASH_INDEX(ConvKey)])

#define CMP_IS_CELL_CACHED(Cell) ((((Cell) & HCELL_CACHED) != 0) && ((Cell) != HCELL_NIL))
#define CMP_GET_CACHED_CELL(Cell) ((ULONG_PTR)((Cell) & ~HCELL_CACHED))
#define CMP_GET_CACHED_DATA(Cell) (&((PCM_CACHED_VALUE_INDEX)CMP_GET_CACHED_CELL(Cell))->Data.CellData)
#define CMP_GET_CACHED_VALUE(Cell) (&((PCM_CACHED_VALUE)CMP_GET_CACHED_CELL(Cell))->KeyValue)

#define CmpGetAllocPageFromKcb(Kcb) ((PCM_ALLOC_PAGE)PAGE_ALIGN(Kcb))
#define CmpGetAllocPageFromDelayAlloc(Alloc) ((PCM_ALLOC_PAGE)PAGE_ALIGN(Alloc))

#define ASSERT_KCB_VALID(Kcb) ASSERT((Kcb)->Signature == CM_KCB_SIGNATURE)
#define CMP_ASSERT_EXCLUSIVE_REGISTRY_LOCK() ASSERT(CmpSpecialBootCondition || CmpTestRegistryLockExclusive())
#define CMP_ASSERT_KCB_LOCK(Kcb) ASSERT(CmpIsKcbLockedExclusive(Kcb) || CmpTestRegistryLockExclusive())

FORCEINLINE
BOOLEAN
CmpIsKcbLockedExclusive(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    return (BOOLEAN)(GET_HASH_ENTRY(CmpCacheTable, Kcb->ConvKey)->Owner == KeGetCurrentThread());
}

FORCEINLINE
VOID
CmpAcquireKcbLockSharedByIndex(
    _In_ ULONG Index)
{
    ExAcquirePushLockShared(&CmpCacheTable[Index].Lock);
}

FORCEINLINE
VOID
CmpAcquireKcbLockExclusiveByIndex(
    _In_ ULONG Index)
{
    ASSERT(CmpCacheTable[Index].Owner != KeGetCurrentThread());

    ExAcquirePushLockExclusive(&CmpCacheTable[Index].Lock);
    CmpCacheTable[Index].Owner = KeGetCurrentThread();
}

FORCEINLINE
VOID
CmpAcquireKcbLockExclusiveByKey(
    _In_ ULONG ConvKey)
{
    CmpAcquireKcbLockExclusiveByIndex(GET_HASH_INDEX(ConvKey));
}

FORCEINLINE
VOID
CmpAcquireKcbLockExclusive(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    CmpAcquireKcbLockExclusiveByKey(Kcb->ConvKey);
}

FORCEINLINE
BOOLEAN
CmpTryToAcquireKcbLockExclusiveByKey(
    _In_ ULONG ConvKey)
{
    PCM_KEY_HASH_TABLE_ENTRY Entry = GET_HASH_ENTRY(CmpCacheTable, ConvKey);

    /* For a caller that already holds a lock ordered after the KCB locks */
    ASSERT(Entry->Owner != KeGetCurrentThread());

    if (!ExTryToAcquirePushLockExclusive(&Entry->Lock))
        return FALSE;

    Entry->Owner = KeGetCurrentThread();
    return TRUE;
}

FORCEINLINE
VOID
CmpReleaseKcbLockByIndex(
    _In_ ULONG Index)
{
    CmpCacheTable[Index].Owner = NULL;
    ExReleasePushLock(&CmpCacheTable[Index].Lock);
}

FORCEINLINE
VOID
CmpReleaseKcbLockByKey(
    _In_ ULONG ConvKey)
{
    CmpReleaseKcbLockByIndex(GET_HASH_INDEX(ConvKey));
}

FORCEINLINE
VOID
CmpReleaseKcbLock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    CmpReleaseKcbLockByKey(Kcb->ConvKey);
}

FORCEINLINE
VOID
CmpAcquireNcbLockExclusiveByKey(
    _In_ ULONG ConvKey)
{
    ExAcquirePushLockExclusive(&GET_HASH_ENTRY(CmpNameCacheTable, ConvKey)->Lock);
}

FORCEINLINE
VOID
CmpReleaseNcbLockByKey(
    _In_ ULONG ConvKey)
{
    ExReleasePushLock(&GET_HASH_ENTRY(CmpNameCacheTable, ConvKey)->Lock);
}
