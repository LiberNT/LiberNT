/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x86-64 Linux register state, signal frames and processor information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxcore.h"

#define LX_EM_X86_64                62
#define LX_X64_REDZONE              128
#define LX_X64_FPSTATE_SIZE         512
#define LX_X64_FPSTATE_SW_RESERVED  464
#define LX_X64_UC_SIGCONTEXT_SS     0x2
#define LX_X64_UC_STRICT_RESTORE_SS 0x4
#define LX_X64_EFLAGS_TF            0x100
#define LX_X64_EFLAGS_IF            0x200
#define LX_X64_EFLAGS_DF            0x400
#define LX_X64_EFLAGS_RF            0x10000
#define LX_X64_EFLAGS_FIX           0x50DD5
#define LX_X64_EFLAGS_RESERVED      0x2
#define LX_X64_MXCSR_DEFAULT        0x1F80
#define LX_X64_FCW_DEFAULT          0x37F

#define LX_FPE_INTOVF               2
#define LX_FPE_FLTDIV               3
#define LX_FPE_FLTOVF               4
#define LX_FPE_FLTUND               5
#define LX_FPE_FLTRES               6
#define LX_FPE_FLTINV               7

VOID LxAmd64Fxsave(_Out_ PXMM_SAVE_AREA32 Area);
VOID LxAmd64Fxrstor(_In_ PXMM_SAVE_AREA32 Area);

typedef struct _LX_X64_SIGCONTEXT
{
    ULONG64 R8;
    ULONG64 R9;
    ULONG64 R10;
    ULONG64 R11;
    ULONG64 R12;
    ULONG64 R13;
    ULONG64 R14;
    ULONG64 R15;
    ULONG64 Rdi;
    ULONG64 Rsi;
    ULONG64 Rbp;
    ULONG64 Rbx;
    ULONG64 Rdx;
    ULONG64 Rax;
    ULONG64 Rcx;
    ULONG64 Rsp;
    ULONG64 Rip;
    ULONG64 EFlags;
    USHORT Cs;
    USHORT Gs;
    USHORT Fs;
    USHORT Ss;
    ULONG64 Err;
    ULONG64 TrapNo;
    ULONG64 OldMask;
    ULONG64 Cr2;
    ULONG64 FpState;
    ULONG64 Reserved[8];
} LX_X64_SIGCONTEXT, *PLX_X64_SIGCONTEXT;

C_ASSERT(sizeof(LX_X64_SIGCONTEXT) == 256);

typedef struct _LX_X64_UCONTEXT
{
    ULONG64 Flags;
    ULONG64 Link;
    LX_STACK Stack;
    LX_X64_SIGCONTEXT Mcontext;
    ULONG64 SigMask;
} LX_X64_UCONTEXT, *PLX_X64_UCONTEXT;

C_ASSERT(FIELD_OFFSET(LX_X64_UCONTEXT, Mcontext) == 40);
C_ASSERT(FIELD_OFFSET(LX_X64_UCONTEXT, SigMask) == 296);

typedef struct _LX_X64_SIGFRAME
{
    ULONG64 ReturnAddress;
    LX_X64_UCONTEXT Uc;
    LX_SIGINFO Info;
} LX_X64_SIGFRAME, *PLX_X64_SIGFRAME;

C_ASSERT(sizeof(LX_X64_SIGFRAME) == 440);

static
VOID
LxAmd64LoadX87(
    _In_ PXMM_SAVE_AREA32 Image)
{
    DECLSPEC_ALIGN(16) XMM_SAVE_AREA32 Live;

    LxAmd64Fxsave(&Live);
    RtlCopyMemory(&Live, Image, FIELD_OFFSET(XMM_SAVE_AREA32, MxCsr));
    RtlCopyMemory(Live.FloatRegisters, Image->FloatRegisters, sizeof(Live.FloatRegisters));
    LxAmd64Fxrstor(&Live);
}

