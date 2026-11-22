/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 thread context creation and context switch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kiarm.h"
#define NDEBUG
#include <debug.h>

/*
 * Kernel stack of a thread that has not run yet:
 *
 *   InitialStack -> [ KI_ARM_TRAP_RECORD ]   user threads only
 *                   [ KI_ARM_START_FRAME ]   popped by KiThreadStartup
 *   KernelStack  -> [ KI_ARM_SWITCH_FRAME ]  Lr = KiThreadStartup
 */
typedef struct _KI_ARM_SWITCH_FRAME
{
    ULONGLONG D[8];
    ULONG ApcBypass;
    ULONG R[8];
    ULONG Lr;
} KI_ARM_SWITCH_FRAME, *PKI_ARM_SWITCH_FRAME;

typedef struct _KI_ARM_START_FRAME
{
    ULONG SystemRoutine;
    ULONG StartRoutine;
    ULONG StartContext;
    ULONG Reserved;
} KI_ARM_START_FRAME, *PKI_ARM_START_FRAME;

typedef struct _KI_ARM_INIT_FRAME
{
    KI_ARM_SWITCH_FRAME SwitchFrame;
    KI_ARM_START_FRAME StartFrame;
} KI_ARM_INIT_FRAME, *PKI_ARM_INIT_FRAME;

typedef struct _KI_ARM_SWITCH_CONTEXT
{
    PKTHREAD OldThread;
    PKTHREAD NewThread;
    KIRQL WaitIrql;
} KI_ARM_SWITCH_CONTEXT, *PKI_ARM_SWITCH_CONTEXT;

C_ASSERT(sizeof(KI_ARM_SWITCH_FRAME) == KI_SWITCH_FRAME_SIZE);
C_ASSERT((sizeof(KI_ARM_INIT_FRAME) % 8) == 0);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, R0) == KI_TF_R0);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, R12) == KI_TF_R12);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Sp) == KI_TF_SP);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Pc) == KI_TF_PC);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Cpsr) == KI_TF_CPSR);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, VfpState) == KI_TF_VFP_STATE);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, PreviousMode) == KI_TF_PREVIOUS_MODE);
C_ASSERT(sizeof(KTRAP_FRAME) == KI_TF_SIZE);
C_ASSERT(FIELD_OFFSET(KEXCEPTION_FRAME, R4) == KI_EF_R4);
C_ASSERT(FIELD_OFFSET(KEXCEPTION_FRAME, R11) == KI_EF_R11);
C_ASSERT(sizeof(KEXCEPTION_FRAME) == KI_EF_SIZE);
C_ASSERT(FIELD_OFFSET(KARM_VFP_STATE, Fpscr) == KI_VFP_FPSCR);
C_ASSERT(FIELD_OFFSET(KARM_VFP_STATE, VfpD) == KI_VFP_D);
C_ASSERT(sizeof(KARM_VFP_STATE) == KI_VFP_SIZE);
C_ASSERT(FIELD_OFFSET(KI_ARM_TRAP_RECORD, ExceptionFrame) == KI_REC_EF);
C_ASSERT(FIELD_OFFSET(KI_ARM_TRAP_RECORD, TrapFrame) == KI_REC_TF);
C_ASSERT(sizeof(KI_ARM_TRAP_RECORD) == KI_REC_SIZE);

