/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 memmove of copies larger than half the last-level cache with non-temporal stores
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <intrin.h>
#include <emmintrin.h>
#include <stddef.h>

size_t MemmoveNtThreshold;

static
size_t
MemmoveCacheSize(
    unsigned Leaf)
{
    size_t Largest = 0;
    unsigned Subleaf;
    int Regs[4];

    for (Subleaf = 0; Subleaf < 16; Subleaf++)
    {
        size_t Size;

        __cpuidex(Regs, (int)Leaf, (int)Subleaf);
        if ((Regs[0] & 0x1F) == 0)
            break;
        Size = (((unsigned)Regs[1] >> 22) + 1) * ((((unsigned)Regs[1] >> 12) & 0x3FF) + 1) *
               (((unsigned)Regs[1] & 0xFFF) + 1) * ((size_t)(unsigned)Regs[2] + 1);
        if (Size > Largest)
            Largest = Size;
    }
    return Largest;
}

static
size_t
MemmoveComputeThreshold(void)
{
    size_t Size = 0;
    int Regs[4];

    __cpuid(Regs, 0);
    if ((unsigned)Regs[0] >= 4)
        Size = MemmoveCacheSize(4);
    if (Size == 0)
    {
        __cpuid(Regs, 0x80000000);
        if ((unsigned)Regs[0] >= 0x8000001D)
        {
            __cpuid(Regs, 0x80000001);
            if (Regs[2] & (1 << 22))
                Size = MemmoveCacheSize(0x8000001D);
        }
    }
    return (Size != 0) ? Size / 2 : (size_t)-1;
}

void *
__cdecl
MemmoveNonTemporal(
    void *Destination,
    const void *Source,
    size_t Length)
{
    unsigned char *To = Destination;
    const unsigned char *From = Source;
    size_t Threshold = MemmoveNtThreshold, Head;

    if (Threshold == 0)
    {
        Threshold = MemmoveComputeThreshold();
        MemmoveNtThreshold = Threshold;
    }
    if (Length < Threshold)
    {
        __movsb(To, From, Length);
        return Destination;
    }

    Head = (16 - ((size_t)To & 15)) & 15;
    __movsb(To, From, Head);
    To += Head;
    From += Head;
    Length -= Head;
    for (; Length >= 64; Length -= 64, To += 64, From += 64)
    {
        __m128i A = _mm_loadu_si128((const __m128i *)From);
        __m128i B = _mm_loadu_si128((const __m128i *)(From + 16));
        __m128i C = _mm_loadu_si128((const __m128i *)(From + 32));
        __m128i D = _mm_loadu_si128((const __m128i *)(From + 48));

        _mm_stream_si128((__m128i *)To, A);
        _mm_stream_si128((__m128i *)(To + 16), B);
        _mm_stream_si128((__m128i *)(To + 32), C);
        _mm_stream_si128((__m128i *)(To + 48), D);
    }
    _mm_sfence();
    __movsb(To, From, Length);
    return Destination;
}
