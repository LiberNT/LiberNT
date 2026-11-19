/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     KDBG RISC-V instruction display, lengths and branch targets
 */

#include <ntoskrnl.h>
#include "kdbg/kdb.h"
#include "riscv64-decode.h"
#define NDEBUG
#include <debug.h>

#define RISCV_OPCODE_OP_IMM    0x13
#define RISCV_OPCODE_AUIPC     0x17
#define RISCV_OPCODE_OP_IMM_32 0x1B
#define RISCV_OPCODE_AMO       0x2F
#define RISCV_OPCODE_OP        0x33
#define RISCV_OPCODE_LUI       0x37
#define RISCV_OPCODE_OP_32     0x3B
#define RISCV_OPCODE_BRANCH    0x63
#define RISCV_OPCODE_JALR      0x67
#define RISCV_OPCODE_JAL       0x6F
#define RISCV_OPCODE_SYSTEM    0x73
#define RISCV_INST_SRET        0x10200073UL
#define RISCV_AMO_LR           0x02
#define RISCV_AMO_SC           0x03
#define RISCV_ATOMIC_SEQUENCE_MAX 16

static LONG64
KdbpRiscvSignExtend(ULONG64 Value, ULONG Bits)
{
    ULONG Shift = 64 - Bits;

    return ((LONG64)(Value << Shift)) >> Shift;
}

static BOOLEAN
KdbpRiscvReadInstruction(ULONG_PTR Address, PULONG Instruction, PLONG Length)
{
    USHORT Low, High;

    *Instruction = 0;
    *Length = 0;
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Low, (PVOID)Address, sizeof(Low))))
        return FALSE;
    if ((Low & 3) != 3)
    {
        *Instruction = Low;
        *Length = 2;
        return TRUE;
    }
    if (!NT_SUCCESS(KdbpSafeReadMemory(&High, (PVOID)(Address + 2), sizeof(High))))
        return FALSE;
    *Instruction = (ULONG)Low | ((ULONG)High << 16);
    *Length = 4;
    return TRUE;
}

static LONG64
KdbpRiscvJalOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 31) & 1) << 20) |
                  (((Inst >> 21) & 0x3FF) << 1) |
                  (((Inst >> 20) & 1) << 11) |
                  (((Inst >> 12) & 0xFF) << 12);

    return KdbpRiscvSignExtend(Imm, 21);
}

static LONG64
KdbpRiscvBranchOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 31) & 1) << 12) |
                  (((Inst >> 7) & 1) << 11) |
                  (((Inst >> 25) & 0x3F) << 5) |
                  (((Inst >> 8) & 0xF) << 1);

    return KdbpRiscvSignExtend(Imm, 13);
}

static LONG64
KdbpRiscvCjOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 12) & 1) << 11) |
                  (((Inst >> 11) & 1) << 4) |
                  (((Inst >> 9) & 3) << 8) |
                  (((Inst >> 8) & 1) << 10) |
                  (((Inst >> 7) & 1) << 6) |
                  (((Inst >> 6) & 1) << 7) |
                  (((Inst >> 3) & 7) << 1) |
                  (((Inst >> 2) & 1) << 5);

    return KdbpRiscvSignExtend(Imm, 12);
}

static LONG64
KdbpRiscvCbOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 12) & 1) << 8) |
                  (((Inst >> 10) & 3) << 3) |
                  (((Inst >> 5) & 3) << 6) |
                  (((Inst >> 3) & 3) << 1) |
                  (((Inst >> 2) & 1) << 5);

    return KdbpRiscvSignExtend(Imm, 9);
}

static BOOLEAN
KdbpRiscvBranchTaken(ULONG Funct3, ULONG64 Rs1, ULONG64 Rs2)
{
    switch (Funct3)
    {
        case 0: return Rs1 == Rs2;
        case 1: return Rs1 != Rs2;
        case 4: return (LONG64)Rs1 < (LONG64)Rs2;
        case 5: return (LONG64)Rs1 >= (LONG64)Rs2;
        case 6: return Rs1 < Rs2;
        case 7: return Rs1 >= Rs2;
        default: return FALSE;
    }
}

/*!\brief Computes the address of the instruction executed after the one at
 *        Context->Pc, using the register state in Context.
 *
 * \retval TRUE   *NextPc holds the successor address.
 * \retval FALSE  The instruction cannot be stepped over (unreadable or
 *                trap return) and *Reason names why.
 */
