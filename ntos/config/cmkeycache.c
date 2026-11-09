/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The cache that remembers which keys are open and what they are called
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

PCM_KEY_HASH_TABLE_ENTRY CmpCacheTable = NULL;
PCM_NAME_HASH_TABLE_ENTRY CmpNameCacheTable = NULL;

/* PRIVATE DATA ***************************************************************/

typedef struct _CM_KCB_RENAME
{
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_NAME_CONTROL_BLOCK NewNameBlock;
    PCM_NAME_CONTROL_BLOCK OldNameBlock;
} CM_KCB_RENAME, *PCM_KCB_RENAME;

static UNICODE_STRING CmpRegistryRootName =
    RTL_CONSTANT_STRING(L"\\REGISTRY");

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
CmpHashName(
    _In_ PCUNICODE_STRING Name)
{
    ULONG ConvKey = 0;
    ULONG i;

    for (i = 0; i < Name->Length / sizeof(WCHAR); i++)
    {
        ConvKey = COMPUTE_HASH_CHAR(ConvKey, Name->Buffer[i]);
    }

    return ConvKey;
}

static
BOOLEAN
CmpNameIsCompressible(
    _In_ PCUNICODE_STRING Name)
{
    ULONG i;

    for (i = 0; i < Name->Length / sizeof(WCHAR); i++)
    {
        if (Name->Buffer[i] > 0xFF) return FALSE;
    }

    return TRUE;
}

static
USHORT
CmpNameCharCount(
    _In_ PCM_NAME_CONTROL_BLOCK Ncb)
{
    if (Ncb->Compressed) return Ncb->NameLength;
    return Ncb->NameLength / sizeof(WCHAR);
}

