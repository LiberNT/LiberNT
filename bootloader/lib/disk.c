/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Block device reads and BCD device resolution
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#define DK_BOUNCE_SIZE 0x10000
#define DK_DIRECT_SIZE 0x100000
#define DK_MBR_SIGNATURE_OFFSET 0x1B8
#define DK_MBR_MAGIC_OFFSET     0x1FE
#define DK_MBR_TABLE_OFFSET     0x1BE
#define DK_MBR_ENTRY_SIZE       16
#define DK_MBR_PARTITIONS       4
#define DK_MBR_TYPE_OFFSET      4
#define DK_MBR_START_OFFSET     8
#define DK_MBR_LENGTH_OFFSET    12
#define DK_CACHE_LINE           0x1000
#define DK_CACHE_WAYS           4
#define DK_CACHE_SETS           64
#define DK_CACHE_LIMIT          0x4000

typedef struct _DK_CACHE_TAG
{
    PVOID Firmware;
    ULONGLONG Line;
} DK_CACHE_TAG;

static PUCHAR DkBounce;
static PUCHAR DkCache;
static DK_CACHE_TAG DkCacheTags[DK_CACHE_SETS * DK_CACHE_WAYS];
static UCHAR DkCacheNext[DK_CACHE_SETS];

static
PUCHAR
DkCacheLine(
    _In_ PDK_DEVICE Device,
    _In_ ULONGLONG Line)
{
    ULONG Set = (ULONG)(Line % DK_CACHE_SETS);
    ULONG Slot = Set * DK_CACHE_WAYS;
    ULONG Way;

    if (!DkCache)
    {
        DkCache = FwAllocatePages(DK_CACHE_SETS * DK_CACHE_WAYS * DK_CACHE_LINE);
        if (!DkCache)
            return NULL;
    }

    for (Way = 0; Way < DK_CACHE_WAYS; Way++)
    {
        if (DkCacheTags[Slot + Way].Firmware == Device->Firmware &&
            DkCacheTags[Slot + Way].Line == Line)
        {
            return DkCache + (SIZE_T)(Slot + Way) * DK_CACHE_LINE;
        }
    }

    Way = DkCacheNext[Set];
    DkCacheNext[Set] = (UCHAR)((Way + 1) % DK_CACHE_WAYS);
    Slot += Way;

    DkCacheTags[Slot].Firmware = NULL;
    if (!FwReadBlocks(Device->Firmware,
                      Line * DK_CACHE_LINE / Device->BlockSize,
                      DK_CACHE_LINE / Device->BlockSize,
                      DkCache + (SIZE_T)Slot * DK_CACHE_LINE))
    {
        return NULL;
    }

    DkCacheTags[Slot].Firmware = Device->Firmware;
    DkCacheTags[Slot].Line = Line;
    return DkCache + (SIZE_T)Slot * DK_CACHE_LINE;
}

BOOLEAN
DkRead(
    _In_ PDK_DEVICE Device,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer)
{
    PUCHAR Output = Buffer;
    PUCHAR Line;
    ULONG Skip, Chunk, Blocks;

    if (Device->BlockSize == 0 ||
        Device->BlockSize > DK_BOUNCE_SIZE ||
        Offset > Device->Length ||
        Length > Device->Length - Offset)
    {
        return FALSE;
    }

    Offset += Device->Offset;

    if (Device->Memory)
    {
        RtlCopyMemory(Output, Device->Memory + Offset, Length);
        return TRUE;
    }

    if (Length <= DK_CACHE_LIMIT && DK_CACHE_LINE % Device->BlockSize == 0)
    {
        while (Length != 0)
        {
            Line = DkCacheLine(Device, Offset / DK_CACHE_LINE);
            if (!Line)
                break;

            Skip = (ULONG)(Offset % DK_CACHE_LINE);
            Chunk = min(Length, DK_CACHE_LINE - Skip);
            RtlCopyMemory(Output, Line + Skip, Chunk);
            Output += Chunk;
            Offset += Chunk;
            Length -= Chunk;
        }
    }

    while (Length != 0)
    {
        Skip = (ULONG)(Offset % Device->BlockSize);

        if (Skip == 0 && Length >= Device->BlockSize && FwBufferAligned(Device->Firmware, Output))
        {
            Chunk = min(Length, DK_DIRECT_SIZE) / Device->BlockSize * Device->BlockSize;
            if (!FwReadBlocks(Device->Firmware, Offset / Device->BlockSize, Chunk / Device->BlockSize, Output))
                return FALSE;

            Output += Chunk;
            Offset += Chunk;
            Length -= Chunk;
            continue;
        }

        if (!DkBounce)
        {
            DkBounce = FwAllocatePages(DK_BOUNCE_SIZE);
            if (!DkBounce)
                return FALSE;
        }

        Chunk = min(Length, DK_BOUNCE_SIZE - Skip);
        Blocks = (Skip + Chunk + Device->BlockSize - 1) / Device->BlockSize;

        if (!FwReadBlocks(Device->Firmware, Offset / Device->BlockSize, Blocks, DkBounce))
            return FALSE;

        RtlCopyMemory(Output, DkBounce + Skip, Chunk);
        Output += Chunk;
        Offset += Chunk;
        Length -= Chunk;
    }

    return TRUE;
}

