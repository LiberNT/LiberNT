/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ClearCodec (MS-RDPEGFX 2.2.4.1) encoder
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <assert.h>
#include <string.h>
#include "clearenc.h"

#define PALETTE_BUCKETS 256

typedef struct CLEAR_WRITER
{
    BYTE *Data;
    UINT32 Capacity, Length;
} CLEAR_WRITER;

typedef struct CLEAR_PALETTE
{
    UINT32 Colors[CLEAR_MAX_PALETTE + 1];
    UINT32 Count;
    UINT32 Keys[PALETTE_BUCKETS];
    BYTE Indexes[PALETTE_BUCKETS];
    BOOL Used[PALETTE_BUCKETS];
} CLEAR_PALETTE;

static UINT32 Pixel(const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 position)
{
    return *(const UINT32 *)(pixels + (size_t)(position / width) * stride + (size_t)(position % width) * 4) & 0x00FFFFFF;
}

static BOOL PaletteIndex(CLEAR_PALETTE *palette, UINT32 color, UINT32 limit, BYTE *index)
{
    UINT32 slot = (color * 0x9E3779B1u) >> 24;
    while (palette->Used[slot])
    {
        if (palette->Keys[slot] == color)
        {
            *index = palette->Indexes[slot];
            return TRUE;
        }
        slot = (slot + 1) & (PALETTE_BUCKETS - 1);
    }
    if (palette->Count >= limit) return FALSE;
    palette->Used[slot] = TRUE;
    palette->Keys[slot] = color;
    palette->Indexes[slot] = (BYTE)palette->Count;
    palette->Colors[palette->Count] = color;
    *index = (BYTE)palette->Count++;
    return TRUE;
}

static BOOL BuildPalette(CLEAR_PALETTE *palette, const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 height, UINT32 limit)
{
    UINT32 position, total = width * height;
    BYTE index;
    assert(limit <= CLEAR_MAX_PALETTE);
    memset(palette, 0, sizeof(*palette));
    for (position = 0; position < total; ++position)
        if (!PaletteIndex(palette, Pixel(pixels, stride, width, position), limit, &index)) return FALSE;
    return TRUE;
}

UINT32 ClearPaletteSize(const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 height, UINT32 limit)
{
    CLEAR_PALETTE palette;
    if (!BuildPalette(&palette, pixels, stride, width, height, min(limit, CLEAR_MAX_PALETTE)))
        return limit + 1;
    return palette.Count;
}

static BOOL WriteBytes(CLEAR_WRITER *writer, const void *data, UINT32 length)
{
    if (writer->Capacity - writer->Length < length) return FALSE;
    memcpy(writer->Data + writer->Length, data, length);
    writer->Length += length;
    return TRUE;
}

static BOOL WriteByte(CLEAR_WRITER *writer, BYTE value)
{
    return WriteBytes(writer, &value, 1);
}

static BOOL WriteUInt16(CLEAR_WRITER *writer, UINT16 value)
{
    BYTE data[2] = { (BYTE)value, (BYTE)(value >> 8) };
    return WriteBytes(writer, data, sizeof(data));
}

static BOOL WriteUInt32(CLEAR_WRITER *writer, UINT32 value)
{
    BYTE data[4] = { (BYTE)value, (BYTE)(value >> 8), (BYTE)(value >> 16), (BYTE)(value >> 24) };
    return WriteBytes(writer, data, sizeof(data));
}

static void PatchUInt32(CLEAR_WRITER *writer, UINT32 offset, UINT32 value)
{
    assert(offset + 4 <= writer->Length);
    writer->Data[offset] = (BYTE)value;
    writer->Data[offset + 1] = (BYTE)(value >> 8);
    writer->Data[offset + 2] = (BYTE)(value >> 16);
    writer->Data[offset + 3] = (BYTE)(value >> 24);
}

static BOOL WriteRunLength(CLEAR_WRITER *writer, UINT32 run)
{
    if (run < 0xFF) return WriteByte(writer, (BYTE)run);
    if (!WriteByte(writer, 0xFF)) return FALSE;
    if (run < 0xFFFF) return WriteUInt16(writer, (UINT16)run);
    return WriteUInt16(writer, 0xFFFF) && WriteUInt32(writer, run);
}

