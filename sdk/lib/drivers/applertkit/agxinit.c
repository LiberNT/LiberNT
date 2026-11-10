/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Initial data the Apple silicon graphics firmware is started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appleadt.h>
#include <reactos/drivers/appleuat.h>
#include <reactos/drivers/appleagxinit.h>

#define AGX_INIT_TAG                'iGpA'
#define AGX_CHANNEL_STATE_BASE      0xfffffc2040003fd0ULL
#define AGX_CHANNEL_RING_BASE       0xfffffc2000002800ULL
#define AGX_CHANNEL_STRIDE          0x44000ULL

/* Every object sits at the address this firmware build was linked to look for it at. */
static const APPLE_AGX_LAYOUT AgxFixed[APPLE_AGX_FIXED_OBJECTS] =
{
    { 0xfffffc204062ff40ULL, APPLE_AGX_ROOT_SIZE,         APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2040670000ULL, APPLE_AGX_REGION_A_SIZE,     APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20406b7b4dULL, APPLE_AGX_RUNTIME_SIZE,      APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20406fb5fcULL, APPLE_AGX_HWDATA_SIZE,       APPLE_AGX_CLASS_FIRMWARE_CACHED },
    { 0xfffffc2040747f00ULL, APPLE_AGX_PAIR_SIZE,         APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc204078bff0ULL, APPLE_AGX_SMALL_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2070003000ULL, 0x1000,                      APPLE_AGX_CLASS_SHARED },
    { 0xfffffc2070008000ULL, 0x4000,                      APPLE_AGX_CLASS_SHARED },
    { 0xfffffc2070010000ULL, 0x4000,                      APPLE_AGX_CLASS_SHARED },
    { 0xfffffc20407cc204ULL, APPLE_AGX_GLOBALS_SIZE,      APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc204081370cULL, APPLE_AGX_POWER_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20003bbc30ULL, APPLE_AGX_CONTROL_SIZE,      APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2040857fc0ULL, APPLE_AGX_FLAGS_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2ffc000000ULL, APPLE_AGX_TIMESTAMPS_SIZE,   APPLE_AGX_CLASS_SHARED },
    { 0xfffffc2040333fd0ULL, APPLE_AGX_STATE_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2000330800ULL, APPLE_AGX_DEVCTL_RING_SIZE,  APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2040377fd0ULL, APPLE_AGX_STATE_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20403b8800ULL, APPLE_AGX_EVENT_RING_SIZE,   APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20403ffee0ULL, APPLE_AGX_LOG_RINGS * APPLE_AGX_LOG_STATE_SIZE, APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2040443000ULL, APPLE_AGX_LOG_RING_SIZE,     APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20404d7fd0ULL, APPLE_AGX_STATE_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2040519000ULL, APPLE_AGX_TRACE_RING_SIZE,   APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2040563fd0ULL, APPLE_AGX_STATE_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20405a4000ULL, APPLE_AGX_STATS_RING_SIZE,   APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc20405ebfd0ULL, APPLE_AGX_STATE_SIZE,        APPLE_AGX_CLASS_FIRMWARE },
    { 0xfffffc2000376c00ULL, APPLE_AGX_FWCTL_RING_SIZE,   APPLE_AGX_CLASS_FIRMWARE },
};

BOOLEAN
NTAPI
AppleAgxLayout(
    _In_ ULONG Object,
    _Out_ PAPPLE_AGX_LAYOUT Layout)
{
    ULONG Index;

    RtlZeroMemory(Layout, sizeof(*Layout));
    if (Object < APPLE_AGX_FIXED_OBJECTS)
    {
        *Layout = AgxFixed[Object];
        return TRUE;
    }
    if (Object >= APPLE_AGX_OBJECTS)
        return FALSE;
    Index = (Object - APPLE_AGX_FIXED_OBJECTS) / 2;
    if ((Object - APPLE_AGX_FIXED_OBJECTS) % 2 == 0)
    {
        Layout->Address = AGX_CHANNEL_STATE_BASE + (ULONG64)Index * AGX_CHANNEL_STRIDE;
        Layout->Size = APPLE_AGX_STATE_SIZE;
    }
    else
    {
        Layout->Address = AGX_CHANNEL_RING_BASE + (ULONG64)Index * AGX_CHANNEL_STRIDE;
        Layout->Size = APPLE_AGX_SUBMIT_RING_SIZE;
    }
    Layout->Class = APPLE_AGX_CLASS_FIRMWARE;
    return TRUE;
}

static
ULONG64
AgxAddress(
    _In_ ULONG Object)
{
    APPLE_AGX_LAYOUT Layout;

    return AppleAgxLayout(Object, &Layout) ? Layout.Address : 0;
}

VOID
NTAPI
AppleAgxClassProtection(
    _In_ UCHAR Class,
    _Out_ PAPPLE_UAT_PROTECTION Protection)
{
    Protection->Access = (Class == APPLE_AGX_CLASS_SHARED) ? APPLE_UAT_ACCESS_BOTH : APPLE_UAT_ACCESS_FIRMWARE;
    Protection->Memory = (Class == APPLE_AGX_CLASS_FIRMWARE_CACHED) ? APPLE_UAT_MEMORY_CACHED
                                                                    : APPLE_UAT_MEMORY_UNCACHED;
    Protection->Read = Class == APPLE_AGX_CLASS_SHARED;
    Protection->Write = TRUE;
}

