/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 initial thread frames and the second half of a context switch
 * COPYRIGHT:   Copyright Alex Ionescu <alex@relsoft.net>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct _KI_FXSAVE_HEADER
{
    USHORT ControlWord;
    USHORT StatusWord;
    USHORT TagWord;
    USHORT ErrorOpcode;
    ULONG ErrorOffset;
    ULONG ErrorSelector;
    ULONG DataOffset;
    ULONG DataSelector;
    ULONG MXCsr;
    ULONG MXCsrMask;
} KI_FXSAVE_HEADER, *PKI_FXSAVE_HEADER;

C_ASSERT(sizeof(KI_FXSAVE_HEADER) == FIELD_OFFSET(FXSAVE_FORMAT, RegisterArea));

typedef struct _KSWITCHFRAME
{
    PVOID ExceptionList;
    BOOLEAN ApcBypassDisable;
    PVOID RetAddr;
} KSWITCHFRAME, *PKSWITCHFRAME;

typedef struct _KSTART_FRAME
{
    PKSYSTEM_ROUTINE SystemRoutine;
    PKSTART_ROUTINE StartRoutine;
    PVOID StartContext;
    BOOLEAN UserThread;
} KSTART_FRAME, *PKSTART_FRAME;

typedef struct _KUINIT_FRAME
{
    KSWITCHFRAME CtxSwitchFrame;
    KSTART_FRAME StartFrame;
    KTRAP_FRAME TrapFrame;
    FX_SAVE_AREA FxSaveArea;
} KUINIT_FRAME, *PKUINIT_FRAME;

typedef struct _KKINIT_FRAME
{
    KSWITCHFRAME CtxSwitchFrame;
    KSTART_FRAME StartFrame;
    FX_SAVE_AREA FxSaveArea;
} KKINIT_FRAME, *PKKINIT_FRAME;

VOID
FASTCALL
KiSwitchThreads(
    _In_ PKTHREAD OldThread,
    IN PKTHREAD NewThread
);

VOID
FASTCALL
KiRetireDpcListInDpcStack(
    _In_ PKPRCB Prcb,
    IN PVOID DpcStack
);

VOID
NTAPI
KiThreadStartup(VOID)
{
    PKTRAP_FRAME TrapFrame;
    PKSTART_FRAME StartFrame;
    PKUINIT_FRAME InitFrame;

    InitFrame = KeGetCurrentThread()->KernelStack;
    StartFrame = &InitFrame->StartFrame;
    TrapFrame = &InitFrame->TrapFrame;

    KfLowerIrql(APC_LEVEL);

    StartFrame->SystemRoutine(StartFrame->StartRoutine, StartFrame->StartContext);

    if (!StartFrame->UserThread)
    {
        KeBugCheck(NO_USER_MODE_CONTEXT);
    }

    KiServiceExit2(TrapFrame);
}

