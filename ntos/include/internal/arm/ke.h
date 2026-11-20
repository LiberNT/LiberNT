/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 kernel-private interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <internal/arm/intrin_i.h>

#define KI_EMULATED_USER_MACHINE 0
#define KI_EMPTY_YIELD_STALL_NS 700
#define KiEmulationProcessorArchitecture(Process) KeProcessorArchitecture

#define KI_USER_MODE_CALLBACK_ENTRY KiArmCallUserMode
#define KiArchCallUserMode(Frame, Result, ResultLength) KiArmCallUserMode((Frame), (Result), (ResultLength))
#define KiArchIdleLoopPollsForWork() FALSE
#define KiArchSystemDllSListMarkersPresent(End, Fault, Resume) \
    ((VOID)(End), (VOID)(Fault), (VOID)(Resume), TRUE)

/* The switch backend completes the outgoing-thread handoff on the new stack. */
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 1

#undef SYNCH_LEVEL
#define SYNCH_LEVEL 12

/* CPSR fields (ARM ARM B1.3.3). */
#define CPSR_MODE_MASK   0x1F
#define CPSR_MODE_USER   0x10
#define CPSR_MODE_FIQ    0x11
#define CPSR_MODE_IRQ    0x12
#define CPSR_MODE_SVC    0x13
#define CPSR_MODE_ABORT  0x17
#define CPSR_MODE_UNDEF  0x1B
#define CPSR_MODE_SYSTEM 0x1F
#define CPSR_THUMB       0x00000020
#define CPSR_FIQ_DISABLE 0x00000040
#define CPSR_IRQ_DISABLE 0x00000080
#define CPSR_ASYNC_ABORT 0x00000100
#define CPSR_ENDIAN_BIG  0x00000200
#define CPSR_IT_MASK     0x0600FC00
#define CPSR_GE_MASK     0x000F0000
#define CPSR_FLAGS_MASK  0xF8000000

/* Bits a user context may set: flags, GE, IT, Thumb. Mode is always user. */
#define CPSR_USER_SETTABLE (CPSR_FLAGS_MASK | CPSR_GE_MASK | CPSR_IT_MASK | CPSR_THUMB)

/* Private Prcb->FeatureBits flags; no NT feature bit names exist for ARM. */
#define KI_ARM_FEATURE_DIVIDE 0x00000001

/* Every trap saves the full VFPv3-D32 state below its trap frame. */
#define KI_ARM_VFP_REGISTERS 32
#define FPEXC_EN 0x40000000

