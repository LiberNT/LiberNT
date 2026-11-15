/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 conversion between CONTEXT records and trap and exception frames
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

#define KI_DR6_LEGAL 0xE00F

typedef struct _KI_FIELD_PAIR
{
    USHORT ContextOffset;
    USHORT FrameOffset;
} KI_FIELD_PAIR;

#define KI_PAIR(Frame, Field) { (USHORT)FIELD_OFFSET(CONTEXT, Field), (USHORT)FIELD_OFFSET(Frame, Field) }

static const KI_FIELD_PAIR KiTrapIntegers[] =
{
    KI_PAIR(KTRAP_FRAME, Rax), KI_PAIR(KTRAP_FRAME, Rcx), KI_PAIR(KTRAP_FRAME, Rdx), KI_PAIR(KTRAP_FRAME, Rbp),
    KI_PAIR(KTRAP_FRAME, R8), KI_PAIR(KTRAP_FRAME, R9), KI_PAIR(KTRAP_FRAME, R10), KI_PAIR(KTRAP_FRAME, R11),
};

static const KI_FIELD_PAIR KiExceptionIntegers[] =
{
    KI_PAIR(KEXCEPTION_FRAME, Rbx), KI_PAIR(KEXCEPTION_FRAME, Rsi), KI_PAIR(KEXCEPTION_FRAME, Rdi),
    KI_PAIR(KEXCEPTION_FRAME, R12), KI_PAIR(KEXCEPTION_FRAME, R13), KI_PAIR(KEXCEPTION_FRAME, R14),
    KI_PAIR(KEXCEPTION_FRAME, R15),
};

static const KI_FIELD_PAIR KiTrapVectors[] =
{
    KI_PAIR(KTRAP_FRAME, Xmm0), KI_PAIR(KTRAP_FRAME, Xmm1), KI_PAIR(KTRAP_FRAME, Xmm2),
    KI_PAIR(KTRAP_FRAME, Xmm3), KI_PAIR(KTRAP_FRAME, Xmm4), KI_PAIR(KTRAP_FRAME, Xmm5),
};

static const KI_FIELD_PAIR KiExceptionVectors[] =
{
    KI_PAIR(KEXCEPTION_FRAME, Xmm6), KI_PAIR(KEXCEPTION_FRAME, Xmm7), KI_PAIR(KEXCEPTION_FRAME, Xmm8),
    KI_PAIR(KEXCEPTION_FRAME, Xmm9), KI_PAIR(KEXCEPTION_FRAME, Xmm10), KI_PAIR(KEXCEPTION_FRAME, Xmm11),
    KI_PAIR(KEXCEPTION_FRAME, Xmm12), KI_PAIR(KEXCEPTION_FRAME, Xmm13), KI_PAIR(KEXCEPTION_FRAME, Xmm14),
    KI_PAIR(KEXCEPTION_FRAME, Xmm15),
};

static const KI_FIELD_PAIR KiDebugRegisters[] =
{
    KI_PAIR(KTRAP_FRAME, Dr0), KI_PAIR(KTRAP_FRAME, Dr1), KI_PAIR(KTRAP_FRAME, Dr2),
    KI_PAIR(KTRAP_FRAME, Dr3), KI_PAIR(KTRAP_FRAME, Dr6), KI_PAIR(KTRAP_FRAME, Dr7),
};

static
VOID
KiCopyIntegers(
    _Inout_ PCONTEXT Context,
    _Inout_ PVOID Frame,
    _In_reads_(Count) const KI_FIELD_PAIR *Pairs,
    _In_ ULONG Count,
    _In_ BOOLEAN ToContext)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        PULONG64 InContext = (PULONG64)((PUCHAR)Context + Pairs[Index].ContextOffset);
        PULONG64 InFrame = (PULONG64)((PUCHAR)Frame + Pairs[Index].FrameOffset);

        if (ToContext)
            *InContext = *InFrame;
        else
            *InFrame = *InContext;
    }
}

static
VOID
KiCopyVectors(
    _Inout_ PCONTEXT Context,
    _Inout_ PVOID Frame,
    _In_reads_(Count) const KI_FIELD_PAIR *Pairs,
    _In_ ULONG Count,
    _In_ BOOLEAN ToContext)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        PM128A InContext = (PM128A)((PUCHAR)Context + Pairs[Index].ContextOffset);
        PM128A InFrame = (PM128A)((PUCHAR)Frame + Pairs[Index].FrameOffset);

        if (ToContext)
            *InContext = *InFrame;
        else
            *InFrame = *InContext;
    }
}

static
KIRQL
KiEnterFrameConversion(VOID)
{
    KIRQL OldIrql = KeGetCurrentIrql();

    /* APC_LEVEL keeps a get/set context APC from changing the frames under us */
    if (OldIrql < APC_LEVEL)
        KeRaiseIrql(APC_LEVEL, &OldIrql);

    return OldIrql;
}

