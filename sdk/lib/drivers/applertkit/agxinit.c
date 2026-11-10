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
    BOOLEAN Shared = Class == APPLE_AGX_CLASS_SHARED || Class == APPLE_AGX_CLASS_SHARED_CACHED;

    Protection->Access = Shared ? APPLE_UAT_ACCESS_BOTH : APPLE_UAT_ACCESS_FIRMWARE;
    Protection->Memory = (Class == APPLE_AGX_CLASS_FIRMWARE_CACHED || Class == APPLE_AGX_CLASS_SHARED_CACHED)
                             ? APPLE_UAT_MEMORY_CACHED
                             : APPLE_UAT_MEMORY_UNCACHED;
    Protection->Read = Shared;
    Protection->Write = TRUE;
}

VOID
NTAPI
AppleAgxLeafProtection(
    _In_ UCHAR Leaf,
    _Out_ PAPPLE_UAT_PROTECTION Protection)
{
    Protection->Access = (Leaf == APPLE_AGX_LEAF_PB_LIST) ? APPLE_UAT_ACCESS_BOTH : APPLE_UAT_ACCESS_GPU;
    Protection->Memory = (Leaf == APPLE_AGX_LEAF_UNCACHED) ? APPLE_UAT_MEMORY_UNCACHED : APPLE_UAT_MEMORY_CACHED;
    Protection->Read = Leaf == APPLE_AGX_LEAF_PB_LIST;
    Protection->Write = TRUE;
}

typedef struct _AGX_CLIENT_RANGE
{
    ULONG64 Start;
    ULONG64 End;
} AGX_CLIENT_RANGE;

static const AGX_CLIENT_RANGE AgxClientRanges[] =
{
    { APPLE_AGX_CLIENT_APERTURE, APPLE_AGX_CLIENT_APERTURE_END },
    { APPLE_AGX_CLIENT_PRE0, APPLE_AGX_CLIENT_PRE0 + APPLE_AGX_CLIENT_PASS_SLOTS * APPLE_AGX_CLIENT_PASS_STRIDE },
    { APPLE_AGX_CLIENT_PB_PAGE_LIST, APPLE_AGX_CLIENT_PB_PAGE_LIST + APPLE_AGX_PB_PAGE_LIST_SIZE },
};

BOOLEAN
NTAPI
AppleAgxClientReserved(
    _In_ ULONG64 Address,
    _In_ ULONG64 Range)
{
    ULONG64 End = Address + Range;
    ULONG Index;

    if (End < Address)
        return TRUE;
    for (Index = 0; Index < RTL_NUMBER_OF(AgxClientRanges); ++Index)
    {
        if (Address < AgxClientRanges[Index].End && End > AgxClientRanges[Index].Start)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
AgxClientInside(
    _In_ ULONG64 Address,
    _In_ ULONG64 Range)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(AgxClientRanges); ++Index)
    {
        if (Address >= AgxClientRanges[Index].Start && Address < AgxClientRanges[Index].End &&
            Range <= AgxClientRanges[Index].End - Address)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* An object at a fixed address starts at that address's page; any other object takes the next
 * free pages of the host's window and leaves one unmapped page after itself. */
NTSTATUS
NTAPI
AppleAgxObjectPlace(
    _Inout_ PAPPLE_AGX_HEAP Heap,
    _In_ const APPLE_AGX_REQUEST *Request,
    _Out_ PULONG64 Page,
    _Out_ PSIZE_T Bytes)
{
    ULONG64 Offset, Length, Base;

    *Page = 0;
    *Bytes = 0;
    if (!Request->Size || Request->Size > APPLE_UAT_HOST_SIZE)
        return STATUS_INVALID_PARAMETER;
    if (Request->Hidden)
    {
        Length = (Request->Size + APPLE_UAT_PAGE_SIZE - 1) & ~(APPLE_UAT_PAGE_SIZE - 1);
        if (Request->At || Request->Offset || Request->Lower || Request->CachedBytes || !Request->Client ||
            (Request->Client & (APPLE_UAT_PAGE_SIZE - 1)) || Request->Leaf == APPLE_AGX_LEAF_NONE ||
            Request->Leaf > APPLE_AGX_LEAF_PB_LIST || !AgxClientInside(Request->Client, Length))
        {
            return STATUS_INVALID_PARAMETER;
        }
        *Bytes = (SIZE_T)Length;
        return STATUS_SUCCESS;
    }
    if (Request->At)
    {
        if (Request->Offset)
            return STATUS_INVALID_PARAMETER;
        Offset = Request->At & (APPLE_UAT_PAGE_SIZE - 1);
        Base = Request->At - Offset;
    }
    else
    {
        if (Request->Offset >= APPLE_UAT_PAGE_SIZE)
            return STATUS_INVALID_PARAMETER;
        Offset = Request->Offset;
        Base = Heap->Next;
    }
    Length = (Offset + Request->Size + APPLE_UAT_PAGE_SIZE - 1) & ~(APPLE_UAT_PAGE_SIZE - 1);
    if (Base < APPLE_UAT_HOST_BASE || Base - APPLE_UAT_HOST_BASE >= APPLE_UAT_HOST_SIZE ||
        Length > APPLE_UAT_HOST_SIZE - (Base - APPLE_UAT_HOST_BASE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Request->At && (Base < Heap->Next || Heap->End < Base || Heap->End - Base < Length + APPLE_UAT_PAGE_SIZE))
        return STATUS_NO_MEMORY;
    if ((Request->CachedBytes & (APPLE_UAT_PAGE_SIZE - 1)) || Request->CachedBytes >= Length)
        return STATUS_INVALID_PARAMETER;
    if (Request->Client && ((Request->Client & (APPLE_UAT_PAGE_SIZE - 1)) || Request->Leaf == APPLE_AGX_LEAF_NONE ||
                            Request->Leaf > APPLE_AGX_LEAF_PB_LIST || !AgxClientInside(Request->Client, Length)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Request->Client && (Request->Leaf != APPLE_AGX_LEAF_NONE || Request->Lower))
        return STATUS_INVALID_PARAMETER;
    if (!Request->At)
        Heap->Next = Base + Length + APPLE_UAT_PAGE_SIZE;
    *Page = Base;
    *Bytes = (SIZE_T)Length;
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

    ULONG Flag;

    if (!Init->Virtual[APPLE_AGX_OBJECT_RUNTIME] || !Init->Virtual[APPLE_AGX_OBJECT_FLAGS])
        return FALSE;
    KeMemoryBarrier();
    RtlCopyMemory(&Value, (PUCHAR)Init->Virtual[APPLE_AGX_OBJECT_RUNTIME] + APPLE_AGX_RUNTIME_READY,
                  sizeof(Value));
    RtlCopyMemory(&Flag, (PUCHAR)Init->Virtual[APPLE_AGX_OBJECT_FLAGS] + APPLE_AGX_FLAGS_READY, sizeof(Flag));
    return Value == APPLE_AGX_RUNTIME_READY_MAGIC && Flag == 1;
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
        !Queue->State || !Queue->Ring || !Queue->Uuid)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Queue->InfoVirtual, APPLE_AGX_QINFO_SIZE);
    RtlZeroMemory(Queue->StateVirtual, APPLE_AGX_QSTATE_SIZE);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_STATE, Queue->State);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_RING, Queue->Ring);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_JOB_LIST, Queue->JobList);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_SCRATCH, Queue->Scratch);
    AgxWriteLong(Queue->InfoVirtual, APPLE_AGX_QINFO_UNKNOWN_2C, 0xffffffffUL);
    AgxWriteLong(Queue->InfoVirtual, APPLE_AGX_QINFO_CLASS, APPLE_AGX_QUEUE_CLASS);
    AgxWriteLong(Queue->InfoVirtual, APPLE_AGX_QINFO_CLASS_B, APPLE_AGX_QUEUE_CLASS);
    AgxWriteQuad(Queue->InfoVirtual, APPLE_AGX_QINFO_MASK, APPLE_AGX_QINFO_MASK_VALUE);
    AgxWriteLong(Queue->InfoVirtual, APPLE_AGX_QINFO_UNKNOWN_48, APPLE_AGX_QUEUE_CLASS);
    AgxWriteLong(Queue->InfoVirtual, APPLE_AGX_QINFO_UNKNOWN_4C, 0xffffffffUL);
    AgxWriteLong(Queue->InfoVirtual, APPLE_AGX_QINFO_UUID, Queue->Uuid);
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
    NTSTATUS Status;

    *Head = 0;
    Status = AppleAgxQueueAppend(Queue, Work);
    if (NT_SUCCESS(Status))
        AppleAgxQueuePublish(Queue, Head);
    return Status;
}

/* Work is appended behind the host's cursor without moving it; the cursor moves once for all of
 * it, so the firmware never sees half of what belongs together. */
