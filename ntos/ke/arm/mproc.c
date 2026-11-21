/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 multiprocessor support: secondary startup, IPIs, generic calls and freezing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kiarm.h"
#define NDEBUG
#include <debug.h>

#define KI_ARM_STARTUP_TAG       'pSiK'
#define KI_ARM_STARTUP_TIMEOUT   5000000UL
#define KI_ARM_FREEZE_TIMEOUT    2000000UL
#define KI_ARM_IPI_GENERIC       0x100
#define KI_ARM_FROZEN            1

/* Long-descriptor level 1 block (ARM ARM B3.6.1): AF, inner shareable, PL1 read/write. */
#define KI_ARM_BLOCK_VALID       0x1ULL
#define KI_ARM_BLOCK_ATTR(Index) ((ULONGLONG)(Index) << 2)
#define KI_ARM_BLOCK_SH_INNER    (3ULL << 8)
#define KI_ARM_BLOCK_AF          (1ULL << 10)
#define KI_ARM_BLOCK_1GB_MASK    0x000000FFC0000000ULL
#define KI_ARM_ROOT_ENTRIES      4
#define KI_ARM_ROOT_KERNEL_INDEX (ARM_LOADER_KSEG0_BASE >> 30)
#define KI_ARM_TTBR_ROOT_MASK    0x000000FFFFFFFFE0ULL

extern ULONG KiArmDcacheLineSize;

typedef struct DECLSPEC_ALIGN(32) _KI_ARM_STARTUP_BLOCK
{
    ULONGLONG TemporaryRoot[KI_ARM_ROOT_ENTRIES];
    ULONGLONG TemporaryRootPhysical;
    ULONGLONG SystemRoot;
    ULONG Mair0;
    ULONG Mair1;
    ULONG Ttbcr;
    ULONG Sctlr;
    ULONG Self;
    ULONG Continuation;
    ULONG Pcr;
    ULONG KernelStack;
    PKTHREAD Thread;
    PVOID DpcStack;
    PVOID PanicStack;
    ULONG Number;
    volatile LONG Started;
} KI_ARM_STARTUP_BLOCK, *PKI_ARM_STARTUP_BLOCK;

C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, TemporaryRoot) == KI_SB_TEMPORARY_ROOT);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, TemporaryRootPhysical) == KI_SB_TEMPORARY_ROOT_PA);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, SystemRoot) == KI_SB_SYSTEM_ROOT);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, Mair0) == KI_SB_MAIR0);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, Mair1) == KI_SB_MAIR1);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, Ttbcr) == KI_SB_TTBCR);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, Sctlr) == KI_SB_SCTLR);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, Self) == KI_SB_SELF);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, Continuation) == KI_SB_CONTINUATION);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, Pcr) == KI_SB_PCR);
C_ASSERT(FIELD_OFFSET(KI_ARM_STARTUP_BLOCK, KernelStack) == KI_SB_KERNEL_STACK);
C_ASSERT(sizeof(KI_ARM_STARTUP_BLOCK) <= PAGE_SIZE);

static KSPIN_LOCK KiArmGenericCallLock;
static PKIPI_BROADCAST_WORKER volatile KiArmGenericWorker;
static ULONG_PTR volatile KiArmGenericArgument;
static volatile LONG KiArmGenericCount;
static volatile LONG KiArmGenericArrived;
static volatile LONG KiArmGenericDeparted;
static volatile LONG KiArmGenericRound;
static PKTRAP_FRAME KiArmIpiTrapFrame[MAXIMUM_PROCESSORS];

/* Startup block physical addresses by core number, for firmware spin loops that pass only the core. */
ULONG KiArmSpinStartupBlocks[KI_ARM_SPIN_STARTUP_BLOCKS];

static PKPRCB volatile KiArmFreezeOwner;
static volatile LONG KiArmThawRequested;
static KAFFINITY KiArmFrozenSet;

static
ULONG
KiArmCountProcessors(
    _In_ KAFFINITY Set)
{
    ULONG Count = 0;

    for (; Set; Set &= Set - 1)
        Count++;
    return Count;
}

