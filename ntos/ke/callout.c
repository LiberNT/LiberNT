/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calls from kernel mode into user mode and their return, shared by the architectures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

#define KI_GDI_BATCH_FLUSH_SPACE 256

static
KI_USER_CALLOUT_FRAME *
KiPushCalloutFrame(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG_PTR UserStack,
    _In_ ULONG RoutineIndex,
    _In_reads_bytes_(Length) PVOID Arguments,
    _In_ ULONG Length)
{
    KI_USER_CALLOUT_FRAME *Frame;
    ULONG_PTR Page;

    Frame = (KI_USER_CALLOUT_FRAME *)ALIGN_DOWN_POINTER_BY(UserStack - KI_CALLOUT_STACK_RESERVE - Length - sizeof(*Frame), 16);

    /* The user stack grows only through its guard page, so it is touched from the top down */
    for (Page = (ULONG_PTR)PAGE_ALIGN(UserStack - 1); Page > (ULONG_PTR)Frame; Page -= PAGE_SIZE)
        ProbeForWrite((PVOID)Page, sizeof(UCHAR), sizeof(UCHAR));

    ProbeForWrite(Frame, sizeof(*Frame) + Length, sizeof(PVOID));
    RtlCopyMemory(Frame + 1, Arguments, Length);

    KiSetupUserCalloutFrame(Frame, Thread->TrapFrame, RoutineIndex, Frame + 1, Length);
    return Frame;
}

NTSTATUS
NTAPI
KeUserModeCallback(
    _In_ ULONG RoutineIndex,
    _In_reads_bytes_(ArgumentLength) PVOID Argument,
    _In_ ULONG ArgumentLength,
    _Out_ PVOID *Result,
    _Out_ PULONG ResultLength)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PULONG_PTR UserStack;
    ULONG_PTR SavedStack;
    ULONG_PTR UserState;
    NTSTATUS Status;
    ULONG BatchCount = 0;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Thread->ApcState.KernelApcInProgress == FALSE);
    ASSERT(KeGetPreviousMode() == UserMode);

    if (Thread->CallbackNestingLevel >= KI_MAX_USER_CALLBACK_NESTING)
        return STATUS_STACK_OVERFLOW;

    UserStack = KiGetUserModeStackAddress();
    SavedStack = *UserStack;

    _SEH2_TRY
    {
        *UserStack = (ULONG_PTR)KiPushCalloutFrame(Thread, SavedStack, RoutineIndex, Argument, ArgumentLength);
        UserState = KiArchCalloutSaveUserState(Thread);

        Thread->CallbackNestingLevel++;
        Status = KiCallUserModeWithStack(NULL, Result, ResultLength);
        Thread->CallbackNestingLevel--;

        /* A callback that faulted in user mode leaves its own stack position behind */
        if (Status == STATUS_CALLBACK_POP_STACK)
            SavedStack = *UserStack;
        else
            KiArchCalloutRestoreUserState(Thread, UserState);

        BatchCount = ((PTEB)Thread->Teb)->GdiBatchCount;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *UserStack = SavedStack;
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Pending GDI batch entries are flushed with room for the flush call below the user stack */
    if (BatchCount != 0)
    {
        *UserStack -= KI_GDI_BATCH_FLUSH_SPACE;
        KeGdiFlushUserBatch();
    }

    *UserStack = SavedStack;
    return Status;
}

NTSTATUS
NTAPI
NtCallbackReturn(
    _In_opt_ PVOID Result,
    _In_ ULONG ResultLength,
    _In_ NTSTATUS CallbackStatus)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKCALLOUT_FRAME CalloutFrame = Thread->CallbackStack;
    PKTRAP_FRAME CallerFrame;

    if (CalloutFrame == NULL)
        return STATUS_NO_CALLBACK_ACTIVE;

    *(PVOID *)KI_CALLOUT_RESULT(CalloutFrame) = Result;
    *(PULONG)KI_CALLOUT_RESULT_LENGTH(CalloutFrame) = ResultLength;

    CallerFrame = (PKTRAP_FRAME)CalloutFrame->TrapFrame;

    /* The kernel stack and trap frame switch back together, with nothing able to observe them halfway */
    _disable();
    KiLeaveCallout(Thread, CalloutFrame, CallerFrame, CallbackStatus);
    Thread->TrapFrame = CallerFrame;
    _enable();

    KiCallbackReturn(KI_CALLOUT_RETURN_STACK(CalloutFrame), CallbackStatus);
}