static
ULONG
AgxPageCount(
    _In_ ULONG64 Address,
    _In_ ULONG Size)
{
    ULONG64 First = Address & ~(APPLE_UAT_PAGE_SIZE - 1);
    ULONG64 Last = (Address + Size + APPLE_UAT_PAGE_SIZE - 1) & ~(APPLE_UAT_PAGE_SIZE - 1);

    return (ULONG)((Last - First) / APPLE_UAT_PAGE_SIZE);
}

/* The objects are mapped a page at a time: nothing here needs physically neighbouring pages, and
 * the largest of them is far too big to ask for as one block. */
static
NTSTATUS
AgxMapObject(
    _Inout_ PAPPLE_AGX_INIT Init,
    _In_ ULONG Object,
    _Out_ PVOID *Virtual)
{
    APPLE_AGX_LAYOUT Layout;
    APPLE_UAT_PROTECTION Protection;
    PHYSICAL_ADDRESS Low, High, Boundary;
    ULONG64 Page, Cached;
    ULONG Index, Count;
    PVOID Allocation;
    NTSTATUS Status;

    *Virtual = NULL;
    if (!AppleAgxLayout(Object, &Layout) || !Layout.Size)
        return STATUS_INVALID_PARAMETER;
    Page = Layout.Address & ~(APPLE_UAT_PAGE_SIZE - 1);
    Count = AgxPageCount(Layout.Address, Layout.Size);
    Cached = (Object == APPLE_AGX_OBJECT_HWDATA) ? Layout.Address + APPLE_AGX_HWDATA_CACHED : 0;
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = APPLE_UAT_PAGE_SIZE;
    for (Index = 0; Index < Count; ++Index)
    {
        ULONG64 At = Page + (ULONG64)Index * APPLE_UAT_PAGE_SIZE;

        if (Init->Allocations == Init->Pages)
            return STATUS_INSUFFICIENT_RESOURCES;
        Allocation = MmAllocateContiguousMemorySpecifyCache(APPLE_UAT_PAGE_SIZE, Low, High, Boundary, MmNonCached);
        if (!Allocation)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Allocation, APPLE_UAT_PAGE_SIZE);
        AppleAgxClassProtection(Cached && At < Cached ? APPLE_AGX_CLASS_FIRMWARE_CACHED : Layout.Class,
                                &Protection);
        Status = AppleUatAdoptedMap(Init->Adopted, At, (ULONG64)MmGetPhysicalAddress(Allocation).QuadPart,
                                    APPLE_UAT_PAGE_SIZE, &Protection);
        if (!NT_SUCCESS(Status))
        {
            MmFreeContiguousMemorySpecifyCache(Allocation, APPLE_UAT_PAGE_SIZE, MmNonCached);
            return Status;
        }
        Init->Allocation[Init->Allocations++] = Allocation;
        if (Index == 0)
            *Virtual = (PUCHAR)Allocation + (Layout.Address - Page);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxInitCreate(
    _Out_ PAPPLE_AGX_INIT Init,
    _In_ PAPPLE_UAT_ADOPTED Adopted,
    _In_z_ PCSTR Owner)
{
    ULONG Object, Pages = 0;
    NTSTATUS Status;

    RtlZeroMemory(Init, sizeof(*Init));
    Init->Adopted = Adopted;
    Init->Owner = Owner;
    for (Object = 0; Object < APPLE_AGX_OBJECTS; ++Object)
    {
        APPLE_AGX_LAYOUT Layout;

        if (AppleAgxLayout(Object, &Layout))
            Pages += AgxPageCount(Layout.Address, Layout.Size);
    }
    Init->Pages = Pages;
    Init->Allocation = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Pages * sizeof(PVOID), AGX_INIT_TAG);
    if (!Init->Allocation)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Init->Allocation, (SIZE_T)Pages * sizeof(PVOID));
    for (Object = 0; Object < APPLE_AGX_OBJECTS; ++Object)
    {
        Status = AgxMapObject(Init, Object, &Init->Virtual[Object]);
        if (!NT_SUCCESS(Status))
        {
            AppleAgxInitRelease(Init);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

static
VOID
AgxWriteQuad(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG64 Value)
{
    RtlCopyMemory((PUCHAR)Base + Offset, &Value, sizeof(Value));
}

static
VOID
AgxWriteLong(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    RtlCopyMemory((PUCHAR)Base + Offset, &Value, sizeof(Value));
}

/* The firmware is handed the magic of the layout it was built with, the shape of the tables it
 * walks, and the address of everything it is expected to reach. */
NTSTATUS
NTAPI
AppleAgxInitWrite(
    _Inout_ PAPPLE_AGX_INIT Init)
{
    PVOID Root = Init->Virtual[APPLE_AGX_OBJECT_ROOT];
    PVOID Runtime = Init->Virtual[APPLE_AGX_OBJECT_RUNTIME];
    PVOID Control = Init->Virtual[APPLE_AGX_OBJECT_CONTROL];
    PVOID Hwdata = Init->Virtual[APPLE_AGX_OBJECT_HWDATA];
    ULONG Index, Level;

    if (!Root || !Runtime || !Control || !Hwdata)
        return STATUS_INVALID_DEVICE_STATE;

    AgxWriteQuad(Root, 0, APPLE_AGX_ROOT_MAGIC);
    AgxWriteQuad(Root, APPLE_AGX_ROOT_REGION_A, AgxAddress(APPLE_AGX_OBJECT_REGION_A));
    AgxWriteQuad(Root, APPLE_AGX_ROOT_RUNTIME, AgxAddress(APPLE_AGX_OBJECT_RUNTIME));
    AgxWriteQuad(Root, APPLE_AGX_ROOT_GLOBALS, AgxAddress(APPLE_AGX_OBJECT_GLOBALS));
    AgxWriteLong(Root, APPLE_AGX_ROOT_ENABLE, 1);
    AgxWriteLong(Root, APPLE_AGX_ROOT_GEOMETRY,
                 (ULONG)APPLE_UAT_PAGE_SIZE | ((ULONG)APPLE_UAT_PAGE_SHIFT << 16) |
                     ((ULONG)APPLE_UAT_LEVELS << 24));
    for (Level = 0; Level < APPLE_UAT_LEVELS; ++Level)
    {
        ULONG Offset = APPLE_AGX_ROOT_LEVELS + Level * APPLE_AGX_ROOT_LEVEL_STRIDE;
        ULONG Shift = APPLE_UAT_PAGE_SHIFT + (APPLE_UAT_LEVELS - 1 - Level) * APPLE_UAT_LEVEL_BITS;
        ULONG Entries = (Level == 0) ? APPLE_UAT_ROOT_ENTRIES : APPLE_UAT_TABLE_ENTRIES;
        UCHAR Describe[8];

        Describe[0] = (UCHAR)sizeof(ULONG64);
        Describe[1] = APPLE_UAT_PAGE_SHIFT;
        Describe[2] = APPLE_UAT_PAGE_SHIFT;
        Describe[3] = (UCHAR)Shift;
        Describe[4] = (UCHAR)(Entries & 0xff);
        Describe[5] = (UCHAR)(Entries >> 8);
        Describe[6] = (UCHAR)(APPLE_UAT_PAGE_SIZE & 0xff);
        Describe[7] = (UCHAR)((APPLE_UAT_PAGE_SIZE >> 8) & 0xff);
        RtlCopyMemory((PUCHAR)Root + Offset, Describe, sizeof(Describe));
        AgxWriteQuad(Root, Offset + 0x08, 1);
        AgxWriteQuad(Root, Offset + 0x10, APPLE_UAT_OUTPUT_MASK);
        AgxWriteQuad(Root, Offset + 0x18, (ULONG64)(Entries - 1) << Shift);
    }
    AgxWriteQuad(Root, APPLE_AGX_ROOT_FLAGS, AgxAddress(APPLE_AGX_OBJECT_FLAGS));
    AgxWriteQuad(Root, APPLE_AGX_ROOT_CONTROL, AgxAddress(APPLE_AGX_OBJECT_CONTROL));
    AgxWriteQuad(Root, APPLE_AGX_ROOT_POWER, AgxAddress(APPLE_AGX_OBJECT_POWER));

    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_HWDATA, AgxAddress(APPLE_AGX_OBJECT_HWDATA));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_PAIR, AgxAddress(APPLE_AGX_OBJECT_PAIR));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_PAIR2, AgxAddress(APPLE_AGX_OBJECT_PAIR));
    for (Index = 0; Index < APPLE_AGX_SUBMIT_CHANNELS; ++Index)
    {
        ULONG Offset = APPLE_AGX_RUNTIME_CHANNELS + Index * APPLE_AGX_RUNTIME_CHANNEL_STRIDE;
        ULONG64 State = AgxAddress(APPLE_AGX_FIXED_OBJECTS + 2 * Index);
        ULONG64 Ring = AgxAddress(APPLE_AGX_FIXED_OBJECTS + 2 * Index + 1);

        AgxWriteQuad(Runtime, Offset, State);
        AgxWriteQuad(Runtime, Offset + 8, State + 0x10);
        AgxWriteQuad(Runtime, Offset + 16, State + 0x20);
        AgxWriteQuad(Runtime, Offset + 24, Ring);
    }
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_CHANNELS +
                              APPLE_AGX_SUBMIT_CHANNELS * APPLE_AGX_RUNTIME_CHANNEL_STRIDE,
                 AgxAddress(APPLE_AGX_OBJECT_DEVCTL_STATE));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_CHANNELS +
                              APPLE_AGX_SUBMIT_CHANNELS * APPLE_AGX_RUNTIME_CHANNEL_STRIDE + 24,
                 AgxAddress(APPLE_AGX_OBJECT_DEVCTL_RING));
    for (Index = 0; Index < 4; ++Index)
    {
        static const ULONG State[4] = { APPLE_AGX_OBJECT_EVENT_STATE, APPLE_AGX_OBJECT_LOG_STATE,
                                        APPLE_AGX_OBJECT_TRACE_STATE, APPLE_AGX_OBJECT_STATS_STATE };
        static const ULONG Ring[4] = { APPLE_AGX_OBJECT_EVENT_RING, APPLE_AGX_OBJECT_LOG_RING,
                                       APPLE_AGX_OBJECT_TRACE_RING, APPLE_AGX_OBJECT_STATS_RING };
        ULONG Offset = APPLE_AGX_RUNTIME_RX + Index * APPLE_AGX_RUNTIME_RX_STRIDE;

        AgxWriteQuad(Runtime, Offset, AgxAddress(State[Index]));
        AgxWriteQuad(Runtime, Offset + 8, AgxAddress(Ring[Index]));
    }
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_SMALL, AgxAddress(APPLE_AGX_OBJECT_SMALL));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_TA_ACTIVITY,
                 AgxAddress(APPLE_AGX_OBJECT_HWDATA) + APPLE_AGX_HWDATA_TA_ACTIVITY);
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_FS_ACTIVITY,
                 AgxAddress(APPLE_AGX_OBJECT_HWDATA) + APPLE_AGX_HWDATA_FS_ACTIVITY);
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_CS_ACTIVITY,
                 AgxAddress(APPLE_AGX_OBJECT_HWDATA) + APPLE_AGX_HWDATA_CS_ACTIVITY);
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_C0, AgxAddress(APPLE_AGX_OBJECT_C0));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_C1, AgxAddress(APPLE_AGX_OBJECT_C1));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_C1_2, AgxAddress(APPLE_AGX_OBJECT_C1));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_C3, AgxAddress(APPLE_AGX_OBJECT_C3));
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_C3_2, AgxAddress(APPLE_AGX_OBJECT_C3));
    AgxWriteLong(Runtime, APPLE_AGX_RUNTIME_UNKNOWN_2D0, 4);
    AgxWriteLong(Runtime, APPLE_AGX_RUNTIME_UNKNOWN_3B0, 0xff);
    AgxWriteQuad(Runtime, APPLE_AGX_RUNTIME_HWDATA_A,
                 AgxAddress(APPLE_AGX_OBJECT_HWDATA) + APPLE_AGX_HWDATA_HWDATA_A);

    AgxWriteLong(Control, APPLE_AGX_CONTROL_ENABLE, 1);
    AgxWriteLong(Control, APPLE_AGX_CONTROL_ENABLE2, 1);
    AgxWriteQuad(Control, APPLE_AGX_CONTROL_FWCTL_STATE,
                 AgxAddress(APPLE_AGX_OBJECT_FWCTL_STATE));
    AgxWriteQuad(Control, APPLE_AGX_CONTROL_FWCTL_RING,
                 AgxAddress(APPLE_AGX_OBJECT_FWCTL_RING));

    AgxWriteQuad(Hwdata, APPLE_AGX_HWDATA_TIMESTAMPS,
                 AgxAddress(APPLE_AGX_OBJECT_TIMESTAMPS));
    AgxWriteQuad(Hwdata, APPLE_AGX_HWDATA_TTBAT, Init->Adopted->KernelRootPhysical);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
