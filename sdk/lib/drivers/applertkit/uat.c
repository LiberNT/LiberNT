/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Address translations of the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appleuat.h>

#define UAT_TAG                     'tUpA'
#define UAT_TYPE_MASK               0x3ULL
#define UAT_TYPE_ENTRY              0x3ULL
#define UAT_ADDRESS_MASK            0x0000fffffffff000ULL
#define UAT_MEMORY_SHIFT            2
#define UAT_MEMORY_MASK             (0x7ULL << UAT_MEMORY_SHIFT)
#define UAT_ACCESS_SHIFT            6
#define UAT_ACCESS_MASK             (0x3ULL << UAT_ACCESS_SHIFT)
#define UAT_ACCESSED                (1ULL << 10)
#define UAT_NOT_GLOBAL              (1ULL << 11)
#define UAT_HIGH_SHIFT              52
#define UAT_HIGH_GPU                (1ULL << 3)
#define UAT_HIGH_NO_EXECUTE_PRIV    (1ULL << 1)
#define UAT_HIGH_NO_EXECUTE_USER    (1ULL << 2)

struct _APPLE_UAT_TABLE
{
    PULONG64 Entries;
    ULONG64 Physical;
    struct _APPLE_UAT_TABLE **Children;
};

ULONG64
NTAPI
AppleUatEntry(
    _In_ ULONG64 Physical,
    _In_ const APPLE_UAT_PROTECTION *Protection,
    _In_ BOOLEAN Global)
{
    ULONG64 High = UAT_HIGH_GPU;

    if (Protection->NoExecutePrivileged)
        High |= UAT_HIGH_NO_EXECUTE_PRIV;
    if (Protection->NoExecuteUser)
        High |= UAT_HIGH_NO_EXECUTE_USER;
    return (Physical & UAT_ADDRESS_MASK) | UAT_TYPE_ENTRY | UAT_ACCESSED |
           ((ULONG64)(Protection->Access & 3) << UAT_ACCESS_SHIFT) |
           ((ULONG64)(Protection->Memory & 7) << UAT_MEMORY_SHIFT) | (High << UAT_HIGH_SHIFT) |
           (Global ? 0 : UAT_NOT_GLOBAL);
}

static
VOID
AppleUatDecode(
    _In_ ULONG64 Entry,
    _Out_ PAPPLE_UAT_PROTECTION Protection)
{
    ULONG64 High = Entry >> UAT_HIGH_SHIFT;

    Protection->Access = (UCHAR)((Entry & UAT_ACCESS_MASK) >> UAT_ACCESS_SHIFT);
    Protection->Memory = (UCHAR)((Entry & UAT_MEMORY_MASK) >> UAT_MEMORY_SHIFT);
    Protection->NoExecutePrivileged = (High & UAT_HIGH_NO_EXECUTE_PRIV) != 0;
    Protection->NoExecuteUser = (High & UAT_HIGH_NO_EXECUTE_USER) != 0;
}

static
PAPPLE_UAT_TABLE
AppleUatAllocateTable(
    _Inout_ PAPPLE_UAT Uat,
    _In_ BOOLEAN Leaf)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PAPPLE_UAT_TABLE Table;

    Table = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Table), UAT_TAG);
    if (!Table)
        return NULL;
    Table->Children = NULL;
    if (!Leaf)
    {
        Table->Children = ExAllocatePoolWithTag(NonPagedPool, APPLE_UAT_TABLE_ENTRIES * sizeof(PAPPLE_UAT_TABLE),
                                                UAT_TAG);
        if (!Table->Children)
        {
            ExFreePoolWithTag(Table, UAT_TAG);
            return NULL;
        }
        RtlZeroMemory(Table->Children, APPLE_UAT_TABLE_ENTRIES * sizeof(PAPPLE_UAT_TABLE));
    }
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = APPLE_UAT_PAGE_SIZE;
    Table->Entries = MmAllocateContiguousMemorySpecifyCache(APPLE_UAT_PAGE_SIZE, Low, High, Boundary, MmCached);
    if (!Table->Entries)
    {
        if (Table->Children)
            ExFreePoolWithTag(Table->Children, UAT_TAG);
        ExFreePoolWithTag(Table, UAT_TAG);
        return NULL;
    }
    RtlZeroMemory(Table->Entries, APPLE_UAT_PAGE_SIZE);
    Table->Physical = (ULONG64)MmGetPhysicalAddress(Table->Entries).QuadPart;
    Uat->Tables++;
    return Table;
}

static
VOID
AppleUatFreeTable(
    _Inout_ PAPPLE_UAT Uat,
    _In_opt_ PAPPLE_UAT_TABLE Table,
    _In_ ULONG Level)
{
    ULONG Index;

    if (!Table)
        return;
    if (Table->Children)
    {
        for (Index = 0; Index < APPLE_UAT_TABLE_ENTRIES; ++Index)
            AppleUatFreeTable(Uat, Table->Children[Index], Level - 1);
        ExFreePoolWithTag(Table->Children, UAT_TAG);
    }
    MmFreeContiguousMemorySpecifyCache(Table->Entries, APPLE_UAT_PAGE_SIZE, MmCached);
    ExFreePoolWithTag(Table, UAT_TAG);
    Uat->Tables--;
}