#ifdef __cplusplus
extern "C" {
#endif

/* Trap frames are always preceded by an exception frame with R4-R10. */
typedef struct _KI_ARM_TRAP_RECORD
{
    KARM_VFP_STATE VfpState;
    KEXCEPTION_FRAME ExceptionFrame;
    KTRAP_FRAME TrapFrame;
} KI_ARM_TRAP_RECORD, *PKI_ARM_TRAP_RECORD;

C_ASSERT((sizeof(KI_ARM_TRAP_RECORD) % 8) == 0);

#define KI_ARM_TRAP_RECORD(Frame) CONTAINING_RECORD((Frame), KI_ARM_TRAP_RECORD, TrapFrame)

NTSTATUS NTAPI KiArmCallUserMode(_In_ PKTRAP_FRAME Frame, _Out_ PVOID *OutputBuffer, _Out_ PULONG OutputLength);
DECLSPEC_NORETURN VOID NTAPI KiArmSystemStartup(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID NTAPI KiArmConsoleInitialize(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
BOOLEAN NTAPI KiArmConsoleReady(VOID);
VOID NTAPI KiArmConsolePutByte(_In_ UCHAR Byte);
VOID NTAPI KiArmConsoleWrite(_In_reads_bytes_(Length) PCCH Buffer, _In_ SIZE_T Length);
BOOLEAN NTAPI KiArmConsoleGetByte(_Out_ PUCHAR Byte);
ULONG NTAPI KiArmConsoleInterface(VOID);
VOID NTAPI KiArmIdentifyProcessor(_Inout_ PKPRCB Prcb);
VOID NTAPI KiArmInitializeVectors(VOID);
VOID NTAPI KiArmInitializePcr(_Out_ PKIPCR Pcr, _In_ PKTHREAD Thread, _In_ ULONG Number, _In_ PVOID DpcStack, _In_ PVOID PanicStack);
VOID NTAPI KiArmRequestSoftwareInterrupt(_In_ KIRQL Irql);
VOID NTAPI KiArmClearSoftwareInterrupt(_In_ KIRQL Irql);
VOID NTAPI KiArmDispatchSoftwareInterrupts(_In_opt_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiArmTrapDispatch(_Inout_ PKTRAP_FRAME TrapFrame, _In_ ULONG Vector);
VOID NTAPI KiArmInterruptDispatch(_Inout_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiArmSystemServiceDispatch(_Inout_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiArmUnimplemented(_In_ const CHAR *Routine);
DECLSPEC_NORETURN VOID NTAPI KiArmRestoreTrapFrame(_In_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiArmServiceExit(_In_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID KiExceptionExit(_In_ PKTRAP_FRAME TrapFrame, _In_ PKEXCEPTION_FRAME ExceptionFrame);
DECLSPEC_NORETURN VOID NTAPI KiThreadStartup(VOID);
NTSTATUS NTAPI KiArmCopyFromUser(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiArmCopyToUser(PVOID Destination, const VOID *Source, SIZE_T Length);
ULONG NTAPI KeGetCurrentProcessorNumber(VOID);
PKTRAP_FRAME NTAPI KeGetTrapFrame(_In_ PKTHREAD Thread);
PKEXCEPTION_FRAME NTAPI KeGetExceptionFrame(_In_ PKTHREAD Thread);
ULONG_PTR NTAPI KeGetTrapFramePc(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KeGetTrapFrameInterruptState(_In_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiRundownThread(_In_ PKTHREAD Thread);
BOOLEAN NTAPI KiSwapContextResume(_In_ BOOLEAN ApcBypass, _In_ PKTHREAD OldThread, _In_ PKTHREAD NewThread);
VOID NTAPI KiRetireDpcListInDpcStack(_In_ PKPRCB Prcb, _In_ PVOID DpcStack);
VOID NTAPI KeFlushProcessTb(VOID);
VOID NTAPI KeSweepICache(_In_opt_ PVOID BaseAddress, _In_ SIZE_T FlushSize);
VOID NTAPI KiArmSaveVfpState(_Out_ PKARM_VFP_STATE State);
VOID NTAPI KiArmRestoreVfpState(_In_ PKARM_VFP_STATE State);
BOOLEAN KiProcessorFreezeHandler(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame);
VOID NTAPI KiFreezeIfRequested(VOID);
VOID NTAPI KiIpiProcessRequests(VOID);

VOID FASTCALL KeUpdateSystemTime(_In_opt_ PKTRAP_FRAME TrapFrame, _In_ ULONG Increment, _In_ KIRQL OldIrql);

/* The ARMv7 HAL is a forwarder: the platform devices are driven here. */
VOID NTAPI KiArmInitializePlatform(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID NTAPI KiArmSetPriorityMask(_In_ KIRQL Irql);
BOOLEAN NTAPI KiArmDispatchDeviceInterrupt(_Inout_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KiArmPlatformInitialized(VOID);
BOOLEAN NTAPI KiArmPsciConduitIsSmc(VOID);
BOOLEAN NTAPI KiArmFindDevice(_In_ const CHAR * const *Compatible, _In_ ULONG RegIndex, _Out_ PPHYSICAL_ADDRESS Address, _Out_opt_ PULONG Length);
BOOLEAN NTAPI KiArmEnableInterruptLine(_In_ ULONG Vector, _In_ KIRQL Irql, _In_ KINTERRUPT_MODE Mode);
VOID NTAPI KiArmDisableInterruptLine(_In_ ULONG Vector);
VOID NTAPI KiArmServiceInterruptVector(_In_ ULONG Vector, _In_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiArmSendInterprocessorInterrupt(_In_ ULONG TargetMask, _In_ ULONG Sgi);
VOID NTAPI KiArmFlushDmaRange(_In_ PVOID Address, _In_ SIZE_T Length, _In_ BOOLEAN Invalidate);
VOID NTAPI KiArmCallOnStack(_In_opt_ PVOID Argument0, _In_opt_ PVOID Argument1, _In_ PVOID Routine, _In_ PVOID StackTop);
BOOLEAN NTAPI KiArmSwapStack(_Out_ PVOID *OldStack, _In_ PVOID NewStack, _In_ PVOID Context, _In_ PVOID Resume);
DECLSPEC_NORETURN VOID NTAPI KiArmStartUserThread(VOID);
VOID NTAPI KiArmExitToUserMode(_Inout_ PKTRAP_FRAME TrapFrame);

#ifdef __cplusplus
}
#endif

/* The trap code reports a UDF breakpoint at the instruction itself. */
#define KI_BREAKPOINT_PC_ADJUST 0

FORCEINLINE
PULONG_PTR
KiGetUserModeStackAddress(VOID)
{
    return (PULONG_PTR)&KeGetCurrentThread()->TrapFrame->Sp;
}

#define KiIsUserModeTrap(TrapFrame) (((TrapFrame)->Cpsr & CPSR_MODE_MASK) == CPSR_MODE_USER)

/* User-mode SList pops have no 64-bit compare-exchange fault rollback in ntdll yet. */
#define KI_USER_SLIST_POP_NO_ROLLBACK

#define KeGetContextSwitches(Prcb) ((Prcb)->KeContextSwitches)
#define KiQueryEffectiveProcessorMhz(Number) (KiProcessorBlock[(Number)]->MHz)
#define Ki386PerfEnd()

FORCEINLINE
ULONG64
KiReadDirectoryTableBase(VOID)
{
    return KiArmReadCp15_64(0, 2) & 0x000000FFFFFFF000ULL;
}

#define KiGetContextPreviousMode(Context) \
    ((((Context)->Cpsr & CPSR_MODE_MASK) == CPSR_MODE_USER) ? UserMode : KernelMode)

/* UDF #0xFE in Thumb-2 (16-bit); the debugger service breakpoint uses the same encoding. */
#define KD_BREAKPOINT_TYPE        USHORT
#define KD_BREAKPOINT_SIZE        sizeof(USHORT)
#define KD_BREAKPOINT_VALUE       0xDEFE
#define KD_ASSERT_BREAKPOINT_SIZE KD_BREAKPOINT_SIZE

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG Cpsr;

    __asm__ __volatile__("mrs %0, cpsr\n\tcpsid i" : "=r"(Cpsr) :: "memory");
    return (Cpsr & CPSR_IRQ_DISABLE) == 0;
}

FORCEINLINE
VOID
KeRestoreInterrupts(BOOLEAN WereEnabled)
{
    if (WereEnabled)
        _enable();
    else
        _disable();
}

FORCEINLINE
VOID
KeInvalidateTlbEntry(_In_ PVOID Address)
{
    /* TLBIMVAAIS: every ASID, inner shareable; a hierarchy change needs a full flush. */
    KiArmDsbIsh();
    KiArmWriteCp15(8, 0, 3, 3, (ULONG_PTR)Address & ~(PAGE_SIZE - 1));
    KiArmDsbIsh();
    KiArmIsb();
}

FORCEINLINE ULONG_PTR KeGetContextPc(_In_ PCONTEXT Context) { return Context->Pc; }
FORCEINLINE VOID KeSetContextPc(_Inout_ PCONTEXT Context, _In_ ULONG_PTR Pc) { Context->Pc = Pc; }
FORCEINLINE ULONG_PTR KeGetContextReturnRegister(_In_ PCONTEXT Context) { return Context->R0; }
FORCEINLINE VOID KeSetContextReturnRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR Value) { Context->R0 = Value; }
FORCEINLINE ULONG_PTR KeGetContextServiceArgument3(_In_ PCONTEXT Context) { return Context->R3; }
FORCEINLINE ULONG_PTR KeGetContextServiceArgument4(_In_ PCONTEXT Context) { return Context->R12; }
FORCEINLINE ULONG_PTR KeGetContextStackRegister(_In_ PCONTEXT Context) { return Context->Sp; }
FORCEINLINE ULONG_PTR KeGetContextFrameRegister(_In_ PCONTEXT Context) { return Context->R11; }
FORCEINLINE VOID KeSetContextFrameRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR Frame) { Context->R11 = Frame; }
FORCEINLINE ULONG_PTR KeGetTrapFrameStackRegister(_In_ PKTRAP_FRAME TrapFrame) { return TrapFrame->Sp; }
FORCEINLINE ULONG_PTR KeGetTrapFrameFrameRegister(_In_ PKTRAP_FRAME TrapFrame) { return TrapFrame->R11; }

FORCEINLINE
PKTRAP_FRAME
KiGetLinkedTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return (PKTRAP_FRAME)TrapFrame->TrapFrame;
}

/* The exception frame of a service always sits below its trap frame. */
typedef struct _KI_SERVICE_EXCEPTION_STATE
{
    UCHAR Reserved;
} KI_SERVICE_EXCEPTION_STATE, *PKI_SERVICE_EXCEPTION_STATE;

FORCEINLINE
PKEXCEPTION_FRAME
KiEnterServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(State);
    return ExceptionFrame ? ExceptionFrame : &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame;
}

FORCEINLINE
VOID
KiAbortServiceException(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(State);
}

FORCEINLINE
VOID
KiLeaveServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);
}

#define KI_DPC_NORMAL_PROCESSING_REQUESTED 0x00000002L
#define KI_DPC_NORMAL_DPC_PRESENT          0x00000010L
#define KI_DPC_THREAD_ACTIVE               0x00010000L
#define KI_DPC_THREAD_REQUESTED            0x00020000L

#define KI_ARCH_DPC_REQUEST_STATE

FORCEINLINE BOOLEAN KiIsDpcInterruptRequested(_In_ PKPRCB Prcb) { return (Prcb->DpcRequestSummary & KI_DPC_NORMAL_PROCESSING_REQUESTED) != 0; }
FORCEINLINE VOID KiSetDpcInterruptRequested(_Inout_ PKPRCB Prcb) { InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_NORMAL_PROCESSING_REQUESTED); }
FORCEINLINE VOID KiClearDpcInterruptRequested(_Inout_ PKPRCB Prcb) { InterlockedAnd(&Prcb->DpcRequestSummary, ~KI_DPC_NORMAL_PROCESSING_REQUESTED); }
FORCEINLINE VOID KiSetDpcPresent(_Inout_ PKPRCB Prcb) { InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_NORMAL_DPC_PRESENT); }
FORCEINLINE VOID KiClearDpcRequestState(_Inout_ PKPRCB Prcb) { InterlockedAnd(&Prcb->DpcRequestSummary, ~(KI_DPC_NORMAL_PROCESSING_REQUESTED | KI_DPC_NORMAL_DPC_PRESENT)); }
FORCEINLINE BOOLEAN KiIsDpcThreadActive(_In_ PKPRCB Prcb) { return (Prcb->DpcRequestSummary & KI_DPC_THREAD_ACTIVE) != 0; }
FORCEINLINE BOOLEAN KiIsDpcThreadRequested(_In_ PKPRCB Prcb) { return (Prcb->DpcRequestSummary & KI_DPC_THREAD_REQUESTED) != 0; }
FORCEINLINE VOID KiSetDpcThreadActive(_Inout_ PKPRCB Prcb) { InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_THREAD_ACTIVE); }
FORCEINLINE VOID KiSetDpcThreadRequested(_Inout_ PKPRCB Prcb) { InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_THREAD_REQUESTED); }
FORCEINLINE VOID KiClearDpcThreadState(_Inout_ PKPRCB Prcb) { InterlockedAnd(&Prcb->DpcRequestSummary, ~(KI_DPC_THREAD_ACTIVE | KI_DPC_THREAD_REQUESTED)); }

#define KiPrcbDpcTimeCount(Prcb) ((Prcb)->DpcTimeCount)
#define KiPrcbDpcTimeLimit(Prcb) ((Prcb)->DpcTimeLimit)
#define KiPrcbDpcWatchdogCount(Prcb) ((Prcb)->DpcWatchdogCount)
#define KiPrcbDpcWatchdogPeriod(Prcb) ((Prcb)->DpcWatchdogPeriod)
extern PCONTEXT KiProcessorContext[MAXIMUM_PROCESSORS];
#define KiPrcbContext(Prcb) (KiProcessorContext[(Prcb)->Number])
/* TODO: the NT10 ARM processor block has no type or vendor string fields */
#define KI_PRCB_CPU_TYPE_OFFSET 0
#define KI_PRCB_VENDOR_STRING_OFFSET 0
#define KI_PRCB_CONTEXT_OFFSET FIELD_OFFSET(KPRCB, Context)
#define KiClockTickSuspended(Prcb) FALSE
#define KiClockTimerInserted(DueTime) ((VOID)(DueTime))
#define KiRefreshProcessorRunTime(Prcb) ((VOID)(Prcb))
#define KiQueryClockTickBaseline(Counter, Remainder) (*(Counter) = KeQueryPerformanceCounter(NULL), *(Remainder) = 0)
