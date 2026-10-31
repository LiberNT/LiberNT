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
                              Length, Index, Want, Buffer[Index]);
                    }
                    break;
                }
            }
        }
    }

    ok(DataFailures == 0, "%s had %lu data failures\n", Name, DataFailures);
    ok(ReturnFailures == 0, "%s had %lu return-value failures\n", Name, ReturnFailures);
}

START_TEST(memset)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE Msvcrt = LoadLibraryW(L"msvcrt.dll");

    ok(Ntdll != NULL && Msvcrt != NULL, "Loading the modules failed: %lu\n", GetLastError());
    if (Ntdll != NULL)
        TestFill("ntdll memset", (PMEMSET)GetProcAddress(Ntdll, "memset"));
    if (Msvcrt != NULL)
    {
        TestFill("msvcrt memset", (PMEMSET)GetProcAddress(Msvcrt, "memset"));
        FreeLibrary(Msvcrt);
    }
}
