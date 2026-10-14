/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GUID Partition Table lookup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#define GPT_HEADER_LBA              1
#define GPT_HEADER_MIN_SIZE         92
#define GPT_HEADER_SIZE_OFFSET      12
#define GPT_HEADER_CRC_OFFSET       16
#define GPT_HEADER_DISK_ID_OFFSET   56
#define GPT_HEADER_ENTRY_LBA_OFFSET 72
#define GPT_HEADER_COUNT_OFFSET     80
#define GPT_HEADER_ENTRY_SIZE_OFFSET 84
#define GPT_HEADER_ENTRY_CRC_OFFSET 88

#define GPT_ENTRY_MIN_SIZE          128
#define GPT_ENTRY_ID_OFFSET         16
#define GPT_ENTRY_FIRST_LBA_OFFSET  32
#define GPT_ENTRY_LAST_LBA_OFFSET   40

#define GPT_MAX_ENTRY_ARRAY_SIZE    0x100000

static const UCHAR GptSignature[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};

static
PUCHAR
GptReadHeader(
    _In_ PDK_DEVICE Disk,
    _In_reads_bytes_opt_(16) const UCHAR* DiskId)
{
    UCHAR Fixed[GPT_HEADER_MIN_SIZE];
    PUCHAR Header;
    ULONG Size, Crc;

    if (!DkRead(Disk, (ULONGLONG)GPT_HEADER_LBA * Disk->BlockSize, sizeof(Fixed), Fixed) ||
        !RtlEqualMemory(Fixed, GptSignature, sizeof(GptSignature)) ||
        (DiskId && !RtlEqualMemory(Fixed + GPT_HEADER_DISK_ID_OFFSET, DiskId, 16)))
    {
        return NULL;
    }

    Size = LeUlong(Fixed + GPT_HEADER_SIZE_OFFSET);
    if (Size < GPT_HEADER_MIN_SIZE || Size > Disk->BlockSize)
        return NULL;

    Header = FwAllocate(Size);
    if (!Header)
        return NULL;

    if (DkRead(Disk, (ULONGLONG)GPT_HEADER_LBA * Disk->BlockSize, Size, Header))
    {
        Crc = LeUlong(Header + GPT_HEADER_CRC_OFFSET);
        RtlZeroMemory(Header + GPT_HEADER_CRC_OFFSET, sizeof(ULONG));
        if (RtlComputeCrc32(0, Header, Size) == Crc)
            return Header;
    }

    FwFree(Header);
    return NULL;
}

BOOLEAN
GptFindPartition(
    _In_ PDK_DEVICE Disk,
    _In_reads_bytes_opt_(16) const UCHAR* DiskId,
    _In_reads_bytes_(16) const UCHAR* PartitionId,
    _Out_ PDK_DEVICE Partition)
{
    PUCHAR Header, Entries, Entry;
    ULONGLONG EntryLba, First, Last;
    ULONG Count, EntrySize, EntryCrc, ArraySize, Index;
    ULONG Number = 0;
    UCHAR Unused[16] = {0};
    BOOLEAN Found = FALSE;

    Header = GptReadHeader(Disk, DiskId);
    if (!Header)
        return FALSE;

    EntryLba = LeUlonglong(Header + GPT_HEADER_ENTRY_LBA_OFFSET);
    Count = LeUlong(Header + GPT_HEADER_COUNT_OFFSET);
    EntrySize = LeUlong(Header + GPT_HEADER_ENTRY_SIZE_OFFSET);
    EntryCrc = LeUlong(Header + GPT_HEADER_ENTRY_CRC_OFFSET);

    if (Count == 0 ||
        EntrySize < GPT_ENTRY_MIN_SIZE ||
        Count > GPT_MAX_ENTRY_ARRAY_SIZE / EntrySize ||
        EntryLba > Disk->Length / Disk->BlockSize)
    {
        FwFree(Header);
        return FALSE;
    }

    ArraySize = Count * EntrySize;
    Entries = FwAllocate(ArraySize);
    if (Entries &&
        DkRead(Disk, EntryLba * Disk->BlockSize, ArraySize, Entries) &&
        RtlComputeCrc32(0, Entries, ArraySize) == EntryCrc)
    {
        for (Index = 0; Index < Count && !Found; Index++)
        {
            Entry = Entries + Index * EntrySize;
            if (RtlEqualMemory(Entry, Unused, sizeof(Unused)))
                continue;

            Number++;
            if (!RtlEqualMemory(Entry + GPT_ENTRY_ID_OFFSET, PartitionId, 16))
                continue;

            First = LeUlonglong(Entry + GPT_ENTRY_FIRST_LBA_OFFSET);
            Last = LeUlonglong(Entry + GPT_ENTRY_LAST_LBA_OFFSET);
            if (First > Last || Last >= Disk->Length / Disk->BlockSize)
                break;

            *Partition = *Disk;
            Partition->Offset = First * Disk->BlockSize;
            Partition->Length = (Last - First + 1) * Disk->BlockSize;
            Partition->PartitionNumber = Number;
            Found = TRUE;
        }
    }

    if (Entries)
        FwFree(Entries);
    FwFree(Header);
    return Found;
}

BOOLEAN
GptGetDiskId(
    _In_ PDK_DEVICE Disk,
    _Out_writes_bytes_(16) PUCHAR DiskId)
{
    PUCHAR Header = GptReadHeader(Disk, NULL);

    if (!Header)
        return FALSE;

    RtlCopyMemory(DiskId, Header + GPT_HEADER_DISK_ID_OFFSET, 16);
    FwFree(Header);
    return TRUE;
}
