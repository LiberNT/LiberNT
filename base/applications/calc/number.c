/*
 * PROJECT:     LiberNT Calculator
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decimal floating point arithmetic and elementary functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "calcnum.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define WIDE_DIGITS (2 * CALC_DIGITS + 2)
#define SERIES_FLOOR (-(CALC_DIGITS + 4))
#define CANCEL_DIGITS (CALC_DIGITS - 6)
#define PARSE_DIGITS 256

static CALC_NUMBER ConstLn2, ConstLn10, ConstPi, ConstHalfPi, ConstEuler;
static int ConstReady;

void
NumZero(CALC_NUMBER *Result)
{
    memset(Result, 0, sizeof(*Result));
}

static CALC_STATUS
NumSet(CALC_NUMBER *Result, int Sign, int Exponent, const int *Digits, int Count, int Inexact)
{
    int First = 0, Index, Kept;

    while (First < Count && Digits[First] == 0)
        First++;
    if (First == Count || Sign == 0)
    {
        NumZero(Result);
        return CALC_OK;
    }
    Exponent -= First;
    Kept = Count - First;
    for (Index = First + CALC_DIGITS; Index < Count; Index++)
    {
        if (Digits[Index] != 0)
            Inexact = 1;
    }
    for (Index = 0; Index < CALC_DIGITS; Index++)
        Result->Digits[Index] = Index < Kept ? (unsigned char)Digits[First + Index] : 0;
    if (Kept > CALC_DIGITS && Digits[First + CALC_DIGITS] >= 5)
    {
        Index = CALC_DIGITS - 1;
        while (Index >= 0 && Result->Digits[Index] == 9)
            Result->Digits[Index--] = 0;
        if (Index >= 0)
        {
            Result->Digits[Index]++;
        }
        else
        {
            Result->Digits[0] = 1;
            Exponent++;
        }
    }
    if (Exponent > CALC_EXPONENT_LIMIT)
    {
        NumZero(Result);
        return CALC_OVERFLOW;
    }
    if (Exponent < -CALC_EXPONENT_LIMIT)
    {
        NumZero(Result);
        return CALC_OK;
    }
    Result->Sign = Sign < 0 ? -1 : 1;
    Result->Exponent = Exponent;
    Result->Inexact = Inexact != 0;
    return CALC_OK;
}

void
NumFromUInt64(CALC_NUMBER *Result, unsigned long long Value)
{
    int Digits[20], Count = 0, Index;
    int Ordered[20];

    while (Value != 0)
    {
        Digits[Count++] = (int)(Value % 10);
        Value /= 10;
    }
    for (Index = 0; Index < Count; Index++)
        Ordered[Index] = Digits[Count - 1 - Index];
    NumSet(Result, 1, Count, Ordered, Count, 0);
}

void
NumFromInt64(CALC_NUMBER *Result, long long Value)
{
    if (Value < 0)
    {
        NumFromUInt64(Result, (unsigned long long)(-(Value + 1)) + 1);
        Result->Sign = -1;
    }
    else
    {
        NumFromUInt64(Result, (unsigned long long)Value);
    }
}

int
NumToInt64(const CALC_NUMBER *Value, long long *Result)
{
    unsigned long long Magnitude = 0;
    int Index;

    *Result = 0;
    if (Value->Sign == 0 || Value->Exponent <= 0)
        return 1;
    if (Value->Exponent > 19)
        return 0;
    for (Index = 0; Index < Value->Exponent; Index++)
    {
        unsigned Digit = Index < CALC_DIGITS ? Value->Digits[Index] : 0;

        if (Magnitude > (ULLONG_MAX - Digit) / 10)
            return 0;
        Magnitude = Magnitude * 10 + Digit;
    }
    if (Value->Sign > 0)
    {
        if (Magnitude > (unsigned long long)LLONG_MAX)
            return 0;
        *Result = (long long)Magnitude;
    }
    else
    {
        if (Magnitude > (unsigned long long)LLONG_MAX + 1)
            return 0;
        *Result = (long long)(0 - Magnitude);
    }
    return 1;
}

int
NumIsZero(const CALC_NUMBER *Value)
{
    return Value->Sign == 0;
}

int
NumIsInteger(const CALC_NUMBER *Value)
{
    int Index;

    if (Value->Sign == 0)
        return 1;
    if (Value->Exponent <= 0)
        return 0;
    for (Index = Value->Exponent; Index < CALC_DIGITS; Index++)
    {
        if (Value->Digits[Index] != 0)
            return 0;
    }
    return 1;
}

static int
CompareMagnitude(const CALC_NUMBER *Left, const CALC_NUMBER *Right)
{
    if (Left->Sign == 0 || Right->Sign == 0)
        return (Left->Sign != 0) - (Right->Sign != 0);
    if (Left->Exponent != Right->Exponent)
        return Left->Exponent > Right->Exponent ? 1 : -1;
    return memcmp(Left->Digits, Right->Digits, CALC_DIGITS);
}

int
NumCompare(const CALC_NUMBER *Left, const CALC_NUMBER *Right)
{
    int Order;

    if (Left->Sign != Right->Sign)
        return Left->Sign > Right->Sign ? 1 : -1;
    Order = CompareMagnitude(Left, Right);
    if (Order != 0)
        Order = Order > 0 ? 1 : -1;
    return Left->Sign < 0 ? -Order : Order;
}

void
NumNegate(CALC_NUMBER *Value)
{
    Value->Sign = -Value->Sign;
}

void
NumTruncate(CALC_NUMBER *Value)
{
    int Index;

    if (Value->Sign == 0)
        return;
    if (Value->Exponent <= 0)
    {
        NumZero(Value);
        return;
    }
    for (Index = Value->Exponent; Index < CALC_DIGITS; Index++)
        Value->Digits[Index] = 0;
}

static CALC_STATUS
AddMagnitudes(const CALC_NUMBER *Large, const CALC_NUMBER *Small, int Sign, CALC_NUMBER *Result)
{
    int Wide[WIDE_DIGITS];
    int Shift = Large->Exponent - Small->Exponent;
    int Inexact = Large->Inexact | Small->Inexact;
    int Count, Index, Carry = 0;

    if (Shift > CALC_DIGITS + 1)
    {
        *Result = *Large;
        Result->Sign = Sign;
        Result->Inexact = 1;
        return CALC_OK;
    }
    Count = CALC_DIGITS + Shift;
    for (Index = Count - 1; Index >= 0; Index--)
    {
        int Digit = Carry;

        if (Index < CALC_DIGITS)
            Digit += Large->Digits[Index];
        if (Index >= Shift)
            Digit += Small->Digits[Index - Shift];
        Wide[Index + 1] = Digit % 10;
        Carry = Digit / 10;
    }
    Wide[0] = Carry;
    return NumSet(Result, Sign, Large->Exponent + 1, Wide, Count + 1, Inexact);
}

static CALC_STATUS
SubtractMagnitudes(const CALC_NUMBER *Large, const CALC_NUMBER *Small, int Sign, CALC_NUMBER *Result)
{
    int Wide[WIDE_DIGITS];
    int Shift = Large->Exponent - Small->Exponent;
    int Inexact = Large->Inexact | Small->Inexact;
    int Count, Index, Borrow = 0;

    if (Shift > CALC_DIGITS + 1)
    {
        *Result = *Large;
        Result->Sign = Sign;
        Result->Inexact = 1;
        return CALC_OK;
    }
    Count = CALC_DIGITS + Shift;
    for (Index = Count - 1; Index >= 0; Index--)
    {
        int Digit = -Borrow;

        if (Index < CALC_DIGITS)
            Digit += Large->Digits[Index];
        if (Index >= Shift)
            Digit -= Small->Digits[Index - Shift];
        Borrow = Digit < 0;
        Wide[Index] = Digit < 0 ? Digit + 10 : Digit;
    }
    return NumSet(Result, Sign, Large->Exponent, Wide, Count, Inexact);
}

CALC_STATUS
NumAdd(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result)
{
    CALC_NUMBER A = *Left, B = *Right;
    CALC_STATUS Status;
    int Order, Top;

    if (A.Sign == 0)
    {
        *Result = B;
        return CALC_OK;
    }
    if (B.Sign == 0)
    {
        *Result = A;
        return CALC_OK;
    }
    if (A.Sign == B.Sign)
    {
        if (A.Exponent >= B.Exponent)
            return AddMagnitudes(&A, &B, A.Sign, Result);
        return AddMagnitudes(&B, &A, A.Sign, Result);
    }
    Order = CompareMagnitude(&A, &B);
    if (Order == 0)
    {
        NumZero(Result);
        return CALC_OK;
    }
    Top = A.Exponent > B.Exponent ? A.Exponent : B.Exponent;
    if (Order > 0)
        Status = SubtractMagnitudes(&A, &B, A.Sign, Result);
    else
        Status = SubtractMagnitudes(&B, &A, B.Sign, Result);
    if (Status == CALC_OK && Result->Sign != 0 && Result->Inexact && Result->Exponent < Top - CANCEL_DIGITS)
        NumZero(Result);
    return Status;
}

CALC_STATUS
NumSubtract(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result)
{
    CALC_NUMBER Negated = *Right;

    NumNegate(&Negated);
    return NumAdd(Left, &Negated, Result);
}

CALC_STATUS
NumMultiply(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result)
{
    int Wide[2 * CALC_DIGITS];
    int Row, Column;

    if (Left->Sign == 0 || Right->Sign == 0)
    {
        NumZero(Result);
        return CALC_OK;
    }
    memset(Wide, 0, sizeof(Wide));
    for (Row = 0; Row < CALC_DIGITS; Row++)
    {
        if (Left->Digits[Row] == 0)
            continue;
        for (Column = 0; Column < CALC_DIGITS; Column++)
            Wide[Row + Column + 1] += Left->Digits[Row] * Right->Digits[Column];
    }
    for (Row = 2 * CALC_DIGITS - 1; Row > 0; Row--)
    {
        Wide[Row - 1] += Wide[Row] / 10;
        Wide[Row] %= 10;
    }
    return NumSet(Result, Left->Sign * Right->Sign, Left->Exponent + Right->Exponent, Wide, 2 * CALC_DIGITS,
                  Left->Inexact | Right->Inexact);
}

CALC_STATUS
NumDivide(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result)
{
    int Remainder[CALC_DIGITS + 1];
    int Quotient[CALC_DIGITS + 3];
    int Inexact = Left->Inexact | Right->Inexact;
    int Sign = Left->Sign * Right->Sign;
    int Exponent = Left->Exponent - Right->Exponent + 1;
    int Position, Index;

    if (Right->Sign == 0)
    {
        NumZero(Result);
        return Left->Sign == 0 ? CALC_UNDEFINED : CALC_DIVIDE_BY_ZERO;
    }
    if (Left->Sign == 0)
    {
        NumZero(Result);
        return CALC_OK;
    }
    Remainder[0] = 0;
    for (Index = 0; Index < CALC_DIGITS; Index++)
        Remainder[Index + 1] = Left->Digits[Index];
    for (Position = 0; Position < CALC_DIGITS + 3; Position++)
    {
        int Digit = 0;

        for (;;)
        {
            int Order = Remainder[0] != 0;

            for (Index = 0; Order == 0 && Index < CALC_DIGITS; Index++)
                Order = Remainder[Index + 1] - Right->Digits[Index];
            if (Order < 0)
                break;
            for (Index = CALC_DIGITS - 1; Index >= 0; Index--)
            {
                Remainder[Index + 1] -= Right->Digits[Index];
                if (Remainder[Index + 1] < 0)
                {
                    Remainder[Index + 1] += 10;
                    Remainder[Index]--;
                }
            }
            Digit++;
        }
        Quotient[Position] = Digit;
        for (Index = 0; Index < CALC_DIGITS; Index++)
            Remainder[Index] = Remainder[Index + 1];
        Remainder[CALC_DIGITS] = 0;
    }
    for (Index = 0; Index <= CALC_DIGITS; Index++)
    {
        if (Remainder[Index] != 0)
            Inexact = 1;
    }
    return NumSet(Result, Sign, Exponent, Quotient, CALC_DIGITS + 3, Inexact);
}

static void
RoundToInteger(CALC_NUMBER *Value)
{
    CALC_NUMBER Half;

    NumFromString("0.5", &Half);
    Half.Sign = Value->Sign;
    NumAdd(Value, &Half, Value);
    NumTruncate(Value);
}

static CALC_STATUS
DivideByInteger(const CALC_NUMBER *Value, long long Divisor, CALC_NUMBER *Result)
{
    CALC_NUMBER Number;

    NumFromInt64(&Number, Divisor);
    return NumDivide(Value, &Number, Result);
}

static void
MultiplyByInteger(const CALC_NUMBER *Value, long long Factor, CALC_NUMBER *Result)
{
    CALC_NUMBER Number;

    NumFromInt64(&Number, Factor);
    NumMultiply(Value, &Number, Result);
}

static void
ReciprocalSeries(int Divisor, int Alternate, CALC_NUMBER *Result)
{
    CALC_NUMBER Term, Part, Sum;
    long long Square = (long long)Divisor * Divisor;
    int Odd, Negative = 0;

    NumFromInt64(&Term, 1);
    DivideByInteger(&Term, Divisor, &Term);
    Sum = Term;
    for (Odd = 3; Odd < 1000; Odd += 2)
    {
        DivideByInteger(&Term, Square, &Term);
        if (Term.Sign == 0 || Term.Exponent < SERIES_FLOOR)
            break;
        DivideByInteger(&Term, Odd, &Part);
        Negative = !Negative;
        if (Alternate && Negative)
            NumSubtract(&Sum, &Part, &Sum);
        else
            NumAdd(&Sum, &Part, &Sum);
    }
    *Result = Sum;
}

static void
InitConstants(void)
{
    CALC_NUMBER A, B, One;

    if (ConstReady)
        return;
    ReciprocalSeries(3, 0, &A);
    MultiplyByInteger(&A, 2, &ConstLn2);
    ReciprocalSeries(9, 0, &B);
    MultiplyByInteger(&ConstLn2, 3, &A);
    MultiplyByInteger(&B, 2, &B);
    NumAdd(&A, &B, &ConstLn10);
    ReciprocalSeries(5, 1, &A);
    ReciprocalSeries(239, 1, &B);
    MultiplyByInteger(&A, 16, &A);
    MultiplyByInteger(&B, 4, &B);
    NumSubtract(&A, &B, &ConstPi);
    DivideByInteger(&ConstPi, 2, &ConstHalfPi);
    ConstLn2.Inexact = ConstLn10.Inexact = ConstPi.Inexact = ConstHalfPi.Inexact = 1;
    ConstReady = 1;
    NumFromInt64(&One, 1);
    NumExp(&One, &ConstEuler);
}

void
NumPi(CALC_NUMBER *Result)
{
    InitConstants();
    *Result = ConstPi;
}

void
NumEuler(CALC_NUMBER *Result)
{
    InitConstants();
    *Result = ConstEuler;
}

CALC_STATUS
NumSquareRoot(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Scaled = *Value, Guess, Quotient, Square;
    double Seed = 0.0, Weight = 0.1;
    int Shift, Index;

    if (Value->Sign < 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    if (Value->Sign == 0)
    {
        NumZero(Result);
        return CALC_OK;
    }
    Shift = Value->Exponent;
    if (Shift % 2 != 0)
        Shift--;
    Scaled.Exponent -= Shift;
    for (Index = 0; Index < 16; Index++)
    {
        Seed += Scaled.Digits[Index] * Weight;
        Weight /= 10.0;
    }
    if (Scaled.Exponent == 1)
        Seed *= 10.0;
    NumFromInt64(&Guess, (long long)(sqrt(Seed) * 1e15));
    Guess.Exponent -= 15;
    for (Index = 0; Index < 5; Index++)
    {
        NumDivide(&Scaled, &Guess, &Quotient);
        NumAdd(&Guess, &Quotient, &Guess);
        DivideByInteger(&Guess, 2, &Guess);
    }
    Guess.Inexact = 0;
    NumMultiply(&Guess, &Guess, &Square);
    Guess.Inexact = Value->Inexact || Square.Inexact || NumCompare(&Square, &Scaled) != 0;
    Guess.Exponent += Shift / 2;
    *Result = Guess;
    return CALC_OK;
}

CALC_STATUS
NumExp(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Input = *Value, Quotient, Reduced, Term, Sum, Scale;
    long long Tens = 0;
    int Index;

    InitConstants();
    if (Input.Sign == 0)
    {
        NumFromInt64(Result, 1);
        return CALC_OK;
    }
    if (Input.Exponent > 6)
    {
        NumZero(Result);
        return Input.Sign > 0 ? CALC_OVERFLOW : CALC_OK;
    }
    NumDivide(&Input, &ConstLn10, &Quotient);
    RoundToInteger(&Quotient);
    NumToInt64(&Quotient, &Tens);
    NumMultiply(&Quotient, &ConstLn10, &Reduced);
    NumSubtract(&Input, &Reduced, &Reduced);
    NumFromString("0.00390625", &Scale);
    NumMultiply(&Reduced, &Scale, &Reduced);
    NumFromInt64(&Sum, 1);
    Term = Sum;
    for (Index = 1; Index < 200; Index++)
    {
        NumMultiply(&Term, &Reduced, &Term);
        DivideByInteger(&Term, Index, &Term);
        if (Term.Sign == 0 || Term.Exponent < SERIES_FLOOR)
            break;
        NumAdd(&Sum, &Term, &Sum);
    }
    for (Index = 0; Index < 8; Index++)
        NumMultiply(&Sum, &Sum, &Sum);
    if (Sum.Exponent + Tens > CALC_EXPONENT_LIMIT)
    {
        NumZero(Result);
        return CALC_OVERFLOW;
    }
    if (Sum.Exponent + Tens < -CALC_EXPONENT_LIMIT)
    {
        NumZero(Result);
        return CALC_OK;
    }
    Sum.Exponent += (int)Tens;
    Sum.Inexact = 1;
    *Result = Sum;
    return CALC_OK;
}

static void
LnNearOne(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER One, Upper, Lower, Ratio, Square, Term, Part, Sum;
    int Odd;

    NumFromInt64(&One, 1);
    NumSubtract(Value, &One, &Upper);
    NumAdd(Value, &One, &Lower);
    NumDivide(&Upper, &Lower, &Ratio);
    NumMultiply(&Ratio, &Ratio, &Square);
    Sum = Ratio;
    Term = Ratio;
    for (Odd = 3; Odd < 1000; Odd += 2)
    {
        NumMultiply(&Term, &Square, &Term);
        if (Term.Sign == 0 || Term.Exponent < SERIES_FLOOR + Ratio.Exponent)
            break;
        DivideByInteger(&Term, Odd, &Part);
        NumAdd(&Sum, &Part, &Sum);
    }
    MultiplyByInteger(&Sum, 2, Result);
}

CALC_STATUS
NumLn(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Mantissa = *Value, Low, High, Part, Sum;
    int Tens, Twos = 0;

    InitConstants();
    if (Value->Sign <= 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    NumFromString("0.7", &Low);
    NumFromString("1.4", &High);
    if (NumCompare(Value, &Low) >= 0 && NumCompare(Value, &High) < 0)
    {
        LnNearOne(Value, &Sum);
        Sum.Inexact = Sum.Sign != 0;
        *Result = Sum;
        return CALC_OK;
    }
    Tens = Mantissa.Exponent;
    Mantissa.Exponent = 0;
    while (NumCompare(&Mantissa, &Low) < 0)
    {
        MultiplyByInteger(&Mantissa, 2, &Mantissa);
        Twos++;
    }
    LnNearOne(&Mantissa, &Sum);
    MultiplyByInteger(&ConstLn2, Twos, &Part);
    NumSubtract(&Sum, &Part, &Sum);
    MultiplyByInteger(&ConstLn10, Tens, &Part);
    NumAdd(&Sum, &Part, &Sum);
    Sum.Inexact = 1;
    *Result = Sum;
    return CALC_OK;
}

CALC_STATUS
NumLog10(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Natural;
    CALC_STATUS Status;
    int Index, Power = Value->Sign > 0 && Value->Digits[0] == 1;

    for (Index = 1; Power && Index < CALC_DIGITS; Index++)
        Power = Value->Digits[Index] == 0;
    if (Power)
    {
        NumFromInt64(Result, Value->Exponent - 1);
        Result->Inexact = Value->Inexact;
        return CALC_OK;
    }
    Status = NumLn(Value, &Natural);
    if (Status != CALC_OK)
    {
        NumZero(Result);
        return Status;
    }
    return NumDivide(&Natural, &ConstLn10, Result);
}

static CALC_STATUS
PowerInteger(const CALC_NUMBER *Base, long long Exponent, CALC_NUMBER *Result)
{
    CALC_NUMBER Factor = *Base, Product, One;
    unsigned long long Count = Exponent < 0 ? (unsigned long long)(-(Exponent + 1)) + 1 : (unsigned long long)Exponent;
    CALC_STATUS Status = CALC_OK;

    NumFromInt64(&One, 1);
    Product = One;
    while (Count != 0 && Status == CALC_OK)
    {
        if (Count & 1)
            Status = NumMultiply(&Product, &Factor, &Product);
        Count >>= 1;
        if (Count != 0 && Status == CALC_OK)
            Status = NumMultiply(&Factor, &Factor, &Factor);
    }
    if (Exponent >= 0)
    {
        *Result = Product;
        return Status;
    }
    if (Status == CALC_OVERFLOW)
    {
        NumZero(Result);
        return CALC_OK;
    }
    if (Product.Sign == 0)
    {
        NumZero(Result);
        return CALC_OVERFLOW;
    }
    return NumDivide(&One, &Product, Result);
}

CALC_STATUS
NumPower(const CALC_NUMBER *Base, const CALC_NUMBER *Exponent, CALC_NUMBER *Result)
{
    CALC_NUMBER Natural;
    CALC_STATUS Status;
    long long Count;

    if (Exponent->Sign == 0)
    {
        NumFromInt64(Result, 1);
        return CALC_OK;
    }
    if (Base->Sign == 0)
    {
        NumZero(Result);
        return Exponent->Sign > 0 ? CALC_OK : CALC_DIVIDE_BY_ZERO;
    }
    if (NumIsInteger(Exponent) && NumToInt64(Exponent, &Count) && Count > -1000000000LL && Count < 1000000000LL)
        return PowerInteger(Base, Count, Result);
    if (Base->Sign < 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    Status = NumLn(Base, &Natural);
    if (Status == CALC_OK)
        Status = NumMultiply(&Natural, Exponent, &Natural);
    if (Status != CALC_OK)
    {
        NumZero(Result);
        return Status;
    }
    return NumExp(&Natural, Result);
}

CALC_STATUS
NumRoot(const CALC_NUMBER *Value, const CALC_NUMBER *Degree, CALC_NUMBER *Result)
{
    CALC_NUMBER Input = *Value, Inverse, One, Rounded, Check;
    CALC_STATUS Status;
    long long Count = 0;
    int Whole, Negative = 0, Index;

    if (Degree->Sign == 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    if (Input.Sign == 0)
    {
        NumZero(Result);
        return Degree->Sign > 0 ? CALC_OK : CALC_DIVIDE_BY_ZERO;
    }
    Whole = NumIsInteger(Degree) && NumToInt64(Degree, &Count);
    if (Input.Sign < 0)
    {
        if (!Whole || Count % 2 == 0)
        {
            NumZero(Result);
            return CALC_INVALID_INPUT;
        }
        Input.Sign = 1;
        Negative = 1;
    }
    NumFromInt64(&One, 1);
    NumDivide(&One, Degree, &Inverse);
    Status = NumPower(&Input, &Inverse, Result);
    if (Status != CALC_OK)
        return Status;
    if (Whole && Count > 1 && Count <= 64)
    {
        Rounded = *Result;
        for (Index = CALC_DIGITS - 12; Index < CALC_DIGITS; Index++)
            Rounded.Digits[Index] = 0;
        if (Result->Digits[CALC_DIGITS - 12] >= 5)
        {
            CALC_NUMBER Unit;

            NumFromInt64(&Unit, 1);
            Unit.Exponent = Rounded.Exponent - (CALC_DIGITS - 12) + 1;
            NumAdd(&Rounded, &Unit, &Rounded);
        }
        Rounded.Inexact = 0;
        if (PowerInteger(&Rounded, Count, &Check) == CALC_OK && !Check.Inexact && NumCompare(&Check, &Input) == 0)
        {
            Rounded.Inexact = Value->Inexact;
            *Result = Rounded;
        }
    }
    if (Negative)
        NumNegate(Result);
    return CALC_OK;
}

CALC_STATUS
NumModulo(const CALC_NUMBER *Left, const CALC_NUMBER *Right, CALC_NUMBER *Result)
{
    CALC_NUMBER A = *Left, B = *Right, Quotient, Product;
    CALC_STATUS Status;

    Status = NumDivide(&A, &B, &Quotient);
    if (Status != CALC_OK)
    {
        NumZero(Result);
        return Status;
    }
    if (Quotient.Exponent > CALC_DIGITS - 2)
    {
        NumZero(Result);
        return CALC_OK;
    }
    NumTruncate(&Quotient);
    NumMultiply(&Quotient, &B, &Product);
    NumSubtract(&A, &Product, Result);
    if (Result->Sign != 0 && Result->Sign != A.Sign)
    {
        B.Sign = A.Sign;
        NumAdd(Result, &B, Result);
    }
    return CALC_OK;
}

CALC_STATUS
NumFactorial(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Product;
    CALC_STATUS Status = CALC_OK;
    long long Count, Index;

    /* TODO: factorial of a non-integer needs the gamma function */
    if (Value->Sign < 0 || !NumIsInteger(Value))
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    if (!NumToInt64(Value, &Count) || Count > 4000)
    {
        NumZero(Result);
        return CALC_OVERFLOW;
    }
    NumFromInt64(&Product, 1);
    for (Index = 2; Index <= Count && Status == CALC_OK; Index++)
    {
        CALC_NUMBER Factor;

        NumFromInt64(&Factor, Index);
        Status = NumMultiply(&Product, &Factor, &Product);
    }
    *Result = Product;
    return Status;
}

