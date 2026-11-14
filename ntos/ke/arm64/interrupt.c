/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/interrupt.c
 * PURPOSE:         ARM64 interrupt bring-up stubs and initialization
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * ARM64 Interrupt management and dispatch (HAL-backed)
 */

#include <ntoskrnl.h>
#if defined(_M_ARM64)
#include <reactos/cpuaudit.h>
#endif
#define NDEBUG
#include <debug.h>
#include <arm64pl011.h>
#include <reactos/smpdbg.h>

/* HAL extension: not yet declared in public headers for ARM64 */
extern ULONG FASTCALL HalGetInterruptSource(VOID);
extern VOID NTAPI HalPerformEndOfInterrupt(VOID);
extern BOOLEAN NTAPI HalArm64ProfileSample(ULONG Increment);
extern VOID NTAPI HalArm64ConnectInterrupt(ULONG Vector);
extern VOID NTAPI HalArm64DisconnectInterrupt(ULONG Vector);

/* Simple vector→KINTERRUPT chain table (SPIs + optional LPIs) */
#define ARM64_LPI_BASE 8192
#define ARM64_LPI_COUNT 1024
#define ARM64_MAX_INTID (ARM64_LPI_BASE + ARM64_LPI_COUNT)
#define ARM64_SGI_COUNT 16
#define ARM64_SGI_IPI 0
#define ARM64_SGI_APC 1
#define ARM64_SGI_DPC 2
#define ARM64_SGI_FREEZE 3
#define ARM64_INTERRUPT_EXIT_APC 0x1
#define ARM64_INTERRUPT_EXIT_DPC 0x2
#define KI_ARM64_INTERRUPT_LOCK_NONE ((PKSPIN_LOCK)(LONG_PTR)-3)
static BOOLEAN KiArm64ConnectInterrupt(IN PKINTERRUPT Interrupt);
static PKINTERRUPT KiArm64BootIntTable[ARM64_MAX_INTID];
static PKINTERRUPT *KiArm64IntTables[MAXIMUM_PROCESSORS] = {KiArm64BootIntTable};
static ULONG KiArm64IntConnections[ARM64_MAX_INTID];
static KSPIN_LOCK KiArm64IntTableLock;
/* Simple timer wiring for bring-up */
static KINTERRUPT KiArm64TimerInterrupt[MAXIMUM_PROCESSORS];
static ULONGLONG KiArm64TimerPeriodTicks;
static ULONGLONG KiArm64TimerFrequency;
static KINTERRUPT KiArm64IpiInterrupt;
static KSPIN_LOCK KiArm64IpiLock;
static BOOLEAN KiArm64UseVirtualTimer = TRUE; /* Use virtual timer - physical timer doesn't work under HVF */

/*
 * Generic-timer PPI INTIDs: 27 = virtual timer (CNTV), 30 = non-secure EL1
 * physical timer (CNTP). Single decision point for every timer PPI operation.
 */
static __inline ULONG KiArm64ClockTimerIntId(void)
{
    return KiArm64UseVirtualTimer ? 27u : 30u;
}
static KTRAP_FRAME KiArm64InterruptTrapFrame[MAXIMUM_PROCESSORS];
static PKTRAP_FRAME KiArm64CurrentInterruptTrapFrame[MAXIMUM_PROCESSORS];

/*
 * Private frame produced by the IRQ vector stubs. Keep this layout in exact
 * lockstep with ARM64_IRQ_* in trapvec.S. The tail stores X22-X28 explicitly:
 * unlike volatile registers, a C call preserves them and therefore cannot
 * reconstruct their interrupted values after the fact.
 */
typedef struct _KI_ARM64_IRQ_FRAME
{
    ULONG64 X[22];
    ULONG64 Fp;
    ULONG64 Lr;
    ULONG64 Elr;
    ULONG64 Spsr;
    ULONG64 SpEl0;
    ULONG64 Reserved0;
    ARM64_NT_NEON128 V[32];
    ULONG Fpcr;
    ULONG ReservedFpcr;
    ULONG Fpsr;
    UCHAR Reserved1[0x4];
    ULONG64 X22;
    ULONG64 X23;
    ULONG64 X24;
    ULONG64 X25;
    ULONG64 X26;
    ULONG64 X27;
    ULONG64 X28;
    ULONG64 Reserved2;
    ULONGLONG AuditEntry;
    ULONGLONG AuditPad;
    ULONG Bcr[ARM64_MAX_BREAKPOINTS];
    ULONG64 Bvr[ARM64_MAX_BREAKPOINTS];
    ULONG Wcr[ARM64_MAX_WATCHPOINTS];
    ULONG DebugRegistersValid;
    ULONG DebugReserved;
    ULONG64 Wvr[ARM64_MAX_WATCHPOINTS];
    UCHAR RedZone[0x10];
} KI_ARM64_IRQ_FRAME, *PKI_ARM64_IRQ_FRAME;

C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, Fp) == 0xB0);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, Elr) == 0xC0);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, SpEl0) == 0xD0);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, V) == 0xE0);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, Fpcr) == 0x2E0);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, Fpsr) == 0x2E8);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, X22) == 0x2F0);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, Bcr) == 0x340);
C_ASSERT(FIELD_OFFSET(KI_ARM64_IRQ_FRAME, RedZone) == 0x3C0);
C_ASSERT(sizeof(KI_ARM64_IRQ_FRAME) == 0x3D0);

VOID
NTAPI
KiArm64SaveIrqUserDebugState(
    _Inout_ PKI_ARM64_IRQ_FRAME Frame)
{
    BOOLEAN Valid;

    KiArm64SaveUserDebugRegisters(&Valid, Frame->Bcr, Frame->Bvr, Frame->Wcr, Frame->Wvr);
    Frame->DebugRegistersValid = Valid;
}

VOID
NTAPI
KiArm64LoadIrqUserDebugState(
    _In_ PKI_ARM64_IRQ_FRAME Frame)
{
    KiArm64LoadUserDebugRegisters(Frame->DebugRegistersValid != 0, Frame->Bcr, Frame->Bvr, Frame->Wcr, Frame->Wvr);
}

/*
 * A freeze IPI can interrupt a thread near the end of its kernel stack.  Keep
 * the large synthetic frames in per-processor storage so debugger entry does
 * not consume another trap frame, exception frame, and full SIMD save area on
 * that thread's stack.  IRQs remain masked while a processor owns its slot.
 */
typedef struct DECLSPEC_CACHEALIGN _KI_ARM64_FREEZE_FRAMES
{
    KEXCEPTION_FRAME ExceptionFrame;
    KTRAP_FRAME TrapFrame;
    KARM64_VFP_STATE VfpState;
} KI_ARM64_FREEZE_FRAMES, *PKI_ARM64_FREEZE_FRAMES;

C_ASSERT(FIELD_OFFSET(KI_ARM64_FREEZE_FRAMES, TrapFrame) ==
         sizeof(KEXCEPTION_FRAME));

static KI_ARM64_FREEZE_FRAMES KiArm64FreezeFrames[MAXIMUM_PROCESSORS];

typedef struct DECLSPEC_CACHEALIGN _KI_ARM64_TIMER_STATE
{
    ULONG Increment;
    ULONG TickOffset;
    ULONGLONG PeriodTicks;
    ULONGLONG LastCounter;
    ULONGLONG CounterRemainder;
    ULONG ElapsedIncrement;
    ULONGLONG PeriodicDeadline;
    KSPIN_LOCK AccountingLock;
    ULONG OwnerIncrement;
    BOOLEAN Owner;
    volatile BOOLEAN IdleSuspended;
} KI_ARM64_TIMER_STATE, *PKI_ARM64_TIMER_STATE;

#define KI_ARM64_CLOCK_RUNNING 0
#define KI_ARM64_CLOCK_ARMING 1
#define KI_ARM64_CLOCK_IDLE 2
#define KI_ARM64_CLOCK_PHASE_MASK 3
#define KI_ARM64_CLOCK_OWNER_SHIFT 2
#define KI_ARM64_CLOCK_STATE(Owner, Phase) ((LONG)(((ULONG)(Owner) << KI_ARM64_CLOCK_OWNER_SHIFT) | (Phase)))
#define KI_ARM64_CLOCK_OWNER(State) ((ULONG)(State) >> KI_ARM64_CLOCK_OWNER_SHIFT)
#define KI_ARM64_CLOCK_PHASE(State) ((ULONG)(State) & KI_ARM64_CLOCK_PHASE_MASK)
#define KI_ARM64_MAXIMUM_IDLE_CLOCK_INTERVAL 10000000ULL
#define KI_ARM64_SUSPENDED_CLOCK_SECONDS 86400ULL

static KI_ARM64_TIMER_STATE KiArm64TimerState[MAXIMUM_PROCESSORS];
static volatile ULONGLONG KiArm64ClockDeadline;
static volatile LONG KiArm64ClockDeadlineReady;
static volatile LONG KiArm64ClockOwnerState = KI_ARM64_CLOCK_STATE(0, KI_ARM64_CLOCK_RUNNING);
static volatile LONG KiArm64DynamicTickEnabled;
static ULONGLONG KiArm64ClockOwnerCounter;
static ULONGLONG KiArm64ClockOwnerRemainder;
static KDPC KiArm64ClockDeadlineDpc[MAXIMUM_PROCESSORS];

/* Include nested and secondary dispatches in interrupt-object rundown. */
typedef struct DECLSPEC_CACHEALIGN _KI_ARM64_DISPATCH_DEPTH
{
    volatile LONG Value;
} KI_ARM64_DISPATCH_DEPTH;

static KI_ARM64_DISPATCH_DEPTH KiArm64DispatchDepth[MAXIMUM_PROCESSORS];

#define KI_ARM64_SPI_BASE 32
#define KI_ARM64_SPI_LIMIT 1020
#define KI_ARM64_FOREIGN_BATCH 8

static KI_ARM64_DISPATCH_DEPTH KiArm64ForeignDispatchDepth[MAXIMUM_PROCESSORS];
static KSPIN_LOCK KiArm64ForeignDispatchLock;

static inline
PKINTERRUPT
KiArm64LoadInterruptHeadNoFence(
    _In_ ULONG IntId,
    _In_ ULONG Cpu)
{
    PKINTERRUPT *Table = (PKINTERRUPT *)ReadPointerNoFence((PVOID const volatile *)&KiArm64IntTables[Cpu]);

    return Table ? (PKINTERRUPT)ReadPointerNoFence((PVOID const volatile *)&Table[IntId]) : NULL;
}

