
#include <float.h>
#include <math.h>
#include <errno.h>

_Check_return_
double
__cdecl
exp2(
    _In_ double x)
{
    /* Prevent compilers from folding pow(2.0, x) back into exp2(x). */
    static const volatile double TWO = 2.0;

    if (_finite(x))
    {
        if (x >= 1024.0)
        {
            errno = ERANGE;
            return HUGE_VAL;
        }
        if (x <= -1075.0)
            return 0.0;
    }
    return pow(TWO, x);
}
