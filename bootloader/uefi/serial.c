/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Debug output through the firmware serial port
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

#define UEFI_SERIAL_MAX_PORTS  16
#define UEFI_SERIAL_PNP_ID     EISA_PNP_ID(0x0501)
#define UEFI_SERIAL_LINE_CHARS 128
#define UEFI_SERIAL_DEBUG_BAUD 115200

static EFI_GUID UefiSerialIoGuid = EFI_SERIAL_IO_PROTOCOL_GUID;
static EFI_GUID UefiGraphicsOutputGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID UefiDevicePathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_SERIAL_IO_PROTOCOL* UefiSerialPorts[UEFI_SERIAL_MAX_PORTS];
static EFI_HANDLE UefiSerialHandles[UEFI_SERIAL_MAX_PORTS];
static ULONG UefiSerialCount;
static BOOLEAN UefiSerialProbed;
static BOOLEAN UefiSerialUseConsole;

VOID (*FwLateDebugPutChar)(UCHAR Character);

static
BOOLEAN
UefiSerialSamePort(
    _In_ EFI_HANDLE First,
    _In_ EFI_HANDLE Second)
{
    EFI_DEVICE_PATH_PROTOCOL *A, *B;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(First, &UefiDevicePathGuid, (VOID**)&A)) ||
        EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(Second, &UefiDevicePathGuid, (VOID**)&B)))
    {
        return FALSE;
    }

    for (;;)
    {
        while (!IsDevicePathEndType(A) && DevicePathNodeLength(A) >= sizeof(EFI_DEVICE_PATH_PROTOCOL) &&
               DevicePathType(A) == MESSAGING_DEVICE_PATH && DevicePathSubType(A) == MSG_UART_DP)
        {
            A = NextDevicePathNode(A);
        }
        while (!IsDevicePathEndType(B) && DevicePathNodeLength(B) >= sizeof(EFI_DEVICE_PATH_PROTOCOL) &&
               DevicePathType(B) == MESSAGING_DEVICE_PATH && DevicePathSubType(B) == MSG_UART_DP)
        {
            B = NextDevicePathNode(B);
        }

        if (IsDevicePathEndType(A) || IsDevicePathEndType(B))
            return IsDevicePathEndType(A) && IsDevicePathEndType(B);

        if (DevicePathNodeLength(A) != DevicePathNodeLength(B) ||
            DevicePathNodeLength(A) < sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            memcmp(A, B, DevicePathNodeLength(A)) != 0)
        {
            return FALSE;
        }

        A = NextDevicePathNode(A);
        B = NextDevicePathNode(B);
    }
}

static
VOID
UefiSerialProbe(VOID)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_HANDLE* Handles;
    VOID* GraphicsOutput;
    UINTN Count, i;
    ULONG j;

    UefiSerialProbed = TRUE;
    UefiSerialCount = 0;

    if (!EFI_ERROR(BootServices->LocateHandleBuffer(ByProtocol, &UefiSerialIoGuid, NULL, &Count, &Handles)))
    {
        for (i = 0; i < Count && UefiSerialCount < (DBG ? UEFI_SERIAL_MAX_PORTS : 1); i++)
        {
            for (j = 0; j < UefiSerialCount && !UefiSerialSamePort(UefiSerialHandles[j], Handles[i]); j++)
            {
            }

            if (j == UefiSerialCount &&
                !EFI_ERROR(BootServices->HandleProtocol(Handles[i],
                                                        &UefiSerialIoGuid,
                                                        (VOID**)&UefiSerialPorts[UefiSerialCount])) &&
                UefiSerialPorts[UefiSerialCount])
            {
                UefiSerialHandles[UefiSerialCount++] = Handles[i];
            }
        }
        BootServices->FreePool(Handles);
    }

    for (i = 0; DBG && i < UefiSerialCount; i++)
    {
        if (UefiSerialPorts[i]->Mode && UefiSerialPorts[i]->Mode->BaudRate == 0)
        {
            UefiSerialPorts[i]->SetAttributes(UefiSerialPorts[i], UEFI_SERIAL_DEBUG_BAUD, 0, 0, NoParity, 8, OneStopBit);
            UefiSerialPorts[i]->SetControl(UefiSerialPorts[i],
                                           EFI_SERIAL_DATA_TERMINAL_READY | EFI_SERIAL_REQUEST_TO_SEND);
        }
    }

    if (UefiSerialCount)
        return;

    UefiSerialUseConsole = GlobalSystemTable->ConOut &&
                           EFI_ERROR(BootServices->LocateProtocol(&UefiGraphicsOutputGuid,
                                                                  NULL,
                                                                  &GraphicsOutput));
}

