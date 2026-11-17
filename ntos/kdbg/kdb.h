/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Built-in kernel debugger declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once
#include "../kd/kd.h"
#include <symz.h>

typedef CONTEXT KDB_KTRAP_FRAME, *PKDB_KTRAP_FRAME;

typedef enum _KDB_BREAKPOINT_TYPE
{
   KdbBreakPointNone = 0,
   KdbBreakPointSoftware,
   KdbBreakPointHardware,
   KdbBreakPointTemporary
} KDB_BREAKPOINT_TYPE;

typedef enum _KDB_ACCESS_TYPE
{
   KdbAccessRead,
   KdbAccessWrite,
   KdbAccessReadWrite,
   KdbAccessExec
} KDB_ACCESS_TYPE;

typedef struct _KDB_BREAKPOINT
{
   KDB_BREAKPOINT_TYPE    Type;
   BOOLEAN                Enabled;
   ULONG_PTR              Address;
   BOOLEAN                Global;
   PEPROCESS              Process;
   PCHAR                  ConditionExpression;
   PVOID                  Condition;
   BOOLEAN                Internal;     /* Set by a step or a run to an address; gone at the next stop */
   ULONG                  HitCount;     /* Times it was reached with its condition holding */
   ULONG                  IgnoreCount;  /* Hits still to let pass without stopping */
   PCHAR                  Commands;     /* What to run when it stops the machine */
   union {

      KD_BREAKPOINT_TYPE  SavedInstruction;

      struct {
         UCHAR            DebugReg : 2;
         UCHAR            Size : 4;
         KDB_ACCESS_TYPE  AccessType;
      } Hw;
   } Data;
} KDB_BREAKPOINT, *PKDB_BREAKPOINT;

typedef enum _KDB_ENTER_CONDITION
{
   KdbDoNotEnter,
   KdbEnterAlways,
   KdbEnterFromKmode,
   KdbEnterFromUmode
} KDB_ENTER_CONDITION;

typedef enum _KD_CONTINUE_TYPE
{
    kdContinue = 0,
    kdDoNotHandleException,
    kdHandleException
} KD_CONTINUE_TYPE;

extern PEPROCESS KdbCurrentProcess;

extern PETHREAD KdbCurrentThread;

extern PETHREAD KdbOriginalThread;

extern EXCEPTION_RECORD64 KdbCurrentExceptionRecord;

extern BOOLEAN KdbCurrentExceptionRecordValid;

extern BOOLEAN KdbActive;

extern BOOLEAN KdbBugCheckRequested;

extern LONG KdbLastBreakPointNr;

extern ULONG KdbNumSingleSteps;

extern BOOLEAN KdbSingleStepOver;

extern PKDB_KTRAP_FRAME KdbCurrentTrapFrame;

#if defined(_M_RISCV64)

BOOLEAN
KdbpRiscvGetNextPc(
   IN PCONTEXT Context,
   OUT PULONG_PTR NextPc,
   OUT PCSTR *Reason);
#endif

typedef BOOLEAN (NTAPI *PKDB_SYMBOL_ENUM_CALLBACK)(_In_ ULONG_PTR Address, _In_ PCSTR ModuleName, _In_ PCSTR FunctionName, _In_ PCSTR FileName, _In_ ULONG SourceLine, _In_opt_ PVOID Context);

VOID
KbdDisableMouse(VOID);

VOID
KbdEnableMouse(VOID);

VOID
NTAPI
KdbpStackSwitchAndCall(
    _In_ PVOID NewStack,
    _In_ VOID (*Function)(VOID));

LONG
KdbpDisassemble(
    _In_ ULONG_PTR Address,
    _In_ ULONG IntelSyntax);

BOOLEAN
KdbpDisassemblerSelfTest(VOID);

LONG
KdbpGetInstLength(
    _In_ ULONG_PTR Address);

VOID
KdbSymzPrepareImage(
    _In_ PVOID ImageBase);

BOOLEAN
KdbpSymzEnumerate(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PSYMZ_ENUM_ROUTINE Routine,
    _In_ PVOID Context);

NTSTATUS
NTAPI
KdbInitialize(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase);

BOOLEAN
NTAPI
KdbRegisterCliCallback(
    PVOID Callback,
    BOOLEAN Deregister);

