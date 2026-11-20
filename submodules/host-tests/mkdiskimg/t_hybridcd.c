/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the USB disk layout mkdiskimg -cd writes into a boot CD image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define CD 2048u
#define SECTOR 512u
#define VOLUME_CD_SECTORS 64u
#define VOLUME_SECTORS (VOLUME_CD_SECTORS * (CD / SECTOR))
#define TAIL_SECTORS 36u
#define TOTAL_SECTORS (VOLUME_SECTORS + TAIL_SECTORS)
#define CATALOG_CD_LBA 19u
#define BIOS_CD_LBA 20u
#define EFI_CD_LBA 21u
#define ESP_START (EFI_CD_LBA * (CD / SECTOR))
#define ESP_SECTORS 128u
#define ENTRY_COUNT 128u
#define ENTRY_SIZE 128u
#define ENTRY_SECTORS (ENTRY_COUNT * ENTRY_SIZE / SECTOR)
#define BOOT_CODE_SIZE 432u
#define SIGNATURE 0x1234ABCDu
#define DISK_ID "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0"

static const uint8_t DiskIdBytes[16] =
    {0x3c, 0x2d, 0x1e, 0x0f, 0x5a, 0x4b, 0x78, 0x69, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0};
static const uint8_t EspType[16] =
    {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

static int Failures;
static const char* Tool;
static const char* WorkDir;

#define CHECK(cond, ...)                                              \
    do                                                                \
    {                                                                 \
        if (!(cond))                                                  \
        {                                                             \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);      \
            fprintf(stderr, __VA_ARGS__);                             \
            fputc('\n', stderr);                                      \
            Failures++;                                               \
        }                                                             \
    } while (0)

