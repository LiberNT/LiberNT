/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 string and memory routines for the RVA20, RVA22 and RVA23 profiles
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RISCV64_RVSTRING_H
#define _RISCV64_RVSTRING_H

#include <stddef.h>

typedef unsigned long long RV_WORD;

#define RV_ONES  0x0101010101010101ULL
#define RV_HIGHS 0x8080808080808080ULL
#define RV_HAS_ZERO(Value) ((((Value) - RV_ONES) & ~(Value) & RV_HIGHS) != 0)
#define RV_MISALIGNMENT(Pointer) ((size_t)(Pointer) & (sizeof(RV_WORD) - 1))
#define RV_WIDE_ONES  0x0001000100010001ULL
#define RV_WIDE_HIGHS 0x8000800080008000ULL
#define RV_HAS_ZERO_WIDE(Value) ((((Value) - RV_WIDE_ONES) & ~(Value) & RV_WIDE_HIGHS) != 0)
#define RV_WORD_WCHARS (sizeof(RV_WORD) / sizeof(wchar_t))

static __inline int
RvCompareBytes(unsigned int Left, unsigned int Right)
{
    return (Left > Right) - (Left < Right);
}

static __inline size_t
RvStrlenRva20(const char *String)
{
    const char *Position = String;
    const RV_WORD *Word;

    while (RV_MISALIGNMENT(Position))
    {
        if (!*Position)
            return (size_t)(Position - String);
        Position++;
    }
    Word = (const RV_WORD *)Position;
    while (!RV_HAS_ZERO(*Word))
        Word++;
    Position = (const char *)Word;
    while (*Position)
        Position++;
    return (size_t)(Position - String);
}

static __inline int
RvStrcmpRva20(const char *First, const char *Second)
{
    const unsigned char *Left = (const unsigned char *)First;
    const unsigned char *Right = (const unsigned char *)Second;

    if (RV_MISALIGNMENT(Left) == RV_MISALIGNMENT(Right))
    {
        const RV_WORD *LeftWord;
        const RV_WORD *RightWord;

        while (RV_MISALIGNMENT(Left))
        {
            if (!*Left || *Left != *Right)
                return RvCompareBytes(*Left, *Right);
            Left++;
            Right++;
        }
        LeftWord = (const RV_WORD *)Left;
        RightWord = (const RV_WORD *)Right;
        while (*LeftWord == *RightWord && !RV_HAS_ZERO(*LeftWord))
        {
            LeftWord++;
            RightWord++;
        }
        Left = (const unsigned char *)LeftWord;
        Right = (const unsigned char *)RightWord;
    }
    while (*Left && *Left == *Right)
    {
        Left++;
        Right++;
    }
    return RvCompareBytes(*Left, *Right);
}

static __inline char *
RvStrcpyRva20(char *Destination, const char *Source)
{
    char *Result = Destination;

    if (RV_MISALIGNMENT(Destination) == RV_MISALIGNMENT(Source))
    {
        RV_WORD *DestinationWord;
        const RV_WORD *SourceWord;

        while (RV_MISALIGNMENT(Source))
        {
            if (!(*Destination++ = *Source++))
                return Result;
        }
        DestinationWord = (RV_WORD *)Destination;
        SourceWord = (const RV_WORD *)Source;
        while (!RV_HAS_ZERO(*SourceWord))
            *DestinationWord++ = *SourceWord++;
        Destination = (char *)DestinationWord;
        Source = (const char *)SourceWord;
    }
    while ((*Destination++ = *Source++))
        ;
    return Result;
}

