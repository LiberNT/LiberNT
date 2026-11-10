/*
 * PROJECT:     ReactOS KDBG Kernel Debugger Terminal Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KD Terminal Management
 * COPYRIGHT:   Copyright 2005 Gregor Anich <blight@blight.eu.org>
 *              Copyright 2022-2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"

CHAR
KdbpTryGetCharKeyboard(PULONG ScanCode, ULONG Retry);

#define KdbpGetCharSerial()  KdbpTryGetCharSerial(0)
CHAR
KdbpTryGetCharSerial(
    _In_ ULONG Retry);

VOID
KdbpSendCommandSerial(
    _In_ PCSTR Command);


/* GLOBALS *******************************************************************/

/* KD Controlling Terminal */
ULONG KdbDebugState = 0; /* KDBG Settings (NOECHO, KDSERIAL) */
SIZE KdTermSize = {0,0};
BOOLEAN KdTermConnected = FALSE;
BOOLEAN KdTermSerial = FALSE;
BOOLEAN KdTermReportsSize = TRUE;
BOOLEAN KdTermCrashMode = FALSE;

#define KD_TERM_IDLE_STALL_US 100
#define KD_TERM_IDLE_SAVE_SECONDS 30

static CHAR KdTermNextKey = ANSI_NULL; /* 1-character input queue buffer */


/* FUNCTIONS *****************************************************************/

/**
 * @brief   Initializes the controlling terminal.
 *
 * @return
 * TRUE if the controlling terminal is serial and detected
 * as being connected, or FALSE if not.
 **/
BOOLEAN
KdpInitTerminal(VOID)
{
    static BOOLEAN Initialized = FALSE;

    /* DbgPrompt can be the first terminal user, before the KDBG pager. */
    if (Initialized)
        return KdTermConnected;
    Initialized = TRUE;

    /* Determine whether the controlling terminal is a serial terminal:
     * serial output is enabled *and* KDSERIAL is set (i.e. user input
     * through serial). */
    KdTermSerial =
#if 0
    // Old logic where KDSERIAL also enables serial output.
    (KdbDebugState & KD_DEBUG_KDSERIAL) ||
    (KdpDebugMode.Serial && !KdpDebugMode.Screen);
#else
    // New logic where KDSERIAL does not necessarily enable serial output.
    KdpDebugMode.Net ||
    (KdpDebugMode.Serial &&
     ((KdbDebugState & KD_DEBUG_KDSERIAL) || !KdpDebugMode.Screen));
#endif

    /* Flush the input buffer */
    KdpFlushTerminalInput();

    if (KdTermSerial && (KdbDebugState & KD_DEBUG_KDSERIAL))
    {
        ULONG Length;

        /* Enable line-wrap */
        KdbpSendCommandSerial("\x1b[?7h");

        /*
         * Query terminal type.
         * Historically it was done with CTRL-E ('\x05'), however nowadays
         * terminals respond to it with an empty (or a user-configurable)
         * string. Instead, use the VT52-compatible 'ESC Z' sequence or the
         * VT100-compatible 'ESC[c' one.
         */
        KdbpSendCommandSerial("\x1b[c");
        KeStallExecutionProcessor(100000);

        Length = 0;
        for (;;)
        {
            /* Verify we get an answer, but don't care about it */
            if (KdbpTryGetCharSerial(5000) == -1)
                break;
            ++Length;
        }

        /* Terminal is connected (TRUE) or not connected (FALSE) */
        KdTermConnected = (Length > 0);
    }
    else
    {
        /* No terminal probe is possible, assume it's *not* connected */
        KdTermConnected = FALSE;
    }
    return KdTermConnected;
}

BOOLEAN
KdpUpdateTerminalSize(
    _Out_ PSIZE TermSize)
{
    static CHAR Buffer[128];
    CHAR c;
    LONG NumberOfCols = -1; // Or initialize to TermSize->cx ??
    LONG NumberOfRows = -1; // Or initialize to TermSize->cy ??
    VID_DISPLAY_INFO DisplayInfo;
    ULONG Columns, Rows;

    if (KdpDebugMode.Net && KdpNetQueryAttached(&Columns, &Rows))
    {
        KdTermConnected = TRUE;
        if (Columns && Rows)
        {
            TermSize->cx = Columns;
            TermSize->cy = Rows;
            return TRUE;
        }
    }

    /* Retrieve the size of the controlling terminal only when it is serial */
    if (KdTermConnected && KdTermSerial && KdTermReportsSize)
    {
        /* Flush the input buffer */
        KdpFlushTerminalInput();

        /* Try to query the terminal size. A reply looks like "\x1b[8;24;80t" */
        KdTermReportsSize = FALSE;
        KdbpSendCommandSerial("\x1b[18t");
        KeStallExecutionProcessor(100000);

        c = KdbpTryGetCharSerial(5000);
        if (c == KEY_ESC)
        {
            c = KdbpTryGetCharSerial(5000);
            if (c == '[')
            {
                ULONG Length = 0;
                for (;;)
                {
                    c = KdbpTryGetCharSerial(5000);
                    if (c == -1)
                        break;

                    Buffer[Length++] = c;
                    if (isalpha(c) || Length >= (sizeof(Buffer) - 1))
                        break;
                }
                Buffer[Length] = ANSI_NULL;

                if (Buffer[0] == '8' && Buffer[1] == ';')
                {
                    SIZE_T i;
                    for (i = 2; (i < Length) && (Buffer[i] != ';'); i++);

                    if (Buffer[i] == ';')
                    {
                        Buffer[i++] = ANSI_NULL;

                        /* Number of rows is now at Buffer + 2
                         * and number of columns at Buffer + i */
                        NumberOfRows = strtoul(Buffer + 2, NULL, 0);
                        NumberOfCols = strtoul(Buffer + i, NULL, 0);
                        KdTermReportsSize = TRUE;
                    }
                }
            }
            /* Clear further characters */
            while (KdbpTryGetCharSerial(5000) != -1);
        }
    }

    if (NumberOfCols <= 0)
    {
        /* Set the number of columns to the default */
        if (KdpDebugMode.Screen && !KdTermSerial)
        {
            InbvQueryDisplayInfo(&DisplayInfo);
            NumberOfCols = DisplayInfo.CharacterWidth ? DisplayInfo.Width / DisplayInfo.CharacterWidth : 80;
        }
        else
            NumberOfCols = 80;
    }
    if (NumberOfRows <= 0)
    {
        /* Set the number of rows to the default */
        if (KdpDebugMode.Screen && !KdTermSerial)
        {
            InbvQueryDisplayInfo(&DisplayInfo);
            NumberOfRows = DisplayInfo.CharacterHeight ? DisplayInfo.Height / DisplayInfo.CharacterHeight : 34;
        }
        else
            NumberOfRows = 24;
    }

    TermSize->cx = NumberOfCols;
    TermSize->cy = NumberOfRows;

    // KdIoPrintf("Cols/Rows: %dx%d\n", TermSize->cx, TermSize->cy);

    return KdTermReportsSize;
}

