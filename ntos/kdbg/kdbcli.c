/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The prompt the machine is driven from while it is stopped
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <kdros.h>
#include "kdb.h"
#include "kdbpriv.h"
#include "../kd/kdterminal.h"
#define NDEBUG
#include <debug.h>

/* The extensions other components bring to the prompt */
#ifdef KDBG
BOOLEAN ExpKdbgExtFileCache(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtDefWrites(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPte(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPfn(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtVad(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtAddress(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtVm(ULONG Argc, PCHAR Argv[]);
#if DBG
BOOLEAN ExpKdbgExtHandle(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPool(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPoolUsed(ULONG Argc, PCHAR Argv[]);
BOOLEAN ExpKdbgExtPoolFind(ULONG Argc, PCHAR Argv[]);
#endif
#endif

/* GLOBALS ********************************************************************/

/* The commands to run the first time the machine stops, if any were found */
volatile PCHAR KdbInitFileBuffer = NULL;

/* PRIVATE DATA ***************************************************************/

#define KDB_COMMAND_MAX         1024
#define KDB_ARGUMENT_MAX        32
#define KDB_CALLBACK_MAX        8
#define KDB_DEFAULT_SCREEN_ROWS 24
#define KDB_DUMP_BYTES_PER_LINE 16
#define KDB_LOG_CHUNK           512

typedef BOOLEAN (*PKDB_COMMAND_ROUTINE)(ULONG Argc, PCHAR Argv[]);

typedef struct _KDB_COMMAND
{
    PCSTR Name;
    PCSTR Syntax;
    PCSTR Help;
    PKDB_COMMAND_ROUTINE Routine;
} KDB_COMMAND, *PKDB_COMMAND;

/* How much of the screen has been used since the last prompt */
static ULONG KdbpLinesPrinted = 0;
static BOOLEAN KdbpOutputAborted = FALSE;

/* Whether the last command asked for the machine to run again */
static BOOLEAN KdbpLeaveLoop = FALSE;

/* Set once the init file has been run, so it only runs once */
static BOOLEAN KdbpInitFileInterpreted = FALSE;

/* Whether a bug check was asked for at the prompt */

static PVOID KdbpCliCallbacks[KDB_CALLBACK_MAX];

/* Where `x` and `u` carry on from when they are given no address */
static ULONG_PTR KdbpLastDumpAddress = 0;
static ULONG_PTR KdbpLastDisassembleAddress = 0;

static CHAR KdbpCommandBuffer[KDB_COMMAND_MAX];
static CHAR KdbpPrintBuffer[1024];

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The context the commands look at: whatever thread the debugger has been
 * pointed at, which is the one that stopped unless it was told otherwise.
 */
static
PKDB_KTRAP_FRAME
KdbpContext(VOID)
{
    return KdbCurrentTrapFrame;
}

static
ULONG
KdbpScreenRows(VOID)
{
    if ((KdTermSize.cy > 2) && (KdTermSize.cy < 1000))
        return (ULONG)KdTermSize.cy;

    return KDB_DEFAULT_SCREEN_ROWS;
}

/* PUBLIC FUNCTIONS: OUTPUT ***************************************************/

VOID
KdbpPagerReset(VOID)
{
    KdbpLinesPrinted = 0;
    KdbpOutputAborted = FALSE;
}

BOOLEAN
KdbpIsOutputAborted(VOID)
{
    return KdbpOutputAborted;
}

/*
 * Shows a block of text, stopping when the screen is full so that what has
 * already gone past can be read. The person reading may give up on the rest.
 */
VOID
KdbpPager(
    _In_ PCHAR Buffer,
    _In_ ULONG BufLength)
{
    ULONG Rows = KdbpScreenRows();
    ULONG Start = 0;
    ULONG Index;

    if ((Buffer == NULL) || (BufLength == 0))
        return;

    for (Index = 0; Index < BufLength; Index++)
    {
        if (Buffer[Index] != '\n')
            continue;

        /* One line at a time, so the count is right even in a long block */
        KdpProvidersPrint(&Buffer[Start], Index - Start + 1);
        Start = Index + 1;

        KdbpLinesPrinted++;

        /* Only worth pausing while somebody is there to press a key */
        if (!KdbActive || (KdbpLinesPrinted + 1 < Rows))
            continue;

        KdIoPuts("--- Press any key to go on, q to stop ---");

        {
            ULONG ScanCode;
            CHAR Key = KdpReadTermKey(&ScanCode);

            KdIoPuts("\r                                        \r");

            if ((Key == 'q') || (Key == 'Q') || (Key == 0x03))
            {
                KdbpOutputAborted = TRUE;
                return;
            }
        }

        KdbpLinesPrinted = 0;
    }

    if (Start < BufLength)
        KdpProvidersPrint(&Buffer[Start], BufLength - Start);
}

/*
 * Everything the debugger prints arrives here. What is being thrown away
 * because the reader gave up is dropped, and the rest is paged.
 */
VOID
KdbpCaptureOutput(
    _In_ PCCH String,
    _In_ USHORT Length)
{
    if (KdbpOutputAborted)
        return;

    KdbpPager((PCHAR)String, Length);
}

VOID
KdbpPrint(
    _In_ PSTR Format,
    _In_ ...)
{
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = _vsnprintf(KdbpPrintBuffer,
                        sizeof(KdbpPrintBuffer) - 1,
                        Format,
                        Arguments);
    va_end(Arguments);

    if ((Length < 0) || (Length > (int)sizeof(KdbpPrintBuffer) - 1))
        Length = (int)sizeof(KdbpPrintBuffer) - 1;

    KdbpPrintBuffer[Length] = ANSI_NULL;
    KdbPutsN(KdbpPrintBuffer, (USHORT)Length);
}

/*
 * A counted wide string, as much of it as is printable. A debugger shows what
 * is there rather than refusing a string that is not valid.
 */
VOID
KdbpPrintUnicodeString(
    _In_ PCUNICODE_STRING String)
{
    USHORT Index;

    if ((String == NULL) || (String->Buffer == NULL) || (String->Length == 0))
    {
        KdbPuts("(null)");
        return;
    }

    for (Index = 0; Index < String->Length / sizeof(WCHAR); Index++)
    {
        WCHAR Char = String->Buffer[Index];
        CHAR Text[2];

        Text[0] = ((Char >= ' ') && (Char < 0x7F)) ? (CHAR)Char : '.';
        Text[1] = ANSI_NULL;

        KdbPuts(Text);
    }
}

/* PUBLIC FUNCTIONS: PARSING **************************************************/

/*
 * A plain hexadecimal number, which is what the components that bring their
 * own commands ask their arguments to be.
 */
BOOLEAN
NTAPI
KdbpGetHexNumber(
    _In_ PCHAR pszNum,
    _Out_ ULONG_PTR *pulValue)
{
    ULONG_PTR Value = 0;
    PCHAR Current = pszNum;
    BOOLEAN Any = FALSE;

    *pulValue = 0;

    if (pszNum == NULL)
        return FALSE;

    while ((*Current == ' ') || (*Current == '\t'))
        Current++;

    if ((Current[0] == '0') && ((Current[1] == 'x') || (Current[1] == 'X')))
        Current += 2;

    for (;;)
    {
        ULONG Digit;

        if ((*Current >= '0') && (*Current <= '9'))
            Digit = (ULONG)(*Current - '0');
        else if ((*Current >= 'a') && (*Current <= 'f'))
            Digit = (ULONG)(*Current - 'a') + 10;
        else if ((*Current >= 'A') && (*Current <= 'F'))
            Digit = (ULONG)(*Current - 'A') + 10;
        else
            break;

        Value = (Value * 16) + Digit;
        Any = TRUE;
        Current++;
    }

    if (!Any)
        return FALSE;

    /* Trailing space is allowed; trailing anything else is not a number */
    while ((*Current == ' ') || (*Current == '\t'))
        Current++;

    if (*Current != ANSI_NULL)
        return FALSE;

    *pulValue = Value;
    return TRUE;
}

/*
 * An address, which may be written as a number, a symbol, a register or any
 * expression over them.
 */
BOOLEAN
NTAPI
KdbpGetAddressExpression(
    _In_ PCHAR Expression,
    _Out_ PULONG_PTR Address)
{
    ULONGLONG Value = 0;
    CHAR ErrorMessage[128];
    LONG ErrorOffset = 0;

    *Address = 0;

    if (!KdbpRpnEvaluateExpression(Expression,
                                   KdbpContext(),
                                   &Value,
                                   &ErrorOffset,
                                   ErrorMessage))
    {
        KdbpPrint("%s at offset %ld\n", ErrorMessage, ErrorOffset);
        return FALSE;
    }

    *Address = (ULONG_PTR)Value;
    return TRUE;
}

/*
 * Lets a component add a command of its own. The callbacks are tried after
 * every command the debugger knows itself.
 */
BOOLEAN
NTAPI
KdbRegisterCliCallback(
    PVOID Callback,
    BOOLEAN Deregister)
{
    ULONG Index;

    if (Callback == NULL)
        return FALSE;

    if (Deregister)
    {
        for (Index = 0; Index < KDB_CALLBACK_MAX; Index++)
        {
            if (KdbpCliCallbacks[Index] == Callback)
            {
                KdbpCliCallbacks[Index] = NULL;
                return TRUE;
            }
        }

        return FALSE;
    }

    for (Index = 0; Index < KDB_CALLBACK_MAX; Index++)
    {
        if (KdbpCliCallbacks[Index] == NULL)
        {
            KdbpCliCallbacks[Index] = Callback;
            return TRUE;
        }
    }

    return FALSE;
}

/* COMMANDS *******************************************************************/

static BOOLEAN KdbpCmdHelp(ULONG Argc, PCHAR Argv[]);

static
BOOLEAN
KdbpCmdContinue(
    ULONG Argc,
    PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    KdbpSetContinueType(kdContinue);
    KdbpLeaveLoop = TRUE;
    return TRUE;
}

/*
 * Lets the exception go on being an exception: whatever would have happened
 * without a debugger happens now.
 */
static
BOOLEAN
KdbpCmdHandle(
    ULONG Argc,
    PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    KdbpSetContinueType(kdHandleException);
    KdbpLeaveLoop = TRUE;
    return TRUE;
}

static
BOOLEAN
KdbpCmdStep(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Count = 1;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Count))
        return TRUE;

    if (Count == 0)
        Count = 1;

    if (!NT_SUCCESS(KdbpArchSetSingleStep(KdbpContext(), TRUE)))
    {
        KdbPuts("Stepping is not supported on this processor\n");
        return TRUE;
    }

    KdbNumSingleSteps = (ULONG)Count;
    KdbSingleStepOver = FALSE;

    KdbpSetContinueType(kdContinue);
    KdbpLeaveLoop = TRUE;
    return TRUE;
}

/*
 * Runs to just after the current instruction, which steps over a call rather
 * than into it.
 */
static
BOOLEAN
KdbpCmdNext(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Pc;
    LONG Length;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    if (KdbpContext() == NULL)
        return TRUE;

    Pc = (ULONG_PTR)KeGetContextPc(KdbpContext());
    Length = KdbpGetInstLength(Pc);

    if (Length <= 0)
    {
        /* Nothing can be said about the instruction, so step into it */
        return KdbpCmdStep(1, Argv);
    }

    Status = KdbpInsertBreakPoint(Pc + (ULONG_PTR)Length,
                                  KdbBreakPointTemporary,
                                  0,
                                  KdbAccessExec,
                                  NULL,
                                  TRUE,
                                  NULL);
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("Cannot set a breakpoint after this instruction (%08lx)\n",
                  Status);
        return TRUE;
    }

    KdbSingleStepOver = TRUE;
    KdbpSetContinueType(kdContinue);
    KdbpLeaveLoop = TRUE;
    return TRUE;
}

static
PCSTR
KdbpAccessTypeName(
    _In_ KDB_ACCESS_TYPE AccessType)
{
    switch (AccessType)
    {
        case KdbAccessRead: return "read";
        case KdbAccessWrite: return "write";
        case KdbAccessReadWrite: return "read/write";
        default: return "exec";
    }
}

static
BOOLEAN
KdbpCmdBreakPointList(
    ULONG Argc,
    PCHAR Argv[])
{
    LONG Index = KdbpGetNextBreakPointNr(0);
    BOOLEAN Any = FALSE;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    while (Index >= 0)
    {
        ULONG_PTR Address = 0;
        KDB_BREAKPOINT_TYPE Type = KdbBreakPointNone;
        KDB_ACCESS_TYPE AccessType = KdbAccessExec;
        UCHAR Size = 0;
        UCHAR DebugReg = 0;
        BOOLEAN Enabled = FALSE;
        BOOLEAN Global = FALSE;
        PEPROCESS Process = NULL;
        PCHAR Condition = NULL;

        if (KdbpGetBreakPointInfo((ULONG)Index,
                                  &Address,
                                  &Type,
                                  &Size,
                                  &AccessType,
                                  &DebugReg,
                                  &Enabled,
                                  &Global,
                                  &Process,
                                  &Condition))
        {
            Any = TRUE;

            KdbpPrint("%ld  %s  %-8s  ",
                      Index,
                      Enabled ? "enabled " : "disabled",
                      (Type == KdbBreakPointHardware) ? "memory" :
                      ((Type == KdbBreakPointTemporary) ? "once" : "code"));

            KdbpPrint("%p ", (PVOID)Address);
            KdbSymPrintAddress((PVOID)Address, KdbpContext());

            if (Type == KdbBreakPointHardware)
            {
                KdbpPrint(" (%s, %u byte%s, register %u)",
                          KdbpAccessTypeName(AccessType),
                          Size,
                          (Size == 1) ? "" : "s",
                          DebugReg);
            }

            if (!Global)
                KdbpPrint(" in process %p", Process);

            if (Condition != NULL)
                KdbpPrint(" if %s", Condition);

            KdbPuts("\n");
        }

        Index = KdbpGetNextBreakPointNr((ULONG)Index + 1);
    }

    if (!Any)
        KdbPuts("No breakpoints are set\n");

    return TRUE;
}

static
BOOLEAN
KdbpCmdBreakPointSet(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address = 0;
    PCHAR Condition = NULL;
    LONG Number = -1;
    NTSTATUS Status;

    if (Argc < 2)
    {
        KdbPuts("An address is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    /* `bp <address> if <expression>` only stops when the condition holds */
    if ((Argc > 3) && (_stricmp(Argv[2], "if") == 0))
        Condition = Argv[3];

    Status = KdbpInsertBreakPoint(Address,
                                  KdbBreakPointSoftware,
                                  0,
                                  KdbAccessExec,
                                  Condition,
                                  TRUE,
                                  &Number);
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("The breakpoint was not set (%08lx)\n", Status);
        return TRUE;
    }

    KdbpPrint("Breakpoint %ld at %p\n", Number, (PVOID)Address);
    return TRUE;
}

static
BOOLEAN
KdbpCmdBreakPointMemory(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address = 0;
    ULONG_PTR Size = sizeof(ULONG_PTR);
    KDB_ACCESS_TYPE AccessType = KdbAccessReadWrite;
    LONG Number = -1;
    NTSTATUS Status;

    if (Argc < 3)
    {
        KdbPuts("An access type and an address are needed\n");
        return TRUE;
    }

    if (_stricmp(Argv[1], "r") == 0)
        AccessType = KdbAccessRead;
    else if (_stricmp(Argv[1], "w") == 0)
        AccessType = KdbAccessWrite;
    else if (_stricmp(Argv[1], "rw") == 0)
        AccessType = KdbAccessReadWrite;
    else if (_stricmp(Argv[1], "x") == 0)
        AccessType = KdbAccessExec;
    else
    {
        KdbPuts("The access type is one of r, w, rw or x\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[2], &Address))
        return TRUE;

    if ((Argc > 3) && !KdbpGetAddressExpression(Argv[3], &Size))
        return TRUE;

    Status = KdbpInsertBreakPoint(Address,
                                  KdbBreakPointHardware,
                                  (UCHAR)Size,
                                  AccessType,
                                  NULL,
                                  TRUE,
                                  &Number);
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("The breakpoint was not set (%08lx)\n", Status);
        return TRUE;
    }

    KdbpPrint("Breakpoint %ld watching %p\n", Number, (PVOID)Address);
    return TRUE;
}

/*
 * One command for the three things that can be done to an existing
 * breakpoint, since all three take the same argument.
 */
static
BOOLEAN
KdbpBreakPointAction(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ ULONG Action)
{
    ULONG_PTR Number;
    LONG Index;

    if (Argc < 2)
    {
        KdbPuts("A breakpoint number, or *, is needed\n");
        return TRUE;
    }

    if (strcmp(Argv[1], "*") == 0)
    {
        for (Index = KdbpGetNextBreakPointNr(0);
             Index >= 0;
             Index = KdbpGetNextBreakPointNr((ULONG)Index + 1))
        {
            if (Action == 0)
            {
                KdbpDeleteBreakPoint(Index, NULL);

                /* The numbers shift as they go, so start over */
                Index = -1;
                continue;
            }

            if (Action == 1)
                KdbpDisableBreakPoint(Index, NULL);
            else
                KdbpEnableBreakPoint(Index, NULL);
        }

        return TRUE;
    }

    if (!KdbpGetHexNumber(Argv[1], &Number))
    {
        KdbpPrint("%s is not a breakpoint number\n", Argv[1]);
        return TRUE;
    }

    if (Action == 0)
    {
        if (!KdbpDeleteBreakPoint((LONG)Number, NULL))
            KdbpPrint("There is no breakpoint %s\n", Argv[1]);
    }
    else if (Action == 1)
    {
        if (!KdbpDisableBreakPoint((LONG)Number, NULL))
            KdbpPrint("There is no breakpoint %s\n", Argv[1]);
    }
    else
    {
        if (!KdbpEnableBreakPoint((LONG)Number, NULL))
            KdbpPrint("There is no breakpoint %s\n", Argv[1]);
    }

    return TRUE;
}

static
BOOLEAN
KdbpCmdBreakPointClear(ULONG Argc, PCHAR Argv[])
{
    return KdbpBreakPointAction(Argc, Argv, 0);
}

static
BOOLEAN
KdbpCmdBreakPointDisable(ULONG Argc, PCHAR Argv[])
{
    return KdbpBreakPointAction(Argc, Argv, 1);
}

static
BOOLEAN
KdbpCmdBreakPointEnable(ULONG Argc, PCHAR Argv[])
{
    return KdbpBreakPointAction(Argc, Argv, 2);
}

static
BOOLEAN
KdbpCmdRegisters(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_KTRAP_FRAME Context = KdbpContext();
    ULONG Index;
    ULONG OnThisLine = 0;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    if (Context == NULL)
    {
        KdbPuts("There are no registers to show\n");
        return TRUE;
    }

    for (Index = 0; Index < KdbpRegisterCount; Index++)
    {
        ULONGLONG Value = 0;
        ULONG Size = 0;

        /*
         * The last few entries are the names that mean the same thing on
         * every processor, and they say nothing new here.
         */
        if ((strcmp(KdbpRegisters[Index].Name, "pc") == 0) ||
            (strcmp(KdbpRegisters[Index].Name, "sp") == 0) ||
            (strcmp(KdbpRegisters[Index].Name, "fp") == 0) ||
            (strcmp(KdbpRegisters[Index].Name, "ret") == 0))
        {
            continue;
        }

        if (!NT_SUCCESS(KdbpGetRegisterValue(Context,
                                             KdbpRegisters[Index].Name,
                                             &Value,
                                             &Size)))
        {
            continue;
        }

        if (Size <= sizeof(ULONG))
            KdbpPrint("%-7s %08lx   ", KdbpRegisters[Index].Name, (ULONG)Value);
        else
            KdbpPrint("%-7s %016I64x   ", KdbpRegisters[Index].Name, Value);

        if (++OnThisLine == 3)
        {
            KdbPuts("\n");
            OnThisLine = 0;
        }
    }

    if (OnThisLine != 0)
        KdbPuts("\n");

    return TRUE;
}

static
BOOLEAN
KdbpCmdRegister(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_KTRAP_FRAME Context = KdbpContext();
    ULONGLONG Value = 0;
    ULONG Size = 0;

    if (Context == NULL)
        return TRUE;

    if (Argc < 2)
        return KdbpCmdRegisters(Argc, Argv);

    /* `r <name> <expression>` changes it; `r <name>` only reads it */
    if (Argc >= 3)
    {
        ULONG_PTR New = 0;

        if (!KdbpGetAddressExpression(Argv[2], &New))
            return TRUE;

        if (!NT_SUCCESS(KdbpSetRegisterValue(Context, Argv[1], (ULONGLONG)New)))
        {
            KdbpPrint("There is no register called %s\n", Argv[1]);
            return TRUE;
        }
    }

    if (!NT_SUCCESS(KdbpGetRegisterValue(Context, Argv[1], &Value, &Size)))
    {
        KdbpPrint("There is no register called %s\n", Argv[1]);
        return TRUE;
    }

    KdbpPrint("%s = %016I64x\n", Argv[1], Value);
    return TRUE;
}

/*
 * Walks back through the frames that led here. The chain is read out of the
 * stack, so a stack that has been overwritten stops the walk rather than
 * sending it anywhere.
 */
static
BOOLEAN
KdbpCmdBackTrace(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_KTRAP_FRAME Context = KdbpContext();
    ULONG_PTR Limit = 32;
    ULONG_PTR Frame;
    ULONG_PTR Pc;
    ULONG Depth = 0;

    if (Context == NULL)
        return TRUE;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Limit))
        return TRUE;

    Pc = (ULONG_PTR)KeGetContextPc(Context);
    Frame = (ULONG_PTR)KeGetContextFrameRegister(Context);

    KdbpPrint("%p ", (PVOID)Pc);
    KdbSymPrintAddress((PVOID)Pc, Context);
    KdbPuts("\n");

    while ((Depth < Limit) && (Frame != 0) && !KdbpIsOutputAborted())
    {
        ULONG_PTR Next = 0;
        ULONG_PTR Return = 0;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Next,
                                           (PVOID)Frame,
                                           sizeof(Next))))
        {
            break;
        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Return,
                                           (PVOID)(Frame + sizeof(ULONG_PTR)),
                                           sizeof(Return))))
        {
            break;
        }

        if (Return == 0)
            break;

        KdbpPrint("%p ", (PVOID)Return);
        KdbSymPrintAddress((PVOID)Return, Context);
        KdbPuts("\n");

        /* A chain that does not climb is not a chain */
        if (Next <= Frame)
            break;

        Frame = Next;
        Depth++;
    }

    return TRUE;
}

