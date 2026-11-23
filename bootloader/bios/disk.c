/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PC/AT BIOS disk access through the INT 13h extensions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <bios.h>

#define BIOS_MAX_DISKS          16
#define BIOS_BDA_DISK_COUNT     0x475
#define BIOS_FIRST_HARD_DISK    0x80
#define BIOS_DISK_RETRIES       3
#define BIOS_SECTOR             512
#define BIOS_CDROM_SECTOR       2048
#define BIOS_DAP_SIZE           0x10
#define BIOS_PARAMETERS_SIZE    0x1E
#define BIOS_MBR_TABLE          0x1BE
#define BIOS_MBR_ENTRY_SIZE     16
#define BIOS_MBR_PARTITIONS     4
#define BIOS_MBR_SIGNATURE      0x1B8
#define BIOS_DAP_OFFSET         0x100
#define BIOS_PARAMETERS_OFFSET  0x200

typedef struct _BIOS_DISK
{
    UCHAR Drive;
    BOOLEAN Cdrom;
    ULONG BlockSize;
    ULONGLONG BlockCount;
} BIOS_DISK, *PBIOS_DISK;

static BIOS_DISK BiosDisks[BIOS_MAX_DISKS];
static ULONG BiosDiskCount;

static
BOOLEAN
BiosProbeDisk(
    _In_ UCHAR Drive,
    _In_ BOOLEAN Cdrom)
{
    PUCHAR Parameters = (PUCHAR)(ULONG_PTR)(BIOS_TRANSFER_BASE + BIOS_PARAMETERS_OFFSET);
    BIOS_REGISTERS Registers;
    PBIOS_DISK Disk;
    ULONG i;

    for (i = 0; i < BiosDiskCount; i++)
    {
        if (BiosDisks[i].Drive == Drive)
            return TRUE;
    }

    if (BiosDiskCount >= BIOS_MAX_DISKS)
        return FALSE;

    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4100;
    Registers.Ebx = 0x55AA;
    Registers.Edx = Drive;
    BiosCall(0x13, &Registers);
    if ((Registers.Eflags & BIOS_CARRY_FLAG) || (Registers.Ebx & 0xFFFF) != 0xAA55 || !(Registers.Ecx & 0x1))
    {
        /* TODO: read drives without the INT 13h extensions through CHS addressing */
        return FALSE;
    }

    RtlZeroMemory(Parameters, BIOS_PARAMETERS_SIZE);
    *(PUSHORT)Parameters = BIOS_PARAMETERS_SIZE;
    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4800;
    Registers.Edx = Drive;
    Registers.Ds = BiosSegment(Parameters);
    Registers.Esi = BiosOffset(Parameters);
    BiosCall(0x13, &Registers);

    Disk = &BiosDisks[BiosDiskCount];
    RtlZeroMemory(Disk, sizeof(*Disk));
    Disk->Drive = Drive;
    Disk->Cdrom = Cdrom;
    Disk->BlockSize = Cdrom ? BIOS_CDROM_SECTOR : BIOS_SECTOR;

    if (!(Registers.Eflags & BIOS_CARRY_FLAG) && (Registers.Eax & 0xFF00) == 0)
    {
        Disk->BlockCount = LeUlonglong(Parameters + 0x10);
        i = Parameters[0x18] | (Parameters[0x19] << 8);
        if (i != 0 && (i & (i - 1)) == 0 && i <= BIOS_TRANSFER_DATA_SIZE)
            Disk->BlockSize = i;
        if (Disk->BlockCount == MAXULONGLONG)
            Disk->BlockCount = 0;
    }

    BiosDiskCount++;
    return TRUE;
}

static
VOID
BiosScanDisks(VOID)
{
    UCHAR Count = *(volatile UCHAR*)BIOS_BDA_DISK_COUNT;
    UCHAR Drive;

    BiosDiskCount = 0;

    for (Drive = BIOS_FIRST_HARD_DISK; Drive < BIOS_FIRST_HARD_DISK + Count && Drive < 0xFF; Drive++)
        BiosProbeDisk(Drive, FALSE);

    if (BiosStartup->BootDrive >= BIOS_FIRST_HARD_DISK)
        BiosProbeDisk((UCHAR)BiosStartup->BootDrive, BiosStartup->BootMedium == BIOS_MEDIUM_CDROM);
}

