/*
 * PROJECT:     LiberNT Disk Image Creator
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Assembles a bootable MBR disk image from partition images
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.img@outlook.com>
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#define SECTOR_SIZE 512
#define MBR_BOOT_CODE_SIZE 440
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_SIGNATURE_OFFSET 510
#define MAX_MBR_PARTITIONS 4
#define MAX_RAW_BLOBS 8
#define GPT_ENTRY_COUNT 128
#define GPT_ENTRY_SIZE 128
#define GPT_ENTRY_SECTORS 32
#define GPT_HEADER_SIZE 92
#define GPT_FIRST_USABLE_SECTOR 34
#define GPT_NAME_LENGTH 36

/* FAT/FAT32 BPB field offsets (from start of VBR) */
#define FAT_BPB_BYTES_PER_SECTOR_OFFSET 11
#define FAT_BPB_RESERVED_SECTORS_OFFSET 14
#define FAT_BPB_NUMBER_OF_FATS_OFFSET 16
#define FAT32_MEDIA_DESCRIPTOR_OFFSET 21
#define FAT_BPB_SECTORS_PER_FAT16_OFFSET 22
#define FAT32_HIDDEN_SECTORS_OFFSET 28
#define FAT32_SECTORS_PER_FAT_OFFSET 36
#define FAT32_BACKUP_BOOT_SECTOR_OFFSET 50
#define FAT16_BOOT_DRIVE_OFFSET 36
#define FAT32_BOOT_DRIVE_OFFSET 64

/* NTFS BPB fields */
#define NTFS_NAME_OFFSET 3
#define NTFS_NAME_LENGTH 8
#define NTFS_SECTORS_PER_TRACK_OFFSET 24
#define NTFS_HEADS_OFFSET 26
#define NTFS_HIDDEN_SECTORS_OFFSET 28

/* exFAT boot-region fields */
#define EXFAT_NAME_OFFSET 3
#define EXFAT_NAME_LENGTH 8
#define EXFAT_PARTITION_OFFSET 64
#define EXFAT_SECTOR_SHIFT_OFFSET 108
#define EXFAT_BOOT_CHECKSUM_SECTOR 11
#define EXFAT_BACKUP_BOOT_SECTOR 12
#define EXFAT_BOOT_REGION_SECTORS 24

/* Fixed disk media descriptor (0xF8) vs removable (0xF0) */
#define MEDIA_DESCRIPTOR_FIXED 0xF8
/* 0xFF tells boot code to use BIOS-provided drive number in DL */
#define BOOT_DRIVE_AUTO 0xFF

/* Conventional translation geometry for LBA-assisted partition tables */
#define CHS_SECTORS_PER_TRACK 63
#define CHS_HEADS 255

/* Default values */
#define DEFAULT_START_SECTOR 2048
#define DEFAULT_PARTITION_TYPE 0x0C /* FAT32 LBA */

/* VHD layout constants */
#define VHD_FOOTER_SIZE 512
#define VHD_DYNAMIC_HEADER_SIZE 1024
#define VHD_BLOCK_SIZE (2 * 1024 * 1024)
#define VHD_DISK_TYPE_DYNAMIC 3
#define VHD_TIMESTAMP_BASE 946684800UL /* 2000-01-01 00:00:00 UTC */

typedef enum _OUTPUT_FORMAT
{
    OUTPUT_FORMAT_RAW = 0,
    OUTPUT_FORMAT_VHD
} OUTPUT_FORMAT;

typedef struct _PARTITION_INPUT
{
    const char* Path;
    int Blank;
    int Data;
    unsigned int ReservedSectors;
    const char* Name;
    unsigned int StartSector;
    unsigned char Type;
    unsigned char TypeGuid[16];
    int HasTypeGuid;
    int Bootable;
    FILE* File;
    long Size;
    unsigned int SectorCount;
    unsigned char BootSector[SECTOR_SIZE];
} PARTITION_INPUT;

typedef struct _RAW_BLOB
{
    const char* Path;
    unsigned int StartSector;
    unsigned char* Data;
    long Size;
} RAW_BLOB;

#pragma pack(push, 1)
typedef struct _PARTITION_ENTRY
{
    unsigned char Status;       /* 0x80 = active */
    unsigned char CHSFirst[3];  /* CHS of first sector */
    unsigned char Type;         /* Partition type */
    unsigned char CHSLast[3];   /* CHS of last sector */
    unsigned int  LBAStart;     /* LBA of first sector */
    unsigned int  LBASize;      /* Number of sectors */
} PARTITION_ENTRY;
#pragma pack(pop)

static void print_usage(const char* name)
{
    printf("Usage: %s -o <output> [-gpt] [-mbr <mbr.bin>] {-partition <part.img>|-data <file>|-blank <sectors>} [-start <sector>] [-type <hex>] [-size <sectors>] [-name <label>] [-guid <type>] [-bootable] ... [-raw <file> -at <sector>] ... [-format <raw|vhd>] [-vhd]\n\n", name);
    printf("  -gpt                Write a GUID partition table behind a protective MBR\n");
    printf("  -o <output>         Output image file\n");
    printf("  -mbr <mbr.bin>      MBR boot code binary (first 440 bytes used; shorter files are zero-padded).\n");
    printf("                      Optional; omit on UEFI-only platforms to leave the boot code area zeroed.\n");
    printf("  -partition <img>    Raw FAT, exFAT, or NTFS partition image (up to four)\n");
    printf("  -data <file>        Partition holding <file> verbatim, zero-padded to whole sectors\n");
    printf("  -blank <sectors>    Zero-filled partition with no filesystem (up to four)\n");
    printf("  -start <sector>     Start sector for the preceding partition (first defaults to %d)\n", DEFAULT_START_SECTOR);
    printf("  -type <hex>         Type ID for the preceding partition (first defaults to 0x%02X)\n", DEFAULT_PARTITION_TYPE);
    printf("  -size <sectors>     Sectors reserved for the preceding -data partition\n");
    printf("  -name <label>       GPT name of the preceding partition\n");
    printf("  -guid <type>        GPT type GUID of the preceding partition (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)\n");
    printf("  -bootable           Set the GPT legacy BIOS bootable attribute of the preceding partition\n");
    printf("  -raw <file>         Data written outside the partitions at the sector given by -at\n");
    printf("  -at <sector>        Start sector of the preceding -raw data (not sector 0)\n");
    printf("  -format <raw|vhd>   Output container format (default: raw)\n");
    printf("  -vhd                Shorthand for -format vhd\n");
}

static int copy_file_data(FILE* dst, FILE* src, long size)
{
    unsigned char buf[32768];
    long remaining = size;

    while (remaining > 0)
    {
        size_t chunk = remaining < (long)sizeof(buf) ? (size_t)remaining : sizeof(buf);
        size_t rd = fread(buf, 1, chunk, src);
        if (rd == 0)
            return -1;
        if (fwrite(buf, 1, rd, dst) != rd)
            return -1;
        remaining -= (long)rd;
    }
    return remaining == 0 ? 0 : -1;
}

static int write_zero_bytes(FILE* file, unsigned long long size)
{
    unsigned char zero[32768];

    memset(zero, 0, sizeof(zero));
    while (size > 0)
    {
        size_t chunk = size < sizeof(zero) ? (size_t)size : sizeof(zero);
        if (fwrite(zero, 1, chunk, file) != chunk)
            return -1;
        size -= chunk;
    }
    return 0;
}

