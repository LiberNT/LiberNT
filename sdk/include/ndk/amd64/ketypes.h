/*++ NDK Version: 0098

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    ketypes.h (AMD64)

Abstract:

    amd64 Type definitions for the Kernel services.

Author:

    Alex Ionescu (alexi@tinykrnl.org) - Updated - 27-Feb-2006
    Timo Kreuzer (timo.kreuzer@reactos.org) - Updated - 14-Aug-2008

--*/

#ifndef _AMD64_KETYPES_H
#define _AMD64_KETYPES_H

#define _PROCESSOR_POWER_STATE_DEFINED
#include <potypes.h>
#ifndef NTOS_MODE_USER
typedef struct _PROCESSOR_POWER_STATE
{
    union
    {
        struct
        {
            struct _PPM_IDLE_STATES *IdleStates;                    // 0x000
            struct _PROC_IDLE_ACCOUNTING *IdleAccounting;           // 0x008
            ULONGLONG IdleTimeLast;                                 // 0x010
            ULONGLONG IdleTimeTotal;                                // 0x018
            volatile ULONGLONG IdleSequenceNumber;                  // 0x020
            ULONGLONG IdleTimeEntry;                                // 0x028
            union
            {
                ULONGLONG IdleTimeExpiration;                       // 0x030
                volatile LONGLONG IdleWakeTime;                     // 0x030
            };
            ULONG IdleWakeLatencyLimit;                             // 0x038
            UCHAR NonInterruptibleTransition;                       // 0x03C
            UCHAR PepWokenTransition;                               // 0x03D
            UCHAR HvTargetState;                                    // 0x03E
            ULONG TargetIdleState;                                  // 0x040
            UCHAR IdlePolicy[6];                                    // 0x044 _PROC_IDLE_POLICY
            volatile ULONG Synchronization;                         // 0x04C _PPM_IDLE_SYNCHRONIZATION_STATE
            ULONGLONG PerfFeedback[18];                             // 0x050 _PROC_FEEDBACK
            ULONG Hypervisor;                                       // 0x0E0 _PROC_HYPERVISOR_STATE
            ULONG LastSysTime;                                      // 0x0E4
            ULONGLONG WmiDispatchPtr;                               // 0x0E8
            LONG WmiInterfaceEnabled;                               // 0x0F0
            ULONGLONG FFHThrottleStateInfo[4];                      // 0x0F8 _PPM_FFH_THROTTLE_STATE_INFO
            KDPC PerfActionDpc;                                     // 0x118
            volatile LONG PerfActionMask;                           // 0x158
            ULONGLONG HvIdleCheck[2];                               // 0x160 _PROC_IDLE_SNAP
            ULONGLONG CheckContext[9];                              // 0x170 _PROC_PERF_CHECK_CONTEXT
            struct _PPM_CONCURRENCY_ACCOUNTING *Concurrency;        // 0x1B8
            struct _PPM_CONCURRENCY_ACCOUNTING *ClassConcurrency;   // 0x1C0
        };
        struct
        {
            PROCESSOR_POWER_STATE_LEGACY_MEMBERS                    // [LiberNT]
        };
    };
    UCHAR ArchitecturalEfficiencyClass;                             // 0x1C8
    volatile UCHAR PerformanceSchedulingClass;                      // 0x1C9
    volatile UCHAR EfficiencySchedulingClass;                       // 0x1CA
    UCHAR Reserved;                                                 // 0x1CB
    UCHAR Parked;                                                   // 0x1CC
    UCHAR SoftParked;                                               // 0x1CD
    UCHAR LongPriorQosPeriod;                                       // 0x1CE
    union
    {
        ULONGLONG SnapTimeLast;                                     // 0x1D0
        ULONGLONG EnergyConsumed;                                   // 0x1D0
    };
    ULONGLONG ActiveTime;                                           // 0x1D8
    ULONGLONG TotalTime;                                            // 0x1E0
    struct _POP_FX_DEVICE *FxDevice;                                // 0x1E8
    ULONGLONG LastQosTransitionTime;                                // 0x1F0
    ULONGLONG QosTransitionHysteresis;                              // 0x1F8
    ULONG RequestedQosClass;                                        // 0x200 _KHETERO_CPU_QOS
    ULONG ResolvedQosClass;                                         // 0x204 _KHETERO_CPU_QOS
    USHORT QosEquivalencyMask;                                      // 0x208
    USHORT HwFeedbackTableOffset;                                   // 0x20A
    UCHAR HwFeedbackParkHint;                                       // 0x20C
    UCHAR HeteroCoreType;                                           // 0x20D
    USHORT HwFeedbackTableIndex;                                    // 0x20E
    struct _KHETRO_HWFEEDBACK_TYPE *HwFeedbackClassList;            // 0x210
    struct _PROCESSOR_CYCLES_WORKLOAD_CLASS *EeCyclesWorkloadClassList;   // 0x218
    struct _PROCESSOR_CYCLES_WORKLOAD_CLASS *PerfCyclesWorkloadClassList; // 0x220
    UCHAR ContainmentGroupType;                                     // 0x228
    USHORT FrequencyBucketThresholds[4];                            // 0x22A
} PROCESSOR_POWER_STATE, *PPROCESSOR_POWER_STATE;
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, ClassConcurrency) == 0x1C0);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, ArchitecturalEfficiencyClass) == 0x1C8);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, FrequencyBucketThresholds) == 0x22A);
C_ASSERT(sizeof(PROCESSOR_POWER_STATE) == 0x238);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, PerformanceSchedulingClass) == 0x1C9);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, EfficiencySchedulingClass) == 0x1CA);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, Reserved) == 0x1CB);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, Parked) == 0x1CC);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, SoftParked) == 0x1CD);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, LongPriorQosPeriod) == 0x1CE);
#endif

//
// Dependencies
//

//
// Kernel Feature Bits
// See https://www.geoffchappell.com/studies/windows/km/ntoskrnl/structs/kprcb/featurebits.htm?tx=61&ts=0,1400
//
#define KF_SMEP                         0x00000001 // Win 6.2
#define KF_RDTSC                        0x00000002 // From ks386.inc, ksamd64.inc
#define KF_CR4                          0x00000004 // From ks386.inc, ksamd64.inc
#define KF_CMOV                         0x00000008
#define KF_GLOBAL_PAGE                  0x00000010 // From ks386.inc, ksamd64.inc
#define KF_LARGE_PAGE                   0x00000020 // From ks386.inc, ksamd64.inc
#define KF_MTRR                         0x00000040
#define KF_CMPXCHG8B                    0x00000080 // From ks386.inc, ksamd64.inc
#define KF_MMX                          0x00000100
#define KF_DTS                          0x00000200 // Win 5.2-6.2
#define KF_PAT                          0x00000400
#define KF_FXSR                         0x00000800
#define KF_FAST_SYSCALL                 0x00001000 // From ks386.inc, ksamd64.inc
#define KF_XMMI                         0x00002000 // SSE
#define KF_3DNOW                        0x00004000
#define KF_AMDK6MTRR                    0x00008000 // Win 5.0-6.1
#define KF_XSAVEOPT                     0x00008000 // From KF_XSAVEOPT_BIT
#define KF_XMMI64                       0x00010000 // SSE2
#define KF_BRANCH                       0x00020000 // From ksamd64.inc, Win 6.1-6.2
#define KF_00040000                     0x00040000 // Unclear
#define KF_SSE3                         0x00080000 // Win 6.0+
#define KF_CMPXCHG16B                   0x00100000 // Win 6.0-6.2
#define KF_AUTHENTICAMD                 0x00200000 // Win 6.1+
#define KF_ACNT2                        0x00400000 // Win 6.1+
#define KF_XSTATE                       0x00800000 // From ksamd64.inc, Win 6.1+
#define KF_GENUINE_INTEL                0x01000000 // Win 6.1+
#define KF_02000000                     0x02000000 // Unclear
#define KF_SLAT                         0x04000000 // Win 6.2+, Intel: EPT supported
#define KF_VIRT_FIRMWARE_ENABLED        0x08000000 // Win 6.2+
#define KF_RDWRFSGSBASE                 0x10000000 // From ksamd64.inc KF_RDWRFSGSBASE_BIT (0x1C)
#define KF_NX_BIT                       0x20000000
#define KF_NX_DISABLED                  0x40000000
#define KF_NX_ENABLED                   0x80000000
#define KF_RDRAND               0x0000000100000000ULL // Win 10.0+
#define KF_SMAP                 0x0000000200000000ULL // From ksamd64.inc
#define KF_RDTSCP               0x0000000400000000ULL // Win 10.0+
#define KF_HUGEPAGE             0x0000002000000000ULL // Win 10.0 1607+
#define KF_XSAVES               0x0000004000000000ULL // From ksamd64.inc KF_XSAVES_BIT (0x26)
#define KF_FPU_LEAKAGE          0x0000020000000000ULL // From ksamd64.inc KF_FPU_LEAKAGE_BIT (0x29)
#define KF_CAT                  0x0000100000000000ULL // From ksamd64.inc KF_CAT_BIT (0x02C)
#define KF_CET_SS               0x0000400000000000ULL // From ksamd64.inc
#define KF_SSSE3                0x0000800000000000ULL
#define KF_SSE4_1               0x0001000000000000ULL
#define KF_SSE4_2               0x0002000000000000ULL

// ReactOS specific
#define KF_AVX                  0x1000000000000000ULL
#define KF_AVX2                 0x2000000000000000ULL
#define KF_AVX512F              0x4000000000000000ULL

#define KF_XSAVEOPT_BIT                 15 // From ksamd64.inc (0x0F -> 0x8000)
#define KF_XSTATE_BIT                   23 // From ksamd64.inc (0x17 -> 0x800000)
#define KF_RDWRFSGSBASE_BIT             28 // From ksamd64.inc (0x1C -> 0x10000000)
#define KF_XSAVES_BIT                   38 // From ksamd64.inc (0x26 -> 0x4000000000)
#define KF_FPU_LEAKAGE_BIT              41 // From ksamd64.inc (0x29 -> 0x20000000000)
#define KF_CAT_BIT                      44 // From ksamd64.inc (0x2C -> 0x100000000000)

//
// Double fault stack size
//
#define DOUBLE_FAULT_STACK_SIZE KERNEL_STACK_SIZE

//
// CPU Vendors
//
typedef enum
{
    CPU_UNKNOWN,
    CPU_AMD,
    CPU_INTEL,
    CPU_VIA,
    CPU_CENTAUR = CPU_VIA
} CPU_VENDORS;

//
// Machine Types
//
#define MACHINE_TYPE_ISA        0x0000
#define MACHINE_TYPE_EISA       0x0001
#define MACHINE_TYPE_MCA        0x0002

//
// X86 80386 Segment Types
//
#define I386_TASK_GATE          0x5
#define I386_TSS                0x9
#define I386_ACTIVE_TSS         0xB
#define I386_CALL_GATE          0xC
#define I386_INTERRUPT_GATE     0xE
#define I386_TRAP_GATE          0xF

//
// Selector Names
//
#define RPL_MASK                0x0003
#define MODE_MASK               0x0001
#define KGDT64_NULL             0x0000
#define KGDT64_R0_CODE          0x0010
#define KGDT64_R0_DATA          0x0018
#define KGDT64_R3_CMCODE        0x0020
#define KGDT64_R3_DATA          0x0028
#define KGDT64_R3_CODE          0x0030
#define KGDT64_SYS_TSS          0x0040
#define KGDT64_R3_CMTEB         0x0050
#define KGDT64_R0_LDT           0x0060

