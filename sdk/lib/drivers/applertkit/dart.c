/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stream setup of Apple silicon DART IOMMUs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appledart.h>

#define DART_PARAMS1                0x00
#define DART_PARAMS2                0x04
#define DART_PARAMS2_BYPASS         0x1
#define DART_T8110_PARAMS3          0x08
#define DART_T8110_PARAMS4          0x0c
#define DART_T8110_TLB_CMD          0x80
#define DART_T8110_TLB_CMD_BUSY     0x80000000
#define DART_T8110_TLB_CMD_FLUSH    0x00000100
#define DART_T8110_PROTECT          0x200
#define DART_T8110_PROTECT_TCR      0x1
#define DART_T8110_ENABLE_STREAMS   0xc00
#define DART_T8110_TCR              0x1000
#define DART_T8110_TCR_TRANSLATE    0x1
#define DART_T8110_TCR_BYPASS       0x6
#define DART_T8110_TCR_FOUR_LEVEL   0x8
#define DART_T8110_TTBR             0x1400
#define DART_T8110_TTBR_VALID       0x1

#define DART_PAGE_SHIFT             14
#define DART_PAGE_SIZE              (1ULL << DART_PAGE_SHIFT)
#define DART_LEVEL_BITS             11
#define DART_TABLE_ENTRIES          (1UL << DART_LEVEL_BITS)
#define DART_PTE_VALID              0x1ULL
#define DART_PTE_SUBPAGE_END        (0xfffULL << 40)
#define DART_PTE_ADDRESS_MASK       0x0000003ffffffc00ULL
#define DART_TAG                    'tDpA'
#define DART_LOW_WINDOW_BASE        0x01000000ULL
#define DART_LOW_WINDOW_END         0xc0000000ULL

typedef struct _DART_TABLE
{
    PULONG64 Entries;
    struct _DART_TABLE **Children;
} DART_TABLE, *PDART_TABLE;

typedef struct _DART_IDENTITY
{
    ULONG InputBits;
    ULONG Levels;
    PDART_TABLE Top;
    ULONG64 TopPhysical;
} DART_IDENTITY, *PDART_IDENTITY;

static DART_IDENTITY AppleDartIdentity;
static volatile LONG AppleDartIdentityBusy;
static ULONG64 AppleDartLowNext = DART_LOW_WINDOW_BASE;

static
ULONG
AppleDartRead(
    _In_ PUCHAR Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Base + Offset));
}

static
VOID
AppleDartWrite(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + Offset), Value);
}

static
ULONG64
AppleDartEntry(
    _In_ ULONG64 Physical)
{
    return (Physical >> 4) & DART_PTE_ADDRESS_MASK;
}

static
PDART_TABLE
AppleDartAllocateTable(
    _In_ BOOLEAN Leaf,
    _Out_ PULONG64 Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PDART_TABLE Table;

    Table = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Table), DART_TAG);
    if (!Table)
        return NULL;
    Table->Children = NULL;
    if (!Leaf)
    {
        Table->Children = ExAllocatePoolWithTag(NonPagedPool, DART_TABLE_ENTRIES * sizeof(PDART_TABLE), DART_TAG);
        if (!Table->Children)
        {
            ExFreePoolWithTag(Table, DART_TAG);
            return NULL;
        }
        RtlZeroMemory(Table->Children, DART_TABLE_ENTRIES * sizeof(PDART_TABLE));
    }
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = DART_PAGE_SIZE;
    Table->Entries = MmAllocateContiguousMemorySpecifyCache(DART_PAGE_SIZE, Low, High, Boundary, MmCached);
    if (!Table->Entries)
    {
        if (Table->Children)
            ExFreePoolWithTag(Table->Children, DART_TAG);
        ExFreePoolWithTag(Table, DART_TAG);
        return NULL;
    }
    RtlZeroMemory(Table->Entries, DART_PAGE_SIZE);
    *Physical = (ULONG64)MmGetPhysicalAddress(Table->Entries).QuadPart;
    return Table;
}

static
NTSTATUS
AppleDartMapPage(
    _Inout_ PDART_IDENTITY Identity,
    _In_ ULONG64 Iova,
    _In_ ULONG64 Physical)
{
    PDART_TABLE Table = Identity->Top, Child;
    ULONG64 TablePhysical;
    ULONG Level, Slot;

    for (Level = Identity->Levels; Level > 1; --Level)
    {
        Slot = (ULONG)(Iova >> (DART_PAGE_SHIFT + (Level - 1) * DART_LEVEL_BITS)) & (DART_TABLE_ENTRIES - 1);
        Child = Table->Children[Slot];
        if (!Child)
        {
            Child = AppleDartAllocateTable(Level == 2, &TablePhysical);
            if (!Child)
                return STATUS_INSUFFICIENT_RESOURCES;
            Table->Children[Slot] = Child;
            Table->Entries[Slot] = AppleDartEntry(TablePhysical) | DART_PTE_VALID;
        }
        Table = Child;
    }
    Table->Entries[(Iova >> DART_PAGE_SHIFT) & (DART_TABLE_ENTRIES - 1)] =
        AppleDartEntry(Physical) | DART_PTE_SUBPAGE_END | DART_PTE_VALID;
    return STATUS_SUCCESS;
}

