/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PC/AT BIOS display, text console and keyboard for the boot menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <bios.h>
#include <gui.h>
#include <drivers/bootvid/framebuf.h>

#define BIOS_CELL_WIDTH         8
#define BIOS_CELL_HEIGHT        16
#define BIOS_FONT_GLYPHS        256
#define BIOS_TEXT_BUFFER        0xB8000
#define BIOS_TEXT_COLUMNS       80
#define BIOS_TEXT_ROWS          25
#define BIOS_DEFAULT_ATTRIBUTE  0x07
#define BIOS_IDLE_STALL_US      1000
#define BIOS_VBE_INFO_OFFSET    0x400
#define BIOS_VBE_MODE_OFFSET    0x800
#define BIOS_VBE_INFO_SIZE      512
#define BIOS_VBE_MODE_SIZE      256
#define BIOS_VBE_MAX_MODES      256
#define BIOS_VBE_SUCCESS        0x004F
#define BIOS_VBE_LINEAR         0x4000
#define BIOS_VBE_ATTR_SUPPORTED 0x0001
#define BIOS_VBE_ATTR_GRAPHICS  0x0010
#define BIOS_VBE_ATTR_LINEAR    0x0080
#define BIOS_VBE_DIRECT_COLOR   6
#define BIOS_ZERO_FLAG          0x0040
#define BIOS_KBC_STATUS         0x64
#define BIOS_KBC_RESET          0xFE
#define BIOS_RESET_CONTROL      0xCF9

typedef struct _BIOS_VIDEO_SIZE
{
    ULONG Width;
    ULONG Height;
} BIOS_VIDEO_SIZE;

static const BIOS_VIDEO_SIZE BiosPreferredSizes[] =
{
    {1024, 768},
    {800, 600},
    {640, 480},
};

