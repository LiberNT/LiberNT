/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter altitude string comparison
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>

typedef struct _RTLP_ALTITUDE
{
    PCWSTR Integer;
    ULONG IntegerLength;
    PCWSTR Fraction;
    ULONG FractionLength;
} RTLP_ALTITUDE, *PRTLP_ALTITUDE;

/* TODO: ordering of malformed altitude strings is undocumented; their characters compare by code unit */
static
VOID
RtlpSplitAltitude(
    _In_ PCUNICODE_STRING String,
    _Out_ PRTLP_ALTITUDE Altitude)
{
    PCWSTR Buffer = String->Buffer;
    ULONG Length = String->Length / sizeof(WCHAR);
    ULONG Dot;

    for (Dot = 0; Dot < Length && Buffer[Dot] != L'.'; Dot++);

    Altitude->Integer = Buffer;
    Altitude->IntegerLength = Dot;
    while (Altitude->IntegerLength != 0 && Altitude->Integer[0] == L'0')
    {
        Altitude->Integer++;
        Altitude->IntegerLength--;
    }

    if (Dot < Length)
    {
        Altitude->Fraction = &Buffer[Dot + 1];
        Altitude->FractionLength = Length - Dot - 1;
    }
    else
    {
        Altitude->Fraction = NULL;
        Altitude->FractionLength = 0;
    }

    while (Altitude->FractionLength != 0 &&
           Altitude->Fraction[Altitude->FractionLength - 1] == L'0')
    {
        Altitude->FractionLength--;
    }
}

static
LONG
RtlpCompareDigits(
    _In_reads_(Length) PCWSTR Digits1,
    _In_reads_(Length) PCWSTR Digits2,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Digits1[Index] != Digits2[Index])
            return (Digits1[Index] < Digits2[Index]) ? -1 : 1;
    }

    return 0;
}

LONG
NTAPI
RtlCompareAltitudes(
    _In_ PCUNICODE_STRING Altitude1,
    _In_ PCUNICODE_STRING Altitude2)
{
    RTLP_ALTITUDE First, Second;
    LONG Result;

    RtlpSplitAltitude(Altitude1, &First);
    RtlpSplitAltitude(Altitude2, &Second);

    if (First.IntegerLength != Second.IntegerLength)
        return (First.IntegerLength < Second.IntegerLength) ? -1 : 1;

    Result = RtlpCompareDigits(First.Integer, Second.Integer, First.IntegerLength);
    if (Result != 0)
        return Result;

    Result = RtlpCompareDigits(First.Fraction,
                               Second.Fraction,
                               min(First.FractionLength, Second.FractionLength));
    if (Result != 0)
        return Result;

    if (First.FractionLength != Second.FractionLength)
        return (First.FractionLength < Second.FractionLength) ? -1 : 1;

    return 0;
}
