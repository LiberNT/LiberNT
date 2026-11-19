/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the interactive debugger says to itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * One register as the person at the prompt can name it. The offset is into
 * the saved context, so every architecture describes its own registers and
 * nothing above has to know which architecture it is running on.
 */
typedef struct _KDB_REGISTER
{
    PCSTR Name;
    USHORT Offset;
    USHORT Size;
} KDB_REGISTER, *PKDB_REGISTER;

extern const KDB_REGISTER KdbpRegisters[];
extern PKTHREAD KdbBreakRequestThread;
extern const ULONG KdbpRegisterCount;
extern const KDB_REGISTER KdbpFloatingRegisters[];
extern const ULONG KdbpFloatingRegisterCount;

/*
 * Stepping one instruction at a time, and stopping on an address being used
 * rather than reached, are both things only the processor can do. Each
 * architecture answers for its own and the rest of the debugger asks without
 * knowing which one it is on.
 */

NTSTATUS
NTAPI
KdbpArchSetSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
KdbpArchSetHardwareBreakPoint(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Index,
    _In_ ULONG_PTR Address,
    _In_ UCHAR Size,
    _In_ KDB_ACCESS_TYPE AccessType,
    _In_ BOOLEAN Enable);

/*
 * Which watch register made the processor stop, or -1 when none did. The
 * register is told it has been seen, so it does not report the same hit again.
 */
LONG
NTAPI
KdbpArchHardwareBreakPointHit(
    _Inout_ PCONTEXT Context);

typedef enum _KDB_STEP_HIT
{
    KdbStepNotHit,
    KdbStepDone,
    KdbStepForeignHit,
    KdbStepAbandoned
} KDB_STEP_HIT;

/* Whether the instruction at an address raises a breakpoint; one that cannot be read counts as one */
BOOLEAN
NTAPI
KdbpArchIsBreakInstruction(
    _In_ ULONG_PTR Address);

/*
 * Called at every entry: takes back a step the processor could not take by
 * itself, and says whether this stop is the end of it or other code that ran
 * into it.
 */
KDB_STEP_HIT
NTAPI
KdbpArchTranslateStep(
    _In_opt_ PEXCEPTION_RECORD64 ExceptionRecord,
    _Inout_ PCONTEXT Context);

/* Fills the answer to a stop with the stepping and watch state the debugger set up */
VOID
NTAPI
KdbpArchPrepareContinue(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_opt_ PCONTEXT Context);

/* How many of them this architecture has */
ULONG
NTAPI
KdbpArchHardwareBreakPointCount(VOID);

/* Lists the interrupt vectors, or only those handled on the page of Address */
VOID
NTAPI
KdbpArchPrintInterruptTable(
    _In_ ULONG_PTR Address);

ULONG
NTAPI
KdbpArchInterruptVectorLimit(VOID);

BOOLEAN
NTAPI
KdbpArchPrintInterruptVector(
    _In_ ULONG Vector,
    _In_ BOOLEAN ShowUnused);

BOOLEAN
NTAPI
KdbpArchContextFromKernelStack(
    _In_ PVOID KernelStack,
    _Out_ PCONTEXT Context);

/* Whether stepping over the instruction at Address runs to the one after it, as over a call */
BOOLEAN
NTAPI
KdbpArchIsStepOverInstruction(
    _In_ ULONG_PTR Address);

/* An instruction the self-test asks the architecture about, and the answer it should give */
typedef struct _KDB_STEP_SAMPLE
{
    UCHAR Bytes[8];
    UCHAR Length;
    BOOLEAN StepOver;
    PCSTR Name;
} KDB_STEP_SAMPLE;

extern const KDB_STEP_SAMPLE KdbpArchStepSamples[];
extern const ULONG KdbpArchStepSampleCount;

/* kdbanalyze.c */

BOOLEAN
KdbpCmdAnalyze(
    ULONG Argc,
    PCHAR Argv[]);

/* kdbmem.c */

