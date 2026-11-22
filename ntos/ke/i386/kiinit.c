/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 kernel entry from the loader: PCR, GDT, IDT, TSS, features and processor startup
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "internal/i386/trap_x.h"

PCONTEXT KiProcessorContext[MAXIMUM_PROCESSORS];

UCHAR DECLSPEC_ALIGN(PAGE_SIZE) P0BootStackData[KERNEL_STACK_SIZE] = {0};
UCHAR DECLSPEC_ALIGN(PAGE_SIZE) KiDoubleFaultStackData[KERNEL_STACK_SIZE] = {0};
ULONG_PTR P0BootStack = (ULONG_PTR)&P0BootStackData[KERNEL_STACK_SIZE];
ULONG_PTR KiDoubleFaultStack = (ULONG_PTR)&KiDoubleFaultStackData[KERNEL_STACK_SIZE];

KSPIN_LOCK KiFreezeExecutionLock;
KSPIN_LOCK Ki486CompatibilityLock;

ULONG KiDpcTimeCount[MAXIMUM_PROCESSORS];
ULONG KiFastSystemCallReturnAddress;

ULONGLONG BootCycles, BootCyclesEnd;

/* The i386 AP path below does not yet bring secondary CPUs into the scheduler.
 * Keep their parking code outside INIT, and acknowledge startup only after
 * entering this resident routine: the BSP may then reclaim all INIT pages. */