NTSTATUS
NTAPI
AppleAgxQueueAppend(
    _Inout_ PAPPLE_AGX_QUEUE Queue,
    _In_ ULONG64 Work)
{
    ULONG Read = 0, Next;

    if (!Queue->RingVirtual || !Queue->StateVirtual || !Queue->Entries || !Work || Queue->Write >= Queue->Entries)
        return STATUS_INVALID_PARAMETER;
    KeMemoryBarrier();
    RtlCopyMemory(&Read, (PUCHAR)Queue->StateVirtual + APPLE_AGX_QSTATE_READ, sizeof(Read));
    if (Read >= Queue->Entries)
        return STATUS_DEVICE_DATA_ERROR;
    Next = (Queue->Write + 1) % Queue->Entries;
    if (Next == Read)
        return STATUS_DEVICE_BUSY;
    AgxWriteQuad(Queue->RingVirtual, Queue->Write * APPLE_AGX_WORK_ENTRY, Work);
    Queue->Write = Next;
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAgxQueuePublish(
    _Inout_ PAPPLE_AGX_QUEUE Queue,
    _Out_ PUSHORT Head)
{
    KeMemoryBarrier();
    AgxWriteLong(Queue->StateVirtual, APPLE_AGX_QSTATE_WRITE, Queue->Write);
    KeMemoryBarrier();
    *Head = (USHORT)Queue->Write;
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
    ((PUCHAR)SchedContext)[0x00] = 0xff;
    ((PUCHAR)SchedContext)[0x01] = 0xff;
    ((PUCHAR)SchedContext)[0x05] = 0x01;
    ((PUCHAR)SchedContext)[0x1e] = 0xff;
    ((PUCHAR)SchedContext)[0x23] = 0x02;
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxDescribeManager(
    _Out_writes_bytes_(APPLE_AGX_MANAGER_SIZE) PVOID Manager,
    _In_ ULONG Id,
    _In_ ULONG Index,
    _In_ ULONG Seeds,
    _In_ ULONG64 Freelist,
    _In_ ULONG64 PoolTable,
    _In_ ULONG64 Counter)
{
    if (!Manager || !Freelist || !PoolTable || !Counter || !Id || !Seeds || Seeds > APPLE_AGX_FREELIST_SEEDS)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Manager, APPLE_AGX_MANAGER_SIZE);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_ID, Id);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_INDEX, Index);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_ONE, 1);
    AgxWriteQuad(Manager, APPLE_AGX_MANAGER_FREELIST, Freelist);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_UNKNOWN_1C, APPLE_AGX_MANAGER_UNKNOWN_1C_VALUE);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_SEEDS_A, Seeds);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_SEEDS_B, Seeds);
    AgxWriteQuad(Manager, APPLE_AGX_MANAGER_POOL_TABLE, PoolTable);
    AgxWriteLong(Manager, APPLE_AGX_MANAGER_UNKNOWN_44, APPLE_AGX_MANAGER_UNKNOWN_44_VALUE);
    AgxWriteQuad(Manager, APPLE_AGX_MANAGER_COUNTER, Counter);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* Every pool is named in the table and broken into units for the freelist, which is what a large
 * workgroup's memory comes out of. */