static
KAFFINITY
KiArmActiveSet(VOID)
{
    return (KAFFINITY)KeActiveProcessors.Bitmap[0];
}

static
ULONG
KiArmNormalMemoryAttribute(
    _In_ ULONG Mair0,
    _In_ ULONG Mair1)
{
    ULONGLONG Mair = ((ULONGLONG)Mair1 << 32) | Mair0;
    ULONG Index;

    /* Normal memory, inner and outer write-back read/write-allocate (ARM ARM B4.1.104). */
    for (Index = 0; Index < 8; Index++)
    {
        if (((Mair >> (Index * 8)) & 0xFF) == 0xFF)
            return Index;
    }
    return MAXULONG;
}

static
PULONGLONG
KiArmMapSystemRoot(
    _In_ ULONGLONG RootPhysical)
{
    ULONGLONG Kseg0Base = KeLoaderBlock->u.Arm.Kseg0PhysicalBase;

    if ((RootPhysical < Kseg0Base) || (RootPhysical >= Kseg0Base + ARM_LOADER_KSEG0_SIZE))
        return NULL;
    return (PULONGLONG)(ULONG_PTR)(RootPhysical - Kseg0Base + ARM_LOADER_KSEG0_BASE);
}

static
VOID
KiArmCleanToPointOfCoherency(
    _In_ PVOID Address,
    _In_ SIZE_T Length)
{
    ULONG_PTR Line, End;

    ASSERT(KiArmDcacheLineSize != 0);
    Line = (ULONG_PTR)Address & ~(ULONG_PTR)(KiArmDcacheLineSize - 1);
    End = (ULONG_PTR)Address + Length;
    KiArmDsb();
    for (; Line < End; Line += KiArmDcacheLineSize)
        KiArmWriteCp15(7, 0, 10, 1, Line);
    KiArmDsb();
}

static
BOOLEAN
KiArmPrepareStartupBlock(
    _Inout_ PKI_ARM_STARTUP_BLOCK Block,
    _In_ ULONG_PTR TrampolinePhysical)
{
    PULONGLONG SystemRoot;
    ULONG Attribute, Index, Identity;

    Block->Mair0 = KiArmReadCp15(10, 0, 2, 0);
    Block->Mair1 = KiArmReadCp15(10, 0, 2, 1);
    Block->Ttbcr = KiArmReadCp15(2, 0, 0, 2);
    Block->Sctlr = KiArmReadCp15(1, 0, 0, 0);
    Block->SystemRoot = (ULONGLONG)KiInitialProcess.Pcb.DirectoryTableBase;

    Attribute = KiArmNormalMemoryAttribute(Block->Mair0, Block->Mair1);
    Identity = (ULONG)(TrampolinePhysical >> 30);
    SystemRoot = KiArmMapSystemRoot(Block->SystemRoot & KI_ARM_TTBR_ROOT_MASK);
    if ((Attribute == MAXULONG) || (Identity >= KI_ARM_ROOT_KERNEL_INDEX) || !SystemRoot)
        return FALSE;

    /* The kernel half is the system root's; the trampoline's gigabyte maps to itself. */
    RtlZeroMemory(Block->TemporaryRoot, sizeof(Block->TemporaryRoot));
    for (Index = KI_ARM_ROOT_KERNEL_INDEX; Index < KI_ARM_ROOT_ENTRIES; Index++)
        Block->TemporaryRoot[Index] = SystemRoot[Index];
    Block->TemporaryRoot[Identity] = ((ULONGLONG)TrampolinePhysical & KI_ARM_BLOCK_1GB_MASK) |
                                     KI_ARM_BLOCK_AF | KI_ARM_BLOCK_SH_INNER |
                                     KI_ARM_BLOCK_ATTR(Attribute) | KI_ARM_BLOCK_VALID;
    Block->TemporaryRootPhysical = (ULONGLONG)MmGetPhysicalAddress(Block->TemporaryRoot).QuadPart;
    Block->Self = (ULONG)(ULONG_PTR)Block;
    Block->Continuation = (ULONG)(ULONG_PTR)KiArmSecondaryEntry;
    ASSERT(Block->Continuation & 1);
    return TRUE;
}

