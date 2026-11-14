/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The debugger's checks of itself: selftest
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_TEST_CAPTURE_SIZE   8192
#define KDB_TEST_SHOWN_OUTPUT   600
#define KDB_TEST_LINE_MAX       1024
#define KDB_TEST_MAX_BREAKPOINTS 64
#define KDB_TEST_SLOW_MS        250

typedef VOID (*PKDB_TEST_ROUTINE)(VOID);

typedef struct _KDB_TEST_GROUP
{
    PCSTR Name;
    PCSTR Help;
    PKDB_TEST_ROUTINE Routine;
} KDB_TEST_GROUP;

/* GLOBALS ********************************************************************/

static BOOLEAN KdbpTestCapturing;
static BOOLEAN KdbpTestVerbose;
static ULONG KdbpTestCaptured;
static BOOLEAN KdbpTestOverflow;
static CHAR KdbpTestOutput[KDB_TEST_CAPTURE_SIZE];
static CHAR KdbpTestLine[KDB_TEST_LINE_MAX];
static CHAR KdbpTestCommandText[KDB_TEST_LINE_MAX];
static CHAR KdbpTestMessage[256];
static ULONG KdbpTestChecks;
static ULONG KdbpTestFailures;
static PCSTR KdbpTestGroupName;

/* What the machine looked like when the checks started, put back after each group */
static CONTEXT KdbpTestSavedContext;
static PETHREAD KdbpTestSavedThread;
static PEPROCESS KdbpTestSavedProcess;
static LONG KdbpTestSavedLastBreakPoint;
static BOOLEAN KdbpTestBreakPointUsed[KDB_TEST_MAX_BREAKPOINTS];

/* Memory the memory and breakpoint commands are pointed at */
static UCHAR KdbpTestBytes[64];
static UCHAR KdbpTestCopy[64];
static WCHAR KdbpTestWide[] = L"LiberNT";
static PVOID KdbpTestPointers[2];
static CHAR KdbpTestScriptText[] = "echo KDBTEST-S1\n\necho KDBTEST-S2\n";
static LIST_ENTRY KdbpTestListHead;
static LIST_ENTRY KdbpTestListItems[3];

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
KdbpTestPrintMessage(
    _In_ BOOLEAN Passed)
{
    if (Passed)
        KdbpPrint("selftest: ok %s: %s\n", KdbpTestGroupName, KdbpTestMessage);
    else
        KdbpPrint("selftest: FAIL %s: %s\n", KdbpTestGroupName, KdbpTestMessage);
}

static
VOID
KdbpTestShowOutput(VOID)
{
    CHAR Shown[KDB_TEST_SHOWN_OUTPUT + 1];
    ULONG Length = min(KdbpTestCaptured, KDB_TEST_SHOWN_OUTPUT);

    RtlCopyMemory(Shown, KdbpTestOutput, Length);
    Shown[Length] = ANSI_NULL;
    KdbpPrint("  output (%lu bytes%s):\n%s\n", KdbpTestCaptured, KdbpTestOverflow ? ", cut" : "", Shown);
}

/*
 * Counts one check. A failed check is always reported; a passed one only when
 * the person asked to see everything.
 */
static
BOOLEAN
KdbpTestCheck(
    _In_ BOOLEAN Passed,
    _In_ PCSTR Format,
    ...)
{
    va_list Arguments;

    KdbpTestChecks++;
    if (!Passed)
        KdbpTestFailures++;

    if (Passed && !KdbpTestVerbose)
        return Passed;

    va_start(Arguments, Format);
    _vsnprintf(KdbpTestMessage, sizeof(KdbpTestMessage) - 1, Format, Arguments);
    va_end(Arguments);
    KdbpTestMessage[sizeof(KdbpTestMessage) - 1] = ANSI_NULL;

    KdbpTestPrintMessage(Passed);
    return Passed;
}

/*
 * Runs one command line with what it prints kept here rather than shown.
 * Returns what it printed; Resumed says whether it asked for the machine to
 * run, which the caller has to undo.
 */
