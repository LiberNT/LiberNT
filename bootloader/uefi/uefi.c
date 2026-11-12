/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI entry point and firmware services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

#define FW_FILE_NAME_CHARS 260
#define FW_MAX_USB_DEVICES 64

EFI_HANDLE GlobalImageHandle;
EFI_SYSTEM_TABLE* GlobalSystemTable;
volatile BOOLEAN BootServicesExitedFlag;

static EFI_GUID FwLoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID FwFileSystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID FwFileInfoGuid = EFI_FILE_INFO_ID;
static EFI_GUID FwBlockIoGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
static EFI_GUID FwDevicePathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID FwVolumeLabelGuid = EFI_FILE_SYSTEM_VOLUME_LABEL_ID;

PVOID
FwAllocate(
    _In_ SIZE_T Size)
{
    VOID* Memory;

    if (EFI_ERROR(GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData, Size, &Memory)))
        return NULL;

    return Memory;
}

VOID
FwFree(
    _In_ PVOID Memory)
{
    GlobalSystemTable->BootServices->FreePool(Memory);
}

VOID
FwPrint(
    _In_ PCWSTR Format,
    ...)
{
    WCHAR Text[512];
    CHAR Narrow[RTL_NUMBER_OF(Text)];
    va_list Arguments;
    ULONG i;

    va_start(Arguments, Format);
    RtlStringCchVPrintfW(Text, RTL_NUMBER_OF(Text), Format, Arguments);
    va_end(Arguments);

    for (i = 0; Text[i] != UNICODE_NULL; i++)
        Narrow[i] = Text[i] <= 0x7F ? (CHAR)Text[i] : '?';
    Narrow[i] = ANSI_NULL;
    UdpLogWrite(Narrow);
    BootLogWrite(Narrow);

    if (BootServicesExitedFlag || !GlobalSystemTable->ConOut)
    {
        for (i = 0; Text[i] != UNICODE_NULL; i++)
            UefiSerialPutChar(Text[i] <= 0x7F ? (UCHAR)Text[i] : '?');
        return;
    }

    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, (CHAR16*)Text);
    UefiSerialMirror(Text);
}

PVOID
FwAllocatePages(
    _In_ SIZE_T Size)
{
    EFI_PHYSICAL_ADDRESS Address;

    if (EFI_ERROR(GlobalSystemTable->BootServices->AllocatePages(AllocateAnyPages,
                                                             EfiLoaderData,
                                                             EFI_SIZE_TO_PAGES(Size),
                                                             &Address)))
    {
        return NULL;
    }

    return (PVOID)(ULONG_PTR)Address;
}

PVOID
FwAllocatePagesBelow(
    _In_ SIZE_T Size,
    _In_ ULONGLONG MaxAddress)
{
    EFI_PHYSICAL_ADDRESS Address = MaxAddress - 1;

    if (EFI_ERROR(GlobalSystemTable->BootServices->AllocatePages(AllocateMaxAddress,
                                                                 EfiLoaderData,
                                                                 EFI_SIZE_TO_PAGES(Size),
                                                                 &Address)))
    {
        return NULL;
    }

    return (PVOID)(ULONG_PTR)Address;
}

BOOLEAN
FwAllocatePagesAt(
    _In_ PVOID Address,
    _In_ SIZE_T Size)
{
    EFI_PHYSICAL_ADDRESS Base = (EFI_PHYSICAL_ADDRESS)(ULONG_PTR)Address;

    return !EFI_ERROR(GlobalSystemTable->BootServices->AllocatePages(AllocateAddress,
                                                                    EfiLoaderData,
                                                                    EFI_SIZE_TO_PAGES(Size),
                                                                    &Base));
}

VOID
FwFreePages(
    _In_ PVOID Memory,
    _In_ SIZE_T Size)
{
    GlobalSystemTable->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)Memory,
                                           EFI_SIZE_TO_PAGES(Size));
}