static
VOID
KiArmFreeStartupResources(
    _In_ PKI_ARM_STARTUP_BLOCK Block)
{
    if (Block->PanicStack)
        MmDeleteKernelStack(Block->PanicStack, FALSE);
    if (Block->DpcStack)
        MmDeleteKernelStack(Block->DpcStack, FALSE);
    if (Block->KernelStack)
        MmDeleteKernelStack((PVOID)(ULONG_PTR)Block->KernelStack, FALSE);
    if (Block->Thread)
        ExFreePoolWithTag(Block->Thread, KI_ARM_STARTUP_TAG);
    if (Block->Pcr)
        ExFreePoolWithTag((PVOID)(ULONG_PTR)Block->Pcr, KI_ARM_STARTUP_TAG);
    ExFreePoolWithTag(Block, KI_ARM_STARTUP_TAG);
}

static
PKI_ARM_STARTUP_BLOCK
KiArmAllocateStartupBlock(
    _In_ ULONG Number)
{
    PKI_ARM_STARTUP_BLOCK Block;
    PVOID Pcr, Thread;

    Block = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, KI_ARM_STARTUP_TAG);
    if (!Block)
        return NULL;
    ASSERT(((ULONG_PTR)Block & (PAGE_SIZE - 1)) == 0);
    RtlZeroMemory(Block, sizeof(*Block));
    Block->Number = Number;

    Pcr = ExAllocatePoolWithTag(NonPagedPool, ROUND_TO_PAGES(sizeof(KIPCR)), KI_ARM_STARTUP_TAG);
    Thread = ExAllocatePoolWithTag(NonPagedPool, sizeof(ETHREAD), KI_ARM_STARTUP_TAG);
    Block->Pcr = (ULONG)(ULONG_PTR)Pcr;
    Block->Thread = Thread;
    Block->KernelStack = (ULONG)(ULONG_PTR)MmCreateKernelStack(FALSE, 0);
    Block->DpcStack = MmCreateKernelStack(FALSE, 0);
    Block->PanicStack = MmCreateKernelStack(FALSE, 0);
    if (!Pcr || !Thread || !Block->KernelStack || !Block->DpcStack || !Block->PanicStack)
    {
        KiArmFreeStartupResources(Block);
        return NULL;
    }

    ASSERT(((ULONG_PTR)Pcr & (PAGE_SIZE - 1)) == 0);
    RtlZeroMemory(Thread, sizeof(ETHREAD));
    return Block;
}

