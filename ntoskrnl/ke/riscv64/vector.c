/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V vector state: lazy first use and context switch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

#define KI_RISCV_VECTOR_REGISTERS    32

#define RISCV_OPCODE_LOAD_FP         0x07
#define RISCV_OPCODE_STORE_FP        0x27
#define RISCV_OPCODE_OP_V            0x57
#define RISCV_OPCODE_SYSTEM          0x73

typedef struct _KI_RISCV_VECTOR_AREA
{
    ULONG64 Vstart;
    ULONG64 Vtype;
    ULONG64 Vl;
    ULONG64 Vcsr;
    ULONG64 Active;
    ULONG64 LastProcessor;
    UCHAR Registers[ANYSIZE_ARRAY];
} KI_RISCV_VECTOR_AREA, *PKI_RISCV_VECTOR_AREA;

C_ASSERT(FIELD_OFFSET(KI_RISCV_VECTOR_AREA, Registers) == 48);

VOID NTAPI KiRiscvSaveVectorRegisters(_Out_ PKI_RISCV_VECTOR_AREA Area, _In_ ULONG64 VectorBytes);
VOID NTAPI KiRiscvRestoreVectorRegisters(_In_ PKI_RISCV_VECTOR_AREA Area, _In_ ULONG64 VectorBytes);
ULONG64 NTAPI KiRiscvReadVectorLength(VOID);

ULONG64 KiRiscvVectorLength;

static __inline VOID
KiRiscvSetVectorStatus(_In_ ULONG64 State)
{
    __asm__ __volatile__("csrc sstatus, %0\n\tcsrs sstatus, %1"
                         :: "r"(RISCV_SSTATUS_VS), "r"(State) : "memory");
}

ULONG64
NTAPI
KiRiscvReadVectorStatus(VOID)
{
    ULONG64 Status;

    __asm__ __volatile__("csrr %0, sstatus" : "=r"(Status));
    return Status & RISCV_SSTATUS_VS;
}

VOID
NTAPI
KiRiscvInitializeVector(VOID)
{
    if (!(KiRiscvProcessorFeatures.Flags & KI_RISCV_FEATURE_V))
        return;

    KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_INITIAL);
    KiRiscvVectorLength = KiRiscvReadVectorLength();
    KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_OFF);
}

PVOID
NTAPI
KiRiscvInitializeVectorArea(
    _Inout_ PKTHREAD Thread,
    _In_ PVOID StackTop)
{
    SIZE_T Size;
    PKI_RISCV_VECTOR_AREA Area;

    Size = ALIGN_UP_BY(FIELD_OFFSET(KI_RISCV_VECTOR_AREA, Registers) +
                       KI_RISCV_VECTOR_REGISTERS * KiRiscvVectorLength, 16);
    Area = (PKI_RISCV_VECTOR_AREA)((ULONG_PTR)StackTop - Size);
    RtlZeroMemory(Area, Size);
    Thread->StateSaveArea = Area;
    return Area;
}

VOID
NTAPI
KiRiscvSaveVectorState(_In_ PKTHREAD Thread)
{
    PKI_RISCV_VECTOR_AREA Area = Thread->StateSaveArea;

    if (KiRiscvVectorLength == 0)
        return;
    if ((Area != NULL) && (KiRiscvReadVectorStatus() == RISCV_SSTATUS_VS_DIRTY))
        KiRiscvSaveVectorRegisters(Area, KiRiscvVectorLength);
    KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_OFF);
}

/* A thread's own fence.i covers only the hart it ran on, so a migrated thread fences again. */
VOID
NTAPI
KiRiscvSynchronizeInstructionStream(_In_ PKTHREAD Thread)
{
    PKI_RISCV_VECTOR_AREA Area = Thread->StateSaveArea;
    ULONG64 Processor;

    if (Area == NULL)
        return;
    Processor = (ULONG64)KeGetCurrentPrcb()->Number + 1;
    if (Area->LastProcessor == Processor)
        return;
    if (Area->LastProcessor != 0)
        __asm__ __volatile__("fence.i" ::: "memory");
    Area->LastProcessor = Processor;
}