/* An interrupt controller may deliver an SPI to a processor other than the one it is routed to */
static
ULONG
KiArm64CollectForeignInterrupts(
    _In_ ULONG IntId,
    _In_ ULONG Cpu,
    _In_ ULONG Skip,
    _Out_writes_to_(KI_ARM64_FOREIGN_BATCH, return) PKINTERRUPT *Batch,
    _Out_ PBOOLEAN More)
{
    PKINTERRUPT Head, Interrupt;
    ULONG Other, Position = 0, Count = 0;

    ASSERT(IntId >= KI_ARM64_SPI_BASE && IntId < KI_ARM64_SPI_LIMIT);
    ASSERT(Cpu == KeGetCurrentProcessorNumber());
    ASSERT(ReadNoFence(&KiArm64ForeignDispatchDepth[Cpu].Value) > 0);
    ASSERT(KeGetCurrentIrql() < HIGH_LEVEL);

    *More = FALSE;
    KxAcquireSpinLock(&KiArm64ForeignDispatchLock);
    for (Other = 0; (Other < (ULONG)KeNumberProcessors) && !*More; Other++)
    {
        if (Other == Cpu)
            continue;

        Head = KiArm64LoadInterruptHeadNoFence(IntId, Other);
        if (Head == NULL)
            continue;

        Interrupt = Head;
        do
        {
            ASSERT(Interrupt->Vector == IntId);
            ASSERT((UCHAR)Interrupt->Number == Other);
            if (Position++ >= Skip)
            {
                if (Count == KI_ARM64_FOREIGN_BATCH)
                {
                    *More = TRUE;
                    break;
                }

                Batch[Count++] = Interrupt;
            }

            Interrupt = CONTAINING_RECORD(Interrupt->InterruptListEntry.Flink, KINTERRUPT, InterruptListEntry);
        } while (Interrupt != Head);
    }
    KxReleaseSpinLock(&KiArm64ForeignDispatchLock);

    return Count;
}

static
VOID
KiArm64WaitForForeignDispatch(VOID)
{
    ULONG Cpu;

    ASSERT(ReadNoFence(&KiArm64ForeignDispatchDepth[KeGetCurrentProcessorNumber()].Value) == 0);

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        while (ReadAcquire(&KiArm64ForeignDispatchDepth[Cpu].Value) != 0)
            YieldProcessor();
    }
}

PKINTERRUPT NTAPI KiArm64QueryInterrupt(_In_ ULONG IntId)
{
    if (IntId >= ARM64_MAX_INTID)
        return NULL;
    return KiArm64LoadInterruptHeadNoFence(IntId, KeGetCurrentProcessorNumber());
}

ULONG NTAPI KiArm64QueryInterruptLimit(VOID)
{
    return ARM64_MAX_INTID;
}

ULONG KiTimerIsrCallCount = 0;
ULONG KiInitInterruptsCallCount = 0;
ULONG KiTimerStartedFlag = 0;
ULONG KiTimerCtlReadback = 0;

static
PKTRAP_FRAME
KiArm64GetInterruptTrapFrame(
    _In_ ULONG Cpu)
{
    return KiArm64CurrentInterruptTrapFrame[Cpu] ?
           KiArm64CurrentInterruptTrapFrame[Cpu] :
           &KiArm64InterruptTrapFrame[Cpu];
}

static
VOID
KiArm64BuildInterruptFrames(_In_ ULONG VectorId, _In_ KIRQL PreviousIrql, _In_ PKI_ARM64_IRQ_FRAME IrqFrame, _Out_ PKTRAP_FRAME TrapFrame, _Out_ PKEXCEPTION_FRAME ExceptionFrame, _Out_ PKARM64_VFP_STATE VfpState)
{
    ULONG Index;

    RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));
    RtlZeroMemory(ExceptionFrame, sizeof(*ExceptionFrame));
    RtlZeroMemory(VfpState, sizeof(*VfpState));

    TrapFrame->PreviousMode = (VectorId >= 8) ? UserMode : KernelMode;
    TrapFrame->SavedIrql = PreviousIrql;
    TrapFrame->VfpState = VfpState;
    TrapFrame->Spsr = (ULONG)IrqFrame->Spsr;
    TrapFrame->Sp = (VectorId >= 8) ?
                    IrqFrame->SpEl0 :
                    (ULONG_PTR)(IrqFrame + 1);
    for (Index = 0; Index <= 18; Index++)
        TrapFrame->X[Index] = IrqFrame->X[Index];
    TrapFrame->Fp = IrqFrame->Fp;
    TrapFrame->Lr = IrqFrame->Lr;
    TrapFrame->Pc = IrqFrame->Elr;

    ExceptionFrame->X19 = IrqFrame->X[19];
    ExceptionFrame->X20 = IrqFrame->X[20];
    ExceptionFrame->X21 = IrqFrame->X[21];
    ExceptionFrame->X22 = IrqFrame->X22;
    ExceptionFrame->X23 = IrqFrame->X23;
    ExceptionFrame->X24 = IrqFrame->X24;
    ExceptionFrame->X25 = IrqFrame->X25;
    ExceptionFrame->X26 = IrqFrame->X26;
    ExceptionFrame->X27 = IrqFrame->X27;
    ExceptionFrame->X28 = IrqFrame->X28;
    ExceptionFrame->Fp = IrqFrame->Fp;
    ExceptionFrame->Lr = IrqFrame->Lr;
    ExceptionFrame->Return = IrqFrame->Elr;
    ExceptionFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    ExceptionFrame->Fpcr = IrqFrame->Fpcr;
    ExceptionFrame->Fpsr = IrqFrame->Fpsr;

    VfpState->Fpcr = IrqFrame->Fpcr;
    VfpState->Fpsr = IrqFrame->Fpsr;
    RtlCopyMemory(VfpState->V, IrqFrame->V, sizeof(VfpState->V));
}

static
VOID
KiArm64RestoreInterruptFrames(_In_ ULONG VectorId, _Inout_ PKI_ARM64_IRQ_FRAME IrqFrame, _In_ PKTRAP_FRAME TrapFrame, _In_ PKEXCEPTION_FRAME ExceptionFrame, _In_ PKARM64_VFP_STATE VfpState)
{
    ULONG Index;

    for (Index = 0; Index <= 18; Index++)
        IrqFrame->X[Index] = TrapFrame->X[Index];
    IrqFrame->X[19] = ExceptionFrame->X19;
    IrqFrame->X[20] = ExceptionFrame->X20;
    IrqFrame->X[21] = ExceptionFrame->X21;
    IrqFrame->X22 = ExceptionFrame->X22;
    IrqFrame->X23 = ExceptionFrame->X23;
    IrqFrame->X24 = ExceptionFrame->X24;
    IrqFrame->X25 = ExceptionFrame->X25;
    IrqFrame->X26 = ExceptionFrame->X26;
    IrqFrame->X27 = ExceptionFrame->X27;
    IrqFrame->X28 = ExceptionFrame->X28;
    IrqFrame->Fp = TrapFrame->Fp;
    IrqFrame->Lr = TrapFrame->Lr;
    IrqFrame->Elr = TrapFrame->Pc;
    IrqFrame->Spsr = TrapFrame->Spsr;
    if (VectorId >= 8)
        IrqFrame->SpEl0 = TrapFrame->Sp;

    IrqFrame->Fpcr = VfpState->Fpcr;
    IrqFrame->Fpsr = VfpState->Fpsr;
    RtlCopyMemory(IrqFrame->V, VfpState->V, sizeof(IrqFrame->V));
}

VOID
KiArm64PermVectorIrqSpx_Handler(VOID);

VOID
KiArm64PermVectorFiqSpx_Handler(VOID);

BOOLEAN
KiArm64UnwindInterruptFrame(
    _In_ ULONG64 FunctionStart,
    _Inout_ PCONTEXT Context,
    _Inout_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    PKI_ARM64_IRQ_FRAME IrqFrame;
    PULONG64 UpperRegisters;
    ULONG Index;

    if ((FunctionStart != (ULONG64)(ULONG_PTR)KiArm64PermVectorIrqSpx_Handler) &&
        (FunctionStart != (ULONG64)(ULONG_PTR)KiArm64PermVectorFiqSpx_Handler))
    {
        return FALSE;
    }

    IrqFrame = (PKI_ARM64_IRQ_FRAME)(ULONG_PTR)Context->Sp;
    for (Index = 0; Index < 3; Index++)
    {
        (&ContextPointers->X19)[Index] = &IrqFrame->X[19 + Index];
        Context->X[19 + Index] = IrqFrame->X[19 + Index];
    }

    UpperRegisters = &IrqFrame->X22;
    for (Index = 0; Index < 7; Index++)
    {
        (&ContextPointers->X22)[Index] = &UpperRegisters[Index];
        Context->X[22 + Index] = UpperRegisters[Index];
    }

    for (Index = 0; Index < 8; Index++)
    {
        (&ContextPointers->D8)[Index] = &IrqFrame->V[8 + Index].Low;
        Context->V[8 + Index].Low = IrqFrame->V[8 + Index].Low;
    }

    ContextPointers->Fp = &IrqFrame->Fp;
    ContextPointers->Lr = &IrqFrame->Lr;
    Context->Fp = IrqFrame->Fp;
    Context->Lr = IrqFrame->Lr;
    Context->Pc = IrqFrame->Elr;
    Context->Sp = (ULONG64)(ULONG_PTR)(IrqFrame + 1);
    Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
    return TRUE;
}

static inline void KiRawDebugPuts(const char *str) {
    UNREFERENCED_PARAMETER(str);
}

static
ULONG
KiArm64CurrentTimerIncrement(VOID)
{
    ULONG Increment = KeTimeIncrement;

    if (Increment == 0)
        Increment = KeMaximumIncrement;
    if (Increment == 0)
        Increment = 100000;
    if (KeMinimumIncrement && Increment < KeMinimumIncrement)
        Increment = KeMinimumIncrement;
    if (KeMaximumIncrement && Increment > KeMaximumIncrement)
        Increment = KeMaximumIncrement;

    return Increment;
}

static
ULONGLONG
KiArm64ComputeTimerPeriodTicks(
    _In_ ULONG Increment)
{
    ULONGLONG Frequency = KiArm64TimerFrequency;
    ULONGLONG Period;

    if (Frequency == 0)
        Frequency = KiArm64GetCounterFrequency();

    Period = (Frequency * Increment) / 10000000ULL;
    return Period ? Period : 1;
}

