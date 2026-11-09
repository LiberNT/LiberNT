/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Working out what an expression typed at the prompt means
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* How much of an error message the caller is given room for */
#define KDB_ERROR_MESSAGE_MAX   128

typedef struct _KDB_PARSER
{
    PCSTR Start;
    PCSTR Current;
    PKDB_KTRAP_FRAME TrapFrame;
    PCSTR Error;
    PCSTR ErrorAt;
} KDB_PARSER, *PKDB_PARSER;

/* PRIVATE FUNCTIONS **********************************************************/

static ULONGLONG KdbpParseOr(_Inout_ PKDB_PARSER Parser);

static
VOID
KdbpFail(
    _Inout_ PKDB_PARSER Parser,
    _In_ PCSTR Message)
{
    /* The first thing that went wrong is the one worth reporting */
    if (Parser->Error != NULL)
        return;

    Parser->Error = Message;
    Parser->ErrorAt = Parser->Current;
}

static
VOID
KdbpSkipSpace(
    _Inout_ PKDB_PARSER Parser)
{
    while ((*Parser->Current == ' ') || (*Parser->Current == '\t'))
        Parser->Current++;
}

/*
 * Whether the next thing in the expression is this operator, and if it is,
 * steps over it. Longer operators are asked about before shorter ones that
 * start the same way, which the callers below rely on.
 */
static
BOOLEAN
KdbpTakeOperator(
    _Inout_ PKDB_PARSER Parser,
    _In_ PCSTR Operator)
{
    SIZE_T Length = strlen(Operator);

    KdbpSkipSpace(Parser);

    if (strncmp(Parser->Current, Operator, Length) != 0)
        return FALSE;

    Parser->Current += Length;
    return TRUE;
}

static
BOOLEAN
KdbpIsNameChar(
    _In_ CHAR Char)
{
    if ((Char >= 'a') && (Char <= 'z'))
        return TRUE;
    if ((Char >= 'A') && (Char <= 'Z'))
        return TRUE;
    if ((Char >= '0') && (Char <= '9'))
        return TRUE;

    return ((Char == '_') || (Char == '!') || (Char == '@') ||
            (Char == '.') || (Char == '$') || (Char == '?'));
}

/*
 * A number. Decimal unless it says otherwise, so that what a person types
 * means what it would mean in C.
 */
static
ULONGLONG
KdbpParseNumber(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = 0;
    ULONG Radix = 10;
    BOOLEAN Any = FALSE;

    if ((Parser->Current[0] == '0') &&
        ((Parser->Current[1] == 'x') || (Parser->Current[1] == 'X')))
    {
        Radix = 16;
        Parser->Current += 2;
    }
    else if ((Parser->Current[0] == '0') &&
             ((Parser->Current[1] == 'n') || (Parser->Current[1] == 'N')))
    {
        Parser->Current += 2;
    }
    else if ((Parser->Current[0] == '0') &&
             ((Parser->Current[1] == 'b') || (Parser->Current[1] == 'B')))
    {
        Radix = 2;
        Parser->Current += 2;
    }

    for (;;)
    {
        CHAR Char = *Parser->Current;
        ULONG Digit;

        if ((Char >= '0') && (Char <= '9'))
            Digit = (ULONG)(Char - '0');
        else if ((Char >= 'a') && (Char <= 'f'))
            Digit = (ULONG)(Char - 'a') + 10;
        else if ((Char >= 'A') && (Char <= 'F'))
            Digit = (ULONG)(Char - 'A') + 10;
        else
            break;

        if (Digit >= Radix)
            break;

        Value = (Value * Radix) + Digit;
        Any = TRUE;
        Parser->Current++;
    }

    if (!Any)
        KdbpFail(Parser, "expected a number");

    return Value;
}

/*
 * A name: either a register of this processor or a symbol of a loaded image.
 * Registers are looked at first, because a person who writes `sp` means the
 * stack pointer even on a machine that has a symbol by that name.
 */
static
ULONGLONG
KdbpParseName(
    _Inout_ PKDB_PARSER Parser)
{
    CHAR Name[128];
    ULONG Length = 0;
    ULONGLONG Value = 0;
    ULONG_PTR Address;
    BOOLEAN Explicit = FALSE;

    if (*Parser->Current == '@')
    {
        Explicit = TRUE;
        Parser->Current++;
    }

    while (KdbpIsNameChar(*Parser->Current) && (Length < sizeof(Name) - 1))
        Name[Length++] = *Parser->Current++;

    Name[Length] = ANSI_NULL;

    if (Length == 0)
    {
        KdbpFail(Parser, "expected a name");
        return 0;
    }

    if (NT_SUCCESS(KdbpGetRegisterValue(Parser->TrapFrame, Name, &Value, NULL)))
        return Value;

    if (Explicit)
    {
        KdbpFail(Parser, "no such register");
        return 0;
    }

    if (KdbpSymLookupName(Name, &Address))
        return (ULONGLONG)Address;

    KdbpFail(Parser, "no such register or symbol");
    return 0;
}