//
// CR0
//
#define CR0_PE                  0x00000001
#define CR0_MP                  0x00000002
#define CR0_EM                  0x00000004
#define CR0_TS                  0x00000008
#define CR0_ET                  0x00000010
#define CR0_NE                  0x00000020
#define CR0_WP                  0x00010000
#define CR0_AM                  0x00040000
#define CR0_NW                  0x20000000
#define CR0_CD                  0x40000000
#define CR0_PG                  0x80000000

//
// CR4
//
#define CR4_VME                 0x1
#define CR4_PVI                 0x2
#define CR4_TSD                 0x4
#define CR4_DE                  0x8
#define CR4_PSE                 0x10
#define CR4_PAE                 0x20
#define CR4_MCE                 0x40
#define CR4_PGE                 0x80
#define CR4_FXSR                0x200
#define CR4_XMMEXCPT            0x400
#define CR4_CHANNELS            0x800
#define CR4_XSAVE               0x40000

//
// DR7
//
#define DR7_LEGAL               0xFFFF0355
#define DR7_ACTIVE              0x00000355
#define DR7_TRACE_BRANCH        0x00000200
#define DR7_LAST_BRANCH         0x00000100

//
// Debug flags
//
#define DEBUG_ACTIVE_DR7                        0x0001
#define DEBUG_ACTIVE_INSTRUMENTED               0x0002
#define DEBUG_ACTIVE_DBG_INSTRUMENTED           0x0003
#define DEBUG_ACTIVE_MINIMAL_THREAD             0x0004
#define DEBUG_ACTIVE_PRIMARY_THREAD             0x0080
#define DEBUG_ACTIVE_PRIMARY_THREAD_BIT         0x0007
#define DEBUG_ACTIVE_PRIMARY_THREAD_LOCK_BIT    0x001F
#define DEBUG_ACTIVE_SCHEDULED_THREAD           0x0040
#define DEBUG_ACTIVE_SCHEDULED_THREAD_BIT       0x0006
#define DEBUG_ACTIVE_SCHEDULED_THREAD_LOCK_BIT  0x001E
#define DEBUG_ACTIVE_SCHEDULED_THREAD_LOCK      0x40000000

//
// EFlags
//
#define EFLAGS_CF               0x01L
#define EFLAGS_ZF               0x40L
#define EFLAGS_TF               0x100L
#define EFLAGS_INTERRUPT_MASK   0x200L
#define EFLAGS_DF               0x400L
#define EFLAGS_IOPL             0x3000L
#define EFLAGS_NESTED_TASK      0x4000L
//#define EFLAGS_NF               0x4000
#define EFLAGS_RF               0x10000
#define EFLAGS_V86_MASK         0x20000
#define EFLAGS_ALIGN_CHECK      0x40000
#define EFLAGS_VIF              0x80000
#define EFLAGS_VIP              0x100000
#define EFLAGS_ID               0x200000
#define EFLAGS_USER_SANITIZE    0x200DD7
#define EFLAG_SIGN              0x8000
#define EFLAG_ZERO              0x4000
#define EFLAGS_TF_MASK          0x0100
#define EFLAGS_TF_SHIFT         0x0008
#define EFLAGS_ID_MASK          0x200000
#define EFLAGS_IF_MASK          0x0200
#define EFLAGS_IF_SHIFT         0x0009

//
// MXCSR Floating Control/Status Bit Masks
//
#define XSW_INVALID_OPERATION   0x0001
#define XSW_DENORMAL            0x0002
#define XSW_ZERO_DIVIDE         0x0004
#define XSW_OVERFLOW            0x0008
#define XSW_UNDERFLOW           0x0010
#define XSW_PRECISION           0x0020
#define XCW_INVALID_OPERATION   0x0080
#define XCW_DENORMAL            0x0100
#define XCW_ZERO_DIVIDE         0x0200
#define XCW_OVERFLOW            0x0400
#define XCW_UNDERFLOW           0x0800
#define XCW_PRECISION           0x1000
#define XCW_ROUND_CONTROL       0x6000
#define XCW_FLUSH_ZERO          0x8000
#define XSW_ERROR_MASK          0x003F
#define XSW_ERROR_SHIFT         7

//
// Legacy floating status word bit masks.
//
#define FSW_INVALID_OPERATION   0x0001
#define FSW_DENORMAL            0x0002
#define FSW_ZERO_DIVIDE         0x0004
#define FSW_OVERFLOW            0x0008
#define FSW_UNDERFLOW           0x0010
#define FSW_PRECISION           0x0020
#define FSW_STACK_FAULT         0x0040
#define FSW_ERROR_SUMMARY       0x0080
#define FSW_CONDITION_CODE_0    0x0100
#define FSW_CONDITION_CODE_1    0x0200
#define FSW_CONDITION_CODE_2    0x0400
#define FSW_CONDITION_CODE_3    0x4000
#define FSW_ERROR_MASK          0x003F

//
// Machine Specific Registers
//
#define MSR_EFER                0xC0000080
#define MSR_STAR                0xC0000081
#define MSR_LSTAR               0xC0000082
#define MSR_CSTAR               0xC0000083
#define MSR_SYSCALL_MASK        0xC0000084
#define MSR_FS_BASE             0xC0000100
#define MSR_GS_BASE             0xC0000101
#define MSR_GS_SWAP             0xC0000102
#define MSR_MCG_CAP             0x0179
#define MSR_MCG_STATUS          0x017A
#define MSR_MC0_STATUS          0x0401
#define MSR_MC0_ADDR            0x0402
#define MSR_MC0_MISC            0x0403
#define MSR_AMD_ACCESS          0x9C5A203A
#define MSR_IA32_MISC_ENABLE    0x000001A0
#define MSR_LAST_BRANCH_FROM    0x01DB
#define MSR_LAST_BRANCH_TO      0x01DC
#define MSR_LAST_EXCEPTION_FROM 0x01DD
#define MSR_LAST_EXCEPTION_TO   0x01DE

//
// Caching values for the PAT MSR
//
#define PAT_UC                  0ULL
#define PAT_WC                  1ULL
#define PAT_WT                  4ULL
#define PAT_WP                  5ULL
#define PAT_WB                  6ULL
#define PAT_UCM                 7ULL

//
// Flags in MSR_EFER
//
#define MSR_SCE                 0x0001
#define MSR_LME                 0x0100
#define MSR_LMA                 0x0400
#define MSR_NXE                 0x0800
#define MSR_PAT                 0x0277
#define MSR_DEBUG_CTL           0x01D9

//
//  Flags in MSR_IA32_MISC_ENABLE
//
#define MSR_XD_ENABLE_MASK      0xFFFFFFFB

//
//  Flags in MSR_DEBUG_CTL
//
#define MSR_DEBUG_CTL_LBR       0x0001
#define MSR_DEBUG_CTL_BTF       0x0002

//
// IPI Types
//
#define IPI_APC                 1
#define IPI_DPC                 2
#define IPI_FREEZE              4
#define IPI_PACKET_READY        8
#define IPI_SYNCH_REQUEST       16

//
// Flags for KPRCB::IpiFrozen
//
// Values shown with !ipi extension in WinDbg:
// 0 = [Running], 1 = [Unknown], 2 = [Frozen], 3 = [Thaw], 4 = [Freeze Owner]
// 5 = [Target Freeze], 6-15 = [Unknown]
// 0x20 = [Active] (flag)
//
#define IPI_FROZEN_STATE_RUNNING 0
#define IPI_FROZEN_STATE_FROZEN 2
#define IPI_FROZEN_STATE_THAW 3
#define IPI_FROZEN_STATE_OWNER 4
#define IPI_FROZEN_STATE_TARGET_FREEZE 5
#define IPI_FROZEN_STATE_SAVING 6
#define IPI_FROZEN_FLAG_ACTIVE 0x20

//
// PRCB Flags
//
#define PRCB_MINOR_VERSION      1
#define PRCB_MAJOR_VERSION      1
#define PRCB_BUILD_DEBUG        1
#define PRCB_BUILD_UNIPROCESSOR 2

//
// Exception active flags
//
#define KEXCEPTION_ACTIVE_INTERRUPT_FRAME 0x0000
#define KEXCEPTION_ACTIVE_EXCEPTION_FRAME 0x0001
#define KEXCEPTION_ACTIVE_SERVICE_FRAME   0x0002

//
// HAL Variables
//
#define PRIMARY_VECTOR_BASE     0x30
#define MAXIMUM_IDTVECTOR       0xFF
#define INITIAL_STALL_COUNT     100
#define MM_HAL_VA_START         0xFFFFFFFFFFC00000ULL /* This is Vista+ */
#define MM_HAL_VA_END           0xFFFFFFFFFFFFFFFFULL
#define APIC_BASE               0xFFFFFFFFFFFE0000ULL

//
// IOPM Definitions
//
#define IO_ACCESS_MAP_NONE      0
#define IOPM_OFFSET             FIELD_OFFSET(KTSS, IoMaps[0].IoMap)
#define KiComputeIopmOffset(MapNumber)              \
    (MapNumber == IO_ACCESS_MAP_NONE) ?             \
        (USHORT)(sizeof(KTSS)) :                    \
        (USHORT)(FIELD_OFFSET(KTSS, IoMaps[MapNumber-1].IoMap))

//
// Static Kernel-Mode Address start (use MM_KSEG0_BASE for actual)
//
#define KSEG0_BASE 0xfffff80000000000ULL

#define NMI_STACK_SIZE 0x2000
#define ISR_STACK_SIZE 0x6000

/* Number of bytes reserved for syscall parameters */
#define MAX_SYSCALL_PARAM_SIZE (16 * 8)

//
// Synchronization-level IRQL
//
#ifndef CONFIG_SMP
#define SYNCH_LEVEL             DISPATCH_LEVEL
#else
#define SYNCH_LEVEL             (IPI_LEVEL - 2)
#endif

//
// Number of pool lookaside lists per pool in the PRCB
//
#define NUMBER_POOL_LOOKASIDE_LISTS 32

//
// Structure for CPUID
//
typedef union _CPU_INFO
{
    UINT32 AsUINT32[4];
    struct
    {
        ULONG Eax;
        ULONG Ebx;
        ULONG Ecx;
        ULONG Edx;
    };
} CPU_INFO, *PCPU_INFO;

//
// Trap Frame Definition
//
typedef struct _KTRAP_FRAME
{
    UINT64 P1Home;
    UINT64 P2Home;
    UINT64 P3Home;
    UINT64 P4Home;
    UINT64 P5;
    CHAR PreviousMode;
    UCHAR PreviousIrql;
    UCHAR FaultIndicator;
    UCHAR ExceptionActive;
    ULONG MxCsr;
    UINT64 Rax;
    UINT64 Rcx;
    UINT64 Rdx;
    UINT64 R8;
    UINT64 R9;
    UINT64 R10;
    UINT64 R11;
    union
    {
        UINT64 GsBase;
        UINT64 GsSwap;
    };
    M128A Xmm0;
    M128A Xmm1;
    M128A Xmm2;
    M128A Xmm3;
    M128A Xmm4;
    M128A Xmm5;
    union
    {
        UINT64 FaultAddress;
        UINT64 ContextRecord;
        UINT64 TimeStampCKCL;
    };
    UINT64 Dr0;
    UINT64 Dr1;
    UINT64 Dr2;
    UINT64 Dr3;
    UINT64 Dr6;
    UINT64 Dr7;
    union
    {
        struct
        {
            UINT64 DebugControl;
            UINT64 LastBranchToRip;
            UINT64 LastBranchFromRip;
            UINT64 LastExceptionToRip;
            UINT64 LastExceptionFromRip;
        };
        struct
        {
            UINT64 LastBranchControl;
            ULONG LastBranchMSR;
        };
    };
    USHORT SegDs;
    USHORT SegEs;
    USHORT SegFs;
    USHORT SegGs;
    UINT64 TrapFrame;
    UINT64 Rbx;
    UINT64 Rdi;
    UINT64 Rsi;
    UINT64 Rbp;
    union
    {
        UINT64 ErrorCode;
        UINT64 ExceptionFrame;
        UINT64 TimeStampKlog;
    };
    UINT64 Rip;
    USHORT SegCs;
    UCHAR Fill0;
    UCHAR Logging;
    USHORT Fill1[2];
    ULONG EFlags;
    ULONG Fill2;
    UINT64 Rsp;
    USHORT SegSs;
    USHORT Fill3;
    LONG CodePatchCycle;
} KTRAP_FRAME, *PKTRAP_FRAME;

