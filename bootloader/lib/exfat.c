/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     exFAT file system support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <ff.h>
#include <diskio.h>

#define EXFAT_CACHE_BLOCK_SIZE 4096
#define EXFAT_CACHE_WAYS       4
#define EXFAT_CACHE_SETS       31
#define EXFAT_CACHE_BLOCKS     (EXFAT_CACHE_WAYS * EXFAT_CACHE_SETS)
#define EXFAT_CACHE_EMPTY      ((LBA_t)~0ULL)

#define EXFAT_SECTOR_SIZE              512
#define EXFAT_NAME_OFFSET              3
#define EXFAT_NAME_LENGTH              8
#define EXFAT_SECTOR_SHIFT_OFFSET      108
#define FAT_BYTES_PER_SECTOR_OFFSET    11
#define EXFAT_BOOT_SIGNATURE_OFFSET    510
#define EXFAT_MIN_SECTOR_SHIFT         9
#define EXFAT_MAX_SECTOR_SHIFT         12
#define EXFAT_PATH_CHARS               260

typedef struct _EXFAT_VOLUME
{
    DK_DEVICE Device;
    ULONG BytesPerSector;
    ULONGLONG SectorCount;
    PUCHAR CacheBuffer;
    LBA_t CacheTags[EXFAT_CACHE_BLOCKS];
    BYTE CacheNextWay[EXFAT_CACHE_SETS];
    ULONG CacheBlockSectors;
    FATFS FileSystem;
} EXFAT_VOLUME, *PEXFAT_VOLUME;

static PEXFAT_VOLUME ExFatVolume;

PARTITION VolToPart[FF_VOLUMES] = {{0, 0}};

VOID
ExFatUnmount(VOID)
{
    if (!ExFatVolume)
        return;

    f_mount(NULL, "0:", 0);
    FwFree(ExFatVolume->CacheBuffer);
    FwFree(ExFatVolume);
    ExFatVolume = NULL;
}

