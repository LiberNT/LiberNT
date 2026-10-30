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
static BOOLEAN FwChainScanTraceStarted;
static CHAR FwChainScanTraceLines[FW_CHAIN_SCAN_TRACE_MAX][160];
static ULONG FwChainScanTraceCount;

VOID
FwTraceChainScan(
    _In_ PCWSTR Format,
    ...)
{
    WCHAR Message[160];
    va_list Arguments;
    ULONG i;

    va_start(Arguments, Format);
    RtlStringCchVPrintfW(Message, RTL_NUMBER_OF(Message), Format, Arguments);
    va_end(Arguments);

    if (FwChainScanTraceCount < RTL_NUMBER_OF(FwChainScanTraceLines))
    {
        for (i = 0; i < RTL_NUMBER_OF(Message) - 1 && Message[i]; i++)
            FwChainScanTraceLines[FwChainScanTraceCount][i] =
                (Message[i] >= 0x20 && Message[i] <= 0x7E) ? (CHAR)Message[i] : '?';

        FwChainScanTraceLines[FwChainScanTraceCount][i] = ANSI_NULL;
        FwChainScanTraceCount++;
    }

    FwPrint(L"%s\r\n", Message);
}

ULONG
FwGetChainScanTrace(
    _Out_writes_to_(Capacity, return) PCSTR* Lines,
    _In_ ULONG Capacity)
{
    ULONG i;
    ULONG Count = min(Capacity, FwChainScanTraceCount);

    for (i = 0; i < Count; i++)
        Lines[i] = FwChainScanTraceLines[i];

    return Count;
}

static
BOOLEAN
FwIsUsbDevicePath(
    _In_ EFI_HANDLE Handle)
{
    EFI_DEVICE_PATH_PROTOCOL* Path;
    EFI_DEVICE_PATH_PROTOCOL* Node;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(Handle,
                                                                  &FwDevicePathGuid,
                                                                  (VOID**)&Path)))
    {
        return FALSE;
    }

    for (Node = Path; !IsDevicePathEnd(Node); Node = NextDevicePathNode(Node))
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
FwTraceUsbHandles(VOID)
{
    EFI_HANDLE* Handles;
    UINTN Count, i;
    ULONG UsbCount = 0;

    if (EFI_ERROR(GlobalSystemTable->BootServices->LocateHandleBuffer(AllHandles,
                                                                      NULL,
                                                                      NULL,
                                                                      &Count,
                                                                      &Handles)))
    {
        FwTraceChainScan(L"Chain scan: cannot enumerate firmware handles.");
        return;
    }

    for (i = 0; i < Count; i++)
    {
        if (FwIsUsbDevicePath(Handles[i]))
            UsbCount++;
    }

    FwTraceChainScan(L"Chain scan: firmware handles %llu, USB device-path handles %lu",
                     (ULONGLONG)Count,
                     UsbCount);
    GlobalSystemTable->BootServices->FreePool(Handles);
}

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

