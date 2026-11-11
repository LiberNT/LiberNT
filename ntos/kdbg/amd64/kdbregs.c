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
    KDB_REG("rax", Rax),
    KDB_REG("rcx", Rcx),
    KDB_REG("rdx", Rdx),
    KDB_REG("rbx", Rbx),
    KDB_REG("rsp", Rsp),
    KDB_REG("rbp", Rbp),
    KDB_REG("rsi", Rsi),
    KDB_REG("rdi", Rdi),
    KDB_REG("r8", R8),
    KDB_REG("r9", R9),
    KDB_REG("r10", R10),
    KDB_REG("r11", R11),
    KDB_REG("r12", R12),
    KDB_REG("r13", R13),
    KDB_REG("r14", R14),
    KDB_REG("r15", R15),
    KDB_REG("rip", Rip),
    KDB_REG("eflags", EFlags),
    KDB_REG("cs", SegCs),
    KDB_REG("ds", SegDs),
    KDB_REG("es", SegEs),
    KDB_REG("fs", SegFs),
    KDB_REG("gs", SegGs),
    KDB_REG("ss", SegSs),
    KDB_REG("dr0", Dr0),
    KDB_REG("dr1", Dr1),
    KDB_REG("dr2", Dr2),
    KDB_REG("dr3", Dr3),
    KDB_REG("dr6", Dr6),
    KDB_REG("dr7", Dr7),
    KDB_REG("mxcsr", MxCsr),

    /* The names that mean the same thing on every architecture */
    KDB_REG("pc", Rip),
    KDB_REG("sp", Rsp),
    KDB_REG("fp", Rbp),
    KDB_REG("ret", Rax)
};

const ULONG KdbpRegisterCount = RTL_NUMBER_OF(KdbpRegisters);

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The processor steps one instruction when the trap flag is set in the flags
 * it resumes with.
 */
NTSTATUS
NTAPI
KdbpArchSetSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    if (Enable)
        Context->EFlags |= EFLAGS_TF;
    else
        Context->EFlags &= ~EFLAGS_TF;

    return STATUS_SUCCESS;
}

ULONG
NTAPI
KdbpArchHardwareBreakPointCount(VOID)
{
    return 4;
}

/*
 * One of the four address-watching registers. The register itself holds the
 * address; what is watched for and how wide it is live in the control
 * register, two bits and two bits per breakpoint.
 */
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
    ULONG64 Control;
    ULONG64 ReadWrite;
    ULONG64 Length;

    if (Index >= 4)
        return STATUS_INVALID_PARAMETER;

    Control = Context->Dr7;

    /* Clearing is only ever a matter of the enable bits */
    if (!Enable)
    {
        Control &= ~(3ULL << (Index * 2));
        Context->Dr7 = Control;
        return STATUS_SUCCESS;
    }

    switch (AccessType)
    {
        case KdbAccessExec:
            ReadWrite = 0;
            /* An execute breakpoint watches the one byte the instruction starts at */
            Size = 1;
            break;
        case KdbAccessWrite:
            ReadWrite = 1;
            break;
        case KdbAccessRead:
        case KdbAccessReadWrite:
            ReadWrite = 3;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    switch (Size)
    {
        case 1: Length = 0; break;
        case 2: Length = 1; break;
        case 4: Length = 3; break;
        case 8: Length = 2; break;
        default: return STATUS_INVALID_PARAMETER;
    }

    /* The watched address has to be aligned to the width being watched */
    if ((Address & (ULONG_PTR)(Size - 1)) != 0)
        return STATUS_DATATYPE_MISALIGNMENT;

    switch (Index)
    {
        case 0: Context->Dr0 = Address; break;
        case 1: Context->Dr1 = Address; break;
        case 2: Context->Dr2 = Address; break;
        case 3: Context->Dr3 = Address; break;
    }

    Control &= ~(0xFULL << (16 + (Index * 4)));
    Control |= (ReadWrite | (Length << 2)) << (16 + (Index * 4));

    /* Watch it on this processor only, which is where the debugger is */
    Control |= (1ULL << (Index * 2));

    Context->Dr7 = Control;
    return STATUS_SUCCESS;
}

/* EOF */