static __inline void *
RvMemcpyRva20(void *Destination, const void *Source, size_t Length)
{
    unsigned char *Target = (unsigned char *)Destination;
    const unsigned char *From = (const unsigned char *)Source;

    if (Length >= 2 * sizeof(RV_WORD))
    {
        RV_WORD *TargetWord;

        while (RV_MISALIGNMENT(Target))
        {
            *Target++ = *From++;
            Length--;
        }
        TargetWord = (RV_WORD *)Target;
        if (!RV_MISALIGNMENT(From))
        {
            const RV_WORD *FromWord = (const RV_WORD *)From;

            while (Length >= 4 * sizeof(RV_WORD))
            {
                RV_WORD Word0 = FromWord[0], Word1 = FromWord[1];
                RV_WORD Word2 = FromWord[2], Word3 = FromWord[3];

                TargetWord[0] = Word0;
                TargetWord[1] = Word1;
                TargetWord[2] = Word2;
                TargetWord[3] = Word3;
                TargetWord += 4;
                FromWord += 4;
                Length -= 4 * sizeof(RV_WORD);
            }
            while (Length >= sizeof(RV_WORD))
            {
                *TargetWord++ = *FromWord++;
                Length -= sizeof(RV_WORD);
            }
            From = (const unsigned char *)FromWord;
        }
        else
        {
            unsigned int Low = (unsigned int)RV_MISALIGNMENT(From) * 8;
            unsigned int High = 64 - Low;
            const RV_WORD *FromWord = (const RV_WORD *)(From - RV_MISALIGNMENT(From));
            RV_WORD Previous = *FromWord++;

            while (Length >= sizeof(RV_WORD))
            {
                RV_WORD Next = *FromWord++;

                *TargetWord++ = (Previous >> Low) | (Next << High);
                Previous = Next;
                From += sizeof(RV_WORD);
                Length -= sizeof(RV_WORD);
            }
        }
        Target = (unsigned char *)TargetWord;
    }
    while (Length--)
        *Target++ = *From++;
    return Destination;
}

static __inline void *
RvMemmoveRva20(void *Destination, const void *Source, size_t Length)
{
    unsigned char *Target = (unsigned char *)Destination;
    const unsigned char *From = (const unsigned char *)Source;

    if (Target == From || Length == 0)
        return Destination;
    if (Target < From || Target >= From + Length)
        return RvMemcpyRva20(Destination, Source, Length);

    Target += Length;
    From += Length;
    if (Length >= 2 * sizeof(RV_WORD) && RV_MISALIGNMENT(Target) == RV_MISALIGNMENT(From))
    {
        RV_WORD *TargetWord;
        const RV_WORD *FromWord;

        while (RV_MISALIGNMENT(Target))
        {
            *--Target = *--From;
            Length--;
        }
        TargetWord = (RV_WORD *)Target;
        FromWord = (const RV_WORD *)From;
        while (Length >= sizeof(RV_WORD))
        {
            *--TargetWord = *--FromWord;
            Length -= sizeof(RV_WORD);
        }
        Target = (unsigned char *)TargetWord;
        From = (const unsigned char *)FromWord;
    }
    while (Length--)
        *--Target = *--From;
    return Destination;
}

static __inline void *
RvMemsetRva20(void *Destination, int Value, size_t Length)
{
    unsigned char *Target = (unsigned char *)Destination;
    unsigned char Byte = (unsigned char)Value;

    if (Length >= 2 * sizeof(RV_WORD))
    {
        RV_WORD Pattern = Byte * RV_ONES;
        RV_WORD *TargetWord;

        while (RV_MISALIGNMENT(Target))
        {
            *Target++ = Byte;
            Length--;
        }
        TargetWord = (RV_WORD *)Target;
        while (Length >= 4 * sizeof(RV_WORD))
        {
            TargetWord[0] = Pattern;
            TargetWord[1] = Pattern;
            TargetWord[2] = Pattern;
            TargetWord[3] = Pattern;
            TargetWord += 4;
            Length -= 4 * sizeof(RV_WORD);
        }
        while (Length >= sizeof(RV_WORD))
        {
            *TargetWord++ = Pattern;
            Length -= sizeof(RV_WORD);
        }
        Target = (unsigned char *)TargetWord;
    }
    while (Length--)
        *Target++ = Byte;
    return Destination;
}

static __inline size_t
RvStrnlenRva20(const char *String, size_t Count)
{
    const char *Position = String;
    const RV_WORD *Word;

    if (!String)
        return 0;
    while (Count && RV_MISALIGNMENT(Position))
    {
        if (!*Position)
            return (size_t)(Position - String);
        Position++;
        Count--;
    }
    Word = (const RV_WORD *)Position;
    while (Count >= sizeof(RV_WORD) && !RV_HAS_ZERO(*Word))
    {
        Word++;
        Count -= sizeof(RV_WORD);
    }
    Position = (const char *)Word;
    while (Count && *Position)
    {
        Position++;
        Count--;
    }
    return (size_t)(Position - String);
}

static __inline char *
RvStrchrRva20(const char *String, int Character)
{
    const char Needle = (char)Character;
    const RV_WORD Pattern = (unsigned char)Needle * RV_ONES;
    const RV_WORD *Word;

    while (RV_MISALIGNMENT(String))
    {
        if (*String == Needle)
            return (char *)String;
        if (!*String)
            return NULL;
        String++;
    }
    Word = (const RV_WORD *)String;
    while (!RV_HAS_ZERO(*Word) && !RV_HAS_ZERO(*Word ^ Pattern))
        Word++;
    for (String = (const char *)Word; *String != Needle; String++)
    {
        if (!*String)
            return NULL;
    }
    return (char *)String;
}