VOID
KdbpCaptureOutput(
    _In_ PCCH String,
    _In_ USHORT Length);

BOOLEAN
KdbpSelfTestCapture(
    _In_reads_(Length) PCCH String,
    _In_ ULONG Length);

NTSTATUS
KdbpCliInit(VOID);

VOID
KdbpCliMainLoop(
    _In_ BOOLEAN EnteredOnSingleStep);

BOOLEAN
NTAPI
KdbpGetAddressExpression(
    _In_ PCHAR Expression,
    _Out_ PULONG_PTR Address);

BOOLEAN
NTAPI
KdbpGetHexNumber(
    _In_ PCHAR pszNum,
    _Out_ ULONG_PTR *pulValue);

BOOLEAN
KdbpIsOutputAborted(VOID);

VOID
KdbpPager(
    _In_ PCHAR Buffer,
    _In_ ULONG BufLength);

VOID
KdbpPrint(
    _In_ PSTR Format,
    _In_ ...);

VOID
KdbpPrintUnicodeString(
    _In_ PCUNICODE_STRING String);

VOID
KdbpPrintCountedString(
    _In_ PCUNICODE_STRING String);

KD_CONTINUE_TYPE
KdbEnterDebuggerException(
    _In_ PEXCEPTION_RECORD64 ExceptionRecord,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN FirstChance);

BOOLEAN
KdbpAttachToProcess(
    PVOID ProcessId);

BOOLEAN
KdbpAttachToThread(
    PVOID ThreadId);

BOOLEAN
KdbpDeleteBreakPoint(
    _In_opt_ LONG BreakPointNr,
    _Inout_opt_ PKDB_BREAKPOINT BreakPoint);

BOOLEAN
KdbpDisableBreakPoint(
    _In_opt_ LONG BreakPointNr,
    _Inout_opt_ PKDB_BREAKPOINT BreakPoint);

BOOLEAN
KdbpEnableBreakPoint(
    _In_opt_ LONG BreakPointNr,
    _Inout_opt_ PKDB_BREAKPOINT BreakPoint);

BOOLEAN
KdbpFindProcessById(
    _In_ PVOID ProcessId,
    _Out_ PEPROCESS *Process);

BOOLEAN
KdbpFindThreadById(
    _In_ PVOID ThreadId,
    _Out_ PETHREAD *Thread);

BOOLEAN
KdbpGetBreakPointInfo(
    _In_ ULONG BreakPointNr,
    _Out_opt_ ULONG_PTR *Address,
    _Out_opt_ KDB_BREAKPOINT_TYPE *Type,
    _Out_opt_ UCHAR *Size,
    _Out_opt_ KDB_ACCESS_TYPE *AccessType,
    _Out_opt_ UCHAR *DebugReg,
    _Out_opt_ BOOLEAN *Enabled,
    _Out_opt_ BOOLEAN *Global,
    _Out_opt_ PEPROCESS *Process,
    _Out_opt_ PCHAR *ConditionExpression);

VOID
KdbpGetCommandLineSettings(
    _In_ PCSTR p1);

BOOLEAN
KdbpGetEnterCondition(
    _In_ LONG ExceptionNr,
    _In_ BOOLEAN FirstChance,
    _Out_ KDB_ENTER_CONDITION *Condition);

LONG
KdbpGetNextBreakPointNr(
    _In_opt_ ULONG Start);

NTSTATUS
KdbpInsertBreakPoint(
    _In_ ULONG_PTR Address,
    _In_ KDB_BREAKPOINT_TYPE Type,
    _In_opt_ UCHAR Size,
    _In_opt_ KDB_ACCESS_TYPE AccessType,
    _In_opt_ PCHAR ConditionExpression,
    _In_ BOOLEAN Global,
    _Out_opt_ PLONG BreakPointNr);

BOOLEAN
KdbpKdbTrapFrameFromKernelStack(
    PVOID KernelStack,
    PKDB_KTRAP_FRAME KdbTrapFrame);

NTSTATUS
KdbpSafeReadMemory(
    _Out_ PVOID Dest,
    _In_ PVOID Src,
    _In_ ULONG Bytes);

NTSTATUS
KdbpSafeWriteMemory(
    _Out_ PVOID Dest,
    _In_ PVOID Src,
    _In_ ULONG Bytes);

