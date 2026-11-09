/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Remembering what lives under a key so a miss costs nothing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

#define CMP_SUBKEY_HINT_LIMIT 256
#define CMP_SUBKEY_NAME_MAX   256

/* PRIVATE FUNCTIONS **********************************************************/

/* The name of one subkey as characters, whichever way the hive stored it */
static
BOOLEAN
CmpReadSubKeyName(
    _In_ PCM_KEY_NODE Node,
    _Out_writes_(CMP_SUBKEY_NAME_MAX) PWCHAR Buffer,
    _Out_ PUNICODE_STRING Name)
{
    USHORT Count;

    Name->Buffer = Buffer;
    Name->Length = 0;
    Name->MaximumLength = CMP_SUBKEY_NAME_MAX * sizeof(WCHAR);

    if (Node->Flags & KEY_COMP_NAME)
    {
        Count = Node->NameLength;
        if (Count > CMP_SUBKEY_NAME_MAX) return FALSE;

        CmpCopyCompressedName(Buffer,
                              (ULONG)(Count * sizeof(WCHAR)),
                              Node->Name,
                              Node->NameLength);
    }
    else
    {
        Count = Node->NameLength / sizeof(WCHAR);
        if (Count > CMP_SUBKEY_NAME_MAX) return FALSE;

        RtlCopyMemory(Buffer, Node->Name, Node->NameLength);
    }

    Name->Length = (USHORT)(Count * sizeof(WCHAR));
    return TRUE;
}

static
BOOLEAN
CmpSubKeyNameHash(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _Out_ PULONG HashKey)
{
    UNICODE_STRING Name;
    WCHAR Buffer[CMP_SUBKEY_NAME_MAX];
    PCM_KEY_NODE Node;
    BOOLEAN Result;

    *HashKey = 0;

    Node = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (Node == NULL) return FALSE;

    Result = CmpReadSubKeyName(Node, Buffer, &Name);
    if (Result) *HashKey = CmpComputeHashKey(0, &Name, FALSE);

    HvReleaseCell(Hive, Cell);
    return Result;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Writes down what lives under a key in whichever of three shapes fits: that
 * nothing does, the one name that does, or the hash of every name that does.
 * None of them can say a key is there when it is not, so a lookup that the
 * cache turns away needs no look at the hive at all.
 */
VOID
NTAPI
CmpBuildSubKeyCache(
    _Inout_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCM_KEY_NODE Node)
{
    PCM_INDEX_HINT_BLOCK Hint;
    HCELL_INDEX Cell;
    ULONG SubKeys;
    ULONG HashKey;
    SIZE_T Size;
    ULONG i;

    Kcb->ExtFlags &= ~(CM_KCB_NO_SUBKEY | CM_KCB_SUBKEY_ONE |
                       CM_KCB_SUBKEY_HINT);

    SubKeys = Node->SubKeyCounts[Stable] + Node->SubKeyCounts[Volatile];

    if (SubKeys == 0)
    {
        Kcb->ExtFlags |= CM_KCB_NO_SUBKEY;
        Kcb->SubKeyCount = 0;
        return;
    }

    if (SubKeys == 1)
    {
        Cell = CmpFindSubKeyByNumber(Kcb->KeyHive, Node, 0);

        if (Cell != HCELL_NIL &&
            CmpSubKeyNameHash(Kcb->KeyHive, Cell, &HashKey))
        {
            Kcb->ExtFlags |= CM_KCB_SUBKEY_ONE;
            Kcb->HashKey = HashKey;
            return;
        }

        Kcb->SubKeyCount = SubKeys;
        return;
    }

    if (SubKeys > CMP_SUBKEY_HINT_LIMIT)
    {
        Kcb->SubKeyCount = SubKeys;
        return;
    }

    Size = FIELD_OFFSET(CM_INDEX_HINT_BLOCK, HashKey) +
           SubKeys * sizeof(ULONG);

    Hint = ExAllocatePoolZero(PagedPool, Size, TAG_CM);
    if (Hint == NULL)
    {
        Kcb->SubKeyCount = SubKeys;
        return;
    }

    Hint->Count = SubKeys;

    for (i = 0; i < SubKeys; i++)
    {
        Cell = CmpFindSubKeyByNumber(Kcb->KeyHive, Node, i);

        if (Cell == HCELL_NIL ||
            !CmpSubKeyNameHash(Kcb->KeyHive, Cell, &Hint->HashKey[i]))
        {
            ExFreePool(Hint);
            Kcb->SubKeyCount = SubKeys;
            return;
        }
    }

    Kcb->ExtFlags |= CM_KCB_SUBKEY_HINT;
    Kcb->IndexHint = Hint;
}

/*
 * Says whether a name could name a subkey of this key. A FALSE answer is
 * final; a TRUE answer still has to be looked up in the hive.
 */
BOOLEAN
NTAPI
CmpSubKeyPossible(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCUNICODE_STRING Name)
{
    PCM_INDEX_HINT_BLOCK Hint;
    ULONG HashKey;
    ULONG i;

    if (Kcb->ExtFlags & CM_KCB_INVALID_CACHED_INFO) return TRUE;
    if (Kcb->ExtFlags & CM_KCB_NO_SUBKEY) return FALSE;

    if (Kcb->ExtFlags & CM_KCB_SUBKEY_ONE)
    {
        HashKey = CmpComputeHashKey(0, Name, FALSE);
        return Kcb->HashKey == HashKey;
    }

    if ((Kcb->ExtFlags & CM_KCB_SUBKEY_HINT) == 0) return TRUE;

    Hint = Kcb->IndexHint;
    if (Hint == NULL) return TRUE;

    HashKey = CmpComputeHashKey(0, Name, FALSE);

    for (i = 0; i < Hint->Count; i++)
    {
        if (Hint->HashKey[i] == HashKey) return TRUE;
    }

    return FALSE;
}

/*
 * Lets go of the hint block, which is the only shape of the subkey cache
 * that owns anything outside the control block.
 */
VOID
NTAPI
CmpReleaseSubKeyCache(
    _Inout_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    if ((Kcb->ExtFlags & CM_KCB_SUBKEY_HINT) == 0) return;

    if (Kcb->IndexHint != NULL) ExFreePool(Kcb->IndexHint);

    Kcb->ExtFlags &= ~CM_KCB_SUBKEY_HINT;
    Kcb->IndexHint = NULL;
}

/* How many subkeys the key has, whichever shape the cache is in */
ULONG
NTAPI
CmpCachedSubKeyCount(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    if (Kcb->ExtFlags & CM_KCB_NO_SUBKEY) return 0;
    if (Kcb->ExtFlags & CM_KCB_SUBKEY_ONE) return 1;

    if (Kcb->ExtFlags & CM_KCB_SUBKEY_HINT)
    {
        return (Kcb->IndexHint != NULL) ? Kcb->IndexHint->Count : 0;
    }

    return Kcb->SubKeyCount;
}