//
// Dummy LDT_ENTRY
//
#ifndef _LDT_ENTRY_DEFINED
#define _LDT_ENTRY_DEFINED
typedef ULONG LDT_ENTRY;
#endif

//
// GDT Entry Definition
//
typedef union _KGDTENTRY64
{
    struct
    {
        USHORT LimitLow;
        USHORT BaseLow;
        union
        {
            struct
            {
                UCHAR BaseMiddle;
                UCHAR Flags1;
                UCHAR Flags2;
                UCHAR BaseHigh;
            } Bytes;
            struct
            {
                ULONG BaseMiddle:8;
                ULONG Type:5;
                ULONG Dpl:2;
                ULONG Present:1;
                ULONG LimitHigh:4;
                ULONG System:1;
                ULONG LongMode:1;
                ULONG DefaultBig:1;
                ULONG Granularity:1;
                ULONG BaseHigh:8;
            } Bits;
        };
        ULONG BaseUpper;
        ULONG MustBeZero;
    };
    UINT64 Alignment;
} KGDTENTRY64, *PKGDTENTRY64;
#define KGDTENTRY KGDTENTRY64
#define PKGDTENTRY PKGDTENTRY64

//
// IDT Entry Access Definition
//
typedef struct _KIDT_ACCESS
{
    union
    {
        struct
        {
            UCHAR Reserved;
            UCHAR SegmentType:4;
            UCHAR SystemSegmentFlag:1;
            UCHAR Dpl:2;
            UCHAR Present:1;
        };
        USHORT Value;
    };
} KIDT_ACCESS, *PKIDT_ACCESS;

//
// IDT Entry Definition
//
typedef union _KIDTENTRY64
{
    struct
    {
        USHORT OffsetLow;
        USHORT Selector;
        USHORT IstIndex:3;
        USHORT Reserved0:5;
        USHORT Type:5;
        USHORT Dpl:2;
        USHORT Present:1;
        USHORT OffsetMiddle;
        ULONG OffsetHigh;
        ULONG Reserved1;
    };
    UINT64 Alignment;
} KIDTENTRY64, *PKIDTENTRY64;
#define KIDTENTRY KIDTENTRY64
#define PKIDTENTRY PKIDTENTRY64

typedef struct _KDESCRIPTOR
{
    USHORT Pad[3];
    USHORT Limit;
    PVOID Base;
} KDESCRIPTOR, *PKDESCRIPTOR;

#ifndef NTOS_MODE_USER

