/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel core internals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "arch/ke.h"

#ifndef KI_USER_MODE_CALLBACK_ENTRY
#define KI_USER_MODE_CALLBACK_ENTRY KiCallUserMode
#endif

#ifndef KiArchCallUserMode
#define KiArchCallUserMode(Frame, Result, ResultLength) ((VOID)(Frame), KiCallUserMode((Result), (ResultLength)))
#endif

#ifndef KiArchSetGuiServiceTable
#define KiArchSetGuiServiceTable(Thread) ((VOID)(Thread))
#endif

#ifndef KiArchSetInitialServiceTable
#define KiArchSetInitialServiceTable(Thread) ((VOID)(Thread))
#endif

#ifndef KiArchIdleProcessorNeedsWake
#define KiArchIdleProcessorNeedsWake(Prcb) ((VOID)(Prcb), TRUE)
#endif

#ifndef KiArchIdleLoopPollsForWork
#define KiArchIdleLoopPollsForWork() TRUE
#endif

#ifndef KiArchPublishSystemCallStubs
#define KiArchPublishSystemCallStubs(DllBase) ((VOID)(DllBase), STATUS_SUCCESS)
#endif

#ifndef KiArchSystemDllSListMarkersPresent
#define KiArchSystemDllSListMarkersPresent(End, Fault, Resume) \
    ((End) != NULL && (Fault) != NULL && (Resume) != NULL)
#endif

#ifndef KiArchExceptionContextFlags
#define KiArchExceptionContextFlags(PreviousMode) ((VOID)(PreviousMode), CONTEXT_ALL)
#endif

#ifndef KiArchBeginUserExceptionDispatch
#define KiArchBeginUserExceptionDispatch(TrapFrame) ((VOID)(TrapFrame))
#endif

#ifndef KiArchPrepareUserBreakpoint
#define KiArchPrepareUserBreakpoint(ExceptionRecord, TrapFrame) ((VOID)(ExceptionRecord), (VOID)(TrapFrame))
#endif

#ifndef KiArchFinishExceptionContext
#define KiArchFinishExceptionContext(TrapFrame, Context) ((VOID)(TrapFrame), (VOID)(Context))
#endif

#ifndef KiArchDebugPortExceptionCode
#define KiArchDebugPortExceptionCode(TrapFrame, Code) ((VOID)(TrapFrame), (Code))
#endif

#ifndef KiArchReportUnhandledKernelFault
#define KiArchReportUnhandledKernelFault(TrapFrame) ((VOID)(TrapFrame))
#endif

#ifndef KiArchContinueExtendedState
/* TODO: restore the extended processor state (CONTEXT_XSTATE) of the context */
#define KiArchContinueExtendedState(SourceContext, ContextFlags, PreviousMode) \
    ((VOID)(SourceContext), (VOID)(ContextFlags), (VOID)(PreviousMode), STATUS_SUCCESS)
#endif

#ifndef KI_ARCH_DPC_REQUEST_STATE
FORCEINLINE
BOOLEAN
KiIsDpcInterruptRequested(
    _In_ PKPRCB Prcb)
{
    return (BOOLEAN)(Prcb->DpcInterruptRequested != FALSE);
}

FORCEINLINE
VOID
KiSetDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = TRUE;
}

FORCEINLINE
VOID
KiClearDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = FALSE;
}

FORCEINLINE
VOID
KiSetDpcPresent(
    _Inout_ PKPRCB Prcb)
{
    UNREFERENCED_PARAMETER(Prcb);
}

FORCEINLINE
VOID
KiClearDpcRequestState(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = FALSE;
}

FORCEINLINE
BOOLEAN
KiIsDpcThreadActive(
    _In_ PKPRCB Prcb)
{
    return (BOOLEAN)(Prcb->DpcThreadActive != FALSE);
}

FORCEINLINE
BOOLEAN
KiIsDpcThreadRequested(
    _In_ PKPRCB Prcb)
{
    return (BOOLEAN)(Prcb->DpcThreadRequested != FALSE);
}

FORCEINLINE
VOID
KiSetDpcThreadActive(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcThreadActive = TRUE;
}

FORCEINLINE
VOID
KiSetDpcThreadRequested(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcThreadRequested = TRUE;
}

FORCEINLINE
VOID
KiClearDpcThreadState(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcThreadActive = FALSE;
    Prcb->DpcThreadRequested = FALSE;
}
#endif

#ifndef KI_CALLOUT_STACK_RESERVE
#define KI_CALLOUT_STACK_RESERVE 0
#endif

#ifndef KI_USER_CALLOUT_FRAME
#define KI_USER_CALLOUT_FRAME UCALLOUT_FRAME
#endif

#ifndef KI_CALLOUT_RESULT
#define KI_CALLOUT_RESULT(Frame) ((Frame)->OutputBuffer)
#define KI_CALLOUT_RESULT_LENGTH(Frame) ((Frame)->OutputLength)
#define KI_CALLOUT_RETURN_STACK(Frame) (Frame)
#endif

#ifndef KiArchPrepareInterruptDispatch
#define KiArchPrepareInterruptDispatch(Interrupt) ((VOID)(Interrupt))
#endif

#ifndef KiArchCalloutSaveUserState
#define KiArchCalloutSaveUserState(Thread) ((VOID)(Thread), (ULONG_PTR)0)
#define KiArchCalloutRestoreUserState(Thread, State) ((VOID)(Thread), (VOID)(State))
#endif