static
BOOLEAN
FwOpenBlockDevice(
    _In_ EFI_HANDLE Handle,
    _In_ BOOLEAN WholeDisk,
    _Out_ PDK_DEVICE Device)
{
    EFI_BLOCK_IO_PROTOCOL* BlockIo;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(Handle,
                                                              &FwBlockIoGuid,
                                                              (VOID**)&BlockIo)) ||
        !BlockIo->Media->MediaPresent ||
        BlockIo->Media->BlockSize == 0 ||
        (WholeDisk && BlockIo->Media->LogicalPartition))
    {
        return FALSE;
    }

    RtlZeroMemory(Device, sizeof(*Device));
    Device->Firmware = BlockIo;
    Device->BlockSize = BlockIo->Media->BlockSize;
    Device->Length = (BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize;
    return TRUE;
}

static EFI_HANDLE* FwDiskHandles;
static UINTN FwDiskHandleCount;
static BOOLEAN FwDisksConnected;

BOOLEAN
FwGetDisk(
    _In_ ULONG Index,
    _Out_ PDK_DEVICE Disk)
{
    UINTN i;

    if (!FwDiskHandles &&
        EFI_ERROR(GlobalSystemTable->BootServices->LocateHandleBuffer(ByProtocol,
                                                                      &FwBlockIoGuid,
                                                                      NULL,
                                                                      &FwDiskHandleCount,
                                                                      &FwDiskHandles)))
    {
        FwDiskHandles = NULL;
        return FALSE;
    }

    for (i = 0; i < FwDiskHandleCount; i++)
    {
        if (FwOpenBlockDevice(FwDiskHandles[i], TRUE, Disk) && Index-- == 0)
            return TRUE;
    }

    return FALSE;
}

static
SIZE_T
FwDevicePathSize(
    _In_ EFI_DEVICE_PATH_PROTOCOL* Path)
{
    EFI_DEVICE_PATH_PROTOCOL* Node;

    for (Node = Path; !IsDevicePathEnd(Node); Node = NextDevicePathNode(Node))
        ;

    return (PUCHAR)Node - (PUCHAR)Path;
}

static
BOOLEAN
FwFindVolumeDisk(
    _In_opt_ EFI_HANDLE Volume,
    _In_ BOOLEAN FixedOnly,
    _Out_ PULONG Index)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_DEVICE_PATH_PROTOCOL *VolumePath, *DiskPath, *Node;
    DK_DEVICE Disk;
    SIZE_T Size;
    ULONG Count;
    UINTN i;

    *Index = MAXULONG;
    if (!Volume ||
        EFI_ERROR(BootServices->HandleProtocol(Volume, &FwDevicePathGuid, (VOID**)&VolumePath)) ||
        !FwGetDisk(0, &Disk))
    {
        return FALSE;
    }

    for (i = 0, Count = 0; i < FwDiskHandleCount; i++)
    {
        if (!FwOpenBlockDevice(FwDiskHandles[i], TRUE, &Disk) ||
            (FixedOnly && FwIsUsbVolume(FwDiskHandles[i])))
        {
            continue;
        }

        if (!EFI_ERROR(BootServices->HandleProtocol(FwDiskHandles[i], &FwDevicePathGuid, (VOID**)&DiskPath)))
        {
            Size = FwDevicePathSize(DiskPath);
            Node = (EFI_DEVICE_PATH_PROTOCOL*)((PUCHAR)VolumePath + Size);
            if (Size < FwDevicePathSize(VolumePath) &&
                RtlEqualMemory(VolumePath, DiskPath, Size) &&
                DevicePathType(Node) == MEDIA_DEVICE_PATH &&
                DevicePathSubType(Node) == MEDIA_HARDDRIVE_DP)
            {
                *Index = Count;
                return TRUE;
            }
        }

        Count++;
    }

    return FALSE;
}

BOOLEAN
FwGetBootDiskIndex(
    _Out_ PULONG Index)
{
    return FwFindVolumeDisk(FwGetBootDeviceHandle(), FALSE, Index);
}

BOOLEAN
FwGetVolumeDiskNumber(
    _In_ PVOID Device,
    _Out_ PULONG Number)
{
    return FwFindVolumeDisk(Device, TRUE, Number);
}

static
VOID
FwFreeDiskHandles(VOID)
{
    if (FwDiskHandles)
    {
        GlobalSystemTable->BootServices->FreePool(FwDiskHandles);
        FwDiskHandles = NULL;
    }
}

