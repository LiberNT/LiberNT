$if (_WDMDDK_)
/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) kernel definitions, from the WDK 10.0.16299 wdm.h ARM section
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define PASSIVE_LEVEL           0
#define LOW_LEVEL               0
#define APC_LEVEL               1
#define DISPATCH_LEVEL          2
#define CLOCK_LEVEL             13
#define IPI_LEVEL               14
#define DRS_LEVEL               14
#define POWER_LEVEL             14
#define PROFILE_LEVEL           15
#define HIGH_LEVEL              15

extern NTKERNELAPI volatile KSYSTEM_TIME KeTickCount;

#define KI_USER_SHARED_DATA     0xFFFF9000
#define SharedUserData          ((KUSER_SHARED_DATA * const)KI_USER_SHARED_DATA)

#define PAGE_SIZE               0x1000
#define PAGE_SHIFT              12L

typedef struct _KFLOATING_SAVE
{
    ULONG Dummy;
} KFLOATING_SAVE, *PKFLOATING_SAVE;

#define YieldProcessor          __yield

FORCEINLINE
VOID
KeMemoryBarrier(VOID)
{
    _ReadWriteBarrier();
    MemoryBarrier();
}

#define KeMemoryBarrierWithoutFence() _ReadWriteBarrier()

/* The generic timer's virtual count (CNTVCT) is readable at every privilege level. */
FORCEINLINE
ULONG64
ReadTimeStampCounter(VOID)
{
    ULONG Low, High;

    __asm__ __volatile__("isb\n\tmrrc p15, 1, %0, %1, c14" : "=r"(Low), "=r"(High) :: "memory");
    return ((ULONG64)High << 32) | Low;
}

_IRQL_requires_max_(HIGH_LEVEL)
_IRQL_saves_
NTHALAPI
KIRQL
NTAPI
KeGetCurrentIrql(VOID);

_IRQL_requires_max_(HIGH_LEVEL)
NTHALAPI
VOID
FASTCALL
KfLowerIrql(
    _In_ _IRQL_restores_ _Notliteral_ KIRQL NewIrql);
#define KeLowerIrql(a) KfLowerIrql(a)

_IRQL_requires_max_(HIGH_LEVEL)
_IRQL_raises_(NewIrql)
_IRQL_saves_
NTHALAPI
KIRQL
FASTCALL
KfRaiseIrql(
    _In_ KIRQL NewIrql);
#define KeRaiseIrql(a,b) *(b) = KfRaiseIrql(a)

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_saves_
_IRQL_raises_(DISPATCH_LEVEL)
NTKERNELAPI
KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID);

NTKERNELAPI
KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID);

_Requires_lock_not_held_(*SpinLock)
_Acquires_lock_(*SpinLock)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_saves_
_IRQL_raises_(DISPATCH_LEVEL)
NTKERNELAPI
KIRQL
NTAPI
KeAcquireSpinLockRaiseToDpc(
    _Inout_ PKSPIN_LOCK SpinLock);
#define KeAcquireSpinLock(SpinLock, OldIrql) *(OldIrql) = KeAcquireSpinLockRaiseToDpc(SpinLock)

_Requires_lock_held_(*SpinLock)
_Releases_lock_(*SpinLock)
_IRQL_requires_(DISPATCH_LEVEL)
NTKERNELAPI
VOID
NTAPI
KeReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ _IRQL_restores_ KIRQL NewIrql);

_Requires_lock_not_held_(*SpinLock)
_Acquires_lock_(*SpinLock)
_IRQL_requires_min_(DISPATCH_LEVEL)
NTKERNELAPI
VOID
NTAPI
KeAcquireSpinLockAtDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock);

_Requires_lock_held_(*SpinLock)
_Releases_lock_(*SpinLock)
_IRQL_requires_min_(DISPATCH_LEVEL)
NTKERNELAPI
VOID
NTAPI
KeReleaseSpinLockFromDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock);

NTSYSAPI
struct _KTHREAD *
NTAPI
KeGetCurrentThread(VOID);