BOOLEAN
NTAPI
KiIpiServiceRoutine(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
KiDispatchInterrupt(VOID);

VOID
NTAPI
KiDeliverApc(
    _In_ KPROCESSOR_MODE DeliveryMode,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame);

static
DECLSPEC_NOINLINE
VOID
KiArm64DeliverInterruptApc(
    _In_ ULONG VectorId,
    _Inout_ PKI_ARM64_IRQ_FRAME IrqFrame,
    _In_ KIRQL PreviousIrql)
{
    KI_ARM64_FREEZE_FRAMES Frames;
    PKTHREAD Thread = KeGetCurrentThread();

    ASSERT(VectorId >= 8);
    ASSERT(Thread != NULL);

    KiArm64BuildInterruptFrames(VectorId,
                               PreviousIrql,
                               IrqFrame,
                               &Frames.TrapFrame,
                               &Frames.ExceptionFrame,
                               &Frames.VfpState);
    Frames.TrapFrame.TrapFrame = (ULONG64)(ULONG_PTR)Thread->TrapFrame;

    _enable();
    KiDeliverApc(UserMode, &Frames.ExceptionFrame, &Frames.TrapFrame);
    _disable();

    KiArm64RestoreInterruptFrames(VectorId,
                                 IrqFrame,
                                 &Frames.TrapFrame,
                                 &Frames.ExceptionFrame,
                                 &Frames.VfpState);
}

/*
 * ARM64 Generic Timer register accessors.
 *
 * CNTP_* registers control the EL1 physical timer (PPI 30 non-secure, 29 secure).
 * CNTV_* registers control the EL1 virtual timer (PPI 27).
 *
 * Timer control register (CTL) bits:
 *   Bit 0 (ENABLE):  1 = timer enabled
 *   Bit 1 (IMASK):   1 = interrupt masked (disabled)
 *   Bit 2 (ISTATUS): read-only, 1 = timer condition met
 *
 * CVAL is an absolute counter deadline. Once the counter reaches CVAL,
 * ISTATUS remains asserted until CVAL is moved into the future or the timer
 * is masked or disabled.
 */

static __inline ULONG KiArm64ReadCntpCtl(void)
{
    ULONGLONG v;
    v = ARM64_READ_SYSREG(CNTP_CTL_EL0);
    return (ULONG)v;
}

static __inline ULONG KiArm64ReadCntvCtl(void)
{
    ULONGLONG v;
    v = ARM64_READ_SYSREG(CNTV_CTL_EL0);
    return (ULONG)v;
}

static __inline ULONGLONG KiArm64ReadCounter(VOID)
{
    ULONGLONG Value;

    if (KiArm64UseVirtualTimer)
    {
        ARM64_ISB();
        Value = ARM64_READ_SYSREG(CNTVCT_EL0);
    }
    else
    {
        ARM64_ISB();
        Value = ARM64_READ_SYSREG(CNTPCT_EL0);
    }
    return Value;
}

static __inline VOID KiArm64WriteTimerCompare(ULONGLONG Value)
{
    if (KiArm64UseVirtualTimer)
        ARM64_WRITE_SYSREG(CNTV_CVAL_EL0, Value);
    else
        ARM64_WRITE_SYSREG(CNTP_CVAL_EL0, Value);
}

static __inline VOID KiArm64WriteCntpCtl(ULONG v)
{
    /*
     * ISB is required after writing CTL to ensure the timer enable/disable
     * takes effect immediately. Without ISB, subsequent instructions may
     * execute before the timer state change is visible.
     */
    ARM64_WRITE_SYSREG(CNTP_CTL_EL0, (ULONGLONG)v);
    ARM64_ISB();
}

FORCEINLINE
ULONG_PTR
KiArm64MaskClockInterrupts(VOID)
{
    ULONG_PTR Daif = ARM64_READ_SYSREG(DAIF);

    ARM64_DAIFSET(3);
    return Daif;
}

FORCEINLINE
VOID
KiArm64RestoreClockInterrupts(
    _In_ ULONG_PTR Daif)
{
    ARM64_WRITE_SYSREG(DAIF, Daif);
}

FORCEINLINE
BOOLEAN
KiArm64ClockInterruptsMasked(VOID)
{
    const ULONG_PTR Mask = ARM64_PSTATE_IRQ_MASK | ARM64_PSTATE_FIQ_MASK;

    return (ARM64_READ_SYSREG(DAIF) & Mask) == Mask;
}

FORCEINLINE
BOOLEAN
KiArm64ProfilingActive(VOID)
{
    return ReadPointerNoFence((PVOID const volatile *)&KiProfileSourceListHead.Flink) != &KiProfileSourceListHead;
}

static
ULONGLONG
KiArm64ClockDeadlineCompare(
    _In_ BOOLEAN FireIfDue)
{
    ULONGLONG Deadline = KiArm64ClockDeadline;
    ULONGLONG Now, Counter;

    if (Deadline == 0)
        return MAXULONGLONG;
    Now = KiQueryInterruptTimePrecise(NULL, FALSE);
    Counter = KiArm64ReadCounter();
    if (Deadline <= Now)
        return FireIfDue ? Counter + 1 : MAXULONGLONG;
    if (Deadline - Now > KeMaximumIncrement)
        return MAXULONGLONG;
    return Counter + ((Deadline - Now) * KiArm64TimerFrequency + 9999999) / 10000000 + 1;
}

static
VOID
KiArm64ArmPeriodicClock(
    _Inout_ PKI_ARM64_TIMER_STATE TimerState,
    _In_ ULONGLONG Counter,
    _In_ BOOLEAN RestartPeriod)
{
    ULONG Increment;
    ULONGLONG Compare, HighResolution, Now, Next;

    ASSERT(KiArm64ClockInterruptsMasked());
    ASSERT(!TimerState->IdleSuspended);

    Increment = KiArm64CurrentTimerIncrement();
    if (!TimerState->Owner && KeMaximumIncrement > Increment)
        Increment = KeMaximumIncrement;
    if (TimerState->Increment != Increment || TimerState->PeriodTicks == 0)
    {
        TimerState->Increment = Increment;
        TimerState->PeriodTicks = KiArm64ComputeTimerPeriodTicks(Increment);
    }
    ASSERT(TimerState->PeriodTicks != 0);

    /* Rearm once beyond the current counter, without replaying missed IRQs. */
    if (RestartPeriod)
        TimerState->PeriodicDeadline = Counter + TimerState->PeriodTicks;
    else if (TimerState->PeriodicDeadline <= Counter)
        TimerState->PeriodicDeadline += ((Counter - TimerState->PeriodicDeadline) / TimerState->PeriodTicks + 1) * TimerState->PeriodTicks;
    if (TimerState->Owner)
    {
        Now = KiQueryInterruptTimePrecise(NULL, FALSE);
        Next = (Now / TimerState->Increment + 1) * TimerState->Increment;
        TimerState->PeriodicDeadline = KiArm64ReadCounter() +
            ((Next - Now) * KiArm64TimerFrequency + 9999999) / 10000000;
    }
    Compare = TimerState->PeriodicDeadline;
    if (TimerState->Owner)
    {
        HighResolution = KiArm64ClockDeadlineCompare(FALSE);
        if (HighResolution < Compare)
            Compare = HighResolution;
    }
    KiArm64WriteTimerCompare(Compare);
}

static
VOID
KiArm64ReprogramClock(
    _In_ ULONG Cpu)
{
    PKI_ARM64_TIMER_STATE TimerState = &KiArm64TimerState[Cpu];
    ULONGLONG Compare, HighResolution;

    ASSERT(Cpu == KeGetCurrentProcessorNumber());
    ASSERT(KiArm64ClockInterruptsMasked());
    ASSERT(KiArm64ClockOwnerState == KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING));
    ASSERT(TimerState->Owner);
    ASSERT(!TimerState->IdleSuspended);

    Compare = TimerState->PeriodicDeadline;
    HighResolution = KiArm64ClockDeadlineCompare(TRUE);
    if (HighResolution < Compare)
        Compare = HighResolution;
    KiArm64WriteTimerCompare(Compare);
}

FORCEINLINE
VOID
KiArm64RequestClockInterrupt(VOID)
{
    ASSERT(KiArm64ClockInterruptsMasked());
    KiArm64WriteTimerCompare(KiArm64ReadCounter());
}

static
BOOLEAN
KiArm64AcquireClockOwnership(
    _In_ ULONG Cpu)
{
    LONG State, Previous;
    ULONG Owner;

    ASSERT(Cpu == KeGetCurrentProcessorNumber());
    ASSERT(KiArm64ClockInterruptsMasked());

    State = ReadAcquire(&KiArm64ClockOwnerState);
    for (;;)
    {
        Owner = KI_ARM64_CLOCK_OWNER(State);
        ASSERT(Owner < MAXIMUM_PROCESSORS);
        ASSERT(KiProcessorBlock[Owner] != NULL);
        ASSERT(KI_ARM64_CLOCK_PHASE(State) <= KI_ARM64_CLOCK_IDLE);
        if (KI_ARM64_CLOCK_PHASE(State) == KI_ARM64_CLOCK_RUNNING)
            return (Owner == Cpu);
        ASSERT((Owner != Cpu) || (KI_ARM64_CLOCK_PHASE(State) == KI_ARM64_CLOCK_IDLE));
        Previous = InterlockedCompareExchange(&KiArm64ClockOwnerState,
                                              KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING),
                                              State);
        if (Previous == State)
            break;
        State = Previous;
    }

    KeMemoryBarrier();
    if (Owner != Cpu)
        KiProcessorBlock[Owner]->ClockOwner = FALSE;
    KiProcessorBlock[Cpu]->ClockOwner = TRUE;
    return TRUE;
}

static
VOID
KiArm64QueueClockDeadlineDpc(
    _In_ ULONG Owner)
{
    PKDPC Dpc = &KiArm64ClockDeadlineDpc[Owner];

    ASSERT(Owner < (ULONG)KeNumberProcessors);
    ASSERT(Owner != KeGetCurrentProcessorNumber());
    ASSERT(Dpc->DeferredRoutine != NULL);
    KeInsertQueueDpc(Dpc, NULL, NULL);
}

static
VOID
NTAPI
KiArm64ClockDeadlineDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    ULONG_PTR Daif;

    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(Dpc == &KiArm64ClockDeadlineDpc[Cpu]);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    Daif = KiArm64MaskClockInterrupts();
    if (ReadAcquire(&KiArm64ClockOwnerState) == KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING))
        KiArm64ReprogramClock(Cpu);
    KiArm64RestoreClockInterrupts(Daif);
}