BOOLEAN
FwGetBootDiskIndex(
    _Out_ PULONG Index)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_DEVICE_PATH_PROTOCOL *BootPath, *DiskPath, *Node;
    EFI_HANDLE Handle = FwGetBootDeviceHandle();
    DK_DEVICE Disk;
    SIZE_T Size;
    ULONG Count;
    UINTN i;

    if (!Handle ||
        EFI_ERROR(BootServices->HandleProtocol(Handle, &FwDevicePathGuid, (VOID**)&BootPath)) ||
        !FwGetDisk(0, &Disk))
    {
        return FALSE;
    }

    for (i = 0, Count = 0; i < FwDiskHandleCount; i++)
    {
        if (!FwOpenBlockDevice(FwDiskHandles[i], TRUE, &Disk))
            continue;

        if (!EFI_ERROR(BootServices->HandleProtocol(FwDiskHandles[i], &FwDevicePathGuid, (VOID**)&DiskPath)))
        {
            Size = FwDevicePathSize(DiskPath);
            Node = (EFI_DEVICE_PATH_PROTOCOL*)((PUCHAR)BootPath + Size);
            if (Size < FwDevicePathSize(BootPath) &&
                RtlEqualMemory(BootPath, DiskPath, Size) &&
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

BOOLEAN
FwScanFileVolumes(
    _In_ PCWSTR Path,
    _Inout_ PFW_VOLUME_SCAN Scan,
    _Out_ PVOID* Device)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    EFI_FILE_PROTOCOL* Root;
    EFI_FILE_PROTOCOL* File;
    EFI_HANDLE* Handles;
    EFI_HANDLE Handle;
    UINTN HandleCount;
    EFI_STATUS Status;

    *Device = NULL;

    if (!FwChainScanTraceStarted)
    {
        FwChainScanTraceStarted = TRUE;
        FwTraceChainScan(L"Chain scan: looking for %s", Path);
        FwTraceUsbHandles();
    }

    if (Scan->State == FwVolumeScanConnect && FwDisksConnected && !Scan->Handles)
    {
        FwTraceChainScan(L"Chain scan: controllers already connected; skipping Block I/O connect phase");
        Scan->State = FwVolumeScanVolumes;
    }

    if (Scan->State == FwVolumeScanDone)
        return FALSE;

    if (!Scan->Handles)
    {
        Status = BootServices->LocateHandleBuffer(ByProtocol,
                                                  Scan->State == FwVolumeScanConnect ? &FwBlockIoGuid : &FwFileSystemGuid,
                                                  NULL,
                                                  &HandleCount,
                                                  &Handles);
        if (EFI_ERROR(Status))
        {
            FwTraceChainScan(L"Chain scan: locate %s failed, status %llx",
                             Scan->State == FwVolumeScanConnect ? L"Block I/O" : L"Simple File System",
                             (ULONGLONG)Status);
            Scan->State = (Scan->State == FwVolumeScanConnect) ? FwVolumeScanVolumes : FwVolumeScanDone;
            return Scan->State != FwVolumeScanDone;
        }

        Scan->Handles = Handles;
        Scan->HandleCount = (ULONG)HandleCount;
        Scan->Index = 0;
        FwTraceChainScan(L"Chain scan: found %llu %s handles",
                         (ULONGLONG)HandleCount,
                         Scan->State == FwVolumeScanConnect ? L"Block I/O" : L"Simple File System");
        return TRUE;
    }

    if (Scan->Index < Scan->HandleCount)
    {
        Handle = Scan->Handles[Scan->Index++];

        if (Scan->State == FwVolumeScanConnect)
        {
            if (!FwDisksConnected)
            {
                Status = BootServices->ConnectController(Handle, NULL, NULL, TRUE);
                FwTraceChainScan(L"Chain scan: connect Block I/O %lu/%lu handle %p %s status %llx",
                                 Scan->Index,
                                 Scan->HandleCount,
                                 Handle,
                                 FwIsUsbDevicePath(Handle) ? L"USB" : L"other",
                                 (ULONGLONG)Status);
            }
        }
        else
        {
            Status = BootServices->HandleProtocol(Handle, &FwFileSystemGuid, (VOID**)&FileSystem);
            FwTraceChainScan(L"Chain scan: Simple File System %lu/%lu handle %p %s status %llx",
                             Scan->Index,
                             Scan->HandleCount,
                             Handle,
                             FwIsUsbDevicePath(Handle) ? L"USB" : L"other",
                             (ULONGLONG)Status);
            if (!EFI_ERROR(Status))
            {
                Status = FileSystem->OpenVolume(FileSystem, &Root);
                FwTraceChainScan(L"Chain scan: open volume status %llx", (ULONGLONG)Status);
                if (!EFI_ERROR(Status))
                {
                    Status = Root->Open(Root, &File, (CHAR16*)Path, EFI_FILE_MODE_READ, 0);
                    FwTraceChainScan(L"Chain scan: open requested file status %llx", (ULONGLONG)Status);
                    if (!EFI_ERROR(Status))
                    {
                        File->Close(File);
                        *Device = Handle;
                        FwTraceChainScan(L"Chain scan: found chainload file on handle %p", Handle);
                    }

                    Root->Close(Root);
                }
            }
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
