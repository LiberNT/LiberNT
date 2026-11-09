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
    KDB_REG("x0", X0),
    KDB_REG("x1", X1),
    KDB_REG("x2", X2),
    KDB_REG("x3", X3),
    KDB_REG("x4", X4),
    KDB_REG("x5", X5),
    KDB_REG("x6", X6),
    KDB_REG("x7", X7),
    KDB_REG("x8", X8),
    KDB_REG("x9", X9),
    KDB_REG("x10", X10),
    KDB_REG("x11", X11),
    KDB_REG("x12", X12),
    KDB_REG("x13", X13),
    KDB_REG("x14", X14),
    KDB_REG("x15", X15),
    KDB_REG("x16", X16),
    KDB_REG("x17", X17),
    KDB_REG("x18", X18),
    KDB_REG("x19", X19),
    KDB_REG("x20", X20),
    KDB_REG("x21", X21),
    KDB_REG("x22", X22),
    KDB_REG("x23", X23),
    KDB_REG("x24", X24),
    KDB_REG("x25", X25),
    KDB_REG("x26", X26),
    KDB_REG("x27", X27),
    KDB_REG("x28", X28),
    KDB_REG("x29", Fp),
    KDB_REG("x30", Lr),
    KDB_REG("lr", Lr),
    KDB_REG("cpsr", Cpsr),
    KDB_REG("fpcr", Fpcr),
    KDB_REG("fpsr", Fpsr),

    /* The names that mean the same thing on every architecture */
    KDB_REG("pc", Pc),
    KDB_REG("sp", Sp),
    KDB_REG("fp", Fp),
    KDB_REG("ret", X0)
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