static __inline char *
RvStrrchrRva20(const char *String, int Character)
{
    const char Needle = (char)Character;
    const RV_WORD Pattern = (unsigned char)Needle * RV_ONES;
    const char *Last = NULL;
    const RV_WORD *Word;

    while (RV_MISALIGNMENT(String))
    {
        if (*String == Needle)
            Last = String;
        if (!*String)
            return (char *)Last;
        String++;
    }
    for (Word = (const RV_WORD *)String; !RV_HAS_ZERO(*Word); Word++)
    {
        if (RV_HAS_ZERO(*Word ^ Pattern))
        {
            const char *Byte = (const char *)Word;
            size_t Index;

            for (Index = 0; Index < sizeof(RV_WORD); Index++)
            {
                if (Byte[Index] == Needle)
                    Last = &Byte[Index];
            }
        }
    }
    for (String = (const char *)Word; ; String++)
    {
        if (*String == Needle)
            Last = String;
        if (!*String)
            return (char *)Last;
    }
}

static __inline size_t
RvWcslenRva20(const wchar_t *String)
{
    const wchar_t *Position = String;
    const RV_WORD *Word;

    while (RV_MISALIGNMENT(Position))
    {
        if (!*Position)
            return (size_t)(Position - String);
        Position++;
    }
    Word = (const RV_WORD *)Position;
    while (!RV_HAS_ZERO_WIDE(*Word))
        Word++;
    Position = (const wchar_t *)Word;
    while (*Position)
        Position++;
    return (size_t)(Position - String);
}

static __inline size_t
RvWcsnlenRva20(const wchar_t *String, size_t Count)
{
    const wchar_t *Position = String;
    const RV_WORD *Word;

    if (!String)
        return 0;
    while (Count && RV_MISALIGNMENT(Position))
    {
        if (!*Position)
            return (size_t)(Position - String);
        Position++;
        Count--;
    }
    Word = (const RV_WORD *)Position;
    while (Count >= RV_WORD_WCHARS && !RV_HAS_ZERO_WIDE(*Word))
    {
        Word++;
        Count -= RV_WORD_WCHARS;
    }
    Position = (const wchar_t *)Word;
    while (Count && *Position)
    {
        Position++;
        Count--;
    }
    return (size_t)(Position - String);
}

static __inline wchar_t *
RvWcschrRva20(const wchar_t *String, wchar_t Character)
{
    const RV_WORD Pattern = (unsigned short)Character * RV_WIDE_ONES;
    const RV_WORD *Word;

    while (RV_MISALIGNMENT(String))
    {
        if (*String == Character)
            return (wchar_t *)String;
        if (!*String)
            return NULL;
        String++;
    }
    Word = (const RV_WORD *)String;
    while (!RV_HAS_ZERO_WIDE(*Word) && !RV_HAS_ZERO_WIDE(*Word ^ Pattern))
        Word++;
    for (String = (const wchar_t *)Word; *String != Character; String++)
    {
        if (!*String)
            return NULL;
    }
    return (wchar_t *)String;
}

static __inline wchar_t *
RvWcsrchrRva20(const wchar_t *String, wchar_t Character)
{
    const RV_WORD Pattern = (unsigned short)Character * RV_WIDE_ONES;
    const wchar_t *Last = NULL;
    const RV_WORD *Word;

    while (RV_MISALIGNMENT(String))
    {
        if (*String == Character)
            Last = String;
        if (!*String)
            return (wchar_t *)Last;
        String++;
    }
    for (Word = (const RV_WORD *)String; !RV_HAS_ZERO_WIDE(*Word); Word++)
    {
        if (RV_HAS_ZERO_WIDE(*Word ^ Pattern))
        {
            const wchar_t *Char = (const wchar_t *)Word;
            size_t Index;

            for (Index = 0; Index < RV_WORD_WCHARS; Index++)
            {
                if (Char[Index] == Character)
                    Last = &Char[Index];
            }
        }
    }
    for (String = (const wchar_t *)Word; ; String++)
    {
        if (*String == Character)
            Last = String;
        if (!*String)
            return (wchar_t *)Last;
    }
}