static
BOOLEAN
CmpNameBlockMatches(
    _In_ PCM_NAME_CONTROL_BLOCK Ncb,
    _In_ PCUNICODE_STRING Name)
{
    USHORT Count = Name->Length / sizeof(WCHAR);
    PUCHAR Narrow;
    USHORT i;

    if (CmpNameCharCount(Ncb) != Count) return FALSE;

    if (!Ncb->Compressed)
    {
        for (i = 0; i < Count; i++)
        {
            if (RtlUpcaseUnicodeChar(Ncb->Name[i]) !=
                RtlUpcaseUnicodeChar(Name->Buffer[i]))
            {
                return FALSE;
            }
        }

        return TRUE;
    }

    Narrow = (PUCHAR)Ncb->Name;

    for (i = 0; i < Count; i++)
    {
        if (RtlUpcaseUnicodeChar((WCHAR)Narrow[i]) !=
            RtlUpcaseUnicodeChar(Name->Buffer[i]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
VOID
CmpWriteNameIntoBlock(
    _In_ PCM_NAME_CONTROL_BLOCK Ncb,
    _In_ PCUNICODE_STRING Name)
{
    USHORT Count = Name->Length / sizeof(WCHAR);
    PUCHAR Narrow;
    USHORT i;

    if (!Ncb->Compressed)
    {
        Ncb->NameLength = Name->Length;
        RtlCopyMemory(Ncb->Name, Name->Buffer, Name->Length);
        return;
    }

    Ncb->NameLength = Count;
    Narrow = (PUCHAR)Ncb->Name;

    for (i = 0; i < Count; i++)
    {
        Narrow[i] = (UCHAR)Name->Buffer[i];
    }
}

/* Copies a name block back out as characters, the way a path wants it */
static
USHORT
CmpReadNameFromBlock(
    _In_ PCM_NAME_CONTROL_BLOCK Ncb,
    _Out_writes_(Count) PWCHAR Buffer,
    _In_ USHORT Count)
{
    PUCHAR Narrow;
    USHORT i;

    if (!Ncb->Compressed)
    {
        RtlCopyMemory(Buffer, Ncb->Name, Count * sizeof(WCHAR));
        return Count;
    }

    Narrow = (PUCHAR)Ncb->Name;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = (WCHAR)Narrow[i];
    }

    return Count;
}

static
VOID
CmpRemoveNameHash(
    _In_ PCM_NAME_HASH Hash)
{
    PCM_NAME_HASH *Previous;

    Previous = &GET_HASH_ENTRY(CmpNameCacheTable, Hash->ConvKey)->Entry;

    while (*Previous != NULL)
    {
        if (*Previous == Hash)
        {
            *Previous = Hash->NextHash;
            Hash->NextHash = NULL;
            return;
        }

        Previous = &(*Previous)->NextHash;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeCache(VOID)
{
    SIZE_T KeyTableSize;
    SIZE_T NameTableSize;
    ULONG i;

    KeyTableSize = CmpHashTableSize * sizeof(CM_KEY_HASH_TABLE_ENTRY);
    NameTableSize = CmpHashTableSize * sizeof(CM_NAME_HASH_TABLE_ENTRY);

    CmpCacheTable = ExAllocatePoolZero(NonPagedPool, KeyTableSize, TAG_CM);
    if (CmpCacheTable == NULL) KeBugCheckEx(REGISTRY_ERROR, 10, 1, 0, 0);

    CmpNameCacheTable = ExAllocatePoolZero(NonPagedPool, NameTableSize, TAG_CM);
    if (CmpNameCacheTable == NULL) KeBugCheckEx(REGISTRY_ERROR, 10, 2, 0, 0);

    for (i = 0; i < CmpHashTableSize; i++)
    {
        ExInitializePushLock(&CmpCacheTable[i].Lock);
        CmpCacheTable[i].Owner = NULL;
        CmpCacheTable[i].Entry = NULL;

        ExInitializePushLock(&CmpNameCacheTable[i].Lock);
        CmpNameCacheTable[i].Entry = NULL;
    }
}

/* A descriptor the hive stops caching must not be pointed at any more */
VOID
NTAPI
CmpForgetCachedSecurity(
    _In_ PCM_KEY_SECURITY_CACHE Cached)
{
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_KEY_HASH Hash;
    ULONG i;

    if (CmpCacheTable == NULL) return;

    for (i = 0; i < CmpHashTableSize; i++)
    {
        for (Hash = CmpCacheTable[i].Entry; Hash != NULL; Hash = Hash->NextHash)
        {
            Kcb = CONTAINING_RECORD(Hash, CM_KEY_CONTROL_BLOCK, KeyHash);
            if (Kcb->CachedSecurity == Cached) Kcb->CachedSecurity = NULL;
        }
    }
}

VOID
NTAPI
CmpInsertKeyHash(
    _In_ PCM_KEY_HASH Hash)
{
    PCM_KEY_HASH_TABLE_ENTRY Bucket;

    Bucket = GET_HASH_ENTRY(CmpCacheTable, Hash->ConvKey);
    Hash->NextHash = Bucket->Entry;
    Bucket->Entry = Hash;
}

VOID
NTAPI
CmpRemoveKeyHash(
    _In_ PCM_KEY_HASH Hash)
{
    PCM_KEY_HASH *Previous;

    Previous = &GET_HASH_ENTRY(CmpCacheTable, Hash->ConvKey)->Entry;

    while (*Previous != NULL)
    {
        if (*Previous == Hash)
        {
            *Previous = Hash->NextHash;
            Hash->NextHash = NULL;
            return;
        }

        Previous = &(*Previous)->NextHash;
    }
}

PCM_NAME_CONTROL_BLOCK
NTAPI
CmpGetNameControlBlock(
    _In_ PUNICODE_STRING Name)
{
    PCM_NAME_CONTROL_BLOCK Ncb;
    PCM_NAME_HASH Hash;
    BOOLEAN Compressed;
    ULONG ConvKey;
    SIZE_T Size;

    ConvKey = CmpHashName(Name);
    Compressed = CmpNameIsCompressible(Name);

    CmpAcquireNcbLockExclusiveByKey(ConvKey);

    for (Hash = GET_HASH_ENTRY(CmpNameCacheTable, ConvKey)->Entry;
         Hash != NULL;
         Hash = Hash->NextHash)
    {
        if (Hash->ConvKey != ConvKey) continue;

        Ncb = CONTAINING_RECORD(Hash, CM_NAME_CONTROL_BLOCK, NameHash);
        if (!CmpNameBlockMatches(Ncb, Name)) continue;
        if (Ncb->RefCount == MAXUSHORT) break;

        Ncb->RefCount++;
        CmpReleaseNcbLockByKey(ConvKey);
        return Ncb;
    }

    Size = FIELD_OFFSET(CM_NAME_CONTROL_BLOCK, Name);
    Size += Compressed ? (Name->Length / sizeof(WCHAR)) : Name->Length;

    Ncb = ExAllocatePoolZero(PagedPool, Size, TAG_CM);
    if (Ncb == NULL)
    {
        CmpReleaseNcbLockByKey(ConvKey);
        return NULL;
    }

    Ncb->Compressed = Compressed;
    Ncb->RefCount = 1;
    Ncb->ConvKey = ConvKey;
    CmpWriteNameIntoBlock(Ncb, Name);

    Ncb->NameHash.NextHash =
        (PCM_NAME_HASH)GET_HASH_ENTRY(CmpNameCacheTable, ConvKey)->Entry;
    GET_HASH_ENTRY(CmpNameCacheTable, ConvKey)->Entry = &Ncb->NameHash;

    CmpReleaseNcbLockByKey(ConvKey);
    return Ncb;
}

VOID
NTAPI
CmpDereferenceNameControlBlockWithLock(
    _In_ PCM_NAME_CONTROL_BLOCK Ncb)
{
    ULONG ConvKey = Ncb->ConvKey;

    CmpAcquireNcbLockExclusiveByKey(ConvKey);

    if (Ncb->RefCount != 0) Ncb->RefCount--;

    if (Ncb->RefCount == 0)
    {
        CmpRemoveNameHash(&Ncb->NameHash);
        CmpReleaseNcbLockByKey(ConvKey);
        ExFreePool(Ncb);
        return;
    }

    CmpReleaseNcbLockByKey(ConvKey);
}

VOID
NTAPI
CmpUpdateKcbFromNode(
    _Inout_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCM_KEY_NODE Node)
{
    Kcb->KcbLastWriteTime = Node->LastWriteTime;
    Kcb->KcbMaxNameLen = (USHORT)Node->MaxNameLen;
    Kcb->KcbMaxValueNameLen = (USHORT)Node->MaxValueNameLen;
    Kcb->KcbMaxValueDataLen = Node->MaxValueDataLen;
    Kcb->Flags = Node->Flags;

    if (!CMP_IS_CELL_CACHED(Kcb->ValueCache.ValueList))
    {
        Kcb->ValueCache.Count = Node->ValueList.Count;
        Kcb->ValueCache.ValueList = Node->ValueList.List;
    }

    CmpReleaseSubKeyCache(Kcb);
    Kcb->ExtFlags &= ~CM_KCB_INVALID_CACHED_INFO;
    CmpBuildSubKeyCache(Kcb, Node);
}

VOID
NTAPI
InitializeKCBKeyBodyList(
    _Inout_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    ULONG i;

    InitializeListHead(&Kcb->KeyBodyListHead);

    for (i = 0; i < RTL_NUMBER_OF(Kcb->KeyBodyArray); i++)
    {
        Kcb->KeyBodyArray[i] = NULL;
    }
}

BOOLEAN
NTAPI
CmpReferenceKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    ULONG Old;

    ASSERT_KCB_VALID(Kcb);

    for (;;)
    {
        Old = Kcb->RefCount;
        if (Old == MAXULONG) return FALSE;

        if ((ULONG)InterlockedCompareExchange((PLONG)&Kcb->RefCount,
                                              (LONG)(Old + 1),
                                              (LONG)Old) == Old)
        {
            return TRUE;
        }
    }
}

PCM_KEY_CONTROL_BLOCK
NTAPI
CmpCreateKeyControlBlock(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Index,
    _In_ PCM_KEY_NODE Node,
    _In_ PCM_KEY_CONTROL_BLOCK Parent,
    _In_ ULONG Flags,
    _In_ PUNICODE_STRING KeyName)
{
    BOOLEAN LockHeld = (Flags & CMP_LOCK_HASHES_FOR_KCB) != 0;
    BOOLEAN IsFake = (Flags & CMP_CREATE_FAKE_KCB) != 0;
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_KEY_HASH Hash;
    ULONG ConvKey;

    ConvKey = CmpComputeHashKey(Parent != NULL ? Parent->ConvKey : 0,
                                KeyName,
                                FALSE);

    if (!LockHeld) CmpAcquireKcbLockExclusiveByKey(ConvKey);

    for (Hash = GET_HASH_ENTRY(CmpCacheTable, ConvKey)->Entry;
         Hash != NULL;
         Hash = Hash->NextHash)
    {
        if (Hash->ConvKey != ConvKey) continue;
        if (Hash->KeyHive != Hive) continue;
        if (Hash->KeyCell != Index) continue;

        Kcb = CONTAINING_RECORD(Hash, CM_KEY_CONTROL_BLOCK, KeyHash);
        if (Kcb->Delete) continue;

        if (((Kcb->ExtFlags & CM_KCB_KEY_NON_EXIST) != 0) != IsFake) continue;

        if (!CmpReferenceKeyControlBlock(Kcb)) break;

        CmpRemoveFromDelayedClose(Kcb);

        if (!IsFake && (Kcb->ExtFlags & CM_KCB_INVALID_CACHED_INFO))
        {
            CmpUpdateKcbFromNode(Kcb, Node);
        }

        if (!LockHeld) CmpReleaseKcbLockByKey(ConvKey);
        return Kcb;
    }

    Kcb = CmpAllocateKeyControlBlock();
    if (Kcb == NULL)
    {
        if (!LockHeld) CmpReleaseKcbLockByKey(ConvKey);
        return NULL;
    }

    Kcb->NameBlock = CmpGetNameControlBlock(KeyName);
    if (Kcb->NameBlock == NULL)
    {
        CmpFreeKeyControlBlock(Kcb);
        if (!LockHeld) CmpReleaseKcbLockByKey(ConvKey);
        return NULL;
    }

    if (Parent != NULL && !CmpReferenceKeyControlBlock(Parent))
    {
        CmpDereferenceNameControlBlockWithLock(Kcb->NameBlock);
        CmpFreeKeyControlBlock(Kcb);
        if (!LockHeld) CmpReleaseKcbLockByKey(ConvKey);
        return NULL;
    }

    Kcb->Signature = CM_KCB_SIGNATURE;
    Kcb->RefCount = 1;
    Kcb->ConvKey = ConvKey;
    Kcb->KeyHive = Hive;
    Kcb->KeyCell = Index;
    Kcb->ParentKcb = Parent;
    Kcb->TotalLevels = (Parent != NULL) ? Parent->TotalLevels + 1 : 1;
    Kcb->DelayedCloseIndex = CmpDelayedCloseSize;
    Kcb->InDelayClose = 0;
    InitializeKCBKeyBodyList(Kcb);

    if (IsFake)
    {
        Kcb->ExtFlags |= CM_KCB_KEY_NON_EXIST | CM_KCB_NO_DELAY_CLOSE;
    }
    else
    {
        CmpUpdateKcbFromNode(Kcb, Node);
    }

    CmpInsertKeyHash(&Kcb->KeyHash);

    if (!LockHeld) CmpReleaseKcbLockByKey(ConvKey);
    return Kcb;
}

VOID
NTAPI
CmpRemoveKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    CMP_ASSERT_KCB_LOCK(Kcb);
    CmpRemoveKeyHash(&Kcb->KeyHash);
}

/* Lets go of everything a cached value held on to outside the hive */
VOID
NTAPI
CmpCleanUpKcbValueCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    PCM_CACHED_VALUE_INDEX Index;
    PULONG_PTR List;
    ULONG i;

    if (CMP_IS_CELL_CACHED(Kcb->ValueCache.ValueList))
    {
        Index = (PCM_CACHED_VALUE_INDEX)
            CMP_GET_CACHED_CELL(Kcb->ValueCache.ValueList);
        List = (PULONG_PTR)Index->Data.List;

        for (i = 0; i < Kcb->ValueCache.Count; i++)
        {
            if (CMP_IS_CELL_CACHED(List[i]))
            {
                ExFreePool((PVOID)CMP_GET_CACHED_CELL(List[i]));
            }
        }

        ExFreePool(Index);
    }

    Kcb->ValueCache.ValueList = HCELL_NIL;
    Kcb->ValueCache.Count = 0;
}

VOID
NTAPI
CmpCleanUpSubKeyInfo(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    CMP_ASSERT_KCB_LOCK(Kcb);

    CmpReleaseSubKeyCache(Kcb);

    Kcb->ExtFlags &= ~(CM_KCB_NO_SUBKEY | CM_KCB_SUBKEY_ONE);
    Kcb->SubKeyCount = 0;
    Kcb->ExtFlags |= CM_KCB_INVALID_CACHED_INFO;
}

VOID
NTAPI
CmpCleanUpKcbCacheWithLock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeldExclusively)
{
    PCM_KEY_CONTROL_BLOCK Parent;

    ASSERT_KCB_VALID(Kcb);
    ASSERT(Kcb->RefCount == 0);

    CmpCleanUpKcbValueCache(Kcb);
    CmpReleaseSubKeyCache(Kcb);

    Kcb->CachedSecurity = NULL;

    CmpRemoveKeyHash(&Kcb->KeyHash);

    if (Kcb->NameBlock != NULL)
    {
        CmpDereferenceNameControlBlockWithLock(Kcb->NameBlock);
        Kcb->NameBlock = NULL;
    }

    Parent = Kcb->ParentKcb;
    Kcb->ParentKcb = NULL;
    Kcb->Signature = CM_KCB_INVALID_SIGNATURE;

    CmpFreeKeyControlBlock(Kcb);

    if (Parent != NULL)
    {
        CmpDereferenceKeyControlBlockWithLock(Parent, LockHeldExclusively);
    }
}

VOID
NTAPI
CmpDereferenceKeyControlBlockWithLock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeldExclusively)
{
    ASSERT_KCB_VALID(Kcb);

    if (InterlockedDecrement((PLONG)&Kcb->RefCount) != 0) return;

    if (Kcb->Delete || (Kcb->ExtFlags & CM_KCB_NO_DELAY_CLOSE))
    {
        CmpCleanUpKcbCacheWithLock(Kcb, LockHeldExclusively);
        return;
    }

    CmpAddToDelayedClose(Kcb, LockHeldExclusively);
}

