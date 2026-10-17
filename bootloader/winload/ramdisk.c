/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Writable RAM disk built from the boot medium or an image file
 * COPYRIGHT:   Copyright 2025-2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"
#include <ramdisk.h>
#include <wim.h>

#define RD_SECTOR_SIZE        512
#define RD_MAX_SIZE           0xFFFFF000ULL
#define RD_MINIMUM_EXTRA      (64ULL * 1024 * 1024)
#define RD_READ_CHUNK         0x1000000
#define RD_PATH_CHARS         260
#define RD_NAME_CHARS         256

#define ISO_BLOCK_SIZE        2048
#define ISO_DESCRIPTOR_BLOCK  16
#define ISO_PRIMARY           1
#define ISO_VOLUME_BLOCKS     80
#define ISO_VOLUME_BLOCK_SIZE 128
#define ISO_ROOT_RECORD       156
#define ISO_RECORD_EXTENT     2
#define ISO_RECORD_LENGTH     10
#define ISO_RECORD_FLAGS      25
#define ISO_RECORD_NAME_SIZE  32
#define ISO_RECORD_NAME       33
#define ISO_FLAG_DIRECTORY    0x02

typedef struct _RD_SOURCE
{
    PUCHAR Memory;
    DK_DEVICE Device;
    ULONGLONG Size;
} RD_SOURCE, *PRD_SOURCE;

static FAT32_WRITER RdWriter;
static PUCHAR RdBase;
static ULONGLONG RdSize;
static ULONGLONG RdTotalBytes;
static ULONGLONG RdCopiedBytes;
static ULONG RdPercent;
static PCSTR RdSkipRootName;
static const UCHAR RdWimMagic[] = {'M', 'S', 'W', 'I', 'M', 0, 0, 0};

static
VOID
RdAdvance(
    _In_ ULONGLONG Bytes)
{
    ULONG Percent;

    RdCopiedBytes = min(RdCopiedBytes + Bytes, RdTotalBytes);
    Percent = (ULONG)(RdCopiedBytes * 100 / RdTotalBytes);
    if (Percent == RdPercent)
        return;

    RdPercent = Percent;
    FwPrint(L"\rLoading files... %lu%%", Percent);
}

static
BOOLEAN
RdRead(
    _In_ PRD_SOURCE Source,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONGLONG Length)
{
    ULONG Chunk;

    if (Offset > Source->Size || Length > Source->Size - Offset)
        return FALSE;

    while (Length != 0)
    {
        Chunk = (ULONG)min(Length, RD_READ_CHUNK);

        if (Source->Memory)
            RtlCopyMemory(Buffer, Source->Memory + Offset, Chunk);
        else if (!DkRead(&Source->Device, Offset, Chunk, Buffer))
            return FALSE;

        Buffer += Chunk;
        Offset += Chunk;
        Length -= Chunk;
        RdAdvance(Chunk);
    }

    return TRUE;
}

static
BOOLEAN
RdGetIsoName(
    _In_ const UCHAR* Record,
    _Out_writes_z_(RD_NAME_CHARS) PSTR Name)
{
    ULONG Length = Record[ISO_RECORD_NAME_SIZE];
    ULONG i;

    if (Length == 1 && Record[ISO_RECORD_NAME] <= 1)
        return FALSE;

    for (i = 0; i < Length && Record[ISO_RECORD_NAME + i] != ';'; i++)
        Name[i] = Record[ISO_RECORD_NAME + i];

    while (i != 0 && Name[i - 1] == '.')
        i--;

    Name[i] = ANSI_NULL;
    return i != 0;
}