static
ULONG
LxAmd64MxCsrMask(VOID)
{
    DECLSPEC_ALIGN(16) XMM_SAVE_AREA32 Live;

    LxAmd64Fxsave(&Live);
    return (Live.MxCsr_Mask != 0) ? Live.MxCsr_Mask : 0xFFBF;
}

PLX_SYSCALL_ENTRY LxAmd64SyscallEntry(_In_ ULONG Number);

PLX_SYSCALL_ENTRY
LxArchSyscallEntry(
    _In_ ULONG Number)
{
    return LxAmd64SyscallEntry(Number);
}

ULONG
LxArchElfMachine(VOID)
{
    return LX_EM_X86_64;
}

ULONG64
LxArchHwcap(VOID)
{
    int Registers[4];

    __cpuid(Registers, 1);
    return (ULONG)Registers[3];
}

ULONG64
LxArchHwcap2(VOID)
{
    return 0;
}

PCSTR
LxArchPlatform(VOID)
{
    return "x86_64";
}

PCSTR
LxArchCpuinfo(VOID)
{
    static const struct
    {
        UCHAR Leaf;
        UCHAR Register;
        UCHAR Bit;
        PCSTR Name;
    } Flags[] =
    {
        { 1, 3, 0, "fpu" }, { 1, 3, 4, "tsc" }, { 1, 3, 5, "msr" }, { 1, 3, 6, "pae" }, { 1, 3, 8, "cx8" },
        { 1, 3, 9, "apic" }, { 1, 3, 11, "sep" }, { 1, 3, 15, "cmov" }, { 1, 3, 16, "pat" }, { 1, 3, 19, "clflush" },
        { 1, 3, 23, "mmx" }, { 1, 3, 24, "fxsr" }, { 1, 3, 25, "sse" }, { 1, 3, 26, "sse2" }, { 1, 3, 28, "ht" },
        { 1, 2, 0, "pni" }, { 1, 2, 1, "pclmulqdq" }, { 1, 2, 9, "ssse3" }, { 1, 2, 12, "fma" }, { 1, 2, 13, "cx16" },
        { 1, 2, 19, "sse4_1" }, { 1, 2, 20, "sse4_2" }, { 1, 2, 22, "movbe" }, { 1, 2, 23, "popcnt" },
        { 1, 2, 25, "aes" }, { 1, 2, 26, "xsave" }, { 1, 2, 28, "avx" }, { 1, 2, 29, "f16c" }, { 1, 2, 30, "rdrand" },
        { 7, 1, 3, "bmi1" }, { 7, 1, 5, "avx2" }, { 7, 1, 8, "bmi2" }, { 7, 1, 9, "erms" }, { 7, 1, 18, "rdseed" },
        { 7, 1, 19, "adx" }, { 7, 1, 29, "sha_ni" },
    };
    static CHAR Text[768];
    static volatile LONG State;
    int Registers[4], Leaf1[4], Leaf7[4] = { 0 };
    CHAR Vendor[13], Brand[49];
    ULONG Index, Length;

    if (ReadAcquire(&State) == 2)
        return Text;
    if (InterlockedCompareExchange(&State, 1, 0) != 0)
    {
        while (ReadAcquire(&State) != 2)
            YieldProcessor();
        return Text;
    }

    __cpuid(Registers, 0);
    RtlCopyMemory(Vendor, &Registers[1], 4);
    RtlCopyMemory(Vendor + 4, &Registers[3], 4);
    RtlCopyMemory(Vendor + 8, &Registers[2], 4);
    Vendor[12] = '\0';
    if ((ULONG)Registers[0] >= 7)
        __cpuidex(Leaf7, 7, 0);
    __cpuid(Leaf1, 1);
    RtlZeroMemory(Brand, sizeof(Brand));
    __cpuid(Registers, 0x80000000);
    if ((ULONG)Registers[0] >= 0x80000004)
    {
        for (Index = 0; Index < 3; Index++)
            __cpuid((int *)(Brand + Index * 16), 0x80000002 + Index);
    }

    Length = (ULONG)_snprintf(Text, sizeof(Text) - 1,
                              "vendor_id\t: %s\ncpu family\t: %lu\nmodel\t\t: %lu\nmodel name\t: %s\nstepping\t: %lu\n"
                              "fpu\t\t: yes\nfpu_exception\t: yes\nwp\t\t: yes\nflags\t\t:",
                              Vendor,
                              ((Leaf1[0] >> 8) & 0xF) + (((Leaf1[0] >> 8) & 0xF) == 0xF ? ((Leaf1[0] >> 20) & 0xFF) : 0),
                              ((Leaf1[0] >> 4) & 0xF) | ((Leaf1[0] >> 12) & 0xF0),
                              Brand,
                              Leaf1[0] & 0xF);
    for (Index = 0; (Index < RTL_NUMBER_OF(Flags)) && (Length < sizeof(Text) - 16); Index++)
    {
        int *Source = (Flags[Index].Leaf == 1) ? Leaf1 : Leaf7;

        if (Source[Flags[Index].Register] & (1 << Flags[Index].Bit))
            Length += (ULONG)_snprintf(Text + Length, sizeof(Text) - 1 - Length, " %s", Flags[Index].Name);
    }
    if (Length < sizeof(Text) - 1)
        Text[Length++] = '\n';
    Text[min(Length, sizeof(Text) - 1)] = '\0';
    WriteRelease(&State, 2);
    return Text;
}