VOID
NTAPI
KiInitializeContextThread(
    _Inout_ PKTHREAD Thread,
    _In_opt_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_opt_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PCONTEXT Context)
{
    PKI_ARM_TRAP_RECORD Record = NULL;
    PKI_ARM_INIT_FRAME InitFrame;
    ULONG_PTR StackPointer;
    SIZE_T FrameSize;

    Thread->CallbackStack = NULL;

    /* The boot thread already runs on the loader stack and is adopted in place. */
    if ((Thread == KeGetCurrentThread()) && !SystemRoutine && !StartRoutine && !StartContext && !Context)
    {
        __asm__ __volatile__("mov %0, sp" : "=r"(StackPointer));
        ASSERT((StackPointer >= Thread->StackLimit) && (StackPointer < (ULONG_PTR)Thread->StackBase));
        Thread->PreviousMode = KernelMode;
        Thread->KernelStack = (PVOID)StackPointer;
        Thread->TrapFrame = NULL;
        return;
    }

    ASSERT(SystemRoutine != NULL);
    FrameSize = sizeof(KI_ARM_INIT_FRAME) + (Context ? sizeof(KI_ARM_TRAP_RECORD) : 0);
    if (((ULONG_PTR)Thread->InitialStack & 7) ||
        ((ULONG_PTR)Thread->InitialStack - Thread->StackLimit < FrameSize))
    {
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     STATUS_BAD_INITIAL_STACK,
                     (ULONG_PTR)Thread,
                     (ULONG_PTR)Thread->InitialStack,
                     Thread->StackLimit);
    }

    if (Context)
    {
        Record = (PKI_ARM_TRAP_RECORD)Thread->InitialStack - 1;
        RtlZeroMemory(Record, sizeof(*Record));
        Record->TrapFrame.VfpState = &Record->VfpState;
        Record->TrapFrame.PreviousMode = UserMode;
        Record->TrapFrame.Cpsr = CPSR_MODE_USER;
        KeContextToTrapFrame(Context, &Record->ExceptionFrame, &Record->TrapFrame,
                             Context->ContextFlags | CONTEXT_CONTROL | CONTEXT_INTEGER, UserMode);
        InitFrame = (PKI_ARM_INIT_FRAME)Record - 1;
    }
    else
    {
        InitFrame = (PKI_ARM_INIT_FRAME)Thread->InitialStack - 1;
    }

    RtlZeroMemory(InitFrame, sizeof(*InitFrame));
    InitFrame->StartFrame.SystemRoutine = (ULONG)(ULONG_PTR)SystemRoutine;
    InitFrame->StartFrame.StartRoutine = (ULONG)(ULONG_PTR)StartRoutine;
    InitFrame->StartFrame.StartContext = (ULONG)(ULONG_PTR)StartContext;
    InitFrame->SwitchFrame.Lr = (ULONG)(ULONG_PTR)KiThreadStartup;
    InitFrame->SwitchFrame.ApcBypass = APC_LEVEL;

    Thread->PreviousMode = Context ? UserMode : KernelMode;
    Thread->KernelStack = &InitFrame->SwitchFrame;
    Thread->TrapFrame = Record ? &Record->TrapFrame : NULL;
}

/* Runs on the incoming stack once the outgoing stack is published. */
static
BOOLEAN
NTAPI
KiArmSwapContextResume(
    _In_ PKI_ARM_SWITCH_CONTEXT SwitchContext)
{
    KI_ARM_SWITCH_CONTEXT Copy = *SwitchContext;
    PKI_ARM_SWITCH_FRAME Outgoing = Copy.OldThread->KernelStack;
    PKI_ARM_SWITCH_FRAME Incoming = Copy.NewThread->KernelStack;

    ASSERT(Copy.OldThread != Copy.NewThread);
    ASSERT(Copy.OldThread->Running);
    ASSERT(Incoming->ApcBypass <= HIGH_LEVEL);

    /* A thread resumes with the IRQL it was itself switched out at, which its own frame keeps until it runs again. */
    Outgoing->ApcBypass = Copy.WaitIrql;
    return KiSwapContextResume((BOOLEAN)(Incoming->ApcBypass != PASSIVE_LEVEL), Copy.OldThread, Copy.NewThread);
}