static
VOID
KiLeaveFrameConversion(
    _In_ KIRQL OldIrql)
{
    if (OldIrql < APC_LEVEL)
        KeLowerIrql(OldIrql);
}

static
BOOLEAN
KiIsUserFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return (BOOLEAN)((TrapFrame->SegCs & MODE_MASK) != KernelMode);
}

static
VOID
KiControlToFrame(
    _In_ PCONTEXT Context,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    TrapFrame->Rip = Context->Rip;
    TrapFrame->Rsp = Context->Rsp;

    if ((PreviousMode == KernelMode) && ((Context->SegCs & MODE_MASK) == KernelMode))
    {
        TrapFrame->SegCs = KGDT64_R0_CODE;
        TrapFrame->SegSs = KGDT64_R0_DATA;
        TrapFrame->EFlags = Context->EFlags & (EFLAGS_USER_SANITIZE | EFLAGS_INTERRUPT_MASK);
        return;
    }

    /* User mode runs the 64-bit code selector or, for anything else, the compatibility one */
    TrapFrame->SegCs = (Context->SegCs == (KGDT64_R3_CODE | RPL_MASK)) ?
                       (KGDT64_R3_CODE | RPL_MASK) : (KGDT64_R3_CMCODE | RPL_MASK);
    TrapFrame->SegSs = KGDT64_R3_DATA | RPL_MASK;
    TrapFrame->EFlags = (Context->EFlags & EFLAGS_USER_SANITIZE) | EFLAGS_INTERRUPT_MASK;
}

static
VOID
KiControlToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _Inout_ PCONTEXT Context)
{
    if (KiIsUserFrame(TrapFrame))
    {
        Context->SegCs = TrapFrame->SegCs;
        Context->SegSs = TrapFrame->SegSs;
    }
    else
    {
        Context->SegCs = KGDT64_R0_CODE;
        Context->SegSs = KGDT64_R0_DATA;
    }

    Context->Rip = TrapFrame->Rip;
    Context->Rsp = TrapFrame->Rsp;
    Context->EFlags = TrapFrame->EFlags;
}

static
VOID
KiDebugToFrame(
    _In_ PCONTEXT Context,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PULONG64 Address[] = { &TrapFrame->Dr0, &TrapFrame->Dr1, &TrapFrame->Dr2, &TrapFrame->Dr3 };
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG Index;

    KiCopyIntegers(Context, TrapFrame, KiDebugRegisters, RTL_NUMBER_OF(KiDebugRegisters), FALSE);

    /* A context with only debug registers need not carry SegCs, so the caller's mode decides */
    if (PreviousMode != KernelMode)
    {
        TrapFrame->Dr6 &= KI_DR6_LEGAL;
        TrapFrame->Dr7 &= DR7_LEGAL;
        for (Index = 0; Index < RTL_NUMBER_OF(Address); Index++)
        {
            if (*Address[Index] > (ULONG64)MmHighestUserAddress)
                *Address[Index] = 0;
        }
    }

    /* Thread creation fills another thread's frame; only our own frame updates our flags */
    if (((PreviousMode != KernelMode) || KiIsUserFrame(TrapFrame)) &&
        ((ULONG_PTR)TrapFrame >= (ULONG_PTR)Thread->StackLimit) &&
        ((ULONG_PTR)TrapFrame < (ULONG_PTR)Thread->InitialStack))
    {
        /* Disabled slots count too: GetThreadContext must still see their addresses */
        if (TrapFrame->Dr0 | TrapFrame->Dr1 | TrapFrame->Dr2 |
            TrapFrame->Dr3 | TrapFrame->Dr6 | TrapFrame->Dr7)
            Thread->Header.DebugActive |= DEBUG_ACTIVE_DR7;
        else
            Thread->Header.DebugActive &= ~DEBUG_ACTIVE_DR7;
    }
}

VOID
NTAPI
KeContextToTrapFrame(
    _In_ PCONTEXT Context,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    KIRQL OldIrql;

    ASSERT(ContextFlags & CONTEXT_AMD64);
    ContextFlags &= ~CONTEXT_AMD64;

    OldIrql = KiEnterFrameConversion();

    if (ContextFlags & CONTEXT_INTEGER)
    {
        KiCopyIntegers(Context, TrapFrame, KiTrapIntegers, RTL_NUMBER_OF(KiTrapIntegers), FALSE);
        if (ExceptionFrame)
            KiCopyIntegers(Context, ExceptionFrame, KiExceptionIntegers, RTL_NUMBER_OF(KiExceptionIntegers), FALSE);
    }

    if (ContextFlags & CONTEXT_FLOATING_POINT)
    {
        if (KiIsUserFrame(TrapFrame))
            KiRestoreUserLegacyFloatingState(&Context->FltSave);

        TrapFrame->MxCsr = Context->MxCsr & KiMxCsrMask;
        KiCopyVectors(Context, TrapFrame, KiTrapVectors, RTL_NUMBER_OF(KiTrapVectors), FALSE);
        if (ExceptionFrame)
            KiCopyVectors(Context, ExceptionFrame, KiExceptionVectors, RTL_NUMBER_OF(KiExceptionVectors), FALSE);
    }

    if (ContextFlags & CONTEXT_CONTROL)
        KiControlToFrame(Context, TrapFrame, PreviousMode);

    if (ContextFlags & CONTEXT_SEGMENTS)
    {
        TrapFrame->SegDs = KGDT64_R3_DATA | RPL_MASK;
        TrapFrame->SegEs = KGDT64_R3_DATA | RPL_MASK;
        TrapFrame->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
        TrapFrame->SegGs = KGDT64_R3_DATA | RPL_MASK;
    }

    if (ContextFlags & CONTEXT_DEBUG_REGISTERS)
        KiDebugToFrame(Context, TrapFrame, PreviousMode);

    KiLeaveFrameConversion(OldIrql);
}

