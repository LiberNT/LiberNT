/*
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern BOOLEAN RtlpUse16ByteSLists;

CODE_SEG("INIT")
VOID
NTAPI


CODE_SEG("INIT")
VOID
KiCalculateCpuFrequency(
{

        {
        }

    }
}

VOID
NTAPI
KiInitializeHandBuiltThread(
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    KeInitializeThread(Process, Thread, NULL, NULL, NULL, NULL, NULL, Stack);

    Thread->NextProcessor = Prcb->Number;
    Thread->IdealProcessor = Prcb->Number;
    Thread->Priority = HIGH_PRIORITY;
    Thread->State = Running;
    Thread->Running = TRUE;
    Thread->Affinity = (ULONG_PTR)1 << Prcb->Number;
    Thread->WaitIrql = DISPATCH_LEVEL;
    Process->ActiveProcessors |= (ULONG_PTR)1 << Prcb->Number;
}

CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartupBootStack(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread = (PKTHREAD)KeLoaderBlock->Thread;
    PKPROCESS Process = Thread->ApcState.Process;
    PVOID KernelStack = (PVOID)KeLoaderBlock->KernelStack;

    KiAmd64BootMark(BootMarkBootStack);

    Prcb->ParentNode = KeNodeBlock[0];
    Prcb->ParentNode->ProcessorMask |= Prcb->SetMember;

    PoInitializePrcb(Prcb);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    KiGetCacheInformation();
    KiInitSpinLocks(Prcb, Prcb->Number);

    Prcb->CurrentThread = Thread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = Thread;

    ExInitPoolLookasidePointers();
    KiAmd64BootMark(BootMarkPrcbLocks);

    KeLowerIrql(APC_LEVEL);
    KiAmd64BootMark(BootMarkApcLevel);

    if (Prcb->Number == 0)
        KiInitializeKernel(Process, Thread, KernelStack, Prcb, LoaderBlock);
    else
        KiInitializeHandBuiltThread(Thread, Process, KernelStack);

    KiCalculateCpuFrequency(Prcb);
    KiAmd64BootMark(BootMarkCpuFrequency);

    KfRaiseIrql(DISPATCH_LEVEL);

    KeSetPriorityThread(Thread, 0);

    KiAcquirePrcbLock(Prcb);
    if (!Prcb->NextThread) KiIdleSummary |= (ULONG_PTR)1 << Prcb->Number;
    KiReleasePrcbLock(Prcb);

    KfRaiseIrql(HIGH_LEVEL);
    KeMemoryBarrier();
    LoaderBlock->Prcb = 0;

    Thread = KeGetCurrentThread();
    Thread->Priority = 0;

    _enable();
    KeLowerIrql(DISPATCH_LEVEL);

    if (Prcb->Number != 0)
        KiPublishActiveProcessor(Prcb);

    Thread->WaitIrql = DISPATCH_LEVEL;

    KiAmd64BootMark(BootMarkIdleLoop);
    KiAmd64BootMarkStop();

    KiIdleLoop();
}

CODE_SEG("INIT")
VOID
NTAPI
{
    PVOID DpcStack;

    RtlpUse16ByteSLists = (KeFeatureBits & KF_CMPXCHG16B) ? TRUE : FALSE;
    Prcb->MultiThreadSetMaster = Prcb;


    InitProcess->QuantumReset = MAXCHAR;

    KiInitializeHandBuiltThread(InitThread, InitProcess, IdleStack);

    KiAmd64BootMark(BootMarkExecutiveEntry);
    ExpInitializeExecutive(0, LoaderBlock);
    KiAmd64BootMark(BootMarkExecutiveDone);


    Prcb->MaximumDpcQueueDepth = KiMaximumDpcQueueDepth;
    Prcb->MinimumDpcRate = KiMinimumDpcRate;
    Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;

    DpcStack = MmCreateKernelStack(FALSE, 0);
    if (!DpcStack) KeBugCheckEx(NO_PAGES_AVAILABLE, 1, 0, 0, 0);
    Prcb->DpcStack = DpcStack;
}
