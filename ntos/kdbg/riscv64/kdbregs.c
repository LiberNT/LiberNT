/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The registers of this processor, by the names a person uses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../kdb.h"
#include "../kdbpriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define KDB_REG(n, f) {n, (USHORT)FIELD_OFFSET(CONTEXT, f), (USHORT)RTL_FIELD_SIZE(CONTEXT, f)}

const KDB_REGISTER KdbpRegisters[] =
{
    KDB_REG("ra", Ra),
    KDB_REG("gp", Gp),
    KDB_REG("tp", Tp),
    KDB_REG("t0", T0),
    KDB_REG("t1", T1),
    KDB_REG("t2", T2),
    KDB_REG("s0", S0),
    KDB_REG("s1", S1),
    KDB_REG("a0", A0),
    KDB_REG("a1", A1),
    KDB_REG("a2", A2),
    KDB_REG("a3", A3),
    KDB_REG("a4", A4),
    KDB_REG("a5", A5),
    KDB_REG("a6", A6),
    KDB_REG("a7", A7),
    KDB_REG("s2", S2),
    KDB_REG("s3", S3),
    KDB_REG("s4", S4),
    KDB_REG("s5", S5),
    KDB_REG("s6", S6),
    KDB_REG("s7", S7),
    KDB_REG("s8", S8),
    KDB_REG("s9", S9),
    KDB_REG("s10", S10),
    KDB_REG("s11", S11),
    KDB_REG("t3", T3),
    KDB_REG("t4", T4),
    KDB_REG("t5", T5),
    KDB_REG("t6", T6),
    KDB_REG("fcsr", Fcsr),

    /* The names that mean the same thing on every architecture */
    KDB_REG("pc", Pc),
    KDB_REG("sp", Sp),
    KDB_REG("fp", S0),
    KDB_REG("ret", A0)
};

const ULONG KdbpRegisterCount = RTL_NUMBER_OF(KdbpRegisters);

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Stepping one instruction at a time is not yet driven on this architecture.
 */
NTSTATUS
NTAPI
KdbpArchSetSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Enable);

    /* TODO: single stepping needs this architecture's own debug state */
    return STATUS_NOT_SUPPORTED;
}

ULONG
NTAPI
KdbpArchHardwareBreakPointCount(VOID)
{
    /* TODO: the watchpoint registers of this architecture are not driven yet */
    return 0;
}

NTSTATUS
NTAPI
KdbpArchSetHardwareBreakPoint(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Index,
    _In_ ULONG_PTR Address,
    _In_ UCHAR Size,
    _In_ KDB_ACCESS_TYPE AccessType,
    _In_ BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(AccessType);
    UNREFERENCED_PARAMETER(Enable);

    return STATUS_NOT_SUPPORTED;
}

/* EOF */