AppleAgxInitReady(
    _In_ PAPPLE_AGX_INIT Init)
{
    ULONG Value;

    if (!Init->Virtual[APPLE_AGX_OBJECT_RUNTIME])
        return FALSE;
    KeMemoryBarrier();
    RtlCopyMemory(&Value, (PUCHAR)Init->Virtual[APPLE_AGX_OBJECT_RUNTIME] + APPLE_AGX_RUNTIME_READY,
                  sizeof(Value));
    return Value == APPLE_AGX_RUNTIME_READY_MAGIC;
}

ULONG64
NTAPI
AppleAgxInitMessage(VOID)
{
    return APPLE_AGX_MESSAGE_INITDATA |
           (AgxFixed[APPLE_AGX_OBJECT_ROOT].Address & APPLE_AGX_MESSAGE_ADDRESS_MASK);
}

VOID
NTAPI
AppleAgxInitRelease(
    _Inout_ PAPPLE_AGX_INIT Init)
{
    ULONG Index;

    if (Init->Allocation)
    {
        for (Index = 0; Index < Init->Allocations; ++Index)
        {
            if (Init->Allocation[Index])
                MmFreeContiguousMemorySpecifyCache(Init->Allocation[Index], APPLE_UAT_PAGE_SIZE, MmNonCached);
        }
        ExFreePoolWithTag(Init->Allocation, AGX_INIT_TAG);
    }
    RtlZeroMemory(Init, sizeof(*Init));
}

