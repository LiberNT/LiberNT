/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 kernel layer: registers, frames, interrupt vectors and processor setup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KI_EMULATED_USER_MACHINE 0
/* Windows 11 x64 spends 65-79 ns on a yield with nothing to run, no more than the system call */
#define KI_EMPTY_YIELD_STALL_NS 0
#define KiEmulationProcessorArchitecture(Process) PROCESSOR_ARCHITECTURE_INTEL
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 1

#define X86_FEATURE_PAE 0x00000040
#define X86_FEATURE_MCE 0x00000080
#define X86_FEATURE_HT  0x10000000

#define X86_MSR_GSBASE        0xC0000101
#define X86_MSR_KERNEL_GSBASE 0xC0000102
#define X86_MSR_STAR          0xC0000081
#define X86_MSR_LSTAR         0xC0000082
#define X86_MSR_CSTAR         0xC0000083
#define X86_MSR_SFMASK        0xC0000084

#define AMD64_TSS 9

#define APIC_EOI_REGISTER 0xFFFFFFFFFFFE00B0ULL
#define X2APIC_BASE_MSR   0x0000001B
#define X2APIC_EOI_MSR    0x0000080B
#define X2APIC_EXTENDED   0x0000000000000400ULL

#define HV_X64_MSR_EOI                0x40000070
#define HV_CPUID_INTERFACE_HV1        0x31237648
#define HV_ACCESS_INTR_CTRL_REGS      0x00000010
#define HV_APIC_ACCESS_RECOMMENDED    0x00000008
#define HV_RELAXED_TIMING_RECOMMENDED 0x00000020
#define HV_X2APIC_MSRS_RECOMMENDED    0x00000100

#define KD_BREAKPOINT_TYPE        UCHAR
#define KD_BREAKPOINT_SIZE        sizeof(UCHAR)
#define KD_BREAKPOINT_VALUE       0xCC

extern BOOLEAN KiHvApicMsrs;
extern BOOLEAN KiX2ApicMsrs;
extern SIZE_T KeXStateLength;
extern ULONG KiMxCsrMask;
extern ULONG KeI386NpxPresent;
extern ULONG KeI386CpuType;
extern ULONG KeI386CpuStep;
extern NTKERNELAPI volatile KSYSTEM_TIME KeTickCount;

#include "intrin_i.h"

typedef struct _KIDT_INIT
{
    UCHAR InterruptId;
    UCHAR Dpl;
    UCHAR IstIndex;
    PVOID ServiceRoutine;
} KIDT_INIT, *PKIDT_INIT;

#pragma pack(push, 1)
typedef struct _KI_INTERRUPT_DISPATCH_ENTRY
{
    UCHAR _Op_nop;
    UCHAR _Op_push;
    UCHAR _Vector;
    UCHAR _Op_jmp;
    ULONG RelativeAddress;
} KI_INTERRUPT_DISPATCH_ENTRY, *PKI_INTERRUPT_DISPATCH_ENTRY;
#pragma pack(pop)

typedef struct _KI_SERVICE_EXCEPTION_STATE
{
    KEXCEPTION_FRAME ExceptionFrame;
} KI_SERVICE_EXCEPTION_STATE, *PKI_SERVICE_EXCEPTION_STATE;

#define KeGetTrapFrame(Thread) \
    ((PKTRAP_FRAME)((ULONG_PTR)(Thread)->InitialStack - sizeof(KTRAP_FRAME)))
#define KeGetExceptionFrame(Thread) \
    ((PKEXCEPTION_FRAME)((ULONG_PTR)KeGetTrapFrame(Thread) - sizeof(KEXCEPTION_FRAME)))
#define KeGetContextSwitches(Prcb) ((Prcb)->KeContextSwitches)
#define KiPrcbTimerTableLockQueue(Prcb, Index) (&(Prcb)->TimerTableLockQueue[(Index)])

/* Windows declares RequestMailbox[1] last in the PRCB and keeps the other mailboxes right after the PCR */
#define KI_EXTRA_REQUEST_MAILBOXES KE_MAXIMUM_GROUPS
C_ASSERT(sizeof(KIPCR) == FIELD_OFFSET(KIPCR, Prcb.RequestMailbox) + sizeof(REQUEST_MAILBOX));

FORCEINLINE
ULONG_PTR
KeGetContextPc(
    _In_ PCONTEXT Context)
{
    return Context->Rip;
}

FORCEINLINE
VOID
KeSetContextPc(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR ProgramCounter)
{
    Context->Rip = ProgramCounter;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR ReturnValue)
{
    Context->Rax = ReturnValue;
}