BOOLEAN
FwRescanDisks(VOID)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_HANDLE* Handles;
    UINTN Count, i;

    if (FwDisksConnected ||
        EFI_ERROR(BootServices->LocateHandleBuffer(AllHandles, NULL, NULL, &Count, &Handles)))
    {
        return FALSE;
    }

    for (i = 0; i < Count; i++)
        BootServices->ConnectController(Handles[i], NULL, NULL, TRUE);

    BootServices->FreePool(Handles);
    FwFreeDiskHandles();
    FwDisksConnected = TRUE;
    return TRUE;
}

EFI_HANDLE
FwGetBootDeviceHandle(VOID)
{
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_HANDLE Image = GlobalImageHandle;
    ULONG Depth;

    for (Depth = 0; Depth < 4 && Image; Depth++)
    {
        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(Image,
                                                                      &FwLoadedImageGuid,
                                                                      (VOID**)&LoadedImage)))
        {
            break;
        }

        if (LoadedImage->DeviceHandle)
            return LoadedImage->DeviceHandle;

        Image = LoadedImage->ParentHandle;
    }

    return NULL;
}

BOOLEAN
FwGetBootDevice(
    _Out_ PDK_DEVICE Device)
{
    EFI_HANDLE Handle = FwGetBootDeviceHandle();

    return Handle && FwOpenBlockDevice(Handle, FALSE, Device);
}

BOOLEAN
FwReadBlocks(
    _In_ PVOID Firmware,
    _In_ ULONGLONG Block,
    _In_ ULONG Count,
    _Out_ PVOID Buffer)
{
    EFI_BLOCK_IO_PROTOCOL* BlockIo = Firmware;

    return !EFI_ERROR(BlockIo->ReadBlocks(BlockIo,
                                          BlockIo->Media->MediaId,
                                          Block,
                                          (UINTN)Count * BlockIo->Media->BlockSize,
                                          Buffer));
}

BOOLEAN
FwWriteBlocks(
    _In_ PVOID Firmware,
    _In_ ULONGLONG Block,
    _In_ ULONG Count,
    _In_ PVOID Buffer)
{
    EFI_BLOCK_IO_PROTOCOL* BlockIo = Firmware;

    return !EFI_ERROR(BlockIo->WriteBlocks(BlockIo,
                                           BlockIo->Media->MediaId,
                                           Block,
                                           (UINTN)Count * BlockIo->Media->BlockSize,
                                           Buffer));
}

BOOLEAN
FwFlushBlocks(
    _In_ PVOID Firmware)
{
    EFI_BLOCK_IO_PROTOCOL* BlockIo = Firmware;

    return !EFI_ERROR(BlockIo->FlushBlocks(BlockIo));
}

BOOLEAN
FwBufferAligned(
    _In_ PVOID Firmware,
    _In_ PVOID Buffer)
{
    EFI_BLOCK_IO_PROTOCOL* BlockIo = Firmware;

    return BlockIo->Media->IoAlign <= 1 ||
           ((ULONG_PTR)Buffer & (BlockIo->Media->IoAlign - 1)) == 0;
}

BOOLEAN
FwGetBootMedium(
    _Out_ PDK_DEVICE Device)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_DEVICE_PATH_PROTOCOL *Path, *Node, *Copy, *End;
    EFI_HANDLE Handle = FwGetBootDeviceHandle();
    SIZE_T Size;
    BOOLEAN Found;

    if (!Handle ||
        EFI_ERROR(BootServices->HandleProtocol(Handle, &FwDevicePathGuid, (VOID**)&Path)))
    {
        return FALSE;
    }

    for (Node = Path; DevicePathType(Node) != MEDIA_DEVICE_PATH; Node = NextDevicePathNode(Node))
    {
        if (IsDevicePathEnd(Node))
            return FALSE;
    }

    Size = (PUCHAR)Node - (PUCHAR)Path;
    Copy = FwAllocate(Size + sizeof(*End));
    if (!Copy)
        return FALSE;

    RtlCopyMemory(Copy, Path, Size);
    End = (EFI_DEVICE_PATH_PROTOCOL*)((PUCHAR)Copy + Size);
    End->Type = END_DEVICE_PATH_TYPE;
    End->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
    End->Length[0] = sizeof(*End);
    End->Length[1] = 0;

    Node = Copy;
    Found = !EFI_ERROR(BootServices->LocateDevicePath(&FwBlockIoGuid, &Node, &Handle)) &&
            IsDevicePathEnd(Node) &&
            FwOpenBlockDevice(Handle, FALSE, Device);
    FwFree(Copy);
    return Found;
}

