/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loader parameter block construction
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

#define OSL_SECTOR_SIZE          512
#define OSL_MBR_SIGNATURE_OFFSET 0x1B8
#define OSL_MBR_MAGIC_OFFSET     0x1FE
#define OSL_ARC_NAME_CHARS       64
#define OSL_PROFILE_STATUS       2
#define OSL_RAMDISK_ARC_NAME     "ramdisk(0)"

typedef struct _OSL_ARC_DISK
{
    ARC_DISK_SIGNATURE Signature;
    CHAR ArcName[OSL_ARC_NAME_CHARS];
} OSL_ARC_DISK, *POSL_ARC_DISK;

extern PCM_FRAMEBUF_DEVICE_DATA FrameBufferData;
extern ULONG_PTR VramAddress;
extern ULONG VramSize;
extern BOOLEAN OslAcpiPresent;

ULONG
OslQueryDpi(VOID);

POSL_SYSTEM_BLOCK OslSystemBlock;

PLOADER_PARAMETER_BLOCK
OslAllocateLoaderBlock(VOID)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    PLOADER_PARAMETER_EXTENSION Extension;
    TIME_FIELDS Time;

    OslSystemBlock = MmAllocateMemoryWithType(sizeof(*OslSystemBlock), LoaderSystemBlock);
    if (!OslSystemBlock)
        return NULL;

    RtlZeroMemory(OslSystemBlock, sizeof(*OslSystemBlock));

    LoaderBlock = &OslSystemBlock->LoaderBlock;
    Extension = &OslSystemBlock->Extension;

    LoaderBlock->NlsData = &OslSystemBlock->NlsDataBlock;
    LoaderBlock->Extension = Extension;
    LoaderBlock->OsMajorVersion = (OSL_NT_VERSION >> 8) & 0xFF;
    LoaderBlock->OsMinorVersion = OSL_NT_VERSION & 0xFF;
    LoaderBlock->FirmwareInformation.FirmwareTypeEfi = 1;

    Extension->Size = sizeof(*Extension);
    Extension->BootViaEFI = 1;

    if (FwGetTime(&Time))
        RtlTimeFieldsToTime(&Time, &Extension->SystemTime);

    InitializeListHead(&LoaderBlock->LoadOrderListHead);
    InitializeListHead(&LoaderBlock->MemoryDescriptorListHead);
    InitializeListHead(&LoaderBlock->BootDriverListHead);
    return LoaderBlock;
}

static
VOID
OslAddFirmwareResources(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY ListHead = &LoaderBlock->FirmwareInformation.u.EfiInformation.FirmwareResourceList;
    LOADER_FIRMWARE_RESOURCE Resource;
    PLOADER_FIRMWARE_RESOURCE Entry;
    ULONG Index;

    InitializeListHead(ListHead);
    for (Index = 0; FwGetFirmwareResource(Index, &Resource); Index++)
    {
        Entry = MmHeapAllocate(sizeof(*Entry));
        if (!Entry)
            break;

        *Entry = Resource;
        InsertTailList(ListHead, &Entry->ListEntry);
    }

    OslConvertList(ListHead);
}

static
VOID
OslAddArcDisk(
    _In_reads_bytes_(OSL_SECTOR_SIZE) const UCHAR* Sector,
    _In_opt_ PDK_DEVICE Disk,
    _In_ PCSTR ArcName)
{
    PARC_DISK_INFORMATION Information = &OslSystemBlock->ArcDiskInformation;
    POSL_ARC_DISK ArcDisk;
    ULONG Sum, i;

    ArcDisk = MmHeapAllocate(sizeof(*ArcDisk));
    if (!ArcDisk)
        return;

    for (Sum = 0, i = 0; i < OSL_SECTOR_SIZE; i += sizeof(ULONG))
        Sum += LeUlong(Sector + i);

    RtlStringCbCopyA(ArcDisk->ArcName, sizeof(ArcDisk->ArcName), ArcName);

    ArcDisk->Signature.Signature = LeUlong(Sector + OSL_MBR_SIGNATURE_OFFSET);
    ArcDisk->Signature.CheckSum = 0 - Sum;
    ArcDisk->Signature.ValidPartitionTable = (Sector[OSL_MBR_MAGIC_OFFSET] == 0x55 &&
                                              Sector[OSL_MBR_MAGIC_OFFSET + 1] == 0xAA);
    ArcDisk->Signature.IsGpt = Disk && GptGetDiskId(Disk, (PUCHAR)ArcDisk->Signature.GptSignature);
    ArcDisk->Signature.ArcName = PaToVa(ArcDisk->ArcName);

    InsertTailList(&Information->DiskSignatureListHead, &ArcDisk->Signature.ListEntry);
}

static
VOID
OslAddArcDisks(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARC_DISK_INFORMATION Information = &OslSystemBlock->ArcDiskInformation;
    CHAR ArcName[OSL_ARC_NAME_CHARS];
    UCHAR Sector[OSL_SECTOR_SIZE];
    ULONGLONG RamdiskSize;
    PUCHAR Ramdisk;
    DK_DEVICE Disk;
    ULONG Index;

    InitializeListHead(&Information->DiskSignatureListHead);

    for (Index = 0; FwGetDisk(Index, &Disk); Index++)
    {
        if (!DkRead(&Disk, 0, sizeof(Sector), Sector))
            continue;

        RtlStringCbPrintfA(ArcName, sizeof(ArcName), "multi(0)disk(0)rdisk(%lu)", Index);
        OslAddArcDisk(Sector, &Disk, ArcName);
    }

    if (OslGetRamdisk(&Ramdisk, &RamdiskSize))
        OslAddArcDisk(Ramdisk, NULL, OSL_RAMDISK_ARC_NAME);

    OslConvertList(&Information->DiskSignatureListHead);
    LoaderBlock->ArcDiskInformation = PaToVa(Information);
}

