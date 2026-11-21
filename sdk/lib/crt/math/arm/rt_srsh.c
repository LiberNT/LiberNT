/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     __rt_srsh, the Windows on ARM helper for a signed 64-bit right shift by a variable count
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

long long __rt_srsh(long long Value, unsigned int Shift)
{
    if (Shift >= 64)
        return (Value < 0) ? -1 : 0;
    return Value >> Shift;
}