VOID
NTAPI
KeTrapFrameToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PCONTEXT Context)
{
    ULONG ContextFlags;
    KIRQL OldIrql;

    OldIrql = KiEnterFrameConversion();

    ContextFlags = Context->ContextFlags;
    ASSERT(ContextFlags & CONTEXT_AMD64);
    ContextFlags &= ~CONTEXT_AMD64;

    if (ContextFlags & CONTEXT_INTEGER)
    {
        KiCopyIntegers(Context, TrapFrame, KiTrapIntegers, RTL_NUMBER_OF(KiTrapIntegers), TRUE);
        if (ExceptionFrame)
            KiCopyIntegers(Context, ExceptionFrame, KiExceptionIntegers, RTL_NUMBER_OF(KiExceptionIntegers), TRUE);
    }

    if (ContextFlags & CONTEXT_FLOATING_POINT)
    {
        if (KiIsUserFrame(TrapFrame))
            KiCaptureUserLegacyFloatingState(&Context->FltSave);

        Context->FltSave.MxCsr = TrapFrame->MxCsr;
        Context->MxCsr = TrapFrame->MxCsr;
        KiCopyVectors(Context, TrapFrame, KiTrapVectors, RTL_NUMBER_OF(KiTrapVectors), TRUE);
        if (ExceptionFrame)
            KiCopyVectors(Context, ExceptionFrame, KiExceptionVectors, RTL_NUMBER_OF(KiExceptionVectors), TRUE);
    }

    if (ContextFlags & CONTEXT_CONTROL)
        KiControlToContext(TrapFrame, Context);

    if (ContextFlags & CONTEXT_SEGMENTS)
    {
        Context->SegDs = KGDT64_R3_DATA | RPL_MASK;
        Context->SegEs = KGDT64_R3_DATA | RPL_MASK;
        Context->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
        Context->SegGs = KGDT64_R3_DATA | RPL_MASK;
    }

    if (ContextFlags & CONTEXT_DEBUG_REGISTERS)
        KiCopyIntegers(Context, TrapFrame, KiDebugRegisters, RTL_NUMBER_OF(KiDebugRegisters), TRUE);

    KiLeaveFrameConversion(OldIrql);
}

VOID
RtlGetUnwindContext(
    _Out_ PCONTEXT Context,
    _In_ DWORD64 TargetFrame);

VOID
RtlSetUnwindContext(
    _In_ PCONTEXT Context,
    _In_ DWORD64 TargetFrame);

/* A system service frame has no exception frame: the nonvolatiles live in the service's own frames below it */
VOID
KiGetTrapContextInternal(
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PCONTEXT Context)
{
    KeTrapFrameToContext(TrapFrame, NULL, Context);
    RtlGetUnwindContext(Context, (ULONG64)TrapFrame - MAX_SYSCALL_PARAM_SIZE);
}

VOID
KiSetTrapContextInternal(
    _Out_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE RequestorMode)
{
    KeContextToTrapFrame(Context, NULL, TrapFrame, Context->ContextFlags, RequestorMode);
    RtlSetUnwindContext(Context, (ULONG64)TrapFrame - MAX_SYSCALL_PARAM_SIZE);
}

VOID
NTAPI
KiInitializeWow64ThreadContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Eip,
    _In_ ULONG Eax,
    _In_ ULONG_PTR CpuArea)
{
    USHORT UserData = KGDT64_R3_DATA | RPL_MASK;

    Context->Rip = Eip;
    Context->Rax = Eax;
    Context->Rsp = (CpuArea - 2 * sizeof(PVOID)) & ~15ULL;
    Context->EFlags = EFLAGS_INTERRUPT_MASK;
    Context->MxCsr = INITIAL_MXCSR;
    Context->SegCs = KGDT64_R3_CODE | RPL_MASK;
    Context->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
    Context->SegDs = UserData;
    Context->SegEs = UserData;
    Context->SegGs = UserData;
    Context->SegSs = UserData;
}