static
BOOLEAN
KdbpRiscvNextPcOf(
    _In_ PCONTEXT Context,
    _In_ ULONG_PTR Pc,
    _In_ ULONG Inst,
    _In_ LONG Length,
    _Out_ PULONG_PTR NextPc,
    _Out_ PCSTR *Reason)
{
    ULONG Opcode, Funct3, Rs1, Rs2;

    ASSERT((Length == 2) || (Length == 4));
    *Reason = NULL;
    *NextPc = Pc + Length;
    if (Length == 2)
    {
        Funct3 = (Inst >> 13) & 7;
        switch (Inst & 3)
        {
            case 1:
                Rs1 = 8 + ((Inst >> 7) & 7);
                if (Funct3 == 5)
                    *NextPc = Pc + KdbpRiscvCjOffset(Inst);
                else if ((Funct3 == 6) && (Context->X[Rs1] == 0))
                    *NextPc = Pc + KdbpRiscvCbOffset(Inst);
                else if ((Funct3 == 7) && (Context->X[Rs1] != 0))
                    *NextPc = Pc + KdbpRiscvCbOffset(Inst);
                return TRUE;

            case 2:
                Rs1 = (Inst >> 7) & 31;
                Rs2 = (Inst >> 2) & 31;
                if ((Funct3 == 4) && (Rs2 == 0) && (Rs1 != 0))
                    *NextPc = (ULONG_PTR)Context->X[Rs1] & ~(ULONG_PTR)1;
                return TRUE;

            default:
                return TRUE;
        }
    }

    Opcode = Inst & 0x7F;
    Funct3 = (Inst >> 12) & 7;
    Rs1 = (Inst >> 15) & 31;
    Rs2 = (Inst >> 20) & 31;
    switch (Opcode)
    {
        case RISCV_OPCODE_JAL:
            *NextPc = Pc + KdbpRiscvJalOffset(Inst);
            return TRUE;

        case RISCV_OPCODE_JALR:
            *NextPc = ((ULONG_PTR)Context->X[Rs1] + KdbpRiscvSignExtend(Inst >> 20, 12)) & ~(ULONG_PTR)1;
            return TRUE;

        case RISCV_OPCODE_BRANCH:
            if (KdbpRiscvBranchTaken(Funct3, Context->X[Rs1], Context->X[Rs2]))
                *NextPc = Pc + KdbpRiscvBranchOffset(Inst);
            return TRUE;

        case RISCV_OPCODE_SYSTEM:
            if (Inst == RISCV_INST_SRET)
            {
                *Reason = "sret target is the trap-return frame";
                return FALSE;
            }
            return TRUE;

        default:
            return TRUE;
    }
}

static
BOOLEAN
KdbpRiscvIsReservation(
    _In_ ULONG Inst,
    _In_ LONG Length,
    _In_ ULONG Funct5)
{
    ULONG Width = (Inst >> 12) & 7;

    return (Length == 4) && ((Inst & 0x7F) == RISCV_OPCODE_AMO) && ((Inst >> 27) == Funct5) &&
           ((Width == 2) || (Width == 3));
}

static
BOOLEAN
KdbpRiscvIsPlainInteger(
    _In_ ULONG Inst,
    _In_ LONG Length)
{
    ULONG Funct3;

    if (Length == 2)
    {
        Funct3 = (Inst >> 13) & 7;
        switch (Inst & 3)
        {
            case 0:
                return Funct3 == 0;

            case 1:
                return Funct3 <= 4;

            default:
                return (Funct3 == 0) || ((Funct3 == 4) && (((Inst >> 2) & 31) != 0));
        }
    }

    switch (Inst & 0x7F)
    {
        case RISCV_OPCODE_OP_IMM:
        case RISCV_OPCODE_OP_IMM_32:
        case RISCV_OPCODE_OP:
        case RISCV_OPCODE_OP_32:
        case RISCV_OPCODE_LUI:
            return TRUE;

        default:
            return FALSE;
    }
}

static
BOOLEAN
KdbpRiscvConditionalTarget(
    _In_ ULONG_PTR Pc,
    _In_ ULONG Inst,
    _In_ LONG Length,
    _Out_ PULONG_PTR Target)
{
    *Target = 0;
    if (Length == 2)
    {
        if (((Inst & 3) != 1) || (((Inst >> 13) & 7) < 6))
            return FALSE;
        *Target = Pc + KdbpRiscvCbOffset(Inst);
        return TRUE;
    }

    if ((Inst & 0x7F) != RISCV_OPCODE_BRANCH)
        return FALSE;
    *Target = Pc + KdbpRiscvBranchOffset(Inst);
    return TRUE;
}

static
BOOLEAN
KdbpRiscvAtomicSequenceEnd(
    _In_ ULONG_PTR Start,
    _Out_ PULONG_PTR End)
{
    ULONG_PTR Exits[RISCV_ATOMIC_SEQUENCE_MAX];
    ULONG_PTR Pc = Start;
    ULONG_PTR Target;
    ULONG ExitCount = 0;
    ULONG Count, Index;
    ULONG Inst;
    LONG Length;

    *End = 0;
    if (!KdbpRiscvReadInstruction(Pc, &Inst, &Length) || !KdbpRiscvIsReservation(Inst, Length, RISCV_AMO_LR))
        return FALSE;

    for (Count = 1; Count < RISCV_ATOMIC_SEQUENCE_MAX; Count++)
    {
        Pc += Length;
        if (!KdbpRiscvReadInstruction(Pc, &Inst, &Length))
            return FALSE;

        if (KdbpRiscvIsReservation(Inst, Length, RISCV_AMO_SC))
        {
            Pc += Length;
            if (!KdbpRiscvReadInstruction(Pc, &Inst, &Length) ||
                !KdbpRiscvConditionalTarget(Pc, Inst, Length, &Target) ||
                (Target != Start))
            {
                return FALSE;
            }

            for (Index = 0; Index < ExitCount; Index++)
            {
                if (Exits[Index] != Pc + Length)
                    return FALSE;
            }

            *End = Pc + Length;
            return TRUE;
        }

        if (KdbpRiscvConditionalTarget(Pc, Inst, Length, &Target))
        {
            if (Target <= Pc)
                return FALSE;
            ASSERT(ExitCount < RTL_NUMBER_OF(Exits));
            Exits[ExitCount++] = Target;
            continue;
        }

        if (!KdbpRiscvIsPlainInteger(Inst, Length))
            return FALSE;
    }

    return FALSE;
}