VOID
NTAPI
KeStartAllProcessors(VOID)
{
    KPROCESSOR_STATE ProcessorState;
    PKI_ARM_STARTUP_BLOCK Block;
    ULONG_PTR TrampolinePhysical, SpinEntryPhysical;
    PHYSICAL_ADDRESS BlockPhysical;
    ULONG Number, Waited, Index;

    ASSERT(KeGetCurrentPrcb()->Number == 0);
    TrampolinePhysical = (ULONG_PTR)MmGetPhysicalAddress((PVOID)((ULONG_PTR)KiArmSecondaryTrampoline & ~1UL)).QuadPart;
    SpinEntryPhysical = (ULONG_PTR)MmGetPhysicalAddress((PVOID)((ULONG_PTR)KiArmSecondarySpinEntry & ~1UL)).QuadPart;

    for (Number = 1; Number < MAXIMUM_PROCESSORS; Number++)
    {
        ASSERT(KeProcessorCount == Number);
        Block = KiArmAllocateStartupBlock(Number);
        if (!Block)
            break;

        if (!KiArmPrepareStartupBlock(Block, TrampolinePhysical))
        {
            KiArmFreeStartupResources(Block);
            break;
        }

        BlockPhysical = MmGetPhysicalAddress(Block);
        ASSERT(BlockPhysical.HighPart == 0);
        KiArmCleanToPointOfCoherency(Block, sizeof(*Block));

        RtlZeroMemory(&ProcessorState, sizeof(ProcessorState));
        ProcessorState.ContextFrame.R0 = BlockPhysical.LowPart;
        ProcessorState.ContextFrame.Pc = (ULONG)TrampolinePhysical | 1;

        /* Firmware that passes no argument enters at the spin entry, which reads the block from its core's slot; one processor starts at a time, so every slot names this block. */
        for (Index = 0; Index < KI_ARM_SPIN_STARTUP_BLOCKS; Index++)
            KiArmSpinStartupBlocks[Index] = BlockPhysical.LowPart;
        KiArmCleanToPointOfCoherency(KiArmSpinStartupBlocks, sizeof(KiArmSpinStartupBlocks));
        ProcessorState.ContextFrame.R1 = (ULONG)SpinEntryPhysical | 1;
        if (!HalStartNextProcessor(KeLoaderBlock, &ProcessorState))
        {
            KiArmFreeStartupResources(Block);
            break;
        }

        for (Waited = 0; !Block->Started && (Waited < KI_ARM_STARTUP_TIMEOUT); Waited += 10)
            KeStallExecutionProcessor(10);
        if (!Block->Started)
        {
            /* FIXME: the processor may still enter later and use these resources, so they stay allocated */
            DPRINT1("Processor %lu did not start\n", Number);
            break;
        }
        ASSERT(KeProcessorCount == Number + 1);
    }

    HalAllProcessorsStarted();
}

DECLSPEC_NORETURN
VOID
NTAPI
KiArmStartSecondaryProcessor(
    _In_ PKI_ARM_STARTUP_BLOCK Block)
{
    PKIPCR Pcr = (PKIPCR)(ULONG_PTR)Block->Pcr;
    PKTHREAD Thread = Block->Thread;
    PKPROCESS Process = &KiInitialProcess.Pcb;
    ULONG Number = Block->Number;
    PKPRCB Prcb;

    ASSERT((PKIPCR)KeGetPcr() == Pcr);
    ASSERT(Number == KeProcessorCount);

    KiArmInitializePcr(Pcr, Thread, Number, Block->DpcStack, Block->PanicStack);
    KiArmWriteCp15(13, 0, 0, 3, (ULONG_PTR)Thread);
    KiArmIsb();
    Prcb = &Pcr->Prcb;
    KiArmIdentifyProcessor(Prcb);

    Thread->ApcState.Process = Process;
    KeInitializeThread(Process, Thread, NULL, NULL, NULL, NULL, NULL, (PVOID)(ULONG_PTR)Block->KernelStack);
    Thread->NextProcessor = Number;
    Thread->IdealProcessor = Number;
    Thread->Priority = HIGH_PRIORITY;
    Thread->State = Running;
#if (NTDDI_VERSION >= NTDDI_WIN7)
    Thread->Running = TRUE;
#endif
    RtlZeroMemory(&Thread->Affinity, sizeof(Thread->Affinity));
    KiThreadAffinityMask(Thread) = Prcb->SetMember;
    Thread->WaitIrql = DISPATCH_LEVEL;
    InterlockedOr((PLONG)&Process->ActiveProcessors, (LONG)Prcb->SetMember);

    KiProcessorBlock[Number] = Prcb;
    KeMemoryBarrier();
    KiSetProcessorCount(Number + 1);
    InterlockedOr((PLONG)&KiNode0.ProcessorMask, (LONG)Prcb->SetMember);

    PoInitializePrcb(Prcb);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    HalInitializeProcessor(Number, KeLoaderBlock);
    KiArmStartClock();
    KiActivateProcessor(Prcb);
    ExpInitializeExecutive(Number, KeLoaderBlock);

    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
        InterlockedOr((PLONG)&KiIdleSummary[0], (LONG)Prcb->SetMember);
    KiReleasePrcbLock(Prcb);

    Thread->Priority = 0;
    KeMemoryBarrier();
    InterlockedExchange(&Block->Started, TRUE);

    KfLowerIrql(DISPATCH_LEVEL);
    _enable();
    KiIdleLoop();
}