PCSTR
LxArchConfigName(VOID)
{
    return "x86";
}

PCSTR
LxArchKernelConfig(VOID)
{
    return "CONFIG_X86=y\nCONFIG_X86_64=y\nCONFIG_64BIT=y\n";
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
    *Number = (ULONG)TrapFrame->Rax;
    TrapFrame->ErrorCode = TrapFrame->Rax;
    Args[0] = (ExceptionFrame != NULL) ? ExceptionFrame->Rdi : TrapFrame->Rdi;
    Args[1] = (ExceptionFrame != NULL) ? ExceptionFrame->Rsi : TrapFrame->Rsi;
    Args[2] = TrapFrame->Rdx;
    Args[3] = TrapFrame->R10;
    Args[4] = TrapFrame->R8;
    Args[5] = TrapFrame->R9;
}

VOID
LxArchSetSyscallResult(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ LONG64 Result)
{
    TrapFrame->Rax = (ULONG64)Result;
}

VOID
LxArchRestartSyscall(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Argument)
{
    UNREFERENCED_PARAMETER(Argument);
    TrapFrame->Rax = TrapFrame->ErrorCode;
    TrapFrame->Rip -= 2;
}

VOID
LxArchCaptureContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PCONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
    LxAmd64Fxsave(&Context->FltSave);
    Context->Rax = TrapFrame->Rax;
    Context->Rcx = TrapFrame->Rcx;
    Context->Rdx = TrapFrame->Rdx;
    Context->Rbp = TrapFrame->Rbp;
    Context->R8 = TrapFrame->R8;
    Context->R9 = TrapFrame->R9;
    Context->R10 = TrapFrame->R10;
    Context->R11 = TrapFrame->R11;
    Context->Rip = TrapFrame->Rip;
    Context->Rsp = TrapFrame->Rsp;
    Context->EFlags = TrapFrame->EFlags;
    Context->SegCs = KGDT64_R3_CODE | RPL_MASK;
    Context->SegSs = KGDT64_R3_DATA | RPL_MASK;
    Context->MxCsr = Context->FltSave.MxCsr = TrapFrame->MxCsr;
    Context->Xmm0 = TrapFrame->Xmm0;
    Context->Xmm1 = TrapFrame->Xmm1;
    Context->Xmm2 = TrapFrame->Xmm2;
    Context->Xmm3 = TrapFrame->Xmm3;
    Context->Xmm4 = TrapFrame->Xmm4;
    Context->Xmm5 = TrapFrame->Xmm5;
    if (ExceptionFrame != NULL)
    {
        Context->Rbx = ExceptionFrame->Rbx;
        Context->Rdi = ExceptionFrame->Rdi;
        Context->Rsi = ExceptionFrame->Rsi;
        Context->R12 = ExceptionFrame->R12;
        Context->R13 = ExceptionFrame->R13;
        Context->R14 = ExceptionFrame->R14;
        Context->R15 = ExceptionFrame->R15;
        Context->Xmm6 = ExceptionFrame->Xmm6;
        Context->Xmm7 = ExceptionFrame->Xmm7;
        Context->Xmm8 = ExceptionFrame->Xmm8;
        Context->Xmm9 = ExceptionFrame->Xmm9;
        Context->Xmm10 = ExceptionFrame->Xmm10;
        Context->Xmm11 = ExceptionFrame->Xmm11;
        Context->Xmm12 = ExceptionFrame->Xmm12;
        Context->Xmm13 = ExceptionFrame->Xmm13;
        Context->Xmm14 = ExceptionFrame->Xmm14;
        Context->Xmm15 = ExceptionFrame->Xmm15;
    }
}

