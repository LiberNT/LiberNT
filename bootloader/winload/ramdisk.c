/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Writable RAM disk built from the boot medium or an image file
 * COPYRIGHT:   Copyright 2025-2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"
#include <ramdisk.h>
#include <libwim/wimboot.h>
#include <ntfsbl.h>

#define RD_SECTOR_SIZE        512
#define RD_MAX_SIZE           0xFFFFF000ULL
#define RD_MINIMUM_EXTRA      (64ULL * 1024 * 1024)
#define RD_VOLUME_START       (1024ULL * 1024)
#define RD_VOLUME_FIXED       (16ULL * 1024 * 1024)
#define RD_VOLUME_ENTRY       2048
#define RD_VOLUME_SLACK       (8ULL * 1024 * 1024)
#define RD_COPY_CHUNK         (1024 * 1024)
#define RD_OVERLAY_BYTES      4096
#define RD_MAX_CLUSTER        (128 * RD_SECTOR_SIZE)
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

#define MBR_SIGNATURE         0x1FE
#define MBR_TABLE             0x1BE
#define MBR_ENTRY_SIZE        16
#define MBR_ENTRY_COUNT       4
#define MBR_ENTRY_TYPE        4
#define MBR_ENTRY_START       8
#define MBR_ENTRY_SECTORS     12
#define MBR_DISK_SIGNATURE    0x1B8
#define MBR_ACTIVE            0x80
#define MBR_TYPE_NTFS         0x07

#define RD_OVERLAY_DIRECTORY  "System Volume Information"
#define RD_OVERLAY_PATH       "System Volume Information/WimOverlay.dat"

typedef struct _RD_SOURCE
{
    PUCHAR Memory;
    DK_DEVICE Device;
    ULONGLONG Size;
} RD_SOURCE, *PRD_SOURCE;

typedef struct _RD_WIM_BUILD
{
    const WIMBOOT_IMAGE* Image;
    ULONG Entries;
    NTSTATUS Status;
} RD_WIM_BUILD, *PRD_WIM_BUILD;

static FAT32_WRITER RdWriter;
static DK_DEVICE RdNtfsDevice;
static BOOLEAN RdNtfsVolume;
static PUCHAR RdBase;
static PUCHAR RdMoveSource;
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

    if (Percent / 10 != RdPercent / 10)
        OslTrace("RAM disk: %lu%% of %I64u MB", Percent, RdTotalBytes >> 20);
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
RdCreateDirectory(
    _In_ PCSTR Path)
{
    ASSERT(Path[0] == '/' && Path[1] != ANSI_NULL);

    if (RdNtfsVolume)
        return NT_SUCCESS(NtfsBlCreateDirectory(Path + 1));

    return Fat32CreateDirectory(&RdWriter, Path);
}

static
BOOLEAN
RdCopyFile(
    _In_ PRD_SOURCE Source,
    _In_ PCSTR Path,
    _In_ ULONGLONG Offset,
    _In_ ULONG Size)
{
    PUCHAR Data = NULL;
    BOOLEAN Success;

    ASSERT(Path[0] == '/' && Path[1] != ANSI_NULL);

    if (!RdNtfsVolume)
    {
        return Fat32CreateFileEx(&RdWriter, Path, Size, &Data) &&
               (Size == 0 || RdRead(Source, Offset, Data, Size));
    }

    if (Size != 0)
    {
        Data = FwAllocatePages(Size);
        if (!Data)
            return FALSE;
    }

    Success = (Size == 0 || RdRead(Source, Offset, Data, Size)) &&
              NT_SUCCESS(NtfsBlWriteFile(Path + 1, Data, Size));
    if (Data)
        FwFreePages(Data, Size);
    return Success;
}

