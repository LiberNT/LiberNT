/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Write space of the boot RAM volume: memory taken only for written data, and the volume grown over it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <expandcore.h>
#include "rdscore.h"

static int Failures;
static int Allocations;
static int AllocationLimit = -1;

#define CHECK(Condition, What) \
    do { if (!(Condition)) { Failures++; printf("FAIL: %s (line %d)\n", What, __LINE__); } } while (0)

static void* TestAllocate(void* Context)
{
    (void)Context;
    if (AllocationLimit >= 0 && Allocations >= AllocationLimit)
        return NULL;
    Allocations++;
    return calloc(1, RDS_CHUNK_SIZE);
}

static void TestFree(void* Context, void* Chunk)
{
    (void)Context;
    Allocations--;
    free(Chunk);
}

static void* TestLoad(void* Context, void* volatile* Slot)
{
    (void)Context;
    return __atomic_load_n((void**)Slot, __ATOMIC_ACQUIRE);
}

static void* TestPublish(void* Context, void* volatile* Slot, void* Chunk)
{
    void* Expected = NULL;

    (void)Context;
    if (__atomic_compare_exchange_n((void**)Slot, &Expected, Chunk, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return NULL;
    return Expected;
}

static const RDS_OPS Ops = { NULL, TestAllocate, TestFree, TestLoad, TestPublish };

typedef struct _LOWER
{
    uint8_t* Data;
    uint64_t Size;
    uint64_t Bytes;
    int Unaligned;
} LOWER;

static int LowerIo(void* Context, uint64_t Offset, uint32_t Length, void* Buffer, int Write)
{
    LOWER* Lower = (LOWER*)Context;

    if (Offset > Lower->Size || Length > Lower->Size - Offset || Offset % 512 != 0 || Length % 512 != 0)
    {
        Lower->Unaligned += Offset % 512 != 0 || Length % 512 != 0;
        return 0;
    }
    if (Write)
        memcpy(Lower->Data + Offset, Buffer, Length);
    else
        memcpy(Buffer, Lower->Data + Offset, Length);
    Lower->Bytes += Length;
    return 1;
}

static void MakeBootSector(uint8_t* Sector, uint16_t BytesPerSector, uint8_t SectorsPerCluster, uint64_t Sectors)
{
    int Index;

    memset(Sector, 0, RDS_BOOT_SECTOR);
    memcpy(Sector + 3, "NTFS    ", 8);
    Sector[11] = (uint8_t)BytesPerSector;
    Sector[12] = (uint8_t)(BytesPerSector >> 8);
    Sector[13] = SectorsPerCluster;
    for (Index = 0; Index < 8; Index++)
        Sector[0x28 + Index] = (uint8_t)(Sectors >> (8 * Index));
}

static void TestStore(void)
{
    const uint64_t Physical = 1024 * 1024;
    const uint64_t Logical = Physical + 3 * RDS_CHUNK_SIZE + 4096;
    void* volatile Chunks[4] = { NULL, NULL, NULL, NULL };
    uint8_t Buffer[8192];
    uint8_t Check[8192];
    uint8_t* Disk = calloc(1, Physical);
    RDS_STORE Store;
    uint8_t* Bounce = calloc(1, 512);
    LOWER Lower = { Disk, Physical, 0, 0 };
    int Index;

    CHECK(RdsChunkCount(Physical, Logical) == 4, "the write space past the last whole chunk still gets a chunk");
    CHECK(RdsChunkCount(Physical, Physical) == 0, "a volume without write space needs no chunk");
    RdsStoreInit(&Store, Physical, Logical, Chunks, &Ops);

    memset(Buffer, 0xA5, sizeof(Buffer));
    CHECK(RdsStoreTransfer(&Store, Physical + 100, 4096, Buffer, 0) == RDS_OK, "unwritten space reads");
    CHECK(Buffer[0] == 0 && Buffer[4095] == 0, "unwritten space reads as zeros");
    CHECK(RdsStoreBytesInUse(&Store) == 0 && Allocations == 0, "reading takes no memory");

    memset(Buffer, 0, sizeof(Buffer));
    CHECK(RdsStoreTransfer(&Store, Physical, 8192, Buffer, 1) == RDS_OK, "zeros are written");
    CHECK(RdsStoreBytesInUse(&Store) == 0, "writing zeros to unwritten space takes no memory");

    for (Index = 0; Index < 10; Index++)
        Buffer[Index] = (uint8_t)(Index + 1);
    CHECK(RdsStoreTransfer(&Store, Physical + RDS_CHUNK_SIZE - 5, 10, Buffer, 1) == RDS_OK, "data is written across two chunks");
    CHECK(RdsStoreBytesInUse(&Store) == 2 * RDS_CHUNK_SIZE && Allocations == 2, "only the two written chunks take memory");
    memset(Check, 0, sizeof(Check));
    CHECK(RdsStoreTransfer(&Store, Physical + RDS_CHUNK_SIZE - 5, 10, Check, 0) == RDS_OK &&
          memcmp(Check, Buffer, 10) == 0, "data written across two chunks reads back");

    memset(Buffer, 0, 10);
    CHECK(RdsStoreTransfer(&Store, Physical + RDS_CHUNK_SIZE - 5, 10, Buffer, 1) == RDS_OK, "zeros overwrite data");
    CHECK(RdsStoreTransfer(&Store, Physical + RDS_CHUNK_SIZE - 5, 10, Check, 0) == RDS_OK &&
          memcmp(Check, Buffer, 10) == 0, "zeros written over data read back as zeros");

    for (Index = 0; Index < 4096; Index++)
        Buffer[Index] = (uint8_t)(Index * 7 + 3);
    CHECK(RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, Physical - 2048, 4096, Buffer, 1) == RDS_OK,
          "a write across the end of the loaded part is split");
    CHECK(memcmp(Disk + Physical - 2048, Buffer, 2048) == 0, "the loaded part takes its half of the write");
    CHECK(RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, Physical - 2048, 4096, Check, 0) == RDS_OK &&
          memcmp(Check, Buffer, 4096) == 0, "a read across the end of the loaded part returns both halves");
    CHECK(RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, 4096, 4096, Check, 1) == RDS_OK && Lower.Bytes == 2048 + 2048 + 4096,
          "a transfer inside the loaded part goes only to the device");

    memset(Disk + 1000, 0x11, 3000);
    for (Index = 0; Index < 1500; Index++)
        Buffer[Index] = (uint8_t)(Index * 13 + 5);
    CHECK(RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, 1300, 1500, Buffer, 1) == RDS_OK,
          "a write that starts and ends inside sectors of the loaded part is done with whole sectors");
    CHECK(memcmp(Disk + 1300, Buffer, 1500) == 0 && Disk[1299] == 0x11 && Disk[2800] == 0x11,
          "the rest of the partly written sectors keeps its data");
    CHECK(RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, 1301, 777, Check, 0) == RDS_OK &&
          memcmp(Check, Buffer + 1, 777) == 0, "a read of part of a sector returns those bytes");
    CHECK(RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, Physical - 700, 1500, Buffer, 1) == RDS_OK &&
          RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, Physical - 700, 1500, Check, 0) == RDS_OK &&
          memcmp(Check, Buffer, 1500) == 0, "a transfer that is not sector aligned crosses the end of the loaded part");
    CHECK(Lower.Unaligned == 0, "the device only ever sees whole sectors");

    CHECK(RdsStoreTransfer(&Store, Logical - 4096, 4096, Buffer, 0) == RDS_OK, "the last sector of the volume reads");
    CHECK(RdsStoreTransfer(&Store, Logical - 4095, 4096, Buffer, 0) == RDS_BAD_PARAMETER, "a read past the volume fails");
    CHECK(RdsStoreTransfer(&Store, Physical - 1, 2, Buffer, 0) == RDS_BAD_PARAMETER,
          "the store refuses a range that starts in the loaded part");
    CHECK(RdsTransfer(&Store, LowerIo, &Lower, 512, Bounce, Logical, 1, Buffer, 1) == RDS_BAD_PARAMETER,
          "a write past the volume fails");

    AllocationLimit = Allocations;
    memset(Buffer, 0x5A, 16);
    CHECK(RdsStoreTransfer(&Store, Physical + 3 * RDS_CHUNK_SIZE, 16, Buffer, 1) == RDS_NO_MEMORY,
          "a write that finds no memory fails");
    memset(Buffer, 0, 16);
    CHECK(RdsStoreTransfer(&Store, Physical + 3 * RDS_CHUNK_SIZE, 16, Buffer, 1) == RDS_OK,
          "writing zeros still works without memory");
    AllocationLimit = -1;

    RdsStoreRelease(&Store);
    CHECK(Allocations == 0, "releasing the store frees every chunk");
    free(Bounce);
    free(Disk);
}

