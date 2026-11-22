/*++ NDK Version: 0098

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    ketypes.h (X86)

Abstract:

    i386 Type definitions for the Kernel services.

Author:

    Alex Ionescu (alexi@tinykrnl.org) - Updated - 27-Feb-2006

--*/

#ifndef _I386_KETYPES_H
#define _I386_KETYPES_H

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
            struct _PROC_IDLE_ACCOUNTING *IdleAccounting;           // 0x004
            ULONGLONG IdleTimeLast;                                 // 0x008
            ULONGLONG IdleTimeTotal;                                // 0x010
            volatile ULONGLONG IdleTimeEntry;                       // 0x018
            ULONGLONG IdleTimeExpiration;                           // 0x020
            UCHAR NonInterruptibleTransition;                       // 0x028
            UCHAR PepWokenTransition;                               // 0x029
            UCHAR HvTargetState;                                    // 0x02A
            UCHAR SoftParked;                                       // 0x02B
            ULONG TargetIdleState;                                  // 0x02C
            UCHAR IdlePolicy[6];                                    // 0x030 _PROC_IDLE_POLICY
            volatile ULONG Synchronization;                         // 0x038 _PPM_IDLE_SYNCHRONIZATION_STATE
            ULONGLONG PerfFeedback[17];                             // 0x040 _PROC_FEEDBACK
            ULONG Hypervisor;                                       // 0x0C8 _PROC_HYPERVISOR_STATE
            ULONG LastSysTime;                                      // 0x0CC
            ULONG WmiDispatchPtr;                                   // 0x0D0
            LONG WmiInterfaceEnabled;                               // 0x0D4
            ULONGLONG FFHThrottleStateInfo[4];                      // 0x0D8 _PPM_FFH_THROTTLE_STATE_INFO
            KDPC PerfActionDpc;                                     // 0x0F8
            volatile LONG PerfActionMask;                           // 0x118
            ULONGLONG HvIdleCheck[2];                               // 0x120 _PROC_IDLE_SNAP
            ULONGLONG CheckContext[5];                              // 0x130 _PROC_PERF_CHECK_CONTEXT
            struct _PPM_CONCURRENCY_ACCOUNTING *Concurrency;        // 0x158
            struct _PPM_CONCURRENCY_ACCOUNTING *ClassConcurrency;   // 0x15C
        };
        struct
        {
            PROCESSOR_POWER_STATE_LEGACY_MEMBERS                    // [LiberNT]
        };
    };
    UCHAR ArchitecturalEfficiencyClass;                             // 0x160
    UCHAR PerformanceSchedulingClass;                               // 0x161
    UCHAR EfficiencySchedulingClass;                                // 0x162
    UCHAR Unused;                                                   // 0x163
    UCHAR Parked;                                                   // 0x164
    UCHAR LongPriorQosPeriod;                                       // 0x165
    union
    {
        ULONGLONG SnapTimeLast;                                     // 0x168
        ULONGLONG EnergyConsumed;                                   // 0x168
    };
    ULONGLONG ActiveTime;                                           // 0x170
    ULONGLONG TotalTime;                                            // 0x178
    struct _POP_FX_DEVICE *FxDevice;                                // 0x180
    ULONGLONG LastQosTranstionTsc;                                  // 0x188
    ULONGLONG QosTransitionHysteresis;                              // 0x190
    ULONG RequestedQosClass;                                        // 0x198 _KHETERO_CPU_QOS
    ULONG ResolvedQosClass;                                         // 0x19C _KHETERO_CPU_QOS
    USHORT QosEquivalencyMask;                                      // 0x1A0
    USHORT HwFeedbackTableIndex;                                    // 0x1A2
    UCHAR HwFeedbackParkHint;                                       // 0x1A4
    UCHAR HwFeedbackPerformanceClass;                               // 0x1A5
    UCHAR HwFeedbackEfficiencyClass;                                // 0x1A6
    UCHAR HeteroCoreType;                                           // 0x1A7
} PROCESSOR_POWER_STATE, *PPROCESSOR_POWER_STATE;
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, ClassConcurrency) == 0x15C);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, ArchitecturalEfficiencyClass) == 0x160);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, HeteroCoreType) == 0x1A7);
C_ASSERT(sizeof(PROCESSOR_POWER_STATE) == 0x1A8);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, PerformanceSchedulingClass) == 0x161);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, EfficiencySchedulingClass) == 0x162);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, Unused) == 0x163);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, Parked) == 0x164);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, LongPriorQosPeriod) == 0x165);
C_ASSERT(FIELD_OFFSET(PROCESSOR_POWER_STATE, SoftParked) == 0x02B);
#endif

/* Loader/kernel contract, independent of the NTDDI-selected KPRCB layout.
 * Vista+ PCRs exceed one page. Shared user data must not alias this storage. */
#define KIPCR_BOOT_SIZE 0x7000

//
// Dependencies
//

//
// Kernel Feature Bits
// See https://www.geoffchappell.com/studies/windows/km/ntoskrnl/structs/kprcb/featurebits.htm?tx=61&ts=0,1400
//
#define KF_V86_VIS                      0x00000001 // From ks386.inc
#define KF_RDTSC                        0x00000002 // From ks386.inc, ksamd64.inc
#define KF_CR4                          0x00000004 // From ks386.inc, ksamd64.inc
#define KF_CMOV                         0x00000008
#define KF_GLOBAL_PAGE                  0x00000010 // From ks386.inc, ksamd64.inc
#define KF_LARGE_PAGE                   0x00000020 // From ks386.inc, ksamd64.inc
#define KF_MTRR                         0x00000040
#define KF_CMPXCHG8B                    0x00000080 // From ks386.inc, ksamd64.inc
#define KF_MMX                          0x00000100
#define KF_WORKING_PTE                  0x00000200
#define KF_PAT                          0x00000400
#define KF_FXSR                         0x00000800
#define KF_FAST_SYSCALL                 0x00001000 // From ks386.inc, ksamd64.inc
#define KF_XMMI                         0x00002000 // SSE
#define KF_3DNOW                        0x00004000
#define KF_AMDK6MTRR                    0x00008000 // Win 5.0-6.1
#define KF_XSAVEOPT                     0x00008000 // From KF_XSAVEOPT_BIT
#define KF_XMMI64                       0x00010000 // SSE2
#define KF_DTS                          0x00020000
#define KF_CLFLUSH                      0x00040000 // Win 6.0+
#define KF_SSE3                         0x00080000 // Win 6.0+
#define KF_AUTHENTICAMD                 0x00100000 // Win 6.1+ (family 5+)
#define KF_ACNT2                        0x00200000 // Win 6.1+
#define KF_XSTATE                       0x00400000 // From ks386.inc, Win 6.1+
#define KF_GENUINE_INTEL                0x00800000 // Win 6.1+
#define KF_SMEP                         0x01000000 // Win 6.2+
#define KF_RDRAND                       0x02000000 // Win 6.3+
#define KF_SLAT                         0x04000000 // Win 6.2+, Intel: EPT supported
#define KF_08000000                     0x08000000 // Win 6.2+
#define KF_NX_BIT                       0x20000000
#define KF_NX_DISABLED                  0x40000000
#define KF_NX_ENABLED                   0x80000000
#define KF_RDTSCP               0x0000000100000000ULL // Win 10.0+
#define KF_CLFLUSHOPT           0x0000000200000000ULL // Win 10.0+
#define KF_HDC                  0x0000000400000000ULL // Win 10.0+
#define KF_FPU_LEAKAGE          0x0000001000000000ULL // From ks386.inc KF_FPU_LEAKAGE_BIT
#define KF_SSSE3                0x0000004000000000ULL
#define KF_SSE4_1               0x0000008000000000ULL
#define KF_SSE4_2               0x0000010000000000ULL

#define KF_FPU_LEAKAGE_BIT              36 // From ks386.inc (0x24 -> 0x1000000000)

//
// KPCR Access for non-IA64 builds
//
#define K0IPCR                  ((ULONG_PTR)(KIP0PCRADDRESS))
#define PCR                     ((KPCR *)K0IPCR)
#if defined(CONFIG_SMP) || defined(NT_BUILD)
//#undef  KeGetPcr
#define KeGetPcr()              ((KPCR *)__readfsdword(FIELD_OFFSET(KPCR, SelfPcr)))
#else
#define KeGetPcr()              PCR
#endif

