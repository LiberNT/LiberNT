/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 boot processor startup from the loader contract
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

PCONTEXT KiProcessorContext[MAXIMUM_PROCESSORS];

static
DECLSPEC_NORETURN
VOID
KiArmStartupStop(
    _In_ PCSTR Reason)
{
    static const CHAR Prefix[] = "\nKiArmSystemStartup: ";
    static const CHAR Suffix[] = ". Halting.\n";

    _disable();
    if (KiArmConsoleReady())
    {
        KiArmConsoleWrite(Prefix, sizeof(Prefix) - 1);
        KiArmConsoleWrite(Reason, strlen(Reason));
        KiArmConsoleWrite(Suffix, sizeof(Suffix) - 1);
    }
    for (;;)
        __wfi();
}

VOID
NTAPI
KiArmInitializePcr(
    _Out_ PKIPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ ULONG Number,
    _In_ PVOID DpcStack,
    _In_ PVOID PanicStack)
{
    PKPRCB Prcb = &Pcr->Prcb;

    RtlZeroMemory(Pcr, sizeof(*Pcr));
    Pcr->Self = (PKPCR)Pcr;
    Pcr->CurrentPrcb = Prcb;
    Pcr->CurrentIrql = HIGH_LEVEL;
    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;
    Pcr->NtTib.Self = &Pcr->NtTib;
    Pcr->NtTib.ExceptionList = EXCEPTION_CHAIN_END;
    Pcr->NtTib.StackBase = NULL;
    Pcr->NtTib.StackLimit = NULL;

    Prcb->LegacyNumber = (UCHAR)Number;
    Prcb->Number = Number;
    Prcb->CurrentThread = Thread;
    Prcb->IdleThread = Thread;
    Prcb->DpcStack = DpcStack;
    Prcb->PanicStackBase = (ULONG)(ULONG_PTR)PanicStack;
    Prcb->MajorVersion = PRCB_MAJOR_VERSION;
    Prcb->Context = &Prcb->ProcessorState.ContextFrame;
    KiPrcbContext(Prcb) = &Prcb->ProcessorState.ContextFrame;
#ifndef CONFIG_SMP
    Prcb->BuildType |= PRCB_BUILD_UNIPROCESSOR;
#endif
#if DBG
    Prcb->BuildType |= PRCB_BUILD_DEBUG;
#endif
    Prcb->SetMember = AFFINITY_MASK(Number);
    Prcb->GroupSetMember = (ULONG)AFFINITY_MASK(Number);
    Prcb->GroupIndex = (UCHAR)Number;
    Prcb->ParentNode = &KiNode0;
    Prcb->MultiThreadProcessorSet = (ULONG)Prcb->SetMember;
    Prcb->CoresPerPhysicalProcessor = 1;
    Prcb->LogicalProcessorsPerCore = 1;
    Prcb->CoreProcessorSet = (ULONG)Prcb->SetMember;
    KiInitSpinLocks(Prcb, Number);

    Pcr->KdVersionBlock = Number ? KeGetPcr()->KdVersionBlock : NULL;
}

static
VOID
KiArmValidateLoaderBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARM_LOADER_BLOCK ArmBlock = &LoaderBlock->u.Arm;

    if (((ULONG_PTR)LoaderBlock < ARM_LOADER_KSEG0_BASE) ||
        (ArmBlock->Version != ARM_LOADER_BLOCK_VERSION) ||
        (ArmBlock->Size != sizeof(*ArmBlock)))
    {
        KiArmStartupStop("loader block version mismatch");
    }

    if ((ArmBlock->PcrPage & (PAGE_SIZE - 1)) ||
        (LoaderBlock->Prcb != ArmBlock->PcrPage + FIELD_OFFSET(KIPCR, Prcb)) ||
        (LoaderBlock->Thread & 7) || (LoaderBlock->Process & 7) ||
        (LoaderBlock->KernelStack & 7) || (ArmBlock->DpcStack & 7))
    {
        KiArmStartupStop("loader block layout mismatch");
    }

    if ((ArmBlock->PageTableRoot & 0x1F) || (KiReadDirectoryTableBase() != (ArmBlock->PageTableRoot & ~0xFFFULL)))
        KiArmStartupStop("page table root mismatch");
}

