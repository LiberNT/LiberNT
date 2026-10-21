/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI GOP-specific boot UI helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <drivers/bootvid/bootvid.h>
#include "logo.h"
#include "resource.h"
#include "inbvgop.h"


#ifndef BI_RGB
#define BI_RGB 0
#endif
#ifndef BI_BITFIELDS
#define BI_BITFIELDS 3
#endif
#define INBV_WORDMARK_SCALE_NUM     2
#define INBV_WORDMARK_SCALE_DEN     5
#define INBV_WORDMARK_TOP_VPOS      0.08f
#define INBV_WORDMARK_BGRT_SAFE_TOP 0.35f
#define INBV_CENTERMARK_VPOS        0.625f

#define INBV_LOGO_FRAME_MS          15
#define INBV_LOGO_DEFAULT_INCREMENT 100000UL
#define INBV_LOGO_POOL_TAG          'oLbI'
#define INBV_LOGO_WIDTH             51
#define INBV_LOGO_TOP_WIDTH         64
#define INBV_LOGO_LIFT              20
#define INBV_LOGO_UNITS             407
#define INBV_LOGO_PROGRESS_MAX      10000
#define INBV_LOGO_PROGRESS_CEILING  9500
#define INBV_LOGO_PROGRESS_SHARE    5
#define INBV_LOGO_PROGRESS_RATE     16000
#define INBV_LOGO_BAR_LENGTH        7000
#define INBV_LOGO_LOOP_CYCLE_MS     2500
#define INBV_LOGO_MAX_RECTS         8
#define INBV_LOGO_RED               0xE0
#define INBV_LOGO_GREEN             0xE0
#define INBV_LOGO_BLUE              0xE0

typedef struct _INBV_GOP_RECT
{
    ULONG X;
    ULONG Y;
    ULONG Width;
    ULONG Height;
} INBV_GOP_RECT, *PINBV_GOP_RECT;

typedef struct _INBV_LOGO_RECT
{
    LONG Left;
    LONG Top;
    LONG Right;
    LONG Bottom;
} INBV_LOGO_RECT, *PINBV_LOGO_RECT;

static const INBV_LOGO_RECT InbvLogoShape[2][2] =
{
    { {   0,  60,  88, 356 }, {  88, 268, 299, 356 } },
    { { 319,   0, 407, 296 }, { 108,   0, 319,  89 } },
};

typedef struct _INBV_LOGO_STATE
{
    ULONG Left, Top;
    ULONG Width, Height;
    INBV_LOGO_RECT Shape[2][2];
    LONG LineX[2];
    LONG LineY[2];
    LONG HalfX, HalfY;
    LONG Perimeter;
    LONG LineTail;
    LONG LineLength;
    ULONG FillEnd;
    ULONG Lut[256];
    ULONGLONG FrameInterval;
    PULONG Frame;
} INBV_LOGO_STATE;

typedef struct _INBV_STATUS_LINE
{
    ULONG Top;
    ULONG Height;
    ULONG Width;
    ULONG CharWidth;
    ULONG PixelTop;
    ULONG PixelHeight;
    ULONG PixelWidth;
    PUCHAR Coverage;
    PULONG Row;
    ULONG Lut[256];
    BOOLEAN Ready;
} INBV_STATUS_LINE;

static INBV_LOGO_STATE g_Logo;
static INBV_STATUS_LINE g_Status;
static LONG    g_LogoTarget;
static KEVENT  g_LogoStop;
static KEVENT  g_LogoDone;
static BOOLEAN g_LogoReady = FALSE;

static VOID
InbvGopFillStatusBand(VOID)
{
    VidSolidColorFill(0, g_Status.Top, g_Status.Width - 1, g_Status.Top + g_Status.Height - 1, BV_COLOR_BLACK);
}

static VOID
InbvGopStatusFree(
    _In_opt_ PUCHAR Coverage,
    _In_opt_ PULONG Row)
{
    if (Coverage)
        ExFreePoolWithTag(Coverage, INBV_LOGO_POOL_TAG);
    if (Row)
        ExFreePoolWithTag(Row, INBV_LOGO_POOL_TAG);
    g_Status.Coverage = NULL;
    g_Status.Row = NULL;
}

extern VOID NTAPI InbvAcquireLock(VOID);
extern VOID NTAPI InbvReleaseLock(VOID);

CODE_SEG("INIT")
static
BOOLEAN
InbvGopQueryInfo(
    _Out_ LOADER_PARAMETER_FRAMEBUFFER *FbInfo)
{
    if (!FbInfo)
        return FALSE;

    RtlZeroMemory(FbInfo, sizeof(*FbInfo));

    if (!InbvGetGopFrameBufferInfo(FbInfo))
        return FALSE;

    if (FbInfo->FrameBufferSize == 0 ||
        FbInfo->HorizontalResolution == 0 ||
        FbInfo->VerticalResolution == 0)
    {
        return FALSE;
    }

    return TRUE;
}

static ULONG InbvMaskShift(ULONG Mask)
{
    ULONG Shift = 0;
    if (!Mask) return 0;
    while ((Mask & 1) == 0) { Shift++; Mask >>= 1; }
    return Shift;
}

static ULONG InbvMaskMax(ULONG Mask)
{
    ULONG Value = 0;
    while (Mask) { Value = (Value << 1) | 1; Mask >>= 1; }
    return Value;
}

static ULONG InbvPackColor(ULONG RedMask, ULONG GreenMask, ULONG BlueMask,
                           ULONG RedShift, ULONG GreenShift, ULONG BlueShift,
                           ULONG RedMax, ULONG GreenMax, ULONG BlueMax,
                           UCHAR r, UCHAR g, UCHAR b)
{
    ULONG R = RedMask   ? ((ULONG)r * RedMax   + 127) / 255 : 0;
    ULONG G = GreenMask ? ((ULONG)g * GreenMax + 127) / 255 : 0;
    ULONG B = BlueMask  ? ((ULONG)b * BlueMax  + 127) / 255 : 0;
    return (RedMask   ? ((R << RedShift)   & RedMask)   : 0) |
           (GreenMask ? ((G << GreenShift) & GreenMask) : 0) |
           (BlueMask  ? ((B << BlueShift)  & BlueMask)  : 0);
}