static
ULONGLONG
KdbpParsePrimary(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value;

    KdbpSkipSpace(Parser);

    if (*Parser->Current == '(')
    {
        Parser->Current++;
        Value = KdbpParseOr(Parser);

        if (!KdbpTakeOperator(Parser, ")"))
            KdbpFail(Parser, "expected a closing parenthesis");

        return Value;
    }

    if ((*Parser->Current >= '0') && (*Parser->Current <= '9'))
        return KdbpParseNumber(Parser);

    return KdbpParseName(Parser);
}

static
ULONGLONG
KdbpParseUnary(
    _Inout_ PKDB_PARSER Parser)
{
    KdbpSkipSpace(Parser);

    if (KdbpTakeOperator(Parser, "-"))
        return (ULONGLONG)(0 - (LONGLONG)KdbpParseUnary(Parser));

    if (KdbpTakeOperator(Parser, "+"))
        return KdbpParseUnary(Parser);

    if (KdbpTakeOperator(Parser, "~"))
        return ~KdbpParseUnary(Parser);

    if (KdbpTakeOperator(Parser, "!"))
        return (KdbpParseUnary(Parser) == 0) ? 1 : 0;

    /* A star reads what the address it is given points at */
    if (KdbpTakeOperator(Parser, "*"))
    {
        ULONGLONG Address = KdbpParseUnary(Parser);
        ULONG_PTR Pointed = 0;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Pointed,
                                           (PVOID)(ULONG_PTR)Address,
                                           sizeof(Pointed))))
        {
            KdbpFail(Parser, "cannot read that address");
            return 0;
        }

        return (ULONGLONG)Pointed;
    }

    return KdbpParsePrimary(Parser);
}

static
ULONGLONG
KdbpParseMultiplicative(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseUnary(Parser);

    for (;;)
    {
        ULONGLONG Right;

        if (KdbpTakeOperator(Parser, "*"))
        {
            Value *= KdbpParseUnary(Parser);
            continue;
        }

        if (KdbpTakeOperator(Parser, "/"))
        {
            Right = KdbpParseUnary(Parser);

            if (Right == 0)
            {
                KdbpFail(Parser, "division by zero");
                return 0;
            }

            Value /= Right;
            continue;
        }

        if (KdbpTakeOperator(Parser, "%"))
        {
            Right = KdbpParseUnary(Parser);

            if (Right == 0)
            {
                KdbpFail(Parser, "division by zero");
                return 0;
            }

            Value %= Right;
            continue;
        }

        return Value;
    }
}

static
ULONGLONG
KdbpParseAdditive(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseMultiplicative(Parser);

    for (;;)
    {
        if (KdbpTakeOperator(Parser, "+"))
        {
            Value += KdbpParseMultiplicative(Parser);
            continue;
        }

        /* Not the start of `->`, which is not an operator here */
        if ((Parser->Current[0] == '-') && (Parser->Current[1] != '>') &&
            KdbpTakeOperator(Parser, "-"))
        {
            Value -= KdbpParseMultiplicative(Parser);
            continue;
        }

        return Value;
    }
}

static
ULONGLONG
KdbpParseShift(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseAdditive(Parser);

    for (;;)
    {
        if (KdbpTakeOperator(Parser, "<<"))
        {
            Value <<= (KdbpParseAdditive(Parser) & 63);
            continue;
        }

        if (KdbpTakeOperator(Parser, ">>"))
        {
            Value >>= (KdbpParseAdditive(Parser) & 63);
            continue;
        }

        return Value;
    }
}

static
ULONGLONG
KdbpParseRelational(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseShift(Parser);

    for (;;)
    {
        if (KdbpTakeOperator(Parser, "<="))
        {
            Value = (Value <= KdbpParseShift(Parser)) ? 1 : 0;
            continue;
        }

        if (KdbpTakeOperator(Parser, ">="))
        {
            Value = (Value >= KdbpParseShift(Parser)) ? 1 : 0;
            continue;
        }

        if ((Parser->Current[0] == '<') && (Parser->Current[1] != '<') &&
            KdbpTakeOperator(Parser, "<"))
        {
            Value = (Value < KdbpParseShift(Parser)) ? 1 : 0;
            continue;
        }

        if ((Parser->Current[0] == '>') && (Parser->Current[1] != '>') &&
            KdbpTakeOperator(Parser, ">"))
        {
            Value = (Value > KdbpParseShift(Parser)) ? 1 : 0;
            continue;
        }

        return Value;
    }
}