BOOLEAN
NTAPI
KiSetClockDeadline(
    _In_ ULONGLONG InterruptTime)
{
    KIRQL OldIrql;
    ULONG_PTR Daif;
    ULONG Cpu;
    LONG State;

    KiArm64ClockDeadline = InterruptTime;
    if (!KiArm64ClockDeadlineReady)
        return TRUE;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeMemoryBarrier();
    Cpu = KeGetCurrentProcessorNumber();
    Daif = KiArm64MaskClockInterrupts();
    State = ReadAcquire(&KiArm64ClockOwnerState);
    ASSERT((KI_ARM64_CLOCK_OWNER(State) != Cpu) || (KI_ARM64_CLOCK_PHASE(State) == KI_ARM64_CLOCK_RUNNING));
    if (State == KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING))
        KiArm64ReprogramClock(Cpu);
    KiArm64RestoreClockInterrupts(Daif);

    if (KI_ARM64_CLOCK_OWNER(State) != Cpu)
    {
        if (KI_ARM64_CLOCK_PHASE(State) == KI_ARM64_CLOCK_RUNNING)
            KiArm64QueueClockDeadlineDpc(KI_ARM64_CLOCK_OWNER(State));
        else if (InterruptTime != 0)
            KiArm64ClockTimerInserted(InterruptTime);
    }
    KeLowerIrql(OldIrql);
    return TRUE;
}

static
VOID
KiArm64AccumulateClockElapsed(
    _Inout_ PKI_ARM64_TIMER_STATE TimerState,
    _In_ ULONGLONG Counter)
{
    ULONGLONG Elapsed, Scaled;

    ASSERT(Counter >= TimerState->LastCounter);
    Elapsed = Counter - TimerState->LastCounter;
    /* Bound the conversion; any remaining time is charged on following IRQs. */
    Elapsed = min(Elapsed, (ULONGLONG)MAXLONG * KiArm64TimerFrequency / 10000000);
    Scaled = Elapsed * 10000000 + TimerState->CounterRemainder;
    TimerState->ElapsedIncrement = (ULONG)(Scaled / KiArm64TimerFrequency);
    TimerState->CounterRemainder = Scaled % KiArm64TimerFrequency;
    TimerState->LastCounter += Elapsed;
}

static
ULONG
KiArm64TakeClockOwnerIncrement(
    _In_ ULONGLONG Counter)
{
    ULONGLONG Elapsed, Scaled;
    ULONG Increment;

    ASSERT(KiArm64ClockInterruptsMasked());
    ASSERT(KiArm64ClockOwnerState == KI_ARM64_CLOCK_STATE(KeGetCurrentProcessorNumber(), KI_ARM64_CLOCK_RUNNING));
    if (Counter <= KiArm64ClockOwnerCounter)
        return 0;
    Elapsed = min(Counter - KiArm64ClockOwnerCounter, (ULONGLONG)MAXLONG * KiArm64TimerFrequency / 10000000);
    Scaled = Elapsed * 10000000 + KiArm64ClockOwnerRemainder;
    Increment = (ULONG)(Scaled / KiArm64TimerFrequency);
    KiArm64ClockOwnerRemainder = Scaled % KiArm64TimerFrequency;
    KiArm64ClockOwnerCounter += Elapsed;
    return Increment;
}

VOID
NTAPI
KiArm64QueryClockTickBaseline(
    _Out_ PLARGE_INTEGER Counter,
    _Out_ PULONGLONG Remainder)
{
    if (KiArm64ClockOwnerCounter == 0)
    {
        *Counter = KeQueryPerformanceCounter(NULL);
        *Remainder = 0;
        return;
    }

    ASSERT(KiArm64ClockOwnerRemainder < KiArm64TimerFrequency);
    Counter->QuadPart = (LONGLONG)KiArm64ClockOwnerCounter;
    *Remainder = KiArm64ClockOwnerRemainder;
}

static
ULONG
KiArm64ConsumeIdleTicks(
    _Inout_ PKI_ARM64_TIMER_STATE TimerState,
    _In_ ULONGLONG Counter)
{
    ULONG Ticks = 0;

    ASSERT(KiArm64ClockInterruptsMasked());
    ASSERT(KeMaximumIncrement != 0);
    while (TimerState->LastCounter != Counter)
    {
        KiArm64AccumulateClockElapsed(TimerState, Counter);
        TimerState->TickOffset += TimerState->ElapsedIncrement;
        TimerState->ElapsedIncrement = 0;
        Ticks += TimerState->TickOffset / KeMaximumIncrement;
        TimerState->TickOffset %= KeMaximumIncrement;
    }
    return Ticks;
}

static
VOID
KiArm64AcknowledgeClockInterrupt(
    _In_ ULONG Cpu)
{
    PKI_ARM64_TIMER_STATE TimerState = &KiArm64TimerState[Cpu];
    ULONGLONG Counter;

    ASSERT(KiArm64ClockInterruptsMasked());
    ASSERT(KiArm64ClockTimerState(KeGetCurrentPrcb())->ClockActive);
    ASSERT(!TimerState->IdleSuspended);

    TimerState->Owner = KiArm64AcquireClockOwnership(Cpu);
    Counter = KiArm64ReadCounter();
    KiArm64AccumulateClockElapsed(TimerState, Counter);
    TimerState->OwnerIncrement = TimerState->Owner ? KiArm64TakeClockOwnerIncrement(Counter) : 0;
    KiArm64ArmPeriodicClock(TimerState, Counter, FALSE);
}

static
VOID
KiArm64ChargeClockTick(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKI_ARM64_TIMER_STATE TimerState,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL Irql)
{
    ULONG RuntimeIncrement;
    ULONG RuntimeTicks;

    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(TimerState == &KiArm64TimerState[Prcb->Number]);

    if (TimerState->Owner)
    {
        ASSERT(KiArm64ClockOwnerState == KI_ARM64_CLOCK_STATE(Prcb->Number, KI_ARM64_CLOCK_RUNNING));
        KiUpdateClockTime(Prcb, TimerState->OwnerIncrement);
    }

    RuntimeIncrement = KeMaximumIncrement ? KeMaximumIncrement : TimerState->Increment;
    TimerState->TickOffset += TimerState->ElapsedIncrement;
    if (TimerState->TickOffset >= RuntimeIncrement)
    {
        RuntimeTicks = TimerState->TickOffset / RuntimeIncrement;
        TimerState->TickOffset %= RuntimeIncrement;
        KiUpdateRunTime(TrapFrame, Irql, RuntimeTicks);
    }
    else
    {
        Prcb->InterruptCount++;
    }
}

static
ULONGLONG
KiArm64NextClockDeadline(
    _In_ ULONGLONG Now)
{
    ULONGLONG Deadline = Now + KI_ARM64_MAXIMUM_IDLE_CLOCK_INTERVAL;
    ULONGLONG DueTime;
    ULONG Index;

    for (Index = 0; Index < TIMER_TABLE_SIZE; Index++)
    {
        DueTime = *(volatile ULONGLONG *)&KiTimerTableListHead[Index].Time.QuadPart;
        if (DueTime < Deadline)
            Deadline = DueTime;
    }

    DueTime = KiArm64ClockDeadline;
    if ((DueTime != 0) && (DueTime < Deadline))
        Deadline = DueTime;
    return Deadline;
}

VOID
NTAPI
KiArm64ClockArmForIdle(
    _Inout_ PKPRCB Prcb)
{
    PKCLOCK_TIMER_STATE ClockState = KiArm64ClockTimerState(Prcb);
    PKI_ARM64_TIMER_STATE TimerState;
    ULONG Cpu = Prcb->Number;
    LONG State, Previous;
    ULONGLONG Now, Deadline, Counter;

    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(Cpu < MAXIMUM_PROCESSORS);
    ASSERT(KiArm64ClockInterruptsMasked());
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(Prcb->CurrentThread == Prcb->IdleThread);
    ASSERT(Prcb->Sleeping);
    TimerState = &KiArm64TimerState[Cpu];
    ASSERT(!TimerState->IdleSuspended);
    ASSERT(!ClockState->ClockArmedForIdle);
    ASSERT(ClockState->OneShotState == KClockTimerOneShotUnarmed);

    if (!ClockState->ClockActive || !KiArm64DynamicTickEnabled || KiArm64ProfilingActive())
        return;

    State = ReadAcquire(&KiArm64ClockOwnerState);
    if (KI_ARM64_CLOCK_OWNER(State) == Cpu)
    {
        ASSERT(State == KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING));
        ASSERT(TimerState->Owner);
        if ((KeActiveProcessors & ~(KiIdleSummary | Prcb->SetMember)) != 0)
            return;
        Previous = InterlockedCompareExchange(&KiArm64ClockOwnerState,
                                              KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_ARMING),
                                              State);
        ASSERT(Previous == State);
        UNREFERENCED_PARAMETER(Previous);
        KeMemoryBarrier();

        Now = KiQueryInterruptTimePrecise(NULL, FALSE);
        Deadline = KiArm64NextClockDeadline(Now);
        if ((Deadline <= Now) || (Deadline - Now < TimerState->Increment))
        {
            if (InterlockedCompareExchange(&KiArm64ClockOwnerState,
                                           KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING),
                                           KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_ARMING)) ==
                KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_ARMING))
            {
                return;
            }
        }
        else
        {
            ASSERT(Deadline - Now <= KI_ARM64_MAXIMUM_IDLE_CLOCK_INTERVAL);
            ClockState->NextTickDueTime = Deadline;
            ClockState->ExpectedWakeReason = KClockTimerKTimerExpirationNonHr;
            if (InterlockedCompareExchange(&KiArm64ClockOwnerState,
                                           KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_IDLE),
                                           KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_ARMING)) ==
                KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_ARMING))
            {
                ClockState->ClockActive = FALSE;
                ClockState->ClockArmedForIdle = TRUE;
                ClockState->OneShotState = KClockTimerOneShotArmed;
                KxAcquireSpinLock(&TimerState->AccountingLock);
                TimerState->IdleSuspended = TRUE;
                KxReleaseSpinLock(&TimerState->AccountingLock);
                Counter = KiArm64ReadCounter();
                KiArm64WriteTimerCompare(Counter + ((Deadline - Now) * KiArm64TimerFrequency + 9999999) / 10000000 + 1);
                return;
            }
        }

        ASSERT(KI_ARM64_CLOCK_OWNER(KiArm64ClockOwnerState) != Cpu);
        Prcb->ClockOwner = FALSE;
    }

    TimerState->Owner = FALSE;
    KxAcquireSpinLock(&TimerState->AccountingLock);
    TimerState->IdleSuspended = TRUE;
    KxReleaseSpinLock(&TimerState->AccountingLock);
    ClockState->ClockActive = FALSE;
    KiArm64WriteTimerCompare(KiArm64ReadCounter() + KiArm64TimerFrequency * KI_ARM64_SUSPENDED_CLOCK_SECONDS);
}

