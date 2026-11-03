
#include <math.h>

#ifdef _MSC_VER
#pragma function(coshf)
#endif

_Check_return_
float
__cdecl
coshf(
    _In_ float x)
{
    if (x != x)
    {
        unsigned int u32 = *(unsigned int *)&x;

        u32 |= 0x00400000U;
        return *(float *)&u32;
    }

    return (float)cosh((double)x);
}