static const ULONG BiosVgaColors[16] =
{
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static UCHAR BiosFont[BIOS_FONT_GLYPHS * BIOS_CELL_HEIGHT];
static BOOLEAN BiosFontLoaded;
static BIOS_DISPLAY BiosDisplay;
static BOOLEAN BiosGraphics;
static ULONG BiosColors[16];
static ULONG BiosColumn, BiosRow;
static BOOLEAN BiosExtendedPending;
static UCHAR BiosExtendedCode;
static ULONG BiosTimerStart;
static ULONG BiosTimerPeriod;

static
ULONG
BiosMask(
    _In_ UCHAR Size,
    _In_ UCHAR Position)
{
    if (Size == 0 || Size >= 32 || Position >= 32)
        return 0;

    return ((1UL << Size) - 1) << Position;
}

static
ULONG
BiosPackColor(
    _In_ ULONG Rgb)
{
    ULONG Red = (Rgb >> 16) & 0xFF, Green = (Rgb >> 8) & 0xFF, Blue = Rgb & 0xFF;
    ULONG Result = 0;
    ULONG Mask, Shift, Bits;

    Mask = BiosDisplay.RedMask;
    if (Mask)
    {
        for (Shift = 0; !(Mask & (1UL << Shift)); Shift++)
            ;
        for (Bits = 0; Shift + Bits < 32 && (Mask & (1UL << (Shift + Bits))); Bits++)
            ;
        Result |= ((Red >> (8 - min(Bits, 8UL))) << Shift) & Mask;
    }

    Mask = BiosDisplay.GreenMask;
    if (Mask)
    {
        for (Shift = 0; !(Mask & (1UL << Shift)); Shift++)
            ;
        for (Bits = 0; Shift + Bits < 32 && (Mask & (1UL << (Shift + Bits))); Bits++)
            ;
        Result |= ((Green >> (8 - min(Bits, 8UL))) << Shift) & Mask;
    }

    Mask = BiosDisplay.BlueMask;
    if (Mask)
    {
        for (Shift = 0; !(Mask & (1UL << Shift)); Shift++)
            ;
        for (Bits = 0; Shift + Bits < 32 && (Mask & (1UL << (Shift + Bits))); Bits++)
            ;
        Result |= ((Blue >> (8 - min(Bits, 8UL))) << Shift) & Mask;
    }

    return Result;
}

static
VOID
BiosLoadFont(VOID)
{
    BIOS_REGISTERS Registers;
    PUCHAR Font;

    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x1130;
    Registers.Ebx = 0x0600;
    BiosCall(0x10, &Registers);

    Font = (PUCHAR)BiosLinear(((ULONG)Registers.Es << 16) | (Registers.Ebp & 0xFFFF));
    if ((Registers.Ecx & 0xFFFF) != BIOS_CELL_HEIGHT || (ULONG_PTR)Font < BIOS_ROM_BASE)
        return;

    RtlCopyMemory(BiosFont, Font, sizeof(BiosFont));
    BiosFontLoaded = TRUE;
}

static
BOOLEAN
BiosQueryMode(
    _In_ USHORT Mode,
    _In_ USHORT Version,
    _Out_ PBIOS_DISPLAY Display)
{
    PUCHAR Info = (PUCHAR)(ULONG_PTR)(BIOS_TRANSFER_BASE + BIOS_VBE_MODE_OFFSET);
    BIOS_REGISTERS Registers;
    USHORT Attributes;
    ULONG Pitch;
    UCHAR Fields;

    RtlZeroMemory(Info, BIOS_VBE_MODE_SIZE);
    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4F01;
    Registers.Ecx = Mode;
    Registers.Es = BiosSegment(Info);
    Registers.Edi = BiosOffset(Info);
    BiosCall(0x10, &Registers);
    if ((Registers.Eax & 0xFFFF) != BIOS_VBE_SUCCESS)
        return FALSE;

    Attributes = *(PUSHORT)Info;
    if ((Attributes & (BIOS_VBE_ATTR_SUPPORTED | BIOS_VBE_ATTR_GRAPHICS | BIOS_VBE_ATTR_LINEAR)) !=
            (BIOS_VBE_ATTR_SUPPORTED | BIOS_VBE_ATTR_GRAPHICS | BIOS_VBE_ATTR_LINEAR) ||
        Info[0x19] != 32 ||
        Info[0x1B] != BIOS_VBE_DIRECT_COLOR ||
        LeUlong(Info + 0x28) == 0)
    {
        return FALSE;
    }

    Fields = (Version >= 0x300) ? 0x36 : 0x1F;
    Pitch = (Version >= 0x300) ? *(PUSHORT)(Info + 0x32) : *(PUSHORT)(Info + 0x10);

    RtlZeroMemory(Display, sizeof(*Display));
    Display->Mode = Mode;
    Display->Width = *(PUSHORT)(Info + 0x12);
    Display->Height = *(PUSHORT)(Info + 0x14);
    Display->Pitch = Pitch;
    Display->BitsPerPixel = 32;
    Display->RedMask = BiosMask(Info[Fields], Info[Fields + 1]);
    Display->GreenMask = BiosMask(Info[Fields + 2], Info[Fields + 3]);
    Display->BlueMask = BiosMask(Info[Fields + 4], Info[Fields + 5]);
    Display->ReservedMask = BiosMask(Info[Fields + 6], Info[Fields + 7]);
    Display->FrameBuffer = LeUlong(Info + 0x28);
    Display->FrameBufferSize = Pitch * Display->Height;

    return Display->Width != 0 &&
           Display->Height != 0 &&
           Pitch >= Display->Width * sizeof(ULONG) &&
           Pitch % sizeof(ULONG) == 0 &&
           Display->RedMask != 0 &&
           Display->GreenMask != 0 &&
           Display->BlueMask != 0;
}

static
VOID
BiosSetGraphicsMode(VOID)
{
    PUCHAR Info = (PUCHAR)(ULONG_PTR)(BIOS_TRANSFER_BASE + BIOS_VBE_INFO_OFFSET);
    USHORT Modes[BIOS_VBE_MAX_MODES];
    BIOS_DISPLAY Candidate, Best;
    BIOS_REGISTERS Registers;
    PUSHORT List;
    USHORT Version;
    ULONG Count, Memory, i, j;
    BOOLEAN Found = FALSE;

    RtlZeroMemory(Info, BIOS_VBE_INFO_SIZE);
    RtlCopyMemory(Info, "VBE2", 4);
    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4F00;
    Registers.Es = BiosSegment(Info);
    Registers.Edi = BiosOffset(Info);
    BiosCall(0x10, &Registers);
    if ((Registers.Eax & 0xFFFF) != BIOS_VBE_SUCCESS || !RtlEqualMemory(Info, "VESA", 4))
        return;

    Version = *(PUSHORT)(Info + 0x04);
    Memory = *(PUSHORT)(Info + 0x12) * 0x10000;
    List = BiosLinear(LeUlong(Info + 0x0E));
    for (Count = 0; Count < RTL_NUMBER_OF(Modes) && List[Count] != 0xFFFF; Count++)
        Modes[Count] = List[Count];

    RtlZeroMemory(&Best, sizeof(Best));
    for (i = 0; i < RTL_NUMBER_OF(BiosPreferredSizes) && !Found; i++)
    {
        for (j = 0; j < Count; j++)
        {
            if (BiosQueryMode(Modes[j], Version, &Candidate) &&
                Candidate.Width == BiosPreferredSizes[i].Width &&
                Candidate.Height == BiosPreferredSizes[i].Height)
            {
                Best = Candidate;
                Found = TRUE;
                break;
            }
        }
    }

    if (!Found)
        return;

    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4F02;
    Registers.Ebx = Best.Mode | BIOS_VBE_LINEAR;
    BiosCall(0x10, &Registers);
    if ((Registers.Eax & 0xFFFF) != BIOS_VBE_SUCCESS)
        return;

    if (Memory > Best.FrameBufferSize)
        Best.FrameBufferSize = Memory;
    BiosShared->Display = Best;
}

BOOLEAN
BiosGetDisplay(
    _Out_ PBIOS_DISPLAY Display)
{
    if (!BiosGraphics)
        return FALSE;

    *Display = BiosDisplay;
    return TRUE;
}

static
VOID
BiosDrawCell(
    _In_ UCHAR Char,
    _In_ UCHAR Attr,
    _In_ ULONG Column,
    _In_ ULONG Row)
{
    volatile USHORT* Text = (volatile USHORT*)BIOS_TEXT_BUFFER;
    PUCHAR Line;
    PULONG Pixel;
    const UCHAR* Glyph;
    ULONG Fore, Back, y, x;

    if (!BiosGraphics)
    {
        if (Column < BIOS_TEXT_COLUMNS && Row < BIOS_TEXT_ROWS)
            Text[Row * BIOS_TEXT_COLUMNS + Column] = (USHORT)(Char | (Attr << 8));
        return;
    }

    if ((Column + 1) * BIOS_CELL_WIDTH > BiosDisplay.Width || (Row + 1) * BIOS_CELL_HEIGHT > BiosDisplay.Height)
        return;

    Fore = BiosColors[Attr & 0x0F];
    Back = BiosColors[(Attr >> 4) & 0x0F];
    Glyph = &BiosFont[Char * BIOS_CELL_HEIGHT];
    Line = (PUCHAR)(ULONG_PTR)BiosDisplay.FrameBuffer +
           (SIZE_T)Row * BIOS_CELL_HEIGHT * BiosDisplay.Pitch +
           (SIZE_T)Column * BIOS_CELL_WIDTH * sizeof(ULONG);

    for (y = 0; y < BIOS_CELL_HEIGHT; y++, Line += BiosDisplay.Pitch)
    {
        Pixel = (PULONG)Line;
        for (x = 0; x < BIOS_CELL_WIDTH; x++)
            Pixel[x] = (BiosFontLoaded && (Glyph[y] & (0x80 >> x))) ? Fore : Back;
    }
}

static
ULONG
BiosColumns(VOID)
{
    return BiosGraphics ? BiosDisplay.Width / BIOS_CELL_WIDTH : BIOS_TEXT_COLUMNS;
}

static
ULONG
BiosRows(VOID)
{
    return BiosGraphics ? BiosDisplay.Height / BIOS_CELL_HEIGHT : BIOS_TEXT_ROWS;
}

static
VOID
BiosScroll(VOID)
{
    PUCHAR Base;
    SIZE_T Line, Rows, i;
    volatile USHORT* Text = (volatile USHORT*)BIOS_TEXT_BUFFER;

    if (!BiosGraphics)
    {
        for (i = 0; i < (BIOS_TEXT_ROWS - 1) * BIOS_TEXT_COLUMNS; i++)
            Text[i] = Text[i + BIOS_TEXT_COLUMNS];
        for (; i < BIOS_TEXT_ROWS * BIOS_TEXT_COLUMNS; i++)
            Text[i] = (BIOS_DEFAULT_ATTRIBUTE << 8) | ' ';
        return;
    }

    Base = (PUCHAR)(ULONG_PTR)BiosDisplay.FrameBuffer;
    Line = (SIZE_T)BIOS_CELL_HEIGHT * BiosDisplay.Pitch;
    Rows = BiosRows();
    RtlMoveMemory(Base, Base + Line, (Rows - 1) * Line);
    for (i = 0; i < BiosColumns(); i++)
        BiosDrawCell(' ', BIOS_DEFAULT_ATTRIBUTE, (ULONG)i, (ULONG)(Rows - 1));
}

VOID
BiosConsoleWrite(
    _In_reads_(Length) PCSTR Text,
    _In_ ULONG Length)
{
    ULONG i;

    for (i = 0; i < Length; i++)
    {
        switch (Text[i])
        {
            case '\r':
                BiosColumn = 0;
                continue;

            case '\n':
                BiosColumn = 0;
                BiosRow++;
                break;

            case '\b':
                if (BiosColumn)
                    BiosColumn--;
                continue;

            default:
                BiosDrawCell((UCHAR)Text[i], BIOS_DEFAULT_ATTRIBUTE, BiosColumn, BiosRow);
                if (++BiosColumn >= BiosColumns())
                {
                    BiosColumn = 0;
                    BiosRow++;
                }
                break;
        }

        if (BiosRow >= BiosRows())
        {
            BiosScroll();
            BiosRow = BiosRows() - 1;
        }
    }
}

VOID
BiosConsoleInitialize(
    _In_ BOOLEAN Root)
{
    ULONG i;

    BiosLoadFont();

    if (Root)
        BiosSetGraphicsMode();

    BiosDisplay = BiosShared->Display;
    BiosGraphics = BiosDisplay.FrameBuffer != 0 && BiosFontLoaded;
    for (i = 0; i < RTL_NUMBER_OF(BiosColors); i++)
        BiosColors[i] = BiosGraphics ? BiosPackColor(BiosVgaColors[i]) : 0;

    if (Root)
        FbConsClearScreen(BIOS_DEFAULT_ATTRIBUTE);
}

BOOLEAN
FwGetFrameBuffer(
    _Out_ PCM_FRAMEBUF_DEVICE_DATA Data,
    _Out_ PULONGLONG Base,
    _Out_ PULONG Size)
{
    if (!BiosGraphics)
        return FALSE;

    RtlZeroMemory(Data, sizeof(*Data));
    Data->ScreenWidth = BiosDisplay.Width;
    Data->ScreenHeight = BiosDisplay.Height;
    Data->PixelsPerScanLine = BiosDisplay.Pitch / sizeof(ULONG);
    Data->BitsPerPixel = BiosDisplay.BitsPerPixel;
    Data->PixelMasks.RedMask = BiosDisplay.RedMask;
    Data->PixelMasks.GreenMask = BiosDisplay.GreenMask;
    Data->PixelMasks.BlueMask = BiosDisplay.BlueMask;
    Data->PixelMasks.ReservedMask = BiosDisplay.ReservedMask;
    Data->LogicalWidth = BiosDisplay.Width;
    Data->LogicalHeight = BiosDisplay.Height;

    *Base = BiosDisplay.FrameBuffer;
    *Size = BiosDisplay.FrameBufferSize;
    return TRUE;
}

BOOLEAN
FwGetEdid(
    _Out_writes_bytes_(Size) PUCHAR Edid,
    _In_ ULONG Size)
{
    PUCHAR Block = (PUCHAR)(ULONG_PTR)BIOS_TRANSFER_DATA;
    BIOS_REGISTERS Registers;

    if (Size > 128)
        return FALSE;

    RtlZeroMemory(Block, 128);
    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4F15;
    Registers.Ebx = 0x0001;
    Registers.Es = BiosSegment(Block);
    Registers.Edi = BiosOffset(Block);
    BiosCall(0x10, &Registers);
    if ((Registers.Eax & 0xFFFF) != BIOS_VBE_SUCCESS)
        return FALSE;

    RtlCopyMemory(Edid, Block, Size);
    return TRUE;
}

VOID
VidFbGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth)
{
    *Width = BiosGraphics ? BiosDisplay.Width : 0;
    *Height = BiosGraphics ? BiosDisplay.Height : 0;
    *Depth = 32;
}