static
BOOLEAN
BiosTransfer(
    _In_ PBIOS_DISK Disk,
    _In_ BOOLEAN Write,
    _In_ ULONGLONG Block,
    _In_ ULONG Count)
{
    PUCHAR Dap = (PUCHAR)(ULONG_PTR)(BIOS_TRANSFER_BASE + BIOS_DAP_OFFSET);
    PVOID Data = (PVOID)(ULONG_PTR)BIOS_TRANSFER_DATA;
    BIOS_REGISTERS Registers;
    ULONG Retry;

    ASSERT(Count != 0 && Count * Disk->BlockSize <= BIOS_TRANSFER_DATA_SIZE);

    for (Retry = 0; Retry < BIOS_DISK_RETRIES; Retry++)
    {
        RtlZeroMemory(Dap, BIOS_DAP_SIZE);
        Dap[0] = BIOS_DAP_SIZE;
        *(PUSHORT)(Dap + 2) = (USHORT)Count;
        *(PUSHORT)(Dap + 4) = BiosOffset(Data);
        *(PUSHORT)(Dap + 6) = BiosSegment(Data);
        *(PULONGLONG)(Dap + 8) = Block;

        RtlZeroMemory(&Registers, sizeof(Registers));
        Registers.Eax = Write ? 0x4300 : 0x4200;
        Registers.Edx = Disk->Drive;
        Registers.Ds = BiosSegment(Dap);
        Registers.Esi = BiosOffset(Dap);
        BiosCall(0x13, &Registers);

        if (!(Registers.Eflags & BIOS_CARRY_FLAG) && (Registers.Eax & 0xFF00) == 0)
            return TRUE;

        RtlZeroMemory(&Registers, sizeof(Registers));
        Registers.Edx = Disk->Drive;
        BiosCall(0x13, &Registers);
    }

    return FALSE;
}

BOOLEAN
FwReadBlocks(
    _In_ PVOID Firmware,
    _In_ ULONGLONG Block,
    _In_ ULONG Count,
    _Out_ PVOID Buffer)
{
    PBIOS_DISK Disk = Firmware;
    PUCHAR Output = Buffer;
    ULONG Chunk, MaxChunk = BIOS_TRANSFER_DATA_SIZE / Disk->BlockSize;

    while (Count != 0)
    {
        Chunk = min(Count, MaxChunk);
        if (!BiosTransfer(Disk, FALSE, Block, Chunk))
            return FALSE;

        RtlCopyMemory(Output, (PVOID)(ULONG_PTR)BIOS_TRANSFER_DATA, Chunk * Disk->BlockSize);
        Output += Chunk * Disk->BlockSize;
        Block += Chunk;
        Count -= Chunk;
    }

    return TRUE;
}

BOOLEAN
FwWriteBlocks(
    _In_ PVOID Firmware,
    _In_ ULONGLONG Block,
    _In_ ULONG Count,
    _In_ PVOID Buffer)
{
    PBIOS_DISK Disk = Firmware;
    PUCHAR Input = Buffer;
    ULONG Chunk, MaxChunk = BIOS_TRANSFER_DATA_SIZE / Disk->BlockSize;

    if (Disk->Cdrom)
        return FALSE;

    while (Count != 0)
    {
        Chunk = min(Count, MaxChunk);
        RtlCopyMemory((PVOID)(ULONG_PTR)BIOS_TRANSFER_DATA, Input, Chunk * Disk->BlockSize);
        if (!BiosTransfer(Disk, TRUE, Block, Chunk))
            return FALSE;

        Input += Chunk * Disk->BlockSize;
        Block += Chunk;
        Count -= Chunk;
    }

    return TRUE;
}

BOOLEAN
FwFlushBlocks(
    _In_ PVOID Firmware)
{
    UNREFERENCED_PARAMETER(Firmware);
    return TRUE;
}

BOOLEAN
FwBufferAligned(
    _In_ PVOID Firmware,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Firmware);
    UNREFERENCED_PARAMETER(Buffer);
    return TRUE;
}