BOOLEAN
ExFatMount(
    _In_ PDK_DEVICE Device)
{
    UCHAR BootSector[EXFAT_SECTOR_SIZE];
    PEXFAT_VOLUME Volume;
    BYTE SectorShift;
    ULONG BytesPerSector;
    ULONG i;

    ExFatUnmount();

    if (!DkRead(Device, 0, sizeof(BootSector), BootSector) ||
        BootSector[EXFAT_BOOT_SIGNATURE_OFFSET] != 0x55 ||
        BootSector[EXFAT_BOOT_SIGNATURE_OFFSET + 1] != 0xAA)
    {
        return FALSE;
    }

    if (RtlEqualMemory(&BootSector[EXFAT_NAME_OFFSET], "EXFAT   ", EXFAT_NAME_LENGTH))
    {
        SectorShift = BootSector[EXFAT_SECTOR_SHIFT_OFFSET];
    }
    else
    {
        BytesPerSector = BootSector[FAT_BYTES_PER_SECTOR_OFFSET] |
                         (BootSector[FAT_BYTES_PER_SECTOR_OFFSET + 1] << 8);
        for (SectorShift = EXFAT_MIN_SECTOR_SHIFT; SectorShift <= EXFAT_MAX_SECTOR_SHIFT; SectorShift++)
        {
            if (BytesPerSector == (1U << SectorShift))
                break;
        }
    }

    if (SectorShift < EXFAT_MIN_SECTOR_SHIFT || SectorShift > EXFAT_MAX_SECTOR_SHIFT)
        return FALSE;

    Volume = FwAllocate(sizeof(*Volume));
    if (!Volume)
        return FALSE;

    RtlZeroMemory(Volume, sizeof(*Volume));
    Volume->Device = *Device;
    Volume->BytesPerSector = 1UL << SectorShift;
    Volume->SectorCount = Device->Length / Volume->BytesPerSector;
    Volume->CacheBlockSectors = EXFAT_CACHE_BLOCK_SIZE / Volume->BytesPerSector;
    Volume->CacheBuffer = FwAllocate(EXFAT_CACHE_BLOCKS * EXFAT_CACHE_BLOCK_SIZE);

    for (i = 0; i < EXFAT_CACHE_BLOCKS; i++)
        Volume->CacheTags[i] = EXFAT_CACHE_EMPTY;

    ExFatVolume = Volume;

    if (f_mount(&Volume->FileSystem, "0:", 1) != FR_OK)
    {
        ExFatUnmount();
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
ExFatIsExFat(VOID)
{
    return ExFatVolume && ExFatVolume->FileSystem.fs_type == FS_EXFAT;
}

PVOID
ExFatReadFile(
    _In_ PCSTR Path,
    _Out_ PULONG Size)
{
    CHAR FatPath[EXFAT_PATH_CHARS];
    PVOID Data;
    FIL File;
    UINT Read;
    ULONG i;

    *Size = 0;

    while (*Path == '\\' || *Path == '/')
        Path++;

    FatPath[0] = '0';
    FatPath[1] = ':';
    FatPath[2] = '/';
    for (i = 0; Path[i] != ANSI_NULL; i++)
    {
        if (i + 4 >= sizeof(FatPath))
            return NULL;
        FatPath[i + 3] = (Path[i] == '\\') ? '/' : Path[i];
    }
    FatPath[i + 3] = ANSI_NULL;

    if (!ExFatVolume || f_open(&File, FatPath, FA_READ) != FR_OK)
        return NULL;

    Data = (f_size(&File) <= MAXULONG) ? FwAllocatePages((SIZE_T)f_size(&File)) : NULL;
    if (Data && (f_read(&File, Data, (UINT)f_size(&File), &Read) != FR_OK || Read != f_size(&File)))
    {
        FwFreePages(Data, (SIZE_T)f_size(&File));
        Data = NULL;
    }

    if (Data)
        *Size = (ULONG)f_size(&File);

    f_close(&File);
    return Data;
}

DSTATUS
disk_initialize(
    BYTE PhysicalDrive)
{
    return (PhysicalDrive == 0 && ExFatVolume) ? STA_PROTECT : STA_NOINIT;
}

DSTATUS
disk_status(
    BYTE PhysicalDrive)
{
    return disk_initialize(PhysicalDrive);
}

DRESULT
disk_read(
    BYTE PhysicalDrive,
    BYTE* Buffer,
    LBA_t Sector,
    UINT Count)
{
    PEXFAT_VOLUME Volume = ExFatVolume;
    PUCHAR CacheBlock;
    LBA_t Block, Base;
    ULONG Set, Slot, Way;

    if (PhysicalDrive != 0 || !Volume || !Buffer || !Count ||
        Sector >= Volume->SectorCount || Count > Volume->SectorCount - Sector ||
        Count > MAXULONG / Volume->BytesPerSector)
    {
        return RES_PARERR;
    }

    if (Count == 1 && Volume->CacheBuffer && Volume->CacheBlockSectors)
    {
        Block = Sector / Volume->CacheBlockSectors;
        Base = Block * Volume->CacheBlockSectors;

        if (Volume->SectorCount - Base >= Volume->CacheBlockSectors)
        {
            Set = (ULONG)(Block % EXFAT_CACHE_SETS);
            Slot = Set * EXFAT_CACHE_WAYS;
            for (Way = 0; Way < EXFAT_CACHE_WAYS; Way++)
            {
                if (Volume->CacheTags[Slot + Way] == Block)
                    break;
            }

            if (Way == EXFAT_CACHE_WAYS)
            {
                Way = Volume->CacheNextWay[Set];
                Volume->CacheNextWay[Set] = (BYTE)((Way + 1) % EXFAT_CACHE_WAYS);
            }

            Slot += Way;
            CacheBlock = Volume->CacheBuffer + (SIZE_T)Slot * EXFAT_CACHE_BLOCK_SIZE;

            if (Volume->CacheTags[Slot] != Block)
            {
                Volume->CacheTags[Slot] = EXFAT_CACHE_EMPTY;
                if (!DkRead(&Volume->Device,
                            (ULONGLONG)Base * Volume->BytesPerSector,
                            EXFAT_CACHE_BLOCK_SIZE,
                            CacheBlock))
                {
                    return RES_ERROR;
                }
                Volume->CacheTags[Slot] = Block;
            }

            RtlCopyMemory(Buffer,
                          CacheBlock + (ULONG)(Sector - Base) * Volume->BytesPerSector,
                          Volume->BytesPerSector);
            return RES_OK;
        }
    }

    return DkRead(&Volume->Device,
                  (ULONGLONG)Sector * Volume->BytesPerSector,
                  Count * Volume->BytesPerSector,
                  Buffer) ? RES_OK : RES_ERROR;
}

DRESULT
disk_write(
    BYTE PhysicalDrive,
    const BYTE* Buffer,
    LBA_t Sector,
    UINT Count)
{
    UNREFERENCED_PARAMETER(PhysicalDrive);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Sector);
    UNREFERENCED_PARAMETER(Count);
    return RES_WRPRT;
}

DRESULT
disk_ioctl(
    BYTE PhysicalDrive,
    BYTE Command,
    void* Buffer)
{
    if (PhysicalDrive != 0 || !ExFatVolume)
        return RES_NOTRDY;

    switch (Command)
    {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t*)Buffer = ExFatVolume->SectorCount;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD*)Buffer = (WORD)ExFatVolume->BytesPerSector;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD*)Buffer = 1;
            return RES_OK;
    }

    return RES_PARERR;
}

DWORD
get_fattime(VOID)
{
    return (1U << 21) | (1U << 16);
}

void*
ff_memalloc(
    UINT Size)
{
    return FwAllocate(Size);
}

void
ff_memfree(
    void* Allocation)
{
    FwFree(Allocation);
}