//
// CPU Vendors
//
typedef enum
{
    CPU_NONE      = 0,
    CPU_INTEL     = 1,
    CPU_AMD       = 2,
    CPU_CYRIX     = 3,
    CPU_TRANSMETA = 4,
    CPU_VIA       = 5,
    CPU_CENTAUR   = CPU_VIA,
    CPU_RISE      = 6,
    CPU_UNKNOWN   = 7
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
#define KGDT_NULL               0x00
#define KGDT_R0_CODE            0x08
#define KGDT_R0_DATA            0x10
#define KGDT_R3_CODE            0x18
#define KGDT_R3_DATA            0x20
#define KGDT_TSS                0x28
#define KGDT_R0_PCR             0x30
#define KGDT_R3_TEB             0x38
#define KGDT_VDM_TILE           0x40
#define KGDT_LDT                0x48
#define KGDT_DF_TSS             0x50
#define KGDT_NMI_TSS            0x58

//
// Define the number of GDTs that can be queried by user mode
//
#define KGDT_NUMBER             10

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
#define EFLAGS_RF               0x10000
#define EFLAGS_V86_MASK         0x20000
#define EFLAGS_ALIGN_CHECK      0x40000
#define EFLAGS_VIF              0x80000
#define EFLAGS_VIP              0x100000
#define EFLAGS_ID               0x200000
#define EFLAGS_USER_SANITIZE    0x3F4DD7
#define EFLAG_SIGN              0x8000
#define EFLAG_ZERO              0x4000

//
// Legacy floating status word bit masks.
//
#define FSW_INVALID_OPERATION   0x1
#define FSW_DENORMAL            0x2
#define FSW_ZERO_DIVIDE         0x4
#define FSW_OVERFLOW            0x8
#define FSW_UNDERFLOW           0x10
#define FSW_PRECISION           0x20
#define FSW_STACK_FAULT         0x40

//
// Machine Specific Registers
//
#define MSR_AMD_ACCESS          0x9C5A203A
#define MSR_IA32_MISC_ENABLE    0x01A0
#define MSR_EFER                0xC0000080

//
// MSR internal Values
//
#define MSR_NXE                 0x0800
#define XHF_NOEXECUTE           0x100000
#define MSR_XD_ENABLE_MASK      0xFFFFFFFB

//
// IPI Types
//
#define IPI_APC                 1
#define IPI_DPC                 2
#define IPI_FREEZE              4
#define IPI_PACKET_READY        8
#define IPI_SYNCH_REQUEST       16

//
// PRCB Flags
//
#define PRCB_MINOR_VERSION      1
#define PRCB_MAJOR_VERSION      1
#define PRCB_BUILD_DEBUG        1
#define PRCB_BUILD_UNIPROCESSOR 2

//
// HAL Variables
//
#define INITIAL_STALL_COUNT     100
#define MM_HAL_VA_START         0xFFC00000
#define MM_HAL_VA_END           0xFFFFFFFF
#define APIC_BASE               0xFFFE0000

//
// IOPM Definitions
//
#define IOPM_COUNT              1
#define IOPM_SIZE               8192
#define IOPM_FULL_SIZE          8196
#define IO_ACCESS_MAP_NONE      0
#define IOPM_DIRECTION_MAP_SIZE 32
#define IOPM_OFFSET             FIELD_OFFSET(KTSS, IoMaps[0].IoMap)
#define KiComputeIopmOffset(MapNumber)              \
    (MapNumber == IO_ACCESS_MAP_NONE) ?             \
        (USHORT)(sizeof(KTSS)) :                    \
        (USHORT)(FIELD_OFFSET(KTSS, IoMaps[MapNumber-1].IoMap))

typedef UCHAR KIO_ACCESS_MAP[IOPM_SIZE];

typedef KIO_ACCESS_MAP *PKIO_ACCESS_MAP;

//
// Size of the XMM register save area in the FXSAVE format
//
#define SIZE_OF_FX_REGISTERS    128

//
// Static Kernel-Mode Address start (use MM_KSEG0_BASE for actual)
//
#define KSEG0_BASE              0x80000000

//
// Synchronization-level IRQL
//
#ifndef CONFIG_SMP
#define SYNCH_LEVEL             DISPATCH_LEVEL
#else
#define SYNCH_LEVEL             (IPI_LEVEL - 2)
#endif

//
// Double fault stack size
//
#define DOUBLE_FAULT_STACK_SIZE             0x3000

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
    ULONG DbgEbp;
    ULONG DbgEip;
    ULONG DbgArgMark;
    ULONG DbgArgPointer;
    ULONG TempSegCs;
    ULONG TempEsp;
    ULONG Dr0;
    ULONG Dr1;
    ULONG Dr2;
    ULONG Dr3;
    ULONG Dr6;
    ULONG Dr7;
    ULONG SegGs;
    ULONG SegEs;
    ULONG SegDs;
    ULONG Edx;
    ULONG Ecx;
    ULONG Eax;
    ULONG PreviousPreviousMode;
    struct _EXCEPTION_REGISTRATION_RECORD FAR *ExceptionList;
    ULONG SegFs;
    ULONG Edi;
    ULONG Esi;
    ULONG Ebx;
    ULONG Ebp;
    ULONG ErrCode;
    ULONG Eip;
    ULONG SegCs;
    ULONG EFlags;
    ULONG HardwareEsp;
    ULONG HardwareSegSs;
    ULONG V86Es;
    ULONG V86Ds;
    ULONG V86Fs;
    ULONG V86Gs;
} KTRAP_FRAME, *PKTRAP_FRAME;

//
// Defines the Callback Stack Layout for User Mode Callbacks
//
typedef struct _KCALLOUT_FRAME
{
    ULONG InitialStack;
    ULONG TrapFrame;
    ULONG CallbackStack;
    ULONG Edi;
    ULONG Esi;
    ULONG Ebx;
    ULONG Ebp;
    ULONG ReturnAddress;
    ULONG Result;
    ULONG ResultLength;
} KCALLOUT_FRAME, *PKCALLOUT_FRAME;

//
// LDT Entry Definition
//
#ifndef _LDT_ENTRY_DEFINED
#define _LDT_ENTRY_DEFINED
typedef struct _LDT_ENTRY
{
    USHORT LimitLow;
    USHORT BaseLow;
    union
    {
        struct
        {
            UCHAR BaseMid;
            UCHAR Flags1;
            UCHAR Flags2;
            UCHAR BaseHi;
        } Bytes;
        struct
        {
            ULONG BaseMid:8;
            ULONG Type:5;
            ULONG Dpl:2;
            ULONG Pres:1;
            ULONG LimitHi:4;
            ULONG Sys:1;
            ULONG Reserved_0:1;
            ULONG Default_Big:1;
            ULONG Granularity:1;
            ULONG BaseHi:8;
        } Bits;
    } HighWord;
} LDT_ENTRY, *PLDT_ENTRY, *LPLDT_ENTRY;
#endif

//
// GDT Entry Definition
//
typedef struct _KGDTENTRY
{
    USHORT LimitLow;
    USHORT BaseLow;
    union
    {
        struct
        {
            UCHAR BaseMid;
            UCHAR Flags1;
            UCHAR Flags2;
            UCHAR BaseHi;
        } Bytes;
        struct
        {
            ULONG BaseMid:8;
            ULONG Type:5;
            ULONG Dpl:2;
            ULONG Pres:1;
            ULONG LimitHi:4;
            ULONG Sys:1;
            ULONG Reserved_0:1;
            ULONG Default_Big:1;
            ULONG Granularity:1;
            ULONG BaseHi:8;
        } Bits;
    } HighWord;
} KGDTENTRY, *PKGDTENTRY;

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
typedef struct _KIDTENTRY
{
    USHORT Offset;
    USHORT Selector;
    USHORT Access;
    USHORT ExtendedOffset;
} KIDTENTRY, *PKIDTENTRY;

typedef struct _DESCRIPTOR
{
    USHORT Pad;
    USHORT Limit;
    ULONG Base;
} KDESCRIPTOR, *PKDESCRIPTOR;

#ifndef NTOS_MODE_USER
//
// Macro to get current KPRCB
//
FORCEINLINE
struct _KPRCB *
KeGetCurrentPrcb(VOID)
{
    return (struct _KPRCB *)(ULONG_PTR)__readfsdword(FIELD_OFFSET(KPCR, Prcb));
}

//
// FN/FX (FPU) Save Area Structures
//
typedef struct _FNSAVE_FORMAT
{
    ULONG ControlWord;
    ULONG StatusWord;
    ULONG TagWord;
    ULONG ErrorOffset;
    ULONG ErrorSelector;
    ULONG DataOffset;
    ULONG DataSelector;
    UCHAR RegisterArea[80];
} FNSAVE_FORMAT, *PFNSAVE_FORMAT;

typedef struct _FXSAVE_FORMAT
{
    USHORT ControlWord;
    USHORT StatusWord;
    USHORT TagWord;
    USHORT ErrorOpcode;
    ULONG ErrorOffset;
    ULONG ErrorSelector;
    ULONG DataOffset;
    ULONG DataSelector;
    ULONG MXCsr;
    ULONG MXCsrMask;
    UCHAR RegisterArea[SIZE_OF_FX_REGISTERS];
    UCHAR Reserved3[128];
    UCHAR Reserved4[224];
    UCHAR Align16Byte[8];
} FXSAVE_FORMAT, *PFXSAVE_FORMAT;

