/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 IDT setup, user exception frames, trap fixups and raise services
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern KI_INTERRUPT_DISPATCH_ENTRY KiUnexpectedRange[256];

KIDT_INIT KiInterruptInitTable[] =
{
    {0x00, 0x00, 0x00, KiDivideErrorFault},
    {0x01, 0x00, 0x00, KiDebugTrapOrFault},
    {0x02, 0x00, 0x03, KiNmiInterrupt},
    {0x03, 0x03, 0x00, KiBreakpointTrap},
    {0x04, 0x03, 0x00, KiOverflowTrap},
    {0x05, 0x00, 0x00, KiBoundFault},
    {0x06, 0x00, 0x00, KiInvalidOpcodeFault},
    {0x07, 0x00, 0x00, KiNpxNotAvailableFault},
    {0x08, 0x00, 0x01, KiDoubleFaultAbort},
    {0x09, 0x00, 0x00, KiNpxSegmentOverrunAbort},
    {0x0A, 0x00, 0x00, KiInvalidTssFault},
    {0x0B, 0x00, 0x00, KiSegmentNotPresentFault},
    {0x0C, 0x00, 0x00, KiStackFault},
    {0x0D, 0x00, 0x00, KiGeneralProtectionFault},
    {0x0E, 0x00, 0x00, KiPageFault},
    {0x10, 0x00, 0x00, KiFloatingErrorFault},
    {0x11, 0x00, 0x00, KiAlignmentFault},
    {0x12, 0x00, 0x02, KiMcheckAbort},
    {0x13, 0x00, 0x00, KiXmmException},
    {0x1F, 0x00, 0x00, KiApcInterrupt},
    {0x29, 0x03, 0x00, KiRaiseSecurityCheckFailure},
    {0x2C, 0x03, 0x00, KiRaiseAssertion},
    {0x2D, 0x03, 0x00, KiDebugServiceTrap},
    {0x2F, 0x00, 0x00, KiDpcInterrupt},
    {0xE1, 0x00, 0x00, KiIpiInterrupt},
    {0, 0, 0, 0}
};

KIDTENTRY64 KiIdt[256];
KDESCRIPTOR KiIdtDescriptor = {{0}, sizeof(KiIdt) - 1, KiIdt};

