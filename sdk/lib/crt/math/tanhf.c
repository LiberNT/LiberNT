
#include <math.h>

#ifdef _MSC_VER
#pragma function(tanhf)
#endif

_Check_return_
float
__cdecl
tanhf(
    _In_ float x)
{
    if (x != x)
    {
        unsigned int u32 = *(unsigned int *)&x;

        u32 |= 0x00400000U;
        return *(float *)&u32;
    }

    return (float)tanh((double)x);
}