static
ULONGLONG
KdbpParseEquality(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseRelational(Parser);

    for (;;)
    {
        if (KdbpTakeOperator(Parser, "=="))
        {
            Value = (Value == KdbpParseRelational(Parser)) ? 1 : 0;
            continue;
        }

        if (KdbpTakeOperator(Parser, "!="))
        {
            Value = (Value != KdbpParseRelational(Parser)) ? 1 : 0;
            continue;
        }

        return Value;
    }
}

static
ULONGLONG
KdbpParseBitAnd(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseEquality(Parser);

    while ((Parser->Current[0] == '&') && (Parser->Current[1] != '&') &&
           KdbpTakeOperator(Parser, "&"))
    {
        Value &= KdbpParseEquality(Parser);
    }

    return Value;
}

static
ULONGLONG
KdbpParseBitXor(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseBitAnd(Parser);

    while (KdbpTakeOperator(Parser, "^"))
        Value ^= KdbpParseBitAnd(Parser);

    return Value;
}

static
ULONGLONG
KdbpParseBitOr(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseBitXor(Parser);

    while ((Parser->Current[0] == '|') && (Parser->Current[1] != '|') &&
           KdbpTakeOperator(Parser, "|"))
    {
        Value |= KdbpParseBitXor(Parser);
    }

    return Value;
}

static
ULONGLONG
KdbpParseAnd(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseBitOr(Parser);

    while (KdbpTakeOperator(Parser, "&&"))
    {
        /*
         * Both sides are worked out whichever way the first one went: the
         * right-hand side still has to be stepped over to find the end of the
         * expression, and a debugger expression has nothing to make happen.
         */
        ULONGLONG Right = KdbpParseBitOr(Parser);

        Value = ((Value != 0) && (Right != 0)) ? 1 : 0;
    }

    return Value;
}

static
ULONGLONG
KdbpParseOr(
    _Inout_ PKDB_PARSER Parser)
{
    ULONGLONG Value = KdbpParseAnd(Parser);

    while (KdbpTakeOperator(Parser, "||"))
    {
        ULONGLONG Right = KdbpParseAnd(Parser);

        Value = ((Value != 0) || (Right != 0)) ? 1 : 0;
    }

    return Value;
}

