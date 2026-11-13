/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 boot-processor loader contract adoption
 *
 * The loader hands the kernel a mapped address space, a boot stack, a KPCR,
 * an initial KPROCESS/KTHREAD and the arch payload of the loader block; the
 * whole of this file is the proof that the delivery matches the live machine
 * before any of it is published (handoff contract section 3.6), and then the
 * portable kernel bring-up that follows.
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

/* These values are kernel-private diagnostics for a debugger attached before
 * any post-firmware console exists. They are not an NT ABI or a boot result. */
typedef enum _KI_LA64_STARTUP_FAILURE
{
    KiLa64StartupNoFailure = 0,
    KiLa64StartupInvalidLoaderPointer,
    KiLa64StartupInvalidPayloadVersion,
    KiLa64StartupInvalidPayloadFlags,
    KiLa64StartupInvalidAddressLayout,
    KiLa64StartupInvalidCpuConfig,
    KiLa64StartupInvalidPageTableRoot,
    KiLa64StartupInvalidCrmd,
    KiLa64StartupInvalidLoaderMapping,
    KiLa64StartupInvalidPcrMapping,
    KiLa64StartupInvalidThreadMapping,
    KiLa64StartupInvalidProcessMapping,
    KiLa64StartupInvalidStackMapping,
    KiLa64StartupInvalidSharedDataMapping,
    KiLa64StartupInvalidExceptionStub,
    KiLa64StartupPcrPublicationFailure,
    KiLa64StartupTrapVectorFailure
} KI_LA64_STARTUP_FAILURE;

typedef enum _KI_LA64_STARTUP_PHASE
{
    KiLa64StartupEntered = 1,
    KiLa64StartupPayloadValidated,
    KiLa64StartupPcrPublished,
    KiLa64StartupTrapVectorInstalled,
    KiLa64StartupPortableKernelInitialized,
    KiLa64StartupInitialThreadInitialized,
    KiLa64StartupExecutiveEntered
} KI_LA64_STARTUP_PHASE;

volatile ULONG KiLa64StartupPhase;
volatile ULONG KiLa64StartupFailure;
ULONG ProcessCount;

C_ASSERT(sizeof(KPROCESS) <= PAGE_SIZE);
C_ASSERT(sizeof(KTHREAD) <= PAGE_SIZE);
C_ASSERT(sizeof(EPROCESS) <= PAGE_SIZE);
C_ASSERT(sizeof(ETHREAD) <= PAGE_SIZE);

static
VOID
KiLa64StartupPrintHex(
    _In_ ULONG_PTR Value)
{
    static const CHAR Digits[] = "0123456789ABCDEF";
    CHAR Buffer[2 * sizeof(ULONG_PTR)];
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Buffer); Index++)
        Buffer[RTL_NUMBER_OF(Buffer) - 1 - Index] = Digits[(Value >> (4 * Index)) & 0xF];
    KiLa64ConsoleWrite(Buffer, sizeof(Buffer));
}

static
DECLSPEC_NORETURN
VOID
KiLa64StartupStop(
    _In_ KI_LA64_STARTUP_FAILURE Failure)
{
    static const CHAR Message[] = "\r\nKiLa64SystemStartup: fatal startup failure ";
    static const CHAR PhaseText[] = " in phase ";
    static const CHAR Tail[] = ". Halting.\r\n";

    KiLa64StartupFailure = Failure;
    KeDisableInterrupts();
    KiLa64SetInterruptMask(0);

    /* The console is up right after payload validation; earlier failures
     * remain visible only through the KiLa64StartupFailure variable. */
    if (KiLa64ConsoleReady())
    {
        KiLa64ConsoleWrite(Message, sizeof(Message) - 1);
        KiLa64StartupPrintHex(Failure);
        KiLa64ConsoleWrite(PhaseText, sizeof(PhaseText) - 1);
        KiLa64StartupPrintHex(KiLa64StartupPhase);
        KiLa64ConsoleWrite(Tail, sizeof(Tail) - 1);
    }
    for (;;)
        __asm__ __volatile__("idle 0" ::: "memory");
}

