/*
 * PROJECT:     LiberNT Device Tree Support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bounded flattened-device-tree header validation
 */

#pragma once

#define FDT_MAGIC                   0xD00DFEEDUL
#define FDT_HEADER_SIZE             40UL
#define FDT_SUPPORTED_VERSION       17UL
#define FDT_MAXIMUM_SIZE            (16UL * 1024UL * 1024UL)

#define FDT_BEGIN_NODE              1UL
#define FDT_END_NODE                2UL
#define FDT_PROPERTY                3UL
#define FDT_NOP                     4UL
#define FDT_END                     9UL

#define FDT_OFFSET_MAGIC            0UL
#define FDT_OFFSET_TOTAL_SIZE       4UL
#define FDT_OFFSET_STRUCTURE        8UL
#define FDT_OFFSET_STRINGS          12UL
#define FDT_OFFSET_RESERVATIONS     16UL
#define FDT_OFFSET_VERSION          20UL
#define FDT_OFFSET_LAST_VERSION     24UL
#define FDT_OFFSET_STRINGS_SIZE     32UL
#define FDT_OFFSET_STRUCTURE_SIZE   36UL

static __inline ULONG
FdtReadBigEndian32(
    _In_reads_bytes_(sizeof(ULONG)) const VOID *Address)
{
    const UCHAR *Bytes = Address;

    return ((ULONG)Bytes[0] << 24) |
           ((ULONG)Bytes[1] << 16) |
           ((ULONG)Bytes[2] << 8) |
           Bytes[3];
}

static __inline BOOLEAN
FdtRangeIsValid(
    _In_ ULONG TotalSize,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    return (Offset <= TotalSize) && (Size <= TotalSize - Offset);
}

static __inline BOOLEAN
FdtValidateHeader(
    _In_reads_bytes_(AvailableSize) const VOID *Blob,
    _In_ SIZE_T AvailableSize,
    _Out_opt_ PULONG TotalSize)
{
    const UCHAR *Bytes = Blob;
    ULONG Size, StructureOffset, StringsOffset, ReservationsOffset;
    ULONG StructureSize, StringsSize, Version, LastCompatibleVersion;

    if ((Blob == NULL) || (AvailableSize < FDT_HEADER_SIZE))
        return FALSE;
    if (FdtReadBigEndian32(Bytes + FDT_OFFSET_MAGIC) !=
        FDT_MAGIC)
    {
        return FALSE;
    }

    Size = FdtReadBigEndian32(Bytes + FDT_OFFSET_TOTAL_SIZE);
    if ((Size < FDT_HEADER_SIZE) ||
        (Size > FDT_MAXIMUM_SIZE) ||
        (Size > AvailableSize))
    {
        return FALSE;
    }

    Version = FdtReadBigEndian32(Bytes + FDT_OFFSET_VERSION);
    LastCompatibleVersion =
        FdtReadBigEndian32(Bytes + FDT_OFFSET_LAST_VERSION);
    if ((Version < FDT_SUPPORTED_VERSION) ||
        (LastCompatibleVersion > FDT_SUPPORTED_VERSION) ||
        (LastCompatibleVersion > Version))
    {
        return FALSE;
    }

    StructureOffset =
        FdtReadBigEndian32(Bytes + FDT_OFFSET_STRUCTURE);
    StructureSize =
        FdtReadBigEndian32(Bytes + FDT_OFFSET_STRUCTURE_SIZE);
    StringsOffset =
        FdtReadBigEndian32(Bytes + FDT_OFFSET_STRINGS);
    StringsSize =
        FdtReadBigEndian32(Bytes + FDT_OFFSET_STRINGS_SIZE);
    ReservationsOffset =
        FdtReadBigEndian32(Bytes + FDT_OFFSET_RESERVATIONS);
    if ((StructureOffset & 3) || (ReservationsOffset & 7) ||
        (StructureOffset < FDT_HEADER_SIZE) ||
        (StringsOffset < FDT_HEADER_SIZE) ||
        (ReservationsOffset < FDT_HEADER_SIZE) ||
        !FdtRangeIsValid(Size, StructureOffset, StructureSize) ||
        !FdtRangeIsValid(Size, StringsOffset, StringsSize) ||
        (ReservationsOffset > Size - 16))
    {
        return FALSE;
    }

    if (TotalSize != NULL)
        *TotalSize = Size;
    return TRUE;
}