CODE_SEG("INIT")
VOID
NTAPI
KeInitExceptions(VOID)
{
    int i, j;

    for (j = i = 0; i < 256; i++)
    {
        ULONG64 Offset;

        ASSERT(j < (int)RTL_NUMBER_OF(KiInterruptInitTable));
        ASSERT(KiInterruptInitTable[j].ServiceRoutine == NULL || KiInterruptInitTable[j].InterruptId >= i);
        if (KiInterruptInitTable[j].InterruptId == i)
        {
            ASSERT(KiInterruptInitTable[j].ServiceRoutine != NULL);
            ASSERT(KiInterruptInitTable[j].Dpl <= 3);
            ASSERT(KiInterruptInitTable[j].IstIndex <= 7);
            Offset = (ULONG64)KiInterruptInitTable[j].ServiceRoutine;
            KiIdt[i].Dpl = KiInterruptInitTable[j].Dpl;
            KiIdt[i].IstIndex = KiInterruptInitTable[j].IstIndex;
            j++;
        }
        else
        {
            Offset = (ULONG64)&KiUnexpectedRange[i]._Op_push;
            KiIdt[i].Dpl = 0;
            KiIdt[i].IstIndex = 0;
        }
        KiIdt[i].OffsetLow = Offset & 0xffff;
        KiIdt[i].Selector = KGDT64_R0_CODE;
        KiIdt[i].Type = 0x0e;
        KiIdt[i].Reserved0 = 0;
        KiIdt[i].Present = 1;
        KiIdt[i].OffsetMiddle = (Offset >> 16) & 0xffff;
        KiIdt[i].OffsetHigh = (Offset >> 32);
        KiIdt[i].Reserved1 = 0;
    }

    ASSERT(j == (int)RTL_NUMBER_OF(KiInterruptInitTable) - 1);
    ASSERT(KiInterruptInitTable[j].ServiceRoutine == NULL);
    KeGetPcr()->IdtBase = KiIdt;
    __lidt(&KiIdtDescriptor.Limit);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiRaiseSecurityCheckFailureHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    EXCEPTION_RECORD ExceptionRecord;

    ExceptionRecord.ExceptionCode = STATUS_STACK_BUFFER_OVERRUN;
    ExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    ExceptionRecord.ExceptionRecord = NULL;
    ExceptionRecord.ExceptionAddress = (PVOID)TrapFrame->Rip;
    ExceptionRecord.NumberParameters = 1;
    ExceptionRecord.ExceptionInformation[0] = TrapFrame->Rcx;

    KeBugCheckWithTf(KERNEL_SECURITY_CHECK_FAILURE,
                     TrapFrame->Rcx,
                     (ULONG_PTR)TrapFrame,
                     (ULONG_PTR)&ExceptionRecord,
                     0,
                     TrapFrame);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiPageFaultIrqlBugCheck(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL Irql)
{
    KeBugCheckWithTf(IRQL_NOT_LESS_OR_EQUAL,
                     TrapFrame->FaultAddress,
                     Irql,
                     (TrapFrame->ErrorCode >> 1) & 9,
                     TrapFrame->Rip,
                     TrapFrame);
}

BOOLEAN
KiDispatchExceptionToUser(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    EXCEPTION_RECORD LocalExceptRecord;
    ULONG64 UserRsp;
    PKUSER_EXCEPTION_STACK UserStack;
    PCONTEXT_EX ContextEx;
    PXSAVE_AREA_HEADER Header;
    PUCHAR XState = NULL;
    ULONG64 Features, Layout = 0;
    ULONG XStateLength = 0;
    BOOLEAN Success = TRUE;

    if (TrapFrame->SegSs != (KGDT64_R3_DATA | RPL_MASK))
    {
        LocalExceptRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
        LocalExceptRecord.ExceptionFlags = 0;
        LocalExceptRecord.NumberParameters = 0;
        ExceptionRecord = &LocalExceptRecord;
    }

    Features = KiGetUserXStateFeatures();
    if (Features)
    {
        if (SharedUserData->XState.CompactionEnabled)
            Layout = Features | XSTATE_COMPACTION_ENABLE_MASK;
        XStateLength = KiGetUserXStateLength(Layout, Features);
    }

    UserRsp = (Context->Rsp - sizeof(KUSER_EXCEPTION_STACK) - XStateLength) & ~63;

    UserStack = (PKUSER_EXCEPTION_STACK)UserRsp;

    _enable();

    if (XStateLength)
    {
        XState = ExAllocatePoolWithTag(NonPagedPool,
                                       XStateLength + KiGetUserXStateScratchSize(),
                                       TAG_KE_XSTATE);
        if (XState)
        {
            Header = (PXSAVE_AREA_HEADER)XState;
            RtlZeroMemory(Header, sizeof(*Header));
            Header->Mask = Features;
            Header->CompactionMask = Layout;
            KiSaveUserXState(XState, XStateLength, XState + XStateLength);
        }
        else
        {
            XStateLength = 0;
        }
    }

    _SEH2_TRY
    {
        ProbeForWrite(UserStack, sizeof(*UserStack) + XStateLength, TYPE_ALIGNMENT(KUSER_EXCEPTION_STACK));
        RtlZeroMemory(UserStack, sizeof(*UserStack));

        UserStack->Context = *Context;
        UserStack->ExceptionRecord = *ExceptionRecord;

        ContextEx = (PCONTEXT_EX)UserStack->ContextEx;
        ContextEx->Legacy.Offset = -(LONG)sizeof(CONTEXT);
        ContextEx->Legacy.Length = sizeof(CONTEXT);
        ContextEx->XState.Offset = (LONG)((ULONG_PTR)(UserStack + 1) - (ULONG_PTR)ContextEx);
        ContextEx->XState.Length = XStateLength;
        ContextEx->All.Offset = -(LONG)sizeof(CONTEXT);
        ContextEx->All.Length = sizeof(CONTEXT) + ContextEx->XState.Offset + XStateLength;
        if (XStateLength)
        {
            RtlCopyMemory(UserStack + 1, XState, XStateLength);
            UserStack->Context.ContextFlags |= CONTEXT_XSTATE;
        }

        UserStack->MachineFrame.Rip = Context->Rip;
        UserStack->MachineFrame.SegCs = Context->SegCs;
        UserStack->MachineFrame.EFlags = Context->EFlags;
        UserStack->MachineFrame.Rsp = Context->Rsp;
        UserStack->MachineFrame.SegSs = Context->SegSs;
    }
    _SEH2_EXCEPT((LocalExceptRecord = *_SEH2_GetExceptionInformation()->ExceptionRecord),
                 EXCEPTION_EXECUTE_HANDLER)
    {
        Success = FALSE;
    }
    _SEH2_END;

    if (XState)
        ExFreePoolWithTag(XState, TAG_KE_XSTATE);

    if (!Success)
        return FALSE;

    TrapFrame->Rcx = (ULONG64)&UserStack->ExceptionRecord;
    TrapFrame->Rdx = (ULONG64)&UserStack->Context;

    TrapFrame->Rsp = UserRsp;

    TrapFrame->SegCs = KGDT64_R3_CODE | RPL_MASK;
    TrapFrame->SegDs = KGDT64_R3_DATA | RPL_MASK;
    TrapFrame->SegEs = KGDT64_R3_DATA | RPL_MASK;
    TrapFrame->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
    TrapFrame->SegGs = KGDT64_R3_DATA | RPL_MASK;
    TrapFrame->SegSs = KGDT64_R3_DATA | RPL_MASK;

    /* The dispatcher runs without the alignment checking the faulting code asked for */
    TrapFrame->EFlags &= ~EFLAGS_ALIGN_CHECK;

    TrapFrame->Rip = (ULONG64)KeUserExceptionDispatcher;
    KiInstrumentTrapFrame(TrapFrame);

    return TRUE;
}

NTSTATUS
KiAmd64DebugPortExceptionCode(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ NTSTATUS ExceptionCode)
{
    /* The debugger of a 32-bit process expects its breakpoints and single steps as WoW64 ones */
    if (TrapFrame->SegCs != (KGDT64_R3_CMCODE | RPL_MASK))
        return ExceptionCode;

    if (ExceptionCode == STATUS_BREAKPOINT)
        return STATUS_WX86_BREAKPOINT;

    if (ExceptionCode == STATUS_SINGLE_STEP)
        return STATUS_WX86_SINGLE_STEP;

    return ExceptionCode;
}

VOID
KiAmd64PrepareUserBreakpoint(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);

    /* User mode sees a breakpoint with its code as the only parameter */
    ExceptionRecord->NumberParameters = 1;
}