typedef struct _FX_SAVE_AREA
{
    union
    {
        FNSAVE_FORMAT FnArea;
        FXSAVE_FORMAT FxArea;
    } U;
    ULONG NpxSavedCpu;
    ULONG Cr0NpxState;
} FX_SAVE_AREA, *PFX_SAVE_AREA;

//
// Special Registers Structure (outside of CONTEXT)
//
typedef struct _KSPECIAL_REGISTERS
{
    ULONG Cr0;
    ULONG Cr2;
    ULONG Cr3;
    ULONG Cr4;
    ULONG KernelDr0;
    ULONG KernelDr1;
    ULONG KernelDr2;
    ULONG KernelDr3;
    ULONG KernelDr6;
    ULONG KernelDr7;
    KDESCRIPTOR Gdtr;
    KDESCRIPTOR Idtr;
    USHORT Tr;
    USHORT Ldtr;
    ULONG Reserved[6];
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

//
// Processor State Data
//
typedef struct _KPROCESSOR_STATE
{
    CONTEXT ContextFrame;
    KSPECIAL_REGISTERS SpecialRegisters;
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;

//
// Processor Region Control Block
//
#pragma pack(push,4)
typedef union _KPRCBFLAG
{
    volatile LONG PrcbFlags;
    struct
    {
        ULONG BamQosLevel:8;
        ULONG PendingQosUpdate:2;
        ULONG CacheIsolationEnabled:1;
        ULONG TracepointActive:1;
        ULONG PrcbFlagsReserved:20;
    };
} KPRCBFLAG, *PKPRCBFLAG;

typedef struct _KPRCB
{
    USHORT MinorVersion; // 0x00000
    USHORT MajorVersion; // 0x00002
    struct _KTHREAD * CurrentThread; // 0x00004
    struct _KTHREAD * NextThread; // 0x00008
    struct _KTHREAD * IdleThread; // 0x0000C
    UCHAR LegacyNumber; // 0x00010
    UCHAR NestingLevel; // 0x00011
    USHORT BuildType; // 0x00012
    UCHAR CpuType; // 0x00014
    UCHAR CpuID; // 0x00015
    union // 0x00016
    {
        USHORT CpuStep;
        struct
        {
            UCHAR CpuStepping;
        };
        struct
        {
            UCHAR PadCpuModel[1];
            UCHAR CpuModel;
        };
    };
    KPROCESSOR_STATE ProcessorState; // 0x00018
    struct _KNODE * ParentNode; // 0x00338
    UCHAR PriorityState[4]; // 0x0033C PCHAR
    ULONG KernelReserved[14]; // 0x00340
    ULONG HalReserved[16]; // 0x00378
    ULONG CFlushSize; // 0x003B8
    UCHAR CoresPerPhysicalProcessor; // 0x003BC
    UCHAR LogicalProcessorsPerCore; // 0x003BD
    UCHAR CpuVendor; // 0x003BE
    UCHAR PrcbPad0[1]; // 0x003BF
    ULONG MHz; // 0x003C0
    UCHAR GroupIndex; // 0x003C4
    UCHAR Group; // 0x003C5
    UCHAR PrcbPad05[2]; // 0x003C6
    union // 0x003C8
    {
        ULONG GroupSetMember;
        KAFFINITY SetMember; // [LiberNT] legacy alias
    };
    ULONG Number; // 0x003CC
    UCHAR ClockOwner; // 0x003D0
    union // 0x003D1
    {
        UCHAR PendingTickFlags;
        struct
        {
            UCHAR PendingTick : 1;
        };
        struct
        {
            UCHAR : 1;
            UCHAR PendingBackupTick : 1;
        };
    };
    UCHAR PrcbPad10[70]; // 0x003D2
    KSPIN_LOCK_QUEUE LockQueue[17]; // 0x00418
    ULONG InterruptCount; // 0x004A0
    ULONG KernelTime; // 0x004A4
    ULONG UserTime; // 0x004A8
    ULONG DpcTime; // 0x004AC
    ULONG DpcTimeCount; // 0x004B0
    ULONG InterruptTime; // 0x004B4
    ULONG AdjustDpcThreshold; // 0x004B8
    ULONG PageColor; // 0x004BC
    UCHAR DebuggerSavedIRQL; // 0x004C0
    UCHAR NodeColor; // 0x004C1
    UCHAR DeepSleep; // 0x004C2
    UCHAR TbFlushListActive; // 0x004C3
    VOID *CachedStack; // 0x004C4
    ULONG NodeShiftedColor; // 0x004C8
    ULONG SecondaryColorMask; // 0x004CC
    ULONG DpcTimeLimit; // 0x004D0
    PVOID MmInternal; // 0x004D4
    UCHAR PrcbFlags[4]; // 0x004D8 _KPRCBFLAG
    PVOID SchedulerAssist; // 0x004DC
    ULONG CcFastReadNoWait; // 0x004E0
    ULONG CcFastReadWait; // 0x004E4
    ULONG CcFastReadNotPossible; // 0x004E8
    ULONG CcCopyReadNoWait; // 0x004EC
    ULONG CcCopyReadWait; // 0x004F0
    ULONG CcCopyReadNoWaitMiss; // 0x004F4
    volatile LONG MmSpinLockOrdering; // 0x004F8
    volatile ULONG IoReadOperationCount; // 0x004FC
    volatile ULONG IoWriteOperationCount; // 0x00500
    volatile ULONG IoOtherOperationCount; // 0x00504
    LARGE_INTEGER IoReadTransferCount; // 0x00508
    LARGE_INTEGER IoWriteTransferCount; // 0x00510
    LARGE_INTEGER IoOtherTransferCount; // 0x00518
    ULONG CcFastMdlReadNoWait; // 0x00520
    ULONG CcFastMdlReadWait; // 0x00524
    ULONG CcFastMdlReadNotPossible; // 0x00528
    ULONG CcMapDataNoWait; // 0x0052C
    ULONG CcMapDataWait; // 0x00530
    ULONG CcPinMappedDataCount; // 0x00534
    ULONG CcPinReadNoWait; // 0x00538
    ULONG CcPinReadWait; // 0x0053C
    ULONG CcMdlReadNoWait; // 0x00540
    ULONG CcMdlReadWait; // 0x00544
    ULONG CcLazyWriteHotSpots; // 0x00548
    ULONG CcLazyWriteIos; // 0x0054C
    ULONG CcLazyWritePages; // 0x00550
    ULONG CcDataFlushes; // 0x00554
    ULONG CcDataPages; // 0x00558
    ULONG CcLostDelayedWrites; // 0x0055C
    ULONG CcFastReadResourceMiss; // 0x00560
    ULONG CcCopyReadWaitMiss; // 0x00564
    ULONG CcFastMdlReadResourceMiss; // 0x00568
    ULONG CcMapDataNoWaitMiss; // 0x0056C
    ULONG CcMapDataWaitMiss; // 0x00570
    ULONG CcPinReadNoWaitMiss; // 0x00574
    ULONG CcPinReadWaitMiss; // 0x00578
    ULONG CcMdlReadNoWaitMiss; // 0x0057C
    ULONG CcMdlReadWaitMiss; // 0x00580
    ULONG CcReadAheadIos; // 0x00584
    ULONG KeAlignmentFixupCount; // 0x00588
    ULONG KeExceptionDispatchCount; // 0x0058C
    ULONG KeSystemCalls; // 0x00590
    ULONG AvailableTime; // 0x00594
    ULONG PrcbPad22[2]; // 0x00598
    PP_LOOKASIDE_LIST PPLookasideList[16]; // 0x005A0
    UCHAR PPNxPagedLookasideList[2304]; // 0x00620 _GENERAL_LOOKASIDE_POOL[32]
    GENERAL_LOOKASIDE_POOL PPNPagedLookasideList[32]; // 0x00F20
    GENERAL_LOOKASIDE_POOL PPPagedLookasideList[32]; // 0x01820
    volatile LONG PacketBarrier; // 0x02120
    volatile LONG ReverseStall; // 0x02124
    PVOID IpiFrame; // 0x02128
    UCHAR PrcbPad3[52]; // 0x0212C
    VOID *CurrentPacket[3]; // 0x02160
    volatile ULONG TargetSet; // 0x0216C
    volatile PKIPI_WORKER WorkerRoutine; // 0x02170
    volatile ULONG IpiFrozen; // 0x02174
    UCHAR PrcbPad4[40]; // 0x02178
    volatile ULONG RequestSummary; // 0x021A0
    volatile LONG TargetCount; // 0x021A4
    ULONGLONG PrcbPad94[1]; // 0x021A8
    ULONGLONG TrappedSecurityDomain; // 0x021B0
    union // 0x021B8
    {
        UCHAR BpbState;
        struct
        {
            UCHAR BpbCpuIdle : 1;
        };
        struct
        {
            UCHAR : 1;
            UCHAR BpbFlushRsbOnTrap : 1;
        };
        struct
        {
            UCHAR : 2;
            UCHAR BpbIbpbOnReturn : 1;
        };
        struct
        {
            UCHAR : 3;
            UCHAR BpbIbpbOnTrap : 1;
        };
        struct
        {
            UCHAR : 4;
            UCHAR BpbReserved : 4;
        };
    };
    union // 0x021B9
    {
        UCHAR BpbFeatures;
        struct
        {
            UCHAR BpbClearOnIdle : 1;
        };
        struct
        {
            UCHAR : 1;
            UCHAR BpbEnabled : 1;
        };
        struct
        {
            UCHAR : 2;
            UCHAR BpbSmep : 1;
        };
        struct
        {
            UCHAR : 3;
            UCHAR BpbFeaturesReserved : 5;
        };
    };
    UCHAR BpbCurrentSpecCtrl; // 0x021BA
    UCHAR BpbKernelSpecCtrl; // 0x021BB
    UCHAR BpbNmiSpecCtrl; // 0x021BC
    UCHAR BpbUserSpecCtrl; // 0x021BD
    UCHAR PrcbPad49[2]; // 0x021BE
    ULONG ProcessorSignature; // 0x021C0
    ULONG ProcessorFlags; // 0x021C4
    UCHAR PrcbPad50[8]; // 0x021C8
    ULONG InterruptLastCount; // 0x021D0
    ULONG InterruptRate; // 0x021D4
    ULONG DeviceInterrupts; // 0x021D8
    PVOID IsrDpcStats; // 0x021DC
    KDPC_DATA DpcData[2]; // 0x021E0
    PVOID DpcStack; // 0x02210
    ULONG MaximumDpcQueueDepth; // 0x02214
    ULONG DpcRequestRate; // 0x02218
    ULONG MinimumDpcRate; // 0x0221C
    ULONG DpcLastCount; // 0x02220
    ULONG PrcbLock; // 0x02224
    UCHAR DpcGate[16]; // 0x02228 _KGATE
    UCHAR IdleState; // 0x02238
    volatile BOOLEAN QuantumEnd; // 0x02239
    volatile UCHAR DpcRoutineActive; // 0x0223A
    volatile UCHAR IdleSchedule; // 0x0223B
    union // 0x0223C
    {
        volatile LONG DpcRequestSummary;
        struct
        {
            SHORT DpcRequestSlot[2];
        };
        struct
        {
            SHORT NormalDpcState;
        };
        struct
        {
            UCHAR PadThreadDpcState[2];
            SHORT ThreadDpcState;
        };
        struct
        {
            ULONG DpcNormalProcessingActive : 1;
        };
        struct
        {
            ULONG : 1;
            ULONG DpcNormalProcessingRequested : 1;
        };
        struct
        {
            ULONG : 2;
            ULONG DpcNormalThreadSignal : 1;
        };
        struct
        {
            ULONG : 3;
            ULONG DpcNormalTimerExpiration : 1;
        };
        struct
        {
            ULONG : 4;
            ULONG DpcNormalDpcPresent : 1;
        };
        struct
        {
            ULONG : 5;
            ULONG DpcNormalLocalInterrupt : 1;
        };
        struct
        {
            ULONG : 6;
            ULONG DpcNormalSpare : 10;
        };
        struct
        {
            ULONG : 16;
            ULONG DpcThreadActive : 1;
        };
        struct
        {
            ULONG : 17;
            ULONG DpcThreadRequested : 1;
        };
        struct
        {
            ULONG : 18;
            ULONG DpcThreadSpare : 14;
        };
    };
    ULONG LastTick; // 0x02240
    ULONG PeriodicCount; // 0x02244
    ULONG PeriodicBias; // 0x02248
    ULONG ClockInterrupts; // 0x0224C
    ULONG ReadyScanTick; // 0x02250
    UCHAR GroupSchedulingOverQuota; // 0x02254
    UCHAR ThreadDpcEnable; // 0x02255
    UCHAR PrcbPad41[6]; // 0x02256
    UCHAR Pad0225C[4];
    union // 0x02260
    {
        UCHAR TimerTable[6224]; // _KTIMER_TABLE
        KSPIN_LOCK_QUEUE TimerTableLockQueue[LOCK_QUEUE_TIMER_TABLE_LOCKS]; // [LiberNT] timer table locks
    };
    ULONG PrcbPad92[12]; // 0x03AB0
    KDPC CallDpc; // 0x03AE0
    LONG ClockKeepAlive; // 0x03B00
    UCHAR PrcbPad6[4]; // 0x03B04
    LONG DpcWatchdogPeriod; // 0x03B08
    LONG DpcWatchdogCount; // 0x03B0C
    volatile LONG KeSpinLockOrdering; // 0x03B10
    ULONG DpcWatchdogProfileCumulativeDpcThreshold; // 0x03B14
    ULONG QueueIndex; // 0x03B18
    SINGLE_LIST_ENTRY DeferredReadyListHead; // 0x03B1C
    ULONG ReadySummary; // 0x03B20
    LONG AffinitizedSelectionMask; // 0x03B24
    ULONG WaitLock; // 0x03B28
    LIST_ENTRY WaitListHead; // 0x03B2C
    ULONG ScbOffset; // 0x03B34
    ULONG ReadyThreadCount; // 0x03B38
    UCHAR Pad03B3C[4];
    ULONGLONG StartCycles; // 0x03B40
    ULONGLONG TaggedCyclesStart; // 0x03B48
    ULONGLONG TaggedCycles[3]; // 0x03B50
    ULONGLONG CycleTime; // 0x03B68
    ULONGLONG AffinitizedCycles; // 0x03B70
    ULONGLONG ImportantCycles; // 0x03B78
    ULONGLONG UnimportantCycles; // 0x03B80
    ULONGLONG ReadyQueueExpectedRunTime; // 0x03B88
    volatile ULONG HighCycleTime; // 0x03B90
    UCHAR Pad03B94[4];
    UCHAR Cycles[64]; // 0x03B98 ULONGLONG[2][4]
    ULONG PrcbPad71; // 0x03BD8
    ULONG DpcWatchdogSequenceNumber; // 0x03BDC
    LIST_ENTRY DispatcherReadyListHead[32]; // 0x03BE0
    PVOID ChainedInterruptList; // 0x03CE0
    LONG LookasideIrpFloat; // 0x03CE4
    UCHAR ScbQueue[8]; // 0x03CE8 _RTL_RB_TREE
    UCHAR ScbList[8]; // 0x03CF0 _LIST_ENTRY
    volatile LONG MmPageFaultCount; // 0x03CF8
    volatile LONG MmCopyOnWriteCount; // 0x03CFC
    volatile LONG MmTransitionCount; // 0x03D00
    volatile LONG MmCacheTransitionCount; // 0x03D04
    volatile LONG MmDemandZeroCount; // 0x03D08
    volatile LONG MmPageReadCount; // 0x03D0C
    volatile LONG MmPageReadIoCount; // 0x03D10
    volatile LONG MmCacheReadCount; // 0x03D14
    volatile LONG MmCacheIoCount; // 0x03D18
    volatile LONG MmDirtyPagesWriteCount; // 0x03D1C
    volatile LONG MmDirtyWriteIoCount; // 0x03D20
    volatile LONG MmMappedPagesWriteCount; // 0x03D24
    volatile LONG MmMappedWriteIoCount; // 0x03D28
    volatile ULONG CachedCommit; // 0x03D2C
    volatile ULONG CachedResidentAvailable; // 0x03D30
    PVOID HyperPte; // 0x03D34
    UCHAR PrcbPad8[4]; // 0x03D38
    UCHAR VendorString[13]; // 0x03D3C
    UCHAR InitialApicId; // 0x03D49
    UCHAR LogicalProcessorsPerPhysicalProcessor; // 0x03D4A
    UCHAR PrcbPad9[1]; // 0x03D4B
    UCHAR Pad03D4C[4];
    ULONGLONG FeatureBits; // 0x03D50
    LARGE_INTEGER UpdateSignature; // 0x03D58
    volatile ULONGLONG IsrTime; // 0x03D60
    ULONGLONG GenerationTarget; // 0x03D68
    PROCESSOR_POWER_STATE PowerState; // 0x03D70
    UCHAR ForceIdleDpc[32]; // 0x03F18 _KDPC
    ULONGLONG MsrIa32TsxCtrl; // 0x03F38
    ULONG PrcbPad91[6]; // 0x03F40
    struct _RTL_HASH_TABLE *DpcRuntimeHistoryHashTable; // 0x03F58
    struct _KDPC *DpcRuntimeHistoryHashTableCleanupDpc; // 0x03F5C
    ULONGLONG CurrentDpcRuntimeHistoryCached; // 0x03F60
    ULONGLONG CurrentDpcStartTime; // 0x03F68
    UCHAR CurrentDpcRoutine[4]; // 0x03F70 0x11BA*
    ULONG DpcWatchdogProfileSingleDpcThreshold; // 0x03F74
    UCHAR DpcWatchdogDpc[32]; // 0x03F78 _KDPC
    UCHAR DpcWatchdogTimer[40]; // 0x03F98 _KTIMER
    UCHAR HypercallPageList[8]; // 0x03FC0 _SLIST_HEADER
    PVOID HypercallCachedPages; // 0x03FC8
    PVOID VirtualApicAssist; // 0x03FCC
    UCHAR StatisticsPage[4]; // 0x03FD0 PULONGLONG
    CACHE_DESCRIPTOR Cache[5]; // 0x03FD4
    ULONG CacheCount; // 0x04010
    UCHAR PackageProcessorSet[12]; // 0x04014 _KAFFINITY_EX
    ULONG SharedReadyQueueMask; // 0x04020
    struct _KSHARED_READY_QUEUE *SharedReadyQueue; // 0x04024
    ULONG SharedQueueScanOwner; // 0x04028
    ULONG CoreProcessorSet; // 0x0402C
    ULONG ScanSiblingMask; // 0x04030
    ULONG LLCMask; // 0x04034
    ULONG CacheProcessorMask[5]; // 0x04038
    ULONG ScanSiblingIndex; // 0x0404C
    PVOID WheaInfo; // 0x04050
    PVOID EtwSupport; // 0x04054
    UCHAR InterruptObjectPool[8]; // 0x04058 _SLIST_HEADER
    PVOID *DpcWatchdogProfile; // 0x04060
    PVOID *DpcWatchdogProfileCurrentEmptyCapture; // 0x04064
    ULONG PackageId; // 0x04068
    ULONG PteBitCache; // 0x0406C
    ULONG PteBitOffset; // 0x04070
    ULONG PrcbPad93; // 0x04074
    struct _PROCESSOR_PROFILE_CONTROL_AREA *ProcessorProfileControlArea; // 0x04078
    PVOID ProfileEventIndexAddress; // 0x0407C
    UCHAR TimerExpirationDpc[32]; // 0x04080 _KDPC
    UCHAR SynchCounters[184]; // 0x040A0 _SYNCH_COUNTERS
    UCHAR FsCounters[16]; // 0x04158 _FILESYSTEM_DISK_COUNTERS
    struct _CONTEXT *Context; // 0x04168
    ULONG ContextFlagsInit; // 0x0416C
    struct _XSAVE_AREA *ExtendedState; // 0x04170
    UCHAR EntropyTimingState[296]; // 0x04174 _KENTROPY_TIMING_STATE
    PVOID IsrStack; // 0x0429C
    struct _KINTERRUPT *VectorToInterruptObject[208]; // 0x042A0
    UCHAR AbSelfIoBoostsList[4]; // 0x045E0 _SINGLE_LIST_ENTRY
    UCHAR AbPropagateBoostsList[4]; // 0x045E4 _SINGLE_LIST_ENTRY
    UCHAR AbDpc[32]; // 0x045E8 _KDPC
    UCHAR IoIrpStackProfilerCurrent[84]; // 0x04608 _IOP_IRP_STACK_PROFILER
    UCHAR IoIrpStackProfilerPrevious[84]; // 0x0465C _IOP_IRP_STACK_PROFILER
    UCHAR TimerExpirationTrace[256]; // 0x046B0 _KTIMER_EXPIRATION_TRACE[16]
    ULONG TimerExpirationTraceCount; // 0x047B0
    PVOID ExSaPageArray; // 0x047B4
    struct _XSAVE_AREA_HEADER *ExtendedSupervisorState; // 0x047B8
    ULONG PrcbPad100[9]; // 0x047BC
    UCHAR LocalSharedReadyQueue[320]; // 0x047E0 _KSHARED_READY_QUEUE
    struct _REQUEST_MAILBOX *Mailbox; // 0x04920
    UCHAR PrcbPad[1468]; // 0x04924
    ULONG KernelDirectoryTableBase; // 0x04EE0
    ULONG EspBaseShadow; // 0x04EE4
    ULONG UserEspShadow; // 0x04EE8
    ULONG ShadowFlags; // 0x04EEC
    ULONG UserDS; // 0x04EF0
    ULONG UserES; // 0x04EF4
    ULONG UserFS; // 0x04EF8
    PVOID EspIretd; // 0x04EFC
    ULONG RestoreSegOption; // 0x04F00
    ULONG SavedEsi; // 0x04F04
    USHORT VerwSelector; // 0x04F08
    USHORT PrcbShadowPad; // 0x04F0A
    ULONG TaskSwitchCount; // 0x04F0C
    ULONG DbgLogs[512]; // 0x04F10
    ULONG DbgCount; // 0x05710
    union // 0x05714
    {
        ULONG PrcbPadRemainingPage[499];
        struct // [LiberNT] state Windows does not keep in the KPRCB
        {
            KEVENT DpcEvent;
            LONG DpcSetEventRequest;
            LONG DpcWatchDogCount;
            ULONG FeatureBitsHigh;
            ULONG MultiThreadProcessorSet;
            struct _KPRCB * MultiThreadSetMaster;
            struct _KTHREAD * NpxThread;
            UCHAR Reserved;
            UCHAR SkipTick;
            LONG Sleeping;
            LONG TickOffset;
            volatile ULONG TimerRequest;
        };
    };
    UCHAR RequestMailbox[32]; // 0x05EE0 _REQUEST_MAILBOX[1]
} KPRCB, *PKPRCB;
C_ASSERT(FIELD_OFFSET(KPRCB, MinorVersion) == 0x0);
C_ASSERT(FIELD_OFFSET(KPRCB, MajorVersion) == 0x2);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentThread) == 0x4);
C_ASSERT(FIELD_OFFSET(KPRCB, NextThread) == 0x8);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleThread) == 0xC);
C_ASSERT(FIELD_OFFSET(KPRCB, LegacyNumber) == 0x10);
C_ASSERT(FIELD_OFFSET(KPRCB, NestingLevel) == 0x11);
C_ASSERT(FIELD_OFFSET(KPRCB, BuildType) == 0x12);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuType) == 0x14);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuID) == 0x15);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuStep) == 0x16);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorState) == 0x18);
C_ASSERT(FIELD_OFFSET(KPRCB, ParentNode) == 0x338);
C_ASSERT(FIELD_OFFSET(KPRCB, PriorityState) == 0x33C);
C_ASSERT(FIELD_OFFSET(KPRCB, KernelReserved) == 0x340);
C_ASSERT(FIELD_OFFSET(KPRCB, HalReserved) == 0x378);
C_ASSERT(FIELD_OFFSET(KPRCB, CFlushSize) == 0x3B8);
C_ASSERT(FIELD_OFFSET(KPRCB, CoresPerPhysicalProcessor) == 0x3BC);
C_ASSERT(FIELD_OFFSET(KPRCB, LogicalProcessorsPerCore) == 0x3BD);
C_ASSERT(FIELD_OFFSET(KPRCB, CpuVendor) == 0x3BE);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad0) == 0x3BF);
C_ASSERT(FIELD_OFFSET(KPRCB, MHz) == 0x3C0);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupIndex) == 0x3C4);
C_ASSERT(FIELD_OFFSET(KPRCB, Group) == 0x3C5);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad05) == 0x3C6);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupSetMember) == 0x3C8);
C_ASSERT(FIELD_OFFSET(KPRCB, Number) == 0x3CC);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockOwner) == 0x3D0);
C_ASSERT(FIELD_OFFSET(KPRCB, PendingTickFlags) == 0x3D1);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad10) == 0x3D2);
C_ASSERT(FIELD_OFFSET(KPRCB, LockQueue) == 0x418);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptCount) == 0x4A0);
C_ASSERT(FIELD_OFFSET(KPRCB, KernelTime) == 0x4A4);
C_ASSERT(FIELD_OFFSET(KPRCB, UserTime) == 0x4A8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcTime) == 0x4AC);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcTimeCount) == 0x4B0);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptTime) == 0x4B4);
C_ASSERT(FIELD_OFFSET(KPRCB, AdjustDpcThreshold) == 0x4B8);
C_ASSERT(FIELD_OFFSET(KPRCB, PageColor) == 0x4BC);
C_ASSERT(FIELD_OFFSET(KPRCB, DebuggerSavedIRQL) == 0x4C0);
C_ASSERT(FIELD_OFFSET(KPRCB, NodeColor) == 0x4C1);
C_ASSERT(FIELD_OFFSET(KPRCB, DeepSleep) == 0x4C2);
C_ASSERT(FIELD_OFFSET(KPRCB, TbFlushListActive) == 0x4C3);
C_ASSERT(FIELD_OFFSET(KPRCB, CachedStack) == 0x4C4);
C_ASSERT(FIELD_OFFSET(KPRCB, NodeShiftedColor) == 0x4C8);
C_ASSERT(FIELD_OFFSET(KPRCB, SecondaryColorMask) == 0x4CC);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcTimeLimit) == 0x4D0);
C_ASSERT(FIELD_OFFSET(KPRCB, MmInternal) == 0x4D4);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbFlags) == 0x4D8);
C_ASSERT(FIELD_OFFSET(KPRCB, SchedulerAssist) == 0x4DC);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadNoWait) == 0x4E0);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadWait) == 0x4E4);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadNotPossible) == 0x4E8);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadNoWait) == 0x4EC);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadWait) == 0x4F0);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadNoWaitMiss) == 0x4F4);
C_ASSERT(FIELD_OFFSET(KPRCB, MmSpinLockOrdering) == 0x4F8);
C_ASSERT(FIELD_OFFSET(KPRCB, IoReadOperationCount) == 0x4FC);
C_ASSERT(FIELD_OFFSET(KPRCB, IoWriteOperationCount) == 0x500);
C_ASSERT(FIELD_OFFSET(KPRCB, IoOtherOperationCount) == 0x504);
C_ASSERT(FIELD_OFFSET(KPRCB, IoReadTransferCount) == 0x508);
C_ASSERT(FIELD_OFFSET(KPRCB, IoWriteTransferCount) == 0x510);
C_ASSERT(FIELD_OFFSET(KPRCB, IoOtherTransferCount) == 0x518);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadNoWait) == 0x520);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadWait) == 0x524);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadNotPossible) == 0x528);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataNoWait) == 0x52C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataWait) == 0x530);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinMappedDataCount) == 0x534);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadNoWait) == 0x538);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadWait) == 0x53C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadNoWait) == 0x540);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadWait) == 0x544);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLazyWriteHotSpots) == 0x548);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLazyWriteIos) == 0x54C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLazyWritePages) == 0x550);
C_ASSERT(FIELD_OFFSET(KPRCB, CcDataFlushes) == 0x554);
C_ASSERT(FIELD_OFFSET(KPRCB, CcDataPages) == 0x558);
C_ASSERT(FIELD_OFFSET(KPRCB, CcLostDelayedWrites) == 0x55C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastReadResourceMiss) == 0x560);
C_ASSERT(FIELD_OFFSET(KPRCB, CcCopyReadWaitMiss) == 0x564);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadResourceMiss) == 0x568);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataNoWaitMiss) == 0x56C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMapDataWaitMiss) == 0x570);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadNoWaitMiss) == 0x574);
C_ASSERT(FIELD_OFFSET(KPRCB, CcPinReadWaitMiss) == 0x578);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadNoWaitMiss) == 0x57C);
C_ASSERT(FIELD_OFFSET(KPRCB, CcMdlReadWaitMiss) == 0x580);
C_ASSERT(FIELD_OFFSET(KPRCB, CcReadAheadIos) == 0x584);
C_ASSERT(FIELD_OFFSET(KPRCB, KeAlignmentFixupCount) == 0x588);
C_ASSERT(FIELD_OFFSET(KPRCB, KeExceptionDispatchCount) == 0x58C);
C_ASSERT(FIELD_OFFSET(KPRCB, KeSystemCalls) == 0x590);
C_ASSERT(FIELD_OFFSET(KPRCB, AvailableTime) == 0x594);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad22) == 0x598);
C_ASSERT(FIELD_OFFSET(KPRCB, PPLookasideList) == 0x5A0);
C_ASSERT(FIELD_OFFSET(KPRCB, PPNxPagedLookasideList) == 0x620);
C_ASSERT(FIELD_OFFSET(KPRCB, PPNPagedLookasideList) == 0xF20);
C_ASSERT(FIELD_OFFSET(KPRCB, PPPagedLookasideList) == 0x1820);
C_ASSERT(FIELD_OFFSET(KPRCB, PacketBarrier) == 0x2120);
C_ASSERT(FIELD_OFFSET(KPRCB, ReverseStall) == 0x2124);
C_ASSERT(FIELD_OFFSET(KPRCB, IpiFrame) == 0x2128);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad3) == 0x212C);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentPacket) == 0x2160);
C_ASSERT(FIELD_OFFSET(KPRCB, TargetSet) == 0x216C);
C_ASSERT(FIELD_OFFSET(KPRCB, WorkerRoutine) == 0x2170);
C_ASSERT(FIELD_OFFSET(KPRCB, IpiFrozen) == 0x2174);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad4) == 0x2178);
C_ASSERT(FIELD_OFFSET(KPRCB, RequestSummary) == 0x21A0);
C_ASSERT(FIELD_OFFSET(KPRCB, TargetCount) == 0x21A4);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad94) == 0x21A8);
C_ASSERT(FIELD_OFFSET(KPRCB, TrappedSecurityDomain) == 0x21B0);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbState) == 0x21B8);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbFeatures) == 0x21B9);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbCurrentSpecCtrl) == 0x21BA);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbKernelSpecCtrl) == 0x21BB);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbNmiSpecCtrl) == 0x21BC);
C_ASSERT(FIELD_OFFSET(KPRCB, BpbUserSpecCtrl) == 0x21BD);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad49) == 0x21BE);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorSignature) == 0x21C0);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorFlags) == 0x21C4);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad50) == 0x21C8);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptLastCount) == 0x21D0);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptRate) == 0x21D4);
C_ASSERT(FIELD_OFFSET(KPRCB, DeviceInterrupts) == 0x21D8);
C_ASSERT(FIELD_OFFSET(KPRCB, IsrDpcStats) == 0x21DC);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcData) == 0x21E0);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcStack) == 0x2210);
C_ASSERT(FIELD_OFFSET(KPRCB, MaximumDpcQueueDepth) == 0x2214);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRequestRate) == 0x2218);
C_ASSERT(FIELD_OFFSET(KPRCB, MinimumDpcRate) == 0x221C);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcLastCount) == 0x2220);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbLock) == 0x2224);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcGate) == 0x2228);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleState) == 0x2238);
C_ASSERT(FIELD_OFFSET(KPRCB, QuantumEnd) == 0x2239);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRoutineActive) == 0x223A);
C_ASSERT(FIELD_OFFSET(KPRCB, IdleSchedule) == 0x223B);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRequestSummary) == 0x223C);
C_ASSERT(FIELD_OFFSET(KPRCB, LastTick) == 0x2240);
C_ASSERT(FIELD_OFFSET(KPRCB, PeriodicCount) == 0x2244);
C_ASSERT(FIELD_OFFSET(KPRCB, PeriodicBias) == 0x2248);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockInterrupts) == 0x224C);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadyScanTick) == 0x2250);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupSchedulingOverQuota) == 0x2254);
C_ASSERT(FIELD_OFFSET(KPRCB, ThreadDpcEnable) == 0x2255);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad41) == 0x2256);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerTable) == 0x2260);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad92) == 0x3AB0);
C_ASSERT(FIELD_OFFSET(KPRCB, CallDpc) == 0x3AE0);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockKeepAlive) == 0x3B00);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad6) == 0x3B04);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogPeriod) == 0x3B08);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogCount) == 0x3B0C);
C_ASSERT(FIELD_OFFSET(KPRCB, KeSpinLockOrdering) == 0x3B10);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfileCumulativeDpcThreshold) == 0x3B14);
C_ASSERT(FIELD_OFFSET(KPRCB, QueueIndex) == 0x3B18);
C_ASSERT(FIELD_OFFSET(KPRCB, DeferredReadyListHead) == 0x3B1C);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadySummary) == 0x3B20);
C_ASSERT(FIELD_OFFSET(KPRCB, AffinitizedSelectionMask) == 0x3B24);
C_ASSERT(FIELD_OFFSET(KPRCB, WaitLock) == 0x3B28);
C_ASSERT(FIELD_OFFSET(KPRCB, WaitListHead) == 0x3B2C);
C_ASSERT(FIELD_OFFSET(KPRCB, ScbOffset) == 0x3B34);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadyThreadCount) == 0x3B38);
C_ASSERT(FIELD_OFFSET(KPRCB, StartCycles) == 0x3B40);
C_ASSERT(FIELD_OFFSET(KPRCB, TaggedCyclesStart) == 0x3B48);
C_ASSERT(FIELD_OFFSET(KPRCB, TaggedCycles) == 0x3B50);
C_ASSERT(FIELD_OFFSET(KPRCB, CycleTime) == 0x3B68);
C_ASSERT(FIELD_OFFSET(KPRCB, AffinitizedCycles) == 0x3B70);
C_ASSERT(FIELD_OFFSET(KPRCB, ImportantCycles) == 0x3B78);
C_ASSERT(FIELD_OFFSET(KPRCB, UnimportantCycles) == 0x3B80);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadyQueueExpectedRunTime) == 0x3B88);
C_ASSERT(FIELD_OFFSET(KPRCB, HighCycleTime) == 0x3B90);
C_ASSERT(FIELD_OFFSET(KPRCB, Cycles) == 0x3B98);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad71) == 0x3BD8);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogSequenceNumber) == 0x3BDC);
C_ASSERT(FIELD_OFFSET(KPRCB, DispatcherReadyListHead) == 0x3BE0);
C_ASSERT(FIELD_OFFSET(KPRCB, ChainedInterruptList) == 0x3CE0);
C_ASSERT(FIELD_OFFSET(KPRCB, LookasideIrpFloat) == 0x3CE4);
C_ASSERT(FIELD_OFFSET(KPRCB, ScbQueue) == 0x3CE8);
C_ASSERT(FIELD_OFFSET(KPRCB, ScbList) == 0x3CF0);
C_ASSERT(FIELD_OFFSET(KPRCB, MmPageFaultCount) == 0x3CF8);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCopyOnWriteCount) == 0x3CFC);
C_ASSERT(FIELD_OFFSET(KPRCB, MmTransitionCount) == 0x3D00);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCacheTransitionCount) == 0x3D04);
C_ASSERT(FIELD_OFFSET(KPRCB, MmDemandZeroCount) == 0x3D08);
C_ASSERT(FIELD_OFFSET(KPRCB, MmPageReadCount) == 0x3D0C);
C_ASSERT(FIELD_OFFSET(KPRCB, MmPageReadIoCount) == 0x3D10);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCacheReadCount) == 0x3D14);
C_ASSERT(FIELD_OFFSET(KPRCB, MmCacheIoCount) == 0x3D18);
C_ASSERT(FIELD_OFFSET(KPRCB, MmDirtyPagesWriteCount) == 0x3D1C);
C_ASSERT(FIELD_OFFSET(KPRCB, MmDirtyWriteIoCount) == 0x3D20);
C_ASSERT(FIELD_OFFSET(KPRCB, MmMappedPagesWriteCount) == 0x3D24);
C_ASSERT(FIELD_OFFSET(KPRCB, MmMappedWriteIoCount) == 0x3D28);
C_ASSERT(FIELD_OFFSET(KPRCB, CachedCommit) == 0x3D2C);
C_ASSERT(FIELD_OFFSET(KPRCB, CachedResidentAvailable) == 0x3D30);
C_ASSERT(FIELD_OFFSET(KPRCB, HyperPte) == 0x3D34);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad8) == 0x3D38);
C_ASSERT(FIELD_OFFSET(KPRCB, VendorString) == 0x3D3C);
C_ASSERT(FIELD_OFFSET(KPRCB, InitialApicId) == 0x3D49);
C_ASSERT(FIELD_OFFSET(KPRCB, LogicalProcessorsPerPhysicalProcessor) == 0x3D4A);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad9) == 0x3D4B);
C_ASSERT(FIELD_OFFSET(KPRCB, FeatureBits) == 0x3D50);
C_ASSERT(FIELD_OFFSET(KPRCB, UpdateSignature) == 0x3D58);
C_ASSERT(FIELD_OFFSET(KPRCB, IsrTime) == 0x3D60);
C_ASSERT(FIELD_OFFSET(KPRCB, GenerationTarget) == 0x3D68);
C_ASSERT(FIELD_OFFSET(KPRCB, PowerState) == 0x3D70);
C_ASSERT(FIELD_OFFSET(KPRCB, ForceIdleDpc) == 0x3F18);
C_ASSERT(FIELD_OFFSET(KPRCB, MsrIa32TsxCtrl) == 0x3F38);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad91) == 0x3F40);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRuntimeHistoryHashTable) == 0x3F58);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRuntimeHistoryHashTableCleanupDpc) == 0x3F5C);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentDpcRuntimeHistoryCached) == 0x3F60);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentDpcStartTime) == 0x3F68);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentDpcRoutine) == 0x3F70);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfileSingleDpcThreshold) == 0x3F74);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogDpc) == 0x3F78);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogTimer) == 0x3F98);
C_ASSERT(FIELD_OFFSET(KPRCB, HypercallPageList) == 0x3FC0);
C_ASSERT(FIELD_OFFSET(KPRCB, HypercallCachedPages) == 0x3FC8);
C_ASSERT(FIELD_OFFSET(KPRCB, VirtualApicAssist) == 0x3FCC);
C_ASSERT(FIELD_OFFSET(KPRCB, StatisticsPage) == 0x3FD0);
C_ASSERT(FIELD_OFFSET(KPRCB, Cache) == 0x3FD4);
C_ASSERT(FIELD_OFFSET(KPRCB, CacheCount) == 0x4010);
C_ASSERT(FIELD_OFFSET(KPRCB, PackageProcessorSet) == 0x4014);
C_ASSERT(FIELD_OFFSET(KPRCB, SharedReadyQueueMask) == 0x4020);
C_ASSERT(FIELD_OFFSET(KPRCB, SharedReadyQueue) == 0x4024);
C_ASSERT(FIELD_OFFSET(KPRCB, SharedQueueScanOwner) == 0x4028);
C_ASSERT(FIELD_OFFSET(KPRCB, CoreProcessorSet) == 0x402C);
C_ASSERT(FIELD_OFFSET(KPRCB, ScanSiblingMask) == 0x4030);
C_ASSERT(FIELD_OFFSET(KPRCB, LLCMask) == 0x4034);
C_ASSERT(FIELD_OFFSET(KPRCB, CacheProcessorMask) == 0x4038);
C_ASSERT(FIELD_OFFSET(KPRCB, ScanSiblingIndex) == 0x404C);
C_ASSERT(FIELD_OFFSET(KPRCB, WheaInfo) == 0x4050);
C_ASSERT(FIELD_OFFSET(KPRCB, EtwSupport) == 0x4054);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptObjectPool) == 0x4058);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfile) == 0x4060);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcWatchdogProfileCurrentEmptyCapture) == 0x4064);
C_ASSERT(FIELD_OFFSET(KPRCB, PackageId) == 0x4068);
C_ASSERT(FIELD_OFFSET(KPRCB, PteBitCache) == 0x406C);
C_ASSERT(FIELD_OFFSET(KPRCB, PteBitOffset) == 0x4070);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad93) == 0x4074);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorProfileControlArea) == 0x4078);
C_ASSERT(FIELD_OFFSET(KPRCB, ProfileEventIndexAddress) == 0x407C);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerExpirationDpc) == 0x4080);
C_ASSERT(FIELD_OFFSET(KPRCB, SynchCounters) == 0x40A0);
C_ASSERT(FIELD_OFFSET(KPRCB, FsCounters) == 0x4158);
C_ASSERT(FIELD_OFFSET(KPRCB, Context) == 0x4168);
C_ASSERT(FIELD_OFFSET(KPRCB, ContextFlagsInit) == 0x416C);
C_ASSERT(FIELD_OFFSET(KPRCB, ExtendedState) == 0x4170);
C_ASSERT(FIELD_OFFSET(KPRCB, EntropyTimingState) == 0x4174);
C_ASSERT(FIELD_OFFSET(KPRCB, IsrStack) == 0x429C);
C_ASSERT(FIELD_OFFSET(KPRCB, VectorToInterruptObject) == 0x42A0);
C_ASSERT(FIELD_OFFSET(KPRCB, AbSelfIoBoostsList) == 0x45E0);
C_ASSERT(FIELD_OFFSET(KPRCB, AbPropagateBoostsList) == 0x45E4);
C_ASSERT(FIELD_OFFSET(KPRCB, AbDpc) == 0x45E8);
C_ASSERT(FIELD_OFFSET(KPRCB, IoIrpStackProfilerCurrent) == 0x4608);
C_ASSERT(FIELD_OFFSET(KPRCB, IoIrpStackProfilerPrevious) == 0x465C);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerExpirationTrace) == 0x46B0);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerExpirationTraceCount) == 0x47B0);
C_ASSERT(FIELD_OFFSET(KPRCB, ExSaPageArray) == 0x47B4);
C_ASSERT(FIELD_OFFSET(KPRCB, ExtendedSupervisorState) == 0x47B8);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad100) == 0x47BC);
C_ASSERT(FIELD_OFFSET(KPRCB, LocalSharedReadyQueue) == 0x47E0);
C_ASSERT(FIELD_OFFSET(KPRCB, Mailbox) == 0x4920);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPad) == 0x4924);
C_ASSERT(FIELD_OFFSET(KPRCB, KernelDirectoryTableBase) == 0x4EE0);
C_ASSERT(FIELD_OFFSET(KPRCB, EspBaseShadow) == 0x4EE4);
C_ASSERT(FIELD_OFFSET(KPRCB, UserEspShadow) == 0x4EE8);
C_ASSERT(FIELD_OFFSET(KPRCB, ShadowFlags) == 0x4EEC);
C_ASSERT(FIELD_OFFSET(KPRCB, UserDS) == 0x4EF0);
C_ASSERT(FIELD_OFFSET(KPRCB, UserES) == 0x4EF4);
C_ASSERT(FIELD_OFFSET(KPRCB, UserFS) == 0x4EF8);
C_ASSERT(FIELD_OFFSET(KPRCB, EspIretd) == 0x4EFC);
C_ASSERT(FIELD_OFFSET(KPRCB, RestoreSegOption) == 0x4F00);
C_ASSERT(FIELD_OFFSET(KPRCB, SavedEsi) == 0x4F04);
C_ASSERT(FIELD_OFFSET(KPRCB, VerwSelector) == 0x4F08);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbShadowPad) == 0x4F0A);
C_ASSERT(FIELD_OFFSET(KPRCB, TaskSwitchCount) == 0x4F0C);
C_ASSERT(FIELD_OFFSET(KPRCB, DbgLogs) == 0x4F10);
C_ASSERT(FIELD_OFFSET(KPRCB, DbgCount) == 0x5710);
C_ASSERT(FIELD_OFFSET(KPRCB, PrcbPadRemainingPage) == 0x5714);
C_ASSERT(FIELD_OFFSET(KPRCB, RequestMailbox) == 0x5EE0);
C_ASSERT(sizeof(KPRCB) == 0x5F00);

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
            struct _EXCEPTION_REGISTRATION_RECORD *Used_ExceptionList;
            PVOID Used_StackBase;
            ULONG MxCsr;
            PVOID TssCopy;
            ULONG ContextSwitches;
            KAFFINITY SetMemberCopy;
            PVOID Used_Self;
        };
    };
    struct _KPCR *SelfPcr;
    struct _KPRCB *Prcb;
    KIRQL Irql;
    ULONG IRR;
    ULONG IrrActive;
    ULONG IDR;
    PVOID KdVersionBlock;
    PKIDTENTRY IDT;
    PKGDTENTRY GDT;
    struct _KTSS *TSS;
    USHORT MajorVersion;
    USHORT MinorVersion;
    KAFFINITY SetMember;
    ULONG StallScaleFactor;
    UCHAR SpareUnused;
    UCHAR Number;
    UCHAR Spare0;
    UCHAR SecondLevelCacheAssociativity;
    ULONG VdmAlert;
    ULONG KernelReserved[14];
    ULONG SecondLevelCacheSize;
    ULONG HalReserved[16];
    ULONG InterruptMode;
    UCHAR Spare1;
    ULONG KernelReserved2[17];
    KPRCB PrcbData;
} KIPCR, *PKIPCR;
#pragma pack(pop)

