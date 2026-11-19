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
#include "../kd64/kdpriv.h"
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

/* PRIVATE DATA ***************************************************************/

#define KDB_COMMAND_MAX         1024
#define KDB_ARGUMENT_MAX        32
#define KDB_CALLBACK_MAX        8
#define KDB_DEFAULT_SCREEN_ROWS 24
#define KDB_DUMP_BYTES_PER_LINE 16
#define KDB_MAXIMUM_FLOATING_REGISTER 16
#define KDB_MAXIMUM_THREADS     65536
#define KDB_MAXIMUM_COMMAND_DEPTH 16

/* The most one command shows or goes over, so that a typo in a count does not run for hours */
#define KDB_MAXIMUM_DUMP_BYTES  0x100000
#define KDB_MAXIMUM_INSTRUCTIONS 0x10000
#define KDB_DEFAULT_TEXT_LENGTH 256
#define KDB_TEXT_PER_LINE       48

/* As long as a breakpoint's condition may be; the breakpoint keeps a copy of it */
#define KDB_CONDITION_MAX       128

/* How many commands may be run at every stop */
#define KDB_DISPLAY_MAX         8
#define KDB_DEFAULT_LOG_BYTES   4096
#define KDB_MAXIMUM_LOG_BYTES   (32 * 1024)

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

/* Whether a bug check was asked for at the prompt */

static PVOID KdbpCliCallbacks[KDB_CALLBACK_MAX];

/* Where `x` and `u` carry on from when they are given no address */
static ULONG_PTR KdbpLastDumpAddress = 0;
static ULONG_PTR KdbpLastDisassembleAddress = 0;

static CHAR KdbpCommandBuffers[KDB_MAXIMUM_COMMAND_DEPTH][KDB_COMMAND_MAX];
static CHAR KdbpAliasBuffers[KDB_MAXIMUM_COMMAND_DEPTH][KDB_COMMAND_MAX];
static ULONG KdbpCommandDepth;
static CHAR KdbpPrintBuffer[1024];

static PKDB_KTRAP_FRAME KdbpViewContext;
static CONTEXT KdbpRecordContext;
static CONTEXT KdbpFrameContext;
static BOOLEAN KdbpFrameSelected;

/* What a p with a count still has to step, and what runs at every stop */
static ULONG KdbpPendingStepOvers;
static CHAR KdbpDisplays[KDB_DISPLAY_MAX][KDB_BREAKPOINT_COMMANDS_MAX];

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The context the commands look at: whatever thread the debugger has been
 * pointed at, which is the one that stopped unless it was told otherwise.
 */
