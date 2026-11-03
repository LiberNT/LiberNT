/*
 * COPYRIGHT:       BSD - See COPYING.ARM in the top level directory
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Portable implementation of sqrt
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include <math.h>
#include <errno.h>
#include <assert.h>

double __cdecl __acrt_report_math_error(int type, char const* name, double arg1, double arg2, double retval, int error);

double
__cdecl
sqrt(
    double x)
{
    union { double d; unsigned long long u; } value;
    unsigned long long bits, mantissa, high, low, root, remainder, trial, significand;
    int exponent, i;

    /* Handle special cases */
    if (x == 0.0)
    {
        return x;
    }
    else if (x < 0.0)
    {
        return __acrt_report_math_error(_DOMAIN, "sqrt", x, 0, -fabs((x - x) / (x - x)), EDOM);
    }

    /* Convert into a 64  bit integer */
    value.d = x;
    bits = value.u;

    /* Check for !finite(x) */
    if ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL)
    {
        return x + x;
    }

    exponent = (int)(bits >> 52);
    mantissa = bits & 0x000fffffffffffffULL;
    if (exponent == 0)
    {
        exponent = 1;
        while (!(mantissa & 0x0010000000000000ULL))
        {
            mantissa <<= 1;
            exponent--;
        }
    }
    else
    {
        mantissa |= 0x0010000000000000ULL;
    }

    exponent -= 1075;
    if (exponent & 1)
    {
        mantissa <<= 1;
        exponent--;
    }

    high = mantissa >> 10;
    low = mantissa << 54;
    root = 0;
    remainder = 0;
    for (i = 0; i < 64; i++)
    {
        remainder = (remainder << 2) | (high >> 62);
        high = (high << 2) | (low >> 62);
        low <<= 2;
        trial = (root << 2) | 1;
        root <<= 1;
        if (remainder >= trial)
        {
            remainder -= trial;
            root |= 1;
        }
    }

    significand = (root + 1) >> 1;
    exponent = (exponent - 54) / 2 + 1;
    if (significand == 0x0020000000000000ULL)
    {
        significand >>= 1;
        exponent++;
    }

    value.u = ((unsigned long long)(exponent + 1075) << 52) | (significand & 0x000fffffffffffffULL);
    return value.d;
}
