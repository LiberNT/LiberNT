/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 thread context get/set and user thread start context
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define PSP_ARM_ACCEPTED_CONTEXT_FLAGS \
    (CONTEXT_ALL | CONTEXT_EXCEPTION_REQUEST | CONTEXT_EXCEPTION_REPORTING | \
     CONTEXT_SERVICE_ACTIVE | CONTEXT_EXCEPTION_ACTIVE)

VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PGET_SET_CTX_CONTEXT Request = CONTAINING_RECORD(Apc, GET_SET_CTX_CONTEXT, Apc);
    PKTHREAD Thread = Apc->SystemArgument2;
    PKTRAP_FRAME Frame = Thread->TrapFrame;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(Thread == KeGetCurrentThread());

    /* A user context lives in the outermost user trap record. */
    if (Request->Mode == UserMode)
    {
        while (Frame && !KiIsUserModeTrap(Frame))
            Frame = (PKTRAP_FRAME)(ULONG_PTR)Frame->TrapFrame;
    }

    if (!Frame || !(Request->Context.ContextFlags & CONTEXT_ARM) ||
        (Request->Context.ContextFlags & ~PSP_ARM_ACCEPTED_CONTEXT_FLAGS))
    {
        Request->Status = Frame ? STATUS_INVALID_PARAMETER : STATUS_INVALID_DEVICE_STATE;
        KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    if (Apc->SystemArgument1)
    {
        KeContextToTrapFrame(&Request->Context, NULL, Frame, Request->Context.ContextFlags, Request->Mode);
    }
    else
    {
        KeTrapFrameToContext(Frame, NULL, &Request->Context);
        Request->Context.ContextFlags &= ~(CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE |
                                           CONTEXT_EXCEPTION_ACTIVE);
        if (Request->Context.ContextFlags & CONTEXT_EXCEPTION_REQUEST)
            Request->Context.ContextFlags |= CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE;
    }

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
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
    Context->Pc = (ULONG)(ULONG_PTR)ThreadStart & ~1UL;
    Context->Cpsr = CPSR_MODE_USER | CPSR_THUMB;
    Context->R0 = (ULONG)(ULONG_PTR)StartRoutine;
    Context->R1 = (ULONG)(ULONG_PTR)Argument;
    Context->Sp = (ULONG)(ULONG_PTR)StackBase & ~7UL;
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

    /* TODO: run the instrumentation callback on the return to user mode */
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

    /* A 32-bit host runs no WoW64 guest. */
    return STATUS_NOT_FOUND;
}
