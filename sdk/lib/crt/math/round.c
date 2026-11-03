/*
 * COPYRIGHT:       BSD - See COPYING.ARM in the top level directory
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Portable implementation of round
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include <math.h>

double round(double arg)
{
    unsigned long long u64 = *(unsigned long long *)&arg;

    if ((u64 & ~(1ULL << 63)) > 0x7FF0000000000000ULL)
    {
        u64 |= 0x0008000000000000ULL;
        return *(double *)&u64;
    }

    if (arg < 0.0)
        return ceil(arg - 0.5);
    else if (arg > 0.0)
        return floor(arg + 0.5);
    return arg;
}
