/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only ISO 9660 file access for boot media
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#define CDFS_BLOCK_SIZE         2048
#define CDFS_DESCRIPTOR_BLOCK   16
#define CDFS_PRIMARY            1
#define CDFS_ROOT_RECORD        156
#define CDFS_RECORD_EXTENT      2
#define CDFS_RECORD_LENGTH      10
#define CDFS_RECORD_FLAGS       25
#define CDFS_RECORD_NAME_SIZE   32
#define CDFS_RECORD_NAME        33
#define CDFS_FLAG_DIRECTORY     0x02
#define CDFS_NAME_CHARS         256
#define CDFS_MAX_DIRECTORY      0x400000

static DK_DEVICE CdfsDevice;
static ULONG CdfsRootBlock;
static ULONG CdfsRootLength;

BOOLEAN
CdfsMount(
    _In_ PDK_DEVICE Device)
{
    UCHAR Descriptor[CDFS_BLOCK_SIZE];

    if (!DkRead(Device, CDFS_DESCRIPTOR_BLOCK * CDFS_BLOCK_SIZE, sizeof(Descriptor), Descriptor) ||
        Descriptor[0] != CDFS_PRIMARY ||
        !RtlEqualMemory(Descriptor + 1, "CD001", 5))
    {
        return FALSE;
    }

    CdfsDevice = *Device;
    CdfsRootBlock = LeUlong(Descriptor + CDFS_ROOT_RECORD + CDFS_RECORD_EXTENT);
    CdfsRootLength = LeUlong(Descriptor + CDFS_ROOT_RECORD + CDFS_RECORD_LENGTH);
    return CdfsRootLength != 0 && CdfsRootLength <= CDFS_MAX_DIRECTORY;
}

static
BOOLEAN
CdfsGetName(
    _In_ const UCHAR* Record,
    _Out_writes_z_(CDFS_NAME_CHARS) PSTR Name)
{
    ULONG Length = Record[CDFS_RECORD_NAME_SIZE];
    ULONG i;

    if (Length == 1 && Record[CDFS_RECORD_NAME] <= 1)
        return FALSE;

    for (i = 0; i < Length && i < CDFS_NAME_CHARS - 1 && Record[CDFS_RECORD_NAME + i] != ';'; i++)
        Name[i] = (CHAR)Record[CDFS_RECORD_NAME + i];

    while (i != 0 && Name[i - 1] == '.')
        i--;

    Name[i] = ANSI_NULL;
    return i != 0;
}

static
BOOLEAN
CdfsLookup(
    _In_ ULONG DirectoryBlock,
    _In_ ULONG DirectoryLength,
    _In_z_ PCSTR Component,
    _Out_ PULONG Block,
    _Out_ PULONG Length,
    _Out_ PBOOLEAN IsDirectory)
{
    CHAR Name[CDFS_NAME_CHARS];
    PUCHAR Directory, Record;
    ULONG Offset = 0;
    BOOLEAN Found = FALSE;

    if (DirectoryLength == 0 || DirectoryLength > CDFS_MAX_DIRECTORY)
        return FALSE;

    Directory = FwAllocate(DirectoryLength);
    if (!Directory)
        return FALSE;

    if (!DkRead(&CdfsDevice, (ULONGLONG)DirectoryBlock * CDFS_BLOCK_SIZE, DirectoryLength, Directory))
        Offset = DirectoryLength;

    while (!Found && Offset + CDFS_RECORD_NAME < DirectoryLength)
    {
        Record = Directory + Offset;
        if (Record[0] == 0)
        {
            Offset = (Offset / CDFS_BLOCK_SIZE + 1) * CDFS_BLOCK_SIZE;
            continue;
        }

        if (Record[0] <= CDFS_RECORD_NAME ||
            Record[0] > DirectoryLength - Offset ||
            Record[CDFS_RECORD_NAME_SIZE] > Record[0] - CDFS_RECORD_NAME)
        {
            break;
        }

        Offset += Record[0];

        if (CdfsGetName(Record, Name) && _stricmp(Name, Component) == 0)
        {
            *Block = LeUlong(Record + CDFS_RECORD_EXTENT);
            *Length = LeUlong(Record + CDFS_RECORD_LENGTH);
            *IsDirectory = (Record[CDFS_RECORD_FLAGS] & CDFS_FLAG_DIRECTORY) != 0;
            Found = TRUE;
        }
    }

    FwFree(Directory);
    return Found;
}

PVOID
CdfsReadFile(
    _In_ PCSTR Path,
    _Out_ PULONG Size)
{
    CHAR Component[CDFS_NAME_CHARS];
    ULONG Block = CdfsRootBlock, Length = CdfsRootLength, i;
    BOOLEAN IsDirectory = TRUE;
    PVOID Data;

    *Size = 0;

    while (*Path != ANSI_NULL)
    {
        while (*Path == '\\' || *Path == '/')
            Path++;

        for (i = 0; Path[i] != ANSI_NULL && Path[i] != '\\' && Path[i] != '/'; i++)
        {
            if (i >= RTL_NUMBER_OF(Component) - 1)
                return NULL;
            Component[i] = Path[i];
        }

        Component[i] = ANSI_NULL;
        Path += i;
        if (i == 0)
            break;

        if (!IsDirectory || !CdfsLookup(Block, Length, Component, &Block, &Length, &IsDirectory))
            return NULL;
    }

    if (IsDirectory || Length == 0)
        return NULL;

    Data = FwAllocatePages(Length);
    if (!Data)
        return NULL;

    if (!DkRead(&CdfsDevice, (ULONGLONG)Block * CDFS_BLOCK_SIZE, Length, Data))
    {
        FwFreePages(Data, Length);
        return NULL;
    }

    *Size = Length;
    return Data;
}
