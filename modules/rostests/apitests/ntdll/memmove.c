/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the ntdll memcpy and memmove exports
 */

#include "precomp.h"

typedef void *(__cdecl *PMEMMOVE)(void *, const void *, size_t);

static const SIZE_T Lengths[] =
{
    0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33,
    63, 64, 65, 127, 128, 129, 191, 192
};

static VOID
TestCopyFunction(const char *Name, PMEMMOVE Function, BOOL AllowOverlap)
{
    UCHAR Actual[256], Expected[256], Temporary[192];
    ULONG Destination, Source, LengthIndex, Index;
    ULONG DataFailures = 0, ReturnFailures = 0;

    if (Function == NULL)
    {
        skip("ntdll does not export %s\n", Name);
        return;
    }

    for (Destination = 0; Destination < 32; ++Destination)
    {
        for (Source = 0; Source < 32; ++Source)
        {
            for (LengthIndex = 0; LengthIndex < _countof(Lengths); ++LengthIndex)
            {
                SIZE_T Length = Lengths[LengthIndex];
                void *Result;

                if (Destination + Length > sizeof(Actual) ||
                    Source + Length > sizeof(Actual))
                {
                    continue;
                }
                if (!AllowOverlap && Length != 0 &&
                    Destination < Source + Length &&
                    Source < Destination + Length)
                {
                    continue;
                }

                for (Index = 0; Index < sizeof(Actual); ++Index)
                {
                    Actual[Index] = Expected[Index] =
                        (UCHAR)(Index * 37u + Destination * 11u + Source);
                }
                for (Index = 0; Index < Length; ++Index)
                    Temporary[Index] = Expected[Source + Index];
                for (Index = 0; Index < Length; ++Index)
                    Expected[Destination + Index] = Temporary[Index];

                Result = Function(&Actual[Destination], &Actual[Source], Length);
                if (Result != &Actual[Destination])
                    ++ReturnFailures;
                for (Index = 0; Index < sizeof(Actual); ++Index)
                {
                    if (Actual[Index] != Expected[Index])
                    {
                        ++DataFailures;
                        if (DataFailures <= 4)
                        {
                            trace("%s mismatch: dst=%lu src=%lu length=%Iu index=%lu expected=%02x actual=%02x\n",
                                  Name, Destination, Source, Length, Index,
                                  Expected[Index], Actual[Index]);
                        }
                        break;
                    }
                }
            }
        }
    }

    ok(DataFailures == 0, "%s had %lu data failures\n", Name, DataFailures);
    ok(ReturnFailures == 0, "%s had %lu return-value failures\n",
       Name, ReturnFailures);
}

static VOID
TestLargeCopy(const char *Name, PMEMMOVE Function, BOOL AllowOverlap)
{
    static const struct
    {
        SIZE_T Destination;
        SIZE_T Source;
        SIZE_T Length;
        BOOL Overlap;
    } Cases[] =
    {
        { 0, 0, 40 << 20, FALSE },
        { 1, 3, (40 << 20) + 5, FALSE },
        { 15, 0, (40 << 20) + 63, FALSE },
        { 8, 9, (40 << 20) - 1, FALSE },
        { 0, 4096, 40 << 20, TRUE },
        { 4097, 0, 40 << 20, TRUE },
    };
    SIZE_T Size = 48 << 20, CaseIndex, Index;
    PUCHAR Buffer, Copy;

    if (Function == NULL)
        return;
    Buffer = VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Copy = VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(Buffer != NULL && Copy != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (Buffer == NULL || Copy == NULL)
        return;

    for (CaseIndex = 0; CaseIndex < _countof(Cases); ++CaseIndex)
    {
        SIZE_T Destination = Cases[CaseIndex].Destination, Source = Cases[CaseIndex].Source;
        SIZE_T Length = Cases[CaseIndex].Length, Mismatch = (SIZE_T)-1;
        PUCHAR Target, Origin;
        void *Result;

        if (Cases[CaseIndex].Overlap && !AllowOverlap)
            continue;
        for (Index = 0; Index < Size; ++Index)
            Buffer[Index] = (UCHAR)((Index >> 12) * 131u + Index * 7u);
        if (Cases[CaseIndex].Overlap)
        {
            Target = Buffer + Destination;
            Origin = Buffer + Source;
            for (Index = 0; Index < Size; ++Index)
                Copy[Index] = Buffer[Index];
        }
        else
        {
            Target = Copy + Destination;
            Origin = Buffer + Source;
            FillMemory(Copy, Size, 0xCC);
        }

        Result = Function(Target, Origin, Length);
        ok(Result == Target, "%s case %Iu returned %p instead of %p\n", Name, CaseIndex, Result, Target);
        for (Index = 0; Index < Length; ++Index)
        {
            UCHAR Want = Cases[CaseIndex].Overlap ? Copy[Source + Index] : Buffer[Source + Index];

            if (Target[Index] != Want)
            {
                Mismatch = Index;
                break;
            }
        }
        ok(Mismatch == (SIZE_T)-1, "%s case %Iu differs at byte %Iu\n", Name, CaseIndex, Mismatch);
        if (!Cases[CaseIndex].Overlap)
        {
            ok(Destination == 0 || Copy[Destination - 1] == 0xCC, "%s case %Iu wrote before the destination\n", Name,
               CaseIndex);
            ok(Copy[Destination + Length] == 0xCC, "%s case %Iu wrote past the destination\n", Name, CaseIndex);
        }
    }

    VirtualFree(Buffer, 0, MEM_RELEASE);
    VirtualFree(Copy, 0, MEM_RELEASE);
}

START_TEST(memmove)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    ok(Ntdll != NULL, "GetModuleHandleW failed: %lu\n", GetLastError());
    if (Ntdll == NULL)
        return;

    TestCopyFunction("memmove", (PMEMMOVE)GetProcAddress(Ntdll, "memmove"), TRUE);
    TestCopyFunction("memcpy", (PMEMMOVE)GetProcAddress(Ntdll, "memcpy"), FALSE);
    TestLargeCopy("memmove", (PMEMMOVE)GetProcAddress(Ntdll, "memmove"), TRUE);
    TestLargeCopy("memcpy", (PMEMMOVE)GetProcAddress(Ntdll, "memcpy"), FALSE);
}
