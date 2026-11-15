/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 initial thread frames and the second half of a context switch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

extern void KiInvalidSystemThreadStartupExit(void);
extern void KiUserThreadStartupExit(void);
extern void KiServiceExit3(void);

/* Layouts ctxswitch.S walks: the switch frame returns into KiThreadStartup, which pops the start frame */
typedef struct _KUINIT_FRAME
{
    KSWITCH_FRAME CtxSwitchFrame;
    KSTART_FRAME StartFrame;
    KEXCEPTION_FRAME ExceptionFrame;
    KTRAP_FRAME TrapFrame;
} KUINIT_FRAME, *PKUINIT_FRAME;

typedef struct _KKINIT_FRAME
{
    KSWITCH_FRAME CtxSwitchFrame;
    KSTART_FRAME StartFrame;
} KKINIT_FRAME, *PKKINIT_FRAME;

#define XSAVE_COMPACTED_FORMAT 0x8000000000000000ULL

static
VOID
KiReserveExtendedState(
    _Inout_ PKTHREAD Thread,
    _In_ SIZE_T FramesSize)
{
    PUCHAR Top = (PUCHAR)Thread->InitialStack;
    PXSAVE_FORMAT Area;

    ASSERT(Top != NULL);
    ASSERT(KeXStateLength >= sizeof(XSAVE_FORMAT));
    ASSERT((ULONG_PTR)Top >= Thread->StackLimit);
    ASSERT(KeXStateLength <= (ULONG_PTR)Top - Thread->StackLimit);

    Area = (PXSAVE_FORMAT)ALIGN_DOWN_POINTER_BY(Top - KeXStateLength, 64);
    ASSERT((ULONG_PTR)Area >= Thread->StackLimit);
    ASSERT((ULONG_PTR)Area - Thread->StackLimit >= FramesSize);

    Thread->InitialStack = Area;
    Thread->StateSaveArea = Area;

    RtlZeroMemory(Area, KeXStateLength);
    Area->MxCsr = INITIAL_MXCSR;
    Area->ControlWord = INITIAL_FPCSR;

    if (KeFeatureBits & KF_XSTATE)
    {
        PXSAVE_AREA XSave = (PXSAVE_AREA)Area;

        XSave->Header.Mask |= XSTATE_MASK_LEGACY_FLOATING_POINT;

        /* XRSTORS faults unless XCOMP_BV marks the area compacted */
        if (KeFeatureBits & KF_XSAVES)
            XSave->Header.CompactionMask |= XSAVE_COMPACTED_FORMAT | XSTATE_MASK_LEGACY_FLOATING_POINT;
    }
}

static
PKSTART_FRAME
KiBuildUserStartFrame(
    _Inout_ PKTHREAD Thread,
    _In_ PCONTEXT Context,
    _Out_ PKSWITCH_FRAME *SwitchFrame)
{
    PKUINIT_FRAME Frame = (PKUINIT_FRAME)Thread->InitialStack - 1;
    PKTRAP_FRAME TrapFrame = &Frame->TrapFrame;
    USHORT UserData = KGDT64_R3_DATA | RPL_MASK;
    ULONG Flags;

    ASSERT((Context->ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL);

    Thread->KernelStack = Frame;
    Thread->PreviousMode = UserMode;
    Thread->NpxState = SharedUserData->XState.EnabledFeatures | XSTATE_MASK_LEGACY;

    RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));
    RtlZeroMemory(&Frame->ExceptionFrame, sizeof(Frame->ExceptionFrame));

    /* A new thread starts with no debug registers and with its floating point state from the area above */
    Flags = Context->ContextFlags & ~(CONTEXT_DEBUG_REGISTERS | CONTEXT_FLOATING_POINT);
    KeContextToTrapFrame(Context, &Frame->ExceptionFrame, TrapFrame, CONTEXT_AMD64 | Flags, UserMode);

    TrapFrame->SegDs = UserData;
    TrapFrame->SegEs = UserData;
    TrapFrame->SegGs = UserData;
    TrapFrame->SegSs = UserData;
    TrapFrame->SegFs = KGDT64_R3_CMTEB | RPL_MASK;

    if (Context->SegCs == (KGDT64_R3_CMCODE | RPL_MASK))
    {
        TrapFrame->SegCs = KGDT64_R3_CMCODE | RPL_MASK;
    }
    else
    {
        /* A 64-bit thread starts as if called: home space for four parameters plus a return slot */
        TrapFrame->SegCs = KGDT64_R3_CODE | RPL_MASK;
        TrapFrame->Rsp -= 5 * sizeof(PVOID);
    }

    TrapFrame->Dr7 = 0;
    TrapFrame->MxCsr = INITIAL_MXCSR;
    TrapFrame->PreviousMode = UserMode;
    TrapFrame->ExceptionActive = KEXCEPTION_ACTIVE_EXCEPTION_FRAME;
    TrapFrame->ExceptionFrame = 0;

    /* KiThreadStartup returns through KiUserThreadStartupExit into KiServiceExit3 */
    Frame->StartFrame.Return = (ULONG64)KiUserThreadStartupExit;
    Frame->ExceptionFrame.Return = (ULONG64)KiServiceExit3;

    *SwitchFrame = &Frame->CtxSwitchFrame;
    return &Frame->StartFrame;
}

