/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Raising an exception against a thread, and putting one back together
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Replaces a thread's register state with the one in a context record, so that
 * when it leaves the kernel it carries on from there instead of from where it
 * was interrupted.
 *
 * The extended state goes in first: it is the part that can be refused, and
 * refusing it after the integer state had already been written would leave the
 * thread half moved.
 *
 * APCs are kept out while the frames are rewritten. One delivered in the
 * middle would be built on a trap frame that is partly the old state and
 * partly the new.
 */
NTSTATUS
NTAPI
KiContinue(
    _In_ PCONTEXT Context,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KIRQL OldIrql = KeGetCurrentIrql();
    NTSTATUS Status;

    if (OldIrql < APC_LEVEL)
        KfRaiseIrql(APC_LEVEL);

    Status = KiContinueExtendedState(Context, Context->ContextFlags);
    if (NT_SUCCESS(Status))
    {
        KeContextToTrapFrame(Context,
                             ExceptionFrame,
                             TrapFrame,
                             Context->ContextFlags,
                             Thread->PreviousMode);
    }

    if (OldIrql < APC_LEVEL)
        KeLowerIrql(OldIrql);

    return Status;
}

/*
 * The same for a context that came from user mode. It is copied into the
 * kernel first, under a handler, so that the frames are never written from
 * memory the caller could still be changing.
 */
NTSTATUS
NTAPI
KiContinuePreviousModeUser(
    _In_ PCONTEXT Context,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    CONTEXT LocalContext;

    _SEH2_TRY
    {
        ProbeForRead(Context, sizeof(CONTEXT), TYPE_ALIGNMENT(CONTEXT));
        RtlCopyMemory(&LocalContext, Context, sizeof(CONTEXT));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return KiContinue(&LocalContext, ExceptionFrame, TrapFrame);
}

/*
 * Carries on from a context the caller supplies, optionally consuming a
 * pending alert on the way out. The alert is tested after the state is in
 * place, so a handler that is about to run sees the alert it was given.
 */
NTSTATUS
NTAPI
NtContinue(
    _In_ PCONTEXT Context,
    _In_ BOOLEAN TestAlert)
{
    PKTHREAD Thread = KeGetCurrentThread();
    NTSTATUS Status;

    if (Thread->TrapFrame == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Thread->PreviousMode != KernelMode)
        Status = KiContinuePreviousModeUser(Context, NULL, Thread->TrapFrame);
    else
        Status = KiContinue(Context, NULL, Thread->TrapFrame);

    if (!NT_SUCCESS(Status))
        return Status;

    if (TestAlert)
        KeTestAlertThread(Thread->PreviousMode);

    return STATUS_SUCCESS;
}

/*
 * As above, with the choices spelled out in a record rather than packed into a
 * boolean: whether to consume an alert, and whether a queued user APC should
 * be delivered instead of carrying on.
 */
NTSTATUS
NTAPI
NtContinueEx(
    _In_ PCONTEXT Context,
    _In_opt_ PKCONTINUE_ARGUMENT ContinueArgument)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KCONTINUE_ARGUMENT LocalArgument;
    KPROCESSOR_MODE PreviousMode = Thread->PreviousMode;
    BOOLEAN TestAlert = FALSE;
    BOOLEAN DeliverApc = FALSE;
    NTSTATUS Status;

    if (Thread->TrapFrame == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * A value of zero or one in place of a pointer is the older interface,
     * where this argument was the TestAlert boolean. Nothing else can be
     * mistaken for it, because no record is at those addresses.
     */
    if ((ULONG_PTR)ContinueArgument <= 1)
    {
        TestAlert = (ContinueArgument != NULL);
    }
    else
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(ContinueArgument,
                             sizeof(KCONTINUE_ARGUMENT),
                             sizeof(ULONG));
                LocalArgument = *(volatile KCONTINUE_ARGUMENT *)ContinueArgument;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            LocalArgument = *(PKCONTINUE_ARGUMENT)ContinueArgument;
        }

        if (LocalArgument.ContinueType >= KCONTINUE_LAST)
            return STATUS_INVALID_PARAMETER;

        TestAlert = (LocalArgument.ContinueFlags &
                     KCONTINUE_FLAG_TEST_ALERT) != 0;
        DeliverApc = (LocalArgument.ContinueFlags &
                      KCONTINUE_FLAG_DELIVER_APC) != 0;
    }

    if (PreviousMode != KernelMode)
        Status = KiContinuePreviousModeUser(Context, NULL, Thread->TrapFrame);
    else
        Status = KiContinue(Context, NULL, Thread->TrapFrame);

    if (!NT_SUCCESS(Status))
        return Status;

    if (TestAlert)
        KeTestAlertThread(PreviousMode);

    if (DeliverApc)
        Thread->ApcState.UserApcPending = TRUE;

    return STATUS_SUCCESS;
}

/*
 * Raises an exception the caller describes, as though the processor had raised
 * it at the point the context names. The thread's own frames are rewritten to
 * that context first, so the handlers that run see the state the caller meant
 * them to see rather than the state at the call.
 *
 * SearchFrames chooses whether the debugger gets a first look.
 */
NTSTATUS
NTAPI
KiRaiseException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ BOOLEAN SearchFrames)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KPROCESSOR_MODE PreviousMode = Thread->PreviousMode;
    EXCEPTION_RECORD LocalRecord;
    CONTEXT LocalContext;
    ULONG ParameterCount;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(Context, sizeof(CONTEXT), TYPE_ALIGNMENT(CONTEXT));
            RtlCopyMemory(&LocalContext, Context, sizeof(CONTEXT));

            ProbeForRead(ExceptionRecord,
                         sizeof(EXCEPTION_RECORD),
                         sizeof(ULONG));
            LocalRecord = *(volatile EXCEPTION_RECORD *)ExceptionRecord;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        LocalContext = *Context;
        LocalRecord = *ExceptionRecord;
    }

    ParameterCount = LocalRecord.NumberParameters;
    if (ParameterCount > EXCEPTION_MAXIMUM_PARAMETERS)
        return STATUS_INVALID_PARAMETER;

    /*
     * The internal bit marks an exception the kernel raised against itself.
     * A caller must not be able to claim one, because the handlers treat such
     * an exception as trusted.
     */
    LocalRecord.ExceptionCode &= ~KI_EXCEPTION_INTERNAL;

    KeContextToTrapFrame(&LocalContext,
                         ExceptionFrame,
                         TrapFrame,
                         LocalContext.ContextFlags,
                         PreviousMode);

    KiDispatchException(&LocalRecord,
                        ExceptionFrame,
                        TrapFrame,
                        PreviousMode,
                        SearchFrames);

    return STATUS_SUCCESS;
}

/*
 * The thread asking for an exception against itself. There is nothing to
 * return on the success path: the dispatch either found a handler, which
 * continues somewhere else, or it did not, and the thread is gone.
 */
NTSTATUS
NTAPI
NtRaiseException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context,
    _In_ BOOLEAN SearchFrames)
{
    PKTHREAD Thread = KeGetCurrentThread();

    if (Thread->TrapFrame == NULL)
        return STATUS_INVALID_PARAMETER;

    return KiRaiseException(ExceptionRecord,
                            Context,
                            NULL,
                            Thread->TrapFrame,
                            SearchFrames);
}

/* EOF */