//
// TSS Definition
//
typedef struct _KiIoAccessMap
{
    UCHAR DirectionMap[IOPM_DIRECTION_MAP_SIZE];
    UCHAR IoMap[IOPM_FULL_SIZE];
} KIIO_ACCESS_MAP;

typedef struct _KTSS
{
    USHORT Backlink;
    USHORT Reserved0;
    ULONG Esp0;
    USHORT Ss0;
    USHORT Reserved1;
    ULONG NotUsed1[4];
    ULONG CR3;
    ULONG Eip;
    ULONG EFlags;
    ULONG Eax;
    ULONG Ecx;
    ULONG Edx;
    ULONG Ebx;
    ULONG Esp;
    ULONG Ebp;
    ULONG Esi;
    ULONG Edi;
    USHORT Es;
    USHORT Reserved2;
    USHORT Cs;
    USHORT Reserved3;
    USHORT Ss;
    USHORT Reserved4;
    USHORT Ds;
    USHORT Reserved5;
    USHORT Fs;
    USHORT Reserved6;
    USHORT Gs;
    USHORT Reserved7;
    USHORT LDT;
    USHORT Reserved8;
    USHORT Flags;
    USHORT IoMapBase;
    KIIO_ACCESS_MAP IoMaps[IOPM_COUNT];
    UCHAR IntDirectionMap[IOPM_DIRECTION_MAP_SIZE];
} KTSS, *PKTSS;

