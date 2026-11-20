/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pixel conversion and placement of the stop screen written through the display miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _DXGK_STOPSCREEN_CORE_H_
#define _DXGK_STOPSCREEN_CORE_H_

#include <ntddk.h>

#define DXGK_STOP_SCREEN_RED   0
#define DXGK_STOP_SCREEN_GREEN 1
#define DXGK_STOP_SCREEN_BLUE  2

typedef struct _DXGK_STOP_SCREEN_CHANNEL
{
    ULONG Mask;
    UCHAR Shift;
    UCHAR Bits;
} DXGK_STOP_SCREEN_CHANNEL, *PDXGK_STOP_SCREEN_CHANNEL;

typedef struct _DXGK_STOP_SCREEN_FORMAT
{
    ULONG BytesPerPixel;
    DXGK_STOP_SCREEN_CHANNEL Channel[3];
} DXGK_STOP_SCREEN_FORMAT, *PDXGK_STOP_SCREEN_FORMAT;

FORCEINLINE
BOOLEAN
DxgkStopScreenCoreSetChannel(
    _Out_ PDXGK_STOP_SCREEN_CHANNEL Channel,
    _In_ ULONG Mask)
{
    UCHAR Shift = 0;
    UCHAR Bits = 0;

    if (Mask == 0)
        return FALSE;

    while ((Mask & (1UL << Shift)) == 0)
        Shift++;
    while ((Shift + Bits < 32) && (Mask & (1UL << (Shift + Bits))) != 0)
        Bits++;

    if ((Bits > 16) || ((Mask >> Shift) != ((1UL << Bits) - 1)))
        return FALSE;

    Channel->Mask = Mask;
    Channel->Shift = Shift;
    Channel->Bits = Bits;
    return TRUE;
}

FORCEINLINE
BOOLEAN
DxgkStopScreenCoreInitFormat(
    _Out_ PDXGK_STOP_SCREEN_FORMAT Format,
    _In_ ULONG BitsPerPixel,
    _In_ ULONG RedMask,
    _In_ ULONG GreenMask,
    _In_ ULONG BlueMask)
{
    RtlZeroMemory(Format, sizeof(*Format));

    if ((BitsPerPixel != 16) && (BitsPerPixel != 24) && (BitsPerPixel != 32))
        return FALSE;
    if (((RedMask & GreenMask) != 0) || ((RedMask & BlueMask) != 0) || ((GreenMask & BlueMask) != 0))
        return FALSE;
    if ((BitsPerPixel < 32) && (((RedMask | GreenMask | BlueMask) >> BitsPerPixel) != 0))
        return FALSE;

    if (!DxgkStopScreenCoreSetChannel(&Format->Channel[DXGK_STOP_SCREEN_RED], RedMask) ||
        !DxgkStopScreenCoreSetChannel(&Format->Channel[DXGK_STOP_SCREEN_GREEN], GreenMask) ||
        !DxgkStopScreenCoreSetChannel(&Format->Channel[DXGK_STOP_SCREEN_BLUE], BlueMask))
    {
        RtlZeroMemory(Format, sizeof(*Format));
        return FALSE;
    }

    Format->BytesPerPixel = BitsPerPixel / 8;
    return TRUE;
}

FORCEINLINE
ULONG
DxgkStopScreenCoreScale(
    _In_ ULONG Pixel,
    _In_ const DXGK_STOP_SCREEN_CHANNEL *Channel)
{
    ULONG Value = (Pixel & Channel->Mask) >> Channel->Shift;
    ULONG Maximum = (1UL << Channel->Bits) - 1;

    ASSERT((Channel->Bits >= 1) && (Channel->Bits <= 16));

    if (Channel->Bits >= 8)
        return Value >> (Channel->Bits - 8);

    return (Value * 255 + Maximum / 2) / Maximum;
}

FORCEINLINE
VOID
DxgkStopScreenCoreConvertRow(
    _In_ const DXGK_STOP_SCREEN_FORMAT *Format,
    _In_reads_bytes_(Count * Format->BytesPerPixel) const UCHAR *Source,
    _In_ ULONG Count,
    _Out_writes_(Count) PULONG Destination)
{
    ULONG Index;
    ULONG Pixel;

    ASSERT((Format->BytesPerPixel >= 2) && (Format->BytesPerPixel <= 4));

    for (Index = 0; Index < Count; Index++, Source += Format->BytesPerPixel)
    {
        if (Format->BytesPerPixel == 4)
        {
            Pixel = *(const ULONG UNALIGNED *)Source;
        }
        else
        {
            Pixel = Source[0] | ((ULONG)Source[1] << 8);
            if (Format->BytesPerPixel == 3)
                Pixel |= (ULONG)Source[2] << 16;
        }

        Destination[Index] = (DxgkStopScreenCoreScale(Pixel, &Format->Channel[DXGK_STOP_SCREEN_RED]) << 16) |
                             (DxgkStopScreenCoreScale(Pixel, &Format->Channel[DXGK_STOP_SCREEN_GREEN]) << 8) |
                             DxgkStopScreenCoreScale(Pixel, &Format->Channel[DXGK_STOP_SCREEN_BLUE]);
    }
}

FORCEINLINE
LONG
DxgkStopScreenCoreOrigin(
    _In_ ULONG SourceExtent,
    _In_ ULONG DisplayExtent)
{
    return (LONG)(((LONGLONG)DisplayExtent - (LONGLONG)SourceExtent) / 2);
}

FORCEINLINE
BOOLEAN
DxgkStopScreenCoreClip(
    _In_ LONG Origin,
    _In_ ULONG DisplayExtent,
    _Inout_ PULONG Start,
    _Inout_ PULONG Length,
    _Out_ PULONG Position)
{
    LONGLONG First = (LONGLONG)Origin + *Start;
    LONGLONG Last = First + *Length;

    *Position = 0;
    if (First < 0)
    {
        *Start += (ULONG)(-First);
        First = 0;
    }
    if (Last > (LONGLONG)DisplayExtent)
        Last = DisplayExtent;
    if (First >= Last)
        return FALSE;

    *Length = (ULONG)(Last - First);
    *Position = (ULONG)First;
    return TRUE;
}

#endif /* _DXGK_STOPSCREEN_CORE_H_ */
