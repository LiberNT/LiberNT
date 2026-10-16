/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI entry point and firmware services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

#define FW_FILE_NAME_CHARS 260

EFI_HANDLE GlobalImageHandle;
EFI_SYSTEM_TABLE* GlobalSystemTable;
volatile BOOLEAN BootServicesExitedFlag;

static EFI_GUID FwLoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID FwFileSystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID FwFileInfoGuid = EFI_FILE_INFO_ID;
static EFI_GUID FwBlockIoGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
static EFI_GUID FwDevicePathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;

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
    va_list Arguments;
    ULONG i;

    va_start(Arguments, Format);
    RtlStringCchVPrintfW(Text, RTL_NUMBER_OF(Text), Format, Arguments);
    va_end(Arguments);

    if (BootServicesExitedFlag || !GlobalSystemTable->ConOut)
    {
        for (i = 0; Text[i] != UNICODE_NULL; i++)
            UefiSerialPutChar(Text[i] <= 0x7F ? (UCHAR)Text[i] : '?');
        return;
    }

    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, (CHAR16*)Text);
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

BOOLEAN
FwRescanDisks(VOID)
{
    static BOOLEAN Connected;
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_HANDLE* Handles;
    UINTN Count, i;

    if (Connected ||
        EFI_ERROR(BootServices->LocateHandleBuffer(AllHandles, NULL, NULL, &Count, &Handles)))
    {
        return FALSE;
    }

    for (i = 0; i < Count; i++)
        BootServices->ConnectController(Handles[i], NULL, NULL, TRUE);

    BootServices->FreePool(Handles);

    if (FwDiskHandles)
    {
        BootServices->FreePool(FwDiskHandles);
        FwDiskHandles = NULL;
    }

    Connected = TRUE;
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
            if (Data &&
                (EFI_ERROR(File->Read(File, &Length, Data)) ||
                 Length != FileInfo.Info.FileSize))
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

ULONG
FwFindFileVolumes(
    _In_ PCWSTR Path,
    _Out_writes_(MaxDevices) PVOID* Devices,
    _In_ ULONG MaxDevices)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    EFI_FILE_PROTOCOL* Root;
    EFI_FILE_PROTOCOL* File;
    EFI_HANDLE* Handles;
    UINTN HandleCount, i;
    ULONG Count = 0;

    FwRescanDisks();

    if (EFI_ERROR(BootServices->LocateHandleBuffer(ByProtocol, &FwFileSystemGuid, NULL, &HandleCount, &Handles)))
        return 0;

    for (i = 0; i < HandleCount && Count < MaxDevices; i++)
    {
        if (EFI_ERROR(BootServices->HandleProtocol(Handles[i], &FwFileSystemGuid, (VOID**)&FileSystem)) ||
            EFI_ERROR(FileSystem->OpenVolume(FileSystem, &Root)))
        {
            continue;
        }

        if (!EFI_ERROR(Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, 0)))
        {
            File->Close(File);
            Devices[Count++] = Handles[i];
        }

        Root->Close(Root);
    }

    BootServices->FreePool(Handles);
    return Count;
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
