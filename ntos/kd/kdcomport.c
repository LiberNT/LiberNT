/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Putting the debug output on a serial line
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * One string down the line. A terminal at the other end expects a carriage
 * return of its own before every line feed, so the output is sent in runs and
 * the return is inserted where it is missing.
 */
static
VOID
NTAPI
KdpSerialPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    ULONG Start = 0;
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (String[Index] != '\n')
            continue;

        if (Index > Start)
            KdPortPutBufferEx(&SerialPortInfo, &String[Start], Index - Start);

        KdPortPutByteEx(&SerialPortInfo, '\r');
        KdPortPutByteEx(&SerialPortInfo, '\n');

        Start = Index + 1;
    }

    if (Length > Start)
        KdPortPutBufferEx(&SerialPortInfo, &String[Start], Length - Start);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The serial line is the one way out that works from the very first phase,
 * which is why a machine that is being brought up is debugged over it.
 */
NTSTATUS
NTAPI
KdpSerialInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    if (!KdpDebugMode.Serial)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        if (!KdPortInitializeEx(&SerialPortInfo, SerialPortNumber))
        {
            /* No such line on this machine */
            KdpDebugMode.Serial = FALSE;
            return STATUS_DEVICE_DOES_NOT_EXIST;
        }

        DispatchTable->KdpPrintRoutine = KdpSerialPrint;
        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);

        /* Whoever is reading the output can also type back over it */
        KdTermSerial = TRUE;
    }

    return STATUS_SUCCESS;
}

/* EOF */
