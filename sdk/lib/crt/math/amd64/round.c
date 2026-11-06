/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     x64 round, which returns a signaling NaN unchanged as Windows x64 does
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <math.h>

double round(double arg)
{
    if (arg < 0.0)
        return ceil(arg - 0.5);
    else if (arg > 0.0)
        return floor(arg + 0.5);
    return arg;
}