#ifndef KiArchLastChanceKernelDebugger
#define KiArchLastChanceKernelDebugger(ExceptionRecord, Context) ((VOID)(ExceptionRecord), (VOID)(Context), FALSE)
#endif

typedef enum _KI_BOOT_MARK
{
    BootMarkBootStructures,
    BootMarkProcessor,
    BootMarkModuleList,
    BootMarkExceptions,
    BootMarkVaLayout,
    BootMarkHalProcessorEntry,
    BootMarkHalProcessor,
    BootMarkDebugger,
    BootMarkMachineDependent,
    BootMarkExtendedState,
    BootMarkBootStack,
    BootMarkPrcbLocks,
    BootMarkApcLevel,
    BootMarkExecutiveEntry,
    BootMarkNls,
    BootMarkHal,
    BootMarkPostHal,
    BootMarkEarlyMemory,
    BootMarkPoolLists,
    BootMarkMemory,
    BootMarkObjects,
    BootMarkExecutive,
    BootMarkSecurity,
    BootMarkProcesses,
    BootMarkPower,
    BootMarkExecutiveDone,
    BootMarkCpuFrequency,
    BootMarkIdleLoop,
    BootMarkMaximum
} KI_BOOT_MARK;

typedef enum _KI_BOOT_READOUT
{
    BootReadoutProcessorState,
    BootReadoutException,
    BootReadoutFatalTrap,
    BootReadoutMachineCheck,
    BootReadoutBugCheck
} KI_BOOT_READOUT;

#ifndef KiArchBootMark

#define KiArchBootMark(Mark) ((VOID)(Mark))
#endif

#ifndef KiArchBootReadout
#define KiArchBootReadout(Source, Code, A, B, C, D) ((VOID)(Source))
#endif

#ifndef KiPrcbGroup
#define KiPrcbGroup(Prcb) ((Prcb)->Group)
#endif

#ifndef KiPrcbPoolLookaside
#define KiPrcbPoolLookaside(Prcb, Paged, Index) \
    ((Paged) ? &(Prcb)->PPPagedLookasideList[(Index)] : &(Prcb)->PPNPagedLookasideList[(Index)])
#endif

#ifndef KI_DPC_TARGET_PROCESSOR_OFFSET
#define KI_DPC_TARGET_PROCESSOR_OFFSET MAXIMUM_PROCESSORS
#endif

typedef struct _KI_SAMPLE_MAP
{
    LARGE_INTEGER PerfStart;
    LARGE_INTEGER PerfEnd;
    LONGLONG PerfDelta;
    LARGE_INTEGER PerfFreq;
    LONGLONG TSCStart;
    LONGLONG TSCEnd;
    LONGLONG TSCDelta;
    ULONG MHz;
} KI_SAMPLE_MAP, *PKI_SAMPLE_MAP;

typedef PCHAR
(NTAPI *PKE_BUGCHECK_UNICODE_TO_ANSI)(
    IN PUNICODE_STRING Unicode,
    IN PCHAR Ansi,
    IN ULONG Length
);

extern PVOID KeUserApcDispatcher;

extern PVOID KeUserCallbackDispatcher;

extern PVOID KeUserExceptionDispatcher;

extern PVOID KeUserExceptionDispatcherWorker;

extern PVOID KeUserEmulationDispatcher;

extern PVOID KeRaiseUserExceptionDispatcher;

#if (NTDDI_VERSION < NTDDI_WIN8)
VOID
NTAPI
KeQuerySystemTimePrecise(
    _Out_ PLARGE_INTEGER CurrentTime);
#endif

extern LARGE_INTEGER KeBootTime;

typedef struct _KI_HIGH_RESOLUTION_DEADLINE
{
    LIST_ENTRY Link;
    ULONGLONG DueTime;
    BOOLEAN Armed;
} KI_HIGH_RESOLUTION_DEADLINE, *PKI_HIGH_RESOLUTION_DEADLINE;

extern volatile ULONG KiTickPhase;

extern ULONGLONG KeBootTimeBias;

extern BOOLEAN ExCmosClockIsSane;

extern USHORT KeProcessorArchitecture;

extern USHORT KeProcessorLevel;

extern USHORT KeProcessorRevision;

extern ULONG64 KeFeatureBits;

extern KAFFINITY KeActiveProcessors;

extern ULONG KeBugCheckActive;

extern PKPRCB KiProcessorBlock[];

#ifdef KI_CORE_PARKING
extern KSCHEDULER_SUBNODE KiNode0SubNode;
#endif

extern KAFFINITY KiHeteroEfficientSet;

extern KAFFINITY KiHeteroPerformanceSet;

#ifndef KI_MAX_NUMA_NODES
#define KI_MAX_NUMA_NODES 1
#endif

#define KI_MAX_USER_CALLBACK_NESTING 31

#ifndef KiPrcbTimerTableLockQueue
#define KiPrcbTimerTableLockQueue(Prcb, Index) (&(Prcb)->LockQueue[LockQueueTimerTableLock + (Index)])
#endif

#ifdef CONFIG_SMP
extern ULONG KeMaximumProcessors;
extern ULONG KeNumprocSpecified;
extern ULONG KeBootprocSpecified;
#endif

extern KNODE KiNode0;

extern PKNODE KeNodeBlock[KI_MAX_NUMA_NODES];

