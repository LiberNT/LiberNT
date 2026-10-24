/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AArch64 Linux system call table and register state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxcore.h"

#define LX_EM_AARCH64     183
#define LX_HWCAP_FP       (1ULL << 0)
#define LX_HWCAP_ASIMD    (1ULL << 1)

#define LX_A64_FRAME_SIZE       4688
#define LX_A64_UC               128
#define LX_A64_UC_STACK         (LX_A64_UC + 16)
#define LX_A64_UC_SIGMASK       (LX_A64_UC + 40)
#define LX_A64_MC               (LX_A64_UC + 176)
#define LX_A64_MC_FAULT         (LX_A64_MC + 0)
#define LX_A64_MC_REGS          (LX_A64_MC + 8)
#define LX_A64_MC_SP            (LX_A64_MC + 256)
#define LX_A64_MC_PC            (LX_A64_MC + 264)
#define LX_A64_MC_PSTATE        (LX_A64_MC + 272)
#define LX_A64_MC_RESERVED      (LX_A64_MC + 288)
#define LX_A64_FPSIMD_MAGIC     0x46508001
#define LX_A64_FPSIMD_SIZE      528
#define LX_A64_PSTATE_NZCV      0xF0000000UL

C_ASSERT(LX_A64_MC_RESERVED + 4096 == LX_A64_FRAME_SIZE);

PLX_SYSCALL_ENTRY
LxArchSyscallEntry(
    _In_ ULONG Number)
{
    return LxGenericSyscallEntry(Number);
}

ULONG
LxArchElfMachine(VOID)
{
    return LX_EM_AARCH64;
}

ULONG64
LxArchHwcap(VOID)
{
    return LX_HWCAP_FP | LX_HWCAP_ASIMD;
}

PCSTR
LxArchPlatform(VOID)
{
    return "aarch64";
}

PCSTR
LxArchCpuinfo(VOID)
{
    return "Features\t: fp asimd\nCPU architecture: 8\n";
}

VOID
LxArchGetSyscall(
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PULONG Number,
    _Out_writes_(6) PULONG64 Args)
{
    ULONG Index;

    *Number = (ULONG)TrapFrame->X8;
    for (Index = 0; Index < 6; Index++)
        Args[Index] = TrapFrame->X[Index];
}

VOID
LxArchSetSyscallResult(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ LONG64 Result)
{
    TrapFrame->X0 = (ULONG64)Result;
}

BOOLEAN
LxArchEmulateInstruction(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ULONG Instruction;
    ULONG64 Value;
    ULONG Rt;

    if (ExceptionRecord->ExceptionCode != STATUS_ILLEGAL_INSTRUCTION)
        return FALSE;
    if (LxCopyFromUser(&Instruction, TrapFrame->Pc, sizeof(Instruction)) != 0)
        return FALSE;
    if ((Instruction & 0xFFFFFFE0) != 0xD53B0020)
        return FALSE;

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Value));
    Rt = Instruction & 0x1F;
    if (Rt <= 18)
        TrapFrame->X[Rt] = Value;
    else if ((Rt <= 28) && (ExceptionFrame != NULL))
        (&ExceptionFrame->X19)[Rt - 19] = Value;
    else if (Rt == 29)
        TrapFrame->Fp = Value;
    else if (Rt == 30)
        TrapFrame->Lr = Value;
    else if (Rt != 31)
        return FALSE;
    TrapFrame->Pc += 4;
    return TRUE;
}

VOID
LxArchRestartSyscall(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Argument)
{
    TrapFrame->X0 = Argument;
    TrapFrame->Pc -= 4;
}

VOID
LxArchSetExecContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Pc,
    _In_ ULONG64 Sp)
{
    RtlZeroMemory(TrapFrame->X, sizeof(TrapFrame->X));
    TrapFrame->Fp = 0;
    TrapFrame->Lr = 0;
    TrapFrame->Sp = Sp;
    TrapFrame->Pc = Pc;
    TrapFrame->Spsr = 0;

    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->X19 = ExceptionFrame->X20 = ExceptionFrame->X21 = 0;
        ExceptionFrame->X22 = ExceptionFrame->X23 = ExceptionFrame->X24 = 0;
        ExceptionFrame->X25 = ExceptionFrame->X26 = ExceptionFrame->X27 = 0;
        ExceptionFrame->X28 = 0;
        ExceptionFrame->Fp = 0;
        ExceptionFrame->Lr = 0;
        ExceptionFrame->Fpcr = 0;
        ExceptionFrame->Fpsr = 0;
    }

    if (TrapFrame->VfpState != NULL)
    {
        RtlZeroMemory(TrapFrame->VfpState->V, sizeof(TrapFrame->VfpState->V));
        TrapFrame->VfpState->Fpcr = 0;
        TrapFrame->VfpState->Fpsr = 0;
    }
}