static
VOID
UefiSerialWritePort(
    _In_ ULONG Port,
    _In_ PCSTR Text)
{
    UINTN Size;

    for (; *Text; Text++)
    {
        if (*Text == '\n')
        {
            Size = 1;
            UefiSerialPorts[Port]->Write(UefiSerialPorts[Port], &Size, (VOID*)"\r");
        }
        Size = 1;
        UefiSerialPorts[Port]->Write(UefiSerialPorts[Port], &Size, (VOID*)Text);
    }
}

VOID
UefiSerialPutChar(
    _In_ UCHAR Character)
{
    CHAR16 Text[2] = {Character, 0};
    UINTN Size;
    ULONG i;

    if (BootServicesExitedFlag)
    {
        if (FwLateDebugPutChar)
            FwLateDebugPutChar(Character);
        return;
    }

    if (!UefiSerialProbed)
        UefiSerialProbe();

    for (i = 0; i < UefiSerialCount; i++)
    {
        Size = 1;
        UefiSerialPorts[i]->Write(UefiSerialPorts[i], &Size, &Character);
    }

    if (UefiSerialCount)
        return;

    if (UefiSerialUseConsole)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, Text);
    else if (FwLateDebugPutChar)
        FwLateDebugPutChar(Character);
}

VOID
UefiSerialDisableFirmware(VOID)
{
    UefiSerialProbed = TRUE;
    UefiSerialCount = 0;
    UefiSerialUseConsole = FALSE;
}

VOID
FwDebugPrint(
    _In_ PCSTR Text)
{
    PCSTR Next;

    if (!UefiSerialProbed && !BootServicesExitedFlag)
        UefiSerialProbe();

    for (Next = Text; *Next; Next++)
    {
        if (*Next == '\n')
            UefiSerialPutChar('\r');
        UefiSerialPutChar(*Next);
    }
    UdpLogWrite(Text);
    BootLogWrite(Text);
}

