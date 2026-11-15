/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Gathering the blocks written to the journal into long sequential writes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "e4core.h"
#include "e4priv.h"

#include <stddef.h>
#include <string.h>

#define E4_STAGE_BLOCKS     256
#define E4_STAGE_MINIMUM    16

static
E4_VOLUME *
E4StageVolume(
    struct ext4_blockdev *Device)
{
    return (E4_VOLUME *)((char *)Device - offsetof(E4_VOLUME, Device));
}

static
bool
E4StageInJournal(
    E4_VOLUME *Volume,
    uint64_t Block)
{
    uint32_t Index;

    for (Index = 0; Index < Volume->StageRunCount; Index++)
    {
        if ((Block >= Volume->StageRuns[Index].Start) &&
            (Block - Volume->StageRuns[Index].Start < Volume->StageRuns[Index].Count))
            return true;
    }
    return false;
}

static
bool
E4StageIsStaged(
    E4_VOLUME *Volume,
    uint64_t Block)
{
    uint32_t Index;

    for (Index = 0; Index < Volume->StageCount; Index++)
    {
        if (Volume->StageBlock[Index] == Block)
            return true;
    }
    return false;
}

static
int
E4StageEmit(
    E4_VOLUME *Volume,
    uint64_t First,
    uint32_t Count)
{
    struct ext4_blockdev *Device = &Volume->Device;
    uint32_t SectorSize = Volume->StageRaw->ph_bsize;
    uint64_t Sector = (First * Volume->BlockSize + Device->part_offset) / SectorSize;

    ext4_assert(Count != 0);
    ext4_assert(Count <= Volume->StageCapacity);
    return Volume->StageRaw->bwrite(Device, Volume->StageOut, Sector, Count * (Volume->BlockSize / SectorSize));
}

int
E4StageFlush(
    E4_VOLUME *Volume)
{
    uint32_t BlockSize = Volume->BlockSize;
    uint32_t Count = Volume->StageCount;
    uint16_t *Order = Volume->StageOrder;
    uint32_t Ordered = 0;
    uint32_t Group = 0;
    uint32_t Index;
    uint64_t First = 0;
    uint32_t Run = 0;
    int Result = EOK;

    if (Count == 0)
        return EOK;
    ext4_assert(Volume->StageData != NULL);
    ext4_assert(Count <= Volume->StageCapacity);

    /* A transaction is valid on the disk once its commit record is there, so that record is written after the
     * other blocks of its transaction, and a transaction after the ones before it. Within a transaction the
     * blocks go out in the order of their place in the journal, which makes one run of the usual case. */
    for (Index = 0; Index < Count; Index++)
    {
        bool Commit = Volume->StageCommit[Index] != 0;
        uint32_t End = Commit ? Index : Index + 1;
        uint32_t Member;
        uint32_t Base;

        if (!Commit && (Index + 1 != Count))
            continue;

        Base = Ordered;
        for (Member = Group; Member < End; Member++)
        {
            uint32_t Slot = Ordered;

            while ((Slot > Base) && (Volume->StageBlock[Order[Slot - 1]] > Volume->StageBlock[Member]))
            {
                Order[Slot] = Order[Slot - 1];
                Slot--;
            }
            Order[Slot] = (uint16_t)Member;
            Ordered++;
        }
        if (Commit)
            Order[Ordered++] = (uint16_t)Index;
        Group = Index + 1;
    }
    ext4_assert(Ordered == Count);

    /* FIXME: no flush of the disk's own cache is asked for around the commit records, so a volatile disk cache
     * may still reorder them */
    for (Index = 0; (Index < Count) && (Result == EOK); Index++)
    {
        uint64_t Block = Volume->StageBlock[Order[Index]];

        if ((Run != 0) && (Block != First + Run))
        {
            Result = E4StageEmit(Volume, First, Run);
            Run = 0;
        }
        if (Run == 0)
            First = Block;
        memcpy(Volume->StageOut + (size_t)Run * BlockSize, Volume->StageData + (size_t)Order[Index] * BlockSize, BlockSize);
        Run++;
    }
    if ((Result == EOK) && (Run != 0))
        Result = E4StageEmit(Volume, First, Run);

    Volume->StageCount = 0;
    Volume->StageFreeEvents = Volume->Fs.free_events;
    if (Result != EOK)
        Volume->Failed = true;
    return Result;
}