//
// i386 CPUs don't have exception frames
//
typedef struct _KEXCEPTION_FRAME KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;

#endif /* NTOS_MODE_USER */

typedef enum _WOW64_SERVICE_TYPES
{
    ServiceNoTurbo = 0x0000,
    Service0Arg = 0x0001,
    Service0ArgReloadState = 0x0002,
    Service1ArgSp = 0x0003,
    Service1ArgNSp = 0x0004,
    Service2ArgNSpNSp = 0x0005,
    Service2ArgNSpNSpReloadState = 0x0006,
    Service2ArgSpNSp = 0x0007,
    Service2ArgSpSp = 0x0008,
    Service2ArgNSpSp = 0x0009,
    Service3ArgNSpNSpNSp = 0x000A,
    Service3ArgSpSpSp = 0x000B,
    Service3ArgSpNSpNSp = 0x000C,
    Service3ArgSpNSpNSpReloadState = 0x000D,
    Service3ArgSpSpNSp = 0x000E,
    Service3ArgNSpSpNSp = 0x000F,
    Service3ArgSpNSpSp = 0x0010,
    Service4ArgNSpNSpNSpNSp = 0x0011,
    Service4ArgSpSpNSpNSp = 0x0012,
    Service4ArgSpSpNSpNSpReloadState = 0x0013,
    Service4ArgSpNSpNSpNSp = 0x0014,
    Service4ArgSpNSpNSpNSpReloadState = 0x0015,
    Service4ArgNSpSpNSpNSp = 0x0016,
    Service4ArgSpSpSpNSp = 0x0017,
    ServiceCpupTdQuerySystemTime = 0x0018,
    ServiceCpupTdGetCurrentProcessorNumber = 0x0019,
    ServiceCpupTdReadWriteFile = 0x001A,
    ServiceCpupTdDeviceIoControlFile = 0x001B,
    ServiceCpupTdRemoveIoCompletion = 0x001C,
    ServiceCpupTdWaitForMultipleObjects = 0x001D,
    ServiceCpupTdWaitForMultipleObjects32 = 0x001E,
    Wow64ServiceTypesCount // = 0x001F
} WOW64_SERVICE_TYPES, *PWOW64_SERVICE_TYPES;