static
BOOLEAN
RdCopyIsoDirectory(
    _In_ PRD_SOURCE Source,
    _In_ ULONG Block,
    _In_ ULONG Length,
    _In_ PCSTR Path)
{
    CHAR Name[RD_NAME_CHARS];
    CHAR Child[RD_PATH_CHARS];
    PUCHAR Directory, Record, Data;
    ULONG Offset = 0;
    ULONG Size;
    BOOLEAN Success = FALSE;

    Directory = FwAllocate(Length);
    if (!Directory)
        return FALSE;

    if (!RdRead(Source, (ULONGLONG)Block * ISO_BLOCK_SIZE, Directory, Length))
        goto Done;

    while (Offset + ISO_RECORD_NAME < Length)
    {
        Record = Directory + Offset;
        if (Record[0] == 0)
        {
            Offset = (Offset / ISO_BLOCK_SIZE + 1) * ISO_BLOCK_SIZE;
            continue;
        }

        if (Record[0] <= ISO_RECORD_NAME ||
            Record[0] > Length - Offset ||
            Record[ISO_RECORD_NAME_SIZE] > Record[0] - ISO_RECORD_NAME)
        {
            goto Done;
        }

        Offset += Record[0];

        if (!RdGetIsoName(Record, Name) ||
            (RdSkipRootName && Path[0] == ANSI_NULL && _stricmp(Name, RdSkipRootName) == 0))
        {
            continue;
        }

        if (!NT_SUCCESS(RtlStringCbPrintfA(Child, sizeof(Child), "%s/%s", Path, Name)))
            goto Done;

        Size = LeUlong(Record + ISO_RECORD_LENGTH);

        if (Record[ISO_RECORD_FLAGS] & ISO_FLAG_DIRECTORY)
        {
            if (!Fat32CreateDirectory(&RdWriter, Child) ||
                !RdCopyIsoDirectory(Source, LeUlong(Record + ISO_RECORD_EXTENT), Size, Child))
            {
                goto Done;
            }
        }
        else if (!Fat32CreateFileEx(&RdWriter, Child, Size, &Data) ||
                 (Size != 0 &&
                  !RdRead(Source,
                          (ULONGLONG)LeUlong(Record + ISO_RECORD_EXTENT) * ISO_BLOCK_SIZE,
                          Data,
                          Size)))
        {
            goto Done;
        }
    }

    Success = TRUE;

Done:
    FwFree(Directory);
    return Success;
}

static
PVOID
RdReadFile(
    PCSTR Path,
    PULONG Size)
{
    PUCHAR Data, Copy;

    Data = Fat32FindFile(&RdWriter, Path, Size);
    if (!Data)
        return NULL;

    Copy = FwAllocatePages(*Size);
    if (Copy)
        RtlCopyMemory(Copy, Data, *Size);

    return Copy;
}

static
BOOLEAN
RdOpenIso(
    _Inout_ PRD_SOURCE Source,
    _Out_writes_bytes_(ISO_BLOCK_SIZE) PUCHAR Descriptor)
{
    ULONGLONG Size;

    if (!RdRead(Source, ISO_DESCRIPTOR_BLOCK * ISO_BLOCK_SIZE, Descriptor, ISO_BLOCK_SIZE) ||
        Descriptor[0] != ISO_PRIMARY ||
        !RtlEqualMemory(Descriptor + 1, "CD001", 5))
    {
        return FALSE;
    }

    Size = (ULONGLONG)LeUlong(Descriptor + ISO_VOLUME_BLOCKS) *
           (Descriptor[ISO_VOLUME_BLOCK_SIZE] | (Descriptor[ISO_VOLUME_BLOCK_SIZE + 1] << 8));
    if (Size != 0 && Size < Source->Size)
        Source->Size = Size;

    return TRUE;
}

static
BOOLEAN
RdLoadIsoFile(
    _In_ PRD_SOURCE Medium,
    _In_reads_bytes_(ISO_BLOCK_SIZE) const UCHAR* Descriptor,
    _In_ PCWSTR Path,
    _Out_ PRD_SOURCE File)
{
    CHAR Name[RD_NAME_CHARS];
    CHAR Component[RD_NAME_CHARS];
    PUCHAR Directory, Record;
    ULONG Block = LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_EXTENT);
    ULONG Length = LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_LENGTH);
    ULONG Offset, i;
    BOOLEAN IsDirectory = TRUE;
    BOOLEAN Found;

    while (*Path != UNICODE_NULL)
    {
        while (*Path == L'\\')
            Path++;

        for (i = 0; Path[i] != UNICODE_NULL && Path[i] != L'\\'; i++)
        {
            if (i >= RD_NAME_CHARS - 1 || Path[i] > 0x7F)
                return FALSE;
            Component[i] = (CHAR)Path[i];
        }

        Component[i] = ANSI_NULL;
        Path += i;
        if (i == 0)
            break;

        Directory = IsDirectory ? FwAllocate(Length) : NULL;
        if (!Directory)
            return FALSE;

        IsDirectory = FALSE;
        Found = FALSE;
        Offset = RdRead(Medium, (ULONGLONG)Block * ISO_BLOCK_SIZE, Directory, Length) ? 0 : Length;

        while (!Found && Offset + ISO_RECORD_NAME < Length)
        {
            Record = Directory + Offset;
            if (Record[0] == 0)
            {
                Offset = (Offset / ISO_BLOCK_SIZE + 1) * ISO_BLOCK_SIZE;
                continue;
            }

            if (Record[0] <= ISO_RECORD_NAME ||
                Record[0] > Length - Offset ||
                Record[ISO_RECORD_NAME_SIZE] > Record[0] - ISO_RECORD_NAME)
            {
                break;
            }

            Offset += Record[0];

            if (RdGetIsoName(Record, Name) && _stricmp(Name, Component) == 0)
            {
                Block = LeUlong(Record + ISO_RECORD_EXTENT);
                Length = LeUlong(Record + ISO_RECORD_LENGTH);
                IsDirectory = (Record[ISO_RECORD_FLAGS] & ISO_FLAG_DIRECTORY) != 0;
                Found = TRUE;
            }
        }

        FwFree(Directory);
        if (!Found)
            return FALSE;
    }

    if (IsDirectory || Length == 0)
        return FALSE;

    RtlZeroMemory(File, sizeof(*File));
    File->Size = Length;
    File->Memory = FwAllocatePages(Length);
    if (!File->Memory)
        return FALSE;

    return RdRead(Medium, (ULONGLONG)Block * ISO_BLOCK_SIZE, File->Memory, Length);
}

