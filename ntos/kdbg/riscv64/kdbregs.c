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

const KDB_REGISTER KdbpFloatingRegisters[] =
{
    KDB_REG("f0", F[0]),
    KDB_REG("f1", F[1]),
    KDB_REG("f2", F[2]),
    KDB_REG("f3", F[3]),
    KDB_REG("f4", F[4]),
    KDB_REG("f5", F[5]),
    KDB_REG("f6", F[6]),
    KDB_REG("f7", F[7]),
    KDB_REG("f8", F[8]),
    KDB_REG("f9", F[9]),
    KDB_REG("f10", F[10]),
    KDB_REG("f11", F[11]),
    KDB_REG("f12", F[12]),
    KDB_REG("f13", F[13]),
    KDB_REG("f14", F[14]),
    KDB_REG("f15", F[15]),
    KDB_REG("f16", F[16]),
    KDB_REG("f17", F[17]),
    KDB_REG("f18", F[18]),
    KDB_REG("f19", F[19]),
    KDB_REG("f20", F[20]),
    KDB_REG("f21", F[21]),
    KDB_REG("f22", F[22]),
    KDB_REG("f23", F[23]),
    KDB_REG("f24", F[24]),
    KDB_REG("f25", F[25]),
    KDB_REG("f26", F[26]),
    KDB_REG("f27", F[27]),
    KDB_REG("f28", F[28]),
    KDB_REG("f29", F[29]),
    KDB_REG("f30", F[30]),
    KDB_REG("f31", F[31]),
    KDB_REG("fcsr", Fcsr)
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
    return KiRiscvQueryInterruptLimit();
}

BOOLEAN
NTAPI
KdbpArchPrintInterruptVector(
    _In_ ULONG Vector,
    _In_ BOOLEAN ShowUnused)
{
    PKINTERRUPT Head = KiRiscvQueryInterrupt(Vector);

    if ((Head != NULL) && KdbpPrintInterruptChain(Vector, Head, ShowUnused))
        return TRUE;

    if ((Head == NULL) && ShowUnused)
        KdbpPrint("Interrupt 0x%lx is not connected\n", Vector);

    return FALSE;
}

/* EOF */