static ULONG
InbvGopNormalizeDpi(ULONG Dpi)
{
    if (Dpi < LOADER_PARAMETER_FRAMEBUFFER_DPI_MIN || Dpi > LOADER_PARAMETER_FRAMEBUFFER_DPI_MAX)
    {
        return LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT;
    }

    return Dpi;
}

static ULONG
InbvGopScaleForDpi(ULONG Value, ULONG Dpi)
{
    return (ULONG)(((ULONGLONG)Value * InbvGopNormalizeDpi(Dpi) +
                    LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT / 2) /
                   LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT);
}

static BOOLEAN
InbvGopValidateWordmarkHeader(_In_ const BITMAPINFOHEADER *Header)
{
    if (!Header ||
        Header->biSize < sizeof(BITMAPINFOHEADER) ||
        Header->biPlanes != 1 ||
        Header->biWidth <= 0 ||
        Header->biHeight == 0 ||
        Header->biHeight == (LONG)MINLONG)
    {
        return FALSE;
    }

    if (Header->biBitCount == 4)
        return Header->biCompression == BI_RGB;

    if (Header->biBitCount == 32)
    {
        return Header->biCompression == BI_RGB || Header->biCompression == BI_BITFIELDS;
    }

    return FALSE;
}

static
BOOLEAN
InbvGopComputeWordmarkRect(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight,
    _In_ ULONG Dpi,
    _Out_ PINBV_GOP_RECT Rect)
{
    PBITMAPINFOHEADER Header;
    ULONG SrcWidth, SrcHeight;
    ULONG DstWidth, DstHeight;
    ULONG MaxWidth, SafeBottom;

    if (!Rect)
        return FALSE;

    Header = (PBITMAPINFOHEADER)InbvGetResourceAddress(IDB_REACTOS_GOP_LOGO);
    if (!InbvGopValidateWordmarkHeader(Header))
        return FALSE;

    SrcWidth = (ULONG)Header->biWidth;
    SrcHeight = (Header->biHeight < 0) ? (ULONG)(-Header->biHeight) : (ULONG)Header->biHeight;
    if (!SrcWidth || !SrcHeight)
        return FALSE;

    DstWidth = (ULONG)(((ULONGLONG)SrcWidth * INBV_WORDMARK_SCALE_NUM +
                        INBV_WORDMARK_SCALE_DEN / 2) /
                       INBV_WORDMARK_SCALE_DEN);
    DstWidth = InbvGopScaleForDpi(DstWidth, Dpi);
    if (DstWidth == 0)
        DstWidth = 1;

    DstHeight = (ULONG)(((ULONGLONG)SrcHeight * INBV_WORDMARK_SCALE_NUM +
                         INBV_WORDMARK_SCALE_DEN / 2) /
                        INBV_WORDMARK_SCALE_DEN);
    DstHeight = InbvGopScaleForDpi(DstHeight, Dpi);
    if (DstHeight == 0)
        DstHeight = 1;

    /*
     * Preserve the logo aspect ratio if an extreme configured scale would
     * overlap the display edge or the firmware-logo safe area.
     */
    MaxWidth = max((ScreenWidth * 9) / 10, 1UL);
    SafeBottom = max((ULONG)(ScreenHeight * INBV_WORDMARK_BGRT_SAFE_TOP), 1UL);
    if (DstWidth > MaxWidth)
    {
        DstHeight = max((ULONG)(((ULONGLONG)DstHeight * MaxWidth) / DstWidth), 1UL);
        DstWidth = MaxWidth;
    }
    if (DstHeight > SafeBottom)
    {
        DstWidth = max((ULONG)(((ULONGLONG)DstWidth * SafeBottom) / DstHeight), 1UL);
        DstHeight = SafeBottom;
    }

    Rect->Width = DstWidth;
    Rect->Height = DstHeight;
    Rect->X = (ScreenWidth > DstWidth) ? ((ScreenWidth - DstWidth) / 2) : 0;

    {
        ULONG TopMargin = (ULONG)(ScreenHeight * INBV_WORDMARK_TOP_VPOS + 0.5f);
        if (TopMargin + DstHeight > SafeBottom)
            TopMargin = (SafeBottom > DstHeight) ? (SafeBottom - DstHeight) : 0;
        if (TopMargin + DstHeight > ScreenHeight)
            TopMargin = (ScreenHeight > DstHeight) ? (ScreenHeight - DstHeight) : 0;
        Rect->Y = TopMargin;
    }
    return TRUE;
}

static VOID
InbvGopLogoResetState(VOID)
{
    if (g_Logo.Frame)
        ExFreePoolWithTag(g_Logo.Frame, INBV_LOGO_POOL_TAG);
    RtlZeroMemory(&g_Logo, sizeof(g_Logo));
}

static BOOLEAN
InbvGopLogoBlit(VOID)
{
    const INBV_LOGO_STATE *S = &g_Logo;

    InbvAcquireLock();
    if (!InbvIsBootDriverInstalled() ||
        InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED)
    {
        InbvReleaseLock();
        return FALSE;
    }
    VidBufferToScreenBlt((PUCHAR)S->Frame, S->Left, S->Top,
                         S->Width, S->Height, S->Width * sizeof(ULONG));
    InbvReleaseLock();
    return TRUE;
}

static LONG
InbvGopLogoOverlap(LONG Low, LONG High, LONG Pixel)
{
    LONG Start = max(Low, Pixel * 256);
    LONG End = min(High, (Pixel + 1) * 256);
    return (End > Start) ? (End - Start) : 0;
}

