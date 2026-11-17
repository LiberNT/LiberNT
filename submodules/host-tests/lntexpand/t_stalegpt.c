/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The storage expander erases leftover GPT headers outside the MBR partitions before it grows one
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expandcore.h"

#define SECTOR        512
#define DISK_SECTORS  65536u
#define PART_START    2048u
#define PART_SECTORS  8192u
#define LAST_SECTOR   (DISK_SECTORS - 1)

static uint8_t* Disk;
static unsigned Writes;
static int Failures;

static int
ReadDisk(void* Context, uint64_t Offset, uint32_t Length, void* Buffer)
{
    (void)Context;
    if (Offset + Length > (uint64_t)DISK_SECTORS * SECTOR)
        return 0;
    memcpy(Buffer, Disk + Offset, Length);
    return 1;
}

static int
WriteDisk(void* Context, uint64_t Offset, uint32_t Length, const void* Buffer)
{
    (void)Context;
    if (Offset + Length > (uint64_t)DISK_SECTORS * SECTOR)
        return 0;
    memcpy(Disk + Offset, Buffer, Length);
    Writes++;
    return 1;
}

static void
LogDisk(void* Context, const char* Line)
{
    (void)Context;
    (void)Line;
}

static void
Layout(uint32_t Start, uint32_t Sectors, uint8_t Type, int Primary, int Backup)
{
    uint8_t* Entry = Disk + 0x1BE;
    uint8_t* Boot = Disk + (uint64_t)Start * SECTOR;
    uint16_t BytesPerSector = SECTOR;
    uint64_t VolumeSectors = Sectors - 1;

    memset(Disk, 0, (size_t)DISK_SECTORS * SECTOR);
    Disk[510] = 0x55;
    Disk[511] = 0xAA;
    Entry[4] = Type;
    memcpy(Entry + 8, &Start, sizeof(Start));
    memcpy(Entry + 12, &Sectors, sizeof(Sectors));

    if (Type == 0x07)
    {
        memcpy(Boot + 3, "NTFS    ", 8);
        memcpy(Boot + 11, &BytesPerSector, sizeof(BytesPerSector));
        Boot[13] = 8;
        memcpy(Boot + 0x28, &VolumeSectors, sizeof(VolumeSectors));
    }

    if (Primary)
        memcpy(Disk + SECTOR, "EFI PART", 8);
    if (Backup)
        memcpy(Disk + (uint64_t)LAST_SECTOR * SECTOR, "EFI PART", 8);
}

static int
HasHeader(uint64_t Lba)
{
    return memcmp(Disk + Lba * SECTOR, "EFI PART", 8) == 0;
}

static void
Expect(int Condition, const char* Case, const char* What)
{
    if (!Condition)
    {
        printf("FAIL %s: %s\n", Case, What);
        Failures++;
    }
}

static void
CheckErased(const char* Case, int Primary, int Backup)
{
    EXPAND_DEVICE Device = { NULL, ReadDisk, WriteDisk, LogDisk, (uint64_t)DISK_SECTORS * SECTOR, SECTOR };
    EXPAND_PLAN Plan;
    uint8_t Boot[SECTOR];
    uint32_t Expected = (uint32_t)(Primary != 0) + (uint32_t)(Backup != 0);
    int Status;

    Layout(PART_START, PART_SECTORS, 0x07, Primary, Backup);
    memcpy(Boot, Disk + (uint64_t)PART_START * SECTOR, SECTOR);
    Writes = 0;

    Status = ExpandBuildPlan(&Device, 0, 1024 * 1024, 0, &Plan);
    Expect(Status == EXPAND_OK, Case, "the plan is refused");
    Expect(Writes == 0, Case, "planning wrote to the disk");
    Expect(Plan.StaleGptCount == Expected, Case, "wrong number of leftover headers");
    Expect(!Primary || Plan.StaleGptLba[0] == 1, Case, "the primary leftover is not LBA 1");
    Expect(!Backup || Plan.StaleGptLba[Expected - 1] == LAST_SECTOR, Case, "the backup leftover is not the last sector");
    Expect(Plan.NewPartitionBytes / SECTOR + PART_START <= LAST_SECTOR - 32, Case,
           "the grown partition reaches the GPT area");

    Status = ExpandEraseStaleGpt(&Device, &Plan);
    Expect(Status == EXPAND_OK, Case, "erasing failed");
    Expect(Writes == Expected, Case, "erasing wrote more than the leftover headers");
    Expect(!HasHeader(1) && !HasHeader(LAST_SECTOR), Case, "a leftover header survived");
    Expect(memcmp(Boot, Disk + (uint64_t)PART_START * SECTOR, SECTOR) == 0, Case, "the partition changed");
}

static void
CheckRefused(const char* Case, uint32_t Start, uint32_t Sectors, uint8_t Type, int Primary, int Backup, int Expected)
{
    EXPAND_DEVICE Device = { NULL, ReadDisk, WriteDisk, LogDisk, (uint64_t)DISK_SECTORS * SECTOR, SECTOR };
    EXPAND_PLAN Plan;
    int Status;

    Layout(Start, Sectors, Type, Primary, Backup);
    Writes = 0;

    Status = ExpandBuildPlan(&Device, 0, 1024 * 1024, 0, &Plan);
    Expect(Status == Expected, Case, "unexpected status");
    Expect(Writes == 0, Case, "a refused plan wrote to the disk");
    Expect(!Primary || HasHeader(1), Case, "the primary header was touched");
    Expect(!Backup || HasHeader(LAST_SECTOR), Case, "the backup header was touched");
}

int
main(void)
{
    Disk = malloc((size_t)DISK_SECTORS * SECTOR);
    if (!Disk)
        return 1;

    CheckErased("MBR over a leftover backup GPT", 0, 1);
    CheckErased("MBR over leftover primary and backup GPT headers", 1, 1);
    CheckErased("MBR without GPT leftovers", 0, 0);
    CheckRefused("leftover backup header inside the partition", PART_START, DISK_SECTORS - PART_START, 0x07, 0, 1,
                 EXPAND_STALE_GPT);
    CheckRefused("leftover primary header inside a partition starting at LBA 1", 1, PART_SECTORS, 0x07, 1, 0,
                 EXPAND_STALE_GPT);
    CheckRefused("protective MBR without a GPT", 1, DISK_SECTORS - 1, 0xEE, 0, 0, EXPAND_CORRUPT);

    free(Disk);
    if (!Failures)
        printf("lntexpand-stale-gpt: leftover GPT headers are erased only outside the partitions\n");
    return Failures != 0;
}