BOOLEAN
FASTCALL
KiSwapContext(
    _In_ KIRQL WaitIrql,
    _In_ PKTHREAD CurrentThread)
{
    KI_ARM_SWITCH_CONTEXT SwitchContext;
    PKTHREAD NewThread = KeGetCurrentPrcb()->CurrentThread;
    BOOLEAN Enabled, Result;

    SwitchContext.OldThread = CurrentThread;
    SwitchContext.NewThread = NewThread;
    SwitchContext.WaitIrql = WaitIrql;

    if (NewThread == CurrentThread)
        return KiSwapContextResume(WaitIrql, CurrentThread, NewThread);

    Enabled = KeDisableInterrupts();
    KiArmWriteCp15(13, 0, 0, 2, (ULONG_PTR)NewThread->Teb);
    KiArmWriteCp15(13, 0, 0, 3, (ULONG_PTR)NewThread);
    Result = (BOOLEAN)(ULONG_PTR)KiArmSwapStack(&CurrentThread->KernelStack,
                                                NewThread->KernelStack,
                                                &SwitchContext,
                                                (PVOID)KiArmSwapContextResume);
    KeRestoreInterrupts(Enabled);
    return Result;
}

BOOLEAN
NTAPI
KiSwapContextResume(
    _In_ BOOLEAN ApcBypass,
    _In_ PKTHREAD OldThread,
    _In_ PKTHREAD NewThread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKPROCESS OldProcess, NewProcess;
    BOOLEAN Enabled;

    ASSERT(Prcb->CurrentThread == NewThread);
    if (Prcb->DpcRoutineActive)
    {
        KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC,
                     (ULONG_PTR)OldThread,
                     (ULONG_PTR)NewThread,
                     (ULONG_PTR)OldThread->InitialStack,
                     0);
    }

    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;
    if (OldProcess != NewProcess)
        KiSwapProcess(NewProcess, OldProcess);

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    KiChargeThreadCycleTime(Prcb, OldThread);
#endif
    Prcb->KeContextSwitches++;
    NewThread->ContextSwitches++;
#if KI_THREAD_HANDOFF_USES_RUNNING
    /* A holder of the locks this takes may be held in a rendezvous that waits for this processor. */
    Enabled = KeDisableInterrupts();
    _enable();
    KiCompleteThreadSwitch(OldThread, NewThread);
    KeRestoreInterrupts(Enabled);
#endif

    if (NewThread->ApcState.KernelApcPending)
    {
        if ((NewThread->SpecialApcDisable == 0) && !ApcBypass)
            return TRUE;
        HalRequestSoftwareInterrupt(APC_LEVEL);
    }
    return FALSE;
}

/* Every process root carries the shared kernel mappings; only TTBR0 changes. */
VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    KAFFINITY Member = KeGetCurrentPrcb()->SetMember;

    InterlockedOr((PLONG)&NewProcess->ActiveProcessors, (LONG)Member);
    ASSERT(NewProcess->DirectoryTableBase != 0);
    KiArmDsbIsh();
    KiArmWriteCp15_64(0, 2, (ULONGLONG)NewProcess->DirectoryTableBase);
    KiArmIsb();
    KeFlushCurrentTb();
    InterlockedAnd((PLONG)&OldProcess->ActiveProcessors, ~(LONG)Member);
}

VOID
NTAPI
KiRundownThread(
    _In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
}

/* Return to user mode through a trap frame after delivering pending user APCs. */
VOID
NTAPI
KiArmExitToUserMode(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(KiIsUserModeTrap(TrapFrame));
    for (;;)
    {
        _disable();
        if (!Thread->ApcState.UserApcPending)
            break;
        KfRaiseIrql(APC_LEVEL);
        _enable();
        KiDeliverApc(UserMode, &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame, TrapFrame);
        KfLowerIrql(PASSIVE_LEVEL);
    }
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiArmStartUserThread(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME TrapFrame = Thread->TrapFrame;

    if (!TrapFrame || !KiIsUserModeTrap(TrapFrame))
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_INVALID_PARAMETER, (ULONG_PTR)Thread, 0, 0);

    KiArmExitToUserMode(TrapFrame);
    KiArmRestoreTrapFrame(TrapFrame);
}

PKTRAP_FRAME
NTAPI
KeGetTrapFrame(
    _In_ PKTHREAD Thread)
{
    return Thread->TrapFrame;
}

PKEXCEPTION_FRAME
NTAPI
KeGetExceptionFrame(
    _In_ PKTHREAD Thread)
{
    return Thread->TrapFrame ? &KI_ARM_TRAP_RECORD(Thread->TrapFrame)->ExceptionFrame : NULL;
}