DECLSPEC_NORETURN
VOID
NTAPI
KiArmSystemStartup(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARM_LOADER_BLOCK ArmBlock = &LoaderBlock->u.Arm;
    ULONG_PTR DirectoryTableBase[2];
    PKPROCESS InitialProcess;
    PKTHREAD InitialThread;
    PKPRCB Prcb;
    PKIPCR Pcr;

    KiArmConsoleInitialize(LoaderBlock);
    KiArmValidateLoaderBlock(LoaderBlock);

    Pcr = (PKIPCR)(ULONG_PTR)ArmBlock->PcrPage;
    LoaderBlock->Process = (ULONG_PTR)&KiInitialProcess.Pcb;
    InitialProcess = (PKPROCESS)(ULONG_PTR)LoaderBlock->Process;
    InitialThread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;
    InitialThread->ApcState.Process = InitialProcess;

    KiArmInitializePcr(Pcr, InitialThread, 0,
                       (PVOID)(ULONG_PTR)ArmBlock->DpcStack,
                       (PVOID)(ULONG_PTR)ArmBlock->PanicStack);
    KiArmWriteCp15(13, 0, 0, 4, (ULONG_PTR)Pcr);
    KiArmIsb();
    KeLoaderBlock = LoaderBlock;
    KeMemoryBarrier();

    KiArmInitializeVectors();

    Prcb = KeGetCurrentPrcb();
    KiProcessorBlock[0] = Prcb;
    KiArmIdentifyProcessor(Prcb);
    KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM;
    KeProcessorLevel = (USHORT)Prcb->ProcessorModel;
    KeProcessorRevision = Prcb->ProcessorRevision;
    KeFeatureBits = Prcb->FeatureBits;
    KeSetDmaIoCoherency(0);

    KiNode0.ProcessorMask |= Prcb->SetMember;
    PoInitializePrcb(Prcb);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    ExInitPoolLookasidePointers();

    KfLowerIrql(APC_LEVEL);
    MiInitializeKernelVaLayout(LoaderBlock);
    KiInitSystem();

    DirectoryTableBase[0] = (ULONG_PTR)ArmBlock->PageTableRoot;
    DirectoryTableBase[1] = 0;
    KeInitializeProcess(InitialProcess, 0, NULL, DirectoryTableBase, FALSE);
    InitialProcess->QuantumReset = MAXCHAR;

    KeInitializeThread(InitialProcess,
                       InitialThread,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       (PVOID)(ULONG_PTR)LoaderBlock->KernelStack);
    InitialThread->NextProcessor = 0;
    InitialThread->IdealProcessor = 0;
    InitialThread->Priority = HIGH_PRIORITY;
    InitialThread->State = Running;
#if (NTDDI_VERSION >= NTDDI_WIN7)
    InitialThread->Running = TRUE;
#endif
    RtlZeroMemory(&InitialThread->Affinity, sizeof(InitialThread->Affinity));
    KiThreadAffinityMask(InitialThread) = 1;
    InitialThread->WaitIrql = DISPATCH_LEVEL;
    InitialProcess->ActiveProcessors = 1;
    Prcb->CurrentThread = InitialThread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = InitialThread;

    KdInitSystem(0, LoaderBlock);
    ExpInitializeExecutive(0, LoaderBlock);

    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(InitialThread, 0);
    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
        InterlockedOr((PLONG)&KiIdleSummary[0], (LONG)Prcb->SetMember);
    KiReleasePrcbLock(Prcb);
    KfRaiseIrql(HIGH_LEVEL);
    LoaderBlock->Prcb = 0;
    InitialThread->Priority = 0;
    KfLowerIrql(DISPATCH_LEVEL);
    InitialThread->WaitIrql = DISPATCH_LEVEL;
    KiIdleLoop();
}

VOID
NTAPI
KiInitMachineDependent(VOID)
{
    LARGE_INTEGER Frequency;

    Frequency.QuadPart = 0;
    KeQueryPerformanceCounter(&Frequency);
    if (Frequency.QuadPart != 0)
    {
        MmWriteableSharedUserData->QpcFrequency = Frequency.QuadPart;
        MmWriteableSharedUserData->QpcBias = 0;
    }

    /* ARMv7-A with VFPv3-D32 and NEON is the minimum this port boots on. */
    SharedUserData->ProcessorFeatures[PF_ARM_VFP_32_REGISTERS_AVAILABLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_ARM_NEON_INSTRUCTIONS_AVAILABLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_FLOATING_POINT_EMULATED] = FALSE;
    SharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE_DOUBLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_FASTFAIL_AVAILABLE] = TRUE;
    if (KeGetCurrentPrcb()->FeatureBits & KI_ARM_FEATURE_DIVIDE)
        SharedUserData->ProcessorFeatures[PF_ARM_DIVIDE_INSTRUCTION_AVAILABLE] = TRUE;
}
