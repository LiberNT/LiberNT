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
#define DART_TABLES_PER_CHUNK       64
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
    PUCHAR Chunk;
    ULONG ChunkUsed;
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
    _Inout_ PDART_IDENTITY Identity,
    _In_ BOOLEAN Leaf,
    _Out_ PULONG64 Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary, Address;
    SIZE_T Size = (DART_TABLES_PER_CHUNK + 1) * DART_PAGE_SIZE;
    PDART_TABLE Table;

    if (!Identity->Chunk || Identity->ChunkUsed == DART_TABLES_PER_CHUNK)
    {
        Low.QuadPart = 0;
        High.QuadPart = -1;
        Boundary.QuadPart = 0;
        Identity->Chunk = MmAllocateContiguousMemorySpecifyCache(Size, Low, High, Boundary, MmCached);
        if (!Identity->Chunk)
            return NULL;
        RtlZeroMemory(Identity->Chunk, Size);
        Address = MmGetPhysicalAddress(Identity->Chunk);
        Identity->ChunkUsed = 0;
        if ((ULONG64)Address.QuadPart & (DART_PAGE_SIZE - 1))
            Identity->Chunk += DART_PAGE_SIZE - ((ULONG64)Address.QuadPart & (DART_PAGE_SIZE - 1));
    }
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
    Table->Entries = (PULONG64)(Identity->Chunk + Identity->ChunkUsed++ * DART_PAGE_SIZE);
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
            Child = AppleDartAllocateTable(Identity, Level == 2, &TablePhysical);
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
        return STATUS_NOT_SUPPORTED;
    Ranges = MmGetPhysicalMemoryRanges();
    if (!Ranges)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Ranges[Index].NumberOfBytes.QuadPart; ++Index)
    {
        Lowest = min(Lowest, (ULONG64)Ranges[Index].BaseAddress.QuadPart);
        Highest = max(Highest, (ULONG64)Ranges[Index].BaseAddress.QuadPart + Ranges[Index].NumberOfBytes.QuadPart);
    }
    Mask = (1ULL << InputBits) - 1;
    if (!Index || Highest > (1ULL << min(OutputBits, 42)) || ((Lowest ^ (Highest - 1)) & ~Mask))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Done;
    }
    Identity->InputBits = InputBits;
    Identity->Levels = (InputBits > DART_PAGE_SHIFT + 2 * DART_LEVEL_BITS) ? 3 : 2;
    Identity->Top = AppleDartAllocateTable(Identity, FALSE, &Identity->TopPhysical);
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
                goto Done;
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

    Compatible = RiscvFdtGetProperty(&Tree->Fdt, Dart, "compatible", &Length);
    if (!RiscvFdtStringListContains(Compatible, Length, "apple,t8110-dart"))
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

    if (Dart == RISCV_FDT_NO_NODE || !RiscvFdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1)
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

    Iommus = RiscvFdtGetProperty(&Tree->Fdt, Node, "iommus", &Length);
    if (!Iommus)
        return STATUS_SUCCESS;
    for (Index = 0; RiscvFdtReadCells(Iommus, Length, Index, 1, &Phandle); Index += 1 + Cells)
    {
        Dart = FdtDriverFindPhandle(Tree, (ULONG)Phandle);
        if (Dart == RISCV_FDT_NO_NODE || !RiscvFdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1 ||
            !RiscvFdtReadCells(Iommus, Length, Index + 1, 1, &Sid))
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
    if (Dart == RISCV_FDT_NO_NODE || !Length || !RiscvFdtReadU32(&Tree->Fdt, Dart, "#iommu-cells", &Cells) || Cells != 1)
        return STATUS_INVALID_PARAMETER;
    Compatible = RiscvFdtGetProperty(&Tree->Fdt, Dart, "compatible", &RegisterLength);
    if (!RiscvFdtStringListContains(Compatible, RegisterLength, "apple,t8110-dart"))
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
        MmUnmapIoSpace(Base, Size);
        return Status;
    }

    if (!AppleDartIdentity.Top)
        Status = AppleDartBuildIdentity(&AppleDartIdentity, InputBits, OutputBits);
    else if (AppleDartIdentity.InputBits != InputBits)
        Status = STATUS_NOT_SUPPORTED;
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
    MmUnmapIoSpace(Base, Size);
    return Status;
}