static
PKDB_KTRAP_FRAME
KdbpContext(VOID)
{
    if (KdbpFrameSelected)
        return &KdbpFrameContext;

    return (KdbpViewContext != NULL) ? KdbpViewContext : KdbCurrentTrapFrame;
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

/* A network debugger already has the whole log, and its keys cannot satisfy a prompt that waits locally */
static
BOOLEAN
KdbpPagerCanWaitForKey(VOID)
{
    if (KdpDebugMode.Net)
        return FALSE;

    return (KdTermSerial || KdpDebugMode.Screen);
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
        if (!KdbActive || !KdbpPagerCanWaitForKey() ||
            (KdbpLinesPrinted + 1 < Rows))
            continue;

        KdpScreenHoldOutput(FALSE);
        KdIoPuts("--- Press any key to go on, q to stop ---");

        {
            ULONG ScanCode;
            CHAR Key = KdpReadTermKey(&ScanCode);

            KdIoPuts("\r                                        \r");
            KdpScreenHoldOutput(TRUE);

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

    KdbpTranscriptAppend(String, Length);
    KdbpPager((PCHAR)String, Length);
}

/* A block of text that is already in the log, such as the log itself: paged, not logged again */
VOID
KdbpPrintBlock(
    _In_reads_(Length) PCHAR Buffer,
    _In_ ULONG Length)
{
    if (KdbpSelfTestCapture(Buffer, Length))
        return;

    KdbpPager(Buffer, Length);
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
    if ((String == NULL) || (String->Buffer == NULL) || (String->Length == 0))
    {
        KdbPuts("(null)");
        return;
    }

    KdbpPrintCountedString(String);
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

        if (Value > (MAXULONG_PTR >> 4))
            return FALSE;

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

/* A count may also be written the WinDbg way, as L followed by the number */
BOOLEAN
NTAPI
KdbpGetCountExpression(
    _In_ PCHAR Expression,
    _Out_ PULONG_PTR Count)
{
    if (((Expression[0] == 'L') || (Expression[0] == 'l')) && (Expression[1] != ANSI_NULL))
        return KdbpGetAddressExpression(Expression + 1, Count);
    return KdbpGetAddressExpression(Expression, Count);
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
static BOOLEAN KdbpRunTo(_In_ ULONG_PTR Address, _In_opt_ PCHAR Condition);

static
BOOLEAN
KdbpCmdContinue(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address;

    /* `g <address>` runs to that address and stops there, or wherever the machine stops first */
    if (Argc > 1)
    {
        if (Argc > 2)
        {
            KdbpPrint("%s is more than %s takes\n", Argv[2], Argv[0]);
            return TRUE;
        }

        if (!KdbpGetAddressExpression(Argv[1], &Address))
            return TRUE;

        return KdbpRunTo(Address, NULL);
    }

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

/*
 * Steps the thread that stopped the machine, whatever frame, record or thread
 * the commands since have been looking at: that is the only one that runs.
 */
static
BOOLEAN
KdbpStartStepping(
    _In_ ULONG Count)
{
    if (KdbCurrentTrapFrame == NULL)
    {
        KdbPuts("There is no stopped thread to step\n");
        return TRUE;
    }

    if (!NT_SUCCESS(KdbpArchSetSingleStep(KdbCurrentTrapFrame, TRUE)))
    {
        KdbPuts("The next instruction cannot be stepped to\n");
        return TRUE;
    }

    KdbNumSingleSteps = Count;
    KdbSingleStepOver = FALSE;

    KdbpSetContinueType(kdContinue);
    KdbpLeaveLoop = TRUE;
    return TRUE;
}

/*
 * Lets the machine run until it reaches an address, through a breakpoint that
 * goes at the next stop, wherever that is. A breakpoint already there does
 * the same job.
 */
static
BOOLEAN
KdbpRunTo(
    _In_ ULONG_PTR Address,
    _In_opt_ PCHAR Condition)
{
    LONG Number = -1;
    NTSTATUS Status;

    Status = KdbpInsertBreakPoint(Address,
                                  KdbBreakPointTemporary,
                                  0,
                                  KdbAccessExec,
                                  Condition,
                                  KdbpIsSystemAddress(Address),
                                  &Number);
    if (NT_SUCCESS(Status))
    {
        KdbpSetBreakPointInternal(Number);
    }
    else if (Status != STATUS_OBJECT_NAME_COLLISION)
    {
        KdbpPrint("A breakpoint cannot be set at %p (0x%08lx)\n", (PVOID)Address, Status);
        return TRUE;
    }

    KdbpSetContinueType(kdContinue);
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

    if (Count > MAXULONG)
    {
        KdbpPrint("A step count is at most 0x%lx\n", MAXULONG);
        return TRUE;
    }

    return KdbpStartStepping((Count == 0) ? 1 : (ULONG)Count);
}

/*
 * Runs to just after the current instruction, which steps over a call rather
 * than into it.
 */
/* Runs over the instruction the stopped thread is at: to the next one after a call, one step otherwise */
static
BOOLEAN
KdbpStepOver(VOID)
{
    ULONG_PTR Pc;
    LONG Length;

    if (KdbCurrentTrapFrame == NULL)
    {
        KdbPuts("There is no stopped thread to step\n");
        return TRUE;
    }

    /* Only a call comes back to the next instruction: anything else is stepped */
    Pc = (ULONG_PTR)KeGetContextPc(KdbCurrentTrapFrame);
    if (!KdbpArchIsStepOverInstruction(Pc))
        return KdbpStartStepping(1);

    Length = KdbpGetInstLength(Pc);
    if (Length <= 0)
        return KdbpStartStepping(1);

    return KdbpRunTo(Pc + (ULONG_PTR)Length, NULL);
}

static
BOOLEAN
KdbpCmdNext(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Count = 1;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Count))
        return TRUE;

    if (Count > MAXULONG)
    {
        KdbpPrint("A step count is at most 0x%lx\n", MAXULONG);
        return TRUE;
    }

    /* The rest of the count is stepped at each stop that is the end of a step */
    KdbpPendingStepOvers = (Count == 0) ? 0 : (ULONG)Count - 1;
    KdbpStepOver();

    if (!KdbpLeaveLoop)
        KdbpPendingStepOvers = 0;

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
        PCSTR Commands = NULL;
        ULONG Hits = 0;
        ULONG Ignore = 0;

        if (KdbpGetBreakPointInfo((ULONG)Index,
                                  &Address,
                                  &Type,
                                  &Size,
                                  &AccessType,
                                  &DebugReg,
                                  &Enabled,
                                  &Global,
                                  &Process,
                                  &Condition) &&
            KdbpGetBreakPointCounts(Index, &Hits, &Ignore, &Commands))
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

            KdbpPrint(" hits %lu", Hits);

            if (Ignore != 0)
                KdbpPrint(" ignore %lu", Ignore);

            if (Condition != NULL)
                KdbpPrint(" if %s", Condition);

            if (Commands != NULL)
                KdbpPrint(" do \"%s\"", Commands);

            KdbPuts("\n");
        }

        Index = KdbpGetNextBreakPointNr((ULONG)Index + 1);
    }

    if (!Any)
        KdbPuts("No breakpoints are set\n");

    return TRUE;
}

static
VOID
KdbpReportBreakPointStatus(
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Address)
{
    switch (Status)
    {
        case STATUS_OBJECT_NAME_COLLISION:
            KdbpPrint("There is already a breakpoint at %p\n", (PVOID)Address);
            break;
        case STATUS_INSUFFICIENT_RESOURCES:
            KdbPuts("Every breakpoint, or every register that watches memory, is in use\n");
            break;
        case STATUS_DATATYPE_MISALIGNMENT:
            KdbpPrint("%p is not aligned to the width watched\n", (PVOID)Address);
            break;
        case STATUS_NOT_SUPPORTED:
            KdbPuts("This processor has no registers that watch memory\n");
            break;
        default:
            KdbpPrint("The breakpoint was not set at %p (0x%08lx)\n", (PVOID)Address, Status);
            break;
    }
}

/* What comes after if, checked now so that a mistake is shown where it is */
static
BOOLEAN
KdbpGetCondition(
    _In_ ULONG First,
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_writes_(Length) PCHAR Condition,
    _In_ ULONG Length)
{
    CHAR ErrorMessage[128];
    LONG ErrorOffset = 0;

    if (First >= Argc)
    {
        KdbPuts("A condition is needed after if\n");
        return FALSE;
    }

    if (!KdbpJoinWords(First, Argc, Argv, Condition, Length))
    {
        KdbpPrint("A condition has at most %lu characters\n", Length - 1);
        return FALSE;
    }

    if (KdbpRpnParseExpression(Condition, &ErrorOffset, ErrorMessage) == NULL)
    {
        KdbpPrint("%s at offset %ld of the condition\n", ErrorMessage, ErrorOffset);
        return FALSE;
    }

    return TRUE;
}

/*
 * What follows the address of bp, or the size of bpm: `if <condition>` and
 * `do <commands>`, in that order, either one or both. Commands that hold a
 * semicolon are written in quotes, or the line would end there.
 */
static
BOOLEAN
KdbpGetBreakPointOptions(
    _In_ ULONG First,
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_writes_(KDB_CONDITION_MAX) PCHAR Condition,
    _Out_writes_(KDB_BREAKPOINT_COMMANDS_MAX) PCHAR Commands)
{
    ULONG Do;

    Condition[0] = ANSI_NULL;
    Commands[0] = ANSI_NULL;

    for (Do = First; (Do < Argc) && (_stricmp(Argv[Do], "do") != 0); Do++)
        NOTHING;

    if (First < Do)
    {
        if (_stricmp(Argv[First], "if") != 0)
        {
            KdbpPrint("%s is not if or do\n", Argv[First]);
            return FALSE;
        }

        if (!KdbpGetCondition(First + 1, Do, Argv, Condition, KDB_CONDITION_MAX))
            return FALSE;
    }

    if (Do < Argc)
    {
        if (Do + 1 >= Argc)
        {
            KdbPuts("Commands are needed after do\n");
            return FALSE;
        }

        if (!KdbpJoinWords(Do + 1, Argc, Argv, Commands, KDB_BREAKPOINT_COMMANDS_MAX))
        {
            KdbpPrint("The commands of a breakpoint have at most %u characters\n", KDB_BREAKPOINT_COMMANDS_MAX - 1);
            return FALSE;
        }
    }

    return TRUE;
}

static
BOOLEAN
KdbpCmdBreakPointSet(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Condition[KDB_CONDITION_MAX];
    static CHAR Commands[KDB_BREAKPOINT_COMMANDS_MAX];
    KDB_BREAKPOINT_TYPE Type = KdbBreakPointSoftware;
    ULONG_PTR Address = 0;
    ULONG First = 1;
    LONG Number = -1;
    NTSTATUS Status;

    /* `bp /1` goes once it has stopped the machine */
    if ((Argc > 1) && (strcmp(Argv[1], "/1") == 0))
    {
        Type = KdbBreakPointTemporary;
        First = 2;
    }

    if (Argc <= First)
    {
        KdbPuts("An address is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[First], &Address) ||
        !KdbpGetBreakPointOptions(First + 1, Argc, Argv, Condition, Commands))
    {
        return TRUE;
    }

    Status = KdbpInsertBreakPoint(Address,
                                  Type,
                                  0,
                                  KdbAccessExec,
                                  (Condition[0] != ANSI_NULL) ? Condition : NULL,
                                  KdbpIsSystemAddress(Address),
                                  &Number);
    if (!NT_SUCCESS(Status))
    {
        KdbpReportBreakPointStatus(Status, Address);
        return TRUE;
    }

    KdbpSetBreakPointCommands(Number, Commands);
    KdbpPrint("Breakpoint %ld at %p\n", Number, (PVOID)Address);
    return TRUE;
}

static
BOOLEAN
KdbpCmdBreakPointMemory(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Condition[KDB_CONDITION_MAX];
    static CHAR Commands[KDB_BREAKPOINT_COMMANDS_MAX];
    ULONG_PTR Address = 0;
    ULONG_PTR Size = sizeof(ULONG_PTR);
    KDB_ACCESS_TYPE AccessType = KdbAccessReadWrite;
    ULONG Options = 3;
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

    if ((Argc > 3) && (_stricmp(Argv[3], "if") != 0) && (_stricmp(Argv[3], "do") != 0))
    {
        if (!KdbpGetAddressExpression(Argv[3], &Size))
            return TRUE;

        Options = 4;
    }

    if (!KdbpGetBreakPointOptions(Options, Argc, Argv, Condition, Commands))
        return TRUE;

    if ((Size != 1) && (Size != 2) && (Size != 4) && (Size != 8))
    {
        KdbpPrint("A watched range is 1, 2, 4 or 8 bytes, not 0x%Ix\n", Size);
        return TRUE;
    }

    if ((AccessType != KdbAccessExec) && ((Address & (Size - 1)) != 0))
    {
        KdbpPrint("%p is not aligned to the %Iu bytes watched\n", (PVOID)Address, Size);
        return TRUE;
    }

    Status = KdbpInsertBreakPoint(Address,
                                  KdbBreakPointHardware,
                                  (UCHAR)Size,
                                  AccessType,
                                  (Condition[0] != ANSI_NULL) ? Condition : NULL,
                                  TRUE,
                                  &Number);
    if (!NT_SUCCESS(Status))
    {
        KdbpReportBreakPointStatus(Status, Address);
        return TRUE;
    }

    KdbpSetBreakPointCommands(Number, Commands);
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
        ULONG Earlier;

        /* A second name for a register already shown says nothing new */
        for (Earlier = 0; Earlier < Index; Earlier++)
        {
            if ((KdbpRegisters[Earlier].Offset == KdbpRegisters[Index].Offset) &&
                (KdbpRegisters[Earlier].Size == KdbpRegisters[Index].Size))
            {
                break;
            }
        }
        if (Earlier != Index)
            continue;

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
    static CHAR Expression[KDB_COMMAND_MAX];
    PKDB_KTRAP_FRAME Context = KdbpContext();
    ULONGLONG Value = 0;
    ULONG Size = 0;

    if (Context == NULL)
    {
        KdbPuts("There are no registers to show\n");
        return TRUE;
    }

    if (Argc < 2)
        return KdbpCmdRegisters(Argc, Argv);

    if (!NT_SUCCESS(KdbpGetRegisterValue(Context, Argv[1], &Value, &Size)))
    {
        KdbpPrint("There is no register called %s\n", Argv[1]);
        return TRUE;
    }

    /* `r <name> <expression>` changes it; `r <name>` only reads it */
    if (Argc >= 3)
    {
        ULONG_PTR New = 0;

        if (!KdbpJoinWords(2, Argc, Argv, Expression, sizeof(Expression)) ||
            !KdbpGetAddressExpression(Expression, &New))
        {
            return TRUE;
        }

        KdbpSetRegisterValue(Context, Argv[1], (ULONGLONG)New);
        KdbpGetRegisterValue(Context, Argv[1], &Value, &Size);
    }

    KdbpPrint("%s = %016I64x\n", Argv[1], Value);
    return TRUE;
}

static
const KDB_REGISTER *
KdbpFindFloatingRegister(
    _In_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < KdbpFloatingRegisterCount; Index++)
    {
        if (_stricmp(KdbpFloatingRegisters[Index].Name, Name) == 0)
            return &KdbpFloatingRegisters[Index];
    }

    return NULL;
}

static
VOID
KdbpPrintFloatingRegister(
    _In_ PKDB_KTRAP_FRAME Context,
    _In_ const KDB_REGISTER *Register)
{
    static const CHAR Digits[] = "0123456789abcdef";
    const UCHAR *Bytes = (const UCHAR *)Context + Register->Offset;
    CHAR Text[2 * KDB_MAXIMUM_FLOATING_REGISTER + 1];
    ULONG Index;

    for (Index = 0; (Index < Register->Size) && (Index < KDB_MAXIMUM_FLOATING_REGISTER); Index++)
    {
        UCHAR Byte = Bytes[Register->Size - 1 - Index];

        Text[2 * Index] = Digits[Byte >> 4];
        Text[2 * Index + 1] = Digits[Byte & 0xF];
    }

    Text[2 * Index] = ANSI_NULL;
    KdbpPrint("%-6s 0x%s\n", Register->Name, Text);
}

static
BOOLEAN
KdbpParseHexBytes(
    _In_ PCSTR Text,
    _Out_writes_bytes_(Size) PUCHAR Value,
    _In_ ULONG Size)
{
    SIZE_T Length;
    ULONG Digits = 0;
    ULONG Nibble = 0;

    if ((Text[0] == '0') && ((Text[1] == 'x') || (Text[1] == 'X')))
        Text += 2;

    for (Length = 0; Text[Length] != ANSI_NULL; Length++)
    {
        if ((Text[Length] == '`') || (Text[Length] == '_'))
            continue;

        if (!isxdigit((UCHAR)Text[Length]) || (++Digits > 2 * Size))
            return FALSE;
    }

    if (Digits == 0)
        return FALSE;

    RtlZeroMemory(Value, Size);

    while (Length != 0)
    {
        CHAR Char = Text[--Length];
        UCHAR Digit;

        if ((Char == '`') || (Char == '_'))
            continue;

        if ((Char >= '0') && (Char <= '9'))
            Digit = (UCHAR)(Char - '0');
        else
            Digit = (UCHAR)((Char | 0x20) - 'a' + 10);

        Value[Nibble / 2] |= (UCHAR)(Digit << ((Nibble & 1) * 4));
        Nibble++;
    }

    return TRUE;
}

static
PKDB_KTRAP_FRAME
KdbpFloatingContext(VOID)
{
    PKDB_KTRAP_FRAME Context = KdbpContext();

    if ((Context != NULL) && ((Context->ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT))
        return Context;

    KdbPuts("This context holds no floating-point state\n");
    return NULL;
}

static
BOOLEAN
KdbpCmdFloatingRegisters(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_KTRAP_FRAME Context = KdbpFloatingContext();
    const KDB_REGISTER *Register;
    ULONG Index;

    if (Context == NULL)
        return TRUE;

    if (Argc > 1)
    {
        Register = KdbpFindFloatingRegister(Argv[1]);
        if (Register == NULL)
            KdbpPrint("There is no floating-point or vector register called %s\n", Argv[1]);
        else
            KdbpPrintFloatingRegister(Context, Register);

        return TRUE;
    }

    for (Index = 0; (Index < KdbpFloatingRegisterCount) && !KdbpIsOutputAborted(); Index++)
        KdbpPrintFloatingRegister(Context, &KdbpFloatingRegisters[Index]);

    return TRUE;
}

static
BOOLEAN
KdbpCmdSetFloatingRegister(
    ULONG Argc,
    PCHAR Argv[])
{
    UCHAR Value[KDB_MAXIMUM_FLOATING_REGISTER];
    const KDB_REGISTER *Register;
    PKDB_KTRAP_FRAME Context;

    if (Argc < 3)
    {
        KdbPuts("A register and a hexadecimal value are needed\n");
        return TRUE;
    }

    Context = KdbpFloatingContext();
    if (Context == NULL)
        return TRUE;

    Register = KdbpFindFloatingRegister(Argv[1]);
    if (Register == NULL)
    {
        KdbpPrint("There is no floating-point or vector register called %s\n", Argv[1]);
        return TRUE;
    }

    if ((Register->Size > sizeof(Value)) || !KdbpParseHexBytes(Argv[2], Value, Register->Size))
    {
        KdbpPrint("%s is not a hexadecimal value of at most %u bytes\n", Argv[2], Register->Size);
        return TRUE;
    }

    RtlCopyMemory((PUCHAR)Context + Register->Offset, Value, Register->Size);
    KdbpPrintFloatingRegister(Context, Register);
    return TRUE;
}

/*
 * Walks back through the frames that led here. The chain is read out of the
 * stack, so a stack that has been overwritten stops the walk rather than
 * sending it anywhere.
 */
static
VOID
KdbpPrintFrame(
    _In_ PKDB_KTRAP_FRAME Walk,
    _In_ PKDB_KTRAP_FRAME Context,
    _In_ BOOLEAN Verbose,
    _In_ BOOLEAN ReturnAddress)
{
    ULONG_PTR Pc = (ULONG_PTR)KeGetContextPc(Walk);

    if (Verbose)
    {
        KdbpPrint("%p sp %p fp %p ",
                  (PVOID)Pc,
                  (PVOID)KeGetContextStackRegister(Walk),
                  (PVOID)KeGetContextFrameRegister(Walk));
    }
    else
    {
        KdbpPrint("%p ", (PVOID)Pc);
    }

    if (ReturnAddress)
        KdbSymPrintReturnAddress((PVOID)Pc);
    else
        KdbSymPrintAddress((PVOID)Pc, Context);
    KdbPuts("\n");
}

static
VOID
KdbpBackTrace(
    _In_ PKDB_KTRAP_FRAME Context,
    _In_ ULONG_PTR Limit,
    _In_ BOOLEAN Verbose)
{
    static KDB_KTRAP_FRAME Walk;
    ULONG Depth = 0;

    Walk = *Context;
    KdbpPrintFrame(&Walk, Context, Verbose, FALSE);

    while ((Depth < Limit) && !KdbpIsOutputAborted() && KiUnwindFrame(&Walk, (Depth != 0)))
    {
        KdbpPrintFrame(&Walk, Context, Verbose, (Walk.ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0);
        Depth++;
    }
}

static
PKDB_KTRAP_FRAME
KdbpThreadContext(
    _In_ PETHREAD Thread,
    _Out_ PKDB_KTRAP_FRAME Buffer)
{
    PVOID KernelStack = NULL;
    ULONG Processor;

    if (Thread == KdbOriginalThread)
        return KdbCurrentTrapFrame;

    for (Processor = 0; Processor < KeProcessorCount; Processor++)
    {
        PKPRCB Prcb = KiProcessorBlock[Processor];

        if ((Prcb != NULL) && (Prcb->CurrentThread == &Thread->Tcb))
            return &Prcb->ProcessorState.ContextFrame;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&KernelStack, &Thread->Tcb.KernelStack, sizeof(KernelStack))))
        return NULL;

    return KdbpKdbTrapFrameFromKernelStack(KernelStack, Buffer) ? Buffer : NULL;
}

typedef struct _KDB_BACKTRACE_WALK
{
    ULONG_PTR Limit;
    BOOLEAN Verbose;
    ULONG Count;
    CHAR ImageName[16];
} KDB_BACKTRACE_WALK, *PKDB_BACKTRACE_WALK;

static
BOOLEAN
NTAPI
KdbpBackTraceThread(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    static KDB_KTRAP_FRAME ThreadContext;
    PKDB_BACKTRACE_WALK Walk = Context;
    PETHREAD Thread = CONTAINING_RECORD(Link, ETHREAD, ThreadListEntry);
    PKDB_KTRAP_FRAME Frame;
    CLIENT_ID Cid;

    RtlZeroMemory(&Cid, sizeof(Cid));
    KdbpSafeReadMemory(&Cid, &Thread->Cid, sizeof(Cid));

    KdbpPrint("Thread %p (%Ix.%Ix) of %s\n",
              Thread,
              (ULONG_PTR)Cid.UniqueProcess,
              (ULONG_PTR)Cid.UniqueThread,
              Walk->ImageName);

    Frame = KdbpThreadContext(Thread, &ThreadContext);
    if (Frame == NULL)
        KdbPuts("  its registers cannot be found\n");
    else
        KdbpBackTrace(Frame, Walk->Limit, Walk->Verbose);

    return (++Walk->Count < KDB_MAXIMUM_THREADS) && !KdbpIsOutputAborted();
}

static
BOOLEAN
NTAPI
KdbpBackTraceProcess(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    PKDB_BACKTRACE_WALK Walk = Context;
    PEPROCESS Process = CONTAINING_RECORD(Link, EPROCESS, ActiveProcessLinks);
    KDB_WALK_RESULT Result;
    PLIST_ENTRY Broken;

    RtlZeroMemory(Walk->ImageName, sizeof(Walk->ImageName));
    KdbpSafeReadMemory(Walk->ImageName, Process->ImageFileName, sizeof(Process->ImageFileName));

    Result = KdbpWalkList(&Process->ThreadListHead, KDB_MAXIMUM_LIST_WALK, KdbpBackTraceThread, Walk, NULL, &Broken);
    KdbpReportWalk(Result, Broken, KDB_MAXIMUM_LIST_WALK);

    return (Walk->Count < KDB_MAXIMUM_THREADS) && !KdbpIsOutputAborted();
}

static
VOID
KdbpBackTraceAll(
    _In_ ULONG_PTR Limit,
    _In_ BOOLEAN Verbose)
{
    KDB_BACKTRACE_WALK Walk;
    KDB_WALK_RESULT Result;
    PLIST_ENTRY Broken;

    RtlZeroMemory(&Walk, sizeof(Walk));
    Walk.Limit = Limit;
    Walk.Verbose = Verbose;

    Result = KdbpWalkList(&PsActiveProcessHead, KDB_MAXIMUM_LIST_WALK, KdbpBackTraceProcess, &Walk, NULL, &Broken);
    KdbpReportWalk(Result, Broken, KDB_MAXIMUM_LIST_WALK);
}

static
BOOLEAN
KdbpCmdBackTrace(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_KTRAP_FRAME Context = KdbpContext();
    ULONG_PTR Limit = 32;
    BOOLEAN Verbose = FALSE;
    BOOLEAN All = FALSE;
    ULONG Index;

    for (Index = 1; Index < Argc; Index++)
    {
        if (_stricmp(Argv[Index], "all") == 0)
            All = TRUE;
        else if ((_stricmp(Argv[Index], "verbose") == 0) || (_stricmp(Argv[Index], "v") == 0))
            Verbose = TRUE;
        else if (!KdbpGetAddressExpression(Argv[Index], &Limit))
            return TRUE;
    }

    if (All)
    {
        KdbpBackTraceAll(Limit, Verbose);
        return TRUE;
    }

    if (Context == NULL)
        return TRUE;

    KdbpBackTrace(Context, Limit, Verbose);
    return TRUE;
}

/*
 * Shows memory. The letter after the command says how wide each value is, and
 * `a` asks for the bytes to be shown as text.
 */
/* What a dump shows besides the values, and where it reads them */
#define KDB_DUMP_TEXT       0x1
#define KDB_DUMP_PHYSICAL   0x2

static
NTSTATUS
KdbpReadDumpValue(
    _In_ ULONG_PTR Address,
    _Out_writes_bytes_(Width) PVOID Value,
    _In_ ULONG Width,
    _In_ ULONG Flags)
{
    if (Flags & KDB_DUMP_PHYSICAL)
    {
        /* TODO: device memory is read cached, as the host debugger's physical reads are */
        return KdpCopyMemoryChunks((ULONG64)Address,
                                   Value,
                                   Width,
                                   0,
                                   MMDBG_COPY_UNSAFE | MMDBG_COPY_PHYSICAL | MMDBG_COPY_CACHED,
                                   NULL);
    }

    return KdbpSafeReadMemory(Value, (PVOID)Address, Width);
}

/* The bytes of a line again, as text, after the values */
static
VOID
KdbpPrintDumpText(
    _In_reads_(Length) const UCHAR *Bytes,
    _In_ ULONG Length,
    _In_ ULONG Values,
    _In_ ULONG Width)
{
    CHAR Text[KDB_DUMP_BYTES_PER_LINE + 1];
    ULONG Index;

    /* A short last line is padded so its text lines up with the others */
    for (Index = Values; Index < KDB_DUMP_BYTES_PER_LINE / Width; Index++)
        KdbpPrint("%*s", (int)(Width * 2 + 1), "");

    for (Index = 0; Index < Length; Index++)
        Text[Index] = ((Bytes[Index] >= ' ') && (Bytes[Index] < 0x7F)) ? (CHAR)Bytes[Index] : '.';

    Text[Length] = ANSI_NULL;
    KdbpPrint(" %s", Text);
}

static
BOOLEAN
KdbpDumpMemory(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ ULONG Width,
    _In_ ULONG Flags)
{
    UCHAR Line[KDB_DUMP_BYTES_PER_LINE];
    ULONG_PTR Address = KdbpLastDumpAddress;
    ULONG_PTR Count = (Flags & KDB_DUMP_TEXT) ? (KDB_DUMP_BYTES_PER_LINE * 8 / Width) : 8;
    ULONG_PTR Index;
    ULONG InLine = 0;

    if ((Flags & KDB_DUMP_PHYSICAL) && (Argc < 2))
    {
        KdbPuts("A physical address is needed\n");
        return TRUE;
    }

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if ((Argc > 2) && !KdbpGetCountExpression(Argv[2], &Count))
        return TRUE;

    if (Count == 0)
        Count = 1;

    if (Count > KDB_MAXIMUM_DUMP_BYTES / Width)
    {
        KdbpPrint("A dump shows at most 0x%lx bytes\n", KDB_MAXIMUM_DUMP_BYTES);
        return TRUE;
    }

    for (Index = 0; Index < Count; Index++)
    {
        ULONG_PTR At = Address + (Index * Width);
        ULONGLONG Value = 0;

        if ((Index % (KDB_DUMP_BYTES_PER_LINE / Width)) == 0)
        {
            if (Index != 0)
            {
                if (Flags & KDB_DUMP_TEXT)
                    KdbpPrintDumpText(Line, InLine * Width, InLine, Width);
                KdbPuts("\n");
            }

            KdbpPrint("%p  ", (PVOID)At);
            InLine = 0;
        }

        if (!NT_SUCCESS(KdbpReadDumpValue(At, &Value, Width, Flags)))
        {
            KdbPuts(" ????");
            break;
        }

        RtlCopyMemory(&Line[InLine * Width], &Value, Width);
        InLine++;

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

    if ((Flags & KDB_DUMP_TEXT) && (InLine != 0))
        KdbpPrintDumpText(Line, InLine * Width, InLine, Width);

    KdbPuts("\n");

    if (!(Flags & KDB_DUMP_PHYSICAL))
        KdbpLastDumpAddress = Address + (Count * Width);

    return TRUE;
}

static BOOLEAN KdbpCmdDumpBytes(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 1, KDB_DUMP_TEXT);
}

static BOOLEAN KdbpCmdDumpWords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 2, 0);
}