static int initialize_zeroed_file(FILE* file, long size)
{
    /* Extending a new file with one trailing zero preserves zero-filled byte
     * semantics while allowing filesystems that support holes to keep it sparse. */
    if (size <= 0 || fseek(file, size - 1, SEEK_SET) != 0 || fputc(0, file) == EOF)
        return -1;
    return 0;
}

static unsigned int crc32_update(unsigned int crc, const unsigned char* data, size_t size)
{
    size_t i;
    int bit;

    crc = ~crc;
    for (i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1)));
    }
    return ~crc;
}

static int read_whole_file(const char* path, unsigned char** data, long* size)
{
    FILE* file = fopen(path, "rb");
    long length;

    *data = NULL;
    *size = 0;
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return -1;
    }
    *data = malloc(length > 0 ? (size_t)length : 1);
    if (!*data || (length > 0 && fread(*data, 1, (size_t)length, file) != (size_t)length))
    {
        free(*data);
        *data = NULL;
        fclose(file);
        return -1;
    }
    fclose(file);
    *size = length;
    return 0;
}

static unsigned short read_le16(const unsigned char* ptr)
{
    return (unsigned short)((unsigned short)ptr[0] |
                            ((unsigned short)ptr[1] << 8));
}

static unsigned int read_le32(const unsigned char* ptr)
{
    return (unsigned int)((unsigned int)ptr[0] |
                          ((unsigned int)ptr[1] << 8) |
                          ((unsigned int)ptr[2] << 16) |
                          ((unsigned int)ptr[3] << 24));
}

static void write_chs(unsigned int lba, unsigned char chs[3])
{
    unsigned int cylinders, heads, sectors;

    sectors = (lba % CHS_SECTORS_PER_TRACK) + 1;
    lba /= CHS_SECTORS_PER_TRACK;
    heads = lba % CHS_HEADS;
    cylinders = lba / CHS_HEADS;

    if (cylinders > 1023)
    {
        cylinders = 1023;
        heads = CHS_HEADS - 1;
        sectors = CHS_SECTORS_PER_TRACK;
    }

    chs[0] = (unsigned char)heads;
    chs[1] = (unsigned char)((sectors & 0x3F) | ((cylinders >> 2) & 0xC0));
    chs[2] = (unsigned char)(cylinders & 0xFF);
}

static int write_byte_at(FILE* file, long offset, unsigned char value)
{
    if (fseek(file, offset, SEEK_SET) != 0)
        return -1;
    return fwrite(&value, 1, 1, file) == 1 ? 0 : -1;
}

static int write_le16_at(FILE* file, long offset, unsigned short value)
{
    unsigned char data[2];

    data[0] = (unsigned char)(value & 0xFF);
    data[1] = (unsigned char)((value >> 8) & 0xFF);

    if (fseek(file, offset, SEEK_SET) != 0)
        return -1;
    return fwrite(data, sizeof(data), 1, file) == 1 ? 0 : -1;
}

static int write_le32_at(FILE* file, long offset, unsigned int value)
{
    unsigned char data[4];

    data[0] = (unsigned char)(value & 0xFF);
    data[1] = (unsigned char)((value >> 8) & 0xFF);
    data[2] = (unsigned char)((value >> 16) & 0xFF);
    data[3] = (unsigned char)((value >> 24) & 0xFF);

    if (fseek(file, offset, SEEK_SET) != 0)
        return -1;
    return fwrite(data, sizeof(data), 1, file) == 1 ? 0 : -1;
}

static int write_data_at(FILE* file, long offset, const void* data, size_t size)
{
    if (fseek(file, offset, SEEK_SET) != 0)
        return -1;
    return fwrite(data, 1, size, file) == size ? 0 : -1;
}

static void write_le32(unsigned char* ptr, unsigned int value)
{
    ptr[0] = (unsigned char)(value & 0xFF);
    ptr[1] = (unsigned char)((value >> 8) & 0xFF);
    ptr[2] = (unsigned char)((value >> 16) & 0xFF);
    ptr[3] = (unsigned char)((value >> 24) & 0xFF);
}

static void write_le64(unsigned char* ptr, unsigned long long value)
{
    write_le32(ptr, (unsigned int)value);
    write_le32(ptr + 4, (unsigned int)(value >> 32));
}

