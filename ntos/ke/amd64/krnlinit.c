/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 processor startup on the boot stack: clock rate, startup thread, kernel phase 0
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern BOOLEAN RtlpUse16ByteSLists;

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeKernel(
    _In_ PKPROCESS InitProcess,
    _In_ PKTHREAD InitThread,
    _In_ PVOID IdleStack,
    _In_ PKPRCB Prcb,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

#define KI_MHZ_SAMPLES 10
#define KI_MHZ_SAMPLE_MICROSECONDS 5000

CODE_SEG("INIT")
static
ULONG
KiSampleTscMhz(VOID)
{
    LARGE_INTEGER Frequency, Start, End;
    ULONG64 TscStart, TscEnd;
    CPU_INFO CpuInfo;

    /* CPUID serializes, so the time stamp reads cannot drift past the counter reads */
    KiCpuId(&CpuInfo, 0);
    Start = KeQueryPerformanceCounter(NULL);
    TscStart = __rdtsc();

    KeStallExecutionProcessor(KI_MHZ_SAMPLE_MICROSECONDS);

    KiCpuId(&CpuInfo, 0);
    End = KeQueryPerformanceCounter(&Frequency);
    TscEnd = __rdtsc();

    return (ULONG)(((TscEnd - TscStart) * Frequency.QuadPart + 500000) /
                   ((End.QuadPart - Start.QuadPart) * 1000000));
}

CODE_SEG("INIT")
VOID
KiCalculateCpuFrequency(
    _Inout_ PKPRCB Prcb)
{
    ULONG Previous, Current, Total, Taken;

    if (!(Prcb->FeatureBits & KF_RDTSC))
        return;

    Previous = KiSampleTscMhz();
    Total = Previous;

    /* Two samples within 1 MHz of each other settle it; otherwise the average of all of them stands */
    for (Taken = 1; Taken < KI_MHZ_SAMPLES; Taken++)
    {
        Current = KiSampleTscMhz();
        if ((Current + 1 >= Previous) && (Current <= Previous + 1))
        {
            Prcb->MHz = Previous;
            return;
        }

        Total += Current;
        Previous = Current;
    }

    Prcb->MHz = Total / KI_MHZ_SAMPLES;
    DPRINT1("Sampling CPU frequency failed. Using average of %lu MHz\n", Prcb->MHz);
}

CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartupBootStack(VOID)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock = KeLoaderBlock;
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread = (PKTHREAD)KeLoaderBlock->Thread;
    PKPROCESS Process = Thread->ApcState.Process;
    PVOID KernelStack = (PVOID)KeLoaderBlock->KernelStack;

    KiAmd64BootMark(BootMarkBootStack);

    Prcb->ParentNode = KeNodeBlock[0];
    if (Prcb->Number < KI_SET_WORD_BITS)
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
        KiInitializeStartupThread(Prcb, Thread, Process, KernelStack, NULL, NULL);

    KiCalculateCpuFrequency(Prcb);
    KiAmd64BootMark(BootMarkCpuFrequency);

    /* Dropping the startup thread to priority 0 lets the phase 1 thread run */
    KiYieldStartupProcessor(Prcb, Thread);

    /* Clearing the mailbox only says the processor exists; it cannot take an IPI yet */
    KfRaiseIrql(HIGH_LEVEL);
    KeMemoryBarrier();
    LoaderBlock->Prcb = 0;

    Thread = KeGetCurrentThread();
    Thread->Priority = 0;

    _enable();
    KeLowerIrql(DISPATCH_LEVEL);

    /* Active means it can answer an IPI, so publish only once interrupts are on */
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
KiInitializeKernel(
    _In_ PKPROCESS InitProcess,
    _In_ PKTHREAD InitThread,
    _In_ PVOID IdleStack,
    _In_ PKPRCB Prcb,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG_PTR PageDirectory[2] = { 0, 0 };
    PVOID DpcStack;

    RtlpUse16ByteSLists = (KeFeatureBits & KF_CMPXCHG16B) ? TRUE : FALSE;
    Prcb->MultiThreadSetMaster = Prcb;

    KiInitSystem();

    KeInitializeProcess(InitProcess, 0, NULL, PageDirectory, FALSE);
    InitProcess->QuantumReset = MAXCHAR;

    KiInitializeStartupThread(Prcb, InitThread, InitProcess, IdleStack, NULL, NULL);

    KiAmd64BootMark(BootMarkExecutiveEntry);
    ExpInitializeExecutive(0, LoaderBlock);
    KiAmd64BootMark(BootMarkExecutiveDone);

    KiTimeIncrementReciprocal = KiComputeReciprocal(KeMaximumIncrement, &KiTimeIncrementShiftCount);

    /* The executive may have changed the DPC tuning while it initialized */
    Prcb->MaximumDpcQueueDepth = KiMaximumDpcQueueDepth;
    Prcb->MinimumDpcRate = KiMinimumDpcRate;
    Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;

    DpcStack = MmCreateKernelStack(FALSE, 0);
    if (!DpcStack) KeBugCheckEx(NO_PAGES_AVAILABLE, 1, 0, 0, 0);
    Prcb->DpcStack = DpcStack;
}
