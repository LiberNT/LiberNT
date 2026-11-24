#pragma once

#define KI_EMULATED_USER_MACHINE 0
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 1
#define KI_EMPTY_YIELD_STALL_NS 700
#define KiEmulationProcessorArchitecture(Process) KeProcessorArchitecture

#include "intrin_i.h"

#define KeGetCurrentProcessorNumber() KeGetCurrentProcessorIndex()

#define DR_MASK(x)                              (1 << (x))
#define DR_REG_MASK                             0x4F

#define KD_BREAKPOINT_TYPE        UCHAR
#define KD_BREAKPOINT_SIZE        sizeof(UCHAR)
#define KD_BREAKPOINT_VALUE       0xCC

#define X86_FEATURE_SSE3        0x00000001
#define X86_FEATURE_SSSE3       0x00000200
#define X86_FEATURE_SSE4_1      0x00080000
#define X86_FEATURE_SSE4_2      0x00100000
#define X86_FEATURE_XSAVE       0x04000000
#define X86_FEATURE_RDRAND      0x40000000

#define X86_FEATURE_VME         0x00000002
#define X86_FEATURE_PSE         0x00000008
#define X86_FEATURE_TSC         0x00000010
#define X86_FEATURE_PAE         0x00000040
#define X86_FEATURE_CX8         0x00000100
#define X86_FEATURE_APIC        0x00000200
#define X86_FEATURE_SYSCALL     0x00000800
#define X86_FEATURE_MTRR        0x00001000
#define X86_FEATURE_PGE         0x00002000
#define X86_FEATURE_CMOV        0x00008000
#define X86_FEATURE_PAT         0x00010000
#define X86_FEATURE_DS          0x00200000
#define X86_FEATURE_MMX         0x00800000
#define X86_FEATURE_FXSR        0x01000000
#define X86_FEATURE_SSE         0x02000000
#define X86_FEATURE_SSE2        0x04000000
#define X86_FEATURE_HT          0x10000000

#define X86_FEATURE_NX          0x00100000

FORCEINLINE
ULONG_PTR
KeGetContextPc(PCONTEXT Context)
{
    return Context->Eip;
}

