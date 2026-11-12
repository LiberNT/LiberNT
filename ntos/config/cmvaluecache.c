/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Finding a value under a key and spelling it out for the caller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE FUNCTIONS **********************************************************/

static
USHORT
CmpValueNameSize(
    _In_ PCM_KEY_VALUE Value)
{
    if (Value->Flags & VALUE_COMP_NAME)
    {
        return (USHORT)(Value->NameLength * sizeof(WCHAR));
    }

    return Value->NameLength;
}

static
VOID
CmpCopyValueName(
    _In_ PCM_KEY_VALUE Value,
    _Out_writes_bytes_(Size) PWCHAR Buffer,
    _In_ USHORT Size)
{
    if (Value->Flags & VALUE_COMP_NAME)
    {
        CmpCopyCompressedName(Buffer, Size, Value->Name, Value->NameLength);
        return;
    }

    RtlCopyMemory(Buffer, Value->Name, Size);
}

/*
 * Walks the value list of a key looking for one name. The list is read out of
 * the hive each time: nothing is promoted into the control block's cache.
 */
static
HCELL_INDEX
CmpLookupValueCell(
    _In_ PHHIVE Hive,
    _In_ PCELL_DATA ValueList,
    _In_ ULONG Count,
    _In_ PCUNICODE_STRING Name,
    _Out_ PULONG Index)
{
    PCM_KEY_VALUE Value;
    UNICODE_STRING Found;
    HCELL_INDEX Cell;
    ULONG i;

    *Index = 0;

    for (i = 0; i < Count; i++)
    {
        Cell = ValueList->u.KeyList[i];
        if (Cell == HCELL_NIL) continue;

        Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
        if (Value == NULL) continue;

        if (Value->Flags & VALUE_COMP_NAME)
        {
            if (CmpCompareCompressedName(Name,
                                         Value->Name,
                                         Value->NameLength) == 0)
            {
                HvReleaseCell(Hive, Cell);
                *Index = i;
                return Cell;
            }
        }
        else
        {
            Found.Buffer = Value->Name;
            Found.Length = Value->NameLength;
            Found.MaximumLength = Value->NameLength;

            if (RtlCompareUnicodeString(Name, &Found, TRUE) == 0)
            {
                HvReleaseCell(Hive, Cell);
                *Index = i;
                return Cell;
            }
        }

        HvReleaseCell(Hive, Cell);
    }

    return HCELL_NIL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpGetValueListFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _Out_ PCELL_DATA *CellData,
    _Out_ BOOLEAN *IndexIsCached,
    _Out_ PHCELL_INDEX ValueListToRelease)
{
    PHHIVE Hive = Kcb->KeyHive;
    HCELL_INDEX List = Kcb->ValueCache.ValueList;

    *CellData = NULL;
    *IndexIsCached = FALSE;
    *ValueListToRelease = HCELL_NIL;

    if (Kcb->ValueCache.Count == 0) return SearchSuccess;

    /* FIXME: a promoted value list cannot be held: CACHED_CHILD_LIST.ValueList
       is a ULONG, while Windows 11 keeps a pointer there (KCB offset 96, 8
       bytes, sizeof 312), so the list is read from the hive every time */
    if (CMP_IS_CELL_CACHED(List))
    {
        *CellData = CMP_GET_CACHED_DATA(List);
        *IndexIsCached = TRUE;
        return SearchSuccess;
    }

    if (List == HCELL_NIL) return SearchFail;

    *CellData = (PCELL_DATA)HvGetCell(Hive, List);
    if (*CellData == NULL) return SearchFail;

    *ValueListToRelease = List;
    return SearchSuccess;
}

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpGetValueKeyFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCELL_DATA CellData,
    _In_ ULONG Index,
    _Out_ PCM_CACHED_VALUE **CachedValue,
    _Out_ PCM_KEY_VALUE *Value,
    _In_ BOOLEAN IndexIsCached,
    _Out_ BOOLEAN *ValueIsCached,
    _Out_ PHCELL_INDEX CellToRelease)
{
    PHHIVE Hive = Kcb->KeyHive;
    HCELL_INDEX Cell;

    *CachedValue = NULL;
    *Value = NULL;
    *ValueIsCached = FALSE;
    *CellToRelease = HCELL_NIL;

    if (Index >= Kcb->ValueCache.Count) return SearchFail;
    if (CellData == NULL) return SearchFail;

    if (IndexIsCached)
    {
        ULONG_PTR Entry = ((PULONG_PTR)CellData)[Index];

        if (CMP_IS_CELL_CACHED(Entry))
        {
            *Value = CMP_GET_CACHED_VALUE(Entry);
            *ValueIsCached = TRUE;
            return SearchSuccess;
        }

        Cell = (HCELL_INDEX)Entry;
    }
    else
    {
        Cell = CellData->u.KeyList[Index];
    }

    if (Cell == HCELL_NIL) return SearchFail;

    *Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
    if (*Value == NULL) return SearchFail;

    *CellToRelease = Cell;
    return SearchSuccess;
}

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpFindValueByNameFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCUNICODE_STRING Name,
    _Out_ PCM_CACHED_VALUE **CachedValue,
    _Out_ ULONG *Index,
    _Out_ PCM_KEY_VALUE *Value,
    _Out_ BOOLEAN *ValueIsCached,
    _Out_ PHCELL_INDEX CellToRelease)
{
    PHHIVE Hive = Kcb->KeyHive;
    VALUE_SEARCH_RETURN_TYPE Result;
    HCELL_INDEX ListToRelease;
    HCELL_INDEX Cell;
    PCELL_DATA List;
    BOOLEAN ListCached;

    *CachedValue = NULL;
    *Index = 0;
    *Value = NULL;
    *ValueIsCached = FALSE;
    *CellToRelease = HCELL_NIL;

    Result = CmpGetValueListFromCache(Kcb, &List, &ListCached, &ListToRelease);
    if (Result != SearchSuccess) return Result;

    if (List == NULL)
    {
        if (ListToRelease != HCELL_NIL) HvReleaseCell(Hive, ListToRelease);
        return SearchFail;
    }

    Cell = CmpLookupValueCell(Hive, List, Kcb->ValueCache.Count, Name, Index);

    if (ListToRelease != HCELL_NIL) HvReleaseCell(Hive, ListToRelease);

    if (Cell == HCELL_NIL) return SearchFail;

    *Value = (PCM_KEY_VALUE)HvGetCell(Hive, Cell);
    if (*Value == NULL) return SearchFail;

    *CellToRelease = Cell;
    return SearchSuccess;
}

VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpGetValueDataFromCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCM_CACHED_VALUE *CachedValue,
    _In_ PCELL_DATA ValueKey,
    _In_ BOOLEAN ValueIsCached,
    _Out_ PVOID *DataPointer,
    _Out_ PBOOLEAN Allocated,
    _Out_ PHCELL_INDEX CellToRelease)
{
    PCM_KEY_VALUE Value = (PCM_KEY_VALUE)ValueKey;
    ULONG Length;

    UNREFERENCED_PARAMETER(CachedValue);
    UNREFERENCED_PARAMETER(ValueIsCached);

    *DataPointer = NULL;
    *Allocated = FALSE;
    *CellToRelease = HCELL_NIL;

    if (!CmpReadValueData(Kcb->KeyHive,
                          Value,
                          &Length,
                          DataPointer,
                          Allocated,
                          CellToRelease))
    {
        return SearchFail;
    }

    return SearchSuccess;
}

/*
 * Writes one value out in the shape the information class asks for. A buffer
 * too small for the fixed part gets nothing and STATUS_BUFFER_TOO_SMALL; one
 * that holds the fixed part but not the whole answer is filled as far as it
 * goes and told STATUS_BUFFER_OVERFLOW.
 */
VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpQueryKeyValueData(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCM_CACHED_VALUE *CachedValue,
    _In_ PCM_KEY_VALUE ValueKey,
    _In_ BOOLEAN ValueIsCached,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength,
    _Out_ PNTSTATUS Status)
{
    HCELL_INDEX CellToRelease = HCELL_NIL;
    BOOLEAN Allocated = FALSE;
    PVOID Data = NULL;
    ULONG DataLength;
    ULONG NameSize;
    ULONG HeaderSize;
    ULONG DataOffset;
    ULONG Required;
    ULONG Copied;
    ULONG Align;

    NameSize = CmpValueNameSize(ValueKey);
    CmpIsKeyValueSmall(&DataLength, ValueKey->DataLength);

    *Status = STATUS_SUCCESS;
    *ResultLength = 0;

    switch (KeyValueInformationClass)
    {
        case KeyValueBasicInformation:
        {
            PKEY_VALUE_BASIC_INFORMATION Answer = KeyValueInformation;

            HeaderSize = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name);
            Required = HeaderSize + NameSize;
            *ResultLength = Required;

            if (Length < HeaderSize)
            {
                *Status = STATUS_BUFFER_TOO_SMALL;
                return SearchSuccess;
            }

            Answer->TitleIndex = 0;
            Answer->Type = ValueKey->Type;
            Answer->NameLength = NameSize;

            Copied = min(NameSize, Length - HeaderSize);
            if (Copied != 0) CmpCopyValueName(ValueKey, Answer->Name, (USHORT)Copied);

            if (Length < Required) *Status = STATUS_BUFFER_OVERFLOW;
            return SearchSuccess;
        }

        case KeyValueFullInformation:
        case KeyValueFullInformationAlign64:
        {
            PKEY_VALUE_FULL_INFORMATION Answer = KeyValueInformation;

            Align = (KeyValueInformationClass == KeyValueFullInformationAlign64)
                        ? sizeof(ULONGLONG) : sizeof(ULONG);

            HeaderSize = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name);
            DataOffset = ALIGN_UP_BY(HeaderSize + NameSize, Align);
            Required = DataOffset + DataLength;
            *ResultLength = Required;

            if (Length < HeaderSize)
            {
                *Status = STATUS_BUFFER_TOO_SMALL;
                return SearchSuccess;
            }

            Answer->TitleIndex = 0;
            Answer->Type = ValueKey->Type;
            Answer->NameLength = NameSize;
            Answer->DataOffset = DataOffset;
            Answer->DataLength = DataLength;

            Copied = min(NameSize, Length - HeaderSize);
            if (Copied != 0) CmpCopyValueName(ValueKey, Answer->Name, (USHORT)Copied);

            if (Length < Required)
            {
                *Status = STATUS_BUFFER_OVERFLOW;
                if (Length <= DataOffset) return SearchSuccess;
            }

            if (DataLength == 0) return SearchSuccess;

            if (CmpGetValueDataFromCache(Kcb,
                                         CachedValue,
                                         (PCELL_DATA)ValueKey,
                                         ValueIsCached,
                                         &Data,
                                         &Allocated,
                                         &CellToRelease) != SearchSuccess)
            {
                *Status = STATUS_INSUFFICIENT_RESOURCES;
                return SearchSuccess;
            }

            Copied = min(DataLength, Length - DataOffset);
            RtlCopyMemory((PUCHAR)KeyValueInformation + DataOffset,
                          Data,
                          Copied);
            break;
        }

        case KeyValuePartialInformation:
        {
            PKEY_VALUE_PARTIAL_INFORMATION Answer = KeyValueInformation;

            HeaderSize = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
            Required = HeaderSize + DataLength;
            *ResultLength = Required;

            if (Length < HeaderSize)
            {
                *Status = STATUS_BUFFER_TOO_SMALL;
                return SearchSuccess;
            }

            Answer->TitleIndex = 0;
            Answer->Type = ValueKey->Type;
            Answer->DataLength = DataLength;

            if (Length < Required) *Status = STATUS_BUFFER_OVERFLOW;
            if (DataLength == 0) return SearchSuccess;

            if (CmpGetValueDataFromCache(Kcb,
                                         CachedValue,
                                         (PCELL_DATA)ValueKey,
                                         ValueIsCached,
                                         &Data,
                                         &Allocated,
                                         &CellToRelease) != SearchSuccess)
            {
                *Status = STATUS_INSUFFICIENT_RESOURCES;
                return SearchSuccess;
            }

            Copied = min(DataLength, Length - HeaderSize);
            RtlCopyMemory(Answer->Data, Data, Copied);
            break;
        }

        case KeyValuePartialInformationAlign64:
        {
            PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64 Answer = KeyValueInformation;

            HeaderSize = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data);
            Required = HeaderSize + DataLength;
            *ResultLength = Required;

            if (Length < HeaderSize)
            {
                *Status = STATUS_BUFFER_TOO_SMALL;
                return SearchSuccess;
            }

            Answer->Type = ValueKey->Type;
            Answer->DataLength = DataLength;

            if (Length < Required) *Status = STATUS_BUFFER_OVERFLOW;
            if (DataLength == 0) return SearchSuccess;

            if (CmpGetValueDataFromCache(Kcb,
                                         CachedValue,
                                         (PCELL_DATA)ValueKey,
                                         ValueIsCached,
                                         &Data,
                                         &Allocated,
                                         &CellToRelease) != SearchSuccess)
            {
                *Status = STATUS_INSUFFICIENT_RESOURCES;
                return SearchSuccess;
            }

            Copied = min(DataLength, Length - HeaderSize);
            RtlCopyMemory(Answer->Data, Data, Copied);
            break;
        }

        default:
            *Status = STATUS_INVALID_PARAMETER;
            return SearchSuccess;
    }

    if (Allocated) ExFreePool(Data);
    if (CellToRelease != HCELL_NIL) HvReleaseCell(Kcb->KeyHive, CellToRelease);

    return SearchSuccess;
}

