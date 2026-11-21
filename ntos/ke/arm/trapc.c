/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 trap dispatch: aborts, undefined instructions and debug services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kiarm.h"
#define NDEBUG
#include <debug.h>

/* Long-descriptor fault status (ARM ARM B3.13.6, TTBCR.EAE = 1). */
#define FSR_STATUS_MASK      0x3F
#define FSR_WNR              0x800
#define FSR_TRANSLATION(s)   (((s) & 0x3C) == 0x04)
#define FSR_ACCESS_FLAG(s)   (((s) & 0x3C) == 0x08)
#define FSR_PERMISSION(s)    (((s) & 0x3C) == 0x0C)
#define FSR_ALIGNMENT        0x21
#define FSR_DEBUG            0x22

/* Thumb UDF #imm8 encodings Windows on ARM uses for its traps. */
#define THUMB_UDF_MASK       0xFF00
#define THUMB_UDF            0xDE00
#define UDF_FASTFAIL         0xFB
#define UDF_ASSERT           0xFC
#define UDF_DEBUG_SERVICE    0xFD
#define UDF_BREAKPOINT       0xFE

static
VOID
KiArmRaiseException(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ NTSTATUS Code,
    _In_ ULONG Parameters,
    _In_ ULONG_PTR Parameter0,
    _In_ ULONG_PTR Parameter1,
    _In_ ULONG_PTR Parameter2)
{
    EXCEPTION_RECORD Record;
    KPROCESSOR_MODE Mode = KiIsUserModeTrap(TrapFrame) ? UserMode : KernelMode;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionCode = Code;
    Record.ExceptionAddress = (PVOID)(ULONG_PTR)TrapFrame->Pc;
    Record.NumberParameters = Parameters;
    Record.ExceptionInformation[0] = Parameter0;
    Record.ExceptionInformation[1] = Parameter1;
    Record.ExceptionInformation[2] = Parameter2;
    KiDispatchException(&Record, &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame, TrapFrame, Mode, TRUE);
}

static
VOID
KiArmHandleAbort(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Address,
    _In_ ULONG Status,
    _In_ BOOLEAN Instruction)
{
    KPROCESSOR_MODE Mode = KiIsUserModeTrap(TrapFrame) ? UserMode : KernelMode;
    ULONG Fsr = Status & FSR_STATUS_MASK;
    BOOLEAN Write = !Instruction && (Status & FSR_WNR);
    KIRQL Irql = KeGetCurrentIrql();
    ULONG FaultCode;
    NTSTATUS Result;

    TrapFrame->FaultStatus = Status;
    TrapFrame->FaultAddress = Address;

    if (Fsr == FSR_ALIGNMENT)
    {
        KiArmRaiseException(TrapFrame, STATUS_DATATYPE_MISALIGNMENT, 0, 0, 0, 0);
        return;
    }

    if (!FSR_TRANSLATION(Fsr) && !FSR_ACCESS_FLAG(Fsr) && !FSR_PERMISSION(Fsr))
    {
        KeBugCheckEx(KERNEL_DATA_INPAGE_ERROR, Status, Address, TrapFrame->Pc, Instruction);
    }

    FaultCode = (FSR_TRANSLATION(Fsr) ? 0 : MI_ARM_FAULT_PRESENT) |
                (Write ? MI_ARM_FAULT_WRITE : 0) |
                (Instruction ? MI_ARM_FAULT_EXECUTE : 0);

    if (!(TrapFrame->Cpsr & CPSR_IRQ_DISABLE))
        _enable();

    Result = MmAccessFault(FaultCode, (PVOID)(ULONG_PTR)Address, Mode, TrapFrame);
    if (NT_SUCCESS(Result))
    {
        KeInvalidateTlbEntry((PVOID)(ULONG_PTR)Address);
        return;
    }

    if (Irql > APC_LEVEL)
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, Address, Irql, Write, TrapFrame->Pc);

    if ((Result == STATUS_ACCESS_VIOLATION) || (Result == STATUS_GUARD_PAGE_VIOLATION) ||
        (Result == STATUS_STACK_OVERFLOW))
    {
        KiArmRaiseException(TrapFrame, Result, 2, Write ? 1 : (Instruction ? 8 : 0), Address, 0);
    }
    else
    {
        KiArmRaiseException(TrapFrame, STATUS_IN_PAGE_ERROR, 3, Write ? 1 : 0, Address, (ULONG_PTR)Result);
    }
}

