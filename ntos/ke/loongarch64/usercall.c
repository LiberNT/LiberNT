/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 NT user APC, callback and context-return contract
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
KeRaiseUserException(_In_ NTSTATUS ExceptionCode)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME Frame = Thread->TrapFrame;
    NTSTATUS Status;

    if (!Thread->Teb || !Frame || !KiLa64UserTrap(Frame) ||
        !KeRaiseUserExceptionDispatcher)
    {
        return STATUS_UNSUCCESSFUL;
    }

    Status = KiLa64CopyToUser(&Thread->Teb->ExceptionCode,
                              &ExceptionCode, sizeof(ExceptionCode));
    if (!NT_SUCCESS(Status))
        return Status;

    /* The trap frame's $ra is the user caller's link, so the C dispatcher
     * returns to it directly instead of through the syscall stub. */
    Frame->Context.Pc = (ULONG_PTR)KeRaiseUserExceptionDispatcher;
    return ExceptionCode;
}

static
BOOLEAN
KiLa64UserControlAddress(_In_ ULONG_PTR Address)
{
    return (Address >= MM_ALLOCATION_GRANULARITY) && (Address < (ULONG_PTR)MmUserProbeAddress);
}

static
VOID
KiLa64UserTransitionFailure(_Inout_ PKTRAP_FRAME Frame, _In_ NTSTATUS Status)
{
    EXCEPTION_RECORD Record = {0};

    Record.ExceptionCode = Status;
    Record.ExceptionAddress = (PVOID)Frame->Context.Pc;
    KiDispatchException(&Record, NULL, Frame, UserMode, TRUE);
}

VOID
NTAPI
KiInitializeUserApc(
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME Frame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    CONTEXT Context = {0};
    ULONG_PTR Stack;
    NTSTATUS Status;

    ASSERT(KiLa64UserTrap(Frame));
    if (!KiLa64UserControlAddress(Frame->Context.Sp) ||
        (Frame->Context.Sp < MM_ALLOCATION_GRANULARITY + sizeof(Context)) ||
        !KiLa64UserControlAddress((ULONG_PTR)NormalRoutine) ||
        !KiLa64UserControlAddress((ULONG_PTR)KeUserApcDispatcher) ||
        ((ULONG_PTR)NormalRoutine & 1) || ((ULONG_PTR)KeUserApcDispatcher & 1))
    {
        KiLa64UserTransitionFailure(Frame, STATUS_ACCESS_VIOLATION);
        return;
    }

    Stack = ALIGN_DOWN_BY(Frame->Context.Sp - sizeof(Context), 16);
    Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(Frame, ExceptionFrame, &Context);
    Status = KiLa64CopyToUser((PVOID)Stack, &Context, sizeof(Context));
    if (!NT_SUCCESS(Status))
    {
        KiLa64UserTransitionFailure(Frame, Status);
        return;
    }

    /* The user dispatcher supplies the saved CONTEXT as the fourth normal
     * routine argument; the first three keep the NT APC calling convention. */
    Frame->Context.A0 = (ULONG_PTR)NormalContext;
    Frame->Context.A1 = (ULONG_PTR)SystemArgument1;
    Frame->Context.A2 = (ULONG_PTR)SystemArgument2;
    Frame->Context.A3 = (ULONG_PTR)NormalRoutine;
    Frame->Context.Sp = Stack;
    Frame->Context.Pc = (ULONG_PTR)KeUserApcDispatcher;
    Frame->Context.Ra = 0;
    Frame->Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
}

/* Fault in the initial execution, stack and TEB pages and verify the actual
 * U/R/W/X permissions. Hardware still enforces them after this check. */
typedef enum _KI_LA64_USER_ACCESS
{
    KiLa64AccessRead = 0,
    KiLa64AccessWrite,
    KiLa64AccessExecute
} KI_LA64_USER_ACCESS;