VOID
LxArchRestoreContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PCONTEXT Context)
{
    TrapFrame->Rax = Context->Rax;
    TrapFrame->Rcx = Context->Rcx;
    TrapFrame->Rdx = Context->Rdx;
    TrapFrame->Rbp = Context->Rbp;
    TrapFrame->R8 = Context->R8;
    TrapFrame->R9 = Context->R9;
    TrapFrame->R10 = Context->R10;
    TrapFrame->R11 = Context->R11;
    TrapFrame->Rdi = Context->Rdi;
    TrapFrame->Rsi = Context->Rsi;
    TrapFrame->Rbx = Context->Rbx;
    TrapFrame->Rip = Context->Rip;
    TrapFrame->Rsp = Context->Rsp;
    TrapFrame->EFlags = (Context->EFlags & EFLAGS_USER_SANITIZE) | LX_X64_EFLAGS_IF | LX_X64_EFLAGS_RESERVED;
    TrapFrame->SegCs = KGDT64_R3_CODE | RPL_MASK;
    TrapFrame->SegSs = KGDT64_R3_DATA | RPL_MASK;
    TrapFrame->MxCsr = Context->MxCsr & LxAmd64MxCsrMask();
    TrapFrame->Xmm0 = Context->Xmm0;
    TrapFrame->Xmm1 = Context->Xmm1;
    TrapFrame->Xmm2 = Context->Xmm2;
    TrapFrame->Xmm3 = Context->Xmm3;
    TrapFrame->Xmm4 = Context->Xmm4;
    TrapFrame->Xmm5 = Context->Xmm5;
    if (ExceptionFrame != NULL)
    {
        ExceptionFrame->Rbx = Context->Rbx;
        ExceptionFrame->Rdi = Context->Rdi;
        ExceptionFrame->Rsi = Context->Rsi;
        ExceptionFrame->R12 = Context->R12;
        ExceptionFrame->R13 = Context->R13;
        ExceptionFrame->R14 = Context->R14;
        ExceptionFrame->R15 = Context->R15;
        ExceptionFrame->Xmm6 = Context->Xmm6;
        ExceptionFrame->Xmm7 = Context->Xmm7;
        ExceptionFrame->Xmm8 = Context->Xmm8;
        ExceptionFrame->Xmm9 = Context->Xmm9;
        ExceptionFrame->Xmm10 = Context->Xmm10;
        ExceptionFrame->Xmm11 = Context->Xmm11;
        ExceptionFrame->Xmm12 = Context->Xmm12;
        ExceptionFrame->Xmm13 = Context->Xmm13;
        ExceptionFrame->Xmm14 = Context->Xmm14;
        ExceptionFrame->Xmm15 = Context->Xmm15;
    }
    if (Context->ContextFlags & CONTEXT_FLOATING_POINT & ~CONTEXT_AMD64)
        LxAmd64LoadX87(&Context->FltSave);
}

