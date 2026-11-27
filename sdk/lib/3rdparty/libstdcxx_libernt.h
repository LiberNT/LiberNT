/*
 * PROJECT:     LiberNT web engine build
 * LICENSE:     MIT (https://spdx.org/licenses/MIT.html)
 * PURPOSE:     Load libstdc++ before the SAL annotations, keep one math.h and the C99 math the web view needs in std
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#if defined(__GNUC__) && !defined(__clang__) && defined(__cplusplus)
#include <cstdlib>
#include <cmath>
#define _INC_MATH
#ifndef _GLIBCXX_USE_C99_MATH_FUNCS
namespace std
{
using ::round;
inline double nearbyint(double value) { return __builtin_nearbyint(value); }
inline float nearbyint(float value) { return __builtin_nearbyintf(value); }
inline long lround(double value) { return __builtin_lround(value); }
inline long lround(float value) { return __builtin_lroundf(value); }
inline bool isfinite(float value) { return __builtin_isfinite(value); }
inline bool isfinite(double value) { return __builtin_isfinite(value); }
inline bool isfinite(long double value) { return __builtin_isfinite(value); }
}
#endif
#include <sal.h>
#undef __valid
#endif
