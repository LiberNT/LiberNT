/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading a line of input at the debugger's prompt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#ifdef KDBG
#include "../kdbg/kdb.h"
#endif
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
KdpEchoChar(
    _In_ CHAR Char)
{
    CHAR Text[2];

    if ((KdbDebugState & KD_DEBUG_KDNOECHO) != 0)
        return;

    Text[0] = Char;
    Text[1] = ANSI_NULL;
    KdIoPuts(Text);
}

/*
 * Takes back what was typed, on the screen as well as in the buffer.
 */
static
VOID
KdpEraseChar(VOID)
{
    if ((KdbDebugState & KD_DEBUG_KDNOECHO) != 0)
        return;

    KdIoPuts("\b \b");
}

/*
 * Replaces what is on the line with something else, which is what choosing an
 * earlier command amounts to.
 */
static
SIZE_T
KdpReplaceLine(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ SIZE_T Size,
    _In_ SIZE_T Length,
    _In_opt_ PCSTR Replacement)
{
    SIZE_T NewLength = 0;

    while (Length != 0)
    {
        KdpEraseChar();
        Length--;
    }

    if (Replacement != NULL)
    {
        while ((Replacement[NewLength] != ANSI_NULL) && (NewLength < Size - 1))
        {
            Buffer[NewLength] = Replacement[NewLength];
            KdpEchoChar(Buffer[NewLength]);
            NewLength++;
        }
    }

    Buffer[NewLength] = ANSI_NULL;
    return NewLength;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Reads one line from the terminal. Returns how much was typed, with the line
 * terminated; a line the person gave up on comes back empty.
 */
SIZE_T
KdIoReadLine(
    _Out_ PCHAR Buffer,
    _In_ SIZE_T Size)
{
    SIZE_T Length = 0;
    LONG HistoryIndex = -1;

    if ((Buffer == NULL) || (Size == 0))
        return 0;

    Buffer[0] = ANSI_NULL;

    if (Size == 1)
        return 0;

    for (;;)
    {
        ULONG ScanCode = 0;
        CHAR Char = KdpReadTermKey(&ScanCode);

        /* A key that stands for no character moves through the history */
        if ((ScanCode == KEYSC_ARROWUP) || (ScanCode == KEY_SCAN_DOWN))
        {
#ifdef KDBG
            PCSTR Entry;

            Entry = KdbGetHistoryEntry(&HistoryIndex,
                                       (ScanCode == KEY_SCAN_DOWN));
            if (Entry != NULL)
                Length = KdpReplaceLine(Buffer, Size, Length, Entry);
#endif
            continue;
        }

        if ((Char == '\r') || (Char == '\n'))
        {
            if ((KdbDebugState & KD_DEBUG_KDNOECHO) == 0)
                KdIoPuts("\n");

            Buffer[Length] = ANSI_NULL;
            return Length;
        }

        if ((Char == KEY_BS) || (Char == KEY_DEL))
        {
            if (Length != 0)
            {
                Length--;
                Buffer[Length] = ANSI_NULL;
                KdpEraseChar();
            }
            continue;
        }

        /* Ctrl-C gives up on the line */
        if (Char == 0x03)
        {
            Length = KdpReplaceLine(Buffer, Size, Length, NULL);

            if ((KdbDebugState & KD_DEBUG_KDNOECHO) == 0)
                KdIoPuts("\n");

            return 0;
        }

        /* Anything that is not printable has no place on the line */
        if ((Char < ' ') || (Char == (CHAR)0x7F))
            continue;

        if (Length >= Size - 1)
            continue;

        Buffer[Length] = Char;
        Length++;
        KdpEchoChar(Char);
    }
}

/* EOF */