static VOID
InbvGopLogoCover(
    _In_reads_(Count) const INBV_LOGO_RECT *Rects,
    _In_ ULONG Count)
{
    const INBV_LOGO_STATE *S = &g_Logo;
    LONG OverlapY[INBV_LOGO_MAX_RECTS];
    ULONG i, x, y;

    for (y = 0; y < S->Height; y++)
    {
        PULONG Row = S->Frame + (SIZE_T)y * S->Width;

        for (i = 0; i < Count; i++)
            OverlapY[i] = InbvGopLogoOverlap(Rects[i].Top, Rects[i].Bottom, (LONG)y);

        for (x = 0; x < S->Width; x++)
        {
            ULONG Coverage = 0;

            for (i = 0; i < Count; i++)
            {
                if (OverlapY[i])
                {
                    Coverage += (ULONG)(OverlapY[i] *
                                        InbvGopLogoOverlap(Rects[i].Left, Rects[i].Right, (LONG)x));
                }
            }

            Coverage = (Coverage * 255 + 32768) >> 16;
            Row[x] = S->Lut[min(Coverage, 255UL)];
        }
    }
}

static VOID
InbvGopLogoRasterize(
    _In_ ULONG Progress)
{
    const INBV_LOGO_STATE *S = &g_Logo;
    INBV_LOGO_RECT Rects[4];
    ULONG Arm;

    for (Arm = 0; Arm < 2; Arm++)
    {
        PINBV_LOGO_RECT Stem = &Rects[Arm * 2];
        PINBV_LOGO_RECT Foot = &Rects[Arm * 2 + 1];
        LONG StemLength, FootLength, Length, StemFill, FootFill;

        *Stem = S->Shape[Arm][0];
        *Foot = S->Shape[Arm][1];
        StemLength = Stem->Bottom - Stem->Top;
        FootLength = Foot->Right - Foot->Left;
        Length = (LONG)(((ULONGLONG)Progress * (ULONG)(StemLength + FootLength)) /
                        INBV_LOGO_PROGRESS_MAX);
        StemFill = min(Length, StemLength);
        FootFill = Length - StemFill;

        if (Arm == 0)
        {
            Stem->Bottom = Stem->Top + StemFill;
            Foot->Right = Foot->Left + FootFill;
        }
        else
        {
            Stem->Top = Stem->Bottom - StemFill;
            Foot->Left = Foot->Right - FootFill;
        }
    }

    InbvGopLogoCover(Rects, RTL_NUMBER_OF(Rects));
}

static VOID
InbvGopLogoLinePoint(
    _In_ ULONG Side,
    _In_ LONG Distance,
    _Out_ PLONG X,
    _Out_ PLONG Y)
{
    const INBV_LOGO_STATE *S = &g_Logo;

    switch (Side)
    {
        case 0:
            *X = S->LineX[0];
            *Y = S->LineY[0] + Distance;
            break;
        case 1:
            *X = S->LineX[0] + Distance;
            *Y = S->LineY[1];
            break;
        case 2:
            *X = S->LineX[1];
            *Y = S->LineY[1] - Distance;
            break;
        default:
            *X = S->LineX[1] - Distance;
            *Y = S->LineY[0];
            break;
    }
}

static ULONG
InbvGopLogoLineRects(
    _In_ LONG Tail,
    _Inout_updates_(INBV_LOGO_MAX_RECTS) PINBV_LOGO_RECT Rects,
    _In_ ULONG Count)
{
    const INBV_LOGO_STATE *S = &g_Logo;
    const LONG Height = S->LineY[1] - S->LineY[0];
    const LONG Width = S->LineX[1] - S->LineX[0];
    const LONG Start[4] = { 0, Height, Height + Width, 2 * Height + Width };
    const LONG Length[4] = { Height, Width, Height, Width };
    LONG Shift, From, To, Low, High, X0, Y0, X1, Y1;
    ULONG Side;

    for (Shift = 0; Shift <= S->Perimeter; Shift += S->Perimeter)
    {
        From = Tail - Shift;
        To = From + S->LineLength;

        for (Side = 0; Side < 4; Side++)
        {
            Low = max(From, Start[Side]);
            High = min(To, Start[Side] + Length[Side]);
            if (High <= Low)
                continue;
            if (Count >= INBV_LOGO_MAX_RECTS)
                return Count;

            InbvGopLogoLinePoint(Side, Low - Start[Side], &X0, &Y0);
            InbvGopLogoLinePoint(Side, High - Start[Side], &X1, &Y1);
            Rects[Count].Left = min(X0, X1) - S->HalfX;
            Rects[Count].Top = min(Y0, Y1) - S->HalfY;
            Rects[Count].Right = max(X0, X1) + S->HalfX;
            Rects[Count].Bottom = max(Y0, Y1) + S->HalfY;
            Count++;
        }
    }
    return Count;
}

