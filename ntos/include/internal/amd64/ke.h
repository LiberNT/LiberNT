
#define KI_EMULATED_USER_MACHINE 0
#define KiEmulationProcessorArchitecture(Process) PROCESSOR_ARCHITECTURE_INTEL
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 1

#define AMD64_TSS 9

#define APIC_EOI_REGISTER 0xFFFFFFFFFFFE00B0ULL
#define HV_RELAXED_TIMING_RECOMMENDED 0x00000020


extern BOOLEAN KiHvApicMsrs;
extern BOOLEAN KiX2ApicMsrs;
extern SIZE_T KeXStateLength;
extern ULONG KiMxCsrMask;

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



FORCEINLINE
ULONG_PTR
{
    return Context->Rip;
}

FORCEINLINE
VOID
{
    Context->Rip = ProgramCounter;
}

FORCEINLINE
VOID
{
    Context->Rax = ReturnValue;
}

FORCEINLINE
ULONG_PTR
{
    return Context->R8;
}

FORCEINLINE
ULONG_PTR
{
    return Context->R9;
}

FORCEINLINE
ULONG_PTR
{
    return Context->Rsp;
}

FORCEINLINE
ULONG_PTR
{
    return Context->Rbp;
}

FORCEINLINE
VOID
{
    Context->Rbp = Frame;
}

FORCEINLINE
ULONG_PTR
{
    return TrapFrame->Rip;
}

FORCEINLINE
{
}

{

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
    UNREFERENCED_PARAMETER(PreviousMode);


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
    _In_ PKTRAP_FRAME TrapFrame)
{
}

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{

    _disable();
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(FlushSize);
}

FORCEINLINE
{
}

FORCEINLINE
VOID
{

    Idt->Selector = KGDT64_R0_CODE;
    Idt->IstIndex = 0;
    Idt->Dpl = 0;
    Idt->Present = 1;
    Idt->Reserved0 = 0;
    Idt->Reserved1 = 0;
}

FORCEINLINE
PVOID
{

}

FORCEINLINE
VOID
KiSendEOI(VOID)
{
    if (KiHvApicMsrs)
        __writemsr(HV_X64_MSR_EOI, 0);
        __writemsr(X2APIC_EOI_MSR, 0);
}

FORCEINLINE
VOID
{

    _disable();
    HalEndSystemInterrupt(Irql, TrapFrame);
}

FORCEINLINE
BOOLEAN
{
}

FORCEINLINE
VOID
{
}


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

VOID KiGetCacheInformation(VOID);
VOID KiSetProcessorType(VOID);
ULONG64 KiGetFeatureBits(VOID);

NTHALAPI
NTSTATUS
NTAPI
HalAllocateAdapterChannel(

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


