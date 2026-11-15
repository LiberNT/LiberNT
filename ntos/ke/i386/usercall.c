/*
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern PGDI_BATCHFLUSH_ROUTINE KeGdiFlushUserBatch;

VOID
NTAPI
{
    CONTEXT Context = { 0 };
    ULONG_PTR Stack, AlignedEsp;
    ULONG ContextLength;
    EXCEPTION_RECORD SehExceptRecord;

    if (TrapFrame->EFlags & EFLAGS_V86_MASK) return;

    Context.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);

    _SEH2_TRY
    {
        ASSERT(KiUserTrap(TrapFrame));

        AlignedEsp = Context.Esp & ~3;
        ContextLength = CONTEXT_ALIGNED_SIZE + (4 * sizeof(ULONG_PTR));
        Stack = ((AlignedEsp - 8) & ~3) - ContextLength;

        ProbeForWrite((PVOID)Stack, AlignedEsp - Stack, 1);
        ASSERT(!(Stack & 3));

        RtlCopyMemory((PVOID)(Stack + (4 * sizeof(ULONG_PTR))),
                      &Context,
                      sizeof(CONTEXT));

        TrapFrame->Eip = (ULONG)KeUserApcDispatcher;
        TrapFrame->HardwareEsp = Stack;

        TrapFrame->SegCs = Ke386SanitizeSeg(KGDT_R3_CODE, UserMode);
        TrapFrame->HardwareSegSs = Ke386SanitizeSeg(KGDT_R3_DATA, UserMode);
        TrapFrame->SegDs = Ke386SanitizeSeg(KGDT_R3_DATA, UserMode);
        TrapFrame->SegEs = Ke386SanitizeSeg(KGDT_R3_DATA, UserMode);
        TrapFrame->SegFs = Ke386SanitizeSeg(KGDT_R3_TEB, UserMode);
        TrapFrame->SegGs = 0;
        TrapFrame->ErrCode = 0;

        TrapFrame->EFlags = Ke386SanitizeFlags(Context.EFlags, UserMode);

        if (KeGetCurrentThread()->Iopl) TrapFrame->EFlags |= EFLAGS_IOPL;

        *(PULONG_PTR)(Stack + 0 * sizeof(ULONG_PTR)) = (ULONG_PTR)NormalRoutine;
        *(PULONG_PTR)(Stack + 1 * sizeof(ULONG_PTR)) = (ULONG_PTR)NormalContext;
        *(PULONG_PTR)(Stack + 2 * sizeof(ULONG_PTR)) = (ULONG_PTR)SystemArgument1;
        *(PULONG_PTR)(Stack + 3 * sizeof(ULONG_PTR)) = (ULONG_PTR)SystemArgument2;
    }
    _SEH2_EXCEPT((RtlCopyMemory(&SehExceptRecord, _SEH2_GetExceptionInformation()->ExceptionRecord, sizeof(EXCEPTION_RECORD)), EXCEPTION_EXECUTE_HANDLER))
    {
        SehExceptRecord.ExceptionAddress = (PVOID)TrapFrame->Eip;
        KiDispatchException(&SehExceptRecord,
                            ExceptionFrame,
                            TrapFrame,
                            UserMode,
                            TRUE);
    }
    _SEH2_END;
}

NTSTATUS
NTAPI
{
    ULONG_PTR NewStack, OldStack;
    PULONG UserEsp;
    NTSTATUS CallbackStatus;
    PEXCEPTION_REGISTRATION_RECORD ExceptionList;
    PTEB Teb;
    ULONG GdiBatchCount = 0;
    ASSERT(KeGetCurrentThread()->ApcState.KernelApcInProgress == FALSE);
    ASSERT(KeGetPreviousMode() == UserMode);

    if (KeGetCurrentThread()->CallbackNestingLevel >= KI_MAX_USER_CALLBACK_NESTING)
        return STATUS_STACK_OVERFLOW;

    UserEsp = KiGetUserModeStackAddress();
    OldStack = *UserEsp;

    _SEH2_TRY
    {
        NewStack = (OldStack - ArgumentLength) & ~3;

        ProbeForWrite((PVOID)(NewStack - 6 * sizeof(ULONG_PTR)),
                      ArgumentLength + 6 * sizeof(ULONG_PTR),
                      sizeof(CHAR));

        RtlCopyMemory((PVOID)NewStack, Argument, ArgumentLength);

        NewStack -= 24;
        *(PULONG)NewStack = 0;
        *(PULONG)(NewStack + 4) = RoutineIndex;
        *(PULONG)(NewStack + 8) = (NewStack + 24);
        *(PULONG)(NewStack + 12) = ArgumentLength;

        Teb = KeGetCurrentThread()->Teb;
        ExceptionList = Teb->NtTib.ExceptionList;

        *UserEsp = NewStack;
        KeGetCurrentThread()->CallbackNestingLevel++;
        CallbackStatus = KiCallUserModeWithStack(NULL, Result, ResultLength);
        KeGetCurrentThread()->CallbackNestingLevel--;
        if (CallbackStatus != STATUS_CALLBACK_POP_STACK)
        {
            Teb->NtTib.ExceptionList = ExceptionList;
        }
        else
        {
            OldStack = *UserEsp;
        }

        GdiBatchCount = Teb->GdiBatchCount;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (GdiBatchCount)
    {
          *UserEsp -= 256;
          KeGdiFlushUserBatch();
    }

    *UserEsp = OldStack;
    return CallbackStatus;
}

NTSTATUS
FASTCALL
{
    PKTHREAD CurrentThread;
    PKTRAP_FRAME TrapFrame, CallbackTrapFrame;
    PFX_SAVE_AREA FxSaveArea, OldFxSaveArea;
    PKPCR Pcr;
    PKTSS Tss;
    ULONG_PTR InitialStack;
    NTSTATUS Status;

    CurrentThread = KeGetCurrentThread();

#if DBG
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        /* We're not, bugcheck */
        KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE,
                     0,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }

    if ((CurrentThread->ApcStateIndex != OriginalApcEnvironment) ||
        (CurrentThread->CombinedApcDisable > 0))
    {
        KeBugCheckEx(APC_INDEX_MISMATCH,
                     0,
                     CurrentThread->ApcStateIndex,
                     CurrentThread->CombinedApcDisable,
                     0);
    }