static const unsigned char gpt_type_esp[16] =
    {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
static const unsigned char gpt_type_basic_data[16] =
    {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
static const unsigned char gpt_type_linux_data[16] =
    {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

static void make_gpt_guid(unsigned char guid[16], unsigned int index, const PARTITION_INPUT* partition)
{
    unsigned char seed[16];
    unsigned int value = 0x4C624E54U ^ index;
    int i;

    memset(seed, 0, sizeof(seed));
    write_le32(seed, index);
    if (partition)
    {
        write_le32(seed + 4, partition->StartSector);
        write_le32(seed + 8, partition->SectorCount);
        write_le32(seed + 12, partition->Type);
    }
    for (i = 0; i < 4; i++)
    {
        value = crc32_update(value, seed, sizeof(seed));
        if (partition && partition->Name)
            value = crc32_update(value, (const unsigned char*)partition->Name, strlen(partition->Name));
        write_le32(guid + 4 * i, value);
    }
    guid[7] = (unsigned char)((guid[7] & 0x0F) | 0x40);
    guid[8] = (unsigned char)((guid[8] & 0x3F) | 0x80);
}

static void build_gpt_entries(unsigned char* entries, const PARTITION_INPUT* partitions, unsigned int partition_count)
{
    unsigned int index;

    memset(entries, 0, GPT_ENTRY_COUNT * GPT_ENTRY_SIZE);
    for (index = 0; index < partition_count; index++)
    {
        const PARTITION_INPUT* partition = &partitions[index];
        unsigned char* entry = entries + index * GPT_ENTRY_SIZE;
        const unsigned char* type = gpt_type_linux_data;
        const char* name = partition->Name ? partition->Name : "";
        size_t i;

        if (partition->HasTypeGuid)
            type = partition->TypeGuid;
        else if (partition->Type == 0xEF)
            type = gpt_type_esp;
        else if (partition->Type == 0x07 || partition->Type == 0x0B || partition->Type == 0x0C)
            type = gpt_type_basic_data;
        memcpy(entry, type, 16);
        make_gpt_guid(entry + 16, index + 1, partition);
        write_le64(entry + 32, partition->StartSector);
        write_le64(entry + 40, (unsigned long long)partition->StartSector + partition->SectorCount - 1);
        if (partition->Bootable)
            write_le64(entry + 48, 1ULL << 2);
        for (i = 0; name[i] != 0 && i < GPT_NAME_LENGTH; i++)
            entry[56 + 2 * i] = (unsigned char)name[i];
    }
}

static void build_gpt_header(unsigned char sector[SECTOR_SIZE],
                             unsigned long long current_lba,
                             unsigned long long other_lba,
                             unsigned long long entries_lba,
                             unsigned long long last_lba,
                             const unsigned char* entries)
{
    memset(sector, 0, SECTOR_SIZE);
    memcpy(sector, "EFI PART", 8);
    write_le32(sector + 8, 0x00010000U);
    write_le32(sector + 12, GPT_HEADER_SIZE);
    write_le64(sector + 24, current_lba);
    write_le64(sector + 32, other_lba);
    write_le64(sector + 40, GPT_FIRST_USABLE_SECTOR);
    write_le64(sector + 48, last_lba - GPT_ENTRY_SECTORS - 1);
    make_gpt_guid(sector + 56, 0, NULL);
    write_le64(sector + 72, entries_lba);
    write_le32(sector + 80, GPT_ENTRY_COUNT);
    write_le32(sector + 84, GPT_ENTRY_SIZE);
    write_le32(sector + 88, crc32_update(0, entries, GPT_ENTRY_COUNT * GPT_ENTRY_SIZE));
    write_le32(sector + 16, crc32_update(0, sector, GPT_HEADER_SIZE));
}

static int patch_fat_partition(FILE* output, const PARTITION_INPUT* partition)
{
    const unsigned char* vbr = partition->BootSector;
    static const unsigned char fat32_reserved_entries[8] = {0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F};
    unsigned short bytes_per_sector;
    unsigned short reserved_sectors;
    unsigned short backup_boot_sector = 0;
    unsigned char number_of_fats;
    unsigned int sectors_per_fat;
    unsigned int boot_drive_offset;
    long vbr_offset;
    long fat_offset;
    long fat_size;
    unsigned int fat_index;
    int is_fat32;

    bytes_per_sector = read_le16(vbr + FAT_BPB_BYTES_PER_SECTOR_OFFSET);
    reserved_sectors = read_le16(vbr + FAT_BPB_RESERVED_SECTORS_OFFSET);
    number_of_fats = vbr[FAT_BPB_NUMBER_OF_FATS_OFFSET];
    sectors_per_fat = read_le16(vbr + FAT_BPB_SECTORS_PER_FAT16_OFFSET);
    is_fat32 = (sectors_per_fat == 0);
    if (is_fat32)
    {
        sectors_per_fat = read_le32(vbr + FAT32_SECTORS_PER_FAT_OFFSET);
        backup_boot_sector = read_le16(vbr + FAT32_BACKUP_BOOT_SECTOR_OFFSET);
        boot_drive_offset = FAT32_BOOT_DRIVE_OFFSET;
    }
    else
    {
        boot_drive_offset = FAT16_BOOT_DRIVE_OFFSET;
    }

    if (bytes_per_sector != SECTOR_SIZE || reserved_sectors == 0 ||
        number_of_fats == 0 || sectors_per_fat == 0)
    {
        fprintf(stderr, "Error: Partition image '%s' has an invalid FAT BPB.\n",
                partition->Path);
        return -1;
    }

    vbr_offset = (long)partition->StartSector * SECTOR_SIZE;

    if (write_le32_at(output,
                      vbr_offset + FAT32_HIDDEN_SECTORS_OFFSET,
                      partition->StartSector) != 0)
    {
        fprintf(stderr, "Error: Cannot patch HiddenSectors in '%s'.\n", partition->Path);
        return -1;
    }

    if (is_fat32 && backup_boot_sector != 0 && backup_boot_sector != 0xFFFF)
    {
        long backup_vbr_offset = vbr_offset + (long)backup_boot_sector * bytes_per_sector;

        if (backup_vbr_offset + FAT32_HIDDEN_SECTORS_OFFSET + 4 > vbr_offset + partition->Size ||
            write_le32_at(output,
                          backup_vbr_offset + FAT32_HIDDEN_SECTORS_OFFSET,
                          partition->StartSector) != 0)
        {
            fprintf(stderr, "Error: Cannot patch the backup FAT BPB in '%s'.\n",
                    partition->Path);
            return -1;
        }
    }

    if (write_byte_at(output,
                      vbr_offset + FAT32_MEDIA_DESCRIPTOR_OFFSET,
                      MEDIA_DESCRIPTOR_FIXED) != 0)
    {
        fprintf(stderr, "Error: Cannot patch the FAT media descriptor in '%s'.\n",
                partition->Path);
        return -1;
    }

    if (is_fat32 && backup_boot_sector != 0 && backup_boot_sector != 0xFFFF)
    {
        long backup_vbr_offset = vbr_offset + (long)backup_boot_sector * bytes_per_sector;

        if (write_byte_at(output,
                          backup_vbr_offset + FAT32_MEDIA_DESCRIPTOR_OFFSET,
                          MEDIA_DESCRIPTOR_FIXED) != 0)
        {
            fprintf(stderr, "Error: Cannot patch the backup FAT media descriptor in '%s'.\n",
                    partition->Path);
            return -1;
        }
    }

    fat_offset = vbr_offset + (long)reserved_sectors * bytes_per_sector;
    fat_size = (long)sectors_per_fat * bytes_per_sector;
    for (fat_index = 0; fat_index < number_of_fats; fat_index++)
    {
        long current_fat_offset = fat_offset + (long)fat_index * fat_size;

        if (current_fat_offset + 1 > vbr_offset + partition->Size ||
            write_byte_at(output, current_fat_offset, MEDIA_DESCRIPTOR_FIXED) != 0)
        {
            fprintf(stderr, "Error: Cannot patch FAT%u in '%s'.\n",
                    fat_index + 1,
                    partition->Path);
            return -1;
        }

        if (is_fat32 &&
            (fat_size < (long)sizeof(fat32_reserved_entries) ||
             write_data_at(output,
                           current_fat_offset,
                           fat32_reserved_entries,
                           sizeof(fat32_reserved_entries)) != 0))
        {
            fprintf(stderr, "Error: Cannot patch FAT%u reserved entries in '%s'.\n",
                    fat_index + 1,
                    partition->Path);
            return -1;
        }
    }

    if (write_byte_at(output,
                      vbr_offset + boot_drive_offset,
                      BOOT_DRIVE_AUTO) != 0)
    {
        fprintf(stderr, "Error: Cannot patch BootDrive in '%s'.\n", partition->Path);
        return -1;
    }

    if (is_fat32 && backup_boot_sector != 0 && backup_boot_sector != 0xFFFF)
    {
        long backup_vbr_offset = vbr_offset + (long)backup_boot_sector * bytes_per_sector;

        if (write_byte_at(output,
                          backup_vbr_offset + boot_drive_offset,
                          BOOT_DRIVE_AUTO) != 0)
        {
            fprintf(stderr, "Error: Cannot patch BootDrive in the backup BPB of '%s'.\n",
                    partition->Path);
            return -1;
        }
    }

    return 0;
}

static unsigned int exfat_boot_checksum(const unsigned char* boot_region,
                                        unsigned int bytes_per_sector)
{
    unsigned int checksum = 0;
    unsigned int index;

    for (index = 0; index < EXFAT_BOOT_CHECKSUM_SECTOR * bytes_per_sector; index++)
    {
        if (index == 106 || index == 107 || index == 112)
            continue;
        checksum = ((checksum & 1) ? 0x80000000U : 0) +
                   (checksum >> 1) + boot_region[index];
    }

    return checksum;
}

static int patch_exfat_partition(FILE* output, const PARTITION_INPUT* partition)
{
    unsigned char* boot_region;
    unsigned int bytes_per_sector;
    unsigned int checksum;
    unsigned int index;
    unsigned int region_size;
    unsigned long long partition_offset;
    long output_offset;

    if (partition->BootSector[EXFAT_SECTOR_SHIFT_OFFSET] < 9 ||
        partition->BootSector[EXFAT_SECTOR_SHIFT_OFFSET] > 12)
    {
        fprintf(stderr, "Error: Partition image '%s' has an invalid exFAT sector size.\n",
                partition->Path);
        return -1;
    }

    bytes_per_sector = 1U << partition->BootSector[EXFAT_SECTOR_SHIFT_OFFSET];
    if (((unsigned long long)partition->StartSector * SECTOR_SIZE) % bytes_per_sector != 0 ||
        partition->Size % bytes_per_sector != 0)
    {
        fprintf(stderr, "Error: exFAT partition '%s' is not aligned to its logical sector size.\n",
                partition->Path);
        return -1;
    }

    region_size = EXFAT_BOOT_REGION_SECTORS * bytes_per_sector;
    if ((unsigned long)partition->Size < region_size)
    {
        fprintf(stderr, "Error: exFAT partition image '%s' is too small.\n", partition->Path);
        return -1;
    }

    boot_region = (unsigned char*)malloc(region_size);
    if (!boot_region)
    {
        fprintf(stderr, "Error: Out of memory while patching '%s'.\n", partition->Path);
        return -1;
    }

    output_offset = (long)partition->StartSector * SECTOR_SIZE;
    if (fseek(output, output_offset, SEEK_SET) != 0 ||
        fread(boot_region, 1, region_size, output) != region_size)
    {
        fprintf(stderr, "Error: Cannot read the exFAT boot regions from '%s'.\n",
                partition->Path);
        free(boot_region);
        return -1;
    }

    if (memcmp(boot_region + EXFAT_NAME_OFFSET, "EXFAT   ", EXFAT_NAME_LENGTH) != 0 ||
        memcmp(boot_region + EXFAT_BACKUP_BOOT_SECTOR * bytes_per_sector + EXFAT_NAME_OFFSET,
               "EXFAT   ",
               EXFAT_NAME_LENGTH) != 0)
    {
        fprintf(stderr, "Error: Partition image '%s' has an invalid exFAT boot region.\n",
                partition->Path);
        free(boot_region);
        return -1;
    }

    partition_offset = (unsigned long long)partition->StartSector * SECTOR_SIZE /
                       bytes_per_sector;
    write_le64(boot_region + EXFAT_PARTITION_OFFSET, partition_offset);
    write_le64(boot_region + EXFAT_BACKUP_BOOT_SECTOR * bytes_per_sector +
               EXFAT_PARTITION_OFFSET,
               partition_offset);

    checksum = exfat_boot_checksum(boot_region, bytes_per_sector);
    for (index = 0; index < bytes_per_sector; index += sizeof(checksum))
        write_le32(boot_region + EXFAT_BOOT_CHECKSUM_SECTOR * bytes_per_sector + index,
                   checksum);

    checksum = exfat_boot_checksum(boot_region + EXFAT_BACKUP_BOOT_SECTOR * bytes_per_sector,
                                   bytes_per_sector);
    for (index = 0; index < bytes_per_sector; index += sizeof(checksum))
        write_le32(boot_region + (EXFAT_BACKUP_BOOT_SECTOR + EXFAT_BOOT_CHECKSUM_SECTOR) *
                   bytes_per_sector + index,
                   checksum);

    if (write_data_at(output, output_offset, boot_region, region_size) != 0)
    {
        fprintf(stderr, "Error: Cannot write the exFAT boot regions for '%s'.\n",
                partition->Path);
        free(boot_region);
        return -1;
    }

    free(boot_region);
    return 0;
}

static int patch_ntfs_partition(FILE* output, const PARTITION_INPUT* partition)
{
    long backup_offset;
    long output_offset;

    if (read_le16(partition->BootSector + FAT_BPB_BYTES_PER_SECTOR_OFFSET) !=
            SECTOR_SIZE ||
        partition->BootSector[510] != 0x55 ||
        partition->BootSector[511] != 0xAA)
    {
        fprintf(stderr, "Error: Partition image '%s' has an invalid NTFS BPB.\n",
                partition->Path);
        return -1;
    }

    output_offset = (long)partition->StartSector * SECTOR_SIZE;
    backup_offset = output_offset + partition->Size - SECTOR_SIZE;
    if (write_le16_at(output,
                      output_offset + NTFS_SECTORS_PER_TRACK_OFFSET,
                      CHS_SECTORS_PER_TRACK) != 0 ||
        write_le16_at(output,
                      output_offset + NTFS_HEADS_OFFSET,
                      CHS_HEADS) != 0 ||
        write_le32_at(output,
                      output_offset + NTFS_HIDDEN_SECTORS_OFFSET,
                      partition->StartSector) != 0 ||
        write_le16_at(output,
                      backup_offset + NTFS_SECTORS_PER_TRACK_OFFSET,
                      CHS_SECTORS_PER_TRACK) != 0 ||
        write_le16_at(output,
                      backup_offset + NTFS_HEADS_OFFSET,
                      CHS_HEADS) != 0 ||
        write_le32_at(output,
                      backup_offset + NTFS_HIDDEN_SECTORS_OFFSET,
                      partition->StartSector) != 0)
    {
        fprintf(stderr, "Error: Cannot patch the NTFS BPB in '%s'.\n",
                partition->Path);
        return -1;
    }

    return 0;
}

static int patch_partition(FILE* output, const PARTITION_INPUT* partition)
{
    if (memcmp(partition->BootSector + EXFAT_NAME_OFFSET,
               "EXFAT   ",
               EXFAT_NAME_LENGTH) == 0)
    {
        return patch_exfat_partition(output, partition);
    }
    if (memcmp(partition->BootSector + NTFS_NAME_OFFSET,
               "NTFS    ",
               NTFS_NAME_LENGTH) == 0)
    {
        return patch_ntfs_partition(output, partition);
    }

    return patch_fat_partition(output, partition);
}

static void write_be32(unsigned char* ptr, unsigned int value)
{
    ptr[0] = (unsigned char)((value >> 24) & 0xFF);
    ptr[1] = (unsigned char)((value >> 16) & 0xFF);
    ptr[2] = (unsigned char)((value >> 8) & 0xFF);
    ptr[3] = (unsigned char)(value & 0xFF);
}

static void write_be64(unsigned char* ptr, unsigned long long value)
{
    ptr[0] = (unsigned char)((value >> 56) & 0xFF);
    ptr[1] = (unsigned char)((value >> 48) & 0xFF);
    ptr[2] = (unsigned char)((value >> 40) & 0xFF);
    ptr[3] = (unsigned char)((value >> 32) & 0xFF);
    ptr[4] = (unsigned char)((value >> 24) & 0xFF);
    ptr[5] = (unsigned char)((value >> 16) & 0xFF);
    ptr[6] = (unsigned char)((value >> 8) & 0xFF);
    ptr[7] = (unsigned char)(value & 0xFF);
}

static unsigned int calculate_checksum(const unsigned char* data, size_t size)
{
    unsigned int sum = 0;
    size_t i;

    for (i = 0; i < size; i++)
        sum += data[i];

    return ~sum;
}

static unsigned long long round_up_to_sector(unsigned long long value)
{
    return (value + (SECTOR_SIZE - 1)) & ~((unsigned long long)SECTOR_SIZE - 1);
}

static int buffer_is_zero(const unsigned char* data, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++)
    {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

static void generate_vhd_uuid(unsigned char uuid[16])
{
    static int seeded = 0;
    int i;

    if (!seeded)
    {
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)clock();
        srand(seed);
        seeded = 1;
    }

    for (i = 0; i < 16; i++)
        uuid[i] = (unsigned char)(rand() & 0xFF);

    uuid[6] = (unsigned char)((uuid[6] & 0x0F) | 0x40);
    uuid[8] = (unsigned char)((uuid[8] & 0x3F) | 0x80);
}

static void calculate_vhd_geometry(unsigned long long size_bytes,
                                   unsigned short* cylinders,
                                   unsigned char* heads,
                                   unsigned char* sectors)
{
    unsigned long total_sectors;
    unsigned long cylinder_times_heads;

    total_sectors = (unsigned long)(size_bytes / SECTOR_SIZE);
    if (total_sectors > 65535UL * 16UL * 255UL)
        total_sectors = 65535UL * 16UL * 255UL;

    if (total_sectors >= 65535UL * 16UL * 63UL)
    {
        *sectors = 255;
        *heads = 16;
        cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
    }
    else
    {
        *sectors = 17;
        cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
        *heads = (unsigned char)((cylinder_times_heads + 1023UL) / 1024UL);

        if (*heads < 4)
            *heads = 4;

        if (cylinder_times_heads >= (unsigned long)(*heads) * 1024UL || *heads > 16)
        {
            *sectors = 31;
            *heads = 16;
            cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
        }

        if (cylinder_times_heads >= (unsigned long)(*heads) * 1024UL)
        {
            *sectors = 63;
            *heads = 16;
            cylinder_times_heads = (total_sectors + (*sectors - 1)) / *sectors;
        }
    }

    *cylinders = (unsigned short)((cylinder_times_heads + (*heads - 1)) / *heads);
    if (*cylinders == 0)
        *cylinders = 1;
}

static void build_vhd_footer(unsigned char footer[VHD_FOOTER_SIZE],
                             unsigned long long disk_size,
                             const unsigned char uuid[16])
{
    unsigned short cylinders;
    unsigned char heads;
    unsigned char sectors;
    unsigned int timestamp = 0;
    time_t now;

    memset(footer, 0, VHD_FOOTER_SIZE);

    memcpy(footer + 0, "conectix", 8);
    write_be32(footer + 8, 0x00000002);
    write_be32(footer + 12, 0x00010000);
    write_be64(footer + 16, VHD_FOOTER_SIZE);

    now = time(NULL);
    if (now > (time_t)VHD_TIMESTAMP_BASE)
        timestamp = (unsigned int)(now - (time_t)VHD_TIMESTAMP_BASE);

    write_be32(footer + 24, timestamp);
    memcpy(footer + 28, "ROS ", 4);
    write_be32(footer + 32, 0x00010000);
    memcpy(footer + 36, "Wi2k", 4);
    write_be64(footer + 40, disk_size);
    write_be64(footer + 48, disk_size);

    calculate_vhd_geometry(disk_size, &cylinders, &heads, &sectors);
    footer[56] = (unsigned char)((cylinders >> 8) & 0xFF);
    footer[57] = (unsigned char)(cylinders & 0xFF);
    footer[58] = heads;
    footer[59] = sectors;

    write_be32(footer + 60, VHD_DISK_TYPE_DYNAMIC);
    memcpy(footer + 68, uuid, 16);
    footer[84] = 0;

    write_be32(footer + 64, 0);
    write_be32(footer + 64, calculate_checksum(footer, VHD_FOOTER_SIZE));
}

static void build_vhd_dynamic_header(unsigned char header[VHD_DYNAMIC_HEADER_SIZE],
                                     unsigned int block_count,
                                     unsigned long long table_offset)
{
    memset(header, 0, VHD_DYNAMIC_HEADER_SIZE);

    memcpy(header + 0, "cxsparse", 8);
    write_be64(header + 8, ~0ULL);
    write_be64(header + 16, table_offset);
    write_be32(header + 24, 0x00010000);
    write_be32(header + 28, block_count);
    write_be32(header + 32, VHD_BLOCK_SIZE);

    write_be32(header + 36, 0);
    write_be32(header + 36, calculate_checksum(header, VHD_DYNAMIC_HEADER_SIZE));
}

static int write_dynamic_vhd(FILE* dst, FILE* src, unsigned long long virtual_size)
{
    unsigned char footer[VHD_FOOTER_SIZE];
    unsigned char header[VHD_DYNAMIC_HEADER_SIZE];
    unsigned char uuid[16];
    unsigned char* block_buffer = NULL;
    unsigned char* bitmap_buffer = NULL;
    unsigned int* bat = NULL;
    unsigned int sectors_per_block;
    unsigned int block_count;
    unsigned int bitmap_bytes;
    unsigned int bitmap_size;
    unsigned long long bat_bytes;
    unsigned long long bat_size;
    unsigned long long table_offset;
    unsigned long long block_index;
    int ret = -1;

    sectors_per_block = VHD_BLOCK_SIZE / SECTOR_SIZE;
    block_count = (unsigned int)((virtual_size + VHD_BLOCK_SIZE - 1) / VHD_BLOCK_SIZE);
    bitmap_bytes = (sectors_per_block + 7) / 8;
    bitmap_size = (unsigned int)round_up_to_sector(bitmap_bytes);
    bat_bytes = (unsigned long long)block_count * sizeof(unsigned int);
    bat_size = round_up_to_sector(bat_bytes);
    table_offset = VHD_FOOTER_SIZE + VHD_DYNAMIC_HEADER_SIZE;

    generate_vhd_uuid(uuid);
    build_vhd_footer(footer, virtual_size, uuid);
    build_vhd_dynamic_header(header, block_count, table_offset);

    bat = (unsigned int*)malloc(block_count * sizeof(unsigned int));
    block_buffer = (unsigned char*)malloc(VHD_BLOCK_SIZE);
    bitmap_buffer = (unsigned char*)malloc(bitmap_size);
    if ((!bat && block_count != 0) || !block_buffer || !bitmap_buffer)
        goto cleanup;

    for (block_index = 0; block_index < block_count; block_index++)
        bat[block_index] = 0xFFFFFFFFU;

    if (fwrite(footer, 1, sizeof(footer), dst) != sizeof(footer))
        goto cleanup;
    if (fwrite(header, 1, sizeof(header), dst) != sizeof(header))
        goto cleanup;
    if (write_zero_bytes(dst, bat_size) != 0)
        goto cleanup;

    if (fseek(src, 0, SEEK_SET) != 0)
        goto cleanup;

    for (block_index = 0; block_index < block_count; block_index++)
    {
        unsigned long long remaining = virtual_size - block_index * (unsigned long long)VHD_BLOCK_SIZE;
        unsigned int block_bytes = remaining > VHD_BLOCK_SIZE ? VHD_BLOCK_SIZE : (unsigned int)remaining;
        unsigned int data_sectors = (block_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        long block_file_offset;
        unsigned int sector_index;

        memset(block_buffer, 0, VHD_BLOCK_SIZE);
        memset(bitmap_buffer, 0, bitmap_size);

        if (fread(block_buffer, 1, block_bytes, src) != block_bytes)
            goto cleanup;

        if (buffer_is_zero(block_buffer, block_bytes))
            continue;

        block_file_offset = ftell(dst);
        if (block_file_offset < 0 || (block_file_offset % SECTOR_SIZE) != 0)
            goto cleanup;
        bat[block_index] = (unsigned int)(block_file_offset / SECTOR_SIZE);

        for (sector_index = 0; sector_index < data_sectors; sector_index++)
            bitmap_buffer[sector_index / 8] |= (unsigned char)(0x80 >> (sector_index % 8));

        if (fwrite(bitmap_buffer, 1, bitmap_size, dst) != bitmap_size)
            goto cleanup;
        if (fwrite(block_buffer, 1, block_bytes, dst) != block_bytes)
            goto cleanup;
        if (block_bytes < VHD_BLOCK_SIZE &&
            write_zero_bytes(dst, VHD_BLOCK_SIZE - block_bytes) != 0)
            goto cleanup;
    }

    if (fwrite(footer, 1, sizeof(footer), dst) != sizeof(footer))
        goto cleanup;

    if (fseek(dst, (long)table_offset, SEEK_SET) != 0)
        goto cleanup;

    for (block_index = 0; block_index < block_count; block_index++)
    {
        unsigned char entry[4];
        write_be32(entry, bat[block_index]);
        if (fwrite(entry, 1, sizeof(entry), dst) != sizeof(entry))
            goto cleanup;
    }

    ret = 0;

cleanup:
    free(bitmap_buffer);
    free(block_buffer);
    free(bat);
    return ret;
}

static int parse_unsigned(const char* text,
                          int base,
                          unsigned int maximum,
                          unsigned int* value)
{
    char* end;
    unsigned long long parsed;

    parsed = strtoull(text, &end, base);
    if (end == text || *end != '\0' || parsed > maximum)
        return -1;

    *value = (unsigned int)parsed;
    return 0;
}

static int parse_guid(const char* text, unsigned char guid[16])
{
    static const int byte_order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    unsigned int index = 0;
    const char* p;

    if (strlen(text) != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return -1;

    for (p = text; *p != '\0' && index < 16; p++)
    {
        unsigned int high, low;

        if (*p == '-')
            continue;
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1]))
            return -1;
        high = (unsigned int)(isdigit((unsigned char)p[0]) ? p[0] - '0' : (tolower((unsigned char)p[0]) - 'a' + 10));
        low = (unsigned int)(isdigit((unsigned char)p[1]) ? p[1] - '0' : (tolower((unsigned char)p[1]) - 'a' + 10));
        guid[byte_order[index++]] = (unsigned char)((high << 4) | low);
        p++;
    }

    return index == 16 ? 0 : -1;
}

int main(int argc, char* argv[])
{
    const char* output_path = NULL;
    const char* mbr_path = NULL;
    PARTITION_INPUT partitions[MAX_MBR_PARTITIONS];
    unsigned int partition_count = 0;
    int current_partition = -1;
    RAW_BLOB blobs[MAX_RAW_BLOBS];
    unsigned int blob_count = 0;
    int current_blob = -1;
    int gpt = 0;
    unsigned char* gpt_entries = NULL;
    OUTPUT_FORMAT output_format = OUTPUT_FORMAT_RAW;
    FILE* f_output = NULL;
    FILE* f_final = NULL;
    FILE* f_mbr = NULL;
    unsigned char mbr_sector[SECTOR_SIZE];
    unsigned long long total_sectors = 0;
    long total_size;
    unsigned int index;
    int i;
    int ret = 1;

    memset(partitions, 0, sizeof(partitions));
    memset(blobs, 0, sizeof(blobs));

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            output_path = argv[++i];
        }
        else if (strcmp(argv[i], "-mbr") == 0 && i + 1 < argc)
        {
            mbr_path = argv[++i];
        }
        else if ((strcmp(argv[i], "-partition") == 0 || strcmp(argv[i], "-blank") == 0 ||
                  strcmp(argv[i], "-data") == 0) && i + 1 < argc)
        {
            const int blank = strcmp(argv[i], "-blank") == 0;
            const int data = strcmp(argv[i], "-data") == 0;
            const char* value = argv[++i];
            PARTITION_INPUT* partition;

            if (partition_count == MAX_MBR_PARTITIONS)
            {
                fprintf(stderr, "Error: MBR images support at most four partitions.\n");
                goto cleanup;
            }

            current_partition = (int)partition_count++;
            current_blob = -1;
            partition = &partitions[current_partition];
            partition->Blank = blank;
            partition->Data = data;
            partition->StartSector = current_partition == 0 ? DEFAULT_START_SECTOR : 0;
            partition->Type = current_partition == 0 ? DEFAULT_PARTITION_TYPE : 0x07;
            if (blank)
            {
                if (parse_unsigned(value, 10, 0xFFFFFFFFU, &partition->SectorCount) != 0 || partition->SectorCount == 0)
                {
                    fprintf(stderr, "Error: Invalid blank partition size.\n");
                    goto cleanup;
                }
            }
            else
            {
                partition->Path = value;
            }
        }
        else if (strcmp(argv[i], "-start") == 0 && i + 1 < argc)
        {
            if (current_partition < 0 ||
                parse_unsigned(argv[++i],
                               10,
                               0xFFFFFFFFU,
                               &partitions[current_partition].StartSector) != 0)
            {
                fprintf(stderr, "Error: Invalid partition start sector.\n");
                goto cleanup;
            }
        }
        else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc)
        {
            unsigned int type;

            if (current_partition < 0 ||
                parse_unsigned(argv[++i], 16, 0xFF, &type) != 0)
            {
                fprintf(stderr, "Error: Invalid partition type.\n");
                goto cleanup;
            }
            partitions[current_partition].Type = (unsigned char)type;
        }
        else if (strcmp(argv[i], "-size") == 0 && i + 1 < argc)
        {
            if (current_partition < 0 || !partitions[current_partition].Data ||
                parse_unsigned(argv[++i], 10, 0xFFFFFFFFU,
                               &partitions[current_partition].ReservedSectors) != 0 ||
                partitions[current_partition].ReservedSectors == 0)
            {
                fprintf(stderr, "Error: -size needs a positive sector count after -data.\n");
                goto cleanup;
            }
        }
        else if (strcmp(argv[i], "-raw") == 0 && i + 1 < argc)
        {
            if (blob_count == MAX_RAW_BLOBS)
            {
                fprintf(stderr, "Error: At most %d -raw blobs are supported.\n", MAX_RAW_BLOBS);
                goto cleanup;
            }
            current_blob = (int)blob_count++;
            blobs[current_blob].Path = argv[++i];
        }
        else if (strcmp(argv[i], "-at") == 0 && i + 1 < argc)
        {
            if (current_blob < 0 ||
                parse_unsigned(argv[++i], 10, 0xFFFFFFFFU, &blobs[current_blob].StartSector) != 0 ||
                blobs[current_blob].StartSector == 0)
            {
                fprintf(stderr, "Error: -at needs a non-zero sector after -raw.\n");
                goto cleanup;
            }
        }
        else if (strcmp(argv[i], "-name") == 0 && i + 1 < argc)
        {
            if (current_partition < 0 || strlen(argv[i + 1]) == 0 || strlen(argv[i + 1]) > GPT_NAME_LENGTH)
            {
                fprintf(stderr, "Error: -name needs a label of at most %d characters after a partition.\n", GPT_NAME_LENGTH);
                goto cleanup;
            }
            partitions[current_partition].Name = argv[++i];
        }
        else if (strcmp(argv[i], "-guid") == 0 && i + 1 < argc)
        {
            if (current_partition < 0 ||
                parse_guid(argv[++i], partitions[current_partition].TypeGuid) != 0)
            {
                fprintf(stderr, "Error: -guid needs a type GUID after a partition.\n");
                goto cleanup;
            }
            partitions[current_partition].HasTypeGuid = 1;
        }
        else if (strcmp(argv[i], "-bootable") == 0)
        {
            if (current_partition < 0)
            {
                fprintf(stderr, "Error: -bootable needs a partition.\n");
                goto cleanup;
            }
            partitions[current_partition].Bootable = 1;
        }
        else if (strcmp(argv[i], "-gpt") == 0)
        {
            gpt = 1;
        }
        else if (strcmp(argv[i], "-format") == 0 && i + 1 < argc)
        {
            const char* format = argv[++i];

            if (strcmp(format, "raw") == 0)
                output_format = OUTPUT_FORMAT_RAW;
            else if (strcmp(format, "vhd") == 0)
                output_format = OUTPUT_FORMAT_VHD;
            else
            {
                fprintf(stderr, "Error: Unknown format '%s'.\n", format);
                print_usage(argv[0]);
                goto cleanup;
            }
        }
        else if (strcmp(argv[i], "-vhd") == 0)
        {
            output_format = OUTPUT_FORMAT_VHD;
        }
        else
        {
            fprintf(stderr, "Error: Unknown argument '%s'.\n", argv[i]);
            print_usage(argv[0]);
            goto cleanup;
        }
    }

    if (!output_path || partition_count == 0)
    {
        fprintf(stderr, "Error: Missing required arguments.\n");
        print_usage(argv[0]);
        goto cleanup;
    }

    for (index = 0; !gpt && index < partition_count; index++)
    {
        if (partitions[index].HasTypeGuid || partitions[index].Bootable)
        {
            fprintf(stderr, "Error: -guid and -bootable need -gpt.\n");
            goto cleanup;
        }
    }

    memset(mbr_sector, 0, sizeof(mbr_sector));
    if (mbr_path)
    {
        f_mbr = fopen(mbr_path, "rb");
        if (!f_mbr)
        {
            fprintf(stderr, "Error: Cannot open MBR file '%s'.\n", mbr_path);
            goto cleanup;
        }
        if (fread(mbr_sector, 1, MBR_BOOT_CODE_SIZE, f_mbr) == 0)
        {
            fprintf(stderr, "Error: Cannot read MBR boot code from '%s'.\n", mbr_path);
            goto cleanup;
        }
        fclose(f_mbr);
        f_mbr = NULL;
    }

    for (index = 0; index < partition_count; index++)
    {
        PARTITION_INPUT* partition = &partitions[index];
        PARTITION_ENTRY* entry;
        unsigned long long end_sector;
        unsigned int other;

        if (partition->StartSector == 0)
        {
            fprintf(stderr, "Error: No start sector specified for partition %u.\n", index + 1);
            goto cleanup;
        }
        if (gpt && partition->StartSector < GPT_FIRST_USABLE_SECTOR)
        {
            fprintf(stderr, "Error: Partition %u starts inside the GPT.\n", index + 1);
            goto cleanup;
        }

        if (partition->Blank)
        {
            unsigned long long partition_size = (unsigned long long)partition->SectorCount * SECTOR_SIZE;

            if (partition_size > (unsigned long long)LONG_MAX)
            {
                fprintf(stderr, "Error: Blank partition %u is too large for this host.\n", index + 1);
                goto cleanup;
            }
            partition->Size = (long)partition_size;
        }
        else
        {
            partition->File = fopen(partition->Path, "rb");
            if (!partition->File)
            {
                fprintf(stderr, "Error: Cannot open partition image '%s'.\n", partition->Path);
                goto cleanup;
            }
            if (fseek(partition->File, 0, SEEK_END) != 0 || (partition->Size = ftell(partition->File)) <= 0 ||
                (!partition->Data && partition->Size % SECTOR_SIZE != 0) || fseek(partition->File, 0, SEEK_SET) != 0)
            {
                fprintf(stderr, "Error: Partition image '%s' has an invalid size.\n", partition->Path);
                goto cleanup;
            }
            if (fread(partition->BootSector, 1, sizeof(partition->BootSector), partition->File) != sizeof(partition->BootSector) || fseek(partition->File, 0, SEEK_SET) != 0)
            {
                fprintf(stderr, "Error: Cannot read partition boot sector from '%s'.\n", partition->Path);
                goto cleanup;
            }

            partition->SectorCount = (unsigned int)((partition->Size + SECTOR_SIZE - 1) / SECTOR_SIZE);
            if (partition->Data && partition->ReservedSectors)
            {
                if (partition->ReservedSectors < partition->SectorCount)
                {
                    fprintf(stderr, "Error: '%s' does not fit in %u sectors.\n",
                            partition->Path, partition->ReservedSectors);
                    goto cleanup;
                }
                partition->SectorCount = partition->ReservedSectors;
            }
        }
        end_sector = (unsigned long long)partition->StartSector + partition->SectorCount;
        if (end_sector == 0 || end_sector - 1 > 0xFFFFFFFFULL)
        {
            fprintf(stderr, "Error: Partition %u exceeds MBR addressing limits.\n",
                    index + 1);
            goto cleanup;
        }

        for (other = 0; other < index; other++)
        {
            unsigned long long other_end =
                (unsigned long long)partitions[other].StartSector +
                partitions[other].SectorCount;

            if (partition->StartSector < other_end &&
                partitions[other].StartSector < end_sector)
            {
                fprintf(stderr, "Error: Partitions %u and %u overlap.\n",
                        other + 1,
                        index + 1);
                goto cleanup;
            }
        }

        if (end_sector > total_sectors)
            total_sectors = end_sector;

        if (gpt)
            continue;

        entry = (PARTITION_ENTRY*)(mbr_sector + MBR_PARTITION_TABLE_OFFSET) + index;
        entry->Status = index == 0 ? 0x80 : 0;
        entry->Type = partition->Type;
        entry->LBAStart = partition->StartSector;
        entry->LBASize = partition->SectorCount;
        write_chs(partition->StartSector, entry->CHSFirst);
        write_chs((unsigned int)(end_sector - 1), entry->CHSLast);
    }

    for (index = 0; index < blob_count; index++)
    {
        RAW_BLOB* blob = &blobs[index];
        unsigned long long blob_end;
        unsigned int other;

        if (blob->StartSector == 0)
        {
            fprintf(stderr, "Error: No -at sector given for '%s'.\n", blob->Path);
            goto cleanup;
        }
        if (read_whole_file(blob->Path, &blob->Data, &blob->Size) != 0 || blob->Size <= 0)
        {
            fprintf(stderr, "Error: Cannot read raw data '%s'.\n", blob->Path);
            goto cleanup;
        }

        blob_end = (unsigned long long)blob->StartSector +
                   ((unsigned long long)blob->Size + SECTOR_SIZE - 1) / SECTOR_SIZE;
        for (other = 0; other < partition_count; other++)
        {
            unsigned long long part_end = (unsigned long long)partitions[other].StartSector +
                                          partitions[other].SectorCount;

            if (blob->StartSector < part_end && partitions[other].StartSector < blob_end)
            {
                fprintf(stderr, "Error: '%s' overlaps partition %u.\n", blob->Path, other + 1);
                goto cleanup;
            }
        }
        for (other = 0; other < index; other++)
        {
            unsigned long long other_end = (unsigned long long)blobs[other].StartSector +
                ((unsigned long long)blobs[other].Size + SECTOR_SIZE - 1) / SECTOR_SIZE;

            if (blob->StartSector < other_end && blobs[other].StartSector < blob_end)
            {
                fprintf(stderr, "Error: '%s' overlaps '%s'.\n", blob->Path, blobs[other].Path);
                goto cleanup;
            }
        }
        if (gpt && blob->StartSector < GPT_FIRST_USABLE_SECTOR)
        {
            fprintf(stderr, "Error: '%s' overlaps the GPT.\n", blob->Path);
            goto cleanup;
        }
        if (blob_end > total_sectors)
            total_sectors = blob_end;
    }

    if (gpt)
    {
        PARTITION_ENTRY* entry = (PARTITION_ENTRY*)(mbr_sector + MBR_PARTITION_TABLE_OFFSET);

        total_sectors += GPT_ENTRY_SECTORS + 1;
        entry->Status = 0;
        entry->CHSFirst[0] = 0x00;
        entry->CHSFirst[1] = 0x02;
        entry->CHSFirst[2] = 0x00;
        entry->Type = 0xEE;
        entry->CHSLast[0] = 0xFF;
        entry->CHSLast[1] = 0xFF;
        entry->CHSLast[2] = 0xFF;
        entry->LBAStart = 1;
        entry->LBASize = total_sectors - 1 > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (unsigned int)(total_sectors - 1);
    }

    if (total_sectors > (unsigned long long)LONG_MAX / SECTOR_SIZE)
    {
        fprintf(stderr, "Error: Output image is too large for this host.\n");
        goto cleanup;
    }
    total_size = (long)(total_sectors * SECTOR_SIZE);
    mbr_sector[MBR_SIGNATURE_OFFSET] = 0x55;
    mbr_sector[MBR_SIGNATURE_OFFSET + 1] = 0xAA;

    if (output_format == OUTPUT_FORMAT_RAW)
    {
        f_output = fopen(output_path, "w+b");
        if (!f_output)
        {
            fprintf(stderr, "Error: Cannot create output file '%s'.\n", output_path);
            goto cleanup;
        }
    }
    else
    {
        f_output = tmpfile();
        if (!f_output)
        {
            fprintf(stderr, "Error: Cannot create temporary raw image for VHD output.\n");
            goto cleanup;
        }
    }

    if (initialize_zeroed_file(f_output, total_size) != 0 ||
        fseek(f_output, 0, SEEK_SET) != 0 ||
        fwrite(mbr_sector, 1, sizeof(mbr_sector), f_output) != sizeof(mbr_sector))
    {
        fprintf(stderr, "Error: Cannot initialize the output disk image.\n");
        goto cleanup;
    }

    for (index = 0; index < partition_count; index++)
    {
        PARTITION_INPUT* partition = &partitions[index];
        long output_offset = (long)partition->StartSector * SECTOR_SIZE;

        if (partition->Blank)
            continue;

        if (fseek(f_output, output_offset, SEEK_SET) != 0 ||
            fseek(partition->File, 0, SEEK_SET) != 0 ||
            copy_file_data(f_output, partition->File, partition->Size) != 0 ||
            fflush(f_output) != 0)
        {
            fprintf(stderr, "Error: Cannot write partition image '%s'.\n", partition->Path);
            goto cleanup;
        }
        if (!partition->Data && patch_partition(f_output, partition) != 0)
            goto cleanup;
    }

    if (gpt)
    {
        unsigned char header[SECTOR_SIZE];
        unsigned long long last_lba = total_sectors - 1;

        gpt_entries = malloc(GPT_ENTRY_COUNT * GPT_ENTRY_SIZE);
        if (!gpt_entries)
            goto cleanup;
        build_gpt_entries(gpt_entries, partitions, partition_count);
        build_gpt_header(header, 1, last_lba, 2, last_lba, gpt_entries);
        if (write_data_at(f_output, SECTOR_SIZE, header, sizeof(header)) != 0 ||
            write_data_at(f_output, 2 * SECTOR_SIZE, gpt_entries, GPT_ENTRY_COUNT * GPT_ENTRY_SIZE) != 0)
        {
            fprintf(stderr, "Error: Cannot write the GPT.\n");
            goto cleanup;
        }
        build_gpt_header(header, last_lba, 1, last_lba - GPT_ENTRY_SECTORS, last_lba, gpt_entries);
        if (write_data_at(f_output, (long)(last_lba - GPT_ENTRY_SECTORS) * SECTOR_SIZE, gpt_entries, GPT_ENTRY_COUNT * GPT_ENTRY_SIZE) != 0 ||
            write_data_at(f_output, (long)last_lba * SECTOR_SIZE, header, sizeof(header)) != 0)
        {
            fprintf(stderr, "Error: Cannot write the backup GPT.\n");
            goto cleanup;
        }
    }

    for (index = 0; index < blob_count; index++)
    {
        if (write_data_at(f_output, (long)blobs[index].StartSector * SECTOR_SIZE,
                          blobs[index].Data, (size_t)blobs[index].Size) != 0 ||
            fflush(f_output) != 0)
        {
            fprintf(stderr, "Error: Cannot write '%s'.\n", blobs[index].Path);
            goto cleanup;
        }
    }

    if (output_format == OUTPUT_FORMAT_VHD)
    {
        if (fflush(f_output) != 0 || fseek(f_output, 0, SEEK_SET) != 0)
        {
            fprintf(stderr, "Error: Cannot rewind staged raw image for VHD output.\n");
            goto cleanup;
        }

        f_final = fopen(output_path, "w+b");
        if (!f_final)
        {
            fprintf(stderr, "Error: Cannot create output file '%s'.\n", output_path);
            goto cleanup;
        }
        if (write_dynamic_vhd(f_final, f_output, (unsigned long long)total_size) != 0)
        {
            fprintf(stderr, "Error: Cannot write dynamic VHD image.\n");
            goto cleanup;
        }
    }

    printf("Created %s '%s' (%ld bytes virtual disk, %u partition%s)\n",
           output_format == OUTPUT_FORMAT_VHD ? "dynamic VHD image" : "disk image",
           output_path,
           total_size,
           partition_count,
           partition_count == 1 ? "" : "s");
    ret = 0;

cleanup:
    if (f_final) fclose(f_final);
    if (f_output) fclose(f_output);
    if (f_mbr) fclose(f_mbr);
    for (index = 0; index < partition_count; index++)
    {
        if (partitions[index].File)
            fclose(partitions[index].File);
    }
    for (index = 0; index < blob_count; index++)
        free(blobs[index].Data);
    free(gpt_entries);
    return ret;
}