VOID
NTAPI
KiArm64ClockResumeFromIdle(
    _Inout_ PKPRCB Prcb)
{
    PKCLOCK_TIMER_STATE ClockState = KiArm64ClockTimerState(Prcb);
    PKI_ARM64_TIMER_STATE TimerState;
    KTRAP_FRAME TrapFrame;
    ULONG Cpu = Prcb->Number;
    ULONG Ticks;

    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(Cpu < MAXIMUM_PROCESSORS);
    ASSERT(KiArm64ClockInterruptsMasked());
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(Prcb->CurrentThread == Prcb->IdleThread);
    TimerState = &KiArm64TimerState[Cpu];

    if (!TimerState->IdleSuspended)
    {
        ASSERT(!ClockState->ClockArmedForIdle);
        ASSERT(ClockState->OneShotState == KClockTimerOneShotUnarmed);
        return;
    }

    ASSERT(!ClockState->ClockActive);
    ASSERT(ClockState->ClockArmedForIdle == (ClockState->OneShotState == KClockTimerOneShotArmed));
    ClockState->ClockArmedForIdle = FALSE;
    ClockState->OneShotState = KClockTimerOneShotUnarmed;
    ClockState->ClockActive = TRUE;
    KxAcquireSpinLock(&TimerState->AccountingLock);
    TimerState->IdleSuspended = FALSE;
    Ticks = KiArm64ConsumeIdleTicks(TimerState, KiArm64ReadCounter());
    KxReleaseSpinLock(&TimerState->AccountingLock);
    TimerState->Owner = KiArm64AcquireClockOwnership(Cpu);

    TrapFrame.PreviousMode = KernelMode;
    TrapFrame.SavedIrql = DISPATCH_LEVEL;
    TrapFrame.Pc = (ULONG_PTR)_ReturnAddress();
    if (TimerState->Owner)
    {
        ASSERT(KiArm64ClockOwnerState == KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING));
        KiUpdateClockTime(Prcb, KiArm64TakeClockOwnerIncrement(KiArm64ReadCounter()));
    }
    if (Ticks != 0)
        KiUpdateRunTime(&TrapFrame, DISPATCH_LEVEL, Ticks);
    else
        Prcb->InterruptCount++;
    KiArm64ArmPeriodicClock(TimerState, KiArm64ReadCounter(), TRUE);
}

VOID
NTAPI
KiArm64RefreshProcessorRunTime(
    _In_ PKPRCB Prcb)
{
    PKI_ARM64_TIMER_STATE TimerState;
    ULONG_PTR Daif;
    ULONG Ticks;
    KIRQL OldIrql = DISPATCH_LEVEL;

    ASSERT(Prcb != NULL);
    ASSERT(Prcb->Number < MAXIMUM_PROCESSORS);
    ASSERT(Prcb->IdleThread != NULL);
    TimerState = &KiArm64TimerState[Prcb->Number];
    if (!TimerState->IdleSuspended)
        return;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Daif = KiArm64MaskClockInterrupts();
    if (Prcb != KeGetCurrentPrcb())
    {
        KxAcquireSpinLock(&TimerState->AccountingLock);
        if (TimerState->IdleSuspended)
        {
            Ticks = KiArm64ConsumeIdleTicks(TimerState, KiArm64ReadCounter());
            Prcb->KernelTime += Ticks;
            Prcb->IdleThread->KernelTime += Ticks;
            KiChargeThreadCycleTime(Prcb, Prcb->IdleThread);
        }
        KxReleaseSpinLock(&TimerState->AccountingLock);
    }
    KiArm64RestoreClockInterrupts(Daif);
    if (OldIrql < DISPATCH_LEVEL)
        KeLowerIrql(OldIrql);
}

VOID
FASTCALL
KiArm64ClockTimerInserted(
    _In_ ULONGLONG DueTime)
{
    PKPRCB Prcb;
    PKPRCB OwnerPrcb;
    LONG State;
    ULONG_PTR Daif;
    BOOLEAN QueueDpc = FALSE;

    KeMemoryBarrier();
    State = ReadAcquire(&KiArm64ClockOwnerState);
    if (KI_ARM64_CLOCK_PHASE(State) == KI_ARM64_CLOCK_RUNNING)
        return;

    OwnerPrcb = KiProcessorBlock[KI_ARM64_CLOCK_OWNER(State)];
    ASSERT(OwnerPrcb != NULL);
    if ((KI_ARM64_CLOCK_PHASE(State) == KI_ARM64_CLOCK_IDLE) &&
        (DueTime >= *(volatile ULONGLONG *)&KiArm64ClockTimerState(OwnerPrcb)->NextTickDueTime))
    {
        return;
    }

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    Prcb = KeGetCurrentPrcb();
    ASSERT(OwnerPrcb != Prcb);
    Daif = KiArm64MaskClockInterrupts();
    if (KiArm64ClockTimerState(Prcb)->ClockActive && KiArm64AcquireClockOwnership(Prcb->Number))
    {
        KiArm64TimerState[Prcb->Number].Owner = TRUE;
        KiArm64RequestClockInterrupt();
    }
    else
    {
        State = ReadAcquire(&KiArm64ClockOwnerState);
        QueueDpc = (KI_ARM64_CLOCK_PHASE(State) != KI_ARM64_CLOCK_RUNNING);
    }
    KiArm64RestoreClockInterrupts(Daif);
    if (QueueDpc)
        KiArm64QueueClockDeadlineDpc(KI_ARM64_CLOCK_OWNER(State));
}

static __inline VOID KiArm64WriteCntvCtl(ULONG v)
{
    /*
     * ISB is required after writing CTL to ensure the timer enable/disable
     * takes effect immediately. Without ISB, subsequent instructions may
     * execute before the timer state change is visible.
     */
    ARM64_WRITE_SYSREG(CNTV_CTL_EL0, (ULONGLONG)v);
    ARM64_ISB();
}

/*
 * ARM64 Timer ISR - Called at CLOCK_LEVEL/DISPATCH_LEVEL on each tick.
 *
 * The interrupt dispatcher rearms the timer before enabling nested IRQs.
 * This routine updates system time and runs the scheduler tick.
 *
 * The timer runs at ~100 Hz (10ms per tick = 100,000 100ns units).
 *
 * NOTE: Do NOT use DPRINT1 or any debug print in this ISR!
 * The print subsystem uses spinlocks and we can deadlock if the
 * timer fires while HalInitSystem is holding the debug print lock.
 */
static BOOLEAN NTAPI
KiArm64TimerIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    PKTRAP_FRAME TrapFrame;
    PKI_ARM64_TIMER_STATE TimerState;
    ULONG Cpu;
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(ServiceContext);

    Cpu = KeGetCurrentProcessorNumber();
    ASSERT(Cpu < MAXIMUM_PROCESSORS);
    TimerState = &KiArm64TimerState[Cpu];

    /*
     * The common clock path dereferences the trap frame for previous-mode
     * accounting. Keep this as a real frame, not a sentinel pointer.
     */
    TrapFrame = KiArm64GetInterruptTrapFrame(Cpu);
    KiCpuAuditTick(TrapFrame->Pc);

    /* SMP boot diagnostics: silent per-CPU tick counter only. The periodic
     * serial dump is disabled for this probe - at 115200 baud it perturbs the
     * timing race; the per-CPU counters are read via GDB at the stall instead. */
    if (SmpDbgEnabled)
    {
        InterlockedIncrement((volatile LONG *)&KiTimerIsrCallCount);
        SmpDbgTimerTick(Cpu);
        SmpDbgHeartbeat(Cpu);
    }

    /*
     * KeUpdateRunTime accounts kernel/user/idle/DPC/interrupt time from the
     * interrupted context. Match the HAL clock contract used by the other
     * architectures: pass the IRQL that was active before the timer interrupt,
     * not CLOCK_LEVEL itself.
     */
    KiArm64ChargeClockTick(KeGetCurrentPrcb(), TimerState, TrapFrame, TrapFrame->SavedIrql);

    /*
     * Clock-driven profiling: ARM64 has no separate profile timer, so the
     * architected-timer clock doubles as the profile source. HalArm64ProfileSample
     * accumulates the per-tick increment and returns TRUE only once the requested
     * profile interval has elapsed, so coarser intervals are honored instead of
     * sampling on every single tick.
     */
    if (HalArm64ProfileSample(TimerState->ElapsedIncrement))
    {
        KeProfileInterrupt(TrapFrame);
    }

    return TRUE;
}

static BOOLEAN NTAPI
KiArm64IpiIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(ServiceContext);

    return KiIpiServiceRoutine(NULL, NULL);
}

/*
 * KiArm64StartTimer - Initialize and start the ARM64 generic timer.
 *
 * This function programs the architected timer to fire at 100 Hz (10ms period).
 * The timer is used for the system clock tick, which drives:
 *   - KeUpdateSystemTime (system time and interrupt time)
 *   - Thread quantum expiration (scheduler preemption)
 *   - Timer DPC expiration
 *   - Profiling (if enabled)
 *
 * We use the virtual timer (CNTV) by default as it's always accessible from EL1,
 * even under a hypervisor. The physical timer (CNTP) may require secure world
 * or hypervisor permissions on some platforms.
 *
 * Timer configuration:
 *   CNTFRQ_EL0: Counter frequency in Hz (typically 1-100 MHz)
 *   CNTV_CVAL_EL0: Absolute counter deadline
 *   CNTV_CTL_EL0: Control (bit 0=ENABLE, bit 1=IMASK)
 *
 * For 100 Hz with a 24.576 MHz counter, the CVAL interval is 245760 ticks.
 * For 100 Hz with a 100 MHz counter, the CVAL interval is 1000000 ticks.
 */
