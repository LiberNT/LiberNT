/*
 * PROJECT:     LiberNT timekeeping
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Architecture-neutral fixed-point clock arithmetic
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

FORCEINLINE
ULONGLONG
RtlpMultiplyTimeValues(ULONGLONG Left, ULONGLONG Right, PULONGLONG High)
{
    ULONGLONG LowProduct = (ULONGLONG)(ULONG)Left * (ULONG)Right;
    ULONGLONG CrossLeft = (Left >> 32) * (ULONG)Right;
    ULONGLONG CrossRight = (ULONGLONG)(ULONG)Left * (Right >> 32);
    ULONGLONG Carry = (LowProduct >> 32) + (ULONG)CrossLeft + (ULONG)CrossRight;

    *High = (Left >> 32) * (Right >> 32) + (CrossLeft >> 32) +
            (CrossRight >> 32) + (Carry >> 32);
    return (Carry << 32) | (ULONG)LowProduct;
}

FORCEINLINE
ULONGLONG
RtlpScaleTimeDelta(ULONGLONG Delta,
                  ULONGLONG Increment,
                  UCHAR Shift,
                  PULONGLONG Fraction)
{
    ULONGLONG High, Low, Result;

    Low = RtlpMultiplyTimeValues(Delta, Increment, &High);
    Result = High;
    if (Shift)
    {
        Result = (High << Shift) | (Low >> (64 - Shift));
        Low <<= Shift;
    }
    if (Fraction) *Fraction = Low;
    return Result;
}
