/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Debug log file on the boot volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

#define BOOTLOG_PATH   L"\\EFI\\LiberNT\\Boot\\boot.log"
#define BOOTLOG_BYTES  4096

static EFI_GUID BootLogFileSystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID BootLogBlockIoGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
static EFI_GUID BootLogDevicePathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_FILE_PROTOCOL* BootLogRoot;
static EFI_FILE_PROTOCOL* BootLogFile;
static BOOLEAN BootLogEnabled;
static BOOLEAN BootLogTruncate;
static BOOLEAN BootLogFailed;
static BOOLEAN BootLogActive;
static CHAR BootLogBuffer[BOOTLOG_BYTES];
static ULONG BootLogLength;

static
BOOLEAN
BootLogTplAllowed(VOID)
{
    EFI_TPL Tpl;

    Tpl = GlobalSystemTable->BootServices->RaiseTPL(TPL_HIGH_LEVEL);
    GlobalSystemTable->BootServices->RestoreTPL(Tpl);
    return Tpl <= TPL_CALLBACK;
}

static
BOOLEAN
BootLogVolumeWritable(
    _In_ EFI_HANDLE Device)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_BLOCK_IO_PROTOCOL* BlockIo;
    EFI_DEVICE_PATH_PROTOCOL* Node;

    if (!EFI_ERROR(BootServices->HandleProtocol(Device, &BootLogBlockIoGuid, (VOID**)&BlockIo)) &&
        BlockIo->Media->ReadOnly)
    {
        return FALSE;
    }

    if (EFI_ERROR(BootServices->HandleProtocol(Device, &BootLogDevicePathGuid, (VOID**)&Node)))
        return TRUE;

    for (; !IsDevicePathEnd(Node); Node = NextDevicePathNode(Node))
    {
        if (DevicePathType(Node) == MEDIA_DEVICE_PATH && DevicePathSubType(Node) == MEDIA_CDROM_DP)
            return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
BootLogOpen(VOID)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    EFI_FILE_PROTOCOL* Existing;
    EFI_HANDLE Device;

    if (BootLogFile)
        return TRUE;

    Device = FwGetBootDeviceHandle();
    if (!Device ||
        !BootLogVolumeWritable(Device) ||
        EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(Device,
                                                                  &BootLogFileSystemGuid,
                                                                  (VOID**)&FileSystem)) ||
        EFI_ERROR(FileSystem->OpenVolume(FileSystem, &BootLogRoot)))
    {
        BootLogFailed = TRUE;
        return FALSE;
    }

    if (BootLogTruncate &&
        !EFI_ERROR(BootLogRoot->Open(BootLogRoot,
                                     &Existing,
                                     BOOTLOG_PATH,
                                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                                     0)))
    {
        Existing->Delete(Existing);
    }
    BootLogTruncate = FALSE;

    if (EFI_ERROR(BootLogRoot->Open(BootLogRoot,
                                    &BootLogFile,
                                    BOOTLOG_PATH,
                                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                                    0)) ||
        EFI_ERROR(BootLogFile->SetPosition(BootLogFile, MAXULONGLONG)))
    {
        if (BootLogFile)
            BootLogFile->Close(BootLogFile);
        BootLogFile = NULL;
        BootLogRoot->Close(BootLogRoot);
        BootLogRoot = NULL;
        BootLogFailed = TRUE;
        return FALSE;
    }

    return TRUE;
}

static
VOID
BootLogFlush(VOID)
{
    UINTN Size = BootLogLength;

    if (!BootLogLength || BootLogFailed || !BootLogTplAllowed() || !BootLogOpen())
        return;

    if (EFI_ERROR(BootLogFile->Write(BootLogFile, &Size, BootLogBuffer)) || Size != BootLogLength)
    {
        BootLogFailed = TRUE;
        return;
    }

    BootLogFile->Flush(BootLogFile);
    BootLogLength = 0;
}

VOID
BootLogStart(
    _In_ BOOLEAN NewLog)
{
    if (!DBG || BootLogEnabled)
        return;

    BootLogEnabled = TRUE;
    BootLogTruncate = NewLog;
}

VOID
BootLogWrite(
    _In_ PCSTR Text)
{
    BOOLEAN Line = FALSE;

    if (!BootLogEnabled || BootLogFailed || BootLogActive || BootServicesExitedFlag)
        return;

    BootLogActive = TRUE;
    for (; *Text; Text++)
    {
        if (*Text == '\n' && BootLogLength < BOOTLOG_BYTES)
            BootLogBuffer[BootLogLength++] = '\r';
        if (BootLogLength < BOOTLOG_BYTES)
            BootLogBuffer[BootLogLength++] = *Text;
        Line |= (*Text == '\n');

        if (BootLogLength >= BOOTLOG_BYTES - 1)
            BootLogFlush();
    }

    if (Line)
        BootLogFlush();
    BootLogActive = FALSE;
}

VOID
BootLogClose(VOID)
{
    if (!BootLogEnabled || BootServicesExitedFlag || BootLogActive)
        return;

    BootLogActive = TRUE;
    BootLogFlush();
    if (BootLogFile && BootLogTplAllowed())
    {
        BootLogFile->Close(BootLogFile);
        BootLogRoot->Close(BootLogRoot);
        BootLogFile = NULL;
        BootLogRoot = NULL;
    }
    BootLogActive = FALSE;
}
