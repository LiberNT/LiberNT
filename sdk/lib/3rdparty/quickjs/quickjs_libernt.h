/*
 * PROJECT:     LiberNT QuickJS build
 * LICENSE:     MIT (https://spdx.org/licenses/MIT.html)
 * PURPOSE:     Headers QuickJS expects before its own on Windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windows.h>
#include <malloc.h>
#include <math.h>

#ifndef alloca
#define alloca __builtin_alloca
#endif
#ifndef signbit
#define signbit(x) __builtin_signbit(x)
#endif

double __cdecl cbrt(double);
double __cdecl log1p(double);
double __cdecl expm1(double);
double __cdecl asinh(double);
double __cdecl acosh(double);
double __cdecl atanh(double);
double __cdecl copysign(double, double);
double __cdecl scalbn(double, int);