static
VOID
OslSetFrameBuffer(
    _Inout_ PLOADER_PARAMETER_EXTENSION Extension)
{
    if (!FrameBufferData || VramAddress == 0 || VramSize == 0)
        return;

    Extension->GopFramebuffer.FrameBufferBase.QuadPart = VramAddress + FrameBufferData->FrameBufferOffset;
    Extension->GopFramebuffer.FrameBufferSize = VramSize;
    Extension->GopFramebuffer.HorizontalResolution = FrameBufferData->ScreenWidth;
    Extension->GopFramebuffer.VerticalResolution = FrameBufferData->ScreenHeight;
    Extension->GopFramebuffer.PixelsPerScanLine = FrameBufferData->PixelsPerScanLine;
    Extension->GopFramebuffer.PixelFormat = FrameBufferData->BitsPerPixel;
    Extension->GopFramebuffer.RedMask = FrameBufferData->PixelMasks.RedMask;
    Extension->GopFramebuffer.GreenMask = FrameBufferData->PixelMasks.GreenMask;
    Extension->GopFramebuffer.BlueMask = FrameBufferData->PixelMasks.BlueMask;
    Extension->GopFramebuffer.Reserved = FrameBufferData->PixelMasks.ReservedMask;
    Extension->GopFramebuffer.Dpi = OslQueryDpi();

    Extension->GopFramebufferTransform.Size = sizeof(Extension->GopFramebufferTransform);
    Extension->GopFramebufferTransform.Version = LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM_VERSION;
    Extension->GopFramebufferTransform.Flags = FrameBufferData->TransformFlags;
    Extension->GopFramebufferTransform.Rotation = FrameBufferData->Rotation;
    Extension->GopFramebufferTransform.LogicalWidth = FrameBufferData->LogicalWidth;
    Extension->GopFramebufferTransform.LogicalHeight = FrameBufferData->LogicalHeight;
}

BOOLEAN
OslFinalizeLoaderBlock(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE OsDevice,
    _In_ PCSTR SystemRoot,
    _In_ PCSTR LoadOptions)
{
    PLOADER_PARAMETER_EXTENSION Extension = LoaderBlock->Extension;
    DK_DEVICE BootPartition;

    if (OsDevice->Memory)
    {
        RtlStringCbCopyA(OslSystemBlock->ArcBootDeviceName,
                         sizeof(OslSystemBlock->ArcBootDeviceName),
                         OSL_RAMDISK_ARC_NAME);
        RtlStringCbCopyA(OslSystemBlock->ArcHalDeviceName,
                         sizeof(OslSystemBlock->ArcHalDeviceName),
                         OSL_RAMDISK_ARC_NAME);
    }
    else
    {
        if (OsDevice->PartitionNumber == 0)
            return FALSE;

        RtlStringCbPrintfA(OslSystemBlock->ArcBootDeviceName,
                           sizeof(OslSystemBlock->ArcBootDeviceName),
                           "multi(0)disk(0)rdisk(%lu)partition(%lu)",
                           OsDevice->DiskIndex,
                           OsDevice->PartitionNumber);

        if (!DkLocateBootPartition(&BootPartition))
            BootPartition = *OsDevice;

        RtlStringCbPrintfA(OslSystemBlock->ArcHalDeviceName,
                           sizeof(OslSystemBlock->ArcHalDeviceName),
                           "multi(0)disk(0)rdisk(%lu)partition(%lu)",
                           BootPartition.DiskIndex,
                           BootPartition.PartitionNumber);
    }

    RtlStringCbCopyA(OslSystemBlock->NtBootPathName, sizeof(OslSystemBlock->NtBootPathName), SystemRoot);
    RtlStringCbCopyA(OslSystemBlock->NtHalPathName, sizeof(OslSystemBlock->NtHalPathName), "\\");
    RtlStringCbCopyA(OslSystemBlock->LoadOptions, sizeof(OslSystemBlock->LoadOptions), LoadOptions);

    LoaderBlock->ArcBootDeviceName = PaToVa(OslSystemBlock->ArcBootDeviceName);
    LoaderBlock->ArcHalDeviceName = PaToVa(OslSystemBlock->ArcHalDeviceName);
    LoaderBlock->NtBootPathName = PaToVa(OslSystemBlock->NtBootPathName);
    LoaderBlock->NtHalPathName = PaToVa(OslSystemBlock->NtHalPathName);
    LoaderBlock->LoadOptions = PaToVa(OslSystemBlock->LoadOptions);

    OslAddArcDisks(LoaderBlock);

    OslConvertConfigurationTree();
    LoaderBlock->ConfigurationRoot = PaToVa(LoaderBlock->ConfigurationRoot);

    OslConvertList(&LoaderBlock->LoadOrderListHead);
    OslConvertList(&LoaderBlock->BootDriverListHead);

    Extension->Profile.Status = OSL_PROFILE_STATUS;
    if (OslAcpiPresent)
        Extension->AcpiTable = (PVOID)1;
    Extension->SMBiosEPSHeader = FwGetSmbiosEntryPoint();
    OslAddFirmwareResources(LoaderBlock);
    OslSetFrameBuffer(Extension);

    Extension->LoaderPerformanceData = PaToVa(&OslSystemBlock->LoaderPerformanceData);
    InitializeListHead(&Extension->BootApplicationPersistentData);
    OslConvertList(&Extension->BootApplicationPersistentData);

    LoaderBlock->NlsData = PaToVa(LoaderBlock->NlsData);
    LoaderBlock->Extension = PaToVa(Extension);
    return TRUE;
}