/*
 * Shows memory. The letter after the command says how wide each value is, and
 * `a` asks for the bytes to be shown as text.
 */
static
BOOLEAN
KdbpDumpMemory(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ ULONG Width,
    _In_ BOOLEAN AsText)
{
    ULONG_PTR Address = KdbpLastDumpAddress;
    ULONG_PTR Count = 8;
    ULONG_PTR Index;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if ((Argc > 2) && !KdbpGetAddressExpression(Argv[2], &Count))
        return TRUE;

    if (Count == 0)
        Count = 1;

    if (AsText)
    {
        ULONG Column = 0;

        for (Index = 0; Index < Count; Index++)
        {
            UCHAR Byte = 0;
            CHAR Text[2];

            if (Column == 0)
                KdbpPrint("%p  ", (PVOID)(Address + Index));

            if (!NT_SUCCESS(KdbpSafeReadMemory(&Byte,
                                               (PVOID)(Address + Index),
                                               sizeof(Byte))))
            {
                KdbPuts("??\n");
                break;
            }

            Text[0] = ((Byte >= ' ') && (Byte < 0x7F)) ? (CHAR)Byte : '.';
            Text[1] = ANSI_NULL;
            KdbPuts(Text);

            if (++Column == KDB_DUMP_BYTES_PER_LINE * 2)
            {
                KdbPuts("\n");
                Column = 0;
            }

            if (KdbpIsOutputAborted())
                break;
        }

        if (Column != 0)
            KdbPuts("\n");

        KdbpLastDumpAddress = Address + Count;
        return TRUE;
    }

    for (Index = 0; Index < Count; Index++)
    {
        ULONG_PTR At = Address + (Index * Width);
        ULONGLONG Value = 0;

        if ((Index % (KDB_DUMP_BYTES_PER_LINE / Width)) == 0)
        {
            if (Index != 0)
                KdbPuts("\n");

            KdbpPrint("%p  ", (PVOID)At);
        }

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Value, (PVOID)At, Width)))
        {
            KdbPuts(" ????");
            break;
        }

        switch (Width)
        {
            case 1: KdbpPrint("%02x ", (ULONG)(UCHAR)Value); break;
            case 2: KdbpPrint("%04x ", (ULONG)(USHORT)Value); break;
            case 4: KdbpPrint("%08lx ", (ULONG)Value); break;
            default: KdbpPrint("%016I64x ", Value); break;
        }

        if (KdbpIsOutputAborted())
            break;
    }

    KdbPuts("\n");

    KdbpLastDumpAddress = Address + (Count * Width);
    return TRUE;
}

