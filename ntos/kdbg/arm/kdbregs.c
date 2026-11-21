/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     KDBG view of the ARMv7 processor: registers, instructions and stops
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../kdb.h"
#include "../kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_REG(n, f) {n, (USHORT)FIELD_OFFSET(CONTEXT, f), (USHORT)RTL_FIELD_SIZE(CONTEXT, f)}

const KDB_REGISTER KdbpRegisters[] =
{
    KDB_REG("r0", R0), KDB_REG("r1", R1), KDB_REG("r2", R2), KDB_REG("r3", R3),
    KDB_REG("r4", R4), KDB_REG("r5", R5), KDB_REG("r6", R6), KDB_REG("r7", R7),
    KDB_REG("r8", R8), KDB_REG("r9", R9), KDB_REG("r10", R10), KDB_REG("r11", R11),
    KDB_REG("r12", R12), KDB_REG("sp", Sp), KDB_REG("lr", Lr), KDB_REG("pc", Pc),
    KDB_REG("cpsr", Cpsr),
};
const ULONG KdbpRegisterCount = RTL_NUMBER_OF(KdbpRegisters);

#define KDB_DREG(n, i) {n, (USHORT)(FIELD_OFFSET(CONTEXT, D) + (i) * sizeof(ULONGLONG)), (USHORT)sizeof(ULONGLONG)}

const KDB_REGISTER KdbpFloatingRegisters[] =
{
    KDB_DREG("d0", 0), KDB_DREG("d1", 1), KDB_DREG("d2", 2), KDB_DREG("d3", 3),
    KDB_DREG("d4", 4), KDB_DREG("d5", 5), KDB_DREG("d6", 6), KDB_DREG("d7", 7),
    KDB_DREG("d8", 8), KDB_DREG("d9", 9), KDB_DREG("d10", 10), KDB_DREG("d11", 11),
    KDB_DREG("d12", 12), KDB_DREG("d13", 13), KDB_DREG("d14", 14), KDB_DREG("d15", 15),
    KDB_REG("fpscr", Fpscr),
};
const ULONG KdbpFloatingRegisterCount = RTL_NUMBER_OF(KdbpFloatingRegisters);

/* Thumb-2: a first halfword of 0b11101, 0b11110 or 0b11111 starts a 32-bit instruction. */
static
BOOLEAN
KdbpArmReadHalf(
    _In_ ULONG_PTR Address,
    _Out_ PUSHORT Half)
{
    return NT_SUCCESS(KdbpSafeReadMemory(Half, (PVOID)(Address & ~1UL), sizeof(*Half)));
}

LONG
KdbpGetInstLength(
    _In_ ULONG_PTR Address)
{
    USHORT First;

    if (!KdbpArmReadHalf(Address, &First))
        return -1;
    return ((First >> 11) >= 0x1D) ? 4 : 2;
}

LONG
KdbpDisassemble(
    _In_ ULONG_PTR Address,
    _In_ ULONG IntelSyntax)
{
    USHORT First, Second;
    LONG Length;

    UNREFERENCED_PARAMETER(IntelSyntax);

    /* TODO: decode Thumb-2 mnemonics; the instruction is shown as its encoding */
    Length = KdbpGetInstLength(Address);
    if ((Length < 0) || !KdbpArmReadHalf(Address, &First))
        return -1;
    if ((Length == 4) && KdbpArmReadHalf(Address + 2, &Second))
        KdbPrintf(".inst.w 0x%04x%04x", First, Second);
    else
        KdbPrintf(".inst.n 0x%04x", First);
    return Length;
}

BOOLEAN
KdbpDisassemblerSelfTest(VOID)
{
    return TRUE;
}

BOOLEAN
NTAPI
KdbpArchIsBreakInstruction(
    _In_ ULONG_PTR Address)
{
    USHORT Half;

    if (!KdbpArmReadHalf(Address, &Half))
        return TRUE;
    return Half == KD_BREAKPOINT_VALUE;
}

/* BL/BLX (T1/T2) and BLX register are the calls a step-over runs past. */
BOOLEAN
NTAPI
KdbpArchIsStepOverInstruction(
    _In_ ULONG_PTR Address)
{
    USHORT First, Second;

    if (!KdbpArmReadHalf(Address, &First))
        return FALSE;
    if ((First & 0xFF87) == 0x4780)
        return TRUE;
    if (((First >> 11) == 0x1E) && KdbpArmReadHalf(Address + 2, &Second))
        return (Second & 0xC000) == 0xC000;
    return FALSE;
}

const KDB_STEP_SAMPLE KdbpArchStepSamples[] =
{
    { { 0x00, 0xF0, 0x00, 0xF8 }, 4, TRUE, "bl" },
    { { 0x88, 0x47 }, 2, TRUE, "blx r1" },
    { { 0x08, 0x46 }, 2, FALSE, "mov r0, r1" },
};
const ULONG KdbpArchStepSampleCount = RTL_NUMBER_OF(KdbpArchStepSamples);

NTSTATUS
NTAPI
KdbpArchSetSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(Context);
    /* TODO: step through a displaced breakpoint after the next instruction */
    return Enable ? STATUS_NOT_SUPPORTED : STATUS_SUCCESS;
}

KDB_STEP_HIT
NTAPI
KdbpArchTranslateStep(
    _In_opt_ PEXCEPTION_RECORD64 ExceptionRecord,
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(Context);
    return KdbStepNotHit;
}

VOID
NTAPI
KdbpArchPrepareContinue(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_opt_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Context);
}

ULONG
NTAPI
KdbpArchHardwareBreakPointCount(VOID)
{
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
    /* TODO: program the debug architecture breakpoint and watchpoint registers */
    return STATUS_NOT_SUPPORTED;
}

LONG
NTAPI
KdbpArchHardwareBreakPointHit(
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    return -1;
}

VOID
NTAPI
KdbpArchPrintInterruptTable(
    _In_ ULONG_PTR Address)
{
    UNREFERENCED_PARAMETER(Address);
    KdbPuts("The GIC routes interrupts by identifier; there is no vector table to list.\n");
}

ULONG
NTAPI
KdbpArchInterruptVectorLimit(VOID)
{
    return 0;
}

BOOLEAN
NTAPI
KdbpArchPrintInterruptVector(
    _In_ ULONG Vector,
    _In_ BOOLEAN ShowUnused)
{
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(ShowUnused);
    return FALSE;
}

/* A switched-out thread's stack starts with d8-d15, a pad word, r4-r11 and lr (KiArmSwapStack). */
BOOLEAN
NTAPI
KdbpArchContextFromKernelStack(
    _In_ PVOID KernelStack,
    _Out_ PCONTEXT Context)
{
    ULONG Frame[8 * 2 + 1 + 8 + 1];

    if (!NT_SUCCESS(KdbpSafeReadMemory(Frame, KernelStack, sizeof(Frame))))
        return FALSE;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    RtlCopyMemory(&Context->R4, &Frame[17], 8 * sizeof(ULONG));
    Context->Lr = Frame[25];
    Context->Pc = Frame[25] & ~1UL;
    Context->Sp = (ULONG)(ULONG_PTR)KernelStack + sizeof(Frame);
    Context->Cpsr = CPSR_MODE_SVC | CPSR_THUMB;
    return TRUE;
}