VOID
StallExecutionProcessor(
    ULONG Microseconds)
{
    GlobalSystemTable->BootServices->Stall(Microseconds);
}

DECLSPEC_NORETURN
VOID
FwHalt(VOID)
{
    if (!BootServicesExitedFlag)
        GlobalSystemTable->BootServices->Exit(GlobalImageHandle, EFI_ABORTED, 0, NULL);

    for (;;)
        ;
}

#define FW_READ_CHUNK (8 * 1024 * 1024)

static
BOOLEAN
FwReadFileChunks(
    _In_ EFI_FILE_PROTOCOL* File,
    _In_ PCWSTR Path,
    _Out_writes_bytes_(Size) PUCHAR Data,
    _In_ UINTN Size)
{
    CHAR Line[FW_FILE_NAME_CHARS + 64];
    UINTN Done = 0, Chunk;

    while (Done < Size)
    {
        Chunk = min(Size - Done, FW_READ_CHUNK);
        if (EFI_ERROR(File->Read(File, &Chunk, Data + Done)) || Chunk == 0)
            return FALSE;

        Done += Chunk;
        if (Size > FW_READ_CHUNK)
        {
            RtlStringCchPrintfA(Line, RTL_NUMBER_OF(Line), "%ls: %I64u of %I64u MB read\n",
                                Path, (ULONGLONG)Done >> 20, (ULONGLONG)Size >> 20);
            FwDebugPrint(Line);
        }
    }

    return TRUE;
}

PVOID
FwReadDeviceFile(
    _In_ PVOID Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    EFI_FILE_PROTOCOL* Root;
    EFI_FILE_PROTOCOL* File;
    union
    {
        EFI_FILE_INFO Info;
        UINT8 Buffer[sizeof(EFI_FILE_INFO) + FW_FILE_NAME_CHARS * sizeof(CHAR16)];
    } FileInfo;
    UINTN Length;
    PVOID Data = NULL;

    *Size = 0;

    if (!Device ||
        EFI_ERROR(BootServices->HandleProtocol(Device,
                                               &FwFileSystemGuid,
                                               (VOID**)&FileSystem)) ||
        EFI_ERROR(FileSystem->OpenVolume(FileSystem, &Root)))
    {
        return NULL;
    }

    if (!EFI_ERROR(Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, 0)))
    {
        Length = sizeof(FileInfo);
        if (!EFI_ERROR(File->GetInfo(File, &FwFileInfoGuid, &Length, &FileInfo)) &&
            FileInfo.Info.FileSize != 0 &&
            FileInfo.Info.FileSize <= MAXULONG)
        {
            Length = (UINTN)FileInfo.Info.FileSize;
            Data = FwAllocatePages(Length);
            if (Data && !FwReadFileChunks(File, Path, Data, Length))
            {
                FwFreePages(Data, (SIZE_T)FileInfo.Info.FileSize);
                Data = NULL;
            }

            if (Data)
                *Size = (ULONG)Length;
        }

        File->Close(File);
    }

    Root->Close(Root);
    return Data;
}

PVOID
FwReadBootFile(
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    return FwReadDeviceFile(FwGetBootDeviceHandle(), Path, Size);
}

BOOLEAN
FwWriteBootFile(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ ULONG Size)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    EFI_FILE_PROTOCOL* Root;
    EFI_FILE_PROTOCOL* File;
    EFI_HANDLE Device = FwGetBootDeviceHandle();
    UINTN Length = Size;
    BOOLEAN Written = FALSE;

    if (!Device ||
        EFI_ERROR(BootServices->HandleProtocol(Device, &FwFileSystemGuid, (VOID**)&FileSystem)) ||
        EFI_ERROR(FileSystem->OpenVolume(FileSystem, &Root)))
    {
        return FALSE;
    }

    if (!EFI_ERROR(Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0)))
        File->Delete(File);

    if (!EFI_ERROR(Root->Open(Root,
                              &File,
                              (CHAR16*)Path,
                              EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                              0)))
    {
        Written = !EFI_ERROR(File->Write(File, &Length, (VOID*)Data)) && Length == Size &&
                  !EFI_ERROR(File->Flush(File));
        File->Close(File);
    }

    Root->Close(Root);
    return Written;
}

