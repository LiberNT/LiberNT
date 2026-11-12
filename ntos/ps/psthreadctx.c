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
    _In_ BOOLEAN SetContext,
    _In_opt_ PVOID XState)
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
    Request.XState = XState;

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
                                  SetContext,
                                  NULL);
}

NTSTATUS
NTAPI
PsGetContextThread(
    _In_ PETHREAD Thread,
    _Inout_ PCONTEXT ThreadContext,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PAGED_CODE();

    return PspAskThreadForContext(Thread, ThreadContext, PreviousMode, FALSE, NULL);
}

NTSTATUS
NTAPI
PsSetContextThread(
    _In_ PETHREAD Thread,
    _In_ PCONTEXT ThreadContext,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PAGED_CODE();

    return PspAskThreadForContext(Thread, ThreadContext, PreviousMode, TRUE, NULL);
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
    NTSTATUS XStateStatus;
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

    Status = PspArchCaptureXStateContext(ThreadContext,
                                         ContextFlags,
                                         PreviousMode,
                                         FALSE,
                                         &XState);
    if (NT_SUCCESS(Status))
    {
        Status = PspAskThreadForContext(Thread, &Context, PreviousMode, FALSE, XState);
        XStateStatus = PspArchCompleteXStateContext(XState, NT_SUCCESS(Status));
        if (NT_SUCCESS(Status)) Status = XStateStatus;
    }

    ObDereferenceObject(Thread);

    /* An extended state area too small for what is in use still gets the rest of the record */
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) return Status;

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

    Status = PspArchCaptureXStateContext(ThreadContext,
                                         Context.ContextFlags,
                                         PreviousMode,
                                         TRUE,
                                         &XState);
    if (NT_SUCCESS(Status))
    {
        Status = PspAskThreadForContext(Thread, &Context, PreviousMode, TRUE, XState);
        PspArchCompleteXStateContext(XState, FALSE);
    }

    ObDereferenceObject(Thread);
    return Status;
}

static
VOID
PspMergeWow64Context(
    _Inout_ PWOW64_CONTEXT Target,
    _In_ PWOW64_CONTEXT Source)
{
    ULONG Flags = Source->ContextFlags;

    if ((Flags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL)
    {
        Target->Ebp = Source->Ebp;
        Target->Eip = Source->Eip;
        Target->SegCs = (USHORT)Source->SegCs;
        Target->EFlags = Source->EFlags;
        Target->Esp = Source->Esp;
        Target->SegSs = (USHORT)Source->SegSs;
    }

    if ((Flags & WOW64_CONTEXT_INTEGER) == WOW64_CONTEXT_INTEGER)
    {
        Target->Edi = Source->Edi;
        Target->Esi = Source->Esi;
        Target->Ebx = Source->Ebx;
        Target->Edx = Source->Edx;
        Target->Ecx = Source->Ecx;
        Target->Eax = Source->Eax;
    }

    /* A selector is 16 bits, as the live 64-bit context keeps it; 32-bit code stores only those */
    if ((Flags & WOW64_CONTEXT_SEGMENTS) == WOW64_CONTEXT_SEGMENTS)
    {
        Target->SegGs = (USHORT)Source->SegGs;
        Target->SegFs = (USHORT)Source->SegFs;
        Target->SegEs = (USHORT)Source->SegEs;
        Target->SegDs = (USHORT)Source->SegDs;
    }

    if ((Flags & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT)
        Target->FloatSave = Source->FloatSave;

    if ((Flags & WOW64_CONTEXT_DEBUG_REGISTERS) == WOW64_CONTEXT_DEBUG_REGISTERS)
    {
        Target->Dr0 = Source->Dr0;
        Target->Dr1 = Source->Dr1;
        Target->Dr2 = Source->Dr2;
        Target->Dr3 = Source->Dr3;
        Target->Dr6 = Source->Dr6;
        Target->Dr7 = Source->Dr7;
    }

    if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
        RtlCopyMemory(Target->ExtendedRegisters, Source->ExtendedRegisters, sizeof(Target->ExtendedRegisters));
}

NTSTATUS
NTAPI
PspCopyWow64CpuAreaContext(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    PTEB Teb = Thread->Tcb.Teb;
    PWOW64_CPU_INIT CpuArea;
    KAPC_STATE ApcState;
    ULONG Flags = Context->ContextFlags;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Teb == NULL) return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(&THREAD_TO_PROCESS(Thread)->Pcb, &ApcState);

    _SEH2_TRY
    {
        CpuArea = (PWOW64_CPU_INIT)Teb->TlsSlots[WOW64_TLS_CPURESERVED];
        ProbeForRead(CpuArea, FIELD_OFFSET(WOW64_CPU_INIT, ContextEx), sizeof(USHORT));

        if (CpuArea->Cpu.Machine != IMAGE_FILE_MACHINE_I386)
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else if (SetContext)
        {
            ProbeForWrite(CpuArea, FIELD_OFFSET(WOW64_CPU_INIT, ContextEx), sizeof(USHORT));
            PspMergeWow64Context(&CpuArea->Context, Context);
            CpuArea->Cpu.Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
        }
        else
        {
            *Context = CpuArea->Context;
            Context->ContextFlags = Flags & ~(CONTEXT_EXCEPTION_REPORTING |
                                              CONTEXT_SERVICE_ACTIVE |
                                              CONTEXT_EXCEPTION_ACTIVE);

            /* The CPU area holds the state the 32-bit code had when it called out of 32-bit mode */
            if ((Flags & CONTEXT_EXCEPTION_REQUEST) != 0)
                Context->ContextFlags |= CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);

    return Status;
}
