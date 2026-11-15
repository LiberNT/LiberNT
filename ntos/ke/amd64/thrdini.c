/*
 */

#include <ntoskrnl.h>

extern void KiInvalidSystemThreadStartupExit(void);
extern void KiUserThreadStartupExit(void);
extern void KiServiceExit3(void);

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


VOID
{

    ASSERT(KeXStateLength >= sizeof(XSAVE_FORMAT));
    if (KeFeatureBits & KF_XSTATE)
    {

        if (KeFeatureBits & KF_XSAVES)
    }






    }
    else
    {







    StartFrame->P1Home = (ULONG64)StartRoutine;
    StartFrame->P2Home = (ULONG64)StartContext;
    StartFrame->P3Home = 0;
    StartFrame->P4Home = (ULONG64)SystemRoutine;
    StartFrame->Reserved = 0;
}

{
    USHORT StackSegment;

#if defined(__GNUC__)
    __asm__ __volatile__("mov %%ss, %0" : "=r"(StackSegment));
#else
    StackSegment = __readss();
#endif
#if defined(__GNUC__)
#else
#endif



    Pcr->NtTib.Self = (PVOID)NewThread->Teb;
    if (NewThread->Header.Minimal)
    else if (NewThread->Teb)
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

    KiCompleteThreadSwitch(OldThread, NewThread);

    KiCheckDeferredReadyList(&Pcr->Prcb);
    if (Pcr->Prcb.NextThread)
        KiRequestDispatchInterrupt(&Pcr->Prcb);


    return FALSE;
}