//
// Special Registers Structure (outside of CONTEXT)
//
typedef struct _KSPECIAL_REGISTERS
{
    ULONG64 Cr0;
    ULONG64 Cr2;
    ULONG64 Cr3;
    ULONG64 Cr4;
    ULONG64 KernelDr0;
    ULONG64 KernelDr1;
    ULONG64 KernelDr2;
    ULONG64 KernelDr3;
    ULONG64 KernelDr6;
    ULONG64 KernelDr7;
    KDESCRIPTOR Gdtr;
    KDESCRIPTOR Idtr;
    USHORT Tr;
    USHORT Ldtr;
    ULONG MxCsr;
    ULONG64 DebugControl;
    ULONG64 LastBranchToRip;
    ULONG64 LastBranchFromRip;
    ULONG64 LastExceptionToRip;
    ULONG64 LastExceptionFromRip;
    ULONG64 Cr8;
    ULONG64 MsrGsBase;
    ULONG64 MsrGsSwap;
    ULONG64 MsrStar;
    ULONG64 MsrLStar;
    ULONG64 MsrCStar;
    ULONG64 MsrSyscallMask;
    ULONG64 Xcr0;
    ULONG64 MsrFsBase;
    ULONG64 SpecialPadding0;
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

//
// Processor State Data
//
typedef struct _KPROCESSOR_STATE
{
    KSPECIAL_REGISTERS SpecialRegisters;
    CONTEXT ContextFrame;
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;


typedef struct _KREQUEST_PACKET
{
    PVOID CurrentPacket[3];
    PVOID WorkerRoutine;
} KREQUEST_PACKET, *PKREQUEST_PACKET;

typedef struct _REQUEST_MAILBOX
{
    struct _REQUEST_MAILBOX *Next;
    INT64 RequestSummary;
    KREQUEST_PACKET RequestPacket;
    volatile LONG *SubNodeTargetCountAddr;
    volatile LONG SubNodeTargetCount;
} REQUEST_MAILBOX, *PREQUEST_MAILBOX;

//
// Processor Region Control Block
//
typedef union _KPRCBFLAG
{
    volatile LONG PrcbFlags;
    struct
    {
        ULONG BamQosLevel:8;
        ULONG PendingQosUpdate:1;
        ULONG CacheIsolationEnabled:1;
        ULONG TracepointActive:1;
        ULONG LongDpcRunning:1;
        ULONG PrcbFlagsReserved:20;
    };
} KPRCBFLAG, *PKPRCBFLAG;

typedef struct _KPRCB
{
    ULONG MxCsr; // 0x00000
    UCHAR LegacyNumber; // 0x00004
    UCHAR ReservedMustBeZero; // 0x00005
    UCHAR InterruptRequest; // 0x00006
    UCHAR IdleHalt; // 0x00007
    struct _KTHREAD * CurrentThread; // 0x00008
    struct _KTHREAD * NextThread; // 0x00010
    struct _KTHREAD * IdleThread; // 0x00018
    UCHAR NestingLevel; // 0x00020
    UCHAR ClockOwner; // 0x00021
    union
    {
        UCHAR PendingTickFlags; // 0x00022
        struct
        {
            UCHAR PendingTick:1;
            UCHAR PendingBackupTick:1;
        };
    };
    UCHAR IdleState; // 0x00023
    ULONG Number; // 0x00024
    UINT64 RspBase; // 0x00028
    UINT64 PrcbLock; // 0x00030
    PCHAR PriorityState; // 0x00038
    CHAR CpuType; // 0x00040
    CHAR CpuID; // 0x00041
    union
    {
        USHORT CpuStep; // 0x00042
        struct
        {
            UCHAR CpuStepping;
            UCHAR CpuModel;
        };
    };
    ULONG MHz; // 0x00044
    ULONGLONG HalReserved[8]; // 0x00048
    USHORT MinorVersion; // 0x00088
    USHORT MajorVersion; // 0x0008A
    UCHAR BuildType; // 0x0008C
    UCHAR CpuVendor; // 0x0008D
    UCHAR LegacyCoresPerPhysicalProcessor; // 0x0008E
    UCHAR LegacyLogicalProcessorsPerCore; // 0x0008F
    ULONGLONG TscFrequency; // 0x00090
    struct _KPRCB_TRACEPOINT_LOG *TracepointLog; // 0x00098
    ULONG CoresPerPhysicalProcessor; // 0x000A0
    ULONG LogicalProcessorsPerCore; // 0x000A4
    ULONG SelfIpiRequestSummary; // 0x000A8
    UCHAR QpcToTscIncrementShift; // 0x000AC
    UCHAR PrcbPad03[3]; // 0x000AD
    ULONGLONG QpcToTscIncrement; // 0x000B0
    ULONGLONG PrcbPad04; // 0x000B8
    struct _KSCHEDULER_SUBNODE *SchedulerSubNode; // 0x000C0
    union // 0x000C8
    {
        ULONGLONG GroupSetMember;
        ULONG64 SetMember; // [LiberNT] legacy alias
    };
    UCHAR Group; // 0x000D0
    UCHAR GroupIndex; // 0x000D1
    UCHAR PrcbPad05[2]; // 0x000D2
    ULONG InitialApicId; // 0x000D4
    ULONG ScbOffset; // 0x000D8
    ULONG ApicMask; // 0x000DC
    PVOID AcpiReserved; // 0x000E0
    ULONG CFlushSize; // 0x000E8
    union
    {
        KPRCBFLAG PrcbFlags; // 0x000EC
        ULONG PrcbFlagsReserved;
    };
    ULONGLONG PrcbPad11[2]; // 0x000F0
    KPROCESSOR_STATE ProcessorState; // 0x00100
    struct _XSAVE_AREA_HEADER *ExtendedSupervisorState; // 0x006C0
    ULONG ProcessorSignature; // 0x006C8
    ULONG ProcessorFlags; // 0x006CC
    union
    {
        ULONGLONG PrcbPad12a[4]; // 0x006D0
        struct
        {
            ULONGLONG TrappedSecurityDomain; // 0x006D0
            UCHAR BpbState[2]; // 0x006D8 _KPRCB_BPB_STATE
            UCHAR BpbTrappedBpbState[2]; // 0x006DA _KPRCB_BPB_STATE
            UCHAR BpbFeatures[1]; // 0x006DC _KPRCB_BPB_FEATURES
            UCHAR PrcbPad12b; // 0x006DD
            UCHAR BpbRetpolineState[1]; // 0x006DE _KPRCB_BPB_RETPOLINE_STATE
            UCHAR BpbRetpolineStateOverflow; // 0x006DF
            volatile SHORT PairRegister; // 0x006E0
            USHORT BpbRetpolineExitSpecCtrl; // 0x006E2
            USHORT BpbTrappedRetpolineExitSpecCtrl; // 0x006E4
            USHORT BpbCurrentSpecCtrl; // 0x006E6
            USHORT BpbKernelSpecCtrl; // 0x006E8
            USHORT BpbNmiSpecCtrl; // 0x006EA
            USHORT BpbUserSpecCtrl; // 0x006EC
            USHORT PrcbPad12c[1]; // 0x006EE
        };
    };
    KSPIN_LOCK_QUEUE LockQueue[17]; // 0x006F0
    PP_LOOKASIDE_LIST PPLookasideList[16]; // 0x00800
    UCHAR PPNxPagedLookasideList[3072]; // 0x00900 _GENERAL_LOOKASIDE_POOL[32]
    GENERAL_LOOKASIDE_POOL PPNPagedLookasideList[32]; // 0x01500
    GENERAL_LOOKASIDE_POOL PPPagedLookasideList[32]; // 0x02100
    ULONGLONG MsrIa32TsxCtrl; // 0x02D00
    SINGLE_LIST_ENTRY DeferredReadyListHead; // 0x02D08
    volatile LONG MmPageFaultCount; // 0x02D10
    volatile LONG MmCopyOnWriteCount; // 0x02D14
    volatile LONG MmTransitionCount; // 0x02D18
    volatile LONG MmDemandZeroCount; // 0x02D1C
    volatile LONG MmPageReadCount; // 0x02D20
    volatile LONG MmPageReadIoCount; // 0x02D24
    volatile LONG MmDirtyPagesWriteCount; // 0x02D28
    volatile LONG MmDirtyWriteIoCount; // 0x02D2C
    volatile LONG MmMappedPagesWriteCount; // 0x02D30
    volatile LONG MmMappedWriteIoCount; // 0x02D34
    ULONG KeSystemCalls; // 0x02D38
    ULONG KeContextSwitches; // 0x02D3C
    ULONG PrcbPad40; // 0x02D40
    ULONG CcFastReadNoWait; // 0x02D44
    ULONG CcFastReadWait; // 0x02D48
    ULONG CcFastReadNotPossible; // 0x02D4C
    ULONG CcCopyReadNoWait; // 0x02D50
    ULONG CcCopyReadWait; // 0x02D54
    ULONG CcCopyReadNoWaitMiss; // 0x02D58
    volatile LONG IoReadOperationCount; // 0x02D5C
    volatile LONG IoWriteOperationCount; // 0x02D60
    volatile LONG IoOtherOperationCount; // 0x02D64
    LARGE_INTEGER IoReadTransferCount; // 0x02D68
    LARGE_INTEGER IoWriteTransferCount; // 0x02D70
    LARGE_INTEGER IoOtherTransferCount; // 0x02D78
    volatile LONG PacketBarrier; // 0x02D80
    volatile LONG TargetCount; // 0x02D84
    volatile ULONG IpiFrozen; // 0x02D88
    ULONG PrcbPad30; // 0x02D8C
    PVOID IsrDpcStats; // 0x02D90
    ULONG DeviceInterrupts; // 0x02D98
    LONG LookasideIrpFloat; // 0x02D9C
    ULONG InterruptLastCount; // 0x02DA0
    ULONG InterruptRate; // 0x02DA4
    ULONGLONG PrcbPad31; // 0x02DA8
    struct _KPRCB *PairPrcb; // 0x02DB0
    UCHAR StaticAffinity[2080]; // 0x02DB8 _KSTATIC_AFFINITY_BLOCK
    UCHAR DeferredDispatchInterrupts[528]; // 0x035D8 _KSOFTWARE_INTERRUPT_BATCH
    struct _KI_RESCHEDULE_CONTEXT *StaticRescheduleContext; // 0x037E8
    struct _KCPU_PARTITION *CpuPartition; // 0x037F0
    ULONGLONG PrcbPad35[1]; // 0x037F8
    SLIST_HEADER InterruptObjectPool; // 0x03800
    struct _RTL_HASH_TABLE *DpcRuntimeHistoryHashTable; // 0x03810
    struct _KDPC *DpcRuntimeHistoryHashTableCleanupDpc; // 0x03818
    UCHAR CurrentDpcRoutine[8]; // 0x03820 0x11AF*
    ULONGLONG CurrentDpcRuntimeHistoryCached; // 0x03828
    ULONGLONG CurrentDpcStartTime; // 0x03830
    struct _KTHREAD *DpcDelegateThread; // 0x03838
    KDPC_DATA DpcData[2]; // 0x03840
    PVOID DpcStack; // 0x038A0
    LONG MaximumDpcQueueDepth; // 0x038A8
    ULONG DpcRequestRate; // 0x038AC
    ULONG MinimumDpcRate; // 0x038B0
    ULONG DpcLastCount; // 0x038B4
    UCHAR ThreadDpcEnable; // 0x038B8
    UCHAR QuantumEnd; // 0x038B9
    UCHAR DpcRoutineActive; // 0x038BA
    UCHAR IdleSchedule; // 0x038BB
    union
    {
        volatile LONG DpcRequestSummary; // 0x038BC
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
    ULONG LastTick; // 0x038C0
    ULONG ClockInterrupts; // 0x038C4
    ULONG ReadyScanTick; // 0x038C8
    ULONG SingleDpcSoftTimeLimitTicks; // 0x038CC
    struct _KSINGLE_DPC_SOFT_TIMEOUT_EVENT_INFO *SingleDpcSoftTimeoutEventInfo; // 0x038D0
    ULONG CumulativeDpcSoftTimeLimitTicks; // 0x038D8
    ULONG DpcWatchdogProfileBufferSize; // 0x038DC
    UCHAR RcuData[32]; // 0x038E0 _KE_PRCB_RCU_DATA
    PVOID InterruptObject[256]; // 0x03900
    union // 0x04100
    {
        UCHAR TimerTable[16920]; // _KTIMER_TABLE
        KSPIN_LOCK_QUEUE TimerTableLockQueue[LOCK_QUEUE_TIMER_TABLE_LOCKS]; // [LiberNT] timer table locks
    };
    PVOID DpcLog; // 0x08318
    ULONG DpcLogIndex; // 0x08320
    ULONG DpcLogBufferSize; // 0x08324
    ULONG PrcbPad92[6]; // 0x08328
    UCHAR DpcGate[24]; // 0x08340 _KGATE
    PVOID PrcbPad52; // 0x08358
    KDPC CallDpc; // 0x08360
    LONG ClockKeepAlive; // 0x083A0
    UCHAR PrcbPad60[2]; // 0x083A4
    union
    {
        USHORT CombinedNmiMceActive; // 0x083A6
        struct
        {
            UCHAR NmiActive; // 0x083A6
            UCHAR MceActive; // 0x083A7
        };
    };
    LONG DpcWatchdogPeriodTicks; // 0x083A8
    LONG DpcWatchdogCount; // 0x083AC
    volatile LONG KeSpinLockOrdering; // 0x083B0
    ULONG DpcWatchdogProfileCumulativeDpcThresholdTicks; // 0x083B4
    PVOID CachedPtes; // 0x083B8
    LIST_ENTRY WaitListHead; // 0x083C0
    UINT64 WaitLock; // 0x083D0
    ULONG ReadySummary; // 0x083D8
    LONG AffinitizedSelectionMask; // 0x083DC
    ULONG QueueIndex; // 0x083E0
    ULONG NormalPriorityQueueIndex; // 0x083E4
    ULONG NormalPriorityReadyScanTick; // 0x083E8
    ULONG DpcWatchdogSequenceNumber; // 0x083EC
    UCHAR TimerExpirationDpc[64]; // 0x083F0 _KDPC
    UCHAR ScbQueue[16]; // 0x08430 _RTL_RB_TREE
    LIST_ENTRY DispatcherReadyListHead[32]; // 0x08440
    ULONG InterruptCount; // 0x08640
    ULONG KernelTime; // 0x08644
    ULONG UserTime; // 0x08648
    ULONG DpcTime; // 0x0864C
    ULONG InterruptTime; // 0x08650
    ULONG AdjustDpcThreshold; // 0x08654
    UCHAR DebuggerSavedIRQL; // 0x08658
    UCHAR GroupSchedulingOverQuota; // 0x08659
    volatile UCHAR DeepSleep; // 0x0865A
    UCHAR PrcbPad80; // 0x0865B
    ULONG DpcTimeCount; // 0x0865C
    ULONG DpcTimeLimitTicks; // 0x08660
    ULONG PeriodicCount; // 0x08664
    ULONG PeriodicBias; // 0x08668
    ULONG AvailableTime; // 0x0866C
    ULONG KeExceptionDispatchCount; // 0x08670
    ULONG ReadyThreadCount; // 0x08674
    ULONGLONG ReadyQueueExpectedRunTime; // 0x08678
    UINT64 StartCycles; // 0x08680
    ULONGLONG TaggedCycles[4]; // 0x08688
    ULONGLONG AffinitizedCycles; // 0x086A8
    PULONG64 CyclesByThreadType; // 0x086B0
    ULONG CpuCycleScalingFactor; // 0x086B8
    volatile USHORT PerformanceScoreByClass[8]; // 0x086BC
    volatile USHORT EfficiencyScoreByClass[8]; // 0x086CC
    UCHAR CycleAccumulationInitialized; // 0x086DC
    UCHAR PrcbPad83[3]; // 0x086DD
    UCHAR MceRecoveryContext[16]; // 0x086E0 _KMCE_RECOVERY_CONTEXT
    struct _KFORCE_PARK_DUTY_CYCLE_DATA *ForceParkDutyCycleData; // 0x086F0
    ULONGLONG StartCyclesQpc; // 0x086F8
    ULONGLONG CycleTimeQpc; // 0x08700
    ULONGLONG NumberOfSecureFaults; // 0x08708
    ULONG PrcbPad84[8]; // 0x08710
    VOID *CachedStacks[2]; // 0x08730
    ULONG DpcWatchdogProfileSingleDpcThresholdTicks; // 0x08740
    LONG PrcbPad82[3]; // 0x08744
    ULONG PageColor; // 0x08750
    ULONG NodeColor; // 0x08754
    ULONG PrcbPad87[2]; // 0x08758
    UCHAR PrcbPad81[5]; // 0x08760
    UCHAR SystemWorkKickInProgress; // 0x08765
    UCHAR ExceptionStackActive; // 0x08766
    UCHAR TbFlushListActive; // 0x08767
    PVOID ExceptionStack; // 0x08768
    volatile LONGLONG MmSpinLockOrdering; // 0x08770
    UINT64 CycleTime; // 0x08778
    UCHAR Cycles[64]; // 0x08780 ULONGLONG[2][4]
    ULONG CcFastMdlReadNoWait; // 0x087C0
    ULONG CcFastMdlReadWait; // 0x087C4
    ULONG CcFastMdlReadNotPossible; // 0x087C8
    ULONG CcMapDataNoWait; // 0x087CC
    ULONG CcMapDataWait; // 0x087D0
    ULONG CcPinMappedDataCount; // 0x087D4
    ULONG CcPinReadNoWait; // 0x087D8
    ULONG CcPinReadWait; // 0x087DC
    ULONG CcMdlReadNoWait; // 0x087E0
    ULONG CcMdlReadWait; // 0x087E4
    ULONG CcLazyWriteHotSpots; // 0x087E8
    ULONG CcLazyWriteIos; // 0x087EC
    ULONG CcLazyWritePages; // 0x087F0
    ULONG CcDataFlushes; // 0x087F4
    ULONG CcDataPages; // 0x087F8
    ULONG CcLostDelayedWrites; // 0x087FC
    ULONG CcFastReadResourceMiss; // 0x08800
    ULONG CcCopyReadWaitMiss; // 0x08804
    ULONG CcFastMdlReadResourceMiss; // 0x08808
    ULONG CcMapDataNoWaitMiss; // 0x0880C
    ULONG CcMapDataWaitMiss; // 0x08810
    ULONG CcPinReadNoWaitMiss; // 0x08814
    ULONG CcPinReadWaitMiss; // 0x08818
    ULONG CcMdlReadNoWaitMiss; // 0x0881C
    ULONG CcMdlReadWaitMiss; // 0x08820
    ULONG CcReadAheadIos; // 0x08824
    volatile LONG MmCacheTransitionCount; // 0x08828
    volatile LONG MmCacheReadCount; // 0x0882C
    volatile LONG MmCacheIoCount; // 0x08830
    ULONG PrcbPad91; // 0x08834
    PVOID MmInternal; // 0x08838
    PROCESSOR_POWER_STATE PowerState; // 0x08840
    ULONGLONG PrcbPad96[1]; // 0x08A78
    PVOID PrcbPad90; // 0x08A80
    UCHAR ScbList[16]; // 0x08A88 _LIST_ENTRY
    UCHAR ForceIdleDpc[64]; // 0x08A98 _KDPC
    KDPC DpcWatchdogDpc; // 0x08AD8
    ULONGLONG PrcbPad98[7]; // 0x08B18
    PVOID VmInternal; // 0x08B50
    CACHE_DESCRIPTOR Cache[5]; // 0x08B58
    ULONG CacheCount; // 0x08B94
    volatile ULONG CachedCommit; // 0x08B98
    volatile ULONG CachedResidentAvailable; // 0x08B9C
    PVOID WheaInfo; // 0x08BA0
    PVOID EtwSupport; // 0x08BA8
    PVOID ExSaPageArray; // 0x08BB0
    ULONG KeAlignmentFixupCount; // 0x08BB8
    ULONG PrcbPad95; // 0x08BBC
    SLIST_HEADER HypercallPageList; // 0x08BC0
    PULONG64 StatisticsPage; // 0x08BD0
    ULONGLONG GenerationTarget; // 0x08BD8
    UCHAR VirtualApicAssistPage[8]; // 0x08BE0 _LARGE_INTEGER
    ULONGLONG PrcbPad85[3]; // 0x08BE8
    PVOID HypercallCachedPages; // 0x08C00
    PVOID VirtualApicAssist; // 0x08C08
    UCHAR PackageProcessorSet[264]; // 0x08C10 _KAFFINITY_EX
    ULONGLONG PrcbPad86[32]; // 0x08D18
    union
    {
        struct
        {
            ULONG ProcessorId; // 0x08E18
            ULONG CoreId; // 0x08E1C
            ULONG ModuleId; // 0x08E20
            ULONG DieId; // 0x08E24
            ULONG PackageId; // 0x08E28
            ULONG ComplexId; // 0x08E2C
        };
        ULONG TopologyId[6]; // 0x08E18
    };
    ULONG PrcbPad101[4]; // 0x08E30
    ULONGLONG SharedReadyQueueMask; // 0x08E40
    struct _KSHARED_READY_QUEUE *SharedReadyQueue; // 0x08E48
    ULONG SharedQueueScanOwner; // 0x08E50
    ULONG ScanSiblingIndex; // 0x08E54
    struct _KCORE_CONTROL_BLOCK *CoreControlBlock; // 0x08E58
    UINT64 CoreProcessorSet; // 0x08E60
    ULONGLONG ScanSiblingMask; // 0x08E68
    ULONGLONG LLCMask; // 0x08E70
    ULONGLONG GroupModuleProcessorSet; // 0x08E78
    struct _KTHREAD *SmtIsolationThread; // 0x08E80
    ULONGLONG PrcbPad97[2]; // 0x08E88
    struct _PROCESSOR_PROFILE_CONTROL_AREA *ProcessorProfileControlArea; // 0x08E98
    PVOID ProfileEventIndexAddress; // 0x08EA0
    PVOID *DpcWatchdogProfile; // 0x08EA8
    PVOID *DpcWatchdogProfileCurrentEmptyCapture; // 0x08EB0
    PVOID SchedulerAssist; // 0x08EB8
    UCHAR SynchCounters[184]; // 0x08EC0 _SYNCH_COUNTERS
    ULONGLONG PrcbPad94; // 0x08F78
    UCHAR FsCounters[16]; // 0x08F80 _FILESYSTEM_DISK_COUNTERS
    UCHAR VendorString[13]; // 0x08F90
    UCHAR PrcbPad100[3]; // 0x08F9D
    ULONG64 FeatureBits; // 0x08FA0
    LARGE_INTEGER UpdateSignature; // 0x08FA8
    ULONGLONG PteBitCache; // 0x08FB0
    ULONG PteBitOffset; // 0x08FB8
    ULONG PrcbPad105; // 0x08FBC
    PCONTEXT Context; // 0x08FC0
    ULONG ContextFlagsInit; // 0x08FC8
    ULONG PrcbPad115; // 0x08FCC
    struct _XSAVE_AREA *ExtendedState; // 0x08FD0
    PVOID IsrStack; // 0x08FD8
    UCHAR EntropyTimingState[344]; // 0x08FE0 _KENTROPY_TIMING_STATE
    UCHAR StibpPairingTrace[56]; // 0x09138 <unnamed-tag>
    UCHAR AbSelfIoBoostsList[8]; // 0x09170 _SINGLE_LIST_ENTRY
    UCHAR AbPropagateBoostsList[8]; // 0x09178 _SINGLE_LIST_ENTRY
    UCHAR AbDpc[64]; // 0x09180 _KDPC
    UCHAR SymCryptEntropyAccumulatorState[384]; // 0x091C0 _SYMCRYPT_ENTROPY_ACCUMULATOR_STATE
    UCHAR IoIrpStackProfilerCurrent[84]; // 0x09340 _IOP_IRP_STACK_PROFILER
    UCHAR IoIrpStackProfilerPrevious[84]; // 0x09394 _IOP_IRP_STACK_PROFILER
    UCHAR SecureFault[24]; // 0x093E8 _KSECURE_FAULT_INFORMATION
    struct _KSHARED_READY_QUEUE *LocalSharedReadyQueue; // 0x09400
    struct _KI_COOPERATIVE_IDLE_SEARCH_CONTEXT *LocalSearchContexts[2]; // 0x09408
    struct _KI_COOPERATIVE_IDLE_SEARCH_CONTEXT *SearchContexts[2]; // 0x09418
    struct _KI_COOPERATIVE_IDLE_SEARCH_GENERATION *SearchGenerations[2]; // 0x09428
    ULONGLONG PrcbPad125[1]; // 0x09438
    ULONG TimerExpirationTraceCount; // 0x09440
    ULONG PrcbPad127; // 0x09444
    UCHAR TimerExpirationTrace[256]; // 0x09448 _KTIMER_EXPIRATION_TRACE[16]
    ULONGLONG PrcbPad128[7]; // 0x09548
    UCHAR ClockTimerState[1304]; // 0x09580 _KCLOCK_TIMER_STATE
    struct _KTRAP_FRAME *IpiFrame; // 0x09A98
    UCHAR PrcbPad129[32]; // 0x09AA0
    struct _REQUEST_MAILBOX *Mailbox; // 0x09AC0
    ULONGLONG PrcbPad130[7]; // 0x09AC8
    UCHAR McheckContext[160]; // 0x09B00 _MACHINE_CHECK_CONTEXT[2]
    ULONGLONG TransitionShadowStack; // 0x09BA0
    ULONGLONG KernelShadowStackInitial; // 0x09BA8
    PULONG64 IstShadowStacksTable; // 0x09BB0
    PVOID HiberSwapShadowStacks[5]; // 0x09BB8
    PVOID HiberSwapShadowStackBases[5]; // 0x09BE0
    PVOID MmFaultCompletionInfo; // 0x09C08
    ULONGLONG PrcbPad132[6]; // 0x09C10
    UCHAR SelfmapLockHandle[96]; // 0x09C40 _KLOCK_QUEUE_HANDLE[4]
    ULONGLONG PrcbPad134b[4]; // 0x09CA0
    UCHAR DieProcessorSet[264]; // 0x09CC0 _KAFFINITY_EX
    ULONGLONG PrcbPad135[32]; // 0x09DC8
    ULONG CoresPerPhysicalDie; // 0x09EC8
    ULONG LogicalProcessorsPerModule; // 0x09ECC
    UCHAR FreezeForRecoveryDpc[64]; // 0x09ED0 _KDPC
    UCHAR ModuleProcessorSet[264]; // 0x09F10 _KAFFINITY_EX
    ULONGLONG PrcbPad136[32]; // 0x0A018
    UCHAR LocalCoreControlBlock[48]; // 0x0A118 _KCORE_CONTROL_BLOCK
    ULONGLONG PrcbPad137a; // 0x0A148
    UCHAR KstackFreeDpc[64]; // 0x0A150 _KDPC
    UCHAR SlistRollbackDpc[64]; // 0x0A190 _KDPC
    UCHAR KstackFreeList[16]; // 0x0A1D0 _SLIST_HEADER
    ULONG IsrStackNesting; // 0x0A1E0
    UCHAR PrcbPad138[3228]; // 0x0A1E4
    ULONGLONG KernelDirectoryTableBase; // 0x0AE80
    ULONGLONG RspBaseShadow; // 0x0AE88
    ULONGLONG UserRspShadow; // 0x0AE90
    ULONG ShadowFlags; // 0x0AE98
    ULONG PrcbPad138b; // 0x0AE9C
    ULONGLONG PrcbPad138c; // 0x0AEA0
    USHORT PrcbPad138d; // 0x0AEA8
    USHORT VerwSelector; // 0x0AEAA
    ULONG DbgMceNestingLevel; // 0x0AEAC
    ULONG DbgMceFlags; // 0x0AEB0
    ULONG CoreControlBlockIndex; // 0x0AEB4
    struct _KCORE_CONTROL_BLOCK_SHADOW *CoreControlBlockShadow; // 0x0AEB8
    UCHAR LocalCoreControlBlockShadow[64]; // 0x0AEC0 _KCORE_CONTROL_BLOCK_SHADOW
    UCHAR CacheProcessorSet[1320]; // 0x0AF00 _KAFFINITY_EX[5]
    ULONGLONG PrcbPad139[160]; // 0x0B428
    ULONG NodeRelativeTopologyIndex[6]; // 0x0B928
    UCHAR ComplexProcessorSet[264]; // 0x0B940 _KAFFINITY_EX
    ULONGLONG PrcbPad139b[32]; // 0x0BA48
    ULONG LogicalProcessorsPerComplex; // 0x0BB48
    ULONG PrcbPad139c; // 0x0BB4C
    ULONGLONG PrcbPad140[102]; // 0x0BB50
    ULONGLONG PrcbPad140a[8]; // 0x0BE80
    union // 0x0BEC0
    {
        ULONGLONG PrcbPad141[512];
        struct // [LiberNT] state Windows does not keep in the KPRCB
        {
            KEVENT DpcEvent;
            LONG DpcSetEventRequest;
            ULONG DpcTimeLimit;
            LONG DpcWatchdogPeriod;
            ULONG FeatureBitsHigh;
            LONG MasterOffset;
            UINT64 MultiThreadProcessorSet;
            struct _KPRCB * MultiThreadSetMaster;
            struct _KNODE * ParentNode;
            UCHAR PollSlot;
            ULONG SecondaryColorMask;
            UINT64 SenderSummary;
            UCHAR SkipTick;
            LONG Sleeping;
            ULONG Stride;
            volatile UINT64 TargetSet;
            LONG TickOffset;
            UINT64 TimerHand;
            UINT64 TimerRequest;
        };
    };
    REQUEST_MAILBOX RequestMailbox[1]; // 0x0CEC0
} KPRCB, *PKPRCB;
C_ASSERT(FIELD_OFFSET(KPRCB, MxCsr) == 0x0);
C_ASSERT(FIELD_OFFSET(KPRCB, LegacyNumber) == 0x4);
C_ASSERT(FIELD_OFFSET(KPRCB, ReservedMustBeZero) == 0x5);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptRequest) == 0x6);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleHalt) == 0x7);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentThread) == 0x8);
C_ASSERT(FIELD_OFFSET(KPRCB, NextThread) == 0x10);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleThread) == 0x18);
C_ASSERT(FIELD_OFFSET(KPRCB, NestingLevel) == 0x20);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockOwner) == 0x21);
C_ASSERT(FIELD_OFFSET(KPRCB, PendingTickFlags) == 0x22);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleState) == 0x23);
C_ASSERT(FIELD_OFFSET(KPRCB, Number) == 0x24);
C_ASSERT(FIELD_OFFSET(KPRCB, RspBase) == 0x28);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbLock) == 0x30);
C_ASSERT(FIELD_OFFSET(KPRCB, PriorityState) == 0x38);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuType) == 0x40);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuID) == 0x41);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuStep) == 0x42);
C_ASSERT(FIELD_OFFSET(KPRCB, MHz) == 0x44);
C_ASSERT(FIELD_OFFSET(KPRCB, HalReserved) == 0x48);
C_ASSERT(FIELD_OFFSET(KPRCB, MinorVersion) == 0x88);
C_ASSERT(FIELD_OFFSET(KPRCB, MajorVersion) == 0x8A);
C_ASSERT(FIELD_OFFSET(KPRCB, BuildType) == 0x8C);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuVendor) == 0x8D);
C_ASSERT(FIELD_OFFSET(KPRCB, LegacyCoresPerPhysicalProcessor) == 0x8E);
C_ASSERT(FIELD_OFFSET(KPRCB, LegacyLogicalProcessorsPerCore) == 0x8F);
C_ASSERT(FIELD_OFFSET(KPRCB, TscFrequency) == 0x90);
C_ASSERT(FIELD_OFFSET(KPRCB, TracepointLog) == 0x98);
C_ASSERT(FIELD_OFFSET(KPRCB, CoresPerPhysicalProcessor) == 0xA0);
C_ASSERT(FIELD_OFFSET(KPRCB, LogicalProcessorsPerCore) == 0xA4);
C_ASSERT(FIELD_OFFSET(KPRCB, SelfIpiRequestSummary) == 0xA8);
C_ASSERT(FIELD_OFFSET(KPRCB, QpcToTscIncrementShift) == 0xAC);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad03) == 0xAD);
C_ASSERT(FIELD_OFFSET(KPRCB, QpcToTscIncrement) == 0xB0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad04) == 0xB8);
C_ASSERT(FIELD_OFFSET(KPRCB, SchedulerSubNode) == 0xC0);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupSetMember) == 0xC8);
C_ASSERT(FIELD_OFFSET(KPRCB, Group) == 0xD0);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupIndex) == 0xD1);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad05) == 0xD2);
C_ASSERT(FIELD_OFFSET(KPRCB, InitialApicId) == 0xD4);
C_ASSERT(FIELD_OFFSET(KPRCB, ScbOffset) == 0xD8);
C_ASSERT(FIELD_OFFSET(KPRCB, ApicMask) == 0xDC);
C_ASSERT(FIELD_OFFSET(KPRCB, AcpiReserved) == 0xE0);
C_ASSERT(FIELD_OFFSET(KPRCB, CFlushSize) == 0xE8);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbFlags) == 0xEC);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad11) == 0xF0);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorState) == 0x100);
C_ASSERT(FIELD_OFFSET(KPRCB, ExtendedSupervisorState) == 0x6C0);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorSignature) == 0x6C8);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorFlags) == 0x6CC);
C_ASSERT(FIELD_OFFSET(KPRCB, TrappedSecurityDomain) == 0x6D0);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbState) == 0x6D8);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbTrappedBpbState) == 0x6DA);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbFeatures) == 0x6DC);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad12b) == 0x6DD);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbRetpolineState) == 0x6DE);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbRetpolineStateOverflow) == 0x6DF);
C_ASSERT(FIELD_OFFSET(KPRCB, PairRegister) == 0x6E0);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbRetpolineExitSpecCtrl) == 0x6E2);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbTrappedRetpolineExitSpecCtrl) == 0x6E4);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbCurrentSpecCtrl) == 0x6E6);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbKernelSpecCtrl) == 0x6E8);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbNmiSpecCtrl) == 0x6EA);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbUserSpecCtrl) == 0x6EC);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad12c) == 0x6EE);
C_ASSERT(FIELD_OFFSET(KPRCB, LockQueue) == 0x6F0);
C_ASSERT(FIELD_OFFSET(KPRCB, PPLookasideList) == 0x800);
C_ASSERT(FIELD_OFFSET(KPRCB, PPNxPagedLookasideList) == 0x900);
C_ASSERT(FIELD_OFFSET(KPRCB, PPNPagedLookasideList) == 0x1500);
C_ASSERT(FIELD_OFFSET(KPRCB, PPPagedLookasideList) == 0x2100);
C_ASSERT(FIELD_OFFSET(KPRCB, MsrIa32TsxCtrl) == 0x2D00);
C_ASSERT(FIELD_OFFSET(KPRCB, DeferredReadyListHead) == 0x2D08);
C_ASSERT(FIELD_OFFSET(KPRCB, MmPageFaultCount) == 0x2D10);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCopyOnWriteCount) == 0x2D14);
C_ASSERT(FIELD_OFFSET(KPRCB, MmTransitionCount) == 0x2D18);
C_ASSERT(FIELD_OFFSET(KPRCB, MmDemandZeroCount) == 0x2D1C);
C_ASSERT(FIELD_OFFSET(KPRCB, MmPageReadCount) == 0x2D20);
C_ASSERT(FIELD_OFFSET(KPRCB, MmPageReadIoCount) == 0x2D24);
C_ASSERT(FIELD_OFFSET(KPRCB, MmDirtyPagesWriteCount) == 0x2D28);
C_ASSERT(FIELD_OFFSET(KPRCB, MmDirtyWriteIoCount) == 0x2D2C);
C_ASSERT(FIELD_OFFSET(KPRCB, MmMappedPagesWriteCount) == 0x2D30);
C_ASSERT(FIELD_OFFSET(KPRCB, MmMappedWriteIoCount) == 0x2D34);
C_ASSERT(FIELD_OFFSET(KPRCB, KeSystemCalls) == 0x2D38);
C_ASSERT(FIELD_OFFSET(KPRCB, KeContextSwitches) == 0x2D3C);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad40) == 0x2D40);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadNoWait) == 0x2D44);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadWait) == 0x2D48);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadNotPossible) == 0x2D4C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadNoWait) == 0x2D50);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadWait) == 0x2D54);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadNoWaitMiss) == 0x2D58);
C_ASSERT(FIELD_OFFSET(KPRCB, IoReadOperationCount) == 0x2D5C);
C_ASSERT(FIELD_OFFSET(KPRCB, IoWriteOperationCount) == 0x2D60);
C_ASSERT(FIELD_OFFSET(KPRCB, IoOtherOperationCount) == 0x2D64);
C_ASSERT(FIELD_OFFSET(KPRCB, IoReadTransferCount) == 0x2D68);
C_ASSERT(FIELD_OFFSET(KPRCB, IoWriteTransferCount) == 0x2D70);
C_ASSERT(FIELD_OFFSET(KPRCB, IoOtherTransferCount) == 0x2D78);
C_ASSERT(FIELD_OFFSET(KPRCB, PacketBarrier) == 0x2D80);
C_ASSERT(FIELD_OFFSET(KPRCB, TargetCount) == 0x2D84);
C_ASSERT(FIELD_OFFSET(KPRCB, IpiFrozen) == 0x2D88);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad30) == 0x2D8C);
C_ASSERT(FIELD_OFFSET(KPRCB, IsrDpcStats) == 0x2D90);
C_ASSERT(FIELD_OFFSET(KPRCB, DeviceInterrupts) == 0x2D98);
C_ASSERT(FIELD_OFFSET(KPRCB, LookasideIrpFloat) == 0x2D9C);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptLastCount) == 0x2DA0);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptRate) == 0x2DA4);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad31) == 0x2DA8);
C_ASSERT(FIELD_OFFSET(KPRCB, PairPrcb) == 0x2DB0);
C_ASSERT(FIELD_OFFSET(KPRCB, StaticAffinity) == 0x2DB8);
C_ASSERT(FIELD_OFFSET(KPRCB, DeferredDispatchInterrupts) == 0x35D8);
C_ASSERT(FIELD_OFFSET(KPRCB, StaticRescheduleContext) == 0x37E8);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuPartition) == 0x37F0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad35) == 0x37F8);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptObjectPool) == 0x3800);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRuntimeHistoryHashTable) == 0x3810);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRuntimeHistoryHashTableCleanupDpc) == 0x3818);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentDpcRoutine) == 0x3820);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentDpcRuntimeHistoryCached) == 0x3828);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentDpcStartTime) == 0x3830);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcDelegateThread) == 0x3838);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcData) == 0x3840);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcStack) == 0x38A0);
C_ASSERT(FIELD_OFFSET(KPRCB, MaximumDpcQueueDepth) == 0x38A8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRequestRate) == 0x38AC);
C_ASSERT(FIELD_OFFSET(KPRCB, MinimumDpcRate) == 0x38B0);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcLastCount) == 0x38B4);
C_ASSERT(FIELD_OFFSET(KPRCB, ThreadDpcEnable) == 0x38B8);
C_ASSERT(FIELD_OFFSET(KPRCB, QuantumEnd) == 0x38B9);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRoutineActive) == 0x38BA);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleSchedule) == 0x38BB);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRequestSummary) == 0x38BC);
C_ASSERT(FIELD_OFFSET(KPRCB, LastTick) == 0x38C0);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockInterrupts) == 0x38C4);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadyScanTick) == 0x38C8);
C_ASSERT(FIELD_OFFSET(KPRCB, SingleDpcSoftTimeLimitTicks) == 0x38CC);
C_ASSERT(FIELD_OFFSET(KPRCB, SingleDpcSoftTimeoutEventInfo) == 0x38D0);
C_ASSERT(FIELD_OFFSET(KPRCB, CumulativeDpcSoftTimeLimitTicks) == 0x38D8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfileBufferSize) == 0x38DC);
C_ASSERT(FIELD_OFFSET(KPRCB, RcuData) == 0x38E0);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptObject) == 0x3900);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerTable) == 0x4100);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcLog) == 0x8318);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcLogIndex) == 0x8320);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcLogBufferSize) == 0x8324);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad92) == 0x8328);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcGate) == 0x8340);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad52) == 0x8358);
C_ASSERT(FIELD_OFFSET(KPRCB, CallDpc) == 0x8360);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockKeepAlive) == 0x83A0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad60) == 0x83A4);
C_ASSERT(FIELD_OFFSET(KPRCB, NmiActive) == 0x83A6);
C_ASSERT(FIELD_OFFSET(KPRCB, MceActive) == 0x83A7);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogPeriodTicks) == 0x83A8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogCount) == 0x83AC);
C_ASSERT(FIELD_OFFSET(KPRCB, KeSpinLockOrdering) == 0x83B0);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfileCumulativeDpcThresholdTicks) == 0x83B4);
C_ASSERT(FIELD_OFFSET(KPRCB, CachedPtes) == 0x83B8);
C_ASSERT(FIELD_OFFSET(KPRCB, WaitListHead) == 0x83C0);
C_ASSERT(FIELD_OFFSET(KPRCB, WaitLock) == 0x83D0);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadySummary) == 0x83D8);
C_ASSERT(FIELD_OFFSET(KPRCB, AffinitizedSelectionMask) == 0x83DC);
C_ASSERT(FIELD_OFFSET(KPRCB, QueueIndex) == 0x83E0);
C_ASSERT(FIELD_OFFSET(KPRCB, NormalPriorityQueueIndex) == 0x83E4);
C_ASSERT(FIELD_OFFSET(KPRCB, NormalPriorityReadyScanTick) == 0x83E8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogSequenceNumber) == 0x83EC);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerExpirationDpc) == 0x83F0);
C_ASSERT(FIELD_OFFSET(KPRCB, ScbQueue) == 0x8430);
C_ASSERT(FIELD_OFFSET(KPRCB, DispatcherReadyListHead) == 0x8440);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptCount) == 0x8640);
C_ASSERT(FIELD_OFFSET(KPRCB, KernelTime) == 0x8644);
C_ASSERT(FIELD_OFFSET(KPRCB, UserTime) == 0x8648);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcTime) == 0x864C);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptTime) == 0x8650);
C_ASSERT(FIELD_OFFSET(KPRCB, AdjustDpcThreshold) == 0x8654);
C_ASSERT(FIELD_OFFSET(KPRCB, DebuggerSavedIRQL) == 0x8658);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupSchedulingOverQuota) == 0x8659);
C_ASSERT(FIELD_OFFSET(KPRCB, DeepSleep) == 0x865A);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad80) == 0x865B);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcTimeCount) == 0x865C);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcTimeLimitTicks) == 0x8660);
C_ASSERT(FIELD_OFFSET(KPRCB, PeriodicCount) == 0x8664);
C_ASSERT(FIELD_OFFSET(KPRCB, PeriodicBias) == 0x8668);
C_ASSERT(FIELD_OFFSET(KPRCB, AvailableTime) == 0x866C);
C_ASSERT(FIELD_OFFSET(KPRCB, KeExceptionDispatchCount) == 0x8670);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadyThreadCount) == 0x8674);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadyQueueExpectedRunTime) == 0x8678);
C_ASSERT(FIELD_OFFSET(KPRCB, StartCycles) == 0x8680);
C_ASSERT(FIELD_OFFSET(KPRCB, TaggedCycles) == 0x8688);
C_ASSERT(FIELD_OFFSET(KPRCB, AffinitizedCycles) == 0x86A8);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuCycleScalingFactor) == 0x86B8);
C_ASSERT(FIELD_OFFSET(KPRCB, PerformanceScoreByClass) == 0x86BC);
C_ASSERT(FIELD_OFFSET(KPRCB, EfficiencyScoreByClass) == 0x86CC);
C_ASSERT(FIELD_OFFSET(KPRCB, CycleAccumulationInitialized) == 0x86DC);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad83) == 0x86DD);
C_ASSERT(FIELD_OFFSET(KPRCB, MceRecoveryContext) == 0x86E0);
C_ASSERT(FIELD_OFFSET(KPRCB, ForceParkDutyCycleData) == 0x86F0);
C_ASSERT(FIELD_OFFSET(KPRCB, StartCyclesQpc) == 0x86F8);
C_ASSERT(FIELD_OFFSET(KPRCB, CycleTimeQpc) == 0x8700);
C_ASSERT(FIELD_OFFSET(KPRCB, NumberOfSecureFaults) == 0x8708);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad84) == 0x8710);
C_ASSERT(FIELD_OFFSET(KPRCB, CachedStacks) == 0x8730);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfileSingleDpcThresholdTicks) == 0x8740);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad82) == 0x8744);
C_ASSERT(FIELD_OFFSET(KPRCB, PageColor) == 0x8750);
C_ASSERT(FIELD_OFFSET(KPRCB, NodeColor) == 0x8754);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad87) == 0x8758);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad81) == 0x8760);
C_ASSERT(FIELD_OFFSET(KPRCB, SystemWorkKickInProgress) == 0x8765);
C_ASSERT(FIELD_OFFSET(KPRCB, ExceptionStackActive) == 0x8766);
C_ASSERT(FIELD_OFFSET(KPRCB, TbFlushListActive) == 0x8767);
C_ASSERT(FIELD_OFFSET(KPRCB, ExceptionStack) == 0x8768);
C_ASSERT(FIELD_OFFSET(KPRCB, MmSpinLockOrdering) == 0x8770);
C_ASSERT(FIELD_OFFSET(KPRCB, CycleTime) == 0x8778);
C_ASSERT(FIELD_OFFSET(KPRCB, Cycles) == 0x8780);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadNoWait) == 0x87C0);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadWait) == 0x87C4);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadNotPossible) == 0x87C8);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataNoWait) == 0x87CC);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataWait) == 0x87D0);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinMappedDataCount) == 0x87D4);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadNoWait) == 0x87D8);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadWait) == 0x87DC);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadNoWait) == 0x87E0);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadWait) == 0x87E4);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLazyWriteHotSpots) == 0x87E8);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLazyWriteIos) == 0x87EC);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLazyWritePages) == 0x87F0);
C_ASSERT(FIELD_OFFSET(KPRCB, CcDataFlushes) == 0x87F4);
C_ASSERT(FIELD_OFFSET(KPRCB, CcDataPages) == 0x87F8);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLostDelayedWrites) == 0x87FC);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadResourceMiss) == 0x8800);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadWaitMiss) == 0x8804);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadResourceMiss) == 0x8808);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataNoWaitMiss) == 0x880C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataWaitMiss) == 0x8810);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadNoWaitMiss) == 0x8814);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadWaitMiss) == 0x8818);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadNoWaitMiss) == 0x881C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadWaitMiss) == 0x8820);
C_ASSERT(FIELD_OFFSET(KPRCB, CcReadAheadIos) == 0x8824);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCacheTransitionCount) == 0x8828);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCacheReadCount) == 0x882C);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCacheIoCount) == 0x8830);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad91) == 0x8834);
C_ASSERT(FIELD_OFFSET(KPRCB, MmInternal) == 0x8838);
C_ASSERT(FIELD_OFFSET(KPRCB, PowerState) == 0x8840);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad96) == 0x8A78);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad90) == 0x8A80);
C_ASSERT(FIELD_OFFSET(KPRCB, ScbList) == 0x8A88);
C_ASSERT(FIELD_OFFSET(KPRCB, ForceIdleDpc) == 0x8A98);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogDpc) == 0x8AD8);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad98) == 0x8B18);
C_ASSERT(FIELD_OFFSET(KPRCB, VmInternal) == 0x8B50);
C_ASSERT(FIELD_OFFSET(KPRCB, Cache) == 0x8B58);
C_ASSERT(FIELD_OFFSET(KPRCB, CacheCount) == 0x8B94);
C_ASSERT(FIELD_OFFSET(KPRCB, CachedCommit) == 0x8B98);
C_ASSERT(FIELD_OFFSET(KPRCB, CachedResidentAvailable) == 0x8B9C);
C_ASSERT(FIELD_OFFSET(KPRCB, WheaInfo) == 0x8BA0);
C_ASSERT(FIELD_OFFSET(KPRCB, EtwSupport) == 0x8BA8);
C_ASSERT(FIELD_OFFSET(KPRCB, ExSaPageArray) == 0x8BB0);
C_ASSERT(FIELD_OFFSET(KPRCB, KeAlignmentFixupCount) == 0x8BB8);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad95) == 0x8BBC);
C_ASSERT(FIELD_OFFSET(KPRCB, HypercallPageList) == 0x8BC0);
C_ASSERT(FIELD_OFFSET(KPRCB, GenerationTarget) == 0x8BD8);
C_ASSERT(FIELD_OFFSET(KPRCB, VirtualApicAssistPage) == 0x8BE0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad85) == 0x8BE8);
C_ASSERT(FIELD_OFFSET(KPRCB, HypercallCachedPages) == 0x8C00);
C_ASSERT(FIELD_OFFSET(KPRCB, VirtualApicAssist) == 0x8C08);
C_ASSERT(FIELD_OFFSET(KPRCB, PackageProcessorSet) == 0x8C10);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad86) == 0x8D18);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorId) == 0x8E18);
C_ASSERT(FIELD_OFFSET(KPRCB, CoreId) == 0x8E1C);
C_ASSERT(FIELD_OFFSET(KPRCB, ModuleId) == 0x8E20);
C_ASSERT(FIELD_OFFSET(KPRCB, DieId) == 0x8E24);
C_ASSERT(FIELD_OFFSET(KPRCB, PackageId) == 0x8E28);
C_ASSERT(FIELD_OFFSET(KPRCB, ComplexId) == 0x8E2C);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad101) == 0x8E30);
C_ASSERT(FIELD_OFFSET(KPRCB, SharedReadyQueueMask) == 0x8E40);
C_ASSERT(FIELD_OFFSET(KPRCB, SharedReadyQueue) == 0x8E48);
C_ASSERT(FIELD_OFFSET(KPRCB, SharedQueueScanOwner) == 0x8E50);
C_ASSERT(FIELD_OFFSET(KPRCB, ScanSiblingIndex) == 0x8E54);
C_ASSERT(FIELD_OFFSET(KPRCB, CoreControlBlock) == 0x8E58);
C_ASSERT(FIELD_OFFSET(KPRCB, CoreProcessorSet) == 0x8E60);
C_ASSERT(FIELD_OFFSET(KPRCB, ScanSiblingMask) == 0x8E68);
C_ASSERT(FIELD_OFFSET(KPRCB, LLCMask) == 0x8E70);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupModuleProcessorSet) == 0x8E78);
C_ASSERT(FIELD_OFFSET(KPRCB, SmtIsolationThread) == 0x8E80);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad97) == 0x8E88);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorProfileControlArea) == 0x8E98);
C_ASSERT(FIELD_OFFSET(KPRCB, ProfileEventIndexAddress) == 0x8EA0);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfile) == 0x8EA8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfileCurrentEmptyCapture) == 0x8EB0);
C_ASSERT(FIELD_OFFSET(KPRCB, SchedulerAssist) == 0x8EB8);
C_ASSERT(FIELD_OFFSET(KPRCB, SynchCounters) == 0x8EC0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad94) == 0x8F78);
C_ASSERT(FIELD_OFFSET(KPRCB, FsCounters) == 0x8F80);
C_ASSERT(FIELD_OFFSET(KPRCB, VendorString) == 0x8F90);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad100) == 0x8F9D);
C_ASSERT(FIELD_OFFSET(KPRCB, FeatureBits) == 0x8FA0);
C_ASSERT(FIELD_OFFSET(KPRCB, UpdateSignature) == 0x8FA8);
C_ASSERT(FIELD_OFFSET(KPRCB, PteBitCache) == 0x8FB0);
C_ASSERT(FIELD_OFFSET(KPRCB, PteBitOffset) == 0x8FB8);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad105) == 0x8FBC);
C_ASSERT(FIELD_OFFSET(KPRCB, Context) == 0x8FC0);
C_ASSERT(FIELD_OFFSET(KPRCB, ContextFlagsInit) == 0x8FC8);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad115) == 0x8FCC);
C_ASSERT(FIELD_OFFSET(KPRCB, ExtendedState) == 0x8FD0);
C_ASSERT(FIELD_OFFSET(KPRCB, IsrStack) == 0x8FD8);
C_ASSERT(FIELD_OFFSET(KPRCB, EntropyTimingState) == 0x8FE0);
C_ASSERT(FIELD_OFFSET(KPRCB, StibpPairingTrace) == 0x9138);
C_ASSERT(FIELD_OFFSET(KPRCB, AbSelfIoBoostsList) == 0x9170);
C_ASSERT(FIELD_OFFSET(KPRCB, AbPropagateBoostsList) == 0x9178);
C_ASSERT(FIELD_OFFSET(KPRCB, AbDpc) == 0x9180);
C_ASSERT(FIELD_OFFSET(KPRCB, SymCryptEntropyAccumulatorState) == 0x91C0);
C_ASSERT(FIELD_OFFSET(KPRCB, IoIrpStackProfilerCurrent) == 0x9340);
C_ASSERT(FIELD_OFFSET(KPRCB, IoIrpStackProfilerPrevious) == 0x9394);
C_ASSERT(FIELD_OFFSET(KPRCB, SecureFault) == 0x93E8);
C_ASSERT(FIELD_OFFSET(KPRCB, LocalSharedReadyQueue) == 0x9400);
C_ASSERT(FIELD_OFFSET(KPRCB, LocalSearchContexts) == 0x9408);
C_ASSERT(FIELD_OFFSET(KPRCB, SearchContexts) == 0x9418);
C_ASSERT(FIELD_OFFSET(KPRCB, SearchGenerations) == 0x9428);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad125) == 0x9438);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerExpirationTraceCount) == 0x9440);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad127) == 0x9444);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerExpirationTrace) == 0x9448);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad128) == 0x9548);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockTimerState) == 0x9580);
C_ASSERT(FIELD_OFFSET(KPRCB, IpiFrame) == 0x9A98);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad129) == 0x9AA0);
C_ASSERT(FIELD_OFFSET(KPRCB, Mailbox) == 0x9AC0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad130) == 0x9AC8);
C_ASSERT(FIELD_OFFSET(KPRCB, McheckContext) == 0x9B00);
C_ASSERT(FIELD_OFFSET(KPRCB, TransitionShadowStack) == 0x9BA0);
C_ASSERT(FIELD_OFFSET(KPRCB, KernelShadowStackInitial) == 0x9BA8);
C_ASSERT(FIELD_OFFSET(KPRCB, HiberSwapShadowStacks) == 0x9BB8);
C_ASSERT(FIELD_OFFSET(KPRCB, HiberSwapShadowStackBases) == 0x9BE0);
C_ASSERT(FIELD_OFFSET(KPRCB, MmFaultCompletionInfo) == 0x9C08);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad132) == 0x9C10);
C_ASSERT(FIELD_OFFSET(KPRCB, SelfmapLockHandle) == 0x9C40);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad134b) == 0x9CA0);
C_ASSERT(FIELD_OFFSET(KPRCB, DieProcessorSet) == 0x9CC0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad135) == 0x9DC8);
C_ASSERT(FIELD_OFFSET(KPRCB, CoresPerPhysicalDie) == 0x9EC8);
C_ASSERT(FIELD_OFFSET(KPRCB, LogicalProcessorsPerModule) == 0x9ECC);
C_ASSERT(FIELD_OFFSET(KPRCB, FreezeForRecoveryDpc) == 0x9ED0);
C_ASSERT(FIELD_OFFSET(KPRCB, ModuleProcessorSet) == 0x9F10);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad136) == 0xA018);
C_ASSERT(FIELD_OFFSET(KPRCB, LocalCoreControlBlock) == 0xA118);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad137a) == 0xA148);
C_ASSERT(FIELD_OFFSET(KPRCB, KstackFreeDpc) == 0xA150);
C_ASSERT(FIELD_OFFSET(KPRCB, SlistRollbackDpc) == 0xA190);
C_ASSERT(FIELD_OFFSET(KPRCB, KstackFreeList) == 0xA1D0);
C_ASSERT(FIELD_OFFSET(KPRCB, IsrStackNesting) == 0xA1E0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad138) == 0xA1E4);
C_ASSERT(FIELD_OFFSET(KPRCB, KernelDirectoryTableBase) == 0xAE80);
C_ASSERT(FIELD_OFFSET(KPRCB, RspBaseShadow) == 0xAE88);
C_ASSERT(FIELD_OFFSET(KPRCB, UserRspShadow) == 0xAE90);
C_ASSERT(FIELD_OFFSET(KPRCB, ShadowFlags) == 0xAE98);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad138b) == 0xAE9C);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad138c) == 0xAEA0);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad138d) == 0xAEA8);
C_ASSERT(FIELD_OFFSET(KPRCB, VerwSelector) == 0xAEAA);
C_ASSERT(FIELD_OFFSET(KPRCB, DbgMceNestingLevel) == 0xAEAC);
C_ASSERT(FIELD_OFFSET(KPRCB, DbgMceFlags) == 0xAEB0);
C_ASSERT(FIELD_OFFSET(KPRCB, CoreControlBlockIndex) == 0xAEB4);
C_ASSERT(FIELD_OFFSET(KPRCB, CoreControlBlockShadow) == 0xAEB8);
C_ASSERT(FIELD_OFFSET(KPRCB, LocalCoreControlBlockShadow) == 0xAEC0);
C_ASSERT(FIELD_OFFSET(KPRCB, CacheProcessorSet) == 0xAF00);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad139) == 0xB428);
C_ASSERT(FIELD_OFFSET(KPRCB, NodeRelativeTopologyIndex) == 0xB928);
C_ASSERT(FIELD_OFFSET(KPRCB, ComplexProcessorSet) == 0xB940);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad139b) == 0xBA48);
C_ASSERT(FIELD_OFFSET(KPRCB, LogicalProcessorsPerComplex) == 0xBB48);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad139c) == 0xBB4C);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad140) == 0xBB50);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad140a) == 0xBE80);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad141) == 0xBEC0);
C_ASSERT(FIELD_OFFSET(KPRCB, RequestMailbox) == 0xCEC0);
C_ASSERT(sizeof(KPRCB) == 0xCF00);