static
BOOLEAN
FwOpenVolumeRoot(
    _In_ PVOID Device,
    _Out_ EFI_FILE_PROTOCOL** Root)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;

    *Root = NULL;
    return Device &&
           !EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(Device, &FwFileSystemGuid, (VOID**)&FileSystem)) &&
           !EFI_ERROR(FileSystem->OpenVolume(FileSystem, Root));
}

BOOLEAN
FwFileExists(
    _In_ PVOID Device,
    _In_ PCWSTR Path)
{
    EFI_FILE_PROTOCOL* Root;
    EFI_FILE_PROTOCOL* File;
    BOOLEAN Exists = FALSE;

    if (!FwOpenVolumeRoot(Device, &Root))
        return FALSE;

    if (!EFI_ERROR(Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, 0)))
    {
        File->Close(File);
        Exists = TRUE;
    }

    Root->Close(Root);
    return Exists;
}

static EFI_HANDLE FwUsbDevices[FW_MAX_USB_DEVICES];
static ULONG FwUsbDeviceCount;

BOOLEAN
FwIsUsbVolume(
    _In_ PVOID Device)
{
    EFI_DEVICE_PATH_PROTOCOL* Node;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(Device, &FwDevicePathGuid, (VOID**)&Node)))
        return FALSE;

    for (; !IsDevicePathEnd(Node); Node = NextDevicePathNode(Node))
    {
        if (DevicePathType(Node) == MESSAGING_DEVICE_PATH &&
            (DevicePathSubType(Node) == MSG_USB_DP ||
             DevicePathSubType(Node) == MSG_USB_CLASS_DP ||
             DevicePathSubType(Node) == MSG_USB_WWID_DP))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static
VOID
FwConnectUsbDevices(VOID)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_HANDLE Present[FW_MAX_USB_DEVICES];
    EFI_HANDLE* Handles;
    UINTN HandleCount, i;
    ULONG Count = 0, j;
    BOOLEAN Known;

    if (EFI_ERROR(BootServices->LocateHandleBuffer(ByProtocol, &FwDevicePathGuid, NULL, &HandleCount, &Handles)))
        return;

    for (i = 0; i < HandleCount && Count < RTL_NUMBER_OF(Present); i++)
    {
        if (!FwIsUsbVolume(Handles[i]))
            continue;

        Known = FALSE;
        for (j = 0; j < FwUsbDeviceCount && !Known; j++)
            Known = (FwUsbDevices[j] == Handles[i]);
        if (!Known)
            BootServices->ConnectController(Handles[i], NULL, NULL, TRUE);
        Present[Count++] = Handles[i];
    }

    BootServices->FreePool(Handles);
    RtlCopyMemory(FwUsbDevices, Present, Count * sizeof(Present[0]));
    FwUsbDeviceCount = Count;
}

BOOLEAN
FwIsBootVolume(
    _In_ PVOID Device)
{
    return Device && Device == FwGetBootDeviceHandle();
}

BOOLEAN
FwGetVolumeLabel(
    _In_ PVOID Device,
    _Out_writes_z_(Chars) PWSTR Label,
    _In_ ULONG Chars)
{
    union
    {
        EFI_FILE_SYSTEM_VOLUME_LABEL Info;
        CHAR16 Buffer[FW_FILE_NAME_CHARS + 1];
    } VolumeLabel;
    EFI_FILE_PROTOCOL* Root;
    UINTN Length = sizeof(VolumeLabel) - sizeof(CHAR16);
    BOOLEAN Found;

    Label[0] = UNICODE_NULL;
    if (!FwOpenVolumeRoot(Device, &Root))
        return FALSE;

    RtlZeroMemory(&VolumeLabel, sizeof(VolumeLabel));
    Found = !EFI_ERROR(Root->GetInfo(Root, &FwVolumeLabelGuid, &Length, &VolumeLabel)) &&
            VolumeLabel.Info.VolumeLabel[0] != UNICODE_NULL &&
            NT_SUCCESS(RtlStringCchCopyW(Label, Chars, VolumeLabel.Info.VolumeLabel));
    Root->Close(Root);
    return Found;
}

