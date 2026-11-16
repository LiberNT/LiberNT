/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 user exception frames and conversion between CONTEXT records and trap frames
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Gregor Anich
 *              Copyright Skywing <skywing@valhallalegends.com>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
VOID
NTAPI
KeInitExceptions(VOID)
{
    ULONG i;
    USHORT FlippedSelector;

    for (i = 0; i <= MAXIMUM_IDTVECTOR; i++)
    {
        FlippedSelector = KiIdt[i].Selector;

        KiIdt[i].Selector = KiIdt[i].ExtendedOffset;
        KiIdt[i].ExtendedOffset = FlippedSelector;
    }
}

ULONG
FASTCALL
KiUpdateDr7(
    _In_ ULONG Dr7)
{
    /* Check if debugging is enabled */
    if (KeGetCurrentThread()->Header.ActiveDR7 &&
        ((Dr7 & ~DR7_RESERVED_MASK) == DR7_OVERRIDE_MASK))
    {
        return 0;
    }

    return Dr7;
}

BOOLEAN
FASTCALL
KiRecordDr7(
    _Out_ PULONG Dr7Ptr,
    _Out_ PULONG DrMask)
{
    ULONG NewMask, Mask;
    UCHAR Result;

    if (!DrMask)
    {
        Mask = KeGetCurrentThread()->Header.ActiveDR7 ? DR_REG_MASK : 0;
    }
    else
    {
        Mask = *DrMask;
    }

    ASSERT((*Dr7Ptr & DR7_RESERVED_MASK) == 0);

    NewMask = Mask;
    if (!(*Dr7Ptr))
    {
        Result = FALSE;

        NewMask &= ~(DR_MASK(7));
        if (NewMask & DR_REG_MASK)
        {
            NewMask |= DR_MASK(DR7_OVERRIDE_V);

            *Dr7Ptr |= DR7_OVERRIDE_MASK;
        }
        else
        {
            ASSERT(NewMask == 0);
        }
    }
    else
    {
        Result = NewMask ? TRUE: FALSE;

        /* Update the mask to disable debugging */
        NewMask &= ~(DR_MASK(DR7_OVERRIDE_V));
        NewMask |= DR_MASK(7);
    }

    if (DrMask)
    {
        *DrMask = NewMask;
    }
    else
    {
        if (Mask != NewMask)
        {
            KeGetCurrentThread()->Header.ActiveDR7 = (NewMask != 0);
        }
    }

    return Result;
}

ULONG
NTAPI
KiEspFromTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    if (KiUserTrap(TrapFrame) ||
        (TrapFrame->EFlags & EFLAGS_V86_MASK))
    {
        return TrapFrame->HardwareEsp;
    }
    else
    {
        if (!(TrapFrame->SegCs & FRAME_EDITED))
        {
            return TrapFrame->TempEsp;
        }
        else
        {
            return (ULONG)&TrapFrame->HardwareEsp;
        }
    }
}

VOID
NTAPI
KiEspToTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Esp)
{
    KIRQL OldIrql;
    ULONG Previous;

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < APC_LEVEL) KeRaiseIrql(APC_LEVEL, &OldIrql);

    Previous = KiEspFromTrapFrame(TrapFrame);

    if (KiUserTrap(TrapFrame) ||
        (TrapFrame->EFlags & EFLAGS_V86_MASK))
    {
        TrapFrame->HardwareEsp = Esp;
    }
    else
    {
        if (Esp < Previous) KeBugCheckEx(SET_OF_INVALID_CONTEXT,
                                         Esp,
                                         Previous,
                                         (ULONG_PTR)TrapFrame,
                                         0);

        if (!(TrapFrame->SegCs & FRAME_EDITED))
        {
            TrapFrame->TempEsp = Esp;
        }
        else
        {
            if (Previous != Esp)
            {
                TrapFrame->TempSegCs = TrapFrame->SegCs;
                TrapFrame->SegCs &= ~FRAME_EDITED;

                TrapFrame->TempEsp = Esp;
            }
        }
    }

    if (OldIrql < APC_LEVEL) KeLowerIrql(OldIrql);
}