#endif

    InitialStack = ALIGN_DOWN_BY(CalloutFrame, 16);

    if ((InitialStack - KERNEL_STACK_SIZE) < CurrentThread->StackLimit)
    {
        Status = MmGrowKernelStack((PVOID)InitialStack);

        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_STACK_OVERFLOW)
            {
                DPRINT1("Thread wants too much stack\n");
            }

            return Status;
        }
    }

    CalloutFrame->CallbackStack = (ULONG_PTR)CurrentThread->CallbackStack;
    CalloutFrame->InitialStack = (ULONG_PTR)CurrentThread->InitialStack;

    TrapFrame = CurrentThread->TrapFrame;
    CalloutFrame->TrapFrame = (ULONG_PTR)TrapFrame;

    CurrentThread->CallbackStack = CalloutFrame;

    OldFxSaveArea = (PVOID)(CalloutFrame->InitialStack - sizeof(FX_SAVE_AREA));
    FxSaveArea = (PVOID)(InitialStack - sizeof(FX_SAVE_AREA));

    _disable();

    FxSaveArea->U.FnArea.ControlWord = OldFxSaveArea->U.FnArea.ControlWord;
    FxSaveArea->U.FnArea.StatusWord = OldFxSaveArea->U.FnArea.StatusWord;
    FxSaveArea->U.FnArea.TagWord = OldFxSaveArea->U.FnArea.TagWord;
    FxSaveArea->U.FnArea.DataSelector = OldFxSaveArea->U.FnArea.DataSelector;
    FxSaveArea->Cr0NpxState = OldFxSaveArea->Cr0NpxState;

    CurrentThread->InitialStack = (PVOID)InitialStack;

    CallbackTrapFrame = (PVOID)((ULONG_PTR)FxSaveArea - sizeof(KTRAP_FRAME));

    *CallbackTrapFrame = *TrapFrame;

    Pcr = KeGetPcr();

    CallbackTrapFrame->ExceptionList = Pcr->NtTib.ExceptionList;

    Tss = Pcr->TSS;

    if (CallbackTrapFrame->EFlags & EFLAGS_V86_MASK)
    {
        Tss->Esp0 = (ULONG_PTR)(CallbackTrapFrame + 1);
    }
    else
    {
        Tss->Esp0 = (ULONG_PTR)&CallbackTrapFrame->V86Es;
    }

    CallbackTrapFrame->Eip = (ULONG_PTR)KeUserCallbackDispatcher;

    _enable();

    KiServiceExit(CallbackTrapFrame, 0);
}