static
PCSTR
KdbpTestRun(
    _In_ PCSTR Command,
    _Out_opt_ PBOOLEAN Resumed)
{
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    ULONGLONG Milliseconds;
    BOOLEAN Leave;

    RtlStringCbCopyA(KdbpTestCommandText, sizeof(KdbpTestCommandText), Command);
    RtlStringCbCopyA(KdbpTestLine, sizeof(KdbpTestLine), Command);

    KdbpTestCaptured = 0;
    KdbpTestOverflow = FALSE;
    KdbpTestOutput[0] = ANSI_NULL;

    KdbpPagerReset();
    Start = KeQueryPerformanceCounter(&Frequency);
    KdbpTestCapturing = TRUE;
    Leave = KdbpCliInterpretCommand(KdbpTestLine);
    KdbpTestCapturing = FALSE;
    KdbpPagerReset();

    /* A command that takes this long at a stop is worth knowing about, though it is not wrong */
    Milliseconds = (Frequency.QuadPart != 0)
                   ? (ULONGLONG)((KeQueryPerformanceCounter(NULL).QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart)
                   : 0;
    if (Milliseconds > KDB_TEST_SLOW_MS)
        KdbpPrint("selftest: slow: \"%s\" took %I64u ms\n", KdbpTestCommandText, Milliseconds);

    if (Resumed != NULL)
        *Resumed = Leave;

    return KdbpTestOutput;
}

static
VOID
KdbpTestFormat(
    _Out_writes_(KDB_TEST_LINE_MAX) PCHAR Buffer,
    _In_ PCSTR Format,
    ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, KDB_TEST_LINE_MAX - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[KDB_TEST_LINE_MAX - 1] = ANSI_NULL;
}

/*
 * One command and what it has to say. Expected is text that must appear in
 * what it prints, Unexpected text that must not; either may be NULL. A
 * command never stays silent and never lets the machine run here.
 */
static
BOOLEAN
KdbpTestCommand(
    _In_ PCSTR Command,
    _In_opt_ PCSTR Expected,
    _In_opt_ PCSTR Unexpected)
{
    BOOLEAN Resumed = FALSE;
    PCSTR Output = KdbpTestRun(Command, &Resumed);
    BOOLEAN Passed;

    Passed = !Resumed &&
             (Output[0] != ANSI_NULL) &&
             ((Expected == NULL) || (strstr(Output, Expected) != NULL)) &&
             ((Unexpected == NULL) || (strstr(Output, Unexpected) == NULL));

    if (Resumed)
        KdbpClearResume();

    KdbpTestCheck(Passed,
                  "\"%s\" %s%s%s%s%s",
                  KdbpTestCommandText,
                  Resumed ? "let the machine run; " : "",
                  (Output[0] == ANSI_NULL) ? "printed nothing; " : "",
                  (Expected != NULL) ? "should print \"" : "",
                  (Expected != NULL) ? Expected : "",
                  (Expected != NULL) ? "\"" : "");

    if (!Passed)
    {
        if (Unexpected != NULL)
            KdbpPrint("  and should not print \"%s\"\n", Unexpected);
        KdbpTestShowOutput();
    }

    return Passed;
}

static
ULONG
KdbpTestCount(
    _In_ PCSTR Text,
    _In_ PCSTR What)
{
    ULONG Count = 0;
    SIZE_T Length = strlen(What);

    while ((Text = strstr(Text, What)) != NULL)
    {
        Count++;
        Text += Length;
    }

    return Count;
}

static
VOID
KdbpTestExpression(
    _In_ PCSTR Expression,
    _In_ BOOLEAN Valid,
    _In_ ULONGLONG Expected)
{
    CHAR Copy[KDB_TEST_LINE_MAX];
    CHAR ErrorMessage[128] = "";
    LONG ErrorOffset = 0;
    ULONGLONG Result = 0;
    BOOLEAN Evaluated;

    RtlStringCbCopyA(Copy, sizeof(Copy), Expression);
    Evaluated = KdbpRpnEvaluateExpression(Copy, KdbCurrentTrapFrame, &Result, &ErrorOffset, ErrorMessage);

    if (Valid)
    {
        KdbpTestCheck(Evaluated && (Result == Expected),
                      "expression \"%.64s\" should be 0x%I64x, is %s0x%I64x %s",
                      Expression, Expected, Evaluated ? "" : "refused ", Result, ErrorMessage);
    }
    else
    {
        KdbpTestCheck(!Evaluated && (ErrorMessage[0] != ANSI_NULL),
                      "expression \"%.64s\" should be refused with a reason, came to 0x%I64x",
                      Expression, Result);
    }
}

/*
 * Puts back what a group may have changed: the registers of the stop, which
 * thread and process are looked at, steps asked for and breakpoints added.
 */
static
VOID
KdbpTestRestore(VOID)
{
    LONG Index;

    if (KdbCurrentTrapFrame != NULL)
        *KdbCurrentTrapFrame = KdbpTestSavedContext;

    KdbCurrentThread = KdbpTestSavedThread;
    KdbCurrentProcess = KdbpTestSavedProcess;
    KdbNumSingleSteps = 0;
    KdbSingleStepOver = FALSE;
    KdbpResetView();
    KdbpClearResume();

    for (Index = KdbpGetNextBreakPointNr(0); Index >= 0; Index = KdbpGetNextBreakPointNr((ULONG)Index + 1))
    {
        if ((Index < KDB_TEST_MAX_BREAKPOINTS) && !KdbpTestBreakPointUsed[Index])
            KdbpDeleteBreakPoint(Index, NULL);
    }

    KdbLastBreakPointNr = KdbpTestSavedLastBreakPoint;
}

static
VOID
KdbpTestSave(VOID)
{
    LONG Index;

    if (KdbCurrentTrapFrame != NULL)
        KdbpTestSavedContext = *KdbCurrentTrapFrame;

    KdbpTestSavedThread = KdbCurrentThread;
    KdbpTestSavedProcess = KdbCurrentProcess;
    KdbpTestSavedLastBreakPoint = KdbLastBreakPointNr;

    RtlZeroMemory(KdbpTestBreakPointUsed, sizeof(KdbpTestBreakPointUsed));
    for (Index = KdbpGetNextBreakPointNr(0); Index >= 0; Index = KdbpGetNextBreakPointNr((ULONG)Index + 1))
    {
        if (Index < KDB_TEST_MAX_BREAKPOINTS)
            KdbpTestBreakPointUsed[Index] = TRUE;
    }
}

static
VOID
KdbpTestPrepareMemory(VOID)
{
    static const CHAR Text[] = "LiberNT";
    ULONG Index;

    for (Index = 0; Index < sizeof(KdbpTestBytes); Index++)
        KdbpTestBytes[Index] = (UCHAR)(0x10 + Index);

    KdbpTestBytes[2] = 0xA5;
    KdbpTestBytes[3] = 0x5A;
    RtlCopyMemory(&KdbpTestBytes[32], Text, sizeof(Text));
    RtlZeroMemory(KdbpTestCopy, sizeof(KdbpTestCopy));

    KdbpTestPointers[0] = (PVOID)KdbpCmdSelfTest;
    KdbpTestPointers[1] = KdbpTestWide;
}

/* THE GROUPS *****************************************************************/

static
VOID
KdbpTestParsing(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    ULONG_PTR Number = 0;
    ULONG Index;

    KdbpTestCheck(KdbpGetHexNumber("1234", &Number) && (Number == 0x1234), "hex 1234");
    KdbpTestCheck(KdbpGetHexNumber("0xfeed", &Number) && (Number == 0xFEED), "hex 0xfeed");
    KdbpTestCheck(KdbpGetHexNumber(" 0Xa ", &Number) && (Number == 0xA), "hex with spaces around it");
    KdbpTestCheck(!KdbpGetHexNumber("1234x", &Number), "hex 1234x refused");
    KdbpTestCheck(!KdbpGetHexNumber("", &Number), "an empty hex number refused");
    KdbpTestCheck(!KdbpGetHexNumber("0x", &Number), "hex 0x with no digits refused");
    KdbpTestCheck(!KdbpGetHexNumber("-1", &Number), "hex -1 refused");
    KdbpTestCheck(!KdbpGetHexNumber("1000000000000000000", &Number), "a hex number past 64 bits refused");
    KdbpTestCheck(KdbpCheckCommandTable() == 0, "the command table has no duplicate or incomplete entries");

    KdbpTestCommand("echo   KDBTEST-A \t  KDBTEST-B", "KDBTEST-A KDBTEST-B\n", NULL);

    /* More words than a command line holds is said, not cut short in silence */
    RtlStringCbCopyA(Line, sizeof(Line), "echo");
    for (Index = 0; Index < 40; Index++)
        RtlStringCbCatA(Line, sizeof(Line), " w");
    KdbpTestCommand(Line, "words", NULL);
}

static
VOID
KdbpTestExpressions(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    ULONGLONG Register = 0;

    KdbpTestExpression("1+2*3", TRUE, 7);
    KdbpTestExpression("(1+2)*3", TRUE, 9);
    KdbpTestExpression("5 - 3", TRUE, 2);
    KdbpTestExpression("10 / 3", TRUE, 3);
    KdbpTestExpression("10 % 3", TRUE, 1);
    KdbpTestExpression("1 << 4", TRUE, 16);
    KdbpTestExpression("0x100 >> 4", TRUE, 16);
    KdbpTestExpression("5 < 6", TRUE, 1);
    KdbpTestExpression("5 > 6", TRUE, 0);
    KdbpTestExpression("5 <= 5", TRUE, 1);
    KdbpTestExpression("5 >= 6", TRUE, 0);
    KdbpTestExpression("5 == 5", TRUE, 1);
    KdbpTestExpression("5 != 5", TRUE, 0);
    KdbpTestExpression("5!=4", TRUE, 1);
    KdbpTestExpression("5 !=4", TRUE, 1);
    KdbpTestExpression("6 & 3", TRUE, 2);
    KdbpTestExpression("6 | 3", TRUE, 7);
    KdbpTestExpression("6 ^ 3", TRUE, 5);
    KdbpTestExpression("1 && 0", TRUE, 0);
    KdbpTestExpression("1 || 0", TRUE, 1);
    KdbpTestExpression("!0", TRUE, 1);
    KdbpTestExpression("~0", TRUE, ~0ULL);
    KdbpTestExpression("-1", TRUE, ~0ULL);
    KdbpTestExpression("0n10", TRUE, 10);
    KdbpTestExpression("0b101", TRUE, 5);
    KdbpTestExpression("0x10", TRUE, 16);
    KdbpTestExpression("0xffffffffffffffff", TRUE, ~0ULL);
    KdbpTestExpression("0x10000000000000000", FALSE, 0);
    KdbpTestExpression("1/0", FALSE, 0);
    KdbpTestExpression("1%0", FALSE, 0);
    KdbpTestExpression("1 +", FALSE, 0);
    KdbpTestExpression("(1", FALSE, 0);
    KdbpTestExpression("1)", FALSE, 0);
    KdbpTestExpression("0x", FALSE, 0);
    KdbpTestExpression("kdbtest_no_such_symbol", FALSE, 0);
    KdbpTestExpression("@kdbtestreg", FALSE, 0);
    KdbpTestExpression("*1", FALSE, 0);

    if (KdbCurrentTrapFrame != NULL)
    {
        Register = (ULONGLONG)KeGetContextStackRegister(KdbCurrentTrapFrame);
        KdbpTestExpression("sp", TRUE, Register);
        KdbpTestExpression("@sp", TRUE, Register);
        KdbpTestExpression("@SP + 8", TRUE, Register + 8);
        KdbpTestExpression("sp!=0", TRUE, Register != 0);
        KdbpTestExpression("@sp==sp", TRUE, 1);
        KdbpTestExpression("pc", TRUE, (ULONGLONG)KeGetContextPc(KdbCurrentTrapFrame));
    }

    KdbpTestFormat(Line, "*0x%p", &KdbpTestPointers[0]);
    KdbpTestExpression(Line, TRUE, (ULONGLONG)(ULONG_PTR)KdbpTestPointers[0]);

    KdbpTestExpression("KdbpCmdSelfTest", TRUE, (ULONGLONG)(ULONG_PTR)KdbpCmdSelfTest);
    KdbpTestExpression("ntoskrnl.exe!KdbpCmdSelfTest", TRUE, (ULONGLONG)(ULONG_PTR)KdbpCmdSelfTest);
    KdbpTestExpression("ntoskrnl!KdbpCmdSelfTest", TRUE, (ULONGLONG)(ULONG_PTR)KdbpCmdSelfTest);
    KdbpTestExpression("nt!KdbpCmdSelfTest", TRUE, (ULONGLONG)(ULONG_PTR)KdbpCmdSelfTest);
    KdbpTestExpression("KdbpCmdSelfTest+0x10-0x10", TRUE, (ULONGLONG)(ULONG_PTR)KdbpCmdSelfTest);

    /* An expression is all the words after the command, not just the first */
    KdbpTestCommand("? 5 - 3", "2 (0x2)", NULL);
    KdbpTestCommand("? (1 + 2) * 3", "9 (0x9)", NULL);
    KdbpTestCommand("?", "needed", NULL);
    KdbpTestCommand("? 1 +", "offset", NULL);
}

static
VOID
KdbpTestSymbols(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];

    KdbpTestFormat(Line, "ln 0x%p", (PVOID)KdbpCmdSelfTest);
    KdbpTestCommand(Line, "KdbpCmdSelfTest", NULL);
    KdbpTestCommand("ln 1", "is in no loaded image", NULL);
    KdbpTestCommand("sym ntoskrnl.exe KdbpCmdSelfTest", "1 symbol\n", NULL);
    KdbpTestCommand("sym nt KdbpCmdSelfTes?", "KdbpCmdSelfTest", NULL);
    KdbpTestCommand("sym ntoskrnl KdbpCmdSelf*", "KdbpCmdSelfTest", NULL);
    KdbpTestCommand("sym kdbtest_no_such_symbol_*", "0 symbols", NULL);
    KdbpTestCommand("sym", "needed", NULL);
    KdbpTestCommand("mod", "ntoskrnl.exe", NULL);
}