extern UCHAR KeNumberNodes;

extern UCHAR KeProcessNodeSeed;

typedef struct _KI_CACHE_RECORD
{
    CACHE_DESCRIPTOR Descriptor;
    KAFFINITY ProcessorSet;
} KI_CACHE_RECORD, *PKI_CACHE_RECORD;

#define KI_MAX_CACHE_RECORDS 256

typedef enum _KI_TOPOLOGY_LEVEL
{
    KiTopologyCore,
    KiTopologyModule,
    KiTopologyDie,
    KiTopologyPackage,
    KiTopologyLevels
} KI_TOPOLOGY_LEVEL;

typedef struct _KI_PROCESSOR_DOMAINS
{
    ULONG Count[KiTopologyLevels];
    KAFFINITY Sets[KiTopologyLevels][MAXIMUM_PROCESSORS];
} KI_PROCESSOR_DOMAINS, *PKI_PROCESSOR_DOMAINS;

extern ETHREAD KiInitialThread;

extern EPROCESS KiInitialProcess;

extern PULONG KiInterruptTemplateObject;

extern ULONG KiUnexpectedEntrySize;

extern ULONG_PTR KiDoubleFaultStack;

extern ULONG KiMaximumDpcQueueDepth;

extern ULONG KiMinimumDpcRate;

extern ULONG KiAdjustDpcThreshold;

extern ULONG KiIdealDpcRate;

extern ULONG KeThreadDpcEnable;

extern LARGE_INTEGER KiTimeIncrementReciprocal;

extern UCHAR KiTimeIncrementShiftCount;

extern ULONG KiTimeLimitIsrMicroseconds;

extern ULONG KiServiceLimit;

extern LIST_ENTRY KeBugcheckCallbackListHead, KeBugcheckReasonCallbackListHead;

extern KSPIN_LOCK BugCheckCallbackLock;

extern KDPC KiTimerExpireDpc;

extern KTIMER_TABLE_ENTRY KiTimerTableListHead[TIMER_TABLE_SIZE];

extern FAST_MUTEX KiGenericCallDpcMutex;

extern LIST_ENTRY KiProfileListHead, KiProfileSourceListHead;

extern KSPIN_LOCK KiProfileLock;

extern LIST_ENTRY KiProcessListHead;

extern LIST_ENTRY KiProcessInSwapListHead, KiProcessOutSwapListHead;

extern LIST_ENTRY KiStackInSwapListHead;

extern KEVENT KiSwapEvent;

extern KAFFINITY KiIdleSummary;

extern PVOID KeUserApcDispatcher;

extern PVOID KeUserCallbackDispatcher;

extern PVOID KeUserExceptionDispatcher;

extern PVOID KeRaiseUserExceptionDispatcher;

extern ULONG KeTimeIncrement;

extern ULONG KeTimeAdjustment;

extern BOOLEAN KiTimeAdjustmentEnabled;

extern BOOLEAN KiRelaxedTiming;

extern LONG KiTickOffset;

extern ULONG KiFreezeFlag;

extern ULONG KiDPCTimeoutSeconds;

extern ULONG KiDPCWatchdogSeconds;

extern PGDI_BATCHFLUSH_ROUTINE KeGdiFlushUserBatch;

extern ULONGLONG BootCycles, BootCyclesEnd;


extern VOID __cdecl KiInterruptTemplate(VOID);

#define PRIORITY_MASK(Priority) (1UL << (Priority))

#define TIMER_OR_EVENT_TYPE 0x7L

#define TIMER_WAIT_BLOCK 0x3L

#define KTHREAD_ALERTED_BY_THREAD_ID_BIT 4

typedef enum _KI_BALANCE_REASON
{
    KiBalanceWakePlacement = 1,
    KiBalanceIdle,
    KiBalanceQuantum
} KI_BALANCE_REASON;

#ifdef CONFIG_SMP
_Requires_lock_not_held_(Target->PrcbLock)
BOOLEAN
FASTCALL
KiBalanceReadyQueues(
    IN PKPRCB Target,
    IN KI_BALANCE_REASON Reason
);
#endif

#ifdef CONFIG_SMP
CODE_SEG("INIT")
VOID
NTAPI
KiExpandInitialProcessAffinities(
    VOID
);
#endif

#ifdef _M_ARM64
VOID
NTAPI
KiChpeSuspendCheckpoint(
    IN PKTHREAD Thread
);

VOID
NTAPI
KiChpeContinueToEmulation(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame
);

FORCEINLINE
BOOLEAN
KiChpeClearDeferredSuspend(
    IN PKTHREAD Thread)
{
    if (Thread->ChpeSuspendDeferred)
    {
        Thread->ChpeSuspendDeferred = FALSE;
        return FALSE;
    }
    return TRUE;
}
#else
#define KiChpeClearDeferredSuspend(Thread) TRUE
#endif

#ifdef _WIN64

VOID
KiDisplayBugCheckRegisterState(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context);
#endif

ULONG64
KiReadCycleCounter(VOID);

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
VOID
NTAPI
KiChargeThreadCycleTime(
    IN OUT PKPRCB Prcb,
    IN OUT PKTHREAD Thread);
#endif

#ifdef _WIN64
VOID
NTAPI
KiInitializeWow64ThreadContext(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Eip,
    _In_ ULONG Eax,
    _In_ ULONG_PTR CpuArea
);
#endif