ULONG
NTAPI
KiSsFromTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    if (TrapFrame->EFlags & EFLAGS_V86_MASK)
    {
        return TrapFrame->HardwareSegSs;
    }
    else if (KiUserTrap(TrapFrame))
    {
        return TrapFrame->HardwareSegSs | RPL_MASK;
    }
    else
    {
        return KGDT_R0_DATA;
    }
}

VOID
NTAPI
KiSsToTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Ss)
{
    Ss &= 0xFFFF;

    if (TrapFrame->EFlags & EFLAGS_V86_MASK)
    {
        TrapFrame->HardwareSegSs = Ss;
    }
    else if (KiUserTrap(TrapFrame))
    {
        TrapFrame->HardwareSegSs = Ss | RPL_MASK;
    }
}

USHORT
NTAPI
KiTagWordFnsaveToFxsave(
    USHORT TagWord)
{
    INT FxTagWord = ~TagWord;

    FxTagWord = (FxTagWord | (FxTagWord >> 1)) & 0x5555;
    FxTagWord = (FxTagWord | (FxTagWord >> 1)) & 0x3333;
    FxTagWord = (FxTagWord | (FxTagWord >> 2)) & 0x0f0f;
    FxTagWord = (FxTagWord | (FxTagWord >> 4)) & 0x00ff;
    return FxTagWord;
}

VOID
NTAPI
Ki386AdjustEsp0(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    ULONG_PTR Stack;
    ULONG EFlags;

    Thread = KeGetCurrentThread();
    Stack = (ULONG_PTR)Thread->InitialStack;

    if (!(TrapFrame->EFlags & EFLAGS_V86_MASK))
    {
        Stack -= sizeof(KTRAP_FRAME) -
                 FIELD_OFFSET(KTRAP_FRAME, V86Es);
    }

    Stack -= sizeof(FX_SAVE_AREA);

    EFlags = __readeflags();
    _disable();

    KeGetPcr()->TSS->Esp0 = Stack;

    __writeeflags(EFlags);
}