VOID
NTAPI
CmpDereferenceKeyControlBlock(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    ULONG ConvKey = Kcb->ConvKey;
    BOOLEAN Owned;

    ASSERT_KCB_VALID(Kcb);

    Owned = CmpIsKcbLockedExclusive(Kcb) ? TRUE : FALSE;

    if (Owned)
    {
        CmpDereferenceKeyControlBlockWithLock(Kcb, TRUE);
        return;
    }

    CmpAcquireKcbLockExclusiveByKey(ConvKey);
    CmpDereferenceKeyControlBlockWithLock(Kcb, TRUE);
    CmpReleaseKcbLockByKey(ConvKey);
}

/*
 * Walks the chain of parents to spell out the whole path of a key, which is
 * the only place the full name of an open key is still written down.
 */
PUNICODE_STRING
NTAPI
CmpConstructName(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    PCM_KEY_CONTROL_BLOCK Current;
    PUNICODE_STRING Name;
    USHORT Length = 0;
    USHORT Count;
    PWCHAR End;

    for (Current = Kcb; Current != NULL; Current = Current->ParentKcb)
    {
        if (Current->NameBlock == NULL) return NULL;

        if (Current->ParentKcb == NULL)
        {
            Length += CmpRegistryRootName.Length;
            break;
        }

        Length += sizeof(WCHAR);
        Length += CmpNameCharCount(Current->NameBlock) * sizeof(WCHAR);
    }

    Name = ExAllocatePoolZero(PagedPool,
                              sizeof(UNICODE_STRING) + Length,
                              TAG_CM);
    if (Name == NULL) return NULL;

    Name->Buffer = (PWCHAR)(Name + 1);
    Name->Length = Length;
    Name->MaximumLength = Length;

    End = Name->Buffer + Length / sizeof(WCHAR);

    for (Current = Kcb; Current != NULL; Current = Current->ParentKcb)
    {
        if (Current->ParentKcb == NULL)
        {
            End -= CmpRegistryRootName.Length / sizeof(WCHAR);
            RtlCopyMemory(End,
                          CmpRegistryRootName.Buffer,
                          CmpRegistryRootName.Length);
            break;
        }

        Count = CmpNameCharCount(Current->NameBlock);
        End -= Count;
        CmpReadNameFromBlock(Current->NameBlock, End, Count);
        End--;
        *End = OBJ_NAME_PATH_SEPARATOR;
    }

    return Name;
}