VOID
KiAmd64ReportUnhandledKernelFault(
    _In_ PKTRAP_FRAME TrapFrame)
{
    UCHAR Instructions[8] = { 0 };
    ULONG Offset;

    if (MmIsAddressValid((PVOID)TrapFrame->Rip) &&
        MmIsAddressValid((PVOID)(TrapFrame->Rip + sizeof(Instructions) - 1)))
    {
        RtlCopyMemory(Instructions, (PVOID)TrapFrame->Rip, sizeof(Instructions));
    }

    /* Rbx, Rsi and Rdi live only in the exception frame, not the trap frame */
    DPRINT1("KiDispatchException: unhandled kernel exception at RIP=%p\n"
            "  Instruction bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n"
            "  ErrorCode=%p EFlags=%lx Cs=%x process %s\n"
            "  RAX=%p RCX=%p RDX=%p R8=%p\n"
            "  R9=%p R10=%p R11=%p RBP=%p RSP=%p\n",
            (PVOID)TrapFrame->Rip,
            Instructions[0], Instructions[1], Instructions[2], Instructions[3],
            Instructions[4], Instructions[5], Instructions[6], Instructions[7],
            (PVOID)(ULONG_PTR)TrapFrame->ErrorCode, TrapFrame->EFlags,
            TrapFrame->SegCs, PsGetProcessImageFileName(PsGetCurrentProcess()),
            (PVOID)TrapFrame->Rax, (PVOID)TrapFrame->Rcx, (PVOID)TrapFrame->Rdx,
            (PVOID)TrapFrame->R8, (PVOID)TrapFrame->R9, (PVOID)TrapFrame->R10,
            (PVOID)TrapFrame->R11, (PVOID)TrapFrame->Rbp, (PVOID)TrapFrame->Rsp);

    for (Offset = 0; Offset < 24 * sizeof(ULONG64); Offset += 4 * sizeof(ULONG64))
    {
        PULONG64 Words = (PULONG64)(TrapFrame->Rsp + Offset);

        if ((TrapFrame->Rsp < (ULONG64)MM_SYSTEM_RANGE_START) ||
            !MmIsAddressValid(Words) ||
            !MmIsAddressValid(&Words[3]))
        {
            break;
        }

        DPRINT1("  Stack+%02lx: %p %p %p %p\n",
                Offset, (PVOID)Words[0], (PVOID)Words[1],
                (PVOID)Words[2], (PVOID)Words[3]);
    }
}

