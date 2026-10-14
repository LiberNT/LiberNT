/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Debug output through the firmware serial port
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

static EFI_GUID UefiSerialIoGuid = EFI_SERIAL_IO_PROTOCOL_GUID;
static EFI_GUID UefiGraphicsOutputGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_SERIAL_IO_PROTOCOL* UefiSerial;
static BOOLEAN UefiSerialProbed;
static BOOLEAN UefiSerialUseConsole;

VOID (*FwLateDebugPutChar)(UCHAR Character);

static
VOID
UefiSerialProbe(VOID)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    VOID* GraphicsOutput;

    UefiSerialProbed = TRUE;

    if (!EFI_ERROR(BootServices->LocateProtocol(&UefiSerialIoGuid, NULL, (VOID**)&UefiSerial)) &&
        UefiSerial)
    {
        return;
    }

    UefiSerial = NULL;
    UefiSerialUseConsole = GlobalSystemTable->ConOut &&
                           EFI_ERROR(BootServices->LocateProtocol(&UefiGraphicsOutputGuid,
                                                                  NULL,
                                                                  &GraphicsOutput));
}

VOID
UefiSerialPutChar(
    _In_ UCHAR Character)
{
    CHAR16 Text[2] = {Character, 0};
    UINTN Size = 1;

    if (BootServicesExitedFlag)
    {
        if (FwLateDebugPutChar)
            FwLateDebugPutChar(Character);
        return;
    }

    if (!UefiSerialProbed)
        UefiSerialProbe();

    if (UefiSerial)
        UefiSerial->Write(UefiSerial, &Size, &Character);
    else if (UefiSerialUseConsole)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, Text);
    else if (FwLateDebugPutChar)
        FwLateDebugPutChar(Character);
}

VOID
UefiSerialDisableFirmware(VOID)
{
    UefiSerialProbed = TRUE;
    UefiSerial = NULL;
    UefiSerialUseConsole = FALSE;
}

VOID
FwDebugPrint(
    _In_ PCSTR Text)
{
    for (; *Text; Text++)
    {
        if (*Text == '\n')
            UefiSerialPutChar('\r');
        UefiSerialPutChar(*Text);
    }
}