static CALC_STATUS
SinCosRadians(const CALC_NUMBER *Value, CALC_NUMBER *Sine, CALC_NUMBER *Cosine)
{
    CALC_NUMBER Turns, Reduced, Square, Term, SinSum, CosSum, Swap;
    int Quadrant = 0, Index;

    InitConstants();
    if (Value->Sign != 0 && Value->Exponent > CALC_DIGITS / 2)
        return CALC_INVALID_INPUT;
    NumDivide(Value, &ConstHalfPi, &Turns);
    RoundToInteger(&Turns);
    if (Turns.Sign != 0)
    {
        int Tens = Turns.Exponent >= 2 && Turns.Exponent - 2 < CALC_DIGITS ? Turns.Digits[Turns.Exponent - 2] : 0;
        int Ones = Turns.Exponent >= 1 && Turns.Exponent - 1 < CALC_DIGITS ? Turns.Digits[Turns.Exponent - 1] : 0;

        Quadrant = (Tens * 10 + Ones) % 4;
        if (Turns.Sign < 0)
            Quadrant = (4 - Quadrant) % 4;
    }
    NumMultiply(&Turns, &ConstHalfPi, &Reduced);
    NumSubtract(Value, &Reduced, &Reduced);
    NumMultiply(&Reduced, &Reduced, &Square);
    SinSum = Reduced;
    Term = Reduced;
    for (Index = 1; Index < 100 && Term.Sign != 0; Index++)
    {
        NumMultiply(&Term, &Square, &Term);
        DivideByInteger(&Term, (long long)(2 * Index) * (2 * Index + 1), &Term);
        if (Term.Sign == 0 || Term.Exponent < SERIES_FLOOR + Reduced.Exponent)
            break;
        if (Index & 1)
            NumSubtract(&SinSum, &Term, &SinSum);
        else
            NumAdd(&SinSum, &Term, &SinSum);
    }
    NumFromInt64(&CosSum, 1);
    Term = CosSum;
    for (Index = 1; Index < 100; Index++)
    {
        NumMultiply(&Term, &Square, &Term);
        DivideByInteger(&Term, (long long)(2 * Index - 1) * (2 * Index), &Term);
        if (Term.Sign == 0 || Term.Exponent < SERIES_FLOOR)
            break;
        if (Index & 1)
            NumSubtract(&CosSum, &Term, &CosSum);
        else
            NumAdd(&CosSum, &Term, &CosSum);
    }
    if (Quadrant & 1)
    {
        Swap = SinSum;
        SinSum = CosSum;
        CosSum = Swap;
        NumNegate(&CosSum);
    }
    if (Quadrant & 2)
    {
        NumNegate(&SinSum);
        NumNegate(&CosSum);
    }
    SinSum.Inexact = SinSum.Sign != 0;
    CosSum.Inexact = CosSum.Sign != 0;
    *Sine = SinSum;
    *Cosine = CosSum;
    return CALC_OK;
}