VOID
NTAPI
AppleAgxRingBind(
    _Out_ PAPPLE_AGX_RING Ring,
    _In_ PVOID State,
    _In_ PVOID Records,
    _In_ ULONG Record)
{
    RtlZeroMemory(Ring, sizeof(*Ring));
    Ring->State = State;
    Ring->Ring = Records;
    Ring->Record = Record;
    Ring->Producer = AppleAgxRingIndex(Ring, APPLE_AGX_STATE_WRITE);
}

ULONG
NTAPI
AppleAgxRingIndex(
    _In_ PAPPLE_AGX_RING Ring,
    _In_ ULONG Offset)
{
    ULONG Value = 0;

    if (!Ring->State)
        return 0;
    KeMemoryBarrier();
    RtlCopyMemory(&Value, (PUCHAR)Ring->State + Offset, sizeof(Value));
    return Value % APPLE_AGX_RING_SLOTS;
}

/* A record is never written over one the firmware has not consumed; behind unconsumed records it
 * goes only while fewer than the allowed number are outstanding. */
NTSTATUS
NTAPI
AppleAgxRingPost(
    _Inout_ PAPPLE_AGX_RING Ring,
    _In_reads_bytes_(Ring->Record) const VOID *Record,
    _In_ BOOLEAN Ahead,
    _Out_opt_ PULONG Next)
{
    ULONG Consumer, Outstanding;

    if (Next)
        *Next = 0;
    if (!Ring->State || !Ring->Ring || !Ring->Record)
        return STATUS_INVALID_DEVICE_STATE;
    if (AppleAgxRingIndex(Ring, APPLE_AGX_STATE_WRITE) != Ring->Producer)
        return STATUS_DEVICE_DATA_ERROR;
    Consumer = AppleAgxRingIndex(Ring, APPLE_AGX_STATE_READ);
    Outstanding = (Ring->Producer + APPLE_AGX_RING_SLOTS - Consumer) % APPLE_AGX_RING_SLOTS;
    if (Ahead ? Outstanding >= APPLE_AGX_RING_AHEAD : Consumer != Ring->Producer)
        return STATUS_DEVICE_BUSY;
    RtlCopyMemory((PUCHAR)Ring->Ring + (SIZE_T)Ring->Producer * Ring->Record, Record, Ring->Record);
    KeMemoryBarrier();
    Ring->Producer = (Ring->Producer + 1) % APPLE_AGX_RING_SLOTS;
    RtlCopyMemory((PUCHAR)Ring->State + APPLE_AGX_STATE_WRITE, &Ring->Producer, sizeof(Ring->Producer));
    KeMemoryBarrier();
    if (Next)
        *Next = Ring->Producer;
    return STATUS_SUCCESS;
}

