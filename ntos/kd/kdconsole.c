/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The terminal the debugger is driven from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* What the person at the terminal asked for on the command line */
ULONG KdbDebugState = 0;

/* As much as is known about the terminal */
SIZE KdTermSize = {80, 24};
BOOLEAN KdTermConnected = FALSE;
BOOLEAN KdTermSerial = FALSE;
BOOLEAN KdTermReportsSize = TRUE;
BOOLEAN KdTermCrashMode = FALSE;

/* PRIVATE DATA ***************************************************************/

/* How long to keep asking the terminal before giving up on an answer */
#define KDP_TERMINAL_RETRY  1000

/* How long to wait for the rest of an escape sequence already begun */
#define KDP_ESCAPE_RETRY    50

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * One character from wherever the person is typing. The terminal may be a
 * serial line, a keyboard on this machine, or a keyboard on the USB controller
 * the debugger drives itself; each is asked in turn and the first answer wins.
 */
static
CHAR
KdpTryGetChar(
    _Out_ PULONG ScanCode,
    _In_ ULONG Retry)
{
    CHAR Char;

    *ScanCode = 0;

    if (KdTermSerial)
    {
        Char = KdbpTryGetCharSerial(Retry);
        if (Char != (CHAR)-1)
            return Char;
    }

    Char = KdbpTryGetCharKeyboard(ScanCode, Retry);
    if (Char != (CHAR)-1)
        return Char;

    /*
     * Once the machine has stopped, the USB controller is driven by the
     * debugger itself, so a keyboard on it still answers.
     */
    if (KdTermCrashMode)
    {
        Char = KdpUsbKeyboardTryGetChar(ScanCode);
        if (Char != (CHAR)-1)
            return Char;
    }

    return (CHAR)-1;
}

/*
 * Turns the tail of an escape sequence into the scan code of the key that sent
 * it. Returns TRUE when the sequence was understood.
 */
static
BOOLEAN
KdpDecodeEscapeSequence(
    _Out_ PULONG ScanCode)
{
    ULONG Ignored;
    CHAR Char;

    *ScanCode = 0;

    /* A bare escape is the Escape key; a bracket starts a sequence */
    Char = KdpTryGetChar(&Ignored, KDP_ESCAPE_RETRY);
    if ((Char != '[') && (Char != 'O'))
        return FALSE;

    Char = KdpTryGetChar(&Ignored, KDP_ESCAPE_RETRY);

    switch (Char)
    {
        case 'A': *ScanCode = KEYSC_ARROWUP; return TRUE;
        case 'B': *ScanCode = KEY_SCAN_DOWN; return TRUE;
        case 'H': *ScanCode = KEYSC_HOME; return TRUE;
        case 'F': *ScanCode = KEYSC_END; return TRUE;

        case '5':
        case '6':
        {
            ULONG Code = (Char == '5') ? KEYSC_PAGEUP : KEYSC_PAGEDOWN;

            /* These carry a trailing tilde that is of no further use */
            KdpTryGetChar(&Ignored, KDP_ESCAPE_RETRY);
            *ScanCode = Code;
            return TRUE;
        }

        default:
            return FALSE;
    }
}

/*
 * Reads the answer to a cursor-position query: ESC [ rows ; columns R.
 */