BOOLEAN
KdbpRiscvGetNextPc(
    IN PCONTEXT Context,
    OUT PULONG_PTR NextPc,
    OUT PCSTR *Reason)
{
    ULONG_PTR Pc = (ULONG_PTR)Context->Pc;
    ULONG_PTR End;
    ULONG Inst;
    ULONG Index;
    LONG Length;

    *NextPc = 0;
    *Reason = NULL;
    if (!KdbpRiscvReadInstruction(Pc, &Inst, &Length))
    {
        *Reason = "instruction is not readable";
        return FALSE;
    }

    /* A breakpoint between a load-reserved and its store-conditional fails the store every time: the sequence is one step */
    if (KdbpRiscvAtomicSequenceEnd(Pc, &End))
    {
        *NextPc = End;
        return TRUE;
    }

    if (!KdbpRiscvNextPcOf(Context, Pc, Inst, Length, NextPc, Reason))
        return FALSE;

    for (Index = 0; (Index < RISCV_ATOMIC_SEQUENCE_MAX) && KdbpRiscvAtomicSequenceEnd(*NextPc, &End); Index++)
        *NextPc = End;
    return TRUE;
}

BOOLEAN
KdbpRiscvEmulateRelative(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Inst,
    _In_ LONG Length)
{
    ULONG_PTR Pc = (ULONG_PTR)Context->Pc;
    ULONG_PTR NextPc;
    PCSTR Reason;
    ULONG Link = 0;
    ULONG Rd = (Inst >> 7) & 31;
    ULONG Funct3;

    ASSERT((Length == 2) || (Length == 4));
    if (Length == 2)
    {
        Funct3 = (Inst >> 13) & 7;
        if (((Inst & 3) == 1) && (Funct3 >= 5))
            Link = 0;
        else if (((Inst & 3) == 2) && (Funct3 == 4) && (((Inst >> 2) & 31) == 0) && (Rd != 0))
            Link = ((Inst >> 12) & 1) ? 1 : 0;
        else
            return FALSE;
    }
    else
    {
        switch (Inst & 0x7F)
        {
            case RISCV_OPCODE_AUIPC:
                if (Rd != 0)
                    Context->X[Rd] = Pc + (ULONG64)KdbpRiscvSignExtend(Inst & 0xFFFFF000UL, 32);
                Context->Pc = Pc + 4;
                return TRUE;

            case RISCV_OPCODE_JAL:
            case RISCV_OPCODE_JALR:
                Link = Rd;
                break;

            case RISCV_OPCODE_BRANCH:
                break;

            default:
                return FALSE;
        }
    }

    if (!KdbpRiscvNextPcOf(Context, Pc, Inst, Length, &NextPc, &Reason))
        return FALSE;

    if (Link != 0)
        Context->X[Link] = Pc + Length;
    Context->Pc = NextPc;
    return TRUE;
}

LONG
KdbpGetInstLength(IN ULONG_PTR Address)
{
    ULONG Inst;
    LONG Length;

    if (!KdbpRiscvReadInstruction(Address, &Inst, &Length))
        return -1;
    return Length;
}

static KDB_RISCV_DECODE_STATE KdbpRiscvDisplayState;
static ULONG_PTR KdbpRiscvDisplayNext;

LONG
KdbpDisassemble(IN ULONG_PTR Address, IN ULONG IntelSyntax)
{
    KDB_RISCV_DECODED Decoded;
    ULONG Inst;
    LONG Length;

    UNREFERENCED_PARAMETER(IntelSyntax);

    if (!KdbpRiscvReadInstruction(Address, &Inst, &Length))
    {
        KdbpPrint("<unreadable>");
        return -1;
    }

    /* What a lui or auipc put in a register carries over only to the instruction right after it */
    if (Address != KdbpRiscvDisplayNext)
        KdbpRiscvDecodeReset(&KdbpRiscvDisplayState);
    KdbpRiscvDecode(Inst, (ULONG)Length, Address, &KdbpRiscvDisplayState, FALSE, &Decoded);
    KdbpRiscvDisplayNext = Address + Length;

    if (Length == 2)
        KdbpPrint("    %04lx  %s", Inst, Decoded.Text);
    else
        KdbpPrint("%08lx  %s", Inst, Decoded.Text);
    if (Decoded.HasTarget)
    {
        KdbPuts(" ");
        KdbSymPrintAddress((PVOID)(ULONG_PTR)Decoded.Target, NULL);
    }
    if (Decoded.HasAddress)
    {
        KdbpPrint(" # 0x%p ", (PVOID)(ULONG_PTR)Decoded.Address);
        KdbSymPrintAddress((PVOID)(ULONG_PTR)Decoded.Address, NULL);
    }
    return Length;
}