static BOOLEAN KdbpCmdDumpBytes(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 1, FALSE);
}

static BOOLEAN KdbpCmdDumpWords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 2, FALSE);
}

static BOOLEAN KdbpCmdDumpDwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 4, FALSE);
}

static BOOLEAN KdbpCmdDumpQwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 8, FALSE);
}

static BOOLEAN KdbpCmdDumpText(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 1, TRUE);
}

static BOOLEAN KdbpCmdDumpPointers(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, sizeof(ULONG_PTR), FALSE);
}

/*
 * Changes memory. The width is the width of one value, and the values are
 * written one after the other from the address given.
 */
static
BOOLEAN
KdbpEditMemory(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ ULONG Width)
{
    ULONG_PTR Address = 0;
    ULONG Index;

    if (Argc < 3)
    {
        KdbPuts("An address and at least one value are needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    for (Index = 2; Index < Argc; Index++)
    {
        ULONG_PTR Value = 0;
        ULONG_PTR At = Address + ((ULONG_PTR)(Index - 2) * Width);

        if (!KdbpGetAddressExpression(Argv[Index], &Value))
            return TRUE;

        if (!NT_SUCCESS(KdbpSafeWriteMemory((PVOID)At, &Value, Width)))
        {
            KdbpPrint("%p cannot be written\n", (PVOID)At);
            return TRUE;
        }
    }

    return TRUE;
}

static BOOLEAN KdbpCmdEditBytes(ULONG Argc, PCHAR Argv[])
{
    return KdbpEditMemory(Argc, Argv, 1);
}

static BOOLEAN KdbpCmdEditWords(ULONG Argc, PCHAR Argv[])
{
    return KdbpEditMemory(Argc, Argv, 2);
}

static BOOLEAN KdbpCmdEditDwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpEditMemory(Argc, Argv, 4);
}

static BOOLEAN KdbpCmdEditQwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpEditMemory(Argc, Argv, 8);
}