BOOLEAN
KdbpSetEnterCondition(
    _In_ LONG ExceptionNr,
    _In_ BOOLEAN FirstChance,
    _In_ KDB_ENTER_CONDITION Condition);

NTSTATUS
KdbpGetRegisterValue(
    _In_ PKDB_KTRAP_FRAME TrapFrame,
    _In_ PCSTR RegisterName,
    _Out_ PULONGLONG Value,
    _Out_opt_ PULONG RegisterSize);

BOOLEAN
KdbpRpnEvaluateExpression(
    _In_ PCHAR Expression,
    _In_ PKDB_KTRAP_FRAME TrapFrame,
    _Out_ PULONGLONG Result,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg);

BOOLEAN
KdbpRpnEvaluateParsedExpression(
    _In_ PVOID Expression,
    _In_ PKDB_KTRAP_FRAME TrapFrame,
    _Out_ PULONGLONG Result,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg);

PVOID
KdbpRpnParseExpression(
    _In_ PCHAR Expression,
    _Out_opt_ PLONG ErrOffset,
    _Out_opt_ PCHAR ErrMsg);

NTSTATUS
KdbpSetRegisterValue(
    _Inout_ PKDB_KTRAP_FRAME TrapFrame,
    _In_ PCSTR RegisterName,
    _In_ ULONGLONG Value);

PCSTR
KdbGetHistoryEntry(
    _Inout_ PLONG NextIndex,
    _In_ BOOLEAN Next);

VOID
KdbpCommandHistoryAppend(
    _In_ PCSTR Command);

VOID
KdbPrintString(
    _In_ const CSTRING* Output);

VOID
__cdecl
KdbPrintf(
    _In_ PCSTR Format,
    ...);

SIZE_T
KdbPrompt(
    _In_ PCSTR Prompt,
    _Out_ PCHAR Buffer,
    _In_ SIZE_T Size);

USHORT
KdbPromptString(
    _In_ const CSTRING* PromptString,
    _Inout_ PSTRING ResponseString);

VOID
KdbPuts(
    _In_ PCSTR String);

VOID
KdbPutsN(
    _In_ PCCH String,
    _In_ USHORT Length);

NTSTATUS
KdbSymEnumerate(
    _In_ PCSTR ModulePattern,
    _In_ PCSTR SymbolPattern,
    _In_ ULONG MaximumMatches,
    _In_ PKDB_SYMBOL_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ PULONG MatchCount,
    _Out_ PBOOLEAN Truncated);

BOOLEAN
KdbSymPrintAddress(
    _In_ PVOID Address,
    _In_ PCONTEXT Context);

BOOLEAN
KdbSymPrintNearest(
    _In_ PVOID Address,
    _In_ PCONTEXT Context);

BOOLEAN
KdbpSymFindModule(
    _In_opt_ PVOID Address,
    _In_opt_ INT Index,
    _Out_ PLDR_DATA_TABLE_ENTRY* pLdrEntry);

BOOLEAN
KdbSymDescribeAddress(
    _In_ PVOID Address,
    _Out_writes_z_(ModuleNameLength) PCHAR ModuleName,
    _In_ ULONG ModuleNameLength,
    _Out_writes_z_(FunctionNameLength) PCHAR FunctionName,
    _In_ ULONG FunctionNameLength,
    _Out_ PULONG_PTR Displacement,
    _Out_writes_opt_z_(FileNameLength) PCHAR FileName,
    _In_ ULONG FileNameLength,
    _Out_opt_ PULONG Line);

BOOLEAN
KdbpSymzDescribe(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ ULONG_PTR RelativeAddress,
    _Out_writes_z_(FunctionNameLength) PCHAR FunctionName,
    _In_ ULONG FunctionNameLength,
    _Out_ PULONG_PTR SymbolAddress,
    _Out_writes_opt_z_(FileNameLength) PCHAR FileName,
    _In_ ULONG FileNameLength,
    _Out_opt_ PULONG Line);

FORCEINLINE
BOOLEAN
KdbpProcessorStateIsFrozen(
    _In_ ULONG FrozenState)
{
#ifdef _WIN64
    return (FrozenState & ~IPI_FROZEN_FLAG_ACTIVE) == IPI_FROZEN_STATE_FROZEN;
#else
    UNREFERENCED_PARAMETER(FrozenState);
    return FALSE;
#endif
}