static
VOID
UefiSerialDescribe(
    _In_ ULONG Port,
    _Out_writes_z_(Chars) PSTR Name,
    _In_ ULONG Chars)
{
    EFI_DEVICE_PATH_PROTOCOL* Path;
    ACPI_EXTENDED_HID_DEVICE_PATH Extended;
    ACPI_HID_DEVICE_PATH Acpi;
    PCI_DEVICE_PATH Pci;
    PCSTR HidText;
    SIZE_T Length;

    Name[0] = ANSI_NULL;
    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(UefiSerialHandles[Port],
                                                                  &UefiDevicePathGuid,
                                                                  (VOID**)&Path)))
    {
        RtlStringCchCopyA(Name, Chars, "no device path");
        return;
    }

    for (; Path && !IsDevicePathEndType(Path); Path = NextDevicePathNode(Path))
    {
        if (DevicePathNodeLength(Path) < sizeof(EFI_DEVICE_PATH_PROTOCOL))
            return;

        Length = strlen(Name);
        if (DevicePathType(Path) == ACPI_DEVICE_PATH && DevicePathSubType(Path) == ACPI_DP &&
            DevicePathNodeLength(Path) >= sizeof(Acpi))
        {
            RtlCopyMemory(&Acpi, Path, sizeof(Acpi));
            if (Acpi.HID == UEFI_SERIAL_PNP_ID)
                RtlStringCchPrintfA(Name + Length, Chars - Length, "%sCOM%lu", Length ? " " : "", Acpi.UID + 1);
            else if ((Acpi.HID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST)
                RtlStringCchPrintfA(Name + Length, Chars - Length, "%sPNP%04lX(%lu)", Length ? " " : "",
                                    EISA_ID_TO_NUM(Acpi.HID), Acpi.UID);
            else
                RtlStringCchPrintfA(Name + Length, Chars - Length, "%sACPI(%08lX,%lu)", Length ? " " : "",
                                    Acpi.HID, Acpi.UID);
        }
        else if (DevicePathType(Path) == ACPI_DEVICE_PATH && DevicePathSubType(Path) == ACPI_EXTENDED_DP &&
                 DevicePathNodeLength(Path) > sizeof(Extended))
        {
            RtlCopyMemory(&Extended, Path, sizeof(Extended));
            HidText = (PCSTR)Path + sizeof(Extended);
            if (Extended.HID == UEFI_SERIAL_PNP_ID || Extended.CID == UEFI_SERIAL_PNP_ID ||
                strncmp(HidText, "PNP0501", DevicePathNodeLength(Path) - sizeof(Extended)) == 0)
            {
                RtlStringCchPrintfA(Name + Length, Chars - Length, "%sCOM%lu", Length ? " " : "", Extended.UID + 1);
            }
            else
            {
                RtlStringCchPrintfA(Name + Length, Chars - Length, "%sACPI(%08lX,%lu,%08lX)", Length ? " " : "",
                                    Extended.HID, Extended.UID, Extended.CID);
            }
        }
        else if (DevicePathType(Path) == HARDWARE_DEVICE_PATH && DevicePathSubType(Path) == HW_PCI_DP &&
                 DevicePathNodeLength(Path) >= sizeof(Pci))
        {
            RtlCopyMemory(&Pci, Path, sizeof(Pci));
            RtlStringCchPrintfA(Name + Length, Chars - Length, "%sPCI(%u.%u)", Length ? " " : "",
                                Pci.Device, Pci.Function);
        }
        else if (DevicePathType(Path) != MESSAGING_DEVICE_PATH || DevicePathSubType(Path) != MSG_UART_DP)
        {
            RtlStringCchPrintfA(Name + Length, Chars - Length, "%s%u/%u", Length ? " " : "",
                                DevicePathType(Path), DevicePathSubType(Path));
        }
    }
}

VOID
FwDebugAnnounce(
    _In_ PCSTR Application,
    _In_ BOOLEAN NewLog)
{
    CHAR Line[UEFI_SERIAL_LINE_CHARS], Name[UEFI_SERIAL_LINE_CHARS / 2];
    ULONG Port, Adapters, i;

    if (!DBG || BootServicesExitedFlag)
        return;

    BootLogStart(NewLog);
    if (!UefiSerialProbed)
        UefiSerialProbe();

    RtlStringCchPrintfA(Line,
                        RTL_NUMBER_OF(Line),
                        "\n%s, firmware %ls revision 0x%08lX, UEFI %lu.%02lu\n",
                        Application,
                        GlobalSystemTable->FirmwareVendor ? GlobalSystemTable->FirmwareVendor : L"(unnamed)",
                        (ULONG)GlobalSystemTable->FirmwareRevision,
                        (ULONG)(GlobalSystemTable->Hdr.Revision >> 16),
                        (ULONG)(GlobalSystemTable->Hdr.Revision & 0xFFFF));
    FwDebugPrint(Line);
    if (UefiSerialCount == 0)
        FwDebugPrint("No firmware serial port\n");

    for (Port = 0; Port < UefiSerialCount; Port++)
    {
        UefiSerialDescribe(Port, Name, RTL_NUMBER_OF(Name));
        RtlStringCchPrintfA(Line,
                            RTL_NUMBER_OF(Line),
                            "Serial port %lu of %lu: %s, %I64u baud",
                            Port + 1,
                            UefiSerialCount,
                            Name,
                            UefiSerialPorts[Port]->Mode ? (ULONGLONG)UefiSerialPorts[Port]->Mode->BaudRate : 0ULL);
        for (i = 0; i < UefiSerialCount; i++)
        {
            UefiSerialWritePort(i, Line);
            UefiSerialWritePort(i, i == Port ? " <- this port\n" : "\n");
        }
        UdpLogWrite(Line);
        UdpLogWrite("\n");
        BootLogWrite(Line);
        BootLogWrite("\n");
    }

    Adapters = UdpLogOpen();
    RtlStringCchPrintfA(Line, RTL_NUMBER_OF(Line), "UDP log broadcast to port 6666 on %lu network adapters\n", Adapters);
    FwDebugPrint(Line);
}