FORCEINLINE
ULONG_PTR
KeGetContextServiceArgument3(
    _In_ PCONTEXT Context)
{
    return Context->R8;
}

FORCEINLINE
ULONG_PTR
KeGetContextServiceArgument4(
    _In_ PCONTEXT Context)
{
    return Context->R9;
}

FORCEINLINE
ULONG_PTR
KeGetContextStackRegister(
    _In_ PCONTEXT Context)
{
    return Context->Rsp;
}

FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(
    _In_ PCONTEXT Context)
{
    return Context->Rbp;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR Frame)
{
    Context->Rbp = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFramePc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Rip;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Rsp;
}

FORCEINLINE
PULONG_PTR
KiGetUserModeStackAddress(VOID)
{
    return &PsGetCurrentThread()->Tcb.TrapFrame->Rsp;
}

FORCEINLINE
BOOLEAN
KiUserTrap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return (BOOLEAN)((TrapFrame->SegCs & MODE_MASK) != 0);
}

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
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);

    if (ExceptionFrame != NULL)
        return ExceptionFrame;

    /* FIXME: nonvolatiles a partial context leaves out are zeroed, not kept */
    RtlZeroMemory(&State->ExceptionFrame, sizeof(State->ExceptionFrame));
    return &State->ExceptionFrame;
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
    _Inout_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    Thread->TrapFrame = (PKTRAP_FRAME)TrapFrame->TrapFrame;
}

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    BOOLEAN WereEnabled = (BOOLEAN)((__readeflags() & EFLAGS_INTERRUPT_MASK) != 0);

    _disable();
    return WereEnabled;
}

FORCEINLINE
VOID
KeRestoreInterrupts(
    _In_ BOOLEAN WereEnabled)
{
    if (WereEnabled)
        _enable();
}

FORCEINLINE
VOID
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    /* Instruction fetch snoops stores on x86; nothing needs writing back */
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(FlushSize);
}

FORCEINLINE
PKIDTENTRY64
KiIdtEntryForVector(
    _In_ ULONG Vector)
{
    return &KeGetPcr()->IdtBase[HalVectorToIDTEntry(Vector)];
}

FORCEINLINE
VOID
KeRegisterInterruptHandler(
    _In_ ULONG Vector,
    _In_ PVOID Handler)
{
    PKIDTENTRY64 Idt = KiIdtEntryForVector(Vector);
    ULONG64 Address = (ULONG64)(ULONG_PTR)Handler;

    Idt->OffsetLow = (USHORT)Address;
    Idt->OffsetMiddle = (USHORT)(Address >> 16);
    Idt->OffsetHigh = (ULONG)(Address >> 32);
    Idt->Selector = KGDT64_R0_CODE;
    Idt->IstIndex = 0;
    Idt->Type = 0x0E;
    Idt->Dpl = 0;
    Idt->Present = 1;
    Idt->Reserved0 = 0;
    Idt->Reserved1 = 0;
}

FORCEINLINE
PVOID
KeQueryInterruptHandler(
    _In_ ULONG Vector)
{
    PKIDTENTRY64 Idt = KiIdtEntryForVector(Vector);

    return (PVOID)(ULONG_PTR)(((ULONG64)Idt->OffsetHigh << 32) |
                              ((ULONG64)Idt->OffsetMiddle << 16) |
                              (ULONG64)Idt->OffsetLow);
}

FORCEINLINE
VOID
KiSendEOI(VOID)
{
    if (KiHvApicMsrs)
        __writemsr(HV_X64_MSR_EOI, 0);
    else if (KiX2ApicMsrs)
        __writemsr(X2APIC_EOI_MSR, 0);
    else
        *(volatile ULONG *)APIC_EOI_REGISTER = 0;
}

FORCEINLINE
VOID
KiEndInterrupt(
    _In_ KIRQL Irql,
    _In_ PKTRAP_FRAME TrapFrame)
{
    ASSERT(TrapFrame->ErrorCode == 0xC10C4);

    _disable();
    HalEndSystemInterrupt(Irql, TrapFrame);
}