NTSTATUS
NTAPI
AppleAgxSeedPools(
    _Out_writes_bytes_(Pools * APPLE_AGX_POOL_STRIDE) PVOID PoolTable,
    _Out_writes_bytes_(Pools * APPLE_AGX_POOL_UNITS * 8) PVOID Freelist,
    _In_reads_(Pools) const ULONG64 *Pool,
    _In_ ULONG Pools,
    _Out_ PULONG Seeds)
{
    ULONG Index, Unit, Seed = 0;

    *Seeds = 0;
    if (!PoolTable || !Freelist || !Pool || !Pools || Pools > APPLE_AGX_POOLS)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Pools; ++Index)
    {
        if (!Pool[Index] || (Pool[Index] & (APPLE_AGX_POOL_UNIT - 1)) || (Pool[Index] & APPLE_AGX_POOL_VALID))
            return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(PoolTable, (SIZE_T)Pools * APPLE_AGX_POOL_STRIDE);
    for (Index = 0; Index < Pools; ++Index)
    {
        AgxWriteQuad(PoolTable, Index * APPLE_AGX_POOL_STRIDE, Pool[Index] | APPLE_AGX_POOL_VALID);
        for (Unit = 0; Unit < APPLE_AGX_POOL_UNITS; ++Unit)
        {
            AgxWriteQuad(Freelist, Seed * (ULONG)sizeof(ULONG64), Pool[Index] + (ULONG64)Unit * APPLE_AGX_POOL_UNIT);
            ++Seed;
        }
    }
    KeMemoryBarrier();
    *Seeds = Seed;
    return STATUS_SUCCESS;
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
static
VOID
AgxWriteTimestamps(
    _Inout_ PVOID Sequence,
    _In_ ULONG Start,
    _In_ ULONG End,
    _In_ ULONG64 Tail,
    _In_ ULONG64 Queue,
    _In_ ULONG Uuid,
    _In_ BOOLEAN Requested);

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
    AgxWriteTimestamps(Sequence, APPLE_AGX_COMPUTE_TIMESTAMP_START, APPLE_AGX_COMPUTE_TIMESTAMP_END,
                       Refs->Work + APPLE_AGX_COMPUTE_TAIL, Refs->Queue, APPLE_AGX_COMPUTE_UUID,
                       Refs->UserTimestamps[0] || Refs->UserTimestamps[1]);
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
    AgxWriteQuad(Work, 0x828, Refs->UserTimestamps[0]);
    AgxWriteQuad(Work, 0x830, Refs->UserTimestamps[1]);
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
    ULONG Pages = APPLE_AGX_PB_PAGES_PER_BLOCK * Buffer->Blocks;

    if (!Manager || !Buffer->PageList || !Buffer->BlockList || !Buffer->BlockControl || !Buffer->Counter ||
        !Buffer->Blocks || Buffer->Blocks > Buffer->MaxBlocks || Buffer->MaxBlocks > APPLE_AGX_PB_MAX_BLOCKS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Manager, APPLE_AGX_PB_MANAGER_SIZE);
    AgxWriteQuad(Manager, APPLE_AGX_PB_PAGE_LIST, Buffer->PageList);
    AgxWriteLong(Manager, APPLE_AGX_PB_PAGE_LIST_BYTES, APPLE_AGX_PB_PAGE_LIST_SIZE);
    AgxWriteLong(Manager, APPLE_AGX_PB_PAGE_COUNT, Pages);
    AgxWriteLong(Manager, APPLE_AGX_PB_RECORDS, APPLE_AGX_PB_LIST_RECORDS);
    AgxWriteLong(Manager, APPLE_AGX_PB_BLOCK_COUNT, Buffer->Blocks);
    AgxWriteQuad(Manager, APPLE_AGX_PB_BLOCK_LIST, Buffer->BlockList);
    AgxWriteQuad(Manager, APPLE_AGX_PB_BLOCK_CONTROL, Buffer->BlockControl);
    AgxWriteLong(Manager, APPLE_AGX_PB_LAST_PAGE, Pages - 1);
    AgxWriteLong(Manager, APPLE_AGX_PB_BLOCK_BYTES, APPLE_AGX_PB_BLOCK_SIZE);
    AgxWriteQuad(Manager, APPLE_AGX_PB_COUNTER, Buffer->Counter);
    AgxWriteLong(Manager, APPLE_AGX_PB_MAX_PAGES_A, APPLE_AGX_PB_PAGES_PER_BLOCK * Buffer->MaxBlocks);
    AgxWriteLong(Manager, APPLE_AGX_PB_MAX_PAGES_B, APPLE_AGX_PB_PAGES_PER_BLOCK * Buffer->MaxBlocks);
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
    AgxWriteLong(Control, APPLE_AGX_BLOCK_CONTROL_PRODUCER, Blocks);
    AgxWriteLong(Misc, APPLE_AGX_MISC_ENABLE, 1);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* The lists name the parameter buffer's pages and blocks by their compact address in units of a
 * page. Growth only adds blocks: the firmware takes their pages itself. */
NTSTATUS
NTAPI
AppleAgxWriteBlocks(
    _Inout_updates_bytes_opt_(APPLE_AGX_PB_PAGE_LIST_SIZE) PVOID PageList,
    _Inout_updates_bytes_(APPLE_AGX_PB_BLOCK_LIST_SIZE) PVOID BlockList,
    _In_ ULONG64 Heap,
    _In_ ULONG First,
    _In_ ULONG Count)
{
    ULONG Block, Page;

    if (!BlockList || !Count || First > APPLE_AGX_PB_BLOCKS || Count > APPLE_AGX_PB_BLOCKS - First ||
        (Heap & (APPLE_AGX_PB_BLOCK_SIZE - 1)) || Heap < APPLE_AGX_COMPACT_BASE ||
        Heap + (ULONG64)APPLE_AGX_PB_BLOCKS * APPLE_AGX_PB_BLOCK_SIZE > APPLE_AGX_COMPACT_END)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Block = First; Block < First + Count; ++Block)
    {
        ULONG64 At = Heap - APPLE_AGX_COMPACT_BASE + (ULONG64)Block * APPLE_AGX_PB_BLOCK_SIZE;

        AgxWriteQuad(BlockList, AppleAgxBlockRecord(Block), At / APPLE_AGX_PB_PAGE_BYTES);
        if (!PageList)
            continue;
        for (Page = 0; Page < APPLE_AGX_PB_PAGES_PER_BLOCK; ++Page)
        {
            AgxWriteLong(PageList, (Block * APPLE_AGX_PB_PAGES_PER_BLOCK + Page) * (ULONG)sizeof(ULONG),
                         (ULONG)((At + (ULONG64)Page * APPLE_AGX_PB_PAGE_BYTES) / APPLE_AGX_PB_PAGE_BYTES));
        }
    }
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAgxC1Record(
    _Out_writes_bytes_(APPLE_AGX_C1_RECORD) PVOID Record,
    _In_ ULONG Blocks)
{
    RtlZeroMemory(Record, APPLE_AGX_C1_RECORD);
    AgxWriteLong(Record, 0, APPLE_AGX_C1_TAG);
    AgxWriteLong(Record, 4, APPLE_AGX_PB_PAGES_PER_BLOCK * Blocks);
}

NTSTATUS
NTAPI
AppleAgxEncodeInitBm(
    _Out_writes_bytes_(APPLE_AGX_INIT_BM_SIZE) PVOID Record,
    _In_ ULONG Context,
    _In_ ULONG Blocks,
    _In_ ULONG64 Manager,
    _In_ ULONG Stamp)
{
    if (!Record || !Manager || !Blocks || Blocks > APPLE_AGX_PB_MAX_BLOCKS || !Context ||
        Context >= APPLE_UAT_CONTEXTS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Record, APPLE_AGX_INIT_BM_SIZE);
    AgxWriteLong(Record, 0x00, APPLE_AGX_OP_INIT_BM);
    AgxWriteLong(Record, 0x04, Context);
    AgxWriteLong(Record, 0x10, Blocks);
    AgxWriteQuad(Record, 0x14, Manager);
    AgxWriteLong(Record, 0x1c, Stamp);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

/* A request names the render's context, the parameter buffer's only slot and the next request
 * number. It is granted up to ten blocks of what is backed below the maximum; past that it is
 * refused, which leaves the firmware without new pages. */
NTSTATUS
NTAPI
AppleAgxAnswerGrowth(
    _Inout_ PAPPLE_AGX_GROWTH Growth,
    _In_reads_(APPLE_AGX_RECORD_WORDS) const ULONG *Request,
    _In_ ULONG Context,
    _Out_writes_(APPLE_AGX_RECORD_WORDS) PULONG Reply,
    _Out_ PULONG First,
    _Out_ PULONG Count)
{
    ULONG Limit, Grant;

    RtlZeroMemory(Reply, APPLE_AGX_RECORD_WORDS * sizeof(ULONG));
    *First = 0;
    *Count = 0;
    if (Request[0] != APPLE_AGX_EVENT_GROW || Request[1] != Context || Request[2] != 0 ||
        Request[3] < Growth->Counter || Growth->Posted > Growth->Max || Growth->Max > APPLE_AGX_PB_MAX_BLOCKS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Limit = min(Growth->Max, Growth->Backed);
    Grant = (Limit > Growth->Posted) ? min(Limit - Growth->Posted, APPLE_AGX_PB_GROW_STEP) : 0;
    Reply[0] = APPLE_AGX_DEVCTL_GROW_REPLY;
    Reply[1] = Grant ? 1 : 0;
    Reply[2] = Request[2];
    Reply[3] = Request[1];
    Reply[4] = Request[3];
    Reply[5] = Request[4];
    Reply[6] = Request[5];
    Reply[7] = Request[6];
    Growth->Counter = Request[3] + 1;
    if (!Grant)
    {
        ++Growth->Refused;
        return STATUS_SUCCESS;
    }
    *First = Growth->Posted;
    *Count = Grant;
    Growth->Posted += Grant;
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

/* Both records name the work's own cells; the user pair is named only when the command asked
 * for timestamps. */
static
VOID
AgxWriteTimestamps(
    _Inout_ PVOID Sequence,
    _In_ ULONG Start,
    _In_ ULONG End,
    _In_ ULONG64 Tail,
    _In_ ULONG64 Queue,
    _In_ ULONG Uuid,
    _In_ BOOLEAN Requested)
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
        AgxWriteQuad(Sequence, At[Index] + 0x24, Requested ? Tail + APPLE_AGX_TAIL_USER : 0);
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
                       Refs->Queue, APPLE_AGX_VERTEX_UUID, Refs->Requested);
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
                       Refs->Queue, APPLE_AGX_FRAGMENT_UUID, Refs->Requested);
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

NTSTATUS
NTAPI
AppleAgxDescribeGeometry(
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG UtileWidth,
    _In_ ULONG UtileHeight,
    _In_ ULONG Samples,
    _In_ ULONG Layers,
    _In_ ULONG Clusters,
    _Out_ PAPPLE_AGX_GEOMETRY Geometry)
{
    ULONG TilesX, TilesY, MacroX, MacroY, UtilesPerTile, SamplesLog2;
    ULONG64 PerCluster;

    if (!Geometry || !Width || !Height || Width > APPLE_AGX_MAX_DIMENSION || Height > APPLE_AGX_MAX_DIMENSION ||
        !Layers || Layers > APPLE_AGX_MAX_LAYERS || !Clusters)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!(UtileWidth == 32 && (UtileHeight == 32 || UtileHeight == 16)) && !(UtileWidth == 16 && UtileHeight == 16))
        return STATUS_INVALID_PARAMETER;
    switch (Samples)
    {
        case 1: SamplesLog2 = 0; break;
        case 2: SamplesLog2 = 1; break;
        case 4: SamplesLog2 = 2; break;
        default: return STATUS_INVALID_PARAMETER;
    }
    UtilesPerTile = (APPLE_AGX_TILE_PIXELS / UtileWidth) * (APPLE_AGX_TILE_PIXELS / UtileHeight);
    TilesX = (Width + APPLE_AGX_TILE_PIXELS - 1) / APPLE_AGX_TILE_PIXELS;
    TilesY = (Height + APPLE_AGX_TILE_PIXELS - 1) / APPLE_AGX_TILE_PIXELS;
    MacroX = (((TilesX + APPLE_AGX_MACROTILES - 1) / APPLE_AGX_MACROTILES) + 3) & ~3u;
    MacroY = (((TilesY + APPLE_AGX_MACROTILES - 1) / APPLE_AGX_MACROTILES) + 3) & ~3u;
    Geometry->Layers = Layers;
    Geometry->Tiles = TilesX * TilesY;
    Geometry->Pixels = (Width - 1) | ((Height - 1) << 16);
    Geometry->Screen = (TilesX - 1) | ((TilesY - 1) << 12);
    Geometry->XBlocks = 3 * MacroX | ((2 * MacroX) << 9) | (MacroX << 18);
    Geometry->YBlocks = 3 * MacroY | ((2 * MacroY) << 9) | (MacroY << 18);
    Geometry->Blocks = MacroX * MacroY;
    Geometry->RegionStride = ((APPLE_AGX_REGION_ENTRY * Geometry->Blocks * UtilesPerTile + 3) & ~3u) / 4;
    Geometry->TpcStride = APPLE_AGX_TPC_ENTRY * UtilesPerTile * Geometry->Blocks / 4;
    Geometry->IspBlocks = (MacroY * (APPLE_AGX_TILE_PIXELS / UtileHeight)) |
                          ((MacroX * (APPLE_AGX_TILE_PIXELS / UtileWidth)) << 16);
    Geometry->UtileConfig = ((UtileWidth / 16) << 12) | ((UtileHeight / 16) << 14) | SamplesLog2;
    Geometry->TilemapBytes = (ULONG64)4 * Geometry->RegionStride * APPLE_AGX_MACROTILES * APPLE_AGX_MACROTILES * Layers;
    PerCluster = (ULONG64)4 * Geometry->TpcStride * APPLE_AGX_MACROTILES * APPLE_AGX_MACROTILES * Layers;
    if (PerCluster > MAXULONG64 / Clusters)
        return STATUS_INVALID_PARAMETER;
    Geometry->TpcBytes = PerCluster * Clusters;
    return STATUS_SUCCESS;
}

static
BOOLEAN
AgxGpuAddressValid(
    _In_ ULONG64 Address)
{
    return Address && !(Address & 3) && Address < (1ULL << APPLE_AGX_GPU_ADDRESS_BITS);
}

static
BOOLEAN
AgxCompact(
    _In_ ULONG64 Address,
    _Out_ PULONG64 Compact)
{
    if (Address < APPLE_AGX_COMPACT_BASE || Address >= APPLE_AGX_COMPACT_END)
        return FALSE;
    *Compact = Address - APPLE_AGX_COMPACT_BASE;
    return TRUE;
}

NTSTATUS
NTAPI
AppleAgxEncodeVertexWork(
    _Out_writes_bytes_(APPLE_AGX_VERTEX_WORK_SIZE) PVOID Work,
    _In_ const APPLE_AGX_VERTEX_INPUT *Input,
    _In_ const APPLE_AGX_GEOMETRY *Geometry,
    _In_ ULONG Context,
    _In_ ULONG StampValue)
{
    const ULONG Tail = APPLE_AGX_VERTEX_TAIL, State = APPLE_AGX_VERTEX_STATE;
    ULONG64 Value[APPLE_AGX_VERTEX_REGISTER_COUNT];
    ULONG64 Tilemap, Tpc, Heap, Preempt[3], User, LayerMetadata, LayerMode;
    ULONG Index;

    if (!Work || !Input || !Geometry || Context >= APPLE_UAT_CONTEXTS || !Input->PageCount ||
        !Geometry->TpcBytes || !Geometry->Layers || !AgxGpuAddressValid(Input->WorkClient) ||
        !AgxGpuAddressValid(Input->WorkClient + APPLE_AGX_VERTEX_WORK_SIZE - 3) || !Input->Notifier ||
        !Input->Manager || !Input->Scene || !Input->Empty || !Input->Sequence || !Input->Pool ||
        !Input->Scratch || !Input->Stamp || !Input->FirmwareStamp || Input->Vdm >= APPLE_AGX_VDM_LIMIT ||
        (Input->Vdm & 3) || !AgxCompact(Input->Tilemap, &Tilemap) || !AgxCompact(Input->Tpc, &Tpc) ||
        !AgxCompact(Input->Heap, &Heap) ||
        !AgxCompact(Input->Heap + (Geometry->Layers > 1 ? 0x200 : 0), &LayerMetadata) ||
        !AgxCompact(Input->SceneUser + 0x40, &User))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < 3; ++Index)
    {
        if (!AgxCompact(Input->Preempt[Index], &Preempt[Index]))
            return STATUS_INVALID_PARAMETER;
    }
    if (Geometry->Layers > 1)
        LayerMode = 0xc000 | (Input->ProcessEmptyTiles ? 0x2000 : 0x1000) | (Geometry->Layers - 1);
    else
        LayerMode = 0x8000;

    RtlZeroMemory(Value, sizeof(Value));
    Value[0] = 1;
    Value[1] = 0x200;
    Value[2] = Tilemap;
    Value[3] = Tilemap;
    Value[6] = Tpc;
    Value[7] = Heap | (1ULL << 63);
    Value[8] = Heap | (1ULL << 63);
    Value[9] = 0x003a0012006b0003ULL;
    Value[10] = 1;
    Value[11] = Geometry->UtileConfig;
    Value[12] = Input->Multisample;
    Value[13] = Preempt[0];
    Value[14] = Preempt[0];
    Value[15] = Preempt[1];
    Value[16] = Preempt[1];
    Value[17] = 1;
    Value[18] = Preempt[2] | (1ULL << 50);
    Value[20] = Input->Vdm;
    Value[24] = 1;
    Value[26] = LayerMetadata;
    Value[27] = LayerMetadata;
    Value[32] = Input->PageCount;
    Value[40] = 0x8860;
    Value[41] = Geometry->RegionStride;
    Value[42] = Geometry->RegionStride;
    Value[43] = 0x88;
    Value[44] = 2 | (Input->Ppp & 0x201);
    Value[45] = Geometry->Pixels;
    Value[46] = Geometry->Screen;
    Value[47] = Geometry->XBlocks;
    Value[48] = Geometry->YBlocks;
    Value[49] = Geometry->Blocks;
    Value[50] = Geometry->TpcStride;
    Value[51] = 0x100;
    Value[52] = LayerMode;
    Value[53] = 0x1c;
    Value[54] = 0x1c;
    Value[64] = 8;
    Value[65] = 0xff0000;
    Value[66] = 0x7f00400020ULL;
    Value[67] = 0x200000001ULL;
    Value[69] = User;
    Value[70] = User;
    Value[71] = 0xa000000000ULL | (Input->InitialSceneEntry & 0xfffffffffULL) | 1;

    RtlZeroMemory(Work, APPLE_AGX_VERTEX_WORK_SIZE);
    AgxWriteQuad(Work, 0x004, 1);
    AgxWriteLong(Work, 0x00c, Context);
    AgxWriteQuad(Work, 0x014, Input->Notifier);
    AgxWriteQuad(Work, 0x024, Input->Manager);
    AgxWriteQuad(Work, 0x02c, Input->Scene);
    AgxWriteQuad(Work, 0x034, Input->Empty);
    AppleAgxWriteRegisters(Work, APPLE_AGX_WORK_VERTEX, Value);
    AgxWriteQuad(Work, 0x740, Input->WorkClient + APPLE_AGX_VERTEX_REGISTERS);
    AgxWriteWord(Work, 0x748, APPLE_AGX_VERTEX_REGISTER_COUNT);
    AgxWriteWord(Work, 0x74a, APPLE_AGX_VERTEX_REGISTER_COUNT * APPLE_AGX_REGISTER_RECORD);
    AgxWriteQuad(Work, 0x760, Input->Tpc);
    AgxWriteQuad(Work, 0x768, Geometry->TpcBytes);
    AgxWriteQuad(Work, 0x770, Input->Sequence);
    AgxWriteLong(Work, 0x778, APPLE_AGX_VERTEX_SEQUENCE_SIZE);
    AgxWriteLong(Work, 0x77c, 1);
    AgxWriteLong(Work, 0x780, StampValue);
    AgxWriteQuad(Work, State + 0x24, Preempt[0]);
    AgxWriteLong(Work, State + 0xbc, 0x67);
    AgxWriteLong(Work, State + 0xc4, 0xffffffff);
    AgxWriteLong(Work, State + 0xd0, 1);
    AgxWriteQuad(Work, State + 0xe0, Input->Stamp);
    AgxWriteQuad(Work, State + 0xe8, Input->FirmwareStamp);
    AgxWriteLong(Work, State + 0xf0, StampValue);
    AgxWriteLong(Work, State + 0xfc, 1);
    AgxWriteLong(Work, State + 0x100, APPLE_AGX_VERTEX_UUID);
    AgxWriteQuad(Work, Tail + 0x08, Input->Timestamps[0]);
    AgxWriteQuad(Work, Tail + 0x10, Input->Timestamps[1]);
    AgxWriteQuad(Work, Tail + 0x18, Input->UserTimestamps[0]);
    AgxWriteQuad(Work, Tail + 0x20, Input->UserTimestamps[1]);
    AgxWriteQuad(Work, Tail + 0x2e, Input->Pool);
    ((PUCHAR)Work)[Tail + 0x36] = 1;
    AgxWriteQuad(Work, Tail + 0x37, 0xad0000);
    AgxWriteQuad(Work, Tail + 0x3f, 0x1600000);
    AgxWriteQuad(Work, Tail + 0x47, Input->Scratch);
    ((PUCHAR)Work)[Tail + 0x4f] = 1;
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxEncodeFragmentWork(
    _Out_writes_bytes_(APPLE_AGX_FRAGMENT_WORK_SIZE) PVOID Work,
    _In_ const APPLE_AGX_FRAGMENT_INPUT *Input,
    _In_ const APPLE_AGX_GEOMETRY *Geometry,
    _In_ ULONG Context,
    _In_ ULONG StampValue)
{
    const ULONG Parameters = 0x790, Meta = 0xbac, Tail = APPLE_AGX_FRAGMENT_TAIL;
    ULONG64 Value[APPLE_AGX_FRAGMENT_REGISTER_COUNT];
    ULONG64 LayerMetadata, User, NativeTiles;
    const APPLE_AGX_FRAGMENT_STATE *State;
    ULONG Width, Height, TilesX, TilesY;

    if (!Work || !Input || !Geometry || Context >= APPLE_UAT_CONTEXTS || !Geometry->Layers ||
        !AgxGpuAddressValid(Input->WorkClient) ||
        !AgxGpuAddressValid(Input->WorkClient + APPLE_AGX_FRAGMENT_WORK_SIZE - 3) ||
        !AgxGpuAddressValid(Input->Tilemap) || !AgxGpuAddressValid(Input->Heap) ||
        !AgxGpuAddressValid(Input->Auxiliary) || !AgxGpuAddressValid(Input->SceneUser) ||
        !AgxGpuAddressValid(Input->Scratch) || !Input->Notifier || !Input->Manager || !Input->Scene ||
        !Input->Empty || !Input->Sequence || !Input->Pool || !Input->Stamp || !Input->FirmwareStamp)
    {
        return STATUS_INVALID_PARAMETER;
    }
    LayerMetadata = Input->Heap + (Geometry->Layers > 1 ? 0x200 : 0);
    if (!AgxGpuAddressValid(LayerMetadata) || !AgxCompact(Input->SceneUser + 0x40, &User))
        return STATUS_INVALID_PARAMETER;
    User &= ~15ULL;
    State = &Input->State;
    Width = (Geometry->Pixels & 0xffff) + 1;
    Height = (Geometry->Pixels >> 16) + 1;
    TilesX = (Width + APPLE_AGX_TILE_PIXELS - 1) / APPLE_AGX_TILE_PIXELS;
    TilesY = (Height + APPLE_AGX_TILE_PIXELS - 1) / APPLE_AGX_TILE_PIXELS;
    NativeTiles = 0x36011 | ((ULONG64)(Geometry->Layers > 1) << 32) | ((ULONG64)(TilesX - 1) << 44) |
                  ((ULONG64)(TilesY - 1) << 53) | 0x2000000000ULL | ((Geometry->UtileConfig & 0xf000) << 28);

    RtlZeroMemory(Value, sizeof(Value));
    Value[0] = 1;
    Value[1] = Geometry->UtileConfig;
    Value[2] = State->EndOfTile.Resources;
    Value[3] = State->EndOfTile.Address;
    Value[4] = State->Background.Resources;
    Value[5] = State->Background.Address;
    Value[6] = State->MergeUpper[0];
    Value[7] = State->MergeUpper[1];
    Value[12] = State->Multisample;
    Value[13] = Geometry->IspBlocks;
    Value[14] = Geometry->IspBlocks;
    Value[15] = Geometry->Screen;
    Value[16] = 0xff0200;
    Value[17] = Input->Heap;
    Value[18] = State->Scissor;
    Value[19] = State->DepthBias;
    Value[20] = State->IspControl;
    Value[21] = ((ULONG64)Height << 32) | Width;
    Value[22] = 0x100000;
    Value[23] = State->TilebufferControl;
    Value[24] = State->DepthDimensions;
    Value[25] = State->DepthClear;
    Value[26] = State->StencilClear;
    Value[27] = State->Query & ~15ULL;
    Value[28] = State->ZlsControl;
    Value[29] = 0x04040404;
    Value[31] = State->Depth.Base;
    Value[32] = State->Depth.Base;
    Value[33] = State->Stencil.Base;
    Value[34] = State->Stencil.Base;
    Value[39] = State->Depth.Stride;
    Value[40] = State->Depth.Stride;
    Value[41] = State->Stencil.Stride;
    Value[42] = State->Stencil.Stride;
    Value[43] = State->DepthCompression.Base;
    Value[44] = State->DepthCompression.Stride;
    Value[45] = State->DepthCompression.Base;
    Value[46] = State->DepthCompression.Stride;
    Value[47] = State->StencilCompression.Base;
    Value[48] = State->StencilCompression.Stride;
    Value[49] = State->StencilCompression.Base;
    Value[50] = State->StencilCompression.Stride;
    Value[51] = Input->Tilemap;
    Value[52] = LayerMetadata;
    Value[53] = (ULONG64)(4 * Geometry->RegionStride) << 24;
    Value[54] = State->TileMode;
    Value[58] = 0x8860;
    Value[59] = Input->Auxiliary;
    Value[60] = Input->Auxiliary;
    Value[61] = 0x1c;
    Value[62] = 0x1c;
    Value[64] = NativeTiles;
    Value[74] = 8;
    Value[76] = User;
    Value[77] = 0x6000400020ULL;
    Value[78] = 0x200000001ULL;

    RtlZeroMemory(Work, APPLE_AGX_FRAGMENT_WORK_SIZE);
    AgxWriteLong(Work, 0x000, 1);
    AgxWriteLong(Work, 0x00c, Context);
    AgxWriteQuad(Work, 0x014, Input->Sequence);
    AgxWriteLong(Work, 0x01c, APPLE_AGX_FRAGMENT_SEQUENCE_SIZE);
    AgxWriteQuad(Work, 0x020, Input->Notifier);
    AgxWriteQuad(Work, 0x028, Input->Manager);
    AgxWriteQuad(Work, 0x030, Input->Scene);
    AgxWriteQuad(Work, 0x038, Input->Empty);
    AgxWriteQuad(Work, 0x040, Input->Tilemap);
    AgxWriteQuad(Work, 0x048, 0x88);
    AgxWriteLong(Work, 0x050, 1);
    AgxWriteWord(Work, 0x054, (USHORT)(Geometry->YBlocks >> 18));
    AgxWriteWord(Work, 0x056, (USHORT)(Geometry->XBlocks >> 18));
    AgxWriteLong(Work, 0x068, State->MergeUpper[0]);
    AgxWriteLong(Work, 0x06c, State->MergeUpper[1]);
    AgxWriteQuad(Work, 0x078, Geometry->Tiles);
    AppleAgxWriteRegisters(Work, APPLE_AGX_WORK_FRAGMENT, Value);
    AgxWriteQuad(Work, 0x780, Input->WorkClient + APPLE_AGX_FRAGMENT_REGISTERS);
    AgxWriteWord(Work, 0x788, APPLE_AGX_FRAGMENT_REGISTER_COUNT);
    AgxWriteWord(Work, 0x78a, APPLE_AGX_FRAGMENT_REGISTER_COUNT * APPLE_AGX_REGISTER_RECORD);
    AgxWriteQuad(Work, Parameters + 0x000, State->DepthBias);
    AgxWriteQuad(Work, Parameters + 0x010, State->Scissor);
    AgxWriteQuad(Work, Parameters + 0x020, State->Query);
    AgxWriteQuad(Work, Parameters + 0x168, State->PartialBackground.Resources);
    AgxWriteQuad(Work, Parameters + 0x170, State->PartialBackground.Address);
    AgxWriteQuad(Work, Parameters + 0x198, State->PartialBackground.Resources);
    AgxWriteQuad(Work, Parameters + 0x1a0, State->PartialBackground.Address);
    AgxWriteQuad(Work, Parameters + 0x1a8, State->ZlsControl);
    AgxWriteLong(Work, Parameters + 0x1b0, 0x04040404);
    AgxWriteQuad(Work, Parameters + 0x1b8, State->Depth.Base);
    AgxWriteQuad(Work, Parameters + 0x1c0, State->Depth.Stride);
    AgxWriteQuad(Work, Parameters + 0x1c8, State->DepthCompression.Stride);
    AgxWriteQuad(Work, Parameters + 0x1d0, State->Depth.Base);
    AgxWriteQuad(Work, Parameters + 0x1d8, State->Depth.Base);
    AgxWriteQuad(Work, Parameters + 0x1e0, State->DepthCompression.Base);
    AgxWriteQuad(Work, Parameters + 0x1e8, State->Stencil.Base);
    AgxWriteQuad(Work, Parameters + 0x1f0, State->Stencil.Stride);
    AgxWriteQuad(Work, Parameters + 0x1f8, State->StencilCompression.Stride);
    AgxWriteQuad(Work, Parameters + 0x200, State->Stencil.Base);
    AgxWriteQuad(Work, Parameters + 0x208, State->Stencil.Base);
    AgxWriteQuad(Work, Parameters + 0x210, State->StencilCompression.Base);
    AgxWriteLong(Work, Parameters + 0x228, State->MirrorTilebufferControl);
    AgxWriteLong(Work, Parameters + 0x230, State->IspControl);
    AgxWriteLong(Work, Parameters + 0x238, Width);
    AgxWriteLong(Work, Parameters + 0x23c, Height);
    AgxWriteLong(Work, Parameters + 0x240, 0x100000);
    AgxWriteLong(Work, Parameters + 0x248, State->MirrorTileMode);
    AgxWriteLong(Work, Parameters + 0x264, (ULONG)State->PartialEndOfTile.Resources);
    AgxWriteQuad(Work, Parameters + 0x26c, State->PartialEndOfTile.Address);
    AgxWriteLong(Work, Parameters + 0x284, (ULONG)State->PartialEndOfTile.Resources);
    AgxWriteQuad(Work, Parameters + 0x28c, State->PartialEndOfTile.Address);
    AgxWriteLong(Work, Parameters + 0x298, State->DepthClear);
    AgxWriteLong(Work, Parameters + 0x29c, State->StencilClear);
    AgxWriteLong(Work, Parameters + 0x2a0, State->SampleSize);
    AgxWriteQuad(Work, Parameters + 0x2c0, State->DepthDimensions);
    AgxWriteLong(Work, 0xb88, 0x67);
    AgxWriteLong(Work, 0xb90, 0xffffffff);
    AgxWriteLong(Work, 0xb94, State->ProcessEmptyTiles ? 1 : 0);
    AgxWriteLong(Work, 0xb98, 1);
    AgxWriteQuad(Work, Meta + 0x00, Input->Stamp);
    AgxWriteQuad(Work, Meta + 0x08, Input->FirmwareStamp);
    AgxWriteLong(Work, Meta + 0x10, StampValue);
    AgxWriteLong(Work, Meta + 0x14, 1);
    AgxWriteLong(Work, Meta + 0x1c, 1);
    AgxWriteLong(Work, Meta + 0x20, APPLE_AGX_FRAGMENT_UUID);
    AgxWriteLong(Work, 0xbe8, 1);
    AgxWriteQuad(Work, Tail + 0x08, Input->Timestamps[0]);
    AgxWriteQuad(Work, Tail + 0x10, Input->Timestamps[1]);
    AgxWriteQuad(Work, Tail + 0x18, Input->UserTimestamps[0]);
    AgxWriteQuad(Work, Tail + 0x20, Input->UserTimestamps[1]);
    AgxWriteQuad(Work, Tail + 0x2e, Input->Pool);
    ((PUCHAR)Work)[Tail + 0x36] = 1;
    AgxWriteQuad(Work, Tail + 0x37, 0xad0000);
    AgxWriteQuad(Work, Tail + 0x3f, 0x1600000);
    AgxWriteQuad(Work, Tail + 0x47, Input->Scratch);
    ((PUCHAR)Work)[Tail + 0x4f] = 1;
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxDescribeScene(
    _Out_writes_bytes_(APPLE_AGX_SCENE_SIZE) PVOID Scene,
    _In_ ULONG64 ManagerEntry,
    _In_ ULONG64 ManagerMisc,
    _In_ ULONG64 UserBuffer,
    _In_ ULONG64 UserBytes)
{
    ULONG64 Base;

    if (!Scene || !ManagerEntry || !ManagerMisc || UserBuffer < APPLE_AGX_COMPACT_BASE || (UserBuffer & 0x3f) ||
        UserBytes < APPLE_AGX_SCENE_USER_MIN || UserBuffer + UserBytes < UserBuffer)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Base = UserBuffer - APPLE_AGX_COMPACT_BASE;
    if (Base + APPLE_AGX_SCENE_USER_AUXILIARY > MAXULONG)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Scene, APPLE_AGX_SCENE_SIZE);
    AgxWriteQuad(Scene, 0x00, ManagerEntry);
    AgxWriteQuad(Scene, 0x08, ManagerEntry);
    AgxWriteQuad(Scene, 0x28, Base + APPLE_AGX_SCENE_USER_HEADER);
    AgxWriteLong(Scene, 0x30, (ULONG)(Base + APPLE_AGX_SCENE_USER_AUXILIARY));
    AgxWriteQuad(Scene, 0x40, ManagerMisc);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

static
VOID
AgxTranslateProgram(
    _Out_ PAPPLE_AGX_PROGRAM Program,
    _In_ const DRM_ASAHI_BG_EOT *Source,
    _In_ ULONG64 UscBase)
{
    Program->Address = UscBase + (Source->Usc & ~63u);
    Program->Resources = Source->RsrcSpec;
}

NTSTATUS
NTAPI
AppleAgxTranslateRender(
    _In_ const DRM_ASAHI_CMD_RENDER *Command,
    _In_ ULONG64 UscBase,
    _In_ ULONG Clusters,
    _Out_ PAPPLE_AGX_RENDER_STATE Render)
{
    const ULONG Known = DRM_ASAHI_RENDER_VERTEX_SCRATCH | DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES |
                        DRM_ASAHI_RENDER_NO_VERTEX_CLUSTERING | DRM_ASAHI_RENDER_DBIAS_IS_INT;
    PAPPLE_AGX_FRAGMENT_STATE State;
    ULONG64 Bytes;
    ULONG Tilebuffer, TileMode;
    NTSTATUS Status;

    if (!Command || !Render || !UscBase || (Command->Flags & ~Known))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Render, sizeof(*Render));
    Status = AppleAgxDescribeGeometry(Command->WidthPx, Command->HeightPx, Command->UtileWidthPx,
                                      Command->UtileHeightPx, Command->Samples, Command->Layers, Clusters,
                                      &Render->Geometry);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!AgxCompact(Command->VdmCtrlStreamBase, &Render->Vdm) || (Render->Vdm & 3))
        return STATUS_INVALID_PARAMETER;
    Render->Multisample = Command->PppMultisamplectl;
    Render->Ppp = Command->PppCtrl;
    Render->ProcessEmptyTiles = (Command->Flags & DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES) != 0;

    Bytes = (ULONG64)Command->SampleSizeB * Command->UtileWidthPx * Command->UtileHeightPx * Command->Samples;
    Tilebuffer = (ULONG)((Bytes + 2047) / 2048) | ((ULONG)(Render->Geometry.UtileConfig & 3) << 17);
    TileMode = 0x280 | (Command->Layers > 1 ? 1 : 0) | (Render->ProcessEmptyTiles ? 0x10000 : 0);

    State = &Render->Fragment;
    State->Multisample = Command->PppMultisamplectl;
    State->MergeUpper[0] = Command->IspMergeUpperX;
    State->MergeUpper[1] = Command->IspMergeUpperY;
    AgxTranslateProgram(&State->EndOfTile, &Command->Eot, UscBase);
    AgxTranslateProgram(&State->Background, &Command->Bg, UscBase);
    AgxTranslateProgram(&State->PartialEndOfTile, &Command->PartialEot, UscBase);
    AgxTranslateProgram(&State->PartialBackground, &Command->PartialBg, UscBase);
    State->Scissor = Command->IspScissorBase;
    State->DepthBias = Command->IspDbiasBase;
    State->Query = Command->IspOclqryBase;
    State->IspControl = 0xc000 | (Command->Flags & DRM_ASAHI_RENDER_DBIAS_IS_INT);
    State->TilebufferControl = Tilebuffer;
    State->MirrorTilebufferControl = Tilebuffer;
    State->TileMode = TileMode;
    State->MirrorTileMode = TileMode;
    State->Depth.Base = Command->Depth.Base;
    State->Depth.Stride = Command->Depth.Stride;
    State->DepthCompression.Base = Command->Depth.CompBase;
    State->DepthCompression.Stride = Command->Depth.CompStride;
    State->Stencil.Base = Command->Stencil.Base;
    State->Stencil.Stride = Command->Stencil.Stride;
    State->StencilCompression.Base = Command->Stencil.CompBase;
    State->StencilCompression.Stride = Command->Stencil.CompStride;
    State->ZlsControl = Command->ZlsCtrl;
    State->DepthDimensions = Command->IspZlsPixels;
    State->DepthClear = Command->IspBgobjdepth;
    State->StencilClear = Command->IspBgobjvals;
    State->SampleSize = Command->SampleSizeB;
    State->ProcessEmptyTiles = Render->ProcessEmptyTiles;
    return STATUS_SUCCESS;
}

