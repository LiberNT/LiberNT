/*
 * PROJECT:     LiberNT NDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) kernel types
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Layout evidence: Windows SDK 10.0.16299 shared/ksarm.h (Pb*, Pc*, Ps*, Ks*, Aa*,
 * Ms*, Sw*, Sf*, Ck*, Mf*, Dc* offsets) and WDK 10.0.16299 km/ntddk.h. Fields
 * the scheduler needs that no evidence places are marked FIXME.
 */

#ifndef _ARM_KETYPES_H
#define _ARM_KETYPES_H

/* PowerState spans Prcb 0xB80-0xCFF (PbPowerState, PbAlignmentFixupCount). */
#define PROCESSOR_POWER_STATE_LEGACY_AREA_SIZE 0x100
#include <potypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNCH_LEVEL             DISPATCH_LEVEL

/* Immediate interprocessor commands (ksarm.h IPI_*). */
#define IPI_APC                 1
#define IPI_DPC                 2
#define IPI_FREEZE              4
#define IPI_PACKET_READY        6

/* No LDT exists on ARM; the NDK prototypes only need a placeholder type. */
#define LDT_ENTRY               ULONG

#define NUMBER_POOL_LOOKASIDE_LISTS 32

/* ksarm.h: INITIAL_STALL_COUNT 0x64, DOUBLE_FAULT_STACK_SIZE 0x3000. */
#define INITIAL_STALL_COUNT     0x64
#define DOUBLE_FAULT_STACK_SIZE 0x3000

#define PRCB_MAJOR_VERSION      1
#define PRCB_BUILD_DEBUG        1
#define PRCB_BUILD_UNIPROCESSOR 2

/* CPSR mode field and flags (ARM ARM B1.3.3). */
#define CPSRM_USER  0x10
#define CPSRM_FIQ   0x11
#define CPSRM_INT   0x12
#define CPSRM_SVC   0x13
#define CPSRM_ABT   0x17
#define CPSRM_UDF   0x1B
#define CPSRM_SYS   0x1F
#define CPSRM_MASK  0x1F
#define CPSRC_THUMB 0x20
#define CPSRC_INT   0x80
#define CPSRC_FIQ   0x40

typedef struct _KSPECIAL_REGISTERS
{
    ULONG Reserved[7];
    ULONG Cp15_Cr13_UsrRW;
    ULONG Cp15_Cr13_UsrRO;
    ULONG Cp15_Cr13_SvcRW;
    ULONG KernelBvr[8];
    ULONG KernelBcr[8];
    ULONG KernelWvr[1];
    ULONG KernelWcr[1];
    ULONG Fpexc;
    ULONG Fpinst;
    ULONG Fpinst2;
    ULONG UserSp;
    ULONG UserLr;
    ULONG AbortSp;
    ULONG AbortLr;
    ULONG AbortSpsr;
    ULONG UdfSp;
    ULONG UdfLr;
    ULONG UdfSpsr;
    ULONG IrqSp;
    ULONG IrqLr;
    ULONG IrqSpsr;
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, Cp15_Cr13_UsrRW) == 0x1C);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, KernelBvr) == 0x28);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, KernelBcr) == 0x48);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, KernelWcr) == 0x6C);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, Fpexc) == 0x70);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, IrqSpsr) == 0xA4);
C_ASSERT(sizeof(KSPECIAL_REGISTERS) == 0xA8);

