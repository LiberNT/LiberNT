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

/*
 * How deep parentheses and prefix operators may go. Each level costs a few
 * frames of the stack the machine stopped on, which is all the debugger has.
 */
#define KDB_EXPRESSION_DEPTH_MAX 32

typedef struct _KDB_PARSER
{
    PCSTR Start;
    PCSTR Current;
    PKDB_KTRAP_FRAME TrapFrame;
    PCSTR Error;
    PCSTR ErrorAt;
    ULONG Depth;
    CHAR ErrorText[KDB_ERROR_MESSAGE_MAX];

    /* Checking the text only: registers, memory and symbols are not read */
    BOOLEAN ParseOnly;
} KDB_PARSER, *PKDB_PARSER;

typedef struct _KDB_BINARY_OPERATOR
{
    PCSTR Text;
    UCHAR Length;
    UCHAR Precedence;
    CHAR Operator;
} KDB_BINARY_OPERATOR;

/* Longer operators come before the shorter ones they start with */
static const KDB_BINARY_OPERATOR KdbpBinaryOperators[] =
{
    {"||", 2, 1, 'o'},
    {"&&", 2, 2, 'a'},
    {"==", 2, 6, 'e'},
    {"!=", 2, 6, 'n'},
    {"<=", 2, 7, 'l'},
    {">=", 2, 7, 'g'},
    {"<<", 2, 8, 'L'},
    {">>", 2, 8, 'R'},
    {"|", 1, 3, '|'},
    {"^", 1, 4, '^'},
    {"&", 1, 5, '&'},
    {"<", 1, 7, '<'},
    {">", 1, 7, '>'},
    {"+", 1, 9, '+'},
    {"-", 1, 9, '-'},
    {"*", 1, 10, '*'},
    {"/", 1, 10, '/'},
    {"%", 1, 10, '%'}
};

/* What $exp and $entry stand for: the last value ? showed, and the entry a list walk is at */
static ULONGLONG KdbpLastExpressionValue;
static ULONGLONG KdbpListEntryValue;

/* PRIVATE FUNCTIONS **********************************************************/

static ULONGLONG KdbpParseBinary(_Inout_ PKDB_PARSER Parser, _In_ ULONG MinimumPrecedence);
static ULONGLONG KdbpParsePrimary(_Inout_ PKDB_PARSER Parser);

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
 * steps over it.
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
 * Reads what an address holds for the expression. While only checking the
 * text nothing is read: the address may not mean anything until later.
 */
static
ULONGLONG
KdbpReadValue(
    _Inout_ PKDB_PARSER Parser,
    _In_ ULONGLONG Address,
    _In_ ULONG Size)
{
    ULONGLONG Value = 0;

    if (Parser->ParseOnly)
        return 0;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Value, (PVOID)(ULONG_PTR)Address, Size)))
    {
        KdbpFail(Parser, "cannot read that address");
        return 0;
    }

    return Value;
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

        if (Value > (MAXULONGLONG - Digit) / Radix)
        {
            KdbpFail(Parser, "the number does not fit in 64 bits");
            return 0;
        }

        Value = (Value * Radix) + Digit;
        Any = TRUE;
        Parser->Current++;
    }

    if (!Any)
        KdbpFail(Parser, "expected a number");

    return Value;
}

/*
 * The names that stand for something of the stop rather than a register:
 * where it is, which thread and process it is in, and what was last worked
 * out. Returns FALSE for a name that is none of them.
 */