static
VOID
KdbpReportError(
    _In_ PKDB_PARSER Parser,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg)
{
    ULONG Index;

    if (ErrOffset != NULL)
    {
        ErrOffset[0] = (Parser->ErrorAt != NULL)
                       ? (LONG)(Parser->ErrorAt - Parser->Start) : 0;
    }

    if (ErrMsg == NULL)
        return;

    if (Parser->Error == NULL)
    {
        ErrMsg[0] = ANSI_NULL;
        return;
    }

    for (Index = 0;
         (Index < KDB_ERROR_MESSAGE_MAX - 1) && (Parser->Error[Index] != ANSI_NULL);
         Index++)
    {
        ErrMsg[Index] = Parser->Error[Index];
    }

    ErrMsg[Index] = ANSI_NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Works out what an expression comes to, with the registers of whatever the
 * debugger is looking at standing for themselves.
 */
BOOLEAN
KdbpRpnEvaluateExpression(
    _In_ PCHAR Expression,
    _In_ PKDB_KTRAP_FRAME TrapFrame,
    _Out_ PULONGLONG Result,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg)
{
    KDB_PARSER Parser;
    ULONGLONG Value;

    *Result = 0;

    if ((Expression == NULL) || (*Expression == ANSI_NULL))
    {
        if (ErrOffset != NULL)
            *ErrOffset = 0;
        if (ErrMsg != NULL)
            strcpy(ErrMsg, "the expression is empty");

        return FALSE;
    }

    Parser.Start = Expression;
    Parser.Current = Expression;
    Parser.TrapFrame = TrapFrame;
    Parser.Error = NULL;
    Parser.ErrorAt = NULL;

    Value = KdbpParseOr(&Parser);

    KdbpSkipSpace(&Parser);

    /* Anything left over means the expression was not understood as written */
    if ((Parser.Error == NULL) && (*Parser.Current != ANSI_NULL))
        KdbpFail(&Parser, "unexpected text after the expression");

    if (Parser.Error != NULL)
    {
        KdbpReportError(&Parser, ErrOffset, ErrMsg);
        return FALSE;
    }

    *Result = Value;
    return TRUE;
}

/*
 * Checks that an expression can be understood, for a caller that will come
 * back to it later. What comes back is the text itself, validated.
 */
PVOID
KdbpRpnParseExpression(
    _In_ PCHAR Expression,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg)
{
    KDB_PARSER Parser;

    if ((Expression == NULL) || (*Expression == ANSI_NULL))
    {
        if (ErrOffset != NULL)
            *ErrOffset = 0;
        if (ErrMsg != NULL)
            strcpy(ErrMsg, "the expression is empty");

        return NULL;
    }

    Parser.Start = Expression;
    Parser.Current = Expression;
    Parser.TrapFrame = NULL;
    Parser.Error = NULL;
    Parser.ErrorAt = NULL;

    KdbpParseOr(&Parser);
    KdbpSkipSpace(&Parser);

    /*
     * A name that cannot be resolved now may resolve later, once the image it
     * belongs to is loaded, so that is not treated as an error here.
     */
    if ((Parser.Error != NULL) &&
        (strcmp(Parser.Error, "no such register or symbol") != 0))
    {
        KdbpReportError(&Parser, ErrOffset, ErrMsg);
        return NULL;
    }

    return Expression;
}

BOOLEAN
KdbpRpnEvaluateParsedExpression(
    _In_ PVOID Expression,
    _In_ PKDB_KTRAP_FRAME TrapFrame,
    _Out_ PULONGLONG Result,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg)
{
    return KdbpRpnEvaluateExpression((PCHAR)Expression,
                                     TrapFrame,
                                     Result,
                                     ErrOffset,
                                     ErrMsg);
}

/*
 * One register of whatever the debugger is looking at. The size is reported
 * as well, because a person asking for a segment register does not want to
 * read it as eight bytes.
 */
NTSTATUS
KdbpGetRegisterValue(
    _In_ PKDB_KTRAP_FRAME TrapFrame,
    _In_ PCSTR RegisterName,
    _Out_ PULONGLONG Value,
    _Out_opt_ PULONG RegisterSize)
{
    ULONG Index;

    *Value = 0;
    if (RegisterSize != NULL)
        *RegisterSize = 0;

    if ((TrapFrame == NULL) || (RegisterName == NULL))
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < KdbpRegisterCount; Index++)
    {
        const KDB_REGISTER *Register = &KdbpRegisters[Index];
        PVOID Location;

        if (_stricmp(RegisterName, Register->Name) != 0)
            continue;

        Location = (PVOID)((ULONG_PTR)TrapFrame + Register->Offset);

        switch (Register->Size)
        {
            case sizeof(UCHAR):
                *Value = *(PUCHAR)Location;
                break;
            case sizeof(USHORT):
                *Value = *(PUSHORT)Location;
                break;
            case sizeof(ULONG):
                *Value = *(PULONG)Location;
                break;
            case sizeof(ULONGLONG):
                *Value = *(PULONGLONG)Location;
                break;
            default:
                return STATUS_NOT_SUPPORTED;
        }

        if (RegisterSize != NULL)
            *RegisterSize = Register->Size;

        return STATUS_SUCCESS;
    }

    return STATUS_NOT_FOUND;
}

/*
 * Changes one register. What is written is what the machine will run with
 * when the debugger lets it go.
 */
NTSTATUS
KdbpSetRegisterValue(
    _Inout_ PKDB_KTRAP_FRAME TrapFrame,
    _In_ PCSTR RegisterName,
    _In_ ULONGLONG Value)
{
    ULONG Index;

    if ((TrapFrame == NULL) || (RegisterName == NULL))
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < KdbpRegisterCount; Index++)
    {
        const KDB_REGISTER *Register = &KdbpRegisters[Index];
        PVOID Location;

        if (_stricmp(RegisterName, Register->Name) != 0)
            continue;

        Location = (PVOID)((ULONG_PTR)TrapFrame + Register->Offset);

        switch (Register->Size)
        {
            case sizeof(UCHAR):
                *(PUCHAR)Location = (UCHAR)Value;
                break;
            case sizeof(USHORT):
                *(PUSHORT)Location = (USHORT)Value;
                break;
            case sizeof(ULONG):
                *(PULONG)Location = (ULONG)Value;
                break;
            case sizeof(ULONGLONG):
                *(PULONGLONG)Location = Value;
                break;
            default:
                return STATUS_NOT_SUPPORTED;
        }

        return STATUS_SUCCESS;
    }

    return STATUS_NOT_FOUND;
}

/* EOF */