static
VOID
KiArmRunGenericWorker(
    _In_ BOOLEAN Initiator,
    _Out_opt_ PULONG_PTR Status)
{
    LONG Count = KiArmGenericCount;
    LONG Round = KiArmGenericRound;
    ULONG_PTR Result;

    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);
    ASSERT(Count >= 1);

    /* Every processor reaches the barrier before any of them runs the worker. */
    InterlockedIncrement(&KiArmGenericArrived);
    while (KiArmGenericArrived < Count)
        YieldProcessor();

    Result = KiArmGenericWorker(KiArmGenericArgument);
    if (Status)
        *Status = Result;

    /* No processor leaves IPI_LEVEL before the worker has run on all of them: the initiator ends the round once it has. */
    InterlockedIncrement(&KiArmGenericDeparted);
    if (Initiator)
    {
        while (KiArmGenericDeparted < Count)
            YieldProcessor();
        ASSERT(KiArmGenericRound == Round);
        InterlockedIncrement(&KiArmGenericRound);
    }
    else
    {
        while (KiArmGenericRound == Round)
            YieldProcessor();
    }
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    ULONG_PTR Status = 0;
    KAFFINITY Others;
    KIRQL OldIrql;

    ASSERT(Function != NULL);

    /* Interrupts stay enabled below IPI_LEVEL, so a competing initiator's request is still served. */
    OldIrql = KfRaiseIrql(SYNCH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(&KiArmGenericCallLock);

    Others = KiArmActiveSet() & ~KeGetCurrentPrcb()->SetMember;
    KiArmGenericWorker = Function;
    KiArmGenericArgument = Argument;
    KiArmGenericCount = (LONG)KiArmCountProcessors(Others) + 1;
    KiArmGenericArrived = 0;
    KiArmGenericDeparted = 0;
    KeMemoryBarrier();

    KfRaiseIrql(IPI_LEVEL);
    if (Others)
        KiIpiSend(Others, KI_ARM_IPI_GENERIC);
    KiArmRunGenericWorker(TRUE, &Status);
    KfLowerIrql(SYNCH_LEVEL);

    KiArmGenericWorker = NULL;
    KeReleaseSpinLockFromDpcLevel(&KiArmGenericCallLock);
    KfLowerIrql(OldIrql);
    return Status;
}

VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    KAFFINITY_EX Affinity;
    KAFFINITY Remaining;
    ULONG Number;

    ASSERT(IpiRequest != 0);
    for (Remaining = TargetSet, Number = 0; Remaining; Remaining >>= 1, Number++)
    {
        if ((Remaining & 1) && KiProcessorBlock[Number])
            InterlockedOr((PLONG)&KiProcessorBlock[Number]->RequestSummary, (LONG)IpiRequest);
    }
    KeMemoryBarrier();
    KeInitializeAffinityEx(&Affinity);
    Affinity.Bitmap[0] = TargetSet;
    HalRequestIpi(IpiAffinity, &Affinity);
}

static
VOID
KiArmFreezeTarget(
    _In_ PKPRCB Prcb)
{
    PKPRCB Owner = KiArmFreezeOwner;
    PKTRAP_FRAME TrapFrame = KiArmIpiTrapFrame[Prcb->Number];
    PCONTEXT Context = &Prcb->ProcessorState.ContextFrame;
    CONTEXT Saved;

    if (!Owner || (Owner == Prcb))
        return;

    /* What this processor was doing goes where the debugger looks for it, before the processor counts as frozen. */
    Context->ContextFlags = CONTEXT_FULL;
    if (TrapFrame)
        KeTrapFrameToContext(TrapFrame, NULL, Context);
    else
        RtlCaptureContext(Context);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    Saved = *Context;
    KeMemoryBarrier();

    InterlockedExchange((PLONG)&Prcb->IpiFrozen, KI_ARM_FROZEN);
    while (!KiArmThawRequested)
        YieldProcessor();

    /* Registers the debugger changed while it looked at this processor are the ones it resumes with. */
    if (TrapFrame && !RtlEqualMemory(&Saved, Context, sizeof(Saved)))
    {
        KeContextToTrapFrame(Context, NULL, TrapFrame, CONTEXT_FULL,
                             KiIsUserModeTrap(TrapFrame) ? UserMode : KernelMode);
    }
    InterlockedExchange((PLONG)&Prcb->IpiFrozen, 0);
}