static
VOID
KdbpTestMemory(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    PUCHAR Bytes = KdbpTestBytes;

    KdbpTestPrepareMemory();

    KdbpTestFormat(Line, "db 0x%p 4", Bytes);
    KdbpTestCommand(Line, "10 11 a5 5a", NULL);
    KdbpTestFormat(Line, "dw 0x%p 2", Bytes);
    KdbpTestCommand(Line, "1110 5aa5", NULL);
    KdbpTestFormat(Line, "dd 0x%p 1", Bytes);
    KdbpTestCommand(Line, "5aa51110", NULL);
    KdbpTestFormat(Line, "dq 0x%p 1", Bytes);
    KdbpTestCommand(Line, "171615145aa51110", NULL);
    KdbpTestFormat(Line, "da 0x%p", &Bytes[32]);
    KdbpTestCommand(Line, "\"LiberNT\"", NULL);
    KdbpTestFormat(Line, "da 0x%p 3", &Bytes[32]);
    KdbpTestCommand(Line, "\"Lib\"", "LiberNT");
    KdbpTestFormat(Line, "du 0x%p", KdbpTestWide);
    KdbpTestCommand(Line, "\"LiberNT\"", NULL);
    KdbpTestFormat(Line, "dps 0x%p 1", KdbpTestPointers);
    KdbpTestCommand(Line, "KdbpCmdSelfTest", NULL);
    KdbpTestCommand("db 1", "??", NULL);

    KdbpTestFormat(Line, "search 0x%p 0n64 a5 5a", Bytes);
    KdbpTestCommand(Line, "1 matches", NULL);
    KdbpTestFormat(Line, "search 0x%p 0n64 zz", Bytes);
    KdbpTestCommand(Line, "not a byte", NULL);

    KdbpTestFormat(Line, "eb 0x%p 1 2 3", &Bytes[48]);
    KdbpTestRun(Line, NULL);
    KdbpTestCheck((Bytes[48] == 1) && (Bytes[49] == 2) && (Bytes[50] == 3), "eb wrote 1 2 3");
    KdbpTestFormat(Line, "ew 0x%p 0x1234", &Bytes[52]);
    KdbpTestRun(Line, NULL);
    KdbpTestCheck(*(PUSHORT)&Bytes[52] == 0x1234, "ew wrote 0x1234");
    KdbpTestFormat(Line, "ed 0x%p 0x89abcdef", &Bytes[56]);
    KdbpTestRun(Line, NULL);
    KdbpTestCheck(*(PULONG)&Bytes[56] == 0x89ABCDEF, "ed wrote 0x89abcdef");
    KdbpTestFormat(Line, "eq 0x%p 0x1122334455667788", &Bytes[56]);
    KdbpTestRun(Line, NULL);
    KdbpTestCheck(*(PULONG64)&Bytes[56] == 0x1122334455667788ULL, "eq wrote 0x1122334455667788");
    KdbpTestCommand("eb 1 1", "cannot be written", NULL);
    KdbpTestFormat(Line, "eb 0x%p", Bytes);
    KdbpTestCommand(Line, "needed", NULL);

    KdbpTestFormat(Line, "fill 0x%p 4 ee ff", &Bytes[40]);
    KdbpTestCommand(Line, "bytes filled", NULL);
    KdbpTestCheck((Bytes[40] == 0xEE) && (Bytes[41] == 0xFF) && (Bytes[42] == 0xEE) && (Bytes[43] == 0xFF) &&
                  (Bytes[44] == 0x10 + 44),
                  "fill wrote the pattern and no more");

    KdbpTestFormat(Line, "move 0x%p 0x%p 8", Bytes, KdbpTestCopy);
    KdbpTestCommand(Line, "bytes moved", NULL);
    KdbpTestCheck((RtlCompareMemory(KdbpTestCopy, Bytes, 8) == 8) && (KdbpTestCopy[8] == 0), "move copied 8 bytes");
    KdbpTestFormat(Line, "compare 0x%p 0x%p 8", Bytes, KdbpTestCopy);
    KdbpTestCommand(Line, "0 differences", NULL);
    KdbpTestFormat(Line, "compare 0x%p 0x%p 9", Bytes, KdbpTestCopy);
    KdbpTestCommand(Line, "1 differences", NULL);

    /* Overlapping ranges are copied as if through a buffer */
    KdbpTestPrepareMemory();
    KdbpTestFormat(Line, "move 0x%p 0x%p 8", Bytes, &Bytes[4]);
    KdbpTestRun(Line, NULL);
    KdbpTestCheck((Bytes[4] == 0x10) && (Bytes[5] == 0x11) && (Bytes[6] == 0xA5) && (Bytes[11] == 0x17),
                  "move to an overlapping range above the source");
    KdbpTestPrepareMemory();
}