/*
 * A key body goes into one of the four slots the control block keeps inline
 * before it costs a list entry, because almost every key is opened once or
 * twice and the slots make closing it a pointer write.
 */
VOID
NTAPI
EnlistKeyBodyWithKCB(
    _In_ PCM_KEY_BODY KeyObject,
    _In_ ULONG Flags)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyObject->KeyControlBlock;
    ULONG i;

    InitializeListHead(&KeyObject->KeyBodyList);

    for (i = 0; i < RTL_NUMBER_OF(Kcb->KeyBodyArray); i++)
    {
        if (InterlockedCompareExchangePointer((PVOID*)&Kcb->KeyBodyArray[i],
                                              KeyObject,
                                              NULL) == NULL)
        {
            return;
        }
    }

    if (Flags & (CMP_ENLIST_KCB_LOCKED_SHARED | CMP_ENLIST_KCB_LOCKED_EXCLUSIVE))
    {
        InsertTailList(&Kcb->KeyBodyListHead, &KeyObject->KeyBodyList);
        return;
    }

    CmpAcquireKcbLockExclusive(Kcb);
    InsertTailList(&Kcb->KeyBodyListHead, &KeyObject->KeyBodyList);
    CmpReleaseKcbLock(Kcb);
}

VOID
NTAPI
DelistKeyBodyFromKCB(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ BOOLEAN LockHeld)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    ULONG i;

    if (Kcb == NULL) return;

    for (i = 0; i < RTL_NUMBER_OF(Kcb->KeyBodyArray); i++)
    {
        if (InterlockedCompareExchangePointer((PVOID*)&Kcb->KeyBodyArray[i],
                                              NULL,
                                              KeyBody) == KeyBody)
        {
            return;
        }
    }

    if (LockHeld)
    {
        RemoveEntryList(&KeyBody->KeyBodyList);
        InitializeListHead(&KeyBody->KeyBodyList);
        return;
    }

    CmpAcquireKcbLockExclusive(Kcb);
    RemoveEntryList(&KeyBody->KeyBodyList);
    InitializeListHead(&KeyBody->KeyBodyList);
    CmpReleaseKcbLock(Kcb);
}