static void TestPlan(void)
{
    uint8_t Sector[RDS_BOOT_SECTOR];
    uint64_t Logical = 0;
    uint64_t VolumeSectors = 0;
    uint32_t SectorSize = 0;

    MakeBootSector(Sector, 512, 8, 2047);
    CHECK(RdsPlanGrowth(Sector, 1024 * 1024, 64 * 1024 * 1024 + 100, &Logical, &VolumeSectors, &SectorSize) == RDS_OK,
          "a volume that fits its device grows");
    CHECK(Logical == 1024 * 1024 + 64 * 1024 * 1024 && VolumeSectors == Logical / 512 - 1 && SectorSize == 512,
          "the grown volume ends on a sector and keeps its last sector for the backup boot sector");

    CHECK(RdsPlanGrowth(Sector, 1024 * 1024, 0, &Logical, &VolumeSectors, &SectorSize) == RDS_BAD_PARAMETER,
          "no write space leaves the volume as it is");
    MakeBootSector(Sector, 512, 8, 2048);
    CHECK(RdsPlanGrowth(Sector, 1024 * 1024, 1 << 20, &Logical, &VolumeSectors, &SectorSize) == RDS_BAD_PARAMETER,
          "a volume larger than its device is not grown");
    MakeBootSector(Sector, 500, 8, 100);
    CHECK(RdsPlanGrowth(Sector, 1024 * 1024, 1 << 20, &Logical, &VolumeSectors, &SectorSize) == RDS_BAD_PARAMETER,
          "a sector size that is not a power of two is refused");
    MakeBootSector(Sector, 512, 8, 100);
    memcpy(Sector + 3, "MSDOS5.0", 8);
    CHECK(RdsPlanGrowth(Sector, 1024 * 1024, 1 << 20, &Logical, &VolumeSectors, &SectorSize) == RDS_NOT_NTFS,
          "a FAT volume is left alone");
}