typedef enum _VDM_INDEX
{
    VDM_INDEX_Invalid = 0,
    VDM_INDEX_0F,
    VDM_INDEX_ESPrefix,
    VDM_INDEX_CSPrefix,
    VDM_INDEX_SSPrefix,
    VDM_INDEX_DSPrefix,
    VDM_INDEX_FSPrefix,
    VDM_INDEX_GSPrefix,
    VDM_INDEX_OPER32Prefix,
    VDM_INDEX_ADDR32Prefix,
    VDM_INDEX_INSB,
    VDM_INDEX_INSW,
    VDM_INDEX_OUTSB,
    VDM_INDEX_OUTSW,
    VDM_INDEX_PUSHF,
    VDM_INDEX_POPF,
    VDM_INDEX_INTnn,
    VDM_INDEX_INTO,
    VDM_INDEX_IRET,
    VDM_INDEX_NPX,
    VDM_INDEX_INBimm,
    VDM_INDEX_INWimm,
    VDM_INDEX_OUTBimm,
    VDM_INDEX_OUTWimm,
    VDM_INDEX_INB,
    VDM_INDEX_INW,
    VDM_INDEX_OUTB,
    VDM_INDEX_OUTW,
    VDM_INDEX_LOCKPrefix,
    VDM_INDEX_REPNEPrefix,
    VDM_INDEX_REPPrefix,
    VDM_INDEX_CLI,
    VDM_INDEX_STI,
    VDM_INDEX_HLT,
    MAX_VDM_INDEX
} VDM_INDEX, *PVDM_INDEX;

#endif /* _I386_KETYPES_H */
