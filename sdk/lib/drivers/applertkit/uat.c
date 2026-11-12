/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Address translations of the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#include <reactos/arm64/sysreg.h>
#endif
#include <reactos/drivers/appleuat.h>

#define UAT_TAG                     'tUpA'
#define UAT_TYPE_MASK               0x3ULL
#define UAT_TYPE_ENTRY              0x3ULL
#define UAT_MEMORY_SHIFT            2
#define UAT_MEMORY_MASK             (0x7ULL << UAT_MEMORY_SHIFT)
#define UAT_ACCESS_SHIFT            6
#define UAT_ACCESS_MASK             (0x3ULL << UAT_ACCESS_SHIFT)
#define UAT_ACCESSED                (1ULL << 10)
#define UAT_NOT_GLOBAL              (1ULL << 11)
#define UAT_HIGH_SHIFT              52
#define UAT_HIGH_GPU                (1ULL << 3)
#define UAT_HIGH_READ               (1ULL << 1)
#define UAT_HIGH_WRITE              (1ULL << 2)

#define UAT_HANDOFF_MAGIC_AP        0x000
#define UAT_HANDOFF_MAGIC_FW        0x008
#define UAT_HANDOFF_LOCK_AP         0x010
#define UAT_HANDOFF_LOCK_FW         0x011
#define UAT_HANDOFF_TURN            0x014
#define UAT_HANDOFF_SLOT            0x018
#define UAT_HANDOFF_FLUSH           0x020
#define UAT_HANDOFF_FLUSH_STRIDE    0x018
#define UAT_HANDOFF_FLUSH_SLOTS     (APPLE_UAT_CONTEXTS + 1)
#define UAT_HANDOFF_PRIVATE         0x640
#define UAT_HANDOFF_MAGIC           0x4b1d000000000002ULL
#define UAT_HANDOFF_NO_SLOT         0xffffffffUL
#define UAT_HANDOFF_TIMEOUT_MS      1000
#define UAT_HANDOFF_POLL_MS         10
#define UAT_HANDOFF_SPIN_US         1
#define UAT_CONTEXT_VALID           1ULL
#define UAT_CONTEXT_ASID_SHIFT      48
#define UAT_HOST_INDEX \
    ((ULONG)((APPLE_UAT_HOST_BASE & APPLE_UAT_ADDRESS_MASK) >> \
             (APPLE_UAT_PAGE_SHIFT + (APPLE_UAT_LEVELS - 1) * APPLE_UAT_LEVEL_BITS)))

#ifndef UNIT_TEST
static
VOID
UatSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}
#endif

#ifndef UNIT_TEST
static
VOID
UatInvalidateAsid(
    _In_ ULONG Asid)
{
    ULONG64 Operand = (ULONG64)Asid << UAT_CONTEXT_ASID_SHIFT;

#if defined(_MSC_VER) && !defined(__clang__)
    ARM64_DSB(SY);
    ARM64_TLBI(ASIDE1OS, Operand);
    ARM64_DSB(SY);
    ARM64_ISB();
#else
    __asm__ __volatile__(".arch armv8.4-a\n\tdsb sy\n\ttlbi aside1os, %0\n\tdsb sy\n\tisb"
                         : : "r"(Operand) : "memory");
#endif
}
#endif

