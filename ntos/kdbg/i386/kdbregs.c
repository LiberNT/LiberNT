/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The registers of this processor, by the names a person uses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../kdb.h"
#include "../kdbpriv.h"
#include <Zydis/Zydis.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* What KiSwapContext leaves under KTHREAD::KernelStack (ctxswitch.S) */
typedef struct _KDB_SWITCH_STACK
{
    ULONG ExceptionList;
    ULONG ApcBypassDisable;
    ULONG SwapContextReturn;
    ULONG Ebp;
    ULONG Edi;
    ULONG Esi;
    ULONG Ebx;
    ULONG Return;
} KDB_SWITCH_STACK;

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

BOOLEAN
NTAPI
KdbpArchContextFromKernelStack(
    _In_ PVOID KernelStack,
    _Out_ PCONTEXT Context)
{
    KDB_SWITCH_STACK Stack;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Stack, KernelStack, sizeof(Stack))))
        return FALSE;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    Context->SegCs = KGDT_R0_CODE;
    Context->SegSs = KGDT_R0_DATA;
    Context->Eip = Stack.Return;
    Context->Esp = (ULONG_PTR)KernelStack + sizeof(Stack);
    Context->Ebp = Stack.Ebp;
    Context->Edi = Stack.Edi;
    Context->Esi = Stack.Esi;
    Context->Ebx = Stack.Ebx;
    return TRUE;
}

/*
 * Whether stepping over the instruction at Address means running to the one
 * after it: a call, or a repeated string instruction, which goes round in
 * place until it is done.
 */
BOOLEAN
NTAPI
KdbpArchIsStepOverInstruction(
    _In_ ULONG_PTR Address)
{
    UCHAR Code[ZYDIS_MAX_INSTRUCTION_LENGTH];
    ZydisDecodedInstruction Instruction;
    ZydisDecoder Decoder;
    ULONG Length;

    for (Length = 0; Length < sizeof(Code); Length++)
    {
        if ((Address + Length < Address) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Code[Length], (PVOID)(Address + Length), sizeof(Code[Length]))))
        {
            break;
        }
    }

    if (!ZYAN_SUCCESS(ZydisDecoderInit(&Decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32)) ||
        !ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&Decoder, NULL, Code, Length, &Instruction)))
    {
        return FALSE;
    }

    if (Instruction.meta.category == ZYDIS_CATEGORY_CALL)
        return TRUE;

    return (Instruction.meta.category == ZYDIS_CATEGORY_STRINGOP) &&
           ((Instruction.attributes & (ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE | ZYDIS_ATTRIB_HAS_REPNE)) != 0);
}

/* What the self-test asks the classification above about */
const KDB_STEP_SAMPLE KdbpArchStepSamples[] =
{
    {{0xE8, 0x00, 0x00, 0x00, 0x00}, 5, TRUE, "call rel32"},
    {{0xFF, 0xD0}, 2, TRUE, "call eax"},
    {{0xFF, 0x15, 0x00, 0x00, 0x00, 0x00}, 6, TRUE, "call [abs32]"},
    {{0x9A, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00}, 7, TRUE, "call far"},
    {{0xF3, 0xA4}, 2, TRUE, "rep movsb"},
    {{0xEB, 0xFE}, 2, FALSE, "jmp short"},
    {{0xC3}, 1, FALSE, "ret"},
    {{0xFF, 0xE0}, 2, FALSE, "jmp eax"},
    {{0x90}, 1, FALSE, "nop"}
};

const ULONG KdbpArchStepSampleCount = RTL_NUMBER_OF(KdbpArchStepSamples);

/* The processor steps by itself here: no stop is a step the debugger has to take back */
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

BOOLEAN
NTAPI
KdbpArchIsBreakInstruction(
    _In_ ULONG_PTR Address)
{
    UCHAR Byte;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Byte, (PVOID)Address, sizeof(Byte))))
        return TRUE;

    return Byte == 0xCC;
}

/* EOF */
