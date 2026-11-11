/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The registers of this processor, by the names a person uses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../kdb.h"
#include "../kdbpriv.h"
#include <reactos/smpdbg.h>
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

const KDB_REGISTER KdbpFloatingRegisters[] =
{
    KDB_REG("v0", V[0]),
    KDB_REG("v1", V[1]),
    KDB_REG("v2", V[2]),
    KDB_REG("v3", V[3]),
    KDB_REG("v4", V[4]),
    KDB_REG("v5", V[5]),
    KDB_REG("v6", V[6]),
    KDB_REG("v7", V[7]),
    KDB_REG("v8", V[8]),
    KDB_REG("v9", V[9]),
    KDB_REG("v10", V[10]),
    KDB_REG("v11", V[11]),
    KDB_REG("v12", V[12]),
    KDB_REG("v13", V[13]),
    KDB_REG("v14", V[14]),
    KDB_REG("v15", V[15]),
    KDB_REG("v16", V[16]),
    KDB_REG("v17", V[17]),
    KDB_REG("v18", V[18]),
    KDB_REG("v19", V[19]),
    KDB_REG("v20", V[20]),
    KDB_REG("v21", V[21]),
    KDB_REG("v22", V[22]),
    KDB_REG("v23", V[23]),
    KDB_REG("v24", V[24]),
    KDB_REG("v25", V[25]),
    KDB_REG("v26", V[26]),
    KDB_REG("v27", V[27]),
    KDB_REG("v28", V[28]),
    KDB_REG("v29", V[29]),
    KDB_REG("v30", V[30]),
    KDB_REG("v31", V[31]),
    KDB_REG("fpcr", Fpcr),
    KDB_REG("fpsr", Fpsr)
};

const ULONG KdbpFloatingRegisterCount = RTL_NUMBER_OF(KdbpFloatingRegisters);

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

VOID
NTAPI
KdbpArchPrintInterruptTable(
    _In_ ULONG_PTR Address)
{
    UNREFERENCED_PARAMETER(Address);

    KdbPuts("This processor has no interrupt descriptor table\n");
}

ULONG
NTAPI
KdbpArchInterruptVectorLimit(VOID)
{
    return KiArm64QueryInterruptLimit();
}

BOOLEAN
NTAPI
KdbpArchPrintInterruptVector(
    _In_ ULONG Vector,
    _In_ BOOLEAN ShowUnused)
{
    PKINTERRUPT Head = KiArm64QueryInterrupt(Vector);
    BOOLEAN Shown = FALSE;
    ULONG Priority;
    ULONG Enable;
    ULONG Pending;
    ULONG Active;

    if (Head != NULL)
        Shown = KdbpPrintInterruptChain(Vector, Head, TRUE);

    HalArm64DbgGicState(Vector, &Priority, &Enable, &Pending, &Active);

    if ((Priority == MAXULONG) ||
        (!Shown && !ShowUnused && (Enable != 1) && (Pending != 1) && (Active != 1)))
    {
        if (!Shown && ShowUnused)
            KdbpPrint("Interrupt 0x%lx is not connected\n", Vector);

        return Shown;
    }

    if (!Shown)
        KdbpPrint("Interrupt 0x%lx:\n", Vector);

    KdbpPrint("  GIC priority 0x%02lx%s%s%s\n",
              Priority,
              (Enable == 1) ? " enabled" : " disabled",
              (Pending == 1) ? " pending" : "",
              (Active == 1) ? " active" : "");
    return TRUE;
}

/* EOF */
