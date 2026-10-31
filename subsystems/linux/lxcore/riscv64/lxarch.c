/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 Linux system calls and register state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxcore.h"

#define LX_RV_UC             128
#define LX_RV_UC_STACK       (LX_RV_UC + 16)
#define LX_RV_UC_SIGMASK     (LX_RV_UC + 40)
#define LX_RV_MC             (LX_RV_UC + 176)
#define LX_RV_FP             (LX_RV_MC + 256)
#define LX_RV_FRAME_SIZE     (LX_RV_FP + 528)

C_ASSERT(LX_RV_FRAME_SIZE == 1088);

static
LX_SYSCALL(LxRiscvFlushIcache)
{
    NTSTATUS Status;

    if (Call->Args[2] & ~1ULL)
        return -LX_EINVAL;
    Status = ZwFlushInstructionCache(NtCurrentProcess(), NULL, 0);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

PLX_SYSCALL_ENTRY
LxArchSyscallEntry(
    _In_ ULONG Number)
{
    static LX_SYSCALL_ENTRY FlushIcache = { LxRiscvFlushIcache, "riscv_flush_icache" };

    if (Number == 259)
        return &FlushIcache;
    return LxGenericSyscallEntry(Number);
}

ULONG
LxArchElfMachine(VOID)
{
    return 243;
}

ULONG64
LxArchHwcap(VOID)
{
    return (1ULL << ('I' - 'A')) | (1ULL << ('M' - 'A')) |
           (1ULL << ('A' - 'A')) | (1ULL << ('F' - 'A')) |
           (1ULL << ('D' - 'A')) | (1ULL << ('C' - 'A'));
}

ULONG64
LxArchHwcap2(VOID)
{
    return 0;
}

PCSTR
LxArchPlatform(VOID)
{
    return "riscv64";
}

PCSTR
LxArchCpuinfo(VOID)
{
    return "isa\t\t: rv64imafdc\nmmu\t\t: sv39\n";
}

PCSTR
LxArchConfigName(VOID)
{
    return "riscv";
}

PCSTR
LxArchKernelConfig(VOID)
{
    return "CONFIG_RISCV=y\n";
}

ULONG64
LxArchImageBase(
    _In_ BOOLEAN Interpreter)
{
    ULONG64 Window = (ULONG64)(ULONG_PTR)MmHighestUserAddress + 1;

    return LX_ALIGN_DOWN(Interpreter ? Window - Window / 8 : (Window / 3) * 2,
                         LX_ALLOC_GRANULARITY);
}

VOID
LxArchGetSyscall(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PULONG Number,
    _Out_writes_(6) PULONG64 Args)
{
    UNREFERENCED_PARAMETER(ExceptionFrame);
    *Number = (ULONG)TrapFrame->Context.A7;
    RtlCopyMemory(Args, &TrapFrame->Context.A0, 6 * sizeof(*Args));
}

VOID
LxArchSetSyscallResult(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ LONG64 Result)
{
    TrapFrame->Context.A0 = (ULONG64)Result;
}

VOID
LxArchSetExecContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Pc,
    _In_ ULONG64 Sp)
{
    UNREFERENCED_PARAMETER(ExceptionFrame);
    RtlZeroMemory(&TrapFrame->Context, sizeof(TrapFrame->Context));
    TrapFrame->Context.ContextFlags = CONTEXT_FULL;
    TrapFrame->Context.Pc = Pc;
    TrapFrame->Context.Sp = Sp;
}

VOID
LxArchCaptureContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ExceptionFrame);
    *Context = TrapFrame->Context;
    Context->ContextFlags = CONTEXT_FULL;
}

ULONG
LxArchCoreRegisters(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_writes_(LX_CORE_MAX_REGISTERS) PULONG64 Registers)
{
    CONTEXT Context;
    ULONG Index;

    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);
    Registers[0] = Context.Pc;
    for (Index = 1; Index < 32; Index++)
        Registers[Index] = Context.X[Index];
    return 32;
}

