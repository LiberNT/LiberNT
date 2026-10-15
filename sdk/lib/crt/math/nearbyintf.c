/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Round to an integral value without raising the inexact exception
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <math.h>
#include <fenv.h>

float __cdecl nearbyintf(float x)
{
    volatile float Input = x;
    volatile float Result;
    fenv_t Environment;

    feholdexcept(&Environment);
    Result = rintf(Input);
    fesetenv(&Environment);
    return Result;
}
