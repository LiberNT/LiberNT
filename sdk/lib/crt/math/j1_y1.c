#include <math.h>
#include <float.h>
#include "ieee754/ieee754.h"

int *_errno(void);

#define BESSEL_LARGE 4503599627370496.0
#define BESSEL_PI 3.14159265358979323846
#define BESSEL_3PI_4 2.35619449019234492885

static double MakeDouble(unsigned long long Bits)
{
  union { unsigned long long l; double d; } u;
  u.l = Bits;
  return u.d;
}

/*
 * @unimplemented
 */
double _j1(double num)
{
  if (!_finite(num))
  {
    *_errno() = EDOM;
    if (!_isnan(num))
      return MakeDouble(0xFFF8000000000000ULL);
  }
  if (fabs(num) >= BESSEL_LARGE)
  {
    double x = fabs(num);
    double result = sqrt(2.0 / (BESSEL_PI * x)) * cos(x - BESSEL_3PI_4);
    return (num < 0.0) ? -result : result;
  }
  return __ieee754_j1(num);
}

/*
 * @implemented
 */
double _y1(double num)
{
  int fpclass = _fpclass(num);

  if (fpclass == _FPCLASS_NZ || fpclass == _FPCLASS_PZ)
  {
    volatile double Zero = 0.0;
    *_errno() = ERANGE;
    return Zero / Zero;
  }
  if (fpclass == _FPCLASS_NINF || fpclass == _FPCLASS_NN ||
      fpclass == _FPCLASS_ND || fpclass == _FPCLASS_PINF)
  {
    *_errno() = EDOM;
    return MakeDouble(0xFFF8000000000000ULL);
  }
  if (_isnan(num))
    *_errno() = EDOM;
  if (num >= BESSEL_LARGE)
    return sqrt(2.0 / (BESSEL_PI * num)) * sin(num - BESSEL_3PI_4);
  return __ieee754_y1(num);
}