#ifndef _M_AMD64
VOID
FASTCALL
KiInterruptDispatch(
    IN PKTRAP_FRAME TrapFrame,
    IN PKINTERRUPT Interrupt
);
#endif

_IRQL_requires_min_(DISPATCH_LEVEL)
_Acquires_nonreentrant_lock_(*LockHandle->Lock)
_Acquires_exclusive_lock_(*LockHandle->Lock)
VOID
FASTCALL
KeAcquireQueuedSpinLockAtDpcLevel(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue
);

_IRQL_requires_min_(DISPATCH_LEVEL)
_Releases_nonreentrant_lock_(*LockHandle->Lock)
_Releases_exclusive_lock_(*LockHandle->Lock)
VOID
FASTCALL
KeReleaseQueuedSpinLockFromDpcLevel(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue
);

#ifdef CONFIG_SMP
ULONG
NTAPI
KiFindIdealProcessor(
    _In_ KAFFINITY ProcessorSet,
    _In_ UCHAR OriginalIdealProcessor);
#endif

VOID
FASTCALL
IopSignalWaitCompletionPacket(
    PKWAIT_BLOCK WaitBlock);

VOID
NTAPI
KeContextToTrapFrame(
    PCONTEXT Context,
    PKEXCEPTION_FRAME ExeptionFrame,
    PKTRAP_FRAME TrapFrame,
    ULONG ContextFlags,
    KPROCESSOR_MODE PreviousMode);

VOID
NTAPI
KeTrapFrameToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PCONTEXT Context);

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID);

VOID
NTAPI
KiRestoreProcessorControlState(
    _In_ PKPROCESSOR_STATE ProcessorState);

VOID
NTAPI
KiRestoreProcessorState(
    _Out_ PKTRAP_FRAME TrapFrame,
    _Out_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState);

VOID
NTAPI
KiSaveProcessorState(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

BOOLEAN
FASTCALL
KiSwapContext(
    _In_ KIRQL WaitIrql,
    _In_ PKTHREAD CurrentThread);

CODE_SEG("INIT")
VOID
NTAPI
KeInitExceptions(VOID);

VOID
NTAPI
KiDispatchException(
    PEXCEPTION_RECORD ExceptionRecord,
    PKEXCEPTION_FRAME ExceptionFrame,
    PKTRAP_FRAME Tf,
    KPROCESSOR_MODE PreviousMode,
    BOOLEAN SearchFrames);

BOOLEAN
KiDispatchExceptionToUser(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD ExceptionRecord);

VOID
KiPrepareUserDebugData(VOID);

BOOLEAN
KiDpcWorkPending(
    _In_ PKPRCB Prcb);

VOID
KiFinishDispatchInterrupt(
    _Inout_ PKPRCB Prcb);

VOID
KiSetupUserCalloutFrame(
    _Out_ KI_USER_CALLOUT_FRAME *UserCalloutFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ApiNumber,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength);

VOID
KiLeaveCallout(
    _Inout_ PKTHREAD Thread,
    _In_ PKCALLOUT_FRAME CalloutFrame,
    _Inout_ PKTRAP_FRAME CallerFrame,
    _In_ NTSTATUS CallbackStatus);

DECLSPEC_NORETURN
VOID
FASTCALL
KiSystemFatalException(
    _In_ ULONG ExceptionCode,
    _In_ PKTRAP_FRAME TrapFrame);

VOID
NTAPI
KxFreezeExecution(VOID);

BOOLEAN
NTAPI
KxIsFreezeOwner(
    _In_ PKPRCB Prcb);

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex);

VOID
NTAPI
KxThawExecution(VOID);

BOOLEAN
NTAPI
KiSetClockDeadline(
    ULONGLONG InterruptTime);

VOID
FASTCALL
KiIpiSend(
    KAFFINITY TargetSet,
    ULONG IpiRequest);

CODE_SEG("INIT")
VOID
NTAPI
KiInitMachineDependent(VOID);

DECLSPEC_NORETURN
CODE_SEG("INIT")
VOID
NTAPI
KiSystemStartup(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
VOID
NTAPI
KeStartAllProcessors(VOID);

BOOLEAN
NTAPI
KiUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN CallerFrame);

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID);

VOID
NTAPI
KiSwapProcess(
    struct _KPROCESS *NewProcess,
    struct _KPROCESS *OldProcess);

VOID
NTAPI
KiInitializeContextThread(
    PKTHREAD Thread,
    PKSYSTEM_ROUTINE SystemRoutine,
    PKSTART_ROUTINE StartRoutine,
    PVOID StartContext,
    PCONTEXT Context);

PVOID
NTAPI
KeSwitchKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit);

VOID
FASTCALL
KiCallOnKernelStack(
    _In_ PVOID Context,
    _In_ PVOID StackTop);

VOID
NTAPI
KiInitializeUserApc(
    _In_ PKEXCEPTION_FRAME Reserved,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2);

NTSTATUS
NTAPI
KiCallUserMode(
    _In_ PVOID *OutputBuffer,
    _In_ PULONG OutputLength);

DECLSPEC_NORETURN
VOID
FASTCALL
KiCallbackReturn(
    _In_ PVOID Stack,
    _In_ NTSTATUS Status);

VOID
FASTCALL
KeZeroPages(
    _In_ PVOID Address,
    _In_ ULONG Size);

