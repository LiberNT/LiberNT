/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SSE2 string and wide string routines for AMD64
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <string.h>
#include <wchar.h>
#include <intrin.h>
#include <emmintrin.h>

#define SSE_BLOCK(Pointer) ((const __m128i *)((size_t)(Pointer) & ~(size_t)15))
#define SSE_SHIFT(Pointer) ((unsigned int)((size_t)(Pointer) & 15))
#define SSE_PAGE_SAFE(Pointer) (((size_t)(Pointer) & 4095) <= 4096 - 16)

static __inline unsigned int
SseFirst(unsigned int Mask)
{
    unsigned long Index;

    _BitScanForward(&Index, Mask);
    return (unsigned int)Index;
}

static __inline unsigned int
SseLast(unsigned int Mask)
{
    unsigned long Index;

    _BitScanReverse(&Index, Mask);
    return (unsigned int)Index;
}

static __inline unsigned int
SseMask8(__m128i Data, __m128i Value)
{
    return (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(Data, Value));
}

static __inline unsigned int
SseMask16(__m128i Data, __m128i Value)
{
    return (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi16(Data, Value));
}

size_t __cdecl
strlen(const char *String)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i *Block = SSE_BLOCK(String);
    unsigned int Mask = SseMask8(_mm_load_si128(Block), Zero) >> SSE_SHIFT(String);

    if (Mask)
        return SseFirst(Mask);

    for (;;)
    {
        Mask = SseMask8(_mm_load_si128(++Block), Zero);
        if (Mask)
            return (size_t)((const char *)Block - String) + SseFirst(Mask);
    }
}

size_t __cdecl
strnlen(const char *String, size_t Count)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i *Block;
    unsigned int Mask;
    size_t Length;

    if (!String || !Count)
        return 0;

    Block = SSE_BLOCK(String);
    Mask = SseMask8(_mm_load_si128(Block), Zero) >> SSE_SHIFT(String);
    if (Mask)
    {
        Length = SseFirst(Mask);
        return Length < Count ? Length : Count;
    }

    for (;;)
    {
        Length = (size_t)((const char *)++Block - String);
        if (Length >= Count)
            return Count;
        Mask = SseMask8(_mm_load_si128(Block), Zero);
        if (Mask)
        {
            Length += SseFirst(Mask);
            return Length < Count ? Length : Count;
        }
    }
}

char * __cdecl
strchr(const char *String, int Character)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i Needle = _mm_set1_epi8((char)Character);
    const __m128i *Block = SSE_BLOCK(String);
    const char *Base = String;
    __m128i Data = _mm_load_si128(Block);
    unsigned int Match = SseMask8(Data, Needle) >> SSE_SHIFT(String);
    unsigned int Stop = (SseMask8(Data, Zero) >> SSE_SHIFT(String)) | Match;

    while (!Stop)
    {
        Base = (const char *)++Block;
        Data = _mm_load_si128(Block);
        Match = SseMask8(Data, Needle);
        Stop = SseMask8(Data, Zero) | Match;
    }

    Stop = SseFirst(Stop);
    return ((Match >> Stop) & 1) ? (char *)Base + Stop : NULL;
}

char * __cdecl
strrchr(const char *String, int Character)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i Needle = _mm_set1_epi8((char)Character);
    const __m128i *Block = SSE_BLOCK(String);
    const char *Base = String;
    const char *Last = NULL;
    __m128i Data = _mm_load_si128(Block);
    unsigned int Match = SseMask8(Data, Needle) >> SSE_SHIFT(String);
    unsigned int Zeros = SseMask8(Data, Zero) >> SSE_SHIFT(String);

    for (;;)
    {
        if (Zeros)
        {
            Match &= Zeros ^ (Zeros - 1);
            if (Match)
                Last = Base + SseLast(Match);
            return (char *)Last;
        }
        if (Match)
            Last = Base + SseLast(Match);

        Base = (const char *)++Block;
        Data = _mm_load_si128(Block);
        Match = SseMask8(Data, Needle);
        Zeros = SseMask8(Data, Zero);
    }
}

size_t __cdecl
wcslen(const wchar_t *String)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i *Block;
    unsigned int Mask;

    if ((size_t)String & 1)
    {
        const wchar_t *Position = String;

        while (*Position)
            Position++;
        return (size_t)(Position - String);
    }

    Block = SSE_BLOCK(String);
    Mask = SseMask16(_mm_load_si128(Block), Zero) >> SSE_SHIFT(String);
    if (Mask)
        return SseFirst(Mask) / sizeof(wchar_t);

    for (;;)
    {
        Mask = SseMask16(_mm_load_si128(++Block), Zero);
        if (Mask)
            return ((size_t)((const char *)Block - (const char *)String) + SseFirst(Mask)) / sizeof(wchar_t);
    }
}

size_t __cdecl
wcsnlen(const wchar_t *String, size_t Count)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i *Block;
    unsigned int Mask;
    size_t Length;

    if (!String || !Count)
        return 0;

    if ((size_t)String & 1)
    {
        for (Length = 0; Length < Count && String[Length]; Length++)
            ;
        return Length;
    }

    Block = SSE_BLOCK(String);
    Mask = SseMask16(_mm_load_si128(Block), Zero) >> SSE_SHIFT(String);
    if (Mask)
    {
        Length = SseFirst(Mask) / sizeof(wchar_t);
        return Length < Count ? Length : Count;
    }

    for (;;)
    {
        Length = (size_t)((const char *)++Block - (const char *)String) / sizeof(wchar_t);
        if (Length >= Count)
            return Count;
        Mask = SseMask16(_mm_load_si128(Block), Zero);
        if (Mask)
        {
            Length += SseFirst(Mask) / sizeof(wchar_t);
            return Length < Count ? Length : Count;
        }
    }
}

