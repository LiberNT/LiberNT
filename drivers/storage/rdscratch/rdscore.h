/*
 * PROJECT:     LiberNT Storage
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Write space past the loaded part of the boot RAM volume, taken from memory as it is written
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RDSCORE_H_
#define _RDSCORE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RDS_CHUNK_SIZE      (64 * 1024)
#define RDS_BOOT_SECTOR     512

#define RDS_OK              0
#define RDS_IO_ERROR        1
#define RDS_NO_MEMORY       2
#define RDS_BAD_PARAMETER   3
#define RDS_NOT_NTFS        4
#define RDS_GROW_FAILED     5

typedef struct _RDS_OPS
{
    void* Context;
    void* (*Allocate)(void* Context);
    void (*Free)(void* Context, void* Chunk);
    void* (*Load)(void* Context, void* volatile* Slot);
    void* (*Publish)(void* Context, void* volatile* Slot, void* Chunk);
} RDS_OPS;

typedef struct _RDS_STORE
{
    uint64_t Physical;
    uint64_t Logical;
    uint32_t ChunkCount;
    void* volatile* Chunks;
    const RDS_OPS* Ops;
} RDS_STORE;

typedef int (*RDS_LOWER_IO)(void* Context, uint64_t Offset, uint32_t Length, void* Buffer, int Write);

uint32_t RdsChunkCount(uint64_t Physical, uint64_t Logical);

void RdsStoreInit(RDS_STORE* Store,
                  uint64_t Physical,
                  uint64_t Logical,
                  void* volatile* Chunks,
                  const RDS_OPS* Ops);

int RdsStoreTransfer(RDS_STORE* Store, uint64_t Offset, uint32_t Length, uint8_t* Buffer, int Write);

uint64_t RdsStoreBytesInUse(const RDS_STORE* Store);

void RdsStoreRelease(RDS_STORE* Store);

int RdsPlanGrowth(const uint8_t* BootSector,
                  uint64_t Physical,
                  uint64_t Space,
                  uint64_t* Logical,
                  uint64_t* VolumeSectors,
                  uint32_t* SectorSize);

int RdsTransfer(RDS_STORE* Store,
                RDS_LOWER_IO Lower,
                void* LowerContext,
                uint32_t SectorSize,
                uint8_t* Bounce,
                uint64_t Offset,
                uint32_t Length,
                uint8_t* Buffer,
                int Write);

int RdsGrowVolume(RDS_STORE* Store,
                  RDS_LOWER_IO Lower,
                  void* LowerContext,
                  uint64_t VolumeSectors,
                  uint32_t SectorSize,
                  void* Workspace);

#ifdef __cplusplus
}
#endif

#endif