VOID
LxArchSetExecContext(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ ULONG64 Pc,
    _In_ ULONG64 Sp)
{
    DECLSPEC_ALIGN(16) CONTEXT Context;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
    Context.Rip = Pc;
    Context.Rsp = Sp;
    Context.EFlags = LX_X64_EFLAGS_IF;
    Context.MxCsr = Context.FltSave.MxCsr = LX_X64_MXCSR_DEFAULT;
    Context.FltSave.ControlWord = LX_X64_FCW_DEFAULT;
    /* FIXME: the AVX upper halves and other extended state keep their values across exec */
    LxArchRestoreContext(TrapFrame, ExceptionFrame, &Context);
}

ULONG
LxArchCoreRegisters(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_writes_(LX_CORE_MAX_REGISTERS) PULONG64 Registers)
{
    DECLSPEC_ALIGN(16) CONTEXT Context;
    ULONG64 Regset[LX_PTRACE_REGSET_MAX / sizeof(ULONG64)];
    ULONG Size, Unit;

    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);
    LxArchPtraceGetRegset(&Context, LxArchGetThreadPointer(TrapFrame), (LONG)TrapFrame->ErrorCode, LX_NT_PRSTATUS,
                          (PUCHAR)Regset, &Size, &Unit);
    Size = min(Size, LX_CORE_MAX_REGISTERS * sizeof(ULONG64));
    RtlCopyMemory(Registers, Regset, Size);
    return Size / sizeof(ULONG64);
}

VOID
LxArchSetForkContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG64 StackPointer)
{
    Context->Rax = 0;
    if (StackPointer != 0)
        Context->Rsp = StackPointer;
}

VOID
LxArchSetThreadPointer(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG64 Value)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    PsGetCurrentThread()->UserFsBase = Value;
    __writemsr(MSR_FS_BASE, Value);
}

ULONG64
LxArchGetThreadPointer(
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    return PsGetCurrentThread()->UserFsBase;
}

ULONG64
LxArchGetUserPc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Rip;
}

ULONG64
LxArchContextPc(
    _In_ PCONTEXT Context)
{
    return Context->Rip;
}

ULONG64
LxArchContextSp(
    _In_ PCONTEXT Context)
{
    return Context->Rsp;
}

ULONG64
LxArchGetUserSp(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Rsp;
}

ULONG
LxArchSigTrampoline(
    _Out_writes_bytes_(16) PUCHAR Code)
{
    static const UCHAR Instructions[] = { 0xB8, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x05 };

    RtlCopyMemory(Code, Instructions, sizeof(Instructions));
    return sizeof(Instructions);
}

LONG64
LxArchReadSigaction(
    _In_ ULONG64 Address,
    _Out_ PLX_SIGACTION Action)
{
    ULONG64 Values[4];
    LONG64 Error = LxCopyFromUser(Values, Address, sizeof(Values));

    if (Error == 0)
    {
        Action->Handler = Values[0];
        Action->Flags = Values[1];
        Action->Restorer = Values[2];
        Action->Mask = Values[3];
    }
    return Error;
}