VOID
NTAPI
CmpFlushNotifiesOnKeyBodyList(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ BOOLEAN LockHeld)
{
    PCM_KEY_BODY KeyBody;
    PLIST_ENTRY Entry;
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Kcb->KeyBodyArray); i++)
    {
        KeyBody = Kcb->KeyBodyArray[i];
        if (KeyBody != NULL && KeyBody->NotifyBlock != NULL)
        {
            CmpFlushNotify(KeyBody, LockHeld);
        }
    }

    Entry = Kcb->KeyBodyListHead.Flink;

    while (Entry != &Kcb->KeyBodyListHead)
    {
        KeyBody = CONTAINING_RECORD(Entry, CM_KEY_BODY, KeyBodyList);
        Entry = Entry->Flink;

        if (KeyBody->NotifyBlock != NULL) CmpFlushNotify(KeyBody, LockHeld);
    }
}

/*
 * Locks every hash bucket a path walk will touch, in index order, so two
 * walks meeting in the middle of a path cannot deadlock on each other.
 */
PULONG
NTAPI
CmpBuildAndLockKcbArray(
    _In_ PCM_HASH_CACHE_STACK HashCacheStack,
    _In_ ULONG KcbLockFlags,
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _Inout_ PULONG OuterStackArray,
    _In_ ULONG TotalRemainingSubkeys,
    _In_ ULONG MatchRemainSubkeyLevel)
{
    PULONG Array = OuterStackArray;
    ULONG Count = 0;
    ULONG Index;
    ULONG i;
    ULONG j;

    Array[++Count] = GET_HASH_INDEX(Kcb->ConvKey);

    for (i = MatchRemainSubkeyLevel; i < TotalRemainingSubkeys; i++)
    {
        if (Count >= CMP_KCBS_IN_ARRAY_LIMIT) break;
        Array[++Count] = GET_HASH_INDEX(HashCacheStack[i].ConvKey);
    }

    for (i = 2; i <= Count; i++)
    {
        Index = Array[i];

        for (j = i; j > 1 && Array[j - 1] > Index; j--)
        {
            Array[j] = Array[j - 1];
        }

        Array[j] = Index;
    }

    j = 1;

    for (i = 2; i <= Count; i++)
    {
        if (Array[i] != Array[j]) Array[++j] = Array[i];
    }

    Count = j;
    Array[0] = Count;

    for (i = 1; i <= Count; i++)
    {
        if (KcbLockFlags & CMP_LOCK_KCB_ARRAY_EXCLUSIVE)
        {
            CmpAcquireKcbLockExclusiveByIndex(Array[i]);
        }
        else
        {
            CmpAcquireKcbLockSharedByIndex(Array[i]);
        }
    }

    return Array;
}

