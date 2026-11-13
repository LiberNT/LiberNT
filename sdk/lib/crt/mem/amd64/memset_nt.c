/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 memset of fills larger than half the last-level cache with non-temporal stores
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <intrin.h>
#include <emmintrin.h>
#include <stddef.h>

size_t MemsetNtThreshold;

size_t MemNtComputeThreshold(void);

void *
__cdecl
MemsetNonTemporal(
    void *Destination,
    int Value,
    size_t Length)
{
    unsigned char *To = Destination;
    size_t Threshold = MemsetNtThreshold, Head;
    __m128i Fill;

    if (Threshold == 0)
    {
        Threshold = MemNtComputeThreshold();
        MemsetNtThreshold = Threshold;
    }
    if (Length < Threshold)
    {
        __stosb(To, (unsigned char)Value, Length);
        return Destination;
    }

    Head = (16 - ((size_t)To & 15)) & 15;
    __stosb(To, (unsigned char)Value, Head);
    To += Head;
    Length -= Head;

    Fill = _mm_set1_epi8((char)Value);
    for (; Length >= 64; Length -= 64, To += 64)
    {
        _mm_stream_si128((__m128i *)To, Fill);
        _mm_stream_si128((__m128i *)(To + 16), Fill);
        _mm_stream_si128((__m128i *)(To + 32), Fill);
        _mm_stream_si128((__m128i *)(To + 48), Fill);
    }
    _mm_sfence();
    __stosb(To, (unsigned char)Value, Length);
    return Destination;
}
