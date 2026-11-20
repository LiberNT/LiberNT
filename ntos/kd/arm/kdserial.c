/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KD serial provider over the ARMv7 PL011 console
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <cportlib/uartinfo.h>

BOOLEAN
NTAPI
KdPortIsComPortNumberValid(
    _In_ ULONG ComPortNumber)
{
    return ComPortNumber > 0;
}

BOOLEAN
NTAPI
KdPortInitializeEx(
    _Inout_ PCPPORT PortInformation,
    _In_ ULONG ComPortNumber)
{
    UCHAR Byte;
    ULONG Drained;

    /* The firmware-described console is authoritative; the COM number is ignored. */
    UNREFERENCED_PARAMETER(ComPortNumber);

    PortInformation->Address = KiArmConsoleReady() ?
                               (PUCHAR)(ULONG_PTR)KeLoaderBlock->u.Arm.EarlyUartAddress : NULL;
    if (PortInformation->BaudRate == 0)
        PortInformation->BaudRate = DEFAULT_DEBUG_BAUD_RATE;
    PortInformation->Flags |= CPPORT_FLAG_KEEP_BAUD;
    if (PortInformation->Address == NULL)
        return FALSE;

    for (Drained = 0; Drained < 64; Drained++)
    {
        if (!KiArmConsoleGetByte(&Byte))
            break;
    }
    return TRUE;
}

BOOLEAN
NTAPI
KdPortGetByteEx(
    _Inout_ PCPPORT PortInformation,
    _Out_ PUCHAR ByteReceived)
{
    UNREFERENCED_PARAMETER(PortInformation);
    return KiArmConsoleGetByte(ByteReceived);
}

VOID
NTAPI
KdPortPutByteEx(
    _Inout_ PCPPORT PortInformation,
    _In_ UCHAR ByteToSend)
{
    UNREFERENCED_PARAMETER(PortInformation);
    KiArmConsolePutByte(ByteToSend);
}

VOID
NTAPI
KdPortPutBufferEx(
    _Inout_ PCPPORT PortInformation,
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ ULONG Length)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(PortInformation);
    for (Index = 0; Index < Length; Index++)
        KiArmConsolePutByte((UCHAR)Buffer[Index]);
}

BOOLEAN
KdpArchKeyboardControllerPresent(VOID)
{
    return FALSE;
}
