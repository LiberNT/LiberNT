/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V thread context get/set APC
 */

#include <ntoskrnl.h>

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

    /* Nested kernel traps link back to the complete interrupted user bank. */
    if (Request->Mode == UserMode)
    {
        while (Frame && !KiUserTrap(Frame))
            Frame = Frame->PreviousTrapFrame;
    }
    if (!Frame && Thread->Teb)
        Frame = (PKTRAP_FRAME)Thread->InitialStack - 1;

    if (!Frame)
    {
        Request->Status = STATUS_INVALID_DEVICE_STATE;
        KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    /* Accept only this architecture's context layout. */
    if (!(Request->Context.ContextFlags & CONTEXT_RISCV64) ||
        (Request->Context.ContextFlags & ~(CONTEXT_ALL | CONTEXT_EXCEPTION_REQUEST |
                                           CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE |
                                           CONTEXT_EXCEPTION_ACTIVE)))
    {
        Request->Status = STATUS_INVALID_PARAMETER;
        KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    if (Apc->SystemArgument1)
        KeContextToTrapFrame(&Request->Context, NULL, Frame,
                            Request->Context.ContextFlags, Request->Mode);
    else
    {
        KeTrapFrameToContext(Frame, NULL, &Request->Context);
        Request->Context.ContextFlags &= ~(CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE |
                                           CONTEXT_EXCEPTION_ACTIVE);
        if (Request->Context.ContextFlags & CONTEXT_EXCEPTION_REQUEST)
        {
            Request->Context.ContextFlags |= CONTEXT_EXCEPTION_REPORTING;
            if (KiUserTrap(Frame) && (Frame->Scause == 8))
                Request->Context.ContextFlags |= CONTEXT_SERVICE_ACTIVE;
            else if (!(Frame->Scause & (1ULL << 63)))
                Request->Context.ContextFlags |= CONTEXT_EXCEPTION_ACTIVE;
        }
    }

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}

C_ASSERT(FIELD_OFFSET(CONTEXT, ContextFlags) == FIELD_OFFSET(WOW64_CONTEXT, ContextFlags));
C_ASSERT(FIELD_OFFSET(WOW64_CONTEXT, SegSs) < sizeof(CONTEXT));

BOOLEAN
NTAPI
PsArchGetWow64StartContext(
    _In_ PCONTEXT Context,
    _Out_ PULONG Eip,
    _Out_ PULONG Eax,
    _Out_ PULONG Ebx,
    _Out_ PULONG Esp)
{
    PWOW64_CONTEXT Wow64Context = (PWOW64_CONTEXT)Context;

    *Eip = *Eax = *Ebx = *Esp = 0;
    if ((Context->ContextFlags & CONTEXT_RISCV64) ||
        !(Wow64Context->ContextFlags & WOW64_CONTEXT_i386))
    {
        return FALSE;
    }

    *Eip = Wow64Context->Eip;
    *Eax = Wow64Context->Eax;
    *Ebx = Wow64Context->Ebx;
    *Esp = Wow64Context->Esp;
    return TRUE;
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
    Context->Pc = (ULONG64)(ULONG_PTR)ThreadStart;
    Context->A0 = (ULONG64)(ULONG_PTR)StartRoutine;
    Context->A1 = (ULONG64)(ULONG_PTR)Argument;
    Context->Sp = (ULONG64)(ULONG_PTR)StackBase & ~15ULL;
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

    /* TODO: get or set the live x86 state through the emulator backend when it is not in the CPU area */
    return STATUS_NOT_FOUND;
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

BOOLEAN
NTAPI
PspArchManagesExecutableWrites(VOID)
{
    return FALSE;
}

