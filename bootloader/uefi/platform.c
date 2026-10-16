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
static EFI_GUID FwGlobalVariableGuid = EFI_GLOBAL_VARIABLE;

#define FW_LOAD_OPTION_MAX       4096
#define FW_LOAD_OPTION_HEADER    6
#define FW_BOOT_ORDER_MAX        256
#define FW_BOOT_VARIABLE_CHARS   16
#define FW_VARIABLE_ATTRIBUTES   (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

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

static
BOOLEAN
FwIsFileNode(
    _In_ EFI_DEVICE_PATH_PROTOCOL* Node,
    _In_ PCWSTR Path)
{
    PUCHAR Name = (PUCHAR)Node + sizeof(EFI_DEVICE_PATH_PROTOCOL);
    ULONG Chars, i;
    WCHAR Char;

    if (DevicePathType(Node) != MEDIA_DEVICE_PATH || DevicePathSubType(Node) != MEDIA_FILEPATH_DP)
        return FALSE;

    Chars = (ULONG)(DevicePathNodeLength(Node) - sizeof(EFI_DEVICE_PATH_PROTOCOL)) / sizeof(WCHAR);
    for (i = 0; i < Chars; i++)
    {
        Char = (WCHAR)(Name[i * 2] | (Name[i * 2 + 1] << 8));
        if (towupper(Char) != towupper(Path[i]))
            return FALSE;
        if (Char == UNICODE_NULL)
            return TRUE;
    }

    return Path[i] == UNICODE_NULL;
}

static
BOOLEAN
FwIsSamePartition(
    _In_ EFI_DEVICE_PATH_PROTOCOL* Node,
    _In_ const HARDDRIVE_DEVICE_PATH* Partition)
{
    return DevicePathType(Node) == MEDIA_DEVICE_PATH &&
           DevicePathSubType(Node) == MEDIA_HARDDRIVE_DP &&
           DevicePathNodeLength(Node) == sizeof(HARDDRIVE_DEVICE_PATH) &&
           RtlEqualMemory(&((HARDDRIVE_DEVICE_PATH*)Node)->PartitionNumber,
                          &Partition->PartitionNumber,
                          sizeof(HARDDRIVE_DEVICE_PATH) - FIELD_OFFSET(HARDDRIVE_DEVICE_PATH, PartitionNumber));
}

