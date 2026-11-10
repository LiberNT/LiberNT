/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Initial data the Apple silicon graphics firmware is started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              The firmware interface described here is taken, under its MIT option, from the
 *              Apple graphics driver of aurora-silicon/linux, at git commit c880586:
 *              Copyright (C) 2026 Eryk Wieliczko
 *              Copyright (C) 2026 The Gravity Linux Contributors
 *              SPDX-License-Identifier: GPL-2.0-only OR MIT
 *              See media/doc/3rd Party Apple GPU interface.txt for what was and was not taken.
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

static
VOID
AgxWriteWord(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ USHORT Value)
{
    RtlCopyMemory((PUCHAR)Base + Offset, &Value, sizeof(Value));
}

/* The sequence starts the work, names everything it reaches, finalizes it and ends. The firmware
 * walks it as an instruction stream, so every address it names is written at its own offset. */
NTSTATUS
NTAPI
AppleAgxEncodeCompute(
    _Out_writes_bytes_(APPLE_AGX_COMPUTE_SEQUENCE_SIZE) PVOID Sequence,
    _In_ const APPLE_AGX_COMPUTE_REFS *Refs,
    _In_reads_opt_(Attachments) const APPLE_AGX_ATTACHMENT *Attachment,
    _In_ ULONG Attachments,
    _In_ ULONG Context,
    _In_ ULONG Stamp,
    _In_ BOOLEAN Faults)
{
    ULONG Final = APPLE_AGX_COMPUTE_FINALIZE_AT;
    ULONG Index;

    if (!Sequence || !Refs->Work || !Refs->Queue || !Refs->Hwdata || !Refs->Notifier ||
        !Refs->Manager || !Refs->FirmwareStamp || Attachments > APPLE_AGX_MAX_ATTACHMENTS ||
        (Attachments && !Attachment))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Sequence, APPLE_AGX_COMPUTE_SEQUENCE_SIZE);
    AgxWriteLong(Sequence, 0x00, APPLE_AGX_OP_COMPUTE_START);
    AgxWriteQuad(Sequence, 0x14, Refs->Work + APPLE_AGX_COMPUTE_REGISTERS);
    AgxWriteQuad(Sequence, 0x1c, Refs->Hwdata + APPLE_AGX_COMPUTE_HWDATA);
    AgxWriteQuad(Sequence, 0x24, Refs->Queue);
    AgxWriteLong(Sequence, 0x2c, Context);
    AgxWriteLong(Sequence, 0x30, Faults ? 1 : 0);
    AgxWriteQuad(Sequence, 0x44, Refs->Work + 0x76c);
    AgxWriteLong(Sequence, 0x50, APPLE_AGX_COMPUTE_UUID);
    for (Index = 0; Index < Attachments; ++Index)
    {
        ULONG64 Base = Attachment[Index].Pointer & ~(ULONG64)(APPLE_AGX_ATTACHMENT_GRAIN - 1);
        ULONG64 End = (Attachment[Index].Pointer + Attachment[Index].Size +
                       APPLE_AGX_ATTACHMENT_GRAIN - 1) & ~(ULONG64)(APPLE_AGX_ATTACHMENT_GRAIN - 1);
        ULONG At = APPLE_AGX_COMPUTE_ATTACHMENTS + APPLE_AGX_COMPUTE_ATTACHMENT_STRIDE * Index;

        AgxWriteQuad(Sequence, At, Base);
        AgxWriteLong(Sequence, At + 8, (ULONG)((End - Base) / APPLE_AGX_ATTACHMENT_GRAIN));
        AgxWriteWord(Sequence, At + 0x0c, 0x17);
        AgxWriteWord(Sequence, At + 0x0e, 1);
    }
    AgxWriteLong(Sequence, 0x154, Attachments);
    AgxWriteQuad(Sequence, 0x15c, Refs->Manager);
    AgxWriteQuad(Sequence, 0x174, Refs->Scratch[0]);
    AgxWriteQuad(Sequence, 0x184, Refs->Scratch[1]);
    AgxWriteQuad(Sequence, 0x18c, Refs->Scratch[2]);
    AgxWriteQuad(Sequence, 0x194, Refs->Scratch[3]);
    AgxWriteQuad(Sequence, 0x19c, Refs->Scratch[4]);
    AgxWriteQuad(Sequence, 0x1a4, Refs->Work + 0x880);
    AgxWriteQuad(Sequence, 0x1b4, Refs->Notifier + APPLE_AGX_NOTIFIER_MASK);
    /* FIXME: the start and end timestamp records are left empty; no timestamp is asked for. */
    AgxWriteLong(Sequence, 0x1f8, 1);
    AgxWriteLong(Sequence, Final, APPLE_AGX_OP_COMPUTE_FINALIZE);
    AgxWriteQuad(Sequence, Final + 0x04, Refs->Hwdata + APPLE_AGX_COMPUTE_HWDATA);
    AgxWriteQuad(Sequence, Final + 0x0c, Refs->Queue);
    AgxWriteLong(Sequence, Final + 0x14, Context);
    AgxWriteQuad(Sequence, Final + 0x18, Refs->Work + 0x76c);
    AgxWriteLong(Sequence, Final + 0x24, APPLE_AGX_COMPUTE_UUID);
    AgxWriteQuad(Sequence, Final + 0x28, Refs->FirmwareStamp);
    AgxWriteLong(Sequence, Final + 0x30, Stamp);
    AgxWriteQuad(Sequence, Final + 0x58, Refs->Work + 0x83e);
    AgxWriteLong(Sequence, Final + 0x60, (ULONG)(0u - Final));
    AgxWriteQuad(Sequence, Final + 0x65, Refs->Work + 0x880);
    AgxWriteQuad(Sequence, Final + 0x6d, Refs->Work + 0x860);
    AgxWriteLong(Sequence, APPLE_AGX_COMPUTE_END_AT, APPLE_AGX_OP_END);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* The registers a compute command is run with: the firmware writes each value to the selector
 * beside it. */
