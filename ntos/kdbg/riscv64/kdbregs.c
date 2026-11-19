/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The registers of this processor, by the names a person uses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../kdb.h"
#include "../kdbpriv.h"
#include "riscv64-decode.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define KDB_REG(n, f) {n, (USHORT)FIELD_OFFSET(CONTEXT, f), (USHORT)RTL_FIELD_SIZE(CONTEXT, f)}

const KDB_REGISTER KdbpRegisters[] =
{
    KDB_REG("ra", Ra),
    KDB_REG("sp", Sp),
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
    KDB_REG("pc", Pc),
    KDB_REG("fcsr", Fcsr),

    /* The names that mean the same thing on every architecture */
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
 * The processor has no step trap in supervisor mode, so a step is a breakpoint
 * at the instruction that runs next, written over it and taken back when the
 * stepping thread reaches it. Each thread has its own.
 */
#define KDB_RISCV_EBREAK            0x00100073UL
#define KDB_RISCV_SRET              0x10200073UL
#define KDB_RISCV_DISPLACED_SLOTS   8
#define KDB_RISCV_DISPLACED_SIZE    8

typedef struct _KDB_RISCV_DISPLACED
{
    ULONG_PTR ResumePc;
    PKTHREAD Thread;
    ULONG Length;
    BOOLEAN InUse;
} KDB_RISCV_DISPLACED;

extern UCHAR KiRiscvTrapEntryEnd[];

typedef struct _KDB_RISCV_STEP
{
    ULONG_PTR Address;
    PKTHREAD Thread;
    ULONG_PTR Stack;
    KD_BREAKPOINT_TYPE Saved;
    BOOLEAN Armed;
} KDB_RISCV_STEP;

static KDB_RISCV_STEP KdbpRiscvSteps[MAXIMUM_PROCESSORS];
static KDB_RISCV_DISPLACED KdbpRiscvDisplaced[KDB_RISCV_DISPLACED_SLOTS];

C_ASSERT(KDB_RISCV_DISPLACED_SLOTS * KDB_RISCV_DISPLACED_SIZE == 64);
C_ASSERT(sizeof(ULONG) + KD_BREAKPOINT_SIZE <= KDB_RISCV_DISPLACED_SIZE);

/* Other code that runs into a step's breakpoint runs the instruction it covers here, one slot each */
__attribute__((naked, aligned(KDB_RISCV_DISPLACED_SIZE)))
static
VOID
KdbpRiscvDisplacedCode(VOID)
{
    __asm__ __volatile__(".rept 32\n\t.2byte 0x9002\n\t.endr");
}

static
NTSTATUS
KdbpRiscvCopyCode(
    _In_ ULONG_PTR Address,
    _Inout_updates_bytes_(Size) PVOID Value,
    _In_ ULONG Size,
    _In_ BOOLEAN Write)
{
    NTSTATUS Status;

    Status = KdpCopyProcessMemoryChunks(NULL,
                                        (ULONG64)Address,
                                        Value,
                                        Size,
                                        0,
                                        MMDBG_COPY_UNSAFE | (Write ? MMDBG_COPY_WRITE : 0),
                                        NULL);
    if (NT_SUCCESS(Status) && Write)
        KeSweepICache((PVOID)Address, Size);
    return Status;
}

static
LONG
KdbpRiscvFindStep(
    _In_ PKTHREAD Thread)
{
    LONG Index;

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpRiscvSteps); Index++)
    {
        if (KdbpRiscvSteps[Index].Armed && (KdbpRiscvSteps[Index].Thread == Thread))
            return Index;
    }

    return -1;
}

/* Another armed step at the same address, which holds the instruction the breakpoint covers */
static
LONG
KdbpRiscvStepAt(
    _In_ ULONG_PTR Address,
    _In_ LONG Except)
{
    LONG Index;

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpRiscvSteps); Index++)
    {
        if ((Index != Except) && KdbpRiscvSteps[Index].Armed && (KdbpRiscvSteps[Index].Address == Address))
            return Index;
    }

    return -1;
}