static
BOOLEAN
KdpReadCursorReport(
    _Out_ PULONG Rows,
    _Out_ PULONG Columns)
{
    ULONG Ignored;
    ULONG Value[2] = {0, 0};
    ULONG Index = 0;
    CHAR Char;

    *Rows = 0;
    *Columns = 0;

    Char = KdpTryGetChar(&Ignored, KDP_TERMINAL_RETRY);
    if (Char != KEY_ESC)
        return FALSE;

    Char = KdpTryGetChar(&Ignored, KDP_ESCAPE_RETRY);
    if (Char != '[')
        return FALSE;

    for (;;)
    {
        Char = KdpTryGetChar(&Ignored, KDP_ESCAPE_RETRY);

        if ((Char >= '0') && (Char <= '9'))
        {
            Value[Index] = (Value[Index] * 10) + (ULONG)(Char - '0');
            continue;
        }

        if ((Char == ';') && (Index == 0))
        {
            Index = 1;
            continue;
        }

        if (Char == 'R')
            break;

        /* Anything else means this was not the answer to the question */
        return FALSE;
    }

    if ((Value[0] == 0) || (Value[1] == 0))
        return FALSE;

    *Rows = Value[0];
    *Columns = Value[1];
    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Throws away whatever has been typed but not read. Called before a question
 * so that the answer cannot be something typed before it was asked.
 */
VOID
KdpFlushTerminalInput(VOID)
{
    ULONG ScanCode;

    while (KdpTryGetChar(&ScanCode, 1) != (CHAR)-1)
    {
        /* Nothing to do: reading it is what throws it away */
    }
}

/*
 * One key. A key that sends an escape sequence is reported through its scan
 * code with no character, which is how the caller tells an arrow key from a
 * letter.
 */
CHAR
KdpReadTermKey(
    _Out_ PULONG ScanCode)
{
    CHAR Char;

    *ScanCode = 0;

    for (;;)
    {
        Char = KdpTryGetChar(ScanCode, KDP_TERMINAL_RETRY);

        if (Char == (CHAR)-1)
            continue;

        /* A key that arrived with a scan code of its own needs no decoding */
        if (*ScanCode != 0)
            return Char;

        if (Char != KEY_ESC)
            return Char;

        if (KdpDecodeEscapeSequence(ScanCode))
            return ANSI_NULL;

        /* An escape that began nothing is the Escape key */
        return KEY_ESC;
    }
}

/*
 * Asks the terminal how big it is, by driving the cursor past any plausible
 * corner and reading back where it actually ended up.
 */
BOOLEAN
KdpUpdateTerminalSize(
    _Out_ PSIZE TermSize)
{
    ULONG Rows = 0;
    ULONG Columns = 0;

    TermSize->cx = KdTermSize.cx;
    TermSize->cy = KdTermSize.cy;

    if (!KdTermConnected || !KdTermReportsSize)
        return FALSE;

    KdpFlushTerminalInput();

    /* Save where the cursor is, go looking for the corner, and come back */
    KdbpSendCommandSerial("\x1b[s\x1b[999;999H\x1b[6n");

    if (!KdpReadCursorReport(&Rows, &Columns))
    {
        /* A terminal that does not answer is not asked again */
        KdTermReportsSize = FALSE;
        KdbpSendCommandSerial("\x1b[u");
        return FALSE;
    }

    KdbpSendCommandSerial("\x1b[u");

    KdTermSize.cx = (LONG)Columns;
    KdTermSize.cy = (LONG)Rows;

    TermSize->cx = KdTermSize.cx;
    TermSize->cy = KdTermSize.cy;

    return TRUE;
}

/*
 * Works out whether there is a terminal at all, and how big it is.
 */
BOOLEAN
KdpInitTerminal(VOID)
{
    SIZE Size;

    /* A serial line is a terminal; a screen and a keyboard are one too */
    KdTermConnected = (KdTermSerial || KdpDebugMode.Screen);

    if (!KdTermConnected)
        return FALSE;

    /* Only something at the far end of a line can be asked about itself */
    if (!KdTermSerial)
    {
        KdTermReportsSize = FALSE;
        return TRUE;
    }

    if ((KdbDebugState & KD_DEBUG_KDNOECHO) == 0)
        KdbpSendCommandSerial("\x1b[0m");

    KdpUpdateTerminalSize(&Size);
    return TRUE;
}

/*
 * Whether the person at the terminal has asked to stop the machine. Anything
 * else typed while the machine is running is thrown away, because there is
 * nothing yet that is listening for it.
 */
BOOLEAN
KdpTerminalPollBreakIn(VOID)
{
    ULONG ScanCode;
    CHAR Char;

    /* The network terminal keeps its own count of what the host sent */
    if (KdpDebugMode.Net && KdpNetPollBreakIn())
        return TRUE;

    Char = KdpTryGetChar(&ScanCode, 1);

    if (Char == (CHAR)-1)
        return FALSE;

    /* Ctrl-C, and nothing else: a terminal is also where people type */
    return (Char == 0x03);
}

/* EOF */
