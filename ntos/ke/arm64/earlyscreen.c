/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 early framebuffer console for DEBUGPORT=SCREEN before boot video starts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/arm64/earlyfont.h>

#define KI_EARLY_SCREEN_CELL_HEIGHT   14
#define KI_EARLY_SCREEN_CACHE_LINE    64
#define KI_EARLY_SCREEN_ALIAS_BASE    0xFFFFFC0000000000ULL

static volatile ULONG *KiEarlyScreenBase;
static ULONG KiEarlyScreenWidth;
static ULONG KiEarlyScreenHeight;
static ULONG KiEarlyScreenStride;
static ULONG KiEarlyScreenScale;
static ULONG KiEarlyScreenColumns;
static ULONG KiEarlyScreenRows;
static ULONG KiEarlyScreenColumn;
static ULONG KiEarlyScreenRow;
static ULONG KiEarlyScreenForeground;
static ULONG64 KiEarlyScreenTtbr0;
static BOOLEAN KiEarlyScreenAlias;


static
BOOLEAN
KiEarlyScreenTranslates(
    _In_ ULONG_PTR Address)
{
    ULONG64 Result;

    ARM64_AT(S1E1W, Address);
    ARM64_ISB();
    Result = ARM64_READ_SYSREG(PAR_EL1);
    return (Result & 1) == 0;
}

static
VOID
KiEarlyScreenClean(
    _In_ volatile ULONG *Pixels,
    _In_ ULONG Count)
{
    ULONG_PTR Line = (ULONG_PTR)Pixels & ~((ULONG_PTR)KI_EARLY_SCREEN_CACHE_LINE - 1);
    ULONG_PTR End = (ULONG_PTR)(Pixels + Count);

    for (; Line < End; Line += KI_EARLY_SCREEN_CACHE_LINE)
        ARM64_DC(CIVAC, Line);
}

static
VOID
KiEarlyScreenClearRow(
    _In_ ULONG Row)
{
    ULONG Line;
    ULONG X;
    ULONG First = Row * KI_EARLY_SCREEN_CELL_HEIGHT * KiEarlyScreenScale;
    ULONG Lines = KI_EARLY_SCREEN_CELL_HEIGHT * KiEarlyScreenScale;

    for (Line = First; Line < First + Lines && Line < KiEarlyScreenHeight; Line++)
    {
        volatile ULONG *Pixels = KiEarlyScreenBase + (SIZE_T)Line * KiEarlyScreenStride;

        for (X = 0; X < KiEarlyScreenWidth; X++)
            Pixels[X] = 0;
        KiEarlyScreenClean(Pixels, KiEarlyScreenWidth);
    }
}

static
VOID
KiEarlyScreenDrawChar(
    _In_ CHAR Character)
{
    const UCHAR *Glyph;
    ULONG Y, X, SubY, SubX;
    ULONG Left = KiEarlyScreenColumn * EARLY_FONT_WIDTH * KiEarlyScreenScale;
    ULONG Top = KiEarlyScreenRow * KI_EARLY_SCREEN_CELL_HEIGHT * KiEarlyScreenScale;

    if ((UCHAR)Character < EARLY_FONT_FIRST_CHAR || (UCHAR)Character > EARLY_FONT_LAST_CHAR)
        Character = '?';
    Glyph = EarlyFont8x13[(UCHAR)Character - EARLY_FONT_FIRST_CHAR];

    for (Y = 0; Y < EARLY_FONT_HEIGHT; Y++)
    {
        for (SubY = 0; SubY < KiEarlyScreenScale; SubY++)
        {
            volatile ULONG *Pixels = KiEarlyScreenBase +
                                     (SIZE_T)(Top + Y * KiEarlyScreenScale + SubY) * KiEarlyScreenStride + Left;

            for (X = 0; X < EARLY_FONT_WIDTH; X++)
            {
                ULONG Color = (Glyph[Y] & (0x80 >> X)) ? KiEarlyScreenForeground : 0;

                for (SubX = 0; SubX < KiEarlyScreenScale; SubX++)
                    Pixels[X * KiEarlyScreenScale + SubX] = Color;
            }
            KiEarlyScreenClean(Pixels, EARLY_FONT_WIDTH * KiEarlyScreenScale);
        }
    }
}

static
VOID
KiEarlyScreenNewLine(VOID)
{
    KiEarlyScreenColumn = 0;
    KiEarlyScreenRow = (KiEarlyScreenRow + 1) % KiEarlyScreenRows;
    KiEarlyScreenClearRow(KiEarlyScreenRow);
}