PCWSTR
FwGetRemovableMediaPath(VOID)
{
    static const struct
    {
        USHORT Machine;
        PCWSTR Path;
    } Paths[] =
    {
        { IMAGE_FILE_MACHINE_I386, L"\\EFI\\BOOT\\BOOTIA32.EFI" },
        { IMAGE_FILE_MACHINE_AMD64, L"\\EFI\\BOOT\\BOOTX64.EFI" },
        { IMAGE_FILE_MACHINE_ARMNT, L"\\EFI\\BOOT\\BOOTARM.EFI" },
        { IMAGE_FILE_MACHINE_ARM64, L"\\EFI\\BOOT\\BOOTAA64.EFI" },
        { IMAGE_FILE_MACHINE_RISCV64, L"\\EFI\\BOOT\\BOOTRISCV64.EFI" },
    };
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG i;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(GlobalImageHandle,
                                                                  &FwLoadedImageGuid,
                                                                  (VOID**)&LoadedImage)))
    {
        return NULL;
    }

    NtHeaders = RtlImageNtHeader(LoadedImage->ImageBase);
    if (!NtHeaders)
        return NULL;

    for (i = 0; i < RTL_NUMBER_OF(Paths); i++)
    {
        if (Paths[i].Machine == NtHeaders->FileHeader.Machine)
            return Paths[i].Path;
    }

    return NULL;
}

BOOLEAN
FwScanFileVolumes(
    _In_opt_ PCWSTR Path,
    _Inout_ PFW_VOLUME_SCAN Scan,
    _Out_ PVOID* Device)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    EFI_HANDLE* Handles;
    EFI_HANDLE Handle;
    UINTN HandleCount;

    *Device = NULL;

    if (Scan->State == FwVolumeScanConnect && FwDisksConnected && !Scan->Handles)
        Scan->State = FwVolumeScanVolumes;

    if (Scan->State == FwVolumeScanDone)
        return FALSE;

    if (!Scan->Handles)
    {
        if (Scan->State == FwVolumeScanVolumes)
            FwConnectUsbDevices();

        if (EFI_ERROR(BootServices->LocateHandleBuffer(ByProtocol,
                                                       Scan->State == FwVolumeScanConnect ? &FwBlockIoGuid : &FwFileSystemGuid,
                                                       NULL,
                                                       &HandleCount,
                                                       &Handles)))
        {
            Scan->State = (Scan->State == FwVolumeScanConnect) ? FwVolumeScanVolumes : FwVolumeScanDone;
            return Scan->State != FwVolumeScanDone;
        }

        Scan->Handles = Handles;
        Scan->HandleCount = (ULONG)HandleCount;
        Scan->Index = 0;
        return TRUE;
    }

    if (Scan->Index < Scan->HandleCount)
    {
        Handle = Scan->Handles[Scan->Index++];

        if (Scan->State == FwVolumeScanConnect)
        {
            if (!FwDisksConnected)
                BootServices->ConnectController(Handle, NULL, NULL, TRUE);
        }
        else if (!Path)
        {
            if (!EFI_ERROR(BootServices->HandleProtocol(Handle, &FwFileSystemGuid, (VOID**)&FileSystem)))
                *Device = Handle;
        }
        else if (FwFileExists(Handle, Path))
        {
            *Device = Handle;
        }

        return TRUE;
    }

    BootServices->FreePool(Scan->Handles);
    Scan->Handles = NULL;

    if (Scan->State == FwVolumeScanConnect)
    {
        FwFreeDiskHandles();
        Scan->State = FwVolumeScanVolumes;
        return TRUE;
    }

    Scan->State = FwVolumeScanDone;
    return FALSE;
}

EFI_STATUS
EFIAPI
EfiEntry(
    _In_ EFI_HANDLE ImageHandle,
    _In_ EFI_SYSTEM_TABLE* SystemTable)
{
    GlobalImageHandle = ImageHandle;
    GlobalSystemTable = SystemTable;

    BlMain();
    return EFI_LOAD_ERROR;
}
