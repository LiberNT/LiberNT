/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI platform tables, display, time and image start
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include <arc/loaderblk.h>
#include <drivers/bootvid/framebuf.h>
#include <EdidActive.h>

#define FW_COLOR_BITS  32
#define FW_MASK_RED    0x000000FF
#define FW_MASK_GREEN  0x0000FF00
#define FW_MASK_BLUE   0x00FF0000
#define FW_MASK_UNUSED 0xFF000000

const USHORT FwApplicationSubsystem = IMAGE_SUBSYSTEM_EFI_APPLICATION;

static EFI_GUID FwAcpi20Guid = EFI_ACPI_20_TABLE_GUID;
static EFI_GUID FwAcpi10Guid = ACPI_10_TABLE_GUID;
static EFI_GUID FwSmbios3Guid = SMBIOS3_TABLE_GUID;
static EFI_GUID FwSmbiosGuid = SMBIOS_TABLE_GUID;
static EFI_GUID FwGraphicsOutputGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID FwLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID FwDevicePathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID FwEdidActiveGuid = EFI_EDID_ACTIVE_PROTOCOL_GUID;

static
PVOID
FwFindConfigurationTable(
    _In_ EFI_GUID* Guid)
{
    UINTN i;

    for (i = 0; i < GlobalSystemTable->NumberOfTableEntries; i++)
    {
        if (RtlEqualMemory(&GlobalSystemTable->ConfigurationTable[i].VendorGuid, Guid, sizeof(*Guid)))
            return GlobalSystemTable->ConfigurationTable[i].VendorTable;
    }

    return NULL;
}

PVOID
FwGetAcpiRootPointer(VOID)
{
    PVOID Table = FwFindConfigurationTable(&FwAcpi20Guid);

    return Table ? Table : FwFindConfigurationTable(&FwAcpi10Guid);
}

PVOID
FwGetSmbiosEntryPoint(VOID)
{
    PVOID Table = FwFindConfigurationTable(&FwSmbios3Guid);

    return Table ? Table : FwFindConfigurationTable(&FwSmbiosGuid);
}

BOOLEAN
FwGetFrameBuffer(
    _Out_ PCM_FRAMEBUF_DEVICE_DATA Data,
    _Out_ PULONGLONG Base,
    _Out_ PULONG Size)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;

    if (EFI_ERROR(GlobalSystemTable->BootServices->LocateProtocol(&FwGraphicsOutputGuid,
                                                                  NULL,
                                                                  (VOID**)&Gop)) ||
        !Gop->Mode ||
        !Gop->Mode->Info ||
        Gop->Mode->FrameBufferBase == 0 ||
        Gop->Mode->FrameBufferSize == 0 ||
        Gop->Mode->FrameBufferSize > MAXULONG)
    {
        return FALSE;
    }

    Info = Gop->Mode->Info;
    RtlZeroMemory(Data, sizeof(*Data));

    switch (Info->PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
            Data->PixelMasks.RedMask = FW_MASK_RED;
            Data->PixelMasks.GreenMask = FW_MASK_GREEN;
            Data->PixelMasks.BlueMask = FW_MASK_BLUE;
            Data->PixelMasks.ReservedMask = FW_MASK_UNUSED;
            break;

        case PixelBlueGreenRedReserved8BitPerColor:
            Data->PixelMasks.RedMask = FW_MASK_BLUE;
            Data->PixelMasks.GreenMask = FW_MASK_GREEN;
            Data->PixelMasks.BlueMask = FW_MASK_RED;
            Data->PixelMasks.ReservedMask = FW_MASK_UNUSED;
            break;

        case PixelBitMask:
            Data->PixelMasks.RedMask = Info->PixelInformation.RedMask;
            Data->PixelMasks.GreenMask = Info->PixelInformation.GreenMask;
            Data->PixelMasks.BlueMask = Info->PixelInformation.BlueMask;
            Data->PixelMasks.ReservedMask = Info->PixelInformation.ReservedMask;
            break;

        default:
            return FALSE;
    }

    Data->ScreenWidth = Info->HorizontalResolution;
    Data->ScreenHeight = Info->VerticalResolution;
    Data->PixelsPerScanLine = Info->PixelsPerScanLine;
    Data->BitsPerPixel = FW_COLOR_BITS;
    Data->LogicalWidth = Info->HorizontalResolution;
    Data->LogicalHeight = Info->VerticalResolution;

    *Base = Gop->Mode->FrameBufferBase;
    *Size = (ULONG)Gop->Mode->FrameBufferSize;
    return TRUE;
}

BOOLEAN
FwGetTime(
    _Out_ PTIME_FIELDS Time)
{
    EFI_TIME EfiTime;

    if (EFI_ERROR(GlobalSystemTable->RuntimeServices->GetTime(&EfiTime, NULL)))
        return FALSE;

    RtlZeroMemory(Time, sizeof(*Time));
    Time->Year = EfiTime.Year;
    Time->Month = EfiTime.Month;
    Time->Day = EfiTime.Day;
    Time->Hour = EfiTime.Hour;
    Time->Minute = EfiTime.Minute;
    Time->Second = EfiTime.Second;
    return TRUE;
}

