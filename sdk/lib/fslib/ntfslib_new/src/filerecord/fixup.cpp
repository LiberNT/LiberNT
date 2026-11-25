/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

/* NOTE: For more information, see: https://flatcap.github.io/linux-ntfs/ntfs/concepts/fixup.html
 *
 * Abbreviations used
 *     USA: Update Sequence Array
 *     USN: Update Sequence Number
 */

/* Records on large sectors may use a 512-byte update-sequence stride. */
#define NTFS_MINIMUM_FIXUP_STRIDE 512

BOOLEAN
NtfsGetFixupStride(_In_ ULONG UpdateSequenceCount,
                   _In_ ULONG RecordSize,
                   _In_ ULONG BytesPerSector,
                   _Out_ PULONG Stride)
{
    ULONG Step;

    *Stride = 0;
    if (UpdateSequenceCount < 2 ||
        RecordSize < BytesPerSector ||
        (RecordSize % BytesPerSector) != 0)
    {
        return FALSE;
    }
    Step = RecordSize / (UpdateSequenceCount - 1);
    if (Step * (UpdateSequenceCount - 1) != RecordSize ||
        Step < NTFS_MINIMUM_FIXUP_STRIDE ||
        Step > BytesPerSector ||
        (Step & (Step - 1)) != 0)
    {
        return FALSE;
    }
    *Stride = Step;
    return TRUE;
}

static NTSTATUS
ValidateFixup(_In_ PNTFSRecordHeader Header,
              _In_ ULONG RecordSize,
              _In_ ULONG BytesPerSector,
              _Out_ PULONG Stride)
{
    ULONG UsaSize = Header->SizeOfUpdateSequence * sizeof(USHORT);

    if (!NtfsGetFixupStride(Header->SizeOfUpdateSequence, RecordSize, BytesPerSector, Stride) ||
        (Header->UpdateSequenceOffset & (sizeof(USHORT) - 1)) != 0 ||
        Header->UpdateSequenceOffset > RecordSize ||
        UsaSize > RecordSize - Header->UpdateSequenceOffset)
    {
        *Stride = 0;
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsApplyFixup(_Inout_ PNTFSRecordHeader Header,
               _In_ ULONG RecordSize,
               _In_ ULONG BytesPerSector)
{
    ULONG Stride;
    NTSTATUS Status = ValidateFixup(Header, RecordSize, BytesPerSector, &Stride);
    if (!NT_SUCCESS(Status))
        return Status;

    PUSHORT Usa = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) + Header->UpdateSequenceOffset);
    PUSHORT SectorTail = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) +
        Stride - sizeof(USHORT));

    for (ULONG Index = 1;
         Index < Header->SizeOfUpdateSequence;
         Index++)
    {
        if (*SectorTail != Usa[0])
        {
            DPRINT1("Update-sequence mismatch: %u read, %u expected.\n",
                    *SectorTail,
                    Usa[0]);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        *SectorTail = Usa[Index];
        SectorTail = reinterpret_cast<PUSHORT>(
            reinterpret_cast<PUCHAR>(SectorTail) + Stride);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfsCommitFixup(_Inout_ PNTFSRecordHeader Header,
                _In_ ULONG RecordSize,
                _In_ ULONG BytesPerSector)
{
    ULONG Stride;
    NTSTATUS Status = ValidateFixup(Header, RecordSize, BytesPerSector, &Stride);
    if (!NT_SUCCESS(Status))
        return Status;

    PUSHORT Usa = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) + Header->UpdateSequenceOffset);
    PUSHORT SectorTail = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) +
        Stride - sizeof(USHORT));

    Usa[0]++;
    if (Usa[0] == 0)
        Usa[0]++;

    for (ULONG Index = 1;
         Index < Header->SizeOfUpdateSequence;
         Index++)
    {
        Usa[Index] = *SectorTail;
        *SectorTail = Usa[0];
        SectorTail = reinterpret_cast<PUSHORT>(
            reinterpret_cast<PUCHAR>(SectorTail) + Stride);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::CommitFixup()
{
    return NtfsCommitFixup(&Header->Header,
                           Header->AllocatedSize,
                           DiskVolume->BytesPerSector);
}

NTSTATUS
FileRecord::ApplyFixup()
{
    return NtfsApplyFixup(&Header->Header,
                          Header->AllocatedSize,
                          DiskVolume->BytesPerSector);
}