/* TODO: GroupSetMember (0xC8), Group (0xD0) and GroupIndex (0xD1) are missing: this layout departs from Windows 11 at 0x38 */
C_ASSERT(FIELD_OFFSET(KPRCB, LegacyNumber) == 0x004);
C_ASSERT(FIELD_OFFSET(KPRCB, NestingLevel) == 0x020);
C_ASSERT(FIELD_OFFSET(KPRCB, Number) == 0x024);
C_ASSERT(FIELD_OFFSET(KPRCB, RspBase) == 0x028);

//
// Processor Control Region
//
typedef struct _KIPCR
{
    union
    {
        NT_TIB NtTib;
        struct
        {
            union _KGDTENTRY64 *GdtBase;
            struct _KTSS64 *TssBase;
            ULONG64 UserRsp;
            struct _KPCR *Self;
            struct _KPRCB *CurrentPrcb;
            PKSPIN_LOCK_QUEUE LockArray;
            PVOID Used_Self;
        };
    };
    union _KIDTENTRY64 *IdtBase;
    ULONG64 Unused[2];
    KIRQL Irql;
    UCHAR SecondLevelCacheAssociativity;
    UCHAR ObsoleteNumber;
    UCHAR Fill0;
    ULONG Unused0[3];
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG StallScaleFactor;
    PVOID Unused1[3];
    ULONG KernelReserved[15];
    ULONG SecondLevelCacheSize;
    ULONG HalReserved[16];
    ULONG Unused2;
    ULONG Fill1;
    PVOID KdVersionBlock; // 0x108
    PVOID Unused3;
    ULONG PcrAlign1[24];
    ULONG Fill2[2]; // 0x178
    KPRCB Prcb; // 0x180
} KIPCR, *PKIPCR;

