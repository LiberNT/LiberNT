/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 emulation of the unaligned loads and stores the processor refuses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kiarm.h"
#define NDEBUG
#include <debug.h>

/*
 * The processor does unaligned 16-bit and 32-bit accesses to normal memory by
 * itself. Load and store double, load and store multiple and the VFP loads
 * and stores fault when their address is not word aligned; Windows on ARM
 * carries those out for the program ("Overview of ARM ABI Conventions",
 * Alignment), which is done here a word at a time. A word access the
 * processor refuses as well, as it does on uncached memory, stays a fault.
 */

#define KI_ARM_ALIGN_WORDS 64
#define KI_ARM_REGISTER_SP 13
#define KI_ARM_REGISTER_PC 15

typedef struct _KI_ARM_UNALIGNED_WORD
{
    ULONG Value;
} __attribute__((packed)) KI_ARM_UNALIGNED_WORD;

typedef struct _KI_ARM_UNALIGNED_ACCESS
{
    ULONG Address;
    ULONG Words;
    ULONG Length;
    BOOLEAN Load;
    BOOLEAN Vfp;
    BOOLEAN Writeback;
    UCHAR Base;
    ULONG BaseValue;
    UCHAR Register[KI_ARM_ALIGN_WORDS];
} KI_ARM_UNALIGNED_ACCESS, *PKI_ARM_UNALIGNED_ACCESS;

static
ULONG
KiArmGetRegister(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Number)
{
    PKEXCEPTION_FRAME ExceptionFrame = &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame;

    ASSERT(Number <= KI_ARM_REGISTER_PC);
    switch (Number)
    {
        case 0: return TrapFrame->R0;
        case 1: return TrapFrame->R1;
        case 2: return TrapFrame->R2;
        case 3: return TrapFrame->R3;
        case 4: return ExceptionFrame->R4;
        case 5: return ExceptionFrame->R5;
        case 6: return ExceptionFrame->R6;
        case 7: return ExceptionFrame->R7;
        case 8: return ExceptionFrame->R8;
        case 9: return ExceptionFrame->R9;
        case 10: return ExceptionFrame->R10;
        case 11: return TrapFrame->R11;
        case 12: return TrapFrame->R12;
        case 13: return TrapFrame->Sp;
        case 14: return TrapFrame->Lr;
        default: return TrapFrame->Pc + 4;
    }
}

static
VOID
KiArmSetRegister(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Number,
    _In_ ULONG Value)
{
    PKEXCEPTION_FRAME ExceptionFrame = &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame;

    ASSERT(Number < KI_ARM_REGISTER_PC);
    switch (Number)
    {
        case 0: TrapFrame->R0 = Value; break;
        case 1: TrapFrame->R1 = Value; break;
        case 2: TrapFrame->R2 = Value; break;
        case 3: TrapFrame->R3 = Value; break;
        case 4: ExceptionFrame->R4 = Value; break;
        case 5: ExceptionFrame->R5 = Value; break;
        case 6: ExceptionFrame->R6 = Value; break;
        case 7: ExceptionFrame->R7 = Value; break;
        case 8: ExceptionFrame->R8 = Value; break;
        case 9: ExceptionFrame->R9 = Value; break;
        case 10: ExceptionFrame->R10 = Value; break;
        case 11: TrapFrame->R11 = Value; ExceptionFrame->R11 = Value; break;
        case 12: TrapFrame->R12 = Value; break;
        case 13: TrapFrame->Sp = Value; break;
        default: TrapFrame->Lr = Value; break;
    }
}

/* ITSTATE is split over CPSR bits 26:25 and 15:10; it moves on by one instruction (ARM ARM A2.5.2). */
static
VOID
KiArmAdvanceItState(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ULONG Cpsr = TrapFrame->Cpsr;
    ULONG State = ((Cpsr >> 25) & 3) | (((Cpsr >> 10) & 0x3F) << 2);

    State = (State & 7) ? ((State & 0xE0) | ((State << 1) & 0x1F)) : 0;
    Cpsr &= ~CPSR_IT_MASK;
    Cpsr |= ((State & 3) << 25) | ((State >> 2) << 10);
    TrapFrame->Cpsr = Cpsr;
}

static
BOOLEAN
KiArmReadCode(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Address,
    _Out_ PUSHORT Code)
{
    BOOLEAN Read = TRUE;

    _SEH2_TRY
    {
        if (KiIsUserModeTrap(TrapFrame))
            ProbeForRead((PVOID)(ULONG_PTR)Address, sizeof(USHORT), sizeof(USHORT));
        *Code = *(volatile USHORT *)(ULONG_PTR)Address;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Read = FALSE;
    }
    _SEH2_END;
    return Read;
}

static
VOID
KiArmListRegisters(
    _Inout_ PKI_ARM_UNALIGNED_ACCESS Access,
    _In_ ULONG List)
{
    ULONG Number;

    for (Number = 0; Number <= KI_ARM_REGISTER_PC; Number++)
    {
        if (List & (1UL << Number))
            Access->Register[Access->Words++] = (UCHAR)Number;
    }
}