VOID
VidFbPresent(
    _In_ const ULONG* Pixels,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    BOOLEAN Native;
    PULONG Target;
    const ULONG* Source;
    ULONG Row, x;

    if (!BiosGraphics || Left >= BiosDisplay.Width || Top >= BiosDisplay.Height)
        return;

    Width = min(Width, BiosDisplay.Width - Left);
    Height = min(Height, BiosDisplay.Height - Top);
    Native = BiosDisplay.RedMask == 0x00FF0000 && BiosDisplay.GreenMask == 0x0000FF00 && BiosDisplay.BlueMask == 0x000000FF;

    for (Row = 0; Row < Height; Row++)
    {
        Target = (PULONG)((PUCHAR)(ULONG_PTR)BiosDisplay.FrameBuffer +
                          (SIZE_T)(Top + Row) * BiosDisplay.Pitch) + Left;
        Source = Pixels + (SIZE_T)(Top + Row) * BiosDisplay.Width + Left;
        if (Native)
        {
            RtlCopyMemory(Target, Source, Width * sizeof(ULONG));
            continue;
        }

        for (x = 0; x < Width; x++)
            Target[x] = BiosPackColor(Source[x]);
    }
}

VOID
FbConsClearScreen(
    _In_ UCHAR Attr)
{
    ULONG Row, Column;

    for (Row = 0; Row < BiosRows(); Row++)
    {
        for (Column = 0; Column < BiosColumns(); Column++)
            BiosDrawCell(' ', Attr, Column, Row);
    }

    BiosColumn = 0;
    BiosRow = 0;
}