NTSTATUS
NTAPI
KeRaiseUserException(
    _In_ NTSTATUS ExceptionCode)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PTEB Teb = Thread->Teb;
    PKTRAP_FRAME TrapFrame = Thread->TrapFrame;

    if (!Teb || !TrapFrame || !KeRaiseUserExceptionDispatcher ||
        KiUserTrap(TrapFrame) == FALSE)
    {
        return ExceptionCode;
    }

    _SEH2_TRY
    {
        Teb->ExceptionCode = ExceptionCode;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    TrapFrame->Rip = (ULONG64)(ULONG_PTR)KeRaiseUserExceptionDispatcher;
    return ExceptionCode;
}

VOID
DECLSPEC_NORETURN
KiSystemFatalException(
    _In_ ULONG ExceptionCode,
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiAmd64BootReadout(BootReadoutFatalTrap,
                       ExceptionCode,
                       TrapFrame->Rip,
                       TrapFrame->Rsp,
                       __readcr2(),
                       TrapFrame->ErrorCode);

    KeBugCheckWithTf(UNEXPECTED_KERNEL_MODE_TRAP,
                     ExceptionCode,
                     0,
                     0,
                     0,
                     TrapFrame);
}

NTSTATUS
NTAPI
KiNpxNotAvailableFaultHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNIMPLEMENTED;
    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 13, 0, 0, 1, TrapFrame);
    return -1;
}

static
BOOLEAN
KiIsPrivilegedInstruction(
    PUCHAR Ip,
    BOOLEAN Wow64)
{
    ULONG i;

    for (i = 0; i < 15; i++)
    {
        if (!Wow64)
        {
            if ((Ip[0] >= 0x40) && (Ip[0] <= 0x4F))
            {
                Ip++;
                continue;
            }
        }

        switch (Ip[0])
        {
            case 0x26:
            case 0x2E:
            case 0x36:
            case 0x3E:
            case 0x64:
            case 0x65:
            case 0x66:
            case 0x67:
            case 0xF0:
            case 0xF2:
            case 0xF3:
                Ip++;
                continue;
        }

        break;
    }

    /* Fifteen prefixes only happen when the code was modified under the fault */
    if (i == 15)
    {
        return FALSE;
    }

    switch (Ip[0])
    {
        case 0xF4:
        case 0xFA:
        case 0xFB:
        case 0x6C:
        case 0x6D:
        case 0x6E:
        case 0x6F:
        case 0xE4:
        case 0xE5:
        case 0xE6:
        case 0xE7:
        case 0xEC:
        case 0xED:
        case 0xEE:
        case 0xEF:
            return TRUE;

        case 0x0F:
        {
            switch (Ip[1])
            {
                case 0x06:
                case 0x07:
                case 0x08:
                case 0x09:
                case 0x20:
                case 0x21:
                case 0x22:
                case 0x23:
                case 0x30:
                case 0x32:
                case 0x33:
                case 0x35:
                    return TRUE;

                case 0x00:
                {
                    switch ((Ip[2] >> 3) & 0x7)
                    {
                        case 2:
                        case 3:
                            return TRUE;
                    }
                    break;
                }

                case 0x01:
                {
                    switch (Ip[2])
                    {
                        case 0xF8:
                            return TRUE;
                    }

                    if (((Ip[2] >> 6) == 3) && (((Ip[2] >> 3) & 0x7) != 6))
                    {
                        break;
                    }

                    switch ((Ip[2] >> 3) & 0x7)
                    {
                        case 2:
                        case 3:
                        case 6:
                        case 7:
                            return TRUE;
                    }
                    break;
                }
            }

            break;
        }
    }

    return FALSE;
}