/* A posted record is done once the firmware's consumer and its completion index have both
 * reached the slot after it. */
BOOLEAN
NTAPI
AppleAgxRingDone(
    _In_ PAPPLE_AGX_RING Ring,
    _In_ ULONG Next)
{
    return Ring->State && AppleAgxRingIndex(Ring, APPLE_AGX_STATE_READ) == Next &&
           AppleAgxRingIndex(Ring, APPLE_AGX_STATE_COMPLETION) == Next;
}

/* On a receive ring the firmware is the producer, so a record is taken while one is waiting. */
BOOLEAN
NTAPI
AppleAgxRingTake(
    _Inout_ PAPPLE_AGX_RING Ring,
    _Out_writes_bytes_(Ring->Record) PVOID Record)
{
    ULONG Read;

    if (!Ring->State || !Ring->Ring || !Ring->Record)
        return FALSE;
    Read = AppleAgxRingIndex(Ring, APPLE_AGX_STATE_READ);
    if (Read == AppleAgxRingIndex(Ring, APPLE_AGX_STATE_WRITE))
        return FALSE;
    RtlCopyMemory(Record, (PUCHAR)Ring->Ring + (SIZE_T)Read * Ring->Record, Ring->Record);
    Read = (Read + 1) % APPLE_AGX_RING_SLOTS;
    KeMemoryBarrier();
    RtlCopyMemory((PUCHAR)Ring->State + APPLE_AGX_STATE_READ, &Read, sizeof(Read));
    KeMemoryBarrier();
    return TRUE;
}

VOID
NTAPI
AppleAgxRingConsume(
    _Inout_ PAPPLE_AGX_RING Ring)
{
    ULONG Write;

    if (!Ring->State)
        return;
    Write = AppleAgxRingIndex(Ring, APPLE_AGX_STATE_WRITE);
    KeMemoryBarrier();
    RtlCopyMemory((PUCHAR)Ring->State + APPLE_AGX_STATE_READ, &Write, sizeof(Write));
    KeMemoryBarrier();
}

ULONG64
NTAPI
AppleAgxRingDoorbell(
    _In_ ULONG Channel)
{
    return APPLE_AGX_DOORBELL_BASE | Channel;
}

/* The register windows of this part, at the addresses its firmware was built to reach them by.
 * The first two are the graphics block's own ranges, which the machine's tree also names. */
static const APPLE_AGX_IOMAP AgxIoMaps[APPLE_AGX_IOMAPS] =
{
    { 0x290d00000ULL, 0xfffffc2068000000ULL, 0x144000,  0 },
    /* FIXME: this window lies below every range this machine reports; it is mapped unconfirmed. */
    { 0x20e100000ULL, 0xfffffc2068149000ULL, 0x4000,    1 },
    { 0x351014000ULL, 0xfffffc2068150000ULL, 0x4000,    2 },
    { 0x290000000ULL, 0xfffffc2068158000ULL, 0x20000,   3 },
    { 0x3502bc000ULL, 0xfffffc206817c000ULL, 0x4000,    7 },
    { 0x290e08000ULL, 0xfffffc2068184000ULL, 0x8000,    9 },
    { 0x220000000ULL, 0xfffffc2068190000ULL, 0x12c000, 11 },
    { 0x35104c000ULL, 0xfffffc20682c0000ULL, 0x4000,   12 },
    { 0x3503d0000ULL, 0xfffffc20682c8000ULL, 0x4000,   18 },
    { 0x3503c0000ULL, 0xfffffc20682d0000ULL, 0x4000,   19 },
    { 0x3503d8000ULL, 0xfffffc20682d8000ULL, 0x4000,   20 },
    { 0x293000000ULL, 0xfffffc20682e0000ULL, 0x400000, 23 },
    { 0x30945c000ULL, 0xfffffc20686e4000ULL, 0x4000,   25 },
    { 0x350280000ULL, 0xfffffc20686ec000ULL, 0x8000,   26 },
    { 0x290e5c000ULL, 0xfffffc20686f8000ULL, 0x4000,   29 },
};