static
int
E4StageWrite(
    struct ext4_blockdev *Device,
    const void *Buffer,
    uint64_t Sector,
    uint32_t Sectors)
{
    E4_VOLUME *Volume = E4StageVolume(Device);
    uint32_t BlockSize = Volume->BlockSize;
    uint64_t Offset = Sector * Volume->StageRaw->ph_bsize;
    uint64_t Length = (uint64_t)Sectors * Volume->StageRaw->ph_bsize;
    bool Stage = false;

    ext4_assert(Volume->StageData != NULL);
    if ((Offset >= Device->part_offset) && (Length != 0) &&
        ((Offset - Device->part_offset) % BlockSize == 0) && (Length % BlockSize == 0) &&
        (Length / BlockSize <= Volume->StageCapacity))
    {
        uint64_t First = (Offset - Device->part_offset) / BlockSize;
        uint32_t Blocks = (uint32_t)(Length / BlockSize);
        uint32_t Index;

        Stage = true;
        for (Index = 0; Index < Blocks; Index++)
        {
            if (!E4StageInJournal(Volume, First + Index))
                Stage = false;
        }

        if (Stage)
        {
            bool Again = false;
            int Result;

            for (Index = 0; Index < Blocks; Index++)
            {
                if (E4StageIsStaged(Volume, First + Index))
                    Again = true;
            }
            if (Again || (Volume->StageCount + Blocks > Volume->StageCapacity))
            {
                Result = E4StageFlush(Volume);
                if (Result != EOK)
                    return Result;
            }
            for (Index = 0; Index < Blocks; Index++)
            {
                uint32_t Slot = Volume->StageCount++;

                Volume->StageBlock[Slot] = First + Index;
                Volume->StageCommit[Slot] = 0;
                memcpy(Volume->StageData + (size_t)Slot * BlockSize, (const uint8_t *)Buffer + (size_t)Index * BlockSize, BlockSize);
            }
            return EOK;
        }
    }

    /* Nothing reaches its own place on the disk before the journal that describes it */
    if (Volume->StageCount != 0)
    {
        int Result = E4StageFlush(Volume);

        if (Result != EOK)
            return Result;
    }
    return Volume->StageRaw->bwrite(Device, Buffer, Sector, Sectors);
}

static
int
E4StageRead(
    struct ext4_blockdev *Device,
    void *Buffer,
    uint64_t Sector,
    uint32_t Sectors)
{
    E4_VOLUME *Volume = E4StageVolume(Device);

    ext4_assert(Volume->StageData != NULL);
    if (Volume->StageCount != 0)
    {
        uint64_t Offset = Sector * Volume->StageRaw->ph_bsize;
        uint64_t Length = (uint64_t)Sectors * Volume->StageRaw->ph_bsize;

        if ((Offset >= Device->part_offset) && (Length != 0))
        {
            uint64_t Block = (Offset - Device->part_offset) / Volume->BlockSize;
            uint64_t Last = (Offset - Device->part_offset + Length - 1) / Volume->BlockSize;
            bool Staged = false;

            for (; Block <= Last; Block++)
            {
                if (E4StageIsStaged(Volume, Block))
                    Staged = true;
            }
            if (Staged)
            {
                int Result = E4StageFlush(Volume);

                if (Result != EOK)
                    return Result;
            }
        }
    }
    return Volume->StageRaw->bread(Device, Buffer, Sector, Sectors);
}

void
E4StageCommit(
    E4_VOLUME *Volume)
{
    if ((Volume->StageData != NULL) && (Volume->StageCount != 0))
        Volume->StageCommit[Volume->StageCount - 1] = 1;
}

/* Blocks that a transaction still in memory has freed are free only once that transaction is on the disk: until
 * then the volume a crash leaves behind still uses them, and data written into them would land in its files. */
int
E4StageSettleFreed(
    E4_VOLUME *Volume)
{
    ext4_assert(Volume->TransDepth == 0);
    if ((Volume->StageData == NULL) || (Volume->StageFreeEvents == Volume->Fs.free_events))
        return EOK;
    if (Volume->StageCount == 0)
    {
        Volume->StageFreeEvents = Volume->Fs.free_events;
        return EOK;
    }
    return E4StageFlush(Volume);
}

