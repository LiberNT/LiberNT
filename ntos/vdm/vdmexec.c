/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Virtual DOS machine execution and context swaps
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

ULONG VdmBopCount;

NTSTATUS
NTAPI
VdmpGetVdmTib(
    _Out_ PVDM_TIB *VdmTib)
{
    PVDM_TIB Tib;
    PAGED_CODE();

    *VdmTib = NULL;

    Tib = NtCurrentTeb()->Vdm;
    if (!Tib) return STATUS_INVALID_SYSTEM_SERVICE;

    if (Tib->Size != sizeof(VDM_TIB)) return STATUS_INVALID_SYSTEM_SERVICE;

    *VdmTib = Tib;
    return STATUS_SUCCESS;
}

VOID
NTAPI
VdmSwapContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT OutContext,
    _In_ PCONTEXT InContext)
{
    ULONG EFlags, OldEFlags;

    ASSERT(KeGetCurrentIrql() == APC_LEVEL);
    ASSERT(TrapFrame->DbgArgMark == 0xBADB0D00);

    if (TrapFrame->EFlags & EFLAGS_V86_MASK)
    {
        OutContext->SegGs = TrapFrame->V86Gs;
        OutContext->SegFs = TrapFrame->V86Fs;
        OutContext->SegEs = TrapFrame->V86Es;
        OutContext->SegDs = TrapFrame->V86Ds;
    }
    else if (TrapFrame->SegCs != (KGDT_R3_CODE | RPL_MASK))
    {
        OutContext->SegGs = TrapFrame->SegGs;
        OutContext->SegFs = TrapFrame->SegFs;
        OutContext->SegEs = TrapFrame->SegEs;
        OutContext->SegDs = TrapFrame->SegDs;
    }

    OutContext->SegCs = TrapFrame->SegCs;
    OutContext->SegSs = TrapFrame->HardwareSegSs;

    OutContext->Eax = TrapFrame->Eax;
    OutContext->Ebx = TrapFrame->Ebx;
    OutContext->Ecx = TrapFrame->Ecx;
    OutContext->Edx = TrapFrame->Edx;
    OutContext->Esi = TrapFrame->Esi;
    OutContext->Edi = TrapFrame->Edi;

    OutContext->Ebp = TrapFrame->Ebp;
    OutContext->Esp = TrapFrame->HardwareEsp;
    OutContext->Eip = TrapFrame->Eip;

    OutContext->EFlags = TrapFrame->EFlags;

    TrapFrame->SegCs = InContext->SegCs;
    TrapFrame->HardwareSegSs = InContext->SegSs;

    TrapFrame->Eax = InContext->Eax;
    TrapFrame->Ebx = InContext->Ebx;
    TrapFrame->Ecx = InContext->Ecx;
    TrapFrame->Edx = InContext->Edx;
    TrapFrame->Esi = InContext->Esi;
    TrapFrame->Edi = InContext->Edi;

    TrapFrame->Ebp = InContext->Ebp;
    TrapFrame->HardwareEsp = InContext->Esp;
    TrapFrame->Eip = InContext->Eip;

    EFlags = InContext->EFlags;
    if (EFlags & EFLAGS_V86_MASK)
    {
        EFlags &= KeI386EFlagsAndMaskV86;
        EFlags |= KeI386EFlagsOrMaskV86;
    }
    else
    {
        TrapFrame->SegCs |= RPL_MASK;
        TrapFrame->HardwareSegSs |= RPL_MASK;

        if (TrapFrame->SegCs < KGDT_R0_CODE)
        {
            TrapFrame->SegCs = KGDT_R3_CODE | RPL_MASK;
        }

        EFlags &= EFLAGS_USER_SANITIZE;
        EFlags |=EFLAGS_INTERRUPT_MASK;
    }

    OldEFlags = TrapFrame->EFlags;
    TrapFrame->EFlags = EFlags;

    if ((OldEFlags ^ EFlags) & EFLAGS_V86_MASK)
    {
        Ki386AdjustEsp0(TrapFrame);
    }

    if (InContext->EFlags & EFLAGS_V86_MASK)
    {
        TrapFrame->V86Gs = InContext->SegGs;
        TrapFrame->V86Fs = InContext->SegFs;
        TrapFrame->V86Es = InContext->SegEs;
        TrapFrame->V86Ds = InContext->SegDs;
    }
    else
    {
        TrapFrame->SegGs = InContext->SegGs;
        TrapFrame->SegFs = InContext->SegFs;
        TrapFrame->SegEs = InContext->SegEs;
        TrapFrame->SegDs = InContext->SegDs;
    }

    TrapFrame->ExceptionList = EXCEPTION_CHAIN_END;
}