static
BOOLEAN
KdbpRiscvStepSelfTest(VOID)
{
    typedef struct _KDB_RISCV_EMULATE_TEST
    {
        ULONG Instruction;
        LONG Length;
        ULONG64 Pc;
        ULONG64 A0;
        ULONG64 A1;
        ULONG64 A4;
        ULONG64 NextPc;
        ULONG Link;
        ULONG64 LinkValue;
    } KDB_RISCV_EMULATE_TEST;
    static const KDB_RISCV_EMULATE_TEST Tests[] =
    {
        { 0x010000efUL, 4, 0x1000ULL, 0, 0, 0, 0x1010ULL, 1, 0x1004ULL },
        { 0x00850567UL, 4, 0x2000ULL, 0x3001ULL, 0, 0, 0x3008ULL, 10, 0x2004ULL },
        { 0x00b50463UL, 4, 0x1104eULL, 5, 5, 0, 0x11056ULL, 0, 0 },
        { 0x00b50463UL, 4, 0x1104eULL, 5, 6, 0, 0x11052ULL, 0, 0 },
        { 0xa009UL, 2, 0x1101cULL, 0, 0, 0, 0x1101eULL, 0, 0 },
        { 0x9702UL, 2, 0x13a90ULL, 0, 0, 0x5001ULL, 0x5000ULL, 1, 0x13a92ULL },
        { 0x8502UL, 2, 0x16b34ULL, 0x7000ULL, 0, 0, 0x7000ULL, 0, 0 },
        { 0xc519UL, 2, 0x11046ULL, 0, 0, 0, 0x11054ULL, 0, 0 },
        { 0xc519UL, 2, 0x11046ULL, 1, 0, 0, 0x11048ULL, 0, 0 },
        { 0x00000097UL, 4, 0x11012ULL, 0, 0, 0, 0x11016ULL, 1, 0x11012ULL },
        { 0x80000517UL, 4, 0x100000016ULL, 0, 0, 0, 0x10000001aULL, 10, 0x80000016ULL },
    };
    /* addi; lr.w; bne to the end; sc.w; bnez back to the lr.w; nop */
    static const USHORT Sequence[] = { 0x0685, 0x252f, 0x1606, 0x1563, 0x00b5, 0x25af, 0x1ae6, 0xf9f5, 0x0001 };
    /* lr.w; ld; sc.w; bnez back: a load inside is no sequence */
    static const USHORT NotSequence[] = { 0x252f, 0x1606, 0x611c, 0x25af, 0x1ae6, 0xf9fd, 0x0001 };
    const ULONG64 Sentinel = 0x5A5A5A5A5A5A5A5AULL;
    CONTEXT Context;
    ULONG_PTR NextPc;
    PCSTR Reason;
    BOOLEAN Passed = TRUE;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Tests); Index++)
    {
        RtlZeroMemory(&Context, sizeof(Context));
        Context.Pc = Tests[Index].Pc;
        Context.Ra = Sentinel;
        Context.A0 = Tests[Index].A0;
        Context.A1 = Tests[Index].A1;
        Context.A4 = Tests[Index].A4;
        if (!KdbpRiscvEmulateRelative(&Context, Tests[Index].Instruction, Tests[Index].Length) ||
            (Context.Pc != Tests[Index].NextPc) ||
            ((Tests[Index].Link != 0) && (Context.X[Tests[Index].Link] != Tests[Index].LinkValue)) ||
            ((Tests[Index].Link != 1) && (Context.Ra != Sentinel)))
        {
            KdbpPrint("riscv64 step: %08lx at %I64x runs to %I64x, expected %I64x\n",
                      Tests[Index].Instruction, Tests[Index].Pc, Context.Pc, Tests[Index].NextPc);
            Passed = FALSE;
        }
    }

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Pc = 0x1251cULL;
    if (KdbpRiscvEmulateRelative(&Context, 0x00c505b3UL, 4) || (Context.Pc != 0x1251cULL))
        Passed = FALSE;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Pc = (ULONG_PTR)&Sequence[1];
    if (!KdbpRiscvGetNextPc(&Context, &NextPc, &Reason) || (NextPc != (ULONG_PTR)&Sequence[8]))
        Passed = FALSE;

    Context.Pc = (ULONG_PTR)&Sequence[0];
    if (!KdbpRiscvGetNextPc(&Context, &NextPc, &Reason) || (NextPc != (ULONG_PTR)&Sequence[8]))
        Passed = FALSE;

    Context.Pc = (ULONG_PTR)&Sequence[3];
    Context.A0 = 1;
    if (!KdbpRiscvGetNextPc(&Context, &NextPc, &Reason) || (NextPc != (ULONG_PTR)&Sequence[8]))
        Passed = FALSE;

    Context.A0 = 0;
    if (!KdbpRiscvGetNextPc(&Context, &NextPc, &Reason) || (NextPc != (ULONG_PTR)&Sequence[5]))
        Passed = FALSE;

    Context.Pc = (ULONG_PTR)&NotSequence[0];
    if (!KdbpRiscvGetNextPc(&Context, &NextPc, &Reason) || (NextPc != (ULONG_PTR)&NotSequence[2]))
        Passed = FALSE;

    if (!Passed)
        KdbpPrint("riscv64 step: the emulation or the atomic sequence cases failed\n");
    return Passed;
}