LONG64
LxArchWriteSigaction(
    _In_ ULONG64 Address,
    _In_ PLX_SIGACTION Action)
{
    ULONG64 Values[4] = { Action->Handler, Action->Flags, Action->Restorer, Action->Mask };

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
    DECLSPEC_ALIGN(16) CONTEXT Context;
    LX_X64_SIGFRAME Frame;
    PLX_X64_SIGCONTEXT Mc = &Frame.Uc.Mcontext;
    ULONG64 FpState, Address;

    if (StackTop == TrapFrame->Rsp)
        StackTop -= LX_X64_REDZONE;
    if (StackTop < LX_X64_FPSTATE_SIZE + sizeof(Frame) + 128)
        return FALSE;
    FpState = LX_ALIGN_DOWN(StackTop - LX_X64_FPSTATE_SIZE, 64);
    Address = LX_ALIGN_DOWN(FpState - sizeof(Frame), 16) - 8;

    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);
    RtlZeroMemory((PUCHAR)&Context.FltSave + LX_X64_FPSTATE_SW_RESERVED,
                  LX_X64_FPSTATE_SIZE - LX_X64_FPSTATE_SW_RESERVED);
    if (LxCopyToUser(FpState, &Context.FltSave, LX_X64_FPSTATE_SIZE) != 0)
        return FALSE;

    RtlZeroMemory(&Frame, sizeof(Frame));
    Frame.ReturnAddress = Restorer;
    Frame.Uc.Flags = LX_X64_UC_SIGCONTEXT_SS | LX_X64_UC_STRICT_RESTORE_SS;
    Frame.Uc.Stack = *AltStack;
    Frame.Uc.SigMask = SavedMask;
    Mc->R8 = Context.R8;
    Mc->R9 = Context.R9;
    Mc->R10 = Context.R10;
    Mc->R11 = Context.R11;
    Mc->R12 = Context.R12;
    Mc->R13 = Context.R13;
    Mc->R14 = Context.R14;
    Mc->R15 = Context.R15;
    Mc->Rdi = Context.Rdi;
    Mc->Rsi = Context.Rsi;
    Mc->Rbp = Context.Rbp;
    Mc->Rbx = Context.Rbx;
    Mc->Rdx = Context.Rdx;
    Mc->Rax = Context.Rax;
    Mc->Rcx = Context.Rcx;
    Mc->Rsp = Context.Rsp;
    Mc->Rip = Context.Rip;
    Mc->EFlags = Context.EFlags;
    Mc->Cs = KGDT64_R3_CODE | RPL_MASK;
    Mc->Ss = KGDT64_R3_DATA | RPL_MASK;
    /* FIXME: the trap number and page fault error code of the fault are not reported */
    if ((Signal == LX_SIGSEGV) || (Signal == LX_SIGBUS))
        Mc->Cr2 = Info->Address;
    Mc->OldMask = SavedMask;
    Mc->FpState = FpState;
    Frame.Info = *Info;
    Frame.Info.Signo = Signal;
    if (LxCopyToUser(Address, &Frame, sizeof(Frame)) != 0)
        return FALSE;

    Context.Rdi = (ULONG64)Signal;
    Context.Rsi = Address + FIELD_OFFSET(LX_X64_SIGFRAME, Info);
    Context.Rdx = Address + FIELD_OFFSET(LX_X64_SIGFRAME, Uc);
    Context.Rax = 0;
    Context.Rsp = Address;
    Context.Rip = Action->Handler;
    Context.EFlags &= ~(LX_X64_EFLAGS_DF | LX_X64_EFLAGS_RF | LX_X64_EFLAGS_TF);
    Context.ContextFlags &= ~CONTEXT_FLOATING_POINT | CONTEXT_AMD64;
    LxArchRestoreContext(TrapFrame, ExceptionFrame, &Context);
    return TRUE;
}