static
VOID
KiArm64StartLocalTimer(VOID)
{
    ULONGLONG Counter;
    ULONGLONG frq;
    PKI_ARM64_TIMER_STATE TimerState;
    PKCLOCK_TIMER_STATE ClockState;
    PKPRCB Prcb;
    ULONG_PTR Daif;
    ULONG Increment;
    ULONG Cpu;
    ULONG ctl;

    Daif = KiArm64MaskClockInterrupts();

    /* Read and validate CNTFRQ_EL0 (shared fallback for bogus firmware) */
    frq = KiArm64GetCounterFrequency();

    KiArm64TimerFrequency = frq;
    Increment = KiArm64CurrentTimerIncrement();
    Cpu = KeGetCurrentProcessorNumber();
    ASSERT(Cpu < MAXIMUM_PROCESSORS);
    Prcb = KeGetCurrentPrcb();
    ClockState = KiArm64ClockTimerState(Prcb);

    TimerState = &KiArm64TimerState[Cpu];
    ASSERT(!TimerState->IdleSuspended);
    TimerState->Owner = (KiArm64ClockOwnerState == KI_ARM64_CLOCK_STATE(Cpu, KI_ARM64_CLOCK_RUNNING));
    ASSERT(TimerState->Owner || (Cpu != 0) || KiArm64DynamicTickEnabled);
    Prcb->ClockOwner = TimerState->Owner;
    ClockState->ClockArmedForIdle = FALSE;
    ClockState->OneShotState = KClockTimerOneShotUnarmed;
    ClockState->ClockActive = TRUE;
    if (KiArm64ClockDeadlineDpc[Cpu].DeferredRoutine == NULL)
    {
        KeInitializeDpc(&KiArm64ClockDeadlineDpc[Cpu], KiArm64ClockDeadlineDpcRoutine, NULL);
        KiSetTargetProcessorDpc(&KiArm64ClockDeadlineDpc[Cpu], (CCHAR)Cpu);
        KeSetImportanceDpc(&KiArm64ClockDeadlineDpc[Cpu], HighImportance);
    }
    TimerState->Increment = Increment;
    TimerState->TickOffset = 0;
    TimerState->PeriodTicks = KiArm64ComputeTimerPeriodTicks(Increment);
    KiArm64TimerPeriodTicks = TimerState->PeriodTicks;
    Counter = KiArm64ReadCounter();
    TimerState->LastCounter = Counter;
    TimerState->CounterRemainder = 0;
    if (TimerState->Owner)
    {
        KiArm64ClockOwnerCounter = Counter;
        KiArm64ClockOwnerRemainder = 0;
    }
    TimerState->ElapsedIncrement = 0;
    TimerState->PeriodicDeadline = Counter + TimerState->PeriodTicks;
    KiArm64WriteTimerCompare(TimerState->PeriodicDeadline);
    ARM64_ISB();

    if (KiArm64UseVirtualTimer)
    {
        /*
         * Configure virtual timer (CNTV):
         * 1. Set an absolute deadline in CNTV_CVAL_EL0
         * 2. Enable timer with ENABLE=1, IMASK=0 in CNTV_CTL_EL0
         */
        KiArm64WriteCntvCtl(1); /* ENABLE=1, IMASK=0 */

        ctl = KiArm64ReadCntvCtl();
    }
    else
    {
        /*
         * Configure physical timer (CNTP):
         * 1. Set an absolute deadline in CNTP_CVAL_EL0
         * 2. Enable timer with ENABLE=1, IMASK=0 in CNTP_CTL_EL0
         */
        KiArm64WriteCntpCtl(1); /* ENABLE=1, IMASK=0 */

        ctl = KiArm64ReadCntpCtl();
    }
    KiTimerCtlReadback = ctl;
    KiTimerStartedFlag = 1;

    if (Cpu == 0 && !KiArm64ClockDeadlineReady)
    {
        InterlockedExchange(&KiArm64ClockDeadlineReady, 1);
        if (TimerState->Owner)
            KiArm64ReprogramClock(Cpu);
    }
    KiArm64RestoreClockInterrupts(Daif);
}

VOID
NTAPI
KeStartArm64ProcessorTimer(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    PKINTERRUPT Interrupt = &KiArm64TimerInterrupt[Cpu];
    PKINTERRUPT *Table;

    ASSERT(Cpu < MAXIMUM_PROCESSORS);
    if (!KiArm64IntTables[Cpu])
    {
        Table = ExAllocatePoolZero(NonPagedPool, sizeof(KiArm64BootIntTable), TAG_KERNEL);
        if (!Table)
            KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, STATUS_INSUFFICIENT_RESOURCES, Cpu, 0, 0);
        InterlockedExchangePointer((PVOID volatile *)&KiArm64IntTables[Cpu], Table);
    }

    KeInitializeInterrupt(Interrupt,
                          KiArm64TimerIsr,
                          &KiArm64TimerPeriodTicks,
                          KI_ARM64_INTERRUPT_LOCK_NONE,
                          KiArm64ClockTimerIntId(),
                          CLOCK_LEVEL,
                          CLOCK_LEVEL,
                          LevelSensitive,
                          FALSE,
                          (CHAR)Cpu,
                          FALSE);
    if (!KeConnectInterrupt(Interrupt))
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, STATUS_UNSUCCESSFUL, Cpu, Interrupt->Vector, 0);
    KiArm64StartLocalTimer();
}

CODE_SEG("INIT")
VOID
NTAPI
KeInitInterrupts(VOID)
{
    KiRawDebugPuts("[KeInitInterrupts] ENTRY\n");
    KiInitInterruptsCallCount = 1;

    KiRawDebugPuts("[KeInitInterrupts] KeInitializeSpinLock IntTableLock\n");
    KeInitializeSpinLock(&KiArm64IntTableLock);
    KiRawDebugPuts("[KeInitInterrupts] IntTableLock done\n");

    /* Wire SGIs for IPI/APC/DPC */
    KiRawDebugPuts("[KeInitInterrupts] IPI spinlock\n");
    KeInitializeSpinLock(&KiArm64IpiLock);
    KiRawDebugPuts("[KeInitInterrupts] IPI KeInitializeInterrupt\n");
    KeInitializeInterrupt(&KiArm64IpiInterrupt,
                          KiArm64IpiIsr,
                          NULL,
                          &KiArm64IpiLock,
                          ARM64_SGI_IPI,
                          IPI_LEVEL,
                          IPI_LEVEL,
                          Latched,
                          FALSE,
                          0,
                          FALSE);
    KiRawDebugPuts("[KeInitInterrupts] IPI KeConnectInterrupt\n");
    (VOID)KiArm64ConnectInterrupt(&KiArm64IpiInterrupt);
    KiRawDebugPuts("[KeInitInterrupts] IPI connect done\n");

    /* CPU interrupt delivery remains masked until HAL phase 0 completes. */
    KeStartArm64ProcessorTimer();
    KiRawDebugPuts("[KeInitInterrupts] EXIT\n");
}

/*
 * KeReenableTimerInterrupt - Re-enable the timer PPI after HAL initialization
 *
 * BACKGROUND:
 * The timer PPI (Private Peripheral Interrupt) is initially connected via
 * KeConnectInterrupt during early kernel initialization (KeInitInterrupts).
 * However, at that point, the HAL's GIC initialization hasn't run yet, so
 * HalpGicUseSysRegs is FALSE. This causes HalEnableSystemInterrupt to take
 * the wrong code path and the PPI is not actually enabled in the GIC.
 *
 * After HalInitSystem(0) completes, HalpGicUseSysRegs is properly set and
 * the GIC redistributor is configured. This function re-enables the timer
 * PPI to ensure it's properly routed through the GIC.
 *
 * This fixes the "5-second timer gap" bug where the timer would only fire
 * sporadically during USB enumeration because the PPI wasn't properly enabled.
 */
VOID
NTAPI
KeReenableTimerInterrupt(VOID)
{
    ULONG TimerIntId = KiArm64ClockTimerIntId();

    /*
     * Re-enable the timer interrupt in the GIC.
     * Now that HalInitSystem(0) has run, HalpGicUseSysRegs is properly set
     * and the GIC redistributor is configured. This call will properly
     * enable the PPI in the redistributor's ISENABLER0 register.
     */
    HalEnableSystemInterrupt(TimerIntId, CLOCK_LEVEL, LevelSensitive);
    KiArm64StartLocalTimer();
    ASSERT(KeGetCurrentProcessorNumber() == 0);
    if (InterlockedExchange(&KiArm64DynamicTickEnabled, 1) == 0)
        DPRINT1("[clock] Dynamic tick enabled: idle processors stop their clock interrupt, the clock owner sleeps up to %I64u ms\n",
                KI_ARM64_MAXIMUM_IDLE_CLOCK_INTERVAL / 10000);
}

static
BOOLEAN
KiArm64CallInterruptServiceRoutine(
    _In_ PKINTERRUPT Interrupt)
{
    BOOLEAN AcquireLock = (Interrupt->ActualLock != KI_ARM64_INTERRUPT_LOCK_NONE);
    BOOLEAN Handled;
    KIRQL OldIrql = KeGetCurrentIrql();
    BOOLEAN RaiseIrql = (OldIrql < Interrupt->SynchronizeIrql);

    if (RaiseIrql) KfRaiseIrql(Interrupt->SynchronizeIrql);
    if (AcquireLock) KxAcquireSpinLock(Interrupt->ActualLock);
    Handled = Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
    if (AcquireLock) KxReleaseSpinLock(Interrupt->ActualLock);
    if (RaiseIrql) KfLowerIrql(OldIrql);
    return Handled;
}

static
VOID
KiArm64DispatchChain(_In_ PKINTERRUPT Head)
{
    PKINTERRUPT Interrupt;
    PLIST_ENTRY ListHead, NextEntry;
    BOOLEAN Handled = FALSE;

    ListHead = &Head->InterruptListEntry;

    if (IsListEmpty(ListHead))
    {
        /* Single ISR */
        (VOID)KiArm64CallInterruptServiceRoutine(Head);
        return;
    }

    /* Chained ISR list (parity with i386 path) */
    NextEntry = ListHead; /* head is an entry */
    Interrupt = Head;

    for (;;)
    {
        Handled = KiArm64CallInterruptServiceRoutine(Interrupt);

        if ((Handled) && (Interrupt->Mode == LevelSensitive)) break;

        NextEntry = NextEntry->Flink;
        if (NextEntry == ListHead)
        {
            if (Interrupt->Mode == LevelSensitive) break;
            if (!Handled) break;
        }

        Interrupt = CONTAINING_RECORD(NextEntry, KINTERRUPT, InterruptListEntry);
    }
}

static
VOID
KiArm64DispatchForeignInterrupts(
    _In_ ULONG IntId,
    _In_ ULONG Cpu,
    _Inout_updates_(KI_ARM64_FOREIGN_BATCH) PKINTERRUPT *Batch,
    _In_ ULONG Count,
    _In_ BOOLEAN More)
{
    ULONG Index, Skip = 0;

    ASSERT(Count != 0 && Count <= KI_ARM64_FOREIGN_BATCH);

    for (;;)
    {
        for (Index = 0; Index < Count; Index++)
        {
            ASSERT(Batch[Index]->Irql <= KeGetCurrentIrql());
            if (KiArm64CallInterruptServiceRoutine(Batch[Index]) &&
                (Batch[Index]->Mode == LevelSensitive))
            {
                return;
            }
        }

        if (!More)
            return;

        Skip += Count;
        _disable();
        Count = KiArm64CollectForeignInterrupts(IntId, Cpu, Skip, Batch, &More);
        _enable();
    }
}

static BOOLEAN KiSecondaryInterruptServicesEnabled;

