/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode i386 entry points of the portable math library
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <emmintrin.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>

double __cdecl __acrt_report_math_error(int type, char const* name, double arg1, double arg2, double retval, int error);
double __cdecl __libm_sse2_core_acos(double x);
double __cdecl __libm_sse2_core_cos(double x);
double __cdecl __libm_sse2_core_exp(double x);
double __cdecl __libm_sse2_core_log(double x);
double __cdecl __libm_sse2_core_log10(double x);
double __cdecl __libm_sse2_core_pow(double x, double y);
double __cdecl __libm_sse2_core_sin(double x);
double __cdecl __libm_sse2_core_tan(double x);

double __cdecl acos(double x)
{
    return __libm_sse2_core_acos(x);
}

double __cdecl cos(double x)
{
    return __libm_sse2_core_cos(x);
}

double __cdecl exp(double x)
{
    return __libm_sse2_core_exp(x);
}

double __cdecl log(double x)
{
    return __libm_sse2_core_log(x);
}

double __cdecl log10(double x)
{
    return __libm_sse2_core_log10(x);
}

double __cdecl pow(double x, double y)
{
    return __libm_sse2_core_pow(x, y);
}

double __cdecl sin(double x)
{
    return __libm_sse2_core_sin(x);
}

double __cdecl sqrt(double x)
{
    union { uint64_t i; double f; } Indefinite = { 0xfff8000000000000ULL };
    __m128d Value = _mm_set_sd(x);

    if (x < 0.0)
        return __acrt_report_math_error(_DOMAIN, "sqrt", x, 0.0, Indefinite.f, EDOM);
    return _mm_cvtsd_f64(_mm_sqrt_sd(Value, Value));
}

double __cdecl tan(double x)
{
    return __libm_sse2_core_tan(x);
}
