/*
 * PROJECT:     LiberNT FreeLoader UEFI Support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Debug log file on the volume the loader was started from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <uefildr.h>
#include <SimpleFileSystem.h>

extern EFI_HANDLE GlobalImageHandle;
extern EFI_SYSTEM_TABLE *GlobalSystemTable;

static EFI_GUID EfiLoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID EfiSimpleFileSystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

static EFI_FILE_PROTOCOL *UefiLogFile;
static CHAR UefiLogLine[512];
static UINTN UefiLogLineLength;
static BOOLEAN UefiLogBusy;

static VOID
UefiLogWrite(VOID)
{
    EFI_FILE_PROTOCOL *File = UefiLogFile;
    UINTN Size = UefiLogLineLength;

    if (!File || !Size || UefiLogBusy)
        return;

    UefiLogBusy = TRUE;
    UefiLogLineLength = 0;
    if (EFI_ERROR(File->Write(File, &Size, UefiLogLine)) ||
        EFI_ERROR(File->Flush(File)))
    {
        UefiLogFile = NULL;
    }
    UefiLogBusy = FALSE;
}

VOID
UefiLogPutChar(
    _In_ UCHAR Character)
{
    if (!UefiLogFile || UefiLogBusy)
        return;

    UefiLogLine[UefiLogLineLength++] = (CHAR)Character;
    if (Character == '\n' || UefiLogLineLength == sizeof(UefiLogLine))
        UefiLogWrite();
}

VOID
UefiLogInitialize(VOID)
{
    EFI_BOOT_SERVICES *BootServices = GlobalSystemTable->BootServices;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_STATUS Status;
    EFI_TIME Time;

    if (UefiLogFile)
        return;

    Status = BootServices->HandleProtocol(GlobalImageHandle,
                                          &EfiLoadedImageGuid,
                                          (VOID**)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage || !LoadedImage->DeviceHandle)
        return;

    Status = BootServices->HandleProtocol(LoadedImage->DeviceHandle,
                                          &EfiSimpleFileSystemGuid,
                                          (VOID**)&FileSystem);
    if (EFI_ERROR(Status) || !FileSystem)
        return;

    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status) || !Root)
        return;

    Status = Root->Open(Root,
                        &File,
                        L"\\uefildr.log",
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                        0);
    Root->Close(Root);
    if (EFI_ERROR(Status) || !File)
        return;

    if (EFI_ERROR(File->SetPosition(File, 0xFFFFFFFFFFFFFFFFULL)))
    {
        File->Close(File);
        return;
    }

    UefiLogFile = File;

    RtlZeroMemory(&Time, sizeof(Time));
    GlobalSystemTable->RuntimeServices->GetTime(&Time, NULL);
    if (NT_SUCCESS(RtlStringCbPrintfA(UefiLogLine,
                                      sizeof(UefiLogLine),
                                      "\n==== uefildr %04u-%02u-%02u %02u:%02u:%02u ====\n",
                                      Time.Year, Time.Month, Time.Day,
                                      Time.Hour, Time.Minute, Time.Second)))
    {
        UefiLogLineLength = strlen(UefiLogLine);
        UefiLogWrite();
    }
}

VOID
UefiLogClose(VOID)
{
    EFI_FILE_PROTOCOL *File;

    UefiLogWrite();
    File = UefiLogFile;
    UefiLogFile = NULL;
    if (File)
        File->Close(File);
}