NTSTATUS
NTAPI
NtCallbackReturn(
    _In_ PVOID Result,
    _In_ ULONG ResultLength,
    _In_ NTSTATUS CallbackStatus)
{
    PKTHREAD CurrentThread;
    PKCALLOUT_FRAME CalloutFrame;
    PKTRAP_FRAME CallbackTrapFrame, TrapFrame;
    PFX_SAVE_AREA FxSaveArea, CbFxSaveArea;
    ULONG Size;
    PKPCR Pcr;
    PKTSS Tss;

    CurrentThread = KeGetCurrentThread();
    CalloutFrame = CurrentThread->CallbackStack;
    if (CalloutFrame == NULL)
    {
        return STATUS_NO_CALLBACK_ACTIVE;
    }

    CallbackTrapFrame = CurrentThread->TrapFrame;

    Pcr = KeGetPcr();
    Pcr->NtTib.ExceptionList = CallbackTrapFrame->ExceptionList;

    *((PVOID*)CalloutFrame->Result) = Result;
    *((ULONG*)CalloutFrame->ResultLength) = ResultLength;

    _disable();

    CbFxSaveArea = (PVOID)((ULONG)CurrentThread->InitialStack - sizeof(FX_SAVE_AREA));
    FxSaveArea = (PVOID)(CalloutFrame->InitialStack - sizeof(FX_SAVE_AREA));

    FxSaveArea->U.FnArea.ControlWord = CbFxSaveArea->U.FnArea.ControlWord;
    FxSaveArea->U.FnArea.StatusWord = CbFxSaveArea->U.FnArea.StatusWord;
    FxSaveArea->U.FnArea.TagWord = CbFxSaveArea->U.FnArea.TagWord;
    FxSaveArea->U.FnArea.DataSelector = CbFxSaveArea->U.FnArea.DataSelector;
    FxSaveArea->Cr0NpxState = CbFxSaveArea->Cr0NpxState;

    TrapFrame = (PKTRAP_FRAME)CalloutFrame->TrapFrame;

    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        if (CallbackTrapFrame->EFlags & EFLAGS_V86_MASK)
        {
            Size = sizeof(KTRAP_FRAME) - FIELD_OFFSET(KTRAP_FRAME, SegFs);
        }
        else
        {
            Size = FIELD_OFFSET(KTRAP_FRAME, V86Es) - FIELD_OFFSET(KTRAP_FRAME, SegFs);
        }

        RtlCopyMemory(&TrapFrame->SegFs, &CallbackTrapFrame->SegFs, Size);
    }

    TrapFrame->Dr7 = 0;

    /* Check if debugging was active */
    if (CurrentThread->Header.ActiveDR7)
    {
        /* Copy debug registers data from it */
        TrapFrame->Dr0 = CallbackTrapFrame->Dr0;
        TrapFrame->Dr1 = CallbackTrapFrame->Dr1;
        TrapFrame->Dr2 = CallbackTrapFrame->Dr2;
        TrapFrame->Dr3 = CallbackTrapFrame->Dr3;
        TrapFrame->Dr6 = CallbackTrapFrame->Dr6;
        TrapFrame->Dr7 = CallbackTrapFrame->Dr7;
    }

    Tss = Pcr->TSS;

    if (TrapFrame->EFlags & EFLAGS_V86_MASK)
    {
        Tss->Esp0 = (ULONG_PTR)(TrapFrame + 1);
    }
    else
    {
        Tss->Esp0 = (ULONG_PTR)&TrapFrame->V86Es;
    }

    CurrentThread->InitialStack = (PVOID)CalloutFrame->InitialStack;

    CurrentThread->TrapFrame = TrapFrame;
    CurrentThread->CallbackStack = (PVOID)CalloutFrame->CallbackStack;

    _enable();

    KiCallbackReturn(&CalloutFrame->Edi, CallbackStatus);
}
