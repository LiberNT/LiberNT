/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the file system type fatten writes into the boot sector of a new FAT volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define SECTOR 512u

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

static uint32_t Get16(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t Get32(const uint8_t* p) { return Get16(p) | Get16(p + 2) << 16; }

static int IsZero(const uint8_t* p, size_t n)
{
    while (n--)
    {
        if (*p++)
            return 0;
    }
    return 1;
}

static void Path(char* out, size_t size, const char* name)
{
    snprintf(out, size, "%s/%s", WorkDir, name);
}

static int Run(const char* arguments)
{
    char command[4096];
    int status;

    snprintf(command, sizeof(command), "'%s' %s", Tool, arguments);
    status = system(command);
    return (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}

static int ReadSector(const char* path, uint8_t* sector)
{
    FILE* f = fopen(path, "rb");
    size_t n;

    if (!f)
        return 0;
    n = fread(sector, 1, SECTOR, f);
    fclose(f);
    return n == SECTOR;
}

static int WriteFile(const char* path, const uint8_t* data, size_t size)
{
    FILE* f = fopen(path, "wb");
    size_t n;

    if (!f)
        return 0;
    n = fwrite(data, 1, size, f);
    return fclose(f) == 0 && n == size;
}

static uint32_t ClusterCount(const uint8_t* bs)
{
    uint32_t bytes = Get16(bs + 11), per_cluster = bs[13], reserved = Get16(bs + 14), fats = bs[16];
    uint32_t total = Get16(bs + 19) ? Get16(bs + 19) : Get32(bs + 32);
    uint32_t fat_size = Get16(bs + 22) ? Get16(bs + 22) : Get32(bs + 36);
    uint32_t meta;

    if (!bytes || !per_cluster)
        return 0;
    meta = reserved + fats * fat_size + (Get16(bs + 17) * 32 + bytes - 1) / bytes;
    if (total <= meta)
        return 0;
    return (total - meta) / per_cluster;
}

static void CheckVolume(const char* label, const char* image, const char* type, const uint8_t* boot)
{
    uint8_t bs[SECTOR];
    uint32_t clusters;

    if (!ReadSector(image, bs))
    {
        CHECK(0, "%s: cannot read the boot sector", label);
        return;
    }

    clusters = ClusterCount(bs);
    CHECK(bs[510] == 0x55 && bs[511] == 0xAA, "%s: no boot signature", label);
    CHECK(memcmp(bs + 3, "mkfs.fat", 8) == 0, "%s: OEM name '%.8s'", label, (const char*)bs + 3);

    if (strcmp(type, "FAT32   ") == 0)
    {
        CHECK(clusters >= 65525, "%s: %u clusters is not FAT32", label, clusters);
        CHECK(memcmp(bs + 82, type, 8) == 0, "%s: FAT32 type '%.8s'", label, (const char*)bs + 82);
        CHECK(IsZero(bs + 52, 12), "%s: the FAT32 reserved BPB bytes were written", label);
        CHECK(bs[66] == 0x29, "%s: no extended boot signature", label);
    }
    else
    {
        CHECK(Get16(bs + 22) != 0, "%s: no FAT12/16 FAT size", label);
        CHECK(strcmp(type, clusters < 4085 ? "FAT12   " : "FAT16   ") == 0,
              "%s: %u clusters does not match the expected %s", label, clusters, type);
        CHECK(memcmp(bs + 54, type, 8) == 0, "%s: type '%.8s', expected '%s'", label, (const char*)bs + 54, type);
        CHECK(bs[38] == 0x29, "%s: no extended boot signature", label);
    }

    if (boot)
        CHECK(memcmp(bs + 62, boot + 62, 510 - 62) == 0, "%s: boot code not taken from the boot file", label);
}

static void TestFormat(const char* label, const char* name, const char* format, const char* type)
{
    char image[1024], arguments[2048];

    Path(image, sizeof(image), name);
    remove(image);
    snprintf(arguments, sizeof(arguments), "'%s' -format %s", image, format);
    CHECK(Run(arguments) == 0, "%s: fatten failed", label);
    CheckVolume(label, image, type, NULL);
}

int main(int argc, char** argv)
{
    char image[1024], boot_path[1024], arguments[4096];
    uint8_t boot[SECTOR];
    unsigned i;

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <fatten> <work directory>\n", argv[0]);
        return 2;
    }
    Tool = argv[1];
    WorkDir = argv[2];

    TestFormat("EFI boot image", "efisys.bin", "16384 EFIBOOT", "FAT12   ");
    TestFormat("floppy", "floppy.img", "2880 FLOPPY", "FAT12   ");
    TestFormat("FAT16 volume", "fat16.img", "131072 DATA", "FAT16   ");
    TestFormat("FAT32 volume", "fat32.img", "140000 fat32 DATA", "FAT32   ");

    for (i = 0; i < SECTOR; i++)
        boot[i] = (uint8_t)(0xC3 ^ i);
    memcpy(boot + 54, "BOOTCODE", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;
    Path(boot_path, sizeof(boot_path), "fat.bin");
    CHECK(WriteFile(boot_path, boot, sizeof(boot)), "cannot write the boot file");
    Path(image, sizeof(image), "efisys-boot.bin");
    remove(image);
    snprintf(arguments, sizeof(arguments), "'%s' -format 16384 EFIBOOT -boot '%s'", image, boot_path);
    CHECK(Run(arguments) == 0, "EFI boot image with boot code: fatten failed");
    CheckVolume("EFI boot image with boot code", image, "FAT12   ", boot);

    if (Failures)
    {
        fprintf(stderr, "%d check(s) failed\n", Failures);
        return 1;
    }
    printf("fatten file system type: all checks passed\n");
    return 0;
}