//
// TSS Definition
//
typedef struct _KiIoAccessMap
{
    UCHAR DirectionMap[32];
    UCHAR IoMap[8196];
} KIIO_ACCESS_MAP;


#pragma pack(push,4)
typedef struct _KTSS64
{
 /* 000 */  ULONG Reserved0;
 /* 004 */  UINT64 Rsp0;
 /* 00c */  UINT64 Rsp1;
 /* 014 */  UINT64 Rsp2;
 /* 01c */  UINT64 Ist[8];
 /* 05c */  UINT64 Reserved1;
 /* 064 */  USHORT Reserved2;
 /* 066 */  USHORT IoMapBase;
} KTSS64, *PKTSS64;
#pragma pack(pop)
#define KTSS KTSS64
#define PKTSS PKTSS64

//
// KEXCEPTION_FRAME
//
typedef struct _KEXCEPTION_FRAME
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    ULONG64 P5;
    ULONG64 InitialStack;                // Win8+ name: Spare1 [ReactOS reuses the freed slot]
    M128A Xmm6;
    M128A Xmm7;
    M128A Xmm8;
    M128A Xmm9;
    M128A Xmm10;
    M128A Xmm11;
    M128A Xmm12;
    M128A Xmm13;
    M128A Xmm14;
    M128A Xmm15;
    ULONG64 TrapFrame;
    ULONG64 OutputBuffer;
    ULONG64 OutputLength;
    ULONG64 CallbackStack;               // Win8+ name: Spare2 [ReactOS reuses the freed slot]
    ULONG64 MxCsr;
    ULONG64 Rbp;
    ULONG64 Rbx;
    ULONG64 Rdi;
    ULONG64 Rsi;
    ULONG64 R12;
    ULONG64 R13;
    ULONG64 R14;
    ULONG64 R15;
    ULONG64 Return;
} KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;