VOID
NTAPI
KiRiscvRestoreVectorState(_In_ PKTHREAD Thread)
{
    PKI_RISCV_VECTOR_AREA Area = Thread->StateSaveArea;

    if (KiRiscvVectorLength == 0)
        return;
    if ((Area != NULL) && Area->Active)
    {
        KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_INITIAL);
        KiRiscvRestoreVectorRegisters(Area, KiRiscvVectorLength);
        KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_CLEAN);
    }
    else
    {
        KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_OFF);
    }
}

static BOOLEAN
KiRiscvReadUserInstruction(
    _In_ ULONG_PTR Pc,
    _Out_ PULONG Instruction)
{
    USHORT Low, High;
    BOOLEAN Success = TRUE;

    _SEH2_TRY
    {
        ProbeForRead((PVOID)Pc, sizeof(USHORT), sizeof(USHORT));
        Low = *(volatile USHORT *)Pc;
        High = 0;
        if ((Low & 3) == 3)
        {
            ProbeForRead((PVOID)(Pc + 2), sizeof(USHORT), sizeof(USHORT));
            High = *(volatile USHORT *)(Pc + 2);
        }
        *Instruction = Low | ((ULONG)High << 16);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Success = FALSE;
    }
    _SEH2_END;
    return Success;
}

static BOOLEAN
KiRiscvIsVectorInstruction(_In_ ULONG Instruction)
{
    ULONG Opcode = Instruction & 0x7F;
    ULONG Width = (Instruction >> 12) & 7;
    ULONG Csr = Instruction >> 20;

    if ((Instruction & 3) != 3)
        return FALSE;
    if (Opcode == RISCV_OPCODE_OP_V)
        return TRUE;
    if ((Opcode == RISCV_OPCODE_LOAD_FP) || (Opcode == RISCV_OPCODE_STORE_FP))
        return (Width == 0) || (Width >= 5);
    if ((Opcode == RISCV_OPCODE_SYSTEM) && ((Width & 3) != 0))
    {
        return (Csr == 0x008) || (Csr == 0x009) || (Csr == 0x00A) || (Csr == 0x00F) ||
               (Csr == 0xC20) || (Csr == 0xC21) || (Csr == 0xC22);
    }
    return FALSE;
}

BOOLEAN
NTAPI
KiRiscvHandleVectorFirstUse(_Inout_ PKTRAP_FRAME TrapFrame)
{
    PKI_RISCV_VECTOR_AREA Area = KeGetCurrentThread()->StateSaveArea;
    ULONG Instruction;
    KIRQL OldIrql;

    if ((KiRiscvVectorLength == 0) || (Area == NULL) ||
        ((TrapFrame->Sstatus & RISCV_SSTATUS_VS) != RISCV_SSTATUS_VS_OFF))
    {
        return FALSE;
    }

    _enable();
    if (!KiRiscvReadUserInstruction(TrapFrame->Context.Pc, &Instruction) ||
        !KiRiscvIsVectorInstruction(Instruction))
    {
        _disable();
        return FALSE;
    }

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    if (!Area->Active)
    {
        Area->Active = TRUE;
        KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_INITIAL);
        KiRiscvRestoreVectorRegisters(Area, KiRiscvVectorLength);
        KiRiscvSetVectorStatus(RISCV_SSTATUS_VS_CLEAN);
    }
    KeLowerIrql(OldIrql);

    _disable();
    TrapFrame->Sstatus = (TrapFrame->Sstatus & ~RISCV_SSTATUS_VS) | RISCV_SSTATUS_VS_DIRTY;
    return TRUE;
}

VOID
NTAPI
KiRiscvRundownVectorState(_In_ PKTHREAD Thread)
{
    Thread->StateSaveArea = NULL;
}
