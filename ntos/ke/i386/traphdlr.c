/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under the BSD license in COPYING.ARM
 * PURPOSE:     i386 trap and interrupt handlers called from the entry stubs
 * COPYRIGHT:   Copyright ReactOS Portable Systems Group
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID __cdecl KiFastCallEntry(VOID);
VOID __cdecl KiFastCallEntryWithSingleStep(VOID);

extern PVOID KeUserPopEntrySListFault;
extern PVOID KeUserPopEntrySListResume;
extern PVOID FrRestore;
VOID FASTCALL Ke386LoadFpuState(IN PFX_SAVE_AREA SaveArea);

UCHAR KiTrapPrefixTable[] =
{
    0xF2,
    0xF3,
    0x67,
    0xF0,
    0x66,
    0x2E,
    0x3E,
    0x26,
    0x64,
    0x65,
    0x36,
};

UCHAR KiTrapIoTable[] =
{
    0xE4,
    0xE5,
    0xEC,
    0xED,
    0x6C,
    0x6D,
    0xE6,
    0xE7,
    0xEE,
    0xEF,
    0x6E,
    0x6F,
};

PFAST_SYSTEM_CALL_EXIT KiFastCallExitHandler;
#if DBG && defined(_M_IX86) && !defined(_WINKD_)
PKDBG_PRESERVICEHOOK KeWin32PreServiceHook = NULL;
PKDBG_POSTSERVICEHOOK KeWin32PostServiceHook = NULL;
#endif
#if DBG
BOOLEAN StopChecking = FALSE;
#endif

FORCEINLINE
BOOLEAN
KiVdmTrap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return ((TrapFrame->EFlags & EFLAGS_V86_MASK) ||
            ((KiUserTrap(TrapFrame)) && (PsGetCurrentProcess()->VdmObjects)));
}

FORCEINLINE
BOOLEAN
KiV86Trap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return ((TrapFrame->EFlags & EFLAGS_V86_MASK) != 0);
}

FORCEINLINE
BOOLEAN
KiIsFrameEdited(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return ((TrapFrame->SegCs & FRAME_EDITED) == 0);
}