static
BOOLEAN
KiArmReadInstruction(
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PUSHORT Instruction)
{
    BOOLEAN Read = TRUE;

    _SEH2_TRY
    {
        if (KiIsUserModeTrap(TrapFrame))
            ProbeForRead((PVOID)(ULONG_PTR)TrapFrame->Pc, sizeof(USHORT), sizeof(USHORT));
        *Instruction = *(volatile USHORT *)(ULONG_PTR)TrapFrame->Pc;
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
KiArmHandleUndefined(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    USHORT Instruction;

    /* LR is the address after the instruction: back up to report it. */
    TrapFrame->Pc -= (TrapFrame->Cpsr & CPSR_THUMB) ? 2 : 4;

    if (!(TrapFrame->Cpsr & CPSR_IRQ_DISABLE))
        _enable();

    if (!(TrapFrame->Cpsr & CPSR_THUMB) || !KiArmReadInstruction(TrapFrame, &Instruction) ||
        ((Instruction & THUMB_UDF_MASK) != THUMB_UDF))
    {
        KiArmRaiseException(TrapFrame, STATUS_ILLEGAL_INSTRUCTION, 0, 0, 0, 0);
        return;
    }

    switch (Instruction & 0xFF)
    {
        case UDF_BREAKPOINT:
            KiArmRaiseException(TrapFrame, STATUS_BREAKPOINT, 1, BREAKPOINT_BREAK, 0, 0);
            break;

        case UDF_DEBUG_SERVICE:
            KiArmRaiseException(TrapFrame, STATUS_BREAKPOINT, 3, TrapFrame->R0, TrapFrame->R1, TrapFrame->R2);
            break;

        case UDF_ASSERT:
            KiArmRaiseException(TrapFrame, STATUS_ASSERTION_FAILURE, 0, 0, 0, 0);
            break;

        case UDF_FASTFAIL:
            if (KiIsUserModeTrap(TrapFrame))
                KiArmRaiseException(TrapFrame, STATUS_STACK_BUFFER_OVERRUN, 1, TrapFrame->R0, 0, 0);
            else
                KeBugCheckEx(KERNEL_SECURITY_CHECK_FAILURE, TrapFrame->R0, (ULONG_PTR)TrapFrame, 0, 0);
            break;

        default:
            KiArmRaiseException(TrapFrame, STATUS_ILLEGAL_INSTRUCTION, 0, 0, 0, 0);
            break;
    }
}

/*
 * Common C entry of every exception vector. Runs on the SVC stack with
 * interrupts masked; the record below the trap frame holds the rest of the
 * interrupted state and is restored by the caller on return.
 */
VOID
NTAPI
KiArmTrapDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Vector)
{
    PKTHREAD Thread = KeGetCurrentThread();
    BOOLEAN User = KiIsUserModeTrap(TrapFrame);
    PKTRAP_FRAME PreviousTrapFrame = NULL;
    KPROCESSOR_MODE PreviousMode = KernelMode;

    if (Vector == KI_VECTOR_IRQ)
    {
        KiArmInterruptDispatch(TrapFrame);
    }
    else
    {
        if (Thread)
        {
            PreviousMode = Thread->PreviousMode;
            PreviousTrapFrame = Thread->TrapFrame;
            TrapFrame->TrapFrame = (ULONG)(ULONG_PTR)PreviousTrapFrame;
            TrapFrame->PreviousMode = PreviousMode;
            if (User || (Vector == KI_VECTOR_SVC))
            {
                Thread->PreviousMode = User ? UserMode : KernelMode;
                Thread->TrapFrame = TrapFrame;
            }
        }

        switch (Vector)
        {
            case KI_VECTOR_SVC:
                KiArmSystemServiceDispatch(TrapFrame);
                break;

            case KI_VECTOR_DABORT:
                KiArmHandleAbort(TrapFrame, KiArmReadCp15(6, 0, 0, 0), KiArmReadCp15(5, 0, 0, 0), FALSE);
                break;

            case KI_VECTOR_PABORT:
                KiArmHandleAbort(TrapFrame, KiArmReadCp15(6, 0, 0, 2), KiArmReadCp15(5, 0, 0, 1), TRUE);
                break;

            case KI_VECTOR_UNDEFINED:
                KiArmHandleUndefined(TrapFrame);
                break;

            default:
                KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP, Vector, (ULONG_PTR)TrapFrame, TrapFrame->Pc, 0);
        }
    }

    if (User)
        KiArmExitToUserMode(TrapFrame);

    _disable();
    if (Thread && (Vector != KI_VECTOR_IRQ))
    {
        Thread->PreviousMode = PreviousMode;
        if (User || (Vector == KI_VECTOR_SVC))
            Thread->TrapFrame = PreviousTrapFrame;
    }
}

ULONG_PTR
NTAPI
KeGetTrapFramePc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Pc;
}

BOOLEAN
NTAPI
KeGetTrapFrameInterruptState(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return (TrapFrame->Cpsr & CPSR_IRQ_DISABLE) == 0;
}

DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(ExceptionFrame == &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    if (KiIsUserModeTrap(TrapFrame))
        KiArmExitToUserMode(TrapFrame);
    _disable();
    if (Thread->TrapFrame == TrapFrame)
    {
        Thread->PreviousMode = TrapFrame->PreviousMode;
        Thread->TrapFrame = (PKTRAP_FRAME)(ULONG_PTR)TrapFrame->TrapFrame;
    }
    KiArmRestoreTrapFrame(TrapFrame);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiArmUnimplemented(
    _In_ const CHAR *Routine)
{
    DbgPrint("ARM: %s is not implemented\n", Routine);
    KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP, 0x41524D00, (ULONG_PTR)Routine, 0, 0);
}

/* A resumed user breakpoint continues after its UDF, as after an x86 int3. */
VOID
NTAPI
KiArmPrepareUserBreakpoint(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ASSERT(ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT);

    if ((ExceptionRecord->NumberParameters == 1) &&
        !(ExceptionRecord->ExceptionFlags & EXCEPTION_SOFTWARE_ORIGINATE) &&
        ((ULONG_PTR)ExceptionRecord->ExceptionAddress == (ULONG_PTR)TrapFrame->Pc))
    {
        TrapFrame->Pc += KD_BREAKPOINT_SIZE;
    }
}