static
VOID
RdWimProgress(
    _In_opt_ PVOID Context,
    _In_ ULONGLONG BytesAdvanced,
    _In_opt_ PCSTR CurrentPath)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(CurrentPath);

    RdAdvance(BytesAdvanced);
}

static
BOOLEAN
RdCreateVolume(
    _In_ ULONGLONG PayloadBytes,
    _In_ ULONGLONG ExtraBytes)
{
    RAMDISK_FAT32_LAYOUT Layout;
    ULONGLONG VolumeOffset;

    RdSize = ALIGN_UP_BY(PayloadBytes + max(ExtraBytes, RD_MINIMUM_EXTRA), MM_PAGE_SIZE);
    if (RdSize > RD_MAX_SIZE)
    {
        FwPrint(L"The RAM disk is larger than 4 GB.\r\n");
        return FALSE;
    }

    RdBase = MmAllocateMemoryWithType((SIZE_T)RdSize, LoaderXIPRom);
    if (!RdBase)
        RdBase = MmAllocateHighMemoryWithType((SIZE_T)RdSize, LoaderXIPRom);
    if (!RdBase)
    {
        FwPrint(L"Not enough memory for a %I64u MB RAM disk.\r\n", RdSize >> 20);
        return FALSE;
    }

    if (!RamDiskFormatFat32(RdBase, RdSize, &Layout))
        return FALSE;

    VolumeOffset = (ULONGLONG)Layout.HiddenSectors * Layout.BytesPerSector;
    RdTotalBytes = PayloadBytes;
    RdCopiedBytes = 0;
    RdPercent = 0;
    return Fat32WriterInit(&RdWriter, RdBase + VolumeOffset, RdSize - VolumeOffset, &Layout);
}

static
BOOLEAN
RdCopyIso(
    _In_ PRD_SOURCE Source,
    _In_reads_bytes_(ISO_BLOCK_SIZE) const UCHAR* Descriptor)
{
    return RdCopyIsoDirectory(Source,
                              LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_EXTENT),
                              LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_LENGTH),
                              "");
}

static
BOOLEAN
RdBuildFromWim(
    _In_ PRD_SOURCE File,
    _In_opt_ PRD_SOURCE Medium,
    _In_reads_bytes_opt_(ISO_BLOCK_SIZE) const UCHAR* Descriptor,
    _In_ PCSTR FileName,
    _In_ ULONGLONG ExtraBytes)
{
    WIM_LOADER_IMAGE_STATS Stats;
    WIM_LOADER_CTX Wim;
    ULONGLONG Payload;
    BOOLEAN Success = FALSE;

    if (WimLoaderOpen(File->Memory, File->Size, &Wim) != ESUCCESS)
        return FALSE;

    if (WimLoaderSelectImage(&Wim, Wim.BootIndex ? Wim.BootIndex : 1) == ESUCCESS &&
        WimLoaderQuerySelectedImageStats(&Wim, &Stats) == ESUCCESS)
    {
        Payload = Stats.FileBytes;
        if (Medium && Medium->Size > File->Size)
            Payload += Medium->Size - File->Size;

        Success = RdCreateVolume(Payload, ExtraBytes) &&
                  WimLoaderExtractToFat(&Wim, &RdWriter, RdWimProgress, NULL) == ESUCCESS;
    }

    WimLoaderClose(&Wim);

    if (Success && Medium)
    {
        RdSkipRootName = FileName;
        Success = RdCopyIso(Medium, Descriptor);
        RdSkipRootName = NULL;
    }

    return Success;
}