static const ULONG AgxComputeSelectors[APPLE_AGX_COMPUTE_REGISTER_COUNT] =
{
    0x1a510, 0x1a420, 0x1a4d0, 0x1a4d8, 0x1a4e0, 0x1a4e8, 0x1a440, 0x1a458, 0x12091, 0x101d9,
    0x1a089, 0x1a091, 0x1a059, 0x1a061, 0x1a0b9, 0x1a0c1, 0x101d1, 0x0d479, 0x1a0e9, 0x107a1,
    0x0a599, 0x0d411,
};

const ULONG *
NTAPI
AppleAgxComputeSelectors(VOID)
{
    return AgxComputeSelectors;
}

/* The work item itself: what kind of work it is, the registers it runs with, the sequence that
 * runs it, the stamps that mark it finished and the allocator it draws memory from. */
NTSTATUS
NTAPI
AppleAgxEncodeComputeWork(
    _Out_writes_bytes_(APPLE_AGX_COMPUTE_WORK_SIZE) PVOID Work,
    _In_ const APPLE_AGX_COMPUTE_REFS *Refs,
    _In_reads_(APPLE_AGX_COMPUTE_REGISTER_COUNT) const ULONG64 *Register,
    _In_ ULONG Context,
    _In_ ULONG Ordinal,
    _In_ ULONG Stamp)
{
    ULONG Index;

    if (!Work || !Register || !Refs->Notifier || !Refs->WorkClient || !Refs->Sequence ||
        !Refs->Manager || !Refs->UserStamp || !Refs->FirmwareStamp || !Refs->Timestamps ||
        !Refs->Tail || !Refs->StreamEnd)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Work, APPLE_AGX_COMPUTE_WORK_SIZE);
    AgxWriteLong(Work, 0x000, APPLE_AGX_WORK_COMPUTE);
    AgxWriteQuad(Work, 0x004, Ordinal - 1);
    AgxWriteLong(Work, 0x010, Context);
    AgxWriteQuad(Work, 0x014, Refs->Notifier);
    for (Index = 0; Index < APPLE_AGX_COMPUTE_REGISTER_COUNT; ++Index)
    {
        ULONG At = APPLE_AGX_COMPUTE_REGISTERS + APPLE_AGX_REGISTER_RECORD * Index;

        AgxWriteLong(Work, At, AgxComputeSelectors[Index]);
        AgxWriteQuad(Work, At + 4, Register[Index]);
    }
    AgxWriteQuad(Work, 0x720, Refs->WorkClient + APPLE_AGX_COMPUTE_REGISTERS);
    AgxWriteWord(Work, 0x728, APPLE_AGX_COMPUTE_REGISTER_COUNT);
    AgxWriteWord(Work, 0x72a, APPLE_AGX_COMPUTE_REGISTER_COUNT * APPLE_AGX_REGISTER_RECORD);
    AgxWriteQuad(Work, 0x760, Refs->Sequence);
    AgxWriteLong(Work, 0x768, APPLE_AGX_COMPUTE_SEQUENCE_SIZE);
    AgxWriteQuad(Work, 0x794, Refs->Preempt);
    AgxWriteQuad(Work, 0x79c, Refs->StreamEnd - 4);
    AgxWriteQuad(Work, 0x7c4, 0x154024201ULL);
    AgxWriteLong(Work, 0x7e0, 0xffffffff);
    AgxWriteQuad(Work, 0x7e8, Refs->UserStamp);
    AgxWriteQuad(Work, 0x7f0, Refs->FirmwareStamp);
    AgxWriteLong(Work, 0x7f8, Stamp);
    AgxWriteLong(Work, 0x7fc, APPLE_AGX_EVENT_SLOT);
    AgxWriteLong(Work, 0x804, 1);
    AgxWriteLong(Work, 0x808, APPLE_AGX_COMPUTE_UUID);
    AgxWriteLong(Work, 0x80c, Ordinal - 1);
    AgxWriteQuad(Work, 0x818, Refs->Timestamps);
    AgxWriteQuad(Work, 0x820, Refs->Timestamps + 8);
    AgxWriteQuad(Work, 0x83e, Refs->Manager);
    AgxWriteQuad(Work, 0x847, 0xad0000ULL);
    AgxWriteQuad(Work, 0x84f, 0x1600000ULL);
    AgxWriteQuad(Work, 0x857, Refs->Tail);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* The registers a vertex command is run with. */
