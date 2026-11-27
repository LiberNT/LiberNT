/*
 * PROJECT:     LiberNT Calculator
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Calculator key handling: entry, pending operations, memory and number bases
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "calcengine.h"

#include <limits.h>
#include <string.h>

#define STANDARD_PRECISION 16
#define SCIENTIFIC_PRECISION 32
#define EXPONENT_DIGITS 4

static void
Append(char *Buffer, size_t Size, const char *Text)
{
    size_t Used = strlen(Buffer);
    size_t Length = strlen(Text);

    if (Used + 1 >= Size)
        return;
    if (Length > Size - Used - 1)
        Length = Size - Used - 1;
    memcpy(Buffer + Used, Text, Length);
    Buffer[Used + Length] = 0;
}

static void
Copy(char *Buffer, size_t Size, const char *Text)
{
    if (Size == 0)
        return;
    Buffer[0] = 0;
    Append(Buffer, Size, Text);
}

static int
Precision(const CALC_ENGINE *Engine)
{
    return Engine->Mode == CALC_MODE_STANDARD ? STANDARD_PRECISION : SCIENTIFIC_PRECISION;
}

static long long
WrapWord(const CALC_ENGINE *Engine, unsigned long long Bits)
{
    if (Engine->WordBits < 64)
    {
        unsigned long long Mask = (1ULL << Engine->WordBits) - 1;

        Bits &= Mask;
        if (Bits & (1ULL << (Engine->WordBits - 1)))
            Bits |= ~Mask;
    }
    return (long long)Bits;
}

static long long
IntegerValue(const CALC_NUMBER *Value)
{
    long long Result = 0;

    NumToInt64(Value, &Result);
    return Result;
}

static void
FormatInteger(const CALC_ENGINE *Engine, long long Value, int Base, char *Buffer, size_t Size)
{
    static const char Symbols[] = "0123456789ABCDEF";
    char Text[72];
    unsigned long long Bits = (unsigned long long)Value;
    int Position = sizeof(Text) - 1, Negative = 0;

    Text[Position] = 0;
    if (Base == 10)
    {
        Negative = Value < 0;
        if (Negative)
            Bits = 0 - Bits;
    }
    else if (Engine->WordBits < 64)
    {
        Bits &= (1ULL << Engine->WordBits) - 1;
    }
    do
    {
        Text[--Position] = Symbols[Bits % (unsigned)Base];
        Bits /= (unsigned)Base;
    } while (Bits != 0);
    if (Negative)
        Text[--Position] = '-';
    Copy(Buffer, Size, Text + Position);
}

static int
ParseInteger(const CALC_ENGINE *Engine, const char *Text, long long *Result)
{
    unsigned long long Bits = 0, Limit;
    int Negative = 0, Any = 0;

    if (Engine->Base == 10 && *Text == '-')
    {
        Negative = 1;
        Text++;
    }
    if (Engine->Base == 10)
        Limit = (1ULL << (Engine->WordBits - 1)) - (Negative ? 0 : 1);
    else
        Limit = Engine->WordBits < 64 ? (1ULL << Engine->WordBits) - 1 : ULLONG_MAX;
    for (; *Text != 0; Text++)
    {
        unsigned Digit;

        if (*Text >= '0' && *Text <= '9')
            Digit = (unsigned)(*Text - '0');
        else if (*Text >= 'A' && *Text <= 'F')
            Digit = (unsigned)(*Text - 'A' + 10);
        else if (*Text >= 'a' && *Text <= 'f')
            Digit = (unsigned)(*Text - 'a' + 10);
        else
            return 0;
        if (Digit >= (unsigned)Engine->Base)
            return 0;
        if (Bits > (Limit - Digit) / (unsigned)Engine->Base)
            return 0;
        Bits = Bits * (unsigned)Engine->Base + Digit;
        Any = 1;
    }
    if (!Any)
        return 0;
    if (Negative)
        Bits = 0 - Bits;
    *Result = WrapWord(Engine, Bits);
    return 1;
}

static void
NumberText(const CALC_ENGINE *Engine, const CALC_NUMBER *Value, char *Buffer, size_t Size)
{
    if (Engine->Mode == CALC_MODE_PROGRAMMER)
        FormatInteger(Engine, IntegerValue(Value), Engine->Base, Buffer, Size);
    else
        NumToString(Value, Precision(Engine), Engine->Mode == CALC_MODE_SCIENTIFIC && Engine->Exponential, Buffer, Size);
}

void
CalcDisplay(const CALC_ENGINE *Engine, char *Buffer, size_t Size)
{
    if (Engine->Entering)
        Copy(Buffer, Size, Engine->Entry);
    else
        NumberText(Engine, &Engine->Value, Buffer, Size);
}

void
CalcDisplayBase(const CALC_ENGINE *Engine, int Base, char *Buffer, size_t Size)
{
    FormatInteger(Engine, IntegerValue(&Engine->Value), Base, Buffer, Size);
}

void
CalcExpression(const CALC_ENGINE *Engine, char *Buffer, size_t Size)
{
    Copy(Buffer, Size, Engine->Expression);
    if (Engine->Operand[0] != 0 && !Engine->OperandWritten)
        Append(Buffer, Size, Engine->Operand);
}

static void
Reset(CALC_ENGINE *Engine)
{
    NumZero(&Engine->Value);
    Engine->Entry[0] = 0;
    Engine->Operand[0] = 0;
    Engine->Expression[0] = 0;
    Engine->Entering = 0;
    Engine->HaveOperand = 0;
    Engine->OperandWritten = 0;
    Engine->OperandStart = 0;
    Engine->Evaluated = 0;
    Engine->Depth = 0;
    Engine->CanRepeat = 0;
    Engine->Error = CALC_OK;
}

void
CalcInit(CALC_ENGINE *Engine)
{
    memset(Engine, 0, sizeof(*Engine));
    Engine->Mode = CALC_MODE_STANDARD;
    Engine->Angle = CALC_ANGLE_DEGREES;
    Engine->Base = 10;
    Engine->WordBits = 64;
    Reset(Engine);
}

static void
Fail(CALC_ENGINE *Engine, CALC_STATUS Status)
{
    NumZero(&Engine->Value);
    Engine->Error = Status;
    Engine->Entering = 0;
    Engine->HaveOperand = 0;
    Engine->OperandWritten = 0;
    Engine->Operand[0] = 0;
    Engine->Depth = 0;
    Engine->CanRepeat = 0;
    Engine->Evaluated = 1;
}

static void
DropOperand(CALC_ENGINE *Engine)
{
    if (Engine->Evaluated)
    {
        Engine->Expression[0] = 0;
        Engine->Evaluated = 0;
    }
    if (Engine->OperandWritten)
    {
        Engine->Expression[Engine->OperandStart] = 0;
        Engine->OperandWritten = 0;
    }
    Engine->Operand[0] = 0;
}

static void
SetOperand(CALC_ENGINE *Engine, const CALC_NUMBER *Value)
{
    DropOperand(Engine);
    Engine->Value = *Value;
    Engine->Entering = 0;
    Engine->HaveOperand = 1;
}

static void
StartEntry(CALC_ENGINE *Engine)
{
    if (Engine->Entering)
        return;
    DropOperand(Engine);
    Copy(Engine->Entry, sizeof(Engine->Entry), "0");
    NumZero(&Engine->Value);
    Engine->Entering = 1;
    Engine->HaveOperand = 1;
}

static void
OperandText(const CALC_ENGINE *Engine, char *Buffer, size_t Size)
{
    if (Engine->Operand[0] != 0)
        Copy(Buffer, Size, Engine->Operand);
    else
        NumberText(Engine, &Engine->Value, Buffer, Size);
}

static void
WriteOperand(CALC_ENGINE *Engine)
{
    char Text[CALC_TEXT_LENGTH];

    if (Engine->OperandWritten)
        return;
    OperandText(Engine, Text, sizeof(Text));
    Append(Engine->Expression, sizeof(Engine->Expression), Text);
}

static void
PressProgrammerDigit(CALC_ENGINE *Engine, int Digit)
{
    static const char Symbols[] = "0123456789ABCDEF";
    char Candidate[CALC_TEXT_LENGTH];
    char Symbol[2] = {Symbols[Digit], 0};
    long long Value;

    if (Digit >= Engine->Base)
        return;
    StartEntry(Engine);
    if (strcmp(Engine->Entry, "0") == 0)
        Candidate[0] = 0;
    else if (strcmp(Engine->Entry, "-0") == 0)
        Copy(Candidate, sizeof(Candidate), "-");
    else
        Copy(Candidate, sizeof(Candidate), Engine->Entry);
    Append(Candidate, sizeof(Candidate), Symbol);
    if (!ParseInteger(Engine, Candidate, &Value))
        return;
    Copy(Engine->Entry, sizeof(Engine->Entry), Candidate);
    NumFromInt64(&Engine->Value, Value);
}

static void
PressDigit(CALC_ENGINE *Engine, int Digit)
{
    char Symbol[2] = {(char)('0' + Digit), 0};
    char *Mark;

    if (Engine->Mode == CALC_MODE_PROGRAMMER)
    {
        PressProgrammerDigit(Engine, Digit);
        return;
    }
    if (Digit > 9)
        return;
    StartEntry(Engine);
    Mark = strchr(Engine->Entry, 'e');
    if (Mark != NULL)
    {
        char *Power = Mark + 2;

        if (strcmp(Power, "0") == 0)
            Power[0] = Symbol[0];
        else if (strlen(Power) < EXPONENT_DIGITS)
            Append(Engine->Entry, sizeof(Engine->Entry), Symbol);
    }
    else if (strcmp(Engine->Entry, "0") == 0)
    {
        Engine->Entry[0] = Symbol[0];
    }
    else if (strcmp(Engine->Entry, "-0") == 0)
    {
        Engine->Entry[1] = Symbol[0];
    }
    else
    {
        const char *Text = Engine->Entry;
        int Count = 0;

        if (*Text == '-')
            Text++;
        if (Text[0] == '0' && Text[1] == '.')
            Text++;
        for (; *Text != 0; Text++)
            Count += *Text != '.';
        if (Count < Precision(Engine))
            Append(Engine->Entry, sizeof(Engine->Entry), Symbol);
    }
    NumFromString(Engine->Entry, &Engine->Value);
}

static void
PressPoint(CALC_ENGINE *Engine)
{
    if (Engine->Mode == CALC_MODE_PROGRAMMER)
        return;
    StartEntry(Engine);
    if (strchr(Engine->Entry, '.') == NULL && strchr(Engine->Entry, 'e') == NULL)
        Append(Engine->Entry, sizeof(Engine->Entry), ".");
}

static void
PressExponent(CALC_ENGINE *Engine)
{
    if (Engine->Mode != CALC_MODE_SCIENTIFIC || !Engine->Entering)
        return;
    if (strchr(Engine->Entry, 'e') != NULL || NumIsZero(&Engine->Value))
        return;
    Append(Engine->Entry, sizeof(Engine->Entry), "e+0");
}

static void
PressBack(CALC_ENGINE *Engine)
{
    size_t Length;
    char *Mark;

    if (!Engine->Entering)
        return;
    Mark = strchr(Engine->Entry, 'e');
    Length = strlen(Engine->Entry);
    if (Mark != NULL)
    {
        char *Power = Mark + 2;

        if (strlen(Power) > 1)
            Engine->Entry[Length - 1] = 0;
        else if (Power[0] != '0')
            Power[0] = '0';
        else
            *Mark = 0;
    }
    else
    {
        Engine->Entry[Length - 1] = 0;
        if (Engine->Entry[0] == 0 || strcmp(Engine->Entry, "-") == 0)
            Copy(Engine->Entry, sizeof(Engine->Entry), "0");
    }
    if (Engine->Mode == CALC_MODE_PROGRAMMER)
    {
        long long Value = 0;

        ParseInteger(Engine, Engine->Entry, &Value);
        NumFromInt64(&Engine->Value, Value);
    }
    else
    {
        NumFromString(Engine->Entry, &Engine->Value);
    }
}

static void
WrapOperand(CALC_ENGINE *Engine, const char *Prefix)
{
    char Previous[CALC_TEXT_LENGTH];

    if (Engine->Evaluated)
    {
        Engine->Expression[0] = 0;
        Engine->Evaluated = 0;
    }
    if (Engine->OperandWritten)
    {
        Copy(Previous, sizeof(Previous), Engine->Expression + Engine->OperandStart);
        Engine->Expression[Engine->OperandStart] = 0;
        Engine->OperandWritten = 0;
    }
    else
    {
        OperandText(Engine, Previous, sizeof(Previous));
    }
    Copy(Engine->Operand, sizeof(Engine->Operand), Prefix);
    Append(Engine->Operand, sizeof(Engine->Operand), Previous);
    Append(Engine->Operand, sizeof(Engine->Operand), ")");
}

static void
PressSign(CALC_ENGINE *Engine)
{
    if (Engine->Entering && Engine->Mode != CALC_MODE_PROGRAMMER)
    {
        char *Mark = strchr(Engine->Entry, 'e');

        if (Mark != NULL)
        {
            Mark[1] = Mark[1] == '-' ? '+' : '-';
        }
        else if (Engine->Entry[0] == '-')
        {
            memmove(Engine->Entry, Engine->Entry + 1, strlen(Engine->Entry));
        }
        else if (!NumIsZero(&Engine->Value) && strlen(Engine->Entry) + 1 < sizeof(Engine->Entry))
        {
            memmove(Engine->Entry + 1, Engine->Entry, strlen(Engine->Entry) + 1);
            Engine->Entry[0] = '-';
        }
        NumFromString(Engine->Entry, &Engine->Value);
        return;
    }
    if (NumIsZero(&Engine->Value))
        return;
    WrapOperand(Engine, "negate(");
    if (Engine->Mode == CALC_MODE_PROGRAMMER)
        NumFromInt64(&Engine->Value, WrapWord(Engine, 0 - (unsigned long long)IntegerValue(&Engine->Value)));
    else
        NumNegate(&Engine->Value);
    Engine->Entering = 0;
    Engine->HaveOperand = 1;
}

static int
OperatorPrecedence(const CALC_ENGINE *Engine, CALC_KEY Operator)
{
    if (Engine->Mode == CALC_MODE_STANDARD)
        return 1;
    switch (Operator)
    {
        case CALC_KEY_OR:
        case CALC_KEY_XOR:
            return 0;
        case CALC_KEY_AND:
            return 1;
        case CALC_KEY_ADD:
        case CALC_KEY_SUBTRACT:
            return 2;
        case CALC_KEY_POWER:
        case CALC_KEY_ROOT:
            return 4;
        default:
            return 3;
    }
}

static const char *
OperatorText(CALC_KEY Operator)
{
    switch (Operator)
    {
        case CALC_KEY_ADD:
            return " + ";
        case CALC_KEY_SUBTRACT:
            return " - ";
        case CALC_KEY_MULTIPLY:
            return " \xC3\x97 ";
        case CALC_KEY_DIVIDE:
            return " \xC3\xB7 ";
        case CALC_KEY_MODULO:
            return " mod ";
        case CALC_KEY_POWER:
            return " ^ ";
        case CALC_KEY_ROOT:
            return " yroot ";
        case CALC_KEY_AND:
            return " AND ";
        case CALC_KEY_OR:
            return " OR ";
        case CALC_KEY_XOR:
            return " XOR ";
        case CALC_KEY_SHIFT_LEFT:
            return " Lsh ";
        case CALC_KEY_SHIFT_RIGHT:
            return " Rsh ";
        default:
            return " ";
    }
}

static CALC_STATUS
ApplyInteger(const CALC_ENGINE *Engine, CALC_KEY Operator, const CALC_NUMBER *Left, const CALC_NUMBER *Right,
             CALC_NUMBER *Result)
{
    long long A = IntegerValue(Left), B = IntegerValue(Right);
    unsigned long long Wide = (unsigned long long)A, Other = (unsigned long long)B, Bits = 0;

    switch (Operator)
    {
        case CALC_KEY_ADD:
            Bits = Wide + Other;
            break;
        case CALC_KEY_SUBTRACT:
            Bits = Wide - Other;
            break;
        case CALC_KEY_MULTIPLY:
            Bits = Wide * Other;
            break;
        case CALC_KEY_DIVIDE:
        case CALC_KEY_MODULO:
            if (B == 0)
                return A == 0 ? CALC_UNDEFINED : CALC_DIVIDE_BY_ZERO;
            if (A == LLONG_MIN && B == -1)
                Bits = Operator == CALC_KEY_DIVIDE ? Wide : 0;
            else
                Bits = (unsigned long long)(Operator == CALC_KEY_DIVIDE ? A / B : A % B);
            break;
        case CALC_KEY_AND:
            Bits = Wide & Other;
            break;
        case CALC_KEY_OR:
            Bits = Wide | Other;
            break;
        case CALC_KEY_XOR:
            Bits = Wide ^ Other;
            break;
        case CALC_KEY_SHIFT_LEFT:
            Bits = B >= 0 && B < Engine->WordBits ? Wide << B : 0;
            break;
        case CALC_KEY_SHIFT_RIGHT:
            if (B < 0)
                Bits = 0;
            else if (B >= 63)
                Bits = A < 0 ? ~0ULL : 0;
            else
                Bits = A < 0 ? ~(~Wide >> B) : Wide >> B;
            break;
        default:
            return CALC_INVALID_INPUT;
    }
    NumFromInt64(Result, WrapWord(Engine, Bits));
    return CALC_OK;
}

static CALC_STATUS
ApplyBinary(const CALC_ENGINE *Engine, CALC_KEY Operator, const CALC_NUMBER *Left, const CALC_NUMBER *Right,
            CALC_NUMBER *Result)
{
    if (Engine->Mode == CALC_MODE_PROGRAMMER)
        return ApplyInteger(Engine, Operator, Left, Right, Result);
    switch (Operator)
    {
        case CALC_KEY_ADD:
            return NumAdd(Left, Right, Result);
        case CALC_KEY_SUBTRACT:
            return NumSubtract(Left, Right, Result);
        case CALC_KEY_MULTIPLY:
            return NumMultiply(Left, Right, Result);
        case CALC_KEY_DIVIDE:
            return NumDivide(Left, Right, Result);
        case CALC_KEY_MODULO:
            return NumModulo(Left, Right, Result);
        case CALC_KEY_POWER:
            return NumPower(Left, Right, Result);
        case CALC_KEY_ROOT:
            return NumRoot(Left, Right, Result);
        default:
            return CALC_INVALID_INPUT;
    }
}

static int
IsOperator(CALC_KEY Key)
{
    return Key >= CALC_KEY_ADD && Key <= CALC_KEY_SHIFT_RIGHT;
}

static int
OperatorAllowed(const CALC_ENGINE *Engine, CALC_KEY Key)
{
    if (Key >= CALC_KEY_AND && Key <= CALC_KEY_SHIFT_RIGHT)
        return Engine->Mode == CALC_MODE_PROGRAMMER;
    if (Key == CALC_KEY_POWER || Key == CALC_KEY_ROOT)
        return Engine->Mode == CALC_MODE_SCIENTIFIC;
    if (Key == CALC_KEY_MODULO)
        return Engine->Mode != CALC_MODE_STANDARD;
    return 1;
}

static void
PressOperator(CALC_ENGINE *Engine, CALC_KEY Key)
{
    CALC_STATUS Status = CALC_OK;

    if (!Engine->HaveOperand && Engine->Depth > 0 && Engine->Stack[Engine->Depth - 1].Operator != CALC_KEY_OPEN)
    {
        Engine->Depth--;
        Engine->Value = Engine->Stack[Engine->Depth].Value;
        Engine->Expression[Engine->Stack[Engine->Depth].Mark] = 0;
    }
    else
    {
        if (Engine->Evaluated)
        {
            Engine->Expression[0] = 0;
            Engine->Evaluated = 0;
        }
        WriteOperand(Engine);
    }
    while (Engine->Depth > 0 && Engine->Stack[Engine->Depth - 1].Operator != CALC_KEY_OPEN &&
           OperatorPrecedence(Engine, Engine->Stack[Engine->Depth - 1].Operator) >= OperatorPrecedence(Engine, Key))
    {
        Engine->Depth--;
        Status = ApplyBinary(Engine, Engine->Stack[Engine->Depth].Operator, &Engine->Stack[Engine->Depth].Value,
                             &Engine->Value, &Engine->Value);
        if (Status != CALC_OK)
        {
            Fail(Engine, Status);
            return;
        }
    }
    if (Engine->Depth == CALC_STACK_DEPTH)
    {
        Fail(Engine, CALC_INVALID_INPUT);
        return;
    }
    Engine->Stack[Engine->Depth].Value = Engine->Value;
    Engine->Stack[Engine->Depth].Operator = Key;
    Engine->Stack[Engine->Depth].Mark = (int)strlen(Engine->Expression);
    Engine->Depth++;
    Append(Engine->Expression, sizeof(Engine->Expression), OperatorText(Key));
    Engine->Entering = 0;
    Engine->HaveOperand = 0;
    Engine->OperandWritten = 0;
    Engine->Operand[0] = 0;
}

static void
PressEquals(CALC_ENGINE *Engine)
{
    CALC_STATUS Status = CALC_OK;
    char Text[CALC_TEXT_LENGTH];

    if (Engine->Depth > 0)
    {
        WriteOperand(Engine);
        if (Engine->Stack[Engine->Depth - 1].Operator != CALC_KEY_OPEN)
        {
            Engine->LastOperator = Engine->Stack[Engine->Depth - 1].Operator;
            Engine->LastOperand = Engine->Value;
            Engine->CanRepeat = 1;
        }
        while (Engine->Depth > 0 && Status == CALC_OK)
        {
            Engine->Depth--;
            if (Engine->Stack[Engine->Depth].Operator == CALC_KEY_OPEN)
            {
                Append(Engine->Expression, sizeof(Engine->Expression), ")");
                continue;
            }
            Status = ApplyBinary(Engine, Engine->Stack[Engine->Depth].Operator, &Engine->Stack[Engine->Depth].Value,
                                 &Engine->Value, &Engine->Value);
        }
    }
    else
    {
        if (Engine->Evaluated)
        {
            Engine->Expression[0] = 0;
            Engine->Evaluated = 0;
        }
        WriteOperand(Engine);
        if (Engine->CanRepeat)
        {
            Append(Engine->Expression, sizeof(Engine->Expression), OperatorText(Engine->LastOperator));
            NumberText(Engine, &Engine->LastOperand, Text, sizeof(Text));
            Append(Engine->Expression, sizeof(Engine->Expression), Text);
            Status = ApplyBinary(Engine, Engine->LastOperator, &Engine->Value, &Engine->LastOperand, &Engine->Value);
        }
    }
    Append(Engine->Expression, sizeof(Engine->Expression), " =");
    if (Status != CALC_OK)
    {
        Fail(Engine, Status);
        return;
    }
    Engine->Depth = 0;
    Engine->Entering = 0;
    Engine->HaveOperand = 1;
    Engine->OperandWritten = 0;
    Engine->Operand[0] = 0;
    Engine->Evaluated = 1;
}

static void
PressOpen(CALC_ENGINE *Engine)
{
    if (Engine->Mode == CALC_MODE_STANDARD || Engine->Depth == CALC_STACK_DEPTH)
        return;
    DropOperand(Engine);
    NumZero(&Engine->Stack[Engine->Depth].Value);
    Engine->Stack[Engine->Depth].Operator = CALC_KEY_OPEN;
    Engine->Stack[Engine->Depth].Mark = (int)strlen(Engine->Expression);
    Engine->Depth++;
    Append(Engine->Expression, sizeof(Engine->Expression), "(");
    NumZero(&Engine->Value);
    Engine->Entering = 0;
    Engine->HaveOperand = 0;
}

static void
PressClose(CALC_ENGINE *Engine)
{
    CALC_STATUS Status = CALC_OK;
    int Index, Open = 0;

    for (Index = 0; Index < Engine->Depth; Index++)
        Open |= Engine->Stack[Index].Operator == CALC_KEY_OPEN;
    if (!Open)
        return;
    WriteOperand(Engine);
    while (Engine->Stack[Engine->Depth - 1].Operator != CALC_KEY_OPEN)
    {
        Engine->Depth--;
        Status = ApplyBinary(Engine, Engine->Stack[Engine->Depth].Operator, &Engine->Stack[Engine->Depth].Value,
                             &Engine->Value, &Engine->Value);
        if (Status != CALC_OK)
        {
            Fail(Engine, Status);
            return;
        }
    }
    Engine->Depth--;
    Engine->OperandStart = Engine->Stack[Engine->Depth].Mark;
    Append(Engine->Expression, sizeof(Engine->Expression), ")");
    Engine->Entering = 0;
    Engine->HaveOperand = 1;
    Engine->OperandWritten = 1;
    Engine->Operand[0] = 0;
}

static void
PressPercent(CALC_ENGINE *Engine)
{
    CALC_NUMBER Hundred, Result;
    CALC_STATUS Status;

    if (Engine->Mode != CALC_MODE_STANDARD)
        return;
    NumFromInt64(&Hundred, 100);
    if (Engine->Depth > 0)
    {
        const CALC_PENDING *Top = &Engine->Stack[Engine->Depth - 1];

        Status = NumDivide(&Engine->Value, &Hundred, &Result);
        if (Status == CALC_OK && (Top->Operator == CALC_KEY_ADD || Top->Operator == CALC_KEY_SUBTRACT))
            Status = NumMultiply(&Top->Value, &Result, &Result);
        if (Status != CALC_OK)
        {
            Fail(Engine, Status);
            return;
        }
    }
    else
    {
        NumZero(&Result);
    }
    SetOperand(Engine, &Result);
}

static CALC_STATUS
ApplyUnary(const CALC_ENGINE *Engine, CALC_KEY Key, const CALC_NUMBER *Input, CALC_NUMBER *Result,
           const char **Prefix)
{
    CALC_NUMBER One, Operand;

    if (Engine->Mode == CALC_MODE_PROGRAMMER)
    {
        if (Key != CALC_KEY_NOT)
            return CALC_UNDEFINED;
        *Prefix = "NOT(";
        NumFromInt64(Result, WrapWord(Engine, ~(unsigned long long)IntegerValue(Input)));
        return CALC_OK;
    }
    NumFromInt64(&One, 1);
    switch (Key)
    {
        case CALC_KEY_RECIPROCAL:
            *Prefix = "1/(";
            return NumDivide(&One, Input, Result);
        case CALC_KEY_SQUARE:
            *Prefix = "sqr(";
            return NumMultiply(Input, Input, Result);
        case CALC_KEY_SQUARE_ROOT:
            *Prefix = "sqrt(";
            return NumSquareRoot(Input, Result);
        default:
            break;
    }
    if (Engine->Mode != CALC_MODE_SCIENTIFIC)
        return CALC_UNDEFINED;
    switch (Key)
    {
        case CALC_KEY_CUBE:
            *Prefix = "cube(";
            NumFromInt64(&Operand, 3);
            return NumPower(Input, &Operand, Result);
        case CALC_KEY_ABSOLUTE:
            *Prefix = "abs(";
            *Result = *Input;
            if (Result->Sign < 0)
                Result->Sign = 1;
            return CALC_OK;
        case CALC_KEY_FACTORIAL:
            *Prefix = "fact(";
            return NumFactorial(Input, Result);
        case CALC_KEY_TEN_POWER:
            *Prefix = "10^(";
            NumFromInt64(&Operand, 10);
            return NumPower(&Operand, Input, Result);
        case CALC_KEY_E_POWER:
            *Prefix = "e^(";
            return NumExp(Input, Result);
        case CALC_KEY_LOG:
            *Prefix = "log(";
            return NumLog10(Input, Result);
        case CALC_KEY_LN:
            *Prefix = "ln(";
            return NumLn(Input, Result);
        case CALC_KEY_SIN:
            if (Engine->Hyperbolic)
            {
                *Prefix = Engine->Inverse ? "asinh(" : "sinh(";
                return Engine->Inverse ? NumAsinh(Input, Result) : NumSinh(Input, Result);
            }
            *Prefix = Engine->Inverse ? "asin(" : "sin(";
            return Engine->Inverse ? NumAsin(Input, Engine->Angle, Result) : NumSin(Input, Engine->Angle, Result);
        case CALC_KEY_COS:
            if (Engine->Hyperbolic)
            {
                *Prefix = Engine->Inverse ? "acosh(" : "cosh(";
                return Engine->Inverse ? NumAcosh(Input, Result) : NumCosh(Input, Result);
            }
            *Prefix = Engine->Inverse ? "acos(" : "cos(";
            return Engine->Inverse ? NumAcos(Input, Engine->Angle, Result) : NumCos(Input, Engine->Angle, Result);
        case CALC_KEY_TAN:
            if (Engine->Hyperbolic)
            {
                *Prefix = Engine->Inverse ? "atanh(" : "tanh(";
                return Engine->Inverse ? NumAtanh(Input, Result) : NumTanh(Input, Result);
            }
            *Prefix = Engine->Inverse ? "atan(" : "tan(";
            return Engine->Inverse ? NumAtan(Input, Engine->Angle, Result) : NumTan(Input, Engine->Angle, Result);
        default:
            return CALC_UNDEFINED;
    }
}

static void
PressUnary(CALC_ENGINE *Engine, CALC_KEY Key)
{
    const char *Prefix = NULL;
    CALC_NUMBER Result;
    CALC_STATUS Status;

    Status = ApplyUnary(Engine, Key, &Engine->Value, &Result, &Prefix);
    if (Prefix == NULL)
        return;
    WrapOperand(Engine, Prefix);
    if (Status != CALC_OK)
    {
        Fail(Engine, Status);
        return;
    }
    Engine->Value = Result;
    Engine->Entering = 0;
    Engine->HaveOperand = 1;
}

static void
PressMemory(CALC_ENGINE *Engine, CALC_KEY Key)
{
    CALC_NUMBER Result;
    CALC_STATUS Status;

    switch (Key)
    {
        case CALC_KEY_MEMORY_CLEAR:
            NumZero(&Engine->Memory);
            Engine->HasMemory = 0;
            return;
        case CALC_KEY_MEMORY_RECALL:
            if (Engine->HasMemory)
            {
                Result = Engine->Memory;
                if (Engine->Mode == CALC_MODE_PROGRAMMER)
                {
                    NumTruncate(&Result);
                    NumFromInt64(&Result, WrapWord(Engine, (unsigned long long)IntegerValue(&Result)));
                }
                SetOperand(Engine, &Result);
            }
            return;
        case CALC_KEY_MEMORY_STORE:
            Engine->Memory = Engine->Value;
            Engine->HasMemory = 1;
            break;
        default:
            if (Engine->Mode == CALC_MODE_PROGRAMMER)
                Status = ApplyInteger(Engine, Key == CALC_KEY_MEMORY_ADD ? CALC_KEY_ADD : CALC_KEY_SUBTRACT,
                                      &Engine->Memory, &Engine->Value, &Result);
            else if (Key == CALC_KEY_MEMORY_ADD)
                Status = NumAdd(&Engine->Memory, &Engine->Value, &Result);
            else
                Status = NumSubtract(&Engine->Memory, &Engine->Value, &Result);
            if (Status != CALC_OK)
            {
                Fail(Engine, Status);
                return;
            }
            Engine->Memory = Result;
            Engine->HasMemory = 1;
            break;
    }
    Engine->Entering = 0;
}

static void
SetBase(CALC_ENGINE *Engine, int Base)
{
    if (Engine->Mode != CALC_MODE_PROGRAMMER)
        return;
    Engine->Entering = 0;
    Engine->Base = Base;
}

static void
CycleWord(CALC_ENGINE *Engine)
{
    int Index;

    if (Engine->Mode != CALC_MODE_PROGRAMMER)
        return;
    Engine->Entering = 0;
    Engine->WordBits = Engine->WordBits == 8 ? 64 : Engine->WordBits / 2;
    NumFromInt64(&Engine->Value, WrapWord(Engine, (unsigned long long)IntegerValue(&Engine->Value)));
    for (Index = 0; Index < Engine->Depth; Index++)
    {
        CALC_NUMBER *Value = &Engine->Stack[Index].Value;

        NumFromInt64(Value, WrapWord(Engine, (unsigned long long)IntegerValue(Value)));
    }
}

void
CalcSetMode(CALC_ENGINE *Engine, CALC_MODE Mode)
{
    CALC_NUMBER Value = Engine->Value;

    if (Engine->Error != CALC_OK)
        NumZero(&Value);
    Reset(Engine);
    Engine->Mode = Mode;
    Engine->Inverse = 0;
    Engine->Hyperbolic = 0;
    if (Mode == CALC_MODE_PROGRAMMER)
    {
        long long Whole;

        NumTruncate(&Value);
        if (!NumToInt64(&Value, &Whole))
            Whole = 0;
        NumFromInt64(&Value, WrapWord(Engine, (unsigned long long)Whole));
    }
    Engine->Value = Value;
    Engine->HaveOperand = !NumIsZero(&Value);
}

int
CalcKeyEnabled(const CALC_ENGINE *Engine, CALC_KEY Key)
{
    if (Engine->Mode != CALC_MODE_PROGRAMMER)
        return 1;
    if (Key >= CALC_KEY_0 && Key <= CALC_KEY_F)
        return (int)(Key - CALC_KEY_0) < Engine->Base;
    return Key != CALC_KEY_POINT;
}

int
CalcPaste(CALC_ENGINE *Engine, const char *Text)
{
    CALC_NUMBER Value;

    if (Engine->Mode == CALC_MODE_PROGRAMMER)
    {
        long long Whole;

        if (!ParseInteger(Engine, Text, &Whole))
            return 0;
        NumFromInt64(&Value, Whole);
    }
    else if (NumFromString(Text, &Value) != CALC_OK)
    {
        return 0;
    }
    if (Engine->Error != CALC_OK)
        Reset(Engine);
    SetOperand(Engine, &Value);
    return 1;
}

void
CalcPress(CALC_ENGINE *Engine, CALC_KEY Key)
{
    CALC_NUMBER Constant;

    if (Key == CALC_KEY_CLEAR || (Key == CALC_KEY_CLEAR_ENTRY && Engine->Error != CALC_OK))
    {
        Reset(Engine);
        return;
    }
    switch (Key)
    {
        case CALC_KEY_INVERSE:
            Engine->Inverse = !Engine->Inverse;
            return;
        case CALC_KEY_HYPERBOLIC:
            Engine->Hyperbolic = !Engine->Hyperbolic;
            return;
        case CALC_KEY_ANGLE:
            Engine->Angle = (CALC_ANGLE)((Engine->Angle + 1) % 3);
            return;
        case CALC_KEY_FORMAT:
            Engine->Exponential = !Engine->Exponential;
            return;
        case CALC_KEY_MEMORY_CLEAR:
            PressMemory(Engine, Key);
            return;
        default:
            break;
    }
    if (Engine->Error != CALC_OK)
    {
        if (!(Key >= CALC_KEY_0 && Key <= CALC_KEY_POINT) && Key != CALC_KEY_PI && Key != CALC_KEY_EULER &&
            Key != CALC_KEY_MEMORY_RECALL)
            return;
        Reset(Engine);
    }
    if (Key >= CALC_KEY_0 && Key <= CALC_KEY_F)
    {
        PressDigit(Engine, (int)(Key - CALC_KEY_0));
        return;
    }
    if (IsOperator(Key))
    {
        if (OperatorAllowed(Engine, Key))
            PressOperator(Engine, Key);
        return;
    }
    switch (Key)
    {
        case CALC_KEY_POINT:
            PressPoint(Engine);
            break;
        case CALC_KEY_EXPONENT:
            PressExponent(Engine);
            break;
        case CALC_KEY_SIGN:
            PressSign(Engine);
            break;
        case CALC_KEY_BACK:
            PressBack(Engine);
            break;
        case CALC_KEY_CLEAR_ENTRY:
            DropOperand(Engine);
            NumZero(&Engine->Value);
            Engine->Entering = 0;
            Engine->HaveOperand = 0;
            break;
        case CALC_KEY_EQUALS:
            PressEquals(Engine);
            break;
        case CALC_KEY_PERCENT:
            PressPercent(Engine);
            break;
        case CALC_KEY_OPEN:
            PressOpen(Engine);
            break;
        case CALC_KEY_CLOSE:
            PressClose(Engine);
            break;
        case CALC_KEY_PI:
        case CALC_KEY_EULER:
            if (Engine->Mode != CALC_MODE_SCIENTIFIC)
                break;
            if (Key == CALC_KEY_PI)
                NumPi(&Constant);
            else
                NumEuler(&Constant);
            SetOperand(Engine, &Constant);
            break;
        case CALC_KEY_MEMORY_RECALL:
        case CALC_KEY_MEMORY_ADD:
        case CALC_KEY_MEMORY_SUBTRACT:
        case CALC_KEY_MEMORY_STORE:
            PressMemory(Engine, Key);
            break;
        case CALC_KEY_HEX:
            SetBase(Engine, 16);
            break;
        case CALC_KEY_DEC:
            SetBase(Engine, 10);
            break;
        case CALC_KEY_OCT:
            SetBase(Engine, 8);
            break;
        case CALC_KEY_BIN:
            SetBase(Engine, 2);
            break;
        case CALC_KEY_WORD:
            CycleWord(Engine);
            break;
        default:
            PressUnary(Engine, Key);
            break;
    }
}
