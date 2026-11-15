/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 user APC delivery frames and user-mode callbacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

static
VOID
KiResumeInUserMode(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Rip,
    _In_ ULONG64 Rsp)
{
    TrapFrame->Rsp = Rsp;
    TrapFrame->Rip = Rip;
    TrapFrame->SegCs = KGDT64_R3_CODE | RPL_MASK;
    TrapFrame->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
    TrapFrame->SegGs = KGDT64_R3_DATA | RPL_MASK;
    TrapFrame->SegSs = KGDT64_R3_DATA | RPL_MASK;
    TrapFrame->EFlags = EFLAGS_INTERRUPT_MASK;
    KiInstrumentTrapFrame(TrapFrame);
}

VOID
NTAPI
KiInitializeUserApc(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    PUAPC_FRAME Frame = (PUAPC_FRAME)ALIGN_DOWN_POINTER_BY(TrapFrame->Rsp - sizeof(UAPC_FRAME), 16);
    PCONTEXT Context = &Frame->Context;
    EXCEPTION_RECORD Record;

    ASSERT((TrapFrame->SegCs & MODE_MASK) != KernelMode);

    _SEH2_TRY
    {
        ProbeForWrite(Frame, sizeof(*Frame), 16);

        Context->ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
        KeTrapFrameToContext(TrapFrame, ExceptionFrame, Context);

        /* KiUserApcDispatcher takes the routine and its arguments from the home slots */
        Context->P1Home = (ULONG64)NormalContext;
        Context->P2Home = (ULONG64)SystemArgument1;
        Context->P3Home = (ULONG64)SystemArgument2;
        Context->P4Home = (ULONG64)NormalRoutine;

        /* The machine frame lets user-mode unwinding walk out of the dispatcher */
        Frame->MachineFrame.Rip = TrapFrame->Rip;
        Frame->MachineFrame.Rsp = TrapFrame->Rsp;
    }
    _SEH2_EXCEPT(Record = *_SEH2_GetExceptionInformation()->ExceptionRecord, EXCEPTION_EXECUTE_HANDLER)
    {
        /* The user stack cannot take the frame: raise the fault in the thread instead */
        Record.ExceptionAddress = (PVOID)TrapFrame->Rip;
        KiDispatchException(&Record, ExceptionFrame, TrapFrame, UserMode, TRUE);
    }
    _SEH2_END;

    KiResumeInUserMode(TrapFrame, (ULONG64)KeUserApcDispatcher, (ULONG64)Context);
}

NTSTATUS
FASTCALL
KiUserModeCallout(
    _Out_ PKCALLOUT_FRAME CalloutFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR NewInitialStack = (ULONG_PTR)ALIGN_DOWN_POINTER_BY(CalloutFrame, 16);
    PKTRAP_FRAME CallerFrame = Thread->TrapFrame;
    KTRAP_FRAME CallbackFrame;
    PKIPCR Pcr;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT((Thread->ApcStateIndex == OriginalApcEnvironment) && (Thread->CombinedApcDisable == 0));

    /* The callback runs on what is left of this stack, so it needs a full kernel stack's worth */
    if ((NewInitialStack - KERNEL_STACK_SIZE) < Thread->StackLimit)
    {
        NTSTATUS Status = MmGrowKernelStack((PVOID)NewInitialStack);

        if (!NT_SUCCESS(Status))
            return Status;
    }

    CalloutFrame->CallbackStack = (ULONG_PTR)Thread->CallbackStack;
    CalloutFrame->InitialStack = (ULONG_PTR)Thread->InitialStack;
    CalloutFrame->TrapFrame = (ULONG_PTR)CallerFrame;
    Thread->CallbackStack = CalloutFrame;

    _disable();

    Thread->InitialStack = (PVOID)NewInitialStack;
    CallbackFrame = *CallerFrame;

    Pcr = (PKIPCR)KeGetPcr();
    Pcr->TssBase->Rsp0 = NewInitialStack;
    Pcr->Prcb.RspBase = NewInitialStack;
    CallbackFrame.Rip = (ULONG_PTR)KeUserCallbackDispatcher;

    _enable();

    KiUserCallbackExit(&CallbackFrame);
}

VOID
KiSetupUserCalloutFrame(
    _Out_ PUCALLOUT_FRAME UserCalloutFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ApiNumber,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    UNREFERENCED_PARAMETER(Buffer);

    UserCalloutFrame->Buffer = (PVOID)(UserCalloutFrame + 1);
    UserCalloutFrame->Length = BufferLength;
    UserCalloutFrame->ApiNumber = ApiNumber;
    UserCalloutFrame->MachineFrame.Rip = TrapFrame->Rip;
    UserCalloutFrame->MachineFrame.Rsp = TrapFrame->Rsp;
}

VOID
KiLeaveCallout(
    _Inout_ PKTHREAD Thread,
    _In_ PKCALLOUT_FRAME CalloutFrame,
    _Inout_ PKTRAP_FRAME CallerFrame,
    _In_ NTSTATUS CallbackStatus)
{
    PKTRAP_FRAME CallbackFrame = Thread->TrapFrame;
    PKIPCR Pcr = (PKIPCR)KeGetPcr();

    /* A callback that faulted in user mode hands back its own frame */
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
        *CallerFrame = *CallbackFrame;

    CallerFrame->Dr7 = 0;
    if (Thread->Header.DebugActive & DEBUG_ACTIVE_DR7)
    {
        CallerFrame->Dr0 = CallbackFrame->Dr0;
        CallerFrame->Dr1 = CallbackFrame->Dr1;
        CallerFrame->Dr2 = CallbackFrame->Dr2;
        CallerFrame->Dr3 = CallbackFrame->Dr3;
        CallerFrame->Dr6 = CallbackFrame->Dr6;
        CallerFrame->Dr7 = CallbackFrame->Dr7;
    }

    Pcr->TssBase->Rsp0 = CalloutFrame->InitialStack;
    Pcr->Prcb.RspBase = CalloutFrame->InitialStack;
    Thread->InitialStack = (PVOID)CalloutFrame->InitialStack;
    Thread->CallbackStack = (PVOID)CalloutFrame->CallbackStack;
}