FORCEINLINE
VOID
KeSetContextPc(PCONTEXT Context, ULONG_PTR ProgramCounter)
{
    Context->Eip = ProgramCounter;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(PCONTEXT Context, ULONG_PTR ReturnValue)
{
    Context->Eax = ReturnValue;
}

FORCEINLINE
ULONG_PTR
KeGetContextServiceArgument3(PCONTEXT Context)
{
    return Context->Ebx;
}

FORCEINLINE
ULONG_PTR
KeGetContextServiceArgument4(PCONTEXT Context)
{
    return Context->Edi;
}

FORCEINLINE
ULONG_PTR
KeGetContextStackRegister(PCONTEXT Context)
{
    return Context->Esp;
}

FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(PCONTEXT Context)
{
    return Context->Ebp;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(PCONTEXT Context, ULONG_PTR Frame)
{
    Context->Ebp = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFramePc(PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Eip;
}

FORCEINLINE
PKTRAP_FRAME
KiGetLinkedTrapFrame(PKTRAP_FRAME TrapFrame)
{
    return (PKTRAP_FRAME)TrapFrame->Edx;
}

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
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(TrapFrame);
    return ExceptionFrame;
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

#define KeGetTrapFrame(Thread) \
    (PKTRAP_FRAME)((ULONG_PTR)((Thread)->InitialStack) - \
                   sizeof(KTRAP_FRAME) - \
                   sizeof(FX_SAVE_AREA))

#define KeGetExceptionFrame(Thread) \
    NULL

#define KiArchSetGuiServiceTable(Thread) \
    ((VOID)((Thread)->Tcb.ServiceTable = KeServiceDescriptorTableShadow))

#define KiArchSetInitialServiceTable(Thread) \
    ((VOID)((Thread)->ServiceTable = KeServiceDescriptorTable))

CODE_SEG("INIT")
NTSTATUS
NTAPI
KiI386PublishSystemCallStubs(
    _In_ PVOID DllBase);

/* int3 traps with EIP past the one-byte instruction */
#define KI_BREAKPOINT_PC_ADJUST 1

/* KiUserCallbackDispatcher keeps its SEH registration in the last two words */
typedef struct _KI_USER_CALLOUT_FRAME32
{
    ULONG ReturnAddress;
    ULONG ApiNumber;
    PVOID Buffer;
    ULONG Length;
    ULONG ExceptionRegistration[2];
} KI_USER_CALLOUT_FRAME32;
C_ASSERT(sizeof(KI_USER_CALLOUT_FRAME32) == 24);

#define KI_USER_CALLOUT_FRAME KI_USER_CALLOUT_FRAME32

VOID
KiI386PrepareInterruptDispatch(
    _Inout_ PKINTERRUPT Interrupt);
#define KiArchPrepareInterruptDispatch(Interrupt) KiI386PrepareInterruptDispatch(Interrupt)
#define KI_CALLOUT_RESULT(Frame) ((Frame)->Result)
#define KI_CALLOUT_RESULT_LENGTH(Frame) ((Frame)->ResultLength)
#define KI_CALLOUT_RETURN_STACK(Frame) (&(Frame)->Edi)
#define KiArchCalloutSaveUserState(Thread) ((ULONG_PTR)((PTEB)(Thread)->Teb)->NtTib.ExceptionList)
#define KiArchCalloutRestoreUserState(Thread, State) \
    ((VOID)(((PTEB)(Thread)->Teb)->NtTib.ExceptionList = (PEXCEPTION_REGISTRATION_RECORD)(State)))

ULONG
KiI386ExceptionContextFlags(
    _In_ KPROCESSOR_MODE PreviousMode);
#define KiArchExceptionContextFlags(PreviousMode) KiI386ExceptionContextFlags(PreviousMode)

/* The frame is marked so the trap exit path treats it as an exception frame */
#define KiArchBeginUserExceptionDispatch(TrapFrame) ((VOID)((TrapFrame)->PreviousPreviousMode |= KI_EXCEPTION_ACTIVE_FRAME))

#define KiArchPublishSystemCallStubs(DllBase) \
    KiI386PublishSystemCallStubs(DllBase)

#define KeGetContextSwitches(Prcb)  \
    CONTAINING_RECORD(Prcb, KIPCR, PrcbData)->ContextSwitches

#define KiPrcbGroup(Prcb) ((VOID)(Prcb), (USHORT)0)

#define KiPrcbPoolLookaside(Prcb, Paged, Index) ((VOID)(Prcb), (VOID)(Paged), (VOID)(Index), (PGENERAL_LOOKASIDE_POOL)NULL)

#define ReadTimeStampCounter() ((ULONG64)__rdtsc())

#define PFX_FLAG_ES                0x00000100
#define PFX_FLAG_CS                0x00000200
#define PFX_FLAG_SS                0x00000400
#define PFX_FLAG_DS                0x00000800
#define PFX_FLAG_FS                0x00001000
#define PFX_FLAG_GS                0x00002000
#define PFX_FLAG_OPER32            0x00004000
#define PFX_FLAG_ADDR32            0x00008000
#define PFX_FLAG_LOCK              0x00010000
#define PFX_FLAG_REPNE             0x00020000
#define PFX_FLAG_REP               0x00040000

extern const PULONG KiNtVdmState;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
FORCEINLINE ULONG KiVdmSetVdmEFlags(ULONG EFlags)
{
    return InterlockedOr((PLONG)KiNtVdmState, EFlags);
}
FORCEINLINE ULONG KiVdmClearVdmEFlags(ULONG EFlags)
{
    return InterlockedAnd((PLONG)KiNtVdmState, ~EFlags);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define KiCallVdmHandler(x)         KiVdmOpcode##x(TrapFrame, Flags)
#define KiCallVdmPrefixHandler(x)   KiVdmOpcodePrefix(TrapFrame, Flags | x)
#define KiVdmUnhandledOpcode(x)                     \
    BOOLEAN                                         \
    FASTCALL                                        \
    KiVdmOpcode##x(IN PKTRAP_FRAME TrapFrame,       \
                   IN ULONG Flags)                  \
    {                                               \
        UNIMPLEMENTED_DBGBREAK();                   \
        return FALSE;                               \
    }

C_ASSERT(NPX_FRAME_LENGTH == sizeof(FX_SAVE_AREA));

typedef struct _KV86_FRAME
{
    PVOID ThreadStack;
    PVOID ThreadTeb;
    PVOID PcrTeb;
} KV86_FRAME, *PKV86_FRAME;

typedef struct _KV8086_STACK_FRAME
{
    KTRAP_FRAME TrapFrame;
    FX_SAVE_AREA NpxArea;
    KV86_FRAME V86Frame;
} KV8086_STACK_FRAME, *PKV8086_STACK_FRAME;

typedef struct _LARGE_IDENTITY_MAP
{
    PHARDWARE_PTE TopLevelDirectory;
    ULONG Cr3;
    ULONG_PTR StartAddress;
    ULONG PagesCount;
    PVOID PagesList[30];
} LARGE_IDENTITY_MAP, *PLARGE_IDENTITY_MAP;

typedef struct _FLOATING_SAVE_CONTEXT
{
    PKTHREAD CurrentThread;
    KIRQL OldNpxIrql;
    PFX_SAVE_AREA Buffer;
    PFX_SAVE_AREA PfxSaveArea;
} FLOATING_SAVE_CONTEXT, *PFLOATING_SAVE_CONTEXT;

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG Flags;
    BOOLEAN Return;

    Flags = __readeflags();
    Return = (Flags & EFLAGS_INTERRUPT_MASK) ? TRUE: FALSE;

    _disable();
    return Return;
}

FORCEINLINE
VOID
KeRestoreInterrupts(BOOLEAN WereEnabled)
{
    if (WereEnabled) _enable();
}

FORCEINLINE
VOID
KeRegisterInterruptHandler(IN ULONG Vector,
                           IN PVOID Handler)
{
    UCHAR Entry;
    ULONG_PTR Address;
    PKIPCR Pcr = (PKIPCR)KeGetPcr();

    Entry = HalVectorToIDTEntry(Vector);
    Address = PtrToUlong(Handler);

    Pcr->IDT[Entry].ExtendedOffset = (USHORT)(Address >> 16);
    Pcr->IDT[Entry].Offset = (USHORT)Address;
}

FORCEINLINE
PVOID
KeQueryInterruptHandler(IN ULONG Vector)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    UCHAR Entry;

    Entry = HalVectorToIDTEntry(Vector);

    return (PVOID)(((Pcr->IDT[Entry].ExtendedOffset << 16) & 0xFFFF0000) |
                    (Pcr->IDT[Entry].Offset & 0xFFFF));
}

FORCEINLINE
VOID
KeInvalidateTlbEntry(IN PVOID Address)
{
    __invlpg(Address);
}

FORCEINLINE
VOID
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(FlushSize);
}