BOOLEAN KdbpCmdNearestSymbol(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDumpUnicode(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDumpDwordSymbols(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDumpQwordSymbols(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDumpPointerSymbols(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdSearchMemory(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdCompareMemory(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdFillMemory(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdMoveMemory(ULONG Argc, PCHAR Argv[]);

/* kdbobj.c */

BOOLEAN KdbpCmdProcessDetail(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdTeb(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdPeb(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdObjectHeader(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDriverObject(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDeviceObject(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDeviceStack(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdIrp(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdFileObject(ULONG Argc, PCHAR Argv[]);

/* kdbpci.c */

BOOLEAN
KdbpCmdPci(
    ULONG Argc,
    PCHAR Argv[]);

/* kdbpnp.c */

BOOLEAN KdbpCmdDeviceTree(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDeviceNode(ULONG Argc, PCHAR Argv[]);

/* kdbscript.c */

BOOLEAN
KdbpJoinWords(
    _In_ ULONG First,
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_writes_(Length) PCHAR Buffer,
    _In_ ULONG Length);

VOID KdbpTranscriptAppend(_In_reads_(Length) PCCH String, _In_ ULONG Length);
VOID KdbpTranscriptCommand(_In_ PCSTR Command);
BOOLEAN KdbpExpandAlias(_In_ ULONG Argc, _In_ PCHAR Argv[], _Out_writes_(Length) PCHAR Buffer, _In_ ULONG Length);
BOOLEAN KdbpCmdVersion(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdEcho(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdAlias(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdUnalias(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdTranscript(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdScript(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdRepeat(ULONG Argc, PCHAR Argv[]);

/* kdbtest.c */

BOOLEAN KdbpCmdSelfTest(ULONG Argc, PCHAR Argv[]);

/* kdbtypes.c */

BOOLEAN KdbpCmdDisplayType(ULONG Argc, PCHAR Argv[]);

/* kdbsched.c */

BOOLEAN KdbpCmdPrcb(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdReady(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDpc(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdTimer(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdInterrupt(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDispatcher(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdInterruptTable(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdInterrupts(ULONG Argc, PCHAR Argv[]);

BOOLEAN
KdbpPrintInterruptChain(
    _In_ ULONG Vector,
    _In_ PKINTERRUPT Head,
    _In_ BOOLEAN ShowInvalid);

/* kdbexpr.c */

VOID
KdbpSetLastExpressionValue(
    _In_ ULONGLONG Value);

VOID
KdbpSetListEntryValue(
    _In_ ULONGLONG Value);

/* kdbsymbols.c */

BOOLEAN
KdbpSymLookupName(
    _In_ PCSTR Name,
    _Out_ PULONG_PTR Address);

/* kdbcli.c */

VOID
KdbpPagerReset(VOID);

BOOLEAN
KdbpCliInterpretCommand(
    _In_ PCHAR Command);

BOOLEAN
KdbpIsCommandName(
    _In_ PCSTR Name);

ULONG
KdbpCheckCommandTable(VOID);

PCSTR
KdbpGetCommandName(
    _In_ ULONG Index);

VOID
KdbpClearResume(VOID);

VOID
KdbpResetView(VOID);

VOID
KdbpPrintBlock(
    _In_reads_(Length) PCHAR Buffer,
    _In_ ULONG Length);

/* kdbcore.c */

/* The most entries a walk of a system list visits before it calls the list broken */
#define KDB_MAXIMUM_LIST_WALK 4096

typedef enum _KDB_WALK_RESULT
{
    KdbWalkComplete,
    KdbWalkStopped,
    KdbWalkUnreadable,
    KdbWalkTooLong
} KDB_WALK_RESULT;

typedef BOOLEAN (NTAPI *PKDB_WALK_ROUTINE)(_In_ PLIST_ENTRY Link, _In_opt_ PVOID Context);

KDB_WALK_RESULT
KdbpWalkList(
    _In_ PLIST_ENTRY Head,
    _In_ ULONG Maximum,
    _In_ PKDB_WALK_ROUTINE Routine,
    _In_opt_ PVOID Context,
    _Out_opt_ PULONG Count,
    _Out_opt_ PLIST_ENTRY *Broken);

VOID
KdbpReportWalk(
    _In_ KDB_WALK_RESULT Result,
    _In_opt_ PLIST_ENTRY Broken,
    _In_ ULONG Maximum);

BOOLEAN
KdbpSetBreakPointInternal(
    _In_ LONG BreakPointNr);

/* The most a breakpoint's commands may be */
#define KDB_BREAKPOINT_COMMANDS_MAX 256

BOOLEAN
KdbpSetBreakPointCommands(
    _In_ LONG BreakPointNr,
    _In_opt_ PCSTR Commands);

BOOLEAN
KdbpSetBreakPointIgnoreCount(
    _In_ LONG BreakPointNr,
    _In_ ULONG Count);

BOOLEAN
KdbpGetBreakPointCounts(
    _In_ LONG BreakPointNr,
    _Out_ PULONG HitCount,
    _Out_ PULONG IgnoreCount,
    _Out_ PCSTR *Commands);

/* The commands of the breakpoint that stopped the machine, handed over once */
BOOLEAN
KdbpTakeStopCommands(
    _Out_writes_(Length) PCHAR Buffer,
    _In_ ULONG Length);

BOOLEAN
KdbpIsSystemAddress(
    _In_ ULONG_PTR Address);

BOOLEAN
KdbpCheckBreakPointCondition(
    _In_ PKDB_BREAKPOINT BreakPoint);

VOID
KdbpSetContinueType(
    _In_ KD_CONTINUE_TYPE Type);

/* EOF */
