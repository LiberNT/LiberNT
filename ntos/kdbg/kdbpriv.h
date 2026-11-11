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

VOID
KdbpPrintCountedString(
    _In_ PCUNICODE_STRING String);

/* kdbpci.c */

BOOLEAN
KdbpCmdPci(
    ULONG Argc,
    PCHAR Argv[]);

/* kdbpnp.c */

BOOLEAN KdbpCmdDeviceTree(ULONG Argc, PCHAR Argv[]);
BOOLEAN KdbpCmdDeviceNode(ULONG Argc, PCHAR Argv[]);

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

/* kdbcore.c */

BOOLEAN
KdbpCheckBreakPointCondition(
    _In_ PKDB_BREAKPOINT BreakPoint);

VOID
KdbpSetContinueType(
    _In_ KD_CONTINUE_TYPE Type);

/* EOF */