static __inline int
RvWcscmpRva20(const wchar_t *First, const wchar_t *Second)
{
    if (RV_MISALIGNMENT(First) == RV_MISALIGNMENT(Second))
    {
        const RV_WORD *FirstWord;
        const RV_WORD *SecondWord;

        while (RV_MISALIGNMENT(First))
        {
            if (*First != *Second || !*First)
                return *First - *Second;
            First++;
            Second++;
        }
        FirstWord = (const RV_WORD *)First;
        SecondWord = (const RV_WORD *)Second;
        while (*FirstWord == *SecondWord && !RV_HAS_ZERO_WIDE(*FirstWord))
        {
            FirstWord++;
            SecondWord++;
        }
        First = (const wchar_t *)FirstWord;
        Second = (const wchar_t *)SecondWord;
    }
    while (*First && *First == *Second)
    {
        First++;
        Second++;
    }
    return *First - *Second;
}

static __inline int
RvWcsncmpRva20(const wchar_t *First, const wchar_t *Second, size_t Count)
{
    if (!Count)
        return 0;
    if (RV_MISALIGNMENT(First) == RV_MISALIGNMENT(Second))
    {
        const RV_WORD *FirstWord;
        const RV_WORD *SecondWord;

        while (RV_MISALIGNMENT(First))
        {
            if (*First != *Second || !*First)
                return *First - *Second;
            First++;
            Second++;
            if (!--Count)
                return 0;
        }
        FirstWord = (const RV_WORD *)First;
        SecondWord = (const RV_WORD *)Second;
        while (Count >= RV_WORD_WCHARS && *FirstWord == *SecondWord && !RV_HAS_ZERO_WIDE(*FirstWord))
        {
            FirstWord++;
            SecondWord++;
            Count -= RV_WORD_WCHARS;
        }
        if (!Count)
            return 0;
        First = (const wchar_t *)FirstWord;
        Second = (const wchar_t *)SecondWord;
    }
    for (;;)
    {
        if (*First != *Second || !*First)
            return *First - *Second;
        First++;
        Second++;
        if (!--Count)
            return 0;
    }
}

static __inline RV_WORD
RvOrcB(RV_WORD Value)
{
    RV_WORD Result;

    __asm__(".option push\n\t.option arch, +zbb\n\torc.b %0, %1\n\t.option pop"
            : "=r"(Result) : "r"(Value));
    return Result;
}

static __inline RV_WORD
RvCtz(RV_WORD Value)
{
    RV_WORD Result;

    __asm__(".option push\n\t.option arch, +zbb\n\tctz %0, %1\n\t.option pop"
            : "=r"(Result) : "r"(Value));
    return Result;
}

static __inline size_t
RvStrlenRva22(const char *String)
{
    size_t Offset = RV_MISALIGNMENT(String);
    const RV_WORD *Word = (const RV_WORD *)(String - Offset);
    RV_WORD Value = *Word;
    RV_WORD Mask;

    if (Offset)
        Value |= ((RV_WORD)1 << (Offset * 8)) - 1;
    while ((Mask = RvOrcB(Value)) == ~(RV_WORD)0)
        Value = *++Word;
    return (size_t)((const char *)Word - String) + (size_t)(RvCtz(~Mask) >> 3);
}

static __inline int
RvStrcmpRva22(const char *First, const char *Second)
{
    const unsigned char *Left = (const unsigned char *)First;
    const unsigned char *Right = (const unsigned char *)Second;

    if (RV_MISALIGNMENT(Left) == RV_MISALIGNMENT(Right))
    {
        const RV_WORD *LeftWord;
        const RV_WORD *RightWord;

        while (RV_MISALIGNMENT(Left))
        {
            if (!*Left || *Left != *Right)
                return RvCompareBytes(*Left, *Right);
            Left++;
            Right++;
        }
        LeftWord = (const RV_WORD *)Left;
        RightWord = (const RV_WORD *)Right;
        for (;;)
        {
            RV_WORD LeftValue = *LeftWord++;
            RV_WORD RightValue = *RightWord++;
            RV_WORD Stop = (LeftValue ^ RightValue) | ~RvOrcB(LeftValue);

            if (Stop)
            {
                unsigned int Shift = (unsigned int)RvCtz(Stop) & ~7u;

                return RvCompareBytes((unsigned int)(LeftValue >> Shift) & 0xFF,
                                      (unsigned int)(RightValue >> Shift) & 0xFF);
            }
        }
    }
    while (*Left && *Left == *Right)
    {
        Left++;
        Right++;
    }
    return RvCompareBytes(*Left, *Right);
}