static
VOID
KdbpTestBreakPoints(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    KD_BREAKPOINT_TYPE Instruction = KD_BREAKPOINT_VALUE;
    PUCHAR Target = &KdbpTestBytes[60 - (60 % sizeof(KD_BREAKPOINT_TYPE))];
    KD_BREAKPOINT_TYPE Original;
    PCHAR Condition = NULL;
    BOOLEAN Global = TRUE;
    PEPROCESS Process = NULL;
    LONG Number;
    PPEB Peb;

    KdbpTestPrepareMemory();
    RtlCopyMemory(&Original, Target, sizeof(Original));

    KdbpTestFormat(Line, "bp 0x%p", Target);
    KdbpTestCommand(Line, "Breakpoint", NULL);
    Number = KdbpGetNextBreakPointNr(0);
    while ((Number >= 0) && (Number < KDB_TEST_MAX_BREAKPOINTS) && KdbpTestBreakPointUsed[Number])
        Number = KdbpGetNextBreakPointNr((ULONG)Number + 1);

    KdbpTestCheck(Number >= 0, "bp took a breakpoint");
    KdbpTestCheck(RtlCompareMemory(Target, &Instruction, sizeof(Instruction)) == sizeof(Instruction),
                  "bp wrote the breakpoint instruction");
    KdbpTestCommand("bl", "enabled", NULL);
    KdbpTestCommand(Line, "already", NULL);

    if (Number >= 0)
    {
        KdbpTestFormat(Line, "bd %lx", Number);
        KdbpTestRun(Line, NULL);
        KdbpTestCheck(RtlCompareMemory(Target, &Original, sizeof(Original)) == sizeof(Original),
                      "bd put the instruction back");
        KdbpTestCommand("bl", "disabled", NULL);
        KdbpTestFormat(Line, "be %lx", Number);
        KdbpTestRun(Line, NULL);
        KdbpTestCheck(RtlCompareMemory(Target, &Instruction, sizeof(Instruction)) == sizeof(Instruction),
                      "be wrote the breakpoint again");
        KdbpTestFormat(Line, "bc %lx", Number);
        KdbpTestRun(Line, NULL);
        KdbpTestCheck(RtlCompareMemory(Target, &Original, sizeof(Original)) == sizeof(Original),
                      "bc put the instruction back");
        KdbpTestCheck(!KdbpGetBreakPointInfo((ULONG)Number, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
                      "bc gave the breakpoint up");
    }

    KdbpTestFormat(Line, "bp 0x%p kdbtest_zz", Target);
    KdbpTestCommand(Line, "is not if or do", "Breakpoint ");
    KdbpTestFormat(Line, "bp 0x%p if", Target);
    KdbpTestCommand(Line, "condition is needed", "Breakpoint ");
    KdbpTestRestore();
    KdbpTestCommand("bc 0x7fffffff", "no breakpoint", NULL);
    KdbpTestCommand("bc zz", "not a breakpoint number", NULL);

    /* A condition is the whole expression after if, register names included */
    KdbpTestFormat(Line, "bp 0x%p if 1 == 1", Target);
    KdbpTestRun(Line, NULL);
    KdbpTestCommand("bl", "if 1 == 1", NULL);
    KdbpTestRestore();
    KdbpTestFormat(Line, "bp 0x%p if @sp != 0", Target);
    KdbpTestCommand(Line, "Breakpoint", "not set");
    KdbpTestRestore();
    KdbpTestFormat(Line, "bp 0x%p if (1", Target);
    KdbpTestCommand(Line, NULL, "Breakpoint ");
    KdbpTestRestore();
    KdbpTestCheck(RtlCompareMemory(Target, &Original, sizeof(Original)) == sizeof(Original),
                  "the test breakpoints were all taken back");

    /* Watching memory: only the widths the processor has, aligned to themselves */
    KdbpTestFormat(Line, "bpm w 0x%p 0x104", &KdbpTestBytes[8]);
    KdbpTestCommand(Line, NULL, "watching");
    KdbpTestRestore();
    KdbpTestFormat(Line, "bpm w 0x%p 3", &KdbpTestBytes[8]);
    KdbpTestCommand(Line, NULL, "watching");
    KdbpTestRestore();
    KdbpTestFormat(Line, "bpm q 0x%p", &KdbpTestBytes[8]);
    KdbpTestCommand(Line, "access type", NULL);
    if (KdbpArchHardwareBreakPointCount() != 0)
    {
        KdbpTestFormat(Line, "bpm w 0x%p 4", &KdbpTestBytes[9]);
        KdbpTestCommand(Line, "aligned", "watching");
        KdbpTestRestore();
        KdbpTestFormat(Line, "bpm w 0x%p 4", &KdbpTestBytes[8]);
        KdbpTestCommand(Line, "watching", NULL);
        KdbpTestCommand("bl", "memory", NULL);
        KdbpTestRestore();
    }

    /* A breakpoint on a process's address belongs to that process */
    Peb = (KdbCurrentProcess != NULL) ? KdbCurrentProcess->Peb : NULL;
    if (Peb != NULL)
    {
        KdbpTestFormat(Line, "bp 0x%p", (PUCHAR)Peb + 0x40);
        KdbpTestRun(Line, NULL);
        Number = KdbpGetNextBreakPointNr(0);
        while ((Number >= 0) && (Number < KDB_TEST_MAX_BREAKPOINTS) && KdbpTestBreakPointUsed[Number])
            Number = KdbpGetNextBreakPointNr((ULONG)Number + 1);

        KdbpTestCheck((Number >= 0) &&
                      KdbpGetBreakPointInfo((ULONG)Number, NULL, NULL, NULL, NULL, NULL, NULL, &Global, &Process, &Condition) &&
                      !Global && (Process == KdbCurrentProcess),
                      "a breakpoint on a user address belongs to its process");
        KdbpTestRestore();
    }

    /* Typos are refused rather than taken for something else */
    KdbpTestCommand("cond * frist always", "first or last", NULL);
    KdbpTestCommand("filter 0x7fffffff 0 maybe", "on or off", NULL);
    KdbpTestCommand("step 0x100000000", NULL, NULL);
    KdbpTestRestore();
}

/* The newest breakpoint a check added, or -1 */
static
LONG
KdbpTestNewBreakPoint(VOID)
{
    LONG Number;
    LONG Newest = -1;

    for (Number = KdbpGetNextBreakPointNr(0); Number >= 0; Number = KdbpGetNextBreakPointNr((ULONG)Number + 1))
    {
        if ((Number < KDB_TEST_MAX_BREAKPOINTS) && !KdbpTestBreakPointUsed[Number])
            Newest = Number;
    }

    return Newest;
}

static
VOID
KdbpTestStepping(VOID)
{
    CONTEXT Expected;
    BOOLEAN Resumed = FALSE;
    BOOLEAN CanStep;
    ULONG_PTR Address = 0;
    ULONG Index;
    LONG Number;
    LONG Length;

    if (KdbCurrentTrapFrame == NULL)
        return;

    for (Index = 0; Index < KdbpArchStepSampleCount; Index++)
    {
        KdbpTestCheck(KdbpArchIsStepOverInstruction((ULONG_PTR)KdbpArchStepSamples[Index].Bytes) ==
                      KdbpArchStepSamples[Index].StepOver,
                      "p %s %s",
                      KdbpArchStepSamples[Index].StepOver ? "steps over" : "steps into",
                      KdbpArchStepSamples[Index].Name);
    }

    Expected = KdbpTestSavedContext;
    CanStep = NT_SUCCESS(KdbpArchSetSingleStep(&Expected, TRUE));
    if (!CanStep)
        KdbpTestCommand("t", "not supported", NULL);

    if (CanStep)
    {
        /* A step is taken by the thread that stopped, whatever frame is looked at */
        KdbpTestRun(".frame 1", NULL);
        KdbpTestRun("t", &Resumed);
        KdbpTestCheck(Resumed && (KdbNumSingleSteps == 1), "t asked for one step");
        KdbpTestCheck(RtlCompareMemory(KdbCurrentTrapFrame, &Expected, sizeof(Expected)) == sizeof(Expected),
                      "t after .frame 1 stepped the stopped thread");
        KdbpTestRestore();

        KdbpTestRun("t 3", &Resumed);
        KdbpTestCheck(Resumed && (KdbNumSingleSteps == 3), "t 3 asked for three steps");
        KdbpTestRestore();
    }

    /* p over anything but a call is a step; over a call, a run to the instruction after it */
    for (Index = 0; Index < KdbpArchStepSampleCount; Index++)
    {
        const KDB_STEP_SAMPLE *Sample = &KdbpArchStepSamples[Index];

        RtlCopyMemory(KdbpTestBytes, Sample->Bytes, sizeof(Sample->Bytes));
        Length = KdbpGetInstLength((ULONG_PTR)KdbpTestBytes);
        KeSetContextPc(KdbCurrentTrapFrame, (ULONG_PTR)KdbpTestBytes);
        KdbpTestRun("p", &Resumed);
        Number = KdbpTestNewBreakPoint();

        if (Sample->StepOver)
        {
            KdbpTestCheck(Resumed && (KdbNumSingleSteps == 0) && (Number >= 0) && (Length == Sample->Length) &&
                          KdbpGetBreakPointInfo((ULONG)Number, &Address, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&
                          (Address == (ULONG_PTR)KdbpTestBytes + Sample->Length),
                          "p at %s runs to the next instruction", Sample->Name);
        }
        else if (CanStep)
        {
            KdbpTestCheck(Resumed && (KdbNumSingleSteps == 1) && (Number < 0),
                          "p at %s steps it", Sample->Name);
        }
        else
        {
            KdbpTestCheck(!Resumed && (Number < 0), "p at %s says it cannot step", Sample->Name);
        }

        KdbpTestRestore();
    }

    KdbpTestPrepareMemory();
}

static
VOID
KdbpTestRegisters(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    CHAR Expected[64];
    ULONGLONG Value = 0;

    if (KdbCurrentTrapFrame == NULL)
        return;

    RtlStringCbPrintfA(Expected, sizeof(Expected), "pc = %016I64x", (ULONGLONG)KeGetContextPc(KdbCurrentTrapFrame));
    KdbpTestCommand("r pc", Expected, NULL);
    KdbpTestCommand("r kdbtestreg", "no register", NULL);
    KdbpTestCommand("regs", NULL, NULL);
    KdbpTestCommand("fpregs kdbtestreg", "no floating-point", NULL);

    KdbpTestFormat(Line, "r %s 0x1234", KdbpRegisters[0].Name);
    KdbpTestRun(Line, NULL);
    KdbpTestCheck(NT_SUCCESS(KdbpGetRegisterValue(KdbCurrentTrapFrame, KdbpRegisters[0].Name, &Value, NULL)) &&
                  (Value == 0x1234),
                  "r %s 0x1234 changed the register", KdbpRegisters[0].Name);
    KdbpTestFormat(Line, "r %s 0x10 + 0x20", KdbpRegisters[0].Name);
    KdbpTestRun(Line, NULL);
    KdbpTestCheck(NT_SUCCESS(KdbpGetRegisterValue(KdbCurrentTrapFrame, KdbpRegisters[0].Name, &Value, NULL)) &&
                  (Value == 0x30),
                  "r %s 0x10 + 0x20 took the whole expression", KdbpRegisters[0].Name);
    KdbpTestRestore();

    KdbpTestCheck(KdbpFloatingRegisterCount != 0, "there are floating-point registers to show");
}

static
VOID
KdbpTestScripts(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    PCSTR Output;

    KdbpTestCommand("alias kdbtestalias echo KDBTEST-ALIASED", "kdbtestalias = echo KDBTEST-ALIASED", NULL);
    KdbpTestCommand("kdbtestalias more", "KDBTEST-ALIASED more", NULL);
    KdbpTestCommand("alias kdbtestalias", "kdbtestalias = ", NULL);
    KdbpTestCommand("unalias kdbtestalias", "no longer", NULL);
    KdbpTestCommand("kdbtestalias", "not something that can be typed", NULL);
    KdbpTestCommand("alias help echo x", "cannot be an alias", NULL);
    KdbpTestCommand("alias kdbtestloop kdbtestloop", NULL, NULL);
    KdbpTestCommand("kdbtestloop", "no deeper", NULL);
    KdbpTestRun("unalias kdbtestloop", NULL);

    Output = KdbpTestRun("repeat 3 echo KDBTEST-R", NULL);
    KdbpTestCheck(KdbpTestCount(Output, "KDBTEST-R\n") == 3, "repeat 3 ran the command three times");
    KdbpTestCommand("repeat 0 echo x", "from 1 to", NULL);

    KdbpTestFormat(Line, "script 0x%p 0x%Ix", KdbpTestScriptText, strlen(KdbpTestScriptText));
    Output = KdbpTestRun(Line, NULL);
    KdbpTestCheck((strstr(Output, "KDBTEST-S1\n") != NULL) && (strstr(Output, "KDBTEST-S2\n") != NULL),
                  "script ran both lines");
    KdbpTestCommand("script 1 4", "cannot be read", NULL);

    KdbpTestCommand("version", "LiberNT", NULL);
    KdbpTestCommand("log", "transcript", NULL);
    KdbpTestCommand("log zz", "not on, off, clear or show", NULL);
    KdbpTestCommand("kdbtest_no_such_command", "not something that can be typed", NULL);
}

static
VOID
KdbpTestDisassembler(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];

    KdbpTestCheck(KdbpDisassemblerSelfTest(), "the disassembler decodes its own cases");
    KdbpTestFormat(Line, "u 0x%p 2", (PVOID)KdbpCmdSelfTest);
    KdbpTestCommand(Line, "KdbpCmdSelfTest", NULL);
}

static
BOOLEAN
NTAPI
KdbpTestCountLink(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Link);

    (*(PULONG)Context)++;
    return TRUE;
}