typedef struct _KARM_ARCH_STATE
{
    ULONG Cp15_Cr0_CpuId;
    ULONG Cp15_Cr1_Control;
    ULONG Cp15_Cr1_AuxControl;
    ULONG Cp15_Cr1_Cpacr;
    ULONG Cp15_Cr2_TtbControl;
    ULONG Cp15_Cr2_Ttb0;
    ULONG Cp15_Cr2_Ttb1;
    ULONG Cp15_Cr3_Dacr;
    ULONG Cp15_Cr5_Dfsr;
    ULONG Cp15_Cr5_Ifsr;
    ULONG Cp15_Cr6_Dfar;
    ULONG Cp15_Cr6_Ifar;
    ULONG Cp15_Cr9_PmControl;
    ULONG Cp15_Cr9_PmCountEnableSet;
    ULONG Cp15_Cr9_PmCycleCounter;
    ULONG Cp15_Cr9_PmEventCounter[31];
    ULONG Cp15_Cr9_PmEventType[31];
    ULONG Cp15_Cr9_PmInterruptSelect;
    ULONG Cp15_Cr9_PmOverflowStatus;
    ULONG Cp15_Cr9_PmSelect;
    ULONG Cp15_Cr9_PmUserEnable;
    ULONG Cp15_Cr10_PrimaryMemoryRemap;
    ULONG Cp15_Cr10_NormalMemoryRemap;
    ULONG Cp15_Cr12_VBARns;
    ULONG Cp15_Cr13_ContextId;
} KARM_ARCH_STATE, *PKARM_ARCH_STATE;

C_ASSERT(FIELD_OFFSET(KARM_ARCH_STATE, Cp15_Cr9_PmEventCounter) == 0x3C);
C_ASSERT(FIELD_OFFSET(KARM_ARCH_STATE, Cp15_Cr9_PmEventType) == 0xB8);
C_ASSERT(FIELD_OFFSET(KARM_ARCH_STATE, Cp15_Cr9_PmInterruptSelect) == 0x134);
C_ASSERT(FIELD_OFFSET(KARM_ARCH_STATE, Cp15_Cr13_ContextId) == 0x150);

typedef struct _KPROCESSOR_STATE
{
    KSPECIAL_REGISTERS SpecialRegisters;
    KARM_ARCH_STATE ArchState;
    UCHAR Reserved[0x200 - 0xA8 - sizeof(KARM_ARCH_STATE)];
    CONTEXT ContextFrame;
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;

C_ASSERT(FIELD_OFFSET(KPROCESSOR_STATE, ArchState) == 0xA8);
C_ASSERT(FIELD_OFFSET(KPROCESSOR_STATE, ContextFrame) == 0x200);
C_ASSERT(sizeof(KPROCESSOR_STATE) == 0x3A0);

typedef struct _KARM_MINI_STACK
{
    ULONG Pc;
    ULONG Cpsr;
    ULONG R4;
    ULONG R5;
    ULONG R6;
    ULONG R7;
    ULONG Reserved[2];
} KARM_MINI_STACK, *PKARM_MINI_STACK;

C_ASSERT(sizeof(KARM_MINI_STACK) == 0x20);

#ifndef NTOS_MODE_USER
/* Table-based unwinding for kernel code (SDK 10.0.16299 winnt.h, _ARM_). */
typedef struct _IMAGE_ARM_RUNTIME_FUNCTION_ENTRY
{
    ULONG BeginAddress;
    union
    {
        ULONG UnwindData;
        struct
        {
            ULONG Flag : 2;
            ULONG FunctionLength : 11;
            ULONG Ret : 2;
            ULONG H : 1;
            ULONG Reg : 3;
            ULONG R : 1;
            ULONG L : 1;
            ULONG C : 1;
            ULONG StackAdjust : 10;
        };
    };
} IMAGE_ARM_RUNTIME_FUNCTION_ENTRY, *PIMAGE_ARM_RUNTIME_FUNCTION_ENTRY;
typedef IMAGE_ARM_RUNTIME_FUNCTION_ENTRY RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

typedef struct _SCOPE_TABLE_ARM
{
    ULONG Count;
    struct
    {
        ULONG BeginAddress;
        ULONG EndAddress;
        ULONG HandlerAddress;
        ULONG JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE_ARM, *PSCOPE_TABLE_ARM;

#define UNW_FLAG_NHANDLER 0x0
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2

#define UNWIND_HISTORY_TABLE_SIZE 12

typedef struct _UNWIND_HISTORY_TABLE_ENTRY
{
    ULONG ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
} UNWIND_HISTORY_TABLE_ENTRY, *PUNWIND_HISTORY_TABLE_ENTRY;

typedef struct _UNWIND_HISTORY_TABLE
{
    ULONG Count;
    UCHAR LocalHint;
    UCHAR GlobalHint;
    UCHAR Search;
    UCHAR Once;
    ULONG LowAddress;
    ULONG HighAddress;
    UNWIND_HISTORY_TABLE_ENTRY Entry[UNWIND_HISTORY_TABLE_SIZE];
} UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS
{
    PULONG R4;
    PULONG R5;
    PULONG R6;
    PULONG R7;
    PULONG R8;
    PULONG R9;
    PULONG R10;
    PULONG R11;
    PULONG Lr;
    PULONGLONG D8;
    PULONGLONG D9;
    PULONGLONG D10;
    PULONGLONG D11;
    PULONGLONG D12;
    PULONGLONG D13;
    PULONGLONG D14;
    PULONGLONG D15;
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

NTSYSAPI
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable);

NTSYSAPI
PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG ImageBase,
    _In_ ULONG ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PVOID *HandlerData,
    _Out_ PULONG EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers);

NTSYSAPI
VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PUNWIND_HISTORY_TABLE HistoryTable);

NTSYSAPI
VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _EXCEPTION_RECORD *ExceptionRecord);
#endif /* !NTOS_MODE_USER */