VOID
CmpUnLockKcbArray(
    _In_ PULONG LockedKcbs)
{
    ULONG i;

    for (i = LockedKcbs[0]; i >= 1; i--)
    {
        CmpReleaseKcbLockByIndex(LockedKcbs[i]);
    }

    LockedKcbs[0] = 0;
}

VOID
NTAPI
CmpAcquireTwoKcbLocksExclusiveByKey(
    _In_ ULONG ConvKey1,
    _In_ ULONG ConvKey2)
{
    ULONG Index1 = GET_HASH_INDEX(ConvKey1);
    ULONG Index2 = GET_HASH_INDEX(ConvKey2);

    if (Index1 == Index2)
    {
        CmpAcquireKcbLockExclusiveByIndex(Index1);
        return;
    }

    if (Index1 < Index2)
    {
        CmpAcquireKcbLockExclusiveByIndex(Index1);
        CmpAcquireKcbLockExclusiveByIndex(Index2);
        return;
    }

    CmpAcquireKcbLockExclusiveByIndex(Index2);
    CmpAcquireKcbLockExclusiveByIndex(Index1);
}

VOID
NTAPI
CmpReleaseTwoKcbLockByKey(
    _In_ ULONG ConvKey1,
    _In_ ULONG ConvKey2)
{
    ULONG Index1 = GET_HASH_INDEX(ConvKey1);
    ULONG Index2 = GET_HASH_INDEX(ConvKey2);

    CmpReleaseKcbLockByIndex(Index1);
    if (Index1 != Index2) CmpReleaseKcbLockByIndex(Index2);
}

