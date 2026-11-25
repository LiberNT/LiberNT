/*
 * PROJECT:     LiberNT Storage
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Write space past the loaded part of the boot RAM volume, taken from memory as it is written
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rdscore.h"

#include <string.h>

#include <expandcore.h>

#define RDS_NTFS_OEM_OFFSET        3
#define RDS_NTFS_SECTOR_OFFSET     11
#define RDS_NTFS_CLUSTER_OFFSET    13
#define RDS_NTFS_SECTORS_OFFSET    0x28

typedef struct _RDS_GROW_CONTEXT
{
    RDS_STORE* Store;
    RDS_LOWER_IO Lower;
    void* LowerContext;
    uint32_t SectorSize;
    uint8_t* Bounce;
} RDS_GROW_CONTEXT;

static uint16_t RdsLe16(const uint8_t* Data)
{
    return (uint16_t)(Data[0] | (Data[1] << 8));
}

static uint64_t RdsLe64(const uint8_t* Data)
{
    uint64_t Value = 0;
    int Index;

    for (Index = 7; Index >= 0; Index--)
        Value = (Value << 8) | Data[Index];
    return Value;
}

static int RdsIsZero(const uint8_t* Buffer, uint32_t Length)
{
    uint32_t Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Buffer[Index] != 0)
            return 0;
    }
    return 1;
}

uint32_t RdsChunkCount(uint64_t Physical, uint64_t Logical)
{
    uint64_t Count;

    if (Logical <= Physical)
        return 0;
    Count = (Logical - Physical + RDS_CHUNK_SIZE - 1) / RDS_CHUNK_SIZE;
    return Count > UINT32_MAX ? 0 : (uint32_t)Count;
}

void RdsStoreInit(RDS_STORE* Store,
                  uint64_t Physical,
                  uint64_t Logical,
                  void* volatile* Chunks,
                  const RDS_OPS* Ops)
{
    Store->Physical = Physical;
    Store->Logical = Logical;
    Store->ChunkCount = RdsChunkCount(Physical, Logical);
    Store->Chunks = Chunks;
    Store->Ops = Ops;
}

static uint8_t* RdsChunk(RDS_STORE* Store, uint32_t Index, int Allocate)
{
    const RDS_OPS* Ops = Store->Ops;
    void* Chunk;
    void* New;

    Chunk = Ops->Load(Ops->Context, &Store->Chunks[Index]);
    if (Chunk || !Allocate)
        return (uint8_t*)Chunk;

    New = Ops->Allocate(Ops->Context);
    if (!New)
        return NULL;

    Chunk = Ops->Publish(Ops->Context, &Store->Chunks[Index], New);
    if (Chunk)
    {
        Ops->Free(Ops->Context, New);
        return (uint8_t*)Chunk;
    }
    return (uint8_t*)New;
}

int RdsStoreTransfer(RDS_STORE* Store, uint64_t Offset, uint32_t Length, uint8_t* Buffer, int Write)
{
    uint8_t* Chunk;
    uint64_t Relative;
    uint32_t Index;
    uint32_t Within;
    uint32_t Bytes;

    if (Offset < Store->Physical || Offset > Store->Logical || Length > Store->Logical - Offset)
        return RDS_BAD_PARAMETER;

    while (Length != 0)
    {
        Relative = Offset - Store->Physical;
        Index = (uint32_t)(Relative / RDS_CHUNK_SIZE);
        Within = (uint32_t)(Relative % RDS_CHUNK_SIZE);
        Bytes = RDS_CHUNK_SIZE - Within < Length ? RDS_CHUNK_SIZE - Within : Length;
        if (Index >= Store->ChunkCount)
            return RDS_BAD_PARAMETER;

        if (Write)
        {
            Chunk = RdsChunk(Store, Index, !RdsIsZero(Buffer, Bytes));
            if (Chunk)
                memcpy(Chunk + Within, Buffer, Bytes);
            else if (!RdsIsZero(Buffer, Bytes))
                return RDS_NO_MEMORY;
        }
        else
        {
            Chunk = RdsChunk(Store, Index, 0);
            if (Chunk)
                memcpy(Buffer, Chunk + Within, Bytes);
            else
                memset(Buffer, 0, Bytes);
        }

        Offset += Bytes;
        Buffer += Bytes;
        Length -= Bytes;
    }
    return RDS_OK;
}

uint64_t RdsStoreBytesInUse(const RDS_STORE* Store)
{
    uint64_t Bytes = 0;
    uint32_t Index;

    for (Index = 0; Index < Store->ChunkCount; Index++)
    {
        if (Store->Ops->Load(Store->Ops->Context, &Store->Chunks[Index]))
            Bytes += RDS_CHUNK_SIZE;
    }
    return Bytes;
}

void RdsStoreRelease(RDS_STORE* Store)
{
    void* Chunk;
    uint32_t Index;

    for (Index = 0; Index < Store->ChunkCount; Index++)
    {
        Chunk = Store->Ops->Load(Store->Ops->Context, &Store->Chunks[Index]);
        if (Chunk)
        {
            Store->Chunks[Index] = NULL;
            Store->Ops->Free(Store->Ops->Context, Chunk);
        }
    }
}

int RdsPlanGrowth(const uint8_t* BootSector,
                  uint64_t Physical,
                  uint64_t Space,
                  uint64_t* Logical,
                  uint64_t* VolumeSectors,
                  uint32_t* SectorSize)
{
    uint64_t Sectors;
    uint64_t Total;
    uint32_t Bytes;
    uint8_t Cluster;

    if (memcmp(BootSector + RDS_NTFS_OEM_OFFSET, "NTFS    ", 8) != 0)
        return RDS_NOT_NTFS;

    Bytes = RdsLe16(BootSector + RDS_NTFS_SECTOR_OFFSET);
    Cluster = BootSector[RDS_NTFS_CLUSTER_OFFSET];
    Sectors = RdsLe64(BootSector + RDS_NTFS_SECTORS_OFFSET);
    if (Bytes < 512 || Bytes > 4096 || (Bytes & (Bytes - 1)) != 0 || Cluster == 0 || Sectors == 0 ||
        Physical % Bytes != 0 || Sectors >= Physical / Bytes || Space > UINT64_MAX - Physical)
    {
        return RDS_BAD_PARAMETER;
    }

    Total = (Physical + Space) / Bytes;
    if (Total < 2 || Total - 1 <= Sectors)
        return RDS_BAD_PARAMETER;

    *Logical = Total * Bytes;
    *VolumeSectors = Total - 1;
    *SectorSize = Bytes;
    return RDS_OK;
}

static int RdsLowerTransfer(RDS_LOWER_IO Lower,
                            void* LowerContext,
                            uint32_t SectorSize,
                            uint8_t* Bounce,
                            uint64_t Offset,
                            uint32_t Length,
                            uint8_t* Buffer,
                            int Write)
{
    uint64_t Start;
    uint32_t Within;
    uint32_t Bytes;

    while (Length != 0)
    {
        Within = (uint32_t)(Offset % SectorSize);
        if (Within == 0 && Length >= SectorSize)
        {
            Bytes = Length - Length % SectorSize;
            if (!Lower(LowerContext, Offset, Bytes, Buffer, Write))
                return RDS_IO_ERROR;
        }
        else
        {
            Start = Offset - Within;
            Bytes = SectorSize - Within < Length ? SectorSize - Within : Length;
            if (!Lower(LowerContext, Start, SectorSize, Bounce, 0))
                return RDS_IO_ERROR;
            if (Write)
            {
                memcpy(Bounce + Within, Buffer, Bytes);
                if (!Lower(LowerContext, Start, SectorSize, Bounce, 1))
                    return RDS_IO_ERROR;
            }
            else
            {
                memcpy(Buffer, Bounce + Within, Bytes);
            }
        }

        Offset += Bytes;
        Buffer += Bytes;
        Length -= Bytes;
    }
    return RDS_OK;
}

int RdsTransfer(RDS_STORE* Store,
                RDS_LOWER_IO Lower,
                void* LowerContext,
                uint32_t SectorSize,
                uint8_t* Bounce,
                uint64_t Offset,
                uint32_t Length,
                uint8_t* Buffer,
                int Write)
{
    uint32_t Head;
    int Result;

    if (Offset > Store->Logical || Length > Store->Logical - Offset || SectorSize == 0 ||
        Store->Physical % SectorSize != 0)
    {
        return RDS_BAD_PARAMETER;
    }
    if (Length == 0)
        return RDS_OK;

    Head = Offset >= Store->Physical ? 0 :
           (Store->Physical - Offset < Length ? (uint32_t)(Store->Physical - Offset) : Length);
    if (Head != 0)
    {
        Result = RdsLowerTransfer(Lower, LowerContext, SectorSize, Bounce, Offset, Head, Buffer, Write);
        if (Result != RDS_OK)
            return Result;
    }
    if (Head == Length)
        return RDS_OK;
    return RdsStoreTransfer(Store, Offset + Head, Length - Head, Buffer + Head, Write);
}

static int RdsGrowRead(void* Context, uint64_t Offset, uint32_t Length, void* Buffer)
{
    RDS_GROW_CONTEXT* Grow = (RDS_GROW_CONTEXT*)Context;

    return RdsTransfer(Grow->Store, Grow->Lower, Grow->LowerContext, Grow->SectorSize, Grow->Bounce,
                       Offset, Length, (uint8_t*)Buffer, 0) == RDS_OK;
}

static int RdsGrowWrite(void* Context, uint64_t Offset, uint32_t Length, const void* Buffer)
{
    RDS_GROW_CONTEXT* Grow = (RDS_GROW_CONTEXT*)Context;

    return RdsTransfer(Grow->Store, Grow->Lower, Grow->LowerContext, Grow->SectorSize, Grow->Bounce,
                       Offset, Length, (uint8_t*)Buffer, 1) == RDS_OK;
}

int RdsGrowVolume(RDS_STORE* Store,
                  RDS_LOWER_IO Lower,
                  void* LowerContext,
                  uint64_t VolumeSectors,
                  uint32_t SectorSize,
                  void* Workspace)
{
    RDS_GROW_CONTEXT Grow;
    EXPAND_DEVICE Device;
    int Result;

    if (VolumeSectors == 0 || SectorSize == 0 || SectorSize > RDS_CHUNK_SIZE ||
        VolumeSectors >= Store->Logical / SectorSize)
    {
        return RDS_BAD_PARAMETER;
    }

    Grow.Store = Store;
    Grow.Lower = Lower;
    Grow.LowerContext = LowerContext;
    Grow.SectorSize = SectorSize;
    Grow.Bounce = (uint8_t*)Store->Ops->Allocate(Store->Ops->Context);
    if (!Grow.Bounce)
        return RDS_NO_MEMORY;

    memset(&Device, 0, sizeof(Device));
    Device.Context = &Grow;
    Device.Read = RdsGrowRead;
    Device.Write = RdsGrowWrite;
    Device.Log = NULL;
    Device.DiskSize = Store->Logical;
    Device.SectorSize = SectorSize;

    Result = ExpandGrowVolume(&Device, 0, VolumeSectors, Workspace) == EXPAND_OK ? RDS_OK : RDS_GROW_FAILED;
    Store->Ops->Free(Store->Ops->Context, Grow.Bounce);
    return Result;
}