VOID
NTAPI
KiInitializeContextThread(
    _In_ PKTHREAD Thread,
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext,
    _In_ PCONTEXT ContextPointer)
{
    PFX_SAVE_AREA FxSaveArea;
    PKI_FXSAVE_HEADER FxSaveFormat;
    PKSTART_FRAME StartFrame;
    PKSWITCHFRAME CtxSwitchFrame;
    PKTRAP_FRAME TrapFrame;
    CONTEXT LocalContext;
    PCONTEXT Context = NULL;
    ULONG ContextFlags;

    if (ContextPointer)
    {
        PKUINIT_FRAME InitFrame;
        InitFrame = (PKUINIT_FRAME)((ULONG_PTR)Thread->InitialStack -
                                    sizeof(KUINIT_FRAME));

        RtlCopyMemory(&LocalContext, ContextPointer, sizeof(CONTEXT));
        Context = &LocalContext;
        ContextFlags = CONTEXT_CONTROL;

        RtlZeroMemory(&InitFrame->TrapFrame,
                      KTRAP_FRAME_LENGTH + sizeof(FX_SAVE_AREA));

        FxSaveArea = &InitFrame->FxSaveArea;

        if (KeI386FxsrPresent)
        {
            FxSaveFormat = (PKI_FXSAVE_HEADER)Context->ExtendedRegisters;

            FxSaveFormat->ControlWord = 0x27F;
            FxSaveFormat->StatusWord = 0;
            FxSaveFormat->TagWord = 0;
            FxSaveFormat->ErrorOffset = 0;
            FxSaveFormat->ErrorSelector = 0;
            FxSaveFormat->DataOffset = 0;
            FxSaveFormat->DataSelector = 0;
            FxSaveFormat->MXCsr = 0x1F80;
        }
        else
        {
            Context->FloatSave.ControlWord = 0x27F;
            Context->FloatSave.StatusWord = 0;
            Context->FloatSave.TagWord = -1;
            Context->FloatSave.ErrorOffset = 0;
            Context->FloatSave.ErrorSelector = 0;
            Context->FloatSave.DataOffset =0;
            Context->FloatSave.DataSelector = 0;
        }

        Context->FloatSave.Cr0NpxState = 0;
        FxSaveArea->Cr0NpxState = 0;
        FxSaveArea->NpxSavedCpu = 0;

        ContextFlags |= (KeI386FxsrPresent) ? CONTEXT_EXTENDED_REGISTERS : CONTEXT_FLOATING_POINT;

        Thread->NpxState = NPX_STATE_NOT_LOADED;

        /* Disable any debug registers */
        Context->ContextFlags &= ~CONTEXT_DEBUG_REGISTERS;

        TrapFrame = &InitFrame->TrapFrame;

        KeContextToTrapFrame(Context,
                             NULL,
                             TrapFrame,
                             Context->ContextFlags | ContextFlags,
                             UserMode);

        TrapFrame->HardwareSegSs |= RPL_MASK;
        TrapFrame->SegDs |= RPL_MASK;
        TrapFrame->SegEs |= RPL_MASK;
        TrapFrame->Dr7 = 0;

        /* Set the debug mark */
        TrapFrame->DbgArgMark = 0xBADB0D00;

        TrapFrame->PreviousPreviousMode = UserMode | KI_EXCEPTION_ACTIVE_FRAME;

        TrapFrame->ExceptionList = EXCEPTION_CHAIN_END;

        StartFrame = &InitFrame->StartFrame;
        CtxSwitchFrame = &InitFrame->CtxSwitchFrame;

        Thread->PreviousMode = UserMode;

        StartFrame->UserThread = TRUE;
    }
    else
    {
        PKKINIT_FRAME InitFrame;
        InitFrame = (PKKINIT_FRAME)((ULONG_PTR)Thread->InitialStack -
                                    sizeof(KKINIT_FRAME));

        FxSaveArea = &InitFrame->FxSaveArea;
        RtlZeroMemory(FxSaveArea, sizeof(FX_SAVE_AREA));

        if (KeI386FxsrPresent)
        {
            FxSaveArea->U.FxArea.ControlWord = 0x27F;
            FxSaveArea->U.FxArea.MXCsr = 0x1F80;
        }
        else
        {
            FxSaveArea->U.FnArea.ControlWord = 0x27F;
            FxSaveArea->U.FnArea.TagWord = -1;
        }

        Thread->NpxState = NPX_STATE_NOT_LOADED;

        StartFrame = &InitFrame->StartFrame;
        CtxSwitchFrame = &InitFrame->CtxSwitchFrame;

        Thread->PreviousMode = KernelMode;

        StartFrame->UserThread = FALSE;
    }

    StartFrame->StartContext = StartContext;
    StartFrame->StartRoutine = StartRoutine;
    StartFrame->SystemRoutine = SystemRoutine;

    CtxSwitchFrame->RetAddr = KiThreadStartup;
    CtxSwitchFrame->ApcBypassDisable = TRUE;
    CtxSwitchFrame->ExceptionList = EXCEPTION_CHAIN_END;

    Thread->KernelStack = (PVOID)CtxSwitchFrame;
}

