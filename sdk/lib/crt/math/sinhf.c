
#include <math.h>

#ifdef _MSC_VER
#pragma function(sinhf)
#endif

_Check_return_
float
__cdecl
sinhf(
    _In_ float x)
{
    if (x != x)
    {
        unsigned int u32 = *(unsigned int *)&x;

        u32 |= 0x00400000U;
        return *(float *)&u32;
    }

    return (float)sinh((double)x);
}