wchar_t * __cdecl
wcschr(const wchar_t *String, wchar_t Character)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i Needle = _mm_set1_epi16((short)Character);
    const __m128i *Block;
    const char *Base = (const char *)String;
    __m128i Data;
    unsigned int Match;
    unsigned int Stop;

    if ((size_t)String & 1)
    {
        while (*String)
        {
            if (*String == Character)
                return (wchar_t *)String;
            String++;
        }
        return Character ? NULL : (wchar_t *)String;
    }

    Block = SSE_BLOCK(String);
    Data = _mm_load_si128(Block);
    Match = SseMask16(Data, Needle) >> SSE_SHIFT(String);
    Stop = (SseMask16(Data, Zero) >> SSE_SHIFT(String)) | Match;

    while (!Stop)
    {
        Base = (const char *)++Block;
        Data = _mm_load_si128(Block);
        Match = SseMask16(Data, Needle);
        Stop = SseMask16(Data, Zero) | Match;
    }

    Stop = SseFirst(Stop);
    return ((Match >> Stop) & 1) ? (wchar_t *)(Base + Stop) : NULL;
}

wchar_t * __cdecl
wcsrchr(const wchar_t *String, wchar_t Character)
{
    const __m128i Zero = _mm_setzero_si128();
    const __m128i Needle = _mm_set1_epi16((short)Character);
    const __m128i *Block;
    const char *Base = (const char *)String;
    const char *Last = NULL;
    __m128i Data;
    unsigned int Match;
    unsigned int Zeros;

    if ((size_t)String & 1)
    {
        const wchar_t *Found = NULL;

        while (*String)
        {
            if (*String == Character)
                Found = String;
            String++;
        }
        return Character ? (wchar_t *)Found : (wchar_t *)String;
    }

    Block = SSE_BLOCK(String);
    Data = _mm_load_si128(Block);
    Match = SseMask16(Data, Needle) >> SSE_SHIFT(String);
    Zeros = SseMask16(Data, Zero) >> SSE_SHIFT(String);

    for (;;)
    {
        if (Zeros)
        {
            Match &= Zeros ^ (Zeros - 1);
            if (Match)
                Last = Base + (SseLast(Match) & ~1u);
            return (wchar_t *)Last;
        }
        if (Match)
            Last = Base + (SseLast(Match) & ~1u);

        Base = (const char *)++Block;
        Data = _mm_load_si128(Block);
        Match = SseMask16(Data, Needle);
        Zeros = SseMask16(Data, Zero);
    }
}

int __cdecl
wcscmp(const wchar_t *First, const wchar_t *Second)
{
    const __m128i Zero = _mm_setzero_si128();

    for (;;)
    {
        if (SSE_PAGE_SAFE(First) && SSE_PAGE_SAFE(Second))
        {
            __m128i Left = _mm_loadu_si128((const __m128i *)First);
            __m128i Right = _mm_loadu_si128((const __m128i *)Second);
            unsigned int Stop = ~(unsigned int)_mm_movemask_epi8(
                _mm_andnot_si128(_mm_cmpeq_epi16(Left, Zero), _mm_cmpeq_epi16(Left, Right))) & 0xffff;

            if (Stop)
            {
                Stop = SseFirst(Stop) / sizeof(wchar_t);
                return First[Stop] - Second[Stop];
            }
            First += 16 / sizeof(wchar_t);
            Second += 16 / sizeof(wchar_t);
        }
        else
        {
            if (*First != *Second)
                return *First - *Second;
            if (!*First)
                return 0;
            First++;
            Second++;
        }
    }
}

int __cdecl
wcsncmp(const wchar_t *First, const wchar_t *Second, size_t Count)
{
    const __m128i Zero = _mm_setzero_si128();

    if (!Count)
        return 0;

    for (;;)
    {
        if (Count >= 16 / sizeof(wchar_t) && SSE_PAGE_SAFE(First) && SSE_PAGE_SAFE(Second))
        {
            __m128i Left = _mm_loadu_si128((const __m128i *)First);
            __m128i Right = _mm_loadu_si128((const __m128i *)Second);
            unsigned int Stop = ~(unsigned int)_mm_movemask_epi8(
                _mm_andnot_si128(_mm_cmpeq_epi16(Left, Zero), _mm_cmpeq_epi16(Left, Right))) & 0xffff;

            if (Stop)
            {
                Stop = SseFirst(Stop) / sizeof(wchar_t);
                return First[Stop] - Second[Stop];
            }
            First += 16 / sizeof(wchar_t);
            Second += 16 / sizeof(wchar_t);
            Count -= 16 / sizeof(wchar_t);
            if (!Count)
                return 0;
        }
        else
        {
            if (*First != *Second)
                return *First - *Second;
            if (!*First)
                return 0;
            First++;
            Second++;
            if (!--Count)
                return 0;
        }
    }
}