static VOID NTAPI
InbvGopLogoThread(PVOID Context)
{
    const INBV_LOGO_STATE *S = &g_Logo;
    const ULONGLONG Cycle = (ULONGLONG)INBV_LOGO_LOOP_CYCLE_MS * 10000;
    LARGE_INTEGER Delay;
    INBV_LOGO_RECT Rects[INBV_LOGO_MAX_RECTS];
    ULONGLONG Now, Last, Next, LoopStart = 0;
    ULONG Shown = 0, Target, Count;
    LONG Tail;

    UNREFERENCED_PARAMETER(Context);
    Last = Next = KeQueryInterruptTime();

    for (;;)
    {
        Now = KeQueryInterruptTime();
        if (Shown < INBV_LOGO_PROGRESS_MAX)
        {
            Target = (ULONG)ReadNoFence(&g_LogoTarget);
            if (Shown < Target)
            {
                Shown = (ULONG)min((ULONGLONG)Target,
                                   Shown + (Now - Last) * INBV_LOGO_PROGRESS_RATE / 10000000);
                InbvGopLogoRasterize((ULONG)(((ULONGLONG)Shown * S->FillEnd) /
                                             INBV_LOGO_PROGRESS_MAX));
                if (!InbvGopLogoBlit())
                    break;
                LoopStart = Now;
            }
        }
        else
        {
            Tail = (LONG)((S->LineTail + (Now - LoopStart) * (ULONG)S->Perimeter / Cycle) %
                          (ULONG)S->Perimeter);
            Count = InbvGopLogoLineRects(Tail, Rects, 0);
            Count = InbvGopLogoLineRects((Tail + S->Perimeter / 2) % S->Perimeter, Rects, Count);
            InbvGopLogoCover(Rects, Count);
            if (!InbvGopLogoBlit())
                break;
        }

        Last = Now;
        Next += S->FrameInterval;
        Now = KeQueryInterruptTime();
        if (Next <= Now)
            Next = Now + S->FrameInterval;
        Delay.QuadPart = (LONGLONG)(Now - Next);
        if (KeWaitForSingleObject(&g_LogoStop,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  &Delay) != STATUS_TIMEOUT)
        {
            break;
        }
    }

    InbvGopLogoRasterize(INBV_LOGO_PROGRESS_MAX);
    InbvGopLogoBlit();

    InbvGopLogoResetState();
    KeSetEvent(&g_LogoDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

CODE_SEG("INIT")
static LONG
InbvGopLogoScale(ULONG Units, ULONG Width)
{
    return (LONG)((Units * Width + INBV_LOGO_UNITS / 2) / INBV_LOGO_UNITS) * 256;
}

CODE_SEG("INIT")
static BOOLEAN
InbvGopLogoSetup(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight)
{
    INBV_LOGO_STATE *S = &g_Logo;
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG RedShift, GreenShift, BlueShift;
    ULONG RedMax, GreenMax, BlueMax;
    ULONG HalfHeight, CenterY, TopHalf, Lift, Arm, Part, i, Root;
    ULONG Increment, Frame100ns;
    LONG StemLength, Total, Reach, Head;

    RtlZeroMemory(S, sizeof(*S));

    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    S->Width = InbvGopScaleForDpi(INBV_LOGO_WIDTH, FbInfo.Dpi);
    for (Arm = 0; Arm < 2; Arm++)
    {
        for (Part = 0; Part < 2; Part++)
        {
            const INBV_LOGO_RECT *Source = &InbvLogoShape[Arm][Part];
            PINBV_LOGO_RECT Rect = &S->Shape[Arm][Part];

            Rect->Left = InbvGopLogoScale(Source->Left, S->Width);
            Rect->Top = InbvGopLogoScale(Source->Top, S->Width);
            Rect->Right = InbvGopLogoScale(Source->Right, S->Width);
            Rect->Bottom = InbvGopLogoScale(Source->Bottom, S->Width);
            S->Height = max(S->Height, (ULONG)Rect->Bottom / 256);
        }
    }

    if (S->Width == 0 ||
        S->Height == 0 ||
        S->Width > ScreenWidth ||
        S->Height > ScreenHeight)
    {
        return FALSE;
    }

    S->HalfX = max(S->Shape[0][0].Right - S->Shape[0][0].Left,
                   S->Shape[1][0].Right - S->Shape[1][0].Left) / 2;
    S->HalfY = max(S->Shape[0][1].Bottom - S->Shape[0][1].Top,
                   S->Shape[1][1].Bottom - S->Shape[1][1].Top) / 2;
    S->LineX[0] = (S->Shape[0][0].Left + S->Shape[0][0].Right) / 2;
    S->LineX[1] = (S->Shape[1][0].Left + S->Shape[1][0].Right) / 2;
    S->LineY[0] = (S->Shape[1][1].Top + S->Shape[1][1].Bottom) / 2;
    S->LineY[1] = (S->Shape[0][1].Top + S->Shape[0][1].Bottom) / 2;
    S->Perimeter = 2 * ((S->LineY[1] - S->LineY[0]) + (S->LineX[1] - S->LineX[0]));
    if (S->Perimeter <= 0)
        return FALSE;

    StemLength = S->Shape[0][0].Bottom - S->Shape[0][0].Top;
    Total = StemLength + S->Shape[0][1].Right - S->Shape[0][1].Left;
    if (Total <= 0)
        return FALSE;

    Reach = (LONG)(((ULONGLONG)INBV_LOGO_BAR_LENGTH * (ULONG)(S->Perimeter / 2)) /
                   INBV_LOGO_PROGRESS_MAX);
    S->FillEnd = (ULONG)min(((ULONGLONG)(ULONG)Reach * INBV_LOGO_PROGRESS_MAX) / (ULONG)Total,
                            (ULONGLONG)INBV_LOGO_PROGRESS_MAX);
    Reach = (LONG)(((ULONGLONG)S->FillEnd * (ULONG)Total) / INBV_LOGO_PROGRESS_MAX);
    S->LineTail = S->Shape[0][0].Top + S->HalfY - S->LineY[0];
    if (Reach <= StemLength)
        Head = S->Shape[0][0].Top + Reach - S->HalfY - S->LineY[0];
    else
        Head = (S->LineY[1] - S->LineY[0]) + Reach - StemLength;
    S->LineLength = max(Head - S->LineTail, 1);

    S->Left = (ScreenWidth - S->Width) / 2;

    HalfHeight = ScreenHeight / 2;
    CenterY = (ULONG)(HalfHeight +
                      (ScreenHeight - HalfHeight) * INBV_CENTERMARK_VPOS);
    TopHalf = (ULONG)(((ULONGLONG)S->Height * INBV_LOGO_TOP_WIDTH) / (2 * INBV_LOGO_WIDTH));
    Lift = InbvGopScaleForDpi(INBV_LOGO_LIFT, FbInfo.Dpi);
    S->Top = (CenterY > TopHalf + Lift) ? (CenterY - TopHalf - Lift) : 0;
    if (S->Top + S->Height > ScreenHeight)
        S->Top = ScreenHeight - S->Height;

    {
        VID_DISPLAY_INFO DisplayInfo;
        ULONG LogoBottom;

        InbvQueryDisplayInfo(&DisplayInfo);
        if (DisplayInfo.PhysicalHeight && DisplayInfo.Height && DisplayInfo.Width &&
            DisplayInfo.CharacterWidth && DisplayInfo.CharacterHeight < DisplayInfo.Height)
        {
            LogoBottom = (ULONG)(((ULONGLONG)(S->Top + S->Height) * DisplayInfo.Height) /
                                 DisplayInfo.PhysicalHeight);
            g_Status.Width = DisplayInfo.Width;
            g_Status.CharWidth = DisplayInfo.CharacterWidth;
            g_Status.Height = DisplayInfo.CharacterHeight;
            g_Status.Top = (LogoBottom + DisplayInfo.Height) / 2 - DisplayInfo.CharacterHeight / 2;
            if (g_Status.Top + g_Status.Height > DisplayInfo.Height)
                g_Status.Top = DisplayInfo.Height - g_Status.Height;
            g_Status.PixelTop = (ULONG)(((ULONGLONG)g_Status.Top * DisplayInfo.PhysicalHeight) /
                                        DisplayInfo.Height);
            g_Status.PixelHeight = (ULONG)(((ULONGLONG)g_Status.Height * DisplayInfo.PhysicalHeight) /
                                           DisplayInfo.Height);
            g_Status.PixelWidth = DisplayInfo.PhysicalWidth;
            if (g_Status.PixelWidth && g_Status.PixelHeight)
            {
                g_Status.Coverage = ExAllocatePoolWithTag(NonPagedPool,
                                                          (SIZE_T)g_Status.PixelWidth *
                                                          g_Status.PixelHeight,
                                                          INBV_LOGO_POOL_TAG);
                g_Status.Row = ExAllocatePoolWithTag(NonPagedPool,
                                                     (SIZE_T)g_Status.PixelWidth * sizeof(ULONG),
                                                     INBV_LOGO_POOL_TAG);
                if (!g_Status.Coverage || !g_Status.Row)
                    InbvGopStatusFree(g_Status.Coverage, g_Status.Row);
            }
            g_Status.Ready = TRUE;
        }
    }

    RedShift   = InbvMaskShift(FbInfo.RedMask);
    GreenShift = InbvMaskShift(FbInfo.GreenMask);
    BlueShift  = InbvMaskShift(FbInfo.BlueMask);
    RedMax     = InbvMaskMax(FbInfo.RedMask   >> RedShift);
    GreenMax   = InbvMaskMax(FbInfo.GreenMask >> GreenShift);
    BlueMax    = InbvMaskMax(FbInfo.BlueMask  >> BlueShift);

    for (i = 0; i < RTL_NUMBER_OF(S->Lut); ++i)
    {
        S->Lut[i] = InbvPackColor(FbInfo.RedMask, FbInfo.GreenMask, FbInfo.BlueMask,
                                  RedShift, GreenShift, BlueShift,
                                  RedMax, GreenMax, BlueMax,
                                  (UCHAR)(INBV_LOGO_RED * i / 255),
                                  (UCHAR)(INBV_LOGO_GREEN * i / 255),
                                  (UCHAR)(INBV_LOGO_BLUE * i / 255));
        for (Root = 0; (Root + 1) * (Root + 1) <= i * 255; Root++);
        if (i * 255 - Root * Root > Root)
            Root++;
        g_Status.Lut[i] = InbvPackColor(FbInfo.RedMask, FbInfo.GreenMask, FbInfo.BlueMask,
                                        RedShift, GreenShift, BlueShift,
                                        RedMax, GreenMax, BlueMax,
                                        (UCHAR)(INBV_LOGO_RED * Root / 255),
                                        (UCHAR)(INBV_LOGO_GREEN * Root / 255),
                                        (UCHAR)(INBV_LOGO_BLUE * Root / 255));
    }

    Increment = KeQueryTimeIncrement();
    if (Increment == 0)
        Increment = INBV_LOGO_DEFAULT_INCREMENT;

    Frame100ns = (ULONG)INBV_LOGO_FRAME_MS * 10000;
    Frame100ns = ((Frame100ns + Increment - 1) / Increment) * Increment;
    if (Frame100ns == 0)
        Frame100ns = Increment;

    S->FrameInterval = Frame100ns;

    S->Frame = ExAllocatePoolWithTag(NonPagedPool,
                                     (SIZE_T)S->Width * S->Height * sizeof(ULONG),
                                     INBV_LOGO_POOL_TAG);
    if (!S->Frame)
        return FALSE;

    return TRUE;
}

CODE_SEG("INIT")
static VOID
InbvGopLogoStart(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight)
{
    HANDLE Thread;
    OBJECT_ATTRIBUTES ObjAttr;

    if (g_LogoReady)
        return;

    if (!InbvGopLogoSetup(ScreenWidth, ScreenHeight))
        return;

    KeInitializeEvent(&g_LogoStop, NotificationEvent, FALSE);
    KeInitializeEvent(&g_LogoDone, NotificationEvent, FALSE);
    g_LogoReady = TRUE;

    InitializeObjectAttributes(&ObjAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(PsCreateSystemThread(&Thread, 0, &ObjAttr, NULL, NULL,
                                        InbvGopLogoThread, NULL)))
    {
        ZwClose(Thread);
    }
    else
    {
        InbvGopLogoResetState();
        g_LogoReady = FALSE;
    }
}

VOID
NTAPI
InbvIndicateProgress(VOID)
{
    LONG Old, New;

    do
    {
        Old = ReadNoFence(&g_LogoTarget);
        New = Old + (INBV_LOGO_PROGRESS_CEILING - Old) / INBV_LOGO_PROGRESS_SHARE;
        if (New <= Old)
            return;
    } while (InterlockedCompareExchange(&g_LogoTarget, New, Old) != Old);
}

VOID
NTAPI
InbvGopLogoComplete(VOID)
{
    InterlockedExchange(&g_LogoTarget, INBV_LOGO_PROGRESS_MAX);
}

VOID
NTAPI
InbvGopLogoStop(VOID)
{
    if (!g_LogoReady)
        return;

    KeSetEvent(&g_LogoStop, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&g_LogoDone, Executive, KernelMode, FALSE, NULL);
    g_LogoReady = FALSE;
}

BOOLEAN
NTAPI
InbvGopDisplayStatus(
    _In_ PCSTR String)
{
    CHAR Line[128];
    SIZE_T Start, End, Length, MaxChars;
    ULONG Left, x, y;

    if (!g_Status.Ready)
        return FALSE;

    End = strlen(String);
    while (End != 0 && (String[End - 1] == '\r' || String[End - 1] == '\n' || String[End - 1] == ' '))
        End--;
    Start = End;
    while (Start != 0 && String[Start - 1] != '\r' && String[Start - 1] != '\n')
        Start--;
    while (Start < End && String[Start] == ' ')
        Start++;

    MaxChars = g_Status.Width / g_Status.CharWidth;
    MaxChars = (MaxChars > 2) ? (MaxChars - 2) : 0;
    if (MaxChars >= sizeof(Line))
        MaxChars = sizeof(Line) - 1;
    Length = End - Start;
    if (Length > MaxChars)
        Length = MaxChars;
    if (Length == 0)
        return TRUE;
    RtlCopyMemory(Line, String + Start, Length);
    Line[Length] = '\0';

    InbvAcquireLock();
    if (g_Status.Ready &&
        InbvIsBootDriverInstalled() &&
        InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
    {
        if (g_Status.Coverage &&
            VidRenderStatusText(Line, g_Status.Coverage, g_Status.PixelWidth, g_Status.PixelHeight))
        {
            for (y = 0; y < g_Status.PixelHeight; y++)
            {
                const UCHAR *Source = g_Status.Coverage + (SIZE_T)y * g_Status.PixelWidth;

                for (x = 0; x < g_Status.PixelWidth; x++)
                    g_Status.Row[x] = g_Status.Lut[Source[x]];
                VidBufferToScreenBlt((PUCHAR)g_Status.Row,
                                     0,
                                     g_Status.PixelTop + y,
                                     g_Status.PixelWidth,
                                     1,
                                     g_Status.PixelWidth * sizeof(ULONG));
            }
        }
        else
        {
            InbvGopFillStatusBand();
            Left = (g_Status.Width - (ULONG)Length * g_Status.CharWidth + g_Status.CharWidth) /
                   (2 * g_Status.CharWidth) * g_Status.CharWidth;
            VidSetScrollRegion(Left,
                               g_Status.Top,
                               min(Left + ((ULONG)Length + 1) * g_Status.CharWidth, g_Status.Width) - 1,
                               g_Status.Top + g_Status.Height - 1);
            VidDisplayString(Line);
        }
    }
    InbvReleaseLock();
    return TRUE;
}

VOID
NTAPI
InbvGopClearStatus(VOID)
{
    PUCHAR Coverage;
    PULONG Row;

    if (!g_Status.Ready)
        return;

    InbvAcquireLock();
    if (InbvIsBootDriverInstalled() && InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
        InbvGopFillStatusBand();
    g_Status.Ready = FALSE;
    Coverage = g_Status.Coverage;
    Row = g_Status.Row;
    InbvReleaseLock();

    InbvGopStatusFree(Coverage, Row);
}

static VOID
NTAPI
InbvGopDisplayFilter(
    _Inout_ PCSTR *String)
{
    InbvGopDisplayStatus(*String);
    *String = "";
}

typedef struct _INBV_GOP_RGB
{
    ULONG Red;
    ULONG Green;
    ULONG Blue;
} INBV_GOP_RGB, *PINBV_GOP_RGB;

static UCHAR
InbvGopExtractChannel(ULONG Pixel, ULONG Mask)
{
    ULONG Shift;
    ULONG MaxValue;
    ULONG Value;

    if (!Mask)
        return 0;

    Shift = InbvMaskShift(Mask);
    MaxValue = InbvMaskMax(Mask >> Shift);
    if (!MaxValue)
        return 0;

    Value = (Pixel & Mask) >> Shift;
    return (UCHAR)(((ULONGLONG)Value * 255 + MaxValue / 2) / MaxValue);
}

static PULONG InbvBootScreen;
static ULONG InbvBootScreenWidth;
static ULONG InbvBootScreenHeight;

BOOLEAN
NTAPI
InbvGopCaptureScreen(VOID)
{
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    VID_DISPLAY_INFO DisplayInfo;
    PULONG Pixels;
    SIZE_T Count, i;

    if (InbvBootScreen)
        return TRUE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !InbvIsBootDriverInstalled() ||
        InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED ||
        !InbvGetGopFrameBufferInfo(&FbInfo) ||
        !FbInfo.RedMask || !FbInfo.GreenMask || !FbInfo.BlueMask)
    {
        return FALSE;
    }

    InbvQueryDisplayInfo(&DisplayInfo);
    if (!DisplayInfo.PhysicalWidth || !DisplayInfo.PhysicalHeight ||
        DisplayInfo.PhysicalWidth > MAXULONG / sizeof(ULONG) / DisplayInfo.PhysicalHeight)
    {
        return FALSE;
    }

    Count = (SIZE_T)DisplayInfo.PhysicalWidth * DisplayInfo.PhysicalHeight;
    Pixels = ExAllocatePoolWithTag(PagedPool, Count * sizeof(ULONG), INBV_LOGO_POOL_TAG);
    if (!Pixels)
        return FALSE;

    VidScreenToBufferBlt((PUCHAR)Pixels,
                         0,
                         0,
                         DisplayInfo.PhysicalWidth,
                         DisplayInfo.PhysicalHeight,
                         DisplayInfo.PhysicalWidth * sizeof(ULONG));

    if (FbInfo.RedMask == 0x00FF0000 && FbInfo.GreenMask == 0x0000FF00 && FbInfo.BlueMask == 0x000000FF)
    {
        for (i = 0; i < Count; i++)
            Pixels[i] &= 0x00FFFFFF;
    }
    else
    {
        for (i = 0; i < Count; i++)
        {
            ULONG Pixel = Pixels[i];

            Pixels[i] = ((ULONG)InbvGopExtractChannel(Pixel, FbInfo.RedMask) << 16) |
                        ((ULONG)InbvGopExtractChannel(Pixel, FbInfo.GreenMask) << 8) |
                        (ULONG)InbvGopExtractChannel(Pixel, FbInfo.BlueMask);
        }
    }

    InbvBootScreenWidth = DisplayInfo.PhysicalWidth;
    InbvBootScreenHeight = DisplayInfo.PhysicalHeight;
    InterlockedExchangePointer((PVOID *)&InbvBootScreen, Pixels);
    return TRUE;
}

NTSTATUS
NTAPI
InbvQueryBootScreen(
    _Out_writes_bytes_opt_(*Height * Stride) PVOID Bits,
    _In_ LONG Stride,
    _Inout_ PULONG Width,
    _Inout_ PULONG Height)
{
    PULONG Pixels;
    ULONG y;

    if (!Width || !Height)
        return STATUS_INVALID_PARAMETER;

    if (!InbvGopCaptureScreen())
        return STATUS_NOT_FOUND;

    if (!Bits)
    {
        *Width = InbvBootScreenWidth;
        *Height = InbvBootScreenHeight;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (*Width != InbvBootScreenWidth ||
        *Height != InbvBootScreenHeight ||
        (ULONG)((Stride < 0) ? -Stride : Stride) < InbvBootScreenWidth * sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Pixels = InterlockedExchangePointer((PVOID *)&InbvBootScreen, NULL);
    if (!Pixels)
        return STATUS_NOT_FOUND;

    for (y = 0; y < InbvBootScreenHeight; y++)
    {
        RtlCopyMemory((PUCHAR)Bits + (LONG_PTR)y * Stride,
                      Pixels + (SIZE_T)y * InbvBootScreenWidth,
                      InbvBootScreenWidth * sizeof(ULONG));
    }
    ExFreePoolWithTag(Pixels, INBV_LOGO_POOL_TAG);
    return STATUS_SUCCESS;
}

VOID
NTAPI
InbvDiscardBootScreen(VOID)
{
    PVOID Pixels = InterlockedExchangePointer((PVOID *)&InbvBootScreen, NULL);

    if (Pixels)
        ExFreePoolWithTag(Pixels, INBV_LOGO_POOL_TAG);
}

static VOID
InbvGopReadWordmarkPixel(
    _In_reads_bytes_(Stride * SrcHeight) const UCHAR *Bits,
    ULONG Stride,
    ULONG BitCount,
    BOOLEAN TopDown,
    ULONG SrcHeight,
    ULONG X,
    ULONG Y,
    _In_reads_opt_(PaletteCount) const RGBQUAD *Palette,
    ULONG PaletteCount,
    ULONG RedMask,
    ULONG GreenMask,
    ULONG BlueMask,
    ULONG AlphaMask,
    _Out_ PINBV_GOP_RGB Color)
{
    const UCHAR *Row;

    Row = Bits + (SIZE_T)(TopDown ? Y : (SrcHeight - 1 - Y)) * Stride;
    if (BitCount == 4)
    {
        UCHAR Value = Row[X / 2];
        ULONG Index = (X & 1) ? (Value & 0x0f) : (Value >> 4);

        if (Index >= PaletteCount)
            Index = 0;

        Color->Red = Palette[Index].rgbRed;
        Color->Green = Palette[Index].rgbGreen;
        Color->Blue = Palette[Index].rgbBlue;
    }
    else
    {
        ULONG Pixel;
        ULONG Alpha = 255;

        RtlCopyMemory(&Pixel, Row + (SIZE_T)X * sizeof(Pixel), sizeof(Pixel));
        Color->Red = InbvGopExtractChannel(Pixel, RedMask);
        Color->Green = InbvGopExtractChannel(Pixel, GreenMask);
        Color->Blue = InbvGopExtractChannel(Pixel, BlueMask);

        if (AlphaMask)
            Alpha = InbvGopExtractChannel(Pixel, AlphaMask);

        /* The boot background is black, so premultiply transparent pixels. */
        Color->Red = (Color->Red * Alpha + 127) / 255;
        Color->Green = (Color->Green * Alpha + 127) / 255;
        Color->Blue = (Color->Blue * Alpha + 127) / 255;
    }
}

static
BOOLEAN
InbvGopDrawWordmark(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight,
    _In_opt_ const INBV_GOP_RECT *WordmarkRectOverride)
{
    PUCHAR Bitmap;
    PBITMAPINFOHEADER Header;
    ULONG SrcWidth, SrcHeight, SrcDelta;
    INBV_GOP_RECT WordmarkRect;
    ULONG DstWidth, DstHeight, DestX, DestY;
    BOOLEAN TopDown = FALSE;
    PUCHAR Bits;
    LPRGBQUAD Palette = NULL;
    ULONG PaletteCount = 0;
    ULONG SourceRedMask = 0x00ff0000;
    ULONG SourceGreenMask = 0x0000ff00;
    ULONG SourceBlueMask = 0x000000ff;
    ULONG SourceAlphaMask = 0;

    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG RedMask = 0, GreenMask = 0, BlueMask = 0;
    ULONG RedShift = 0, GreenShift = 0, BlueShift = 0;
    ULONG RedMax = 0, GreenMax = 0, BlueMax = 0;

    Bitmap = InbvGetResourceAddress(IDB_REACTOS_GOP_LOGO);
    Header = (PBITMAPINFOHEADER)Bitmap;
    if (!InbvGopValidateWordmarkHeader(Header))
        return FALSE;

    SrcWidth = (ULONG)Header->biWidth;
    SrcHeight = (Header->biHeight < 0) ? (ULONG)(-Header->biHeight) : (ULONG)Header->biHeight;
    TopDown = (Header->biHeight < 0);
    if (!SrcWidth || !SrcHeight)
        return FALSE;

    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    if (WordmarkRectOverride)
    {
        WordmarkRect = *WordmarkRectOverride;
    }
    else
    {
        if (!InbvGopComputeWordmarkRect(ScreenWidth, ScreenHeight, FbInfo.Dpi, &WordmarkRect))
            return FALSE;
    }

    DstWidth = WordmarkRect.Width;
    DstHeight = WordmarkRect.Height;
    DestX = WordmarkRect.X;
    DestY = WordmarkRect.Y;

    if (!DstWidth || !DstHeight || DestX >= ScreenWidth || DestY >= ScreenHeight)
    {
        return FALSE;
    }

    DstWidth = min(DstWidth, ScreenWidth - DestX);
    DstHeight = min(DstHeight, ScreenHeight - DestY);

    RedMask = FbInfo.RedMask; GreenMask = FbInfo.GreenMask; BlueMask = FbInfo.BlueMask;
    RedShift = InbvMaskShift(RedMask); GreenShift = InbvMaskShift(GreenMask); BlueShift = InbvMaskShift(BlueMask);
    RedMax = InbvMaskMax(RedMask >> RedShift); GreenMax = InbvMaskMax(GreenMask >> GreenShift); BlueMax = InbvMaskMax(BlueMask >> BlueShift);

    Bits = Bitmap + Header->biSize;
    if (Header->biBitCount == 4)
    {
        PaletteCount = Header->biClrUsed ? min(Header->biClrUsed, 16UL) : 16;
        Palette = (LPRGBQUAD)Bits;
        Bits += PaletteCount * sizeof(*Palette);
    }
    else if (Header->biCompression == BI_BITFIELDS)
    {
        const ULONG *Masks = (const ULONG *)(Bitmap + sizeof(BITMAPINFOHEADER));

        SourceRedMask = Masks[0];
        SourceGreenMask = Masks[1];
        SourceBlueMask = Masks[2];

        /*
         * V4/V5 headers embed four masks after the common 40-byte header.
         * A 40-byte BITMAPINFOHEADER stores three masks immediately before
         * the pixel array instead.
         */
        if (Header->biSize >= sizeof(BITMAPINFOHEADER) + 4 * sizeof(ULONG))
            SourceAlphaMask = Masks[3];
        else
            Bits += 3 * sizeof(*Masks);

        if (!SourceRedMask || !SourceGreenMask || !SourceBlueMask)
            return FALSE;
    }

    SrcDelta = (ULONG)((((ULONGLONG)SrcWidth * Header->biBitCount + 31) / 32) * 4);
    if (!SrcDelta)
        return FALSE;

    for (ULONG dy = 0; dy < DstHeight; dy++)
    {
        ULONG sy = (ULONG)min((ULONGLONG)SrcHeight - 1,
                              ((ULONGLONG)dy * SrcHeight) / DstHeight);
        ULONG sy1 = (DstHeight < SrcHeight) ? min(sy + 1, SrcHeight - 1) : sy;

        static ULONG LineBuf[1024];
        ULONG produced = 0;
        while (produced < DstWidth)
        {
            ULONG toDo = min((ULONG)1024, DstWidth - produced);
            for (ULONG dx = 0; dx < toDo; dx++)
            {
                ULONG sx0 = (ULONG)min((ULONGLONG)SrcWidth - 1,
                                       ((ULONGLONG)(produced + dx) * SrcWidth) /
                                       DstWidth);
                ULONG sx1 = (DstWidth < SrcWidth) ? min(sx0 + 1, SrcWidth - 1) : sx0;
                INBV_GOP_RGB C00, C10, C01, C11;
                ULONG r, g, b;

                InbvGopReadWordmarkPixel(Bits, SrcDelta, Header->biBitCount,
                                         TopDown, SrcHeight, sx0, sy,
                                         Palette, PaletteCount,
                                         SourceRedMask, SourceGreenMask,
                                         SourceBlueMask, SourceAlphaMask, &C00);
                InbvGopReadWordmarkPixel(Bits, SrcDelta, Header->biBitCount,
                                         TopDown, SrcHeight, sx1, sy,
                                         Palette, PaletteCount,
                                         SourceRedMask, SourceGreenMask,
                                         SourceBlueMask, SourceAlphaMask, &C10);
                InbvGopReadWordmarkPixel(Bits, SrcDelta, Header->biBitCount,
                                         TopDown, SrcHeight, sx0, sy1,
                                         Palette, PaletteCount,
                                         SourceRedMask, SourceGreenMask,
                                         SourceBlueMask, SourceAlphaMask, &C01);
                InbvGopReadWordmarkPixel(Bits, SrcDelta, Header->biBitCount,
                                         TopDown, SrcHeight, sx1, sy1,
                                         Palette, PaletteCount,
                                         SourceRedMask, SourceGreenMask,
                                         SourceBlueMask, SourceAlphaMask, &C11);

                r = (C00.Red + C10.Red + C01.Red + C11.Red) / 4;
                g = (C00.Green + C10.Green + C01.Green + C11.Green) / 4;
                b = (C00.Blue + C10.Blue + C01.Blue + C11.Blue) / 4;

                LineBuf[dx] = InbvPackColor(RedMask,GreenMask,BlueMask,
                                             RedShift,GreenShift,BlueShift,
                                             RedMax,GreenMax,BlueMax,
                                             (UCHAR)r,(UCHAR)g,(UCHAR)b);
            }
            VidBufferToScreenBlt((PUCHAR)LineBuf,
                                 DestX + produced,
                                 DestY + dy,
                                 toDo,
                                 1,
                                 toDo * sizeof(ULONG));
            produced += toDo;
        }
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvGopHandleBootBitmap(VOID)
{
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    VID_DISPLAY_INFO DisplayInfo;
    ULONG Width, Height;
    BOOLEAN WordmarkDrawn;

    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    /* Native 32-bit blits use physical dimensions after rotation. */
    InbvQueryDisplayInfo(&DisplayInfo);
    Width = DisplayInfo.PhysicalWidth;
    Height = DisplayInfo.PhysicalHeight;
    if ((Width == 0) || (Height == 0))
        return FALSE;

    InbvSetTextColor(BV_COLOR_WHITE);
    InbvInstallDisplayStringFilter(InbvGopDisplayFilter);

    WordmarkDrawn = InbvGopDrawWordmark(Width, Height, NULL);
    InbvGopLogoStart(Width, Height);

    if (!WordmarkDrawn)
    {
        static const CHAR LoadingMsg[] = "ReactOS";
        ULONG CharW;
        ULONG msgPx;
        ULONG x;
        ULONG y;

        CharW = DisplayInfo.CharacterWidth ? DisplayInfo.CharacterWidth : 8;
        msgPx = (ULONG)(sizeof(LoadingMsg) - 1) * CharW;
        x = (DisplayInfo.Width > msgPx) ? ((DisplayInfo.Width - msgPx) / 2) : 0;
        y = (ULONG)(DisplayInfo.Height * INBV_WORDMARK_TOP_VPOS + 0.5f);
        VidDisplayStringXY(LoadingMsg, x, y, TRUE);
    }

    InbvEnableDisplayString(FALSE);
    return TRUE;
}
