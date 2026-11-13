/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Space held for file data the cache has taken but not yet written
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

/* The ranges of the file outside [Start, End) stay, the parts inside are counted and dropped */
static
NTSTATUS
BtrfsCutRanges(
    _Inout_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End,
    _In_ BOOLEAN Insert,
    _Out_ PULONGLONG Inside)
{
    ULONG Count = Fcb->ReservedCount, Index, Out = 0;
    PBTRFS_RANGE New;

    *Inside = 0;
    ASSERT(Start < End);

    New = ExAllocatePoolZero(PagedPool, (Count + 2) * sizeof(BTRFS_RANGE), TAG_BTRFS);
    if (New == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Count; Index++)
    {
        BTRFS_RANGE Range = Fcb->Reserved[Index];
        ULONGLONG RangeEnd = Range.Start + Range.Length;

        if (RangeEnd <= Start || Range.Start >= End)
        {
            New[Out++] = Range;
            continue;
        }

        *Inside += min(RangeEnd, End) - max(Range.Start, Start);
        if (Range.Start < Start)
        {
            New[Out].Start = Range.Start;
            New[Out].Length = Start - Range.Start;
            Out++;
        }
        if (RangeEnd > End)
        {
            New[Out].Start = End;
            New[Out].Length = RangeEnd - End;
            Out++;
        }
    }

    if (Insert)
    {
        ULONG Slot;

        for (Slot = 0; Slot < Out && New[Slot].Start < Start; Slot++);
        RtlMoveMemory(&New[Slot + 1], &New[Slot], (Out - Slot) * sizeof(BTRFS_RANGE));
        New[Slot].Start = Start;
        New[Slot].Length = End - Start;
        Out++;
    }

    ASSERT(Out <= Count + 2);
    if (Fcb->Reserved != NULL) ExFreePoolWithTag(Fcb->Reserved, TAG_BTRFS);
    Fcb->Reserved = New;
    Fcb->ReservedCount = Out;
    return STATUS_SUCCESS;
}

static
VOID
BtrfsSectorRange(
    _In_ PBTRFS_VCB Vcb,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length,
    _Out_ PULONGLONG Start,
    _Out_ PULONGLONG End)
{
    *Start = Offset & ~((ULONGLONG)Vcb->SectorSize - 1);
    *End = (Offset + Length + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1);
}

static
ULONGLONG
BtrfsHeldWithin(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End)
{
    ULONGLONG Held = 0;
    ULONG Index;

    for (Index = 0; Index < Fcb->ReservedCount; Index++)
    {
        ULONGLONG RangeStart = Fcb->Reserved[Index].Start, RangeEnd = RangeStart + Fcb->Reserved[Index].Length;

        if (RangeEnd > Start && RangeStart < End) Held += min(RangeEnd, End) - max(RangeStart, Start);
    }
    return Held;
}

/* A cached write fails at once on a full volume rather than when the lazy writer reaches it (NTFS, Linux btrfs) */
NTSTATUS
BtrfsReserveCachedWrite(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG Start, End, Wanted, Merged;
    ULONG Sequence;
    NTSTATUS Status;

    ASSERT(Length != 0);
    BtrfsSectorRange(Vcb, Offset, Length, &Start, &End);

    BtrfsAcquireTreeExclusive(Vcb);
    Sequence = Vcb->ChangeSequence;
    Wanted = (End - Start) - BtrfsHeldWithin(Fcb, Start, End);

    Status = BtrfsStartTransaction(Vcb);
    if (NT_SUCCESS(Status) && Wanted != 0) Status = BtrfsReserveSpace(Vcb, BTRFS_BLOCK_GROUP_DATA, Vcb->DelallocBytes + Wanted);
    if (NT_SUCCESS(Status) && Wanted != 0) Status = BtrfsCutRanges(Fcb, Start, End, TRUE, &Merged);
    if (NT_SUCCESS(Status)) Vcb->DelallocBytes += Wanted;

    Status = BtrfsFinishChange(Vcb, Status, Sequence);
    BtrfsReleaseTree(Vcb);
    return Status;
}

/* Written, truncated or deleted data no longer holds space; End is past the last byte, MAXULONGLONG for the whole tail */
VOID
BtrfsReleaseReservation(
    _In_ PBTRFS_FCB Fcb,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG End)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG Start = Offset & ~((ULONGLONG)Vcb->SectorSize - 1), Released = 0;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->TreeLock));
    if (Fcb->ReservedCount == 0 || End <= Offset) return;

    if (End < MAXULONGLONG - Vcb->SectorSize) End = (End + Vcb->SectorSize - 1) & ~((ULONGLONG)Vcb->SectorSize - 1);
    if (!NT_SUCCESS(BtrfsCutRanges(Fcb, Start, End, FALSE, &Released))) return;

    ASSERT(Vcb->DelallocBytes >= Released);
    Vcb->DelallocBytes -= min(Released, Vcb->DelallocBytes);
}

VOID
BtrfsReleaseAllReservations(
    _In_ PBTRFS_FCB Fcb)
{
    PBTRFS_VCB Vcb = Fcb->Vcb;
    ULONGLONG Held = 0;
    ULONG Index;

    if (Fcb->Reserved == NULL) return;

    BtrfsAcquireTreeExclusive(Vcb);
    for (Index = 0; Index < Fcb->ReservedCount; Index++)
        Held += Fcb->Reserved[Index].Length;
    ASSERT(Vcb->DelallocBytes >= Held);
    Vcb->DelallocBytes -= min(Held, Vcb->DelallocBytes);
    ExFreePoolWithTag(Fcb->Reserved, TAG_BTRFS);
    Fcb->Reserved = NULL;
    Fcb->ReservedCount = 0;
    BtrfsReleaseTree(Vcb);
}
