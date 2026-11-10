/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CPU core interface of the NT front end over the felix86 recompiler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../amd64emu.h"
#include "bridge.h"

#define EMU_CONTEXT_HAS(Flags, Part) (((Flags) & (Part)) == (Part))
#define EMU_EFLAGS_TF 0x100

C_ASSERT(sizeof(((AMD64_CONTEXT *)0)->FltSave) == 512);

static ULONG_PTR Felix86NtModuleBase;
static ULONG_PTR Felix86NtModuleEnd;

NTSTATUS
EmuCoreInitialize(VOID)
{
    PIMAGE_NT_HEADERS Headers;
    PVOID Base;
    const char *Error;

    if (!RtlPcToFileHeader((PVOID)EmuCoreInitialize, &Base)) return STATUS_INVALID_IMAGE_FORMAT;
    Headers = RtlImageNtHeader(Base);
    Felix86NtModuleBase = (ULONG_PTR)Base;
    Felix86NtModuleEnd = Felix86NtModuleBase + Headers->OptionalHeader.SizeOfImage;
    Error = Felix86NtInitialize();
    if (!Error) return STATUS_SUCCESS;
    Felix86NtReportError(Error, STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
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

PULONG64
EmuCoreGetVectors(
    _In_ PEMU_CPU Cpu,
    _Out_ PULONG Stride)
{
    uint32_t Count;
    PULONG64 Vectors = Felix86NtVectors((FELIX86_NT_CPU *)Cpu, &Count);

    *Stride = Count;
    return Vectors;
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

ULONG64
EmuCoreGetFrame(
    _In_ PEMU_CPU Cpu)
{
    return Felix86NtGetFrame((FELIX86_NT_CPU *)Cpu);
}

VOID
EmuCoreSetFrame(
    _In_ PEMU_CPU Cpu,
    _In_ ULONG64 Frame)
{
    Felix86NtSetFrame((FELIX86_NT_CPU *)Cpu, Frame);
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

ULONG
EmuCoreForgetCode(
    _In_ PVOID Base,
    _In_ SIZE_T Size)
{
    return Felix86NtForgetCode((ULONG64)Base, Size);
}

VOID
EmuCoreProtectCode(
    _In_ PVOID Base,
    _In_ SIZE_T Size,
    _In_ ULONG NewProtect)
{
    Felix86NtCacheProtect((ULONG64)Base, Size, NewProtect);
}

VOID
EmuCoreExcludeCode(
    _In_ PVOID Base,
    _In_ SIZE_T Size)
{
    Felix86NtExcludeCode((ULONG64)Base, Size);
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
    if (Result == FELIX86_NT_FAULT_PARK) return EmuFaultPark;

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
EmuCoreRequestPark(
    _In_ PEMU_CPU Cpu)
{
    Felix86NtRequestPark((FELIX86_NT_CPU *)Cpu);
}

BOOLEAN
EmuCoreCancelPark(
    _In_ PEMU_CPU Cpu)
{
    return Felix86NtCancelPark((FELIX86_NT_CPU *)Cpu) != 0;
}

VOID
EmuCoreBeginTermination(VOID)
{
    Felix86NtBeginTermination();
}

VOID
EmuCoreEndTermination(
    _In_ BOOLEAN Terminated)
{
    Felix86NtEndTermination(Terminated);
}

VOID
Felix86NtSystemService(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers)
{
    EmuSystemService(Owner, (PEMU_REGISTERS)Registers);
}

VOID
Felix86NtSystemService32(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers)
{
    EmuFatal(STATUS_ILLEGAL_INSTRUCTION, "32-bit system call in an AMD64 process");
}

static
VOID
Felix86NtDispatch(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Interrupt,
    _Inout_ PEXCEPTION_RECORD Record)
{
    NTSTATUS Status;

    /* TODO: report the exception to a debugger of the process before the guest dispatch */
    Thread->HostDepth++;
    if (Interrupt == EMU_NO_INTERRUPT || !EmuSoftwareInterrupt(Thread, Interrupt, Record))
    {
        Status = EmuDispatchException(Thread, Record);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("AMD64EMU: exception %08lx at %p cannot be dispatched: %08lx\n",
                     Record->ExceptionCode,
                     Record->ExceptionAddress,
                     Status);
            NtTerminateProcess(NtCurrentProcess(), Record->ExceptionCode);
        }
    }
    Thread->HostDepth--;
}

VOID
Felix86NtBreakpoint(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers,
    _In_ ULONG64 Next)
{
    EXCEPTION_RECORD Record;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionCode = STATUS_BREAKPOINT;
    Record.ExceptionAddress = (PVOID)(ULONG_PTR)(Next - 1);
    Record.NumberParameters = 1;
    ((PEMU_REGISTERS)Registers)->Rip = Next - 1;
    Felix86NtDispatch(Owner, EMU_NO_INTERRUPT, &Record);
}

VOID
Felix86NtSingleStep(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers)
{
    PEMU_THREAD Thread = Owner;
    EXCEPTION_RECORD Record;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionCode = STATUS_SINGLE_STEP;
    Record.ExceptionAddress = (PVOID)(ULONG_PTR)((PEMU_REGISTERS)Registers)->Rip;
    Felix86NtSetFlags((FELIX86_NT_CPU *)Thread->Cpu, Felix86NtGetFlags((FELIX86_NT_CPU *)Thread->Cpu) & ~(ULONG64)EMU_EFLAGS_TF);
    Felix86NtDispatch(Thread, EMU_NO_INTERRUPT, &Record);
}

VOID
Felix86NtInterrupt(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers,
    _In_ ULONG64 Next,
    _In_ uint32_t Vector)
{
    EXCEPTION_RECORD Record;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionCode = STATUS_ACCESS_VIOLATION;
    Record.ExceptionAddress = (PVOID)(ULONG_PTR)((PEMU_REGISTERS)Registers)->Rip;
    Record.NumberParameters = 2;
    Record.ExceptionInformation[1] = (ULONG_PTR)-1;
    Felix86NtDispatch(Owner, Vector, &Record);
}

ULONG64
Felix86NtNativeEntry(
    _In_ ULONG64 Rip)
{
    return EmuNativeEntry(Rip);
}