ULONG
NTAPI
AppleAgxStamp(
    _In_ ULONG Seed,
    _In_ ULONG64 Ordinal)
{
    return Seed + (ULONG)Ordinal * APPLE_AGX_STAMP_STEP;
}

NTSTATUS
NTAPI
AppleAgxEncodeBarrier(
    _Out_writes_bytes_(APPLE_AGX_BARRIER_SIZE) PVOID Barrier,
    _In_ ULONG64 StampAddress,
    _In_ ULONG Wait,
    _In_ ULONG Event,
    _In_ ULONG Own,
    _In_ ULONG Uuid,
    _In_ BOOLEAN Hold)
{
    if (!Barrier || !StampAddress || Event > APPLE_AGX_EVENT_COMPUTE)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Barrier, APPLE_AGX_BARRIER_SIZE);
    AgxWriteLong(Barrier, 0x00, APPLE_AGX_OP_BARRIER);
    AgxWriteQuad(Barrier, 0x04, StampAddress);
    AgxWriteQuad(Barrier, 0x0c, StampAddress);
    AgxWriteLong(Barrier, 0x14, Wait);
    AgxWriteLong(Barrier, 0x18, Event);
    AgxWriteLong(Barrier, 0x1c, Own);
    AgxWriteLong(Barrier, 0x20, Uuid);
    AgxWriteLong(Barrier, 0x2a, Hold ? 1 : 0);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxBuildRender(
    _In_ const APPLE_AGX_RENDER_JOB *Job,
    _In_ const APPLE_AGX_RENDER_STATE *Render,
    _In_ ULONG Context,
    _In_ ULONG64 Ordinal)
{
    APPLE_AGX_RENDER_REFS Refs;
    APPLE_AGX_VERTEX_INPUT Vertex;
    APPLE_AGX_FRAGMENT_INPUT Fragment;
    ULONG VertexStamp, FragmentStamp, Index;
    NTSTATUS Status;

    ULONG64 SceneEntry;

    if (!Job || !Render || !Job->VertexWork || !Job->VertexSequence || !Job->FragmentWork ||
        !Job->FragmentSequence || !Job->Scene || !Job->FragmentBarrier || !Job->SceneAddress || !Job->SceneList)
    {
        return STATUS_INVALID_PARAMETER;
    }
    SceneEntry = Job->SceneList + sizeof(ULONG) * (Ordinal % APPLE_AGX_SCENE_LIST_SLOTS);
    VertexStamp = AppleAgxStamp(APPLE_AGX_SEED_VERTEX, Ordinal);
    FragmentStamp = AppleAgxStamp(APPLE_AGX_SEED_FRAGMENT, Ordinal);

    RtlZeroMemory(&Vertex, sizeof(Vertex));
    Vertex.Notifier = Job->Notifier;
    Vertex.Manager = Job->Manager;
    Vertex.Scene = Job->SceneAddress;
    Vertex.Empty = Job->Empty;
    Vertex.WorkClient = Job->VertexWorkClient;
    Vertex.Sequence = Job->VertexSequenceAddress;
    Vertex.Pool = Job->Pool[0];
    Vertex.Scratch = Job->VertexScratch;
    Vertex.Stamp = Job->UserStamp[0];
    Vertex.FirmwareStamp = Job->FirmwareStamp[0];
    Vertex.Tilemap = Job->Tilemap;
    Vertex.Tpc = Job->Tpc;
    Vertex.Heap = Job->Heap;
    Vertex.SceneUser = Job->SceneUser;
    Vertex.InitialSceneEntry = Job->SceneList + sizeof(ULONG);
    Vertex.Vdm = Render->Vdm;
    Vertex.Multisample = Render->Multisample;
    Vertex.Ppp = Render->Ppp;
    Vertex.PageCount = Job->PageCount;
    Vertex.ProcessEmptyTiles = Render->ProcessEmptyTiles;
    for (Index = 0; Index < 2; ++Index)
    {
        Vertex.Timestamps[Index] = Job->Timestamps[0][Index];
        Vertex.UserTimestamps[Index] = Job->UserTimestamps[0][Index];
    }
    for (Index = 0; Index < 3; ++Index)
        Vertex.Preempt[Index] = Job->Preempt[Index];

    RtlZeroMemory(&Fragment, sizeof(Fragment));
    Fragment.Notifier = Job->Notifier;
    Fragment.Manager = Job->Manager;
    Fragment.Scene = Job->SceneAddress;
    Fragment.Empty = Job->Empty;
    Fragment.Sequence = Job->FragmentSequenceAddress;
    Fragment.Pool = Job->Pool[1];
    Fragment.Stamp = Job->UserStamp[1];
    Fragment.FirmwareStamp = Job->FirmwareStamp[1];
    Fragment.WorkClient = Job->FragmentWorkClient;
    Fragment.Tilemap = Job->Tilemap;
    Fragment.Heap = Job->Heap;
    Fragment.Auxiliary = Job->Auxiliary;
    Fragment.SceneUser = Job->SceneUser;
    Fragment.Scratch = Job->FragmentScratch;
    Fragment.State = Render->Fragment;
    for (Index = 0; Index < 2; ++Index)
    {
        Fragment.Timestamps[Index] = Job->Timestamps[1][Index];
        Fragment.UserTimestamps[Index] = Job->UserTimestamps[1][Index];
    }

    Status = AppleAgxDescribeScene(Job->Scene, SceneEntry, Job->ManagerMisc, Job->SceneUser,
                                   Job->SceneUserBytes);
    if (NT_SUCCESS(Status))
        Status = AppleAgxEncodeVertexWork(Job->VertexWork, &Vertex, &Render->Geometry, Context, VertexStamp);
    if (NT_SUCCESS(Status))
        Status = AppleAgxEncodeFragmentWork(Job->FragmentWork, &Fragment, &Render->Geometry, Context, FragmentStamp);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Refs, sizeof(Refs));
    Refs.Hwdata = Job->Hwdata;
    Refs.Notifier = Job->Notifier;
    Refs.Scene = Job->SceneAddress;
    Refs.Manager = Job->Manager;
    Refs.Pool = Job->Pool[0];
    Refs.Work = Job->VertexWorkAddress;
    Refs.Sequence = Job->VertexSequenceAddress;
    Refs.Queue = Job->Queue[0];
    Refs.FirmwareStamp = Job->FirmwareStamp[0];
    Refs.Scratch = Job->VertexScratch;
    Refs.Requested = Job->UserTimestamps[0][0] || Job->UserTimestamps[0][1];
    Status = AppleAgxEncodeVertexSequence(Job->VertexSequence, &Refs, Context, VertexStamp);
    if (!NT_SUCCESS(Status))
        return Status;
    Refs.Pool = Job->Pool[1];
    Refs.Work = Job->FragmentWorkAddress;
    Refs.Sequence = Job->FragmentSequenceAddress;
    Refs.Queue = Job->Queue[1];
    Refs.FirmwareStamp = Job->FirmwareStamp[1];
    Refs.Scratch = Job->FragmentScratch;
    Refs.ScratchBytes = Job->FragmentScratchBytes;
    Refs.Requested = Job->UserTimestamps[1][0] || Job->UserTimestamps[1][1];
    Status = AppleAgxEncodeFragmentSequence(Job->FragmentSequence, &Refs, Context, FragmentStamp,
                                            Job->QueueCommands);
    if (!NT_SUCCESS(Status))
        return Status;
    return AppleAgxEncodeBarrier(Job->FragmentBarrier, Job->FirmwareStamp[0], VertexStamp, APPLE_AGX_EVENT_VERTEX,
                                 FragmentStamp, APPLE_AGX_FRAGMENT_UUID, FALSE);
}

