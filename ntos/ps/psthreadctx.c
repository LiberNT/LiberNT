/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading and writing the register state of a thread, including
 *              one that is not the caller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The register state of a thread lives on its kernel stack, and only the
 * thread itself stands on that stack at a point where the state is whole.
 * So a caller that wants another thread's state asks the thread to copy it,
 * by way of an APC, and waits for the answer.
 */
static
NTSTATUS
PspAskThreadForContext(
    _In_ PETHREAD Thread,
    _Inout_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE Mode,
    _In_ BOOLEAN SetContext)
{
    PKNORMAL_ROUTINE NormalRoutine = NULL;
    PVOID NormalContext = NULL;
    PVOID Argument1;
    PVOID Argument2;
    GET_SET_CTX_CONTEXT Request;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&Request, FIELD_OFFSET(GET_SET_CTX_CONTEXT, Context));
    Request.Context = *Context;
    Request.Mode = Mode;
    Request.Status = STATUS_SUCCESS;

    KeInitializeEvent(&Request.Event, NotificationEvent, FALSE);

    KeInitializeApc(&Request.Apc,
                    &Thread->Tcb,
                    OriginalApcEnvironment,
                    PspGetOrSetContextKernelRoutine,
                    NULL,
                    NULL,
                    KernelMode,
                    NULL);

    /*
     * The routine that runs on the thread finds the request through the APC
     * it is embedded in, is told by the first argument whether to write the
     * state rather than read it, and is given the thread as the second.
     */
    Argument1 = (PVOID)(ULONG_PTR)SetContext;
    Argument2 = &Thread->Tcb;

    /*
     * The thread's own state is reached without an APC: the routine is run
     * here, where it already stands on the right stack, and the event it
     * signals is simply never waited on.
     */
    if (Thread == PsGetCurrentThread())
    {
        Request.Apc.SystemArgument1 = Argument1;
        Request.Apc.SystemArgument2 = Argument2;

        PspGetOrSetContextKernelRoutine(&Request.Apc,
                                        &NormalRoutine,
                                        &NormalContext,
                                        &Argument1,
                                        &Argument2);

        if (!NT_SUCCESS(Request.Status)) return Request.Status;
        if (!SetContext) *Context = Request.Context;

        return STATUS_SUCCESS;
    }

    if (!KeInsertQueueApc(&Request.Apc,
                          Argument1,
                          Argument2,
                          IO_NO_INCREMENT))
    {
        return STATUS_THREAD_IS_TERMINATING;
    }

    /*
     * The wait is not alertable and has no timeout: the request lives on this
     * stack, so leaving before the thread is done with it would hand the
     * thread memory that is gone.
     */
    Status = KeWaitForSingleObject(&Request.Event,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (!NT_SUCCESS(Request.Status)) return Request.Status;

    if (!SetContext) *Context = Request.Context;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
PspGetOrSetUserContext(
    _In_ PETHREAD Thread,
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    PAGED_CODE();

    return PspAskThreadForContext(Thread,
                                  Context,
                                  ExGetPreviousMode(),
                                  SetContext);
}

NTSTATUS
NTAPI
PsGetContextThread(
    _In_ PETHREAD Thread,
    _Inout_ PCONTEXT ThreadContext,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PAGED_CODE();

    return PspAskThreadForContext(Thread, ThreadContext, PreviousMode, FALSE);
}

NTSTATUS
NTAPI
PsSetContextThread(
    _In_ PETHREAD Thread,
    _In_ PCONTEXT ThreadContext,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PAGED_CODE();

    return PspAskThreadForContext(Thread, ThreadContext, PreviousMode, TRUE);
}

/* SYSTEM CALLS ***************************************************************/

/*
 * Reads a thread's state into the caller's record. The flags in the record
 * say which parts are wanted, and they are read before anything else so that
 * a caller cannot change what is being asked for while it is answered.
 */
NTSTATUS
NTAPI
NtGetContextThread(
    _In_ HANDLE ThreadHandle,
    _Inout_ PCONTEXT ThreadContext)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID XState = NULL;
    ULONG ContextFlags;
    CONTEXT Context;
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(ThreadContext, sizeof(CONTEXT), sizeof(ULONG));
            ContextFlags = ThreadContext->ContextFlags;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        ContextFlags = ThreadContext->ContextFlags;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_GET_CONTEXT,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* A thread that runs only in the kernel has no user state to report */
    if (Thread->SystemThread)
    {
        ObDereferenceObject(Thread);
        return STATUS_INVALID_HANDLE;
    }

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = ContextFlags;

    Status = PspArchCaptureXStateContext(&Context,
                                         ContextFlags,
                                         PreviousMode,
                                         FALSE,
                                         &XState);
    if (NT_SUCCESS(Status))
    {
        Status = PsGetContextThread(Thread, &Context, PreviousMode);
        PspArchCompleteXStateContext(XState, NT_SUCCESS(Status));
    }

    ObDereferenceObject(Thread);

    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ThreadContext = Context;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *ThreadContext = Context;
    }

    return Status;
}

/*
 * Writes a thread's state from the caller's record. A process that was told
 * not to allow this refuses it, because writing a thread's registers is how
 * code is made to run somewhere it was never meant to.
 */
NTSTATUS
NTAPI
NtSetContextThread(
    _In_ HANDLE ThreadHandle,
    _In_ PCONTEXT ThreadContext)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID XState = NULL;
    CONTEXT Context;
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ThreadContext, sizeof(CONTEXT), sizeof(ULONG));
            Context = *ThreadContext;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Context = *ThreadContext;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_SET_CONTEXT,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (Thread->SystemThread)
    {
        ObDereferenceObject(Thread);
        return STATUS_INVALID_HANDLE;
    }

    if (THREAD_TO_PROCESS(Thread)->RestrictSetThreadContext &&
        Thread != PsGetCurrentThread())
    {
        ObDereferenceObject(Thread);
        return STATUS_ACCESS_DENIED;
    }

    Status = PspArchCaptureXStateContext(&Context,
                                         Context.ContextFlags,
                                         PreviousMode,
                                         TRUE,
                                         &XState);
    if (NT_SUCCESS(Status))
    {
        Status = PsSetContextThread(Thread, &Context, PreviousMode);
        PspArchCompleteXStateContext(XState, FALSE);
    }

    ObDereferenceObject(Thread);
    return Status;
}
