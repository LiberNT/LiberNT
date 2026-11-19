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

extern KI_INTERRUPT_DISPATCH_ENTRY KiUnexpectedRange[256];
extern KI_INTERRUPT_DISPATCH_ENTRY KiUnexpectedRangeEnd[];

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

#define KDB_X87_REGISTER_SIZE 10

const KDB_REGISTER KdbpFloatingRegisters[] =
{
    {"st0", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[0]), KDB_X87_REGISTER_SIZE},
    {"st1", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[1]), KDB_X87_REGISTER_SIZE},
    {"st2", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[2]), KDB_X87_REGISTER_SIZE},
    {"st3", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[3]), KDB_X87_REGISTER_SIZE},
    {"st4", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[4]), KDB_X87_REGISTER_SIZE},
    {"st5", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[5]), KDB_X87_REGISTER_SIZE},
    {"st6", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[6]), KDB_X87_REGISTER_SIZE},
    {"st7", (USHORT)FIELD_OFFSET(CONTEXT, FltSave.FloatRegisters[7]), KDB_X87_REGISTER_SIZE},
    KDB_REG("xmm0", Xmm0),
    KDB_REG("xmm1", Xmm1),
    KDB_REG("xmm2", Xmm2),
    KDB_REG("xmm3", Xmm3),
    KDB_REG("xmm4", Xmm4),
    KDB_REG("xmm5", Xmm5),
    KDB_REG("xmm6", Xmm6),
    KDB_REG("xmm7", Xmm7),
    KDB_REG("xmm8", Xmm8),
    KDB_REG("xmm9", Xmm9),
    KDB_REG("xmm10", Xmm10),
    KDB_REG("xmm11", Xmm11),
    KDB_REG("xmm12", Xmm12),
    KDB_REG("xmm13", Xmm13),
    KDB_REG("xmm14", Xmm14),
    KDB_REG("xmm15", Xmm15),
    KDB_REG("fctrl", FltSave.ControlWord),
    KDB_REG("fstat", FltSave.StatusWord),
    KDB_REG("ftag", FltSave.TagWord),
    KDB_REG("fop", FltSave.ErrorOpcode),
    KDB_REG("fioff", FltSave.ErrorOffset),
    KDB_REG("fiseg", FltSave.ErrorSelector),
    KDB_REG("fooff", FltSave.DataOffset),
    KDB_REG("foseg", FltSave.DataSelector),
    KDB_REG("mxcsr", MxCsr)
};

const ULONG KdbpFloatingRegisterCount = RTL_NUMBER_OF(KdbpFloatingRegisters);

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
KdbpArchPrintInterruptTable(
    _In_ ULONG_PTR Address)
{
    PKIDTENTRY64 Idt = KeGetPcr()->IdtBase;
    KIDTENTRY64 Entry;
    ULONG_PTR Handler;
    ULONG Vector;
    ULONG Shown = 0;

    for (Vector = 0; (Vector < 256) && !KdbpIsOutputAborted(); Vector++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Entry, &Idt[Vector], sizeof(Entry))) || !Entry.Present)
            continue;

        Handler = (ULONG_PTR)Entry.OffsetLow |
                  ((ULONG_PTR)Entry.OffsetMiddle << 16) |
                  ((ULONG_PTR)Entry.OffsetHigh << 32);

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

    if ((Handler >= (PVOID)KiUnexpectedRange) && (Handler <= (PVOID)KiUnexpectedRangeEnd))
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
    KSWITCH_FRAME Frame;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Frame, KernelStack, sizeof(Frame))))
        return FALSE;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    Context->SegCs = KGDT64_R0_CODE;
    Context->SegSs = KGDT64_R0_DATA;
    Context->Rip = Frame.Return;
    Context->Rsp = (ULONG_PTR)KernelStack + sizeof(Frame);
    Context->Rbp = Frame.Rbp;
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

    if (!ZYAN_SUCCESS(ZydisDecoderInit(&Decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)) ||
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
    {{0xFF, 0xD0}, 2, TRUE, "call rax"},
    {{0x41, 0xFF, 0xD3}, 3, TRUE, "call r11"},
    {{0xFF, 0x15, 0x00, 0x00, 0x00, 0x00}, 6, TRUE, "call [rip]"},
    {{0xF3, 0xA4}, 2, TRUE, "rep movsb"},
    {{0xF3, 0x48, 0xAB}, 3, TRUE, "rep stosq"},
    {{0xEB, 0xFE}, 2, FALSE, "jmp short"},
    {{0xC3}, 1, FALSE, "ret"},
    {{0x75, 0x00}, 2, FALSE, "jne"},
    {{0xFF, 0xE0}, 2, FALSE, "jmp rax"},
    {{0xA4}, 1, FALSE, "movsb"},
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

/* EOF */
