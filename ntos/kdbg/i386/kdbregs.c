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
    KDB_REG("eax", Eax),
    KDB_REG("ecx", Ecx),
    KDB_REG("edx", Edx),
    KDB_REG("ebx", Ebx),
    KDB_REG("esp", Esp),
    KDB_REG("ebp", Ebp),
    KDB_REG("esi", Esi),
    KDB_REG("edi", Edi),
    KDB_REG("eip", Eip),
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

    /* The names that mean the same thing on every architecture */
    KDB_REG("pc", Eip),
    KDB_REG("sp", Esp),
    KDB_REG("fp", Ebp),
    KDB_REG("ret", Eax)
};

const ULONG KdbpRegisterCount = RTL_NUMBER_OF(KdbpRegisters);

#define KDB_X87_REGISTER_SIZE 10

/* TODO: xmm0-xmm7 and mxcsr from ExtendedRegisters when the context has CONTEXT_EXTENDED_REGISTERS */
const KDB_REGISTER KdbpFloatingRegisters[] =
{
    {"st0", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[0 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    {"st1", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[1 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    {"st2", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[2 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    {"st3", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[3 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    {"st4", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[4 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    {"st5", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[5 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    {"st6", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[6 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    {"st7", (USHORT)FIELD_OFFSET(CONTEXT, FloatSave.RegisterArea[7 * KDB_X87_REGISTER_SIZE]), KDB_X87_REGISTER_SIZE},
    KDB_REG("fctrl", FloatSave.ControlWord),
    KDB_REG("fstat", FloatSave.StatusWord),
    KDB_REG("ftag", FloatSave.TagWord),
    KDB_REG("fioff", FloatSave.ErrorOffset),
    KDB_REG("fiseg", FloatSave.ErrorSelector),
    KDB_REG("fooff", FloatSave.DataOffset),
    KDB_REG("foseg", FloatSave.DataSelector)
};

const ULONG KdbpFloatingRegisterCount = RTL_NUMBER_OF(KdbpFloatingRegisters);

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
    ULONG Control;
    ULONG ReadWrite;
    ULONG Length;

    if (Index >= 4)
        return STATUS_INVALID_PARAMETER;

    Control = Context->Dr7;

    /* Clearing is only ever a matter of the enable bits */
    if (!Enable)
    {
        Control &= ~(3UL << (Index * 2));
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

    Control &= ~(0xFUL << (16 + (Index * 4)));
    Control |= (ReadWrite | (Length << 2)) << (16 + (Index * 4));

    /* Watch it on this processor only, which is where the debugger is */
    Control |= (1UL << (Index * 2));

    Context->Dr7 = Control;
    return STATUS_SUCCESS;
}

VOID
NTAPI
KdbpArchPrintInterruptTable(
    _In_ ULONG_PTR Address)
{
    PKIDTENTRY Idt = KeGetPcr()->IDT;
    KIDTENTRY Entry;
    ULONG_PTR Handler;
    ULONG Vector;
    ULONG Shown = 0;

    for (Vector = 0; (Vector < 256) && !KdbpIsOutputAborted(); Vector++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, &Idt[Vector], sizeof(Entry))) || !(Entry.Access & 0x8000))
            continue;

        Handler = (ULONG_PTR)Entry.Offset | ((ULONG_PTR)Entry.ExtendedOffset << 16);

        if ((Address != 0) && (PAGE_ALIGN(Handler) != PAGE_ALIGN(Address)))
            continue;

        KdbpPrint("%02lx %p ", Vector, (PVOID)Handler);
        KdbSymPrintAddress((PVOID)Handler, NULL);
        KdbPuts("\n");
        Shown++;
    }

    if (Shown == 0)
        KdbPuts("No vector of this processor is handled there\n");
}

ULONG
NTAPI
KdbpArchInterruptVectorLimit(VOID)
{
    return MAXIMUM_IDTVECTOR + 1;
}

BOOLEAN
NTAPI
KdbpArchPrintInterruptVector(
    _In_ ULONG Vector,
    _In_ BOOLEAN ShowUnused)
{
    PVOID Handler = KeQueryInterruptHandler(Vector);

    if ((Handler >= (PVOID)KiStartUnexpectedRange) && (Handler < (PVOID)KiEndUnexpectedRange))
    {
        if (ShowUnused)
            KdbpPrint("Vector 0x%lx is not connected\n", Vector);

        return FALSE;
    }

    if (((ULONG_PTR)Handler >= FIELD_OFFSET(KINTERRUPT, DispatchCode)) &&
        KdbpPrintInterruptChain(Vector, CONTAINING_RECORD(Handler, KINTERRUPT, DispatchCode), FALSE))
    {
        return TRUE;
    }

    if (ShowUnused)
    {
        KdbpPrint("Vector 0x%lx is handled by the system at %p ", Vector, Handler);
        KdbSymPrintAddress(Handler, NULL);
        KdbPuts("\n");
    }

    return FALSE;
}

/* EOF */