NTSTATUS
NTAPI
KeInitializeSecondaryInterruptServices(VOID)
{
    KiSecondaryInterruptServicesEnabled = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
KeDispatchSecondaryInterrupt(
    _In_ ULONG Vector,
    _In_ ULONG_PTR Flags,
    _In_opt_ PVOID Reserved)
{
    ULONG Cpu;
    KIRQL OldIrql;
    PKINTERRUPT Head;
    BOOLEAN Connected;
    BOOLEAN IsLevel;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Reserved);

    if (!KiSecondaryInterruptServicesEnabled || Vector >= ARM64_MAX_INTID)
        return FALSE;
    if (KiDispatchPassiveInterrupt(Vector, &IsLevel))
        return !IsLevel;

    OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    Cpu = KeGetCurrentProcessorNumber();
    ASSERT(Cpu < MAXIMUM_PROCESSORS);
    InterlockedIncrement(&KiArm64DispatchDepth[Cpu].Value);
    Head = KiArm64LoadInterruptHeadNoFence(Vector, Cpu);
    Connected = (Head != NULL);
    if (Connected) KiArm64DispatchChain(Head);
    InterlockedDecrement(&KiArm64DispatchDepth[Cpu].Value);
    KfLowerIrql(OldIrql);
    return Connected;
}

static
ULONG
KiArm64SoftwareInterrupt(_In_ ULONG IntId)
{
    KIRQL Level = (IntId == ARM64_SGI_DPC) ? DISPATCH_LEVEL : APC_LEVEL;
    KIRQL OldIrql;

    if (!HalBeginSystemInterrupt(Level, IntId, &OldIrql))
        return 0;

    KeGetCurrentPrcb()->InterruptCount++;

    /*
     * Acknowledge the SGI while still on the per-processor ISR stack, but do
     * not run dispatcher/APC code here.  Either path can switch threads and
     * therefore must execute after the assembly wrapper has restored the
     * interrupted thread's kernel stack.  This is the same stack ownership
     * split used by Windows ARM64's KiSwitchStackAndPlayInterrupt path.
     */
    HalEndSystemInterrupt(OldIrql, NULL);
    return (IntId == ARM64_SGI_DPC) ? ARM64_INTERRUPT_EXIT_DPC : ARM64_INTERRUPT_EXIT_APC;
}

VOID
KiArm64InterruptDispatchExit(
    _In_ ULONG ExitWork,
    _In_ ULONG VectorId,
    _Inout_ PKI_ARM64_IRQ_FRAME IrqFrame)
{
    PKIPCR Pcr;
    PKPRCB Prcb;
    PKTHREAD Thread;
    KIRQL OldIrql;

    ASSERT((ExitWork & ~(ARM64_INTERRUPT_EXIT_APC | ARM64_INTERRUPT_EXIT_DPC)) == 0);
    ASSERT(VectorId < 16);
    ASSERT(IrqFrame != NULL);

    /*
     * A nested SGI has already been acknowledged by the GIC, but its logical
     * pending bit remains set until the outermost interrupt returns to the
     * interrupted thread stack.
     */
    Pcr = KeGetPcr();
    if (Pcr != NULL)
    {
        if (Pcr->DispatchInterrupt != 0)
            ExitWork |= ARM64_INTERRUPT_EXIT_DPC;
        if (Pcr->ApcInterrupt != 0)
            ExitWork |= ARM64_INTERRUPT_EXIT_APC;
    }

    if (ExitWork & ARM64_INTERRUPT_EXIT_DPC)
    {
        OldIrql = KeGetCurrentIrql();
        if (OldIrql < DISPATCH_LEVEL)
        {
            Prcb = KeGetCurrentPrcb();
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            if (Prcb != NULL)
                KiClearDpcInterruptRequested(Prcb);

            KfRaiseIrql(DISPATCH_LEVEL);
            _enable();
            KiDispatchInterrupt();
            _disable();
            KfLowerIrql(OldIrql);
        }
    }

    if (ExitWork & ARM64_INTERRUPT_EXIT_APC)
    {
        OldIrql = KeGetCurrentIrql();
        Thread = KeGetCurrentThread();
        if ((OldIrql < APC_LEVEL) &&
            (Thread != NULL) &&
            (KiIsKernelApcDeliverable(Thread, OldIrql) ||
             ((VectorId >= 8) && Thread->ApcState.UserApcPending)))
        {
            HalClearSoftwareInterrupt(APC_LEVEL);
            KfRaiseIrql(APC_LEVEL);
            if (VectorId >= 8)
            {
                KiArm64DeliverInterruptApc(VectorId, IrqFrame, OldIrql);
            }
            else
            {
                _enable();
                KiDeliverApc(KernelMode, NULL, Thread->TrapFrame);
                _disable();
            }
            KfLowerIrql(OldIrql);
        }
    }
}

static ULONG
KiArm64InterruptDispatchEntryInner(_In_ ULONG VectorId, _In_ PKI_ARM64_IRQ_FRAME IrqFrame)
{
    ULONG IntId;
    ULONG Cpu;
    KIRQL OldIrql;
    KIRQL RequestIrql = DISPATCH_LEVEL;
    PKINTERRUPT Head;
    PKINTERRUPT Foreign[KI_ARM64_FOREIGN_BATCH];
    ULONG ForeignCount = 0;
    BOOLEAN ForeignMore = FALSE;
    BOOLEAN ForeignDispatch = FALSE;
    KTRAP_FRAME LocalTrapFrame;
    PKTRAP_FRAME SavedTrapFrame;
    BOOLEAN Begun;

    /* Ask HAL for current INTID */
    IntId = HalGetInterruptSource();
    IrqFrame->Reserved2 = IntId;

    if ((IntId == ARM64_SGI_APC) || (IntId == ARM64_SGI_DPC))
    {
        return KiArm64SoftwareInterrupt(IntId);
    }

    if (IntId == ARM64_SGI_FREEZE)
    {
        /*
         * Enter the interrupt through the normal HAL path so the acknowledged
         * SGI is pushed on the active-INTID stack before it is completed.
         * Calling HalPerformEndOfInterrupt directly here can otherwise pop an
         * interrupted device/timer entry, or decode the HAL's raw fallback as
         * an intid-plus-one value and EOI the wrong SGI.
         */
        if (HalBeginSystemInterrupt(HIGH_LEVEL, IntId, &OldIrql))
        {
            KeGetCurrentPrcb()->InterruptCount++;
            HalEndSystemInterrupt(OldIrql, NULL);
        }
        return 0;
    }

    if (IntId == ARM64_SGI_IPI)
    {
        PKPRCB Prcb;
        PKI_ARM64_FREEZE_FRAMES FreezeFrames;

        /* Service the reschedule/generic-call IPI directly: KiIpiServiceRoutine is
         * lockless, and taking the shared chain lock here can deadlock KeIpiGenericCall. */
        if (HalBeginSystemInterrupt(IPI_LEVEL, IntId, &OldIrql))
        {
            Prcb = KeGetCurrentPrcb();
            Prcb->InterruptCount++;
            if ((Prcb != NULL) && (Prcb->IpiFrozen == IPI_FROZEN_STATE_TARGET_FREEZE))
            {
                Cpu = Prcb->Number;
                ASSERT(Cpu < MAXIMUM_PROCESSORS);
                FreezeFrames = &KiArm64FreezeFrames[Cpu];
                KiArm64BuildInterruptFrames(VectorId, OldIrql, IrqFrame, &FreezeFrames->TrapFrame, &FreezeFrames->ExceptionFrame, &FreezeFrames->VfpState);
                KiIpiServiceRoutine(&FreezeFrames->TrapFrame, &FreezeFrames->ExceptionFrame);
                _disable();
                KiArm64RestoreInterruptFrames(VectorId, IrqFrame, &FreezeFrames->TrapFrame, &FreezeFrames->ExceptionFrame, &FreezeFrames->VfpState);
            }
            else
            {
                _enable();
                KiIpiServiceRoutine(NULL, NULL);
                _disable();
            }
            HalEndSystemInterrupt(OldIrql, NULL);
        }
        return 0;
    }

    Cpu = KeGetCurrentProcessorNumber();
    ASSERT(Cpu < MAXIMUM_PROCESSORS);
    InterlockedIncrement(&KiArm64DispatchDepth[Cpu].Value);
    Head = (IntId < ARM64_MAX_INTID) ? KiArm64LoadInterruptHeadNoFence(IntId, Cpu) : NULL;
    if (Head != NULL)
    {
        RequestIrql = Head->Irql;
    }
    else if ((IntId >= KI_ARM64_SPI_BASE) && (IntId < KI_ARM64_SPI_LIMIT) &&
             (KeGetCurrentIrql() < HIGH_LEVEL))
    {
        InterlockedIncrement(&KiArm64ForeignDispatchDepth[Cpu].Value);
        ForeignDispatch = TRUE;
        ForeignCount = KiArm64CollectForeignInterrupts(IntId, Cpu, 0, Foreign, &ForeignMore);
        if (ForeignCount != 0)
            RequestIrql = Foreign[0]->Irql;
    }

    Begun = HalBeginSystemInterrupt(RequestIrql, IntId, &OldIrql);
    if (!Begun)
    {
        if (ForeignDispatch)
            InterlockedDecrement(&KiArm64ForeignDispatchDepth[Cpu].Value);
        InterlockedDecrement(&KiArm64DispatchDepth[Cpu].Value);
        return 0;
    }

    /* The clock path accounts for its interrupt in KeUpdateSystemTime/RunTime. */
    if (Head != &KiArm64TimerInterrupt[Cpu])
        KeGetCurrentPrcb()->InterruptCount++;

    /*
     * The clock path consumes PreviousMode/SavedIrql for accounting and Pc
     * for profiling.  Keep the control state coherent with the architectural
     * IRQ frame without zeroing/copying the unused 340+ bytes on every tick.
     * SavedIrql, not PreviousIrql: the Win11 layout overlays PreviousIrql with
     * PreviousMode at 0x003, and PreviousMode is live here.
     */
    LocalTrapFrame.PreviousMode = (VectorId >= 8) ? UserMode : KernelMode;
    LocalTrapFrame.SavedIrql = OldIrql;
    LocalTrapFrame.Spsr = (ULONG)IrqFrame->Spsr;
    LocalTrapFrame.Sp = (VectorId >= 8) ? IrqFrame->SpEl0 : (ULONG_PTR)(IrqFrame + 1);
    LocalTrapFrame.Lr = IrqFrame->Lr;
    LocalTrapFrame.Fp = IrqFrame->Fp;
    LocalTrapFrame.Pc = IrqFrame->Elr;
    SavedTrapFrame = KiArm64CurrentInterruptTrapFrame[Cpu];
    KiArm64CurrentInterruptTrapFrame[Cpu] = &LocalTrapFrame;

    if (Head != NULL)
    {
        /* Deassert the level-sensitive timer before nested IRQs are enabled. */
        if (Head == &KiArm64TimerInterrupt[Cpu]) KiArm64AcknowledgeClockInterrupt(Cpu);
        _enable();
        if (KiCpuAuditActive && Cpu < CPU_AUDIT_CPUS && IntId < CPU_AUDIT_IRQS)
            KiCpuAuditIrqs[Cpu][IntId].HandlerCalls++;
        KiArm64DispatchChain(Head);
        _disable();
    }
    else if (ForeignCount != 0)
    {
        _enable();
        KiArm64DispatchForeignInterrupts(IntId, Cpu, Foreign, ForeignCount, ForeignMore);
        _disable();
    }

    KiArm64CurrentInterruptTrapFrame[Cpu] = SavedTrapFrame;
    if (ForeignDispatch)
        InterlockedDecrement(&KiArm64ForeignDispatchDepth[Cpu].Value);
    InterlockedDecrement(&KiArm64DispatchDepth[Cpu].Value);

    HalEndSystemInterrupt(OldIrql, NULL);

    return 0;
}