typedef struct _DISPATCHER_CONTEXT
{
    ULONG ControlPc;
    ULONG ImageBase;
    struct _IMAGE_ARM_RUNTIME_FUNCTION_ENTRY *FunctionEntry;
    ULONG EstablisherFrame;
    ULONG TargetPc;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    struct _UNWIND_HISTORY_TABLE *HistoryTable;
    ULONG ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    PUCHAR NonVolatileRegisters;
    ULONG Reserved;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

C_ASSERT(FIELD_OFFSET(DISPATCHER_CONTEXT, NonVolatileRegisters) == 0x2C);

typedef struct _MACHINE_FRAME
{
    ULONG Sp;
    ULONG Pc;
} MACHINE_FRAME, *PMACHINE_FRAME;

typedef KEXCEPTION_FRAME KCALLOUT_FRAME, *PKCALLOUT_FRAME;

typedef struct _UCALLOUT_FRAME
{
    PVOID Buffer;
    ULONG Length;
    ULONG ApiNumber;
    ULONG OriginalLr;
    MACHINE_FRAME MachineFrame;
} UCALLOUT_FRAME, *PUCALLOUT_FRAME;

C_ASSERT(FIELD_OFFSET(UCALLOUT_FRAME, MachineFrame.Sp) == 0x10);
C_ASSERT(sizeof(UCALLOUT_FRAME) == 0x18);

typedef struct _KSTART_FRAME
{
    ULONG R0;
    ULONG R1;
    ULONG R2;
    ULONG Return;
} KSTART_FRAME, *PKSTART_FRAME;

typedef struct _KSWITCH_FRAME
{
    KIRQL ApcBypass;
    UCHAR Fill[7];
    ULONG R11;
    ULONG Return;
} KSWITCH_FRAME, *PKSWITCH_FRAME;

C_ASSERT(sizeof(KSWITCH_FRAME) == 0x10);

typedef struct _KDESCRIPTOR
{
    USHORT Pad;
    USHORT Limit;
    ULONG Base;
} KDESCRIPTOR, *PKDESCRIPTOR;

typedef struct _KREQUEST_PACKET
{
    PVOID CurrentPacket[3];
    PVOID WorkerRoutine;
} KREQUEST_PACKET, *PKREQUEST_PACKET;

typedef struct _REQUEST_MAILBOX
{
    LONG RequestSummary;
    KREQUEST_PACKET RequestPacket;
    PVOID Virtual[7];
} REQUEST_MAILBOX, *PREQUEST_MAILBOX;

typedef struct _KPRCB
{
    UCHAR LegacyNumber;
    UCHAR ReservedMustBeZero;
    UCHAR IdleHalt;
    UCHAR PrcbPad003;
    struct _KTHREAD *CurrentThread;
    struct _KTHREAD *NextThread;
    struct _KTHREAD *IdleThread;
    UCHAR NestingLevel;
    UCHAR ClockOwner;
    UCHAR PendingTickFlags;
    UCHAR PrcbPad013;
    ULONG Number;
    ULONG PrcbLock;
    PCHAR PriorityState;
    KPROCESSOR_STATE ProcessorState;
    /* FIXME: no symbol evidence places 0x3C0-0x3CF */
    USHORT ProcessorModel;
    USHORT ProcessorRevision;
    ULONG MHz;
    ULONG64 CycleCounterFrequency;
    ULONG HalReserved[15];
    USHORT MinorVersion;
    USHORT MajorVersion;
    UCHAR BuildType;
    UCHAR CpuVendor;
    UCHAR CoresPerPhysicalProcessor;
    UCHAR LogicalProcessorsPerCore;
    PVOID AcpiReserved;
    ULONG GroupSetMember;
    UCHAR Group;
    UCHAR GroupIndex;
    UCHAR PrcbPad41E[0x480 - 0x41E];
    KSPIN_LOCK_QUEUE LockQueue[17];
    UCHAR ProcessorVendorString[2];
    UCHAR PrcbPad50A[2];
    ULONG FeatureBits;
    /* FIXME: no symbol evidence places the fields of 0x510-0x57F */
    union
    {
        struct
        {
        ULONG MaxBreakpoints;
        ULONG MaxWatchpoints;
        PCONTEXT Context;
        ULONG ContextFlagsInit;
        KAFFINITY SetMember;
        ULONG MultiThreadProcessorSet;
        ULONG CoreProcessorSet;
        LONG DpcSetEventRequest;
        LONG Sleeping;
        ULONG TimerHand;
        ULONG TimerRequest;
        };
        UCHAR PrcbRegion510[0x580 - 0x510];
    };
    PP_LOOKASIDE_LIST PPLookasideList[16];
    LONG PacketBarrier;
    SINGLE_LIST_ENTRY DeferredReadyListHead;
    volatile LONG MmPageFaultCount;
    volatile LONG MmCopyOnWriteCount;
    volatile LONG MmTransitionCount;
    volatile LONG MmDemandZeroCount;
    volatile LONG MmPageReadCount;
    volatile LONG MmPageReadIoCount;
    volatile LONG MmDirtyPagesWriteCount;
    volatile LONG MmDirtyWriteIoCount;
    volatile LONG MmMappedPagesWriteCount;
    volatile LONG MmMappedWriteIoCount;
    ULONG KeSystemCalls;
    ULONG KeContextSwitches;
    ULONG CcFastReadNoWait;
    ULONG CcFastReadWait;
    ULONG CcFastReadNotPossible;
    ULONG CcCopyReadNoWait;
    ULONG CcCopyReadWait;
    ULONG CcCopyReadNoWaitMiss;
    LONG LookasideIrpFloat;
    volatile LONG IoReadOperationCount;
    volatile LONG IoWriteOperationCount;
    volatile LONG IoOtherOperationCount;
    LARGE_INTEGER IoReadTransferCount;
    LARGE_INTEGER IoWriteTransferCount;
    LARGE_INTEGER IoOtherTransferCount;
    UCHAR PrcbPad678[8];
    PREQUEST_MAILBOX Mailbox;
    LONG TargetCount;
    volatile ULONG IpiFrozen;
    volatile ULONG RequestSummary;
    KDPC_DATA DpcData[2];
    PVOID DpcStack;
    PVOID SpBase;
    LONG MaximumDpcQueueDepth;
    ULONG DpcRequestRate;
    ULONG MinimumDpcRate;
    ULONG DpcLastCount;
    UCHAR ThreadDpcEnable;
    volatile UCHAR QuantumEnd;
    volatile UCHAR DpcRoutineActive;
    volatile UCHAR IdleSchedule;
    union
    {
        volatile LONG DpcRequestSummary;
        SHORT DpcRequestSlot[2];
        struct
        {
            SHORT NormalDpcState;
            SHORT ThreadDpcState;
        };
        struct
        {
            ULONG DpcNormalProcessingActive : 1;
            ULONG DpcNormalProcessingRequested : 1;
            ULONG DpcNormalThreadSignal : 1;
            ULONG DpcNormalTimerExpiration : 1;
            ULONG DpcNormalDpcPresent : 1;
            ULONG DpcNormalLocalInterrupt : 1;
            ULONG DpcNormalSpare : 10;
            ULONG DpcThreadActive : 1;
            ULONG DpcThreadRequested : 1;
            ULONG DpcThreadSpare : 14;
        };
    };
    /* FIXME: no symbol evidence places the fields of 0x6E0-0x6FF */
    union
    {
        struct
        {
        ULONG LastTimerHand;
        ULONG LastTick;
        ULONG ClockInterrupts;
        ULONG ReadyScanTick;
        ULONG InterruptLastCount;
        ULONG InterruptRate;
        };
        UCHAR PrcbRegion6E0[0x700 - 0x6E0];
    };
    union
    {
        KGATE DpcGate;
        KEVENT DpcEvent;
    };
    /* FIXME: no symbol evidence places the fields of 0x710-0x743 */
    union
    {
        struct
        {
        ULONG MPAffinity;
        KDPC CallDpc;
        LONG DpcWatchdogPeriod;
        LONG DpcWatchdogCount;
        LONG ClockKeepAlive;
        };
        UCHAR PrcbRegion710[0x744 - 0x710];
    };
    LONG KeSpinLockOrdering;
    UCHAR PrcbPad748[0x780 - 0x748];
    LIST_ENTRY WaitListHead;
    /* FIXME: no symbol evidence places the fields of 0x788-0x7FF */
    union
    {
        struct
        {
            ULONG WaitLock;
            ULONG ReadySummary;
            LONG AffinitizedSelectionMask;
            ULONG QueueIndex;
            KDPC TimerExpirationDpc;
            LIST_ENTRY ScbList;
        };
        UCHAR PrcbRegion788[0x800 - 0x788];
    };
    LIST_ENTRY DispatcherReadyListHead[32];
    ULONG InterruptCount;
    ULONG KernelTime;
    ULONG UserTime;
    ULONG DpcTime;
    ULONG InterruptTime;
    ULONG AdjustDpcThreshold;
    UCHAR SkipTick;
    UCHAR DebuggerSavedIRQL;
    UCHAR PollSlot;
    UCHAR GroupSchedulingOverQuota;
    ULONG DpcTimeCount;
    ULONG DpcTimeLimit;
    ULONG PeriodicCount;
    ULONG PeriodicBias;
    ULONG AvailableTime;
    ULONG ScbOffset;
    ULONG KeExceptionDispatchCount;
    struct _KNODE *ParentNode;
    ULONG PrcbPad93C;
    ULONG64 AffinitizedCycles;
    ULONG64 StartCycles;
    ULONG64 GenerationTarget;
    UCHAR PrcbPad958[0x970 - 0x958];
    ULONG64 CycleCounterHigh;
    KENTROPY_TIMING_STATE EntropyTimingState;
    LONG MmSpinLockOrdering;
    ULONG PageColor;
    ULONG NodeColor;
    ULONG NodeShiftedColor;
    ULONG SecondaryColorMask;
    ULONG64 CycleTime;
    UCHAR PrcbPadAC0[0xB00 - 0xAC0];
    ULONG CcFastMdlReadNoWait;
    ULONG CcFastMdlReadWait;
    ULONG CcFastMdlReadNotPossible;
    ULONG CcMapDataNoWait;
    ULONG CcMapDataWait;
    ULONG CcPinMappedDataCount;
    ULONG CcPinReadNoWait;
    ULONG CcPinReadWait;
    ULONG CcMdlReadNoWait;
    ULONG CcMdlReadWait;
    ULONG CcLazyWriteHotSpots;
    ULONG CcLazyWriteIos;
    ULONG CcLazyWritePages;
    ULONG CcDataFlushes;
    ULONG CcDataPages;
    ULONG CcLostDelayedWrites;
    ULONG CcFastReadResourceMiss;
    ULONG CcCopyReadWaitMiss;
    ULONG CcFastMdlReadResourceMiss;
    ULONG CcMapDataNoWaitMiss;
    ULONG CcMapDataWaitMiss;
    ULONG CcPinReadNoWaitMiss;
    ULONG CcPinReadWaitMiss;
    ULONG CcMdlReadNoWaitMiss;
    ULONG CcMdlReadWaitMiss;
    ULONG CcReadAheadIos;
    volatile LONG MmCacheTransitionCount;
    volatile LONG MmCacheReadCount;
    volatile LONG MmCacheIoCount;
    UCHAR PrcbPadB74[0xB80 - 0xB74];
    PROCESSOR_POWER_STATE PowerState;
    UCHAR PrcbPadPower[0xD00 - 0xB80 - sizeof(PROCESSOR_POWER_STATE)];
    ULONG KeAlignmentFixupCount;
    /* FIXME: no symbol evidence places the fields of 0xD08-0xE7F */
    ULONG PrcbPadD04;
    union
    {
        struct
        {
        SLIST_HEADER InterruptObjectPool;
        ULONG DeviceInterrupts;
        PVOID IsrDpcStats;
        KDPC DpcWatchdogDpc;
        KTIMER DpcWatchdogTimer;
        ULONG CachedCommit;
        ULONG CachedResidentAvailable;
        PVOID HyperPte;
        PVOID WheaInfo;
        PVOID EtwSupport;
        CACHE_DESCRIPTOR Cache[6];
        UCHAR CacheCount;
        };
        UCHAR PrcbRegionD08[0xE80 - 0xD08];
    };
    SYNCH_COUNTERS SynchCounters;
    UCHAR PrcbPadSynch[0xF50 - 0xE80 - sizeof(SYNCH_COUNTERS)];
    KARM_MINI_STACK FiqMiniStack;
    KARM_MINI_STACK IrqMiniStack;
    KARM_MINI_STACK UdfMiniStack;
    KARM_MINI_STACK AbtMiniStack;
    KARM_MINI_STACK PanicMiniStack;
    ULONG PanicStackBase;
    PVOID IsrStack;
    ULONG PteBitCache;
    ULONG PteBitOffset;
    /* FIXME: no symbol evidence places the fields of 0x1000-0x297F */
    union
    {
        struct
        {
        KTIMER_TABLE TimerTable;
        SINGLE_LIST_ENTRY AbSelfIoBoostsList;
        SINGLE_LIST_ENTRY AbPropagateBoostsList;
        KDPC AbDpc;
        };
        UCHAR PrcbRegion1000[0x2980 - 0x1000];
    };
    GENERAL_LOOKASIDE_POOL PPNxPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    GENERAL_LOOKASIDE_POOL PPNPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    GENERAL_LOOKASIDE_POOL PPPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    UCHAR PrcbPadLookaside[0x4880 - 0x3B80 - NUMBER_POOL_LOOKASIDE_LISTS * sizeof(GENERAL_LOOKASIDE_POOL)];
    REQUEST_MAILBOX RequestMailbox[1];
    UCHAR PrcbPadEnd[0x4CE0 - 0x4880 - sizeof(REQUEST_MAILBOX)];
} KPRCB, *PKPRCB;

C_ASSERT(FIELD_OFFSET(KPRCB, IdleHalt) == 0x2);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentThread) == 0x4);
C_ASSERT(FIELD_OFFSET(KPRCB, NextThread) == 0x8);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleThread) == 0xC);
C_ASSERT(FIELD_OFFSET(KPRCB, NestingLevel) == 0x10);
C_ASSERT(FIELD_OFFSET(KPRCB, Number) == 0x14);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbLock) == 0x18);
C_ASSERT(FIELD_OFFSET(KPRCB, PriorityState) == 0x1C);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorState) == 0x20);
C_ASSERT(FIELD_OFFSET(KPRCB, HalReserved) == 0x3D0);
C_ASSERT(FIELD_OFFSET(KPRCB, MinorVersion) == 0x40C);
C_ASSERT(FIELD_OFFSET(KPRCB, MajorVersion) == 0x40E);
C_ASSERT(FIELD_OFFSET(KPRCB, BuildType) == 0x410);
C_ASSERT(FIELD_OFFSET(KPRCB, CoresPerPhysicalProcessor) == 0x412);
C_ASSERT(FIELD_OFFSET(KPRCB, LogicalProcessorsPerCore) == 0x413);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupSetMember) == 0x418);
C_ASSERT(FIELD_OFFSET(KPRCB, Group) == 0x41C);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupIndex) == 0x41D);
C_ASSERT(FIELD_OFFSET(KPRCB, LockQueue) == 0x480);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorVendorString) == 0x508);
C_ASSERT(FIELD_OFFSET(KPRCB, FeatureBits) == 0x50C);
C_ASSERT(FIELD_OFFSET(KPRCB, PPLookasideList) == 0x580);
C_ASSERT(FIELD_OFFSET(KPRCB, PacketBarrier) == 0x600);
C_ASSERT(FIELD_OFFSET(KPRCB, DeferredReadyListHead) == 0x604);
C_ASSERT(FIELD_OFFSET(KPRCB, KeSystemCalls) == 0x630);
C_ASSERT(FIELD_OFFSET(KPRCB, KeContextSwitches) == 0x634);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadNoWait) == 0x638);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadNoWaitMiss) == 0x64C);
C_ASSERT(FIELD_OFFSET(KPRCB, LookasideIrpFloat) == 0x650);
C_ASSERT(FIELD_OFFSET(KPRCB, IoReadOperationCount) == 0x654);
C_ASSERT(FIELD_OFFSET(KPRCB, IoReadTransferCount) == 0x660);
C_ASSERT(FIELD_OFFSET(KPRCB, IoOtherTransferCount) == 0x670);
C_ASSERT(FIELD_OFFSET(KPRCB, Mailbox) == 0x680);
C_ASSERT(FIELD_OFFSET(KPRCB, IpiFrozen) == 0x688);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcData) == 0x690);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcData[0].DpcLock) == 0x698);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcData[0].DpcCount) == 0x6A0);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcStack) == 0x6C0);
C_ASSERT(FIELD_OFFSET(KPRCB, SpBase) == 0x6C4);
C_ASSERT(FIELD_OFFSET(KPRCB, MaximumDpcQueueDepth) == 0x6C8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRequestRate) == 0x6CC);
C_ASSERT(FIELD_OFFSET(KPRCB, MinimumDpcRate) == 0x6D0);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcLastCount) == 0x6D4);
C_ASSERT(FIELD_OFFSET(KPRCB, QuantumEnd) == 0x6D9);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRoutineActive) == 0x6DA);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleSchedule) == 0x6DB);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRequestSummary) == 0x6DC);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcGate) == 0x700);
C_ASSERT(FIELD_OFFSET(KPRCB, KeSpinLockOrdering) == 0x744);
C_ASSERT(FIELD_OFFSET(KPRCB, WaitListHead) == 0x780);
C_ASSERT(FIELD_OFFSET(KPRCB, DispatcherReadyListHead) == 0x800);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptCount) == 0x900);
C_ASSERT(FIELD_OFFSET(KPRCB, KernelTime) == 0x904);
C_ASSERT(FIELD_OFFSET(KPRCB, UserTime) == 0x908);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcTime) == 0x90C);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptTime) == 0x910);
C_ASSERT(FIELD_OFFSET(KPRCB, AdjustDpcThreshold) == 0x914);
C_ASSERT(FIELD_OFFSET(KPRCB, SkipTick) == 0x918);
C_ASSERT(FIELD_OFFSET(KPRCB, DebuggerSavedIRQL) == 0x919);
C_ASSERT(FIELD_OFFSET(KPRCB, KeExceptionDispatchCount) == 0x934);
C_ASSERT(FIELD_OFFSET(KPRCB, ParentNode) == 0x938);
C_ASSERT(FIELD_OFFSET(KPRCB, StartCycles) == 0x948);
C_ASSERT(FIELD_OFFSET(KPRCB, CycleCounterHigh) == 0x970);
C_ASSERT(FIELD_OFFSET(KPRCB, EntropyTimingState.EntropyCount) == 0x978);
C_ASSERT(FIELD_OFFSET(KPRCB, EntropyTimingState.Buffer) == 0x97C);
C_ASSERT(FIELD_OFFSET(KPRCB, PageColor) == 0xAA8);
C_ASSERT(FIELD_OFFSET(KPRCB, NodeColor) == 0xAAC);
C_ASSERT(FIELD_OFFSET(KPRCB, NodeShiftedColor) == 0xAB0);
C_ASSERT(FIELD_OFFSET(KPRCB, SecondaryColorMask) == 0xAB4);
C_ASSERT(FIELD_OFFSET(KPRCB, CycleTime) == 0xAB8);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadNoWait) == 0xB00);
C_ASSERT(FIELD_OFFSET(KPRCB, PowerState) == 0xB80);
C_ASSERT(FIELD_OFFSET(KPRCB, KeAlignmentFixupCount) == 0xD00);
C_ASSERT(FIELD_OFFSET(KPRCB, SynchCounters.SpinLockAcquireCount) == 0xE80);
C_ASSERT(FIELD_OFFSET(KPRCB, FiqMiniStack) == 0xF50);
C_ASSERT(FIELD_OFFSET(KPRCB, IrqMiniStack) == 0xF70);
C_ASSERT(FIELD_OFFSET(KPRCB, UdfMiniStack) == 0xF90);
C_ASSERT(FIELD_OFFSET(KPRCB, AbtMiniStack) == 0xFB0);
C_ASSERT(FIELD_OFFSET(KPRCB, PanicMiniStack) == 0xFD0);
C_ASSERT(FIELD_OFFSET(KPRCB, PanicStackBase) == 0xFF0);
C_ASSERT(FIELD_OFFSET(KPRCB, PPNPagedLookasideList) == 0x3280);
C_ASSERT(FIELD_OFFSET(KPRCB, PPPagedLookasideList) == 0x3B80);
C_ASSERT(FIELD_OFFSET(KPRCB, RequestMailbox) == 0x4880);