static
BOOLEAN
DkFindMbrPartition(
    _In_ PDK_DEVICE Disk,
    _In_ ULONG Signature,
    _In_ ULONGLONG Offset,
    _Out_ PDK_DEVICE Partition)
{
    UCHAR Sector[512];
    PUCHAR Entry;
    ULONG Index, Number;

    if (!DkRead(Disk, 0, sizeof(Sector), Sector) ||
        Sector[DK_MBR_MAGIC_OFFSET] != 0x55 ||
        Sector[DK_MBR_MAGIC_OFFSET + 1] != 0xAA ||
        LeUlong(Sector + DK_MBR_SIGNATURE_OFFSET) != Signature)
    {
        return FALSE;
    }

    for (Index = 0, Number = 0; Index < DK_MBR_PARTITIONS; Index++)
    {
        Entry = Sector + DK_MBR_TABLE_OFFSET + Index * DK_MBR_ENTRY_SIZE;
        if (Entry[DK_MBR_TYPE_OFFSET] == 0)
            continue;

        Number++;
        if ((ULONGLONG)LeUlong(Entry + DK_MBR_START_OFFSET) * Disk->BlockSize != Offset)
            continue;

        *Partition = *Disk;
        Partition->Offset = Offset;
        Partition->Length = (ULONGLONG)LeUlong(Entry + DK_MBR_LENGTH_OFFSET) * Disk->BlockSize;
        Partition->PartitionNumber = Number;
        return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
DkMatchBcdPartition(
    _In_ PBCD_DEVICE BcdDevice,
    _In_ ULONG Index,
    _Inout_ PDK_DEVICE Disk,
    _Out_ PDK_DEVICE Device)
{
    Disk->DiskIndex = Index;
    return BcdDevice->Type == BcdDeviceGptPartition ?
           GptFindPartition(Disk, BcdDevice->Disk, BcdDevice->Partition, Device) :
           DkFindMbrPartition(Disk, BcdDevice->Signature, BcdDevice->PartitionOffset, Device);
}

BOOLEAN
DkOpenBcdDevice(
    _In_ PBCD_DEVICE BcdDevice,
    _Out_ PDK_DEVICE Device)
{
    DK_DEVICE Disk;
    ULONG Index;

    switch (BcdDevice->Type)
    {
        case BcdDeviceBoot:
            return DkLocateBootPartition(Device) || FwGetBootDevice(Device);

        case BcdDeviceGptPartition:
        case BcdDeviceMbrPartition:
            if (FwGetBootDiskIndex(&Index) &&
                FwGetDisk(Index, &Disk) &&
                DkMatchBcdPartition(BcdDevice, Index, &Disk, Device))
            {
                return TRUE;
            }

            do
            {
                for (Index = 0; FwGetDisk(Index, &Disk); Index++)
                {
                    if (DkMatchBcdPartition(BcdDevice, Index, &Disk, Device))
                        return TRUE;
                }
            } while (FwRescanDisks());
            break;
    }

    return FALSE;
}

BOOLEAN
DkLocateBootPartition(
    _Out_ PDK_DEVICE Partition)
{
    UCHAR Signature[16];
    UCHAR Sector[512];
    DK_DEVICE Disk;
    ULONG Number, Index, BootIndex;
    BOOLEAN IsGpt, HaveBootIndex;

    if (!FwGetBootPartition(&Number, Signature, &IsGpt))
        return FALSE;

    HaveBootIndex = FwGetBootDiskIndex(&BootIndex);
    for (Index = 0; FwGetDisk(Index, &Disk); Index++)
    {
        if (HaveBootIndex && Index != BootIndex)
            continue;

        Disk.DiskIndex = Index;

        if (IsGpt)
        {
            if (GptFindPartition(&Disk, NULL, Signature, Partition))
                return TRUE;
        }
        else if (DkRead(&Disk, 0, sizeof(Sector), Sector) &&
                 RtlEqualMemory(Sector + DK_MBR_SIGNATURE_OFFSET, Signature, sizeof(ULONG)) &&
                 FwGetBootDevice(Partition))
        {
            Partition->DiskIndex = Index;
            Partition->PartitionNumber = Number;
            return TRUE;
        }
    }

    return FALSE;
}