static
PKSTART_FRAME
KiBuildSystemStartFrame(
    _Inout_ PKTHREAD Thread,
    _Out_ PKSWITCH_FRAME *SwitchFrame)
{
    PKKINIT_FRAME Frame = (PKKINIT_FRAME)Thread->InitialStack - 1;

    Thread->KernelStack = Frame;
    Thread->PreviousMode = KernelMode;
    Thread->NpxState = 0;

    /* A system thread must never return from its start routine */
    Frame->StartFrame.Return = (ULONG64)KiInvalidSystemThreadStartupExit;

    *SwitchFrame = &Frame->CtxSwitchFrame;
    return &Frame->StartFrame;
}

VOID
NTAPI
KiInitializeContextThread(
    _Inout_ PKTHREAD Thread,
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PCONTEXT Context)
{
    PKSWITCH_FRAME SwitchFrame;
    PKSTART_FRAME StartFrame;

    KiReserveExtendedState(Thread, Context ? sizeof(KUINIT_FRAME) : sizeof(KKINIT_FRAME));

    if (Context)
        StartFrame = KiBuildUserStartFrame(Thread, Context, &SwitchFrame);
    else
        StartFrame = KiBuildSystemStartFrame(Thread, &SwitchFrame);

    SwitchFrame->Return = (ULONG64)KiThreadStartup;
    SwitchFrame->ApcBypass = TRUE;

    StartFrame->P1Home = (ULONG64)StartRoutine;
    StartFrame->P2Home = (ULONG64)StartContext;
    StartFrame->P3Home = 0;
    StartFrame->P4Home = (ULONG64)SystemRoutine;
    StartFrame->Reserved = 0;
}

static
VOID
KiEnsureKernelStackSegment(VOID)
{
    USHORT StackSegment;

#if defined(__GNUC__)
    __asm__ __volatile__("mov %%ss, %0" : "=r"(StackSegment));
#else
    StackSegment = __readss();
#endif

    if (StackSegment == KGDT64_R0_DATA)
        return;

#if defined(__GNUC__)
    __asm__ __volatile__("mov %0, %%ss" :: "r"((USHORT)KGDT64_R0_DATA) : "memory");
#else
    __writess(KGDT64_R0_DATA);
#endif
}

static
VOID
KiLoadThreadUserSegments(
    _In_ PKIPCR Pcr,
    _In_ PKTHREAD NewThread)
{
    PETHREAD Thread = (PETHREAD)NewThread;
    PVOID CompatTeb = NULL;

    /* A WoW64 thread's 32-bit TEB follows its native TEB */
    if (NewThread->Teb && THREAD_TO_PROCESS(Thread)->WoW64Process)
        CompatTeb = (PUCHAR)NewThread->Teb + ROUND_TO_PAGES(sizeof(TEB));

    Pcr->NtTib.Self = (PVOID)NewThread->Teb;

    /* A minimal (pico) thread brings its own GS and FS bases instead of a TEB */
    if (NewThread->Header.Minimal)
        __writemsr(MSR_GS_SWAP, Thread->UserGsBase);
    else if (NewThread->Teb)
        __writemsr(MSR_GS_SWAP, (ULONG64)NewThread->Teb);

    KiSetGdtDescriptorBase(KiGetGdtEntry(Pcr->GdtBase, KGDT64_R3_CMTEB), (ULONG64)CompatTeb);

    if (NewThread->Teb)
    {
#if defined(__GNUC__)
        __asm__ __volatile__("mov %0, %%ax\n\tmov %%ax, %%fs" :: "i"(KGDT64_R3_CMTEB | RPL_MASK) : "ax", "memory");
#else
        __writefs(KGDT64_R3_CMTEB | RPL_MASK);
#endif
    }

    if (NewThread->Header.Minimal)
        __writemsr(MSR_FS_BASE, Thread->UserFsBase);
}

BOOLEAN
KiSwapContextResume(
    _In_ BOOLEAN ApcBypass,
    _In_ PKTHREAD OldThread,
    _In_ PKTHREAD NewThread)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKPROCESS OldProcess = OldThread->ApcState.Process;
    PKPROCESS NewProcess = NewThread->ApcState.Process;

    KiEnsureKernelStackSegment();

    Pcr->TssBase->Rsp0 = (ULONG64)NewThread->InitialStack;
    Pcr->Prcb.RspBase = Pcr->TssBase->Rsp0;

    if (OldThread->NpxState != 0)
        KiSaveXState(OldThread->StateSaveArea, OldThread->NpxState);
    if (NewThread->NpxState != 0)
        KiRestoreXState(NewThread->StateSaveArea, NewThread->NpxState);

    if (OldProcess != NewProcess)
        KiSwapProcess(NewProcess, OldProcess);

    KiLoadThreadUserSegments(Pcr, NewThread);

    Pcr->Prcb.KeContextSwitches++;
    NewThread->ContextSwitches++;

    if (Pcr->Prcb.DpcRoutineActive)
    {
        KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC,
                     (ULONG_PTR)OldThread,
                     (ULONG_PTR)NewThread,
                     (ULONG_PTR)OldThread->InitialStack,
                     0);
    }

    /* Only now, off the outgoing thread's stack, can its handoff complete */
    KiCompleteThreadSwitch(OldThread, NewThread);

    KiCheckDeferredReadyList(&Pcr->Prcb);
    if (Pcr->Prcb.NextThread)
        KiRequestDispatchInterrupt(&Pcr->Prcb);

    if (!NewThread->ApcState.KernelApcPending)
        return FALSE;

    /* A pending kernel APC runs now unless special APCs are off or the switch asked to bypass it */
    if ((NewThread->SpecialApcDisable == 0) && (ApcBypass == 0))
        return TRUE;

    HalRequestSoftwareInterrupt(APC_LEVEL);
    return FALSE;
}
