/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 thread context get and set for the process manager
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
PspGetContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PVOID NonVolatileContext,
    _Inout_ PCONTEXT Context)
{
    PAGED_CODE();

    KeTrapFrameToContext(TrapFrame, NULL, Context);
}

VOID
NTAPI
PspSetContext(
    _Out_ PKTRAP_FRAME TrapFrame,
    _Out_ PVOID NonVolatileContext,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE Mode)
{
    PAGED_CODE();

    KeContextToTrapFrame(Context, NULL, TrapFrame, Context->ContextFlags, Mode);
}

VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE* NormalRoutine,
    _Inout_ PVOID* NormalContext,
    _Inout_ PVOID* SystemArgument1,
    _Inout_ PVOID* SystemArgument2)
{
    PGET_SET_CTX_CONTEXT GetSetContext;
    PKEVENT Event;
    PCONTEXT Context;
    PETHREAD Thread;
    KPROCESSOR_MODE Mode;
    PKTRAP_FRAME TrapFrame = NULL;
    PAGED_CODE();

    GetSetContext = CONTAINING_RECORD(Apc, GET_SET_CTX_CONTEXT, Apc);
    Context = &GetSetContext->Context;
    Event = &GetSetContext->Event;
    Mode = GetSetContext->Mode;
    Thread = Apc->SystemArgument2;

    if (Mode == KernelMode) TrapFrame = Thread->Tcb.TrapFrame;

    if (!TrapFrame)
    {
        TrapFrame = KeGetTrapFrame(&Thread->Tcb);
    }

    if (Apc->SystemArgument1)
    {
        PspSetContext(TrapFrame, NULL, Context, Mode);
    }
    else
    {
        ULONG ContextFlags;

        PspGetContext(TrapFrame, NULL, Context);

        ContextFlags = Context->ContextFlags &
                       ~(CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE | CONTEXT_EXCEPTION_ACTIVE);
        if (ContextFlags & CONTEXT_EXCEPTION_REQUEST)
        {
            ContextFlags |= CONTEXT_EXCEPTION_REPORTING;
            if (TrapFrame->PreviousPreviousMode != KI_INTERRUPT_TRAP_FRAME)
            {
                ContextFlags |= (TrapFrame->PreviousPreviousMode & KI_EXCEPTION_ACTIVE_FRAME) ?
                                CONTEXT_EXCEPTION_ACTIVE : CONTEXT_SERVICE_ACTIVE;
            }
        }
        Context->ContextFlags = ContextFlags;
    }

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
}

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

    /* TODO: run the callback on the return to 32-bit user mode */
    return STATUS_NOT_SUPPORTED;
}

BOOLEAN
NTAPI
PspArchManagesExecutableWrites(VOID)
{
    return FALSE;
}

NTSTATUS
NTAPI
PspArchCopyLiveWow64Context(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SetContext);

    return STATUS_NOT_FOUND;
}