LONG64
LxArchSignalReturn(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PULONG64 Mask)
{
    DECLSPEC_ALIGN(16) CONTEXT Context;
    LX_X64_UCONTEXT Uc;
    PLX_X64_SIGCONTEXT Mc = &Uc.Mcontext;
    LONG64 Error;

    *Mask = 0;
    Error = LxCopyFromUser(&Uc, TrapFrame->Rsp, sizeof(Uc));
    if (Error != 0)
        return Error;
    LxArchCaptureContext(TrapFrame, ExceptionFrame, &Context);
    if (Mc->FpState != 0)
    {
        Error = LxCopyFromUser(&Context.FltSave, Mc->FpState, LX_X64_FPSTATE_SIZE);
        if (Error != 0)
            return Error;
        Context.MxCsr = Context.FltSave.MxCsr;
    }
    else
    {
        Context.ContextFlags &= ~CONTEXT_FLOATING_POINT | CONTEXT_AMD64;
    }
    Context.R8 = Mc->R8;
    Context.R9 = Mc->R9;
    Context.R10 = Mc->R10;
    Context.R11 = Mc->R11;
    Context.R12 = Mc->R12;
    Context.R13 = Mc->R13;
    Context.R14 = Mc->R14;
    Context.R15 = Mc->R15;
    Context.Rdi = Mc->Rdi;
    Context.Rsi = Mc->Rsi;
    Context.Rbp = Mc->Rbp;
    Context.Rbx = Mc->Rbx;
    Context.Rdx = Mc->Rdx;
    Context.Rax = Mc->Rax;
    Context.Rcx = Mc->Rcx;
    Context.Rsp = Mc->Rsp;
    Context.Rip = Mc->Rip;
    Context.EFlags = (Context.EFlags & ~LX_X64_EFLAGS_FIX) | (Mc->EFlags & LX_X64_EFLAGS_FIX);
    LxArchRestoreContext(TrapFrame, ExceptionFrame, &Context);
    *Mask = Uc.SigMask;
    return 0;
}

LONG
LxArchSpecStoreBypassMode(VOID)
{
    return LX_SPEC_MODE_UNSUPPORTED;
}

/* FIXME: the processor's vulnerabilities and mitigations are not probed */
PCSTR
LxArchCpuVulnerability(
    _In_ PCSTR Name)
{
    UNREFERENCED_PARAMETER(Name);
    return "Unknown: No mitigations";
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

    RtlZeroMemory(Buffer, LX_PTRACE_REGSET_MAX);
    switch (Type)
    {
        case LX_NT_PRSTATUS:
            Registers[0] = Context->R15;
            Registers[1] = Context->R14;
            Registers[2] = Context->R13;
            Registers[3] = Context->R12;
            Registers[4] = Context->Rbp;
            Registers[5] = Context->Rbx;
            Registers[6] = Context->R11;
            Registers[7] = Context->R10;
            Registers[8] = Context->R9;
            Registers[9] = Context->R8;
            Registers[10] = Context->Rax;
            Registers[11] = Context->Rcx;
            Registers[12] = Context->Rdx;
            Registers[13] = Context->Rsi;
            Registers[14] = Context->Rdi;
            Registers[15] = (ULONG64)(LONG64)SyscallNr;
            Registers[16] = Context->Rip;
            Registers[17] = KGDT64_R3_CODE | RPL_MASK;
            Registers[18] = Context->EFlags;
            Registers[19] = Context->Rsp;
            Registers[20] = KGDT64_R3_DATA | RPL_MASK;
            Registers[21] = Tls;
            Registers[22] = PsGetCurrentThread()->UserGsBase;
            *Size = 27 * sizeof(ULONG64);
            *Unit = sizeof(ULONG64);
            return 0;

        case LX_NT_PRFPREG:
            RtlCopyMemory(Buffer, &Context->FltSave, LX_X64_FPSTATE_SIZE);
            *Size = LX_X64_FPSTATE_SIZE;
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

    switch (Type)
    {
        case LX_NT_PRSTATUS:
            if (Length < 27 * sizeof(ULONG64))
                return -LX_EINVAL;
            Context->R15 = Registers[0];
            Context->R14 = Registers[1];
            Context->R13 = Registers[2];
            Context->R12 = Registers[3];
            Context->Rbp = Registers[4];
            Context->Rbx = Registers[5];
            Context->R11 = Registers[6];
            Context->R10 = Registers[7];
            Context->R9 = Registers[8];
            Context->R8 = Registers[9];
            Context->Rax = Registers[10];
            Context->Rcx = Registers[11];
            Context->Rdx = Registers[12];
            Context->Rsi = Registers[13];
            Context->Rdi = Registers[14];
            *SyscallNr = (LONG)Registers[15];
            Context->Rip = Registers[16];
            Context->EFlags = (Context->EFlags & ~LX_X64_EFLAGS_FIX) | (Registers[18] & LX_X64_EFLAGS_FIX);
            Context->Rsp = Registers[19];
            *Tls = Registers[21];
            return 0;

        case LX_NT_PRFPREG:
            if (Length < LX_X64_FPSTATE_SIZE)
                return -LX_EINVAL;
            RtlCopyMemory(&Context->FltSave, Buffer, LX_X64_FPSTATE_SIZE);
            Context->MxCsr = Context->FltSave.MxCsr;
            Context->ContextFlags |= CONTEXT_FLOATING_POINT;
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
    if (Enable)
        Context->EFlags |= LX_X64_EFLAGS_TF;
    else
        Context->EFlags &= ~LX_X64_EFLAGS_TF;
    return TRUE;
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
    *Minimum = 28;
    *Maximum = 32;
}

ULONG64
LxArchStackRndMask(VOID)
{
    return 0x3FFFFF;
}

LONG
LxArchPtraceSyscallNumber(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ LONG Recorded)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    return Recorded;
}

LONG64
LxArchPtraceSyscallReturn(
    _In_ PCONTEXT Context)
{
    return (LONG64)Context->Rax;
}

VOID
LxArchClassifyFault(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PKTRAP_FRAME TrapFrame,
    _Inout_ PLONG Signal,
    _Inout_ PLONG Code)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    switch (ExceptionRecord->ExceptionCode)
    {
        case STATUS_BREAKPOINT:
            *Code = LX_SI_KERNEL;
            break;
        case STATUS_PRIVILEGED_INSTRUCTION:
            *Signal = LX_SIGSEGV;
            *Code = LX_SI_KERNEL;
            break;
        case STATUS_INTEGER_OVERFLOW:
            *Code = LX_FPE_INTOVF;
            break;
        case STATUS_FLOAT_DIVIDE_BY_ZERO:
            *Code = LX_FPE_FLTDIV;
            break;
        case STATUS_FLOAT_OVERFLOW:
            *Code = LX_FPE_FLTOVF;
            break;
        case STATUS_FLOAT_UNDERFLOW:
            *Code = LX_FPE_FLTUND;
            break;
        case STATUS_FLOAT_INEXACT_RESULT:
            *Code = LX_FPE_FLTRES;
            break;
        case STATUS_FLOAT_INVALID_OPERATION:
        case STATUS_FLOAT_DENORMAL_OPERAND:
        case STATUS_FLOAT_STACK_CHECK:
            *Code = LX_FPE_FLTINV;
            break;
        default:
            break;
    }
}