VOID
LxArchSetForkContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG64 StackPointer)
{
    Context->A0 = 0;
    if (StackPointer != 0)
        Context->Sp = StackPointer;
}

VOID
LxArchRestoreContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ExceptionFrame);
    TrapFrame->Context = *Context;
}

VOID
LxArchSetThreadPointer(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Value)
{
    TrapFrame->Context.Tp = Value;
}

ULONG64
LxArchGetThreadPointer(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Tp;
}

ULONG64
LxArchGetUserPc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Pc;
}

ULONG64
LxArchContextPc(
    _In_ PCONTEXT Context)
{
    return Context->Pc;
}

ULONG64
LxArchContextSp(
    _In_ PCONTEXT Context)
{
    return Context->Sp;
}

ULONG64
LxArchGetUserSp(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Sp;
}

ULONG
LxArchSigTrampoline(
    _Out_writes_bytes_(16) PUCHAR Code)
{
    static const ULONG Instructions[2] = { 0x08B00893, 0x00000073 };

    RtlCopyMemory(Code, Instructions, sizeof(Instructions));
    return sizeof(Instructions);
}

LONG64
LxArchReadSigaction(
    _In_ ULONG64 Address,
    _Out_ PLX_SIGACTION Action)
{
    ULONG64 Values[3];
    LONG64 Error = LxCopyFromUser(Values, Address, sizeof(Values));

    if (Error == 0)
    {
        Action->Handler = Values[0];
        Action->Flags = Values[1] & ~LX_SA_RESTORER;
        Action->Restorer = 0;
        Action->Mask = Values[2];
    }
    return Error;
}

LONG64
LxArchWriteSigaction(
    _In_ ULONG64 Address,
    _In_ PLX_SIGACTION Action)
{
    ULONG64 Values[3] = { Action->Handler, Action->Flags, Action->Mask };

    return LxCopyToUser(Address, Values, sizeof(Values));
}