FORCEINLINE
VOID
KiCommonExit(
    _In_ PKTRAP_FRAME TrapFrame,
    BOOLEAN SkipPreviousMode)
{
    _disable();

    KiCheckForApcDelivery(TrapFrame);

    KeGetPcr()->NtTib.ExceptionList = TrapFrame->ExceptionList;

    /* Check if there are active debug registers */
    if (__builtin_expect(TrapFrame->Dr7 & ~DR7_RESERVED_MASK, 0))
    {
        if (KiUserTrap(TrapFrame) ||
            (TrapFrame->EFlags & EFLAGS_V86_MASK))
        {
            /* Handle debug registers */
            KiHandleDebugRegistersOnTrapExit(TrapFrame);
        }
    }

    /* Debugging checks */
    KiExitTrapDebugChecks(TrapFrame, SkipPreviousMode);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiEoiHelper(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiCommonExit(TrapFrame, TRUE);

    if (TrapFrame->EFlags & EFLAGS_V86_MASK) KiTrapReturnNoSegments(TrapFrame);

    if (KiUserTrap(TrapFrame)) KiTrapReturn(TrapFrame);

    if (KiIsFrameEdited(TrapFrame)) KiEditedTrapReturn(TrapFrame);

    if (TrapFrame->EFlags & EFLAGS_TF) KiTrapReturnNoSegments(TrapFrame);

    KiTrapReturnNoSegmentsRet8(TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiServiceExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ NTSTATUS Status)
{
    ASSERT((TrapFrame->EFlags & EFLAGS_V86_MASK) == 0);
    ASSERT(!KiIsFrameEdited(TrapFrame));

    TrapFrame->Eax = Status;

    KiCommonExit(TrapFrame, FALSE);

    KeGetCurrentThread()->PreviousMode = (CCHAR)TrapFrame->PreviousPreviousMode;

    if (KiUserTrap(TrapFrame))
    {
        if (TrapFrame->EFlags & EFLAGS_TF)
        {
            KiSystemCallTrapReturn(TrapFrame);
        }
        else
        {
            KiFastCallExitHandler(TrapFrame);
            UNREACHABLE;
        }
    }

    KiSystemCallReturn(TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiServiceExit2(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiCommonExit(TrapFrame, FALSE);

    KeGetCurrentThread()->PreviousMode = (CCHAR)TrapFrame->PreviousPreviousMode;

    if (TrapFrame->EFlags & EFLAGS_V86_MASK) KiTrapReturnNoSegments(TrapFrame);

    if (KiUserTrap(TrapFrame)) KiTrapReturn(TrapFrame);

    if (KiIsFrameEdited(TrapFrame)) KiEditedTrapReturn(TrapFrame);

    if (TrapFrame->EFlags & EFLAGS_TF) KiTrapReturnNoSegments(TrapFrame);

    KiTrapReturnNoSegmentsRet8(TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiDebugHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Parameter1,
    _In_ ULONG Parameter2,
    _In_ ULONG Parameter3)
{
    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    if (KiUserTrap(TrapFrame)) _enable();

    KiDispatchExceptionFromTrapFrame(STATUS_BREAKPOINT,
                                     0,
                                     TrapFrame->Eip - (Parameter1 == BREAKPOINT_BREAK),
                                     3,
                                     Parameter1,
                                     Parameter2,
                                     Parameter3,
                                     TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiNpxHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKTHREAD Thread,
    _In_ PFX_SAVE_AREA SaveArea)
{
    ULONG Cr0, Mask, Error, ErrorOffset, DataOffset;

    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    if (!KiUserTrap(TrapFrame))
    {
        SaveArea->Cr0NpxState |= CR0_TS;

        if ((PVOID)TrapFrame->Eip == FrRestore)
        {
            TrapFrame->Eip += 3;
            KiEoiHelper(TrapFrame);
        }
    }

    if (Thread->NpxState == NPX_STATE_LOADED)
    {
        Cr0 = __readcr0();
        Cr0 &= ~(CR0_MP | CR0_EM | CR0_TS);
        __writecr0(Cr0);

        Ke386SaveFpuState(SaveArea);

        Cr0 |= NPX_STATE_NOT_LOADED;
        Cr0 |= SaveArea->Cr0NpxState;
        __writecr0(Cr0);

        Thread->NpxState = NPX_STATE_NOT_LOADED;
        KeGetCurrentPrcb()->NpxThread = NULL;
    }

    SaveArea->Cr0NpxState &= ~CR0_TS;
    _enable();

    if (KeI386FxsrPresent)
    {
        Mask = SaveArea->U.FxArea.ControlWord;
        Error = SaveArea->U.FxArea.StatusWord;

        ErrorOffset = SaveArea->U.FxArea.ErrorOffset;
        DataOffset = SaveArea->U.FxArea.DataOffset;
    }
    else
    {
        Mask = SaveArea->U.FnArea.ControlWord;
        Error = SaveArea->U.FnArea.StatusWord;

        ErrorOffset = SaveArea->U.FnArea.ErrorOffset;
        DataOffset = SaveArea->U.FnArea.DataOffset;
    }

    Mask &= (FSW_INVALID_OPERATION |
             FSW_DENORMAL |
             FSW_ZERO_DIVIDE |
             FSW_OVERFLOW |
             FSW_UNDERFLOW |
             FSW_PRECISION);
    Error &= ~Mask;

    if (Error & FSW_INVALID_OPERATION)
    {
        /*
         * Now check if this is actually a Stack Fault. This is needed because
         * on x86 the Invalid Operation error is set for Stack Check faults as well.
         */
        if (Error & FSW_STACK_FAULT)
        {
            KiDispatchException2Args(STATUS_FLOAT_STACK_CHECK,
                                     ErrorOffset,
                                     0,
                                     DataOffset,
                                     TrapFrame);
        }
        else
        {
            KiDispatchException1Args(STATUS_FLOAT_INVALID_OPERATION,
                                     ErrorOffset,
                                     0,
                                     TrapFrame);
        }
    }

    if (Error & FSW_ZERO_DIVIDE)
    {
        KiDispatchException1Args(STATUS_FLOAT_DIVIDE_BY_ZERO,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }

    if (Error & FSW_DENORMAL)
    {
        KiDispatchException1Args(STATUS_FLOAT_INVALID_OPERATION,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }

    if (Error & FSW_OVERFLOW)
    {
        KiDispatchException1Args(STATUS_FLOAT_OVERFLOW,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }

    if (Error & FSW_UNDERFLOW)
    {
        KiDispatchException1Args(STATUS_FLOAT_UNDERFLOW,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }

    if (Error & FSW_PRECISION)
    {
        KiDispatchException1Args(STATUS_FLOAT_INEXACT_RESULT,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }

    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 1, Error, 0, 0, TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap00Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    _enable();

    KiDispatchException0Args(STATUS_INTEGER_DIVIDE_BY_ZERO,
                             TrapFrame->Eip,
                             TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap01Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    if (TrapFrame->Eip == (ULONG)KiFastCallEntry)
    {
        TrapFrame->EFlags &= ~EFLAGS_TF;

        TrapFrame->Eip = (ULONG)KiFastCallEntryWithSingleStep;

        KiEoiHelper(TrapFrame);
    }

    if (KiUserTrap(TrapFrame)) _enable();

    TrapFrame->EFlags &= ~EFLAGS_TF;
    KiDispatchException0Args(STATUS_SINGLE_STEP,
                             TrapFrame->Eip,
                             TrapFrame);
}

VOID
__cdecl
KiTrap02Handler(VOID)
{
    PKTSS Tss, NmiTss;
    PKTHREAD Thread;
    PKPROCESS Process;
    PKGDTENTRY TssGdt;
    KTRAP_FRAME TrapFrame;
    KIRQL OldIrql;

    _disable();

    Tss = KeGetPcr()->TSS;
    Thread = ((PKIPCR)KeGetPcr())->PrcbData.CurrentThread;
    Process = Thread->ApcState.Process;

    Tss->CR3 = KPROCESS_DTB0(Process);
    Tss->IoMapBase = Process->IopmOffset;
    Tss->LDT = Process->LdtDescriptor.LimitLow ? KGDT_LDT : 0;

    TssGdt = &((PKIPCR)KeGetPcr())->GDT[KGDT_NMI_TSS / sizeof(KGDTENTRY)];
    NmiTss = (PKTSS)(ULONG_PTR)(TssGdt->BaseLow |
                                TssGdt->HighWord.Bytes.BaseMid << 16 |
                                TssGdt->HighWord.Bytes.BaseHi << 24);

    KeGetPcr()->TSS = NmiTss;
    __writeeflags(__readeflags() &~ EFLAGS_NESTED_TASK);
    TssGdt->HighWord.Bits.Dpl = 0;
    TssGdt->HighWord.Bits.Pres = 1;
    TssGdt->HighWord.Bits.Type = I386_TSS;

    RtlZeroMemory(&TrapFrame, sizeof(KTRAP_FRAME));
    TrapFrame.HardwareSegSs = Tss->Ss0;
    TrapFrame.HardwareEsp = Tss->Esp0;
    TrapFrame.EFlags = Tss->EFlags;
    TrapFrame.SegCs = Tss->Cs;
    TrapFrame.Eip = Tss->Eip;
    TrapFrame.Ebp = Tss->Ebp;
    TrapFrame.Ebx = Tss->Ebx;
    TrapFrame.Esi = Tss->Esi;
    TrapFrame.Edi = Tss->Edi;
    TrapFrame.SegFs = Tss->Fs;
    TrapFrame.ExceptionList = KeGetPcr()->NtTib.ExceptionList;
    TrapFrame.PreviousPreviousMode = (ULONG)-1;
    TrapFrame.Eax = Tss->Eax;
    TrapFrame.Ecx = Tss->Ecx;
    TrapFrame.Edx = Tss->Edx;
    TrapFrame.SegDs = Tss->Ds;
    TrapFrame.SegEs = Tss->Es;
    TrapFrame.SegGs = Tss->Gs;
    TrapFrame.DbgEip = Tss->Eip;
    TrapFrame.DbgEbp = Tss->Ebp;

    KiSaveProcessorState(&TrapFrame, NULL);

    if (!KiHandleNmi())
    {
        /*
         * They did not, so call the platform HAL routine to bugcheck the system
         *
         * Make sure the HAL believes it's running at HIGH IRQL... we can't use
         * the normal APIs here as playing with the IRQL could change the system
         * state.
         */
        OldIrql = KeGetPcr()->Irql;
        KeGetPcr()->Irql = HIGH_LEVEL;
        HalHandleNMI(NULL);
        KeGetPcr()->Irql = OldIrql;
    }

    if (KeGetPcr()->TSS->Backlink == KGDT_NMI_TSS)
    {
        KiSystemFatalException(EXCEPTION_NMI, NULL);
    }

    KeGetPcr()->TSS = Tss;

    TssGdt->HighWord.Bits.Dpl = 0;
    TssGdt->HighWord.Bits.Pres = 1;
    TssGdt->HighWord.Bits.Type = I386_ACTIVE_TSS;

    __writeeflags(__readeflags() | EFLAGS_NESTED_TASK);

}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap03Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    KiDebugHandler(TrapFrame, BREAKPOINT_BREAK, 0, 0);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap04Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    _enable();

    KiDispatchException0Args(STATUS_INTEGER_OVERFLOW,
                             TrapFrame->Eip - 1,
                             TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap05Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    if (!KiUserTrap(TrapFrame)) KiSystemFatalException(EXCEPTION_BOUND_CHECK, TrapFrame);

    _enable();

    KiDispatchException0Args(STATUS_ARRAY_BOUNDS_EXCEEDED,
                             TrapFrame->Eip,
                             TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap06Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PUCHAR Instruction;
    ULONG i;
    KIRQL OldIrql;

    if (__builtin_expect(KiV86Trap(TrapFrame), 1))
    {
        KiEnterV86Trap(TrapFrame);

        if (__builtin_expect(!PsGetCurrentProcess()->VdmObjects, 0))
        {
            _enable();

            KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                                     TrapFrame->Eip,
                                     TrapFrame);
        }

        KeRaiseIrql(APC_LEVEL, &OldIrql);
        _enable();

        if (!VdmDispatchBop(TrapFrame))
        {
            UNIMPLEMENTED_FATAL();
        }

        KeLowerIrql(OldIrql);
        _disable();

        KiExitV86Trap(TrapFrame);
    }

    KiEnterTrap(TrapFrame);

    Instruction = (PUCHAR)TrapFrame->Eip;
    _enable();

    if (KiUserTrap(TrapFrame))
    {
        /* FIXME: Use SEH */
        for (i = 0; i < 4; i++)
        {
            if (Instruction[i] == 0xF0)
            {
                KiDispatchException0Args(STATUS_INVALID_LOCK_SEQUENCE,
                                         TrapFrame->Eip,
                                         TrapFrame);
            }
        }

        /* FIXME: SEH ends here */
    }

    KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                             TrapFrame->Eip,
                             TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap07Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread, NpxThread;
    PFX_SAVE_AREA SaveArea, NpxSaveArea;
    ULONG Cr0;

    KiEnterTrap(TrapFrame);

    for (;;)
    {
        Thread = KeGetCurrentThread();

        SaveArea = KiGetThreadNpxArea(Thread);

        if (SaveArea->Cr0NpxState & CR0_EM)
        {
            UNIMPLEMENTED_FATAL();
        }

        Cr0 = __readcr0();
        if (Thread->NpxState != NPX_STATE_LOADED)
        {
            Cr0 &= ~(CR0_MP | CR0_EM | CR0_TS);
            __writecr0(Cr0);

            NpxThread = KeGetCurrentPrcb()->NpxThread;
            if (NpxThread)
            {
                NpxSaveArea = KiGetThreadNpxArea(NpxThread);

                Ke386SaveFpuState(NpxSaveArea);

                NpxThread->NpxState = NPX_STATE_NOT_LOADED;
           }

            Ke386LoadFpuState(SaveArea);

            Thread->NpxState = NPX_STATE_LOADED;
            KeGetCurrentPrcb()->NpxThread = Thread;

            _enable();

            if (!SaveArea->Cr0NpxState) KiEoiHelper(TrapFrame);

            _disable();

            Cr0 = __readcr0();
            Cr0 |= SaveArea->Cr0NpxState;
            __writecr0(Cr0);

            _enable();
            if (Cr0 & CR0_TS) KiEoiHelper(TrapFrame);

            __writecr0(__readcr0() &~ CR0_TS);
            _disable();
        }
        else
        {
            break;
        }
    }

    if (Cr0 & CR0_TS)
    {
        if (Cr0 & CR0_MP)
        {
            __writecr0(__readcr0() &~ CR0_TS);
            KiEoiHelper(TrapFrame);
        }

        KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 2, Cr0, 0, 0, TrapFrame);
    }

    KiNpxHandler(TrapFrame, Thread, SaveArea);
}

DECLSPEC_NORETURN
VOID
__cdecl
KiTrap08Handler(VOID)
{
    PKTSS Tss, DfTss;
    PKTHREAD Thread;
    PKPROCESS Process;
    PKGDTENTRY TssGdt;

    _disable();

    Tss = KeGetPcr()->TSS;
    Thread = ((PKIPCR)KeGetPcr())->PrcbData.CurrentThread;
    Process = Thread->ApcState.Process;

    Tss->CR3 = KPROCESS_DTB0(Process);
    Tss->IoMapBase = Process->IopmOffset;
    Tss->LDT = Process->LdtDescriptor.LimitLow ? KGDT_LDT : 0;

    TssGdt = &((PKIPCR)KeGetPcr())->GDT[KGDT_DF_TSS / sizeof(KGDTENTRY)];
    DfTss  = (PKTSS)(ULONG_PTR)(TssGdt->BaseLow |
                                TssGdt->HighWord.Bytes.BaseMid << 16 |
                                TssGdt->HighWord.Bytes.BaseHi << 24);

    KeGetPcr()->TSS = DfTss;
    __writeeflags(__readeflags() &~ EFLAGS_NESTED_TASK);
    TssGdt->HighWord.Bits.Dpl = 0;
    TssGdt->HighWord.Bits.Pres = 1;

    TssGdt->HighWord.Bits.Type = I386_TSS;

    /* Bugcheck the system */
    KeBugCheckWithTf(UNEXPECTED_KERNEL_MODE_TRAP,
                     EXCEPTION_DOUBLE_FAULT,
                     (ULONG_PTR)Tss,
                     0,
                     0,
                     NULL);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap09Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    _enable();
    KiSystemFatalException(EXCEPTION_NPX_OVERRUN, TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap0AHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    KiSystemFatalException(EXCEPTION_INVALID_TSS, TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap0BHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    /* FIXME: Kill the system */
    UNIMPLEMENTED;
    KiSystemFatalException(EXCEPTION_SEGMENT_NOT_PRESENT, TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap0CHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    /* FIXME: Kill the system */
    UNIMPLEMENTED;
    KiSystemFatalException(EXCEPTION_STACK_FAULT, TrapFrame);
}

DECLSPEC_NORETURN VOID FASTCALL KiTrap0EHandler(IN PKTRAP_FRAME);

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap0DHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG i, j, Iopl;
    BOOLEAN Privileged = FALSE;
    PUCHAR Instructions;
    UCHAR Instruction = 0;
    KIRQL OldIrql;

    if (__builtin_expect(KiV86Trap(TrapFrame), 1))
    {
        KiEnterV86Trap(TrapFrame);

        if (__builtin_expect(!PsGetCurrentProcess()->VdmObjects, 0))
        {
            _enable();

            KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                                     TrapFrame->Eip,
                                     TrapFrame);
        }

        KeRaiseIrql(APC_LEVEL, &OldIrql);
        _enable();

        if (__builtin_expect(Ki386HandleOpcodeV86(TrapFrame) == 0xFF, 0))
        {
            UNIMPLEMENTED_FATAL();
        }

        KeLowerIrql(OldIrql);
        _disable();

        KiExitV86Trap(TrapFrame);
    }

    KiEnterTrap(TrapFrame);

    if (KiUserTrap(TrapFrame))
    {
        ASSERT(KiVdmTrap(TrapFrame) == FALSE);

        _enable();
        if (!TrapFrame->ErrCode)
        {
            /* FIXME: Use SEH */
            Instructions = (PUCHAR)TrapFrame->Eip;

            for (i = 0; i < 15; i++)
            {
                for (j = 0; j < sizeof(KiTrapPrefixTable); j++)
                {
                    if (Instructions[i] == KiTrapPrefixTable[j])
                    {
                        break;
                    }
                }

                if (j == sizeof(KiTrapPrefixTable))
                {
                    Instruction = Instructions[i];
                    break;
                }
            }

            if (i == 15)
            {
                KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                                         TrapFrame->Eip,
                                         TrapFrame);
            }

            DPRINT("Instruction (%lu) at fault: %lx %lx %lx %lx\n",
                    i,
                    Instructions[i],
                    Instructions[i + 1],
                    Instructions[i + 2],
                    Instructions[i + 3]);
            if (Instruction == 0xF4)
            {
                Privileged = TRUE;
            }
            else if (Instruction == 0x0F)
            {
                if (((Instructions[i + 1] == 0x00) &&
                     (((Instructions[i + 2] & 0x38) == 0x10) ||
                      ((Instructions[i + 2] & 0x38) == 0x18))) ||
                    ((Instructions[i + 1] == 0x01) &&
                     (((Instructions[i + 2] >> 6) != 3) || ((Instructions[i + 2] & 0x38) == 0x30)) &&
                     (((Instructions[i + 2] & 0x38) == 0x10) ||
                      ((Instructions[i + 2] & 0x38) == 0x18) ||
                      ((Instructions[i + 2] & 0x38) == 0x30))) ||
                    (Instructions[i + 1] == 0x08) ||
                    (Instructions[i + 1] == 0x09) ||
                    (Instructions[i + 1] == 0x35) ||
                    (Instructions[i + 1] == 0x21) ||
                    (Instructions[i + 1] == 0x06) ||
                    (Instructions[i + 1] == 0x20) ||
                    (Instructions[i + 1] == 0x22) ||
                    (Instructions[i + 1] == 0x23) ||
                    (Instructions[i + 1] == 0x30) ||
                    (Instructions[i + 1] == 0x32) ||
                    (Instructions[i + 1] == 0x33))

                {
                    Privileged = TRUE;
                }
            }
            else
            {
                Iopl = (TrapFrame->EFlags & EFLAGS_IOPL) >> 12;
                if ((TrapFrame->SegCs & RPL_MASK) > Iopl)
                {
                    if ((Instruction == 0xFA) || (Instruction == 0xFB))
                    {
                        Privileged = TRUE;
                    }
                    else
                    {
                        for (j = 0; j < sizeof(KiTrapIoTable); j++)
                        {
                            if (Instruction == KiTrapIoTable[j])
                            {
                                Privileged = TRUE;
                                break;
                            }
                        }
                    }
                }
            }

            if (Privileged)
            {
                KiDispatchException0Args(STATUS_PRIVILEGED_INSTRUCTION,
                                         TrapFrame->Eip,
                                         TrapFrame);
            }
        }

        KiDispatchException2Args(STATUS_ACCESS_VIOLATION,
                                 TrapFrame->Eip,
                                 0,
                                 0xFFFFFFFF,
                                 TrapFrame);
    }

    if ((PVOID)TrapFrame->Eip >= (PVOID)KiTrap0DHandler &&
        (PVOID)TrapFrame->Eip <  (PVOID)KiTrap0EHandler)
    {
        UNIMPLEMENTED_FATAL();
    }

    /*
     * NOTE2: Another scenario is the IRET during a V8086 restore (BIOS Call)
     * which will cause a GPF since the trap frame is a total mess (on purpose)
     * as built in KiEnterV86Mode.
     *
     * The idea is to scan for IRET, scan for the known EIP address, validate CS
     * and then manually issue a jump to the V8086 return EIP.
     */
    /*
     * NOTE: The ASM trap exit code would restore segment registers by doing
     * a POP <SEG>, which could cause an invalid segment if someone had messed
     * with the segment values.
     *
     * Another case is a bogus SS, which would hit a GPF when doing the iret.
     * This could only be done through a buggy or malicious driver, or perhaps
     * the kernel debugger.
     *
     * The kernel normally restores the "true" segment if this happens.
     *
     * However, since we're restoring in C, not ASM, we can't detect
     * POP <SEG> since the actual instructions will be different.
     *
     * A better technique would be to check the EIP and somehow edit the
     * trap frame before restarting the instruction -- but we would need to
     * know the extract instruction that was used first.
     *
     * We could force a special instrinsic to use stack instructions, or write
     * a simple instruction length checker.
     *
     * Nevertheless, this is a lot of work for the purpose of avoiding a crash
     * when the user is purposedly trying to create one from kernel-mode, so
     * we should probably table this for now since it's not a "real" issue.
     */
    Instructions = (PUCHAR)TrapFrame->Eip;
    if (Instructions[0] == 0xCF)
    {
        /*
         * Some evil shit is going on here -- this is not the SS:ESP you're
         * looking for! Instead, this is actually CS:EIP you're looking at!
         * Why? Because part of the trap frame actually corresponds to the IRET
         * stack during the trap exit!
         */
        if ((TrapFrame->HardwareEsp == (ULONG)Ki386BiosCallReturnAddress) &&
            (TrapFrame->HardwareSegSs == (KGDT_R0_CODE | RPL_MASK)))
        {
            Ki386BiosCallReturnAddress(TrapFrame);
        }
        else
        {
            UNIMPLEMENTED_FATAL();
        }
    }

    if ((Instructions[0] == 0xF) &&
        ((Instructions[1] == 0x32) ||
         (Instructions[1] == 0x30)))
    {
        KiDispatchException0Args(STATUS_ACCESS_VIOLATION,
                                 TrapFrame->Eip,
                                 TrapFrame);
    }

    if (TrapFrame->SegDs != (KGDT_R3_DATA | RPL_MASK))
    {
        TrapFrame->SegDs = (KGDT_R3_DATA | RPL_MASK);
    }
    else if (TrapFrame->SegEs != (KGDT_R3_DATA | RPL_MASK))
    {
        TrapFrame->SegEs = (KGDT_R3_DATA | RPL_MASK);
    }
    else
    {
        KiSystemFatalException(EXCEPTION_GP_FAULT, TrapFrame);
    }

    KiTrapReturn(TrapFrame);
}

BOOLEAN
FASTCALL
KiCheckForSListFault(
    PKTRAP_FRAME TrapFrame)
{
    /* TODO: ntos/ps/psstartup.c looks these labels up as
     * RtlpInterlockedPopEntrySList*, while the system DLL exports them as
     * ExpInterlockedPopEntrySList*, so KeUserPopEntrySListFault stays NULL and
     * the user-mode half of this recovery never runs. Whatever that lookup
     * ends up doing, a NULL label must not be compared against, or every fault
     * at address zero answers to it. */
    /* Explanation: An S-List fault can occur due to a race condition between 2
       threads simultaneously trying to pop an element from the S-List. After
       thread 1 has read the pointer to the top element on the S-List it is
       preempted and thread 2 calls InterlockedPopEntrySlist on the same S-List,
       removing the top element and freeing it's memory. After that thread 1
       resumes and tries to read the address of the Next pointer from the top
       element, which it assumes will be the next top element.
       But since that memory has been freed, we get a page fault. To handle this
       race condition, we let thread 1 repeat the operation.
       We do NOT invoke the page fault handler in this case, since we do not
       want to trigger any side effects, like paging or a guard page fault.

       Sequence of operations:

           Thread 1 : mov eax, [ebp] <= eax now points to the first element
           Thread 1 : mov edx, [ebp + 4] <= edx is loaded with Depth and Sequence
            *** preempted ***
           Thread 2 : calls InterlockedPopEntrySlist, changing the top element
           Thread 2 : frees the memory of the element that was popped
            *** preempted ***
           Thread 1 : checks if eax is NULL
           Thread 1 : InterlockedPopEntrySListFault: mov ebx, [eax] <= faults

        To be sure that we are dealing with exactly the case described above, we
        check whether the ListHeader has changed. If Thread 2 only popped one
        entry, the Next field in the S-List-header has changed.
        If after thread 1 has faulted, thread 2 allocates a new element, by
        chance getting the same address as the previously freed element and
        pushes it on the list again, we will see the same top element, but the
        Sequence member of the S-List header has changed. Therefore we check
        both fields to make sure we catch any concurrent modification of the
        S-List-header.
    */
    if ((TrapFrame->Eip == (ULONG_PTR)ExpInterlockedPopEntrySListFault) ||
        ((KeUserPopEntrySListFault != NULL) &&
         (TrapFrame->Eip == (ULONG_PTR)KeUserPopEntrySListFault)))
    {
        ULARGE_INTEGER SListHeader;
        PVOID ResumeAddress;

#if DBG
        {
            const UCHAR *Instruction = (const UCHAR *)TrapFrame->Eip;

            /* mov ebx, [eax], followed by cmpxchg8b [ebp]. The MP
             * kernel and ntdll include LOCK; the UP kernel omits it. */
            ASSERT(Instruction[0] == 0x8B && Instruction[1] == 0x18);
            Instruction += 2;
            if (*Instruction == 0xF0) ++Instruction;
            ASSERT(Instruction[0] == 0x0F && Instruction[1] == 0xC7 &&
                   Instruction[2] == 0x4D && Instruction[3] == 0x00);
        }
#endif

        if (TrapFrame->Eip == (ULONG_PTR)KeUserPopEntrySListFault)
        {
            _SEH2_TRY
            {
                ProbeForRead((PVOID)TrapFrame->Ebp,
                             sizeof(ULARGE_INTEGER),
                             TYPE_ALIGNMENT(SLIST_HEADER));
                SListHeader = *(PULARGE_INTEGER)TrapFrame->Ebp;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return FALSE;
            }
            _SEH2_END;
            ResumeAddress = KeUserPopEntrySListResume;
        }
        else
        {
            SListHeader = *(PULARGE_INTEGER)TrapFrame->Ebp;
            ResumeAddress = ExpInterlockedPopEntrySListResume;
        }

        if ((SListHeader.LowPart != TrapFrame->Eax) ||
            (SListHeader.HighPart != TrapFrame->Edx))
        {
            DPRINT1("*** Got an S-List-Fault ***\n");
            KeGetCurrentThread()->SListFaultCount++;

            TrapFrame->Eip = (ULONG_PTR)ResumeAddress;

            return TRUE;
        }
    }

    return FALSE;
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap0EHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    ULONG_PTR Cr2;
    NTSTATUS Status;

    KiEnterTrap(TrapFrame);

    Thread = KeGetCurrentThread();
    if (KeGetTrapFrame(Thread) != TrapFrame)
    {
        if (((ULONG_PTR)KeGetTrapFrame(Thread) - (ULONG_PTR)TrapFrame) <=
            FIELD_OFFSET(KTRAP_FRAME, EFlags))
        {
            UNIMPLEMENTED_FATAL();
        }
    }

    Cr2 = __readcr2();

    _enable();

    if (!(TrapFrame->EFlags & EFLAGS_INTERRUPT_MASK))
    {
        /* This is completely illegal, bugcheck the system */
        KeBugCheckWithTf(IRQL_NOT_LESS_OR_EQUAL,
                         Cr2,
                         (ULONG_PTR)-1,
                         TrapFrame->ErrCode,
                         TrapFrame->Eip,
                         TrapFrame);
    }

    if (KiCheckForSListFault(TrapFrame))
    {
        KiEoiHelper(TrapFrame);
    }

    Status = MmAccessFault(TrapFrame->ErrCode,
                           (PVOID)Cr2,
                           KiUserTrap(TrapFrame),
                           TrapFrame);
    if (NT_SUCCESS(Status))
    {
        /* Check whether the kernel debugger has owed breakpoints to be inserted */
        KdSetOwedBreakpoints();

        KiEoiHelper(TrapFrame);
    }

    /* An unresolved fault above APC_LEVEL is fatal */
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        KeBugCheckWithTf(IRQL_NOT_LESS_OR_EQUAL,
                         Cr2,
                         KeGetCurrentIrql(),
                         (TrapFrame->ErrCode >> 1) & 9,
                         TrapFrame->Eip,
                         TrapFrame);
    }

    if (KiVdmTrap(TrapFrame))
    {
        DPRINT1("VDM PAGE FAULT at %lx:%lx for address %lx\n",
                TrapFrame->SegCs, TrapFrame->Eip, Cr2);
        if (VdmDispatchPageFault(TrapFrame))
        {
            DPRINT1("VDM page fault with status 0x%lx resolved\n", Status);
            KiEoiHelper(TrapFrame);
        }
        DPRINT1("VDM page fault with status 0x%lx NOT resolved\n", Status);
    }

    if (Status == STATUS_ACCESS_VIOLATION)
    {
        KiDispatchException2Args(KI_EXCEPTION_ACCESS_VIOLATION,
                                 TrapFrame->Eip,
                                 MI_IS_WRITE_ACCESS(TrapFrame->ErrCode),
                                 Cr2,
                                 TrapFrame);
    }
    else if ((Status == STATUS_GUARD_PAGE_VIOLATION) ||
             (Status == STATUS_STACK_OVERFLOW))
    {
        KiDispatchException2Args(Status,
                                 TrapFrame->Eip,
                                 MI_IS_WRITE_ACCESS(TrapFrame->ErrCode),
                                 Cr2,
                                 TrapFrame);
    }

    KiDispatchExceptionFromTrapFrame(STATUS_IN_PAGE_ERROR,
                                     0,
                                     TrapFrame->Eip,
                                     3,
                                     MI_IS_WRITE_ACCESS(TrapFrame->ErrCode),
                                     Cr2,
                                     Status,
                                     TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap0FHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    /* FIXME: Kill the system */
    UNIMPLEMENTED;
    KiSystemFatalException(EXCEPTION_RESERVED_TRAP, TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap10Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    PFX_SAVE_AREA SaveArea;

    KiEnterTrap(TrapFrame);

    Thread = KeGetCurrentThread();
    SaveArea = KiGetThreadNpxArea(Thread);
    if (Thread != KeGetCurrentPrcb()->NpxThread)
    {
        _enable();
        SaveArea->Cr0NpxState |= CR0_TS;

        KiEoiHelper(TrapFrame);
    }

    KiNpxHandler(TrapFrame, Thread, SaveArea);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap11Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    _enable();
    KiSystemFatalException(EXCEPTION_ALIGNMENT_CHECK, TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiTrap13Handler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    PFX_SAVE_AREA SaveArea;
    ULONG Cr0, MxCsrMask, Error;

    KiEnterTrap(TrapFrame);

    Thread = KeGetCurrentThread();
    if (Thread != KeGetCurrentPrcb()->NpxThread)
    {
        KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 13, (ULONG_PTR)Thread, 0, 0, TrapFrame);
    }

    SaveArea = KiGetThreadNpxArea(Thread);

    ASSERT(KiVdmTrap(TrapFrame) == FALSE);

    if (!KiUserTrap(TrapFrame))
    {
        KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 13, 0, 0, 2, TrapFrame);
    }

    Cr0 = __readcr0();
    Cr0 &= ~(CR0_MP | CR0_EM | CR0_TS);
    __writecr0(Cr0);

    Ke386SaveFpuState(SaveArea);

    Cr0 |= NPX_STATE_NOT_LOADED;
    Cr0 |= SaveArea->Cr0NpxState;
     __writecr0(Cr0);

    Thread->NpxState = NPX_STATE_NOT_LOADED;
    KeGetCurrentPrcb()->NpxThread = NULL;

    SaveArea->Cr0NpxState &= ~CR0_TS;
    _enable();

    MxCsrMask = ~((USHORT)SaveArea->U.FxArea.MXCsr >> 7);

    Error = (USHORT)SaveArea->U.FxArea.MXCsr & (FSW_INVALID_OPERATION |
                                                FSW_DENORMAL |
                                                FSW_ZERO_DIVIDE |
                                                FSW_OVERFLOW |
                                                FSW_UNDERFLOW |
                                                FSW_PRECISION);
    Error &= MxCsrMask;

    if (Error & (FSW_INVALID_OPERATION |
                 FSW_DENORMAL |
                 FSW_ZERO_DIVIDE |
                 FSW_OVERFLOW |
                 FSW_UNDERFLOW |
                 FSW_PRECISION))
    {
        KiDispatchException1Args(STATUS_FLOAT_MULTIPLE_TRAPS,
                                 TrapFrame->Eip,
                                 0,
                                 TrapFrame);
    }

    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 13, 0, 0, 1, TrapFrame);
}

VOID
FASTCALL
KiRaiseSecurityCheckFailureHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    TrapFrame->Eip -= 2;

    if (KiUserTrap(TrapFrame))
    {
        KiDispatchExceptionFromTrapFrame(STATUS_STACK_BUFFER_OVERRUN,
                                         EXCEPTION_NONCONTINUABLE,
                                         TrapFrame->Eip,
                                         1,
                                         TrapFrame->Ecx,
                                         0,
                                         0,
                                         TrapFrame);
    }
    else
    {
        EXCEPTION_RECORD ExceptionRecord;

        /* Bugcheck the system */
        ExceptionRecord.ExceptionCode = STATUS_STACK_BUFFER_OVERRUN;
        ExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
        ExceptionRecord.ExceptionRecord = NULL;
        ExceptionRecord.ExceptionAddress = (PVOID)TrapFrame->Eip;
        ExceptionRecord.NumberParameters = 1;
        ExceptionRecord.ExceptionInformation[0] = TrapFrame->Ecx;

        KeBugCheckWithTf(KERNEL_SECURITY_CHECK_FAILURE,
                         TrapFrame->Ecx,
                         (ULONG_PTR)TrapFrame,
                         (ULONG_PTR)&ExceptionRecord,
                         0,
                         TrapFrame);
    }
}

VOID
FASTCALL
KiGetTickCountHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    DbgPrint("INT 0x2A attempted, returning 0 tick count\n");
    TrapFrame->Eax = 0;

    KiEoiHelper(TrapFrame);
}

VOID
FASTCALL
KiCallbackReturnHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    NTSTATUS Status;

    TrapFrame->ExceptionList = KeGetPcr()->NtTib.ExceptionList;

    Thread = KeGetCurrentThread();
    Thread->TrapFrame = TrapFrame;
    Thread->PreviousMode = KiUserTrap(TrapFrame);
    ASSERT(Thread->PreviousMode != KernelMode);

    Status = NtCallbackReturn((PVOID)TrapFrame->Ecx,
                              TrapFrame->Edx,
                              TrapFrame->Eax);

    KiServiceExit(TrapFrame, Status);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiRaiseAssertionHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    TrapFrame->Eip -= 2;

    KiDispatchException0Args(STATUS_ASSERTION_FAILURE,
                             TrapFrame->Eip,
                             TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiDebugServiceHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    KiEnterTrap(TrapFrame);

    TrapFrame->Eip++;

    KiDebugHandler(TrapFrame, TrapFrame->Eax, TrapFrame->Ecx, TrapFrame->Edx);
}

FORCEINLINE
VOID
KiDbgPreServiceHook(
    ULONG SystemCallNumber,
    PULONG_PTR Arguments)
{
#if DBG && !defined(_WINKD_)
    if (SystemCallNumber >= 0x1000 && KeWin32PreServiceHook)
        KeWin32PreServiceHook(SystemCallNumber, Arguments);
#endif
}

FORCEINLINE
ULONG_PTR
KiDbgPostServiceHook(
    ULONG SystemCallNumber,
    ULONG_PTR Result)
{
#if DBG && !defined(_WINKD_)
    if (SystemCallNumber >= 0x1000 && KeWin32PostServiceHook)
        return KeWin32PostServiceHook(SystemCallNumber, Result);
#endif
    return Result;
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiSystemServiceHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PVOID Arguments)
{
    PKTHREAD Thread;
    PKSERVICE_TABLE_DESCRIPTOR DescriptorTable;
    ULONG Id, Offset, StackBytes;
    NTSTATUS Status;
    PVOID Handler;
    ULONG SystemCallNumber = TrapFrame->Eax;

    Thread = KeGetCurrentThread();

    /* Set debug header */
    KiFillTrapFrameDebug(TrapFrame);

    TrapFrame->Edx = (ULONG_PTR)Thread->TrapFrame;

    TrapFrame->ErrCode = 0;

    TrapFrame->PreviousPreviousMode = Thread->PreviousMode;

    TrapFrame->ExceptionList = KeGetPcr()->NtTib.ExceptionList;
    KeGetPcr()->NtTib.ExceptionList = EXCEPTION_CHAIN_END;

    /* Default to debugging disabled */
    TrapFrame->Dr7 = 0;

    if (KiUserTrap(TrapFrame))
    {
        /* Check for active debugging */
        if (KeGetCurrentThread()->Header.ActiveDR7)
        {
            /* Handle debug registers */
            KiHandleDebugRegistersOnTrapEntry(TrapFrame);
        }
    }

    Thread->TrapFrame = TrapFrame;
    Thread->PreviousMode = KiUserTrap(TrapFrame);

    _enable();

    Offset = (SystemCallNumber >> SERVICE_TABLE_SHIFT) & SERVICE_TABLE_MASK;
    Id = SystemCallNumber & SERVICE_NUMBER_MASK;

    Thread->SystemCallNumber = SystemCallNumber;
    Thread->FirstArgument = NULL;
    if (!KiUserTrap(TrapFrame))
    {
        Thread->FirstArgument = *(PVOID*)Arguments;
    }
    else if ((ULONG_PTR)Arguments < (ULONG_PTR)MmUserProbeAddress)
    {
        _SEH2_TRY
        {
            Thread->FirstArgument = *(volatile PVOID*)Arguments;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    DescriptorTable = (PVOID)((ULONG_PTR)Thread->ServiceTable + Offset);

    if ((Offset & SERVICE_TABLE_TEST) &&
        PsGetCurrentProcess()->MitigationFlagsValues.DisallowWin32kSystemCalls)
    {
        Status = STATUS_INVALID_SYSTEM_SERVICE;
        goto ExitCall;
    }

    if (__builtin_expect(Id >= DescriptorTable->Limit, 0))
    {
        if (!(Offset & SERVICE_TABLE_TEST))
        {
            Status = STATUS_INVALID_SYSTEM_SERVICE;
            goto ExitCall;
        }

        Status = KiConvertToGuiThread();

        TrapFrame = *(volatile PVOID*)&Thread->TrapFrame;
        DescriptorTable = (PVOID)(*(volatile ULONG_PTR*)&Thread->ServiceTable + Offset);

        if (!NT_SUCCESS(Status))
        {
            Status = KiGetGuiServiceFailureStatus(Id);
            goto ExitCall;
        }

        if (Id >= DescriptorTable->Limit)
        {
            Status = STATUS_INVALID_SYSTEM_SERVICE;
            goto ExitCall;
        }
    }

    if (__builtin_expect(Offset & SERVICE_TABLE_TEST, 0))
    {
        if (NtCurrentTeb()->GdiBatchCount) KeGdiFlushUserBatch();
    }

    KeGetCurrentPrcb()->KeSystemCalls++;

    /* FIXME: Increase individual counts on debug systems */
    StackBytes = DescriptorTable->Number[Id];

    if (__builtin_expect((Arguments < (PVOID)MmUserProbeAddress) && !(KiUserTrap(TrapFrame)), 0))
    {
        UNIMPLEMENTED_FATAL();
    }

    /* Call pre-service debug hook */
    KiDbgPreServiceHook(SystemCallNumber, Arguments);

    Handler = (PVOID)DescriptorTable->Base[Id];
    Status = KiSystemCallTrampoline(Handler, Arguments, StackBytes);

    /* Call post-service debug hook */
    Status = KiDbgPostServiceHook(SystemCallNumber, Status);

    KiExitSystemCallDebugChecks(Id, TrapFrame);

ExitCall:
    Thread->TrapFrame = (PKTRAP_FRAME)TrapFrame->Edx;

    KiServiceExit(TrapFrame, Status);
}

VOID
FASTCALL
KiCheckForSListAddress(
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNIMPLEMENTED;
}

VOID
NTAPI
Kei386EoiHelper(VOID)
{
    KeBugCheck(MISMATCHED_HAL);
}