NTSTATUS
NTAPI
AppleUatCreate(
    _Out_ PAPPLE_UAT Uat,
    _In_ BOOLEAN Global,
    _In_z_ PCSTR Owner)
{
    RtlZeroMemory(Uat, sizeof(*Uat));
    Uat->Global = Global;
    Uat->Owner = Owner;
    Uat->Top = AppleUatAllocateTable(Uat, FALSE);
    if (!Uat->Top)
        return STATUS_INSUFFICIENT_RESOURCES;
    Uat->TopPhysical = Uat->Top->Physical;
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleUatDestroy(
    _Inout_ PAPPLE_UAT Uat)
{
    AppleUatFreeTable(Uat, Uat->Top, APPLE_UAT_LEVELS);
    Uat->Top = NULL;
    Uat->TopPhysical = 0;
}

static
ULONG
AppleUatSlot(
    _In_ ULONG64 Address,
    _In_ ULONG Level)
{
    return (ULONG)(Address >> (APPLE_UAT_PAGE_SHIFT + (Level - 1) * APPLE_UAT_LEVEL_BITS)) &
           (APPLE_UAT_TABLE_ENTRIES - 1);
}

static
PAPPLE_UAT_TABLE
AppleUatWalk(
    _Inout_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address,
    _In_ BOOLEAN Create)
{
    PAPPLE_UAT_TABLE Table = Uat->Top, Child;
    ULONG Level, Slot;

    for (Level = APPLE_UAT_LEVELS; Table && Level > 1; --Level)
    {
        Slot = AppleUatSlot(Address, Level);
        Child = Table->Children[Slot];
        if (!Child)
        {
            if (!Create)
                return NULL;
            Child = AppleUatAllocateTable(Uat, Level == 2);
            if (!Child)
                return NULL;
            Table->Children[Slot] = Child;
            Table->Entries[Slot] = (Child->Physical & UAT_ADDRESS_MASK) | UAT_TYPE_ENTRY;
        }
        Table = Child;
    }
    return Table;
}

NTSTATUS
NTAPI
AppleUatMap(
    _Inout_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length,
    _In_ const APPLE_UAT_PROTECTION *Protection)
{
    PAPPLE_UAT_TABLE Table;
    ULONG64 Offset;

    if (!Length || (Address & (APPLE_UAT_PAGE_SIZE - 1)) || (Physical & (APPLE_UAT_PAGE_SIZE - 1)) ||
        (Length & (APPLE_UAT_PAGE_SIZE - 1)) || Address + Length < Address ||
        Address + Length > APPLE_UAT_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Offset = 0; Offset < Length; Offset += APPLE_UAT_PAGE_SIZE)
    {
        Table = AppleUatWalk(Uat, Address + Offset, TRUE);
        if (!Table)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (Table->Entries[AppleUatSlot(Address + Offset, 1)] & UAT_TYPE_MASK)
            return STATUS_CONFLICTING_ADDRESSES;
        Table->Entries[AppleUatSlot(Address + Offset, 1)] =
            AppleUatEntry(Physical + Offset, Protection, Uat->Global);
    }
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
AppleUatTranslate(
    _In_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address,
    _Out_ PULONG64 Physical,
    _Out_opt_ PAPPLE_UAT_PROTECTION Protection)
{
    PAPPLE_UAT_TABLE Table;
    ULONG64 Entry;

    *Physical = 0;
    if (Address >= APPLE_UAT_MAX_ADDRESS)
        return FALSE;
    Table = AppleUatWalk((PAPPLE_UAT)Uat, Address, FALSE);
    if (!Table)
        return FALSE;
    Entry = Table->Entries[AppleUatSlot(Address, 1)];
    if ((Entry & UAT_TYPE_MASK) != UAT_TYPE_ENTRY)
        return FALSE;
    *Physical = (Entry & UAT_ADDRESS_MASK) | (Address & (APPLE_UAT_PAGE_SIZE - 1));
    if (Protection)
        AppleUatDecode(Entry, Protection);
    return TRUE;
}

NTSTATUS
NTAPI
AppleUatUnmap(
    _Inout_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address,
    _In_ SIZE_T Length)
{
    PAPPLE_UAT_TABLE Table;
    ULONG64 Offset;

    if (!Length || (Address & (APPLE_UAT_PAGE_SIZE - 1)) || (Length & (APPLE_UAT_PAGE_SIZE - 1)) ||
        Address + Length < Address || Address + Length > APPLE_UAT_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Offset = 0; Offset < Length; Offset += APPLE_UAT_PAGE_SIZE)
    {
        Table = AppleUatWalk(Uat, Address + Offset, FALSE);
        if (Table)
            Table->Entries[AppleUatSlot(Address + Offset, 1)] = 0;
    }
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}
