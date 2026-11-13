/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KD serial provider over the LoongArch64 early console
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
/* The early console the kernel owns (ntos/ke/loongarch64/console.c) is the
 * only byte stream that exists before the HAL has mapped MMIO. */
#include "../../ke/loongarch64/la64ke.h"
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
    PLOONGARCH64_LOADER_BLOCK LaBlock = &KeLoaderBlock->u.LoongArch64;
    PUCHAR Base = NULL;
    UCHAR Byte;
    ULONG Drained;

    /* The firmware-described console is authoritative; the COM number and
     * any DEBUGPORT=COM:<addr> override are accepted and ignored. */
    UNREFERENCED_PARAMETER(ComPortNumber);

    if (KiLa64ConsoleReady() &&
        (KiLa64ConsoleInterface() == LOONGARCH64_EARLY_CONSOLE_NS16550))
    {
        Base = (PUCHAR)(ULONG_PTR)(LaBlock->UncachedBase + LaBlock->EarlyConsoleAddress);
    }

    PortInformation->Address = Base;
    if (PortInformation->BaudRate == 0)
        PortInformation->BaudRate = DEFAULT_DEBUG_BAUD_RATE;
    PortInformation->Flags |= CPPORT_FLAG_KEEP_BAUD;
    if (Base == NULL)
        return FALSE;

    /* Drop stale input so the first poll does not see boot-time noise. */
    for (Drained = 0; Drained < 64; Drained++)
    {
        if (!KiLa64ConsoleGetByte(&Byte))
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
    return KiLa64ConsoleGetByte(ByteReceived);
}

VOID
NTAPI
KdPortPutByteEx(
    _Inout_ PCPPORT PortInformation,
    _In_ UCHAR ByteToSend)
{
    UNREFERENCED_PARAMETER(PortInformation);
    KiLa64ConsolePutByte(ByteToSend);
}

VOID
NTAPI
KdPortPutBufferEx(
    _Inout_ PCPPORT PortInformation,
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(PortInformation);
    KiLa64ConsoleWrite(Buffer, Length);
}

BOOLEAN
KdpArchKeyboardControllerPresent(VOID)
{
    /* No 8042 controller: input arrives on the serial line only. */
    return FALSE;
}

/* EOF */