BOOLEAN
LxArchSetupSignalFrame(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 StackTop,
    _In_ LONG Signal,
    _In_ PLX_SIGINFO Info,
    _In_ PLX_SIGACTION Action,
    _In_ ULONG64 Restorer,
    _In_ ULONG64 SavedMask,
    _In_ PLX_STACK AltStack)
{
    ULONG64 Frame;
    PUCHAR Buffer;
    LONG64 Error;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    if (StackTop < LX_RV_FRAME_SIZE)
        return FALSE;
    Frame = LX_ALIGN_DOWN(StackTop - LX_RV_FRAME_SIZE, 16);
    Buffer = ExAllocatePoolZero(PagedPool, LX_RV_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return FALSE;

    RtlCopyMemory(Buffer, Info, sizeof(*Info));
    ((PLX_SIGINFO)Buffer)->Signo = Signal;
    RtlCopyMemory(Buffer + LX_RV_UC_STACK, AltStack, sizeof(*AltStack));
    *(PULONG64)(Buffer + LX_RV_UC_SIGMASK) = SavedMask;
    *(PULONG64)(Buffer + LX_RV_MC) = TrapFrame->Context.Pc;
    RtlCopyMemory(Buffer + LX_RV_MC + 8, &TrapFrame->Context.X[1], 31 * sizeof(ULONG64));
    RtlCopyMemory(Buffer + LX_RV_FP, TrapFrame->Context.F, sizeof(TrapFrame->Context.F));
    *(PULONG)(Buffer + LX_RV_FP + 256) = TrapFrame->Context.Fcsr;
    Error = LxCopyToUser(Frame, Buffer, LX_RV_FRAME_SIZE);
    ExFreePoolWithTag(Buffer, LX_TAG);
    if (Error != 0)
        return FALSE;

    TrapFrame->Context.A0 = (ULONG64)Signal;
    TrapFrame->Context.A1 = Frame;
    TrapFrame->Context.A2 = Frame + LX_RV_UC;
    TrapFrame->Context.Sp = Frame;
    TrapFrame->Context.Ra = Restorer;
    TrapFrame->Context.Pc = Action->Handler;
    return TRUE;
}

LONG64
LxArchSignalReturn(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PULONG64 Mask)
{
    ULONG64 Frame = TrapFrame->Context.Sp;
    PUCHAR Buffer;
    LONG64 Error;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    *Mask = 0;
    if (Frame & 15)
        return -LX_EFAULT;
    Buffer = ExAllocatePoolZero(PagedPool, LX_RV_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return -LX_ENOMEM;
    Error = LxCopyFromUser(Buffer, Frame, LX_RV_FRAME_SIZE);
    if ((Error == 0) && ((*(PULONG)(Buffer + LX_RV_FP + 516) != 0) ||
                         (*(PULONG64)(Buffer + LX_RV_FP + 520) != 0)))
        Error = -LX_EINVAL;
    if (Error == 0)
    {
        TrapFrame->Context.Pc = *(PULONG64)(Buffer + LX_RV_MC);
        RtlCopyMemory(&TrapFrame->Context.X[1], Buffer + LX_RV_MC + 8, 31 * sizeof(ULONG64));
        RtlCopyMemory(TrapFrame->Context.F, Buffer + LX_RV_FP, sizeof(TrapFrame->Context.F));
        TrapFrame->Context.Fcsr = *(PULONG)(Buffer + LX_RV_FP + 256) & 0xff;
        *Mask = *(PULONG64)(Buffer + LX_RV_UC_SIGMASK);
    }
    ExFreePoolWithTag(Buffer, LX_TAG);
    return Error;
}

VOID
LxArchRestartSyscall(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Argument)
{
    TrapFrame->Context.A0 = Argument;
    TrapFrame->Context.Pc -= 4;
}

LONG
LxArchSpecStoreBypassMode(VOID)
{
    return LX_SPEC_MODE_UNSUPPORTED;
}

/* FIXME: ghostwrite reads Not affected without checking for a T-Head core with xtheadvector. */
PCSTR
LxArchCpuVulnerability(
    _In_ PCSTR Name)
{
    UNREFERENCED_PARAMETER(Name);
    return "Not affected";
}

VOID
LxArchApplySpeculation(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(TrapFrame);
}

LONG
LxArchPtraceGetRegset(
    _In_ PCONTEXT Context,
    _In_ ULONG64 Tls,
    _In_ LONG SyscallNr,
    _In_ ULONG Type,
    _Out_writes_bytes_(LX_PTRACE_REGSET_MAX) PUCHAR Buffer,
    _Out_ PULONG Size,
    _Out_ PULONG Unit)
{
    PULONG64 Registers = (PULONG64)Buffer;
    ULONG Index;

    UNREFERENCED_PARAMETER(Tls);
    UNREFERENCED_PARAMETER(SyscallNr);
    RtlZeroMemory(Buffer, LX_PTRACE_REGSET_MAX);
    switch (Type)
    {
        case LX_NT_PRSTATUS:
            Registers[0] = Context->Pc;
            for (Index = 1; Index < 32; Index++)
                Registers[Index] = Context->X[Index];
            *Size = 32 * sizeof(ULONG64);
            *Unit = sizeof(ULONG64);
            return 0;

        case LX_NT_PRFPREG:
            RtlCopyMemory(Buffer, Context->F, sizeof(Context->F));
            *(PULONG)&Registers[32] = Context->Fcsr;
            *Size = 33 * sizeof(ULONG64);
            *Unit = sizeof(ULONG64);
            return 0;

        default:
            return -LX_EINVAL;
    }
}

LONG
LxArchPtraceSetRegset(
    _Inout_ PCONTEXT Context,
    _Inout_ PULONG64 Tls,
    _Inout_ PLONG SyscallNr,
    _In_ ULONG Type,
    _In_reads_bytes_(LX_PTRACE_REGSET_MAX) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PULONG64 Registers = (PULONG64)Buffer;
    ULONG Index;

    UNREFERENCED_PARAMETER(Tls);
    UNREFERENCED_PARAMETER(SyscallNr);
    UNREFERENCED_PARAMETER(Length);
    switch (Type)
    {
        case LX_NT_PRSTATUS:
            Context->Pc = Registers[0];
            for (Index = 1; Index < 32; Index++)
                Context->X[Index] = Registers[Index];
            return 0;

        case LX_NT_PRFPREG:
            RtlCopyMemory(Context->F, Buffer, sizeof(Context->F));
            Context->Fcsr = *(PULONG)&Registers[32];
            return 0;

        default:
            return -LX_EINVAL;
    }
}

BOOLEAN
LxArchPtraceSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(Context);
    return !Enable;
}

BOOLEAN
LxArchPersonalityAllowed(
    _In_ ULONG Personality)
{
    UNREFERENCED_PARAMETER(Personality);
    return TRUE;
}

ULONG
LxArchExecPersonality(
    _In_ ULONG Personality)
{
    return ((Personality & 0xFF) != LX_PER_LINUX32) ? (Personality & ~0xFFUL) : Personality;
}

VOID
LxArchMmapRndBits(
    _Out_ PULONG Minimum,
    _Out_ PULONG Maximum)
{
    *Minimum = 18;
    *Maximum = 24;
}

ULONG64
LxArchStackRndMask(VOID)
{
    return 0x3FFFF;
}

LONG
LxArchPtraceSyscallNumber(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ LONG Recorded)
{
    UNREFERENCED_PARAMETER(Recorded);
    return (LONG)TrapFrame->Context.A7;
}

LONG64
LxArchPtraceSyscallReturn(
    _In_ PCONTEXT Context)
{
    return (LONG64)Context->A0;
}

VOID
LxArchClassifyFault(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _Inout_ PLONG Signal,
    _Inout_ PLONG Code)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(Signal);
    UNREFERENCED_PARAMETER(Code);
}