static
NTSTATUS
KiGeneralProtectionFaultUserMode(
    _In_ PKTRAP_FRAME TrapFrame)
{
    BOOLEAN Wow64 = TrapFrame->SegCs == (KGDT64_R3_CMCODE | RPL_MASK);
    PUCHAR InstructionPointer;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    InstructionPointer = (PUCHAR)TrapFrame->Rip;

    _SEH2_TRY
    {
        ProbeForRead(InstructionPointer, 64, 1);

        if (KiIsPrivilegedInstruction(InstructionPointer, Wow64))
        {
            Status = STATUS_PRIVILEGED_INSTRUCTION;
        }
        else
        {
            Status = STATUS_ACCESS_VIOLATION;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END

    return Status;
}

NTSTATUS
NTAPI
KiInvalidOpcodeFaultHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    BOOLEAN Wow64 = TrapFrame->SegCs == (KGDT64_R3_CMCODE | RPL_MASK);
    PUCHAR InstructionPointer = (PUCHAR)TrapFrame->Rip;
    NTSTATUS Status = STATUS_ILLEGAL_INSTRUCTION;

    _SEH2_TRY
    {
        ProbeForRead(InstructionPointer, 64, 1);

        if (KiIsPrivilegedInstruction(InstructionPointer, Wow64))
        {
            Status = STATUS_PRIVILEGED_INSTRUCTION;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_ILLEGAL_INSTRUCTION;
    }
    _SEH2_END

    return Status;
}

NTSTATUS
NTAPI
KiGeneralProtectionFaultHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PUCHAR Instructions;

    if (TrapFrame->SegCs & 3)
    {
        return KiGeneralProtectionFaultUserMode(TrapFrame);
    }

    if (TrapFrame->SegDs != (KGDT64_R3_DATA | RPL_MASK))
    {
        TrapFrame->SegDs = (KGDT64_R3_DATA | RPL_MASK);
        return STATUS_SUCCESS;
    }
    else if (TrapFrame->SegEs != (KGDT64_R3_DATA | RPL_MASK))
    {
        TrapFrame->SegEs = (KGDT64_R3_DATA | RPL_MASK);
        return STATUS_SUCCESS;
    }

    Instructions = (PUCHAR)TrapFrame->Rip;

    if ((Instructions[0] == 0xF) &&
        ((Instructions[1] == 0x30) ||
         (Instructions[1] == 0x32)))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    return STATUS_ACCESS_VIOLATION;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiMcheckAbortHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Status, Address, Misc, ReportedStatus = 0;
    ULONG Bank, BankCount, ReportedBank = 0;
    BOOLEAN Found = FALSE;

    KiAmd64BootReadout(BootReadoutMachineCheck,
                       __readmsr(MSR_MCG_STATUS),
                       TrapFrame->Rip,
                       TrapFrame->Rsp,
                       __readmsr(MSR_MCG_CAP),
                       0);

    BankCount = (ULONG)(__readmsr(MSR_MCG_CAP) & 0xFF);
    DbgPrint("Machine check on processor %lu, global status 0x%I64x\n",
             KeGetCurrentPrcb()->Number,
             __readmsr(MSR_MCG_STATUS));

    for (Bank = 0; Bank < BankCount; Bank++)
    {
        Status = __readmsr(MSR_MC0_STATUS + 4 * Bank);
        if (!(Status & (1ULL << 63)))
            continue;

        Address = (Status & (1ULL << 58)) ? __readmsr(MSR_MC0_ADDR + 4 * Bank) : 0;
        Misc = (Status & (1ULL << 59)) ? __readmsr(MSR_MC0_MISC + 4 * Bank) : 0;
        DbgPrint("Machine check bank %lu: status 0x%I64x, address 0x%I64x, misc 0x%I64x\n",
                 Bank, Status, Address, Misc);

        if (!Found || ((Status & (1ULL << 61)) && !(ReportedStatus & (1ULL << 61))))
        {
            Found = TRUE;
            ReportedBank = Bank;
            ReportedStatus = Status;
        }
    }

    KeBugCheckWithTf(MACHINE_CHECK_EXCEPTION,
                     ReportedBank,
                     0,
                     (ULONG_PTR)(ReportedStatus >> 32),
                     (ULONG_PTR)(ReportedStatus & 0xFFFFFFFF),
                     TrapFrame);
}

NTSTATUS
NTAPI
KiFloatingErrorHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    XSAVE_FORMAT Area;
    USHORT Error;

    UNREFERENCED_PARAMETER(TrapFrame);

    _fxsave64(&Area);
    Error = Area.StatusWord & ~Area.ControlWord &
            (FSW_INVALID_OPERATION | FSW_DENORMAL | FSW_ZERO_DIVIDE |
             FSW_OVERFLOW | FSW_UNDERFLOW | FSW_PRECISION);

    if (Error & FSW_INVALID_OPERATION)
    {
        return (Area.StatusWord & FSW_STACK_FAULT) ? STATUS_FLOAT_STACK_CHECK :
                                                     STATUS_FLOAT_INVALID_OPERATION;
    }
    if (Error & FSW_ZERO_DIVIDE)
        return STATUS_FLOAT_DIVIDE_BY_ZERO;
    if (Error & FSW_DENORMAL)
        return STATUS_FLOAT_INVALID_OPERATION;
    if (Error & FSW_OVERFLOW)
        return STATUS_FLOAT_OVERFLOW;
    if (Error & FSW_UNDERFLOW)
        return STATUS_FLOAT_UNDERFLOW;
    if (Error & FSW_PRECISION)
        return STATUS_FLOAT_INEXACT_RESULT;
    return STATUS_FLOAT_INVALID_OPERATION;
}