NTSTATUS
NTAPI
AppleAgxDescribeParams(
    _In_ ULONG Version,
    _In_ ULONG Clusters,
    _In_ ULONG64 CoreWord,
    _In_ ULONG ChipId,
    _In_ ULONG MaxKilohertz,
    _Out_ PDRM_ASAHI_PARAMS_GLOBAL Params)
{
    const ULONG64 Mask = (1ULL << APPLE_AGX_CLUSTER_CORE_BITS) - 1;
    ULONG Cluster;

    if (!Params || !Clusters || Clusters * APPLE_AGX_CLUSTER_CORE_BITS > 64)
        return STATUS_INVALID_PARAMETER;
    if (((Version >> 24) & 0xff) != APPLE_AGX_G15_VERSION_CODE ||
        ((Version >> 16) & 0xff) != APPLE_AGX_VARIANT_S_CODE)
    {
        return STATUS_NOT_SUPPORTED;
    }
    RtlZeroMemory(Params, sizeof(*Params));
    Params->GpuGeneration = 15;
    Params->GpuVariant = 'S';
    Params->GpuRevision = (Version >> 8) & 0xff;
    Params->ChipId = ChipId;
    Params->NumDies = 1;
    Params->NumClustersTotal = Clusters;
    Params->NumCoresPerCluster = APPLE_AGX_CLUSTER_CORE_BITS;
    Params->MaxFrequencyKhz = MaxKilohertz;
    for (Cluster = 0; Cluster < Clusters; ++Cluster)
        Params->CoreMasks[Cluster] = (CoreWord >> (Cluster * APPLE_AGX_CLUSTER_CORE_BITS)) & Mask;
    Params->VmStart = APPLE_AGX_USER_VA_START;
    Params->VmEnd = APPLE_AGX_USER_VA_END;
    Params->VmKernelMinSize = APPLE_AGX_KERNEL_VA_MIN;
    Params->MaxCommandsPerSubmission = APPLE_AGX_MAX_COMMANDS;
    Params->MaxAttachments = APPLE_AGX_MAX_ATTACHMENTS;
    Params->CommandTimestampFrequencyHz = APPLE_AGX_TIMESTAMP_HZ;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxDescribeVm(
    _In_ const DRM_ASAHI_VM_CREATE *Create,
    _Out_ PAPPLE_AGX_VM_RANGES Ranges)
{
    ULONG64 Start, End, Half;

    if (!Create || !Ranges || Create->Pad || Create->KernelStart < APPLE_AGX_USER_VA_START ||
        Create->KernelEnd > APPLE_AGX_USER_VA_END || Create->KernelStart >= Create->KernelEnd ||
        Create->KernelEnd - Create->KernelStart < APPLE_AGX_KERNEL_VA_MIN ||
        ((Create->KernelStart | Create->KernelEnd) & (APPLE_UAT_PAGE_SIZE - 1)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Start = (Create->KernelStart + APPLE_AGX_BUFFER_PAGE - 1) & ~(APPLE_AGX_BUFFER_PAGE - 1);
    End = Create->KernelEnd & ~(APPLE_AGX_BUFFER_PAGE - 1);
    Half = ((End - Start) >> 1) & ~(APPLE_AGX_BUFFER_PAGE - 1);
    Ranges->UserStart = APPLE_AGX_USER_VA_START;
    Ranges->UserEnd = APPLE_AGX_USER_VA_END;
    Ranges->KernelStart = Start;
    Ranges->KernelGpuEnd = Start + Half;
    Ranges->KernelEnd = End;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxGemSize(
    _In_ const DRM_ASAHI_GEM_CREATE *Create,
    _Out_ PULONG64 Size)
{
    if (!Create || !Size || !Create->Size || Create->Pad ||
        (Create->Flags & ~(DRM_ASAHI_GEM_WRITEBACK | DRM_ASAHI_GEM_VM_PRIVATE)) ||
        (!(Create->Flags & DRM_ASAHI_GEM_VM_PRIVATE) && Create->VmId) ||
        Create->Size > MAXULONG64 - (APPLE_UAT_PAGE_SIZE - 1))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *Size = (Create->Size + APPLE_UAT_PAGE_SIZE - 1) & ~(APPLE_UAT_PAGE_SIZE - 1);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxCheckBind(
    _In_ const DRM_ASAHI_GEM_BIND_OP *Op,
    _In_ const APPLE_AGX_VM_RANGES *Ranges,
    _In_ ULONG64 ObjectSize,
    _Out_ PAPPLE_UAT_PROTECTION Protection)
{
    const ULONG MapFlags = DRM_ASAHI_BIND_READ | DRM_ASAHI_BIND_WRITE | DRM_ASAHI_BIND_SINGLE_PAGE;
    ULONG64 End, Accessed;

    if (!Op || !Ranges || !Protection || !Op->Range ||
        ((Op->Addr | Op->Range | Op->Offset) & (APPLE_UAT_PAGE_SIZE - 1)) || Op->Addr + Op->Range < Op->Addr)
    {
        return STATUS_INVALID_PARAMETER;
    }
    End = Op->Addr + Op->Range;
    if (Op->Addr < Ranges->UserStart || End > Ranges->UserEnd ||
        (Op->Addr < Ranges->KernelEnd && End > Ranges->KernelStart))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Protection, sizeof(*Protection));
    if (Op->Flags & DRM_ASAHI_BIND_UNBIND)
    {
        if (Op->Offset || Op->Flags != DRM_ASAHI_BIND_UNBIND || Op->Handle)
            return STATUS_INVALID_PARAMETER;
        return STATUS_SUCCESS;
    }
    if ((Op->Flags & ~MapFlags) || !(Op->Flags & (DRM_ASAHI_BIND_READ | DRM_ASAHI_BIND_WRITE)))
        return STATUS_INVALID_PARAMETER;
    Accessed = (Op->Flags & DRM_ASAHI_BIND_SINGLE_PAGE) ? APPLE_UAT_PAGE_SIZE : Op->Range;
    if (Op->Offset + Accessed < Op->Offset || Op->Offset + Accessed > ObjectSize)
        return STATUS_INVALID_PARAMETER;
    Protection->Access = APPLE_UAT_ACCESS_GPU;
    Protection->Memory = APPLE_UAT_MEMORY_UNCACHED;
    Protection->Read = (Op->Flags & DRM_ASAHI_BIND_READ) != 0;
    Protection->Write = (Op->Flags & DRM_ASAHI_BIND_WRITE) != 0;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxBindingInsert(
    _Inout_ PAPPLE_AGX_BINDINGS Bindings,
    _In_ const APPLE_AGX_BINDING *Binding)
{
    ULONG Index;

    if (!Bindings || !Binding || !Binding->Range || !Binding->Object ||
        Binding->Address + Binding->Range < Binding->Address)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < Bindings->Count; ++Index)
    {
        const APPLE_AGX_BINDING *Entry = &Bindings->Entry[Index];

        if (Binding->Address < Entry->Address + Entry->Range && Entry->Address < Binding->Address + Binding->Range)
            return STATUS_CONFLICTING_ADDRESSES;
    }
    if (Bindings->Count >= Bindings->Capacity)
        return STATUS_INSUFFICIENT_RESOURCES;
    Bindings->Entry[Bindings->Count++] = *Binding;
    if (Bindings->Reference)
        Bindings->Reference(Binding->Object, 1);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxBindingRemove(
    _Inout_ PAPPLE_AGX_BINDINGS Bindings,
    _In_ ULONG64 Address,
    _In_ ULONG64 Range)
{
    ULONG64 End = Address + Range;
    ULONG Index, Splits = 0;

    if (!Bindings || !Range || End < Address)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Bindings->Count; ++Index)
    {
        const APPLE_AGX_BINDING *Entry = &Bindings->Entry[Index];

        if (Address > Entry->Address && End < Entry->Address + Entry->Range)
            ++Splits;
    }
    if (Bindings->Count + Splits > Bindings->Capacity)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Bindings->Count;)
    {
        PAPPLE_AGX_BINDING Entry = &Bindings->Entry[Index];
        ULONG64 EntryEnd = Entry->Address + Entry->Range;

        if (End <= Entry->Address || Address >= EntryEnd)
        {
            ++Index;
            continue;
        }
        if (Address <= Entry->Address && End >= EntryEnd)
        {
            PVOID Object = Entry->Object;

            Bindings->Entry[Index] = Bindings->Entry[--Bindings->Count];
            if (Bindings->Reference)
                Bindings->Reference(Object, -1);
            continue;
        }
        if (Address > Entry->Address && End < EntryEnd)
        {
            APPLE_AGX_BINDING Tail = *Entry;

            Tail.Address = End;
            Tail.Range = EntryEnd - End;
            if (!Tail.SinglePage)
                Tail.Offset += End - Entry->Address;
            Entry->Range = Address - Entry->Address;
            Bindings->Entry[Bindings->Count++] = Tail;
            if (Bindings->Reference)
                Bindings->Reference(Tail.Object, 1);
        }
        else if (Address <= Entry->Address)
        {
            if (!Entry->SinglePage)
                Entry->Offset += End - Entry->Address;
            Entry->Range = EntryEnd - End;
            Entry->Address = End;
        }
        else
        {
            Entry->Range = Address - Entry->Address;
        }
        ++Index;
    }
    return STATUS_SUCCESS;
}

const APPLE_AGX_BINDING *
NTAPI
AppleAgxBindingFind(
    _In_ const APPLE_AGX_BINDINGS *Bindings,
    _In_ ULONG64 Address)
{
    ULONG Index;

    if (!Bindings)
        return NULL;
    for (Index = 0; Index < Bindings->Count; ++Index)
    {
        const APPLE_AGX_BINDING *Entry = &Bindings->Entry[Index];

        if (Address >= Entry->Address && Address - Entry->Address < Entry->Range)
            return Entry;
    }
    return NULL;
}

NTSTATUS
NTAPI
AppleAgxComputeRegisters(
    _In_ const DRM_ASAHI_CMD_COMPUTE *Command,
    _In_ ULONG64 Preempt,
    _Out_writes_(APPLE_AGX_COMPUTE_REGISTER_COUNT) PULONG64 Register)
{
    ULONG64 Saved = Preempt + APPLE_AGX_COMPUTE_PREEMPT_TAIL;
    ULONG Index;

    RtlZeroMemory(Register, APPLE_AGX_COMPUTE_REGISTER_COUNT * sizeof(ULONG64));
    if (!Preempt || Command->Flags || Command->CdmCtrlStreamEnd <= Command->CdmCtrlStreamBase ||
        Command->CdmCtrlStreamBase < APPLE_AGX_USER_VA_START || Command->CdmCtrlStreamEnd > APPLE_AGX_USER_VA_END)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Register[0] = Preempt;
    Register[1] = Command->CdmCtrlStreamBase;
    for (Index = 0; Index < 4; ++Index)
        Register[2 + Index] = Saved + 8 * Index;
    Register[6] = 0x154024201ULL;
    Register[7] = 0x10c08860ULL;
    Register[8] = 0x1c;
    Register[9] = 0x1c;
    Register[18] = APPLE_AGX_COMPUTE_REG_1A0E9;
    Register[19] = 0xff0000;
    Register[20] = APPLE_AGX_COMPUTE_PTC;
    Register[21] = APPLE_AGX_COMPUTE_DUPM;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAgxNextCommand(
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _Inout_ PULONG Offset,
    _Out_ PAPPLE_AGX_COMMAND Command)
{
    RtlZeroMemory(Command, sizeof(*Command));
    if (*Offset == Length)
        return STATUS_NO_MORE_ENTRIES;
    if (*Offset > Length || Length - *Offset < sizeof(DRM_ASAHI_CMD_HEADER))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&Command->Header, (const UCHAR *)Buffer + *Offset, sizeof(Command->Header));
    if (Command->Header.Size > Length - *Offset - sizeof(DRM_ASAHI_CMD_HEADER))
        return STATUS_INVALID_PARAMETER;
    Command->Payload = (const UCHAR *)Buffer + *Offset + sizeof(DRM_ASAHI_CMD_HEADER);
    Command->Size = Command->Header.Size;
    *Offset += sizeof(DRM_ASAHI_CMD_HEADER) + Command->Header.Size;
    return STATUS_SUCCESS;
}