#ifndef UNIT_TEST
VOID
NTAPI
AppleUatInvalidateAll(VOID)
{
#if defined(_MSC_VER) && !defined(__clang__)
    ARM64_DSB(SY);
    ARM64_TLBI_ALL(VMALLE1OS);
    ARM64_DSB(SY);
    ARM64_ISB();
#else
    __asm__ __volatile__(".arch armv8.4-a\n\tdsb sy\n\ttlbi vmalle1os\n\tdsb sy\n\tisb" : : : "memory");
#endif
}
#endif

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

    if (Protection->Read)
        High |= UAT_HIGH_READ;
    if (Protection->Write)
        High |= UAT_HIGH_WRITE;
    return (Physical & APPLE_UAT_OUTPUT_MASK) | UAT_TYPE_ENTRY | UAT_ACCESSED |
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
    Protection->Read = (High & UAT_HIGH_READ) != 0;
    Protection->Write = (High & UAT_HIGH_WRITE) != 0;
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
    Table->Entries = MmAllocateContiguousMemorySpecifyCache(APPLE_UAT_PAGE_SIZE, Low, High, Boundary, MmNonCached);
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
    MmFreeContiguousMemorySpecifyCache(Table->Entries, APPLE_UAT_PAGE_SIZE, MmNonCached);
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
    ULONG Entries = Level == APPLE_UAT_LEVELS ? APPLE_UAT_ROOT_ENTRIES : APPLE_UAT_TABLE_ENTRIES;

    return (ULONG)(Address >> (APPLE_UAT_PAGE_SHIFT + (Level - 1) * APPLE_UAT_LEVEL_BITS)) & (Entries - 1);
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
            Table->Entries[Slot] = (Child->Physical & APPLE_UAT_OUTPUT_MASK) | UAT_TYPE_ENTRY;
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

    Address &= APPLE_UAT_ADDRESS_MASK;
    if (!Length || (Address & (APPLE_UAT_PAGE_SIZE - 1)) || (Physical & (APPLE_UAT_PAGE_SIZE - 1)) ||
        (Length & (APPLE_UAT_PAGE_SIZE - 1)) || Length > APPLE_UAT_ADDRESS_MASK + 1 - Address ||
        Physical >= (1ULL << APPLE_UAT_OUTPUT_BITS) ||
        Length > (1ULL << APPLE_UAT_OUTPUT_BITS) - Physical)
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
    Address &= APPLE_UAT_ADDRESS_MASK;
    Table = AppleUatWalk((PAPPLE_UAT)Uat, Address, FALSE);
    if (!Table)
        return FALSE;
    Entry = Table->Entries[AppleUatSlot(Address, 1)];
    if ((Entry & UAT_TYPE_MASK) != UAT_TYPE_ENTRY)
        return FALSE;
    *Physical = (Entry & APPLE_UAT_OUTPUT_MASK) | (Address & (APPLE_UAT_PAGE_SIZE - 1));
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

    Address &= APPLE_UAT_ADDRESS_MASK;
    if (!Length || (Address & (APPLE_UAT_PAGE_SIZE - 1)) || (Length & (APPLE_UAT_PAGE_SIZE - 1)) ||
        Length > APPLE_UAT_ADDRESS_MASK + 1 - Address)
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

static
volatile ULONG64 *
AppleUatHandoffQuad(
    _In_ PUCHAR Handoff,
    _In_ ULONG Offset)
{
    return (volatile ULONG64 *)(Handoff + Offset);
}

/* The firmware and the host take turns over the handoff page, each watching the other's flag. */
static
BOOLEAN
AppleUatHandoffLock(
    _In_ PAPPLE_UAT_ADOPTED Adopted)
{
    volatile UCHAR *Own = (volatile UCHAR *)(Adopted->Handoff + UAT_HANDOFF_LOCK_AP);
    volatile UCHAR *Theirs = (volatile UCHAR *)(Adopted->Handoff + UAT_HANDOFF_LOCK_FW);
    volatile ULONG *Turn = (volatile ULONG *)(Adopted->Handoff + UAT_HANDOFF_TURN);
    ULONG Spins = UAT_HANDOFF_TIMEOUT_MS * 1000 / UAT_HANDOFF_SPIN_US;

    *Own = 1;
    KeMemoryBarrier();
    while (*Theirs != 0)
    {
        if (Spins-- == 0)
        {
            *Own = 0;
            DbgPrint("%s: the graphics firmware keeps the handoff page\n", Adopted->Owner);
            return FALSE;
        }
        if (*Turn != 0)
        {
            *Own = 0;
            KeStallExecutionProcessor(UAT_HANDOFF_SPIN_US);
            *Own = 1;
            KeMemoryBarrier();
        }
        else
        {
            KeStallExecutionProcessor(UAT_HANDOFF_SPIN_US);
        }
    }
    KeMemoryBarrier();
    return TRUE;
}

static
VOID
AppleUatHandoffUnlock(
    _In_ PAPPLE_UAT_ADOPTED Adopted)
{
    *(volatile ULONG *)(Adopted->Handoff + UAT_HANDOFF_TURN) = 1;
    KeMemoryBarrier();
    *(volatile UCHAR *)(Adopted->Handoff + UAT_HANDOFF_LOCK_AP) = 0;
}