static
BOOLEAN
KdbpPseudoRegister(
    _Inout_ PKDB_PARSER Parser,
    _In_ PCSTR Name,
    _Out_ PULONGLONG Value)
{
    static CONTEXT Caller;
    PVOID Pointer = NULL;

    *Value = 0;

    if ((_stricmp(Name, "$ip") != 0) && (_stricmp(Name, "$pc") != 0) && (_stricmp(Name, "$sp") != 0) &&
        (_stricmp(Name, "$fp") != 0) && (_stricmp(Name, "$ra") != 0) && (_stricmp(Name, "$thread") != 0) &&
        (_stricmp(Name, "$proc") != 0) && (_stricmp(Name, "$teb") != 0) && (_stricmp(Name, "$peb") != 0) &&
        (_stricmp(Name, "$tid") != 0) && (_stricmp(Name, "$pid") != 0) && (_stricmp(Name, "$prcb") != 0) &&
        (_stricmp(Name, "$cpu") != 0) && (_stricmp(Name, "$exp") != 0) && (_stricmp(Name, "$entry") != 0))
    {
        return FALSE;
    }

    if (Parser->ParseOnly)
        return TRUE;

    if (_stricmp(Name, "$exp") == 0)
    {
        *Value = KdbpLastExpressionValue;
        return TRUE;
    }

    if (_stricmp(Name, "$entry") == 0)
    {
        *Value = KdbpListEntryValue;
        return TRUE;
    }

    if (_stricmp(Name, "$cpu") == 0)
    {
        *Value = KeGetCurrentProcessorNumber();
        return TRUE;
    }

    if (_stricmp(Name, "$prcb") == 0)
    {
        *Value = (ULONG_PTR)KeGetCurrentPrcb();
        return TRUE;
    }

    if ((_stricmp(Name, "$thread") == 0) || (_stricmp(Name, "$proc") == 0))
    {
        *Value = (_stricmp(Name, "$thread") == 0) ? (ULONG_PTR)KdbCurrentThread : (ULONG_PTR)KdbCurrentProcess;
        return TRUE;
    }

    if ((_stricmp(Name, "$tid") == 0) || (_stricmp(Name, "$teb") == 0))
    {
        if ((KdbCurrentThread == NULL) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Pointer,
                                           (_stricmp(Name, "$tid") == 0) ? (PVOID)&KdbCurrentThread->Cid.UniqueThread
                                                                         : (PVOID)&KdbCurrentThread->Tcb.Teb,
                                           sizeof(Pointer))))
        {
            KdbpFail(Parser, "the thread looked at cannot be read");
            return TRUE;
        }

        *Value = (ULONG_PTR)Pointer;
        return TRUE;
    }

    if ((_stricmp(Name, "$pid") == 0) || (_stricmp(Name, "$peb") == 0))
    {
        if ((KdbCurrentProcess == NULL) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Pointer,
                                           (_stricmp(Name, "$pid") == 0) ? (PVOID)&KdbCurrentProcess->UniqueProcessId
                                                                         : (PVOID)&KdbCurrentProcess->Peb,
                                           sizeof(Pointer))))
        {
            KdbpFail(Parser, "the process looked at cannot be read");
            return TRUE;
        }

        *Value = (ULONG_PTR)Pointer;
        return TRUE;
    }

    if (Parser->TrapFrame == NULL)
    {
        KdbpFail(Parser, "there are no registers here");
        return TRUE;
    }

    if ((_stricmp(Name, "$ip") == 0) || (_stricmp(Name, "$pc") == 0))
    {
        *Value = (ULONG_PTR)KeGetContextPc(Parser->TrapFrame);
    }
    else if (_stricmp(Name, "$sp") == 0)
    {
        *Value = (ULONG_PTR)KeGetContextStackRegister(Parser->TrapFrame);
    }
    else if (_stricmp(Name, "$fp") == 0)
    {
        *Value = (ULONG_PTR)KeGetContextFrameRegister(Parser->TrapFrame);
    }
    else
    {
        /* Where the function the registers are in returns to */
        Caller = *Parser->TrapFrame;
        if (!KiUnwindFrame(&Caller, FALSE))
        {
            KdbpFail(Parser, "the return address cannot be found");
            return TRUE;
        }

        *Value = (ULONG_PTR)KeGetContextPc(&Caller);
    }

    return TRUE;
}

/* The functions that read memory of a given width: poi reads a pointer */
static
ULONG
KdbpReadFunctionSize(
    _In_ PCSTR Name)
{
    if (_stricmp(Name, "poi") == 0)
        return sizeof(ULONG_PTR);
    if (_stricmp(Name, "by") == 0)
        return sizeof(UCHAR);
    if (_stricmp(Name, "wo") == 0)
        return sizeof(USHORT);
    if (_stricmp(Name, "dwo") == 0)
        return sizeof(ULONG);
    if (_stricmp(Name, "qwo") == 0)
        return sizeof(ULONG64);

    return 0;
}

