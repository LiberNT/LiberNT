/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 *              Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */

#ifndef _LOONGARCH64_KETYPES_H
#define _LOONGARCH64_KETYPES_H

#define PROCESSOR_POWER_STATE_LEGACY_AREA_SIZE 0x1A8
/* TODO: no Windows layout evidence for PROCESSOR_POWER_STATE on loongarch64 */
#include <potypes.h>

#define SYNCH_LEVEL 12

#define NUMBER_POOL_LOOKASIDE_LISTS 32

#define RPL_MASK              0x0003
#define KGDT64_R3_CMCODE      0x0020
#define KGDT64_R3_DATA        0x0028
#define KGDT64_R3_CMTEB       0x0050

#define EFLAGS_INTERRUPT_MASK 0x200L

#define PRCB_BUILD_DEBUG        1
#define PRCB_BUILD_UNIPROCESSOR 2

#define IPI_APC           1
#define IPI_DPC           2
#define IPI_FREEZE        4
#define IPI_PACKET_READY  6
#define IPI_SYNCH_REQUEST 16

#define IPI_FROZEN_STATE_RUNNING       0
#define IPI_FROZEN_STATE_FROZEN        2
#define IPI_FROZEN_STATE_THAW          3
#define IPI_FROZEN_STATE_OWNER         4
#define IPI_FROZEN_STATE_TARGET_FREEZE 5
#define IPI_FROZEN_STATE_SAVING        6
#define IPI_FROZEN_FLAG_ACTIVE         0x20

#define INITIAL_STALL_COUNT     100
/* TODO: no Windows LoongArch64 stack-size contract exists; the panic stack is kernel-stack sized like the RISC-V port. */
#define DOUBLE_FAULT_STACK_SIZE KERNEL_STACK_SIZE

#define LDT_ENTRY ULONG

typedef struct _KTRAP_FRAME KTRAP_FRAME, *PKTRAP_FRAME;
typedef struct _KEXCEPTION_FRAME KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;
typedef struct _KPROCESSOR_STATE KPROCESSOR_STATE, *PKPROCESSOR_STATE;
typedef struct _KPRCB KPRCB, *PKPRCB;
typedef struct _KPCR KPCR, *PKPCR;
/* TODO: fix the LASX save policy once the kernel FPU policy is decided. */
struct DECLSPEC_ALIGN(16) _KTRAP_FRAME
{
    CONTEXT Context;
    ULONG64 Badv;
    ULONG Crmd;
    ULONG Prmd;
    ULONG Euen;
    ULONG Ecfg;
    ULONG Estat;
    ULONG Spare;
    PKTRAP_FRAME PreviousTrapFrame;
    KIRQL PreviousIrql;
};

C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Context) == 0);
C_ASSERT((FIELD_OFFSET(KTRAP_FRAME, PreviousTrapFrame) & 7) == 0);
C_ASSERT((sizeof(KTRAP_FRAME) & 15) == 0);

typedef struct _KSTART_FRAME
{
    ULONG64 SystemRoutine;
    ULONG64 StartRoutine;
    ULONG64 StartContext;
    ULONG64 Return;
} KSTART_FRAME, *PKSTART_FRAME;

C_ASSERT((sizeof(KSTART_FRAME) & 15) == 0);

typedef struct DECLSPEC_ALIGN(16) _KSWITCH_FRAME
{
    ULONG64 S0;
    ULONG64 S1;
    ULONG64 S2;
    ULONG64 S3;
    ULONG64 S4;
    ULONG64 S5;
    ULONG64 S6;
    ULONG64 S7;
    ULONG64 S8;
    ULONG64 Fp;
    ULONG64 Sp;
    ULONG64 Ra;
    UCHAR ApcBypass;
    UCHAR Reserved[15];
} KSWITCH_FRAME, *PKSWITCH_FRAME;

C_ASSERT((sizeof(KSWITCH_FRAME) & 15) == 0);