static BOOLEAN KdbpCmdDumpDwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 4, 0);
}

static BOOLEAN KdbpCmdDumpDwordsText(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 4, KDB_DUMP_TEXT);
}

static BOOLEAN KdbpCmdDumpQwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 8, 0);
}

/*
 * A string of bytes, the way it would be read: up to its terminating zero or
 * the count, whichever comes first, in quotes, with what cannot be printed
 * shown as dots.
 */
static
BOOLEAN
KdbpCmdDumpText(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address = KdbpLastDumpAddress;
    ULONG_PTR Count = KDB_DEFAULT_TEXT_LENGTH;
    ULONG_PTR Index;
    ULONG Column = 0;
    BOOLEAN Ended = FALSE;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if ((Argc > 2) && !KdbpGetCountExpression(Argv[2], &Count))
        return TRUE;

    if ((Count == 0) || (Count > KDB_MAXIMUM_DUMP_BYTES))
    {
        KdbpPrint("A string is from 1 to 0x%lx bytes\n", KDB_MAXIMUM_DUMP_BYTES);
        return TRUE;
    }

    KdbpPrint("%p  \"", (PVOID)Address);

    for (Index = 0; (Index < Count) && !KdbpIsOutputAborted(); Index++)
    {
        UCHAR Byte = 0;
        CHAR Text[2];

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Byte, (PVOID)(Address + Index), sizeof(Byte))))
        {
            KdbPuts("\" ??\n");
            KdbpLastDumpAddress = Address + Index;
            return TRUE;
        }

        if (Byte == 0)
        {
            Ended = TRUE;
            break;
        }

        /* A long string goes on over lines of their own */
        if ((Column == KDB_TEXT_PER_LINE) && (Index + 1 < Count))
        {
            KdbpPrint("\"\n%p  \"", (PVOID)(Address + Index));
            Column = 0;
        }

        Text[0] = ((Byte >= ' ') && (Byte < 0x7F)) ? (CHAR)Byte : '.';
        Text[1] = ANSI_NULL;
        KdbPuts(Text);
        Column++;
    }

    KdbPuts("\"\n");

    KdbpLastDumpAddress = Address + Index + (Ended ? 1 : 0);
    return TRUE;
}

