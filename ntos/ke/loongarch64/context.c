/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 base context and private trap-frame conversion
 */

#include <ntoskrnl.h>

/* $r0 is hardwired zero and $r21 is the kernel's KPCR base, so neither is
 * part of a restorable context: a user caller cannot own either. */
static
VOID
KiLa64CopyContext(
    _Inout_ PCONTEXT Destination,
    _In_ const CONTEXT *Source,
    _In_ ULONG ContextFlags)
{
    ULONG Index;

    ASSERT((ContextFlags & CONTEXT_LOONGARCH64) != 0);
    ASSERT((ContextFlags & ~(CONTEXT_ALL | CONTEXT_UNWOUND_TO_CALL)) == 0);
    ContextFlags &= ~CONTEXT_LOONGARCH64;

    if (ContextFlags & (CONTEXT_CONTROL & ~CONTEXT_LOONGARCH64))
    {
        Destination->Pc = Source->Pc;
        Destination->Ra = Source->Ra;
        Destination->Sp = Source->Sp;
    }
    if (ContextFlags & (CONTEXT_INTEGER & ~CONTEXT_LOONGARCH64))
    {
        Destination->Zero = 0;
        for (Index = 1; Index < RTL_NUMBER_OF(Source->R); Index++)
        {
            if (Index == 21)
                continue;
            Destination->R[Index] = Source->R[Index];
        }
    }
    if (ContextFlags & (CONTEXT_FLOATING_POINT & ~CONTEXT_LOONGARCH64))
    {
        RtlCopyMemory(Destination->F, Source->F, sizeof(Source->F));
        Destination->Fcsr = Source->Fcsr;
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
    BOOLEAN Interrupts;

    UNREFERENCED_PARAMETER(ExceptionFrame);

    /* The flags can come from the caller's CONTEXT: apply only this
     * architecture's register sets. */
    ContextFlags &= CONTEXT_ALL | CONTEXT_UNWOUND_TO_CALL;
    if (!(ContextFlags & CONTEXT_LOONGARCH64))
        return;

    Interrupts = KeDisableInterrupts();
    /* A public CONTEXT contains registers, never a user-supplied PRMD. The
     * privilege boundary must survive a kernel caller editing an existing
     * user frame: PRMD is what ERTN turns into CRMD.PLV. */
    if ((PreviousMode == UserMode) || KiLa64UserTrap(TrapFrame))
        TrapFrame->Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
    KiLa64CopyContext(&TrapFrame->Context, Context, ContextFlags);
    TrapFrame->Context.ContextFlags |= ContextFlags;
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KeTrapFrameToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PCONTEXT Context)
{
    BOOLEAN Interrupts;
    ULONG Flags;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    Interrupts = KeDisableInterrupts();
    Flags = Context->ContextFlags;
    if (!(TrapFrame->Context.ContextFlags & (CONTEXT_FLOATING_POINT & ~CONTEXT_LOONGARCH64)))
        Flags &= ~(CONTEXT_FLOATING_POINT & ~CONTEXT_LOONGARCH64);
    KiLa64CopyContext(Context, &TrapFrame->Context, Flags & (CONTEXT_ALL | CONTEXT_UNWOUND_TO_CALL));
    Context->ContextFlags = Flags;
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KiInitializeWow64ThreadContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Eip,
    _In_ ULONG Eax,
    _In_ ULONG_PTR CpuArea)
{
    UNREFERENCED_PARAMETER(Eax);

    Context->Pc = Eip;
    Context->Sp = CpuArea & ~(ULONG_PTR)15;
}