const APPLE_AGX_IOMAP *
NTAPI
AppleAgxIoMap(
    _In_ ULONG Index)
{
    return Index < APPLE_AGX_IOMAPS ? &AgxIoMaps[Index] : NULL;
}

/* Each window is mapped as device memory and then recorded in the hardware description, so the
 * firmware both reaches it and knows where it is. */
NTSTATUS
NTAPI
AppleAgxMapRegisters(
    _Inout_ PAPPLE_AGX_INIT Init)
{
    const APPLE_UAT_PROTECTION Device = { APPLE_UAT_ACCESS_FIRMWARE, APPLE_UAT_MEMORY_DEVICE, FALSE, TRUE };
    PVOID Hwdata = Init->Virtual[APPLE_AGX_OBJECT_HWDATA];
    ULONG Index;
    NTSTATUS Status;

    if (!Hwdata)
        return STATUS_INVALID_DEVICE_STATE;
    for (Index = 0; Index < APPLE_AGX_IOMAPS; ++Index)
    {
        const APPLE_AGX_IOMAP *Map = &AgxIoMaps[Index];
        SIZE_T Length = ((SIZE_T)Map->Size + APPLE_UAT_PAGE_SIZE - 1) & ~(SIZE_T)(APPLE_UAT_PAGE_SIZE - 1);
        ULONG Record = APPLE_AGX_HWDATA_IOMAPS + (ULONG)Map->Slot * APPLE_AGX_HWDATA_IOMAP_STRIDE;

        if (Map->Slot >= APPLE_AGX_IOMAP_SLOTS ||
            Record + APPLE_AGX_HWDATA_IOMAP_STRIDE > APPLE_AGX_HWDATA_SIZE)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = AppleUatAdoptedMap(Init->Adopted, Map->Address & ~(APPLE_UAT_PAGE_SIZE - 1), Map->Physical,
                                   Length, &Device);
        if (!NT_SUCCESS(Status))
            return Status;
        AgxWriteQuad(Hwdata, Record + APPLE_AGX_IOMAP_PHYSICAL, Map->Physical);
        AgxWriteQuad(Hwdata, Record + APPLE_AGX_IOMAP_ADDRESS, Map->Address);
        AgxWriteLong(Hwdata, Record + APPLE_AGX_IOMAP_TOTAL, Map->Size);
        AgxWriteLong(Hwdata, Record + APPLE_AGX_IOMAP_WRITABLE, 1);
    }
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* Each table takes nine of the machine's performance states, and the two take different ones. */
static const UCHAR AgxDvfsA[APPLE_AGX_DVFS_STATES] = { 0, 1, 2, 3, 5, 7, 9, 11, 13 };
static const UCHAR AgxDvfsB[APPLE_AGX_DVFS_STATES] = { 0, 1, 2, 3, 4, 6, 8, 10, 12 };

const UCHAR *
NTAPI
AppleAgxDvfsStates(
    _In_ BOOLEAN Second)
{
    return Second ? AgxDvfsB : AgxDvfsA;
}

NTSTATUS
NTAPI
AppleAgxWriteStates(
    _Inout_ PAPPLE_AGX_INIT Init,
    _In_ const APPLE_AGX_DESCRIPTION *Description,
    _Out_ PULONG TopKilohertz)
{
    PVOID Hwdata = Init->Virtual[APPLE_AGX_OBJECT_HWDATA];
    ULONG State, Repeat, Top;

    *TopKilohertz = 0;
    if (!Hwdata)
        return STATUS_INVALID_DEVICE_STATE;
    for (State = 0; State < APPLE_AGX_DVFS_STATES; ++State)
    {
        if (AgxDvfsA[State] >= Description->States || AgxDvfsB[State] >= Description->States)
            return STATUS_INVALID_PARAMETER;
    }
    /* The firmware steps through these in order, so a table that does not rise is refused. */
    for (State = 1; State < APPLE_AGX_DVFS_STATES; ++State)
    {
        const APPLE_AGX_STATE *A = &Description->State[AgxDvfsA[State]];
        const APPLE_AGX_STATE *PreviousA = &Description->State[AgxDvfsA[State - 1]];
        const APPLE_AGX_STATE *B = &Description->State[AgxDvfsB[State]];
        const APPLE_AGX_STATE *PreviousB = &Description->State[AgxDvfsB[State - 1]];

        if (A->Frequency <= PreviousA->Frequency || B->Frequency <= PreviousB->Frequency ||
            A->Millivolts < PreviousA->Millivolts ||
            Description->SramState[AgxDvfsA[State]].Millivolts <
                Description->SramState[AgxDvfsA[State - 1]].Millivolts)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    for (State = 0; State < APPLE_AGX_DVFS_STATES; ++State)
    {
        ULONG A = AgxDvfsA[State], B = AgxDvfsB[State];
        ULONG MegahertzA = Description->State[A].Frequency / 1000000;
        ULONG MegahertzB = Description->State[B].Frequency / 1000000;

        AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_DVFS_MHZ_A + 4 * State, MegahertzA);
        AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_DVFS_MHZ_B + 4 * State, MegahertzB);
        AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_DVFS_MHZ_B2 + 4 * State, MegahertzB);
        for (Repeat = 0; Repeat < APPLE_AGX_HWDATA_DVFS_REPEAT; ++Repeat)
        {
            AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_DVFS_CORE_MV + APPLE_AGX_HWDATA_DVFS_STRIDE * State +
                                     4 * Repeat, Description->State[A].Millivolts);
            AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_DVFS_SRAM_MV + APPLE_AGX_HWDATA_DVFS_STRIDE * State +
                                     4 * Repeat, Description->SramState[A].Millivolts);
        }
    }
    Top = Description->State[AgxDvfsA[APPLE_AGX_DVFS_STATES - 1]].Frequency / 1000000;
    AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_TOP_MHZ, Top);
    AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_CHECK_A38, 1);
    AgxWriteLong(Hwdata, APPLE_AGX_HWDATA_DVFS_COUNT, APPLE_AGX_DVFS_STATES - 1);
    KeMemoryBarrier();
    *TopKilohertz = Top * 1000;
    return STATUS_SUCCESS;
}