_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
_When_(return>=0, _Kernel_float_saved_)
FORCEINLINE
NTSTATUS
KeSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatSave)
{
    UNREFERENCED_PARAMETER(FloatSave);
    return STATUS_SUCCESS;
}

_Success_(1)
_IRQL_requires_max_(DISPATCH_LEVEL)
_Kernel_float_restored_
FORCEINLINE
NTSTATUS
KeRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatSave)
{
    UNREFERENCED_PARAMETER(FloatSave);
    return STATUS_SUCCESS;
}

#define KeGetDcacheFillSize() 1L

NTKERNELAPI
VOID
NTAPI
KeFlushIoBuffers(
    _In_ PMDL Mdl,
    _In_ BOOLEAN ReadOperation,
    _In_ BOOLEAN DmaOperation);

#define DbgRaiseAssertionFailure() __emit(0xdefc)

/* KeTickCount is declared NTKERNELAPI here, so its address is the variable itself, not an import slot. */
#define KeQueryTickCount(CurrentCount) { \
    KSYSTEM_TIME volatile *_TickCount = &KeTickCount; \
    for (;;) { \
        (CurrentCount)->HighPart = _TickCount->High1Time; \
        (CurrentCount)->LowPart = _TickCount->LowPart; \
        if ((CurrentCount)->HighPart == _TickCount->High2Time) break; \
        YieldProcessor(); \
    } \
}

typedef union _ARM_IDCODE
{
    ULONG Ulong;
    struct
    {
        ULONG MinorRevision : 4;
        ULONG Model : 12;
        ULONG Architecture : 4;
        ULONG Revision : 4;
        ULONG Implementer : 8;
    };
} ARM_IDCODE, *PARM_IDCODE;

#define CP15_PMSELR     15, 0,  9, 12, 5
#define CP15_PMXEVCNTR  15, 0,  9, 13, 2
#define CP15_TPIDRURW   15, 0, 13,  0, 2
#define CP15_TPIDRURO   15, 0, 13,  0, 3
#define CP15_TPIDRPRW   15, 0, 13,  0, 4
$endif (_WDMDDK_)
$if (_NTDDK_)

#define PAUSE_PROCESSOR __yield();

#ifndef KERNEL_STACK_SCALE
#define KERNEL_STACK_SCALE 1
#endif
#define KERNEL_STACK_SIZE               (0x3000 * KERNEL_STACK_SCALE)
#define KERNEL_LARGE_STACK_SIZE         (0xF000 * KERNEL_STACK_SCALE)
#define KERNEL_LARGE_STACK_COMMIT       KERNEL_STACK_SIZE
#define KERNEL_MCA_EXCEPTION_STACK_SIZE 0x2000

#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

#define CONTEXT_ARM             0x00200000L
#define CONTEXT_CONTROL         (CONTEXT_ARM | 0x1L)
#define CONTEXT_INTEGER         (CONTEXT_ARM | 0x2L)
#define CONTEXT_FLOATING_POINT  (CONTEXT_ARM | 0x4L)
#define CONTEXT_DEBUG_REGISTERS (CONTEXT_ARM | 0x8L)
#define CONTEXT_FULL            (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#define CONTEXT_ALL             (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS)

#define CONTEXT_EXCEPTION_ACTIVE 0x8000000
#define CONTEXT_SERVICE_ACTIVE 0x10000000
#define CONTEXT_EXCEPTION_REQUEST 0x40000000
#define CONTEXT_EXCEPTION_REPORTING 0x80000000
#define CONTEXT_UNWOUND_TO_CALL     0x20000000

#define ARM_MAX_BREAKPOINTS 8
#define ARM_MAX_WATCHPOINTS 1

typedef struct _NEON128
{
    ULONGLONG Low;
    LONGLONG High;
} NEON128, *PNEON128;