static CALC_STATUS
SinCos(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Sine, CALC_NUMBER *Cosine)
{
    static const int ExactSine[4] = {0, 1, 0, -1};
    CALC_NUMBER Full, Quarter, Reduced, Steps, Radians;
    long long Step;

    InitConstants();
    if (Angle == CALC_ANGLE_RADIANS)
        return SinCosRadians(Value, Sine, Cosine);
    NumFromInt64(&Full, Angle == CALC_ANGLE_DEGREES ? 360 : 400);
    NumFromInt64(&Quarter, Angle == CALC_ANGLE_DEGREES ? 90 : 100);
    if (NumModulo(Value, &Full, &Reduced) != CALC_OK)
        return CALC_INVALID_INPUT;
    NumDivide(&Reduced, &Quarter, &Steps);
    if (NumIsInteger(&Steps) && NumToInt64(&Steps, &Step))
    {
        Step = ((Step % 4) + 4) % 4;
        NumFromInt64(Sine, ExactSine[Step]);
        NumFromInt64(Cosine, ExactSine[(Step + 1) % 4]);
        return CALC_OK;
    }
    NumMultiply(&Reduced, &ConstPi, &Radians);
    DivideByInteger(&Radians, Angle == CALC_ANGLE_DEGREES ? 180 : 200, &Radians);
    return SinCosRadians(&Radians, Sine, Cosine);
}