BOOLEAN
FASTCALL
KiSwapContextExit(
    _In_ PKTHREAD OldThread,
    _In_ PKSWITCHFRAME SwitchFrame)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKPROCESS OldProcess, NewProcess;
    PKTHREAD NewThread;

    NewThread = Pcr->PrcbData.CurrentThread;

    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;
    if (OldProcess != NewProcess)
    {
        if (*(PULONGLONG)&OldProcess->LdtDescriptor != *(PULONGLONG)&NewProcess->LdtDescriptor)
        {
            if (NewProcess->LdtDescriptor.LimitLow)
            {
                KeSetGdtSelector(KGDT_LDT,
                                 ((PULONG)&NewProcess->LdtDescriptor)[0],
                                 ((PULONG)&NewProcess->LdtDescriptor)[1]);
                Ke386SetLocalDescriptorTable(KGDT_LDT);
            }
            else
            {
                Ke386SetLocalDescriptorTable(0);
            }
        }

        __writecr3(KPROCESS_DTB0(NewProcess));
    }

    Ke386SetGs(0);

    KiSetTebBase((PKPCR)Pcr, &NewThread->Teb->NtTib);

    Pcr->TSS->Esp0 = (ULONG_PTR)NewThread->InitialStack;
    if (!((KeGetTrapFrame(NewThread))->EFlags & EFLAGS_V86_MASK))
    {
        Pcr->TSS->Esp0 -= sizeof(KTRAP_FRAME) - FIELD_OFFSET(KTRAP_FRAME, V86Es);
    }
    Pcr->TSS->Esp0 -= NPX_FRAME_LENGTH;
    Pcr->TSS->IoMapBase = NewProcess->IopmOffset;

    NewThread->ContextSwitches++;

    Pcr->NtTib.ExceptionList = SwitchFrame->ExceptionList;

    if (Pcr->PrcbData.DpcRoutineActive)
    {
        KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC,
                     (ULONG_PTR)OldThread,
                     (ULONG_PTR)NewThread,
                     (ULONG_PTR)OldThread->InitialStack,
                     0);
    }

    if (NewThread->ApcState.KernelApcPending)
    {
        if (!NewThread->SpecialApcDisable)
        {
            if (SwitchFrame->ApcBypassDisable)
                HalRequestSoftwareInterrupt(APC_LEVEL);
            else
                return TRUE;
        }
    }

    return FALSE;
}

VOID
FASTCALL
KiSwapContextEntry(
    _In_ PKSWITCHFRAME SwitchFrame,
    _In_ ULONG_PTR OldThreadAndApcFlag)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKTHREAD OldThread, NewThread;
    ULONG Cr0, NewCr0;
    PKTHREAD NpxThread;
    PFX_SAVE_AREA NpxSaveArea;

    SwitchFrame->ApcBypassDisable = OldThreadAndApcFlag & 3;
    SwitchFrame->ExceptionList = Pcr->NtTib.ExceptionList;

    Pcr->ContextSwitches++;

    OldThread = (PKTHREAD)(OldThreadAndApcFlag & ~3);
    NewThread = Pcr->PrcbData.CurrentThread;

    OldThread->KernelStack = SwitchFrame;

    _disable();

    KiChargeThreadCycleTime(&Pcr->PrcbData, OldThread);

    /* Get the current CR0 state */
    Cr0 = __readcr0();

    /*
     * The outgoing thread's NPX state lives in this processor's registers.
     * Save it and drop local ownership so the thread can resume on any processor.
     */
    NpxThread = Pcr->PrcbData.NpxThread;
    if (NpxThread)
    {
        ASSERT(NpxThread == OldThread);

        if (NpxThread->NpxState == NPX_STATE_LOADED)
        {
            __writecr0(Cr0 & ~(CR0_MP | CR0_EM | CR0_TS));

            NpxSaveArea = KiGetThreadNpxArea(NpxThread);
            Ke386SaveFpuState(NpxSaveArea);
            NpxSaveArea->NpxSavedCpu = 0;
            NpxThread->NpxState = NPX_STATE_NOT_LOADED;
        }

        Pcr->PrcbData.NpxThread = NULL;
    }

    /* Set the incoming thread's CR0 state */
    NewCr0 = NewThread->NpxState |
             (Cr0 & ~(CR0_MP | CR0_EM | CR0_TS)) |
             KiGetThreadNpxArea(NewThread)->Cr0NpxState;
    if (Cr0 != NewCr0)  __writecr0(NewCr0);

    _enable();
    KiSwitchThreads(OldThread, NewThread->KernelStack);
}

VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKPRCB Prcb = &Pcr->PrcbData;
    PVOID ExceptionList;

    _disable();

    /* DPCs run on their own stack, outside the SEH chain of the interrupted code */
    if (KiDpcWorkPending(Prcb))
    {
        ExceptionList = Pcr->NtTib.ExceptionList;
        Pcr->NtTib.ExceptionList = EXCEPTION_CHAIN_END;
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
        Pcr->NtTib.ExceptionList = ExceptionList;
    }

    _enable();
    KiFinishDispatchInterrupt(Prcb);
}
