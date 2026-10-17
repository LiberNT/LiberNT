/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI firmware definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <bl.h>

#include <Uefi.h>
#include <DevicePath.h>
#include <LoadedImage.h>
#include <SimpleFileSystem.h>
#include <GraphicsOutput.h>
#include <BlockIo.h>
#include <SerialIo.h>
#include <Acpi.h>
#include <GlobalVariable.h>

extern EFI_HANDLE GlobalImageHandle;
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern volatile BOOLEAN BootServicesExitedFlag;

EFI_HANDLE
FwGetBootDeviceHandle(VOID);

VOID
UefiExitBootServices(VOID);

VOID
UefiLeaveBootServices(VOID);

VOID
UefiSerialPutChar(
    _In_ UCHAR Character);