static
BOOLEAN
KdbpCmdDisassemble(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address = KdbpLastDisassembleAddress;
    ULONG_PTR Count = 8;
    ULONG_PTR Index;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if ((Argc > 2) && !KdbpGetAddressExpression(Argv[2], &Count))
        return TRUE;

    if ((Address == 0) && (KdbpContext() != NULL))
        Address = (ULONG_PTR)KeGetContextPc(KdbpContext());

    for (Index = 0; Index < Count; Index++)
    {
        LONG Length;

        KdbpPrint("%p ", (PVOID)Address);
        KdbSymPrintAddress((PVOID)Address, KdbpContext());
        KdbPuts(": ");

        Length = KdbpDisassemble(Address, 1);
        KdbPuts("\n");

        if (Length <= 0)
            break;

        Address += (ULONG_PTR)Length;

        if (KdbpIsOutputAborted())
            break;
    }

    KdbpLastDisassembleAddress = Address;
    return TRUE;
}

static
BOOLEAN
KdbpCmdModules(
    ULONG Argc,
    PCHAR Argv[])
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    INT Index = 0;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    while (KdbpSymFindModule(NULL, Index, &LdrEntry))
    {
        KdbpPrint("%p %08lx  ",
                  LdrEntry->DllBase,
                  LdrEntry->SizeOfImage);
        KdbpPrintUnicodeString(&LdrEntry->BaseDllName);
        KdbPuts("\n");

        Index++;

        if (KdbpIsOutputAborted())
            break;
    }

    if (Index == 0)
        KdbPuts("No modules are loaded\n");

    return TRUE;
}