void
E4StageStart(
    E4_VOLUME *Volume)
{
    uint32_t BlockSize = Volume->BlockSize;
    uint32_t JournalBlocks = jbd_get32(&Volume->JournalFs.sb, maxlen);
    uint32_t JournalInode = ext4_get32(&Volume->Fs.sb, journal_inode_number);
    uint32_t Capacity = E4_STAGE_BLOCKS;
    uint32_t Block = 1;
    uint8_t *Memory;
    size_t Size;

    ext4_assert(Volume->Journaled);
    ext4_assert(Volume->StageData == NULL);
    Volume->StageRunCount = 0;
    Volume->StageCount = 0;

    /* TODO: a journal on its own device, or one in more pieces than the table holds, is written block by block */
    if ((JournalInode == 0) || (Volume->Device.bdif->ph_bsize == 0) || (BlockSize % Volume->Device.bdif->ph_bsize != 0))
        return;
    if (Capacity > JournalBlocks / 4)
        Capacity = JournalBlocks / 4;
    if (Capacity < E4_STAGE_MINIMUM)
        return;

    /* The first block of the journal is its superblock, which is never staged */
    while (Block < JournalBlocks)
    {
        uint64_t Physical = 0;
        uint32_t Mapped = 0;

        if ((E4Map(Volume, JournalInode, Block, JournalBlocks - Block, false, &Physical, &Mapped, NULL) != EOK) ||
            (Physical == 0) || (Mapped == 0))
        {
            Volume->StageRunCount = 0;
            return;
        }
        if ((Volume->StageRunCount != 0) &&
            (Volume->StageRuns[Volume->StageRunCount - 1].Start + Volume->StageRuns[Volume->StageRunCount - 1].Count == Physical))
        {
            Volume->StageRuns[Volume->StageRunCount - 1].Count += Mapped;
        }
        else
        {
            if (Volume->StageRunCount == E4_STAGE_RUNS)
            {
                Volume->StageRunCount = 0;
                return;
            }
            Volume->StageRuns[Volume->StageRunCount].Start = Physical;
            Volume->StageRuns[Volume->StageRunCount].Count = Mapped;
            Volume->StageRunCount++;
        }
        Block += Mapped;
    }

    Size = (size_t)Capacity * (2 * (size_t)BlockSize + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint8_t));
    Memory = ext4_malloc(Size);
    if (Memory == NULL)
    {
        Volume->StageRunCount = 0;
        return;
    }

    Volume->StageOut = Memory;
    Volume->StageData = Volume->StageOut + (size_t)Capacity * BlockSize;
    Volume->StageBlock = (uint64_t *)(Volume->StageData + (size_t)Capacity * BlockSize);
    Volume->StageOrder = (uint16_t *)(Volume->StageBlock + Capacity);
    Volume->StageCommit = (uint8_t *)(Volume->StageOrder + Capacity);
    Volume->StageCapacity = Capacity;
    Volume->StageFreeEvents = Volume->Fs.free_events;

    Volume->StageRaw = Volume->Device.bdif;
    Volume->StageInterface = *Volume->StageRaw;
    Volume->StageInterface.bread = E4StageRead;
    Volume->StageInterface.bwrite = E4StageWrite;
    Volume->Device.bdif = &Volume->StageInterface;
}

void
E4StageStop(
    E4_VOLUME *Volume)
{
    if (Volume->StageData == NULL)
        return;
    ext4_assert(Volume->StageCount == 0);
    ext4_assert(Volume->Device.bdif == &Volume->StageInterface);

    Volume->StageRaw->ph_refctr = Volume->StageInterface.ph_refctr;
    Volume->StageRaw->bread_ctr = Volume->StageInterface.bread_ctr;
    Volume->StageRaw->bwrite_ctr = Volume->StageInterface.bwrite_ctr;
    Volume->Device.bdif = Volume->StageRaw;
    Volume->StageRaw = NULL;

    ext4_free(Volume->StageOut);
    Volume->StageBlock = NULL;
    Volume->StageOrder = NULL;
    Volume->StageCommit = NULL;
    Volume->StageOut = NULL;
    Volume->StageData = NULL;
    Volume->StageCapacity = 0;
    Volume->StageRunCount = 0;
}