static
BOOLEAN
AppleDartUnmapPage(
    _Inout_ PDART_IDENTITY Identity,
    _In_ ULONG64 Iova)
{
    PDART_TABLE Table = Identity->Top;
    ULONG Level, Slot;

    for (Level = Identity->Levels; Table && Level > 1; --Level)
    {
        Slot = (ULONG)(Iova >> (DART_PAGE_SHIFT + (Level - 1) * DART_LEVEL_BITS)) & (DART_TABLE_ENTRIES - 1);
        Table = Table->Children[Slot];
    }
    if (!Table)
        return FALSE;
    Slot = (ULONG)(Iova >> DART_PAGE_SHIFT) & (DART_TABLE_ENTRIES - 1);
    if (!(Table->Entries[Slot] & DART_PTE_VALID))
        return FALSE;
    Table->Entries[Slot] = 0;
    return TRUE;
}

static
NTSTATUS
AppleDartBuildIdentity(
    _Inout_ PDART_IDENTITY Identity,
    _In_ ULONG InputBits,
    _In_ ULONG OutputBits)
{
    PPHYSICAL_MEMORY_RANGE Ranges;
    ULONG64 Mask, Page, End, Lowest = MAXULONG64, Highest = 0;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    if (InputBits < DART_PAGE_SHIFT + DART_LEVEL_BITS + 1 || InputBits > DART_PAGE_SHIFT + 3 * DART_LEVEL_BITS)
    {
        DbgPrint("DART: %lu-bit device addresses are outside the supported table depths\n", InputBits);
        return STATUS_NOT_SUPPORTED;
    }
    Ranges = MmGetPhysicalMemoryRanges();
    if (!Ranges)
    {
        DbgPrint("DART: no physical memory ranges\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (Index = 0; Ranges[Index].NumberOfBytes.QuadPart; ++Index)
    {
        Lowest = min(Lowest, (ULONG64)Ranges[Index].BaseAddress.QuadPart);
        Highest = max(Highest, (ULONG64)Ranges[Index].BaseAddress.QuadPart + Ranges[Index].NumberOfBytes.QuadPart);
    }
    Mask = (1ULL << InputBits) - 1;
    if (!Index || Highest > (1ULL << min(OutputBits, 42)) || ((Lowest ^ (Highest - 1)) & ~Mask))
    {
        DbgPrint("DART: memory %I64x-%I64x in %lu ranges does not fit %lu-bit input and %lu-bit output addresses\n",
                 Lowest, Highest, Index, InputBits, OutputBits);
        Status = STATUS_NOT_SUPPORTED;
        goto Done;
    }
    Identity->InputBits = InputBits;
    Identity->Levels = (InputBits > DART_PAGE_SHIFT + 2 * DART_LEVEL_BITS) ? 3 : 2;
    Identity->Top = AppleDartAllocateTable(FALSE, &Identity->TopPhysical);
    if (!Identity->Top)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    for (Index = 0; Ranges[Index].NumberOfBytes.QuadPart; ++Index)
    {
        Page = (ULONG64)Ranges[Index].BaseAddress.QuadPart & ~(DART_PAGE_SIZE - 1);
        End = (ULONG64)Ranges[Index].BaseAddress.QuadPart + Ranges[Index].NumberOfBytes.QuadPart;
        for (; Page < End; Page += DART_PAGE_SIZE)
        {
            Status = AppleDartMapPage(Identity, Page & Mask, Page);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint("DART: identity table for page %I64x not allocated: %lx\n", Page, Status);
                goto Done;
            }
        }
    }
    KeMemoryBarrier();

Done:
    ExFreePool(Ranges);
    return Status;
}

static
VOID
AppleDartFlush(
    _In_ PUCHAR Base,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner)
{
    ULONG Spin;

    KeMemoryBarrier();
    AppleDartWrite(Base, DART_T8110_TLB_CMD, DART_T8110_TLB_CMD_FLUSH | Sid);
    for (Spin = 0; Spin < 1000 && (AppleDartRead(Base, DART_T8110_TLB_CMD) & DART_T8110_TLB_CMD_BUSY); ++Spin)
        KeStallExecutionProcessor(1);
    if (Spin == 1000)
        DbgPrint("%s: DART TLB flush did not complete\n", Owner);
}

static
VOID
AppleDartTranslateStream(
    _In_ PUCHAR Base,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner)
{
    ULONG Ttbr = (ULONG)((AppleDartIdentity.TopPhysical >> DART_PAGE_SHIFT) << 2) | DART_T8110_TTBR_VALID;
    ULONG Tcr = DART_T8110_TCR_TRANSLATE | (AppleDartIdentity.Levels == 3 ? DART_T8110_TCR_FOUR_LEVEL : 0);

    if (AppleDartRead(Base, DART_T8110_TCR + 4 * Sid) == Tcr && AppleDartRead(Base, DART_T8110_TTBR + 4 * Sid) == Ttbr)
    {
        AppleDartFlush(Base, Sid, Owner);
        return;
    }
    AppleDartWrite(Base, DART_T8110_TCR + 4 * Sid, 0);
    AppleDartWrite(Base, DART_T8110_TTBR + 4 * Sid, Ttbr);
    AppleDartWrite(Base, DART_T8110_ENABLE_STREAMS + 4 * (Sid >> 5),
                   AppleDartRead(Base, DART_T8110_ENABLE_STREAMS + 4 * (Sid >> 5)) | (1UL << (Sid & 31)));
    AppleDartFlush(Base, Sid, Owner);
    AppleDartWrite(Base, DART_T8110_TCR + 4 * Sid, Tcr);
}

static
NTSTATUS
AppleDartIdentityStream(
    _In_ PUCHAR Base,
    _In_ ULONG Sid,
    _In_ ULONG Params3,
    _In_z_ PCSTR Owner)
{
    ULONG InputBits = (Params3 >> 16) & 0x3f, OutputBits = (Params3 >> 24) & 0x3f;
    NTSTATUS Status = STATUS_SUCCESS;

    if (InterlockedCompareExchange(&AppleDartIdentityBusy, 1, 0) != 0)
        return STATUS_DEVICE_BUSY;
    if (!AppleDartIdentity.Top)
    {
        Status = AppleDartBuildIdentity(&AppleDartIdentity, InputBits, OutputBits);
        DbgPrint("%s: DART identity tables for %lu-bit addresses, %lu levels: %lx\n", Owner, InputBits,
                 AppleDartIdentity.Levels, Status);
    }
    else if (AppleDartIdentity.InputBits != InputBits)
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    InterlockedExchange(&AppleDartIdentityBusy, 0);
    if (!NT_SUCCESS(Status) || !AppleDartIdentity.Top)
        return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
    AppleDartTranslateStream(Base, Sid, Owner);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AppleDartPhysicalStream(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner)
{
    ULONG Length, Size, Params2, Params3, Streams, Protect;
    const VOID *Compatible;
    ULONG64 Physical;
    PUCHAR Base;
    NTSTATUS Status = STATUS_SUCCESS;

    Compatible = FdtGetProperty(&Tree->Fdt, Dart, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8110-dart"))
    {
        DbgPrint("%s: DART %lx is not a t8110 DART\n", Owner, Dart);
        return STATUS_NOT_SUPPORTED;
    }
    Base = FdtDriverMapReg(Tree, Dart, 0, &Physical, &Size);
    if (!Base)
        return STATUS_INSUFFICIENT_RESOURCES;
    Params2 = AppleDartRead(Base, DART_PARAMS2);
    Params3 = AppleDartRead(Base, DART_T8110_PARAMS3);
    Streams = AppleDartRead(Base, DART_T8110_PARAMS4) & 0x1ff;
    Protect = AppleDartRead(Base, DART_T8110_PROTECT);
    DbgPrint("%s: DART %I64x stream %lu: params %08lx %08lx %08lx, %lu streams, protect %08lx, tcr %08lx ttbr %08lx\n",
             Owner, Physical, Sid, AppleDartRead(Base, DART_PARAMS1), Params2, Params3, Streams, Protect,
             Sid < Streams ? AppleDartRead(Base, DART_T8110_TCR + 4 * Sid) : 0,
             Sid < Streams ? AppleDartRead(Base, DART_T8110_TTBR + 4 * Sid) : 0);
    if (Sid >= Streams)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    else if (Protect & DART_T8110_PROTECT_TCR)
    {
        if ((AppleDartRead(Base, DART_T8110_TCR + 4 * Sid) & DART_T8110_TCR_BYPASS) != DART_T8110_TCR_BYPASS)
        {
            DbgPrint("%s: DART stream %lu is locked by the firmware without bypass\n", Owner, Sid);
            Status = STATUS_ACCESS_DENIED;
        }
    }
    else if (Params2 & DART_PARAMS2_BYPASS)
    {
        AppleDartWrite(Base, DART_T8110_TCR + 4 * Sid, DART_T8110_TCR_BYPASS);
        AppleDartWrite(Base, DART_T8110_TTBR + 4 * Sid, 0);
        AppleDartWrite(Base, DART_T8110_ENABLE_STREAMS + 4 * (Sid >> 5),
                       AppleDartRead(Base, DART_T8110_ENABLE_STREAMS + 4 * (Sid >> 5)) | (1UL << (Sid & 31)));
        AppleDartFlush(Base, Sid, Owner);
    }
    else
    {
        Status = AppleDartIdentityStream(Base, Sid, Params3, Owner);
    }
    MmUnmapIoSpace(Base, Size);
    return Status;
}

NTSTATUS
NTAPI
AppleDartMapStream(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner)
{
    ULONG Cells;

    if (Dart == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return AppleDartPhysicalStream(Tree, Dart, Sid, Owner);
}

NTSTATUS
NTAPI
AppleDartMapPhysical(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Owner)
{
    const VOID *Iommus;
    ULONG Length, Index, Dart, Cells = 0;
    ULONG64 Phandle, Sid;
    NTSTATUS Status;

    Iommus = FdtGetProperty(&Tree->Fdt, Node, "iommus", &Length);
    if (!Iommus)
        return STATUS_SUCCESS;
    for (Index = 0; FdtReadCells(Iommus, Length, Index, 1, &Phandle); Index += 1 + Cells)
    {
        Dart = FdtDriverFindPhandle(Tree, (ULONG)Phandle);
        if (Dart == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1 ||
            !FdtReadCells(Iommus, Length, Index + 1, 1, &Sid))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Status = AppleDartPhysicalStream(Tree, Dart, (ULONG)Sid, Owner);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleDartMapLow(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length,
    _Out_ PULONG64 Iova,
    _In_z_ PCSTR Owner)
{
    ULONG RegisterLength, Size, Params3, Streams, Cells, InputBits, OutputBits;
    ULONG64 First, Last, Window, Page;
    const VOID *Compatible;
    PUCHAR Base;
    NTSTATUS Status = STATUS_SUCCESS;

    *Iova = 0;
    if (Dart == FDT_NO_NODE || !Length || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1)
        return STATUS_INVALID_PARAMETER;
    Compatible = FdtGetProperty(&Tree->Fdt, Dart, "compatible", &RegisterLength);
    if (!FdtStringListContains(Compatible, RegisterLength, "apple,t8110-dart"))
        return STATUS_NOT_SUPPORTED;
    Base = FdtDriverMapReg(Tree, Dart, 0, NULL, &Size);
    if (!Base)
        return STATUS_INSUFFICIENT_RESOURCES;
    Params3 = AppleDartRead(Base, DART_T8110_PARAMS3);
    Streams = AppleDartRead(Base, DART_T8110_PARAMS4) & 0x1ff;
    InputBits = (Params3 >> 16) & 0x3f;
    OutputBits = (Params3 >> 24) & 0x3f;
    if (Sid >= Streams)
        Status = STATUS_INVALID_PARAMETER;
    else if ((AppleDartRead(Base, DART_T8110_PROTECT) & DART_T8110_PROTECT_TCR))
        Status = STATUS_ACCESS_DENIED;
    else if (InterlockedCompareExchange(&AppleDartIdentityBusy, 1, 0) != 0)
        Status = STATUS_DEVICE_BUSY;
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_DEVICE_BUSY)
            DbgPrint("%s: DART stream %lu of %lu cannot take a low mapping: %lx\n", Owner, Sid, Streams, Status);
        MmUnmapIoSpace(Base, Size);
        return Status;
    }

    if (!AppleDartIdentity.Top)
    {
        Status = AppleDartBuildIdentity(&AppleDartIdentity, InputBits, OutputBits);
        DbgPrint("%s: DART identity tables for %lu-bit addresses, %lu levels: %lx\n", Owner, InputBits,
                 AppleDartIdentity.Levels, Status);
    }
    else if (AppleDartIdentity.InputBits != InputBits)
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    First = Physical & ~(DART_PAGE_SIZE - 1);
    Last = (Physical + Length + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1);
    Window = AppleDartLowNext;
    if (NT_SUCCESS(Status) && (Last <= First || Window + (Last - First) > DART_LOW_WINDOW_END))
        Status = STATUS_INSUFFICIENT_RESOURCES;
    for (Page = First; NT_SUCCESS(Status) && Page < Last; Page += DART_PAGE_SIZE)
        Status = AppleDartMapPage(&AppleDartIdentity, Window + (Page - First), Page);
    if (NT_SUCCESS(Status))
        AppleDartLowNext = Window + (Last - First);
    KeMemoryBarrier();
    InterlockedExchange(&AppleDartIdentityBusy, 0);

    if (NT_SUCCESS(Status))
    {
        AppleDartTranslateStream(Base, Sid, Owner);
        *Iova = Window + (Physical - First);
        DbgPrint("%s: DART stream %lu maps %I64x+%Ix at device address %I64x\n", Owner, Sid, Physical, Length, *Iova);
    }
    else
    {
        DbgPrint("%s: DART stream %lu low mapping of %I64x+%Ix failed: %lx\n", Owner, Sid, Physical, Length, Status);
    }
    MmUnmapIoSpace(Base, Size);
    return Status;
}

NTSTATUS
NTAPI
AppleDartUnmapLow(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length,
    _In_z_ PCSTR Owner)
{
    ULONG RegisterLength, Size, Cells;
    ULONG64 First, Last, Page;
    const VOID *Compatible;
    PUCHAR Base;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Dart == FDT_NO_NODE || !Length || Iova < DART_LOW_WINDOW_BASE || Iova + Length < Iova ||
        Iova + Length > DART_LOW_WINDOW_END || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Compatible = FdtGetProperty(&Tree->Fdt, Dart, "compatible", &RegisterLength);
    if (!FdtStringListContains(Compatible, RegisterLength, "apple,t8110-dart"))
        return STATUS_NOT_SUPPORTED;
    Base = FdtDriverMapReg(Tree, Dart, 0, NULL, &Size);
    if (!Base)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Sid >= (AppleDartRead(Base, DART_T8110_PARAMS4) & 0x1ff))
        Status = STATUS_INVALID_PARAMETER;
    else if (InterlockedCompareExchange(&AppleDartIdentityBusy, 1, 0) != 0)
        Status = STATUS_DEVICE_BUSY;
    if (!NT_SUCCESS(Status))
    {
        MmUnmapIoSpace(Base, Size);
        return Status;
    }

    First = Iova & ~(DART_PAGE_SIZE - 1);
    Last = (Iova + Length + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1);
    for (Page = First; Page < Last; Page += DART_PAGE_SIZE)
    {
        if (!AppleDartUnmapPage(&AppleDartIdentity, Page))
            Status = STATUS_NOT_FOUND;
    }
    KeMemoryBarrier();
    InterlockedExchange(&AppleDartIdentityBusy, 0);
    AppleDartFlush(Base, Sid, Owner);
    MmUnmapIoSpace(Base, Size);
    return Status;
}

static
PULONG64
AppleDartAdoptedTable(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Physical,
    _Out_ PBOOLEAN Mapped)
{
    PHYSICAL_ADDRESS Address;
    ULONG Index;

    PULONG64 Table;

    *Mapped = FALSE;
    for (Index = 0; Index < Adopted->OwnTables; ++Index)
    {
        if (Adopted->OwnPhysical[Index] == Physical)
            return Adopted->OwnVirtual[Index];
    }
    for (Index = 0; Index < Adopted->ForeignTables; ++Index)
    {
        if (Adopted->ForeignPhysical[Index] == Physical)
            return Adopted->ForeignVirtual[Index];
    }
    Address.QuadPart = (LONGLONG)Physical;
    Table = MmMapIoSpace(Address, DART_PAGE_SIZE, MmCached);
    if (Table && Adopted->ForeignTables < RTL_NUMBER_OF(Adopted->ForeignPhysical))
    {
        Adopted->ForeignPhysical[Adopted->ForeignTables] = Physical;
        Adopted->ForeignVirtual[Adopted->ForeignTables] = Table;
        Adopted->ForeignTables++;
        return Table;
    }
    *Mapped = Table != NULL;
    return Table;
}

static
VOID
AppleDartAdoptedTableDone(
    _In_ PULONG64 Table,
    _In_ BOOLEAN Mapped)
{
    if (Table && Mapped)
        MmUnmapIoSpace(Table, DART_PAGE_SIZE);
}

static
ULONG64
AppleDartAdoptedWalk(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Iova,
    _In_ BOOLEAN Create)
{
    ULONG64 TablePhysical = Adopted->TopPhysical, Entry, ChildPhysical;
    PHYSICAL_ADDRESS Low, High, Boundary;
    PULONG64 Table, Child;
    BOOLEAN Mapped;
    ULONG Level, Slot;

    for (Level = Adopted->Levels; Level > 1; --Level)
    {
        Slot = (ULONG)(Iova >> (DART_PAGE_SHIFT + (Level - 1) * DART_LEVEL_BITS)) & (DART_TABLE_ENTRIES - 1);
        Table = AppleDartAdoptedTable(Adopted, TablePhysical, &Mapped);
        if (!Table)
            return 0;
        Entry = Table[Slot];
        if (!(Entry & DART_PTE_VALID))
        {
            if (!Create || Adopted->OwnTables == RTL_NUMBER_OF(Adopted->OwnPhysical))
            {
                AppleDartAdoptedTableDone(Table, Mapped);
                return 0;
            }
            Low.QuadPart = 0;
            High.QuadPart = -1;
            Boundary.QuadPart = DART_PAGE_SIZE;
            Child = MmAllocateContiguousMemorySpecifyCache(DART_PAGE_SIZE, Low, High, Boundary, MmCached);
            if (!Child)
            {
                AppleDartAdoptedTableDone(Table, Mapped);
                return 0;
            }
            RtlZeroMemory(Child, DART_PAGE_SIZE);
            ChildPhysical = (ULONG64)MmGetPhysicalAddress(Child).QuadPart;
            Adopted->OwnPhysical[Adopted->OwnTables] = ChildPhysical;
            Adopted->OwnVirtual[Adopted->OwnTables] = Child;
            Adopted->OwnTables++;
            KeMemoryBarrier();
            Entry = AppleDartEntry(ChildPhysical) | DART_PTE_VALID;
            Table[Slot] = Entry;
        }
        AppleDartAdoptedTableDone(Table, Mapped);
        TablePhysical = (Entry & DART_PTE_ADDRESS_MASK) << 4;
    }
    return TablePhysical;
}

NTSTATUS
NTAPI
AppleDartAdopt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Dart,
    _In_ ULONG Sid,
    _In_ ULONG64 WindowBase,
    _In_ ULONG64 WindowEnd,
    _In_z_ PCSTR Owner,
    _Out_ PAPPLE_DART_ADOPTED Adopted)
{
    ULONG Length, Size, Cells, Params3, Streams, Tcr, Ttbr;
    const VOID *Compatible;
    ULONG64 Physical;
    PUCHAR Base;

    RtlZeroMemory(Adopted, sizeof(*Adopted));
    if (Dart == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1 ||
        WindowEnd <= WindowBase || (WindowBase & (DART_PAGE_SIZE - 1)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Compatible = FdtGetProperty(&Tree->Fdt, Dart, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8110-dart"))
        return STATUS_NOT_SUPPORTED;
    Base = FdtDriverMapReg(Tree, Dart, 0, &Physical, &Size);
    if (!Base)
        return STATUS_INSUFFICIENT_RESOURCES;
    Params3 = AppleDartRead(Base, DART_T8110_PARAMS3);
    Streams = AppleDartRead(Base, DART_T8110_PARAMS4) & 0x1ff;
    Tcr = Sid < Streams ? AppleDartRead(Base, DART_T8110_TCR + 4 * Sid) : 0;
    Ttbr = Sid < Streams ? AppleDartRead(Base, DART_T8110_TTBR + 4 * Sid) : 0;
    DbgPrint("%s: DART %I64x stream %lu of %lu left by the firmware: tcr %08lx ttbr %08lx params %08lx protect %08lx\n",
             Owner, Physical, Sid, Streams, Tcr, Ttbr, Params3, AppleDartRead(Base, DART_T8110_PROTECT));
    if (Sid >= Streams || !(Tcr & DART_T8110_TCR_TRANSLATE) ||
        (Tcr & DART_T8110_TCR_BYPASS) == DART_T8110_TCR_BYPASS || !(Ttbr & DART_T8110_TTBR_VALID))
    {
        MmUnmapIoSpace(Base, Size);
        return STATUS_DEVICE_NOT_READY;
    }
    Adopted->Base = Base;
    Adopted->BaseSize = Size;
    Adopted->Sid = Sid;
    Adopted->Levels = (Tcr & DART_T8110_TCR_FOUR_LEVEL) ? 3 : 2;
    Adopted->InputBits = (Params3 >> 16) & 0x3f;
    Adopted->TopPhysical = (ULONG64)(Ttbr >> 2) << DART_PAGE_SHIFT;
    Adopted->NextIova = WindowBase;
    Adopted->EndIova = WindowEnd;
    Adopted->Owner = Owner;
    /* The tables index only the levels the stream has; a device address above them has no slot. */
    if (WindowEnd - 1 >= (1ULL << min(Adopted->InputBits, DART_PAGE_SHIFT + Adopted->Levels * DART_LEVEL_BITS)))
    {
        DbgPrint("%s: DART window %I64x-%I64x is beyond %lu-bit addresses with %lu levels\n", Owner, WindowBase,
                 WindowEnd, Adopted->InputBits, Adopted->Levels);
        AppleDartAdoptedRelease(Adopted);
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
AppleDartAdoptedTranslate(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Iova,
    _Out_ PULONG64 Physical)
{
    ULONG64 Leaf, Entry;
    PULONG64 Table;
    BOOLEAN Mapped;

    *Physical = 0;
    Leaf = AppleDartAdoptedWalk(Adopted, Iova, FALSE);
    if (!Leaf)
        return FALSE;
    Table = AppleDartAdoptedTable(Adopted, Leaf, &Mapped);
    if (!Table)
        return FALSE;
    Entry = Table[(Iova >> DART_PAGE_SHIFT) & (DART_TABLE_ENTRIES - 1)];
    AppleDartAdoptedTableDone(Table, Mapped);
    if (!(Entry & DART_PTE_VALID))
        return FALSE;
    *Physical = ((Entry & DART_PTE_ADDRESS_MASK) << 4) | (Iova & (DART_PAGE_SIZE - 1));
    return TRUE;
}

BOOLEAN
NTAPI
AppleDartAdoptedRangeFree(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length)
{
    ULONG64 First = Iova & ~(DART_PAGE_SIZE - 1), Last = (Iova + Length + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1);
    ULONG64 Page, Unused;

    if (!Length || Last <= First)
        return FALSE;
    for (Page = First; Page < Last; Page += DART_PAGE_SIZE)
    {
        if (AppleDartAdoptedTranslate(Adopted, Page, &Unused))
            return FALSE;
    }
    return TRUE;
}

NTSTATUS
NTAPI
AppleDartAdoptedMapAt(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Iova,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length)
{
    ULONG64 First, Last, Page, Leaf;
    PULONG64 Table;
    BOOLEAN Mapped;

    First = Physical & ~(DART_PAGE_SIZE - 1);
    Last = (Physical + Length + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1);
    if (!Length || Last <= First || (Iova & (DART_PAGE_SIZE - 1)) != (Physical & (DART_PAGE_SIZE - 1)))
        return STATUS_INVALID_PARAMETER;
    Iova &= ~(DART_PAGE_SIZE - 1);
    if (!AppleDartAdoptedRangeFree(Adopted, Iova, (SIZE_T)(Last - First)))
        return STATUS_CONFLICTING_ADDRESSES;
    for (Page = 0; Page < Last - First; Page += DART_PAGE_SIZE)
    {
        Leaf = AppleDartAdoptedWalk(Adopted, Iova + Page, TRUE);
        Table = Leaf ? AppleDartAdoptedTable(Adopted, Leaf, &Mapped) : NULL;
        if (!Table)
        {
            DbgPrint("%s: DART stream %lu has no table for device address %I64x\n", Adopted->Owner, Adopted->Sid,
                     Iova + Page);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Table[((Iova + Page) >> DART_PAGE_SHIFT) & (DART_TABLE_ENTRIES - 1)] =
            AppleDartEntry(First + Page) | DART_PTE_SUBPAGE_END | DART_PTE_VALID;
        AppleDartAdoptedTableDone(Table, Mapped);
    }
    AppleDartFlush(Adopted->Base, Adopted->Sid, Adopted->Owner);
    DbgPrint("%s: DART stream %lu maps %I64x+%Ix at device address %I64x\n", Adopted->Owner, Adopted->Sid, Physical,
             Length, Iova + (Physical - First));
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleDartAdoptedMap(
    _Inout_ PAPPLE_DART_ADOPTED Adopted,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length,
    _Out_ PULONG64 Iova)
{
    ULONG64 First, Span, Start;
    NTSTATUS Status;

    *Iova = 0;
    First = Physical & ~(DART_PAGE_SIZE - 1);
    Span = ((Physical + Length + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1)) - First;
    if (!Length || !Span)
        return STATUS_INVALID_PARAMETER;
    for (Start = Adopted->NextIova; Start + Span <= Adopted->EndIova; Start += DART_PAGE_SIZE)
    {
        if (!AppleDartAdoptedRangeFree(Adopted, Start, (SIZE_T)Span))
            continue;
        Status = AppleDartAdoptedMapAt(Adopted, Start + (Physical - First), Physical, Length);
        if (!NT_SUCCESS(Status))
            return Status;
        Adopted->NextIova = Start + Span;
        *Iova = Start + (Physical - First);
        return STATUS_SUCCESS;
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID
NTAPI
AppleDartAdoptedRelease(
    _Inout_ PAPPLE_DART_ADOPTED Adopted)
{
    ULONG Index;

    for (Index = 0; Index < Adopted->ForeignTables; ++Index)
        MmUnmapIoSpace(Adopted->ForeignVirtual[Index], DART_PAGE_SIZE);
    Adopted->ForeignTables = 0;
    if (Adopted->Base)
        MmUnmapIoSpace(Adopted->Base, Adopted->BaseSize);
    Adopted->Base = NULL;
}

static
PULONG64
AppleDartLeafEntry(
    _In_ PDART_IDENTITY Identity,
    _In_ ULONG64 Iova)
{
    PDART_TABLE Table = Identity->Top;
    ULONG Level, Slot;

    for (Level = Identity->Levels; Table && Level > 1; --Level)
    {
        Slot = (ULONG)(Iova >> (DART_PAGE_SHIFT + (Level - 1) * DART_LEVEL_BITS)) & (DART_TABLE_ENTRIES - 1);
        Table = Table->Children[Slot];
    }
    return Table ? &Table->Entries[(Iova >> DART_PAGE_SHIFT) & (DART_TABLE_ENTRIES - 1)] : NULL;
}

static
VOID
AppleDartFreeTable(
    _In_ PDART_TABLE Table)
{
    ULONG Slot;

    if (Table->Children)
    {
        for (Slot = 0; Slot < DART_TABLE_ENTRIES; ++Slot)
        {
            if (Table->Children[Slot])
                AppleDartFreeTable(Table->Children[Slot]);
        }
        ExFreePoolWithTag(Table->Children, DART_TAG);
    }
    MmFreeContiguousMemorySpecifyCache(Table->Entries, DART_PAGE_SIZE, MmCached);
    ExFreePoolWithTag(Table, DART_TAG);
}

static
VOID
AppleDartDomainFlush(
    _In_ PAPPLE_DART_DOMAIN Domain)
{
    ULONG Index;

    KeMemoryBarrier();
    for (Index = 0; Index < Domain->Streams; ++Index)
        AppleDartFlush(Domain->Stream[Index].Base, Domain->Stream[Index].Sid, Domain->Owner);
}

static
VOID
AppleDartDomainRelease(
    _Inout_ PAPPLE_DART_DOMAIN Domain)
{
    PDART_IDENTITY Tables = Domain->Tables;
    ULONG Index;

    for (Index = 0; Index < Domain->Streams; ++Index)
        MmUnmapIoSpace(Domain->Stream[Index].Base, Domain->Stream[Index].BaseSize);
    if (Tables)
    {
        if (Tables->Top)
            AppleDartFreeTable(Tables->Top);
        ExFreePoolWithTag(Tables, DART_TAG);
    }
    RtlZeroMemory(Domain, sizeof(*Domain));
}

NTSTATUS
NTAPI
AppleDartDomainCreate(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Owner,
    _Out_ PAPPLE_DART_DOMAIN Domain)
{
    PAPPLE_DART_DOMAIN_STREAM Stream;
    const VOID *Iommus, *Compatible;
    ULONG Length, CompatibleLength, Index, Dart, Cells, Size, Params3, Streams, Protect, InputBits, Ttbr, Tcr;
    ULONG64 Phandle, Sid, Physical;
    PDART_IDENTITY Tables;
    PUCHAR Base;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    RtlZeroMemory(Domain, sizeof(*Domain));
    Domain->Owner = Owner;
    Iommus = FdtGetProperty(&Tree->Fdt, Node, "iommus", &Length);
    if (!Iommus)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; NT_SUCCESS(Status) && FdtReadCells(Iommus, Length, Index, 1, &Phandle); Index += 2)
    {
        Dart = FdtDriverFindPhandle(Tree, (ULONG)Phandle);
        if (Dart == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1 ||
            !FdtReadCells(Iommus, Length, Index + 1, 1, &Sid) || Domain->Streams == APPLE_DART_DOMAIN_STREAMS)
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }
        Compatible = FdtGetProperty(&Tree->Fdt, Dart, "compatible", &CompatibleLength);
        if (!FdtStringListContains(Compatible, CompatibleLength, "apple,t8110-dart"))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }
        Base = FdtDriverMapReg(Tree, Dart, 0, &Physical, &Size);
        if (!Base)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        Stream = &Domain->Stream[Domain->Streams++];
        Stream->Base = Base;
        Stream->BaseSize = Size;
        Stream->Sid = (ULONG)Sid;
        Stream->Physical = Physical;
        Params3 = AppleDartRead(Base, DART_T8110_PARAMS3);
        Streams = AppleDartRead(Base, DART_T8110_PARAMS4) & 0x1ff;
        Protect = AppleDartRead(Base, DART_T8110_PROTECT);
        InputBits = (Params3 >> 16) & 0x3f;
        DbgPrint("%s: DART %I64x stream %lu of %lu: params %08lx, %lu-bit addresses, protect %08lx\n", Owner, Physical,
                 Stream->Sid, Streams, Params3, InputBits, Protect);
        if (Stream->Sid >= Streams)
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        else if (Protect & DART_T8110_PROTECT_TCR)
            Status = STATUS_ACCESS_DENIED;
        else if (InputBits < DART_PAGE_SHIFT + DART_LEVEL_BITS + 1 || InputBits > DART_PAGE_SHIFT + 3 * DART_LEVEL_BITS ||
                 (Domain->InputBits && InputBits != Domain->InputBits))
            Status = STATUS_NOT_SUPPORTED;
        Domain->InputBits = InputBits;
    }
    if (NT_SUCCESS(Status) && !Domain->Streams)
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    if (NT_SUCCESS(Status))
    {
        Tables = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Tables), DART_TAG);
        if (Tables)
        {
            RtlZeroMemory(Tables, sizeof(*Tables));
            Tables->InputBits = Domain->InputBits;
            Tables->Levels = (Domain->InputBits > DART_PAGE_SHIFT + 2 * DART_LEVEL_BITS) ? 3 : 2;
            Domain->Tables = Tables;
            Tables->Top = AppleDartAllocateTable(FALSE, &Tables->TopPhysical);
        }
        if (!Tables || !Tables->Top)
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("%s: DART domain over %lu streams not created: %lx\n", Owner, Domain->Streams, Status);
        AppleDartDomainRelease(Domain);
        return Status;
    }
    Domain->Levels = Tables->Levels;
    Domain->TopPhysical = Tables->TopPhysical;
    Ttbr = (ULONG)((Domain->TopPhysical >> DART_PAGE_SHIFT) << 2) | DART_T8110_TTBR_VALID;
    Tcr = DART_T8110_TCR_TRANSLATE | (Domain->Levels == 3 ? DART_T8110_TCR_FOUR_LEVEL : 0);
    ASSERT(((ULONG64)(Ttbr >> 2) << DART_PAGE_SHIFT) == Domain->TopPhysical);
    for (Index = 0; Index < Domain->Streams; ++Index)
    {
        Stream = &Domain->Stream[Index];
        AppleDartWrite(Stream->Base, DART_T8110_TCR + 4 * Stream->Sid, 0);
        AppleDartWrite(Stream->Base, DART_T8110_TTBR + 4 * Stream->Sid, Ttbr);
        AppleDartWrite(Stream->Base, DART_T8110_ENABLE_STREAMS + 4 * (Stream->Sid >> 5),
                       AppleDartRead(Stream->Base, DART_T8110_ENABLE_STREAMS + 4 * (Stream->Sid >> 5)) |
                           (1UL << (Stream->Sid & 31)));
        AppleDartFlush(Stream->Base, Stream->Sid, Owner);
        AppleDartWrite(Stream->Base, DART_T8110_TCR + 4 * Stream->Sid, Tcr);
    }
    DbgPrint("%s: DART domain of %lu streams translates %lu-bit addresses through %lu levels at %I64x\n", Owner,
             Domain->Streams, Domain->InputBits, Domain->Levels, Domain->TopPhysical);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleDartDomainMap(
    _Inout_ PAPPLE_DART_DOMAIN Domain,
    _In_ ULONG64 Iova,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length)
{
    PDART_IDENTITY Tables = Domain->Tables;
    ULONG64 Span, Offset;
    PULONG64 Entry;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    Span = ((ULONG64)Length + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1);
    if (!Tables || !Length || !Span || ((Iova | Physical) & (DART_PAGE_SIZE - 1)) || Iova + Span < Iova ||
        ((Iova + Span - 1) >> Domain->InputBits) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Offset = 0; Offset < Span; Offset += DART_PAGE_SIZE)
    {
        Entry = AppleDartLeafEntry(Tables, Iova + Offset);
        if (Entry && (*Entry & DART_PTE_VALID))
            return STATUS_CONFLICTING_ADDRESSES;
    }
    for (Offset = 0; NT_SUCCESS(Status) && Offset < Span; Offset += DART_PAGE_SIZE)
        Status = AppleDartMapPage(Tables, Iova + Offset, Physical + Offset);
    if (!NT_SUCCESS(Status))
    {
        while (Offset)
        {
            Offset -= DART_PAGE_SIZE;
            AppleDartUnmapPage(Tables, Iova + Offset);
        }
    }
    AppleDartDomainFlush(Domain);
    return Status;
}

VOID
NTAPI
AppleDartDomainUnmap(
    _Inout_ PAPPLE_DART_DOMAIN Domain,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length)
{
    PDART_IDENTITY Tables = Domain->Tables;
    ULONG64 Span, Offset;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT((Iova & (DART_PAGE_SIZE - 1)) == 0);
    Span = ((ULONG64)Length + DART_PAGE_SIZE - 1) & ~(DART_PAGE_SIZE - 1);
    if (!Tables)
        return;
    for (Offset = 0; Offset < Span; Offset += DART_PAGE_SIZE)
        AppleDartUnmapPage(Tables, Iova + Offset);
    AppleDartDomainFlush(Domain);
}

BOOLEAN
NTAPI
AppleDartDomainTranslate(
    _In_ PAPPLE_DART_DOMAIN Domain,
    _In_ ULONG64 Iova,
    _Out_ PULONG64 Physical)
{
    PULONG64 Entry;

    *Physical = 0;
    if (!Domain->Tables)
        return FALSE;
    Entry = AppleDartLeafEntry(Domain->Tables, Iova);
    if (!Entry || !(*Entry & DART_PTE_VALID))
        return FALSE;
    *Physical = ((*Entry & DART_PTE_ADDRESS_MASK) << 4) | (Iova & (DART_PAGE_SIZE - 1));
    return TRUE;
}

VOID
NTAPI
AppleDartDomainDestroy(
    _Inout_ PAPPLE_DART_DOMAIN Domain)
{
    PAPPLE_DART_DOMAIN_STREAM Stream;
    ULONG Index;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    for (Index = 0; Index < Domain->Streams; ++Index)
    {
        Stream = &Domain->Stream[Index];
        AppleDartWrite(Stream->Base, DART_T8110_TCR + 4 * Stream->Sid, 0);
        AppleDartWrite(Stream->Base, DART_T8110_TTBR + 4 * Stream->Sid, 0);
        AppleDartFlush(Stream->Base, Stream->Sid, Domain->Owner);
    }
    AppleDartDomainRelease(Domain);
}
