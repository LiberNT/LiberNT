/*
 * PROJECT:     LiberNT Calculator
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decimal floating point number interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>

#define CALC_DIGITS 56
#define CALC_EXPONENT_LIMIT 10000

typedef enum _CALC_STATUS
{
    CALC_OK,
    CALC_DIVIDE_BY_ZERO,
    CALC_INVALID_INPUT,
    CALC_OVERFLOW,
    CALC_UNDEFINED
} CALC_STATUS;

typedef enum _CALC_ANGLE
{
    CALC_ANGLE_DEGREES,
    CALC_ANGLE_RADIANS,
    CALC_ANGLE_GRADIANS
} CALC_ANGLE;

typedef struct _CALC_NUMBER
{
    int Sign;
    int Exponent;
    int Inexact;
    unsigned char Digits[CALC_DIGITS];
} CALC_NUMBER;

void NumZero(CALC_NUMBER *Result);
void NumFromInt64(CALC_NUMBER *Result, long long Value);
void NumFromUInt64(CALC_NUMBER *Result, unsigned long long Value);
int NumToInt64(const CALC_NUMBER *Value, long long *Result);
int NumIsZero(const CALC_NUMBER *Value);
int NumIsInteger(const CALC_NUMBER *Value);
int NumCompare(const CALC_NUMBER *Left, const CALC_NUMBER *Right);
void NumNegate(CALC_NUMBER *Value);
void NumTruncate(CALC_NUMBER *Value);
void NumPi(CALC_NUMBER *Result);
void NumEuler(CALC_NUMBER *Result);

CALC_STATUS NumAdd(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result);
CALC_STATUS NumSubtract(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result);
CALC_STATUS NumMultiply(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result);
CALC_STATUS NumDivide(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result);
CALC_STATUS NumModulo(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result);
CALC_STATUS NumPower(const CALC_NUMBER *Base, const CALC_NUMBER *Exponent, CALC_NUMBER *Result);
CALC_STATUS NumRoot(const CALC_NUMBER *Value, const CALC_NUMBER *Degree, CALC_NUMBER *Result);
CALC_STATUS NumSquareRoot(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumExp(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumLn(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumLog10(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumFactorial(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumSin(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result);
CALC_STATUS NumCos(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result);
CALC_STATUS NumTan(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result);
CALC_STATUS NumAsin(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result);
CALC_STATUS NumAcos(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result);
CALC_STATUS NumAtan(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result);
CALC_STATUS NumSinh(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumCosh(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumTanh(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumAsinh(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumAcosh(const CALC_NUMBER *Value, CALC_NUMBER *Result);
CALC_STATUS NumAtanh(const CALC_NUMBER *Value, CALC_NUMBER *Result);

CALC_STATUS NumFromString(const char *Text, CALC_NUMBER *Result);
void NumToString(const CALC_NUMBER *Value, int Precision, int Exponential, char *Buffer, size_t Size);