VOID
NTAPI
KeContextToTrapFrame(
    _In_ PCONTEXT Context,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PFX_SAVE_AREA FxSaveArea;
    ULONG i;
    BOOLEAN V86Switch = FALSE;
    KIRQL OldIrql;
    ULONG DrMask = 0;

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < APC_LEVEL) KeRaiseIrql(APC_LEVEL, &OldIrql);

    if ((ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL)
    {
        if ((Context->EFlags & EFLAGS_V86_MASK) !=
            (TrapFrame->EFlags & EFLAGS_V86_MASK))
        {
            V86Switch = TRUE;
        }

        TrapFrame->EFlags = Ke386SanitizeFlags(Context->EFlags, PreviousMode);

        TrapFrame->Ebp = Context->Ebp;
        TrapFrame->Eip = Context->Eip;

        if (TrapFrame->EFlags & EFLAGS_V86_MASK)
        {
            TrapFrame->SegCs = Context->SegCs;
        }
        else
        {
            TrapFrame->SegCs = Ke386SanitizeSeg(Context->SegCs, PreviousMode);

            if ((PreviousMode != KernelMode) && (TrapFrame->SegCs < 8))
            {
                TrapFrame->SegCs = KGDT_R3_CODE | RPL_MASK;
            }
        }

        KiSsToTrapFrame(TrapFrame, Context->SegSs);

        KiEspToTrapFrame(TrapFrame, Context->Esp);

        if (V86Switch) Ki386AdjustEsp0(TrapFrame);
    }

    if ((ContextFlags & CONTEXT_INTEGER) == CONTEXT_INTEGER)
    {
        TrapFrame->Eax = Context->Eax;
        TrapFrame->Ebx = Context->Ebx;
        TrapFrame->Ecx = Context->Ecx;
        TrapFrame->Edx = Context->Edx;
        TrapFrame->Esi = Context->Esi;
        TrapFrame->Edi = Context->Edi;
    }

    if ((ContextFlags & CONTEXT_SEGMENTS) == CONTEXT_SEGMENTS)
    {
        if (TrapFrame->EFlags & EFLAGS_V86_MASK)
        {
            TrapFrame->V86Ds = Context->SegDs;
            TrapFrame->V86Es = Context->SegEs;
            TrapFrame->V86Fs = Context->SegFs;
            TrapFrame->V86Gs = Context->SegGs;
        }
        else if (!KiUserTrap(TrapFrame))
        {
            TrapFrame->SegDs = KGDT_R3_DATA | RPL_MASK;
            TrapFrame->SegEs = KGDT_R3_DATA | RPL_MASK;
            TrapFrame->SegFs = Ke386SanitizeSeg(Context->SegFs, PreviousMode);
            TrapFrame->SegGs = 0;
        }
        else
        {
            TrapFrame->SegDs = Context->SegDs;
            TrapFrame->SegEs = Context->SegEs;
            TrapFrame->SegFs = Context->SegFs;

            if (TrapFrame->SegCs == (KGDT_R3_CODE | RPL_MASK))
            {
                TrapFrame->SegGs = 0;
            }
            else
            {
                TrapFrame->SegGs = Context->SegGs;
            }
        }
    }

    if (((ContextFlags & CONTEXT_EXTENDED_REGISTERS) ==
        CONTEXT_EXTENDED_REGISTERS) && KiUserTrap(TrapFrame))
    {
        FxSaveArea = (PFX_SAVE_AREA)(TrapFrame + 1);

        KiFlushNPXState(NULL);

        RtlCopyMemory(&FxSaveArea->U.FxArea,
                      &Context->ExtendedRegisters[0],
                      MAXIMUM_SUPPORTED_EXTENSION);

        FxSaveArea->U.FxArea.MXCsr &= KiMXCsrMask;

        FxSaveArea->Cr0NpxState &= ~(CR0_EM | CR0_MP | CR0_TS);

        if (PsGetCurrentProcess()->VdmObjects)
        {
            FxSaveArea->Cr0NpxState |= Context->FloatSave.Cr0NpxState & (CR0_EM | CR0_MP);
        }
    }

    if (((ContextFlags & CONTEXT_FLOATING_POINT) ==
        CONTEXT_FLOATING_POINT) && KiUserTrap(TrapFrame))
    {
        FxSaveArea = (PFX_SAVE_AREA)(TrapFrame + 1);

        KiFlushNPXState(NULL);

        if (KeI386FxsrPresent)
        {
            FxSaveArea->U.FxArea.ControlWord = (USHORT)Context->FloatSave.ControlWord;
            FxSaveArea->U.FxArea.StatusWord = (USHORT)Context->FloatSave.StatusWord;
            FxSaveArea->U.FxArea.TagWord =
                KiTagWordFnsaveToFxsave((USHORT)Context->FloatSave.TagWord);
            FxSaveArea->U.FxArea.ErrorOpcode =
                (USHORT)((Context->FloatSave.ErrorSelector >> 16) & 0xFFFF);
            FxSaveArea->U.FxArea.ErrorOffset = Context->FloatSave.ErrorOffset;
            FxSaveArea->U.FxArea.ErrorSelector = Context->FloatSave.ErrorSelector & 0xFFFF;
            FxSaveArea->U.FxArea.DataOffset = Context->FloatSave.DataOffset;
            FxSaveArea->U.FxArea.DataSelector = Context->FloatSave.DataSelector;

            RtlZeroMemory(&FxSaveArea->U.FxArea.RegisterArea[0], SIZE_OF_FX_REGISTERS);

            for (i = 0; i < 8; i++)
            {
                RtlCopyMemory(FxSaveArea->U.FxArea.RegisterArea + (i * 16),
                              Context->FloatSave.RegisterArea + (i * 10),
                              10);
            }
        }
        else
        {
            FxSaveArea->U.FnArea.ControlWord = Context->FloatSave.ControlWord;
            FxSaveArea->U.FnArea.StatusWord = Context->FloatSave.StatusWord;
            FxSaveArea->U.FnArea.TagWord = Context->FloatSave.TagWord;
            FxSaveArea->U.FnArea.ErrorOffset = Context->FloatSave.ErrorOffset;
            FxSaveArea->U.FnArea.ErrorSelector = Context->FloatSave.ErrorSelector;
            FxSaveArea->U.FnArea.DataOffset = Context->FloatSave.DataOffset;
            FxSaveArea->U.FnArea.DataSelector = Context->FloatSave.DataSelector;

            for (i = 0; i < SIZE_OF_80387_REGISTERS; i++)
            {
                FxSaveArea->U.FnArea.RegisterArea[i] = Context->FloatSave.RegisterArea[i];
            }
        }

        FxSaveArea->Cr0NpxState &= ~(CR0_EM | CR0_MP | CR0_TS);

        if (PsGetCurrentProcess()->VdmObjects)
        {
            FxSaveArea->Cr0NpxState |= Context->FloatSave.Cr0NpxState & (CR0_EM | CR0_MP);
        }
    }

    /* Handle the Debug Registers */
    if ((ContextFlags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
    {
        TrapFrame->Dr0 = Context->Dr0;
        TrapFrame->Dr1 = Context->Dr1;
        TrapFrame->Dr2 = Context->Dr2;
        TrapFrame->Dr3 = Context->Dr3;

        if (PreviousMode != KernelMode)
        {
            if (Context->Dr0 > (ULONG)MmHighestUserAddress) TrapFrame->Dr0 = 0;
            if (Context->Dr1 > (ULONG)MmHighestUserAddress) TrapFrame->Dr1 = 0;
            if (Context->Dr2 > (ULONG)MmHighestUserAddress) TrapFrame->Dr2 = 0;
            if (Context->Dr3 > (ULONG)MmHighestUserAddress) TrapFrame->Dr3 = 0;
        }

        TrapFrame->Dr6 = Context->Dr6 & DR6_LEGAL;

        if (TrapFrame->Dr0) DrMask |= DR_MASK(0);
        if (TrapFrame->Dr1) DrMask |= DR_MASK(1);
        if (TrapFrame->Dr2) DrMask |= DR_MASK(2);
        if (TrapFrame->Dr3) DrMask |= DR_MASK(3);
        if (TrapFrame->Dr6) DrMask |= DR_MASK(6);

        TrapFrame->Dr7 = Context->Dr7 & DR7_LEGAL;
        KiRecordDr7(&TrapFrame->Dr7, &DrMask);

        if (PreviousMode != KernelMode)
        {
            KeGetCurrentThread()->Header.ActiveDR7 = (DrMask != 0);
        }
    }

    if (KeGetCurrentThread()->Iopl) TrapFrame->EFlags |= EFLAGS_IOPL;

    if (OldIrql < APC_LEVEL) KeLowerIrql(OldIrql);
}

VOID
NTAPI
KeTrapFrameToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PCONTEXT Context)
{
    PFX_SAVE_AREA FxSaveArea;
    struct _AlignHack
    {
        UCHAR Hack[15];
        FLOATING_SAVE_AREA UnalignedArea;
    } FloatSaveBuffer;
    FLOATING_SAVE_AREA *FloatSaveArea;
    KIRQL OldIrql;
    ULONG i;

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < APC_LEVEL) KeRaiseIrql(APC_LEVEL, &OldIrql);

    if ((Context->ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL)
    {
        Context->Ebp = TrapFrame->Ebp;
        Context->Eip = TrapFrame->Eip;
        Context->EFlags = TrapFrame->EFlags;

        if (!(TrapFrame->SegCs & FRAME_EDITED) &&
            !(TrapFrame->EFlags & EFLAGS_V86_MASK))
        {
            Context->SegCs = TrapFrame->TempSegCs & 0xFFFF;
        }
        else
        {
            Context->SegCs = TrapFrame->SegCs & 0xFFFF;
        }

        Context->SegSs = KiSsFromTrapFrame(TrapFrame);
        Context->Esp = KiEspFromTrapFrame(TrapFrame);
    }

    if ((Context->ContextFlags & CONTEXT_SEGMENTS) == CONTEXT_SEGMENTS)
    {
        if (TrapFrame->EFlags & EFLAGS_V86_MASK)
        {
            Context->SegGs = TrapFrame->V86Gs & 0xFFFF;
            Context->SegFs = TrapFrame->V86Fs & 0xFFFF;
            Context->SegEs = TrapFrame->V86Es & 0xFFFF;
            Context->SegDs = TrapFrame->V86Ds & 0xFFFF;
        }
        else
        {
            if (TrapFrame->SegCs == KGDT_R0_CODE)
            {
                TrapFrame->SegGs = 0;
                TrapFrame->SegFs = KGDT_R0_PCR;
                TrapFrame->SegEs = KGDT_R3_DATA | RPL_MASK;
                TrapFrame->SegDs = KGDT_R3_DATA | RPL_MASK;
            }

            Context->SegGs = TrapFrame->SegGs & 0xFFFF;
            Context->SegFs = TrapFrame->SegFs & 0xFFFF;
            Context->SegEs = TrapFrame->SegEs & 0xFFFF;
            Context->SegDs = TrapFrame->SegDs & 0xFFFF;
        }
    }

    if ((Context->ContextFlags & CONTEXT_INTEGER) == CONTEXT_INTEGER)
    {
        Context->Eax = TrapFrame->Eax;
        Context->Ebx = TrapFrame->Ebx;
        Context->Ecx = TrapFrame->Ecx;
        Context->Edx = TrapFrame->Edx;
        Context->Esi = TrapFrame->Esi;
        Context->Edi = TrapFrame->Edi;
    }

    if (((Context->ContextFlags & CONTEXT_EXTENDED_REGISTERS) ==
        CONTEXT_EXTENDED_REGISTERS) && KiUserTrap(TrapFrame))
    {
        FxSaveArea = (PFX_SAVE_AREA)(TrapFrame + 1);

        KiFlushNPXState(NULL);

        RtlCopyMemory(&Context->ExtendedRegisters[0],
                      &FxSaveArea->U.FxArea,
                      MAXIMUM_SUPPORTED_EXTENSION);
    }

    if (((Context->ContextFlags & CONTEXT_FLOATING_POINT) ==
        CONTEXT_FLOATING_POINT) && KiUserTrap(TrapFrame))
    {
        FxSaveArea = (PFX_SAVE_AREA)(TrapFrame + 1);

        if (KeI386FxsrPresent)
        {
            FloatSaveArea = (FLOATING_SAVE_AREA*)((ULONG_PTR)&FloatSaveBuffer.UnalignedArea &~ 0xF);

            KiFlushNPXState(FloatSaveArea);
        }
        else
        {
            FloatSaveArea = (FLOATING_SAVE_AREA*)&FxSaveArea->U.FnArea;
            KiFlushNPXState(NULL);
        }

        Context->FloatSave.ControlWord = FloatSaveArea->ControlWord;
        Context->FloatSave.StatusWord = FloatSaveArea->StatusWord;
        Context->FloatSave.TagWord = FloatSaveArea->TagWord;
        Context->FloatSave.ErrorOffset = FloatSaveArea->ErrorOffset;
        Context->FloatSave.ErrorSelector = FloatSaveArea->ErrorSelector;
        Context->FloatSave.DataOffset = FloatSaveArea->DataOffset;
        Context->FloatSave.DataSelector = FloatSaveArea->DataSelector;
        Context->FloatSave.Cr0NpxState = FxSaveArea->Cr0NpxState;

        for (i = 0; i < SIZE_OF_80387_REGISTERS; i++)
        {
            Context->FloatSave.RegisterArea[i] = FloatSaveArea->RegisterArea[i];
        }
    }

    /* Handle debug registers */
    if ((Context->ContextFlags & CONTEXT_DEBUG_REGISTERS) ==
        CONTEXT_DEBUG_REGISTERS)
    {
        if (TrapFrame->Dr7 & ~DR7_RESERVED_MASK)
        {
            /* Copy the debug registers */
            Context->Dr0 = TrapFrame->Dr0;
            Context->Dr1 = TrapFrame->Dr1;
            Context->Dr2 = TrapFrame->Dr2;
            Context->Dr3 = TrapFrame->Dr3;
            Context->Dr6 = TrapFrame->Dr6;

            Context->Dr7 = KiUpdateDr7(TrapFrame->Dr7);
        }
        else
        {
            Context->Dr0 =
            Context->Dr1 =
            Context->Dr2 =
            Context->Dr3 =
            Context->Dr6 =
            Context->Dr7 = 0;
        }
    }

    if (OldIrql < APC_LEVEL) KeLowerIrql(OldIrql);
}

BOOLEAN
FASTCALL
KeInvalidAccessAllowed(
    _In_opt_ PVOID TrapInformation)
{
    ULONG Eip;
    PKTRAP_FRAME TrapFrame = TrapInformation;
    VOID NTAPI ExpInterlockedPopEntrySListFault(VOID);

    if (!TrapInformation) return FALSE;

    switch (TrapFrame->SegCs)
    {
        case KGDT_R0_CODE:

            Eip = (ULONG)&ExpInterlockedPopEntrySListFault;
            break;

        case KGDT_R3_CODE | RPL_MASK:

            Eip = 0;
            break;

        default:

            /* Anything else gets a bugcheck */
            Eip = 0;
    }

    return (TrapFrame->Eip == Eip) ? TRUE : FALSE;
}

ULONG
KiI386ExceptionContextFlags(
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Flags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;

    if ((PreviousMode == UserMode) || (KeGetPcr()->KdVersionBlock != NULL))
    {
        Flags |= CONTEXT_FLOATING_POINT;
        if (KeI386FxsrPresent)
            Flags |= CONTEXT_EXTENDED_REGISTERS;
    }

    return Flags;
}

/* KiUserExceptionDispatcher starts with the record and context addresses on top of its stack */
static
VOID
KiPushUserExceptionFrame(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    const ULONG ContextSize = ALIGN_UP_BY(sizeof(CONTEXT) + sizeof(CONTEXT_EX), sizeof(ULONG));
    ULONG_PTR ContextAt, RecordAt, ArgumentsAt;
    PCONTEXT_EX ContextEx;
    ULONG RecordSize;

    ASSERT(ExceptionRecord->NumberParameters <= EXCEPTION_MAXIMUM_PARAMETERS);

    if ((TrapFrame->HardwareSegSs != (KGDT_R3_DATA | RPL_MASK)) || (TrapFrame->EFlags & EFLAGS_V86_MASK))
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);

    ContextAt = ALIGN_DOWN_BY(Context->Esp, sizeof(ULONG)) - ContextSize;
    ProbeForWrite((PVOID)ContextAt, ContextSize, sizeof(ULONG));
    RtlCopyMemory((PVOID)ContextAt, Context, sizeof(CONTEXT));

    ContextEx = (PCONTEXT_EX)(ContextAt + sizeof(CONTEXT));
    RtlZeroMemory(ContextEx, sizeof(*ContextEx));
    ContextEx->All.Offset = -(LONG)sizeof(CONTEXT);
    ContextEx->All.Length = sizeof(CONTEXT) + sizeof(CONTEXT_EX);
    ContextEx->Legacy.Offset = -(LONG)sizeof(CONTEXT);
    ContextEx->Legacy.Length = sizeof(CONTEXT);
    ContextEx->XState.Offset = sizeof(CONTEXT_EX);

    RecordSize = ALIGN_UP_BY(FIELD_OFFSET(EXCEPTION_RECORD, ExceptionInformation[ExceptionRecord->NumberParameters]), sizeof(ULONG));
    RecordAt = ContextAt - RecordSize;
    ArgumentsAt = RecordAt - 2 * sizeof(ULONG_PTR);
    ProbeForWrite((PVOID)ArgumentsAt, RecordSize + 2 * sizeof(ULONG_PTR), sizeof(ULONG));
    RtlCopyMemory((PVOID)RecordAt, ExceptionRecord, RecordSize);
    ((PULONG_PTR)ArgumentsAt)[0] = RecordAt;
    ((PULONG_PTR)ArgumentsAt)[1] = ContextAt;

    KiSsToTrapFrame(TrapFrame, KGDT_R3_DATA);
    KiEspToTrapFrame(TrapFrame, ArgumentsAt);
    TrapFrame->SegCs = Ke386SanitizeSeg(KGDT_R3_CODE, UserMode);
    TrapFrame->SegDs = Ke386SanitizeSeg(KGDT_R3_DATA, UserMode);
    TrapFrame->SegEs = Ke386SanitizeSeg(KGDT_R3_DATA, UserMode);
    TrapFrame->SegFs = Ke386SanitizeSeg(KGDT_R3_TEB, UserMode);
    TrapFrame->SegGs = 0;
    TrapFrame->Eip = (ULONG)KeUserExceptionDispatcher;
}