static
BOOLEAN
NTAPI
KdbpTestStopAtFirst(
    _In_ PLIST_ENTRY Link,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Link);
    UNREFERENCED_PARAMETER(Context);

    return FALSE;
}

static
VOID
KdbpTestBuildList(VOID)
{
    ULONG Index;

    InitializeListHead(&KdbpTestListHead);
    for (Index = 0; Index < RTL_NUMBER_OF(KdbpTestListItems); Index++)
        InsertTailList(&KdbpTestListHead, &KdbpTestListItems[Index]);
}

/* A list walk ends at its head, at a link it cannot read, or after as many entries as it was allowed */
static
VOID
KdbpTestWalks(VOID)
{
    KDB_WALK_RESULT Result;
    PLIST_ENTRY Broken = NULL;
    ULONG Visited = 0;
    ULONG Count = 0;

    KdbpTestBuildList();
    Result = KdbpWalkList(&KdbpTestListHead, 16, KdbpTestCountLink, &Count, &Visited, &Broken);
    KdbpTestCheck((Result == KdbWalkComplete) && (Visited == 3) && (Count == 3) && (Broken == NULL),
                  "a list of three is walked to its head (result %u, %lu visited)", Result, Visited);

    KdbpTestListItems[1].Flink = (PLIST_ENTRY)(ULONG_PTR)1;
    Count = 0;
    Result = KdbpWalkList(&KdbpTestListHead, 16, KdbpTestCountLink, &Count, &Visited, &Broken);
    KdbpTestCheck((Result == KdbWalkUnreadable) && (Visited == 2) && (Broken == (PLIST_ENTRY)(ULONG_PTR)1),
                  "a link that cannot be read ends the walk (result %u, %lu visited)", Result, Visited);

    KdbpTestBuildList();
    KdbpTestListItems[2].Flink = &KdbpTestListItems[1];
    Count = 0;
    Result = KdbpWalkList(&KdbpTestListHead, 16, KdbpTestCountLink, &Count, &Visited, &Broken);
    KdbpTestCheck((Result == KdbWalkTooLong) && (Visited == 16),
                  "a loop that misses the head ends at the limit (result %u, %lu visited)", Result, Visited);

    KdbpTestBuildList();
    Result = KdbpWalkList(&KdbpTestListHead, 16, KdbpTestStopAtFirst, NULL, &Visited, &Broken);
    KdbpTestCheck((Result == KdbWalkStopped) && (Visited == 1),
                  "the walk stops when its routine says so (result %u, %lu visited)", Result, Visited);

    Result = KdbpWalkList((PLIST_ENTRY)(ULONG_PTR)1, 16, KdbpTestCountLink, &Count, &Visited, &Broken);
    KdbpTestCheck((Result == KdbWalkUnreadable) && (Visited == 0), "a head that cannot be read is said so");

    KdbpTestCommand("proc", "System", "cannot be read");
    KdbpTestCommand("thread", "state", "cannot be read");
    KdbpTestCommand("bt all 1", "Thread ", "The list");
    KdbpTestCommand("!timer", "timers queued", "The list");
    KdbpTestCommand("!ready", "Ready queues", "The list");
}