VOID
LxArchCaptureContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PCONTEXT Context)
{
    ULONG Index;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
    Context->Cpsr = TrapFrame->Spsr;
    for (Index = 0; Index <= 18; Index++)
        Context->X[Index] = TrapFrame->X[Index];

    if (ExceptionFrame != NULL)
    {
        Context->X19 = ExceptionFrame->X19;
        Context->X20 = ExceptionFrame->X20;
        Context->X21 = ExceptionFrame->X21;
        Context->X22 = ExceptionFrame->X22;
        Context->X23 = ExceptionFrame->X23;
        Context->X24 = ExceptionFrame->X24;
        Context->X25 = ExceptionFrame->X25;
        Context->X26 = ExceptionFrame->X26;
        Context->X27 = ExceptionFrame->X27;
        Context->X28 = ExceptionFrame->X28;
        Context->Fp = ExceptionFrame->Fp;
        Context->Lr = ExceptionFrame->Lr;
    }
    else
    {
        Context->Fp = TrapFrame->Fp;
        Context->Lr = TrapFrame->Lr;
    }

    Context->Sp = TrapFrame->Sp;
    Context->Pc = TrapFrame->Pc;

    if (TrapFrame->VfpState != NULL)
    {
        Context->Fpcr = TrapFrame->VfpState->Fpcr;
        Context->Fpsr = TrapFrame->VfpState->Fpsr;
        RtlCopyMemory(Context->V, TrapFrame->VfpState->V, sizeof(Context->V));
    }
}

VOID
LxArchSetForkContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG64 StackPointer)
{
    Context->X0 = 0;
    if (StackPointer != 0)
        Context->Sp = StackPointer;
}

VOID
LxArchSetThreadPointer(
    _In_ ULONG64 Value)
{
    __asm__ __volatile__("msr tpidr_el0, %0" : : "r"(Value) : "memory");
}

ULONG64
LxArchGetThreadPointer(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("mrs %0, tpidr_el0" : "=r"(Value));
    return Value;
}

ULONG64
LxArchGetUserPc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Pc;
}

ULONG64
LxArchGetUserSp(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Sp;
}

ULONG
LxArchSigTrampoline(
    _Out_writes_bytes_(16) PUCHAR Code)
{
    static const ULONG Instructions[2] = { 0xD2801168, 0xD4000001 };

    RtlCopyMemory(Code, Instructions, sizeof(Instructions));
    return sizeof(Instructions);
}