static __inline char *
RvStrcpyRva22(char *Destination, const char *Source)
{
    char *Result = Destination;

    if (RV_MISALIGNMENT(Destination) == RV_MISALIGNMENT(Source))
    {
        RV_WORD *DestinationWord;
        const RV_WORD *SourceWord;
        RV_WORD Value;

        while (RV_MISALIGNMENT(Source))
        {
            if (!(*Destination++ = *Source++))
                return Result;
        }
        DestinationWord = (RV_WORD *)Destination;
        SourceWord = (const RV_WORD *)Source;
        while (RvOrcB(Value = *SourceWord) == ~(RV_WORD)0)
        {
            *DestinationWord++ = Value;
            SourceWord++;
        }
        Destination = (char *)DestinationWord;
        Source = (const char *)SourceWord;
    }
    while ((*Destination++ = *Source++))
        ;
    return Result;
}

#define RV_STRLEN_HEAD_WORDS 16
#define RV_STRING_HEAD_WORDS 8

size_t RvStrlenVector(const char *String);
int RvStrcmpVector(const char *First, const char *Second);
char *RvStrcpyVector(char *Destination, const char *Source);

static __inline size_t
RvStrlenRva23(const char *String)
{
    size_t Offset = RV_MISALIGNMENT(String);
    const RV_WORD *Word = (const RV_WORD *)(String - Offset);
    RV_WORD Value = *Word;
    RV_WORD Mask;
    unsigned int Count = RV_STRLEN_HEAD_WORDS;

    if (Offset)
        Value |= ((RV_WORD)1 << (Offset * 8)) - 1;
    while ((Mask = RvOrcB(Value)) == ~(RV_WORD)0)
    {
        if (!--Count)
        {
            const char *Next = (const char *)(Word + 1);

            return (size_t)(Next - String) + RvStrlenVector(Next);
        }
        Value = *++Word;
    }
    return (size_t)((const char *)Word - String) + (size_t)(RvCtz(~Mask) >> 3);
}

static __inline int
RvStrcmpRva23(const char *First, const char *Second)
{
    const unsigned char *Left = (const unsigned char *)First;
    const unsigned char *Right = (const unsigned char *)Second;

    if (RV_MISALIGNMENT(Left) == RV_MISALIGNMENT(Right))
    {
        const RV_WORD *LeftWord;
        const RV_WORD *RightWord;
        unsigned int Count = RV_STRING_HEAD_WORDS;

        while (RV_MISALIGNMENT(Left))
        {
            if (!*Left || *Left != *Right)
                return RvCompareBytes(*Left, *Right);
            Left++;
            Right++;
        }
        LeftWord = (const RV_WORD *)Left;
        RightWord = (const RV_WORD *)Right;
        do
        {
            RV_WORD LeftValue = *LeftWord++;
            RV_WORD RightValue = *RightWord++;
            RV_WORD Stop = (LeftValue ^ RightValue) | ~RvOrcB(LeftValue);

            if (Stop)
            {
                unsigned int Shift = (unsigned int)RvCtz(Stop) & ~7u;

                return RvCompareBytes((unsigned int)(LeftValue >> Shift) & 0xFF,
                                      (unsigned int)(RightValue >> Shift) & 0xFF);
            }
        } while (--Count);
        Left = (const unsigned char *)LeftWord;
        Right = (const unsigned char *)RightWord;
    }
    return RvStrcmpVector((const char *)Left, (const char *)Right);
}

static __inline char *
RvStrcpyRva23(char *Destination, const char *Source)
{
    char *Result = Destination;

    if (RV_MISALIGNMENT(Destination) == RV_MISALIGNMENT(Source))
    {
        RV_WORD *DestinationWord;
        const RV_WORD *SourceWord;
        RV_WORD Value;
        unsigned int Count = RV_STRING_HEAD_WORDS;

        while (RV_MISALIGNMENT(Source))
        {
            if (!(*Destination++ = *Source++))
                return Result;
        }
        DestinationWord = (RV_WORD *)Destination;
        SourceWord = (const RV_WORD *)Source;
        while (RvOrcB(Value = *SourceWord) == ~(RV_WORD)0)
        {
            *DestinationWord++ = Value;
            SourceWord++;
            if (!--Count)
            {
                RvStrcpyVector((char *)DestinationWord, (const char *)SourceWord);
                return Result;
            }
        }
        Destination = (char *)DestinationWord;
        Source = (const char *)SourceWord;
        while ((*Destination++ = *Source++))
            ;
        return Result;
    }
    RvStrcpyVector(Destination, Source);
    return Result;
}