/* Every structure the loader hands over lives in the cached direct map
 * (decision D-1): DMW1 covers the first PhysicalLimit bytes of RAM, so a
 * pointer is usable exactly when it is inside that window. */
static
BOOLEAN
KiLa64AddressInDirectMap(
    _In_ ULONG_PTR Address,
    _In_ SIZE_T Size,
    _In_ ULONG64 PhysicalLimit)
{
    ULONG64 Offset;

    if ((Size == 0) || (Address < (ULONG_PTR)LOONGARCH64_LOADER_DIRECT_MAP_BASE))
        return FALSE;

    Offset = Address - LOONGARCH64_LOADER_DIRECT_MAP_BASE;
    return (Offset < PhysicalLimit) && (Size <= PhysicalLimit - Offset);
}

/* The live registers the payload claims to describe. Each read is a separate
 * instruction because the CSR address is an immediate ([V1-4.2.1]). */
typedef enum _KI_LA64_CSR
{
    KiLa64CsrPgdl = 0,
    KiLa64CsrPgdh,
    KiLa64CsrDmw0,
    KiLa64CsrDmw1,
    KiLa64CsrDmw2,
    KiLa64CsrDmw3
} KI_LA64_CSR;

static
ULONG64
KiLa64ReadAddressCsr(
    _In_ KI_LA64_CSR Csr)
{
    ULONG64 Value = 0;

    switch (Csr)
    {
        case KiLa64CsrPgdl: __asm__ __volatile__("csrrd %0, 0x19" : "=r"(Value)); break;
        case KiLa64CsrPgdh: __asm__ __volatile__("csrrd %0, 0x1a" : "=r"(Value)); break;
        case KiLa64CsrDmw0: __asm__ __volatile__("csrrd %0, 0x180" : "=r"(Value)); break;
        case KiLa64CsrDmw1: __asm__ __volatile__("csrrd %0, 0x181" : "=r"(Value)); break;
        case KiLa64CsrDmw2: __asm__ __volatile__("csrrd %0, 0x182" : "=r"(Value)); break;
        default:            __asm__ __volatile__("csrrd %0, 0x183" : "=r"(Value)); break;
    }
    return Value;
}

static
ULONG
KiLa64ReadConfigCsr(
    _In_ ULONG Address)
{
    ULONG64 Value = 0;

    switch (Address)
    {
        case 0x1c: __asm__ __volatile__("csrrd %0, 0x1c" : "=r"(Value)); break;
        case 0x1d: __asm__ __volatile__("csrrd %0, 0x1d" : "=r"(Value)); break;
        case 0x1e: __asm__ __volatile__("csrrd %0, 0x1e" : "=r"(Value)); break;
        case 0x0:  __asm__ __volatile__("csrrd %0, 0x0" : "=r"(Value)); break;
        default:   __asm__ __volatile__("csrrd %0, 0x18" : "=r"(Value)); break;
    }
    return (ULONG)Value;
}