typedef struct DECLSPEC_ALIGN(16) _KCALLOUT_FRAME
{
    ULONG64 S0;
    ULONG64 S1;
    ULONG64 S2;
    ULONG64 S3;
    ULONG64 S4;
    ULONG64 S5;
    ULONG64 S6;
    ULONG64 S7;
    ULONG64 S8;
    ULONG64 Fp;
    ULONG64 Sp;
    ULONG64 Ra;
    ULONG64 Fs0;
    ULONG64 Fs1;
    ULONG64 Fs2;
    ULONG64 Fs3;
    ULONG64 Fs4;
    ULONG64 Fs5;
    ULONG64 Fs6;
    ULONG64 Fs7;
    ULONG Crmd;
    ULONG Fcsr;
    PVOID CallbackStack;
    PVOID InitialStack;
    PKTRAP_FRAME TrapFrame;
    PVOID *OutputBuffer;
    PULONG OutputLength;
} KCALLOUT_FRAME, *PKCALLOUT_FRAME;

C_ASSERT((sizeof(KCALLOUT_FRAME) & 15) == 0);

typedef struct _UCALLOUT_FRAME
{
    PVOID Buffer;
    ULONG Length;
    ULONG ApiNumber;
    ULONG64 Pc;
    ULONG64 Sp;
} UCALLOUT_FRAME, *PUCALLOUT_FRAME;

C_ASSERT(sizeof(UCALLOUT_FRAME) == 32);