static const ULONG AgxVertexSelectors[APPLE_AGX_VERTEX_REGISTER_COUNT] =
{
    0x01748, 0x10141, 0x1c039, 0x1c9c8, 0x1c041, 0x1c9d0, 0x1c0a1, 0x1c031, 0x1c9c0, 0x1c051,
    0x1c061, 0x10149, 0x10139, 0x10111, 0x1c9b0, 0x10119, 0x1c9b8, 0x1c958, 0x1c950, 0x1c930,
    0x1c880, 0x1c898, 0x1c948, 0x1c888, 0x1c890, 0x1c918, 0x1c079, 0x1c9d8, 0x1c089, 0x1c9e0,
    0x16c41, 0x1ca40, 0x1c9a8, 0x1c920, 0x10151, 0x1c199, 0x1c1a1, 0x1c1a9, 0x1c1b1, 0x1c1b9,
    0x1c8f8, 0x1c0b1, 0x1c850, 0x10131, 0x10121, 0x10129, 0x101b9, 0x1c069, 0x1c071, 0x1c081,
    0x1c0a9, 0x10171, 0x10169, 0x12099, 0x101e1, 0x1c9e8, 0x1a099, 0x1a0a1, 0x1a069, 0x1a071,
    0x1a0c9, 0x1a0d1, 0x101c9, 0x0d471, 0x1a0f1, 0x10799, 0x0a5a1, 0x0d419, 0x1c830, 0x1ca30,
    0x16c39, 0x1c910,
};

/* The registers a fragment command is run with. */
static const ULONG AgxFragmentSelectors[APPLE_AGX_FRAGMENT_REGISTER_COUNT] =
{
    0x01739, 0x10009, 0x15379, 0x15381, 0x15369, 0x15371, 0x15131, 0x15139, 0x100a1, 0x15069,
    0x15071, 0x16058, 0x10019, 0x100b1, 0x16030, 0x100d9, 0x10791, 0x16098, 0x15109, 0x15101,
    0x15021, 0x15211, 0x15049, 0x10051, 0x15321, 0x15301, 0x15309, 0x15311, 0x15319, 0x15349,
    0x15351, 0x15329, 0x15331, 0x15339, 0x15341, 0x15231, 0x15221, 0x15239, 0x15229, 0x15401,
    0x15421, 0x15409, 0x15429, 0x153c1, 0x15411, 0x153c9, 0x15431, 0x153d1, 0x15419, 0x153d9,
    0x15439, 0x16429, 0x16060, 0x16431, 0x10039, 0x16020, 0x16451, 0x15359, 0x100b8, 0x16461,
    0x16090, 0x120a1, 0x101e9, 0x160a8, 0x16068, 0x160b8, 0x1a0a9, 0x1a0b1, 0x1a079, 0x1a081,
    0x1a0d9, 0x1a0e1, 0x101c1, 0x0d469, 0x1a0f9, 0x1c838, 0x1ca28, 0x0a5a9, 0x0d429,
};