NTSTATUS
NTAPI
VdmpStartExecution(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PKTRAP_FRAME VdmFrame;
    NTSTATUS Status;
    PVDM_TIB VdmTib;
    BOOLEAN Interrupts;
    KIRQL OldIrql;
    CONTEXT VdmContext;
    PAGED_CODE();

    VdmFrame = (PVOID)((ULONG_PTR)Thread->Tcb.InitialStack -
                                  sizeof(FX_SAVE_AREA) -
                                  sizeof(KTRAP_FRAME));
    Status = VdmpGetVdmTib(&VdmTib);
    if (!NT_SUCCESS(Status)) return STATUS_INVALID_SYSTEM_SERVICE;

    KeRaiseIrql(APC_LEVEL, &OldIrql);

    Interrupts = (BOOLEAN)(VdmTib->VdmContext.EFlags & EFLAGS_INTERRUPT_MASK);

    ASSERT(*KiNtVdmState == 0);
    ASSERT(VdmTib->VdmContext.EFlags & EFLAGS_V86_MASK);

    if ((KeI386VirtualIntExtensions) &&
        (VdmTib->VdmContext.EFlags & EFLAGS_V86_MASK))
    {
        if (Interrupts)
        {
            VdmTib->VdmContext.EFlags |= EFLAGS_VIF;
        }
        else
        {
            VdmTib->VdmContext.EFlags &= ~EFLAGS_VIF;
            VdmTib->VdmContext.EFlags |= EFLAGS_INTERRUPT_MASK;
        }
    }
    else
    {
        if (VdmTib->VdmContext.EFlags & EFLAGS_INTERRUPT_MASK)
        {
            InterlockedOr((PLONG)KiNtVdmState, EFLAGS_INTERRUPT_MASK);
        }
        else
        {
            InterlockedAnd((PLONG)KiNtVdmState, ~EFLAGS_INTERRUPT_MASK);
        }

        VdmTib->VdmContext.EFlags |= EFLAGS_INTERRUPT_MASK;
    }

    VdmContext = VdmTib->VdmContext;
    if (!(VdmContext.SegCs & FRAME_EDITED))
    {
        KeLowerIrql(OldIrql);
        return STATUS_INVALID_SYSTEM_SERVICE;
    }

    VdmSwapContext(VdmFrame, &VdmTib->MonitorContext, &VdmContext);

    KeLowerIrql(OldIrql);
    return VdmFrame->Eax;
}

VOID
NTAPI
VdmEndExecution(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PVDM_TIB VdmTib)
{
    KIRQL OldIrql;
    CONTEXT Context;
    PAGED_CODE();

    ASSERT((TrapFrame->EFlags & EFLAGS_V86_MASK) ||
           (TrapFrame->SegCs != (KGDT_R3_CODE | RPL_MASK)));

    KeRaiseIrql(APC_LEVEL, &OldIrql);

    VdmTib->MonitorContext.Eax = STATUS_SUCCESS;

    Context = VdmTib->MonitorContext;

    if ((Context.EFlags & EFLAGS_V86_MASK) || (Context.SegCs & FRAME_EDITED))
    {
        VdmSwapContext(TrapFrame, &VdmTib->VdmContext, &Context);

        if ((KeI386VirtualIntExtensions) &&
            (VdmTib->VdmContext.EFlags & EFLAGS_V86_MASK))
        {
            if (VdmTib->VdmContext.EFlags & EFLAGS_VIF)
            {
                VdmTib->VdmContext.EFlags |= EFLAGS_INTERRUPT_MASK;
            }
            else
            {
                VdmTib->VdmContext.EFlags &= ~EFLAGS_INTERRUPT_MASK;
            }

            TrapFrame->EFlags &= ~(EFLAGS_VIP | EFLAGS_VIF);
            VdmTib->VdmContext.EFlags &= ~(EFLAGS_VIP | EFLAGS_VIF);
        }
        else
        {
            VdmTib->VdmContext.EFlags = (VdmTib->VdmContext.EFlags & ~EFLAGS_INTERRUPT_MASK) |
                                        (*KiNtVdmState & EFLAGS_INTERRUPT_MASK);
        }
    }

    KeLowerIrql(OldIrql);
}

BOOLEAN
NTAPI
VdmDispatchBop(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PUCHAR Eip;
    PVDM_TIB VdmTib;

    if (TrapFrame->EFlags & EFLAGS_V86_MASK)
    {
        Eip = (PUCHAR)((TrapFrame->Eip & 0xFFFF) +
                      ((TrapFrame->SegCs & 0xFFFF) << 4));

        if (*(PUSHORT)Eip == 0xC4C4)
        {
            if (Eip[2] == 0x50)
            {
                /* FIXME: No VDM Support */
                ASSERT(FALSE);
            }

            VdmBopCount++;

            VdmTib = NtCurrentTeb()->Vdm;

            VdmTib->EventInfo.InstructionSize = 3;
            VdmTib->EventInfo.BopNumber = Eip[2];
            VdmTib->EventInfo.Event = VdmBop;

            VdmEndExecution(TrapFrame, VdmTib);
        }
        else
        {
            return FALSE;
        }
    }
    else
    {
        /* FIXME: Shouldn't happen on ROS */
        ASSERT(FALSE);
    }

    return TRUE;
}

BOOLEAN
NTAPI
VdmDispatchPageFault(
    _In_ PKTRAP_FRAME TrapFrame)
{
    NTSTATUS Status;
    PVDM_TIB VdmTib;

    PAGED_CODE();

    Status = VdmpGetVdmTib(&VdmTib);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("VdmDispatchPageFault: no VDM TIB, Vdm=%p\n", NtCurrentTeb()->Vdm);
        return FALSE;
    }

    ASSERT(TrapFrame->EFlags & EFLAGS_V86_MASK);

    _SEH2_TRY
    {
        VdmTib->EventInfo.Event = VdmMemAccess;
        VdmTib->EventInfo.InstructionSize = 0;

        VdmEndExecution(TrapFrame, VdmTib);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DPRINT1("VdmDispatchPageFault EFlags %lx exit with 0x%lx\n", TrapFrame->EFlags, Status);
    return NT_SUCCESS(Status);
}
