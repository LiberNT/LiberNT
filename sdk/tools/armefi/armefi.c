/*
 * PROJECT:     LiberNT Build
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Prepare an ARMv7 UEFI application: Thumb machine type and self-applied base relocations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * UEFI firmware applies the base relocations of the image it loads, and some
 * firmware knows only the absolute ones: it refuses the MOVW/MOVT pairs
 * (IMAGE_REL_BASED_THUMB_MOV32) Thumb-2 code is full of. The entry code of a
 * LiberNT boot application applies its relocations itself, from the table this
 * tool hands it in a descriptor inside the image, and the firmware is left an
 * empty table.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DOS_SIGNATURE 0x5A4D
#define PE_SIGNATURE 0x00004550
#define PE32_MAGIC 0x010B
#define MACHINE_ARMNT 0x01C4
#define MACHINE_THUMB 0x01C2
#define FILE_RELOCS_STRIPPED 0x0001
#define DIRECTORY_BASERELOC 5
#define SECTION_HEADER_SIZE 40

/* The descriptor of bootloader/arch/arm/efientry.S */
#define DESCRIPTOR_SIGNATURE_LOW 0x4D524145U
#define DESCRIPTOR_SIGNATURE_HIGH 0x434C5246U
#define DESCRIPTOR_SIZE 36
#define DESCRIPTOR_TABLE_RVA 8
#define DESCRIPTOR_TABLE_SIZE 12
#define DESCRIPTOR_IMAGE_BASE 16
#define DESCRIPTOR_IMAGE_SIZE 20
#define DESCRIPTOR_EMPTY_BLOCK 28

static uint16_t Read16(const unsigned char *At)
{
    return (uint16_t)(At[0] | (At[1] << 8));
}

static uint32_t Read32(const unsigned char *At)
{
    return (uint32_t)At[0] | ((uint32_t)At[1] << 8) | ((uint32_t)At[2] << 16) | ((uint32_t)At[3] << 24);
}

static void Write16(unsigned char *At, uint16_t Value)
{
    At[0] = (unsigned char)Value;
    At[1] = (unsigned char)(Value >> 8);
}

static void Write32(unsigned char *At, uint32_t Value)
{
    At[0] = (unsigned char)Value;
    At[1] = (unsigned char)(Value >> 8);
    At[2] = (unsigned char)(Value >> 16);
    At[3] = (unsigned char)(Value >> 24);
}

/* The PE checksum: the one's complement sum of the file's 16-bit words, without the checksum field, plus its length. */
static uint32_t Checksum(const unsigned char *Image, size_t Size, size_t Field)
{
    uint32_t Sum = 0;
    size_t Offset;

    for (Offset = 0; Offset + 1 < Size; Offset += 2)
    {
        if (Offset == Field || Offset == Field + 2)
            continue;
        Sum += Read16(Image + Offset);
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }

    if (Size & 1)
    {
        Sum += Image[Size - 1];
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }

    Sum = (Sum & 0xFFFF) + (Sum >> 16);
    return Sum + (uint32_t)Size;
}