CALC_STATUS
NumSin(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result)
{
    CALC_NUMBER Sine, Cosine;
    CALC_STATUS Status = SinCos(Value, Angle, &Sine, &Cosine);

    if (Status == CALC_OK)
        *Result = Sine;
    else
        NumZero(Result);
    return Status;
}

CALC_STATUS
NumCos(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result)
{
    CALC_NUMBER Sine, Cosine;
    CALC_STATUS Status = SinCos(Value, Angle, &Sine, &Cosine);

    if (Status == CALC_OK)
        *Result = Cosine;
    else
        NumZero(Result);
    return Status;
}

CALC_STATUS
NumTan(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result)
{
    CALC_NUMBER Sine, Cosine;
    CALC_STATUS Status = SinCos(Value, Angle, &Sine, &Cosine);

    if (Status != CALC_OK || Cosine.Sign == 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    return NumDivide(&Sine, &Cosine, Result);
}

static void
AtanRadians(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Input = *Value, One, Work, Square, Term, Part, Sum;
    int Negative = Input.Sign < 0, Inverted = 0, Index;

    InitConstants();
    if (Input.Sign == 0)
    {
        NumZero(Result);
        return;
    }
    Input.Sign = 1;
    NumFromInt64(&One, 1);
    if (NumCompare(&Input, &One) > 0)
    {
        NumDivide(&One, &Input, &Input);
        Inverted = 1;
    }
    for (Index = 0; Index < 2; Index++)
    {
        NumMultiply(&Input, &Input, &Work);
        NumAdd(&Work, &One, &Work);
        NumSquareRoot(&Work, &Work);
        NumAdd(&Work, &One, &Work);
        NumDivide(&Input, &Work, &Input);
    }
    NumMultiply(&Input, &Input, &Square);
    Sum = Input;
    Term = Input;
    for (Index = 1; Index < 500; Index++)
    {
        NumMultiply(&Term, &Square, &Term);
        if (Term.Sign == 0 || Term.Exponent < SERIES_FLOOR + Input.Exponent)
            break;
        DivideByInteger(&Term, 2 * Index + 1, &Part);
        if (Index & 1)
            NumSubtract(&Sum, &Part, &Sum);
        else
            NumAdd(&Sum, &Part, &Sum);
    }
    MultiplyByInteger(&Sum, 4, &Sum);
    if (Inverted)
        NumSubtract(&ConstHalfPi, &Sum, &Sum);
    if (Negative)
        NumNegate(&Sum);
    Sum.Inexact = 1;
    *Result = Sum;
}

static CALC_STATUS
FromRadians(const CALC_NUMBER *Radians, CALC_ANGLE Angle, CALC_NUMBER *Result)
{
    CALC_NUMBER Scaled;

    if (Angle == CALC_ANGLE_RADIANS || Radians->Sign == 0)
    {
        *Result = *Radians;
        return CALC_OK;
    }
    MultiplyByInteger(Radians, Angle == CALC_ANGLE_DEGREES ? 180 : 200, &Scaled);
    return NumDivide(&Scaled, &ConstPi, Result);
}

CALC_STATUS
NumAtan(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result)
{
    CALC_NUMBER Radians;

    AtanRadians(Value, &Radians);
    return FromRadians(&Radians, Angle, Result);
}

CALC_STATUS
NumAsin(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result)
{
    CALC_NUMBER One, Square, Root, Ratio, Radians;

    InitConstants();
    NumFromInt64(&One, 1);
    NumMultiply(Value, Value, &Square);
    if (NumCompare(&Square, &One) > 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    NumSubtract(&One, &Square, &Root);
    NumSquareRoot(&Root, &Root);
    if (Root.Sign == 0)
    {
        if (Angle != CALC_ANGLE_RADIANS)
        {
            NumFromInt64(Result, (Angle == CALC_ANGLE_DEGREES ? 90 : 100) * Value->Sign);
            return CALC_OK;
        }
        Radians = ConstHalfPi;
        Radians.Sign = Value->Sign;
    }
    else
    {
        NumDivide(Value, &Root, &Ratio);
        AtanRadians(&Ratio, &Radians);
    }
    return FromRadians(&Radians, Angle, Result);
}

CALC_STATUS
NumAcos(const CALC_NUMBER *Value, CALC_ANGLE Angle, CALC_NUMBER *Result)
{
    CALC_NUMBER Quarter, Part;
    CALC_STATUS Status;

    InitConstants();
    Status = NumAsin(Value, Angle, &Part);
    if (Status != CALC_OK)
    {
        NumZero(Result);
        return Status;
    }
    if (Angle == CALC_ANGLE_RADIANS)
        Quarter = ConstHalfPi;
    else
        NumFromInt64(&Quarter, Angle == CALC_ANGLE_DEGREES ? 90 : 100);
    return NumSubtract(&Quarter, &Part, Result);
}

static void
SinhSeries(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Square, Term = *Value, Sum = *Value;
    int Index;

    NumMultiply(Value, Value, &Square);
    for (Index = 1; Index < 100; Index++)
    {
        NumMultiply(&Term, &Square, &Term);
        DivideByInteger(&Term, (long long)(2 * Index) * (2 * Index + 1), &Term);
        if (Term.Sign == 0 || Term.Exponent < SERIES_FLOOR + Value->Exponent)
            break;
        NumAdd(&Sum, &Term, &Sum);
    }
    Sum.Inexact = 1;
    *Result = Sum;
}

static CALC_STATUS
HyperbolicPair(const CALC_NUMBER *Value, CALC_NUMBER *Sine, CALC_NUMBER *Cosine)
{
    CALC_NUMBER Rising, Falling, One;
    CALC_STATUS Status;

    Status = NumExp(Value, &Rising);
    if (Status != CALC_OK)
        return Status;
    NumFromInt64(&One, 1);
    if (Rising.Sign == 0)
        return CALC_OVERFLOW;
    NumDivide(&One, &Rising, &Falling);
    NumAdd(&Rising, &Falling, Cosine);
    DivideByInteger(Cosine, 2, Cosine);
    if (Value->Sign == 0 || Value->Exponent > 0)
    {
        NumSubtract(&Rising, &Falling, Sine);
        DivideByInteger(Sine, 2, Sine);
    }
    else
    {
        SinhSeries(Value, Sine);
    }
    return CALC_OK;
}

CALC_STATUS
NumSinh(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Sine, Cosine;
    CALC_STATUS Status = HyperbolicPair(Value, &Sine, &Cosine);

    if (Status == CALC_OK)
        *Result = Sine;
    else
        NumZero(Result);
    return Status;
}

CALC_STATUS
NumCosh(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Sine, Cosine;
    CALC_STATUS Status = HyperbolicPair(Value, &Sine, &Cosine);

    if (Status == CALC_OK)
        *Result = Cosine;
    else
        NumZero(Result);
    return Status;
}

CALC_STATUS
NumTanh(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Sine, Cosine;
    CALC_STATUS Status;

    if (Value->Sign != 0 && Value->Exponent > 3)
    {
        NumFromInt64(Result, Value->Sign);
        Result->Inexact = 1;
        return CALC_OK;
    }
    Status = HyperbolicPair(Value, &Sine, &Cosine);
    if (Status != CALC_OK)
    {
        NumZero(Result);
        return Status;
    }
    return NumDivide(&Sine, &Cosine, Result);
}

static int
IsTiny(const CALC_NUMBER *Value)
{
    return Value->Sign != 0 && Value->Exponent < -15;
}

static void
TinyCubic(const CALC_NUMBER *Value, int Divisor, int Add, CALC_NUMBER *Result)
{
    CALC_NUMBER Cube;

    NumMultiply(Value, Value, &Cube);
    NumMultiply(&Cube, Value, &Cube);
    DivideByInteger(&Cube, Divisor, &Cube);
    if (Add)
        NumAdd(Value, &Cube, Result);
    else
        NumSubtract(Value, &Cube, Result);
    Result->Inexact = 1;
}

CALC_STATUS
NumAsinh(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER Input = *Value, One, Work;
    CALC_STATUS Status;
    int Negative = Input.Sign < 0;

    if (Input.Sign == 0)
    {
        NumZero(Result);
        return CALC_OK;
    }
    if (IsTiny(&Input))
    {
        TinyCubic(&Input, 6, 0, Result);
        return CALC_OK;
    }
    Input.Sign = 1;
    NumFromInt64(&One, 1);
    Status = NumMultiply(&Input, &Input, &Work);
    if (Status == CALC_OK)
    {
        NumAdd(&Work, &One, &Work);
        NumSquareRoot(&Work, &Work);
        NumAdd(&Work, &Input, &Work);
    }
    else
    {
        MultiplyByInteger(&Input, 2, &Work);
    }
    Status = NumLn(&Work, Result);
    if (Negative)
        NumNegate(Result);
    return Status;
}

CALC_STATUS
NumAcosh(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER One, Work;
    CALC_STATUS Status;

    NumFromInt64(&One, 1);
    if (NumCompare(Value, &One) < 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    Status = NumMultiply(Value, Value, &Work);
    if (Status == CALC_OK)
    {
        NumSubtract(&Work, &One, &Work);
        NumSquareRoot(&Work, &Work);
        NumAdd(&Work, Value, &Work);
    }
    else
    {
        MultiplyByInteger(Value, 2, &Work);
    }
    return NumLn(&Work, Result);
}

CALC_STATUS
NumAtanh(const CALC_NUMBER *Value, CALC_NUMBER *Result)
{
    CALC_NUMBER One, Upper, Lower, Ratio;
    CALC_STATUS Status;

    if (Value->Sign == 0)
    {
        NumZero(Result);
        return CALC_OK;
    }
    if (IsTiny(Value))
    {
        TinyCubic(Value, 3, 1, Result);
        return CALC_OK;
    }
    NumFromInt64(&One, 1);
    NumAdd(&One, Value, &Upper);
    NumSubtract(&One, Value, &Lower);
    if (Upper.Sign <= 0 || Lower.Sign <= 0)
    {
        NumZero(Result);
        return CALC_INVALID_INPUT;
    }
    NumDivide(&Upper, &Lower, &Ratio);
    Status = NumLn(&Ratio, Result);
    if (Status == CALC_OK)
        DivideByInteger(Result, 2, Result);
    return Status;
}

CALC_STATUS
NumFromString(const char *Text, CALC_NUMBER *Result)
{
    int Digits[PARSE_DIGITS];
    int Count = 0, Whole = 0, Seen = 0, Point = 0, Sign = 1, Dropped = 0;
    long Exponent = 0;

    NumZero(Result);
    while (*Text == ' ')
        Text++;
    if (*Text == '+' || *Text == '-')
    {
        if (*Text == '-')
            Sign = -1;
        Text++;
    }
    for (;; Text++)
    {
        if (*Text >= '0' && *Text <= '9')
        {
            Seen = 1;
            if (Count < PARSE_DIGITS)
            {
                Digits[Count++] = *Text - '0';
                if (!Point)
                    Whole++;
            }
            else
            {
                if (!Point)
                    Exponent++;
                if (*Text != '0')
                    Dropped = 1;
            }
        }
        else if (*Text == '.' && !Point)
        {
            Point = 1;
        }
        else
        {
            break;
        }
    }
    if (!Seen)
        return CALC_INVALID_INPUT;
    if (*Text == 'e' || *Text == 'E')
    {
        long Power = 0;
        int Negative = 0, Any = 0;

        Text++;
        if (*Text == '+' || *Text == '-')
        {
            Negative = *Text == '-';
            Text++;
        }
        for (; *Text >= '0' && *Text <= '9'; Text++)
        {
            Any = 1;
            if (Power < 100000)
                Power = Power * 10 + (*Text - '0');
        }
        if (!Any)
            return CALC_INVALID_INPUT;
        Exponent += Negative ? -Power : Power;
    }
    while (*Text == ' ')
        Text++;
    if (*Text != 0)
        return CALC_INVALID_INPUT;
    return NumSet(Result, Sign, Whole + (int)Exponent, Digits, Count, Dropped);
}

static int
RoundDigits(const CALC_NUMBER *Value, int Keep, int *Digits, int *Exponent)
{
    int Index, Count = Keep;

    *Exponent = Value->Exponent;
    for (Index = 0; Index < Keep; Index++)
        Digits[Index] = Index < CALC_DIGITS ? Value->Digits[Index] : 0;
    if (Keep < CALC_DIGITS && Value->Digits[Keep] >= 5)
    {
        Index = Keep - 1;
        while (Index >= 0 && Digits[Index] == 9)
            Digits[Index--] = 0;
        if (Index >= 0)
        {
            Digits[Index]++;
        }
        else
        {
            Digits[0] = 1;
            (*Exponent)++;
        }
    }
    while (Count > 1 && Digits[Count - 1] == 0)
        Count--;
    return Count;
}

void
NumToString(const CALC_NUMBER *Value, int Precision, int Exponential, char *Buffer, size_t Size)
{
    char Text[CALC_DIGITS + 32];
    int Digits[CALC_DIGITS + 1];
    int Count, Exponent, Index, Length = 0;

    if (Size == 0)
        return;
    if (Precision < 1)
        Precision = 1;
    if (Precision > CALC_DIGITS)
        Precision = CALC_DIGITS;
    if (Value->Sign == 0)
    {
        Text[Length++] = '0';
        Text[Length] = 0;
        strncpy(Buffer, Text, Size - 1);
        Buffer[Size - 1] = 0;
        return;
    }
    Count = RoundDigits(Value, Precision, Digits, &Exponent);
    if (!Exponential && Exponent <= 0 && Count - Exponent > Precision)
    {
        if (-Exponent <= 2)
        {
            Count = RoundDigits(Value, Precision + Exponent, Digits, &Exponent);
            if (Exponent <= 0 && Count - Exponent > Precision)
                Count = RoundDigits(Value, Precision + Exponent, Digits, &Exponent);
        }
        else
        {
            Exponential = 1;
        }
    }
    if (Exponent > Precision)
        Exponential = 1;
    if (Value->Sign < 0)
        Text[Length++] = '-';
    if (Exponential)
    {
        int Power = Exponent - 1;

        Text[Length++] = (char)('0' + Digits[0]);
        Text[Length++] = '.';
        for (Index = 1; Index < Count; Index++)
            Text[Length++] = (char)('0' + Digits[Index]);
        Text[Length++] = 'e';
        Text[Length++] = Power < 0 ? '-' : '+';
        if (Power < 0)
            Power = -Power;
        for (Index = 10000; Index > 1 && Power < Index; Index /= 10)
        {
        }
        for (; Index > 0; Index /= 10)
            Text[Length++] = (char)('0' + (Power / Index) % 10);
    }
    else if (Exponent <= 0)
    {
        Text[Length++] = '0';
        Text[Length++] = '.';
        for (Index = 0; Index < -Exponent; Index++)
            Text[Length++] = '0';
        for (Index = 0; Index < Count; Index++)
            Text[Length++] = (char)('0' + Digits[Index]);
    }
    else
    {
        for (Index = 0; Index < Exponent; Index++)
            Text[Length++] = (char)('0' + (Index < Count ? Digits[Index] : 0));
        if (Count > Exponent)
        {
            Text[Length++] = '.';
            for (Index = Exponent; Index < Count; Index++)
                Text[Length++] = (char)('0' + Digits[Index]);
        }
    }
    Text[Length] = 0;
    strncpy(Buffer, Text, Size - 1);
    Buffer[Size - 1] = 0;
}