typedef struct DECLSPEC_ALIGN(8) _CONTEXT
{
    ULONG ContextFlags;
    ULONG R0;
    ULONG R1;
    ULONG R2;
    ULONG R3;
    ULONG R4;
    ULONG R5;
    ULONG R6;
    ULONG R7;
    ULONG R8;
    ULONG R9;
    ULONG R10;
    ULONG R11;
    ULONG R12;
    ULONG Sp;
    ULONG Lr;
    ULONG Pc;
    ULONG Cpsr;
    ULONG Fpscr;
    ULONG Padding;
    union
    {
        NEON128 Q[16];
        ULONGLONG D[32];
        ULONG S[32];
    } DUMMYUNIONNAME;
    ULONG Bvr[ARM_MAX_BREAKPOINTS];
    ULONG Bcr[ARM_MAX_BREAKPOINTS];
    ULONG Wvr[ARM_MAX_WATCHPOINTS];
    ULONG Wcr[ARM_MAX_WATCHPOINTS];
    ULONG Padding2[2];
} CONTEXT;

#define PCR_MINOR_VERSION 1
#define PCR_MAJOR_VERSION 1

typedef struct _KPCR
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
            PKSPIN_LOCK_QUEUE LockArray;
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
} KPCR, *PKPCR;

#define CP15_PCR_RESERVED_MASK 0xFFF
#define KIPCR() (((ULONG_PTR)_MoveFromCoprocessor(CP15_TPIDRPRW)) & ~CP15_PCR_RESERVED_MASK)

FORCEINLINE
PKPCR
KeGetPcr(VOID)
{
    return (PKPCR)KIPCR();
}

/* The processor index is Prcb.Number, at PCR + 0x594 (WDK 16299 KeGetCurrentProcessorIndex). */
FORCEINLINE
ULONG
KeGetCurrentProcessorIndex(VOID)
{
    return *(PULONG)(KIPCR() + 0x594);
}

#if (NTDDI_VERSION < NTDDI_WIN7) || !defined(NT_PROCESSOR_GROUPS)
FORCEINLINE
ULONG
KeGetCurrentProcessorNumber(VOID)
{
    return *(PULONG)(KIPCR() + 0x594);
}
#endif

typedef struct _KEXCEPTION_FRAME
{
    ULONG Param5;
    ULONG TrapFrame;
    ULONG OutputBuffer;
    ULONG OutputLength;
    ULONG Pad;
    ULONG R4;
    ULONG R5;
    ULONG R6;
    ULONG R7;
    ULONG R8;
    ULONG R9;
    ULONG R10;
    ULONG R11;
    ULONG Return;
} KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;

typedef struct _KARM_VFP_STATE
{
    struct _KARM_VFP_STATE *Link;
    ULONG Fpscr;
    ULONG Reserved;
    ULONG Reserved2;
    ULONGLONG VfpD[32];
} KARM_VFP_STATE, *PKARM_VFP_STATE;

#define KTRAP_FRAME_ARGUMENTS (14 * 4)

typedef struct _KTRAP_FRAME
{
    ULONG Arg3;
    ULONG FaultStatus;
    union
    {
        ULONG FaultAddress;
        ULONG TrapFrame;
    };
    ULONG Reserved;
    UCHAR ExceptionActive;
    UCHAR ContextFromKFramesUnwound;
    UCHAR DebugRegistersValid;
    union
    {
        KPROCESSOR_MODE PreviousMode;
        KIRQL PreviousIrql;
    };
    PKARM_VFP_STATE VfpState;
    ULONG Bvr[ARM_MAX_BREAKPOINTS];
    ULONG Bcr[ARM_MAX_BREAKPOINTS];
    ULONG Wvr[ARM_MAX_WATCHPOINTS];
    ULONG Wcr[ARM_MAX_WATCHPOINTS];
    ULONG R0;
    ULONG R1;
    ULONG R2;
    ULONG R3;
    ULONG R12;
    ULONG Sp;
    ULONG Lr;
    ULONG R11;
    ULONG Pc;
    ULONG Cpsr;
} KTRAP_FRAME, *PKTRAP_FRAME;
$endif (_NTDDK_)