typedef struct _MACHINE_FRAME
{
    ULONG64 Rip;
    USHORT SegCs;
    USHORT Fill1[3];
    ULONG EFlags;
    ULONG Fill2;
    ULONG64 Rsp;
    USHORT SegSs;
    USHORT Fill3[3];
} MACHINE_FRAME, *PMACHINE_FRAME;

//
// Defines the Callback Stack Layout for User Mode Callbacks
//
typedef KEXCEPTION_FRAME KCALLOUT_FRAME, *PKCALLOUT_FRAME;

//
// User side callout frame
//
typedef struct _UCALLOUT_FRAME
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    PVOID Buffer;
    ULONG Length;
    ULONG ApiNumber;
    MACHINE_FRAME MachineFrame;
} UCALLOUT_FRAME, *PUCALLOUT_FRAME; // size = 0x0058

//
// User side APC dispatcher frame
//
typedef struct _UAPC_FRAME
{
    CONTEXT Context;
    MACHINE_FRAME MachineFrame;
} UAPC_FRAME, *PUAPC_FRAME;

//
// Stack frame layout for KiUserExceptionDispatcher
// The name is totally made up
//
typedef struct _KUSER_EXCEPTION_STACK
{
    CONTEXT Context;
    ULONG64 ContextEx[4];
    EXCEPTION_RECORD ExceptionRecord;
    ULONG64 Alignment;
    MACHINE_FRAME MachineFrame;
} KUSER_EXCEPTION_STACK, * PKUSER_EXCEPTION_STACK;