BOOLEAN
FwGetDisk(
    _In_ ULONG Index,
    _Out_ PDK_DEVICE Disk)
{
    if (Index >= BiosDiskCount)
        return FALSE;

    RtlZeroMemory(Disk, sizeof(*Disk));
    Disk->Firmware = &BiosDisks[Index];
    Disk->Length = BiosDisks[Index].BlockCount ? BiosDisks[Index].BlockCount * BiosDisks[Index].BlockSize
                                               : MAXULONGLONG;
    Disk->BlockSize = BiosDisks[Index].BlockSize;
    Disk->DiskIndex = Index;
    return TRUE;
}

BOOLEAN
FwRescanDisks(VOID)
{
    return FALSE;
}

static
BOOLEAN
BiosOpenPartition(
    _In_ ULONG DiskIndex,
    _In_ ULONG PartitionNumber,
    _Out_ PDK_DEVICE Partition)
{
    UCHAR Sector[BIOS_SECTOR];
    PUCHAR Entry;
    DK_DEVICE Disk;
    ULONG Index, Number;

    if (!FwGetDisk(DiskIndex, &Disk))
        return FALSE;

    if (PartitionNumber == 0)
    {
        *Partition = Disk;
        return TRUE;
    }

    if (!DkRead(&Disk, 0, sizeof(Sector), Sector) || Sector[510] != 0x55 || Sector[511] != 0xAA)
        return FALSE;

    for (Index = 0, Number = 0; Index < BIOS_MBR_PARTITIONS; Index++)
    {
        Entry = Sector + BIOS_MBR_TABLE + Index * BIOS_MBR_ENTRY_SIZE;
        if (Entry[4] == 0)
            continue;

        if (++Number != PartitionNumber)
            continue;

        *Partition = Disk;
        Partition->Offset = (ULONGLONG)LeUlong(Entry + 8) * Disk.BlockSize;
        Partition->Length = (ULONGLONG)LeUlong(Entry + 12) * Disk.BlockSize;
        Partition->PartitionNumber = Number;
        return TRUE;
    }

    return FALSE;
}

static
VOID
BiosFindBootDevice(VOID)
{
    UCHAR Sector[BIOS_SECTOR];
    PUCHAR Entry;
    DK_DEVICE Disk;
    ULONG Index, Slot, Number;

    BiosParameters->BootDisk = MAXULONG;
    BiosParameters->BootPartition = 0;

    for (Index = 0; Index < BiosDiskCount; Index++)
    {
        if (BiosDisks[Index].Drive == BiosStartup->BootDrive)
            break;
    }

    if (Index == BiosDiskCount)
        return;

    BiosParameters->BootDisk = Index;
    if (BiosDisks[Index].Cdrom || BiosStartup->PartitionStart == 0)
        return;

    if (!FwGetDisk(Index, &Disk) ||
        !DkRead(&Disk, 0, sizeof(Sector), Sector) ||
        Sector[510] != 0x55 ||
        Sector[511] != 0xAA)
    {
        return;
    }

    for (Slot = 0, Number = 0; Slot < BIOS_MBR_PARTITIONS; Slot++)
    {
        Entry = Sector + BIOS_MBR_TABLE + Slot * BIOS_MBR_ENTRY_SIZE;
        if (Entry[4] == 0)
            continue;

        Number++;
        if (LeUlong(Entry + 8) == BiosStartup->PartitionStart)
        {
            BiosParameters->BootPartition = Number;
            return;
        }
    }

    /* TODO: boot volumes in extended partitions and on GPT disks booted through a protective MBR */
}

BOOLEAN
FwGetBootDevice(
    _Out_ PDK_DEVICE Device)
{
    if (BiosParameters->BootDisk == MAXULONG)
        return FALSE;

    return BiosOpenPartition(BiosParameters->BootDisk, BiosParameters->BootPartition, Device);
}

BOOLEAN
FwGetBootDiskIndex(
    _Out_ PULONG Index)
{
    if (BiosParameters->BootDisk == MAXULONG)
        return FALSE;

    *Index = BiosParameters->BootDisk;
    return TRUE;
}

