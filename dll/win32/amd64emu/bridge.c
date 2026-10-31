/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 entry points of native functions whose interface depends on the architecture
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

typedef VOID (*PEMU_BRIDGE_ROUTINE)(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers);

typedef struct _EMU_BRIDGE
{
    PCWSTR Module;
    PCSTR Name;
    PEMU_BRIDGE_ROUTINE Routine;
    PVOID Native;
    ULONG64 Stub;
} EMU_BRIDGE, *PEMU_BRIDGE;

static
VOID
EmuBridgeWin32Result(
    _Inout_ PEMU_REGISTERS Registers,
    _In_ NTSTATUS Status)
{
    if (!NT_SUCCESS(Status)) RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
    Registers->Gpr[EmuRax] = NT_SUCCESS(Status);
}

static
VOID
EmuBridgeNtGetContextThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = (ULONG)EmuGetContextThread((HANDLE)Registers->Gpr[EmuR10],
                                                        (AMD64_CONTEXT *)Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeGetThreadContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    EmuBridgeWin32Result(Registers, EmuGetContextThread((HANDLE)Registers->Gpr[EmuR10],
                                                        (AMD64_CONTEXT *)Registers->Gpr[EmuRdx]));
}

static
VOID
EmuBridgeNtSetContextThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    BOOLEAN Resumed;
    NTSTATUS Status;

    Status = EmuSetContextThread(Thread, (HANDLE)Registers->Gpr[EmuR10],
                                 (const AMD64_CONTEXT *)Registers->Gpr[EmuRdx], &Resumed);
    if (!Resumed) Registers->Gpr[EmuRax] = (ULONG)Status;
}

static
VOID
EmuBridgeSetThreadContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    BOOLEAN Resumed;
    NTSTATUS Status;

    Status = EmuSetContextThread(Thread, (HANDLE)Registers->Gpr[EmuR10],
                                 (const AMD64_CONTEXT *)Registers->Gpr[EmuRdx], &Resumed);
    if (!Resumed) EmuBridgeWin32Result(Registers, Status);
}

static
VOID
EmuBridgeNtSuspendThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    Registers->Gpr[EmuRax] = (ULONG)EmuSuspendThread((HANDLE)Registers->Gpr[EmuR10], (PULONG)Registers->Gpr[EmuRdx]);
}

static
VOID
EmuBridgeSuspendThread(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    ULONG Previous;
    NTSTATUS Status;

    Status = EmuSuspendThread((HANDLE)Registers->Gpr[EmuR10], &Previous);
    if (NT_SUCCESS(Status))
    {
        Registers->Gpr[EmuRax] = Previous;
        return;
    }
    RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
    Registers->Gpr[EmuRax] = (ULONG)-1;
}

static
VOID
EmuBridgeRtlCaptureContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers)
{
    AMD64_CONTEXT *Context = (AMD64_CONTEXT *)Registers->Gpr[EmuR10];
    ULONG64 Rsp = Registers->Gpr[EmuRsp];

    Context->ContextFlags = CONTEXT_AMD64_FULL | CONTEXT_AMD64_SEGMENTS;
    EmuCoreGetContext(Thread->Cpu, Context);
    Context->Rcx = Registers->Gpr[EmuR10];
    Context->Rip = *(PULONG64)Rsp;
    Context->Rsp = Rsp + sizeof(ULONG64);
}

static EMU_BRIDGE EmuBridges[] =
{
    { L"ntdll.dll", "NtGetContextThread", EmuBridgeNtGetContextThread },
    { L"ntdll.dll", "NtSetContextThread", EmuBridgeNtSetContextThread },
    { L"ntdll.dll", "NtSuspendThread", EmuBridgeNtSuspendThread },
    { L"ntdll.dll", "RtlCaptureContext", EmuBridgeRtlCaptureContext },
    { L"kernelbase.dll", "GetThreadContext", EmuBridgeGetThreadContext },
    { L"kernelbase.dll", "SetThreadContext", EmuBridgeSetThreadContext },
    { L"kernelbase.dll", "SuspendThread", EmuBridgeSuspendThread },
    { L"kernel32.dll", "GetThreadContext", EmuBridgeGetThreadContext },
    { L"kernel32.dll", "SetThreadContext", EmuBridgeSetThreadContext },
    { L"kernel32.dll", "SuspendThread", EmuBridgeSuspendThread },
};

static
VOID
EmuResolveBridges(VOID)
{
    UNICODE_STRING Module;
    ANSI_STRING Name;
    PVOID Base, Native;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(EmuBridges); Index++)
    {
        if (EmuBridges[Index].Native) continue;
        RtlInitUnicodeString(&Module, EmuBridges[Index].Module);
        if (!NT_SUCCESS(LdrGetDllHandle(NULL, NULL, &Module, &Base))) continue;
        RtlInitAnsiString(&Name, EmuBridges[Index].Name);
        if (NT_SUCCESS(LdrGetProcedureAddress(Base, &Name, 0, &Native)))
            InterlockedExchangePointer(&EmuBridges[Index].Native, Native);
    }
}

ULONG64
EmuBridgeEntry(
    _In_ ULONG64 Rip)
{
    UCHAR Code[] = { 0x4C, 0x8B, 0xD1, 0xB8, 0, 0, 0, 0, 0x0F, 0x05, 0xC3 };
    ULONG Service, Index;

    EmuResolveBridges();
    for (Index = 0; Index < RTL_NUMBER_OF(EmuBridges); Index++)
    {
        if ((ULONG_PTR)EmuBridges[Index].Native != Rip) continue;
        if (!EmuBridges[Index].Stub)
        {
            Service = EMU_BRIDGE_SERVICE + Index;
            RtlCopyMemory(&Code[4], &Service, sizeof(Service));
            EmuBridges[Index].Stub = EmuCreateStub(Code, sizeof(Code));
        }
        return EmuBridges[Index].Stub;
    }
    return 0;
}

BOOLEAN
EmuBridgeService(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Service,
    _Inout_ PEMU_REGISTERS Registers)
{
    ULONG Index = Service - EMU_BRIDGE_SERVICE;

    if (Service < EMU_BRIDGE_SERVICE || Index >= RTL_NUMBER_OF(EmuBridges)) return FALSE;

    _SEH2_TRY
    {
        EmuBridges[Index].Routine(Thread, Registers);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Registers->Gpr[EmuRax] = (ULONG)_SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return TRUE;
}