/* The queue tells the firmware where its cursors, its ring of work and its scratch are. */
NTSTATUS
NTAPI
AppleAgxQueueDescribe(
    _Inout_ PAPPLE_AGX_QUEUE Queue)
{
    if (!Queue->InfoVirtual || !Queue->StateVirtual || !Queue->RingVirtual || !Queue->Entries ||
        !Queue->State || !Queue->Ring)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Queue->InfoVirtual, APPLE_AGX_QINFO_SIZE);
    RtlZeroMemory(Queue->StateVirtual, APPLE_AGX_QSTATE_SIZE);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_STATE, Queue->State);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_RING, Queue->Ring);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_JOB_LIST, Queue->JobList);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_SCRATCH, Queue->Scratch);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_SCHED_CONTEXT, Queue->SchedContext);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_UNKNOWN_AC, APPLE_AGX_QINFO_UNKNOWN_AC_VALUE);
    AgxWriteLong(Queue->StateVirtual, APPLE_AGX_QSTATE_CAPACITY, Queue->Entries);
    Queue->Write = 0;
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* A work address goes into the ring at the host's cursor, which then names the head the firmware
 * is asked to run to. The entry the firmware has not read yet is never written over. */
NTSTATUS
NTAPI
AppleAgxQueuePost(
    _Inout_ PAPPLE_AGX_QUEUE Queue,
    _In_ ULONG64 Work,
    _Out_ PUSHORT Head)
{
    ULONG Read = 0, Next;

    *Head = 0;
    if (!Queue->RingVirtual || !Queue->StateVirtual || !Queue->Entries || !Work)
        return STATUS_INVALID_PARAMETER;
    KeMemoryBarrier();
    RtlCopyMemory(&Read, (PUCHAR)Queue->StateVirtual + APPLE_AGX_QSTATE_READ, sizeof(Read));
    if (Read >= Queue->Entries)
        return STATUS_DEVICE_DATA_ERROR;
    Next = (Queue->Write + 1) % Queue->Entries;
    if (Next == Read)
        return STATUS_DEVICE_BUSY;
    AgxWriteQuad(Queue->RingVirtual, Queue->Write * APPLE_AGX_WORK_ENTRY, Work);
    KeMemoryBarrier();
    Queue->Write = Next;
    AgxWriteLong(Queue->StateVirtual, APPLE_AGX_QSTATE_WRITE, Queue->Write);
    KeMemoryBarrier();
    *Head = (USHORT)Queue->Write;
    return STATUS_SUCCESS;
}

ULONG
NTAPI
AppleAgxQueueDone(
    _In_ PAPPLE_AGX_QUEUE Queue)
{
    ULONG Done = 0;

    if (!Queue->StateVirtual)
        return 0;
    KeMemoryBarrier();
    RtlCopyMemory(&Done, (PUCHAR)Queue->StateVirtual + APPLE_AGX_QSTATE_DONE, sizeof(Done));
    return Done;
}

VOID
NTAPI
AppleAgxSubmitRecord(
    _Out_writes_bytes_(APPLE_AGX_SUBMIT_RECORD) PVOID Record,
    _In_ ULONG64 Queue,
    _In_ ULONG Kind,
    _In_ USHORT Head,
    _In_ UCHAR Event,
    _In_ BOOLEAN NewQueue)
{
    RtlZeroMemory(Record, APPLE_AGX_SUBMIT_RECORD);
    AgxWriteQuad(Record, APPLE_AGX_SUBMIT_QUEUE, Queue);
    AgxWriteLong(Record, APPLE_AGX_SUBMIT_KIND, Kind);
    RtlCopyMemory((PUCHAR)Record + APPLE_AGX_SUBMIT_HEAD, &Head, sizeof(Head));
    ((PUCHAR)Record)[APPLE_AGX_SUBMIT_EVENT] = Event;
    ((PUCHAR)Record)[APPLE_AGX_SUBMIT_NEW_QUEUE] = NewQueue ? 1 : 0;
}

