/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Round to an integral value without raising the inexact exception
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <math.h>
#include <fenv.h>

double __cdecl nearbyint(double x)
{
    volatile double Input = x;
    volatile double Result;
    fenv_t Environment;

    feholdexcept(&Environment);
    Result = rint(Input);
    fesetenv(&Environment);
    return Result;
}