/*
 * Says whether writing this value would change anything, because a write that
 * changes nothing must not wake a change notification.
 */
VALUE_SEARCH_RETURN_TYPE
NTAPI
CmpCompareNewValueDataAgainstKCBCache(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PUNICODE_STRING ValueName,
    _In_ ULONG Type,
    _In_ PVOID Data,
    _In_ ULONG DataSize)
{
    VALUE_SEARCH_RETURN_TYPE Result = SearchFail;
    HCELL_INDEX CellToRelease = HCELL_NIL;
    HCELL_INDEX DataToRelease = HCELL_NIL;
    PCM_CACHED_VALUE *CachedValue;
    BOOLEAN ValueIsCached;
    BOOLEAN Allocated = FALSE;
    PCM_KEY_VALUE Value;
    PVOID OldData = NULL;
    ULONG OldLength;
    ULONG Index;

    if (CmpFindValueByNameFromCache(Kcb,
                                    ValueName,
                                    &CachedValue,
                                    &Index,
                                    &Value,
                                    &ValueIsCached,
                                    &CellToRelease) != SearchSuccess)
    {
        return SearchFail;
    }

    CmpIsKeyValueSmall(&OldLength, Value->DataLength);

    if (Value->Type == Type && OldLength == DataSize)
    {
        if (DataSize == 0)
        {
            Result = SearchSuccess;
        }
        else if (CmpGetValueDataFromCache(Kcb,
                                          CachedValue,
                                          (PCELL_DATA)Value,
                                          ValueIsCached,
                                          &OldData,
                                          &Allocated,
                                          &DataToRelease) == SearchSuccess)
        {
            if (RtlCompareMemory(OldData, Data, DataSize) == DataSize)
            {
                Result = SearchSuccess;
            }
        }
    }

    if (Allocated) ExFreePool(OldData);
    if (DataToRelease != HCELL_NIL) HvReleaseCell(Kcb->KeyHive, DataToRelease);
    if (CellToRelease != HCELL_NIL) HvReleaseCell(Kcb->KeyHive, CellToRelease);

    return Result;
}