static
NTSTATUS
KiLa64CheckUserPage(_In_ PVOID Address, _In_ KI_LA64_USER_ACCESS Access, _Inout_ PKTRAP_FRAME Frame)
{
    MI_LA64_PAGE_WALK Walk;
    NTSTATUS Status;
    ULONG64 Required = MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT | MI_LA64_PTE_PLV_USER;
    ULONG Fault = (Access == KiLa64AccessExecute) ? MI_LA64_FAULT_EXECUTE :
                  (Access == KiLa64AccessWrite) ? MI_LA64_FAULT_WRITE : 0;

    if (Access == KiLa64AccessWrite)
        Required |= MI_LA64_PTE_WRITE | MI_LA64_PTE_DIRTY;

    for (;;)
    {
        Status = MiLa64WalkCurrentPageTables(Address, &Walk);
        if (NT_SUCCESS(Status))
        {
            /* An executable user page is VALID, PRESENT, PLV 3 and not NX;
             * every other access is that plus the permission it names. */
            if (((Walk.Value.u.Long & Required) == Required) &&
                ((Access != KiLa64AccessExecute) || !(Walk.Value.u.Long & MI_LA64_PTE_NX)))
            {
                return STATUS_SUCCESS;
            }
            Fault |= MI_LA64_FAULT_PRESENT;
        }
        Status = MmAccessFault(Fault, Address, UserMode, Frame);
        if (!NT_SUCCESS(Status))
            return Status;

        /* A stack guard fault is the first half of stack growth: the memory
         * manager consumed the old guard and installed the next one.
         * Complete the second fault before checking the leaf. */
        if (Status == STATUS_PAGE_FAULT_GUARD_PAGE)
        {
            Status = MmAccessFault(Fault, Address, UserMode, Frame);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        Status = MiLa64WalkCurrentPageTables(Address, &Walk);
        if (!NT_SUCCESS(Status)) 
            return STATUS_ACCESS_VIOLATION;
        if (((Walk.Value.u.Long & Required) == Required) &&
            ((Access != KiLa64AccessExecute) || !(Walk.Value.u.Long & MI_LA64_PTE_NX)))
        {
            return STATUS_SUCCESS;
        }
        return STATUS_ACCESS_VIOLATION;
    }
}

/* Publish the user mode the frame returns to and resume it. ERTN takes
 * CRMD.PLV and CRMD.IE from PRMD ([V1-7.4.2]), so a prepared frame is only
 * a user frame once PRMD says so. */
static
DECLSPEC_NORETURN
VOID
KiLa64ResumeFrame(_Inout_ PKTRAP_FRAME Frame)
{
    if (KiLa64UserTrap(Frame))
        KiLa64WritePrmd((Frame->Prmd & ~LA64_PRMD_PPLV_MASK) | LA64_PRMD_PPLV_USER | LA64_PRMD_PIE);
    else
        KiLa64WritePrmd((Frame->Prmd & ~LA64_PRMD_PPLV_MASK) | LA64_PRMD_PIE);

    KiLa64RestoreTrapFrame(Frame);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiLa64ReturnToUser(_Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(KiLa64UserTrap(Frame));
Retry:
    for (;;)
    {
        KeDisableInterrupts();
        if (!Thread->ApcState.UserApcPending)
            break;
        KfRaiseIrql(APC_LEVEL);
        KeRestoreInterrupts(TRUE);
        KiDeliverApc(UserMode, NULL, Frame);
        KfLowerIrql(PASSIVE_LEVEL);
    }
    KeRestoreInterrupts(TRUE);

    /* LoongArch64 instructions are four bytes wide, so an entry point that
     * is not word aligned cannot be executed. */
    if (!KiLa64UserControlAddress(Frame->Context.Pc) || (Frame->Context.Pc & 3) ||
        !KiLa64UserControlAddress(Frame->Context.Sp) || (Frame->Context.Sp & 15))
    {
        KiLa64UserTransitionFailure(Frame, STATUS_ACCESS_VIOLATION);
    }
    if (!Thread->Header.Minimal)
    {
        Status = KiLa64CheckUserPage((PVOID)Frame->Context.Pc, KiLa64AccessExecute, Frame);
        if (NT_SUCCESS(Status))
            Status = KiLa64CheckUserPage((PVOID)(Frame->Context.Sp - 1), KiLa64AccessWrite, Frame);
        if (NT_SUCCESS(Status))
            Status = KiLa64CheckUserPage(Thread->Teb, KiLa64AccessWrite, Frame);
        if (!NT_SUCCESS(Status))
            KiLa64UserTransitionFailure(Frame, Status);
    }

    KeDisableInterrupts();
    if (Thread->ApcState.UserApcPending)
        goto Retry;
    Frame->Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
    Thread->PreviousMode = UserMode;
    Thread->TrapFrame = Frame->PreviousTrapFrame;
    KiLa64ResumeFrame(Frame);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiLa64ServiceExit(_Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    KeDisableInterrupts();
    if (Thread->ApcState.UserApcPending ||
        !KiLa64UserControlAddress(Frame->Context.Pc) || (Frame->Context.Pc & 3) ||
        !KiLa64UserControlAddress(Frame->Context.Sp) || (Frame->Context.Sp & 15))
    {
        KeRestoreInterrupts(TRUE);
        KiLa64ReturnToUser(Frame);
    }

    Frame->Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
    Thread->PreviousMode = UserMode;
    Thread->TrapFrame = Frame->PreviousTrapFrame;
    KiLa64ResumeFrame(Frame);
}

/* NtRaiseException and NtContinue leave through the system service's frame.
 * A kernel-mode continuation resumes through RtlRestoreContext. */
DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    UNREFERENCED_PARAMETER(ExceptionFrame);

    if (KiLa64UserTrap(TrapFrame))
        KiLa64ReturnToUser(TrapFrame);

    /* A kernel frame resumes exactly where the context says, with the mode
     * the frame already carries. */
    KiLa64ResumeFrame(TrapFrame);
}

NTSTATUS
NTAPI
KiLa64UserModeCallout(_Inout_ PKTRAP_FRAME Frame, _Out_ PKCALLOUT_FRAME CalloutFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR InitialStack = (ULONG_PTR)CalloutFrame;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!(InitialStack & 15));
    ASSERT(InitialStack >= Thread->StackLimit);
    ASSERT(InitialStack + sizeof(*CalloutFrame) <= (ULONG_PTR)Thread->InitialStack);

    /* A U-origin trap below the suspended call needs a full committed kernel
     * stack. Grow only the existing GUI reservation, never a small stack. */
    if (InitialStack - Thread->StackLimit < KERNEL_STACK_SIZE)
    {
        if (!Thread->LargeStack)
            return STATUS_STACK_OVERFLOW;
        Status = MmGrowKernelStack(CalloutFrame);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    CalloutFrame->CallbackStack = Thread->CallbackStack;
    CalloutFrame->InitialStack = Thread->InitialStack;
    CalloutFrame->TrapFrame = Thread->TrapFrame;
    Frame->PreviousTrapFrame = Thread->TrapFrame;

    KeDisableInterrupts();
    Thread->CallbackStack = CalloutFrame;
    Thread->InitialStack = CalloutFrame;
    Thread->TrapFrame = Frame;
    /* The prepared frame is above CalloutFrame, not in the reusable entry
     * stack below it. ReturnToUser restores its previous (outer) link. */
    KiLa64ReturnToUser(Frame);
}

NTSTATUS
NTAPI
KeUserModeCallback(
    _In_ ULONG RoutineIndex,
    _In_ PVOID Argument,
    _In_ ULONG ArgumentLength,
    _Out_ PVOID *Result,
    _Out_ PULONG ResultLength)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME OuterFrame = Thread->TrapFrame;
    KTRAP_FRAME CallbackFrame;
    UCALLOUT_FRAME UserFrame;
    ULONG_PTR OldStack, UserArguments, UserStack;
    ULONG GdiBatchCount;
    NTSTATUS Status, CallbackStatus;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Thread->PreviousMode != UserMode ||
        Thread->ApcState.KernelApcInProgress || Thread->ApcStateIndex != OriginalApcEnvironment ||
        Thread->CombinedApcDisable || !OuterFrame || !KiLa64UserTrap(OuterFrame))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Thread->CallbackNestingLevel >= KI_MAX_USER_CALLBACK_NESTING)
        return STATUS_STACK_OVERFLOW;
    if (!Result || !ResultLength || (ArgumentLength && !Argument))
        return STATUS_INVALID_PARAMETER;
    if (!KiLa64UserControlAddress((ULONG_PTR)KeUserCallbackDispatcher) ||
        ((ULONG_PTR)KeUserCallbackDispatcher & 3))
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    OldStack = OuterFrame->Context.Sp;
    if (!KiLa64UserControlAddress(OldStack) ||
        OldStack < MM_ALLOCATION_GRANULARITY + (ULONG_PTR)ArgumentLength + sizeof(UserFrame))
    {
        return STATUS_ACCESS_VIOLATION;
    }
    if (OldStack & 15)
        return STATUS_DATATYPE_MISALIGNMENT;
    UserArguments = ALIGN_DOWN_BY(OldStack - ArgumentLength, 16);
    UserStack = UserArguments - sizeof(UserFrame);

    /* Only publish the callback after both protected copies succeed. A
     * failed copy may leave a user-buffer prefix, never a new kernel link. */
    Status = KiLa64CopyToUser((PVOID)UserArguments, Argument, ArgumentLength);
    if (!NT_SUCCESS(Status))
        return Status;
    UserFrame.Buffer = (PVOID)UserArguments;
    UserFrame.Length = ArgumentLength;
    UserFrame.ApiNumber = RoutineIndex;
    UserFrame.Pc = OuterFrame->Context.Pc;
    UserFrame.Sp = OldStack;
    Status = KiLa64CopyToUser((PVOID)UserStack, &UserFrame, sizeof(UserFrame));
    if (!NT_SUCCESS(Status))
        return Status;

    CallbackFrame = *OuterFrame;
    CallbackFrame.Context.Pc = (ULONG_PTR)KeUserCallbackDispatcher;
    CallbackFrame.Context.Sp = UserStack;
    CallbackFrame.Context.Ra = 0;
    CallbackFrame.Context.A0 = RoutineIndex;
    CallbackFrame.Context.A1 = UserArguments;
    CallbackFrame.Context.A2 = ArgumentLength;
    CallbackFrame.Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
    Status = KiLa64CheckUserPage((PVOID)CallbackFrame.Context.Pc, KiLa64AccessExecute, &CallbackFrame);
    if (NT_SUCCESS(Status))
        Status = KiLa64CheckUserPage((PVOID)(UserStack - 1), KiLa64AccessWrite, &CallbackFrame);
    if (NT_SUCCESS(Status))
        Status = KiLa64CheckUserPage(Thread->Teb, KiLa64AccessWrite, &CallbackFrame);
    if (!NT_SUCCESS(Status))
        return Status;

    Thread->CallbackNestingLevel++;
    /* TODO: a callout this stack cannot hold fails instead of going on through KiCallUserModeWithStack */
    CallbackStatus = KiLa64CallUserMode(&CallbackFrame, Result, ResultLength);
    Thread->CallbackNestingLevel--;
    ASSERT(Thread->TrapFrame == OuterFrame);
    /* POP_STACK supplies a new user continuation; ordinary callbacks leave
     * the interrupted context untouched, including its complete FP bank. */
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
        OldStack = OuterFrame->Context.Sp;

    Status = KiLa64CopyFromUser(&GdiBatchCount,
                                (PUCHAR)Thread->Teb + FIELD_OFFSET(TEB, GdiBatchCount),
                                sizeof(GdiBatchCount));
    if (!NT_SUCCESS(Status))
        return Status;
    if (GdiBatchCount)
    {
        if (!KeGdiFlushUserBatch || OldStack < MM_ALLOCATION_GRANULARITY + 256)
            return STATUS_INVALID_DEVICE_STATE;
        OuterFrame->Context.Sp = OldStack - 256;
        KeGdiFlushUserBatch();
    }
    OuterFrame->Context.Sp = OldStack;
    return CallbackStatus;
}