static BOOLEAN KdbpCmdDumpPointers(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, sizeof(ULONG_PTR), 0);
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

    if ((Argc > 2) && !KdbpGetCountExpression(Argv[2], &Count))
        return TRUE;

    if (Count > KDB_MAXIMUM_INSTRUCTIONS)
    {
        KdbpPrint("At most 0x%lx instructions are shown at once\n", KDB_MAXIMUM_INSTRUCTIONS);
        return TRUE;
    }

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
    CHAR Module[64];
    PCSTR Separator;

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
    else
    {
        /* module!pattern, the way WinDbg writes it */
        Separator = strchr(Argv[1], '!');
        if (Separator != NULL)
        {
            if ((ULONG_PTR)(Separator - Argv[1]) >= sizeof(Module))
            {
                KdbPuts("The module name is too long\n");
                return TRUE;
            }
            RtlCopyMemory(Module, Argv[1], Separator - Argv[1]);
            Module[Separator - Argv[1]] = ANSI_NULL;
            ModulePattern = (Module[0] != ANSI_NULL) ? Module : "*";
            SymbolPattern = Separator + 1;
        }
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
VOID
KdbpUseContext(
    _In_opt_ PKDB_KTRAP_FRAME Context,
    _In_ PCSTR What);

static
VOID
KdbpUseThreadContext(
    _In_ PETHREAD Thread)
{
    static KDB_KTRAP_FRAME ThreadContext;
    PKDB_KTRAP_FRAME Context = KdbpThreadContext(Thread, &ThreadContext);

    if (Context == NULL)
        KdbpPrint("The registers of thread %p cannot be found\n", Thread);

    KdbpUseContext((Context == KdbCurrentTrapFrame) ? NULL : Context, "Registers of the thread");
}

static
BOOLEAN
NTAPI
KdbpPrintProcessLink(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    PEPROCESS Process = CONTAINING_RECORD(Link, EPROCESS, ActiveProcessLinks);
    CHAR Name[sizeof(Process->ImageFileName) + 1];
    HANDLE Id = NULL;

    UNREFERENCED_PARAMETER(Context);

    RtlZeroMemory(Name, sizeof(Name));
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Id, &Process->UniqueProcessId, sizeof(Id))) ||
        !NT_SUCCESS(KdbpSafeReadMemory(Name, Process->ImageFileName, sizeof(Process->ImageFileName))))
    {
        KdbpPrint(" %p  cannot be read\n", Process);
        return !KdbpIsOutputAborted();
    }

    KdbpPrint("%s%p  %6Ix  %s\n",
              (Process == KdbCurrentProcess) ? "*" : " ",
              Process,
              (ULONG_PTR)Id,
              Name);

    return !KdbpIsOutputAborted();
}

static
BOOLEAN
KdbpCmdProcesses(
    ULONG Argc,
    PCHAR Argv[])
{
    KDB_WALK_RESULT Result;
    PLIST_ENTRY Broken;

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
        else
            KdbpUseThreadContext(KdbCurrentThread);

        return TRUE;
    }

    Result = KdbpWalkList(&PsActiveProcessHead, KDB_MAXIMUM_LIST_WALK, KdbpPrintProcessLink, NULL, NULL, &Broken);
    KdbpReportWalk(Result, Broken, KDB_MAXIMUM_LIST_WALK);
    return TRUE;
}

static
BOOLEAN
NTAPI
KdbpPrintThreadLink(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    PETHREAD Thread = CONTAINING_RECORD(Link, ETHREAD, ThreadListEntry);
    HANDLE Id = NULL;
    UCHAR State = 0;

    UNREFERENCED_PARAMETER(Context);

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Id, &Thread->Cid.UniqueThread, sizeof(Id))) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&State, (PVOID)&Thread->Tcb.State, sizeof(State))))
    {
        KdbpPrint(" %p  cannot be read\n", Thread);
        return !KdbpIsOutputAborted();
    }

    KdbpPrint("%s%p  %6Ix  state %u\n",
              (Thread == KdbCurrentThread) ? "*" : " ",
              Thread,
              (ULONG_PTR)Id,
              State);

    return !KdbpIsOutputAborted();
}

static
BOOLEAN
KdbpCmdThreads(
    ULONG Argc,
    PCHAR Argv[])
{
    KDB_WALK_RESULT Result;
    PLIST_ENTRY Broken;
    PEPROCESS Process = KdbCurrentProcess;

    if (Argc > 1)
    {
        ULONG_PTR Id = 0;

        if (!KdbpGetHexNumber(Argv[1], &Id) && !KdbpGetAddressExpression(Argv[1], &Id))
            return TRUE;

        if (!KdbpAttachToThread((PVOID)Id))
            KdbpPrint("There is no thread %s, or it cannot be looked at\n",
                      Argv[1]);
        else
            KdbpUseThreadContext(KdbCurrentThread);

        return TRUE;
    }

    if (Process == NULL)
    {
        KdbPuts("No process is being looked at\n");
        return TRUE;
    }

    Result = KdbpWalkList(&Process->ThreadListHead, KDB_MAXIMUM_LIST_WALK, KdbpPrintThreadLink, NULL, NULL, &Broken);
    KdbpReportWalk(Result, Broken, KDB_MAXIMUM_LIST_WALK);
    return TRUE;
}

static
PETHREAD
KdbpThreadArgument(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address;

    if (Argc < 2)
        return KdbCurrentThread;

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return NULL;

    return (PETHREAD)Address;
}

static
BOOLEAN
KdbpCmdThreadDetail(
    ULONG Argc,
    PCHAR Argv[])
{
    static ETHREAD Snapshot;
    PETHREAD Thread = KdbpThreadArgument(Argc, Argv);

    if (Thread == NULL)
        return TRUE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Snapshot, Thread, sizeof(Snapshot))))
    {
        KdbpPrint("Thread %p cannot be read\n", Thread);
        return TRUE;
    }

    KdbpPrint("Thread %p  Cid %p.%p  Process %p\n",
              Thread,
              Snapshot.Cid.UniqueProcess,
              Snapshot.Cid.UniqueThread,
              Snapshot.Tcb.ApcState.Process);
    KdbpPrint("  State %u, priority %d (base %d), wait reason %u, wait mode %d, wait IRQL %u\n",
              Snapshot.Tcb.State,
              Snapshot.Tcb.Priority,
              Snapshot.Tcb.BasePriority,
              Snapshot.Tcb.WaitReason,
              Snapshot.Tcb.WaitMode,
              Snapshot.Tcb.WaitIrql);
    KdbpPrint("  Stack: initial %p, base %p, limit %p, current %p\n",
              Snapshot.Tcb.InitialStack,
              Snapshot.Tcb.StackBase,
              (PVOID)Snapshot.Tcb.StackLimit,
              Snapshot.Tcb.KernelStack);
    KdbpPrint("  APC disable: kernel %d, special %d, state index %u\n",
              Snapshot.Tcb.KernelApcDisable,
              Snapshot.Tcb.SpecialApcDisable,
              Snapshot.Tcb.ApcStateIndex);
    KdbpPrint("  APC pending: kernel %u, user %u, kernel APC running %u\n",
              Snapshot.Tcb.ApcState.KernelApcPending,
              Snapshot.Tcb.ApcState.UserApcPending,
              Snapshot.Tcb.ApcState.KernelApcInProgress);

    KdbpPrint("  Start %p ", Snapshot.StartAddress);
    KdbSymPrintAddress(Snapshot.StartAddress, NULL);
    KdbpPrint(", Win32 start %p ", Snapshot.Win32StartAddress);
    KdbSymPrintAddress(Snapshot.Win32StartAddress, NULL);
    KdbPuts("\n");

    return TRUE;
}

static
VOID
KdbpPrintApcQueue(
    _In_ PCSTR Name,
    _In_ PLIST_ENTRY Head)
{
    LIST_ENTRY Entry;
    PLIST_ENTRY Current;
    ULONG Count = 0;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, Head, sizeof(Entry))))
        return;

    KdbpPrint("%s APCs:\n", Name);

    for (Current = Entry.Flink;
         (Current != Head) && (Count < 64) && !KdbpIsOutputAborted();
         Current = Entry.Flink, Count++)
    {
        PKAPC Address = CONTAINING_RECORD(Current, KAPC, ApcListEntry);
        KAPC Apc;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Apc, Address, sizeof(Apc))))
        {
            KdbpPrint("  %p cannot be read\n", Address);
            return;
        }

        KdbpPrint("  %p kernel %p ", Address, (PVOID)Apc.KernelRoutine);
        KdbSymPrintAddress((PVOID)Apc.KernelRoutine, NULL);
        KdbpPrint(", normal %p ", (PVOID)Apc.NormalRoutine);
        KdbSymPrintAddress((PVOID)Apc.NormalRoutine, NULL);
        KdbPuts("\n");

        Entry = Apc.ApcListEntry;
    }

    if (Count == 0)
        KdbPuts("  none\n");
}

static
BOOLEAN
KdbpCmdApc(
    ULONG Argc,
    PCHAR Argv[])
{
    PETHREAD Thread = KdbpThreadArgument(Argc, Argv);
    UCHAR ApcStateIndex;

    if (Thread == NULL)
        return TRUE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&ApcStateIndex, &Thread->Tcb.ApcStateIndex, sizeof(ApcStateIndex))))
    {
        KdbpPrint("Thread %p cannot be read\n", Thread);
        return TRUE;
    }

    KdbpPrintApcQueue("Kernel", &Thread->Tcb.ApcState.ApcListHead[KernelMode]);
    KdbpPrintApcQueue("User", &Thread->Tcb.ApcState.ApcListHead[UserMode]);

    if (ApcStateIndex != 0)
    {
        KdbpPrintApcQueue("Saved kernel", &Thread->Tcb.SavedApcState.ApcListHead[KernelMode]);
        KdbpPrintApcQueue("Saved user", &Thread->Tcb.SavedApcState.ApcListHead[UserMode]);
    }

    return TRUE;
}

static
VOID
KdbpPrintOwner(
    _In_ const OWNER_ENTRY *Entry)
{
    if (Entry->OwnerThread != 0)
        KdbpPrint("  Owner thread %p, count %lu\n", (PVOID)Entry->OwnerThread, (ULONG)Entry->OwnerCount);
}