static
VOID
LxArm64SetFpLr(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Fp,
    _In_ ULONG64 Lr)
{
    TrapFrame->Fp = Fp;
    TrapFrame->Lr = Lr;
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->Fp = Fp;
        ExceptionFrame->Lr = Lr;
    }
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
    CONTEXT Context;
    PUCHAR Buffer;
    ULONG64 Record[2];
    ULONG64 Next, Frame;
    PULONG Fpsimd;
    LONG64 Error;

    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);

    Next = LX_ALIGN_DOWN(StackTop - sizeof(Record), 16);
    Frame = Next - LX_A64_FRAME_SIZE;

    Buffer = ExAllocatePoolZero(PagedPool, LX_A64_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return FALSE;

    RtlCopyMemory(Buffer, Info, sizeof(*Info));
    ((PLX_SIGINFO)Buffer)->Signo = Signal;
    RtlCopyMemory(Buffer + LX_A64_UC_STACK, AltStack, sizeof(*AltStack));
    *(PULONG64)(Buffer + LX_A64_UC_SIGMASK) = SavedMask;

    if ((Signal == LX_SIGSEGV) || (Signal == LX_SIGBUS) || (Signal == LX_SIGILL) ||
        (Signal == LX_SIGFPE) || (Signal == LX_SIGTRAP))
    {
        *(PULONG64)(Buffer + LX_A64_MC_FAULT) = Info->Address;
    }
    RtlCopyMemory(Buffer + LX_A64_MC_REGS, Context.X, sizeof(Context.X));
    *(PULONG64)(Buffer + LX_A64_MC_SP) = Context.Sp;
    *(PULONG64)(Buffer + LX_A64_MC_PC) = Context.Pc;
    *(PULONG64)(Buffer + LX_A64_MC_PSTATE) = Context.Cpsr;

    Fpsimd = (PULONG)(Buffer + LX_A64_MC_RESERVED);
    Fpsimd[0] = LX_A64_FPSIMD_MAGIC;
    Fpsimd[1] = LX_A64_FPSIMD_SIZE;
    Fpsimd[2] = Context.Fpsr;
    Fpsimd[3] = Context.Fpcr;
    RtlCopyMemory(&Fpsimd[4], Context.V, sizeof(Context.V));

    Record[0] = Context.Fp;
    Record[1] = Context.Lr;

    Error = LxCopyToUser(Frame, Buffer, LX_A64_FRAME_SIZE);
    if (Error == 0)
        Error = LxCopyToUser(Next, Record, sizeof(Record));
    ExFreePoolWithTag(Buffer, LX_TAG);
    if (Error != 0)
        return FALSE;

    TrapFrame->X0 = (ULONG64)Signal;
    TrapFrame->X1 = Frame;
    TrapFrame->X2 = Frame + LX_A64_UC;
    TrapFrame->Sp = Frame;
    TrapFrame->Pc = Action->Handler;
    LxArm64SetFpLr(TrapFrame, ExceptionFrame, Next, Restorer);
    return TRUE;
}

LONG64
LxArchSignalReturn(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PULONG64 Mask)
{
    ULONG64 Frame = TrapFrame->Sp;
    PULONG64 Regs;
    PULONG Fpsimd;
    PUCHAR Buffer;
    LONG64 Error;
    ULONG Index;

    *Mask = 0;
    if (Frame & 15)
        return -LX_EFAULT;

    Buffer = ExAllocatePoolZero(PagedPool, LX_A64_FRAME_SIZE, LX_TAG);
    if (Buffer == NULL)
        return -LX_ENOMEM;

    Error = LxCopyFromUser(Buffer, Frame, LX_A64_FRAME_SIZE);
    Fpsimd = (PULONG)(Buffer + LX_A64_MC_RESERVED);
    if ((Error == 0) && ((Fpsimd[0] != LX_A64_FPSIMD_MAGIC) || (Fpsimd[1] != LX_A64_FPSIMD_SIZE)))
        Error = -LX_EINVAL;
    if (Error != 0)
    {
        ExFreePoolWithTag(Buffer, LX_TAG);
        return Error;
    }

    Regs = (PULONG64)(Buffer + LX_A64_MC_REGS);
    for (Index = 0; Index <= 18; Index++)
        TrapFrame->X[Index] = Regs[Index];
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->X19 = Regs[19];
        ExceptionFrame->X20 = Regs[20];
        ExceptionFrame->X21 = Regs[21];
        ExceptionFrame->X22 = Regs[22];
        ExceptionFrame->X23 = Regs[23];
        ExceptionFrame->X24 = Regs[24];
        ExceptionFrame->X25 = Regs[25];
        ExceptionFrame->X26 = Regs[26];
        ExceptionFrame->X27 = Regs[27];
        ExceptionFrame->X28 = Regs[28];
    }
    LxArm64SetFpLr(TrapFrame, ExceptionFrame, Regs[29], Regs[30]);
    TrapFrame->Sp = *(PULONG64)(Buffer + LX_A64_MC_SP);
    TrapFrame->Pc = *(PULONG64)(Buffer + LX_A64_MC_PC);
    TrapFrame->Spsr = (TrapFrame->Spsr & ~LX_A64_PSTATE_NZCV) |
                      ((ULONG)*(PULONG64)(Buffer + LX_A64_MC_PSTATE) & LX_A64_PSTATE_NZCV);

    if (TrapFrame->VfpState != NULL)
    {
        TrapFrame->VfpState->Fpsr = Fpsimd[2];
        TrapFrame->VfpState->Fpcr = Fpsimd[3];
        RtlCopyMemory(TrapFrame->VfpState->V, &Fpsimd[4], sizeof(TrapFrame->VfpState->V));
    }

    *Mask = *(PULONG64)(Buffer + LX_A64_UC_SIGMASK);
    ExFreePoolWithTag(Buffer, LX_TAG);
    return 0;
}
