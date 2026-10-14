/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Open Firmware terminal input and text console for the boot menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>
#include <ofw.h>

#define OFW_ESCAPE_WAIT_MS     30
#define OFW_COLOR_DEPTH        32
#define OFW_CELL_WIDTH         8
#define OFW_CELL_HEIGHT        16
#define OFW_IDLE_STALL_US      1000

static UCHAR OfwInputQueue[16];
static ULONG OfwInputHead, OfwInputTail;
static BOOLEAN OfwExtendedPending;
static UCHAR OfwExtendedCode;
static ULONG OfwTimerStart;
static ULONG OfwTimerPeriod;

static const UCHAR OfwVgaToAnsi[8] = {0, 4, 2, 6, 1, 5, 3, 7};

static
BOOLEAN
OfwPollByte(VOID)
{
    UCHAR Byte;

    if ((OfwInputTail + 1) % sizeof(OfwInputQueue) == OfwInputHead)
        return TRUE;

    if (OfwStdin == OFW_INVALID || OfwRead(OfwStdin, &Byte, 1) != 1)
        return FALSE;

    OfwInputQueue[OfwInputTail] = Byte;
    OfwInputTail = (OfwInputTail + 1) % sizeof(OfwInputQueue);
    return TRUE;
}

static
BOOLEAN
OfwPeekByte(
    _In_ ULONG Index,
    _Out_ PUCHAR Byte,
    _In_ ULONG WaitMs)
{
    ULONG Start = OfwMilliseconds();
    ULONG Count;

    for (;;)
    {
        Count = (OfwInputTail + sizeof(OfwInputQueue) - OfwInputHead) % sizeof(OfwInputQueue);
        if (Index < Count)
        {
            *Byte = OfwInputQueue[(OfwInputHead + Index) % sizeof(OfwInputQueue)];
            return TRUE;
        }

        if (!OfwPollByte() && OfwMilliseconds() - Start >= WaitMs)
            return FALSE;
    }
}

static
VOID
OfwDropBytes(
    _In_ ULONG Count)
{
    OfwInputHead = (OfwInputHead + Count) % sizeof(OfwInputQueue);
}

static
UCHAR
OfwDecodeEscape(VOID)
{
    UCHAR Introducer, Final;
    ULONG Number = 0, Used = 2;

    if (!OfwPeekByte(1, &Introducer, OFW_ESCAPE_WAIT_MS) || (Introducer != '[' && Introducer != 'O'))
    {
        OfwDropBytes(1);
        return 0;
    }

    for (;;)
    {
        if (!OfwPeekByte(Used, &Final, OFW_ESCAPE_WAIT_MS))
        {
            OfwDropBytes(Used);
            return 0;
        }

        Used++;
        if (Final < '0' || Final > '9')
            break;

        Number = Number * 10 + (Final - '0');
    }

    OfwDropBytes(Used);

    switch (Final)
    {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'P': return KEY_F1;
        case 'Q': return KEY_F2;
        case 'R': return KEY_F3;
        case 'S': return KEY_F4;
        case '~':
            switch (Number)
            {
                case 1: return KEY_HOME;
                case 3: return KEY_DELETE;
                case 4: return KEY_END;
                case 11: return KEY_F1;
                case 12: return KEY_F2;
                case 13: return KEY_F3;
                case 14: return KEY_F4;
                case 15: return KEY_F5;
                case 17: return KEY_F6;
                case 18: return KEY_F7;
                case 19: return KEY_F8;
                case 20: return KEY_F9;
                case 21: return KEY_F10;
            }
            break;
    }

    return 0;
}

BOOLEAN
FwKeyAvailable(VOID)
{
    if (OfwExtendedPending || OfwInputHead != OfwInputTail)
        return TRUE;

    return OfwPollByte();
}

int
FwReadKey(VOID)
{
    UCHAR Byte, Next, Scan;

    if (OfwExtendedPending)
    {
        OfwExtendedPending = FALSE;
        return OfwExtendedCode;
    }

    while (!OfwPeekByte(0, &Byte, 0))
        ;

    if (Byte == KEY_ESC)
    {
        if (!OfwPeekByte(1, &Next, OFW_ESCAPE_WAIT_MS))
        {
            OfwDropBytes(1);
            return KEY_ESC;
        }

        Scan = OfwDecodeEscape();
        if (Scan == 0)
            return KEY_ESC;

        OfwExtendedPending = TRUE;
        OfwExtendedCode = Scan;
        return KEY_EXTENDED;
    }

    OfwDropBytes(1);

    if (Byte == 0x7F)
        return KEY_BACKSPACE;

    if (Byte == '\n')
        return KEY_ENTER;

    return Byte;
}