BOOLEAN
LxArchHardwareBreakpointHit(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PULONG64 Address)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(Address);
    return FALSE;
}

BOOLEAN
LxArchEmulateInstruction(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(TrapFrame);
    return FALSE;
}

BOOLEAN
LxArchDeliverAsync(
    _In_ PLX_THREAD Thread)
{
    struct
    {
        CONTEXT Context;
        KTRAP_FRAME TrapFrame;
    } *Frames = LxAlloc(sizeof(*Frames));
    BOOLEAN Delivered = FALSE;

    if (Frames == NULL)
        return FALSE;
    Frames->Context.ContextFlags = CONTEXT_FULL | CONTEXT_EXCEPTION_REQUEST;
    if (NT_SUCCESS(PsGetContextThread(PsGetCurrentThread(), &Frames->Context, KernelMode)) &&
        (Frames->Context.ContextFlags & CONTEXT_EXCEPTION_REPORTING) &&
        !(Frames->Context.ContextFlags & (CONTEXT_SERVICE_ACTIVE | CONTEXT_EXCEPTION_ACTIVE)) &&
        (Frames->Context.Pc <= (ULONG64)(ULONG_PTR)MmHighestUserAddress))
    {
        Frames->Context.ContextFlags = CONTEXT_FULL;
        LxArchRestoreContext(&Frames->TrapFrame, NULL, &Frames->Context);
        LxSignalCheckPending(Thread, &Frames->TrapFrame, NULL);
        LxArchCaptureContext(&Frames->TrapFrame, NULL, &Frames->Context);
        PsSetContextThread(PsGetCurrentThread(), &Frames->Context, KernelMode);
        Delivered = TRUE;
    }
    LxFree(Frames);
    return Delivered;
}
