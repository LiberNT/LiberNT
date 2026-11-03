/* Copyright (C) 1994 DJ Delorie, see COPYING.DJ for details */
#include <math.h>

#ifdef _MSC_VER
#pragma function(sinh)
#endif

/*
 * @implemented
 */
double sinh(double x)
{
 if (x != x)
 {
     unsigned long long u64 = *(unsigned long long *)&x;

     u64 |= 0x0008000000000000ULL;
     return *(double *)&u64;
 }

 if(x >= 0.0)
 {
   const double epos = exp(x);
   return (epos - 1.0/epos) / 2.0;
 }
 else
 {
   const double eneg = exp(-x);
   return (1.0/eneg - eneg) / 2.0;
 }
}