VOID
NTAPI
KiArm64EarlyScreenWrite(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    ULONG Index;

    if (!KiEarlyScreenBase)
        return;
    if (KiEarlyScreenAlias)
    {
        if (!KiEarlyScreenTranslates((ULONG_PTR)KiEarlyScreenBase))
            return;
    }
    else if (ARM64_READ_SYSREG(TTBR0_EL1) != KiEarlyScreenTtbr0)
    {
        return;
    }

    for (Index = 0; Index < Length && String[Index]; Index++)
    {
        if (String[Index] == '\n')
        {
            KiEarlyScreenNewLine();
            continue;
        }
        if (String[Index] == '\r')
        {
            KiEarlyScreenColumn = 0;
            continue;
        }
        if (KiEarlyScreenColumn >= KiEarlyScreenColumns)
            KiEarlyScreenNewLine();
        KiEarlyScreenDrawChar(String[Index]);
        KiEarlyScreenColumn++;
    }

    ARM64_DSB(SY);
}

static
VOID
KiEarlyScreenPrint(
    _In_ PCSTR Format,
    ...)
{
    CHAR Text[256];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);
    KiArm64EarlyScreenWrite(Text, sizeof(Text));
}

VOID
KiArm64EarlyScreenInitialize(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_FRAMEBUFFER FrameBuffer;
    ULONG Row;

    if (!LoaderBlock || !LoaderBlock->Extension || !LoaderBlock->LoadOptions ||
        !strstr(LoaderBlock->LoadOptions, "DEBUGPORT=SCREEN") ||
        LoaderBlock->Extension->Size < FIELD_OFFSET(LOADER_PARAMETER_EXTENSION, GopFramebuffer) +
                                       sizeof(LOADER_PARAMETER_FRAMEBUFFER))
    {
        return;
    }

    FrameBuffer = &LoaderBlock->Extension->GopFramebuffer;
    if (FrameBuffer->FrameBufferBase.QuadPart == 0 || FrameBuffer->PixelFormat != 32 ||
        FrameBuffer->HorizontalResolution < 320 || FrameBuffer->VerticalResolution < 200 ||
        FrameBuffer->PixelsPerScanLine < FrameBuffer->HorizontalResolution)
    {
        return;
    }

    KiEarlyScreenWidth = FrameBuffer->HorizontalResolution;
    KiEarlyScreenHeight = FrameBuffer->VerticalResolution;
    KiEarlyScreenStride = FrameBuffer->PixelsPerScanLine;
    KiEarlyScreenScale = (KiEarlyScreenWidth >= 2048) ? 2 : 1;
    KiEarlyScreenColumns = KiEarlyScreenWidth / (EARLY_FONT_WIDTH * KiEarlyScreenScale);
    KiEarlyScreenRows = KiEarlyScreenHeight / (KI_EARLY_SCREEN_CELL_HEIGHT * KiEarlyScreenScale);
    KiEarlyScreenForeground = (FrameBuffer->RedMask | FrameBuffer->GreenMask | FrameBuffer->BlueMask) ?
                              (FrameBuffer->RedMask | FrameBuffer->GreenMask | FrameBuffer->BlueMask) :
                              0x00FFFFFF;
    KiEarlyScreenTtbr0 = ARM64_READ_SYSREG(TTBR0_EL1);
    for (KiEarlyScreenAlias = TRUE; ; KiEarlyScreenAlias = FALSE)
    {
        ULONG64 Base = KiEarlyScreenAlias ? (KI_EARLY_SCREEN_ALIAS_BASE | FrameBuffer->FrameBufferBase.QuadPart) :
                                            (ULONG64)FrameBuffer->FrameBufferBase.QuadPart;
        BOOLEAN Mapped = TRUE;

        for (Row = 0; Row < KiEarlyScreenHeight && Mapped; Row += 64)
            Mapped = KiEarlyScreenTranslates((ULONG_PTR)(Base + (ULONG64)Row * KiEarlyScreenStride * sizeof(ULONG)));
        if (Mapped)
            Mapped = KiEarlyScreenTranslates((ULONG_PTR)(Base + (ULONG64)KiEarlyScreenHeight * KiEarlyScreenStride * sizeof(ULONG) - 1));
        if (Mapped)
        {
            KiEarlyScreenBase = (volatile ULONG *)(ULONG_PTR)Base;
            break;
        }
        if (!KiEarlyScreenAlias)
            return;
    }

    for (Row = 0; Row < KiEarlyScreenRows; Row++)
        KiEarlyScreenClearRow(Row);
    KiEarlyScreenRow = 0;
    KiEarlyScreenColumn = 0;

    KiEarlyScreenPrint("LiberNT kernel started: early screen console %lux%lu stride %lu masks %08lx/%08lx/%08lx\n",
                       KiEarlyScreenWidth, KiEarlyScreenHeight, KiEarlyScreenStride,
                       FrameBuffer->RedMask, FrameBuffer->GreenMask, FrameBuffer->BlueMask);
    KiEarlyScreenPrint("options: %s\n", LoaderBlock->LoadOptions);
    KiEarlyScreenPrint("early screen through the %s mapping\n", KiEarlyScreenAlias ? "kernel alias" : "identity");
#ifndef _WINKD_
    KdpEarlyScreenRoutine = KiArm64EarlyScreenWrite;
#else
    /* TODO: feed the early screen console from the KD64 debug print path */
#endif
}