static
KI_LA64_STARTUP_FAILURE
KiLa64ValidateLoaderBlock(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOONGARCH64_LOADER_BLOCK LaBlock;
    MI_LA64_PAGE_WALK SharedWalk;
    ULONG Index;

    if (!KiLa64AddressInDirectMap((ULONG_PTR)LoaderBlock, sizeof(*LoaderBlock),
                                  LOONGARCH64_LOADER_PHYSICAL_LIMIT))
    {
        return KiLa64StartupInvalidLoaderPointer;
    }

    LaBlock = &LoaderBlock->u.LoongArch64;
    /* Version 1 accepts a payload at least as large as the version size: an
     * appended field is allowed, a smaller record is not. */
    if ((LaBlock->Version != LOONGARCH64_LOADER_BLOCK_VERSION) ||
        (LaBlock->Size < sizeof(LOONGARCH64_LOADER_BLOCK)))
    {
        return KiLa64StartupInvalidPayloadVersion;
    }
    /* Every required flag must be set; only the optional ones may be added. */
    if (((LaBlock->Flags & LOONGARCH64_LOADER_REQUIRED_FLAGS) !=
         LOONGARCH64_LOADER_REQUIRED_FLAGS) ||
        (LaBlock->Flags &
         ~(LOONGARCH64_LOADER_REQUIRED_FLAGS | LOONGARCH64_LOADER_OPTIONAL_FLAGS)))
    {
        return KiLa64StartupInvalidPayloadFlags;
    }
    if ((LaBlock->DirectMapBase != LOONGARCH64_LOADER_DIRECT_MAP_BASE) ||
        (LaBlock->UncachedBase != LOONGARCH64_LOADER_UNCACHED_BASE) ||
        (LaBlock->WriteCombineBase != LOONGARCH64_LOADER_WRITE_COMBINE_BASE) ||
        (LaBlock->PhysicalLimit != LOONGARCH64_LOADER_PHYSICAL_LIMIT) ||
        (LaBlock->HighestMappedPhysicalAddress < PAGE_SIZE) ||
        (LaBlock->HighestMappedPhysicalAddress >= LaBlock->PhysicalLimit))
    {
        return KiLa64StartupInvalidAddressLayout;
    }

    /* Section 3.6 item 3: the payload's address space must be the live one.
     * A mismatch means the loader and the kernel disagree about which
     * translations exist, which nothing later can repair. */
    if ((LaBlock->Flags & LOONGARCH64_LOADER_FLAG_ADDRESS_SPACE_VALID) != 0)
    {
        if ((KiLa64ReadAddressCsr(KiLa64CsrPgdl) != LaBlock->Pgdl) ||
            (KiLa64ReadAddressCsr(KiLa64CsrPgdh) != LaBlock->Pgdh) ||
            (KiLa64ReadConfigCsr(0x1c) != (ULONG)LaBlock->Pwcl) ||
            (KiLa64ReadConfigCsr(0x1d) != (ULONG)LaBlock->Pwch) ||
            (KiLa64ReadConfigCsr(0x1e) != LaBlock->StlbPs) ||
            (KiLa64ReadConfigCsr(0x18) != LaBlock->Asid))
        {
            return KiLa64StartupInvalidAddressLayout;
        }
        for (Index = 0; Index < RTL_NUMBER_OF(LaBlock->Dmw); Index++)
        {
            if ((ULONG)KiLa64ReadAddressCsr((KI_LA64_CSR)(KiLa64CsrDmw0 + Index)) != LaBlock->Dmw[Index])
                return KiLa64StartupInvalidAddressLayout;
        }
    }

    /* CPUCFG1 must describe the 48-bit split the address space layer assumes
     * (CPUCFG1.ARCH == LoongArch64, VALEN == 48; contract G-1). */
    if (!KiLa64Processor.Valid)
        return KiLa64StartupInvalidCpuConfig;

    /* One root serves both halves, and MiAddressSpaceAdopt narrows the frame
     * number to 32 bits, so the root page must sit below 2^(32+12). */
    if (((LaBlock->PageDirectoryRoot & (PAGE_SIZE - 1)) != 0) ||
        ((LaBlock->Pgdl & ~0xFFFULL) != LaBlock->PageDirectoryRoot) ||
        ((LaBlock->Pgdh & ~0xFFFULL) != LaBlock->PageDirectoryRoot) ||
        (LaBlock->PageDirectoryRoot >= (1ULL << 44)))
    {
        return KiLa64StartupInvalidPageTableRoot;
    }

    /* Entry contract: translated kernel mode with interrupts off. */
    {
        ULONG Crmd = KiLa64ReadConfigCsr(0x0);

        if ((Crmd & LA64_CRMD_IE) || (Crmd & LA64_CRMD_DA) || !(Crmd & LA64_CRMD_PG))
            return KiLa64StartupInvalidCrmd;
    }

    KiLa64StartupPhase = KiLa64StartupPayloadValidated;

    if (!KiLa64AddressInDirectMap((ULONG_PTR)LoaderBlock, sizeof(*LoaderBlock),
                                  LaBlock->HighestMappedPhysicalAddress) ||
        !KiLa64AddressInDirectMap((ULONG_PTR)LaBlock, LaBlock->Size,
                                  LaBlock->HighestMappedPhysicalAddress))
    {
        return KiLa64StartupInvalidLoaderMapping;
    }
    if (((LaBlock->PcrPage & (PAGE_SIZE - 1)) != 0) ||
        (LoaderBlock->Prcb !=
         LaBlock->PcrPage + FIELD_OFFSET(KPCR, Prcb)) ||
        !KiLa64AddressInDirectMap((ULONG_PTR)LaBlock->PcrPage, sizeof(KPCR),
                                  LaBlock->HighestMappedPhysicalAddress))
    {
        return KiLa64StartupInvalidPcrMapping;
    }
    if (((LaBlock->DpcStack & 15) != 0) ||
        (LaBlock->DpcStack < LaBlock->DirectMapBase + KERNEL_STACK_SIZE) ||
        !KiLa64AddressInDirectMap((ULONG_PTR)(LaBlock->DpcStack - KERNEL_STACK_SIZE),
                                  KERNEL_STACK_SIZE, LaBlock->HighestMappedPhysicalAddress))
    {
        return KiLa64StartupInvalidStackMapping;
    }
    if (((LoaderBlock->Thread & (PAGE_SIZE - 1)) != 0) ||
        !KiLa64AddressInDirectMap(LoaderBlock->Thread, sizeof(KTHREAD),
                                  LaBlock->HighestMappedPhysicalAddress))
    {
        return KiLa64StartupInvalidThreadMapping;
    }
    if (((LoaderBlock->Process & (PAGE_SIZE - 1)) != 0) ||
        !KiLa64AddressInDirectMap(LoaderBlock->Process, sizeof(KPROCESS),
                                  LaBlock->HighestMappedPhysicalAddress))
    {
        return KiLa64StartupInvalidProcessMapping;
    }
    if (((LoaderBlock->KernelStack & 15) != 0) ||
        (LoaderBlock->KernelStack < LaBlock->DirectMapBase + KERNEL_STACK_SIZE) ||
        !KiLa64AddressInDirectMap(LoaderBlock->KernelStack - KERNEL_STACK_SIZE,
                                  KERNEL_STACK_SIZE, LaBlock->HighestMappedPhysicalAddress))
    {
        return KiLa64StartupInvalidStackMapping;
    }
#if (NTDDI_VERSION >= NTDDI_WIN8)
    if (LoaderBlock->KernelStackSize != KERNEL_STACK_SIZE)
        return KiLa64StartupInvalidStackMapping;
#endif
    /* The shared user data page is the one kernel-half mapping the loader
     * makes; the PTE it installed has to still name the page it reports. */
    if (((LaBlock->SharedUserDataPage & (PAGE_SIZE - 1)) != 0) ||
        (LaBlock->SharedUserDataPage >= LaBlock->PhysicalLimit) ||
        !NT_SUCCESS(MiLa64WalkCurrentPageTables((PVOID)(ULONG_PTR)KI_USER_SHARED_DATA,
                                                &SharedWalk)))
    {
        return KiLa64StartupInvalidSharedDataMapping;
    }
    /* ExceptionStub is a direct-map virtual address of the fail-stop page and
     * ExceptionStubPhysical is the same page as TLBRENTRY sees it. */
    if (((LaBlock->ExceptionStubPhysical & (PAGE_SIZE - 1)) != 0) ||
        (LaBlock->ExceptionStub !=
         LaBlock->DirectMapBase + LaBlock->ExceptionStubPhysical))
    {
        return KiLa64StartupInvalidExceptionStub;
    }

    return KiLa64StartupNoFailure;
}