int main(int argc, char **argv)
{
    unsigned char *Image = NULL, *FileHeader, *Optional, *Sections, *Directory, *Descriptor = NULL;
    uint32_t HeaderOffset, DescriptorRva = 0, RawOffset, RawSize, VirtualAddress, Offset, Index;
    uint16_t SectionCount, OptionalSize;
    int Result = 1;
    long Size;
    FILE *File;

    if (argc != 2)
    {
        fprintf(stderr, "usage: armefi <image>\n");
        return 1;
    }

    File = fopen(argv[1], "r+b");
    if (!File)
    {
        fprintf(stderr, "armefi: cannot open %s\n", argv[1]);
        return 1;
    }

    if (fseek(File, 0, SEEK_END) != 0 || (Size = ftell(File)) < 0x40 || fseek(File, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "armefi: %s is not an image\n", argv[1]);
        goto Done;
    }

    Image = malloc((size_t)Size);
    if (!Image || fread(Image, 1, (size_t)Size, File) != (size_t)Size)
    {
        fprintf(stderr, "armefi: cannot read %s\n", argv[1]);
        goto Done;
    }

    HeaderOffset = Read32(Image + 0x3C);
    if (Read16(Image) != DOS_SIGNATURE || HeaderOffset > (uint32_t)Size - 24 ||
        Read32(Image + HeaderOffset) != PE_SIGNATURE)
    {
        fprintf(stderr, "armefi: %s is not a PE image\n", argv[1]);
        goto Done;
    }

    FileHeader = Image + HeaderOffset + 4;
    SectionCount = Read16(FileHeader + 2);
    OptionalSize = Read16(FileHeader + 16);
    Optional = FileHeader + 20;
    Sections = Optional + OptionalSize;
    if (OptionalSize < 96 + 8 * (DIRECTORY_BASERELOC + 1) ||
        (size_t)(Sections - Image) + (size_t)SectionCount * SECTION_HEADER_SIZE > (size_t)Size ||
        Read16(Optional) != PE32_MAGIC || Read32(Optional + 92) <= DIRECTORY_BASERELOC)
    {
        fprintf(stderr, "armefi: %s is not a 32-bit image with base relocations\n", argv[1]);
        goto Done;
    }

    if (Read16(FileHeader) == MACHINE_ARMNT)
        Write16(FileHeader, MACHINE_THUMB);
    if (Read16(FileHeader) != MACHINE_THUMB)
    {
        fprintf(stderr, "armefi: %s is not an ARMv7 image\n", argv[1]);
        goto Done;
    }

    for (Index = 0; Index < SectionCount && !Descriptor; Index++)
    {
        VirtualAddress = Read32(Sections + Index * SECTION_HEADER_SIZE + 12);
        RawSize = Read32(Sections + Index * SECTION_HEADER_SIZE + 16);
        RawOffset = Read32(Sections + Index * SECTION_HEADER_SIZE + 20);
        if (RawOffset > (uint32_t)Size || RawSize > (uint32_t)Size - RawOffset)
        {
            fprintf(stderr, "armefi: %s has a section outside the file\n", argv[1]);
            goto Done;
        }

        for (Offset = 0; Offset + DESCRIPTOR_SIZE <= RawSize; Offset += 4)
        {
            if (Read32(Image + RawOffset + Offset) == DESCRIPTOR_SIGNATURE_LOW &&
                Read32(Image + RawOffset + Offset + 4) == DESCRIPTOR_SIGNATURE_HIGH)
            {
                Descriptor = Image + RawOffset + Offset;
                DescriptorRva = VirtualAddress + Offset;
                break;
            }
        }
    }

    if (!Descriptor)
    {
        fprintf(stderr, "armefi: %s has no relocation descriptor\n", argv[1]);
        goto Done;
    }

    Directory = Optional + 96 + 8 * DIRECTORY_BASERELOC;
    if (Read32(Descriptor + DESCRIPTOR_TABLE_RVA) == 0)
    {
        Write32(Descriptor + DESCRIPTOR_TABLE_RVA, Read32(Directory));
        Write32(Descriptor + DESCRIPTOR_TABLE_SIZE, Read32(Directory + 4));
        Write32(Descriptor + DESCRIPTOR_IMAGE_BASE, Read32(Optional + 28));
        Write32(Descriptor + DESCRIPTOR_IMAGE_SIZE, Read32(Optional + 56));
        Write32(Directory, DescriptorRva + DESCRIPTOR_EMPTY_BLOCK);
        Write32(Directory + 4, 8);
    }
    Write16(FileHeader + 18, (uint16_t)(Read16(FileHeader + 18) & ~FILE_RELOCS_STRIPPED));
    Write32(Optional + 64, Checksum(Image, (size_t)Size, (size_t)(Optional + 64 - Image)));

    if (fseek(File, 0, SEEK_SET) != 0 || fwrite(Image, 1, (size_t)Size, File) != (size_t)Size)
    {
        fprintf(stderr, "armefi: cannot write %s\n", argv[1]);
        goto Done;
    }

    Result = 0;

Done:
    free(Image);
    fclose(File);
    return Result;
}