static BOOL WriteColor(CLEAR_WRITER *writer, UINT32 color)
{
    BYTE bgr[3] = { (BYTE)color, (BYTE)(color >> 8), (BYTE)(color >> 16) };
    return WriteBytes(writer, bgr, sizeof(bgr));
}

static BOOL WriteResidual(CLEAR_WRITER *writer, const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 height)
{
    UINT32 position = 0, total = width * height;
    while (position < total)
    {
        UINT32 color = Pixel(pixels, stride, width, position), run = 1;
        while (position + run < total && Pixel(pixels, stride, width, position + run) == color) ++run;
        if (!WriteColor(writer, color) || !WriteRunLength(writer, run)) return FALSE;
        position += run;
    }
    return TRUE;
}

static UINT32 IndexBits(UINT32 count)
{
    UINT32 bits = 0, largest = count - 1;
    assert(count >= 2 && count <= CLEAR_MAX_PALETTE);
    while (largest) { ++bits; largest >>= 1; }
    return bits;
}

static BOOL WriteRlex(CLEAR_WRITER *writer, const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 height)
{
    CLEAR_PALETTE palette;
    UINT32 position = 0, total = width * height, i, bits, maxDepth;
    BYTE index, next;
    if (!BuildPalette(&palette, pixels, stride, width, height, CLEAR_MAX_PALETTE)) return FALSE;
    if (palette.Count < 2) palette.Colors[palette.Count++] = palette.Colors[0] ^ 0x00FFFFFF;
    bits = IndexBits(palette.Count);
    maxDepth = (1u << (8 - bits)) - 1;
    if (!WriteByte(writer, (BYTE)palette.Count)) return FALSE;
    for (i = 0; i < palette.Count; ++i)
        if (!WriteColor(writer, palette.Colors[i])) return FALSE;
    while (position < total)
    {
        UINT32 run = 1, depth = 0;
        (void)PaletteIndex(&palette, Pixel(pixels, stride, width, position), CLEAR_MAX_PALETTE, &index);
        while (position + run < total && Pixel(pixels, stride, width, position + run) == palette.Colors[index]) ++run;
        while (depth < maxDepth && position + run + depth < total && index + depth + 1 < palette.Count)
        {
            (void)PaletteIndex(&palette, Pixel(pixels, stride, width, position + run + depth), CLEAR_MAX_PALETTE, &next);
            if (next != index + depth + 1) break;
            ++depth;
        }
        if (!WriteByte(writer, (BYTE)((index + depth) | (depth << bits))) || !WriteRunLength(writer, run - 1)) return FALSE;
        position += run + depth;
    }
    return TRUE;
}

BOOL ClearEncode(BYTE sequence, CLEAR_LAYER layer, const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 height,
                 BYTE *output, UINT32 capacity, UINT32 *size)
{
    CLEAR_WRITER writer = { output, capacity, 0 };
    UINT32 counts, start, subcodec;
    assert(width && height && width <= 0xFFFF && height <= 0xFFFF);
    if (!WriteByte(&writer, 0) || !WriteByte(&writer, sequence)) return FALSE;
    counts = writer.Length;
    if (!WriteUInt32(&writer, 0) || !WriteUInt32(&writer, 0) || !WriteUInt32(&writer, 0)) return FALSE;
    start = writer.Length;
    if (layer == ClearResidual)
    {
        if (!WriteResidual(&writer, pixels, stride, width, height)) return FALSE;
        PatchUInt32(&writer, counts, writer.Length - start);
    }
    else
    {
        if (!WriteUInt16(&writer, 0) || !WriteUInt16(&writer, 0) ||
            !WriteUInt16(&writer, (UINT16)width) || !WriteUInt16(&writer, (UINT16)height) ||
            !WriteUInt32(&writer, 0) || !WriteByte(&writer, 2)) return FALSE;
        subcodec = writer.Length;
        if (!WriteRlex(&writer, pixels, stride, width, height)) return FALSE;
        if (writer.Length - subcodec > 3 * width * height) return FALSE;
        PatchUInt32(&writer, subcodec - 5, writer.Length - subcodec);
        PatchUInt32(&writer, counts + 8, writer.Length - start);
    }
    *size = writer.Length;
    return TRUE;
}