static
BOOLEAN
KdbpIsRegisterName(
    _In_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < KdbpRegisterCount; Index++)
    {
        if (_stricmp(Name, KdbpRegisters[Index].Name) == 0)
            return TRUE;
    }

    return FALSE;
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
    {
        /* `name!=value` compares; it does not name a symbol of an image */
        if ((Parser->Current[0] == '!') && (Parser->Current[1] == '='))
            break;

        Name[Length++] = *Parser->Current++;
    }

    Name[Length] = ANSI_NULL;

    if (Length == 0)
    {
        KdbpFail(Parser, "expected a name");
        return 0;
    }

    if (KdbpIsNameChar(*Parser->Current) && (*Parser->Current != '!'))
    {
        KdbpFail(Parser, "the name is too long");
        return 0;
    }

    /* poi(address) and its narrower kin read memory */
    if (!Explicit && (KdbpReadFunctionSize(Name) != 0))
    {
        ULONG Size = KdbpReadFunctionSize(Name);

        KdbpSkipSpace(Parser);
        if (*Parser->Current != '(')
        {
            KdbpFail(Parser, "expected ( after the name of a function");
            return 0;
        }

        return KdbpReadValue(Parser, KdbpParsePrimary(Parser), Size);
    }

    if (!Explicit && (Name[0] == '$'))
    {
        if (!KdbpPseudoRegister(Parser, Name, &Value))
            KdbpFail(Parser, "no such pseudo-register");

        return Value;
    }

    if (Parser->ParseOnly)
    {
        if (Explicit && !KdbpIsRegisterName(Name))
            KdbpFail(Parser, "no such register");

        /* A symbol may only be found once its image is loaded */
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

    if (Parser->Error == NULL)
    {
        ULONG Index;

        for (Index = 0; (Index < Length) && isxdigit((UCHAR)Name[Index]); Index++);
        RtlStringCbPrintfA(Parser->ErrorText, sizeof(Parser->ErrorText),
                           (Index == Length) ? "no such register or symbol: %s; a hexadecimal number starts with 0x"
                                             : "no such register or symbol: %s",
                           Name);
        Parser->Current -= Length;
        KdbpFail(Parser, Parser->ErrorText);
    }
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
        if (Parser->Depth >= KDB_EXPRESSION_DEPTH_MAX)
        {
            KdbpFail(Parser, "the expression is nested too deeply");
            return 0;
        }

        Parser->Current++;
        Parser->Depth++;
        Value = KdbpParseBinary(Parser, 1);
        Parser->Depth--;

        if (!KdbpTakeOperator(Parser, ")"))
            KdbpFail(Parser, "expected a closing parenthesis");

        return Value;
    }

    if ((*Parser->Current >= '0') && (*Parser->Current <= '9'))
        return KdbpParseNumber(Parser);

    return KdbpParseName(Parser);
}

/*
 * The operators written in front of a value. They are gathered first and
 * applied from the innermost out, so a long run of them costs no stack.
 */
static
ULONGLONG
KdbpParseUnary(
    _Inout_ PKDB_PARSER Parser)
{
    CHAR Prefixes[KDB_EXPRESSION_DEPTH_MAX];
    ULONG Count = 0;
    ULONGLONG Value;

    for (;;)
    {
        KdbpSkipSpace(Parser);

        if ((*Parser->Current != '-') && (*Parser->Current != '+') && (*Parser->Current != '~') &&
            (*Parser->Current != '!') && (*Parser->Current != '*'))
        {
            break;
        }

        if (Count + Parser->Depth >= KDB_EXPRESSION_DEPTH_MAX)
        {
            KdbpFail(Parser, "the expression is nested too deeply");
            return 0;
        }

        Prefixes[Count++] = *Parser->Current++;
    }

    Value = KdbpParsePrimary(Parser);

    while ((Count != 0) && (Parser->Error == NULL))
    {
        switch (Prefixes[--Count])
        {
            case '-':
                Value = (ULONGLONG)(0 - (LONGLONG)Value);
                break;
            case '~':
                Value = ~Value;
                break;
            case '!':
                Value = (Value == 0) ? 1 : 0;
                break;
            case '*':
                /* A star reads what the address it is given points at */
                Value = KdbpReadValue(Parser, Value, sizeof(ULONG_PTR));
                break;
            default:
                break;
        }
    }

    return Value;
}

static
const KDB_BINARY_OPERATOR *
KdbpPeekBinaryOperator(
    _Inout_ PKDB_PARSER Parser)
{
    ULONG Index;

    KdbpSkipSpace(Parser);

    for (Index = 0; Index < RTL_NUMBER_OF(KdbpBinaryOperators); Index++)
    {
        if (strncmp(Parser->Current, KdbpBinaryOperators[Index].Text, KdbpBinaryOperators[Index].Length) == 0)
            return &KdbpBinaryOperators[Index];
    }

    return NULL;
}