static
DECLSPEC_NORETURN
DECLSPEC_NOINLINE
VOID
NTAPI
KiParkUninitializedProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    _disable();
    KeMemoryBarrier();
    LoaderBlock->Prcb = 0;

    for (;;)
    {
        __halt();
    }
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitMachineDependent(VOID)
{
    ULONG CpuCount;
    BOOLEAN FbCaching = FALSE;
    NTSTATUS Status;
    ULONG ReturnLength;
    ULONG i, Affinity, Sample = 0;
    PFX_SAVE_AREA FxSaveArea;
    ULONG MXCsrMask = 0xFFBF;
    CPU_INFO CpuInfo;
    KI_SAMPLE_MAP Samples[10];
    PKI_SAMPLE_MAP CurrentSample = Samples;
    LARGE_IDENTITY_MAP IdentityMap;

    if (KeFeatureBits & KF_LARGE_PAGE)
    {
        if (Ki386CreateIdentityMap(&IdentityMap, Ki386EnableCurrentLargePage, 2))
            KeIpiGenericCall(Ki386EnableTargetLargePage, (ULONG_PTR)&IdentityMap);

        Ki386FreeIdentityMap(&IdentityMap);
    }

    if (KeFeatureBits & KF_GLOBAL_PAGE)
    {
        CpuCount = KeNumberProcessors;
        KeIpiGenericCall(Ki386EnableGlobalPage, (ULONG_PTR)&CpuCount);
    }

    if (KeFeatureBits & (KF_PAT | KF_MTRR))
    {
        Status = HalQuerySystemInformation(HalFrameBufferCachingInformation,
                                           sizeof(BOOLEAN),
                                           &FbCaching,
                                           &ReturnLength);
        if ((NT_SUCCESS(Status)) && (FbCaching))
        {
            KeFeatureBits &= ~(KF_PAT | KF_MTRR);
        }
    }

    if (KeFeatureBits & KF_PAT) KiInitializePAT();

    if (KeFeatureBits & KF_CR4)
    {
        /* Do an IPI call to enable the Debug Exceptions */
        CpuCount = KeNumberProcessors;
        KeIpiGenericCall(Ki386EnableDE, (ULONG_PTR)&CpuCount);
    }

    if (KeFeatureBits & KF_FXSR)
    {
        CpuCount = KeNumberProcessors;
        KeIpiGenericCall(Ki386EnableFxsr, (ULONG_PTR)&CpuCount);

        if (KeFeatureBits & KF_XMMI)
        {
            CpuCount = KeNumberProcessors;
            KeIpiGenericCall(Ki386EnableXMMIExceptions, (ULONG_PTR)&CpuCount);

            *(PCHAR)RtlPrefetchMemoryNonTemporal = 0x90;
            /* FIXME: Implement and enable XMM Page Zeroing for Mm */
        }
    }

    KiRestoreFastSyscallReturnState();

    i = KeActiveProcessors.Bitmap[0];
    for (Affinity = 1; i; Affinity <<= 1)
    {
        if (i & Affinity)
        {
            i &= ~Affinity;
            KeSetSystemAffinityThread(Affinity);

            KeGetCurrentPrcb()->MHz = 0;

            if (KeFeatureBits & KF_RDTSC)
            {
                for (;;)
                {
                    KiCpuId(&CpuInfo, 0);

                    CurrentSample->PerfStart = KeQueryPerformanceCounter(NULL);
                    CurrentSample->TSCStart = __rdtsc();
                    CurrentSample->PerfFreq.QuadPart = -50000;

                    KeDelayExecutionThread(KernelMode,
                                           FALSE,
                                           &CurrentSample->PerfFreq);

                    KiCpuId(&CpuInfo, 0);

                    CurrentSample->PerfEnd =
                        KeQueryPerformanceCounter(&CurrentSample->PerfFreq);
                    CurrentSample->TSCEnd = __rdtsc();

                    CurrentSample->PerfDelta = CurrentSample->PerfEnd.QuadPart -
                                               CurrentSample->PerfStart.QuadPart;
                    CurrentSample->TSCDelta = CurrentSample->TSCEnd -
                                              CurrentSample->TSCStart;

                    CurrentSample->MHz = (ULONG)((CurrentSample->TSCDelta *
                                                  CurrentSample->
                                                  PerfFreq.QuadPart + 500000) /
                                                 (CurrentSample->PerfDelta *
                                                  1000000));

                    if (Sample)
                    {
                        if ((CurrentSample->MHz == CurrentSample[-1].MHz) ||
                            (CurrentSample->MHz == CurrentSample[-1].MHz + 1) ||
                            (CurrentSample->MHz == CurrentSample[-1].MHz - 1))
                        {
                            break;
                        }
                    }

                    CurrentSample++;
                    Sample++;

                    if (Sample == RTL_NUMBER_OF(Samples))
                    {
                        ULONG TotalMHz = 0;
                        while (Sample--)
                        {
                            TotalMHz += Samples[Sample].MHz;
                        }
                        CurrentSample[-1].MHz = TotalMHz / RTL_NUMBER_OF(Samples);
                        DPRINT1("Sampling CPU frequency failed. Using average of %lu MHz\n", CurrentSample[-1].MHz);
                        break;
                    }
                }

                KeGetCurrentPrcb()->MHz = CurrentSample[-1].MHz;
            }

            if (KeFeatureBits & KF_MTRR)
            {
                KiInitializeMTRR(i ? FALSE : TRUE);
            }

            if (KeFeatureBits & KF_AMDK6MTRR) KiAmdK6InitializeMTRR();

            /* Check if this is a buggy Pentium and apply the fixup if so */
            if (KiI386PentiumLockErrataPresent) KiI386PentiumLockErrataFixup();

            if (KeFeatureBits & KF_FXSR)
            {
                FxSaveArea = KiGetThreadNpxArea(KeGetCurrentThread());

                FxSaveArea->U.FxArea.MXCsrMask = 0;

                Ke386SaveFpuState(FxSaveArea);

                if (FxSaveArea->U.FxArea.MXCsrMask != 0)
                {
                    MXCsrMask = FxSaveArea->U.FxArea.MXCsrMask;
                }

                if (!KiMXCsrMask)
                {
                    KiMXCsrMask = MXCsrMask;
                }
                else
                {
                    if (KiMXCsrMask != MXCsrMask)
                    {
                        KeBugCheckEx(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED,
                                     KF_FXSR,
                                     KiMXCsrMask,
                                     MXCsrMask,
                                     0);
                    }
                }

                KiMXCsrMask &= MXCsrMask;
            }
        }
    }

    KeRevertToUserAffinityThread();

    if (KiTimeLimitIsrMicroseconds)
    {
        /* FIXME: TODO */
        DPRINT1("ISR Time Limit not yet supported\n");
    }

    KiSetCR0Bits();
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializePcr(IN ULONG ProcessorNumber,
                _In_ PKIPCR Pcr,
                _In_ PKIDTENTRY Idt,
                _In_ PKGDTENTRY Gdt,
                _In_ PKTSS Tss,
                _In_ PKTHREAD IdleThread,
                _In_ PVOID DpcStack)
{
    Pcr->NtTib.ExceptionList = EXCEPTION_CHAIN_END;
    Pcr->NtTib.StackBase = 0;
    Pcr->NtTib.StackLimit = 0;
    Pcr->NtTib.Self = NULL;

    Pcr->PrcbData.CurrentThread = IdleThread;

    Pcr->SelfPcr = (PKPCR)Pcr;
    Pcr->Prcb = &Pcr->PrcbData;
    KiProcessorContext[ProcessorNumber] = &Pcr->PrcbData.ProcessorState.ContextFrame;

    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;

    Pcr->PrcbData.MajorVersion = PRCB_MAJOR_VERSION;
    Pcr->PrcbData.MinorVersion = PRCB_MINOR_VERSION;

    Pcr->PrcbData.BuildType = 0;
#if DBG
    Pcr->PrcbData.BuildType |= PRCB_BUILD_DEBUG;
#endif

    Pcr->PrcbData.Number = (UCHAR)ProcessorNumber;
    Pcr->PrcbData.SetMember = 1 << ProcessorNumber;

    KiProcessorBlock[ProcessorNumber] = Pcr->Prcb;

    Pcr->Irql = PASSIVE_LEVEL;

    Pcr->GDT = (PVOID)Gdt;
    Pcr->IDT = Idt;
    Pcr->TSS = Tss;
    Pcr->TssCopy = Tss;
    Pcr->PrcbData.DpcStack = DpcStack;

    Pcr->PrcbData.MultiThreadProcessorSet = Pcr->PrcbData.SetMember;
    Pcr->PrcbData.MultiThreadSetMaster = &Pcr->PrcbData;
}

static
CODE_SEG("INIT")
VOID
KiVerifyCpuFeatures(PKPRCB Prcb)
{
    CPU_INFO CpuInfo;

    ULONG EFlags = __readeflags();

    ULONG NewEFlags = EFlags ^ EFLAGS_ID;
    __writeeflags(NewEFlags);

    NewEFlags = __readeflags();

    if (NewEFlags == EFlags)
    {
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, 0x1, 0, 0, 0);
    }
    else
    {
        EFlags |= EFLAGS_ID;
        __writeeflags(EFlags);
    }

    KiCpuId(&CpuInfo, 0);
    if (CpuInfo.Eax == 0)
    {
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, 0x1, 0, 0, 0);
    }

    KiSetProcessorType();

    if (Prcb->CpuType == 3)
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, 0x386, 0, 0, 0);

    ULONG64 FeatureBits = KiGetFeatureBits();

    if (!(FeatureBits & KF_RDTSC))
    {
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, 0x2, 0x00000010, 0, 0);
    }

    if (!(FeatureBits & KF_CMPXCHG8B))
    {
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, 0x2, 0x00000100, 0, 0);
    }

    // Check x87 FPU is present. FIXME: put this into FeatureBits?
    KiCpuId(&CpuInfo, 1);

    if (!(CpuInfo.Edx & 0x00000001))
    {
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, 0x2, 0x00000001, 0, 0);
    }

    ULONG Cr0 = __readcr0();

    Cr0 &= ~(CR0_EM | CR0_MP);

    Cr0 |= CR0_NE;

    __writecr0(Cr0);

    // Check for Pentium FPU bug.
    if (KiIsNpxErrataPresent())
    {
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, 0x2, 0x00000001, 0, 0);
    }

    Prcb->FeatureBits = (ULONG)FeatureBits;
    Prcb->FeatureBitsHigh = FeatureBits >> 32;
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeKernel(IN PKPROCESS InitProcess,
                   _In_ PKTHREAD InitThread,
                   _In_ PVOID IdleStack,
                   _In_ PKPRCB Prcb,
                   _In_ CCHAR Number,
                   _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG PageDirectory[2];
    PVOID DpcStack;
    KIRQL DummyIrql;
    ULONG NxFeatureBits;

    PoInitializePrcb(Prcb);

    if (Number == 0)
        KeFeatureBits = Prcb->FeatureBits | (ULONG64)Prcb->FeatureBitsHigh << 32;

    SharedUserData->NXSupportPolicy = KiNxPolicyFromLoadOptions(KeLoaderBlock->LoadOptions, &NxFeatureBits);
    KeFeatureBits |= NxFeatureBits;

    KiSaveProcessorControlState(&Prcb->ProcessorState);

#if DBG

    if (Number == 0)
        KiReportCpuFeatures();
#endif

    KiGetCacheInformation();

    KiInitSpinLocks(Prcb, Number);

    Prcb->ParentNode = KeNodeBlock[0];
    Prcb->ParentNode->ProcessorMask |= Prcb->SetMember;

    if (!Number)
    {
        KeI386CpuType = Prcb->CpuType;
        KeI386CpuStep = Prcb->CpuStep;
        KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
        KeProcessorLevel = (USHORT)Prcb->CpuType;
        if (Prcb->CpuID) KeProcessorRevision = Prcb->CpuStep;
        KeI386FxsrPresent = (KeFeatureBits & KF_FXSR) ? TRUE : FALSE;
        KeI386XMMIPresent = (KeFeatureBits & KF_XMMI) ? TRUE : FALSE;

        Prcb->MultiThreadSetMaster = Prcb;

        KeLowerIrql(APC_LEVEL);

        KeInitializeSpinLock(&KiFreezeExecutionLock);
        KeInitializeSpinLock(&Ki486CompatibilityLock);

        MiInitializeKernelVaLayout(LoaderBlock);
        KiInitSystem();

        PageDirectory[0] = 0;
        PageDirectory[1] = 0;
        KeInitializeProcess(InitProcess,
                            0,
                            NULL,
                            PageDirectory,
                            FALSE);
        InitProcess->QuantumReset = MAXCHAR;
    }
    else
    {
        /* FIXME */
        DPRINT1("Starting CPU#%u - you are brave\n", Number);
    }

    ASSERT(Prcb->Number == (UCHAR)Number);
    KiInitializeStartupThread(Prcb, InitThread, InitProcess, IdleStack, NULL, NULL);

    SharedUserData->ProcessorFeatures[PF_FLOATING_POINT_PRECISION_ERRATA] = FALSE;
    SharedUserData->ProcessorFeatures[PF_MMX_INSTRUCTIONS_AVAILABLE] =
        (KeFeatureBits & KF_MMX) ? TRUE: FALSE;
    SharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE_DOUBLE] =
        (KeFeatureBits & KF_CMPXCHG8B) ? TRUE: FALSE;
    SharedUserData->ProcessorFeatures[PF_XMMI_INSTRUCTIONS_AVAILABLE] =
        ((KeFeatureBits & KF_FXSR) && (KeFeatureBits & KF_XMMI)) ? TRUE: FALSE;
    SharedUserData->ProcessorFeatures[PF_XMMI64_INSTRUCTIONS_AVAILABLE] =
        ((KeFeatureBits & KF_FXSR) && (KeFeatureBits & KF_XMMI64)) ? TRUE: FALSE;
    SharedUserData->ProcessorFeatures[PF_3DNOW_INSTRUCTIONS_AVAILABLE] =
        (KeFeatureBits & KF_3DNOW) ? TRUE: FALSE;
    SharedUserData->ProcessorFeatures[PF_RDTSC_INSTRUCTION_AVAILABLE] =
        (KeFeatureBits & KF_RDTSC) ? TRUE: FALSE;
    SharedUserData->ProcessorFeatures[PF_RDRAND_INSTRUCTION_AVAILABLE] =
        (KeFeatureBits & KF_RDRAND) ? TRUE : FALSE;

    // Note: On x86 we lack support for saving/restoring SSE state
    SharedUserData->ProcessorFeatures[PF_SSE3_INSTRUCTIONS_AVAILABLE] =
        (KeFeatureBits & KF_SSE3) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_SSSE3_INSTRUCTIONS_AVAILABLE] =
        (KeFeatureBits & KF_SSSE3) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_SSE4_1_INSTRUCTIONS_AVAILABLE] =
        (KeFeatureBits & KF_SSE4_1) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_SSE4_2_INSTRUCTIONS_AVAILABLE] =
        (KeFeatureBits & KF_SSE4_2) ? TRUE : FALSE;

    Prcb->CurrentThread = InitThread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = InitThread;

    ExpInitializeExecutive(Number, LoaderBlock);

    if (!Number)
    {
        KiTimeIncrementReciprocal =
            KiComputeReciprocal(KeMaximumIncrement,
                                &KiTimeIncrementShiftCount);

        Prcb->MaximumDpcQueueDepth = KiMaximumDpcQueueDepth;
        Prcb->MinimumDpcRate = KiMinimumDpcRate;
        Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;

        DpcStack = MmCreateKernelStack(FALSE, 0);
        if (!DpcStack) KeBugCheckEx(NO_PAGES_AVAILABLE, 1, 0, 0, 0);
        Prcb->DpcStack = DpcStack;

        Ki386IopmSaveArea = ExAllocatePoolWithTag(PagedPool,
                                                  IOPM_SIZE,
                                                  TAG_KERNEL);
        if (!Ki386IopmSaveArea)
        {
            /* Bugcheck. We need this for V86/VDM support. */
            KeBugCheckEx(NO_PAGES_AVAILABLE, 2, IOPM_SIZE, 0, 0);
        }
    }

    KiYieldStartupProcessor(Prcb, InitThread);

    KeRaiseIrql(HIGH_LEVEL, &DummyIrql);
    LoaderBlock->Prcb = 0;
}