/* The inter-processor interrupt: what it interrupted is kept at hand for a freeze request to save. */
VOID
NTAPI
KiArmServiceIpi(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG Number = KeGetCurrentPrcb()->Number;
    PKTRAP_FRAME Outer;

    ASSERT(Number < MAXIMUM_PROCESSORS);
    ASSERT(KeGetCurrentIrql() >= IPI_LEVEL);

    Outer = KiArmIpiTrapFrame[Number];
    KiArmIpiTrapFrame[Number] = TrapFrame;
    KiIpiProcessRequests();
    KiArmIpiTrapFrame[Number] = Outer;
}

VOID
NTAPI
KiIpiProcessRequests(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Summary;

    ASSERT(KeGetCurrentIrql() >= IPI_LEVEL);
    Summary = (ULONG)InterlockedExchange((PLONG)&Prcb->RequestSummary, 0);

    if (Summary & KI_ARM_IPI_GENERIC)
        KiArmRunGenericWorker(FALSE, NULL);
    if (Summary & IPI_FREEZE)
        KiArmFreezeTarget(Prcb);
    if (Summary & IPI_APC)
        HalRequestSoftwareInterrupt(APC_LEVEL);
    if (Summary & IPI_DPC)
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
}

BOOLEAN
NTAPI
KiIpiServiceRoutine(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    KiIpiProcessRequests();
    return TRUE;
}

static
BOOLEAN
KiArmWaitForFrozenState(
    _In_ KAFFINITY Set,
    _In_ ULONG State)
{
    KAFFINITY Remaining;
    ULONG Number, Waited;

    for (Remaining = Set, Number = 0; Remaining; Remaining >>= 1, Number++)
    {
        if (!(Remaining & 1))
            continue;
        for (Waited = 0; (KiProcessorBlock[Number]->IpiFrozen != State) && (Waited < KI_ARM_FREEZE_TIMEOUT); Waited += 10)
            KeStallExecutionProcessor(10);
        if (KiProcessorBlock[Number]->IpiFrozen != State)
            return FALSE;
    }
    return TRUE;
}

VOID
NTAPI
KxFreezeExecution(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KAFFINITY Targets;

    ASSERT(KeGetCurrentIrql() == HIGH_LEVEL);
    ASSERT(KiArmFreezeOwner == NULL);

    InterlockedExchange(&KiArmThawRequested, FALSE);
    KiArmFreezeOwner = Prcb;
    KeMemoryBarrier();

    Targets = KiArmActiveSet() & ~Prcb->SetMember;
    KiArmFrozenSet = Targets;
    if (!Targets)
        return;

    KiIpiSend(Targets, IPI_FREEZE);
    /* FIXME: a processor that does not take the request in time keeps running while the system is frozen */
    KiArmWaitForFrozenState(Targets, KI_ARM_FROZEN);
}

BOOLEAN
NTAPI
KxIsFreezeOwner(
    _In_ PKPRCB Prcb)
{
    return KiArmFreezeOwner == Prcb;
}

VOID
NTAPI
KxThawExecution(VOID)
{
    ASSERT(KiArmFreezeOwner == KeGetCurrentPrcb());

    InterlockedExchange(&KiArmThawRequested, TRUE);
    if (KiArmFrozenSet)
        KiArmWaitForFrozenState(KiArmFrozenSet, 0);
    KiArmFrozenSet = 0;
    KiArmFreezeOwner = NULL;
    KeMemoryBarrier();
}