NTSTATUS
NTAPI
NtCallbackReturn(_In_ PVOID Result, _In_ ULONG ResultLength, _In_ NTSTATUS CallbackStatus)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKCALLOUT_FRAME CalloutFrame = Thread->CallbackStack;
    PKTRAP_FRAME CurrentFrame = Thread->TrapFrame;
    PKTRAP_FRAME OuterFrame;

    if (!CalloutFrame)
        return STATUS_NO_CALLBACK_ACTIVE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Thread->PreviousMode != UserMode ||
        Thread->ApcStateIndex != OriginalApcEnvironment || Thread->CombinedApcDisable ||
        !CurrentFrame || !KiLa64UserTrap(CurrentFrame))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    ASSERT((PVOID)CalloutFrame == Thread->InitialStack);
    ASSERT((ULONG_PTR)CalloutFrame >= Thread->StackLimit);
    ASSERT((ULONG_PTR)CalloutFrame + sizeof(*CalloutFrame) <= (ULONG_PTR)Thread->StackBase);
    OuterFrame = CalloutFrame->TrapFrame;
    ASSERT(OuterFrame && KiLa64UserTrap(OuterFrame));

    if (CallbackStatus == STATUS_CALLBACK_POP_STACK &&
        (!KiLa64UserControlAddress(CurrentFrame->Context.Pc) || (CurrentFrame->Context.Pc & 3) ||
         !KiLa64UserControlAddress(CurrentFrame->Context.Sp) || (CurrentFrame->Context.Sp & 15)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* The tuple is user data, not a kernel buffer to dereference. Its NT
     * caller remains responsible for probing or capturing returned bytes. */
    *CalloutFrame->OutputBuffer = Result;
    *CalloutFrame->OutputLength = ResultLength;
    KeDisableInterrupts();
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        OuterFrame->Context = CurrentFrame->Context;
        OuterFrame->Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
        /* In particular, never copy CurrentFrame's PreviousTrapFrame link:
         * it belongs to the callback stack that is being discarded. */
    }
    Thread->InitialStack = CalloutFrame->InitialStack;
    Thread->TrapFrame = OuterFrame;
    Thread->CallbackStack = CalloutFrame->CallbackStack;
    Thread->PreviousMode = UserMode;
    KiLa64CallbackReturn(CalloutFrame, CallbackStatus);
}