static
VOID
KdbpRiscvDisarmStep(
    _In_ LONG Index)
{
    KDB_RISCV_STEP *Step;

    if (Index < 0)
        return;

    ASSERT(Index < (LONG)RTL_NUMBER_OF(KdbpRiscvSteps));
    Step = &KdbpRiscvSteps[Index];
    ASSERT(Step->Armed);
    Step->Armed = FALSE;

    if (KdbpRiscvStepAt(Step->Address, Index) >= 0)
        return;

    if (!NT_SUCCESS(KdbpRiscvCopyCode(Step->Address, &Step->Saved, sizeof(Step->Saved), TRUE)))
        KdbpPrint("The instruction at %p could not be put back after a step\n", (PVOID)Step->Address);
}

NTSTATUS
NTAPI
KdbpArchSetSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    KD_BREAKPOINT_TYPE Instruction = KD_BREAKPOINT_VALUE;
    PKTHREAD Thread = KeGetCurrentThread();
    KDB_RISCV_STEP *Step;
    ULONG_PTR NextPc;
    PCSTR Reason;
    NTSTATUS Status;
    LONG Index, Shared;

    KdbpRiscvDisarmStep(KdbpRiscvFindStep(Thread));
    if (!Enable)
        return STATUS_SUCCESS;

    if (!KdbpRiscvGetNextPc(Context, &NextPc, &Reason))
        return STATUS_NOT_SUPPORTED;

    /* The debugger's own trap returns through that code: a breakpoint there would stop the debugger itself */
    if ((NextPc >= (ULONG_PTR)KiRiscvTrapEntry) && (NextPc < (ULONG_PTR)KiRiscvTrapEntryEnd))
        return STATUS_NOT_SUPPORTED;

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpRiscvSteps); Index++)
    {
        if (!KdbpRiscvSteps[Index].Armed)
            break;
    }
    if (Index == (LONG)RTL_NUMBER_OF(KdbpRiscvSteps))
        return STATUS_INSUFFICIENT_RESOURCES;

    Step = &KdbpRiscvSteps[Index];
    Shared = KdbpRiscvStepAt(NextPc, Index);
    if (Shared >= 0)
    {
        Step->Saved = KdbpRiscvSteps[Shared].Saved;
    }
    else
    {
        Status = KdbpRiscvCopyCode(NextPc, &Step->Saved, sizeof(Step->Saved), FALSE);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = KdbpRiscvCopyCode(NextPc, &Instruction, sizeof(Instruction), TRUE);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Step->Address = NextPc;
    Step->Thread = Thread;
    Step->Stack = (ULONG_PTR)Context->Sp;
    Step->Armed = TRUE;
    return STATUS_SUCCESS;
}

static
BOOLEAN
KdbpRiscvStepInstruction(
    _In_ LONG Index,
    _Out_ PULONG Inst,
    _Out_ PLONG Length)
{
    KDB_RISCV_STEP *Step = &KdbpRiscvSteps[Index];
    USHORT High;

    ASSERT(Step->Armed);
    *Inst = Step->Saved;
    *Length = 2;
    if ((Step->Saved & 3) != 3)
        return TRUE;

    if (!NT_SUCCESS(KdbpRiscvCopyCode(Step->Address + sizeof(USHORT), &High, sizeof(High), FALSE)))
        return FALSE;
    *Inst |= (ULONG)High << 16;
    *Length = 4;
    return TRUE;
}

static
BOOLEAN
KdbpRiscvIsBreakInstruction(
    _In_ ULONG Inst,
    _In_ LONG Length)
{
    return (Length == 2) ? (Inst == KD_BREAKPOINT_VALUE) : (Inst == KDB_RISCV_EBREAK);
}