static
BOOLEAN
FwOptionStartsFile(
    _In_reads_bytes_(Size) PUCHAR Option,
    _In_ UINTN Size,
    _In_ const HARDDRIVE_DEVICE_PATH* Partition,
    _In_ PCWSTR Path)
{
    EFI_DEVICE_PATH_PROTOCOL *Node, *Next;
    PUCHAR End;
    UINTN Offset = FW_LOAD_OPTION_HEADER;

    if (Size < FW_LOAD_OPTION_HEADER)
        return FALSE;

    while (Offset + sizeof(WCHAR) <= Size && (Option[Offset] | Option[Offset + 1]) != 0)
        Offset += sizeof(WCHAR);
    Offset += sizeof(WCHAR);

    End = Option + Offset + (Option[4] | (Option[5] << 8));
    if (End > Option + Size)
        return FALSE;

    for (Node = (EFI_DEVICE_PATH_PROTOCOL*)(Option + Offset);
         (PUCHAR)Node + sizeof(EFI_DEVICE_PATH_PROTOCOL) <= End && !IsDevicePathEnd(Node);
         Node = Next)
    {
        if (DevicePathNodeLength(Node) < sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            (PUCHAR)Node + DevicePathNodeLength(Node) > End)
        {
            return FALSE;
        }

        Next = NextDevicePathNode(Node);
        if (FwIsSamePartition(Node, Partition) &&
            (PUCHAR)Next + sizeof(EFI_DEVICE_PATH_PROTOCOL) <= End &&
            (PUCHAR)Next + DevicePathNodeLength(Next) <= End &&
            FwIsFileNode(Next, Path))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
FwFindBootOption(
    _In_ EFI_DEVICE_PATH_PROTOCOL* DevicePath,
    _In_ PCWSTR Path,
    _Out_ PUINT16 Number)
{
    EFI_RUNTIME_SERVICES* RuntimeServices = GlobalSystemTable->RuntimeServices;
    EFI_DEVICE_PATH_PROTOCOL* Node;
    UINT16 Order[FW_BOOT_ORDER_MAX];
    WCHAR Name[FW_BOOT_VARIABLE_CHARS];
    PUCHAR Option;
    UINTN Size, Count, i;
    BOOLEAN Found = FALSE;

    for (Node = DevicePath;
         !IsDevicePathEnd(Node) &&
         (DevicePathType(Node) != MEDIA_DEVICE_PATH || DevicePathSubType(Node) != MEDIA_HARDDRIVE_DP);
         Node = NextDevicePathNode(Node))
        ;

    Size = sizeof(Order);
    if (IsDevicePathEnd(Node) ||
        DevicePathNodeLength(Node) != sizeof(HARDDRIVE_DEVICE_PATH) ||
        EFI_ERROR(RuntimeServices->GetVariable(L"BootOrder", &FwGlobalVariableGuid, NULL, &Size, Order)))
    {
        return FALSE;
    }

    Option = FwAllocate(FW_LOAD_OPTION_MAX);
    if (!Option)
        return FALSE;

    Count = Size / sizeof(UINT16);
    for (i = 0; i < Count && !Found; i++)
    {
        RtlStringCchPrintfW(Name, RTL_NUMBER_OF(Name), L"Boot%04X", Order[i]);
        Size = FW_LOAD_OPTION_MAX;
        if (!EFI_ERROR(RuntimeServices->GetVariable(Name, &FwGlobalVariableGuid, NULL, &Size, Option)) &&
            FwOptionStartsFile(Option, Size, (HARDDRIVE_DEVICE_PATH*)Node, Path))
        {
            *Number = Order[i];
            Found = TRUE;
        }
    }

    FwFree(Option);
    return Found;
}

BOOLEAN
FwStartFirmwareApplication(
    _In_ PVOID Device,
    _In_ PCWSTR Path)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, *Node, *FilePath;
    EFI_HANDLE Handle;
    SIZE_T DeviceSize, NodeSize;
    PUCHAR Buffer;
    UINT16 Number;
    ULONG i;

    if (EFI_ERROR(BootServices->HandleProtocol(Device, &FwDevicePathGuid, (VOID**)&DevicePath)))
        return FALSE;

    if (FwFindBootOption(DevicePath, Path, &Number) &&
        !EFI_ERROR(GlobalSystemTable->RuntimeServices->SetVariable(L"BootNext",
                                                                   &FwGlobalVariableGuid,
                                                                   FW_VARIABLE_ATTRIBUTES,
                                                                   sizeof(Number),
                                                                   &Number)))
    {
        GlobalSystemTable->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    }

    for (Node = DevicePath; !IsDevicePathEnd(Node); Node = NextDevicePathNode(Node))
        ;

    DeviceSize = (PUCHAR)Node - (PUCHAR)DevicePath;
    NodeSize = sizeof(EFI_DEVICE_PATH_PROTOCOL) + (wcslen(Path) + 1) * sizeof(WCHAR);
    if (NodeSize > MAXUSHORT)
        return FALSE;

    Buffer = FwAllocate(DeviceSize + NodeSize + sizeof(EFI_DEVICE_PATH_PROTOCOL));
    if (!Buffer)
        return FALSE;

    RtlCopyMemory(Buffer, DevicePath, DeviceSize);
    FilePath = (EFI_DEVICE_PATH_PROTOCOL*)(Buffer + DeviceSize);
    FilePath->Type = MEDIA_DEVICE_PATH;
    FilePath->SubType = MEDIA_FILEPATH_DP;
    FilePath->Length[0] = (UINT8)NodeSize;
    FilePath->Length[1] = (UINT8)(NodeSize >> 8);
    for (i = 0; Path[i] != UNICODE_NULL; i++)
    {
        ((PUCHAR)(FilePath + 1))[i * 2] = (UCHAR)Path[i];
        ((PUCHAR)(FilePath + 1))[i * 2 + 1] = (UCHAR)(Path[i] >> 8);
    }
    ((PUCHAR)(FilePath + 1))[i * 2] = 0;
    ((PUCHAR)(FilePath + 1))[i * 2 + 1] = 0;

    Node = (EFI_DEVICE_PATH_PROTOCOL*)(Buffer + DeviceSize + NodeSize);
    Node->Type = END_DEVICE_PATH_TYPE;
    Node->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
    Node->Length[0] = sizeof(EFI_DEVICE_PATH_PROTOCOL);
    Node->Length[1] = 0;

    if (EFI_ERROR(BootServices->LoadImage(FALSE,
                                          GlobalImageHandle,
                                          (EFI_DEVICE_PATH_PROTOCOL*)Buffer,
                                          NULL,
                                          0,
                                          &Handle)))
    {
        FwFree(Buffer);
        return FALSE;
    }

    FwFree(Buffer);
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