PCWSTR
FwGetLoadOptions(
    _Out_ PULONG Chars)
{
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;

    *Chars = 0;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(GlobalImageHandle,
                                                                  &FwLoadedImageProtocolGuid,
                                                                  (VOID**)&LoadedImage)) ||
        !LoadedImage->LoadOptions)
    {
        return NULL;
    }

    *Chars = LoadedImage->LoadOptionsSize / sizeof(WCHAR);
    return LoadedImage->LoadOptions;
}

BOOLEAN
FwGetBootPartition(
    _Out_ PULONG PartitionNumber,
    _Out_writes_bytes_(16) PUCHAR Signature,
    _Out_ PBOOLEAN IsGpt)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_DEVICE_PATH_PROTOCOL* Node;
    HARDDRIVE_DEVICE_PATH* HardDrive;
    EFI_HANDLE Device = FwGetBootDeviceHandle();

    if (!Device ||
        EFI_ERROR(BootServices->HandleProtocol(Device, &FwDevicePathGuid, (VOID**)&Node)))
    {
        return FALSE;
    }

    for (; !IsDevicePathEnd(Node); Node = NextDevicePathNode(Node))
    {
        if (DevicePathType(Node) != MEDIA_DEVICE_PATH ||
            DevicePathSubType(Node) != MEDIA_HARDDRIVE_DP)
        {
            continue;
        }

        HardDrive = (HARDDRIVE_DEVICE_PATH*)Node;
        if (HardDrive->SignatureType != SIGNATURE_TYPE_GUID &&
            HardDrive->SignatureType != SIGNATURE_TYPE_MBR)
        {
            return FALSE;
        }

        *PartitionNumber = HardDrive->PartitionNumber;
        *IsGpt = (HardDrive->SignatureType == SIGNATURE_TYPE_GUID);
        RtlCopyMemory(Signature, HardDrive->Signature, 16);
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
FwStartImage(
    _In_reads_bytes_(Size) PVOID Image,
    _In_ ULONG Size,
    _In_ PCWSTR LoadOptions)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_HANDLE Handle;

    if (EFI_ERROR(BootServices->LoadImage(FALSE, GlobalImageHandle, NULL, Image, Size, &Handle)))
        return FALSE;

    if (EFI_ERROR(BootServices->HandleProtocol(Handle,
                                               &FwLoadedImageProtocolGuid,
                                               (VOID**)&LoadedImage)))
    {
        BootServices->UnloadImage(Handle);
        return FALSE;
    }

    LoadedImage->LoadOptions = (VOID*)LoadOptions;
    LoadedImage->LoadOptionsSize = (UINT32)((wcslen(LoadOptions) + 1) * sizeof(WCHAR));

    return !EFI_ERROR(BootServices->StartImage(Handle, NULL, NULL));
}

BOOLEAN
FwGetLoaderImage(
    _Out_ PVOID* Base,
    _Out_ PSIZE_T Size)
{
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(GlobalImageHandle,
                                                                  &FwLoadedImageProtocolGuid,
                                                                  (VOID**)&LoadedImage)))
    {
        return FALSE;
    }

    *Base = LoadedImage->ImageBase;
    *Size = (SIZE_T)LoadedImage->ImageSize;
    return TRUE;
}

BOOLEAN
FwGetImageDirectory(
    _Out_writes_z_(Chars) PWSTR Directory,
    _In_ ULONG Chars)
{
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_DEVICE_PATH_PROTOCOL* Node;
    PUCHAR Name;
    ULONG Length, i, End = 0;

    if (Chars == 0 ||
        EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(GlobalImageHandle,
                                                                  &FwLoadedImageProtocolGuid,
                                                                  (VOID**)&LoadedImage)) ||
        !LoadedImage->FilePath)
    {
        return FALSE;
    }

    Node = LoadedImage->FilePath;
    if (DevicePathType(Node) != MEDIA_DEVICE_PATH || DevicePathSubType(Node) != MEDIA_FILEPATH_DP)
        return FALSE;

    Name = (PUCHAR)Node + sizeof(EFI_DEVICE_PATH_PROTOCOL);
    Length = (ULONG)(DevicePathNodeLength(Node) - sizeof(EFI_DEVICE_PATH_PROTOCOL)) / sizeof(WCHAR);

    for (i = 0; i < Length && i < Chars - 1; i++)
    {
        Directory[i] = (WCHAR)(Name[i * 2] | (Name[i * 2 + 1] << 8));
        if (Directory[i] == UNICODE_NULL)
            break;
        if (Directory[i] == L'\\')
            End = i + 1;
    }

    Directory[End] = UNICODE_NULL;
    return End != 0;
}

BOOLEAN
FwGetEdid(
    _Out_writes_bytes_(Size) PUCHAR Edid,
    _In_ ULONG Size)
{
    EFI_EDID_ACTIVE_PROTOCOL* EdidActive;

    if (EFI_ERROR(GlobalSystemTable->BootServices->LocateProtocol(&FwEdidActiveGuid,
                                                                  NULL,
                                                                  (VOID**)&EdidActive)) ||
        !EdidActive->Edid ||
        EdidActive->SizeOfEdid < Size)
    {
        return FALSE;
    }

    RtlCopyMemory(Edid, EdidActive->Edid, Size);
    return TRUE;
}