static
BOOLEAN
NTAPI
KdbpSymbolPrintCallback(
    _In_ ULONG_PTR Address,
    _In_ PCSTR ModuleName,
    _In_ PCSTR FunctionName,
    _In_ PCSTR FileName,
    _In_ ULONG SourceLine,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(SourceLine);
    UNREFERENCED_PARAMETER(Context);

    KdbpPrint("%p %s!%s\n", (PVOID)Address, ModuleName, FunctionName);

    return !KdbpIsOutputAborted();
}

static
BOOLEAN
KdbpCmdSymbols(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG MatchCount = 0;
    BOOLEAN Truncated = FALSE;
    PCSTR ModulePattern = "*";
    PCSTR SymbolPattern;

    if (Argc < 2)
    {
        KdbPuts("A symbol name or pattern is needed\n");
        return TRUE;
    }

    SymbolPattern = Argv[1];

    if (Argc > 2)
    {
        ModulePattern = Argv[1];
        SymbolPattern = Argv[2];
    }

    KdbSymEnumerate(ModulePattern,
                    SymbolPattern,
                    256,
                    KdbpSymbolPrintCallback,
                    NULL,
                    &MatchCount,
                    &Truncated);

    KdbpPrint("%lu symbol%s%s\n",
              MatchCount,
              (MatchCount == 1) ? "" : "s",
              Truncated ? ", and more that were not shown" : "");

    return TRUE;
}

static
BOOLEAN
KdbpCmdProcesses(
    ULONG Argc,
    PCHAR Argv[])
{
    PLIST_ENTRY Entry;

    if (Argc > 1)
    {
        ULONG_PTR Id = 0;

        if (!KdbpGetHexNumber(Argv[1], &Id))
        {
            KdbpPrint("%s is not a process identifier\n", Argv[1]);
            return TRUE;
        }

        if (!KdbpAttachToProcess((PVOID)Id))
            KdbpPrint("There is no process %s, or it cannot be looked at\n",
                      Argv[1]);

        return TRUE;
    }

    for (Entry = PsActiveProcessHead.Flink;
         Entry != &PsActiveProcessHead;
         Entry = Entry->Flink)
    {
        PEPROCESS Process;

        Process = CONTAINING_RECORD(Entry, EPROCESS, ActiveProcessLinks);

        KdbpPrint("%s%p  %6Iu  %.15s\n",
                  (Process == KdbCurrentProcess) ? "*" : " ",
                  Process,
                  (ULONG_PTR)Process->UniqueProcessId,
                  Process->ImageFileName);

        if (KdbpIsOutputAborted())
            break;
    }

    return TRUE;
}