VOID
FbConsPutChar(
    _In_ UCHAR Char,
    _In_ UCHAR Attr,
    _In_ ULONG Column,
    _In_ ULONG Row)
{
    BiosDrawCell(Char, Attr, Column, Row);
}

VOID
FbConsGetCellSize(
    _Out_ PULONG CellWidth,
    _Out_ PULONG CellHeight)
{
    *CellWidth = BIOS_CELL_WIDTH;
    *CellHeight = BIOS_CELL_HEIGHT;
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
    StallExecutionProcessor(BIOS_IDLE_STALL_US);
}

BOOLEAN
FwKeyAvailable(VOID)
{
    BIOS_REGISTERS Registers;

    if (BiosExtendedPending)
        return TRUE;

    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x1100;
    BiosCall(0x16, &Registers);
    return !(Registers.Eflags & BIOS_ZERO_FLAG);
}

int
FwReadKey(VOID)
{
    BIOS_REGISTERS Registers;
    UCHAR Ascii, Scan;

    if (BiosExtendedPending)
    {
        BiosExtendedPending = FALSE;
        return BiosExtendedCode;
    }

    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x1000;
    BiosCall(0x16, &Registers);

    Ascii = (UCHAR)Registers.Eax;
    Scan = (UCHAR)(Registers.Eax >> 8);
    if ((Ascii == 0 || Ascii == 0xE0) && Scan != 0)
    {
        BiosExtendedPending = TRUE;
        BiosExtendedCode = Scan;
        return KEY_EXTENDED;
    }

    return Ascii;
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
    /* TODO: read a PS/2 mouse through the INT 15h C2h pointing device services */
    return FALSE;
}

BOOLEAN
FwSetInputTimer(
    ULONG Milliseconds)
{
    BiosTimerPeriod = Milliseconds;
    BiosTimerStart = BiosMilliseconds();
    return Milliseconds != 0;
}

BOOLEAN
FwInputTimerExpired(VOID)
{
    if (BiosTimerPeriod == 0 || BiosMilliseconds() - BiosTimerStart < BiosTimerPeriod)
        return FALSE;

    BiosTimerPeriod = 0;
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
    FwPrint(L"Firmware setup is not supported by this system.\r\n");
}

VOID
Reboot(VOID)
{
    ULONG Spins;

    _disable();

    for (Spins = 0; Spins < 0x10000 && (__inbyte(BIOS_KBC_STATUS) & 0x02); Spins++)
        ;
    __outbyte(BIOS_KBC_STATUS, BIOS_KBC_RESET);
    StallExecutionProcessor(50000);

    __outbyte(BIOS_RESET_CONTROL, 0x02);
    StallExecutionProcessor(1);
    __outbyte(BIOS_RESET_CONTROL, 0x06);
    StallExecutionProcessor(50000);

    FwHalt();
}
