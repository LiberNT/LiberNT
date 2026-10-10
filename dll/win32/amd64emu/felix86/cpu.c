/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CPU core interface of the NT front end over the felix86 recompiler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../amd64emu.h"
#include "bridge.h"

#define EMU_CONTEXT_HAS(Flags, Part) (((Flags) & (Part)) == (Part))

C_ASSERT(sizeof(((AMD64_CONTEXT *)0)->FltSave) == 512);

static ULONG_PTR Felix86NtModuleBase;
static ULONG_PTR Felix86NtModuleEnd;

NTSTATUS
EmuCoreInitialize(VOID)
{
    PIMAGE_NT_HEADERS Headers;
    PVOID Base;

    if (!RtlPcToFileHeader((PVOID)EmuCoreInitialize, &Base)) return STATUS_INVALID_IMAGE_FORMAT;
    Headers = RtlImageNtHeader(Base);
    Felix86NtModuleBase = (ULONG_PTR)Base;
    Felix86NtModuleEnd = Felix86NtModuleBase + Headers->OptionalHeader.SizeOfImage;
    return Felix86NtInitialize() ? STATUS_NOT_SUPPORTED : STATUS_SUCCESS;
}

NTSTATUS
EmuCoreCreateCpu(
    _In_ PVOID Owner,
    _In_ PVOID GuestTeb,
    _Out_ PEMU_CPU *Cpu)
{
    *Cpu = (PEMU_CPU)Felix86NtCreateCpu(Owner, (ULONG64)GuestTeb);
    return *Cpu ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

VOID
EmuCoreDestroyCpu(
    _In_ PEMU_CPU Cpu)
{
    Felix86NtDestroyCpu((FELIX86_NT_CPU *)Cpu);
}

PEMU_REGISTERS
EmuCoreGetRegisters(
    _In_ PEMU_CPU Cpu)
{
    return (PEMU_REGISTERS)Felix86NtRegisters((FELIX86_NT_CPU *)Cpu);
}

VOID
EmuCoreGetContext(
    _In_ PEMU_CPU Cpu,
    _Inout_ AMD64_CONTEXT *Context)
{
    PEMU_REGISTERS Registers = EmuCoreGetRegisters(Cpu);
    ULONG Flags = Context->ContextFlags;

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_INTEGER))
        RtlCopyMemory(&Context->Rax, Registers->Gpr, sizeof(Registers->Gpr));

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_CONTROL))
    {
        Context->Rip = Registers->Rip;
        Context->Rsp = Registers->Gpr[EmuRsp];
        Context->EFlags = (ULONG)Felix86NtGetFlags((FELIX86_NT_CPU *)Cpu);
        Context->SegCs = EMU_SEG_CODE;
        Context->SegSs = EMU_SEG_DATA;
    }

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_SEGMENTS))
    {
        Context->SegDs = EMU_SEG_DATA;
        Context->SegEs = EMU_SEG_DATA;
        Context->SegGs = EMU_SEG_DATA;
        Context->SegFs = EMU_SEG_TEB;
    }

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_FLOATING_POINT))
    {
        RtlZeroMemory(&Context->FltSave, sizeof(Context->FltSave));
        Felix86NtSaveFloating((FELIX86_NT_CPU *)Cpu, &Context->FltSave);
        Context->MxCsr = Context->FltSave.MxCsr;
    }

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_DEBUG_REGISTERS))
    {
        Context->Dr0 = 0;
        Context->Dr1 = 0;
        Context->Dr2 = 0;
        Context->Dr3 = 0;
        Context->Dr6 = 0;
        Context->Dr7 = 0;
    }
}

VOID
EmuCoreSetContext(
    _In_ PEMU_CPU Cpu,
    _In_ const AMD64_CONTEXT *Context)
{
    PEMU_REGISTERS Registers = EmuCoreGetRegisters(Cpu);
    ULONG Flags = Context->ContextFlags;
    ULONG64 StackPointer = Registers->Gpr[EmuRsp];

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_INTEGER))
    {
        RtlCopyMemory(Registers->Gpr, &Context->Rax, sizeof(Registers->Gpr));
        Registers->Gpr[EmuRsp] = StackPointer;
    }

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_CONTROL))
    {
        Registers->Rip = Context->Rip;
        Registers->Gpr[EmuRsp] = Context->Rsp;
        Felix86NtSetFlags((FELIX86_NT_CPU *)Cpu, Context->EFlags);
    }

    if (EMU_CONTEXT_HAS(Flags, CONTEXT_AMD64_FLOATING_POINT))
        Felix86NtRestoreFloating((FELIX86_NT_CPU *)Cpu, &Context->FltSave);
}

DECLSPEC_NORETURN
VOID
EmuCoreRun(
    _In_ PEMU_CPU Cpu)
{
    Felix86NtRun((FELIX86_NT_CPU *)Cpu);
}

VOID
EmuCoreInvalidate(
    _In_ PVOID Base,
    _In_ SIZE_T Size)
{
    if (Size) Felix86NtInvalidate((ULONG64)Base, (ULONG64)Base + Size);
}

VOID
EmuCoreGetProcessor(
    _Out_ PUSHORT Level,
    _Out_ PUSHORT Revision)
{
    Felix86NtProcessor(Level, Revision);
}

EMU_FAULT
EmuCoreFault(
    _In_ PEMU_CPU Cpu,
    _In_ BOOLEAN InService,
    _In_ const EXCEPTION_RECORD *NativeRecord,
    _Inout_ PCONTEXT NativeContext,
    _Out_ PEXCEPTION_RECORD GuestRecord,
    _Out_ PULONG Interrupt)
{
    FELIX86_NT_NATIVE Native;
    FELIX86_NT_GUEST Guest;
    ULONG Index;
    int Result;

    RtlZeroMemory(&Native, sizeof(Native));
    RtlZeroMemory(&Guest, sizeof(Guest));
    Native.Pc = &NativeContext->Pc;
    Native.X = NativeContext->X;
    Native.F = NativeContext->F;
    Native.Code = NativeRecord->ExceptionCode;
    Native.Parameters = min(NativeRecord->NumberParameters, RTL_NUMBER_OF(Native.Information));
    for (Index = 0; Index < Native.Parameters; Index++)
        Native.Information[Index] = NativeRecord->ExceptionInformation[Index];
    Native.InService = InService;
    Native.InModule = NativeContext->Pc >= Felix86NtModuleBase && NativeContext->Pc < Felix86NtModuleEnd;

    *Interrupt = EMU_NO_INTERRUPT;
    Result = Felix86NtFault((FELIX86_NT_CPU *)Cpu, &Native, &Guest);
    if (Result == FELIX86_NT_FAULT_UNRELATED) return EmuFaultUnrelated;
    if (Result == FELIX86_NT_FAULT_RESUME) return EmuFaultResume;

    RtlZeroMemory(GuestRecord, sizeof(*GuestRecord));
    GuestRecord->ExceptionCode = Guest.Code;
    GuestRecord->ExceptionAddress = (PVOID)Guest.Address;
    GuestRecord->NumberParameters = Guest.Parameters;
    for (Index = 0; Index < Guest.Parameters; Index++)
        GuestRecord->ExceptionInformation[Index] = Guest.Information[Index];
    *Interrupt = Guest.Interrupt;
    return EmuFaultGuest;
}

VOID
Felix86NtSystemService(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers)
{
    EmuSystemService(Owner, (PEMU_REGISTERS)Registers);
}