/* Line separators, quoted words, the memory functions and the names of the stop */
static
VOID
KdbpTestLanguage(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    PCSTR Output;
    static CONTEXT Caller;

    Output = KdbpTestRun("echo KDBTEST-1; kdbtest_no_such_command; echo KDBTEST-3", NULL);
    KdbpTestCheck((strstr(Output, "KDBTEST-1\n") != NULL) && (strstr(Output, "KDBTEST-3\n") != NULL),
                  "; runs each statement of a line, past one that fails");
    KdbpTestCommand("echo \"KDBTEST-A  ;  B\" C", "KDBTEST-A  ;  B C\n", NULL);
    KdbpTestCommand("echo KDBTEST-\"Q R\"", "KDBTEST-Q R\n", NULL);

    KdbpTestPrepareMemory();
    KdbpTestFormat(Line, "poi(0x%p)", &KdbpTestPointers[0]);
    KdbpTestExpression(Line, TRUE, (ULONGLONG)(ULONG_PTR)KdbpTestPointers[0]);
    KdbpTestFormat(Line, "by(0x%p)", KdbpTestBytes);
    KdbpTestExpression(Line, TRUE, 0x10);
    KdbpTestFormat(Line, "wo(0x%p)", KdbpTestBytes);
    KdbpTestExpression(Line, TRUE, 0x1110);
    KdbpTestFormat(Line, "dwo(0x%p) + 1", KdbpTestBytes);
    KdbpTestExpression(Line, TRUE, 0x5AA51111);
    KdbpTestFormat(Line, "qwo(0x%p)", KdbpTestBytes);
    KdbpTestExpression(Line, TRUE, 0x171615145AA51110ULL);
    KdbpTestFormat(Line, "by(by(0x%p) - 0x10 + 0x%p)", KdbpTestBytes, KdbpTestBytes);
    KdbpTestExpression(Line, TRUE, 0x10);
    KdbpTestExpression("poi 1", FALSE, 0);
    KdbpTestExpression("poi(1)", FALSE, 0);
    KdbpTestExpression("poi(", FALSE, 0);

    if (KdbCurrentTrapFrame != NULL)
    {
        KdbpTestExpression("$ip == pc", TRUE, 1);
        KdbpTestExpression("$pc == $ip", TRUE, 1);
        KdbpTestExpression("$sp == sp", TRUE, 1);
        Caller = *KdbCurrentTrapFrame;
        if (KiUnwindFrame(&Caller, FALSE))
            KdbpTestExpression("$ra", TRUE, (ULONGLONG)KeGetContextPc(&Caller));
    }

    KdbpTestExpression("$thread", TRUE, (ULONGLONG)(ULONG_PTR)KdbCurrentThread);
    KdbpTestExpression("$proc", TRUE, (ULONGLONG)(ULONG_PTR)KdbCurrentProcess);
    KdbpTestExpression("$cpu", TRUE, KeGetCurrentProcessorNumber());
    KdbpTestExpression("$prcb", TRUE, (ULONGLONG)(ULONG_PTR)KeGetCurrentPrcb());
    if (KdbCurrentThread != NULL)
        KdbpTestExpression("$tid", TRUE, (ULONGLONG)(ULONG_PTR)KdbCurrentThread->Cid.UniqueThread);
    if (KdbCurrentProcess != NULL)
        KdbpTestExpression("$pid", TRUE, (ULONGLONG)(ULONG_PTR)KdbCurrentProcess->UniqueProcessId);
    KdbpTestExpression("$kdbtest_zz", FALSE, 0);
    KdbpTestRun("? 0x1234", NULL);
    KdbpTestExpression("$exp + 1", TRUE, 0x1235);

    /* A condition may use registers and memory that only mean something where it fires */
    KdbpTestFormat(Line, "bp 0x%p if poi($sp) != 0 && by(pc) == 0xcc && $tid != 0", &KdbpTestBytes[60]);
    KdbpTestCommand(Line, "Breakpoint", "offset");
    KdbpTestRestore();

    KdbpTestCommand(".formats 0x41424344", "Decimal: 1094861636", NULL);
    KdbpTestCommand(".formats 0x41424344", "Chars:   ....ABCD", NULL);
    KdbpTestCommand(".formats 5", "00000101", NULL);
}