ULONG
KiArm64InterruptDispatchEntry(ULONG VectorId, PKI_ARM64_IRQ_FRAME Frame)
{
    ULONG Cpu = KeGetCurrentProcessorNumber(), Result;
    ULONGLONG Begin = KiCpuAuditActive ? KiCpuAuditClock() : 0;
    Frame->Reserved0 = 0;
    Result = KiArm64InterruptDispatchEntryInner(VectorId, Frame);
    if (Begin && Cpu < CPU_AUDIT_CPUS && Frame->Reserved2 < CPU_AUDIT_IRQS)
    {
        CPU_AUDIT_IRQ *Irq = &KiCpuAuditIrqs[Cpu][Frame->Reserved2];
        ULONGLONG Elapsed = KiCpuAuditClock() - Begin;
        Irq->Count++; Irq->DispatchTicks += Elapsed;
        if (Elapsed > Irq->MaximumTicks) Irq->MaximumTicks = Elapsed;
        Frame->Reserved0 = (ULONG_PTR)Irq;
    }
    return Result;
}

/*
 * KINTERRUPT support (connect/disconnect/synchronize)
 */

/*
 * Dispatch reads only the current processor's table. Connect/disconnect run
 * on that processor before taking KiArm64IntTableLock, so raising IRQL here
 * excludes both hardware and secondary dispatch without stopping other CPUs.
 * A handler already in progress must have returned before its CPU can run us.
 */
static
VOID
KiArm64SynchronizeInterruptChain(_In_ PKINTERRUPT Interrupt, _In_ BOOLEAN Connect)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    PKINTERRUPT *Table = KiArm64IntTables[Cpu];
    PKINTERRUPT Head;
    KIRQL OldIrql;

    ASSERT(Cpu == (UCHAR)Interrupt->Number);
    ASSERT(ReadAcquire(&KiArm64DispatchDepth[Cpu].Value) == 0);

    OldIrql = KfRaiseIrql(HIGH_LEVEL);
    KxAcquireSpinLock(&KiArm64ForeignDispatchLock);
    Head = Table[Interrupt->Vector];
    if (Connect)
    {
        InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
    }
    else
    {
        if (IsListEmpty(&Head->InterruptListEntry))
            Table[Interrupt->Vector] = NULL;
        else
        {
            if (Head == Interrupt)
                Table[Interrupt->Vector] = CONTAINING_RECORD(Head->InterruptListEntry.Flink, KINTERRUPT, InterruptListEntry);
            RemoveEntryList(&Interrupt->InterruptListEntry);
        }
    }
    Interrupt->Connected = Connect;
    KeMemoryBarrier();
    KxReleaseSpinLock(&KiArm64ForeignDispatchLock);
    KfLowerIrql(OldIrql);
}

VOID
NTAPI
KeInitializeInterrupt(IN PKINTERRUPT Interrupt,
                      IN PKSERVICE_ROUTINE ServiceRoutine,
                      IN PVOID ServiceContext,
                      IN PKSPIN_LOCK SpinLock,
                      IN ULONG Vector,
                      IN KIRQL Irql,
                      IN KIRQL SynchronizeIrql,
                      IN KINTERRUPT_MODE InterruptMode,
                      IN BOOLEAN ShareVector,
                      IN CHAR ProcessorNumber,
                      IN BOOLEAN FloatingSave)
{
    Interrupt->Type = InterruptObject;
    Interrupt->Size = sizeof(KINTERRUPT);
    if (!SpinLock) SpinLock = &Interrupt->SpinLock;
    KeInitializeSpinLock(&Interrupt->SpinLock);

    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->ActualLock = SpinLock;
    Interrupt->Vector = Vector;
    Interrupt->Irql = Irql;
    Interrupt->SynchronizeIrql = SynchronizeIrql;
    Interrupt->Mode = InterruptMode;
    Interrupt->ShareVector = ShareVector;
    Interrupt->Number = ProcessorNumber;
    Interrupt->FloatingSave = FloatingSave;

    Interrupt->TickCount = 0;
    Interrupt->Connected = FALSE;
    Interrupt->ServiceCount = 0;
    Interrupt->DispatchCount = 0;
    Interrupt->DispatchAddress = NULL;

    InitializeListHead(&Interrupt->InterruptListEntry);
}

static
BOOLEAN
KiArm64ConnectInterrupt(IN PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;
    PKINTERRUPT Head;
    PKINTERRUPT *Table;
    ULONG Vector = Interrupt->Vector;
    ULONG Cpu = (UCHAR)Interrupt->Number;
    KAFFINITY PreviousAffinity = 0;
    BOOLEAN RestoreAffinity = FALSE;
    BOOLEAN Connected;

    if (Vector >= ARM64_MAX_INTID || Cpu >= MAXIMUM_PROCESSORS) return FALSE;
    if (Interrupt->Irql == PASSIVE_LEVEL) return KiConnectPassiveInterrupt(Interrupt);
    Table = KiArm64IntTables[Cpu];
    if (!Table) return FALSE;

    /* Pin before locking: every vector's dispatch table belongs to its CPU. */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
        RestoreAffinity = TRUE;
    }
    else if (Cpu != KeGetCurrentProcessorNumber())
        return FALSE;

    KeAcquireSpinLock(&KiArm64IntTableLock, &OldIrql);
    if (Interrupt->Connected) goto Done;

    Head = Table[Vector];
    if (!Head)
    {
        InitializeListHead(&Interrupt->InterruptListEntry);
        KeMemoryBarrier();
        Table[Vector] = Interrupt;
        HalArm64ConnectInterrupt(Vector);
        if ((Vector >= 32 && KiArm64IntConnections[Vector] != 0) ||
            HalEnableSystemInterrupt(Vector, Interrupt->Irql, Interrupt->Mode))
        {
            KiArm64IntConnections[Vector]++;
            Interrupt->Connected = TRUE;
        }
        else
        {
            HalArm64DisconnectInterrupt(Vector);
            KiArm64SynchronizeInterruptChain(Interrupt, FALSE);
        }
    }
    else
    {
        if ((Interrupt->ShareVector == 0) || (Head->ShareVector == 0) ||
            (Interrupt->Mode != Head->Mode))
        {
            Interrupt->Connected = FALSE;
        }
        else
        {
            KiArm64SynchronizeInterruptChain(Interrupt, TRUE);
        }
    }

Done:
    Connected = Interrupt->Connected;
    KeReleaseSpinLock(&KiArm64IntTableLock, OldIrql);
    if (!Connected) KiArm64WaitForForeignDispatch();
    if (RestoreAffinity) KeRevertToUserAffinityThreadEx(PreviousAffinity);

    return Connected;
}

BOOLEAN
NTAPI
KeConnectInterrupt(IN PKINTERRUPT Interrupt)
{
    if (Interrupt->Vector < ARM64_SGI_COUNT) return FALSE;
    return KiArm64ConnectInterrupt(Interrupt);
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(IN PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;
    PKINTERRUPT Head;
    ULONG Vector = Interrupt->Vector;
    ULONG Cpu = (UCHAR)Interrupt->Number;
    KAFFINITY PreviousAffinity = 0;
    BOOLEAN RestoreAffinity = FALSE;
    BOOLEAN Connected;

    if (Vector >= ARM64_MAX_INTID || Cpu >= MAXIMUM_PROCESSORS) return FALSE;
    if (Interrupt->Irql == PASSIVE_LEVEL) return KiDisconnectPassiveInterrupt(Interrupt);
    if (!KiArm64IntTables[Cpu]) return FALSE;
    /* Wait for the owning CPU at the caller's IRQL, before taking the lock. */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
        RestoreAffinity = TRUE;
    }
    else if (Cpu != KeGetCurrentProcessorNumber())
        return FALSE;
    KeAcquireSpinLock(&KiArm64IntTableLock, &OldIrql);
    Head = KiArm64IntTables[Cpu][Vector];
    Connected = Interrupt->Connected;
    if (!Head || !Connected)
        goto Done;

    if (IsListEmpty(&Head->InterruptListEntry))
    {
        /* Single interrupt case */
        ASSERT(Head == Interrupt);
        ASSERT(KiArm64IntConnections[Vector] != 0);
        KiArm64IntConnections[Vector]--;
        HalArm64DisconnectInterrupt(Vector);
        if (Vector < 32 || KiArm64IntConnections[Vector] == 0)
            HalDisableSystemInterrupt(Vector, Interrupt->Irql);
    }
    KiArm64SynchronizeInterruptChain(Interrupt, FALSE);

Done:
    KeReleaseSpinLock(&KiArm64IntTableLock, OldIrql);
    if (Connected) KiArm64WaitForForeignDispatch();
    if (RestoreAffinity) KeRevertToUserAffinityThreadEx(PreviousAffinity);
    return Connected;
}

BOOLEAN
NTAPI
KeSynchronizeExecution(IN OUT PKINTERRUPT Interrupt,
                       IN PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
                       IN PVOID SynchronizeContext OPTIONAL)
{
    BOOLEAN Success;
    KIRQL OldIrql;
    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiSynchronizePassiveInterrupt(Interrupt, SynchronizeRoutine, SynchronizeContext);
    OldIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
    Success = SynchronizeRoutine(SynchronizeContext);
    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
    KeLowerIrql(OldIrql);
    return Success;
}

/* Legacy alias retained for parity with other arches */
VOID KiUnexpectedInterrupt(VOID)
{
    KeBugCheck(TRAP_CAUSE_UNKNOWN);
}