static
BOOLEAN
RdCopyIsoDirectory(
    _In_ PRD_SOURCE Source,
    _In_ ULONG Block,
    _In_ ULONG Length,
    _In_ PCSTR Path,
    _Inout_opt_ PULONGLONG Measure)
{
    CHAR Name[RD_NAME_CHARS];
    CHAR Child[RD_PATH_CHARS];
    PUCHAR Directory, Record;
    ULONG Offset = 0;
    ULONG Size;
    BOOLEAN Success = FALSE;

    Directory = FwAllocate(Length);
    if (!Directory)
        return FALSE;

    if (!RdRead(Source, (ULONGLONG)Block * ISO_BLOCK_SIZE, Directory, Length))
        goto Done;

    if (Measure)
        *Measure += ALIGN_UP_BY((ULONGLONG)Length * 4, RD_MAX_CLUSTER);

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
            if ((!Measure && !RdCreateDirectory(Child)) ||
                !RdCopyIsoDirectory(Source, LeUlong(Record + ISO_RECORD_EXTENT), Size, Child, Measure))
            {
                goto Done;
            }
        }
        else if (Measure)
        {
            *Measure += ALIGN_UP_BY((ULONGLONG)Size, RD_MAX_CLUSTER);
        }
        else if (!RdCopyFile(Source,
                             Child,
                             (ULONGLONG)LeUlong(Record + ISO_RECORD_EXTENT) * ISO_BLOCK_SIZE,
                             Size))
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
PVOID
RdReadNtfsFile(
    PCSTR Path,
    PULONG Size)
{
    ULONGLONG FileSize;
    BOOLEAN IsDirectory;
    PVOID File;
    PVOID Data = NULL;
    ULONG Read;

    while (*Path == '\\' || *Path == '/')
        Path++;

    if (!NT_SUCCESS(NtfsBlOpenFile(Path, &File, &FileSize, &IsDirectory)))
        return NULL;

    if (!IsDirectory && FileSize != 0 && FileSize <= MAXULONG)
    {
        Data = FwAllocatePages((SIZE_T)FileSize);
        if (Data &&
            (!NT_SUCCESS(NtfsBlReadFileAt(File, 0, Data, (ULONG)FileSize, &Read)) || Read != FileSize))
        {
            FwFreePages(Data, (SIZE_T)FileSize);
            Data = NULL;
        }
    }

    NtfsBlCloseFile(File);
    if (Data)
        *Size = (ULONG)FileSize;
    return Data;
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
    UCHAR Magic[sizeof(RdWimMagic)];
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

    /* A WIM is read from the medium while its volume is built, never as a whole */
    if (Length > sizeof(Magic) &&
        RdRead(Medium, (ULONGLONG)Block * ISO_BLOCK_SIZE, Magic, sizeof(Magic)) &&
        RtlEqualMemory(Magic, RdWimMagic, sizeof(Magic)))
    {
        File->Device = Medium->Device;
        File->Device.Offset += (ULONGLONG)Block * ISO_BLOCK_SIZE;
        File->Device.Length = Length;
        return TRUE;
    }

    File->Memory = FwAllocatePages(Length);
    if (!File->Memory)
        return FALSE;

    return RdRead(Medium, (ULONGLONG)Block * ISO_BLOCK_SIZE, File->Memory, Length);
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
RdFindImagePartition(
    _In_ const RD_SOURCE* File,
    _Out_ PULONGLONG Offset,
    _Out_ PULONGLONG Length)
{
    const UCHAR* Entry;
    ULONGLONG Start, Sectors;
    ULONG i;

    if (!File->Memory || File->Size < RD_SECTOR_SIZE ||
        File->Memory[MBR_SIGNATURE] != 0x55 || File->Memory[MBR_SIGNATURE + 1] != 0xAA)
    {
        return FALSE;
    }

    for (i = 0; i < MBR_ENTRY_COUNT; i++)
    {
        Entry = File->Memory + MBR_TABLE + i * MBR_ENTRY_SIZE;
        Start = (ULONGLONG)LeUlong(Entry + MBR_ENTRY_START) * RD_SECTOR_SIZE;
        Sectors = LeUlong(Entry + MBR_ENTRY_SECTORS);
        if (Entry[MBR_ENTRY_TYPE] == 0 || Sectors == 0)
            continue;

        if (Start > File->Size || Sectors * RD_SECTOR_SIZE > File->Size - Start)
            return FALSE;

        *Offset = Start;
        *Length = Sectors * RD_SECTOR_SIZE;
        return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
RdIsDiskImage(
    _In_ const RD_SOURCE* File,
    _Out_ PULONGLONG Offset,
    _Out_ PULONGLONG Length)
{
    UCHAR Descriptor[ISO_BLOCK_SIZE];
    RD_SOURCE Probe = *File;

    if (RdOpenIso(&Probe, Descriptor))
        return FALSE;

    return RdFindImagePartition(File, Offset, Length);
}

static
BOOLEAN
RdAdoptImage(
    _Inout_ PRD_SOURCE File)
{
    RdSize = ALIGN_UP_BY(File->Size, MM_PAGE_SIZE);
    if (RdSize > RD_MAX_SIZE)
    {
        FwPrint(L"The RAM disk is larger than 4 GB.\r\n");
        return FALSE;
    }

    if ((ULONG_PTR)File->Memory + RdSize > ((ULONGLONG)MmArchMaxRamdiskPage << MM_PAGE_SHIFT))
    {
        FwPrint(L"The RAM disk image is outside the RAM disk address range.\r\n");
        return FALSE;
    }

    RdBase = File->Memory;
    RtlZeroMemory(RdBase + File->Size, (SIZE_T)(RdSize - File->Size));
    MmSetMemoryType(RdBase, (SIZE_T)RdSize, LoaderXIPRom);
    File->Memory = NULL;
    return TRUE;
}

static
BOOLEAN
RdCopyIso(
    _In_ PRD_SOURCE Source,
    _In_reads_bytes_(ISO_BLOCK_SIZE) const UCHAR* Descriptor,
    _Inout_opt_ PULONGLONG Measure)
{
    return RdCopyIsoDirectory(Source,
                              LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_EXTENT),
                              LeUlong(Descriptor + ISO_ROOT_RECORD + ISO_RECORD_LENGTH),
                              "",
                              Measure);
}

static
BOOLEAN
RdMountNtfs(
    _In_ PUCHAR Memory,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length)
{
    RtlZeroMemory(&RdNtfsDevice, sizeof(RdNtfsDevice));
    RdNtfsDevice.Memory = Memory;
    RdNtfsDevice.Offset = Offset;
    RdNtfsDevice.Length = Length;
    RdNtfsDevice.BlockSize = RD_SECTOR_SIZE;
    return NT_SUCCESS(NtfsBlMount(&RdNtfsDevice));
}

static
PVOID
RdWimAllocate(
    _In_ size_t Size,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return FwAllocate(Size);
}

static
VOID
RdWimFree(
    _In_opt_ PVOID Pointer,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    if (Pointer)
        FwFree(Pointer);
}

static
int
RdWimRead(
    void* Context,
    uint64_t Offset,
    void* Buffer,
    size_t Size)
{
    return RdRead((PRD_SOURCE)Context, Offset, Buffer, Size) ? 0 : -1;
}

static
int
RdWimCount(
    void* Context,
    const WIMBOOT_ENTRY* Entry)
{
    UNREFERENCED_PARAMETER(Entry);
    ((PRD_WIM_BUILD)Context)->Entries++;
    return 0;
}

static
int
RdWimCreate(
    void* Context,
    const WIMBOOT_ENTRY* Entry)
{
    PRD_WIM_BUILD Build = Context;
    UCHAR Reparse[WIMBOOT_POINTER_SIZE];

    if (Entry->IsDirectory)
    {
        Build->Status = NtfsBlCreateDirectory(Entry->Path);
    }
    else
    {
        if (Entry->Size != 0)
            WimBootPointer(Reparse, Build->Image, Entry);
        Build->Status = NtfsBlCreateWimFile(Entry->Path,
                                            Entry->Attributes,
                                            Entry->Size,
                                            Entry->Size != 0 ? Reparse : NULL,
                                            Entry->Size != 0 ? sizeof(Reparse) : 0);
    }

    if (!NT_SUCCESS(Build->Status))
    {
        OslTrace("RAM disk: %s could not be created (0x%lx)", Entry->Path, Build->Status);
        return 1;
    }
    return 0;
}

static
BOOLEAN
RdIsWim(
    _In_ PRD_SOURCE File)
{
    UCHAR Magic[sizeof(RdWimMagic)];

    if (File->Size <= sizeof(Magic))
        return FALSE;
    if (File->Memory)
        return RtlEqualMemory(File->Memory, RdWimMagic, sizeof(Magic));
    return RdRead(File, 0, Magic, sizeof(Magic)) && RtlEqualMemory(Magic, RdWimMagic, sizeof(Magic));
}

static
VOID
RdWriteMbr(
    _Out_writes_bytes_(RD_SECTOR_SIZE) PUCHAR Sector,
    _In_ ULONGLONG VolumeBytes,
    _In_ ULONG Signature)
{
    PUCHAR Entry = Sector + MBR_TABLE;
    ULONG Start = (ULONG)(RD_VOLUME_START / RD_SECTOR_SIZE);
    ULONG Sectors = (ULONG)(VolumeBytes / RD_SECTOR_SIZE);
    ULONG i;

    ASSERT(VolumeBytes / RD_SECTOR_SIZE <= MAXULONG);

    RtlZeroMemory(Sector, RD_SECTOR_SIZE);
    for (i = 0; i < sizeof(ULONG); i++)
    {
        Sector[MBR_DISK_SIGNATURE + i] = (UCHAR)(Signature >> (8 * i));
        Entry[MBR_ENTRY_START + i] = (UCHAR)(Start >> (8 * i));
        Entry[MBR_ENTRY_SECTORS + i] = (UCHAR)(Sectors >> (8 * i));
    }
    Entry[0] = MBR_ACTIVE;
    Entry[MBR_ENTRY_TYPE] = MBR_TYPE_NTFS;
    Sector[MBR_SIGNATURE] = 0x55;
    Sector[MBR_SIGNATURE + 1] = 0xAA;
}

static
BOOLEAN
RdStreamWim(
    _In_ PRD_SOURCE File,
    _In_ PCSTR Path)
{
    ULONGLONG Done;
    PUCHAR Chunk;
    PVOID Handle = NULL;
    ULONG Bytes;
    BOOLEAN Success;

    Chunk = FwAllocate(RD_COPY_CHUNK);
    Success = Chunk != NULL &&
              NT_SUCCESS(NtfsBlWriteFile(Path, NULL, 0)) &&
              NT_SUCCESS(NtfsBlOpenFile(Path, &Handle, NULL, NULL));

    for (Done = 0; Success && Done < File->Size; Done += Bytes)
    {
        Bytes = (ULONG)min(File->Size - Done, (ULONGLONG)RD_COPY_CHUNK);
        Success = RdRead(File, Done, Chunk, Bytes) &&
                  NT_SUCCESS(NtfsBlWriteFileAt(Handle, Done, Chunk, Bytes));
    }

    NtfsBlCloseFile(Handle);
    if (Chunk)
        FwFree(Chunk);
    return Success;
}

static
BOOLEAN
RdRegisterWim(
    _In_ const WIMBOOT_IMAGE* Image,
    _In_ PCSTR FileName,
    _In_ ULONG Signature)
{
    WCHAR Path[RD_NAME_CHARS + 1];
    PUCHAR Overlay;
    ULONG Length;
    ULONG i;
    BOOLEAN Success;

    Path[0] = L'\\';
    for (i = 0; FileName[i] != ANSI_NULL && i < RD_NAME_CHARS - 1; i++)
        Path[i + 1] = (WCHAR)(UCHAR)FileName[i];
    Path[i + 1] = UNICODE_NULL;

    Overlay = FwAllocate(RD_OVERLAY_BYTES);
    if (!Overlay)
        return FALSE;

    Length = WimBootOverlay(Overlay, RD_OVERLAY_BYTES, Image, (const uint16_t*)Path, RD_VOLUME_START, Signature);
    Success = Length != 0 &&
              NT_SUCCESS(NtfsBlCreateDirectory(RD_OVERLAY_DIRECTORY)) &&
              NT_SUCCESS(NtfsBlWriteFile(RD_OVERLAY_PATH, Overlay, Length));
    FwFree(Overlay);
    return Success;
}

static
BOOLEAN
RdBuildWimVolume(
    _In_ PRD_SOURCE File,
    _In_opt_ PRD_SOURCE Medium,
    _In_reads_bytes_opt_(ISO_BLOCK_SIZE) const UCHAR* Descriptor,
    _In_ PCSTR FileName,
    _Out_ PULONGLONG Length)
{
    WimCoreAllocator Allocator = { RdWimAllocate, RdWimFree, NULL };
    RD_WIM_BUILD Build;
    WIMBOOT_IMAGE Image;
    WimCoreCtx Wim;
    ULONGLONG Payload = 0;
    ULONGLONG VolumeBytes;
    ULONG Signature;
    BOOLEAN Success = FALSE;

    RtlZeroMemory(&Build, sizeof(Build));
    wimcore_init(&Wim);
    if ((File->Memory
            ? wimcore_open_memory(&Wim, &Allocator, File->Memory, File->Size)
            : wimcore_open(&Wim, &Allocator, RdWimRead, File, File->Size)) != 0 ||
        WimBootOpen(&Wim, &Image) != WIMBOOT_OK ||
        WimBootWalk(&Wim, &Image, RdWimCount, &Build) != WIMBOOT_OK)
    {
        FwPrint(L"The WIM image cannot be used.\r\n");
        goto Done;
    }

    RdSkipRootName = FileName;
    if (Medium && !RdCopyIso(Medium, Descriptor, &Payload))
        goto Done;

    VolumeBytes = ALIGN_UP_BY(File->Size, RD_MAX_CLUSTER) + Payload + RD_VOLUME_FIXED +
                  (ULONGLONG)Build.Entries * RD_VOLUME_ENTRY + RD_VOLUME_SLACK;
    VolumeBytes = ALIGN_UP_BY(VolumeBytes, MM_PAGE_SIZE);
    if (VolumeBytes > RD_MAX_SIZE - RD_VOLUME_START)
    {
        FwPrint(L"The RAM disk is larger than 4 GB.\r\n");
        goto Done;
    }

    RdSize = RD_VOLUME_START + VolumeBytes;
    RdBase = MmAllocateMemoryWithType((SIZE_T)RdSize, LoaderXIPRom);
    if (!RdBase)
        RdBase = MmAllocateHighMemoryWithType((SIZE_T)RdSize, LoaderXIPRom);
    if (!RdBase)
    {
        FwPrint(L"Not enough memory for a %I64u MB RAM disk.\r\n", RdSize >> 20);
        goto Done;
    }
    RtlZeroMemory(RdBase, (SIZE_T)RdSize);

    Signature = LeUlong(Image.Guid) | 1;
    RdWriteMbr(RdBase, VolumeBytes, Signature);
    if (!NT_SUCCESS(NtfsBlFormatMemory(RdBase + RD_VOLUME_START,
                                       VolumeBytes,
                                       (ULONG)(RD_VOLUME_START / RD_SECTOR_SIZE),
                                       L"LiberNT")) ||
        !RdMountNtfs(RdBase, RD_VOLUME_START, VolumeBytes))
    {
        goto Done;
    }

    RdTotalBytes = max(File->Size + Payload, 1);
    RdCopiedBytes = 0;
    RdPercent = 0;
    Build.Image = &Image;
    Build.Status = STATUS_SUCCESS;
    if (!RdStreamWim(File, FileName) ||
        !RdRegisterWim(&Image, FileName, Signature) ||
        WimBootWalk(&Wim, &Image, RdWimCreate, &Build) != WIMBOOT_OK)
    {
        goto Done;
    }

    if (Medium)
    {
        RdNtfsVolume = TRUE;
        Success = RdCopyIso(Medium, Descriptor, NULL);
        RdNtfsVolume = FALSE;
        if (!Success)
            goto Done;
    }

    /* The volume loads the WIM it backs its files with when it opens */
    Success = NT_SUCCESS(NtfsBlUnmount()) && RdMountNtfs(RdBase, RD_VOLUME_START, VolumeBytes);
    *Length = VolumeBytes;

Done:
    RdSkipRootName = NULL;
    wimcore_close(&Wim);
    OslTrace("RAM disk: %lu WIM entries, %I64u MB of medium files", Build.Entries, Payload >> 20);
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
    ULONGLONG Payload;
    ULONGLONG ImageOffset = 0, ImageLength = 0;
    ULONG FileSize = 0;
    ULONG i;
    BOOLEAN HaveMedium = FALSE;
    BOOLEAN DiskImage = FALSE;
    BOOLEAN WimVolume = FALSE;
    BOOLEAN Success;

    RtlZeroMemory(&Medium, sizeof(Medium));
    RtlZeroMemory(&File, sizeof(File));
    Parent.Ramdisk = FALSE;
    RdTotalBytes = MAXULONGLONG;

    for (Leaf = Path, i = 0; Path[i] != UNICODE_NULL; i++)
    {
        if (Path[i] == L'\\')
            Leaf = &Path[i + 1];
    }

    for (i = 0; Leaf[i] != UNICODE_NULL && i < RD_NAME_CHARS - 1; i++)
        FileName[i] = (CHAR)Leaf[i];
    FileName[i] = ANSI_NULL;

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
        OslTrace("RAM disk: reading %ls", Path);
        if (Parent.Type == BcdDeviceBoot)
            File.Memory = FwReadBootFile(Path, &FileSize);
        else if (DkOpenBcdDevice(&Parent, &ParentDevice))
            File.Memory = FsReadFile(&ParentDevice, Path, &FileSize);

        File.Size = FileSize;
        OslTrace("RAM disk: %ls is %lu bytes", Path, FileSize);
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
        Payload = 0;
        Success = RdCopyIso(&Medium, Descriptor, &Payload) &&
                  RdCreateVolume(Payload, ExtraBytes) &&
                  RdCopyIso(&Medium, Descriptor, NULL);
    }
    else if (RdIsWim(&File))
    {
        DiskImage = TRUE;
        WimVolume = TRUE;
        ImageOffset = RD_VOLUME_START;
        Success = RdBuildWimVolume(&File,
                                   HaveMedium ? &Medium : NULL,
                                   HaveMedium ? Descriptor : NULL,
                                   FileName,
                                   &ImageLength);
    }
    else if (RdIsDiskImage(&File, &ImageOffset, &ImageLength))
    {
        DiskImage = TRUE;
        Success = RdAdoptImage(&File);
    }
    else
    {
        FileSize = (ULONG)File.Size;
        Payload = 0;
        Success = RdOpenIso(&File, Descriptor) &&
                  RdCopyIso(&File, Descriptor, &Payload) &&
                  RdCreateVolume(Payload, ExtraBytes) &&
                  RdCopyIso(&File, Descriptor, NULL);
        File.Size = FileSize;
    }

    FwPrint(L"\r                      \r");
    OslTrace("RAM disk: %s, %I64u MB", Success ? "built" : "failed", RdSize >> 20);

    if (File.Memory)
        FwFreePages(File.Memory, (SIZE_T)File.Size);

    if (!Success)
    {
        FwPrint(L"The RAM disk could not be built from its source.\r\n");
        return FALSE;
    }

    RtlZeroMemory(Device, sizeof(*Device));
    Device->Memory = RdBase;
    Device->BlockSize = RD_SECTOR_SIZE;
    if (DiskImage)
    {
        Device->Offset = ImageOffset;
        Device->Length = ImageLength;
        FsMemoryReader = WimVolume ? RdReadNtfsFile : NULL;
        FsMemoryDriverName = L"ntfs";
    }
    else
    {
        Device->Offset = RdWriter.VolumeBase - RdBase;
        Device->Length = RdWriter.VolumeSize;
        FsMemoryReader = RdReadFile;
        FsMemoryDriverName = L"fastfat";
    }
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

BOOLEAN
OslPlanRamdiskMove(
    _In_ ULONGLONG LowestAddress)
{
    PPAGE_LOOKUP_TABLE_ITEM Table;
    PFN_NUMBER Count, Pages, Page, Lowest, Limit, Run = 0;
    PUCHAR Target;

    if (!RdBase || (ULONGLONG)(ULONG_PTR)RdBase >= LowestAddress)
        return FALSE;

    Table = MmGetMemoryMap(&Count);
    if (!Table || Count == 0)
        return FALSE;

    Pages = (PFN_NUMBER)(RdSize >> MM_PAGE_SHIFT);
    Lowest = max((PFN_NUMBER)(LowestAddress >> MM_PAGE_SHIFT), MmLowestPhysicalPage);
    Limit = min(MmLowestPhysicalPage + Count, MmArchMaxRamdiskPage);
    for (Page = Limit; Page > Lowest; Page--)
    {
        if (Table[Page - 1 - MmLowestPhysicalPage].PageAllocated != LoaderFirmwareTemporary)
        {
            Run = 0;
            continue;
        }
        if (++Run < Pages)
            continue;

        Target = (PUCHAR)(ULONG_PTR)((ULONGLONG)(Page - 1) << MM_PAGE_SHIFT);
        MmSetMemoryType(RdBase, (SIZE_T)RdSize, LoaderFree);
        MmSetMemoryType(Target, (SIZE_T)RdSize, LoaderXIPRom);
        RdMoveSource = RdBase;
        RdBase = Target;
        return TRUE;
    }
    return FALSE;
}

VOID
OslFinishRamdiskMove(VOID)
{
    if (!RdMoveSource)
        return;

    RtlCopyMemory(RdBase, RdMoveSource, (SIZE_T)RdSize);
    RdMoveSource = NULL;
}
