/*
 */

#include <ntoskrnl.h>



VOID
NTAPI
{
    KIRQL OldIrql;

    ASSERT(ContextFlags & CONTEXT_AMD64);
    ContextFlags &= ~CONTEXT_AMD64;


    if (ContextFlags & CONTEXT_INTEGER)
    {
        if (ExceptionFrame)
    }

    if (ContextFlags & CONTEXT_FLOATING_POINT)
    {
            KiRestoreUserLegacyFloatingState(&Context->FltSave);
        TrapFrame->MxCsr = Context->MxCsr & KiMxCsrMask;
        if (ExceptionFrame)
    }

    if (ContextFlags & CONTEXT_CONTROL)

    if (ContextFlags & CONTEXT_SEGMENTS)
    {
        TrapFrame->SegDs = KGDT64_R3_DATA | RPL_MASK;
        TrapFrame->SegEs = KGDT64_R3_DATA | RPL_MASK;
        TrapFrame->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
        TrapFrame->SegGs = KGDT64_R3_DATA | RPL_MASK;
    }

    if (ContextFlags & CONTEXT_DEBUG_REGISTERS)

}

VOID
NTAPI
{
    ULONG ContextFlags;
    KIRQL OldIrql;


    ContextFlags = Context->ContextFlags;
    ASSERT(ContextFlags & CONTEXT_AMD64);
    ContextFlags &= ~CONTEXT_AMD64;

    if (ContextFlags & CONTEXT_INTEGER)
    {
        if (ExceptionFrame)
    }

    if (ContextFlags & CONTEXT_FLOATING_POINT)
    {
            KiCaptureUserLegacyFloatingState(&Context->FltSave);
        Context->FltSave.MxCsr = TrapFrame->MxCsr;
        Context->MxCsr = TrapFrame->MxCsr;
        if (ExceptionFrame)
    }

    if (ContextFlags & CONTEXT_CONTROL)

    if (ContextFlags & CONTEXT_SEGMENTS)
    {
        Context->SegDs = KGDT64_R3_DATA | RPL_MASK;
        Context->SegEs = KGDT64_R3_DATA | RPL_MASK;
        Context->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
        Context->SegGs = KGDT64_R3_DATA | RPL_MASK;
    }

    if (ContextFlags & CONTEXT_DEBUG_REGISTERS)

}

VOID
RtlGetUnwindContext(
    _Out_ PCONTEXT Context,
    _In_ DWORD64 TargetFrame);

VOID
KiGetTrapContextInternal(
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PCONTEXT Context)
{
    KeTrapFrameToContext(TrapFrame, NULL, Context);
}

VOID
KiSetTrapContextInternal(
    _Out_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE RequestorMode)
{
}

VOID
NTAPI
KiInitializeWow64ThreadContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Eip,
    _In_ ULONG Eax,
    _In_ ULONG_PTR CpuArea)
{
    Context->Rip = Eip;
    Context->Rax = Eax;
    Context->Rsp = (CpuArea - 2 * sizeof(PVOID)) & ~15ULL;
    Context->EFlags = EFLAGS_INTERRUPT_MASK;
    Context->SegCs = KGDT64_R3_CODE | RPL_MASK;
    Context->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
}
