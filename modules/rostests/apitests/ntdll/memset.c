/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the ntdll and msvcrt memset exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef void *(__cdecl *PMEMSET)(void *, int, size_t);

static const SIZE_T Lengths[] =
{
    0, 1, 2, 3, 6, 7, 8, 14, 15, 16, 17, 30, 31, 32, 33, 62, 63, 64, 65, 126, 127, 128, 129,
    191, 255, 256, 1000, 2047, 2048, 2049, 4095, 4096, 4097, 6000
};

static VOID
TestFill(const char *Name, PMEMSET Function)
{
    static UCHAR Buffer[6000 + 96];
    ULONG Offset, LengthIndex, DataFailures = 0, ReturnFailures = 0;

    if (Function == NULL)
    {
        skip("%s is not exported\n", Name);
        return;
    }

    for (Offset = 0; Offset < 32; ++Offset)
    {
        for (LengthIndex = 0; LengthIndex < _countof(Lengths); ++LengthIndex)
        {
            SIZE_T Length = Lengths[LengthIndex], Index;
            UCHAR Value = (UCHAR)(0x80 | (Offset * 7 + LengthIndex));
            void *Result;

            for (Index = 0; Index < sizeof(Buffer); ++Index)
                Buffer[Index] = 0x5A;
            Result = Function(&Buffer[Offset], 0x100 | Value, Length);
            if (Result != &Buffer[Offset])
                ++ReturnFailures;
            for (Index = 0; Index < sizeof(Buffer); ++Index)
            {
                UCHAR Want = (Index >= Offset && Index < Offset + Length) ? Value : 0x5A;

                if (Buffer[Index] != Want)
                {
                    ++DataFailures;
                    if (DataFailures <= 4)
                    {
                        trace("%s mismatch: offset=%lu length=%Iu index=%Iu expected=%02x actual=%02x\n", Name, Offset,
                              (size_t)Length, (size_t)Index, Want, Buffer[Index]);
                    }
                    break;
                }
            }
        }
    }

    ok(DataFailures == 0, "%s had %lu data failures\n", Name, DataFailures);
    ok(ReturnFailures == 0, "%s had %lu return-value failures\n", Name, ReturnFailures);
}

static VOID
TestLargeFill(const char *Name, PMEMSET Function)
{
    static const SIZE_T Lengths[] =
    {
        1 << 20, (8 << 20) + 1, (24 << 20) + 63, (40 << 20) - 1, 40 << 20
    };
    SIZE_T Size = (48 << 20), LengthIndex;
    PUCHAR Buffer;

    if (Function == NULL)
        return;

    Buffer = VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL)
    {
        skip("%s: %Iu bytes are not available: %lu\n", Name, (size_t)Size, GetLastError());
        return;
    }

    for (LengthIndex = 0; LengthIndex < _countof(Lengths); ++LengthIndex)
    {
        SIZE_T Length = Lengths[LengthIndex], Offset = (LengthIndex * 17) & 63, Index;
        UCHAR Value = (UCHAR)(0x80 | LengthIndex), Guard = (UCHAR)~Value;
        SIZE_T Mismatch = (SIZE_T)-1;
        PUCHAR Target = Buffer + Offset;
        void *Result;

        memset(Buffer, Guard, Size);
        Result = Function(Target, 0x100 | Value, Length);
        if (Result != Target)
            ok(0, "%s length %Iu returned %p, expected %p\n", Name, (size_t)Length, Result, Target);

        for (Index = 0; Index < Length; ++Index)
        {
            if (Target[Index] != Value)
            {
                Mismatch = Index;
                break;
            }
        }
        ok(Mismatch == (SIZE_T)-1, "%s length %Iu offset %Iu: byte %Iu is %02x, expected %02x\n", Name,
           (size_t)Length, (size_t)Offset, (size_t)Mismatch, Mismatch == (SIZE_T)-1 ? 0 : Target[Mismatch], Value);

        for (Index = 0; Index < Offset; ++Index)
        {
            if (Buffer[Index] != Guard)
                break;
        }
        ok(Index == Offset, "%s length %Iu wrote %Iu bytes before the start\n", Name, (size_t)Length,
           (size_t)(Offset - Index));

        for (Index = Offset + Length; Index < Size; ++Index)
        {
            if (Buffer[Index] != Guard)
                break;
        }
        ok(Index == Size, "%s length %Iu wrote past the end at %Iu\n", Name, (size_t)Length,
           (size_t)(Index - Offset - Length));
    }

    VirtualFree(Buffer, 0, MEM_RELEASE);
}

START_TEST(memset)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE Msvcrt = LoadLibraryW(L"msvcrt.dll");

    ok(Ntdll != NULL && Msvcrt != NULL, "Loading the modules failed: %lu\n", GetLastError());
    if (Ntdll != NULL)
    {
        TestFill("ntdll memset", (PMEMSET)GetProcAddress(Ntdll, "memset"));
        TestLargeFill("ntdll memset", (PMEMSET)GetProcAddress(Ntdll, "memset"));
    }
    if (Msvcrt != NULL)
    {
        TestFill("msvcrt memset", (PMEMSET)GetProcAddress(Msvcrt, "memset"));
        TestLargeFill("msvcrt memset", (PMEMSET)GetProcAddress(Msvcrt, "memset"));
        FreeLibrary(Msvcrt);
    }
}