static
ULONGLONG
KdbpApplyBinary(
    _Inout_ PKDB_PARSER Parser,
    _In_ CHAR Operator,
    _In_ ULONGLONG Left,
    _In_ ULONGLONG Right)
{
    switch (Operator)
    {
        case 'o': return ((Left != 0) || (Right != 0)) ? 1 : 0;
        case 'a': return ((Left != 0) && (Right != 0)) ? 1 : 0;
        case '|': return Left | Right;
        case '^': return Left ^ Right;
        case '&': return Left & Right;
        case 'e': return (Left == Right) ? 1 : 0;
        case 'n': return (Left != Right) ? 1 : 0;
        case '<': return (Left < Right) ? 1 : 0;
        case '>': return (Left > Right) ? 1 : 0;
        case 'l': return (Left <= Right) ? 1 : 0;
        case 'g': return (Left >= Right) ? 1 : 0;
        case 'L': return Left << (Right & 63);
        case 'R': return Left >> (Right & 63);
        case '+': return Left + Right;
        case '-': return Left - Right;
        case '*': return Left * Right;
        case '/':
        case '%':
            if (Right == 0)
            {
                /* While only the text is checked every value is zero */
                if (!Parser->ParseOnly)
                    KdbpFail(Parser, "division by zero");
                return 0;
            }
            return (Operator == '/') ? (Left / Right) : (Left % Right);
        default:
            return 0;
    }
}

/*
 * The binary operators, by precedence climbing: the operators of C, with C's
 * precedence and left to right. Both sides of && and || are always worked out:
 * a debugger expression has nothing to make happen.
 */
static
ULONGLONG
KdbpParseBinary(
    _Inout_ PKDB_PARSER Parser,
    _In_ ULONG MinimumPrecedence)
{
    ULONGLONG Value = KdbpParseUnary(Parser);

    while (Parser->Error == NULL)
    {
        const KDB_BINARY_OPERATOR *Operator = KdbpPeekBinaryOperator(Parser);
        ULONGLONG Right;

        if ((Operator == NULL) || (Operator->Precedence < MinimumPrecedence))
            break;

        Parser->Current += Operator->Length;

        /* The right-hand side binds only what binds tighter, which keeps the operators left to right */
        Right = KdbpParseBinary(Parser, Operator->Precedence + 1);
        if (Parser->Error != NULL)
            break;

        Value = KdbpApplyBinary(Parser, Operator->Operator, Value, Right);
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

static
BOOLEAN
KdbpParseWhole(
    _Inout_ PKDB_PARSER Parser,
    _In_ PCSTR Expression,
    _In_opt_ PKDB_KTRAP_FRAME TrapFrame,
    _In_ BOOLEAN ParseOnly,
    _Out_ PULONGLONG Result,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg)
{
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

    Parser->Start = Expression;
    Parser->Current = Expression;
    Parser->TrapFrame = TrapFrame;
    Parser->Error = NULL;
    Parser->ErrorAt = NULL;
    Parser->Depth = 0;
    Parser->ParseOnly = ParseOnly;

    Value = KdbpParseBinary(Parser, 1);

    KdbpSkipSpace(Parser);

    /* Anything left over means the expression was not understood as written */
    if ((Parser->Error == NULL) && (*Parser->Current != ANSI_NULL))
        KdbpFail(Parser, "unexpected text after the expression");

    if (Parser->Error != NULL)
    {
        KdbpReportError(Parser, ErrOffset, ErrMsg);
        return FALSE;
    }

    *Result = Value;
    return TRUE;
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

    return KdbpParseWhole(&Parser, Expression, TrapFrame, FALSE, Result, ErrOffset, ErrMsg);
}

/*
 * Checks that an expression can be understood, for a caller that will come
 * back to it later, at a place whose registers and memory are not known yet.
 * What comes back is the text itself, validated.
 */
PVOID
KdbpRpnParseExpression(
    _In_ PCHAR Expression,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg)
{
    KDB_PARSER Parser;
    ULONGLONG Ignored;

    if (!KdbpParseWhole(&Parser, Expression, NULL, TRUE, &Ignored, ErrOffset, ErrMsg))
        return NULL;

    return Expression;
}

/* Remembers what ? showed last, for $exp */
VOID
KdbpSetLastExpressionValue(
    _In_ ULONGLONG Value)
{
    KdbpLastExpressionValue = Value;
}

/* The entry a list walk is at, for $entry */
VOID
KdbpSetListEntryValue(
    _In_ ULONGLONG Value)
{
    KdbpListEntryValue = Value;
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
