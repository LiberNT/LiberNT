/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The zone buffer, a fixed-size block allocator over caller memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Carves a segment into blocks and puts every one of them on the free list. The
 * segment keeps its own header, so the blocks start behind it.
 */
static
VOID
ExpChainZoneSegment(
    _Inout_ PZONE_HEADER Zone,
    _Inout_ PVOID Segment,
    _In_ ULONG SegmentSize)
{
    PZONE_SEGMENT_HEADER Header = Segment;
    PSINGLE_LIST_ENTRY Block;
    ULONG Offset;

    Header->Reserved = NULL;
    Header->SegmentList.Next = Zone->SegmentList.Next;
    Zone->SegmentList.Next = &Header->SegmentList;

    for (Offset = sizeof(ZONE_SEGMENT_HEADER);
         Offset + Zone->BlockSize <= SegmentSize;
         Offset += Zone->BlockSize)
    {
        Block = (PSINGLE_LIST_ENTRY)((PUCHAR)Segment + Offset);
        Block->Next = Zone->FreeList.Next;
        Zone->FreeList.Next = Block;
    }

    Zone->TotalSegmentSize += SegmentSize;
}

static
NTSTATUS
ExpCheckZoneSegment(
    _In_ PZONE_HEADER Zone,
    _In_ ULONG SegmentSize)
{
    if (SegmentSize < sizeof(ZONE_SEGMENT_HEADER) + Zone->BlockSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ExInitializeZone(
    _Out_ PZONE_HEADER Zone,
    _In_ ULONG BlockSize,
    _Inout_ PVOID InitialSegment,
    _In_ ULONG InitialSegmentSize)
{
    ULONG Size;

    Size = (BlockSize + sizeof(ULONG_PTR) - 1) & ~(sizeof(ULONG_PTR) - 1);
    if (Size < sizeof(SINGLE_LIST_ENTRY))
    {
        Size = sizeof(SINGLE_LIST_ENTRY);
    }

    Zone->BlockSize = Size;
    Zone->TotalSegmentSize = 0;
    Zone->FreeList.Next = NULL;
    Zone->SegmentList.Next = NULL;

    if (InitialSegmentSize < sizeof(ZONE_SEGMENT_HEADER) + Size)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ExpChainZoneSegment(Zone, InitialSegment, InitialSegmentSize);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExExtendZone(
    _Inout_ PZONE_HEADER Zone,
    _Inout_ PVOID Segment,
    _In_ ULONG SegmentSize)
{
    NTSTATUS Status;

    Status = ExpCheckZoneSegment(Zone, SegmentSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ExpChainZoneSegment(Zone, Segment, SegmentSize);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExInterlockedExtendZone(
    _Inout_ PZONE_HEADER Zone,
    _Inout_ PVOID Segment,
    _In_ ULONG SegmentSize,
    _Inout_ PKSPIN_LOCK Lock)
{
    NTSTATUS Status;
    KIRQL OldIrql;

    Status = ExpCheckZoneSegment(Zone, SegmentSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireSpinLock(Lock, &OldIrql);
    ExpChainZoneSegment(Zone, Segment, SegmentSize);
    KeReleaseSpinLock(Lock, OldIrql);

    return STATUS_SUCCESS;
}

/* EOF */
