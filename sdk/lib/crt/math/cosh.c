/* Copyright (C) 1994 DJ Delorie, see COPYING.DJ for details */
#include <math.h>

#ifdef _MSC_VER
#pragma function(cosh)
#endif

/*
 * @implemented
 */
double cosh(double x)
{
    if (x != x)
    {
        unsigned long long u64 = *(unsigned long long *)&x;

        u64 |= 0x0008000000000000ULL;
        return *(double *)&u64;
    }

    const double ebig = exp(fabs(x));
    return (ebig + 1.0/ebig) / 2.0;
}