/* What the faulting instruction transfers, when it is one of those carried out here (ARM ARM A8.8, Thumb encodings). */
static
BOOLEAN
KiArmDecodeUnalignedAccess(
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PKI_ARM_UNALIGNED_ACCESS Access)
{
    USHORT First, Second;
    ULONG Base, Offset, Value, Index, List;
    BOOLEAN Add, PreIndexed;

    RtlZeroMemory(Access, sizeof(*Access));
    Access->Length = sizeof(USHORT);

    if (!(TrapFrame->Cpsr & CPSR_THUMB) || !KiArmReadCode(TrapFrame, TrapFrame->Pc, &First))
        return FALSE;

    if ((First & 0xF000) == 0xC000)
    {
        /* LDM and STM, 16-bit: the base moves on unless a load reads it. */
        Base = (First >> 8) & 7;
        List = First & 0xFF;
        if (List == 0)
            return FALSE;
        Access->Load = (BOOLEAN)((First & 0x0800) != 0);
        KiArmListRegisters(Access, List);
        Access->Base = (UCHAR)Base;
        Access->Address = KiArmGetRegister(TrapFrame, Base);
        Access->Writeback = (BOOLEAN)(!Access->Load || !(List & (1UL << Base)));
        Access->BaseValue = Access->Address + Access->Words * sizeof(ULONG);
        return TRUE;
    }

    if ((First >> 11) < 0x1D)
        return FALSE;
    if (!KiArmReadCode(TrapFrame, TrapFrame->Pc + sizeof(USHORT), &Second))
        return FALSE;
    Access->Length = 2 * sizeof(USHORT);

    Base = First & 0xF;
    Value = (Base == KI_ARM_REGISTER_PC) ? ((TrapFrame->Pc + 4) & ~3UL) : KiArmGetRegister(TrapFrame, Base);
    Add = (BOOLEAN)((First & 0x0080) != 0);
    PreIndexed = (BOOLEAN)((First & 0x0100) != 0);
    Access->Base = (UCHAR)Base;
    Access->Load = (BOOLEAN)((First & 0x0010) != 0);

    if (((First & 0xFE40) == 0xE840) && (First & 0x0120))
    {
        /* LDRD and STRD: two words at the base, at the base and offset, or both with the base moved. */
        Offset = (Second & 0xFF) << 2;
        Access->Writeback = (BOOLEAN)((First & 0x0020) != 0);
        if ((Base == KI_ARM_REGISTER_PC) && (!Access->Load || !PreIndexed || Access->Writeback))
            return FALSE;
        Access->BaseValue = Add ? (Value + Offset) : (Value - Offset);
        Access->Address = PreIndexed ? Access->BaseValue : Value;
        Access->Register[0] = (UCHAR)(Second >> 12);
        Access->Register[1] = (UCHAR)((Second >> 8) & 0xF);
        Access->Words = 2;
        return TRUE;
    }

    if (((First & 0xFE40) == 0xE800) && ((((First >> 7) & 3) == 1) || (((First >> 7) & 3) == 2)))
    {
        /* LDM and STM, 32-bit: upwards from the base, or downwards to end below it. */
        List = Second;
        if ((Base == KI_ARM_REGISTER_PC) || (List & (1UL << KI_ARM_REGISTER_SP)) ||
            (!Access->Load && (List & (1UL << KI_ARM_REGISTER_PC))) || (List == 0))
        {
            return FALSE;
        }
        KiArmListRegisters(Access, List);
        Offset = Access->Words * sizeof(ULONG);
        Access->Writeback = (BOOLEAN)((First & 0x0020) != 0);
        if (((First >> 7) & 3) == 1)
        {
            Access->Address = Value;
            Access->BaseValue = Value + Offset;
        }
        else
        {
            Access->Address = Value - Offset;
            Access->BaseValue = Value - Offset;
        }
        if (Access->Load && (List & (1UL << Base)))
            Access->Writeback = FALSE;
        return TRUE;
    }

    if (((First & 0xFE00) != 0xEC00) || ((Second & 0x0E00) != 0x0A00))
        return FALSE;

    /* VFP: the registers are words of the D register file, S registers being its halves. */
    Access->Vfp = TRUE;
    Offset = (Second & 0xFF) << 2;
    Index = (Second & 0x0100) ? (((ULONG)(First & 0x0040) >> 2) | (Second >> 12)) << 1
                              : (((ULONG)(Second >> 12) << 1) | ((First >> 6) & 1));

    if (PreIndexed && !(First & 0x0020))
    {
        /* VLDR and VSTR */
        Access->Words = (Second & 0x0100) ? 2 : 1;
        Access->Address = Add ? (Value + Offset) : (Value - Offset);
    }
    else if ((!PreIndexed && Add) || (PreIndexed && !Add && (First & 0x0020)))
    {
        /* VLDM, VSTM, VPUSH and VPOP */
        Access->Words = Second & 0xFF;
        if ((Access->Words == 0) || ((Second & 0x0100) && (Access->Words & 1)) || (Base == KI_ARM_REGISTER_PC))
            return FALSE;
        Access->Writeback = (BOOLEAN)((First & 0x0020) != 0);
        Access->Address = Add ? Value : (Value - Offset);
        Access->BaseValue = Add ? (Value + Offset) : (Value - Offset);
    }
    else
    {
        return FALSE;
    }

    if (Index + Access->Words > KI_ARM_ALIGN_WORDS)
        return FALSE;
    for (Value = 0; Value < Access->Words; Value++)
        Access->Register[Value] = (UCHAR)(Index + Value);
    return TRUE;
}

