/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start and end guest threads on native threads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

VOID
NTAPI
EmuGuestThreadEntry(
    _In_ PVOID Start)
{
    UNREFERENCED_PARAMETER(Start);
    EmuFatal(STATUS_INVALID_THREAD, "guest thread started outside the emulation host");
}

NTSTATUS
EmuStartGuest(
    _In_ PEMU_THREAD Thread,
    _In_ const AMD64_CONTEXT *Start)
{
    PAMD64_APC_FRAME Frame = (PVOID)((Start->Rsp - sizeof(*Frame)) & ~(ULONG64)15);
    AMD64_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        RtlZeroMemory(Frame, sizeof(*Frame));
        Frame->Context = *Start;
        Frame->Context.P1Home = 0;
        Frame->Context.P2Home = (ULONG64)EmuProcess.GuestNtdll;
        Frame->Context.P3Home = 0;
        Frame->Context.P4Home = EmuProcess.LdrInitializeThunk;
        Frame->MachineFrame.Rip = Start->Rip;
        Frame->MachineFrame.SegCs = Start->SegCs;
        Frame->MachineFrame.EFlags = Start->EFlags;
        Frame->MachineFrame.Rsp = Start->Rsp;
        Frame->MachineFrame.SegSs = Start->SegSs;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status)) return Status;

    Context = *Start;
    Context.Rip = EmuProcess.KiUserApcDispatcher;
    Context.Rsp = (ULONG64)Frame;
    EmuCoreSetContext(Thread->Cpu, &Context);
    return STATUS_SUCCESS;
}

NTSTATUS
EmuCreateThread(
    _In_ ULONG Service,
    _Inout_ PULONG_PTR Arguments)
{
    HANDLE ProcessHandle = (HANDLE)Arguments[3];
    const AMD64_CONTEXT *GuestContext = (const AMD64_CONTEXT *)Arguments[5];
    PROCESS_BASIC_INFORMATION Information;
    PEMU_THREAD_START Start = NULL;
    CONTEXT Context;
    BOOLEAN Local;
    NTSTATUS Status;

    if (!GuestContext) return STATUS_INVALID_PARAMETER;

    Local = ProcessHandle == NtCurrentProcess();
    if (!Local)
    {
        Status = NtQueryInformationProcess(ProcessHandle,
                                           ProcessBasicInformation,
                                           &Information,
                                           sizeof(Information),
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;
        Local = Information.UniqueProcessId == (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
    }

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;
    Context.Sp = GuestContext->Rsp & ~(ULONG64)15;
    if (Local)
    {
        Start = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Start));
        if (!Start) return STATUS_NO_MEMORY;
        Start->Context = *GuestContext;
        Context.Pc = (ULONG_PTR)EmuGuestThreadEntry;
        Context.A0 = (ULONG_PTR)Start;
    }
    else
    {
        Context.Pc = EmuProcess.NativeRtlUserThreadStart;
        Context.A0 = GuestContext->Rcx;
        Context.A1 = GuestContext->Rdx;
    }

    Arguments[5] = (ULONG_PTR)&Context;
    Status = (NTSTATUS)EmuInvokeSystemService(Service, Arguments);
    if (!NT_SUCCESS(Status) && Start) RtlFreeHeap(RtlGetProcessHeap(), 0, Start);
    return Status;
}

DECLSPEC_NORETURN
VOID
EmuExitThread(
    _In_ PEMU_THREAD Thread,
    _In_ NTSTATUS ExitStatus)
{
    PEMULATION_THREAD Host = Thread->Host;
    PTEB GuestTeb = Thread->GuestTeb;
    PVOID Base;
    SIZE_T Size;

    Host->HostData = NULL;
    EmuCoreDestroyCpu(Thread->Cpu);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Thread);
    LdrShutdownThread();

    if (GuestTeb->ReservedPad1)
    {
        Base = GuestTeb->DeallocationStack;
        Size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
    }

    GuestTeb->DeallocationStack = Host->AllocationBase;
    GuestTeb->ReservedPad1 = TRUE;
    NtTerminateThread(NtCurrentThread(), ExitStatus);
    for (;;) NtYieldExecution();
}