C_ASSERT(FIELD_OFFSET(KUSER_EXCEPTION_STACK, ExceptionRecord) == 0x4F0);
C_ASSERT(FIELD_OFFSET(KUSER_EXCEPTION_STACK, MachineFrame) == 0x590);

typedef struct _DISPATCHER_CONTEXT
{
    ULONG64 ControlPc;
    ULONG64 ImageBase;
    struct _RUNTIME_FUNCTION *FunctionEntry;
    ULONG64 EstablisherFrame;
    ULONG64 TargetIp;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    struct _UNWIND_HISTORY_TABLE *HistoryTable;
    ULONG ScopeIndex;
    ULONG Fill0;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

typedef struct _KSTART_FRAME
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    ULONG64 Reserved;
    ULONG64 Return;
} KSTART_FRAME, *PKSTART_FRAME;

typedef struct _KSWITCH_FRAME
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    ULONG64 P5Home;
    KIRQL ApcBypass;
    UCHAR Fill1[7];
    ULONG64 Rbp;
    ULONG64 Return;
} KSWITCH_FRAME, *PKSWITCH_FRAME;

#define PROCESSOR_START_FLAG_FORCE_ENABLE_NX 0x0001
typedef struct _KPROCESSOR_START_BLOCK
{
    ULONG CompletionFlag; // 0x0004
    ULONG Flags; // 0x0008
    ULONG Gdt32; // 0x000C
    ULONG Idt32; // 0x0012
    PVOID Gdt; // 0x0018
    // ???
    ULONG64 TiledMemoryMap; // 0x0058
    UCHAR PmTarget[6]; // 0x0060
    UCHAR LmIdentityTarget[6]; // 0x0066
    ULONG64 LmTarget; // 0x0070
    struct _KPROCESSOR_START_BLOCK *SelfMap; // 0x0078
    ULONG64 MsrPat; // 0x0080
    ULONG64 MsrEFER; // 0x0088
    KPROCESSOR_STATE ProcessorState; // 0x0090
} KPROCESSOR_START_BLOCK, *PKPROCESSOR_START_BLOCK; // size 00640

//
// Inline function to get current KPRCB
//
FORCEINLINE
struct _KPRCB *
KeGetCurrentPrcb(VOID)
{
    return (struct _KPRCB *)__readgsqword(FIELD_OFFSET(KIPCR, CurrentPrcb));
}

#endif
#endif