/*
 * A rename takes the name block it needs before it changes anything, so the
 * part that cannot be undone never has to allocate.
 */
PVOID
NTAPI
CmpPrepareKeyControlBlockRename(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING NewName)
{
    PCM_KCB_RENAME Rename;

    Rename = ExAllocatePoolZero(PagedPool, sizeof(CM_KCB_RENAME), TAG_CM);
    if (Rename == NULL) return NULL;

    Rename->NewNameBlock = CmpGetNameControlBlock(NewName);
    if (Rename->NewNameBlock == NULL)
    {
        ExFreePool(Rename);
        return NULL;
    }

    Rename->Kcb = Kcb;
    Rename->OldNameBlock = Kcb->NameBlock;
    return Rename;
}

/* Every key below a renamed one is known by a hash of its path, so the whole
   subtree has to be hashed again under the new name */
static
VOID
CmpRehashSubtree(
    _In_ PCM_KEY_CONTROL_BLOCK Root)
{
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_KEY_CONTROL_BLOCK Ancestor;
    PCM_KEY_HASH Hash;
    PCM_KEY_HASH Next;
    UNICODE_STRING Name;
    WCHAR Buffer[256];
    ULONG Levels;
    ULONG Index;
    ULONG i;

    CMP_ASSERT_EXCLUSIVE_REGISTRY_LOCK();

    for (Levels = Root->TotalLevels + 1; Levels <= CMP_SUBKEY_LEVELS_DEPTH_LIMIT; Levels++)
    {
        for (i = 0; i < CmpHashTableSize; i++)
        {
            for (Hash = CmpCacheTable[i].Entry; Hash != NULL; Hash = Next)
            {
                Next = Hash->NextHash;
                Kcb = CONTAINING_RECORD(Hash, CM_KEY_CONTROL_BLOCK, KeyHash);

                if (Kcb->TotalLevels != Levels) continue;
                if (Kcb->NameBlock == NULL) continue;

                for (Ancestor = Kcb->ParentKcb;
                     Ancestor != NULL && Ancestor != Root;
                     Ancestor = Ancestor->ParentKcb)
                {
                    NOTHING;
                }

                if (Ancestor != Root) continue;

                Index = CmpNameCharCount(Kcb->NameBlock);
                if (Index > RTL_NUMBER_OF(Buffer)) continue;

                CmpReadNameFromBlock(Kcb->NameBlock, Buffer, (USHORT)Index);
                Name.Buffer = Buffer;
                Name.Length = (USHORT)(Index * sizeof(WCHAR));
                Name.MaximumLength = Name.Length;

                CmpRemoveKeyHash(&Kcb->KeyHash);
                Kcb->ConvKey = CmpComputeHashKey(Kcb->ParentKcb->ConvKey,
                                                 &Name,
                                                 FALSE);
                CmpInsertKeyHash(&Kcb->KeyHash);
            }
        }
    }
}

