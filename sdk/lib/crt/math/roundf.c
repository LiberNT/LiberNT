/*
 * COPYRIGHT:       BSD - See COPYING.ARM in the top level directory
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Portable implementation of roundf
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include <math.h>

float roundf(float arg)
{
    unsigned int u32 = *(unsigned int *)&arg;

    if ((u32 & 0x7FFFFFFFU) > 0x7F800000U)
    {
        u32 |= 0x00400000U;
        return *(float *)&u32;
    }

    if (arg < 0.0)
        return ceilf(arg - 0.5);
    else if (arg > 0.0)
        return floorf(arg + 0.5);
    return arg;
}
