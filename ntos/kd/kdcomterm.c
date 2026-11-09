/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Typing back over the serial line the output goes out on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * One character from the line, or -1 when none arrived. Retry says how many
 * times to look before giving up, which is what lets a caller tell "nothing
 * yet" from "nothing coming".
 */
CHAR
KdbpTryGetCharSerial(
    _In_ ULONG Retry)
{
    UCHAR Byte = 0;
    ULONG Remaining = Retry;

    if (!KdpDebugMode.Serial)
        return (CHAR)-1;

    for (;;)
    {
        if (KdPortGetByteEx(&SerialPortInfo, &Byte))
            return (CHAR)Byte;

        if (Remaining == 0)
            return (CHAR)-1;

        Remaining--;

        /*
         * Nothing to wait on this early and nothing that may be waited on
         * once the machine has stopped, so the delay is the loop itself.
         */
        YieldProcessor();
    }
}

/*
 * Sends a string to the terminal without it being debug output: escape
 * sequences the terminal is meant to act on rather than show.
 */
VOID
KdbpSendCommandSerial(
    _In_ PCSTR Command)
{
    if (!KdpDebugMode.Serial || (Command == NULL))
        return;

    while (*Command != ANSI_NULL)
    {
        KdPortPutByteEx(&SerialPortInfo, (UCHAR)*Command);
        Command++;
    }
}

/* EOF */