/* TODO: hardware breakpoints and watchpoints through DR0-DR3 */
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

/* TODO: the legacy vsyscall page at 0xffffffffff600000 is not emulated */
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
        KEXCEPTION_FRAME ExceptionFrame;
    } *Frames = LxAlloc(sizeof(*Frames));
    BOOLEAN Delivered = FALSE;

    if (Frames == NULL)
        return FALSE;
    Frames->Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT |
                                   CONTEXT_EXCEPTION_REQUEST;
    if (NT_SUCCESS(PsGetContextThread(PsGetCurrentThread(), &Frames->Context, KernelMode)) &&
        (Frames->Context.ContextFlags & CONTEXT_EXCEPTION_REPORTING) &&
        !(Frames->Context.ContextFlags & (CONTEXT_SERVICE_ACTIVE | CONTEXT_EXCEPTION_ACTIVE)) &&
        ((Frames->Context.SegCs & RPL_MASK) == RPL_MASK))
    {
        Frames->Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
        LxArchRestoreContext(&Frames->TrapFrame, &Frames->ExceptionFrame, &Frames->Context);
        LxSignalCheckPending(Thread, &Frames->TrapFrame, &Frames->ExceptionFrame);
        LxArchCaptureContext(&Frames->TrapFrame, &Frames->ExceptionFrame, &Frames->Context);
        PsSetContextThread(PsGetCurrentThread(), &Frames->Context, KernelMode);
        Delivered = TRUE;
    }
    LxFree(Frames);
    return Delivered;
}