#define KiPrcbDpcTimeCount(Prcb) ((Prcb)->DpcTimeCount)
#define KiPrcbDpcTimeLimit(Prcb) ((Prcb)->DpcTimeLimit)
#define KiPrcbDpcWatchdogCount(Prcb) ((Prcb)->DpcWatchdogCount)
#define KiPrcbDpcWatchdogPeriod(Prcb) ((Prcb)->DpcWatchdogPeriod)
#define KiPrcbContext(Prcb) ((Prcb)->Context)
#define KI_PRCB_CPU_TYPE_OFFSET FIELD_OFFSET(KPRCB, CpuType)
#define KI_PRCB_VENDOR_STRING_OFFSET FIELD_OFFSET(KPRCB, VendorString)
#define KI_PRCB_CONTEXT_OFFSET FIELD_OFFSET(KPRCB, Context)
#define KiClockTickSuspended(Prcb) FALSE
#define KiClockTimerInserted(DueTime) ((VOID)(DueTime))
#define KiRefreshProcessorRunTime(Prcb) ((VOID)(Prcb))
#define KiQueryClockTickBaseline(Counter, Remainder) (*(Counter) = KeQueryPerformanceCounter(NULL), *(Remainder) = 0)
#define KiQueryEffectiveProcessorMhz(Number) KiAmd64QueryEffectiveMHz(Number)

DECLSPEC_NORETURN VOID KiSwitchToBootStack(_In_ ULONG_PTR InitialStack);

VOID KiDivideErrorFault(VOID);
VOID KiDebugTrapOrFault(VOID);
VOID KiNmiInterrupt(VOID);
VOID KiBreakpointTrap(VOID);
VOID KiOverflowTrap(VOID);
VOID KiBoundFault(VOID);
VOID KiInvalidOpcodeFault(VOID);
VOID KiNpxNotAvailableFault(VOID);
VOID KiDoubleFaultAbort(VOID);
VOID KiNpxSegmentOverrunAbort(VOID);
VOID KiInvalidTssFault(VOID);
VOID KiSegmentNotPresentFault(VOID);
VOID KiStackFault(VOID);
VOID KiGeneralProtectionFault(VOID);
VOID KiPageFault(VOID);
VOID KiFloatingErrorFault(VOID);
VOID KiAlignmentFault(VOID);
VOID KiMcheckAbort(VOID);
VOID KiXmmException(VOID);
VOID KiApcInterrupt(VOID);
VOID KiRaiseSecurityCheckFailure(VOID);
VOID KiRaiseAssertion(VOID);
VOID KiDebugServiceTrap(VOID);
VOID KiDpcInterrupt(VOID);
VOID KiIpiInterrupt(VOID);

DECLSPEC_NORETURN
VOID
NTAPI
KiRaiseSecurityCheckFailureHandler(
    _In_ PKTRAP_FRAME TrapFrame);

VOID KiGetCacheInformation(VOID);
VOID KiSetProcessorType(VOID);
ULONG64 KiGetFeatureBits(VOID);
VOID KiReportCpuFeatures(_In_ PKPRCB Prcb);

ULONG
NTAPI
KiAmd64QueryEffectiveMHz(
    _In_ ULONG ProcessorNumber);

VOID MiAmd64FlushTargets(_In_ ULONG64 Targets, _In_ ULONG64 Address, _In_ ULONG64 Pages);
VOID MiAmd64ProcessTlbRequest(_In_ ULONG Source);
BOOLEAN MiAmd64PrepareProcessorTlb(_In_ ULONG Cpu);
VOID KiSendMemoryIpi(_In_ ULONG Word, _In_ ULONG64 Targets);

NTHALAPI ULONG NTAPI HalQueryMaximumProcessorCount(VOID);
NTHALAPI VOID NTAPI HalpRequestIpi(_In_ ULONG Word, _In_ KAFFINITY TargetSet);
NTHALAPI VOID NTAPI HalpSendNMI(_In_ ULONG Word, _In_ KAFFINITY TargetSet);
NTHALAPI VOID NTAPI HalpSendSoftwareInterrupt(_In_ ULONG Word, _In_ KAFFINITY TargetSet, _In_ KIRQL Irql);

#define KiArchMaximumProcessors() KE_MAXIMUM_PROCESSORS

VOID
FASTCALL
KiIpiSendProcessor(
    _In_ ULONG Number,
    _In_ ULONG IpiRequest);
#define KiIpiSendProcessor KiIpiSendProcessor
ULONG64 KiQueryFreezeEpoch(VOID);
BOOLEAN KiIsFreezeActive(VOID);

VOID
NTAPI
KiIpiProcessRequests(VOID);

VOID
KiThreadStartup(
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext,
    _In_ BOOLEAN UserThread,
    _In_ KTRAP_FRAME TrapFrame);

NTHALAPI
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine);

VOID
KiGetTrapContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _Out_ PCONTEXT Context);

VOID
KiSetTrapContext(
    _Out_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE RequestorMode);

DECLSPEC_NORETURN
VOID
KiUserCallbackExit(
    _In_ PKTRAP_FRAME TrapFrame);

DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
KiInstrumentTrapFrame(
    _Inout_ PKTRAP_FRAME TrapFrame);

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
KiFreezeIfRequested(VOID);

CODE_SEG("INIT")
VOID
NTAPI
KiPublishActiveProcessor(
    _In_ PKPRCB Prcb);

VOID
NTAPI
KiInitializeXStateConfiguration(
    _In_ ULONG Processor);

ULONG64
NTAPI
KiGetUserXStateFeatures(VOID);

ULONG
NTAPI
KiGetUserXStateLength(
    _In_ ULONG64 Layout,
    _In_ ULONG64 Features);

ULONG
NTAPI
KiGetUserXStateMaximumLength(VOID);

ULONG
NTAPI
KiGetUserXStateScratchSize(VOID);

ULONG64
NTAPI
KiGetUserXStateSetFeatures(
    _In_ PXSAVE_AREA_HEADER Header);

NTSTATUS
NTAPI
KiSaveUserXState(
    _Inout_updates_bytes_(Length) PUCHAR XState,
    _In_ ULONG Length,
    _Out_ PVOID Scratch);

VOID
NTAPI
KiRestoreUserXState(
    _In_ PUCHAR XState,
    _Out_ PVOID Scratch);

VOID
NTAPI
KiCaptureUserLegacyFloatingState(
    _Out_ PXSAVE_FORMAT FltSave);

VOID
NTAPI
KiRestoreUserLegacyFloatingState(
    _In_ PXSAVE_FORMAT FltSave);

VOID
NTAPI
KiInitializeMxCsrMask(VOID);

VOID
NTAPI
KiAmd64BootMark(
    _In_ ULONG Mark);

VOID
NTAPI
KiAmd64BootMarkStop(VOID);

VOID
NTAPI
KiAmd64BootReadout(
    _In_ ULONG Source,
    _In_ ULONG64 Code,
    _In_ ULONG64 A,
    _In_ ULONG64 B,
    _In_ ULONG64 C,
    _In_ ULONG64 D);

#define KiArchBootMark(Mark) KiAmd64BootMark(Mark)
#define KiArchBootReadout(Source, Code, A, B, C, D) KiAmd64BootReadout(Source, Code, A, B, C, D)

/* int3 traps with RIP past the one-byte instruction */
#define KI_BREAKPOINT_PC_ADJUST 1

NTSTATUS
KiAmd64DebugPortExceptionCode(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ NTSTATUS ExceptionCode);
#define KiArchDebugPortExceptionCode(TrapFrame, Code) KiAmd64DebugPortExceptionCode(TrapFrame, Code)

VOID
KiAmd64ReportUnhandledKernelFault(
    _In_ PKTRAP_FRAME TrapFrame);
#define KiArchReportUnhandledKernelFault(TrapFrame) KiAmd64ReportUnhandledKernelFault(TrapFrame)

VOID
KiAmd64PrepareUserBreakpoint(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PKTRAP_FRAME TrapFrame);
#define KiArchPrepareUserBreakpoint(ExceptionRecord, TrapFrame) KiAmd64PrepareUserBreakpoint(ExceptionRecord, TrapFrame)

VOID
KiAmd64PrepareInterruptDispatch(
    _Inout_ PKINTERRUPT Interrupt);
#define KiArchPrepareInterruptDispatch(Interrupt) KiAmd64PrepareInterruptDispatch(Interrupt)

NTSTATUS
NTAPI
KiAmd64ContinueExtendedState(
    _In_ PCONTEXT SourceContext,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode);
#define KiArchContinueExtendedState(SourceContext, ContextFlags, PreviousMode) KiAmd64ContinueExtendedState(SourceContext, ContextFlags, PreviousMode)

NTSTATUS
NTAPI
PspAmd64CaptureXStateContext(
    _In_ PCONTEXT Context,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SetContext,
    _Out_ PVOID *XState);
#define PspArchCaptureXStateContext PspAmd64CaptureXStateContext

NTSTATUS
NTAPI
PspAmd64CompleteXStateContext(
    _In_opt_ PVOID XState,
    _In_ BOOLEAN CopyOut);
#define PspArchCompleteXStateContext PspAmd64CompleteXStateContext

VOID
NTAPI
PspAmd64InitializeUserThreadStartup(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame);
#define PspArchInitializeUserThreadStartup PspAmd64InitializeUserThreadStartup

NTSTATUS
NTAPI
PspAmd64CopyWow64DebugRegisters(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext);
#define PspArchCopyWow64DebugRegisters PspAmd64CopyWow64DebugRegisters