/* Moves code that reached the step's breakpoint past the instruction under it, and leaves the breakpoint for the stepping thread */
static
BOOLEAN
KdbpRiscvDisplace(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Inst,
    _In_ LONG Length)
{
    UCHAR Code[sizeof(ULONG) + KD_BREAKPOINT_SIZE];
    KD_BREAKPOINT_TYPE Break = KD_BREAKPOINT_VALUE;
    ULONG_PTR Slot;
    ULONG Index;

    ASSERT((Length == 2) || (Length == 4));
    ASSERT(!KdbpRiscvIsBreakInstruction(Inst, Length));

    if (KdbpRiscvEmulateRelative(Context, Inst, Length))
        return TRUE;

    if (Inst == KDB_RISCV_SRET)
        return FALSE;

    /* TODO: user-mode code has no slot to run in; the step is given up */
    if ((ULONG_PTR)Context->Pc < (ULONG_PTR)MmSystemRangeStart)
        return FALSE;

    for (Index = 0; Index < KDB_RISCV_DISPLACED_SLOTS; Index++)
    {
        if (!KdbpRiscvDisplaced[Index].InUse)
            break;
    }
    if (Index == KDB_RISCV_DISPLACED_SLOTS)
        return FALSE;

    RtlCopyMemory(Code, &Inst, Length);
    RtlCopyMemory(Code + Length, &Break, sizeof(Break));
    Slot = (ULONG_PTR)KdbpRiscvDisplacedCode + Index * KDB_RISCV_DISPLACED_SIZE;
    if (!NT_SUCCESS(KdbpRiscvCopyCode(Slot, Code, Length + sizeof(Break), TRUE)))
        return FALSE;

    /* FIXME: an exception raised by the moved instruction is dispatched from the slot, outside its function's handlers */
    KdbpRiscvDisplaced[Index].ResumePc = (ULONG_PTR)Context->Pc + Length;
    KdbpRiscvDisplaced[Index].Thread = KeGetCurrentThread();
    KdbpRiscvDisplaced[Index].Length = (ULONG)Length;
    KdbpRiscvDisplaced[Index].InUse = TRUE;
    Context->Pc = Slot;
    return TRUE;
}

static
BOOLEAN
KdbpRiscvFinishDisplaced(
    _Inout_ PCONTEXT Context)
{
    ULONG_PTR Base = (ULONG_PTR)KdbpRiscvDisplacedCode;
    ULONG_PTR Pc = (ULONG_PTR)Context->Pc;
    KDB_RISCV_DISPLACED *Displaced;
    ULONG Index;

    if ((Pc < Base) || (Pc >= Base + KDB_RISCV_DISPLACED_SLOTS * KDB_RISCV_DISPLACED_SIZE))
        return FALSE;

    Index = (ULONG)((Pc - Base) / KDB_RISCV_DISPLACED_SIZE);
    Displaced = &KdbpRiscvDisplaced[Index];
    if (!Displaced->InUse || (Pc != Base + Index * KDB_RISCV_DISPLACED_SIZE + Displaced->Length))
        return FALSE;

    ASSERT(Displaced->Thread == KeGetCurrentThread());
    ASSERT((Displaced->Length == 2) || (Displaced->Length == 4));
    Context->Pc = Displaced->ResumePc;
    Displaced->InUse = FALSE;
    return TRUE;
}

BOOLEAN
NTAPI
KdbpArchIsBreakInstruction(
    _In_ ULONG_PTR Address)
{
    USHORT Code[2];

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Code[0], (PVOID)Address, sizeof(Code[0]))))
        return TRUE;

    if ((Code[0] & 3) != 3)
        return Code[0] == KD_BREAKPOINT_VALUE;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Code[1], (PVOID)(Address + sizeof(USHORT)), sizeof(Code[1]))))
        return TRUE;

    return ((ULONG)Code[0] | ((ULONG)Code[1] << 16)) == KDB_RISCV_EBREAK;
}