BOOLEAN
NTAPI
KeDisableThreadApcQueueing(
    _In_ PKTHREAD Thread);

PLIST_ENTRY
NTAPI
KeFlushQueueApc(
    _In_ PKTHREAD Thread,
    _In_ KPROCESSOR_MODE PreviousMode);

BOOLEAN
NTAPI
KeRemoveQueueApc(
    PKAPC Apc);

VOID
NTAPI
KiCheckForKernelApcDelivery(VOID);

VOID
FASTCALL
KiInsertQueueApc(
    _In_ PKAPC Apc,
    _In_ KPRIORITY PriorityBoost);

VOID
NTAPI
KiMoveApcState(
    PKAPC_STATE OldState,
    PKAPC_STATE NewState);

VOID
NTAPI
KiSchedulerApcTerminate(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2);

VOID
NTAPI
KeFlushCurrentTb(VOID);

VOID
NTAPI
KiQueryProcessorDomains(
    _Out_ PKI_PROCESSOR_DOMAINS Domains);

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeMachineType(VOID);

VOID
NTAPI
KeInitInterrupts(VOID);

VOID
NTAPI
KiIpiSendPacket(
    _In_ KAFFINITY TargetProcessors,
    _In_ PKIPI_WORKER WorkerFunction,
    _In_ PKIPI_BROADCAST_WORKER BroadcastFunction,
    _In_ ULONG_PTR Context,
    _In_ PULONG Count);

VOID
FASTCALL
KiIpiSignalPacketDone(
    _In_ PKIPI_CONTEXT PacketContext);

VOID
NTAPI
KeBalanceSetManager(
    _In_ PVOID Context);

VOID
NTAPI
KeSetSystemTime(
    _In_ PLARGE_INTEGER NewSystemTime,
    _Out_ PLARGE_INTEGER OldSystemTime,
    _In_ BOOLEAN FixInterruptTime,
    _In_ PLARGE_INTEGER HalTime);

VOID
NTAPI
KeUpdateRunTime(
    PKTRAP_FRAME TrapFrame,
    KIRQL Irql);

VOID
NTAPI
KiArmHighResolutionDeadline(
    _Inout_ PKI_HIGH_RESOLUTION_DEADLINE Deadline,
    _In_ ULONGLONG DueTime);

VOID
NTAPI
KiDisarmHighResolutionDeadline(
    _Inout_ PKI_HIGH_RESOLUTION_DEADLINE Deadline);

VOID
NTAPI
KiFreezeInterruptTime(VOID);

ULONGLONG
NTAPI
KiQueryInterruptTimePrecise(
    PULONG64 QpcTimeStamp,
    BOOLEAN Unbiased);

ULONG
NTAPI
KiQueryRequestedTimeIncrement(VOID);

VOID
NTAPI
KiQueryTimeAdjustment(
    _Out_ PULONG Adjustment,
    _Out_ PBOOLEAN Enabled);

VOID
NTAPI
KiResumeClockTime(
    ULONGLONG SleepTime);

VOID
NTAPI
KiSetRequestedTimeIncrement(
    _In_ ULONG Increment);

VOID
NTAPI
KiSetTimeAdjustment(
    ULONG Adjustment,
    BOOLEAN Enabled);

VOID
NTAPI
KiSuspendClockTime(VOID);

VOID
NTAPI
KiThawInterruptTime(VOID);

VOID
FASTCALL
KiUpdateClockTime(
    _In_ PKPRCB Prcb,
    _In_ ULONG Increment);

VOID
NTAPI
KiUpdateIdleRunTime(
    PKTRAP_FRAME TrapFrame,
    ULONG Ticks);

VOID
NTAPI
KiUpdateRunTime(
    PKTRAP_FRAME TrapFrame,
    KIRQL Irql,
    ULONG Ticks);

ULONGLONG
NTAPI
KiUpdateSharedTime(
    ULONG Increment,
    BOOLEAN UpdateSystem);

VOID
FASTCALL
KiCheckProcessorUnpark(
    _In_ PKTHREAD Thread);

KAFFINITY
FASTCALL
KiGetNonParkedProcessorSet(VOID);

KAFFINITY
FASTCALL
KiHeteroPreferredProcessorSet(
    _In_ PKTHREAD Thread,
    _In_ KAFFINITY ProcessorSet);

VOID
NTAPI
KiInitializeProcessorEfficiency(VOID);

BOOLEAN
FASTCALL
KiIsProcessorParked(
    _In_ PKPRCB Prcb);

BOOLEAN
FASTCALL
KiShouldVacateProcessor(
    _In_ PKTHREAD Thread,
    _In_ PKPRCB Prcb);

BOOLEAN
FASTCALL
KiThreadPrefersEfficiency(
    _In_ PKTHREAD Thread);

PCHAR
NTAPI
KeBugCheckUnicodeToAnsi(
    _In_ PUNICODE_STRING Unicode,
    _Out_ PCHAR Ansi,
    _In_ ULONG Length);

DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheckWithTf(
    ULONG BugCheckCode,
    ULONG_PTR BugCheckParameter1,
    ULONG_PTR BugCheckParameter2,
    ULONG_PTR BugCheckParameter3,
    ULONG_PTR BugCheckParameter4,
    PKTRAP_FRAME Tf);

VOID
NTAPI
KeDumpStackFrames(
    PULONG Frame);

VOID
NTAPI
KeRosDumpStackFrames(
    PULONG_PTR Frame,
    ULONG FrameCount);