NTSTATUS
NTAPI
KiXmmExceptionHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    /* #XM needs an unmasked status bit; release builds still report a defined code if one is missing */
    ULONG ExceptionCode = STATUS_FLOAT_INVALID_OPERATION;

    if ((TrapFrame->MxCsr & _MM_EXCEPT_INVALID) &&
        !(TrapFrame->MxCsr & _MM_MASK_INVALID))
    {
        ExceptionCode = STATUS_FLOAT_INVALID_OPERATION;
    }
    /* A denormal operand reports an invalid operation, as Windows does */
    else if ((TrapFrame->MxCsr & _MM_EXCEPT_DENORM) &&
             !(TrapFrame->MxCsr & _MM_MASK_DENORM))
    {
        ExceptionCode = STATUS_FLOAT_INVALID_OPERATION;
    }
    else if ((TrapFrame->MxCsr & _MM_EXCEPT_DIV_ZERO) &&
             !(TrapFrame->MxCsr & _MM_MASK_DIV_ZERO))
    {
        ExceptionCode = STATUS_FLOAT_DIVIDE_BY_ZERO;
    }
    else if ((TrapFrame->MxCsr & _MM_EXCEPT_OVERFLOW) &&
             !(TrapFrame->MxCsr & _MM_MASK_OVERFLOW))
    {
        ExceptionCode = STATUS_FLOAT_OVERFLOW;
    }
    else if ((TrapFrame->MxCsr & _MM_EXCEPT_UNDERFLOW) &&
             !(TrapFrame->MxCsr & _MM_MASK_UNDERFLOW))
    {
        ExceptionCode = STATUS_FLOAT_UNDERFLOW;
    }
    else if ((TrapFrame->MxCsr & _MM_EXCEPT_INEXACT) &&
             !(TrapFrame->MxCsr & _MM_MASK_INEXACT))
    {
        ExceptionCode = STATUS_FLOAT_INEXACT_RESULT;
    }
    else
    {
        ASSERT(FALSE);
    }

    return ExceptionCode;
}

NTSTATUS
NTAPI
KiAmd64ContinueExtendedState(
    _In_ PCONTEXT SourceContext,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PCONTEXT_EX ContextEx;
    PXSAVE_AREA_HEADER Header;
    PUCHAR UserXState = NULL, XState;
    ULONG Length = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    if ((ContextFlags & CONTEXT_XSTATE) != CONTEXT_XSTATE || !KiGetUserXStateFeatures())
        return STATUS_SUCCESS;

    ContextEx = (PCONTEXT_EX)(SourceContext + 1);

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(ContextEx, sizeof(*ContextEx), sizeof(ULONG));

        Length = ContextEx->XState.Length;
        UserXState = (PUCHAR)ContextEx + ContextEx->XState.Offset;

        if ((PreviousMode != KernelMode) && (Length >= sizeof(XSAVE_AREA_HEADER)) &&
            (Length <= KiGetUserXStateMaximumLength()))
        {
            ProbeForRead(UserXState, Length, sizeof(ULONG));
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Length < sizeof(XSAVE_AREA_HEADER) || Length > KiGetUserXStateMaximumLength())
        return STATUS_INVALID_PARAMETER;

    XState = ExAllocatePoolWithTag(NonPagedPool, Length + KiGetUserXStateScratchSize(), TAG_KE_XSTATE);
    if (!XState)
        return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        RtlCopyMemory(XState, UserXState, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
    {
        Header = (PXSAVE_AREA_HEADER)XState;
        if (Length < KiGetUserXStateLength(Header->CompactionMask, Header->Mask & KiGetUserXStateSetFeatures(Header)))
            Status = STATUS_BUFFER_OVERFLOW;
        else
            KiRestoreUserXState(XState, XState + Length);
    }

    ExFreePoolWithTag(XState, TAG_KE_XSTATE);
    return Status;
}