static __inline int
RvMemcmpWordRva20(RV_WORD Left, RV_WORD Right)
{
    unsigned int Shift = 0;

    while (!(((Left ^ Right) >> Shift) & 0xFF))
        Shift += 8;
    return (int)((Left >> Shift) & 0xFF) - (int)((Right >> Shift) & 0xFF);
}

static __inline int
RvMemcmpWordRva22(RV_WORD Left, RV_WORD Right)
{
    unsigned int Shift = (unsigned int)RvCtz(Left ^ Right) & ~7u;

    return (int)((Left >> Shift) & 0xFF) - (int)((Right >> Shift) & 0xFF);
}

static __inline int
RvMemcmpWord(RV_WORD Left, RV_WORD Right, int Zbb)
{
    return Zbb ? RvMemcmpWordRva22(Left, Right) : RvMemcmpWordRva20(Left, Right);
}

static __inline int
RvMemcmpScalar(const void *First, const void *Second, size_t Length, int Zbb)
{
    const unsigned char *Left = (const unsigned char *)First;
    const unsigned char *Right = (const unsigned char *)Second;
    RV_WORD LeftValue, RightValue, Mask;

    if (Length < sizeof(RV_WORD))
    {
        while (Length--)
        {
            if (*Left != *Right)
                return (int)*Left - (int)*Right;
            Left++;
            Right++;
        }
        return 0;
    }

    if (RV_MISALIGNMENT(Left) == RV_MISALIGNMENT(Right))
    {
        size_t Head = RV_MISALIGNMENT(Left);
        size_t End = Head + Length;
        const RV_WORD *LeftWord = (const RV_WORD *)(Left - Head);
        const RV_WORD *RightWord = (const RV_WORD *)(Right - Head);
        const RV_WORD *LastWord = LeftWord + (End - 1) / sizeof(RV_WORD);

        Mask = ~(RV_WORD)0 << (Head * 8);
        while (LeftWord != LastWord)
        {
            LeftValue = *LeftWord++ & Mask;
            RightValue = *RightWord++ & Mask;
            if (LeftValue != RightValue)
                return RvMemcmpWord(LeftValue, RightValue, Zbb);
            Mask = ~(RV_WORD)0;
        }
        if (End & (sizeof(RV_WORD) - 1))
            Mask &= ((RV_WORD)1 << ((End & (sizeof(RV_WORD) - 1)) * 8)) - 1;
        LeftValue = *LeftWord & Mask;
        RightValue = *RightWord & Mask;
        return (LeftValue != RightValue) ? RvMemcmpWord(LeftValue, RightValue, Zbb) : 0;
    }

    while (RV_MISALIGNMENT(Left))
    {
        if (*Left != *Right)
            return (int)*Left - (int)*Right;
        Left++;
        Right++;
        Length--;
    }
    {
        unsigned int Offset = (unsigned int)RV_MISALIGNMENT(Right);
        unsigned int Low = Offset * 8;
        unsigned int High = 64 - Low;
        const RV_WORD *LeftWord = (const RV_WORD *)Left;
        const RV_WORD *RightWord = (const RV_WORD *)(Right - Offset);
        RV_WORD Previous = *RightWord++;

        while (Length >= sizeof(RV_WORD))
        {
            RV_WORD Next = *RightWord++;

            LeftValue = *LeftWord++;
            RightValue = (Previous >> Low) | (Next << High);
            if (LeftValue != RightValue)
                return RvMemcmpWord(LeftValue, RightValue, Zbb);
            Previous = Next;
            Length -= sizeof(RV_WORD);
        }
        if (Length == 0)
            return 0;
        RightValue = Previous >> Low;
        if (Length > sizeof(RV_WORD) - Offset)
            RightValue |= *RightWord << High;
        Mask = ((RV_WORD)1 << (Length * 8)) - 1;
        LeftValue = *LeftWord & Mask;
        RightValue &= Mask;
        return (LeftValue != RightValue) ? RvMemcmpWord(LeftValue, RightValue, Zbb) : 0;
    }
}

static __inline int
RvMemcmpRva20(const void *First, const void *Second, size_t Length)
{
    return RvMemcmpScalar(First, Second, Length, 0);
}

static __inline int
RvMemcmpRva22(const void *First, const void *Second, size_t Length)
{
    return RvMemcmpScalar(First, Second, Length, 1);
}

#endif