const ULONG *
NTAPI
AppleAgxSelectors(
    _In_ ULONG WorkType,
    _Out_ PULONG Count)
{
    switch (WorkType)
    {
        case APPLE_AGX_WORK_VERTEX:
            *Count = APPLE_AGX_VERTEX_REGISTER_COUNT;
            return AgxVertexSelectors;
        case APPLE_AGX_WORK_FRAGMENT:
            *Count = APPLE_AGX_FRAGMENT_REGISTER_COUNT;
            return AgxFragmentSelectors;
        case APPLE_AGX_WORK_COMPUTE:
            *Count = APPLE_AGX_COMPUTE_REGISTER_COUNT;
            return AgxComputeSelectors;
        default:
            *Count = 0;
            return NULL;
    }
}

/* Every engine's work image carries its registers the same way, at its own offset. */
NTSTATUS
NTAPI
AppleAgxWriteRegisters(
    _Inout_ PVOID Work,
    _In_ ULONG WorkType,
    _In_ const ULONG64 *Value)
{
    ULONG Count = 0, Index, At;
    const ULONG *Selector = AppleAgxSelectors(WorkType, &Count);

    if (!Work || !Value || !Selector)
        return STATUS_INVALID_PARAMETER;
    switch (WorkType)
    {
        case APPLE_AGX_WORK_VERTEX: At = APPLE_AGX_VERTEX_REGISTERS; break;
        case APPLE_AGX_WORK_FRAGMENT: At = APPLE_AGX_FRAGMENT_REGISTERS; break;
        default: At = APPLE_AGX_COMPUTE_REGISTERS; break;
    }
    for (Index = 0; Index < Count; ++Index)
    {
        AgxWriteLong(Work, At + APPLE_AGX_REGISTER_RECORD * Index, Selector[Index]);
        AgxWriteQuad(Work, At + APPLE_AGX_REGISTER_RECORD * Index + 4, Value[Index]);
    }
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* A block's record cycles through the list, so the list need not be as long as the buffer. */
ULONG
NTAPI
AppleAgxBlockRecord(
    _In_ ULONG Block)
{
    return APPLE_AGX_PB_RECORD * (Block % APPLE_AGX_PB_LIST_RECORDS);
}

NTSTATUS
NTAPI
AppleAgxDescribeParameterBuffer(
    _Out_writes_bytes_(APPLE_AGX_PB_MANAGER_SIZE) PVOID Manager,
    _In_ const APPLE_AGX_PARAMETER_BUFFER *Buffer)
{
    if (!Manager || !Buffer->PageList || !Buffer->BlockList || !Buffer->BlockControl ||
        !Buffer->Counter || !Buffer->Pages || !Buffer->Blocks || !Buffer->BlockBytes ||
        Buffer->Pages > APPLE_AGX_PB_PAGES || Buffer->Blocks > APPLE_AGX_PB_BLOCKS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Manager, APPLE_AGX_PB_MANAGER_SIZE);
    AgxWriteQuad(Manager, APPLE_AGX_PB_PAGE_LIST, Buffer->PageList);
    AgxWriteLong(Manager, APPLE_AGX_PB_PAGE_LIST_BYTES, Buffer->Pages * (ULONG)sizeof(ULONG));
    AgxWriteLong(Manager, APPLE_AGX_PB_PAGE_COUNT, Buffer->Pages);
    AgxWriteLong(Manager, APPLE_AGX_PB_RECORDS, APPLE_AGX_PB_LIST_RECORDS);
    AgxWriteLong(Manager, APPLE_AGX_PB_BLOCK_COUNT, Buffer->Blocks);
    AgxWriteQuad(Manager, APPLE_AGX_PB_BLOCK_LIST, Buffer->BlockList);
    AgxWriteQuad(Manager, APPLE_AGX_PB_BLOCK_CONTROL, Buffer->BlockControl);
    AgxWriteLong(Manager, APPLE_AGX_PB_LAST_PAGE, Buffer->Pages - 1);
    AgxWriteLong(Manager, APPLE_AGX_PB_BLOCK_BYTES, Buffer->BlockBytes);
    AgxWriteQuad(Manager, APPLE_AGX_PB_COUNTER, Buffer->Counter);
    AgxWriteLong(Manager, APPLE_AGX_PB_MAX_PAGES_A, APPLE_AGX_PB_PAGES);
    AgxWriteLong(Manager, APPLE_AGX_PB_MAX_PAGES_B, APPLE_AGX_PB_PAGES);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxDescribeBlockControl(
    _Out_writes_bytes_(APPLE_AGX_BLOCK_CONTROL_SIZE) PVOID Control,
    _Out_writes_bytes_(APPLE_AGX_MISC_SIZE) PVOID Misc,
    _In_ ULONG Blocks)
{
    if (!Control || !Misc || !Blocks || Blocks > APPLE_AGX_PB_BLOCKS)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Control, APPLE_AGX_BLOCK_CONTROL_SIZE);
    RtlZeroMemory(Misc, APPLE_AGX_MISC_SIZE);
    AgxWriteLong(Control, APPLE_AGX_BLOCK_CONTROL_BLOCKS, Blocks);
    AgxWriteLong(Misc, APPLE_AGX_MISC_ENABLE, 1);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

static
BOOLEAN
AgxRenderRefsValid(
    _In_ const APPLE_AGX_RENDER_REFS *Refs)
{
    return Refs->Work && Refs->Sequence && Refs->Hwdata && Refs->Notifier && Refs->Queue &&
           Refs->Scene && Refs->Manager && Refs->Pool && Refs->FirmwareStamp && Refs->Scratch;
}

static
VOID
AgxWriteTimestamps(
    _Inout_ PVOID Sequence,
    _In_ ULONG Start,
    _In_ ULONG End,
    _In_ ULONG64 Tail,
    _In_ ULONG64 Queue,
    _In_ ULONG Uuid)
{
    const ULONG At[2] = { Start, End };
    const ULONG Opcode[2] = { APPLE_AGX_OP_TIMESTAMP, APPLE_AGX_OP_TIMESTAMP_END };
    const ULONG64 Selected[2] = { Tail + APPLE_AGX_TAIL_PAIR, Tail + APPLE_AGX_TAIL_END };
    ULONG Index;

    for (Index = 0; Index < 2; ++Index)
    {
        AgxWriteLong(Sequence, At[Index], Opcode[Index]);
        AgxWriteQuad(Sequence, At[Index] + 0x04, Tail);
        AgxWriteQuad(Sequence, At[Index] + 0x0c, Tail + APPLE_AGX_TAIL_PAIR);
        AgxWriteQuad(Sequence, At[Index] + 0x14, Selected[Index]);
        AgxWriteQuad(Sequence, At[Index] + 0x1c, Queue);
        AgxWriteQuad(Sequence, At[Index] + 0x2c, Tail + APPLE_AGX_TAIL_COMPLETE);
        AgxWriteLong(Sequence, At[Index] + 0x34, Uuid);
    }
    AgxWriteLong(Sequence, End - 4, 1);
}

NTSTATUS
NTAPI
AppleAgxEncodeVertexSequence(
    _Out_writes_bytes_(APPLE_AGX_SEQUENCE_STORAGE) PVOID Sequence,
    _In_ const APPLE_AGX_RENDER_REFS *Refs,
    _In_ ULONG Context,
    _In_ ULONG Stamp)
{
    const ULONG Final = APPLE_AGX_VERTEX_FINALIZE_AT;
    ULONG64 Tail, Stats, Sampler, State;

    if (!Sequence || !Refs || !AgxRenderRefsValid(Refs) || Context >= APPLE_UAT_CONTEXTS)
        return STATUS_INVALID_PARAMETER;
    Tail = Refs->Work + APPLE_AGX_VERTEX_TAIL;
    Stats = Refs->Hwdata + APPLE_AGX_VERTEX_HWDATA;
    Sampler = Refs->Work + APPLE_AGX_VERTEX_SAMPLER;
    State = Refs->Work + APPLE_AGX_VERTEX_STATE;
    RtlZeroMemory(Sequence, APPLE_AGX_SEQUENCE_STORAGE);
    AgxWriteLong(Sequence, 0x00, APPLE_AGX_OP_VERTEX_START);
    AgxWriteQuad(Sequence, 0x14, Refs->Work + APPLE_AGX_VERTEX_REGISTERS);
    AgxWriteQuad(Sequence, 0x1c, Refs->Manager);
    AgxWriteQuad(Sequence, 0x24, Refs->Scene);
    AgxWriteQuad(Sequence, 0x2c, Stats);
    AgxWriteQuad(Sequence, 0x34, Refs->Queue);
    AgxWriteQuad(Sequence, 0x3c, Sampler);
    AgxWriteLong(Sequence, 0x44, Context);
    AgxWriteQuad(Sequence, 0x64, State);
    AgxWriteQuad(Sequence, 0x6c, Refs->Work + APPLE_AGX_VERTEX_SCRATCH_STATE);
    AgxWriteQuad(Sequence, 0x78, (ULONG64)APPLE_AGX_VERTEX_UUID << 32);
    AgxWriteQuad(Sequence, APPLE_AGX_VERTEX_POOL, Refs->Pool);
    AgxWriteQuad(Sequence, 0x1a4, Refs->Scratch);
    AgxWriteQuad(Sequence, 0x1b4, 1);
    AgxWriteQuad(Sequence, 0x1bc, Refs->Notifier + APPLE_AGX_NOTIFIER_MASK);
    AgxWriteTimestamps(Sequence, APPLE_AGX_VERTEX_TIMESTAMP_START, APPLE_AGX_VERTEX_TIMESTAMP_END, Tail,
                       Refs->Queue, APPLE_AGX_VERTEX_UUID);
    AgxWriteLong(Sequence, Final, APPLE_AGX_OP_VERTEX_FINALIZE);
    AgxWriteQuad(Sequence, Final + 0x04, Refs->Scene);
    AgxWriteQuad(Sequence, Final + 0x0c, Refs->Manager);
    AgxWriteQuad(Sequence, Final + 0x14, Stats);
    AgxWriteQuad(Sequence, Final + 0x1c, Refs->Queue);
    AgxWriteQuad(Sequence, Final + 0x24, Sampler);
    AgxWriteLong(Sequence, Final + 0x2c, Context);
    AgxWriteQuad(Sequence, Final + 0x34, State);
    AgxWriteLong(Sequence, Final + 0x40, APPLE_AGX_VERTEX_UUID);
    AgxWriteQuad(Sequence, Final + 0x44, Refs->FirmwareStamp);
    AgxWriteLong(Sequence, Final + 0x4c, Stamp);
    AgxWriteQuad(Sequence, Final + 0x74, Refs->Sequence + APPLE_AGX_VERTEX_POOL);
    AgxWriteLong(Sequence, Final + 0x7c, (ULONG)(0u - Final));
    AgxWriteQuad(Sequence, Final + 0x84, Tail + APPLE_AGX_TAIL_REQUEST);
    AgxWriteLong(Sequence, APPLE_AGX_VERTEX_END_AT, APPLE_AGX_OP_END);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxEncodeFragmentSequence(
    _Out_writes_bytes_(APPLE_AGX_SEQUENCE_STORAGE) PVOID Sequence,
    _In_ const APPLE_AGX_RENDER_REFS *Refs,
    _In_ ULONG Context,
    _In_ ULONG Stamp,
    _In_ ULONG QueueCommands)
{
    const ULONG Final = APPLE_AGX_FRAGMENT_FINALIZE_AT;
    ULONG64 Tail, Stats, Busy, Struct7, Flag;

    if (!Sequence || !Refs || !AgxRenderRefsValid(Refs) || Context >= APPLE_UAT_CONTEXTS ||
        Refs->ScratchBytes < APPLE_AGX_FRAGMENT_SCRATCH_MIN)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Tail = Refs->Work + APPLE_AGX_FRAGMENT_TAIL;
    Stats = Refs->Hwdata + APPLE_AGX_FRAGMENT_HWDATA;
    Busy = Refs->Work + APPLE_AGX_FRAGMENT_BUSY;
    Struct7 = Refs->Work + APPLE_AGX_FRAGMENT_STRUCT7;
    Flag = Refs->Work + APPLE_AGX_FRAGMENT_SCRATCH_FLAG;
    RtlZeroMemory(Sequence, APPLE_AGX_SEQUENCE_STORAGE);
    AgxWriteLong(Sequence, 0x00, APPLE_AGX_OP_FRAGMENT_START);
    AgxWriteQuad(Sequence, 0x14, Refs->Work + APPLE_AGX_FRAGMENT_REGISTERS);
    AgxWriteQuad(Sequence, 0x1c, Refs->Scene);
    AgxWriteQuad(Sequence, 0x24, Stats);
    AgxWriteQuad(Sequence, 0x2c, Busy);
    AgxWriteQuad(Sequence, 0x34, Refs->Work + APPLE_AGX_FRAGMENT_STRUCT6);
    AgxWriteQuad(Sequence, 0x3c, Struct7);
    AgxWriteQuad(Sequence, 0x44, Refs->Queue);
    AgxWriteQuad(Sequence, 0x4c, Refs->Work);
    AgxWriteLong(Sequence, 0x54, Context);
    AgxWriteLong(Sequence, 0x58, 1);
    AgxWriteQuad(Sequence, 0x68, QueueCommands);
    AgxWriteQuad(Sequence, 0x74, Flag);
    AgxWriteQuad(Sequence, 0x7c, Refs->Work + APPLE_AGX_FRAGMENT_SCRATCH_STATE);
    AgxWriteLong(Sequence, 0x98, APPLE_AGX_FRAGMENT_UUID);
    AgxWriteQuad(Sequence, APPLE_AGX_FRAGMENT_POOL, Refs->Pool);
    AgxWriteQuad(Sequence, 0x1bc, Refs->Scratch);
    AgxWriteQuad(Sequence, 0x1d4, Refs->Notifier + APPLE_AGX_NOTIFIER_MASK);
    AgxWriteTimestamps(Sequence, APPLE_AGX_FRAGMENT_TIMESTAMP_START, APPLE_AGX_FRAGMENT_TIMESTAMP_END, Tail,
                       Refs->Queue, APPLE_AGX_FRAGMENT_UUID);
    AgxWriteLong(Sequence, Final, APPLE_AGX_OP_FRAGMENT_FINALIZE);
    AgxWriteLong(Sequence, Final + 0x04, APPLE_AGX_FRAGMENT_UUID);
    AgxWriteQuad(Sequence, Final + 0x0c, Refs->FirmwareStamp);
    AgxWriteLong(Sequence, Final + 0x14, Stamp);
    AgxWriteQuad(Sequence, Final + 0x1c, Refs->Scene);
    AgxWriteQuad(Sequence, Final + 0x24, Refs->Manager);
    AgxWriteQuad(Sequence, Final + 0x2c, 1);
    AgxWriteQuad(Sequence, Final + 0x34, Stats);
    AgxWriteQuad(Sequence, Final + 0x3c, Struct7);
    AgxWriteQuad(Sequence, Final + 0x44, Busy);
    AgxWriteQuad(Sequence, Final + 0x4c, Refs->Queue);
    AgxWriteQuad(Sequence, Final + 0x54, Refs->Work);
    AgxWriteQuad(Sequence, Final + 0x5c, Context);
    AgxWriteQuad(Sequence, Final + 0x64, Flag);
    AgxWriteQuad(Sequence, Final + 0x9c, Refs->Sequence + APPLE_AGX_FRAGMENT_POOL);
    AgxWriteLong(Sequence, Final + 0xa4, (ULONG)(0u - Final));
    AgxWriteLong(Sequence, Final + 0xa8, 1);
    AgxWriteQuad(Sequence, Final + 0xac, Tail + APPLE_AGX_TAIL_REQUEST);
    AgxWriteLong(Sequence, APPLE_AGX_FRAGMENT_END_AT, APPLE_AGX_OP_END);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}