static
VOID
KdbpPrintResource(
    _In_ PERESOURCE Address,
    _In_ PERESOURCE Resource)
{
    OWNER_ENTRY Entry;
    ULONG TableSize = 0;
    ULONG Index;

    KdbpPrint("Resource %p %s, active %d, shared waiters %lu, exclusive waiters %lu, contention %lu\n",
              Address,
              (Resource->Flag & ResourceOwnedExclusive) ? "exclusive" : "shared",
              Resource->ActiveCount,
              Resource->NumberOfSharedWaiters,
              Resource->NumberOfExclusiveWaiters,
              Resource->ContentionCount);

    KdbpPrintOwner(&Resource->OwnerEntry);

    if ((Resource->OwnerTable != NULL) &&
        NT_SUCCESS(KdbpSafeReadMemory(&Entry, Resource->OwnerTable, sizeof(Entry))))
    {
        TableSize = min(Entry.TableSize, 64);
    }

    for (Index = 1; Index < TableSize; Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, &Resource->OwnerTable[Index], sizeof(Entry))))
            break;

        KdbpPrintOwner(&Entry);
    }
}

static
BOOLEAN
KdbpCmdLocks(
    ULONG Argc,
    PCHAR Argv[])
{
    static ERESOURCE Resource;
    BOOLEAN Verbose = FALSE;
    PLIST_ENTRY Current;
    LIST_ENTRY Head;
    ULONG_PTR Address;
    ULONG Total = 0;
    ULONG Held = 0;

    if ((Argc > 1) && (strcmp(Argv[1], "-v") == 0))
    {
        Verbose = TRUE;
    }
    else if (Argc > 1)
    {
        if (!KdbpGetAddressExpression(Argv[1], &Address))
            return TRUE;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Resource, (PVOID)Address, sizeof(Resource))))
            KdbpPrint("Resource %p cannot be read\n", (PVOID)Address);
        else
            KdbpPrintResource((PERESOURCE)Address, &Resource);

        return TRUE;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Head, &ExpSystemResourcesList, sizeof(Head))))
        return TRUE;

    for (Current = Head.Flink;
         (Current != &ExpSystemResourcesList) && (Total < 0x100000) && !KdbpIsOutputAborted();
         Total++)
    {
        PERESOURCE Entry = CONTAINING_RECORD(Current, ERESOURCE, SystemResourcesList);

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Resource, Entry, sizeof(Resource))))
        {
            KdbpPrint("Resource %p cannot be read\n", Entry);
            break;
        }

        if ((Resource.ActiveCount != 0) ||
            (Resource.NumberOfSharedWaiters != 0) ||
            (Resource.NumberOfExclusiveWaiters != 0))
        {
            Held++;
            KdbpPrintResource(Entry, &Resource);
        }
        else if (Verbose)
        {
            KdbpPrintResource(Entry, &Resource);
        }

        Current = Resource.SystemResourcesList.Flink;
    }

    KdbpPrint("%lu resources, %lu held or waited on\n", Total, Held);
    return TRUE;
}

static
BOOLEAN
KdbpCmdProcessor(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG Current = KeGetCurrentPrcb()->Number;
    ULONG_PTR Number;
    ULONG Processor;
    PKPRCB Prcb;

    if (Argc < 2)
    {
        for (Processor = 0; Processor < KeProcessorCount; Processor++)
        {
            Prcb = KiProcessorBlock[Processor];
            if (Prcb == NULL)
                continue;

            KdbpPrint("%s%lu  PRCB %p  thread %p  pc %p\n",
                      (Processor == Current) ? "*" : " ",
                      Processor,
                      Prcb,
                      Prcb->CurrentThread,
                      (Processor == Current) ? (PVOID)KeGetContextPc(KdbCurrentTrapFrame)
                                             : (PVOID)KeGetContextPc(&Prcb->ProcessorState.ContextFrame));
        }

        return TRUE;
    }

    if (_stricmp(Argv[1], "current") == 0)
    {
        Number = Current;
    }
    else if (!KdbpGetHexNumber(Argv[1], &Number))
    {
        KdbpPrint("%s is not a processor number\n", Argv[1]);
        return TRUE;
    }

    if ((Number >= KeProcessorCount) || (KiProcessorBlock[Number] == NULL))
    {
        KdbpPrint("There is no processor %Iu\n", Number);
        return TRUE;
    }

    Prcb = KiProcessorBlock[Number];
    KdbpFrameSelected = FALSE;
    if (Number == Current)
    {
        KdbpViewContext = NULL;
        KdbCurrentThread = KdbOriginalThread;
        KdbCurrentProcess = (PEPROCESS)KdbOriginalThread->Tcb.ApcState.Process;
    }
    else
    {
        PEPROCESS Process = NULL;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Process, &Prcb->CurrentThread->ApcState.Process, sizeof(Process))) ||
            (Process == NULL))
        {
            KdbpPrint("The thread processor %Iu runs, %p, cannot be read\n", Number, Prcb->CurrentThread);
            return TRUE;
        }

        KdbpViewContext = &Prcb->ProcessorState.ContextFrame;
        KdbCurrentThread = (PETHREAD)Prcb->CurrentThread;
        KdbCurrentProcess = Process;
    }

    KdbpPrint("Processor %Iu\n", Number);
    return TRUE;
}

static
VOID
KdbpUseContext(
    _In_opt_ PKDB_KTRAP_FRAME Context,
    _In_ PCSTR What)
{
    KdbpViewContext = Context;
    KdbpFrameSelected = FALSE;

    if (Context == NULL)
    {
        KdbPuts("Back to the context of the stop\n");
        return;
    }

    KdbpPrint("%s, pc %p ", What, (PVOID)KeGetContextPc(Context));
    KdbSymPrintAddress((PVOID)KeGetContextPc(Context), Context);
    KdbPuts("\n");
}

static
BOOLEAN
KdbpCmdContextRecord(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address;

    if (Argc < 2)
    {
        KdbpUseContext(NULL, NULL);
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&KdbpRecordContext, (PVOID)Address, sizeof(KdbpRecordContext))))
    {
        KdbpPrint("No context record can be read at %p\n", (PVOID)Address);
        return TRUE;
    }

    KdbpUseContext(&KdbpRecordContext, "Context record");
    return TRUE;
}

static
BOOLEAN
KdbpCmdTrapFrame(
    ULONG Argc,
    PCHAR Argv[])
{
    static KTRAP_FRAME TrapFrame;
    ULONG_PTR Address;

    if (Argc < 2)
    {
        KdbpUseContext(NULL, NULL);
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&TrapFrame, (PVOID)Address, sizeof(TrapFrame))))
    {
        KdbpPrint("No trap frame can be read at %p\n", (PVOID)Address);
        return TRUE;
    }

    RtlZeroMemory(&KdbpRecordContext, sizeof(KdbpRecordContext));
    KdbpRecordContext.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(&TrapFrame, NULL, &KdbpRecordContext);

    KdbpUseContext(&KdbpRecordContext, "Trap frame");
    return TRUE;
}

static
BOOLEAN
KdbpCmdExceptionRecord(
    ULONG Argc,
    PCHAR Argv[])
{
    EXCEPTION_RECORD Record;
    ULONG_PTR Address;
    ULONG Index;

    if (Argc < 2)
    {
        if (!KdbCurrentExceptionRecordValid)
        {
            KdbPuts("This stop has no exception record\n");
            return TRUE;
        }

        Record.ExceptionCode = KdbCurrentExceptionRecord.ExceptionCode;
        Record.ExceptionFlags = KdbCurrentExceptionRecord.ExceptionFlags;
        Record.ExceptionAddress = (PVOID)(ULONG_PTR)KdbCurrentExceptionRecord.ExceptionAddress;
        Record.NumberParameters = KdbCurrentExceptionRecord.NumberParameters;
        for (Index = 0; Index < EXCEPTION_MAXIMUM_PARAMETERS; Index++)
            Record.ExceptionInformation[Index] = (ULONG_PTR)KdbCurrentExceptionRecord.ExceptionInformation[Index];
    }
    else
    {
        if (!KdbpGetAddressExpression(Argv[1], &Address))
            return TRUE;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Record, (PVOID)Address, sizeof(Record))))
        {
            KdbpPrint("No exception record can be read at %p\n", (PVOID)Address);
            return TRUE;
        }
    }

    KdbpPrint("ExceptionAddress: %p ", Record.ExceptionAddress);
    KdbSymPrintAddress(Record.ExceptionAddress, NULL);
    KdbpPrint("\nExceptionCode: %08lx\nExceptionFlags: %08lx\nNumberParameters: %lu\n",
              Record.ExceptionCode,
              Record.ExceptionFlags,
              Record.NumberParameters);

    for (Index = 0; (Index < Record.NumberParameters) && (Index < EXCEPTION_MAXIMUM_PARAMETERS); Index++)
        KdbpPrint("   Parameter[%lu]: %p\n", Index, (PVOID)Record.ExceptionInformation[Index]);

    return TRUE;
}

static
BOOLEAN
KdbpCmdFrame(
    ULONG Argc,
    PCHAR Argv[])
{
    PKDB_KTRAP_FRAME Base = (KdbpViewContext != NULL) ? KdbpViewContext : KdbCurrentTrapFrame;
    ULONG_PTR Number = 0;
    ULONG Index;

    if (Base == NULL)
        return TRUE;

    if ((Argc > 1) && !KdbpGetHexNumber(Argv[1], &Number))
    {
        KdbpPrint("%s is not a frame number\n", Argv[1]);
        return TRUE;
    }

    KdbpFrameContext = *Base;
    for (Index = 0; Index < Number; Index++)
    {
        if (!KiUnwindFrame(&KdbpFrameContext, (Index != 0)))
        {
            KdbpPrint("The stack has only %lu frames\n", Index + 1);
            return TRUE;
        }
    }

    KdbpFrameSelected = (Number != 0);
    KdbpPrint("Frame %Iu, pc %p ", Number, (PVOID)KeGetContextPc(&KdbpFrameContext));
    KdbSymPrintAddress((PVOID)KeGetContextPc(&KdbpFrameContext), &KdbpFrameContext);
    KdbPuts("\n");
    return TRUE;
}