VOID
VidFbGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth)
{
    ULONG Address, Physical, Stride;

    *Depth = OFW_COLOR_DEPTH;
    if (!OfwGetDisplay(&Address, &Physical, Width, Height, &Stride))
    {
        *Width = 0;
        *Height = 0;
    }
}

VOID
VidFbPresent(
    _In_ const ULONG* Pixels,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Address, Physical, ScreenWidth, ScreenHeight, Stride, Row;
    PUCHAR Target;

    if (!OfwGetDisplay(&Address, &Physical, &ScreenWidth, &ScreenHeight, &Stride) ||
        Left >= ScreenWidth ||
        Top >= ScreenHeight)
    {
        return;
    }

    Width = min(Width, ScreenWidth - Left);
    Height = min(Height, ScreenHeight - Top);

    for (Row = 0; Row < Height; Row++)
    {
        Target = (PUCHAR)(ULONG_PTR)Address + (Top + Row) * Stride + Left * sizeof(ULONG);
        RtlCopyMemory(Target,
                      Pixels + (SIZE_T)(Top + Row) * ScreenWidth + Left,
                      Width * sizeof(ULONG));
    }
}

static
VOID
OfwSetTextAttribute(
    _In_ UCHAR Attr)
{
    CHAR Text[24];

    RtlStringCbPrintfA(Text,
                       sizeof(Text),
                       "\x1b[0;%u;%um",
                       ((Attr & 0x08) ? 90 : 30) + OfwVgaToAnsi[Attr & 0x07],
                       40 + OfwVgaToAnsi[(Attr >> 4) & 0x07]);
    OfwWrite(OfwStdout, Text, (ULONG)strlen(Text));
}

VOID
FbConsClearScreen(
    _In_ UCHAR Attr)
{
    if (OfwStdout == OFW_INVALID)
        return;

    OfwSetTextAttribute(Attr);
    OfwWrite(OfwStdout, "\x1b[2J\x1b[H", 7);
}

VOID
FbConsPutChar(
    _In_ UCHAR Char,
    _In_ UCHAR Attr,
    _In_ ULONG Column,
    _In_ ULONG Row)
{
    CHAR Text[16];

    if (OfwStdout == OFW_INVALID)
        return;

    OfwSetTextAttribute(Attr);
    RtlStringCbPrintfA(Text, sizeof(Text), "\x1b[%lu;%luH", Row + 1, Column + 1);
    OfwWrite(OfwStdout, Text, (ULONG)strlen(Text));
    OfwWrite(OfwStdout, &Char, 1);
}

VOID
FbConsGetCellSize(
    _Out_ PULONG CellWidth,
    _Out_ PULONG CellHeight)
{
    *CellWidth = OFW_CELL_WIDTH;
    *CellHeight = OFW_CELL_HEIGHT;
}

VOID
FwVideoSync(VOID)
{
}

VOID
VideoFreeOffScreenBuffer(VOID)
{
}

VOID
FwIdle(VOID)
{
    StallExecutionProcessor(OFW_IDLE_STALL_US);
}

BOOLEAN
FwGetPointerState(
    ULONG Width,
    ULONG Height,
    PMACH_POINTER_STATE State)
{
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(State);
    return FALSE;
}

BOOLEAN
FwSetInputTimer(
    ULONG Milliseconds)
{
    OfwTimerPeriod = Milliseconds;
    OfwTimerStart = OfwMilliseconds();
    return Milliseconds != 0;
}

BOOLEAN
FwInputTimerExpired(VOID)
{
    ULONG Now;

    if (OfwTimerPeriod == 0)
        return FALSE;

    Now = OfwMilliseconds();
    if (Now - OfwTimerStart < OfwTimerPeriod)
        return FALSE;

    OfwTimerPeriod = 0;
    return TRUE;
}

BOOLEAN
FwFirmwareSetupSupported(VOID)
{
    return FALSE;
}

VOID
FwBootToFirmware(VOID)
{
    OfwExit();
}

VOID
Reboot(VOID)
{
    OfwExit();
}