/* db and dc show the bytes again as text; physical memory reads the same bytes */
static
VOID
KdbpTestMoreMemory(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    PHYSICAL_ADDRESS Physical;
    PCSTR Output;
    ULONG Index;

    KdbpTestPrepareMemory();
    KdbpTestFormat(Line, "db 0x%p 8", &KdbpTestBytes[32]);
    KdbpTestCommand(Line, " LiberNT.\n", NULL);
    KdbpTestFormat(Line, "dc 0x%p 1", KdbpTestBytes);
    KdbpTestCommand(Line, "5aa51110", NULL);
    KdbpTestFormat(Line, "dc 0x%p 1", &KdbpTestBytes[32]);
    KdbpTestCommand(Line, " Libe\n", NULL);

    KdbpTestFormat(Line, "!vtop 0x%p", KdbpTestBytes);
    KdbpTestCommand(Line, "is physical 0x", NULL);
    Physical = MmGetPhysicalAddress(KdbpTestBytes);
    if (Physical.QuadPart != 0)
    {
        KdbpTestFormat(Line, "!db 0x%I64x 4", Physical.QuadPart);
        KdbpTestCommand(Line, "10 11 a5 5a", NULL);
        KdbpTestFormat(Line, "!dd 0x%I64x 1", Physical.QuadPart);
        KdbpTestCommand(Line, "5aa51110", NULL);
    }
    KdbpTestCommand("!dd", "physical address is needed", NULL);

    KdbpTestBuildList();
    KdbpTestFormat(Line, "!list 0x%p", &KdbpTestListHead);
    KdbpTestCommand(Line, "3 entries\n", NULL);
    KdbpTestFormat(Line, "!list 0x%p 16 ? $entry", &KdbpTestListHead);
    Output = KdbpTestRun(Line, NULL);
    for (Index = 0; Index < RTL_NUMBER_OF(KdbpTestListItems); Index++)
    {
        CHAR Expected[40];

        RtlStringCbPrintfA(Expected, sizeof(Expected), "(0x%Ix)", (ULONG_PTR)&KdbpTestListItems[Index]);
        KdbpTestCheck(strstr(Output, Expected) != NULL, "!list ran ? $entry for entry %lu", Index);
    }
    KdbpTestFormat(Line, "!list 0x%p 2", &KdbpTestListHead);
    KdbpTestCommand(Line, "2 entries shown, and there are more", NULL);
    KdbpTestListItems[1].Flink = (PLIST_ENTRY)(ULONG_PTR)1;
    KdbpTestFormat(Line, "!list 0x%p", &KdbpTestListHead);
    KdbpTestCommand(Line, "cannot be read at 0000000000000001", NULL);
    KdbpTestBuildList();
}

/* bp /1, if, do, ignore and hit counts; display; g, gu and p with a count */
static
VOID
KdbpTestControl(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    PUCHAR Target = &KdbpTestBytes[60 - (60 % sizeof(KD_BREAKPOINT_TYPE))];
    static CONTEXT Caller;
    ULONG_PTR Address = 0;
    PCHAR Condition = NULL;
    BOOLEAN Resumed = FALSE;
    PCSTR Output;
    LONG Number;
    ULONG Display;

    KdbpTestPrepareMemory();

    KdbpTestFormat(Line, "bp /1 0x%p", Target);
    KdbpTestCommand(Line, "Breakpoint", NULL);
    KdbpTestCommand("bl", " once ", NULL);
    KdbpTestRestore();

    KdbpTestFormat(Line, "bp 0x%p if 1 == 1 do \"echo X; echo Y\"", Target);
    KdbpTestCommand(Line, "Breakpoint", NULL);
    KdbpTestCommand("bl", "if 1 == 1 do \"echo X; echo Y\"", NULL);
    KdbpTestCommand("bl", " hits 0", NULL);
    Number = KdbpTestNewBreakPoint();
    KdbpTestFormat(Line, "ignore %lx 3", Number);
    KdbpTestCommand(Line, "lets the next 3 hits pass", NULL);
    KdbpTestCommand("bl", " ignore 3", NULL);
    KdbpTestCommand("ignore 0x7f 1", "no breakpoint", NULL);
    KdbpTestCommand("ignore kdbtest_zz 1", "not a breakpoint number", NULL);
    KdbpTestRestore();

    KdbpTestFormat(Line, "bp 0x%p do", Target);
    KdbpTestCommand(Line, "Commands are needed", "Breakpoint ");
    KdbpTestFormat(Line, "bp 0x%p kdbtest_zz", Target);
    KdbpTestCommand(Line, "is not if or do", "Breakpoint ");
    KdbpTestRestore();

    if (KdbpArchHardwareBreakPointCount() != 0)
    {
        KdbpTestFormat(Line, "bpm w 0x%p 4 if 1 do \"echo W\"", &KdbpTestBytes[8]);
        KdbpTestCommand(Line, "watching", NULL);
        KdbpTestCommand("bl", "if 1 do \"echo W\"", NULL);
        KdbpTestRestore();
    }

    Output = KdbpTestRun("display echo KDBTEST-D", NULL);
    Display = strtoul(Output, NULL, 10);
    KdbpTestCheck(Display != 0, "display took a command");
    KdbpTestCommand("display", "echo KDBTEST-D", NULL);
    KdbpTestFormat(Line, "undisplay %lx", Display);
    KdbpTestCommand(Line, "no longer run", NULL);
    KdbpTestCommand(Line, "is not the number", NULL);
    KdbpTestCommand("undisplay", "is needed", NULL);

    if (KdbCurrentTrapFrame == NULL)
        return;

    /* g <address> runs to it; a mistyped address lets nothing run */
    KdbpTestFormat(Line, "g 0x%p", Target);
    KdbpTestRun(Line, &Resumed);
    Number = KdbpTestNewBreakPoint();
    KdbpTestCheck(Resumed && (Number >= 0) &&
                  KdbpGetBreakPointInfo((ULONG)Number, &Address, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) &&
                  (Address == (ULONG_PTR)Target),
                  "g <address> runs to the address");
    KdbpTestRestore();
    KdbpTestCommand("g kdbtest_zz", "no such", NULL);
    KdbpTestCommand("g 1 2", "more than", NULL);

    /* gu runs to where this function returns, once the stack is back above it */
    Caller = *KdbCurrentTrapFrame;
    if (KiUnwindFrame(&Caller, FALSE))
    {
        KdbpTestRun("gu", &Resumed);
        Number = KdbpTestNewBreakPoint();
        KdbpTestCheck(Resumed && (Number >= 0) &&
                      KdbpGetBreakPointInfo((ULONG)Number, &Address, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &Condition) &&
                      (Address == (ULONG_PTR)KeGetContextPc(&Caller)) &&
                      (Condition != NULL) && (strstr(Condition, "sp >= ") != NULL),
                      "gu runs to the return address with the stack above this frame");
        KdbpTestRestore();
    }

    KdbpTestRun("p 3", &Resumed);
    KdbpTestCheck(Resumed || (KdbNumSingleSteps == 0), "p 3 took its first step");
    KdbpTestRestore();
}

/*
 * Every command, given nothing, garbage, too much or numbers that fit
 * nothing, says something and lets the machine stay where it is.
 */