NTSTATUS
NTAPI
AppleUatAdopt(
    _Out_ PAPPLE_UAT_ADOPTED Adopted,
    _In_ ULONG64 Handoff,
    _In_ ULONG64 Contexts,
    _In_ ULONG64 KernelRoot,
    _In_ ULONG64 HostLevel,
    _In_z_ PCSTR Owner)
{
    PHYSICAL_ADDRESS Address;
    NTSTATUS Status;

    RtlZeroMemory(Adopted, sizeof(*Adopted));
    Adopted->Owner = Owner;
    if (!Handoff || !Contexts || !KernelRoot || !HostLevel ||
        ((Handoff | Contexts | KernelRoot | HostLevel) & (APPLE_UAT_PAGE_SIZE - 1)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Adopted->KernelRootPhysical = KernelRoot;
    Adopted->HostLevelPhysical = HostLevel;
    Address.QuadPart = (LONGLONG)Handoff;
    Adopted->Handoff = MmMapIoSpace(Address, APPLE_UAT_PAGE_SIZE, MmNonCached);
    Address.QuadPart = (LONGLONG)Contexts;
    Adopted->Contexts = MmMapIoSpace(Address, APPLE_UAT_PAGE_SIZE, MmNonCached);
    Address.QuadPart = (LONGLONG)KernelRoot;
    Adopted->KernelRoot = MmMapIoSpace(Address, APPLE_UAT_PAGE_SIZE, MmNonCached);
    Address.QuadPart = (LONGLONG)HostLevel;
    Adopted->HostLevel = MmMapIoSpace(Address, APPLE_UAT_PAGE_SIZE, MmNonCached);
    if (!Adopted->Handoff || !Adopted->Contexts || !Adopted->KernelRoot || !Adopted->HostLevel)
    {
        AppleUatAdoptedRelease(Adopted);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = AppleUatCreate(&Adopted->Lower, FALSE, Owner);
    if (!NT_SUCCESS(Status))
    {
        AppleUatAdoptedRelease(Adopted);
        return Status;
    }
    Status = AppleUatCreate(&Adopted->Host, TRUE, Owner);
    if (!NT_SUCCESS(Status))
    {
        AppleUatAdoptedRelease(Adopted);
        return Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleUatAdoptedHandshake(
    _Inout_ PAPPLE_UAT_ADOPTED Adopted)
{
    ULONG Index, Waited;
    BOOLEAN Answered = FALSE;

    if (!Adopted->Handoff)
        return STATUS_INVALID_DEVICE_STATE;
    *AppleUatHandoffQuad(Adopted->Handoff, UAT_HANDOFF_MAGIC_AP) = UAT_HANDOFF_MAGIC;
    *(volatile ULONG *)(Adopted->Handoff + UAT_HANDOFF_SLOT) = UAT_HANDOFF_NO_SLOT;
    *AppleUatHandoffQuad(Adopted->Handoff, UAT_HANDOFF_PRIVATE) = 0;
    KeMemoryBarrier();
    for (Waited = 0; Waited < UAT_HANDOFF_TIMEOUT_MS; Waited += UAT_HANDOFF_POLL_MS)
    {
        if (!AppleUatHandoffLock(Adopted))
            return STATUS_IO_TIMEOUT;
        Answered = *AppleUatHandoffQuad(Adopted->Handoff, UAT_HANDOFF_MAGIC_FW) == UAT_HANDOFF_MAGIC;
        AppleUatHandoffUnlock(Adopted);
        if (Answered)
            break;
        UatSleepMs(UAT_HANDOFF_POLL_MS);
    }
    if (!Answered)
    {
        DbgPrint("%s: the graphics firmware did not answer on the handoff page\n", Adopted->Owner);
        return STATUS_DEVICE_NOT_READY;
    }
    for (Index = 0; Index < UAT_HANDOFF_FLUSH_SLOTS * UAT_HANDOFF_FLUSH_STRIDE / sizeof(ULONG64); ++Index)
        *AppleUatHandoffQuad(Adopted->Handoff, UAT_HANDOFF_FLUSH + Index * sizeof(ULONG64)) = 0;
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* The firmware owns the first two entries of its own root; everything above them is the host's. */
NTSTATUS
NTAPI
AppleUatAdoptedInstall(
    _Inout_ PAPPLE_UAT_ADOPTED Adopted)
{
    ULONG Index;

    if (!Adopted->Handoff || !Adopted->Contexts || !Adopted->KernelRoot || !Adopted->HostLevel)
        return STATUS_INVALID_DEVICE_STATE;
    if (!AppleUatHandoffLock(Adopted))
        return STATUS_IO_TIMEOUT;
    for (Index = UAT_HOST_INDEX; Index < APPLE_UAT_ROOT_ENTRIES; ++Index)
        Adopted->KernelRoot[Index] = 0;
    for (Index = 0; Index < APPLE_UAT_TABLE_ENTRIES; ++Index)
        Adopted->HostLevel[Index] = 0;
    KeMemoryBarrier();
    /* FIXME: a firmware already running needs its translations invalidated here. */
    Adopted->KernelRoot[UAT_HOST_INDEX] = (Adopted->HostLevelPhysical & APPLE_UAT_OUTPUT_MASK) | UAT_TYPE_ENTRY;
    for (Index = 2; Index < 2 * APPLE_UAT_CONTEXTS; ++Index)
        Adopted->Contexts[Index] = 0;
    Adopted->Contexts[0] = (Adopted->Lower.TopPhysical & APPLE_UAT_OUTPUT_MASK) | UAT_CONTEXT_VALID;
    Adopted->Contexts[1] = (Adopted->KernelRootPhysical & APPLE_UAT_OUTPUT_MASK) | UAT_CONTEXT_VALID;
    KeMemoryBarrier();
    AppleUatHandoffUnlock(Adopted);
    return STATUS_SUCCESS;
}

/* The window's own level lives in firmware memory, so the addresses built in RAM are copied into it. */
static
VOID
AppleUatAdoptedPublish(
    _Inout_ PAPPLE_UAT_ADOPTED Adopted)
{
    PAPPLE_UAT_TABLE Level = Adopted->Host.Top->Children[UAT_HOST_INDEX];
    ULONG Index;

    if (!Level)
        return;
    for (Index = 0; Index < APPLE_UAT_TABLE_ENTRIES; ++Index)
        Adopted->HostLevel[Index] = Level->Entries[Index];
    KeMemoryBarrier();
}

NTSTATUS
NTAPI
AppleUatAdoptedMap(
    _Inout_ PAPPLE_UAT_ADOPTED Adopted,
    _In_ ULONG64 Address,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length,
    _In_ const APPLE_UAT_PROTECTION *Protection)
{
    NTSTATUS Status;

    if (!Adopted->HostLevel)
        return STATUS_INVALID_DEVICE_STATE;
    if (Address < APPLE_UAT_HOST_BASE || Address - APPLE_UAT_HOST_BASE >= APPLE_UAT_HOST_SIZE ||
        Length > APPLE_UAT_HOST_SIZE - (Address - APPLE_UAT_HOST_BASE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = AppleUatMap(&Adopted->Host, Address, Physical, Length, Protection);
    if (NT_SUCCESS(Status))
        AppleUatAdoptedPublish(Adopted);
    return Status;
}

BOOLEAN
NTAPI
AppleUatAdoptedTranslate(
    _In_ PAPPLE_UAT_ADOPTED Adopted,
    _In_ ULONG64 Address,
    _Out_ PULONG64 Physical)
{
    return AppleUatTranslate(&Adopted->Host, Address, Physical, NULL);
}

VOID
NTAPI
AppleUatAdoptedRelease(
    _Inout_ PAPPLE_UAT_ADOPTED Adopted)
{
    if (Adopted->Host.Top)
        AppleUatDestroy(&Adopted->Host);
    if (Adopted->Lower.Top)
        AppleUatDestroy(&Adopted->Lower);
    if (Adopted->HostLevel)
        MmUnmapIoSpace(Adopted->HostLevel, APPLE_UAT_PAGE_SIZE);
    Adopted->HostLevel = NULL;
    if (Adopted->KernelRoot)
        MmUnmapIoSpace(Adopted->KernelRoot, APPLE_UAT_PAGE_SIZE);
    Adopted->KernelRoot = NULL;
    if (Adopted->Contexts)
        MmUnmapIoSpace(Adopted->Contexts, APPLE_UAT_PAGE_SIZE);
    Adopted->Contexts = NULL;
    if (Adopted->Handoff)
        MmUnmapIoSpace(Adopted->Handoff, APPLE_UAT_PAGE_SIZE);
    Adopted->Handoff = NULL;
}

NTSTATUS
NTAPI
AppleUatAdoptedBindSlot(
    _Inout_ PAPPLE_UAT_ADOPTED Adopted,
    _In_ ULONG Slot,
    _In_ ULONG64 RootPhysical)
{
    ULONG64 Asid = (ULONG64)Slot << UAT_CONTEXT_ASID_SHIFT;

    if (!Adopted->Handoff || !Adopted->Contexts || !Slot || Slot >= APPLE_UAT_CONTEXTS || !RootPhysical ||
        (RootPhysical & (APPLE_UAT_PAGE_SIZE - 1)) || (RootPhysical & ~APPLE_UAT_OUTPUT_MASK))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!AppleUatHandoffLock(Adopted))
        return STATUS_IO_TIMEOUT;
    Adopted->Contexts[2 * Slot] = RootPhysical | UAT_CONTEXT_VALID | Asid;
    Adopted->Contexts[2 * Slot + 1] = (Adopted->KernelRootPhysical & APPLE_UAT_OUTPUT_MASK) | UAT_CONTEXT_VALID | Asid;
    KeMemoryBarrier();
    AppleUatHandoffUnlock(Adopted);
    UatInvalidateAsid(Slot);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleUatAdoptedUnbindSlot(
    _Inout_ PAPPLE_UAT_ADOPTED Adopted,
    _In_ ULONG Slot)
{
    if (!Adopted->Handoff || !Adopted->Contexts || !Slot || Slot >= APPLE_UAT_CONTEXTS)
        return STATUS_INVALID_PARAMETER;
    if (!AppleUatHandoffLock(Adopted))
        return STATUS_IO_TIMEOUT;
    Adopted->Contexts[2 * Slot] = 0;
    Adopted->Contexts[2 * Slot + 1] = 0;
    KeMemoryBarrier();
    AppleUatHandoffUnlock(Adopted);
    UatInvalidateAsid(Slot);
    return STATUS_SUCCESS;
}