BOOLEAN
OslBuildRamdisk(
    _In_ PBCD_DEVICE BcdDevice,
    _In_reads_bytes_opt_(ImageSize) PVOID Image,
    _In_ ULONG ImageSize,
    _In_ ULONGLONG ExtraBytes,
    _Out_ PDK_DEVICE Device)
{
    UCHAR Descriptor[ISO_BLOCK_SIZE];
    CHAR FileName[RD_NAME_CHARS];
    BCD_DEVICE Parent = *BcdDevice;
    DK_DEVICE ParentDevice;
    RD_SOURCE Medium, File;
    PCWSTR Path = Image ? L"\\" : BcdDevice->RamdiskPath;
    PCWSTR Leaf;
    ULONG FileSize = 0;
    ULONG i;
    BOOLEAN HaveMedium = FALSE;
    BOOLEAN Success;

    RtlZeroMemory(&Medium, sizeof(Medium));
    RtlZeroMemory(&File, sizeof(File));
    Parent.Ramdisk = FALSE;
    RdTotalBytes = MAXULONGLONG;

    if (Image)
    {
        File.Memory = Image;
        File.Size = ImageSize;
    }
    else if (Parent.Type == BcdDeviceBoot && FwGetBootMedium(&Medium.Device))
    {
        Medium.Size = Medium.Device.Length;
        HaveMedium = RdOpenIso(&Medium, Descriptor);
    }

    if (!Image && Path[0] != UNICODE_NULL)
    {
        if (Parent.Type == BcdDeviceBoot)
            File.Memory = FwReadBootFile(Path, &FileSize);
        else if (DkOpenBcdDevice(&Parent, &ParentDevice))
            File.Memory = FsReadFile(&ParentDevice, Path, &FileSize);

        File.Size = FileSize;
        if (File.Memory)
            HaveMedium = FALSE;
        else if (!HaveMedium || !RdLoadIsoFile(&Medium, Descriptor, Path, &File))
            File.Size = 0;
    }

    if (Path[0] == UNICODE_NULL && !HaveMedium && DkOpenBcdDevice(&Parent, Device))
        return TRUE;

    if (Path[0] == UNICODE_NULL ? !HaveMedium : File.Size == 0)
    {
        FwPrint(L"The RAM disk source was not found.\r\n");
        return FALSE;
    }

    if (Path[0] == UNICODE_NULL)
    {
        Success = RdCreateVolume(Medium.Size, ExtraBytes) && RdCopyIso(&Medium, Descriptor);
    }
    else if (File.Size > sizeof(RdWimMagic) &&
             RtlEqualMemory(File.Memory, RdWimMagic, sizeof(RdWimMagic)))
    {
        for (Leaf = Path, i = 0; Path[i] != UNICODE_NULL; i++)
        {
            if (Path[i] == L'\\')
                Leaf = &Path[i + 1];
        }

        for (i = 0; Leaf[i] != UNICODE_NULL && i < RD_NAME_CHARS - 1; i++)
            FileName[i] = (CHAR)Leaf[i];
        FileName[i] = ANSI_NULL;

        Success = RdBuildFromWim(&File,
                                 HaveMedium ? &Medium : NULL,
                                 HaveMedium ? Descriptor : NULL,
                                 FileName,
                                 ExtraBytes);
    }
    else
    {
        FileSize = (ULONG)File.Size;
        Success = RdOpenIso(&File, Descriptor) &&
                  RdCreateVolume(File.Size, ExtraBytes) &&
                  RdCopyIso(&File, Descriptor);
        File.Size = FileSize;
    }

    FwPrint(L"\r                      \r");

    if (File.Memory)
        FwFreePages(File.Memory, (SIZE_T)File.Size);

    if (!Success)
    {
        FwPrint(L"The RAM disk could not be built from its source.\r\n");
        return FALSE;
    }

    RtlZeroMemory(Device, sizeof(*Device));
    Device->Memory = RdBase;
    Device->Offset = RdWriter.VolumeBase - RdBase;
    Device->Length = RdWriter.VolumeSize;
    Device->BlockSize = RD_SECTOR_SIZE;
    FsMemoryReader = RdReadFile;
    return TRUE;
}

BOOLEAN
OslGetRamdisk(
    _Out_ PUCHAR* Base,
    _Out_ PULONGLONG Size)
{
    *Base = RdBase;
    *Size = RdSize;
    return RdBase != NULL;
}