static
VOID
KdbpTestEveryCommand(VOID)
{
    static const PCSTR Skipped[] =
    {
        "cont", "g", "gu", "handle", "step", "t", "next", "p", "bugcheck", "reboot",
        "!kdpull", "!kdreload", "selftest", "display"
    };
    static const PCSTR Arguments[] =
    {
        "", " kdbtest_zz", " ((", " -1", " 0x1ffffffffffffffff", " @kdbtest_zz @kdbtest_zz @kdbtest_zz", " -1 -1 -1"
    };
    CHAR Line[KDB_TEST_LINE_MAX];
    PCSTR Name;
    ULONG Index;
    ULONG Skip;
    ULONG Argument;

    for (Index = 0; (Name = KdbpGetCommandName(Index)) != NULL; Index++)
    {
        BOOLEAN Excluded = FALSE;

        for (Skip = 0; Skip < RTL_NUMBER_OF(Skipped); Skip++)
        {
            if (_stricmp(Name, Skipped[Skip]) == 0)
                Excluded = TRUE;
        }

        if (Excluded)
            continue;

        for (Argument = 0; Argument < RTL_NUMBER_OF(Arguments); Argument++)
        {
            KdbpTestFormat(Line, "%s%s", Name, Arguments[Argument]);
            KdbpTestCommand(Line, NULL, NULL);
            KdbpTestRestore();
        }
    }

    KdbpTestRun("unalias @kdbtest_zz", NULL);
    KdbpTestRun("unalias -1", NULL);
}

/*
 * The cases that used to take the machine down or never come back: kept
 * last, so whatever ran before them is reported if one still does.
 */
static
VOID
KdbpTestHazards(VOID)
{
    CHAR Line[KDB_TEST_LINE_MAX];
    ULONG Index;

    RtlStringCbCopyA(Line, sizeof(Line), "? ");
    for (Index = 0; Index < 400; Index++)
        RtlStringCbCatA(Line, sizeof(Line), "(");
    RtlStringCbCatA(Line, sizeof(Line), "1");
    KdbpTestCommand(Line, "nested", NULL);

    RtlStringCbCopyA(Line, sizeof(Line), "? ");
    for (Index = 0; Index < 400; Index++)
        RtlStringCbCatA(Line, sizeof(Line), "-");
    RtlStringCbCatA(Line, sizeof(Line), "1");
    KdbpTestCommand(Line, "nested", NULL);

    KdbpTestFormat(Line, "search 0x%p 0xffffffffffffffff 1", KdbpTestBytes);
    KdbpTestCommand(Line, "at most", NULL);
    KdbpTestFormat(Line, "db 0x%p 0x10000000", KdbpTestBytes);
    KdbpTestCommand(Line, "at most", NULL);
}

static const KDB_TEST_GROUP KdbpTestGroups[] =
{
    {"parse", "number and command line parsing", KdbpTestParsing},
    {"expr", "expressions, registers and symbols in them", KdbpTestExpressions},
    {"sym", "symbol lookups both ways", KdbpTestSymbols},
    {"mem", "memory display, change, search, fill, move and compare", KdbpTestMemory},
    {"bp", "breakpoints and what they refuse", KdbpTestBreakPoints},
    {"step", "stepping", KdbpTestStepping},
    {"regs", "registers", KdbpTestRegisters},
    {"walk", "walks of lists that may be damaged", KdbpTestWalks},
    {"lang", "statements, quotes, memory functions and pseudo-registers", KdbpTestLanguage},
    {"view", "text columns, physical memory and list walks", KdbpTestMoreMemory},
    {"ctl", "breakpoint options, display, g, gu and p counts", KdbpTestControl},
    {"script", "aliases, repeat, scripts and the transcript", KdbpTestScripts},
    {"dis", "the disassembler", KdbpTestDisassembler},
    {"cmd", "every command with bad arguments", KdbpTestEveryCommand},
    {"hazard", "inputs that used to stop the machine for good", KdbpTestHazards},
};

static
const KDB_TEST_GROUP *
KdbpTestFindGroup(
    _In_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbpTestGroups); Index++)
    {
        if (_stricmp(KdbpTestGroups[Index].Name, Name) == 0)
            return &KdbpTestGroups[Index];
    }

    return NULL;
}

static
VOID
KdbpTestRunGroup(
    _In_ const KDB_TEST_GROUP *Group)
{
    ULONG Checks = KdbpTestChecks;
    ULONG Failures = KdbpTestFailures;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;

    KdbpTestGroupName = Group->Name;
    KdbpTestSave();
    Start = KeQueryPerformanceCounter(&Frequency);
    Group->Routine();
    End = KeQueryPerformanceCounter(NULL);
    KdbpTestRestore();

    KdbpPrint("selftest: %s: %lu checks, %lu failed, %I64u ms\n",
              Group->Name,
              KdbpTestChecks - Checks,
              KdbpTestFailures - Failures,
              (Frequency.QuadPart != 0) ? (ULONGLONG)((End.QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart) : 0);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Keeps what the debugger prints while a check runs. Returns TRUE when it is
 * to go no further.
 */
BOOLEAN
KdbpSelfTestCapture(
    _In_reads_(Length) PCCH String,
    _In_ ULONG Length)
{
    ULONG Room;

    if (!KdbpTestCapturing)
        return FALSE;

    Room = KDB_TEST_CAPTURE_SIZE - 1 - KdbpTestCaptured;
    if (Length > Room)
    {
        KdbpTestOverflow = TRUE;
        Length = Room;
    }

    RtlCopyMemory(&KdbpTestOutput[KdbpTestCaptured], String, Length);
    KdbpTestCaptured += Length;
    KdbpTestOutput[KdbpTestCaptured] = ANSI_NULL;

    return !KdbpTestVerbose;
}

BOOLEAN
KdbpCmdSelfTest(
    ULONG Argc,
    PCHAR Argv[])
{
    const KDB_TEST_GROUP *Group;
    BOOLEAN Any = FALSE;
    ULONG Index;

    if (KdbpTestCapturing)
    {
        KdbPuts("selftest does not run inside itself\n");
        return TRUE;
    }

    KdbpTestVerbose = FALSE;
    KdbpTestChecks = 0;
    KdbpTestFailures = 0;

    for (Index = 1; Index < Argc; Index++)
    {
        if ((_stricmp(Argv[Index], "-v") == 0) || (_stricmp(Argv[Index], "verbose") == 0))
        {
            KdbpTestVerbose = TRUE;
            continue;
        }

        if ((_stricmp(Argv[Index], "list") == 0) || (_stricmp(Argv[Index], "?") == 0))
        {
            for (Index = 0; Index < RTL_NUMBER_OF(KdbpTestGroups); Index++)
                KdbpPrint("%-8s %s\n", KdbpTestGroups[Index].Name, KdbpTestGroups[Index].Help);
            return TRUE;
        }

        if ((_stricmp(Argv[Index], "all") != 0) && (KdbpTestFindGroup(Argv[Index]) == NULL))
        {
            KdbpPrint("%s is not a group of checks; selftest list shows them\n", Argv[Index]);
            return TRUE;
        }
    }

    for (Index = 1; Index < Argc; Index++)
    {
        if (_stricmp(Argv[Index], "all") == 0)
        {
            Any = FALSE;
            break;
        }

        Group = KdbpTestFindGroup(Argv[Index]);
        if (Group != NULL)
        {
            KdbpTestRunGroup(Group);
            Any = TRUE;
        }
    }

    if (!Any)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(KdbpTestGroups); Index++)
            KdbpTestRunGroup(&KdbpTestGroups[Index]);
    }

    KdbpPrint("selftest: %lu checks, %lu failed\n", KdbpTestChecks, KdbpTestFailures);
    return TRUE;
}

/* EOF */
