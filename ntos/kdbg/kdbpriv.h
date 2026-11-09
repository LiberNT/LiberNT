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