/*
 * Carries out the load or store that raised an alignment fault. STATUS_SUCCESS
 * means the instruction is done and the program goes on after it,
 * STATUS_NOT_SUPPORTED that it is not one of those carried out here, and any
 * other status is what the memory access itself raised.
 */
NTSTATUS
NTAPI
KiArmEmulateUnalignedAccess(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PULONG FaultAddress,
    _Out_ PBOOLEAN Write)
{
    KI_ARM_UNALIGNED_ACCESS Access;
    ULONG Word[KI_ARM_ALIGN_WORDS];
    volatile KI_ARM_UNALIGNED_WORD *Memory;
    BOOLEAN User = KiIsUserModeTrap(TrapFrame);
    NTSTATUS Status = STATUS_SUCCESS;
    PULONG VfpWord;
    ULONG Index;

    *Write = FALSE;

    if (!KiArmDecodeUnalignedAccess(TrapFrame, &Access))
        return STATUS_NOT_SUPPORTED;

    ASSERT((Access.Words != 0) && (Access.Words <= KI_ARM_ALIGN_WORDS));

    /* The instruction read back has to be the one that faulted: unaligned, and reaching the address the fault names. */
    if (!(Access.Address & (sizeof(ULONG) - 1)) ||
        ((ULONG)(*FaultAddress - Access.Address) >= Access.Words * sizeof(ULONG)))
    {
        return STATUS_NOT_SUPPORTED;
    }

    *FaultAddress = Access.Address;
    *Write = !Access.Load;

    /* The stack pointer of kernel code is where its trap record ends, so it cannot be moved from here. */
    if (!User && Access.Writeback && (Access.Base == KI_ARM_REGISTER_SP))
        return STATUS_NOT_SUPPORTED;
    if (Access.Vfp && !TrapFrame->VfpState)
        return STATUS_NOT_SUPPORTED;
    VfpWord = Access.Vfp ? (PULONG)TrapFrame->VfpState->VfpD : NULL;

    if (!Access.Load)
    {
        for (Index = 0; Index < Access.Words; Index++)
        {
            Word[Index] = Access.Vfp ? VfpWord[Access.Register[Index]]
                                     : KiArmGetRegister(TrapFrame, Access.Register[Index]);
        }
    }

    Memory = (volatile KI_ARM_UNALIGNED_WORD *)(ULONG_PTR)Access.Address;
    _SEH2_TRY
    {
        if (User)
        {
            if (Access.Load)
                ProbeForRead((PVOID)(ULONG_PTR)Access.Address, Access.Words * sizeof(ULONG), sizeof(UCHAR));
            else
                ProbeForWrite((PVOID)(ULONG_PTR)Access.Address, Access.Words * sizeof(ULONG), sizeof(UCHAR));
        }

        for (Index = 0; Index < Access.Words; Index++)
        {
            if (Access.Load)
                Word[Index] = Memory[Index].Value;
            else
                Memory[Index].Value = Word[Index];
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        return Status;

    if (Access.Load)
    {
        /* A load of the program counter is a branch, which stays in Thumb state or is not done here. */
        if (!Access.Vfp && (Access.Register[Access.Words - 1] == KI_ARM_REGISTER_PC) && !(Word[Access.Words - 1] & 1))
            return STATUS_NOT_SUPPORTED;

        for (Index = 0; Index < Access.Words; Index++)
        {
            if (Access.Vfp)
                VfpWord[Access.Register[Index]] = Word[Index];
            else if (Access.Register[Index] != KI_ARM_REGISTER_PC)
                KiArmSetRegister(TrapFrame, Access.Register[Index], Word[Index]);
        }
    }

    if (Access.Writeback)
        KiArmSetRegister(TrapFrame, Access.Base, Access.BaseValue);

    if (Access.Load && !Access.Vfp && (Access.Register[Access.Words - 1] == KI_ARM_REGISTER_PC))
    {
        TrapFrame->Pc = Word[Access.Words - 1] & ~1UL;
        TrapFrame->Cpsr &= ~CPSR_IT_MASK;
    }
    else
    {
        TrapFrame->Pc += Access.Length;
        KiArmAdvanceItState(TrapFrame);
    }

    return STATUS_SUCCESS;
}