BOOLEAN
KiDispatchExceptionToUser(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    EXCEPTION_RECORD Fault;
    ULONG Attempt;

    /* A user stack that runs out is reported once, as a stack overflow at the address that faulted */
    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        _SEH2_TRY
        {
            KiPushUserExceptionFrame(TrapFrame, Context, ExceptionRecord);
            _SEH2_YIELD(return TRUE);
        }
        _SEH2_EXCEPT((RtlCopyMemory(&Fault, _SEH2_GetExceptionInformation()->ExceptionRecord, sizeof(Fault)), EXCEPTION_EXECUTE_HANDLER))
        {
        }
        _SEH2_END;

        if ((NTSTATUS)Fault.ExceptionCode != STATUS_STACK_OVERFLOW)
            break;

        Fault.ExceptionAddress = ExceptionRecord->ExceptionAddress;
        RtlCopyMemory(ExceptionRecord, &Fault, sizeof(*ExceptionRecord));
    }

    return FALSE;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiDispatchExceptionFromTrapFrame(
    _In_ NTSTATUS Code,
    _In_ ULONG Flags,
    _In_ ULONG_PTR Address,
    _In_ ULONG ParameterCount,
    _In_ ULONG_PTR Parameter1,
    _In_ ULONG_PTR Parameter2,
    _In_ ULONG_PTR Parameter3,
    _In_ PKTRAP_FRAME TrapFrame)
{
    EXCEPTION_RECORD ExceptionRecord;

    ExceptionRecord.ExceptionCode = Code;
    ExceptionRecord.ExceptionFlags = Flags;
    ExceptionRecord.ExceptionRecord = NULL;
    ExceptionRecord.ExceptionAddress = (PVOID)Address;
    ExceptionRecord.NumberParameters = ParameterCount;
    if (ParameterCount)
    {
        ExceptionRecord.ExceptionInformation[0] = Parameter1;
        ExceptionRecord.ExceptionInformation[1] = Parameter2;
        ExceptionRecord.ExceptionInformation[2] = Parameter3;
    }

    KiDispatchException(&ExceptionRecord,
                        NULL,
                        TrapFrame,
                        TrapFrame->EFlags & EFLAGS_V86_MASK ?
                        -1 : KiUserTrap(TrapFrame),
                        TRUE);

    KiEoiHelper(TrapFrame);
}

DECLSPEC_NORETURN
VOID
FASTCALL
KiSystemFatalException(
    _In_ ULONG ExceptionCode,
    _In_ PKTRAP_FRAME TrapFrame)
{
    /* Bugcheck the system */
    KeBugCheckWithTf(UNEXPECTED_KERNEL_MODE_TRAP,
                     ExceptionCode,
                     0,
                     0,
                     0,
                     TrapFrame);
}

NTSTATUS
NTAPI
KeRaiseUserException(
    _In_ NTSTATUS ExceptionCode)
{
    ULONG OldEip;
    PTEB Teb = KeGetCurrentThread()->Teb;
    PKTRAP_FRAME TrapFrame = KeGetCurrentThread()->TrapFrame;

    _SEH2_TRY
    {
        Teb->ExceptionCode = ExceptionCode;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    OldEip = TrapFrame->Eip;

    TrapFrame->Eip = (ULONG_PTR)KeRaiseUserExceptionDispatcher;

    return (NTSTATUS)OldEip;
}