/**
 * @brief   Flushes terminal input (serial, PS/2 and USB keyboards).
 **/
VOID
KdpFlushTerminalInput(VOID)
{
    ULONG ScanCode;

    KdTermNextKey = ANSI_NULL;
    while (KdbpTryGetCharSerial(1) != -1);
    while (KdbpTryGetCharKeyboard(&ScanCode, 1) != -1);
}

static CHAR
KdpTryReadSerialKey(
    _Out_ PULONG ScanCode)
{
    CHAR Key;

    Key = KdbpTryGetCharSerial(1);
    if (Key == KEY_ESC) /* ESC */
    {
        Key = KdbpGetCharSerial();
        if (Key == '[')
        {
            Key = KdbpGetCharSerial();
            switch (Key)
            {
                case 'A':
                    *ScanCode = KEY_SCAN_UP;
                    break;
                case 'B':
                    *ScanCode = KEY_SCAN_DOWN;
                    break;
                case 'C':
                    break;
                case 'D':
                    break;
            }
        }
    }

    return Key;
}

static VOID
KdpTermNotice(
    _In_ PCSTR Text)
{
    STRING Output;

    RtlInitString(&Output, Text);
    KdLogDbgPrint(&Output);
    KdIoPuts(Text);
}

static VOID
KdpSaveUnattendedSession(VOID)
{
    CHAR Text[160];
    NTSTATUS Status;

    RtlStringCbPrintfA(Text, sizeof(Text),
                       "\nKD: no debugger input for %u s, saving this session to lastboot.log; restart instead of continuing\n",
                       KD_TERM_IDLE_SAVE_SECONDS);
    KdpTermNotice(Text);
    Status = KdpWriteBootLogAtCrash(NULL, NULL);
    if (NT_SUCCESS(Status))
    {
        KdTermCrashMode = TRUE;
        return;
    }

    RtlStringCbPrintfA(Text, sizeof(Text), "KD: saving the session to lastboot.log failed with 0x%08lx\n", Status);
    KdpTermNotice(Text);
}

/**
 * @brief
 * Reads one character from the first terminal input that has one:
 * serial, PS/2 keyboard or USB keyboard. This function returns a
 * scan code even when reading is done from a serial terminal.
 **/
CHAR
KdpReadTermKey(
    _Out_ PULONG ScanCode)
{
    CHAR Key;
    BOOLEAN FromSerial = TRUE;
    ULONG IdleUs = 0;

    *ScanCode = 0;

    if (KdTermNextKey)
    {
        Key = KdTermNextKey;
        KdTermNextKey = ANSI_NULL;
    }
    else
    {
        for (;;)
        {
            FromSerial = TRUE;
            *ScanCode = 0;
            Key = KdpTryReadSerialKey(ScanCode);
            if ((Key != -1) || (*ScanCode != 0))
                break;

            FromSerial = FALSE;
            Key = KdbpTryGetCharKeyboard(ScanCode, 1);
            if (Key != -1)
                break;

            *ScanCode = 0;
            Key = KdpUsbKeyboardTryGetChar(ScanCode);
            if (Key != -1)
                break;

            if (KdTermCrashMode || KdTermConnected || KeBugCheckActive || (IdleUs == MAXULONG) ||
                KdpNetQueryAttached(NULL, NULL))
            {
                continue;
            }

            KeStallExecutionProcessor(KD_TERM_IDLE_STALL_US);
            IdleUs += KD_TERM_IDLE_STALL_US;
            if (IdleUs >= KD_TERM_IDLE_SAVE_SECONDS * 1000000)
            {
                KdpSaveUnattendedSession();
                IdleUs = MAXULONG;
            }
        }
    }

    /* Check for return */
    if ((Key == '\r') && FromSerial)
    {
        /*
         * We might need to discard the next '\n' which most clients
         * should send after \r. Wait a bit to make sure we receive it.
         */
        KeStallExecutionProcessor(100000);

        KdTermNextKey = KdbpTryGetCharSerial(5);
        if (KdTermNextKey == '\n' || KdTermNextKey == -1) /* \n or no response at all */
            KdTermNextKey = ANSI_NULL;
    }

    return Key;
}

/* EOF */