static
BOOLEAN
KdbpCmdEvaluate(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Expression[KDB_COMMAND_MAX];
    ULONG_PTR Value = 0;

    if (Argc < 2)
    {
        KdbPuts("An expression is needed\n");
        return TRUE;
    }

    /* The expression is the rest of the line, spaces and all */
    if (!KdbpJoinWords(1, Argc, Argv, Expression, sizeof(Expression)) ||
        !KdbpGetAddressExpression(Expression, &Value))
    {
        return TRUE;
    }

    KdbpSetLastExpressionValue(Value);
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
    static CHAR Tail[KDB_MAXIMUM_LOG_BYTES];
    ULONG_PTR Wanted = KDB_DEFAULT_LOG_BYTES;
    ULONG Length;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Wanted))
        return TRUE;

    if ((Wanted == 0) || (Wanted > sizeof(Tail)))
    {
        KdbpPrint("dmesg shows from 1 to 0x%lx bytes of the end of the log\n", (ULONG)sizeof(Tail));
        return TRUE;
    }

    Length = KdpCopyPrintBufferTail(Tail, (ULONG)Wanted);

    if (Length == 0)
    {
        KdbPuts("Nothing has been printed\n");
        return TRUE;
    }

    KdbpPrintBlock(Tail, Length);
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

    if ((_stricmp(Argv[3], "on") != 0) && (_stricmp(Argv[3], "off") != 0))
    {
        KdbpPrint("%s is not on or off\n", Argv[3]);
        return TRUE;
    }

    State = (_stricmp(Argv[3], "on") == 0);

    if (!NT_SUCCESS(KdpSetFilterState((ULONG)Component,
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

    ASSERT(KdbActive);
    ASSERT(!KdbBugCheckRequested);

    KdbpSetContinueType(kdHandleException);
    KdbpLeaveLoop = TRUE;
    KdbBugCheckRequested = TRUE;

    KdbPuts("Crashing on request\n");

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

    if ((_stricmp(Argv[2], "first") != 0) && (_stricmp(Argv[2], "last") != 0))
    {
        KdbpPrint("%s is not first or last\n", Argv[2]);
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

static
BOOLEAN
KdbpCmdKdFiles(
    ULONG Argc,
    PCHAR Argv[])
{
    BOOLEAN Pull = (_stricmp(Argv[0], "!kdpull") == 0);
    NTSTATUS Status;

    if (Argc != 2)
    {
        KdbpPrint("%s needs %s\n", Argv[0], Pull ? "the NT path of a file" : "the name of a service");
        return TRUE;
    }

    Status = KdQueueFileRequest(Pull ? KD_FILE_REQUEST_PULL : KD_FILE_REQUEST_RELOAD, Argv[1]);
    if (NT_SUCCESS(Status))
        KdbpPrint("%s %s runs after cont\n", Argv[0], Argv[1]);
    else
        KdbpPrint("%s %s was refused (0x%08lx)\n", Argv[0], Argv[1], Status);

    return TRUE;
}

/*
 * Runs the stopped thread until the function it is in returns, by a
 * breakpoint on the return address that only holds once the stack is back
 * above where the function's own frame is: a deeper call of the same function
 * returning there first does not count.
 */
static
BOOLEAN
KdbpCmdGoUp(
    ULONG Argc,
    PCHAR Argv[])
{
    static CONTEXT Caller;
    static CHAR Condition[64];

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    if (KdbCurrentTrapFrame == NULL)
    {
        KdbPuts("There is no stopped thread to run\n");
        return TRUE;
    }

    Caller = *KdbCurrentTrapFrame;
    if (!KiUnwindFrame(&Caller, FALSE))
    {
        KdbPuts("Where this function returns to cannot be found\n");
        return TRUE;
    }

    RtlStringCbPrintfA(Condition, sizeof(Condition), "sp >= 0x%Ix", (ULONG_PTR)KeGetContextStackRegister(&Caller));
    return KdbpRunTo((ULONG_PTR)KeGetContextPc(&Caller), Condition);
}

static
BOOLEAN
KdbpCmdIgnore(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Number;
    ULONG_PTR Count;

    if (Argc != 3)
    {
        KdbPuts("A breakpoint number and how many hits to let pass are needed\n");
        return TRUE;
    }

    if (!KdbpGetHexNumber(Argv[1], &Number) || (Number > MAXLONG))
    {
        KdbpPrint("%s is not a breakpoint number\n", Argv[1]);
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[2], &Count))
        return TRUE;

    if (Count > MAXULONG)
    {
        KdbpPrint("At most 0x%lx hits are let pass\n", MAXULONG);
        return TRUE;
    }

    if (!KdbpSetBreakPointIgnoreCount((LONG)Number, (ULONG)Count))
        KdbpPrint("There is no breakpoint %s\n", Argv[1]);
    else
        KdbpPrint("Breakpoint %Ix lets the next %Iu hits pass\n", Number, Count);

    return TRUE;
}

/* `display <command>` runs the command at every stop; `display` alone lists them */
static
BOOLEAN
KdbpCmdDisplay(
    ULONG Argc,
    PCHAR Argv[])
{
    BOOLEAN Any = FALSE;
    ULONG Index;

    if (Argc < 2)
    {
        for (Index = 0; Index < KDB_DISPLAY_MAX; Index++)
        {
            if (KdbpDisplays[Index][0] != ANSI_NULL)
            {
                KdbpPrint("%lu: %s\n", Index + 1, KdbpDisplays[Index]);
                Any = TRUE;
            }
        }

        if (!Any)
            KdbPuts("Nothing is run at every stop\n");

        return TRUE;
    }

    for (Index = 0; (Index < KDB_DISPLAY_MAX) && (KdbpDisplays[Index][0] != ANSI_NULL); Index++)
        NOTHING;

    if (Index == KDB_DISPLAY_MAX)
    {
        KdbpPrint("All %u commands run at every stop are in use\n", KDB_DISPLAY_MAX);
        return TRUE;
    }

    if (!KdbpJoinWords(1, Argc, Argv, KdbpDisplays[Index], sizeof(KdbpDisplays[Index])))
    {
        KdbpPrint("A command run at every stop has at most %u characters\n", KDB_BREAKPOINT_COMMANDS_MAX - 1);
        return TRUE;
    }

    KdbpPrint("%lu: %s\n", Index + 1, KdbpDisplays[Index]);
    return TRUE;
}

static
BOOLEAN
KdbpCmdUndisplay(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Number;

    if (Argc != 2)
    {
        KdbPuts("The number of a command run at every stop, or *, is needed\n");
        return TRUE;
    }

    if (strcmp(Argv[1], "*") == 0)
    {
        RtlZeroMemory(KdbpDisplays, sizeof(KdbpDisplays));
        KdbPuts("Nothing is run at every stop\n");
        return TRUE;
    }

    if (!KdbpGetHexNumber(Argv[1], &Number) || (Number == 0) || (Number > KDB_DISPLAY_MAX) ||
        (KdbpDisplays[Number - 1][0] == ANSI_NULL))
    {
        KdbpPrint("%s is not the number of a command run at every stop\n", Argv[1]);
        return TRUE;
    }

    KdbpDisplays[Number - 1][0] = ANSI_NULL;
    KdbpPrint("%Iu is no longer run at every stop\n", Number);
    return TRUE;
}

/* One value written every way a person may want to read it */
static
BOOLEAN
KdbpCmdFormats(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Expression[KDB_COMMAND_MAX];
    CHAR Binary[64 + 8 + 1];
    CHAR Text[sizeof(ULONG64) + 1];
    ULONG_PTR Value = 0;
    ULONG64 Wide;
    ULONG Position = 0;
    LONG Bit;
    ULONG Index;

    if (Argc < 2)
    {
        KdbPuts("An expression is needed\n");
        return TRUE;
    }

    if (!KdbpJoinWords(1, Argc, Argv, Expression, sizeof(Expression)) ||
        !KdbpGetAddressExpression(Expression, &Value))
    {
        return TRUE;
    }

    Wide = (ULONG64)Value;
    KdbpSetLastExpressionValue(Wide);

    for (Bit = 63; Bit >= 0; Bit--)
    {
        Binary[Position++] = (Wide & (1ULL << Bit)) ? '1' : '0';
        if (((Bit % 8) == 0) && (Bit != 0))
            Binary[Position++] = ' ';
    }
    Binary[Position] = ANSI_NULL;

    /* The bytes as text, the most significant first, as the number reads */
    for (Index = 0; Index < sizeof(ULONG64); Index++)
    {
        UCHAR Byte = (UCHAR)(Wide >> ((sizeof(ULONG64) - 1 - Index) * 8));

        Text[Index] = ((Byte >= ' ') && (Byte < 0x7F)) ? (CHAR)Byte : '.';
    }
    Text[sizeof(ULONG64)] = ANSI_NULL;

    KdbpPrint("Hex:     0x%I64x\n", Wide);
    KdbpPrint("Decimal: %I64u (signed %I64d)\n", Wide, (LONG64)Wide);
    KdbpPrint("Octal:   0%I64o\n", Wide);
    KdbpPrint("Binary:  %s\n", Binary);
    KdbpPrint("Chars:   %s\n", Text);
    KdbpPrint("Symbol:  ");
    if (!KdbSymPrintAddress((PVOID)Value, NULL))
        KdbPuts("none");
    KdbPuts("\n");

    return TRUE;
}

typedef struct _KDB_LIST_WALK
{
    PCHAR Command;
    BOOLEAN Resumed;
} KDB_LIST_WALK, *PKDB_LIST_WALK;

static
BOOLEAN
NTAPI
KdbpListEntry(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    static CHAR Command[KDB_COMMAND_MAX];
    PKDB_LIST_WALK Walk = Context;

    KdbpPrint("%p\n", Link);

    if (Walk->Command[0] != ANSI_NULL)
    {
        KdbpSetListEntryValue((ULONG_PTR)Link);
        RtlStringCbCopyA(Command, sizeof(Command), Walk->Command);

        if (KdbpCliInterpretCommand(Command))
        {
            Walk->Resumed = TRUE;
            return FALSE;
        }
    }

    return !KdbpIsOutputAborted();
}

/*
 * `!list <head> [count] [command]` walks a list of LIST_ENTRY links from its
 * head, showing each link and running the command for it with $entry
 * standing for the link.
 */
static
BOOLEAN
KdbpCmdList(
    ULONG Argc,
    PCHAR Argv[])
{
    static CHAR Command[KDB_COMMAND_MAX];
    KDB_LIST_WALK Walk;
    KDB_WALK_RESULT Result;
    PLIST_ENTRY Broken;
    ULONG_PTR Head;
    ULONG_PTR Maximum = KDB_MAXIMUM_LIST_WALK;
    ULONG Count = 0;

    if (Argc < 2)
    {
        KdbPuts("The address of the head of a list is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Head))
        return TRUE;

    if ((Argc > 2) && !KdbpGetAddressExpression(Argv[2], &Maximum))
        return TRUE;

    if ((Maximum == 0) || (Maximum > KDB_MAXIMUM_LIST_WALK))
    {
        KdbpPrint("A list walk shows from 1 to %u entries\n", KDB_MAXIMUM_LIST_WALK);
        return TRUE;
    }

    Command[0] = ANSI_NULL;
    if ((Argc > 3) && !KdbpJoinWords(3, Argc, Argv, Command, sizeof(Command)))
    {
        KdbPuts("The command is too long\n");
        return TRUE;
    }

    Walk.Command = Command;
    Walk.Resumed = FALSE;

    Result = KdbpWalkList((PLIST_ENTRY)Head, (ULONG)Maximum, KdbpListEntry, &Walk, &Count, &Broken);
    if (Walk.Resumed)
        return TRUE;

    if ((Result == KdbWalkTooLong) && (Maximum < KDB_MAXIMUM_LIST_WALK))
        KdbpPrint("%lu entries shown, and there are more\n", Count);
    else
        KdbpReportWalk(Result, Broken, (ULONG)Maximum);

    KdbpPrint("%lu entries\n", Count);
    return TRUE;
}

/* Where an address of the address space the machine stopped in is in physical memory */
static
BOOLEAN
KdbpCmdVirtualToPhysical(
    ULONG Argc,
    PCHAR Argv[])
{
    PHYSICAL_ADDRESS Physical;
    ULONG_PTR Address;

    if (Argc < 2)
    {
        KdbPuts("An address is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    /* TODO: a user address of a process looked at with proc is translated in the address space of the stop */
    Physical = MmGetPhysicalAddress((PVOID)Address);
    if (Physical.QuadPart == 0)
        KdbpPrint("%p is not mapped\n", (PVOID)Address);
    else
        KdbpPrint("%p is physical 0x%I64x\n", (PVOID)Address, Physical.QuadPart);

    return TRUE;
}

static BOOLEAN KdbpCmdDumpPhysicalBytes(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 1, KDB_DUMP_PHYSICAL | KDB_DUMP_TEXT);
}

static BOOLEAN KdbpCmdDumpPhysicalDwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 4, KDB_DUMP_PHYSICAL);
}

static BOOLEAN KdbpCmdDumpPhysicalQwords(ULONG Argc, PCHAR Argv[])
{
    return KdbpDumpMemory(Argc, Argv, 8, KDB_DUMP_PHYSICAL);
}

/* THE COMMANDS ***************************************************************/

static const KDB_COMMAND KdbpCommands[] =
{
    {"help", "help", "Lists what can be typed here", KdbpCmdHelp},
    {"?", "? <expression>", "Works out what an expression comes to", KdbpCmdEvaluate},
    {"cont", "cont [address]", "Lets the machine go on, or run to an address", KdbpCmdContinue},
    {"g", "g [address]", "Lets the machine go on, or run to an address", KdbpCmdContinue},
    {"gu", "gu", "Runs until the function the thread is in returns", KdbpCmdGoUp},
    {"handle", "handle", "Goes on and leaves the exception unhandled", KdbpCmdHandle},
    {"step", "step [count]", "Runs one instruction", KdbpCmdStep},
    {"t", "t [count]", "Runs one instruction", KdbpCmdStep},
    {"next", "next [count]", "Runs one instruction, over a call", KdbpCmdNext},
    {"p", "p [count]", "Runs one instruction, over a call", KdbpCmdNext},
    {"bl", "bl", "Lists the breakpoints", KdbpCmdBreakPointList},
    {"bp", "bp [/1] <address> [if <expr>] [do \"<commands>\"]", "Stops when an address is reached", KdbpCmdBreakPointSet},
    {"bpm", "bpm <r|w|rw|x> <addr> [size] [if <expr>] [do \"<commands>\"]", "Stops when an address is used", KdbpCmdBreakPointMemory},
    {"ignore", "ignore <number> <count>", "Lets a breakpoint's next hits pass", KdbpCmdIgnore},
    {"display", "display [command]", "Runs a command at every stop, or lists them", KdbpCmdDisplay},
    {"undisplay", "undisplay <number|*>", "Stops running a command at every stop", KdbpCmdUndisplay},
    {"bc", "bc <number|*>", "Gives up a breakpoint", KdbpCmdBreakPointClear},
    {"bd", "bd <number|*>", "Leaves a breakpoint in place but off", KdbpCmdBreakPointDisable},
    {"be", "be <number|*>", "Turns a breakpoint back on", KdbpCmdBreakPointEnable},
    {"regs", "regs", "Shows every register", KdbpCmdRegisters},
    {"r", "r <name> [value]", "Shows or changes one register", KdbpCmdRegister},
    {"fpregs", "fpregs [register]", "Shows the floating-point and vector registers", KdbpCmdFloatingRegisters},
    {"vregs", "vregs [register]", "Shows the floating-point and vector registers", KdbpCmdFloatingRegisters},
    {"setfpreg", "setfpreg <register> <hex value>", "Changes a floating-point or vector register", KdbpCmdSetFloatingRegister},
    {"setvreg", "setvreg <register> <hex value>", "Changes a floating-point or vector register", KdbpCmdSetFloatingRegister},
    {"bt", "bt [all] [verbose] [frames]", "Shows what led here, or for every thread", KdbpCmdBackTrace},
    {"k", "k [all] [verbose] [frames]", "Shows what led here, or for every thread", KdbpCmdBackTrace},
    {"db", "db <address> [count]", "Shows memory as bytes", KdbpCmdDumpBytes},
    {"dw", "dw <address> [count]", "Shows memory as words", KdbpCmdDumpWords},
    {"dd", "dd <address> [count]", "Shows memory as double words", KdbpCmdDumpDwords},
    {"dc", "dc <address> [count]", "Shows memory as double words and text", KdbpCmdDumpDwordsText},
    {"dq", "dq <address> [count]", "Shows memory as quad words", KdbpCmdDumpQwords},
    {"dp", "dp <address> [count]", "Shows memory as pointers", KdbpCmdDumpPointers},
    {"da", "da <address> [count]", "Shows memory as a string, up to its end", KdbpCmdDumpText},
    {"du", "du <address> [count]", "Shows memory as a wide string", KdbpCmdDumpUnicode},
    {"dds", "dds <address> [count]", "Shows double words with the symbols they point at", KdbpCmdDumpDwordSymbols},
    {"dqs", "dqs <address> [count]", "Shows quad words with the symbols they point at", KdbpCmdDumpQwordSymbols},
    {"dps", "dps <address> [count]", "Shows pointers with the symbols they point at", KdbpCmdDumpPointerSymbols},
    {"ln", "ln <address>", "Names the symbol nearest an address", KdbpCmdNearestSymbol},
    {".formats", ".formats <expression>", "Shows a value as hex, decimal, octal, binary and text", KdbpCmdFormats},
    {"!list", "!list <head> [count] [command]", "Walks a list, running a command for each $entry", KdbpCmdList},
    {"!vtop", "!vtop <address>", "Says where an address is in physical memory", KdbpCmdVirtualToPhysical},
    {"!db", "!db <physical address> [count]", "Shows physical memory as bytes", KdbpCmdDumpPhysicalBytes},
    {"!dd", "!dd <physical address> [count]", "Shows physical memory as double words", KdbpCmdDumpPhysicalDwords},
    {"!dq", "!dq <physical address> [count]", "Shows physical memory as quad words", KdbpCmdDumpPhysicalQwords},
    {"search", "search <address> <length> <byte>...", "Looks for bytes in memory", KdbpCmdSearchMemory},
    {"compare", "compare <address> <address> <length>", "Compares two ranges of memory", KdbpCmdCompareMemory},
    {"fill", "fill <address> <length> <byte>...", "Fills memory with bytes", KdbpCmdFillMemory},
    {"move", "move <source> <destination> <length>", "Copies memory", KdbpCmdMoveMemory},
    {"eb", "eb <address> <value>...", "Changes bytes", KdbpCmdEditBytes},
    {"ew", "ew <address> <value>...", "Changes words", KdbpCmdEditWords},
    {"ed", "ed <address> <value>...", "Changes double words", KdbpCmdEditDwords},
    {"eq", "eq <address> <value>...", "Changes quad words", KdbpCmdEditQwords},
    {"u", "u [address] [count]", "Shows the instructions at an address", KdbpCmdDisassemble},
    {"mod", "mod", "Lists the loaded images", KdbpCmdModules},
    {"lm", "lm", "Lists the loaded images", KdbpCmdModules},
    {"sym", "sym [module] <pattern>", "Looks for symbols", KdbpCmdSymbols},
    {"x", "x [module] <pattern>", "Looks for symbols", KdbpCmdSymbols},
    {"proc", "proc [id]", "Lists the processes, or looks at one", KdbpCmdProcesses},
    {"thread", "thread [id]", "Lists the threads, or looks at one", KdbpCmdThreads},
    {"!thread", "!thread [address]", "Shows a thread in detail", KdbpCmdThreadDetail},
    {"!apc", "!apc [address]", "Shows the APCs queued to a thread", KdbpCmdApc},
    {"!locks", "!locks [-v|address]", "Shows the resources that are held or waited on", KdbpCmdLocks},
    {"!process", "!process [address]", "Shows a process", KdbpCmdProcessDetail},
    {"!teb", "!teb [address]", "Shows a thread environment block", KdbpCmdTeb},
    {"!peb", "!peb [address]", "Shows a process environment block", KdbpCmdPeb},
    {"!object", "!object <address>", "Shows the header, type and name of an object", KdbpCmdObjectHeader},
    {"!drvobj", "!drvobj <address>", "Shows a driver object", KdbpCmdDriverObject},
    {"!devobj", "!devobj <address>", "Shows a device object", KdbpCmdDeviceObject},
    {"!devstack", "!devstack <address>", "Shows the stack a device object is in", KdbpCmdDeviceStack},
    {"!irp", "!irp <address>", "Shows an IRP and its stack locations", KdbpCmdIrp},
    {"!fileobj", "!fileobj <address>", "Shows a file object", KdbpCmdFileObject},
    {"!prcb", "!prcb [processor]", "Shows a processor control block", KdbpCmdPrcb},
    {"!ready", "!ready [processor]", "Shows the ready queues of a processor", KdbpCmdReady},
    {"!dpc", "!dpc [processor]", "Shows the DPC queues of a processor", KdbpCmdDpc},
    {"!timer", "!timer [address]", "Lists the queued timers, or shows one", KdbpCmdTimer},
    {"!interrupt", "!interrupt <address>", "Shows an interrupt object", KdbpCmdInterrupt},
    {"!dispatcher", "!dispatcher <address>", "Shows a dispatcher object and its waiters", KdbpCmdDispatcher},
    {"idt", "idt [address]", "Lists the interrupt vectors, or those handled on the page of an address", KdbpCmdInterruptTable},
    {"!irqs", "!irqs [vector]", "Lists the interrupt objects connected on this processor, or those of one vector", KdbpCmdInterrupts},
    {"cpu", "cpu [number|current]", "Lists the processors, or looks at a frozen one", KdbpCmdProcessor},
    {".cxr", ".cxr [address]", "Looks at the context record at an address, or back at the stop", KdbpCmdContextRecord},
    {".trap", ".trap [address]", "Looks at the trap frame at an address, or back at the stop", KdbpCmdTrapFrame},
    {".exr", ".exr [address]", "Shows an exception record, or the one of the stop", KdbpCmdExceptionRecord},
    {".frame", ".frame [number]", "Looks at a frame of the stack, or back at its top", KdbpCmdFrame},
    {"!analyze", "!analyze", "Says what stopped the machine and where", KdbpCmdAnalyze},
    {"!pci", "!pci [bus [device [function]]|bus:device.function]", "Lists the PCI functions, or shows one in detail", KdbpCmdPci},
    {"!devtree", "!devtree", "Lists the Plug and Play device tree", KdbpCmdDeviceTree},
    {"!devnode", "!devnode [address]", "Shows a device node and its resources, or the root one", KdbpCmdDeviceNode},
    {"dmesg", "dmesg [bytes]", "Shows the end of what the system has printed", KdbpCmdLog},
    {"dt", "dt [[module!]type [address]]", "Shows the layout of a kernel type, or one at an address", KdbpCmdDisplayType},
    {"version", "version", "Shows the versions of the system and of the debugger", KdbpCmdVersion},
    {"echo", "echo [text]", "Prints a line of text", KdbpCmdEcho},
    {"alias", "alias [name [command]]", "Lists the aliases, shows one, or makes a name stand for a command", KdbpCmdAlias},
    {"unalias", "unalias <name>", "Forgets an alias", KdbpCmdUnalias},
    {"log", "log [on|off|clear|show]", "Keeps or shows a transcript of the debugger's output", KdbpCmdTranscript},
    {"script", "script <address> <length>", "Runs the commands written in memory, one per line", KdbpCmdScript},
    {"repeat", "repeat <count> <command>", "Runs a command several times", KdbpCmdRepeat},
    {"selftest", "selftest", "Checks the debugger's parsers, guarded reads and tables", KdbpCmdSelfTest},
    {"filter", "filter <comp> <level> <on|off>", "Decides what is printed", KdbpCmdFilter},
    {"cond", "cond <nr|*> <first|last> <what>", "Decides what to stop for", KdbpCmdCondition},
    {"bugcheck", "bugcheck", "Crashes the machine on purpose", KdbpCmdBugCheck},
    {"reboot", "reboot", "Restarts the machine", KdbpCmdReboot},
    {"!kdpull", "!kdpull <file>", "After cont, replaces a file with the host's .kdfiles copy", KdbpCmdKdFiles},
    {"!kdreload", "!kdreload <service>", "After cont, restarts a driver from the host's .kdfiles copy", KdbpCmdKdFiles},

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
/*
 * Splits a command into its words, in place. A word in double quotes may hold
 * spaces; the quotes themselves are dropped.
 */
static
ULONG
KdbpSplitCommand(
    _Inout_ PCHAR Command,
    _Out_writes_(KDB_ARGUMENT_MAX) PCHAR *Argv,
    _Out_ PBOOLEAN TooMany)
{
    ULONG Argc = 0;
    PCHAR Current = Command;

    *TooMany = FALSE;

    for (;;)
    {
        PCHAR Write;
        BOOLEAN Quoted = FALSE;

        while ((*Current == ' ') || (*Current == '\t'))
            Current++;

        if (*Current == ANSI_NULL)
            break;

        if (Argc == KDB_ARGUMENT_MAX)
        {
            *TooMany = TRUE;
            break;
        }

        Argv[Argc++] = Current;
        Write = Current;

        while ((*Current != ANSI_NULL) && (Quoted || ((*Current != ' ') && (*Current != '\t'))))
        {
            if (*Current == '"')
            {
                Quoted = !Quoted;
                Current++;
                continue;
            }

            *Write++ = *Current++;
        }

        if (*Current != ANSI_NULL)
            Current++;

        *Write = ANSI_NULL;
    }

    return Argc;
}

/* Where the statement starting at Statement ends: at a semicolon outside quotes, or with the line */
static
PCHAR
KdbpStatementEnd(
    _In_ PCHAR Statement)
{
    BOOLEAN Quoted = FALSE;

    for (; *Statement != ANSI_NULL; Statement++)
    {
        if (*Statement == '"')
            Quoted = !Quoted;
        else if ((*Statement == ';') && !Quoted)
            break;
    }

    return Statement;
}

BOOLEAN
KdbpIsCommandName(
    _In_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; KdbpCommands[Index].Name != NULL; Index++)
    {
        if (_stricmp(KdbpCommands[Index].Name, Name) == 0)
            return TRUE;
    }

    return FALSE;
}

ULONG
KdbpCheckCommandTable(VOID)
{
    ULONG Failures = 0;
    ULONG Index;
    ULONG Other;

    for (Index = 0; KdbpCommands[Index].Name != NULL; Index++)
    {
        if ((KdbpCommands[Index].Syntax == NULL) ||
            (KdbpCommands[Index].Help == NULL) ||
            (KdbpCommands[Index].Routine == NULL))
        {
            KdbpPrint("selftest: the command %s is incomplete\n", KdbpCommands[Index].Name);
            Failures++;
        }

        for (Other = Index + 1; KdbpCommands[Other].Name != NULL; Other++)
        {
            if (_stricmp(KdbpCommands[Index].Name, KdbpCommands[Other].Name) == 0)
            {
                KdbpPrint("selftest: the command %s is listed twice\n", KdbpCommands[Index].Name);
                Failures++;
            }
        }
    }

    return Failures;
}

PCSTR
KdbpGetCommandName(
    _In_ ULONG Index)
{
    if (Index >= RTL_NUMBER_OF(KdbpCommands) - 1)
        return NULL;

    return KdbpCommands[Index].Name;
}

/* Takes back a command's request for the machine to run, for a caller that only wanted to see it made */
VOID
KdbpClearResume(VOID)
{
    KdbpLeaveLoop = FALSE;
    KdbpPendingStepOvers = 0;
    KdbpSetContinueType(kdHandleException);
}

VOID
KdbpResetView(VOID)
{
    KdbpViewContext = NULL;
    KdbpFrameSelected = FALSE;
}

static
BOOLEAN
KdbpRunStatement(
    _Inout_ PCHAR Statement,
    _Inout_updates_(KDB_COMMAND_MAX) PCHAR Expanded)
{
    PCHAR Argv[KDB_ARGUMENT_MAX];
    BOOLEAN TooMany;
    ULONG Argc;
    ULONG Index;

    KdbpLeaveLoop = FALSE;

    /* The statement as it was written, for the components that read their own commands */
    RtlStringCbCopyA(Expanded, KDB_COMMAND_MAX, Statement);

    Argc = KdbpSplitCommand(Statement, Argv, &TooMany);
    if (Argc == 0)
        return FALSE;

    if (TooMany)
    {
        KdbpPrint("A command line holds at most %u words\n", KDB_ARGUMENT_MAX);
        return FALSE;
    }

    if (KdbpExpandAlias(Argc, Argv, Expanded, KDB_COMMAND_MAX))
        return (Expanded[0] != ANSI_NULL) && KdbpCliInterpretCommand(Expanded);

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

        if (!MmIsAddressValid((PVOID)Routine))
            continue;

        if (Routine(Expanded, Argc, Argv))
            return KdbpLeaveLoop;
    }

    KdbpPrint("%s is not something that can be typed here. "
              "Type help for what can.\n", Argv[0]);

    return FALSE;
}

/* Runs the statements of a line one after the other, until one lets the machine run */
static
BOOLEAN
KdbpRunCommand(
    _In_ PCHAR Command,
    _Inout_updates_(KDB_COMMAND_MAX) PCHAR Buffer,
    _Inout_updates_(KDB_COMMAND_MAX) PCHAR Expanded)
{
    PCHAR Statement;

    KdbpLeaveLoop = FALSE;

    /* The line is taken apart in a copy, since the words point into it */
    strncpy(Buffer, Command, KDB_COMMAND_MAX - 1);
    Buffer[KDB_COMMAND_MAX - 1] = ANSI_NULL;

    for (Statement = Buffer;;)
    {
        PCHAR End = KdbpStatementEnd(Statement);
        BOOLEAN Last = (*End == ANSI_NULL);

        *End = ANSI_NULL;

        if (KdbpRunStatement(Statement, Expanded))
            return TRUE;

        if (Last)
            return FALSE;

        Statement = End + 1;
    }
}

/*
 * Runs one command line. Returns TRUE when the machine is to run again.
 */
BOOLEAN
KdbpCliInterpretCommand(
    _In_ PCHAR Command)
{
    BOOLEAN Leave;

    if (KdbpCommandDepth >= KDB_MAXIMUM_COMMAND_DEPTH)
    {
        KdbpPrint("Commands that run commands go no deeper than %u\n", KDB_MAXIMUM_COMMAND_DEPTH);
        return FALSE;
    }

    KdbpTranscriptCommand(Command);

    KdbpCommandDepth++;
    Leave = KdbpRunCommand(Command,
                           KdbpCommandBuffers[KdbpCommandDepth - 1],
                           KdbpAliasBuffers[KdbpCommandDepth - 1]);
    KdbpCommandDepth--;

    return Leave;
}

static
VOID
KdbpReportUnresponsiveProcessor(VOID)
{
    ULONG Current = KeGetCurrentPrcb()->Number;
    ULONG Processor;

    if (KiBugCheckData[0] != CLOCK_WATCHDOG_TIMEOUT)
        return;

    for (Processor = 0; Processor < KeProcessorCount; Processor++)
    {
        PKPRCB Prcb = KiProcessorBlock[Processor];

        if ((Processor == Current) || (Prcb == NULL) || ((ULONG_PTR)Prcb != KiBugCheckData[3]))
            continue;

        KdbpPagerReset();
        KdbpPrint("\nProcessor %lu stopped answering\n", Processor);
        KdbpBackTrace(&Prcb->ProcessorState.ContextFrame, KdbpScreenRows() - 5, FALSE);
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

    /* Each block is paged on its own, so a stop that nobody watches never waits for a key */
    if (Context != NULL)
    {
        KdbpPagerReset();
        KdbpCmdRegisters(1, NULL);

        KdbpPagerReset();
        KdbpBackTrace(Context, KdbpScreenRows() - 3, FALSE);
    }

    if (KiBugCheckData[0] != 0)
    {
        KdbpPagerReset();
        KdbpCmdThreadDetail(1, NULL);

        KdbpPagerReset();
        KdbpCmdApc(1, NULL);

        KdbpPagerReset();
        KdbpCmdAnalyze(1, NULL);

        KdbpReportUnresponsiveProcessor();

        KdbpPagerReset();
        KdbpPrint("\n*** Fatal System Error: 0x%08lx (0x%p,0x%p,0x%p,0x%p)\n",
                  (ULONG)KiBugCheckData[0],
                  (PVOID)KiBugCheckData[1],
                  (PVOID)KiBugCheckData[2],
                  (PVOID)KiBugCheckData[3],
                  (PVOID)KiBugCheckData[4]);
    }

    KdbpPagerReset();
}

static
VOID
KdbpPersistSessionLog(VOID)
{
    /* Writing through the crash storage path resets the boot disk, so only a bug check session may do it */
    if (KiBugCheckData[0] != 0)
        (VOID)KdpWriteBootLogAtCrash(NULL, NULL);
}

/* Runs what is to be run at every stop. Returns TRUE when one of them let the machine run */
static
BOOLEAN
KdbpRunDisplays(VOID)
{
    static CHAR Command[KDB_BREAKPOINT_COMMANDS_MAX];
    ULONG Index;

    for (Index = 0; Index < KDB_DISPLAY_MAX; Index++)
    {
        if (KdbpDisplays[Index][0] == ANSI_NULL)
            continue;

        RtlStringCbCopyA(Command, sizeof(Command), KdbpDisplays[Index]);
        KdbpPagerReset();
        KdbpPrint("kdb:> %s\n", Command);

        if (KdbpCliInterpretCommand(Command))
            return TRUE;
    }

    return FALSE;
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

    KdbpViewContext = NULL;
    KdbpFrameSelected = FALSE;
    KdbpPagerReset();
    KdbpReportStop(EnteredOnSingleStep);
    KdbpPersistSessionLog();

    /* A p with a count goes on by itself as long as each stop is the end of its step */
    if (KdbpPendingStepOvers != 0)
    {
        if (EnteredOnSingleStep)
        {
            KdbpPendingStepOvers--;
            KdbpLeaveLoop = FALSE;
            KdbpStepOver();
            if (KdbpLeaveLoop)
                return;
        }

        KdbpPendingStepOvers = 0;
    }

    /* The commands of the breakpoint the machine stopped at, then those of every stop */
    KdbpLeaveLoop = FALSE;
    if (KdbpTakeStopCommands(Command, sizeof(Command)))
    {
        KdbpPagerReset();
        KdbpPrint("kdb:> %s\n", Command);
        if (KdbpCliInterpretCommand(Command))
            return;
    }

    if (KdbpRunDisplays())
        return;

    KdbpPersistSessionLog();

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

        KdbpPersistSessionLog();
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
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DispatchTable);

    if (BootPhase == 0)
    {
        Status = KdbpCliInit();
        if (NT_SUCCESS(Status) && KdDebuggerEnabled)
            KdDebuggerNotPresent = FALSE;

        return Status;
    }

    return STATUS_SUCCESS;
}

/* EOF */