typedef struct _KSPECIAL_REGISTERS
{
    ULONG64 Era;
    ULONG64 Badv;
    ULONG64 Eentry;
    ULONG64 Pgdl;
    ULONG64 Pgdh;
    ULONG64 Dmw[4];
    ULONG Crmd;
    ULONG Prmd;
    ULONG Euen;
    ULONG Ecfg;
    ULONG Estat;
    ULONG Asid;
    ULONG Stlbps;
    ULONG Pwcl;
    ULONG Pwch;
    ULONG Llbctl;
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

C_ASSERT((sizeof(KSPECIAL_REGISTERS) & 7) == 0);

struct _KPROCESSOR_STATE
{
    CONTEXT ContextFrame;
    KSPECIAL_REGISTERS SpecialRegisters;
};

C_ASSERT(FIELD_OFFSET(KPROCESSOR_STATE, SpecialRegisters) == sizeof(CONTEXT));

struct _KPRCB
{
    PKTHREAD CurrentThread;
    PKTHREAD NextThread;
    PKTHREAD IdleThread;
    ULONG Number;
    ULONG MHz;
    KPROCESSOR_STATE ProcessorState;
    union
    {
        KAFFINITY GroupSetMember;
        KAFFINITY SetMember;
    };
    KSPIN_LOCK PrcbLock;
    BOOLEAN DpcRoutineActive;
    ULONG ReadySummary;
    ULONG QueueIndex;
    LIST_ENTRY DispatcherReadyListHead[MAXIMUM_PRIORITY];
    SINGLE_LIST_ENTRY DeferredReadyListHead;
    LIST_ENTRY WaitListHead;
    KSPIN_LOCK WaitLock;
    KSPIN_LOCK_QUEUE LockQueue[LockQueueMaximumLock];
    KSPIN_LOCK_QUEUE TimerTableLockQueue[LOCK_QUEUE_TIMER_TABLE_LOCKS];
    PP_LOOKASIDE_LIST PPLookasideList[16];
    GENERAL_LOOKASIDE_POOL PPNPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    GENERAL_LOOKASIDE_POOL PPPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    LONG LookasideIrpFloat;

    KDPC_DATA DpcData[2];
    PVOID DpcStack;
    LONG MaximumDpcQueueDepth;
    ULONG DpcRequestRate;
    ULONG MinimumDpcRate;
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
            ULONG DpcNormalProcessingActive:1;
            ULONG DpcNormalProcessingRequested:1;
            ULONG DpcNormalThreadSignal:1;
            ULONG DpcNormalTimerExpiration:1;
            ULONG DpcNormalDpcPresent:1;
            ULONG DpcNormalLocalInterrupt:1;
            ULONG DpcNormalPriorityAntiStarvation:1;
            ULONG DpcNormalSwapToDpcDelegate:1;
            ULONG DpcNormalSpare:8;
            ULONG DpcThreadActive:1;
            ULONG DpcThreadRequested:1;
            ULONG DpcThreadSpare:14;
        };
    };
    UCHAR ThreadDpcEnable;
    UCHAR QuantumEnd;
    UCHAR IdleSchedule;
    BOOLEAN Sleeping;
    ULONG64 TimerHand;
    ULONG64 TimerRequest;
    ULONG DpcLastCount;
    LONG DpcSetEventRequest;
    KEVENT DpcEvent;
    KDPC CallDpc;
    LONG ClockKeepAlive;
    LONG DpcWatchdogPeriodTicks;
    LONG DpcWatchdogCount;
    ULONG DpcTimeCount;
    ULONG DpcTimeLimitTicks;
    ULONG AdjustDpcThreshold;
    UCHAR SkipTick;
    KIRQL DebuggerSavedIRQL;
    UCHAR BuildType;
    PROCESSOR_POWER_STATE PowerState;

    struct _KNODE *ParentNode;
    KAFFINITY MultiThreadProcessorSet;
    PKPRCB MultiThreadSetMaster;
    UCHAR Group;
    UCHAR GroupIndex;
    ULONG CoresPerPhysicalProcessor;
    ULONG LogicalProcessorsPerCore;
    PVOID SchedulerSubNode;
    union
    {
        ULONG TopologyId[5];
        struct
        {
            ULONG ProcessorId;
            ULONG CoreId;
            ULONG ModuleId;
            ULONG DieId;
            ULONG PackageId;
        };
    };
    KAFFINITY CoreProcessorSet;
    KAFFINITY ScanSiblingMask;
    KAFFINITY LLCMask;

    ULONG KeSystemCalls;
    ULONG KeContextSwitches;
    ULONG KeAlignmentFixupCount;
    ULONG KeExceptionDispatchCount;
    ULONG KernelTime;
    ULONG UserTime;
    ULONG DpcTime;
    ULONG InterruptTime;
    ULONG InterruptCount;
    ULONG ClockInterrupts;
    ULONG DeviceInterrupts;
    volatile LONG MmPageFaultCount;
    volatile LONG MmCopyOnWriteCount;
    volatile LONG MmDemandZeroCount;
    volatile LONG MmTransitionCount;
    volatile LONG MmCacheTransitionCount;
    volatile LONG MmPageReadCount;
    volatile LONG MmPageReadIoCount;
    volatile LONG MmCacheReadCount;
    volatile LONG MmCacheIoCount;
    volatile LONG MmDirtyPagesWriteCount;
    volatile LONG MmDirtyWriteIoCount;
    volatile LONG MmMappedPagesWriteCount;
    volatile LONG MmMappedWriteIoCount;
    ULONG CcFastReadNoWait;
    ULONG CcFastReadWait;
    ULONG CcFastReadNotPossible;
    ULONG CcCopyReadNoWait;
    ULONG CcCopyReadWait;
    ULONG CcCopyReadNoWaitMiss;
    volatile LONG IoReadOperationCount;
    volatile LONG IoWriteOperationCount;
    volatile LONG IoOtherOperationCount;
    LARGE_INTEGER IoReadTransferCount;
    LARGE_INTEGER IoWriteTransferCount;
    LARGE_INTEGER IoOtherTransferCount;

    ULONG64 StartCycles;
    volatile ULONG64 CycleTime;

    volatile LONG RequestSummary;
    volatile ULONG IpiFrozen;
};

struct _KPCR
{
    ULONG_PTR CpuId;
    volatile KIRQL CurrentIrql;
    PVOID PanicStack;
    KTRAP_FRAME PanicFrame;
    KPRCB Prcb;
    PVOID KdVersionBlock;
};

typedef KPCR KIPCR, *PKIPCR;

#ifndef NTOS_MODE_USER
FORCEINLINE
PKPCR
KeGetPcr(VOID)
{
    PKPCR Pcr;

    __asm__ __volatile__("move %0, $r21" : "=r"(Pcr));
    return Pcr;
}

FORCEINLINE
PKPRCB
KeGetCurrentPrcb(VOID)
{
    return &KeGetPcr()->Prcb;
}
#endif

#endif /* _LOONGARCH64_KETYPES_H */