static void Put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void Put32(uint8_t* p, uint32_t v) { Put16(p, (uint16_t)v); Put16(p + 2, (uint16_t)(v >> 16)); }
static void Put32Be(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t Get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t Get64(const uint8_t* p) { return (uint64_t)Get32(p) | (uint64_t)Get32(p + 4) << 32; }

static int IsZero(const uint8_t* p, size_t n)
{
    while (n--)
    {
        if (*p++)
            return 0;
    }
    return 1;
}

static uint32_t Crc32(const uint8_t* p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    int bit;

    while (n--)
    {
        crc ^= *p++;
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void Path(char* out, size_t size, const char* name)
{
    snprintf(out, size, "%s/%s", WorkDir, name);
}

static int WriteFile(const char* path, const uint8_t* data, size_t size)
{
    FILE* f = fopen(path, "wb");
    int ok = f && fwrite(data, 1, size, f) == size;

    if (f && fclose(f) != 0)
        ok = 0;
    return ok;
}

static uint8_t* ReadFile(const char* path, size_t* size)
{
    FILE* f = fopen(path, "rb");
    uint8_t* data = NULL;
    long length;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0)
    {
        data = malloc((size_t)length);
        if (data && fread(data, 1, (size_t)length, f) != (size_t)length)
        {
            free(data);
            data = NULL;
        }
        *size = (size_t)length;
    }
    fclose(f);
    return data;
}

static void BuildIso(uint8_t* iso)
{
    uint8_t* pvd = iso + 16 * CD;
    uint8_t* brvd = iso + 17 * CD;
    uint8_t* term = iso + 18 * CD;
    uint8_t* cat = iso + CATALOG_CD_LBA * CD;
    uint8_t* fat = iso + EFI_CD_LBA * CD;
    uint32_t sum = 0;
    unsigned int i;

    memset(iso, 0, VOLUME_SECTORS * SECTOR);

    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    Put32(pvd + 80, VOLUME_CD_SECTORS);
    Put32Be(pvd + 84, VOLUME_CD_SECTORS);
    Put16(pvd + 128, CD);
    pvd[130] = CD >> 8;
    pvd[131] = CD & 0xFF;

    brvd[0] = 0;
    memcpy(brvd + 1, "CD001", 5);
    brvd[6] = 1;
    memcpy(brvd + 7, "EL TORITO SPECIFICATION", 23);
    Put32(brvd + 71, CATALOG_CD_LBA);

    term[0] = 0xFF;
    memcpy(term + 1, "CD001", 5);
    term[6] = 1;

    cat[0] = 0x01;
    memcpy(cat + 4, "LiberNT test", 12);
    cat[30] = 0x55;
    cat[31] = 0xAA;
    for (i = 0; i < 32; i += 2)
        sum += (uint32_t)cat[i] | (uint32_t)cat[i + 1] << 8;
    Put16(cat + 28, (uint16_t)(0x10000u - (sum & 0xFFFFu)));
    cat[32] = 0x88;
    Put16(cat + 32 + 6, 4);
    Put32(cat + 32 + 8, BIOS_CD_LBA);
    cat[64] = 0x91;
    cat[65] = 0xEF;
    Put16(cat + 66, 1);
    cat[96] = 0x88;
    Put16(cat + 96 + 6, ESP_SECTORS);
    Put32(cat + 96 + 8, EFI_CD_LBA);

    Put32(iso + BIOS_CD_LBA * CD + 64, 0x7078C0FBu);

    fat[0] = 0xEB;
    fat[1] = 0x3C;
    fat[2] = 0x90;
    Put16(fat + 11, SECTOR);
    fat[13] = 4;
    Put16(fat + 14, 1);
    fat[16] = 2;
    Put16(fat + 17, 512);
    Put16(fat + 19, ESP_SECTORS);
    fat[21] = 0xF8;
    fat[510] = 0x55;
    fat[511] = 0xAA;

    for (i = 40 * CD; i < VOLUME_SECTORS * SECTOR; i++)
        iso[i] = (uint8_t)(i * 7 + 3);
}

static int RunTool(const char* iso, const char* mbr)
{
    char command[4096];
    int status;

    if (mbr)
        snprintf(command, sizeof(command), "'%s' -cd '%s' -mbr '%s' -signature %08x -diskid %s", Tool, iso, mbr, SIGNATURE, DISK_ID);
    else
        snprintf(command, sizeof(command), "'%s' -cd '%s' -signature %08x -diskid %s", Tool, iso, SIGNATURE, DISK_ID);
    status = system(command);
    return (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}

static void CheckHeader(const uint8_t* disk, uint64_t lba, uint64_t alternate, uint64_t entries_lba)
{
    const uint8_t* h = disk + lba * SECTOR;
    uint8_t copy[92];

    CHECK(memcmp(h, "EFI PART", 8) == 0, "GPT header at LBA %llu has no signature", (unsigned long long)lba);
    CHECK(Get32(h + 8) == 0x00010000u, "GPT revision %08x", Get32(h + 8));
    CHECK(Get32(h + 12) == 92, "GPT header size %u", Get32(h + 12));
    memcpy(copy, h, sizeof(copy));
    memset(copy + 16, 0, 4);
    CHECK(Crc32(copy, sizeof(copy)) == Get32(h + 16), "GPT header CRC at LBA %llu", (unsigned long long)lba);
    CHECK(Get64(h + 24) == lba, "MyLBA %llu", (unsigned long long)Get64(h + 24));
    CHECK(Get64(h + 32) == alternate, "AlternateLBA %llu", (unsigned long long)Get64(h + 32));
    CHECK(Get64(h + 40) == 34, "FirstUsableLBA %llu", (unsigned long long)Get64(h + 40));
    CHECK(Get64(h + 48) == TOTAL_SECTORS - ENTRY_SECTORS - 2, "LastUsableLBA %llu", (unsigned long long)Get64(h + 48));
    CHECK(memcmp(h + 56, DiskIdBytes, 16) == 0, "disk GUID differs from -diskid");
    CHECK(Get64(h + 72) == entries_lba, "PartitionEntryLBA %llu", (unsigned long long)Get64(h + 72));
    CHECK(Get32(h + 80) == ENTRY_COUNT && Get32(h + 84) == ENTRY_SIZE, "entry count/size %u/%u", Get32(h + 80), Get32(h + 84));
    CHECK(Get32(h + 88) == Crc32(disk + entries_lba * SECTOR, ENTRY_COUNT * ENTRY_SIZE), "entry array CRC at LBA %llu", (unsigned long long)entries_lba);
    CHECK(IsZero(h + 92, SECTOR - 92), "GPT header reserved bytes not zero");
}

static void CheckHybrid(const uint8_t* disk, size_t size, const uint8_t* iso, const uint8_t* boot_code)
{
    const uint8_t* e = disk + 446;
    const uint8_t* entries = disk + 2 * SECTOR;
    static const char name[] = "EFI system partition";
    unsigned int i;

    CHECK(size == TOTAL_SECTORS * SECTOR, "image size %zu, expected %u", size, TOTAL_SECTORS * SECTOR);

    if (boot_code)
    {
        CHECK(memcmp(disk, boot_code, BOOT_CODE_SIZE) == 0, "MBR boot code not copied");
        CHECK(Get64(disk + BOOT_CODE_SIZE) == BIOS_CD_LBA * (CD / SECTOR), "BIOS image LBA %llu", (unsigned long long)Get64(disk + BOOT_CODE_SIZE));
    }
    else
    {
        CHECK(IsZero(disk, 440), "EFI-only image has MBR boot code");
    }
    CHECK(Get32(disk + 440) == SIGNATURE, "MBR disk signature %08x", Get32(disk + 440));
    CHECK(disk[510] == 0x55 && disk[511] == 0xAA, "MBR boot signature missing");

    CHECK(e[0] == 0x00, "protective entry is marked active (%02x)", e[0]);
    CHECK(e[1] == 0x00 && e[2] == 0x02 && e[3] == 0x00, "protective entry starting CHS %02x%02x%02x", e[1], e[2], e[3]);
    CHECK(e[4] == 0xEE, "MBR entry 1 type %02x, expected protective 0xEE", e[4]);
    CHECK(e[5] == 0xFF && e[6] == 0xFF && e[7] == 0xFF, "protective entry ending CHS");
    CHECK(Get32(e + 8) == 1, "protective entry starts at %u", Get32(e + 8));
    CHECK(Get32(e + 12) == TOTAL_SECTORS - 1, "protective entry size %u", Get32(e + 12));

    if (boot_code)
    {
        CHECK(e[16] == 0x80, "placeholder entry not active (%02x)", e[16]);
        CHECK(e[20] == 0x00, "placeholder entry type %02x, expected unused 0x00", e[20]);
        CHECK(e[17] == 0 && e[18] == 1 && e[19] == 0 && e[21] == 0 && e[22] == 1 && e[23] == 0, "placeholder entry CHS");
        CHECK(Get32(e + 24) == 0 && Get32(e + 28) == 1, "placeholder entry LBA %u size %u", Get32(e + 24), Get32(e + 28));
    }
    else
    {
        CHECK(IsZero(e + 16, 16), "EFI-only image has a second MBR entry");
    }
    CHECK(IsZero(e + 32, 32), "MBR entries 3 and 4 are not empty");
    for (i = 0; i < 4; i++)
        CHECK(e[16 * i + 4] != 0xEF, "MBR entry %u is an EFI system partition", i + 1);
    if (size != TOTAL_SECTORS * SECTOR)
        return;

    CheckHeader(disk, 1, TOTAL_SECTORS - 1, 2);
    CheckHeader(disk, TOTAL_SECTORS - 1, 1, TOTAL_SECTORS - 1 - ENTRY_SECTORS);
    CHECK(memcmp(entries, disk + (TOTAL_SECTORS - 1 - ENTRY_SECTORS) * SECTOR, ENTRY_COUNT * ENTRY_SIZE) == 0,
          "backup entry array differs from the primary one");

    CHECK(memcmp(entries, EspType, 16) == 0, "GPT entry 1 is not an EFI system partition");
    CHECK(!IsZero(entries + 16, 16), "GPT entry 1 has no unique GUID");
    CHECK(Get64(entries + 32) == ESP_START, "ESP starts at %llu", (unsigned long long)Get64(entries + 32));
    CHECK(Get64(entries + 40) == ESP_START + ESP_SECTORS - 1, "ESP ends at %llu", (unsigned long long)Get64(entries + 40));
    CHECK(Get64(entries + 48) == 0, "ESP attributes %llx", (unsigned long long)Get64(entries + 48));
    for (i = 0; i < sizeof(name) - 1; i++)
        CHECK(entries[56 + 2 * i] == (uint8_t)name[i] && entries[57 + 2 * i] == 0, "ESP name differs at %u", i);
    CHECK(IsZero(entries + ENTRY_SIZE, (ENTRY_COUNT - 1) * ENTRY_SIZE), "GPT has more than one entry");

    CHECK(memcmp(disk + 34 * SECTOR, iso + 34 * SECTOR, (VOLUME_SECTORS - 34) * SECTOR) == 0, "ISO 9660 volume changed");
    CHECK(IsZero(disk + VOLUME_SECTORS * SECTOR, (TAIL_SECTORS - ENTRY_SECTORS - 1) * SECTOR), "padding after the volume not zero");
}

static uint8_t* MakeImage(const char* path, uint8_t* iso, size_t extra, uint8_t poke_sector)
{
    size_t size = VOLUME_SECTORS * SECTOR + extra;
    uint8_t* data = calloc(1, size);

    if (!data)
        return NULL;
    BuildIso(iso);
    memcpy(data, iso, VOLUME_SECTORS * SECTOR);
    if (poke_sector)
        data[poke_sector * SECTOR + 17] = 0x11;
    if (!WriteFile(path, data, size))
    {
        free(data);
        return NULL;
    }
    return data;
}

static void TestRejected(const char* label, size_t extra, uint8_t poke_sector, const char* mbr, uint8_t* iso)
{
    char path[1024];
    uint8_t *before, *after;
    size_t size = 0;

    Path(path, sizeof(path), "rejected.iso");
    before = MakeImage(path, iso, extra, poke_sector);
    CHECK(before != NULL, "%s: cannot write the image", label);
    if (!before)
        return;
    CHECK(RunTool(path, mbr) != 0, "%s: mkdiskimg accepted the image", label);
    after = ReadFile(path, &size);
    CHECK(after && size == VOLUME_SECTORS * SECTOR + extra && memcmp(before, after, size) == 0, "%s: rejected image was modified", label);
    free(before);
    free(after);
}

int main(int argc, char** argv)
{
    char iso_path[1024], mbr_path[1024];
    uint8_t* iso = malloc(VOLUME_SECTORS * SECTOR);
    uint8_t boot_code[BOOT_CODE_SIZE];
    uint8_t *first = NULL, *second = NULL, *seed;
    size_t first_size = 0, second_size = 0;
    unsigned int i;

    if (argc != 3 || !iso)
    {
        fprintf(stderr, "usage: %s <mkdiskimg> <work directory>\n", argv[0]);
        return 2;
    }
    Tool = argv[1];
    WorkDir = argv[2];

    for (i = 0; i < BOOT_CODE_SIZE; i++)
        boot_code[i] = (uint8_t)(0xA5 ^ i);
    Path(mbr_path, sizeof(mbr_path), "isombr.bin");
    Path(iso_path, sizeof(iso_path), "hybrid.iso");
    CHECK(WriteFile(mbr_path, boot_code, sizeof(boot_code)), "cannot write the boot code");

    seed = MakeImage(iso_path, iso, 0, 0);
    CHECK(seed && IsZero(seed + SECTOR, 33 * SECTOR), "cannot write the BIOS and EFI image");
    free(seed);
    CHECK(RunTool(iso_path, mbr_path) == 0, "BIOS and EFI image rejected");
    first = ReadFile(iso_path, &first_size);
    CHECK(first != NULL, "cannot read the BIOS and EFI result");
    if (first)
        CheckHybrid(first, first_size, iso, boot_code);

    CHECK(RunTool(iso_path, mbr_path) == 0, "second run rejected its own output");
    second = ReadFile(iso_path, &second_size);
    CHECK(first && second && first_size == second_size && memcmp(first, second, first_size) == 0,
          "second run changed the image");
    free(first);
    free(second);

    seed = MakeImage(iso_path, iso, 0, 0);
    free(seed);
    CHECK(RunTool(iso_path, NULL) == 0, "EFI-only image rejected");
    first = ReadFile(iso_path, &first_size);
    CHECK(first != NULL, "cannot read the EFI-only result");
    if (first)
        CheckHybrid(first, first_size, iso, NULL);
    free(first);

    TestRejected("system area in use", 0, 5, mbr_path, iso);
    TestRejected("data behind the volume", (TAIL_SECTORS + 4) * SECTOR, 0, mbr_path, iso);

    free(iso);
    if (Failures)
    {
        fprintf(stderr, "%d check(s) failed\n", Failures);
        return 1;
    }
    printf("mkdiskimg hybrid CD layout: all checks passed\n");
    return 0;
}
