/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ps/i386/psctx.c
 * PURPOSE:         Process Manager: Set/Get Context for i386
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
PspGetContext(IN PKTRAP_FRAME TrapFrame,
              IN PVOID NonVolatileContext,
              IN OUT PCONTEXT Context)
{
    PAGED_CODE();

    /* Convert the trap frame to a context */
    KeTrapFrameToContext(TrapFrame, NULL, Context);
}

VOID
NTAPI
PspSetContext(OUT PKTRAP_FRAME TrapFrame,
              OUT PVOID NonVolatileContext,
              IN PCONTEXT Context,
              IN KPROCESSOR_MODE Mode)
{
    PAGED_CODE();

    /* Convert the context to a trap frame structure */
    KeContextToTrapFrame(Context, NULL, TrapFrame, Context->ContextFlags, Mode);
}

VOID
NTAPI
PspGetOrSetContextKernelRoutine(IN PKAPC Apc,
                                IN OUT PKNORMAL_ROUTINE* NormalRoutine,
                                IN OUT PVOID* NormalContext,
                                IN OUT PVOID* SystemArgument1,
                                IN OUT PVOID* SystemArgument2)
{
    PGET_SET_CTX_CONTEXT GetSetContext;
    PKEVENT Event;
    PCONTEXT Context;
    PETHREAD Thread;
    KPROCESSOR_MODE Mode;
    PKTRAP_FRAME TrapFrame = NULL;
    PAGED_CODE();

    /* Get the Context Structure */
    GetSetContext = CONTAINING_RECORD(Apc, GET_SET_CTX_CONTEXT, Apc);
    Context = &GetSetContext->Context;
    Event = &GetSetContext->Event;
    Mode = GetSetContext->Mode;
    Thread = Apc->SystemArgument2;

    /* If this is a kernel-mode request, grab the saved trap frame */
    if (Mode == KernelMode) TrapFrame = Thread->Tcb.TrapFrame;

    /* If we don't have one, grab it from the stack */
    if (!TrapFrame)
    {
        /* Trap frame is right under our initial stack */
        TrapFrame = KeGetTrapFrame(&Thread->Tcb);
    }

    /* Check if it's a set or get */
    if (Apc->SystemArgument1)
    {
        /* Set the Context */
        PspSetContext(TrapFrame, NULL, Context, Mode);
    }
    else
    {
        /* Get the Context */
        PspGetContext(TrapFrame, NULL, Context);
    }

    /* Notify the Native API that we are done */
    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
}

/* EOF */

VOID
NTAPI
PsArchInitializeUserThreadContext(
    _Out_ PCONTEXT Context,
    _In_ PVOID ThreadStart,
    _In_ PVOID StartRoutine,
    _In_opt_ PVOID Argument,
    _In_ PVOID StackBase)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_FULL;
    Context->Eip = (ULONG)ThreadStart;
    Context->Eax = (ULONG)StartRoutine;
    Context->Ebx = (ULONG)Argument;
    Context->Esp = (ULONG)StackBase - 3 * sizeof(ULONG);
    Context->EFlags = EFLAGS_INTERRUPT_MASK;
    Context->SegCs = KGDT_R3_CODE | RPL_MASK;
    Context->SegDs = KGDT_R3_DATA | RPL_MASK;
    Context->SegEs = KGDT_R3_DATA | RPL_MASK;
    Context->SegFs = KGDT_R3_TEB | RPL_MASK;
    Context->SegSs = KGDT_R3_DATA | RPL_MASK;
}

NTSTATUS
NTAPI
PspArchCaptureXStateContext(
    _In_ PCONTEXT Context,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SetContext,
    _Out_ PVOID *XState)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextFlags);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(SetContext);

    /* TODO: capture the extended processor state (CONTEXT_XSTATE) of the context */
    *XState = NULL;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PspArchCompleteXStateContext(
    _In_opt_ PVOID XState,
    _In_ BOOLEAN CopyOut)
{
    UNREFERENCED_PARAMETER(XState);
    UNREFERENCED_PARAMETER(CopyOut);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PspArchSetInstrumentationCallback(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Information);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(PreviousMode);

    return STATUS_INVALID_INFO_CLASS;
}

VOID
NTAPI
PspArchInitializeUserThreadStartup(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KiInitializeUserApc(ExceptionFrame,
                        TrapFrame,
                        PspSystemDllEntryPoint,
                        NULL,
                        PspSystemDllBase,
                        NULL);
}