/* Install this processor's real vectors, replacing the loader's fail-stop
 * page. EENTRY is a virtual address ([V1-7.4.10]) while TLBRENTRY is a
 * physical one ([V1-7.5.11]) because the refill runs with DA=1. */
BOOLEAN
NTAPI
KiLa64InitializeTrapVector(VOID)
{
    ULONG64 Entry = (ULONG64)(ULONG_PTR)KiLa64TrapEntry;
    ULONG64 Refill = (ULONG64)(ULONG_PTR)KiLa64TlbRefillEntry;
    ULONG64 Physical, ReadBack;

    /* EENTRY bits 11:0 are read-only zero, so the handler must be page
     * aligned. */
    if ((Entry & 0xFFF) != 0)
        return FALSE;
    if (Refill < LOONGARCH64_LOADER_DIRECT_MAP_BASE)
        return FALSE;
    Physical = Refill - LOONGARCH64_LOADER_DIRECT_MAP_BASE;
    if (Physical >= LOONGARCH64_LOADER_PHYSICAL_LIMIT)
        return FALSE;

    KiLa64MemoryBarrier();
    KiLa64WriteEentry(Entry);
    KiLa64WriteTlbRefillEntry(Physical);
    KiLa64InstructionBarrier();

    __asm__ __volatile__("csrrd %0, 0xc" : "=r"(ReadBack));
    if (ReadBack != Entry)
        return FALSE;
    ReadBack = KiLa64ReadTlbRefillEntry();
    return ReadBack == Physical;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiLa64SystemStartup(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    KI_LA64_STARTUP_FAILURE Failure;
    PLOONGARCH64_LOADER_BLOCK LaBlock;
    PKPROCESS InitialProcess;
    PKTHREAD InitialThread;
    PKPRCB Prcb;
    ULONG_PTR DirectoryTableBase[2];

    KiLa64StartupPhase = KiLa64StartupEntered;
    KiLa64StartupFailure = KiLa64StartupNoFailure;
    Failure = KiLa64ValidateLoaderBlock(LoaderBlock);
    if (Failure != KiLa64StartupNoFailure)
        KiLa64StartupStop(Failure);

    /* Bring up the loader-described console first so every later failure can
     * print, then identify the core. A missing console is not fatal. */
    LaBlock = &LoaderBlock->u.LoongArch64;
    KiLa64ConsoleInitialize(LoaderBlock);
    KiLa64IdentifyProcessor(LoaderBlock);
    if (!KiLa64Processor.Valid)
        KiLa64StartupStop(KiLa64StartupInvalidCpuConfig);

    LoaderBlock->Process = (ULONG_PTR)&KiInitialProcess.Pcb;
    InitialProcess = (PKPROCESS)(ULONG_PTR)LoaderBlock->Process;
    InitialThread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;
    InitialThread->ApcState.Process = InitialProcess;
    KeLoaderBlock = LoaderBlock;
    KeMemoryBarrier();

    if (!KiLa64InitializeBootPcr(
            (PKPCR)(ULONG_PTR)LaBlock->PcrPage,
            InitialThread,
            (ULONG_PTR)LaBlock->CpuId,
            (PVOID)(ULONG_PTR)LaBlock->DpcStack))
    {
        KiLa64StartupStop(KiLa64StartupPcrPublicationFailure);
    }
    KiLa64StartupPhase = KiLa64StartupPcrPublished;

    if (!KiLa64InitializeTrapVector())
        KiLa64StartupStop(KiLa64StartupTrapVectorFailure);
    KiLa64StartupPhase = KiLa64StartupTrapVectorInstalled;

    /* Every line stays masked until the HAL enables a source; CLOCK_LEVEL on
     * IS[11] and IPI_LEVEL on IS[12] follow [V1-6.1.2]. */
    KiLa64SetInterruptMask(0);
    KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_LOONGARCH;
    KeProcessorLevel = 0;
    KeProcessorRevision = 0;
    KeFeatureBits = KiLa64Processor.CpuCfg2;
    KeSetDmaIoCoherency(0);

    Prcb = KeGetCurrentPrcb();
    KiNode0.ProcessorMask |= Prcb->SetMember;
    KiLa64JoinSchedulerSubNode(Prcb);
    PoInitializePrcb(Prcb);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    ExInitPoolLookasidePointers();

    /* Portable initialization takes locks at APC level. Interrupt delivery
     * remains globally disabled until the HAL has installed real sources. */
    KfLowerIrql(APC_LEVEL);
    MiInitializeKernelVaLayout(LoaderBlock);
    KiInitSystem();
    KiLa64StartupPhase = KiLa64StartupPortableKernelInitialized;

    /* One root serves both halves; the generic second slot is the upper-half
     * root, which is the same page on this port. */
    DirectoryTableBase[0] = LaBlock->PageDirectoryRoot;
    DirectoryTableBase[1] = LaBlock->PageDirectoryRoot;
    InitializeListHead(&KiProcessListHead);
    KeInitializeProcess(InitialProcess,
                        0,
                        MAXULONG_PTR,
                        DirectoryTableBase,
                        FALSE);
    InitialProcess->QuantumReset = MAXCHAR;

    /* This is the already executing boot thread. The architecture context
     * initializer accepts only that special case until resumable start and
     * switch frames are implemented. */
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
    KiLa64StartupPhase = KiLa64StartupInitialThreadInitialized;

    /* Same point as the x64 KiInitializeKernel: the debugger (and with it
     * every DPRINT) becomes live before the executive starts. */
    KdInitSystem(0, LoaderBlock);
    KiLa64ReportProcessorFeatures();
    KiLa64SaveProcessorClock(Prcb);

    KiLa64StartupPhase = KiLa64StartupExecutiveEntered;
    ExpInitializeExecutive(0, LoaderBlock);

    /* Phase 0 created the phase-1 thread on the ready list. Dropping the boot
     * thread to the idle priority selects it as Prcb->NextThread; the idle
     * loop then performs the first real context switch. */
    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(InitialThread, 0);
    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
        InterlockedOr64((PLONG64)&KiIdleSummary, Prcb->SetMember);
    KiReleasePrcbLock(Prcb);

    KfRaiseIrql(HIGH_LEVEL);
    LoaderBlock->Prcb = 0;
    InitialThread->Priority = 0;

    /* Interrupt delivery stays masked until the HAL enables a source; the
     * idle loop unmasks around `idle 0` only once one exists. */
    KfLowerIrql(DISPATCH_LEVEL);
    InitialThread->WaitIrql = DISPATCH_LEVEL;
    KiIdleLoop();
}

VOID
NTAPI
KiInitMachineDependent(VOID)
{
    ULONG64 Pgdh, Pwcl, Pwch;
    ULONG64 Vector, Refill;
    PKPCR Pcr = KeGetPcr();
    LARGE_INTEGER Frequency;

    __asm__ __volatile__("csrrd %0, 0x1a\n\tcsrrd %1, 0x1c\n\tcsrrd %2, 0x1d"
                         : "=r"(Pgdh), "=r"(Pwcl), "=r"(Pwch) :: "memory");
    Vector = KiLa64ReadEentry();
    Refill = KiLa64ReadTlbRefillEntry();

    if ((Pcr->Prcb.Number >= (ULONG)(UCHAR)KeNumberProcessors) ||
        (KiProcessorBlock[Pcr->Prcb.Number] != &Pcr->Prcb) ||
        ((Pgdh & ~0xFFFULL) == 0) ||
        (Pwcl != MI_LA64_PWCL_4LEVEL_4K) ||
        (Pwch != MI_LA64_PWCH_4LEVEL_4K) ||
        (Vector != (ULONG64)(ULONG_PTR)KiLa64TrapEntry) ||
        (Refill != ((ULONG64)(ULONG_PTR)KiLa64TlbRefillEntry -
                    LOONGARCH64_LOADER_DIRECT_MAP_BASE)))
    {
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,
                     STATUS_INVALID_DEVICE_STATE,
                     (ULONG64)Pgdh,
                     Vector,
                     (ULONG64)Pcr);
    }

    /* The stable counter is the performance counter; publishing its
     * frequency is what lets user mode convert the QPC without a call. */
    Frequency.QuadPart = 0;
    KeQueryPerformanceCounter(&Frequency);
    if (Frequency.QuadPart != 0)
    {
        MmWriteableSharedUserData->QpcFrequency = Frequency.QuadPart;
        KeMemoryBarrier();
    }
}