/* The objects every queue of the graph shares: what the firmware signals completion through, the
 * list it keeps its jobs on and the context it schedules them in. */
NTSTATUS
NTAPI
AppleAgxDescribeGraph(
    _In_ PVOID Notifier,
    _In_ ULONG64 Threshold,
    _In_ PVOID JobList,
    _In_ ULONG64 JobListAddress,
    _In_ PVOID SchedContext)
{
    ULONG Index;

    if (!Notifier || !JobList || !SchedContext || !Threshold || !JobListAddress)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Notifier, APPLE_AGX_NOTIFIER_SIZE);
    AgxWriteQuad(Notifier, APPLE_AGX_NOTIFIER_THRESHOLD, Threshold);
    AgxWriteLong(Notifier, APPLE_AGX_NOTIFIER_UNKNOWN_10, APPLE_AGX_NOTIFIER_UNKNOWN_10_VALUE);
    for (Index = 0; Index < 8; ++Index)
        ((PUCHAR)Notifier)[APPLE_AGX_NOTIFIER_MASK + Index] = 0xff;
    RtlZeroMemory(JobList, APPLE_AGX_JOB_LIST_SIZE);
    AgxWriteQuad(JobList, APPLE_AGX_JOB_LIST_SELF, JobListAddress);
    RtlZeroMemory(SchedContext, APPLE_AGX_SCHED_CONTEXT_SIZE);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxDescribeManager(
    _Out_writes_bytes_(APPLE_AGX_MANAGER_SIZE) PVOID Manager,
    _In_ ULONG64 Freelist,
    _In_ ULONG64 PoolTable,
    _In_ ULONG64 Counter)
{
    if (!Manager || !Freelist || !PoolTable || !Counter)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Manager, APPLE_AGX_MANAGER_SIZE);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_ID, APPLE_AGX_MANAGER_COMPUTE_ID);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_INDEX, APPLE_AGX_MANAGER_COMPUTE_INDEX);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_ONE, 1);
    AgxWriteQuad(Manager, APPLE_AGX_MANAGER_FREELIST, Freelist);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_UNKNOWN_1C, APPLE_AGX_MANAGER_UNKNOWN_1C_VALUE);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_SEEDS_A, APPLE_AGX_FREELIST_SEEDS);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_SEEDS_B, APPLE_AGX_FREELIST_SEEDS);
    AgxWriteQuad(Manager, APPLE_AGX_MANAGER_POOL_TABLE, PoolTable);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_UNKNOWN_44, APPLE_AGX_MANAGER_UNKNOWN_44_VALUE);
    AgxWriteQuad(Manager, APPLE_AGX_MANAGER_COUNTER, Counter);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* Every pool is named in the table; only the first few are broken into units for the freelist,
 * which is what a large workgroup's memory comes out of. */
NTSTATUS
NTAPI
AppleAgxSeedPools(
    _Out_writes_bytes_(APPLE_AGX_POOLS *APPLE_AGX_POOL_STRIDE) PVOID PoolTable,
    _Out_writes_bytes_(APPLE_AGX_FREELIST_SEEDS * 8) PVOID Freelist,
    _In_ ULONG64 FirstPool,
    _Out_ PULONG Seeds)
{
    ULONG Pool, Unit, Index = 0;
    ULONG Units = APPLE_AGX_POOL_SIZE / APPLE_AGX_POOL_UNIT;

    *Seeds = 0;
    if (!PoolTable || !Freelist || !FirstPool || (FirstPool & (APPLE_AGX_POOL_UNIT - 1)))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(PoolTable, APPLE_AGX_POOLS * APPLE_AGX_POOL_STRIDE);
    RtlZeroMemory(Freelist, (SIZE_T)APPLE_AGX_FREELIST_SEEDS * sizeof(ULONG64));
    for (Pool = 0; Pool < APPLE_AGX_POOLS; ++Pool)
    {
        ULONG64 At = FirstPool + (ULONG64)Pool * APPLE_AGX_POOL_SIZE;

        AgxWriteQuad(PoolTable, Pool * APPLE_AGX_POOL_STRIDE, At | APPLE_AGX_POOL_VALID);
    }
    for (Pool = 0; Pool < APPLE_AGX_SEEDED_POOLS; ++Pool)
    {
        ULONG64 At = FirstPool + (ULONG64)Pool * APPLE_AGX_POOL_SIZE;

        for (Unit = 0; Unit < Units; ++Unit)
        {
            if (Index == APPLE_AGX_FREELIST_SEEDS)
                return STATUS_INVALID_PARAMETER;
            AgxWriteQuad(Freelist, Index * (ULONG)sizeof(ULONG64),
                         At + (ULONG64)Unit * APPLE_AGX_POOL_UNIT);
            ++Index;
        }
    }
    KeMemoryBarrier();
    *Seeds = Index;
    return Index == APPLE_AGX_FREELIST_SEEDS ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}