static
BOOLEAN
KdbpCmdThreads(
    ULONG Argc,
    PCHAR Argv[])
{
    PLIST_ENTRY Entry;
    PEPROCESS Process = KdbCurrentProcess;

    if (Argc > 1)
    {
        ULONG_PTR Id = 0;

        if (!KdbpGetHexNumber(Argv[1], &Id))
        {
            KdbpPrint("%s is not a thread identifier\n", Argv[1]);
            return TRUE;
        }

        if (!KdbpAttachToThread((PVOID)Id))
            KdbpPrint("There is no thread %s, or it cannot be looked at\n",
                      Argv[1]);

        return TRUE;
    }

    if (Process == NULL)
        return TRUE;

    for (Entry = Process->ThreadListHead.Flink;
         Entry != &Process->ThreadListHead;
         Entry = Entry->Flink)
    {
        PETHREAD Thread;

        Thread = CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry);

        KdbpPrint("%s%p  %6Iu  state %u\n",
                  (Thread == KdbCurrentThread) ? "*" : " ",
                  Thread,
                  (ULONG_PTR)Thread->Cid.UniqueThread,
                  Thread->Tcb.State);

        if (KdbpIsOutputAborted())
            break;
    }

    return TRUE;
}

static
BOOLEAN
KdbpCmdEvaluate(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Value = 0;

    if (Argc < 2)
    {
        KdbPuts("An expression is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Value))
        return TRUE;

    KdbpPrint("%Iu (0x%Ix) ", Value, Value);
    KdbSymPrintAddress((PVOID)Value, KdbpContext());
    KdbPuts("\n");

    return TRUE;
}

/*
 * Shows what the system has printed, which is kept whether or not anybody was
 * watching at the time.
 */
static
BOOLEAN
KdbpCmdLog(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Chunk[KDB_LOG_CHUNK];
    ULONG Length;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    Length = KdpCopyPrintBufferTail(Chunk, sizeof(Chunk));

    if (Length == 0)
    {
        KdbPuts("Nothing has been printed\n");
        return TRUE;
    }

    KdbpPager(Chunk, Length);
    KdbPuts("\n");

    return TRUE;
}

/*
 * Turns one component's output on or off while the machine is stopped, which
 * is the only moment at which a person can decide they want to see more.
 */
static
BOOLEAN
KdbpCmdFilter(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Component = 0;
    ULONG_PTR Level = 0;
    BOOLEAN State;

    if (Argc < 4)
    {
        KdbPuts("A component, a level and on or off are needed\n");
        return TRUE;
    }

    if (!KdbpGetHexNumber(Argv[1], &Component) ||
        !KdbpGetHexNumber(Argv[2], &Level))
    {
        KdbPuts("The component and the level are numbers\n");
        return TRUE;
    }

    State = (_stricmp(Argv[3], "on") == 0);

    if (!NT_SUCCESS(NtSetDebugFilterState((ULONG)Component,
                                          (ULONG)Level,
                                          State)))
    {
        KdbPuts("That component has no filter of its own\n");
    }

    return TRUE;
}

static
BOOLEAN
KdbpCmdBugCheck(
    ULONG Argc,
    PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    KdbpSetContinueType(kdHandleException);
    KdbpLeaveLoop = TRUE;

    KdbPuts("Crashing on request\n");
    KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0, 0, 0, 0);

    return TRUE;
}

static
BOOLEAN
KdbpCmdReboot(
    ULONG Argc,
    PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    KdbPuts("Restarting the machine\n");
    HalReturnToFirmware(HalRebootRoutine);

    return TRUE;
}

/*
 * Changes what the debugger stops for. An exception number of -1 means all of
 * them, which is what a person usually wants.
 */
static
BOOLEAN
KdbpCmdCondition(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Number = 0;
    BOOLEAN FirstChance;
    KDB_ENTER_CONDITION Condition;

    if (Argc < 4)
    {
        KdbPuts("An exception number (or *), first or last, "
                "and never, always, kmode or umode are needed\n");
        return TRUE;
    }

    if (strcmp(Argv[1], "*") == 0)
    {
        Number = (ULONG_PTR)-1;
    }
    else if (!KdbpGetHexNumber(Argv[1], &Number))
    {
        KdbPuts("The exception is given by number\n");
        return TRUE;
    }

    FirstChance = (_stricmp(Argv[2], "first") == 0);

    if (_stricmp(Argv[3], "never") == 0)
        Condition = KdbDoNotEnter;
    else if (_stricmp(Argv[3], "always") == 0)
        Condition = KdbEnterAlways;
    else if (_stricmp(Argv[3], "kmode") == 0)
        Condition = KdbEnterFromKmode;
    else if (_stricmp(Argv[3], "umode") == 0)
        Condition = KdbEnterFromUmode;
    else
    {
        KdbPuts("The condition is never, always, kmode or umode\n");
        return TRUE;
    }

    if (!KdbpSetEnterCondition((LONG)Number, FirstChance, Condition))
        KdbPuts("There is no such exception\n");

    return TRUE;
}

/* THE COMMANDS ***************************************************************/

static const KDB_COMMAND KdbpCommands[] =
{
    {"help", "help", "Lists what can be typed here", KdbpCmdHelp},
    {"?", "? <expression>", "Works out what an expression comes to", KdbpCmdEvaluate},
    {"cont", "cont", "Lets the machine go on", KdbpCmdContinue},
    {"g", "g", "Lets the machine go on", KdbpCmdContinue},
    {"handle", "handle", "Goes on and leaves the exception unhandled", KdbpCmdHandle},
    {"step", "step [count]", "Runs one instruction", KdbpCmdStep},
    {"t", "t [count]", "Runs one instruction", KdbpCmdStep},
    {"next", "next", "Runs one instruction, over a call", KdbpCmdNext},
    {"p", "p", "Runs one instruction, over a call", KdbpCmdNext},
    {"bl", "bl", "Lists the breakpoints", KdbpCmdBreakPointList},
    {"bp", "bp <address> [if <expr>]", "Stops when an address is reached", KdbpCmdBreakPointSet},
    {"bpm", "bpm <r|w|rw|x> <addr> [size]", "Stops when an address is used", KdbpCmdBreakPointMemory},
    {"bc", "bc <number|*>", "Gives up a breakpoint", KdbpCmdBreakPointClear},
    {"bd", "bd <number|*>", "Leaves a breakpoint in place but off", KdbpCmdBreakPointDisable},
    {"be", "be <number|*>", "Turns a breakpoint back on", KdbpCmdBreakPointEnable},
    {"regs", "regs", "Shows every register", KdbpCmdRegisters},
    {"r", "r <name> [value]", "Shows or changes one register", KdbpCmdRegister},
    {"bt", "bt [frames]", "Shows what led here", KdbpCmdBackTrace},
    {"k", "k [frames]", "Shows what led here", KdbpCmdBackTrace},
    {"db", "db <address> [count]", "Shows memory as bytes", KdbpCmdDumpBytes},
    {"dw", "dw <address> [count]", "Shows memory as words", KdbpCmdDumpWords},
    {"dd", "dd <address> [count]", "Shows memory as double words", KdbpCmdDumpDwords},
    {"dq", "dq <address> [count]", "Shows memory as quad words", KdbpCmdDumpQwords},
    {"dp", "dp <address> [count]", "Shows memory as pointers", KdbpCmdDumpPointers},
    {"da", "da <address> [count]", "Shows memory as text", KdbpCmdDumpText},
    {"eb", "eb <address> <value>...", "Changes bytes", KdbpCmdEditBytes},
    {"ew", "ew <address> <value>...", "Changes words", KdbpCmdEditWords},
    {"ed", "ed <address> <value>...", "Changes double words", KdbpCmdEditDwords},
    {"eq", "eq <address> <value>...", "Changes quad words", KdbpCmdEditQwords},
    {"u", "u [address] [count]", "Shows the instructions at an address", KdbpCmdDisassemble},
    {"mod", "mod", "Lists the loaded images", KdbpCmdModules},
    {"sym", "sym [module] <pattern>", "Looks for symbols", KdbpCmdSymbols},
    {"proc", "proc [id]", "Lists the processes, or looks at one", KdbpCmdProcesses},
    {"thread", "thread [id]", "Lists the threads, or looks at one", KdbpCmdThreads},
    {"dmesg", "dmesg", "Shows what the system has printed", KdbpCmdLog},
    {"filter", "filter <comp> <level> <on|off>", "Decides what is printed", KdbpCmdFilter},
    {"cond", "cond <nr|*> <first|last> <what>", "Decides what to stop for", KdbpCmdCondition},
    {"bugcheck", "bugcheck", "Crashes the machine on purpose", KdbpCmdBugCheck},
    {"reboot", "reboot", "Restarts the machine", KdbpCmdReboot},

#ifdef KDBG
    {"!pte", "!pte <address>", "Shows the page table entry for an address", ExpKdbgExtPte},
    {"!pfn", "!pfn <frame>", "Shows one page frame", ExpKdbgExtPfn},
    {"!vad", "!vad [address]", "Shows the address ranges of a process", ExpKdbgExtVad},
    {"!address", "!address [address]", "Says what an address is", ExpKdbgExtAddress},
    {"!vm", "!vm", "Shows what memory is being used for", ExpKdbgExtVm},
    {"!filecache", "!filecache", "Shows the file cache", ExpKdbgExtFileCache},
    {"!defwrites", "!defwrites", "Shows the writes being held back", ExpKdbgExtDefWrites},
#if DBG
    {"!handle", "!handle [address]", "Shows a handle table", ExpKdbgExtHandle},
    {"!pool", "!pool [address]", "Shows the pool, or one block of it", ExpKdbgExtPool},
    {"!poolused", "!poolused [tag]", "Shows what the pool is used for", ExpKdbgExtPoolUsed},
    {"!poolfind", "!poolfind <tag>", "Looks for pool blocks by tag", ExpKdbgExtPoolFind},
#endif
#endif

    {NULL, NULL, NULL, NULL}
};

static
BOOLEAN
KdbpCmdHelp(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG Index;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    for (Index = 0; KdbpCommands[Index].Name != NULL; Index++)
    {
        if (KdbpIsOutputAborted())
            break;

        KdbpPrint("%-28s %s\n",
                  KdbpCommands[Index].Syntax,
                  KdbpCommands[Index].Help);
    }

    return TRUE;
}

/* THE PROMPT *****************************************************************/

/*
 * Splits a command line into its words. The line is written through, so the
 * words point into it.
 */
static
ULONG
KdbpSplitCommand(
    _Inout_ PCHAR Command,
    _Out_writes_(KDB_ARGUMENT_MAX) PCHAR *Argv)
{
    ULONG Argc = 0;
    PCHAR Current = Command;

    while ((*Current != ANSI_NULL) && (Argc < KDB_ARGUMENT_MAX))
    {
        while ((*Current == ' ') || (*Current == '\t'))
            Current++;

        if (*Current == ANSI_NULL)
            break;

        Argv[Argc++] = Current;

        while ((*Current != ANSI_NULL) && (*Current != ' ') && (*Current != '\t'))
            Current++;

        if (*Current != ANSI_NULL)
            *Current++ = ANSI_NULL;
    }

    return Argc;
}

/*
 * Runs one command line. Returns TRUE when the machine is to run again.
 */
BOOLEAN
KdbpCliInterpretCommand(
    _In_ PCHAR Command)
{
    PCHAR Argv[KDB_ARGUMENT_MAX];
    ULONG Argc;
    ULONG Index;

    KdbpLeaveLoop = FALSE;

    /* The line is taken apart in a copy, since the words point into it */
    strncpy(KdbpCommandBuffer, Command, sizeof(KdbpCommandBuffer) - 1);
    KdbpCommandBuffer[sizeof(KdbpCommandBuffer) - 1] = ANSI_NULL;

    Argc = KdbpSplitCommand(KdbpCommandBuffer, Argv);
    if (Argc == 0)
        return FALSE;

    for (Index = 0; KdbpCommands[Index].Name != NULL; Index++)
    {
        if (_stricmp(Argv[0], KdbpCommands[Index].Name) != 0)
            continue;

        KdbpCommands[Index].Routine(Argc, Argv);
        return KdbpLeaveLoop;
    }

    /* Something a component added to the prompt, perhaps */
    for (Index = 0; Index < KDB_CALLBACK_MAX; Index++)
    {
        PKDBG_CLI_ROUTINE Routine;

        if (KdbpCliCallbacks[Index] == NULL)
            continue;

        Routine = (PKDBG_CLI_ROUTINE)KdbpCliCallbacks[Index];

        if (Routine(Command, Argc, Argv))
            return KdbpLeaveLoop;
    }

    KdbpPrint("%s is not something that can be typed here. "
              "Type help for what can.\n", Argv[0]);

    return FALSE;
}

/*
 * Runs the commands that were found in the start-up file. They run once, the
 * first time the machine stops.
 */
VOID
KdbpCliInterpretInitFile(VOID)
{
    PCHAR Current = (PCHAR)KdbInitFileBuffer;

    if ((Current == NULL) || KdbpInitFileInterpreted)
        return;

    KdbpInitFileInterpreted = TRUE;

    while (*Current != ANSI_NULL)
    {
        PCHAR Line = Current;

        while ((*Current != ANSI_NULL) && (*Current != '\n') && (*Current != '\r'))
            Current++;

        while ((*Current == '\n') || (*Current == '\r'))
            *Current++ = ANSI_NULL;

        /* A blank line or a remark says nothing */
        if ((*Line == ANSI_NULL) || (*Line == '#'))
            continue;

        KdbpPrint("kdb:> %s\n", Line);
        KdbpCliInterpretCommand(Line);
    }
}

/*
 * Says where the machine stopped.
 */
static
VOID
KdbpReportStop(
    _In_ BOOLEAN EnteredOnSingleStep)
{
    PKDB_KTRAP_FRAME Context = KdbpContext();

    if (EnteredOnSingleStep)
    {
        /* One instruction has run; where it stopped is all there is to say */
        if (Context != NULL)
        {
            ULONG_PTR Pc = (ULONG_PTR)KeGetContextPc(Context);

            KdbpPrint("%p ", (PVOID)Pc);
            KdbSymPrintAddress((PVOID)Pc, Context);
            KdbPuts(": ");
            KdbpDisassemble(Pc, 1);
            KdbPuts("\n");
        }

        return;
    }

    if (KdbLastBreakPointNr >= 0)
    {
        KdbpPrint("\nBreakpoint %ld\n", KdbLastBreakPointNr);
    }
    else if (KdbCurrentExceptionRecordValid)
    {
        KdbpPrint("\nException %08lx at ",
                  KdbCurrentExceptionRecord.ExceptionCode);
        KdbSymPrintAddress(
            (PVOID)(ULONG_PTR)KdbCurrentExceptionRecord.ExceptionAddress,
            Context);
        KdbPuts("\n");
    }
    else
    {
        KdbPuts("\nStopped\n");
    }

    if (Context != NULL)
    {
        ULONG_PTR Pc = (ULONG_PTR)KeGetContextPc(Context);

        KdbpPrint("%p ", (PVOID)Pc);
        KdbSymPrintAddress((PVOID)Pc, Context);
        KdbPuts("\n");
    }
}

/*
 * Where a person sits while the machine is stopped. Returns once a command
 * has asked for the machine to run again.
 */
VOID
KdbpCliMainLoop(
    _In_ BOOLEAN EnteredOnSingleStep)
{
    static CHAR Command[KDB_COMMAND_MAX];
    static CHAR Previous[KDB_COMMAND_MAX];

    KdbpPagerReset();
    KdbpReportStop(EnteredOnSingleStep);

    /* Whatever the machine was told to do at the first stop is done here */
    KdbpCliInterpretInitFile();

    for (;;)
    {
        SIZE_T Length;

        KdbpPagerReset();

        Length = KdbPrompt("\nkdb:> ", Command, sizeof(Command));

        /* An empty line means the last command again, as at any prompt */
        if (Length == 0)
        {
            if (Previous[0] == ANSI_NULL)
                continue;

            strncpy(Command, Previous, sizeof(Command) - 1);
            Command[sizeof(Command) - 1] = ANSI_NULL;
        }
        else
        {
            strncpy(Previous, Command, sizeof(Previous) - 1);
            Previous[sizeof(Previous) - 1] = ANSI_NULL;
            KdbpCommandHistoryAppend(Command);
        }

        if (KdbpCliInterpretCommand(Command))
            return;
    }
}

/* START-UP *******************************************************************/

NTSTATUS
KdbpCliInit(VOID)
{
    KdbpPagerReset();
    KdbpLastDumpAddress = 0;
    KdbpLastDisassembleAddress = 0;

    return STATUS_SUCCESS;
}

/*
 * Reads the start-up file, if there is one. It is read once the system root
 * is mounted, because that is the first moment there is a file to read.
 */
static
VOID
KdbpReadInitFile(VOID)
{
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(
        L"\\SystemRoot\\system32\\drivers\\etc\\kdbinit");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_STANDARD_INFORMATION Information;
    HANDLE Handle = NULL;
    PCHAR Buffer;
    ULONG Size;
    NTSTATUS Status;

    if (KdbInitFileBuffer != NULL)
        return;

    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
        return;

    Status = ZwQueryInformationFile(Handle,
                                    &IoStatusBlock,
                                    &Information,
                                    sizeof(Information),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status) ||
        (Information.EndOfFile.HighPart != 0) ||
        (Information.EndOfFile.LowPart == 0) ||
        (Information.EndOfFile.LowPart > 0x10000))
    {
        ZwClose(Handle);
        return;
    }

    Size = Information.EndOfFile.LowPart;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Size + 1, 'IBdK');
    if (Buffer == NULL)
    {
        ZwClose(Handle);
        return;
    }

    Status = ZwReadFile(Handle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        Buffer,
                        Size,
                        NULL,
                        NULL);
    ZwClose(Handle);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, 'IBdK');
        return;
    }

    Buffer[IoStatusBlock.Information] = ANSI_NULL;
    KdbInitFileBuffer = Buffer;
}

/*
 * The interactive debugger as one of the things the debug output can be sent
 * to. It is not a way out for output of its own, so it never joins the list
 * of providers; it only needs to know how far the boot has got.
 */
NTSTATUS
NTAPI
KdbInitialize(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    UNREFERENCED_PARAMETER(DispatchTable);

    if (BootPhase == 0)
        return KdbpCliInit();

    if (BootPhase == KDP_BOOT_PHASE_SYSTEM_ROOT)
        KdbpReadInitFile();

    return STATUS_SUCCESS;
}

/* EOF */