FORCEINLINE
PRKTHREAD
KeGetCurrentThread(VOID)
{
    return ((PKIPCR)KeGetPcr())->PrcbData.CurrentThread;
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializePcr(IN ULONG ProcessorNumber,
                IN PKIPCR Pcr,
                IN PKIDTENTRY Idt,
                IN PKGDTENTRY Gdt,
                IN PKTSS Tss,
                IN PKTHREAD IdleThread,
                IN PVOID DpcStack);

FORCEINLINE
VOID
Ke386SetGdtEntryBase(PKGDTENTRY GdtEntry, PVOID BaseAddress)
{
    GdtEntry->BaseLow = (USHORT)((ULONG_PTR)BaseAddress & 0xFFFF);
    GdtEntry->HighWord.Bytes.BaseMid = (UCHAR)((ULONG_PTR)BaseAddress >> 16);
    GdtEntry->HighWord.Bytes.BaseHi = (UCHAR)((ULONG_PTR)BaseAddress >> 24);
}

FORCEINLINE
VOID
KiSetTebBase(PKPCR Pcr, PNT_TIB TebAddress)
{
    Pcr->NtTib.Self = TebAddress;
    Ke386SetGdtEntryBase(&Pcr->GDT[KGDT_R3_TEB / sizeof(KGDTENTRY)], TebAddress);
}

CODE_SEG("INIT")
VOID
FASTCALL
Ki386InitializeTss(
    IN PKTSS Tss,
    IN PKIDTENTRY Idt,
    IN PKGDTENTRY Gdt
);

CODE_SEG("INIT")
VOID
NTAPI
KiSetCR0Bits(VOID);

CODE_SEG("INIT")
VOID
NTAPI
KiGetCacheInformation(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
KiIsNpxErrataPresent(
    VOID
);

CODE_SEG("INIT")
VOID
NTAPI
KiSetProcessorType(VOID);

CODE_SEG("INIT")
ULONG64
NTAPI
KiGetFeatureBits(VOID);

#if DBG
CODE_SEG("INIT")
VOID
KiReportCpuFeatures(VOID);
#endif

VOID
NTAPI
KiThreadStartup(VOID);

NTSTATUS
NTAPI
Ke386GetGdtEntryThread(
    IN PKTHREAD Thread,
    IN ULONG Offset,
    IN PKGDTENTRY Descriptor
);

VOID
NTAPI
KiFlushNPXState(
    IN FLOATING_SAVE_AREA *SaveArea
);

VOID
NTAPI
Ki386AdjustEsp0(
    IN PKTRAP_FRAME TrapFrame
);

VOID
NTAPI
Ki386SetupAndExitToV86Mode(
    OUT PTEB VdmTeb
);

CODE_SEG("INIT")
VOID
NTAPI
KeI386VdmInitialize(
    VOID
);

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableGlobalPage(
    IN ULONG_PTR Context
);

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableTargetLargePage(
    IN ULONG_PTR Context
);

BOOLEAN
NTAPI
Ki386CreateIdentityMap(
    IN PLARGE_IDENTITY_MAP IdentityMap,
    IN PVOID StartPtr,
    IN ULONG Length
);

VOID
NTAPI
Ki386FreeIdentityMap(
    IN PLARGE_IDENTITY_MAP IdentityMap
);

VOID
NTAPI
Ki386EnableCurrentLargePage(
    IN ULONG_PTR StartAddress,
    IN ULONG Cr3
);

CODE_SEG("INIT")
VOID
NTAPI
KiI386PentiumLockErrataFixup(
    VOID
);

CODE_SEG("INIT")
VOID
NTAPI
KiInitializePAT(
    VOID
);

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeMTRR(
    IN BOOLEAN FinalCpu
);

CODE_SEG("INIT")
VOID
NTAPI
KiAmdK6InitializeMTRR(
    VOID
);

CODE_SEG("INIT")
VOID
NTAPI
KiRestoreFastSyscallReturnState(
    VOID
);

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableDE(
    IN ULONG_PTR Context
);

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableFxsr(
    IN ULONG_PTR Context
);

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableXMMIExceptions(
    IN ULONG_PTR Context
);

BOOLEAN
NTAPI
VdmDispatchBop(
    IN PKTRAP_FRAME TrapFrame
);

BOOLEAN
NTAPI
VdmDispatchPageFault(
    _In_ PKTRAP_FRAME TrapFrame
);

BOOLEAN
FASTCALL
KiVdmOpcodePrefix(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG Flags
);

BOOLEAN
FASTCALL
Ki386HandleOpcodeV86(
    IN PKTRAP_FRAME TrapFrame
);

DECLSPEC_NORETURN
VOID
FASTCALL
KiEoiHelper(
    IN PKTRAP_FRAME TrapFrame
);

VOID
FASTCALL
Ki386BiosCallReturnAddress(
    IN PKTRAP_FRAME TrapFrame
);

ULONG_PTR
FASTCALL
KiExitV86Mode(
    IN PKTRAP_FRAME TrapFrame
);

DECLSPEC_NORETURN
VOID
NTAPI
KiDispatchExceptionFromTrapFrame(
    IN NTSTATUS Code,
    IN ULONG Flags,
    IN ULONG_PTR Address,
    IN ULONG ParameterCount,
    IN ULONG_PTR Parameter1,
    IN ULONG_PTR Parameter2,
    IN ULONG_PTR Parameter3,
    IN PKTRAP_FRAME TrapFrame
);

NTSTATUS
NTAPI
KiConvertToGuiThread(
    VOID
);

DECLSPEC_NORETURN
VOID
FASTCALL
KiServiceExit(
    IN PKTRAP_FRAME TrapFrame,
    IN NTSTATUS Status
);

DECLSPEC_NORETURN
VOID
FASTCALL
KiServiceExit2(
    IN PKTRAP_FRAME TrapFrame
);

FORCEINLINE
DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    KiServiceExit2(TrapFrame);
}

extern PVOID Ki386IopmSaveArea;
extern ULONG KeI386EFlagsAndMaskV86;
extern ULONG KeI386EFlagsOrMaskV86;
extern BOOLEAN KeI386VirtualIntExtensions;
extern KIDTENTRY KiIdt[MAXIMUM_IDTVECTOR+1];
extern KDESCRIPTOR KiIdtDescriptor;
extern BOOLEAN KiI386PentiumLockErrataPresent;
extern ULONG KeI386NpxPresent;
extern ULONG KeI386XMMIPresent;
extern ULONG KeI386FxsrPresent;
extern ULONG KiMXCsrMask;
extern ULONG KeI386CpuType;
extern ULONG KeI386CpuStep;
extern ULONG KeI386MachineType;
extern ULONG KiFastSystemCallDisable;
extern VOID __cdecl KiTrap02(VOID);
extern VOID __cdecl KiTrap08(VOID);
extern VOID __cdecl KiTrap13(VOID);
extern VOID __cdecl KiFastCallEntry(VOID);
extern VOID NTAPI ExpInterlockedPopEntrySListFault(VOID);
extern VOID NTAPI ExpInterlockedPopEntrySListResume(VOID);
extern VOID __cdecl CopyParams(VOID);
extern VOID __cdecl ReadBatch(VOID);

#include "trap_x.h"

FORCEINLINE
PFX_SAVE_AREA
KiGetThreadNpxArea(IN PKTHREAD Thread)
{
    ASSERT((ULONG_PTR)Thread->InitialStack % 16 == 0);
    return (PFX_SAVE_AREA)((ULONG_PTR)Thread->InitialStack - sizeof(FX_SAVE_AREA));
}

FORCEINLINE
ULONG
Ke386SanitizeSeg(IN ULONG Cs,
                IN KPROCESSOR_MODE Mode)
{
    return ((Mode == KernelMode) ?
            (Cs & (0xFFFF & ~RPL_MASK)) :
            (RPL_MASK | (Cs & 0xFFFF)));
}

FORCEINLINE
ULONG
Ke386SanitizeFlags(IN ULONG Eflags,
                   IN KPROCESSOR_MODE Mode)
{
    return ((Mode == KernelMode) ?
            (Eflags & (EFLAGS_USER_SANITIZE | EFLAGS_INTERRUPT_MASK)) :
            (EFLAGS_INTERRUPT_MASK | (Eflags & EFLAGS_USER_SANITIZE)));
}

FORCEINLINE
DECLSPEC_NORETURN
VOID
KiDispatchException0Args(IN NTSTATUS Code,
                         IN ULONG_PTR Address,
                         IN PKTRAP_FRAME TrapFrame)
{
    KiDispatchExceptionFromTrapFrame(Code, 0, Address, 0, 0, 0, 0, TrapFrame);
}

FORCEINLINE
DECLSPEC_NORETURN
VOID
KiDispatchException1Args(IN NTSTATUS Code,
                         IN ULONG_PTR Address,
                         IN ULONG P1,
                         IN PKTRAP_FRAME TrapFrame)
{
    KiDispatchExceptionFromTrapFrame(Code, 0, Address, 1, P1, 0, 0, TrapFrame);
}

FORCEINLINE
DECLSPEC_NORETURN
VOID
KiDispatchException2Args(IN NTSTATUS Code,
                         IN ULONG_PTR Address,
                         IN ULONG P1,
                         IN ULONG P2,
                         IN PKTRAP_FRAME TrapFrame)
{
    KiDispatchExceptionFromTrapFrame(Code, 0, Address, 2, P1, P2, 0, TrapFrame);
}

NTSTATUS
NTAPI
KiSystemCallTrampoline(_In_ PVOID Handler,
                       _In_ PVOID Arguments,
                       _In_ ULONG StackBytes);

FORCEINLINE
VOID
KiCheckForApcDelivery(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    KIRQL OldIrql;

    if ((TrapFrame->EFlags & EFLAGS_V86_MASK) || (KiUserTrap(TrapFrame)))
    {
        Thread = KeGetCurrentThread();
        while (TRUE)
        {
            Thread->Alerted[KernelMode] = FALSE;

            if (!Thread->ApcState.UserApcPending) break;

            OldIrql = KfRaiseIrql(APC_LEVEL);
            _enable();

            KiDeliverApc(UserMode, NULL, TrapFrame);

            KfLowerIrql(OldIrql);
            _disable();
        }
    }
}

CODE_SEG("INIT")
FORCEINLINE
DECLSPEC_NORETURN
VOID
KiSwitchToBootStack(IN ULONG_PTR InitialStack)
{
    CODE_SEG("INIT") DECLSPEC_NORETURN VOID NTAPI KiSystemStartupBootStack(VOID);

#ifdef __GNUC__
    __asm__
    (
        "movl %0, %%esp\n\t"
        "subl %1, %%esp\n\t"
        "pushl %2\n\t"
        "jmp _KiSystemStartupBootStack@0"
        :
        : "c"(InitialStack),
          "i"(NPX_FRAME_LENGTH + KTRAP_FRAME_ALIGN + KTRAP_FRAME_LENGTH),
          "i"(CR0_EM | CR0_TS | CR0_MP),
          "p"(KiSystemStartupBootStack)
        : "%esp"
    );
#elif defined(_MSC_VER)
    __asm
    {
        mov esp, InitialStack
        sub esp, (NPX_FRAME_LENGTH + KTRAP_FRAME_ALIGN + KTRAP_FRAME_LENGTH)
        push (CR0_EM | CR0_TS | CR0_MP)
        jmp KiSystemStartupBootStack
    }
#else
#error Unknown Compiler
#endif

    UNREACHABLE;
}

FORCEINLINE
VOID
KiEndInterrupt(IN KIRQL Irql,
               IN PKTRAP_FRAME TrapFrame)
{
    _disable();
    HalEndSystemInterrupt(Irql, TrapFrame);

    KiEoiHelper(TrapFrame);
}

FORCEINLINE
PULONG
KiGetUserModeStackAddress(void)
{
    return &(KeGetCurrentThread()->TrapFrame->HardwareEsp);
}

extern ULONG KiDpcTimeCount[MAXIMUM_PROCESSORS];

#define KiPrcbDpcTimeCount(Prcb) (KiDpcTimeCount[(Prcb)->Number])
#define KiPrcbDpcTimeLimit(Prcb) ((Prcb)->DpcTimeLimit)
#define KiPrcbDpcWatchdogCount(Prcb) ((Prcb)->DpcWatchDogCount)
#define KiPrcbDpcWatchdogPeriod(Prcb) ((Prcb)->DpcWatchdogPeriod)
extern PCONTEXT KiProcessorContext[MAXIMUM_PROCESSORS];
#define KiPrcbContext(Prcb) (KiProcessorContext[(Prcb)->Number])
#define KI_PRCB_CPU_TYPE_OFFSET FIELD_OFFSET(KPRCB, CpuType)
#define KI_PRCB_VENDOR_STRING_OFFSET FIELD_OFFSET(KPRCB, VendorString)

#define KI_PRCB_CONTEXT_OFFSET 0
#define KiClockTickSuspended(Prcb) FALSE
#define KiClockTimerInserted(DueTime) ((VOID)(DueTime))
#define KiRefreshProcessorRunTime(Prcb) ((VOID)(Prcb))
#define KiQueryEffectiveProcessorMhz(Number) (KiProcessorBlock[(Number)]->MHz)
#define KiQueryClockTickBaseline(Counter, Remainder) (*(Counter) = KeQueryPerformanceCounter(NULL), *(Remainder) = 0)