VOID
KiDisplayAndLogBugCheckString(
    _In_z_ PCSTR String);

BOOLEAN
NTAPI
KiHandleNmi(VOID);

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeBugCheck(VOID);

BOOLEAN
KiIsBugCheckEntryOwner(
    PKPRCB Prcb);

PVOID
NTAPI
KiPcToFileHeader(
    _In_ PVOID Pc,
    _Out_ PLDR_DATA_TABLE_ENTRY *LdrEntry,
    _In_ BOOLEAN DriversOnly,
    _Out_ PBOOLEAN InKernel);

PVOID
NTAPI
KiRosPcToUserFileHeader(
    _In_ PVOID Pc,
    _Out_ PLDR_DATA_TABLE_ENTRY *LdrEntry);

BOOLEAN
NTAPI
KiUnwindFrameRecord(
    _Inout_ PCONTEXT Context);

VOID
FASTCALL
KiExitDispatcher(
    KIRQL OldIrql);

VOID
FASTCALL
KiEndThreadWait(
    _Inout_ PKTHREAD Thread);

VOID
FASTCALL
KiUnlinkWaitBlocks(
    _In_ PKTHREAD Thread);

VOID
FASTCALL
KiUnwaitThread(
    _In_ PKTHREAD Thread,
    _In_ LONG_PTR WaitStatus,
    _In_ KPRIORITY Increment);

VOID
FASTCALL
KiWaitTest(
    PVOID Object,
    KPRIORITY Increment);

PKTHREAD
FASTCALL
KiLockCommittedWaiter(
    _Inout_ PKWAIT_BLOCK WaitBlock);

VOID
FASTCALL
KiUnlinkWaitBlock(
    _Inout_ PKWAIT_BLOCK WaitBlock);

BOOLEAN
FASTCALL
KiIsDpcThreadBusy(
    _In_ PKPRCB Prcb,
    _In_ PKTHREAD Thread);

VOID
FASTCALL
KiRetireDpcList(
    _In_ PKPRCB Prcb);

VOID
NTAPI
KiStartDpcThreads(VOID);

NTSTATUS
NTAPI
KiContinue(
    _In_ PCONTEXT Context,
    _In_ PCONTEXT SourceContext,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame);

NTSTATUS
NTAPI
KiRaiseException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ BOOLEAN SearchFrames);

