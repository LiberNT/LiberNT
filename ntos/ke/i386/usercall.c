/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 user APC delivery frames and user-mode callbacks
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
KiInitializeUserApc(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
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

static
VOID
KiCopyFloatingControl(
    _Out_ PFX_SAVE_AREA To,
    _In_ PFX_SAVE_AREA From)
{
    To->U.FnArea.ControlWord = From->U.FnArea.ControlWord;
    To->U.FnArea.StatusWord = From->U.FnArea.StatusWord;
    To->U.FnArea.TagWord = From->U.FnArea.TagWord;
    To->U.FnArea.DataSelector = From->U.FnArea.DataSelector;
    To->Cr0NpxState = From->Cr0NpxState;
}

NTSTATUS
FASTCALL
KiUserModeCallout(
    PKCALLOUT_FRAME CalloutFrame)
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

    KiCopyFloatingControl(FxSaveArea, OldFxSaveArea);

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

VOID
KiSetupUserCalloutFrame(
    _Out_ KI_USER_CALLOUT_FRAME *UserCalloutFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ApiNumber,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    UNREFERENCED_PARAMETER(TrapFrame);

    UserCalloutFrame->ReturnAddress = 0;
    UserCalloutFrame->ApiNumber = ApiNumber;
    UserCalloutFrame->Buffer = Buffer;
    UserCalloutFrame->Length = BufferLength;
}

VOID
KiLeaveCallout(
    _Inout_ PKTHREAD Thread,
    _In_ PKCALLOUT_FRAME CalloutFrame,
    _Inout_ PKTRAP_FRAME CallerFrame,
    _In_ NTSTATUS CallbackStatus)
{
    PKTRAP_FRAME CallbackFrame = Thread->TrapFrame;
    PKPCR Pcr = KeGetPcr();
    ULONG Size;

    Pcr->NtTib.ExceptionList = CallbackFrame->ExceptionList;

    /* The caller goes on with the floating-point control state the callback left */
    KiCopyFloatingControl((PFX_SAVE_AREA)(CalloutFrame->InitialStack - sizeof(FX_SAVE_AREA)),
                          (PFX_SAVE_AREA)((ULONG_PTR)Thread->InitialStack - sizeof(FX_SAVE_AREA)));

    /* A callback that faulted in user mode hands back its own frame; only a V86 frame carries the V86 segments */
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        Size = (CallbackFrame->EFlags & EFLAGS_V86_MASK) ? sizeof(KTRAP_FRAME) - FIELD_OFFSET(KTRAP_FRAME, SegFs)
                                                        : FIELD_OFFSET(KTRAP_FRAME, V86Es) - FIELD_OFFSET(KTRAP_FRAME, SegFs);
        RtlCopyMemory(&CallerFrame->SegFs, &CallbackFrame->SegFs, Size);
    }

    CallerFrame->Dr7 = 0;
    if (Thread->Header.ActiveDR7)
    {
        CallerFrame->Dr0 = CallbackFrame->Dr0;
        CallerFrame->Dr1 = CallbackFrame->Dr1;
        CallerFrame->Dr2 = CallbackFrame->Dr2;
        CallerFrame->Dr3 = CallbackFrame->Dr3;
        CallerFrame->Dr6 = CallbackFrame->Dr6;
        CallerFrame->Dr7 = CallbackFrame->Dr7;
    }

    Pcr->TSS->Esp0 = (CallerFrame->EFlags & EFLAGS_V86_MASK) ? (ULONG_PTR)(CallerFrame + 1) : (ULONG_PTR)&CallerFrame->V86Es;
    Thread->InitialStack = (PVOID)CalloutFrame->InitialStack;
    Thread->CallbackStack = (PVOID)CalloutFrame->CallbackStack;
}