BOOLEAN
KdbpDisassemblerSelfTest(VOID)
{
    typedef struct _KDB_RISCV_DISASM_TEST
    {
        ULONG Instruction;
        ULONG Length;
        ULONG64 Pc;
        PCSTR Text;
    } KDB_RISCV_DISASM_TEST;
    /* Each text is what gdb prints for the instruction; the extensions it does not decode follow */
    static const KDB_RISCV_DISASM_TEST Tests[] =
    {
        { 0x952eUL, 2, 0x1129cULL, "add\ta0,a0,a1" },
        { 0x00c505b3UL, 4, 0x20480ULL, "add\ta1,a0,a2" },
        { 0x7119UL, 2, 0x11000ULL, "addi\tsp,sp,-128" },
        { 0x02450513UL, 4, 0x1118cULL, "addi\ta0,a0,36" },
        { 0x2511UL, 2, 0x11202ULL, "addiw\ta0,a0,4" },
        { 0x0015851bUL, 4, 0x11834ULL, "addiw\ta0,a1,1" },
        { 0x9d2dUL, 2, 0x11376ULL, "addw\ta0,a0,a1" },
        { 0x00b5063bUL, 4, 0x18002fc0cULL, "addw\ta2,a0,a1" },
        { 0x06a5b52fUL, 4, 0x40e4bcULL, "amoadd.d.aqrl\ta0,a0,(a1)" },
        { 0x06a5a52fUL, 4, 0x208d0ULL, "amoadd.w.aqrl\ta0,a0,(a1)" },
        { 0x66a5b52fUL, 4, 0x510828ULL, "amoand.d.aqrl\ta0,a0,(a1)" },
        { 0x62a5b52fUL, 4, 0x656064ULL, "amoand.d.rl\ta0,a0,(a1)" },
        { 0x66b625afUL, 4, 0x20a18ULL, "amoand.w.aqrl\ta1,a1,(a2)" },
        { 0x44a5b52fUL, 4, 0x655fccULL, "amoor.d.aq\ta0,a0,(a1)" },
        { 0x46a5b52fUL, 4, 0x180037e40ULL, "amoor.d.aqrl\ta0,a0,(a1)" },
        { 0x46b625afUL, 4, 0x208c4ULL, "amoor.w.aqrl\ta1,a1,(a2)" },
        { 0x0ea5b52fUL, 4, 0x180008c52ULL, "amoswap.d.aqrl\ta0,a0,(a1)" },
        { 0x0ea5a52fUL, 4, 0x28172ULL, "amoswap.w.aqrl\ta0,a0,(a1)" },
        { 0x8d6dUL, 2, 0x11102ULL, "and\ta0,a0,a1" },
        { 0x00a77833UL, 4, 0x18000151eULL, "and\ta6,a4,a0" },
        { 0x890dUL, 2, 0x111c4ULL, "andi\ta0,a0,3" },
        { 0x40057513UL, 4, 0x12ac0ULL, "andi\ta0,a0,1024" },
        { 0x00000097UL, 4, 0x11012ULL, "auipc\tra,0x0" },
        { 0x00b50463UL, 4, 0x1104eULL, "beq\ta0,a1,0x11056" },
        { 0xc519UL, 2, 0x11046ULL, "beqz\ta0,0x11054" },
        { 0x14050d63UL, 4, 0x119bcULL, "beqz\ta0,0x11b16" },
        { 0x02b55163UL, 4, 0x1183aULL, "bge\ta0,a1,0x1185c" },
        { 0x00b57663UL, 4, 0x114d8ULL, "bgeu\ta0,a1,0x114e4" },
        { 0x00055663UL, 4, 0x1189eULL, "bgez\ta0,0x118aa" },
        { 0x02b54163UL, 4, 0x11876ULL, "blt\ta0,a1,0x11898" },
        { 0x06b56263UL, 4, 0x110beULL, "bltu\ta0,a1,0x11122" },
        { 0x04054663UL, 4, 0x1273eULL, "bltz\ta0,0x1278a" },
        { 0x00b51763UL, 4, 0x111faULL, "bne\ta0,a1,0x11208" },
        { 0xe119UL, 2, 0x1101aULL, "bnez\ta0,0x11020" },
        { 0x28051a63UL, 4, 0x144aeULL, "bnez\ta0,0x14742" },
        { 0x14453073UL, 4, 0x20abeULL, "csrc\tsip,a0" },
        { 0x10017073UL, 4, 0x26008ULL, "csrci\tsstatus,2" },
        { 0xda002573UL, 4, 0x20a9aULL, "csrr\ta0,scountovf" },
        { 0x100172f3UL, 4, 0x401008ULL, "csrrci\tt0,sstatus,2" },
        { 0x14011173UL, 4, 0x7c2784ULL, "csrrw\tsp,sscratch,sp" },
        { 0x1002a073UL, 4, 0x401018ULL, "csrs\tsstatus,t0" },
        { 0x10016073UL, 4, 0x1512eULL, "csrsi\tsstatus,2" },
        { 0x14d51073UL, 4, 0x225aaULL, "csrw\tstimecmp,a0" },
        { 0x02b54533UL, 4, 0x180035fc4ULL, "div\ta0,a0,a1" },
        { 0x02b55533UL, 4, 0x12770ULL, "divu\ta0,a0,a1" },
        { 0x02c5553bUL, 4, 0x18000b966ULL, "divuw\ta0,a0,a2" },
        { 0x02b5453bUL, 4, 0x18001bf5aULL, "divw\ta0,a0,a1" },
        { 0x9002UL, 2, 0x13c2aULL, "ebreak" },
        { 0x00000073UL, 4, 0x223c2ULL, "ecall" },
        { 0x22f7a753UL, 4, 0x28a30ULL, "fabs.d\tfa4,fa5" },
        { 0x02f7f7d3UL, 4, 0x29a5aULL, "fadd.d\tfa5,fa5,fa5" },
        { 0xe2079553UL, 4, 0x29a4cULL, "fclass.d\ta0,fa5" },
        { 0xd22527d3UL, 4, 0x28a42ULL, "fcvt.d.l\tfa5,a0,rdn" },
        { 0xd23577d3UL, 4, 0x28b50ULL, "fcvt.d.lu\tfa5,a0" },
        { 0x420786d3UL, 4, 0x18009e24aULL, "fcvt.d.s\tfa3,fa5" },
        { 0xd20507d3UL, 4, 0x289ecULL, "fcvt.d.w\tfa5,a0" },
        { 0xd21507d3UL, 4, 0x18009b344ULL, "fcvt.d.wu\tfa5,a0" },
        { 0xc2272553UL, 4, 0x28a3eULL, "fcvt.l.d\ta0,fa4,rdn" },
        { 0xc2379553UL, 4, 0x28a5eULL, "fcvt.lu.d\ta0,fa5,rtz" },
        { 0x4017f7d3UL, 4, 0x18009e278ULL, "fcvt.s.d\tfa5,fa5" },
        { 0xd01577d3UL, 4, 0x49da04ULL, "fcvt.s.wu\tfa5,a0" },
        { 0xc207a553UL, 4, 0x28970ULL, "fcvt.w.d\ta0,fa5,rdn" },
        { 0xc0179553UL, 4, 0x49da5eULL, "fcvt.wu.s\ta0,fa5,rtz" },
        { 0x1ae7f7d3UL, 4, 0x28aa8ULL, "fdiv.d\tfa5,fa5,fa4" },
        { 0x0330000fUL, 4, 0x11984ULL, "fence\trw,rw" },
        { 0x0000100fUL, 4, 0x24cb8ULL, "fence.i" },
        { 0xa2e7a553UL, 4, 0x28920ULL, "feq.d\ta0,fa5,fa4" },
        { 0x211cUL, 2, 0x28900ULL, "fld\tfa5,0(a0)" },
        { 0x1102b007UL, 4, 0x1800724e8ULL, "fld\tft0,272(t0)" },
        { 0xa2e78553UL, 4, 0x28936ULL, "fle.d\ta0,fa5,fa4" },
        { 0xa2e795d3UL, 4, 0x2897cULL, "flt.d\ta1,fa5,fa4" },
        { 0xa0f71553UL, 4, 0x49da24ULL, "flt.s\ta0,fa4,fa5" },
        { 0x02812787UL, 4, 0x18009e230ULL, "flw\tfa5,40(sp)" },
        { 0x6ae7f7c3UL, 4, 0x28a20ULL, "fmadd.d\tfa5,fa5,fa4,fa3" },
        { 0x68e7f7c3UL, 4, 0x49dc78ULL, "fmadd.s\tfa5,fa5,fa4,fa3" },
        { 0x6ae7f7c7UL, 4, 0x18004d70aULL, "fmsub.d\tfa5,fa5,fa4,fa3" },
        { 0x12e7f7d3UL, 4, 0x289f2ULL, "fmul.d\tfa5,fa5,fa4" },
        { 0x10e7f7d3UL, 4, 0x49da14ULL, "fmul.s\tfa5,fa5,fa4" },
        { 0xf2000753UL, 4, 0x2891cULL, "fmv.d.x\tfa4,zero" },
        { 0xf00887d3UL, 4, 0x18009e210ULL, "fmv.w.x\tfa5,a7" },
        { 0xe20785d3UL, 4, 0x289feULL, "fmv.x.d\ta1,fa5" },
        { 0xe0078553UL, 4, 0x18009e27cULL, "fmv.x.w\ta0,fa5" },
        { 0x22f797d3UL, 4, 0x29faaULL, "fneg.d\tfa5,fa5" },
        { 0x62d7774bUL, 4, 0x2a292ULL, "fnmsub.d\tfa4,fa4,fa3,fa2" },
        { 0x003022f3UL, 4, 0x18005a2b8ULL, "frcsr\tt0" },
        { 0x00102573UL, 4, 0x18009e2fcULL, "frflags\ta0" },
        { 0x00339073UL, 4, 0x1800724e4ULL, "fscsr\tt2" },
        { 0xb13eUL, 2, 0x28902ULL, "fsd\tfa5,160(sp)" },
        { 0x10053827UL, 4, 0x18005a238ULL, "fsd\tft0,272(a0)" },
        { 0x00151073UL, 4, 0x18009e30cULL, "fsflags\ta0" },
        { 0x22e787d3UL, 4, 0x28a46ULL, "fsgnj.d\tfa5,fa5,fa4" },
        { 0x0af7f753UL, 4, 0x29a64ULL, "fsub.d\tfa4,fa5,fa5" },
        { 0x00e12c27UL, 4, 0x18009e226ULL, "fsw\tfa4,24(sp)" },
        { 0xa009UL, 2, 0x1101cULL, "j\t0x1101e" },
        { 0x1c30006fUL, 4, 0x19b9eULL, "j\t0x1a560" },
        { 0x9702UL, 2, 0x13a90ULL, "jalr\ta4" },
        { 0x8502UL, 2, 0x16b34ULL, "jr\ta0" },
        { 0x000e0067UL, 4, 0x2b330ULL, "jr\tt3" },
        { 0x00050503UL, 4, 0x11276ULL, "lb\ta0,0(a0)" },
        { 0x02f14503UL, 4, 0x111d0ULL, "lbu\ta0,47(sp)" },
        { 0x7526UL, 2, 0x1100cULL, "ld\ta0,104(sp)" },
        { 0xfd843503UL, 4, 0x138c8ULL, "ld\ta0,-40(s0)" },
        { 0x00059583UL, 4, 0x12b08ULL, "lh\ta1,0(a1)" },
        { 0x02015503UL, 4, 0x110c4ULL, "lhu\ta0,32(sp)" },
        { 0x55fdUL, 2, 0x11068ULL, "li\ta1,-1" },
        { 0x20800593UL, 4, 0x110baULL, "li\ta1,520" },
        { 0x1406b52fUL, 4, 0x180059e7eULL, "lr.d.aq\ta0,(a3)" },
        { 0x1606352fUL, 4, 0x180001eecULL, "lr.d.aqrl\ta0,(a2)" },
        { 0x1606252fUL, 4, 0x28140ULL, "lr.w.aqrl\ta0,(a2)" },
        { 0x6505UL, 2, 0x110f4ULL, "lui\ta0,0x1" },
        { 0xd00e05b7UL, 4, 0x11b62ULL, "lui\ta1,0xd00e0" },
        { 0x5552UL, 2, 0x1104aULL, "lw\ta0,52(sp)" },
        { 0x0a052503UL, 4, 0x12a1eULL, "lw\ta0,160(a0)" },
        { 0x01056503UL, 4, 0x111c0ULL, "lwu\ta0,16(a0)" },
        { 0x02c585b3UL, 4, 0x119acULL, "mul\ta1,a1,a2" },
        { 0x02b51533UL, 4, 0x18004bf04ULL, "mulh\ta0,a0,a1" },
        { 0x02b52533UL, 4, 0x18004e342ULL, "mulhsu\ta0,a0,a1" },
        { 0x02b53533UL, 4, 0x123beULL, "mulhu\ta0,a0,a1" },
        { 0x02d6063bUL, 4, 0x1170eULL, "mulw\ta2,a2,a3" },
        { 0x85aaUL, 2, 0x1102cULL, "mv\ta1,a0" },
        { 0x00058593UL, 4, 0x1251cULL, "mv\ta1,a1" },
        { 0x40c0063bUL, 4, 0x180022136ULL, "negw\ta2,a2" },
        { 0x0001UL, 2, 0x26032ULL, "nop" },
        { 0x00000013UL, 4, 0x180020fbcULL, "nop" },
        { 0xfff54513UL, 4, 0x17c9cULL, "not\ta0,a0" },
        { 0x8dd1UL, 2, 0x11a56ULL, "or\ta1,a1,a2" },
        { 0x00b56733UL, 4, 0x29a74ULL, "or\ta4,a0,a1" },
        { 0x04056513UL, 4, 0x16c10ULL, "ori\ta0,a0,64" },
        { 0xc0002573UL, 4, 0x6454a0ULL, "rdcycle\ta0" },
        { 0xc0102573UL, 4, 0x22634ULL, "rdtime\ta0" },
        { 0x02b56533UL, 4, 0x1800363ccULL, "rem\ta0,a0,a1" },
        { 0x02c57533UL, 4, 0x12902ULL, "remu\ta0,a0,a2" },
        { 0x02d6763bUL, 4, 0x180048802ULL, "remuw\ta2,a2,a3" },
        { 0x02c5e5bbUL, 4, 0x18001bee6ULL, "remw\ta1,a1,a2" },
        { 0x8082UL, 2, 0x1113aULL, "ret" },
        { 0x00008067UL, 4, 0x180020fb8ULL, "ret" },
        { 0x02a107a3UL, 4, 0x11168ULL, "sb\ta0,47(sp)" },
        { 0x18e6b62fUL, 4, 0x180059e86ULL, "sc.d\ta2,a4,(a3)" },
        { 0x1ae635afUL, 4, 0x180001ef4ULL, "sc.d.rl\ta1,a4,(a2)" },
        { 0x1ae625afUL, 4, 0x28148ULL, "sc.w.rl\ta1,a4,(a2)" },
        { 0xfc86UL, 2, 0x11002ULL, "sd\tra,120(sp)" },
        { 0x4e113423UL, 4, 0x138a2ULL, "sd\tra,1256(sp)" },
        { 0x00153513UL, 4, 0x111c6ULL, "seqz\ta0,a0" },
        { 0x2501UL, 2, 0x1122aULL, "sext.w\ta0,a0" },
        { 0x0005071bUL, 4, 0x180008538ULL, "sext.w\ta4,a0" },
        { 0x12000073UL, 4, 0x2b302ULL, "sfence.vma" },
        { 0x00b61023UL, 4, 0x12b0eULL, "sh\ta1,0(a2)" },
        { 0x00b51533UL, 4, 0x12dd4ULL, "sll\ta0,a0,a1" },
        { 0x1552UL, 2, 0x110c8ULL, "slli\ta0,a0,0x34" },
        { 0x02051593UL, 4, 0x12256ULL, "slli\ta1,a0,0x20" },
        { 0x0185151bUL, 4, 0x11d0aULL, "slliw\ta0,a0,0x18" },
        { 0x00b515bbUL, 4, 0x1d33eULL, "sllw\ta1,a0,a1" },
        { 0x00b52533UL, 4, 0x180046a8cULL, "slt\ta0,a0,a1" },
        { 0x03a52513UL, 4, 0x29024ULL, "slti\ta0,a0,58" },
        { 0x0085b593UL, 4, 0x122aaULL, "sltiu\ta1,a1,8" },
        { 0x00b53533UL, 4, 0x11db0ULL, "sltu\ta0,a0,a1" },
        { 0x00a03533UL, 4, 0x112e0ULL, "snez\ta0,a0" },
        { 0x40b55533UL, 4, 0x28546ULL, "sra\ta0,a0,a1" },
        { 0x8509UL, 2, 0x24ac8ULL, "srai\ta0,a0,0x2" },
        { 0x41455593UL, 4, 0x2a9acULL, "srai\ta1,a0,0x14" },
        { 0x4085551bUL, 4, 0x19e68ULL, "sraiw\ta0,a0,0x8" },
        { 0x40c5553bUL, 4, 0x29f06ULL, "sraw\ta0,a0,a2" },
        { 0x10200073UL, 4, 0x7c2a72ULL, "sret" },
        { 0x00b55533UL, 4, 0x18d66ULL, "srl\ta0,a0,a1" },
        { 0x9181UL, 2, 0x11632ULL, "srli\ta1,a1,0x20" },
        { 0x02055593UL, 4, 0x11844ULL, "srli\ta1,a0,0x20" },
        { 0x00c5551bUL, 4, 0x12cbeULL, "srliw\ta0,a0,0xc" },
        { 0x00b5553bUL, 4, 0x18e0eULL, "srlw\ta0,a0,a1" },
        { 0x8d11UL, 2, 0x11572ULL, "sub\ta0,a0,a2" },
        { 0x40b505b3UL, 4, 0x1229eULL, "sub\ta1,a0,a1" },
        { 0x9d91UL, 2, 0x11530ULL, "subw\ta1,a1,a2" },
        { 0x40b5063bUL, 4, 0x137b2ULL, "subw\ta2,a0,a1" },
        { 0xde2eUL, 2, 0x11030ULL, "sw\ta1,60(sp)" },
        { 0x0ac52023UL, 4, 0x12c60ULL, "sw\ta2,160(a0)" },
        { 0x0000UL, 2, 0x122deULL, "unimp" },
        { 0x10500073UL, 4, 0x25cf8ULL, "wfi" },
        { 0x8d2dUL, 2, 0x141e4ULL, "xor\ta0,a0,a1" },
        { 0x00f54633UL, 4, 0x180013164ULL, "xor\ta2,a0,a5" },
        { 0x00154513UL, 4, 0x11db4ULL, "xori\ta0,a0,1" },
        { 0x0ff57513UL, 4, 0x1c390ULL, "zext.b\ta0,a0" },
        { 0xe2850027UL, 4, 0x4073c0ULL, "vs8r.v\tv0,(a0)" },
        { 0xe2838007UL, 4, 0x4073e4ULL, "vl8re8.v\tv0,(t2)" },
        { 0x8062f057UL, 4, 0x407402ULL, "vsetvl\tzero,t0,t1" },
        { 0x60151513UL, 4, 0x26c9aULL, "ctz\ta0,a0" },
        { 0x0025200fUL, 4, 0x6431d6ULL, "cbo.flush\t0(a0)" },
        { 0x0015200fUL, 4, 0x6431deULL, "cbo.clean\t0(a0)" },
        { 0x0ff0000fUL, 4, 0x1000ULL, "fence" },
        { 0x0000UL, 2, 0x1000ULL, "unimp" },
        { 0xffffffffUL, 4, 0x1000ULL, ".insn\t4, 0xffffffff" }
    };
    /* auipc then jalr names a call target; a register rewritten in between names nothing */
    static const ULONG CallSequence[2] = { 0x0023f097UL, 0xcfe080e7UL };
    static const ULONG StaleSequence[3] = { 0x80000537UL, 0x03013503UL, 0x0dc52503UL };
    KDB_RISCV_DECODE_STATE State;
    KDB_RISCV_DECODED Decoded;
    BOOLEAN Passed = TRUE;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Tests); Index++)
    {
        KdbpRiscvDecodeReset(&State);
        KdbpRiscvDecode(Tests[Index].Instruction, Tests[Index].Length, Tests[Index].Pc, &State, FALSE, &Decoded);
        if (strcmp(Decoded.Text, Tests[Index].Text) != 0)
        {
            KdbpPrint("riscv64 disassembler: %08lx gives \"%s\", expected \"%s\"\n",
                      Tests[Index].Instruction, Decoded.Text, Tests[Index].Text);
            Passed = FALSE;
        }
    }

    KdbpRiscvDecodeReset(&State);
    KdbpRiscvDecode(CallSequence[0], 4, 0x401024ULL, &State, FALSE, &Decoded);
    KdbpRiscvDecode(CallSequence[1], 4, 0x401028ULL, &State, FALSE, &Decoded);
    if (!Decoded.HasAddress || Decoded.Address != 0x63fd22ULL || strcmp(Decoded.Text, "jalr\t-770(ra)") != 0)
        Passed = FALSE;

    KdbpRiscvDecodeReset(&State);
    for (Index = 0; Index < RTL_NUMBER_OF(StaleSequence); Index++)
        KdbpRiscvDecode(StaleSequence[Index], 4, 0x1000ULL + Index * 4, &State, FALSE, &Decoded);
    if (Decoded.HasAddress)
        Passed = FALSE;

    if (KdbpRiscvJalOffset(0x0100006F) != 16 || KdbpRiscvBranchOffset(0xFE208CE3) != -8 ||
        KdbpRiscvCjOffset(0xBFFD) != -2 || KdbpRiscvCbOffset(0xC011) != 4)
        Passed = FALSE;

    if (!KdbpRiscvStepSelfTest())
        Passed = FALSE;
    return Passed;
}