VOID
NTAPI
CmpCommitKeyControlBlockRename(
    _In_ PVOID RenameContext,
    _In_ HCELL_INDEX NewCell)
{
    PCM_KCB_RENAME Rename = RenameContext;
    PCM_KEY_CONTROL_BLOCK Kcb = Rename->Kcb;
    UNICODE_STRING Name;
    WCHAR Buffer[256];
    USHORT Count;

    CmpRemoveKeyHash(&Kcb->KeyHash);

    Kcb->NameBlock = Rename->NewNameBlock;
    Kcb->KeyCell = NewCell;

    Count = CmpNameCharCount(Kcb->NameBlock);

    if (Kcb->ParentKcb != NULL && Count <= RTL_NUMBER_OF(Buffer))
    {
        CmpReadNameFromBlock(Kcb->NameBlock, Buffer, Count);
        Name.Buffer = Buffer;
        Name.Length = (USHORT)(Count * sizeof(WCHAR));
        Name.MaximumLength = Name.Length;

        Kcb->ConvKey = CmpComputeHashKey(Kcb->ParentKcb->ConvKey,
                                         &Name,
                                         FALSE);
    }

    CmpInsertKeyHash(&Kcb->KeyHash);
    CmpRehashSubtree(Kcb);

    if (Rename->OldNameBlock != NULL)
    {
        CmpDereferenceNameControlBlockWithLock(Rename->OldNameBlock);
    }

    ExFreePool(Rename);
}

VOID
NTAPI
CmpCancelKeyControlBlockRename(
    _In_ PVOID RenameContext)
{
    PCM_KCB_RENAME Rename = RenameContext;

    if (Rename->NewNameBlock != NULL)
    {
        CmpDereferenceNameControlBlockWithLock(Rename->NewNameBlock);
    }

    ExFreePool(Rename);
}