KDB_STEP_HIT
NTAPI
KdbpArchTranslateStep(
    _In_opt_ PEXCEPTION_RECORD64 ExceptionRecord,
    _Inout_ PCONTEXT Context)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR Pc = (ULONG_PTR)Context->Pc;
    ULONG_PTR Address, Distance;
    KDB_RISCV_STEP *Step;
    ULONG Inst;
    LONG Index, Length;

    if ((ExceptionRecord == NULL) || (ExceptionRecord->ExceptionCode != (NTSTATUS)STATUS_BREAKPOINT))
        return KdbStepNotHit;

    if (KdbpRiscvFinishDisplaced(Context))
        return KdbStepForeignHit;

    Index = KdbpRiscvStepAt(Pc, -1);
    if (Index < 0)
        return KdbStepNotHit;

    Address = KdbpRiscvSteps[Index].Address;
    Context->Pc = Address;

    /* An interrupt runs on its own stack, and in the thread it interrupted */
    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpRiscvSteps); Index++)
    {
        Step = &KdbpRiscvSteps[Index];
        if (!Step->Armed || (Step->Address != Address) || (Step->Thread != Thread))
            continue;

        Distance = ((ULONG_PTR)Context->Sp > Step->Stack) ? (ULONG_PTR)Context->Sp - Step->Stack
                                                          : Step->Stack - (ULONG_PTR)Context->Sp;
        if (Distance < KERNEL_LARGE_STACK_SIZE)
        {
            KdbpRiscvDisarmStep(Index);
            return KdbStepDone;
        }
    }

    Index = KdbpRiscvStepAt(Address, -1);
    ASSERT(Index >= 0);
    if (!KdbpRiscvStepInstruction(Index, &Inst, &Length))
        return KdbStepNotHit;

    /* FIXME: a breakpoint of the debugger's own under the step is handled for the other thread, and can take the step's instruction back with it */
    if (KdbpRiscvIsBreakInstruction(Inst, Length))
        return KdbStepNotHit;

    if (KdbpRiscvDisplace(Context, Inst, Length))
        return KdbStepForeignHit;

    for (Index = 0; Index < (LONG)RTL_NUMBER_OF(KdbpRiscvSteps); Index++)
    {
        if (KdbpRiscvSteps[Index].Armed && (KdbpRiscvSteps[Index].Address == Address))
            KdbpRiscvDisarmStep(Index);
    }
    return KdbStepAbandoned;
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
    Context->Pc = Frame.Ra;
    Context->Ra = Frame.Ra;
    Context->Sp = (ULONG_PTR)KernelStack + sizeof(Frame);
    Context->S0 = Frame.S0;
    Context->S1 = Frame.S1;
    Context->S2 = Frame.S2;
    Context->S3 = Frame.S3;
    Context->S4 = Frame.S4;
    Context->S5 = Frame.S5;
    Context->S6 = Frame.S6;
    Context->S7 = Frame.S7;
    Context->S8 = Frame.S8;
    Context->S9 = Frame.S9;
    Context->S10 = Frame.S10;
    Context->S11 = Frame.S11;
    return TRUE;
}

/*
 * Whether stepping over the instruction at Address means running to the one
 * after it: a jump that links ra or t0, which is how a call is made here.
 */
BOOLEAN
NTAPI
KdbpArchIsStepOverInstruction(
    _In_ ULONG_PTR Address)
{
    USHORT Low;
    ULONG Instruction;
    ULONG Link;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Low, (PVOID)Address, sizeof(Low))))
        return FALSE;

    /* C.JALR: the one compressed call of RV64 */
    if ((Low & 3) != 3)
        return ((Low & 0xF07F) == 0x9002) && (((Low >> 7) & 0x1F) != 0);

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Instruction, (PVOID)Address, sizeof(Instruction))))
        return FALSE;

    Link = (Instruction >> 7) & 0x1F;
    if (((Instruction & 0x7F) == 0x6F) || (((Instruction & 0x7F) == 0x67) && (((Instruction >> 12) & 7) == 0)))
        return (Link == 1) || (Link == 5);

    return FALSE;
}

/* What the self-test asks the classification above about */
const KDB_STEP_SAMPLE KdbpArchStepSamples[] =
{
    {{0xEF, 0x00, 0x00, 0x00}, 4, TRUE, "jal ra"},
    {{0xE7, 0x80, 0x02, 0x00}, 4, TRUE, "jalr ra, t0"},
    {{0x82, 0x92}, 2, TRUE, "c.jalr t0"},
    {{0x6F, 0x00, 0x00, 0x00}, 4, FALSE, "j"},
    {{0x67, 0x80, 0x00, 0x00}, 4, FALSE, "ret"},
    {{0x82, 0x80}, 2, FALSE, "c.jr ra"},
    {{0x02, 0x90}, 2, FALSE, "c.ebreak"},
    {{0x13, 0x00, 0x00, 0x00}, 4, FALSE, "nop"}
};

const ULONG KdbpArchStepSampleCount = RTL_NUMBER_OF(KdbpArchStepSamples);

/* There are no watch registers driven here, so none can have fired */
LONG
NTAPI
KdbpArchHardwareBreakPointHit(
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    return -1;
}

/* The answer to a stop leaves the engine's symbol range as it is: there is nothing else here to keep */
VOID
NTAPI
KdbpArchPrepareContinue(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_opt_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    State->u.Continue2.ControlSet.CurrentSymbolStart = 1;
}

/* EOF */
