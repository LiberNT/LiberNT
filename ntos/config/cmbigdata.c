/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Values too large for one cell, broken across as many as they need
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
CmpBigDataSegmentCount(
    _In_ ULONG DataSize)
{
    return (DataSize + CM_KEY_VALUE_BIG - 1) / CM_KEY_VALUE_BIG;
}

static
PHCELL_INDEX
CmpMapBigDataList(
    _In_ PHHIVE Hive,
    _In_ PCM_BIG_DATA Big)
{
    PCELL_DATA List;

    if (Big->List == HCELL_NIL) return NULL;

    List = (PCELL_DATA)HvGetCell(Hive, Big->List);
    if (List == NULL) return NULL;

    return (PHCELL_INDEX)&List->u.KeyList[0];
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * A value of more than one cell's worth is kept as a list of pieces. This
 * walks the list and puts the pieces back together in one buffer, which is
 * the only form the caller can be given.
 */
BOOLEAN
NTAPI
CmpGetBigValueData(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_VALUE Value,
    _In_ ULONG Length,
    _Out_ PVOID *Buffer,
    _Out_ PBOOLEAN Allocated)
{
    PHCELL_INDEX Segments;
    PCM_BIG_DATA Big;
    PCELL_DATA Piece;
    ULONG Remaining;
    ULONG Offset;
    ULONG Size;
    ULONG i;
    PUCHAR Room;

    *Buffer = NULL;
    *Allocated = FALSE;

    if (Value->Data == HCELL_NIL) return FALSE;

    Big = (PCM_BIG_DATA)HvGetCell(Hive, Value->Data);
    if (Big == NULL) return FALSE;

    if (Big->Signature != CM_BIG_DATA_SIGNATURE || Big->Count == 0)
    {
        HvReleaseCell(Hive, Value->Data);
        return FALSE;
    }

    Segments = CmpMapBigDataList(Hive, Big);
    if (Segments == NULL)
    {
        HvReleaseCell(Hive, Value->Data);
        return FALSE;
    }

    Room = ExAllocatePoolZero(PagedPool, Length, TAG_CM);
    if (Room == NULL)
    {
        HvReleaseCell(Hive, Big->List);
        HvReleaseCell(Hive, Value->Data);
        return FALSE;
    }

    Remaining = Length;
    Offset = 0;

    for (i = 0; i < Big->Count && Remaining != 0; i++)
    {
        if (Segments[i] == HCELL_NIL) break;

        Piece = (PCELL_DATA)HvGetCell(Hive, Segments[i]);
        if (Piece == NULL) break;

        Size = (Remaining > CM_KEY_VALUE_BIG) ? CM_KEY_VALUE_BIG : Remaining;
        RtlCopyMemory(Room + Offset, Piece, Size);
        HvReleaseCell(Hive, Segments[i]);

        Offset += Size;
        Remaining -= Size;
    }

    HvReleaseCell(Hive, Big->List);
    HvReleaseCell(Hive, Value->Data);

    if (Remaining != 0)
    {
        ExFreePool(Room);
        return FALSE;
    }

    *Buffer = Room;
    *Allocated = TRUE;
    return TRUE;
}

VOID
NTAPI
CmpFreeBigValueData(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX BigCell)
{
    PHCELL_INDEX Segments;
    PCM_BIG_DATA Big;
    HCELL_INDEX List;
    ULONG Count;
    ULONG i;

    if (BigCell == HCELL_NIL) return;

    Big = (PCM_BIG_DATA)HvGetCell(Hive, BigCell);
    if (Big == NULL) return;

    if (Big->Signature != CM_BIG_DATA_SIGNATURE)
    {
        HvReleaseCell(Hive, BigCell);
        return;
    }

    Count = Big->Count;
    List = Big->List;

    Segments = CmpMapBigDataList(Hive, Big);

    if (Segments != NULL)
    {
        for (i = 0; i < Count; i++)
        {
            if (Segments[i] == HCELL_NIL) continue;
            HvFreeCell(Hive, Segments[i]);
        }

        HvReleaseCell(Hive, List);
    }

    HvReleaseCell(Hive, BigCell);

    if (List != HCELL_NIL) HvFreeCell(Hive, List);
    HvFreeCell(Hive, BigCell);
}

BOOLEAN
NTAPI
CmpMarkBigValueDataDirty(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX BigCell)
{
    PHCELL_INDEX Segments;
    PCM_BIG_DATA Big;
    BOOLEAN Result = TRUE;
    ULONG Count;
    ULONG i;

    if (BigCell == HCELL_NIL) return TRUE;

    if (!HvMarkCellDirty(Hive, BigCell, FALSE)) return FALSE;

    Big = (PCM_BIG_DATA)HvGetCell(Hive, BigCell);
    if (Big == NULL) return FALSE;

    Count = Big->Count;

    if (Big->List != HCELL_NIL &&
        !HvMarkCellDirty(Hive, Big->List, FALSE))
    {
        HvReleaseCell(Hive, BigCell);
        return FALSE;
    }

    Segments = CmpMapBigDataList(Hive, Big);

    if (Segments != NULL)
    {
        for (i = 0; i < Count; i++)
        {
            if (Segments[i] == HCELL_NIL) continue;
            if (!HvMarkCellDirty(Hive, Segments[i], FALSE)) Result = FALSE;
        }

        HvReleaseCell(Hive, Big->List);
    }

    HvReleaseCell(Hive, BigCell);
    return Result;
}

/*
 * Writes a value too large for one cell: one cell holds the count, one holds
 * the list of pieces, and each piece holds as much as a cell can take.
 */
NTSTATUS
NTAPI
CmpSetBigValueData(
    _In_ PHHIVE Hive,
    _In_ HSTORAGE_TYPE StorageType,
    _In_ HCELL_INDEX ValueCell,
    _In_reads_bytes_(DataSize) PVOID Data,
    _In_ ULONG DataSize,
    _Out_ PHCELL_INDEX BigCell)
{
    PHCELL_INDEX Segments;
    PCM_BIG_DATA Big;
    PCELL_DATA Piece;
    HCELL_INDEX ListCell;
    HCELL_INDEX Cell;
    ULONG Remaining;
    ULONG Offset;
    ULONG Count;
    ULONG Size;
    ULONG i;

    *BigCell = HCELL_NIL;

    Count = CmpBigDataSegmentCount(DataSize);
    if (Count == 0 || Count > MAXUSHORT) return STATUS_INVALID_PARAMETER;

    Cell = HvAllocateCell(Hive,
                          sizeof(CM_BIG_DATA),
                          StorageType,
                          ValueCell);
    if (Cell == HCELL_NIL) return STATUS_INSUFFICIENT_RESOURCES;

    ListCell = HvAllocateCell(Hive,
                              Count * (ULONG)sizeof(HCELL_INDEX),
                              StorageType,
                              Cell);
    if (ListCell == HCELL_NIL)
    {
        HvFreeCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Big = (PCM_BIG_DATA)HvGetCell(Hive, Cell);
    if (Big == NULL)
    {
        HvFreeCell(Hive, ListCell);
        HvFreeCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Big->Signature = CM_BIG_DATA_SIGNATURE;
    Big->Count = (USHORT)Count;
    Big->List = ListCell;

    Segments = CmpMapBigDataList(Hive, Big);
    if (Segments == NULL)
    {
        HvReleaseCell(Hive, Cell);
        HvFreeCell(Hive, ListCell);
        HvFreeCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < Count; i++) Segments[i] = HCELL_NIL;

    Remaining = DataSize;
    Offset = 0;

    for (i = 0; i < Count; i++)
    {
        Size = (Remaining > CM_KEY_VALUE_BIG) ? CM_KEY_VALUE_BIG : Remaining;

        Segments[i] = HvAllocateCell(Hive, Size, StorageType, ListCell);
        if (Segments[i] == HCELL_NIL) break;

        Piece = (PCELL_DATA)HvGetCell(Hive, Segments[i]);
        if (Piece == NULL)
        {
            HvFreeCell(Hive, Segments[i]);
            Segments[i] = HCELL_NIL;
            break;
        }

        RtlCopyMemory(Piece, (PUCHAR)Data + Offset, Size);
        HvReleaseCell(Hive, Segments[i]);

        Offset += Size;
        Remaining -= Size;
    }

    if (Remaining != 0)
    {
        for (i = 0; i < Count; i++)
        {
            if (Segments[i] == HCELL_NIL) continue;
            HvFreeCell(Hive, Segments[i]);
        }

        HvReleaseCell(Hive, ListCell);
        HvReleaseCell(Hive, Cell);
        HvFreeCell(Hive, ListCell);
        HvFreeCell(Hive, Cell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    HvReleaseCell(Hive, ListCell);
    HvReleaseCell(Hive, Cell);

    HvMarkCellDirty(Hive, Cell, FALSE);
    HvMarkCellDirty(Hive, ListCell, FALSE);

    *BigCell = Cell;
    return STATUS_SUCCESS;
}

/*
 * Lets go of whatever a value's data was held in, whichever of the three
 * shapes it was in, and leaves the value saying it holds nothing.
 */
VOID
NTAPI
CmpReleaseValueData(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX ValueCell)
{
    PCM_KEY_VALUE Value;
    ULONG Length;

    if (ValueCell == HCELL_NIL) return;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, ValueCell);
    if (Value == NULL) return;

    if (CmpIsKeyValueSmall(&Length, Value->DataLength) || Length == 0)
    {
        Value->DataLength = CM_KEY_VALUE_SPECIAL_SIZE;
        Value->Data = HCELL_NIL;
        HvReleaseCell(Hive, ValueCell);
        return;
    }

    if (CmpIsKeyValueBig(Hive, Value->DataLength))
    {
        CmpFreeBigValueData(Hive, Value->Data);
    }
    else if (Value->Data != HCELL_NIL)
    {
        CmpFreeValueData(Hive, Value->Data, Length);
    }

    Value->DataLength = CM_KEY_VALUE_SPECIAL_SIZE;
    Value->Data = HCELL_NIL;
    HvReleaseCell(Hive, ValueCell);
}

/*
 * The one place that decides which of the three shapes a value's data takes:
 * inside the value cell, in a cell of its own, or broken into pieces.
 */
NTSTATUS
NTAPI
CmpWriteValueData(
    _In_ PHHIVE Hive,
    _In_ HSTORAGE_TYPE StorageType,
    _In_ HCELL_INDEX ValueCell,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize,
    _In_ ULONG SmallData)
{
    PCM_KEY_VALUE Value;
    HCELL_INDEX DataCell;
    NTSTATUS Status;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, ValueCell);
    if (Value == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (DataSize <= CM_KEY_VALUE_SMALL)
    {
        Value->Type = Type;
        Value->DataLength = DataSize | CM_KEY_VALUE_SPECIAL_SIZE;
        Value->Data = SmallData;
        HvReleaseCell(Hive, ValueCell);
        return STATUS_SUCCESS;
    }

    HvReleaseCell(Hive, ValueCell);

    if (DataSize > CM_KEY_VALUE_BIG &&
        Hive->Version >= HSYS_WHISTLER_BETA1)
    {
        Status = CmpSetBigValueData(Hive,
                                    StorageType,
                                    ValueCell,
                                    Data,
                                    DataSize,
                                    &DataCell);
    }
    else
    {
        Status = CmpSetValueDataNew(Hive,
                                    Data,
                                    DataSize,
                                    StorageType,
                                    ValueCell,
                                    &DataCell);
    }

    if (!NT_SUCCESS(Status)) return Status;

    Value = (PCM_KEY_VALUE)HvGetCell(Hive, ValueCell);
    if (Value == NULL)
    {
        if (DataSize > CM_KEY_VALUE_BIG &&
            Hive->Version >= HSYS_WHISTLER_BETA1)
        {
            CmpFreeBigValueData(Hive, DataCell);
        }
        else
        {
            CmpFreeValueData(Hive, DataCell, DataSize);
        }

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Value->Type = Type;
    Value->DataLength = DataSize;
    Value->Data = DataCell;
    HvReleaseCell(Hive, ValueCell);
    return STATUS_SUCCESS;
}

/*
 * Reads a value's data wherever it lives. A value small enough to sit inside
 * its own cell is handed back from there without a copy.
 */
BOOLEAN
NTAPI
CmpReadValueData(
    _In_ PHHIVE Hive,
    _In_ PCM_KEY_VALUE Value,
    _Out_ PULONG Length,
    _Out_ PVOID *Buffer,
    _Out_ PBOOLEAN Allocated,
    _Out_ PHCELL_INDEX CellToRelease)
{
    *Buffer = NULL;
    *Allocated = FALSE;
    *CellToRelease = HCELL_NIL;

    CmpIsKeyValueSmall(Length, Value->DataLength);

    if (*Length == 0)
    {
        *Buffer = NULL;
        return TRUE;
    }

    if (CmpIsKeyValueSmall(Length, Value->DataLength))
    {
        *Buffer = &Value->Data;
        return TRUE;
    }

    if (CmpIsKeyValueBig(Hive, Value->DataLength))
    {
        return CmpGetBigValueData(Hive, Value, *Length, Buffer, Allocated);
    }

    if (Value->Data == HCELL_NIL) return FALSE;

    *Buffer = HvGetCell(Hive, Value->Data);
    if (*Buffer == NULL) return FALSE;

    *CellToRelease = Value->Data;
    return TRUE;
}