static int GrowImage(const char* Input, const char* Output, uint64_t Space)
{
    void* volatile* Chunks;
    uint8_t* Disk;
    uint8_t* Workspace;
    uint64_t Logical, VolumeSectors, Offset;
    uint32_t SectorSize, Count, Bytes;
    RDS_STORE Store;
    LOWER Lower;
    FILE* File;
    long Size;
    int Result;

    File = fopen(Input, "rb");
    if (!File || fseek(File, 0, SEEK_END) != 0 || (Size = ftell(File)) <= 0 || fseek(File, 0, SEEK_SET) != 0)
        return 1;
    Disk = malloc((size_t)Size);
    if (!Disk || fread(Disk, 1, (size_t)Size, File) != (size_t)Size)
        return 1;
    fclose(File);

    Result = RdsPlanGrowth(Disk, (uint64_t)Size, Space, &Logical, &VolumeSectors, &SectorSize);
    CHECK(Result == RDS_OK, "the image plans its growth");
    if (Result != RDS_OK)
        return 1;

    Count = RdsChunkCount((uint64_t)Size, Logical);
    Chunks = calloc(Count, sizeof(void*));
    Workspace = calloc(1, EXPAND_WORKSPACE_BYTES);
    RdsStoreInit(&Store, (uint64_t)Size, Logical, Chunks, &Ops);
    Lower.Data = Disk;
    Lower.Size = (uint64_t)Size;
    Lower.Bytes = 0;
    Lower.Unaligned = 0;

    Result = RdsGrowVolume(&Store, LowerIo, &Lower, VolumeSectors, SectorSize, Workspace);
    CHECK(Result == RDS_OK, "the volume grows over the write space");
    CHECK(Lower.Unaligned == 0, "growing the volume only sends whole sectors to the device");
    printf("grown from %ld to %llu bytes, %llu bytes of memory taken\n",
           Size, (unsigned long long)Logical, (unsigned long long)RdsStoreBytesInUse(&Store));
    CHECK(RdsStoreBytesInUse(&Store) <= 2 * RDS_CHUNK_SIZE,
          "growing the volume takes memory only for the backup boot sector and the bitmap it writes past the loaded part");

    File = fopen(Output, "wb");
    CHECK(File != NULL, "the grown image is written");
    if (File)
    {
        fwrite(Disk, 1, (size_t)Size, File);
        for (Offset = (uint64_t)Size; Offset < Logical; Offset += Bytes)
        {
            Bytes = Logical - Offset < RDS_CHUNK_SIZE ? (uint32_t)(Logical - Offset) : RDS_CHUNK_SIZE;
            RdsStoreTransfer(&Store, Offset, Bytes, Workspace, 0);
            fwrite(Workspace, 1, Bytes, File);
        }
        fclose(File);
    }

    RdsStoreRelease(&Store);
    free((void*)Chunks);
    free(Workspace);
    free(Disk);
    return 0;
}

int main(int Argc, char** Argv)
{
    TestStore();
    TestPlan();
    if (Argc == 4 && GrowImage(Argv[1], Argv[2], strtoull(Argv[3], NULL, 0) << 20) != 0)
        Failures++;
    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
