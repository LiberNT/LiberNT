/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Where the Apple silicon power management processor reads the power requests of the blocks it manages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appleadt.h>

#define PTD_RANGE_ENTRY_SIZE        32
#define PTD_RANGE_INDEX_OFFSET      4
#define PTD_RANGE_NAME_OFFSET       16
#define PTD_RANGE_NAME_SIZE         16
#define PMGR_DEVICE_SIZE            48
#define PMGR_DEVICE_ID_OFFSET       3
#define PMGR_DEVICE_NAME_OFFSET     32
#define PMGR_DEVICE_NAME_SIZE       16
#define ADT_ADDRESS_CELLS           2
#define ADT_REG_ENTRY_SIZE          (2 * sizeof(ULONG64))
#define ADT_RANGE_ENTRY_SIZE        (3 * sizeof(ULONG64))

static
BOOLEAN
PtdNameEquals(
    _In_reads_bytes_(FieldSize) const UCHAR *Field,
    _In_ ULONG FieldSize,
    _In_z_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < FieldSize && Name[Index]; ++Index)
    {
        if (Field[Index] != (UCHAR)Name[Index])
            return FALSE;
    }
    if (Name[Index])
        return FALSE;
    return Index == FieldSize || !Field[Index];
}

static
ULONG64
PtdReadU64(
    _In_reads_bytes_(sizeof(ULONG64)) const UCHAR *Data)
{
    ULONG64 Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static
BOOLEAN
PtdCellsAre(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _In_ ULONG Expected)
{
    ULONG Value;

    return AppleAdtReadU32(Adt, Node, Name, &Value) && Value == Expected;
}

NTSTATUS
NTAPI
AppleAdtPtdRangeIndex(
    _In_ PAPPLE_ADT Adt,
    _In_z_ PCSTR Name,
    _Out_ PULONG Index)
{
    const UCHAR *Ranges, *Entry;
    ULONG Nub, Length, Offset;

    *Index = 0;
    Nub = AppleAdtPath(Adt, "/arm-io/pmp/iop-pmp-nub");
    if (Nub == APPLE_ADT_NO_NODE)
        return STATUS_NOT_FOUND;
    Ranges = AppleAdtGetProperty(Adt, Nub, "ptd-range", &Length);
    if (!Ranges || !Length || Length % PTD_RANGE_ENTRY_SIZE)
        return STATUS_NOT_FOUND;
    for (Offset = 0; Offset < Length; Offset += PTD_RANGE_ENTRY_SIZE)
    {
        Entry = Ranges + Offset;
        if (PtdNameEquals(Entry + PTD_RANGE_NAME_OFFSET, PTD_RANGE_NAME_SIZE, Name))
        {
            RtlCopyMemory(Index, Entry + PTD_RANGE_INDEX_OFFSET, sizeof(*Index));
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS
NTAPI
AppleAdtPmpRequestWord(
    _In_ PAPPLE_ADT Adt,
    _Out_ PULONG64 Table,
    _Out_ PULONG Index)
{
    const UCHAR *Ranges, *Reg;
    ULONG ArmIo, Pmp, Length, RegIndex, Offset;
    ULONG64 Address, Child, Parent, Size;
    BOOLEAN Found;
    NTSTATUS Status;

    *Table = 0;
    *Index = 0;
    ArmIo = AppleAdtPath(Adt, "/arm-io");
    Pmp = AppleAdtPath(Adt, "/arm-io/pmp");
    if (ArmIo == APPLE_ADT_NO_NODE || Pmp == APPLE_ADT_NO_NODE)
        return STATUS_NOT_FOUND;
    Status = AppleAdtPtdRangeIndex(Adt, "SOC-DEV-PS-REQ", Index);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!PtdCellsAre(Adt, AppleAdtRootNode(Adt), "#address-cells", ADT_ADDRESS_CELLS) ||
        !PtdCellsAre(Adt, ArmIo, "#address-cells", ADT_ADDRESS_CELLS) ||
        !PtdCellsAre(Adt, ArmIo, "#size-cells", ADT_ADDRESS_CELLS) ||
        !AppleAdtReadU32(Adt, Pmp, "ptd-update-reg-index", &RegIndex))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Reg = AppleAdtGetProperty(Adt, Pmp, "reg", &Length);
    if (!Reg || RegIndex >= Length / ADT_REG_ENTRY_SIZE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Address = PtdReadU64(Reg + RegIndex * ADT_REG_ENTRY_SIZE);

    Ranges = AppleAdtGetProperty(Adt, ArmIo, "ranges", &Length);
    if (!Ranges || Length % ADT_RANGE_ENTRY_SIZE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Offset = 0, Found = FALSE; Offset < Length; Offset += ADT_RANGE_ENTRY_SIZE)
    {
        Child = PtdReadU64(Ranges + Offset);
        Parent = PtdReadU64(Ranges + Offset + sizeof(ULONG64));
        Size = PtdReadU64(Ranges + Offset + 2 * sizeof(ULONG64));
        if (Address >= Child && Address - Child < Size)
        {
            Address = Parent + (Address - Child);
            Found = TRUE;
            break;
        }
    }
    if (!Found || Address < APPLE_PMP_PTD_UPDATE_OFFSET)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *Table = Address - APPLE_PMP_PTD_UPDATE_OFFSET;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAdtPmgrPmpBit(
    _In_ PAPPLE_ADT Adt,
    _In_z_ PCSTR Name,
    _Out_ PULONG Bit)
{
    const UCHAR *Devices, *Record;
    ULONG Pmgr, Length, Offset;

    *Bit = APPLE_PMP_NO_REQUEST_BIT;
    Pmgr = AppleAdtPath(Adt, "/arm-io/pmgr");
    if (Pmgr == APPLE_ADT_NO_NODE)
        return STATUS_NOT_FOUND;
    Devices = AppleAdtGetProperty(Adt, Pmgr, "devices", &Length);
    if (!Devices || !Length || Length % PMGR_DEVICE_SIZE)
        return STATUS_NOT_FOUND;
    for (Offset = 0; Offset < Length; Offset += PMGR_DEVICE_SIZE)
    {
        Record = Devices + Offset;
        if (!PtdNameEquals(Record + PMGR_DEVICE_NAME_OFFSET, PMGR_DEVICE_NAME_SIZE, Name))
            continue;
        if (Record[PMGR_DEVICE_ID_OFFSET])
            *Bit = Record[PMGR_DEVICE_ID_OFFSET] - 1UL;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}
