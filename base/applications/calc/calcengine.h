/*
 * PROJECT:     LiberNT Calculator
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calculator key handling interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "calcnum.h"

#define CALC_STACK_DEPTH 64
#define CALC_TEXT_LENGTH 320
#define CALC_EXPRESSION_LENGTH 1024

typedef enum _CALC_MODE
{
    CALC_MODE_STANDARD,
    CALC_MODE_SCIENTIFIC,
    CALC_MODE_PROGRAMMER
} CALC_MODE;

typedef enum _CALC_KEY
{
    CALC_KEY_NONE,
    CALC_KEY_0,
    CALC_KEY_1,
    CALC_KEY_2,
    CALC_KEY_3,
    CALC_KEY_4,
    CALC_KEY_5,
    CALC_KEY_6,
    CALC_KEY_7,
    CALC_KEY_8,
    CALC_KEY_9,
    CALC_KEY_A,
    CALC_KEY_B,
    CALC_KEY_C,
    CALC_KEY_D,
    CALC_KEY_E,
    CALC_KEY_F,
    CALC_KEY_POINT,
    CALC_KEY_EXPONENT,
    CALC_KEY_SIGN,
    CALC_KEY_BACK,
    CALC_KEY_CLEAR_ENTRY,
    CALC_KEY_CLEAR,
    CALC_KEY_ADD,
    CALC_KEY_SUBTRACT,
    CALC_KEY_MULTIPLY,
    CALC_KEY_DIVIDE,
    CALC_KEY_MODULO,
    CALC_KEY_POWER,
    CALC_KEY_ROOT,
    CALC_KEY_AND,
    CALC_KEY_OR,
    CALC_KEY_XOR,
    CALC_KEY_SHIFT_LEFT,
    CALC_KEY_SHIFT_RIGHT,
    CALC_KEY_EQUALS,
    CALC_KEY_PERCENT,
    CALC_KEY_OPEN,
    CALC_KEY_CLOSE,
    CALC_KEY_RECIPROCAL,
    CALC_KEY_SQUARE,
    CALC_KEY_CUBE,
    CALC_KEY_SQUARE_ROOT,
    CALC_KEY_ABSOLUTE,
    CALC_KEY_FACTORIAL,
    CALC_KEY_TEN_POWER,
    CALC_KEY_E_POWER,
    CALC_KEY_LOG,
    CALC_KEY_LN,
    CALC_KEY_SIN,
    CALC_KEY_COS,
    CALC_KEY_TAN,
    CALC_KEY_NOT,
    CALC_KEY_PI,
    CALC_KEY_EULER,
    CALC_KEY_MEMORY_CLEAR,
    CALC_KEY_MEMORY_RECALL,
    CALC_KEY_MEMORY_ADD,
    CALC_KEY_MEMORY_SUBTRACT,
    CALC_KEY_MEMORY_STORE,
    CALC_KEY_INVERSE,
    CALC_KEY_HYPERBOLIC,
    CALC_KEY_ANGLE,
    CALC_KEY_FORMAT,
    CALC_KEY_HEX,
    CALC_KEY_DEC,
    CALC_KEY_OCT,
    CALC_KEY_BIN,
    CALC_KEY_WORD,
    CALC_KEY_COUNT
} CALC_KEY;

typedef struct _CALC_PENDING
{
    CALC_NUMBER Value;
    CALC_KEY Operator;
    int Mark;
} CALC_PENDING;

typedef struct _CALC_ENGINE
{
    CALC_MODE Mode;
    CALC_ANGLE Angle;
    int Base;
    int WordBits;
    int Inverse;
    int Hyperbolic;
    int Exponential;
    CALC_NUMBER Value;
    char Entry[CALC_TEXT_LENGTH];
    int Entering;
    int HaveOperand;
    int OperandWritten;
    int OperandStart;
    int Evaluated;
    char Operand[CALC_TEXT_LENGTH];
    CALC_PENDING Stack[CALC_STACK_DEPTH];
    int Depth;
    CALC_KEY LastOperator;
    CALC_NUMBER LastOperand;
    int CanRepeat;
    CALC_NUMBER Memory;
    int HasMemory;
    CALC_STATUS Error;
    char Expression[CALC_EXPRESSION_LENGTH];
} CALC_ENGINE;

void CalcInit(CALC_ENGINE *Engine);
void CalcSetMode(CALC_ENGINE *Engine, CALC_MODE Mode);
void CalcPress(CALC_ENGINE *Engine, CALC_KEY Key);
int CalcKeyEnabled(const CALC_ENGINE *Engine, CALC_KEY Key);
void CalcDisplay(const CALC_ENGINE *Engine, char *Buffer, size_t Size);
void CalcDisplayBase(const CALC_ENGINE *Engine, int Base, char *Buffer, size_t Size);
void CalcExpression(const CALC_ENGINE *Engine, char *Buffer, size_t Size);
int CalcPaste(CALC_ENGINE *Engine, const char *Text);