CODE_SEG("INIT")
VOID
FASTCALL
KiGetMachineBootPointers(IN PKGDTENTRY *Gdt,
                         _In_ PKIDTENTRY *Idt,
                         _In_ PKIPCR *Pcr,
                         _In_ PKTSS *Tss)
{
    KDESCRIPTOR GdtDescriptor, IdtDescriptor;
    KGDTENTRY TssSelector, PcrSelector;
    USHORT Tr, Fs;

    Ke386GetGlobalDescriptorTable(&GdtDescriptor.Limit);
    __sidt(&IdtDescriptor.Limit);

    *Gdt = (PKGDTENTRY)GdtDescriptor.Base;
    *Idt = (PKIDTENTRY)IdtDescriptor.Base;

    Tr = Ke386GetTr();
    Fs = Ke386GetFs();

    PcrSelector = *(PKGDTENTRY)((ULONG_PTR)*Gdt + (Fs & ~RPL_MASK));

    *Pcr = (PKIPCR)(ULONG_PTR)(PcrSelector.BaseLow |
                               PcrSelector.HighWord.Bytes.BaseMid << 16 |
                               PcrSelector.HighWord.Bytes.BaseHi << 24);

    TssSelector = *(PKGDTENTRY)((ULONG_PTR)*Gdt + (Tr & ~RPL_MASK));

    *Tss = (PKTSS)(ULONG_PTR)(TssSelector.BaseLow |
                              TssSelector.HighWord.Bytes.BaseMid << 16 |
                              TssSelector.HighWord.Bytes.BaseHi << 24);
}

CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartupBootStack(VOID)
{
    PKTHREAD Thread;

    KiInitializeKernel(&KiInitialProcess.Pcb,
                       (PKTHREAD)KeLoaderBlock->Thread,
                       (PVOID)(KeLoaderBlock->KernelStack & ~3),
                       (PKPRCB)__readfsdword(FIELD_OFFSET(KIPCR, Prcb)),
                       KeNumberProcessors - 1,
                       KeLoaderBlock);

    Thread = KeGetCurrentThread();
    Thread->Priority = 0;

    _enable();
    KeLowerIrql(DISPATCH_LEVEL);

    Thread->WaitIrql = DISPATCH_LEVEL;

    KiIdleLoop();
}

static
VOID
KiMarkPageAsReadOnly(
    PVOID Address)
{
    PHARDWARE_PTE PointerPte;

    ASSERT(ALIGN_DOWN_POINTER_BY(Address, PAGE_SIZE) == Address);

    PointerPte = ((PHARDWARE_PTE)PTE_BASE) + ((ULONG_PTR)Address / PAGE_SIZE);
    ASSERT(PointerPte->Valid);
    ASSERT(PointerPte->Write);

    PointerPte->Write = 0;

    __invlpg(Address);
}

CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartup(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG Cpu;
    PKTHREAD InitialThread;
    ULONG InitialStack;
    PKGDTENTRY Gdt;
    PKIDTENTRY Idt;
    KIDTENTRY NmiEntry, DoubleFaultEntry;
    PKTSS Tss;
    PKIPCR Pcr;
    KIRQL DummyIrql;

    BootCycles = __rdtsc();

    KeLoaderBlock = LoaderBlock;
    Cpu = KeNumberProcessors;
    if (!Cpu)
    {
        Ke386SetFs(KGDT_R0_PCR);
        __writefsbyte(FIELD_OFFSET(KIPCR, PrcbData.LegacyNumber), (UCHAR)Cpu);
        __writefsdword(FIELD_OFFSET(KIPCR, PrcbData.Number), Cpu);

        LoaderBlock->KernelStack = (ULONG_PTR)P0BootStack;
        LoaderBlock->Thread = (ULONG_PTR)&KiInitialThread;
    }

    InitialStack = LoaderBlock->KernelStack;
    InitialThread = (PKTHREAD)LoaderBlock->Thread;

    InitializeListHead(&InitialThread->ApcState.ApcListHead[KernelMode]);

    KiInitializeMachineType();

    if (Cpu) goto AppCpuInit;

    KiGetMachineBootPointers(&Gdt, &Idt, &Pcr, &Tss);

    Ki386InitializeTss(Tss, Idt, Gdt);

    C_ASSERT(sizeof(KIPCR) <= KIPCR_BOOT_SIZE);
    RtlZeroMemory(Pcr, sizeof(*Pcr));
    KiInitializePcr(Cpu,
                    Pcr,
                    Idt,
                    Gdt,
                    Tss,
                    InitialThread,
                    (PVOID)KiDoubleFaultStack);

    InitialThread->ApcState.Process = &KiInitialProcess.Pcb;

    /* Clear DR6/7 to cleanup bootloader debugging */
    __writefsdword(FIELD_OFFSET(KIPCR, NtTib.Self), 0);
    __writefsdword(FIELD_OFFSET(KIPCR, PrcbData.ProcessorState.SpecialRegisters.KernelDr6), 0);
    __writefsdword(FIELD_OFFSET(KIPCR, PrcbData.ProcessorState.SpecialRegisters.KernelDr7), 0);

    KeInitExceptions();

    Ke386SetDs(KGDT_R3_DATA | RPL_MASK);
    Ke386SetEs(KGDT_R3_DATA | RPL_MASK);

    RtlCopyMemory(&NmiEntry, &Idt[2], sizeof(KIDTENTRY));
    RtlCopyMemory(&DoubleFaultEntry, &Idt[8], sizeof(KIDTENTRY));

    RtlCopyMemory(Idt,
                  (PVOID)KiIdtDescriptor.Base,
                  KiIdtDescriptor.Limit + 1);

    RtlCopyMemory(&Idt[2], &NmiEntry, sizeof(KIDTENTRY));
    RtlCopyMemory(&Idt[8], &DoubleFaultEntry, sizeof(KIDTENTRY));

AppCpuInit:

    //TODO: We don't setup IPIs yet so freeze other processors here.
    if (Cpu)
    {
        KiParkUninitializedProcessor(LoaderBlock);
    }

    do
    {
        while (*(volatile PKSPIN_LOCK*)&KiFreezeExecutionLock == (PVOID)1);
    } while(InterlockedBitTestAndSet((PLONG)&KiFreezeExecutionLock, 0));

    __writefsbyte(FIELD_OFFSET(KIPCR, Number), (UCHAR)Cpu);
    __writefsdword(FIELD_OFFSET(KIPCR, SetMember), 1 << Cpu);
    __writefsdword(FIELD_OFFSET(KIPCR, SetMemberCopy), 1 << Cpu);
    __writefsdword(FIELD_OFFSET(KIPCR, PrcbData.SetMember), 1 << Cpu);

    KiVerifyCpuFeatures(Pcr->Prcb);

    HalInitializeProcessor(Cpu, KeLoaderBlock);

    KeActiveProcessors.Bitmap[0] |= __readfsdword(FIELD_OFFSET(KIPCR, SetMember));
    KiSetProcessorCount(KeProcessorCount + 1);

    if (!Cpu)
    {
        /* Initialize debugging system */
        KdInitSystem(0, KeLoaderBlock);

        if (KdPollBreakIn()) DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);

        KiMarkPageAsReadOnly(P0BootStackData);
        KiMarkPageAsReadOnly(KiDoubleFaultStackData);
    }

    KeRaiseIrql(HIGH_LEVEL, &DummyIrql);

    KiSwitchToBootStack(InitialStack & ~3);
}

/*
 * A 32-bit Nt stub reads the kernel entry point out of the shared user data,
 * so the system DLL's own stub addresses have to be published there.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
KiI386PublishSystemCallStubs(
    _In_ PVOID DllBase)
{
    PVOID Call;
    PVOID Return;

    PAGED_CODE();

    if (KeFeatureBits & KF_FAST_SYSCALL)
    {
        Call = RtlFindExportedRoutineByName(DllBase, "KiFastSystemCall");
        Return = RtlFindExportedRoutineByName(DllBase, "KiFastSystemCallRet");
        if ((Call == NULL) || (Return == NULL)) return STATUS_PROCEDURE_NOT_FOUND;

        SharedUserData->SystemCall = (ULONG)(ULONG_PTR)Call;
        KiFastSystemCallReturnAddress = (ULONG)(ULONG_PTR)Return;
        return STATUS_SUCCESS;
    }

    Call = RtlFindExportedRoutineByName(DllBase, "KiIntSystemCall");
    if (Call == NULL) return STATUS_PROCEDURE_NOT_FOUND;

    SharedUserData->SystemCall = (ULONG)(ULONG_PTR)Call;
    KiFastSystemCallReturnAddress = 0;
    return STATUS_SUCCESS;
}
