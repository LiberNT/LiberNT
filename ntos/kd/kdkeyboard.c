/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading the keyboard on the controller the firmware left behind
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * The controller as the specification describes it: one port for data and one
 * for status and commands.
 */
#define KDP_KBD_DATA_PORT       0x60
#define KDP_KBD_STATUS_PORT     0x64
#define KDP_KBD_COMMAND_PORT    0x64

/* Status bits */
#define KDP_KBD_STATUS_OUTPUT_FULL  0x01
#define KDP_KBD_STATUS_INPUT_FULL   0x02
#define KDP_KBD_STATUS_FROM_MOUSE   0x20

/* Controller commands */
#define KDP_KBD_DISABLE_MOUSE   0xA7
#define KDP_KBD_ENABLE_MOUSE    0xA8

/* A break code is a make code with the top bit set */
#define KDP_KBD_BREAK_FLAG      0x80

#define KDP_KBD_SCANCODE_MAX    0x54

#define KDP_SCAN_LEFT_SHIFT     0x2A
#define KDP_SCAN_RIGHT_SHIFT    0x36

/* Scan code set 1, as a keyboard with the United States layout sends it */
static const CHAR KdpScanCodeTable[KDP_KBD_SCANCODE_MAX] =
{
    0,     0x1B,  '1',   '2',   '3',   '4',   '5',   '6',
    '7',   '8',   '9',   '0',   '-',   '=',   '\b',  '\t',
    'q',   'w',   'e',   'r',   't',   'y',   'u',   'i',
    'o',   'p',   '[',   ']',   '\n',  0,     'a',   's',
    'd',   'f',   'g',   'h',   'j',   'k',   'l',   ';',
    '\'',  '`',   0,     '\\',  'z',   'x',   'c',   'v',
    'b',   'n',   'm',   ',',   '.',   '/',   0,     '*',
    0,     ' ',   0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     '7',
    '8',   '9',   '-',   '4',   '5',   '6',   '+',   '1',
    '2',   '3',   '0',   '.'
};

static const CHAR KdpShiftedScanCodeTable[KDP_KBD_SCANCODE_MAX] =
{
    0,     0x1B,  '!',   '@',   '#',   '$',   '%',   '^',
    '&',   '*',   '(',   ')',   '_',   '+',   '\b',  '\t',
    'Q',   'W',   'E',   'R',   'T',   'Y',   'U',   'I',
    'O',   'P',   '{',   '}',   '\n',  0,     'A',   'S',
    'D',   'F',   'G',   'H',   'J',   'K',   'L',   ':',
    '"',   '~',   0,     '|',   'Z',   'X',   'C',   'V',
    'B',   'N',   'M',   '<',   '>',   '?',   0,     '*',
    0,     ' ',   0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     '7',
    '8',   '9',   '-',   '4',   '5',   '6',   '+',   '1',
    '2',   '3',   '0',   '.'
};

/* Whether a shift key is being held down right now */
static BOOLEAN KdpKeyboardShift = FALSE;

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The character a scan code stands for, or zero when it stands for a key that
 * is not a character.
 */
CHAR
KdpScanCodeToChar(
    _In_ UCHAR ScanCode,
    _In_ BOOLEAN Shift)
{
    if (ScanCode >= KDP_KBD_SCANCODE_MAX)
        return ANSI_NULL;

    if (Shift)
        return KdpShiftedScanCodeTable[ScanCode];

    return KdpScanCodeTable[ScanCode];
}

/*
 * One key from the controller, or -1 when nothing was pressed. The scan code
 * is reported as well, because the keys that matter most to a debugger -- the
 * arrows and the paging keys -- stand for no character at all.
 */
CHAR
KdbpTryGetCharKeyboard(
    _Out_ PULONG ScanCode,
    _In_ ULONG Retry)
{
    ULONG Remaining = Retry;

    *ScanCode = 0;

    for (;;)
    {
        UCHAR Status;
        UCHAR Code;

        Status = READ_PORT_UCHAR((PUCHAR)KDP_KBD_STATUS_PORT);

        if ((Status & KDP_KBD_STATUS_OUTPUT_FULL) == 0)
        {
            if (Remaining == 0)
                return (CHAR)-1;

            Remaining--;
            YieldProcessor();
            continue;
        }

        Code = READ_PORT_UCHAR((PUCHAR)KDP_KBD_DATA_PORT);

        /* The mouse shares the controller and has nothing to say here */
        if ((Status & KDP_KBD_STATUS_FROM_MOUSE) != 0)
            continue;

        /* Letting go of a key only matters for the shift keys */
        if ((Code & KDP_KBD_BREAK_FLAG) != 0)
        {
            Code &= ~KDP_KBD_BREAK_FLAG;

            if ((Code == KDP_SCAN_LEFT_SHIFT) || (Code == KDP_SCAN_RIGHT_SHIFT))
                KdpKeyboardShift = FALSE;

            continue;
        }

        if ((Code == KDP_SCAN_LEFT_SHIFT) || (Code == KDP_SCAN_RIGHT_SHIFT))
        {
            KdpKeyboardShift = TRUE;
            continue;
        }

        *ScanCode = Code;
        return KdpScanCodeToChar(Code, KdpKeyboardShift);
    }
}

/*
 * The mouse is told to be quiet while the debugger is reading the keyboard,
 * because its reports arrive through the same controller and would be read as
 * if somebody had typed them.
 */
VOID
KbdDisableMouse(VOID)
{
    ULONG Retry = 10000;

    while (((READ_PORT_UCHAR((PUCHAR)KDP_KBD_STATUS_PORT) &
             KDP_KBD_STATUS_INPUT_FULL) != 0) && (Retry != 0))
    {
        Retry--;
        YieldProcessor();
    }

    WRITE_PORT_UCHAR((PUCHAR)KDP_KBD_COMMAND_PORT, KDP_KBD_DISABLE_MOUSE);
}

VOID
KbdEnableMouse(VOID)
{
    ULONG Retry = 10000;

    while (((READ_PORT_UCHAR((PUCHAR)KDP_KBD_STATUS_PORT) &
             KDP_KBD_STATUS_INPUT_FULL) != 0) && (Retry != 0))
    {
        Retry--;
        YieldProcessor();
    }

    WRITE_PORT_UCHAR((PUCHAR)KDP_KBD_COMMAND_PORT, KDP_KBD_ENABLE_MOUSE);
}

/* EOF */