BOOLEAN
NTAPI
KeFreezeExecution(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
KeThawExecution(
    _In_ BOOLEAN Enable);

BOOLEAN
KiHasFreezeFailure(VOID);

VOID
KiSetFreezeFailure(VOID);

BOOLEAN
FASTCALL
KeInvalidAccessAllowed(
    _In_opt_ PVOID TrapInformation);

VOID
FASTCALL
KiChainedDispatch(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKINTERRUPT Interrupt);

VOID
NTAPI
Ke386SetIOPL(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
KeInitSystem(VOID);

NTSTATUS
NTAPI
KiCallUserModeWithStack(
    _In_opt_ PKTRAP_FRAME Frame,
    _Out_ PVOID *Result,
    _Out_ PULONG ResultLength);

CODE_SEG("INIT")
LARGE_INTEGER
NTAPI
KiComputeReciprocal(
    _In_ LONG Divisor,
    _Out_ PUCHAR Shift);

VOID
NTAPI
KiDeleteCalloutStacks(
    _Inout_ PKTHREAD Thread);

CODE_SEG("INIT")
VOID
NTAPI
KiInitSpinLocks(
    _In_ PKPRCB Prcb,
    _In_ CCHAR Number);

CODE_SEG("INIT")
VOID
NTAPI
KiInitSystem(VOID);

UCHAR
NTAPI
KiNxPolicyFromLoadOptions(
    _In_opt_ PCSTR LoadOptions,
    _Out_ PULONG NxFeatureBits);

VOID
NTAPI
KiInitializeStartupThread(
    _In_ PKPRCB Prcb,
    _Out_ PKTHREAD Thread,
    _Inout_ PKPROCESS Process,
    _In_ PVOID Stack,
    _In_opt_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_opt_ PKSTART_ROUTINE StartRoutine);

VOID
NTAPI
KiYieldStartupProcessor(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD StartupThread);

VOID
FASTCALL
KiRunStackCallout(
    _In_ PVOID Context);

VOID
FASTCALL
KiCompleteTimer(
    _In_ PKTIMER Timer,
    _In_ PKSPIN_LOCK_QUEUE LockQueue);

VOID
NTAPI
KiExpireTimers(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2);

BOOLEAN
FASTCALL
KiInsertTimerTable(
    _In_ PKTIMER Timer,
    _In_ ULONG Hand);

VOID
NTAPI
KiTimerExpiration(
    _In_ PKDPC Dpc,
    _In_ PVOID DeferredContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2);

VOID
FASTCALL
KiAcquireFastMutex(
    _In_ PFAST_MUTEX FastMutex);

VOID
FASTCALL
KiAcquireGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex);

BOOLEAN
KiConnectPassiveInterrupt(
    _Inout_ PKINTERRUPT Interrupt);

BOOLEAN
KiDisconnectPassiveInterrupt(
    _Inout_ PKINTERRUPT Interrupt);

BOOLEAN
KiDispatchPassiveInterrupt(
    _In_ ULONG Vector,
    _Out_opt_ PBOOLEAN IsLevel);

BOOLEAN
KiSynchronizePassiveInterrupt(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    _In_opt_ PVOID SynchronizeContext);

VOID
NTAPI
KeInitializeProcess(
    struct _KPROCESS *Process,
    KPRIORITY Priority,
    KAFFINITY Affinity,
    PULONG_PTR DirectoryTableBase,
    _In_ BOOLEAN Enable);

ULONG
NTAPI
KeQueryRuntimeProcess(
    _In_ PKPROCESS Process,
    _Out_ PULONG UserTime);

KAFFINITY
NTAPI
KeSetAffinityProcess(
    _In_ PKPROCESS Process,
    _In_ KAFFINITY Affinity);

BOOLEAN
NTAPI
KeSetAutoAlignmentProcess(
    _In_ PKPROCESS Process,
    _In_ BOOLEAN Enable);

BOOLEAN
NTAPI
KeSetCheckStackExtentsProcess(
    _In_ PKPROCESS Process,
    _In_ BOOLEAN Enable);

BOOLEAN
NTAPI
KeSetDisableBoostProcess(
    _In_ PKPROCESS Process,
    _In_ BOOLEAN Disable);

KPRIORITY
NTAPI
KeSetPriorityAndQuantumProcess(
    _In_ PKPROCESS Process,
    _In_ KPRIORITY Priority,
    _In_opt_ UCHAR Quantum);

ULONG
NTAPI
KeSetProcess(
    struct _KPROCESS* Process,
    KPRIORITY Increment,
    BOOLEAN InWait);

VOID
NTAPI
KeSetQuantumProcess(
    _In_ PKPROCESS Process,
    _In_ UCHAR Quantum);

VOID
NTAPI
KeUninitializeProcess(
    _Inout_ struct _KPROCESS *Process);

VOID
NTAPI
KiAttachProcess(
    struct _KTHREAD *Thread,
    struct _KPROCESS *Process,
    PKLOCK_QUEUE_HANDLE ApcLock,
    struct _KAPC_STATE *SavedApcState);

NTSTATUS
NTAPI
KiGetGuiServiceFailureStatus(
    _In_ ULONG ServiceNumber);

KHETERO_CPU_POLICY
NTAPI
KeQueryHeteroCpuPolicyThread(
    _In_ PKTHREAD Thread,
    _In_ LOGICAL UserPolicy);

ULONG64
NTAPI
KeQueryTotalCycleTimeProcess(
    _Inout_ PKPROCESS Process,
    _Out_ PULONG64 CycleTimeStamp);

ULONG64
NTAPI
KeQueryTotalCycleTimeThread(
    _Inout_ PKTHREAD Thread,
    _Out_ PULONG64 CycleTimeStamp);

CODE_SEG("INIT")
VOID
NTAPI
KiCaptureProcessorTopology(VOID);

VOID
NTAPI
KeInitializeProfile(
    struct _KPROFILE* Profile,
    struct _KPROCESS* Process,
    PVOID ImageBase,
    SIZE_T ImageSize,
    ULONG BucketSize,
    KPROFILE_SOURCE ProfileSource,
    KAFFINITY Affinity);

ULONG
NTAPI
KeQueryIntervalProfile(
    KPROFILE_SOURCE ProfileSource);

VOID
NTAPI
KeSetIntervalProfile(
    ULONG Interval,
    KPROFILE_SOURCE ProfileSource);

BOOLEAN
NTAPI
KeStartProfile(
    struct _KPROFILE* Profile,
    PVOID Buffer);

BOOLEAN
NTAPI
KeStopProfile(
    struct _KPROFILE* Profile);

VOID
NTAPI
KiAdjustQuantumThread(
    _In_ PKTHREAD Thread);

VOID
FASTCALL
KiDeferredReadyThread(
    _In_ PKTHREAD Thread);

_Requires_lock_held_(Prcb->PrcbLock)
PKTHREAD
FASTCALL
KiIdleSchedule(
    _In_ PKPRCB Prcb);

VOID
FASTCALL
KiProcessDeferredReadyList(
    _In_ PKPRCB Prcb);

VOID
NTAPI
KiQuantumEnd(VOID);

VOID
FASTCALL
KiQueueThreadForReaping(
    _In_ PKTHREAD Thread);

VOID
NTAPI
KiReadyThread(
    _In_ PKTHREAD Thread);

PKTHREAD
FASTCALL
KiSelectNextThread(
    _In_ PKPRCB Prcb);

KAFFINITY
FASTCALL
KiSetAffinityThread(
    _In_ PKTHREAD Thread,
    _In_ KAFFINITY Affinity);

VOID
FASTCALL
KiSetPriorityThread(
    _In_ PKTHREAD Thread,
    _In_ KPRIORITY Priority);

LONG_PTR
FASTCALL
KiSwapThread(
    _In_ PKTHREAD Thread,
    _In_ PKPRCB Prcb,
    _In_ BOOLEAN NormalWait);

VOID
FASTCALL
KiYieldDelayExecution(VOID);

VOID
NTAPI
KeInitializeEventPair(
    PKEVENT_PAIR EventPair);

VOID
FASTCALL
KeInitializeGate(
    PKGATE Gate);

VOID
FASTCALL
KeSignalGateBoostPriority(
    PKGATE Gate);

VOID
FASTCALL
KeWaitForGate(
    PKGATE Gate,
    KWAIT_REASON WaitReason,
    KPROCESSOR_MODE WaitMode);

ULONG
NTAPI
KeAlertResumeThread(
    _In_ PKTHREAD Thread);

BOOLEAN
NTAPI
KeAlertThread(
    _In_ PKTHREAD Thread,
    _In_ KPROCESSOR_MODE AlertMode);

BOOLEAN
NTAPI
KeAlertThreadByThreadId(
    _In_ PKTHREAD Thread);

VOID
NTAPI
KeBoostPriorityThread(
    _In_ PKTHREAD Thread,
    _In_ KPRIORITY Increment);

CHAR
NTAPI
KeExchangePriorityFloorThread(
    _Inout_ PKTHREAD Thread,
    _Inout_ PCHAR StoredFloor,
    _In_ LONG NewFloor);

UCHAR
NTAPI
KeFindNextRightSetAffinity(
    _In_ UCHAR Number,
    _In_ KAFFINITY Set);

ULONG
NTAPI
KeForceResumeThread(
    _In_ PKTHREAD Thread);

VOID
NTAPI
KeFreezeAllThreads(VOID);

NTSTATUS
NTAPI
KeInitThread(
    _Inout_ PKTHREAD Thread,
    _In_ PVOID KernelStack,
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext,
    _In_ PCONTEXT Context,
    _In_ PVOID Teb,
    _In_ PKPROCESS Process);

VOID
NTAPI
KeInitializeThread(
    _In_ PKPROCESS Process,
    _Inout_ PKTHREAD Thread,
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext,
    _In_ PCONTEXT Context,
    _In_ PVOID Teb,
    _In_ PVOID KernelStack);

LONG
NTAPI
KeQueryBasePriorityThread(
    _In_ PKTHREAD Thread);

BOOLEAN
NTAPI
KeReadStateThread(
    _In_ PKTHREAD Thread);

VOID
NTAPI
KeReadyThread(
    _In_ PKTHREAD Thread);

ULONG
NTAPI
KeResumeThread(
    _In_ PKTHREAD Thread);

VOID
NTAPI
KeRundownThread(VOID);

BOOLEAN
NTAPI
KeSetAutoAlignmentThread(
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN Enable);

BOOLEAN
NTAPI
KeSetDisableBoostThread(
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN Disable);

VOID
NTAPI
KeStartThread(
    _Inout_ PKTHREAD Thread);

ULONG
NTAPI
KeSuspendThread(
    PKTHREAD Thread);

BOOLEAN
NTAPI
KeTestAlertThread(
    _In_ KPROCESSOR_MODE AlertMode);

VOID
NTAPI
KeThawAllThreads(VOID);

VOID
NTAPI
KeUninitThread(
    _In_ PKTHREAD Thread);

VOID
NTAPI
KiSuspendNop(
    _In_ PKAPC Apc,
    _In_ PKNORMAL_ROUTINE *NormalRoutine,
    _In_ PVOID *NormalContext,
    _In_ PVOID *SystemArgument1,
    _In_ PVOID *SystemArgument2);

VOID
NTAPI
KiSuspendRundown(
    _In_ PKAPC Apc);

VOID
NTAPI
KiSuspendThread(
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2);

BOOLEAN
NTAPI
KiTestAlert(VOID);

KAFFINITY
FASTCALL
KiThreadCpuSetAffinity(
    _In_ PKTHREAD Thread,
    _In_ KAFFINITY Affinity);

NTSTATUS
NTAPI
KeWaitForAlertByThreadId(
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_opt_ PLARGE_INTEGER Timeout);

VOID
FASTCALL
KiActivateWaiterQueue(
    _In_ PKQUEUE Queue);

LONG
NTAPI
KiInsertQueue(
    _In_ PKQUEUE Queue,
    _In_ PLIST_ENTRY Entry,
    BOOLEAN Head);

PLIST_ENTRY
NTAPI
KiRemoveQueue(
    _In_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

NTSTATUS
NTAPI
KeSetSelectedCpuSetsThread(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG CpuSetCount,
    _In_reads_(CpuSetCount) PULONG64 CpuSetMasks);

VOID
NTAPI
KiEndUnexpectedRange(VOID);

VOID
NTAPI
KiQueryProcessorEfficiencyClasses(
    _Out_writes_all_(MAXIMUM_PROCESSORS) PUCHAR EfficiencyClasses);

VOID
NTAPI
KiQueryProcessorTopology(
    _Out_writes_to_opt_(MaxRecords, *RecordCount) PKI_CACHE_RECORD Records,
    _In_ ULONG MaxRecords,
    _Out_ PULONG RecordCount,
    _Out_writes_to_opt_(MaxSets, *SetCount) PKAFFINITY Sets,
    _In_ ULONG MaxSets,
    _Out_ PULONG SetCount);

BOOLEAN
NTAPI
KiReadValidMemory(
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG_PTR Address,
    _In_ ULONG Size);

VOID
NTAPI
KiStartUnexpectedRange(VOID);

FORCEINLINE
LONG
KiExtTimerPeriodToMilliseconds(
    _In_ LONGLONG Period)
{
    LONG PeriodMs = 0;

    if (Period != 0)
    {
        PeriodMs = (LONG)(Period / 10000);
        if (PeriodMs == 0)
            PeriodMs = 1;
    }

    return PeriodMs;
}

#include "ke_x.h"