BOOLEAN
FwGetBootPartition(
    _Out_ PULONG PartitionNumber,
    _Out_writes_bytes_(16) PUCHAR Signature,
    _Out_ PBOOLEAN IsGpt)
{
    UCHAR Sector[BIOS_SECTOR];
    DK_DEVICE Disk;

    if (BiosParameters->BootDisk == MAXULONG ||
        BiosParameters->BootPartition == 0 ||
        !FwGetDisk(BiosParameters->BootDisk, &Disk) ||
        !DkRead(&Disk, 0, sizeof(Sector), Sector))
    {
        return FALSE;
    }

    RtlZeroMemory(Signature, 16);
    RtlCopyMemory(Signature, Sector + BIOS_MBR_SIGNATURE, sizeof(ULONG));
    *PartitionNumber = BiosParameters->BootPartition;
    *IsGpt = FALSE;
    return TRUE;
}

BOOLEAN
FwGetBootMedium(
    _Out_ PDK_DEVICE Device)
{
    if (BiosParameters->BootDisk == MAXULONG)
        return FALSE;

    return FwGetDisk(BiosParameters->BootDisk, Device);
}

PVOID
FwReadBootFile(
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    DK_DEVICE Device;

    *Size = 0;

    if (!FwGetBootDevice(&Device))
        return NULL;

    return FsReadFile(&Device, Path, Size);
}

BOOLEAN
FwWriteBootFile(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ ULONG Size)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Size);
    /* TODO: write files on the BIOS boot volume; until then the starts of an update entry are not counted */
    return FALSE;
}

PVOID
FwReadDeviceFile(
    _In_ PVOID Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Path);
    *Size = 0;
    return NULL;
}

BOOLEAN
FwScanFileVolumes(
    _In_opt_ PCWSTR Path,
    _Inout_ PFW_VOLUME_SCAN Scan,
    _Out_ PVOID* Device)
{
    UNREFERENCED_PARAMETER(Path);
    /* TODO: list the volumes of the BIOS disks for the boot menu's other-system entries */
    Scan->State = FwVolumeScanDone;
    *Device = NULL;
    return FALSE;
}

BOOLEAN
FwFileExists(
    _In_ PVOID Device,
    _In_ PCWSTR Path)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Path);
    return FALSE;
}

BOOLEAN
FwIsUsbVolume(
    _In_ PVOID Device)
{
    UNREFERENCED_PARAMETER(Device);
    return FALSE;
}

BOOLEAN
FwIsBootVolume(
    _In_ PVOID Device)
{
    UNREFERENCED_PARAMETER(Device);
    return FALSE;
}

BOOLEAN
FwIsSameVolume(
    _In_ PVOID First,
    _In_ PVOID Second)
{
    return First == Second;
}

BOOLEAN
FwGetVolumeLabel(
    _In_ PVOID Device,
    _Out_writes_z_(Chars) PWSTR Label,
    _In_ ULONG Chars)
{
    UNREFERENCED_PARAMETER(Device);
    if (Chars)
        Label[0] = UNICODE_NULL;
    return FALSE;
}

BOOLEAN
FwGetVolumeDiskNumber(
    _In_ PVOID Device,
    _Out_ PULONG Number)
{
    UNREFERENCED_PARAMETER(Device);
    *Number = MAXULONG;
    return FALSE;
}

PCWSTR
FwGetRemovableMediaPath(VOID)
{
    return NULL;
}

BOOLEAN
FwStartFirmwareApplication(
    _In_ PVOID Device,
    _In_ PCWSTR Path,
    _In_opt_ PCWSTR LoadOptions)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(LoadOptions);
    return FALSE;
}

PVOID
FwDownloadFile(
    _In_ PCSTR Url,
    _In_opt_ PCSTR Address,
    _In_ BOOLEAN Wait,
    _Out_ PULONG Size)
{
    UNREFERENCED_PARAMETER(Url);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Wait);
    /* TODO: download files through the PXE TFTP services */
    *Size = 0;
    return NULL;
}

VOID
BiosDiskInitialize(
    _In_ BOOLEAN Root)
{
    BiosScanDisks();
    if (Root)
        BiosFindBootDevice();
}
