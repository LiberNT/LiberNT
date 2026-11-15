

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

#ifndef KiArchPublishSystemCallStubs
#define KiArchPublishSystemCallStubs(DllBase) ((VOID)(DllBase), STATUS_SUCCESS)
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





typedef struct _KI_CACHE_RECORD
{
    CACHE_DESCRIPTOR Descriptor;
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


#define PRIORITY_MASK(Priority) (1UL << (Priority))

#define TIMER_OR_EVENT_TYPE 0x7L

#define TIMER_WAIT_BLOCK 0x3L

#define KTHREAD_ALERTED_BY_THREAD_ID_BIT 4


FASTCALL
);

VOID
NTAPI
);

NTAPI
);

NTAPI
);

BOOLEAN


NTAPI

NTAPI
);

VOID
);

VOID
NTAPI

VOID

NTAPI

NTAPI

BOOLEAN

VOID
NTAPI

VOID

VOID

VOID

FASTCALL

VOID


BOOLEAN

VOID

FASTCALL


BOOLEAN

VOID

BOOLEAN

VOID
FASTCALL

CODE_SEG("INIT")
VOID
NTAPI

CODE_SEG("INIT")
VOID
NTAPI

VOID

VOID


VOID

VOID
FASTCALL

VOID


VOID
FASTCALL

VOID
NTAPI

VOID

VOID
FASTCALL


NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI
    PKTRAP_FRAME TrapFrame,

VOID

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

VOID
FASTCALL

FASTCALL

VOID
NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

BOOLEAN

NTAPI

NTAPI

VOID
FASTCALL

VOID
FASTCALL
KiUnlinkThread(

VOID
FASTCALL
KiUnlinkWaitBlocks(

VOID

BOOLEAN

VOID
FASTCALL

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

VOID

VOID
NTAPI

BOOLEAN
NTAPI



VOID
NTAPI

VOID
NTAPI

VOID
NTAPI


NTAPI

VOID
NTAPI
KiTimerExpiration(

VOID

VOID

VOID



BOOLEAN

BOOLEAN

VOID
NTAPI

NTAPI

NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

VOID
NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI

VOID
FASTCALL

FASTCALL

VOID

VOID
NTAPI

VOID

VOID
NTAPI



NTAPI

VOID
FASTCALL

VOID
FASTCALL



BOOLEAN

BOOLEAN
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTSTATUS
NTAPI

VOID

NTAPI

VOID
NTAPI

VOID
NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

VOID
NTAPI

NTAPI


VOID

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

FASTCALL

NTAPI

FASTCALL

NTAPI

NTAPI


VOID

NTAPI

NTAPI

NTAPI

NTAPI

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