typedef struct _KIPCR
{
    union
    {
        NT_TIB NtTib;
        struct
        {
            ULONG TibPad0[2];
            PVOID Spare1;
            struct _KPCR *Self;
            struct _KPRCB *CurrentPrcb;
            struct _KSPIN_LOCK_QUEUE *LockArray;
            PVOID Used_Self;
        };
    };
    KIRQL CurrentIrql;
    UCHAR SecondLevelCacheAssociativity;
    ULONG Unused0[3];
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG StallScaleFactor;
    PVOID Unused1[3];
    ULONG KernelReserved[15];
    ULONG SecondLevelCacheSize;
    union
    {
        USHORT SoftwareInterruptPending;
        struct
        {
            UCHAR ApcInterrupt;
            UCHAR DispatchInterrupt;
        };
    };
    USHORT InterruptPad;
    ULONG HalReserved[32];
    PVOID KdVersionBlock;
    PVOID Unused3;
    ULONG PcrAlign1[8];
    UCHAR PcrPad[0x580 - 0x12C];
    KPRCB Prcb;
} KIPCR, *PKIPCR;

C_ASSERT(FIELD_OFFSET(KIPCR, Self) == 0xC);
C_ASSERT(FIELD_OFFSET(KIPCR, CurrentPrcb) == 0x10);
C_ASSERT(FIELD_OFFSET(KIPCR, LockArray) == 0x14);
C_ASSERT(FIELD_OFFSET(KIPCR, Used_Self) == 0x18);
C_ASSERT(FIELD_OFFSET(KIPCR, StallScaleFactor) == 0x30);
C_ASSERT(FIELD_OFFSET(KIPCR, HalReserved) == 0x84);
C_ASSERT(sizeof(KPCR) == 0x12C);
C_ASSERT(FIELD_OFFSET(KIPCR, Prcb) == 0x580);
C_ASSERT(sizeof(KIPCR) == 0x5260);

#ifndef NTOS_MODE_USER

FORCEINLINE
struct _KPRCB *
KeGetCurrentPrcb(VOID)
{
    return KeGetPcr()->CurrentPrcb;
}

#define _KeGetCurrentThread()  KeGetCurrentPrcb()->CurrentThread
#define _KeGetPreviousMode()   KeGetCurrentPrcb()->CurrentThread->PreviousMode
#define _KeIsExecutingDpc()    (KeGetCurrentPrcb()->DpcRoutineActive != 0)

#endif /* !NTOS_MODE_USER */

#ifdef __cplusplus
}
#endif

#endif /* _ARM_KETYPES_H */
