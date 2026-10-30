/*
 * PROJECT:     LiberNT WoW64 x86 CPU
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Run 32-bit x86 code of WoW64 processes with the felix86 recompiler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define NTOS_MODE_USER
#include <windef.h>
#include <winbase.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <pseh/pseh2.h>
#include <reactos/wow64shared.h>
#include "../amd64emu/felix86/bridge.h"

#define FELIX_TLS_THREAD     2
#define FELIX_SEG_CODE       0x23
#define FELIX_SEG_DATA       0x2B
#define FELIX_SEG_TEB        0x53
#define FELIX_INTERRUPT_FASTFAIL 0x29
#define FELIX_INTERRUPT_ASSERT   0x2C
#define FELIX_INTERRUPT_DEBUG    0x2D
#define FELIX_PARK_TIMEOUT       (-10 * 1000 * 1000)

enum
{
    FelixRax,
    FelixRcx,
    FelixRdx,
    FelixRbx,
    FelixRsp,
    FelixRbp,
    FelixRsi,
    FelixRdi,
    FelixRip = 16
};

typedef struct _FELIX_THREAD
{
    FELIX86_NT_CPU *Cpu;
    PCONTEXT EntryContext;
    ULONG InService;
    PVOID UnmapBase;
    SIZE_T UnmapSize;
    HANDLE ParkEvent;
    HANDLE ReleaseEvent;
} FELIX_THREAD, *PFELIX_THREAD;

#pragma pack(push, 1)
typedef struct _FELIX_FXSAVE
{
    USHORT ControlWord;
    USHORT StatusWord;
    UCHAR TagWord;
    UCHAR Reserved1;
    USHORT ErrorOpcode;
    ULONG ErrorOffset;
    USHORT ErrorSelector;
    USHORT Reserved2;
    ULONG DataOffset;
    USHORT DataSelector;
    USHORT Reserved3;
    ULONG MxCsr;
    ULONG MxCsrMask;
    UCHAR FloatRegisters[8][16];
    UCHAR XmmRegisters[8][16];
    UCHAR Reserved4[224];
} FELIX_FXSAVE, *PFELIX_FXSAVE;
#pragma pack(pop)

C_ASSERT(sizeof(FELIX_FXSAVE) == WOW64_MAXIMUM_SUPPORTED_EXTENSION);

NTSYSAPI
NTSTATUS
NTAPI
RtlWow64GetCurrentCpuArea(
    _Out_opt_ PUSHORT Machine,
    _Out_opt_ PVOID *Context,
    _Out_opt_ PVOID *ContextEx);

NTSYSAPI
NTSTATUS
NTAPI
RtlWow64GetThreadContext(
    _In_ HANDLE ThreadHandle,
    _Inout_ PWOW64_CONTEXT ThreadContext);

NTSYSAPI
NTSTATUS
NTAPI
RtlWow64SetThreadContext(
    _In_ HANDLE ThreadHandle,
    _In_ PWOW64_CONTEXT ThreadContext);

NTSTATUS
WINAPI
Wow64SystemServiceEx(
    _In_ UINT Number,
    _In_ PUINT Arguments);

static UCHAR *FelixBopCode;
static RTL_CRITICAL_SECTION FelixSuspendLock;
static ULONG_PTR FelixModuleBase;
static ULONG_PTR FelixModuleEnd;

static
DECLSPEC_NORETURN
VOID
FelixFatal(
    _In_ NTSTATUS Status,
    _In_ PCSTR Message)
{
    DbgPrint("WOW64FELIX: %s: %08lx\n", Message, Status);
    NtTerminateProcess(NtCurrentProcess(), Status);
    for (;;) NtYieldExecution();
}

static
PFELIX_THREAD
FelixCurrentThread(VOID)
{
    return NtCurrentTeb()->TlsSlots[FELIX_TLS_THREAD];
}

static
PWOW64_CONTEXT
FelixCpuArea(VOID)
{
    PVOID Context = NULL;

    if (!NT_SUCCESS(RtlWow64GetCurrentCpuArea(NULL, &Context, NULL)) || !Context)
        FelixFatal(STATUS_INVALID_PARAMETER, "the thread has no WoW64 CPU area");
    return Context;
}

static
VOID
FelixFxsaveToFloatSave(
    _In_ const FELIX_FXSAVE *Fx,
    _Out_ PWOW64_FLOATING_SAVE_AREA FloatSave)
{
    ULONG Index, Tag = 0;

    for (Index = 0; Index < 8; Index++)
    {
        if (!(Fx->TagWord & (1 << Index))) Tag |= 3 << (2 * Index);
        RtlCopyMemory(&FloatSave->RegisterArea[Index * 10], Fx->FloatRegisters[Index], 10);
    }

    FloatSave->ControlWord = 0xFFFF0000 | Fx->ControlWord;
    FloatSave->StatusWord = 0xFFFF0000 | Fx->StatusWord;
    FloatSave->TagWord = 0xFFFF0000 | Tag;
    FloatSave->ErrorOffset = Fx->ErrorOffset;
    FloatSave->ErrorSelector = Fx->ErrorSelector | ((ULONG)Fx->ErrorOpcode << 16);
    FloatSave->DataOffset = Fx->DataOffset;
    FloatSave->DataSelector = 0xFFFF0000 | Fx->DataSelector;
    FloatSave->Cr0NpxState = 0;
}

static
VOID
FelixFloatSaveToFxsave(
    _In_ const WOW64_FLOATING_SAVE_AREA *FloatSave,
    _Inout_ PFELIX_FXSAVE Fx)
{
    ULONG Index;

    Fx->ControlWord = (USHORT)FloatSave->ControlWord;
    Fx->StatusWord = (USHORT)FloatSave->StatusWord;
    Fx->TagWord = 0;
    for (Index = 0; Index < 8; Index++)
    {
        if (((FloatSave->TagWord >> (2 * Index)) & 3) != 3) Fx->TagWord |= 1 << Index;
        RtlZeroMemory(Fx->FloatRegisters[Index], sizeof(Fx->FloatRegisters[Index]));
        RtlCopyMemory(Fx->FloatRegisters[Index], &FloatSave->RegisterArea[Index * 10], 10);
    }
    Fx->ErrorOffset = FloatSave->ErrorOffset;
    Fx->ErrorSelector = (USHORT)FloatSave->ErrorSelector;
    Fx->ErrorOpcode = (USHORT)(FloatSave->ErrorSelector >> 16);
    Fx->DataOffset = FloatSave->DataOffset;
    Fx->DataSelector = (USHORT)FloatSave->DataSelector;
}

static
VOID
FelixSaveState(
    _In_ PFELIX_THREAD Thread,
    _Out_ PWOW64_CONTEXT Context)
{
    PULONG64 Registers = Felix86NtRegisters(Thread->Cpu);

    Context->ContextFlags = WOW64_CONTEXT_FULL | WOW64_CONTEXT_FLOATING_POINT | WOW64_CONTEXT_EXTENDED_REGISTERS;
    Context->Eax = (ULONG)Registers[FelixRax];
    Context->Ecx = (ULONG)Registers[FelixRcx];
    Context->Edx = (ULONG)Registers[FelixRdx];
    Context->Ebx = (ULONG)Registers[FelixRbx];
    Context->Esp = (ULONG)Registers[FelixRsp];
    Context->Ebp = (ULONG)Registers[FelixRbp];
    Context->Esi = (ULONG)Registers[FelixRsi];
    Context->Edi = (ULONG)Registers[FelixRdi];
    Context->Eip = (ULONG)Registers[FelixRip];
    Context->EFlags = (ULONG)Felix86NtGetFlags(Thread->Cpu);
    Context->SegCs = FELIX_SEG_CODE;
    Context->SegDs = FELIX_SEG_DATA;
    Context->SegEs = FELIX_SEG_DATA;
    Context->SegSs = FELIX_SEG_DATA;
    Context->SegGs = FELIX_SEG_DATA;
    Context->SegFs = FELIX_SEG_TEB;
    Context->Dr0 = Context->Dr1 = Context->Dr2 = Context->Dr3 = Context->Dr6 = Context->Dr7 = 0;
    RtlZeroMemory(Context->ExtendedRegisters, sizeof(Context->ExtendedRegisters));
    Felix86NtSaveFloating(Thread->Cpu, Context->ExtendedRegisters);
    FelixFxsaveToFloatSave((PFELIX_FXSAVE)Context->ExtendedRegisters, &Context->FloatSave);
}

static
VOID
FelixLoadState(
    _In_ PFELIX_THREAD Thread,
    _In_ const WOW64_CONTEXT *Context)
{
    PULONG64 Registers = Felix86NtRegisters(Thread->Cpu);
    FELIX_FXSAVE Fx;
    ULONG Flags = Context->ContextFlags;

    if ((Flags & WOW64_CONTEXT_INTEGER) == WOW64_CONTEXT_INTEGER)
    {
        Registers[FelixRax] = Context->Eax;
        Registers[FelixRcx] = Context->Ecx;
        Registers[FelixRdx] = Context->Edx;
        Registers[FelixRbx] = Context->Ebx;
        Registers[FelixRsi] = Context->Esi;
        Registers[FelixRdi] = Context->Edi;
    }
    if ((Flags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL)
    {
        Registers[FelixRsp] = Context->Esp;
        Registers[FelixRbp] = Context->Ebp;
        Registers[FelixRip] = Context->Eip;
        Felix86NtSetFlags(Thread->Cpu, Context->EFlags);
    }
    if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
    {
        Felix86NtRestoreFloating(Thread->Cpu, Context->ExtendedRegisters);
    }
    else if ((Flags & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT)
    {
        Felix86NtSaveFloating(Thread->Cpu, &Fx);
        FelixFloatSaveToFxsave(&Context->FloatSave, &Fx);
        Felix86NtRestoreFloating(Thread->Cpu, &Fx);
    }
}

static
VOID
FelixLoadCpuArea(
    _In_ PFELIX_THREAD Thread,
    _In_opt_ const NTSTATUS *Status,
    _In_ ULONG Bop)
{
    WOW64_CONTEXT Context = *FelixCpuArea();

    Context.ContextFlags = WOW64_CONTEXT_FULL |
                           (Context.ContextFlags & (WOW64_CONTEXT_FLOATING_POINT | WOW64_CONTEXT_EXTENDED_REGISTERS));
    if (Status && Context.Eip == Bop) Context.Eax = *Status;
    FelixLoadState(Thread, &Context);
}

VOID
Felix86NtSystemService(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers)
{
    FelixFatal(STATUS_ILLEGAL_INSTRUCTION, "64-bit system call in a WoW64 process");
}

VOID
Felix86NtSystemService32(
    _In_ PVOID Owner,
    _Inout_ PULONG64 Registers)
{
    PFELIX_THREAD Thread = Owner;
    PWOW64_CONTEXT Context = FelixCpuArea();
    PCONTEXT EntryContext = Thread->EntryContext;
    ULONG64 Frame = Felix86NtGetFrame(Thread->Cpu);
    ULONG InService = Thread->InService;
    ULONG Bop = (ULONG)Registers[FelixRip];
    NTSTATUS Status;

    FelixSaveState(Thread, Context);
    Thread->InService = InService + 1;
    Status = Wow64SystemServiceEx((ULONG)Registers[FelixRax], ULongToPtr((ULONG)Registers[FelixRsp] + 8));
    Thread->InService = InService;
    Thread->EntryContext = EntryContext;
    Felix86NtSetFrame(Thread->Cpu, Frame);
    FelixLoadCpuArea(Thread, &Status, Bop);
}

ULONG64
Felix86NtNativeEntry(
    _In_ ULONG64 Rip)
{
    return 0;
}

static
VOID
FelixInvalidate(
    _In_ PVOID Base,
    _In_ SIZE_T Size)
{
    if (Size) Felix86NtInvalidate((ULONG_PTR)Base, (ULONG_PTR)Base + Size);
}

static
VOID
FelixQueryAllocation(
    _In_ PVOID Address,
    _Out_ PVOID *Base,
    _Out_ PSIZE_T Size)
{
    MEMORY_BASIC_INFORMATION Information;
    PUCHAR Current;
    PVOID AllocationBase;

    *Base = NULL;
    *Size = 0;
    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), Address, MemoryBasicInformation,
                                         &Information, sizeof(Information), NULL)) ||
        Information.State == MEM_FREE)
    {
        return;
    }

    AllocationBase = Information.AllocationBase;
    Current = AllocationBase;
    while (NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), Current, MemoryBasicInformation,
                                           &Information, sizeof(Information), NULL)) &&
           Information.State != MEM_FREE &&
           Information.AllocationBase == AllocationBase)
    {
        Current += Information.RegionSize;
    }

    *Base = AllocationBase;
    *Size = Current - (PUCHAR)AllocationBase;
}

VOID
WINAPI
BTCpuProcessInit(VOID)
{
    WOW64INFO *Info = NtCurrentTeb()->TlsSlots[WOW64_TLS_WOW64INFO];
    PIMAGE_NT_HEADERS Headers;
    PVOID Base = NULL;
    SIZE_T Size = PAGE_SIZE;
    ULONG OldProtect;
    NTSTATUS Status;

    if (!RtlPcToFileHeader((PVOID)BTCpuProcessInit, &Base)) FelixFatal(STATUS_INVALID_IMAGE_FORMAT, "no module");
    Headers = RtlImageNtHeader(Base);
    FelixModuleBase = (ULONG_PTR)Base;
    FelixModuleEnd = FelixModuleBase + Headers->OptionalHeader.SizeOfImage;

    if (Felix86NtInitialize()) FelixFatal(STATUS_NOT_SUPPORTED, "the processor cannot run felix86");
    Status = RtlInitializeCriticalSection(&FelixSuspendLock);
    if (!NT_SUCCESS(Status)) FelixFatal(Status, "cannot initialize the suspend lock");

    Base = NULL;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0x7FFFFFFF, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) FelixFatal(Status, "cannot allocate the system call gate");
    FelixBopCode = Base;
    FelixBopCode[0] = 0xCD;
    FelixBopCode[1] = 0x80;
    FelixBopCode[2] = 0xC3;
    Status = NtProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, PAGE_EXECUTE_READ, &OldProtect);
    if (!NT_SUCCESS(Status)) FelixFatal(Status, "cannot protect the system call gate");

    if (Info) Info->CpuFlags |= WOW64_CPUFLAGS_SOFTWARE;
}

VOID
WINAPI
BTCpuProcessTerm(
    _In_ HANDLE Process,
    _In_ BOOL After,
    _In_ NTSTATUS Status)
{
    if (Process) return;
    if (!After) Felix86NtBeginTermination();
    else Felix86NtEndTermination(NT_SUCCESS(Status));
}

VOID
WINAPI
BTCpuThreadInit(VOID)
{
    PTEB Teb = NtCurrentTeb();
    PFELIX_THREAD Thread;

    Thread = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Thread));
    if (!Thread) FelixFatal(STATUS_NO_MEMORY, "cannot allocate the thread state");
    if (!NT_SUCCESS(NtCreateEvent(&Thread->ParkEvent, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE)) ||
        !NT_SUCCESS(NtCreateEvent(&Thread->ReleaseEvent, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE)))
    {
        FelixFatal(STATUS_NO_MEMORY, "cannot create the park events");
    }
    Thread->Cpu = Felix86NtCreateCpu(Thread, 0);
    if (!Thread->Cpu) FelixFatal(STATUS_NO_MEMORY, "cannot create the CPU");
    Felix86NtSetMode32(Thread->Cpu, (ULONG_PTR)Teb + Teb->WowTebOffset);
    Teb->TlsSlots[FELIX_TLS_THREAD] = Thread;
}

VOID
WINAPI
BTCpuThreadTerm(
    _In_ HANDLE ThreadHandle,
    _In_ LONG ExitCode)
{
    THREAD_BASIC_INFORMATION Information;
    PFELIX_THREAD Thread;

    if (ThreadHandle != NtCurrentThread() &&
        (!NT_SUCCESS(NtQueryInformationThread(ThreadHandle, ThreadBasicInformation, &Information, sizeof(Information), NULL)) ||
         Information.ClientId.UniqueThread != NtCurrentTeb()->ClientId.UniqueThread))
    {
        return;
    }

    Thread = FelixCurrentThread();
    if (!Thread) return;
    RtlEnterCriticalSection(&FelixSuspendLock);
    NtCurrentTeb()->TlsSlots[FELIX_TLS_THREAD] = NULL;
    RtlLeaveCriticalSection(&FelixSuspendLock);
    NtClose(Thread->ParkEvent);
    NtClose(Thread->ReleaseEvent);
    Felix86NtDestroyCpu(Thread->Cpu);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Thread);
}

PVOID
WINAPI
BTCpuGetBopCode(VOID)
{
    return FelixBopCode;
}

NTSTATUS
WINAPI
BTCpuGetContext(
    _In_ HANDLE Thread,
    _In_ HANDLE Process,
    _In_opt_ PVOID Unknown,
    _Inout_ PWOW64_CONTEXT Context)
{
    return RtlWow64GetThreadContext(Thread, Context);
}

NTSTATUS
WINAPI
BTCpuSuspendLocalThread(
    _In_ HANDLE ThreadHandle,
    _Out_opt_ PULONG Count)
{
    THREAD_BASIC_INFORMATION Information;
    LARGE_INTEGER Timeout;
    PFELIX_THREAD Target;
    BOOLEAN Parked;
    ULONG Previous;
    NTSTATUS Status;

    Status = NtQueryInformationThread(ThreadHandle, ThreadBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status)) return Status;
    if (Information.ClientId.UniqueThread == NtCurrentTeb()->ClientId.UniqueThread) return NtSuspendThread(ThreadHandle, Count);

    RtlEnterCriticalSection(&FelixSuspendLock);
    Status = NtSuspendThread(ThreadHandle, &Previous);
    Target = NT_SUCCESS(Status) && !Previous ? ((PTEB)Information.TebBaseAddress)->TlsSlots[FELIX_TLS_THREAD] : NULL;
    if (Target && Target->EntryContext && !Target->InService)
    {
        NtClearEvent(Target->ParkEvent);
        Felix86NtRequestPark(Target->Cpu);
        NtResumeThread(ThreadHandle, NULL);
        Timeout.QuadPart = FELIX_PARK_TIMEOUT;
        Parked = NtWaitForSingleObject(Target->ParkEvent, FALSE, &Timeout) == STATUS_SUCCESS;
        if (!Parked && !Felix86NtCancelPark(Target->Cpu))
        {
            NtWaitForSingleObject(Target->ParkEvent, FALSE, NULL);
            Parked = TRUE;
        }
        if (!Parked) DbgPrint("WOW64FELIX: thread %p did not reach a safepoint\n", Target);
        NtSuspendThread(ThreadHandle, NULL);
        if (Parked) NtSetEvent(Target->ReleaseEvent, NULL);
    }
    RtlLeaveCriticalSection(&FelixSuspendLock);

    if (NT_SUCCESS(Status) && Count) *Count = Previous;
    return Status;
}

NTSTATUS
WINAPI
BTCpuSetContext(
    _In_ HANDLE Thread,
    _In_ HANDLE Process,
    _In_opt_ PVOID Unknown,
    _In_ PWOW64_CONTEXT Context)
{
    return RtlWow64SetThreadContext(Thread, Context);
}

VOID
WINAPI
FelixSimulate(
    _In_ PCONTEXT EntryContext)
{
    PFELIX_THREAD Thread = FelixCurrentThread();

    if (!Thread) FelixFatal(STATUS_INVALID_THREAD, "the thread has no CPU");
    Thread->EntryContext = EntryContext;
    Thread->InService = 0;
    FelixLoadCpuArea(Thread, NULL, 0);
    Felix86NtRun(Thread->Cpu);
}

static
BOOLEAN
FelixDebugService(
    _In_ PFELIX_THREAD Thread)
{
    PULONG64 Registers = Felix86NtRegisters(Thread->Cpu);
    ULONG Length;

    if ((ULONG)Registers[FelixRax] == BREAKPOINT_PRINT)
    {
        Length = min((USHORT)Registers[FelixRdx], 512);
        _SEH2_TRY
        {
            DbgPrintEx((ULONG)Registers[FelixRbx],
                       (ULONG)Registers[FelixRdi],
                       "%.*s",
                       Length,
                       (PCSTR)ULongToPtr((ULONG)Registers[FelixRcx]));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }
    Registers[FelixRax] = STATUS_SUCCESS;
    Registers[FelixRip] += 3;
    return TRUE;
}

NTSTATUS
WINAPI
BTCpuResetToConsistentState(
    _Inout_ PEXCEPTION_POINTERS Pointers)
{
    PFELIX_THREAD Thread = FelixCurrentThread();
    PEXCEPTION_RECORD Record = Pointers->ExceptionRecord;
    PCONTEXT NativeContext = Pointers->ContextRecord;
    FELIX86_NT_NATIVE Native;
    FELIX86_NT_GUEST Guest;
    PWOW64_CONTEXT Context;
    ULONG Index;
    int Result;

    if (!Thread || !Thread->Cpu || !Thread->EntryContext) return STATUS_SUCCESS;

    RtlZeroMemory(&Native, sizeof(Native));
    Native.Pc = &NativeContext->Pc;
    Native.X = NativeContext->X;
    Native.F = NativeContext->F;
    Native.Code = Record->ExceptionCode;
    Native.Parameters = min(Record->NumberParameters, RTL_NUMBER_OF(Native.Information));
    for (Index = 0; Index < Native.Parameters; Index++) Native.Information[Index] = Record->ExceptionInformation[Index];
    Native.InService = Thread->InService != 0;
    Native.InModule = NativeContext->Pc >= FelixModuleBase && NativeContext->Pc < FelixModuleEnd;

    Result = Felix86NtFault(Thread->Cpu, &Native, &Guest);
    if (Result == FELIX86_NT_FAULT_UNRELATED) return STATUS_SUCCESS;
    if (Result == FELIX86_NT_FAULT_RESUME) NtContinue(NativeContext, FALSE);
    if (Result == FELIX86_NT_FAULT_PARK)
    {
        FelixSaveState(Thread, FelixCpuArea());
        NtSetEvent(Thread->ParkEvent, NULL);
        NtWaitForSingleObject(Thread->ReleaseEvent, FALSE, NULL);
        FelixLoadCpuArea(Thread, NULL, 0);
        NtContinue(NativeContext, FALSE);
    }

    if (Guest.Interrupt == FELIX_INTERRUPT_DEBUG && FelixDebugService(Thread)) NtContinue(NativeContext, FALSE);
    if (Guest.Interrupt == FELIX_INTERRUPT_FASTFAIL)
        NtTerminateProcess(NtCurrentProcess(), STATUS_STACK_BUFFER_OVERRUN);

    RtlZeroMemory(Record, sizeof(*Record));
    Record->ExceptionCode = Guest.Interrupt == FELIX_INTERRUPT_ASSERT ? STATUS_ASSERTION_FAILURE : Guest.Code;
    Record->ExceptionAddress = (PVOID)(ULONG_PTR)Guest.Address;
    Record->NumberParameters = Guest.Interrupt == FELIX_INTERRUPT_ASSERT ? 0 : Guest.Parameters;
    for (Index = 0; Index < Record->NumberParameters; Index++) Record->ExceptionInformation[Index] = Guest.Information[Index];

    Context = FelixCpuArea();
    FelixSaveState(Thread, Context);
    *NativeContext = *Thread->EntryContext;
    return STATUS_SUCCESS;
}

BOOLEAN
WINAPI
BTCpuIsProcessorFeaturePresent(
    _In_ UINT Feature)
{
    UINT32 Basic[4], Extended[4];

    Felix86NtCpuid(1, 0, Basic);
    Felix86NtCpuid(7, 0, Extended);
    switch (Feature)
    {
        case PF_COMPARE_EXCHANGE_DOUBLE: return (Basic[3] >> 8) & 1;
        case PF_MMX_INSTRUCTIONS_AVAILABLE: return (Basic[3] >> 23) & 1;
        case PF_XMMI_INSTRUCTIONS_AVAILABLE: return (Basic[3] >> 25) & 1;
        case PF_RDTSC_INSTRUCTION_AVAILABLE: return (Basic[3] >> 4) & 1;
        case PF_PAE_ENABLED: return (Basic[3] >> 6) & 1;
        case PF_XMMI64_INSTRUCTIONS_AVAILABLE: return (Basic[3] >> 26) & 1;
        case PF_NX_ENABLED: return TRUE;
        case PF_SSE3_INSTRUCTIONS_AVAILABLE: return Basic[2] & 1;
        case PF_XSAVE_ENABLED: return (Basic[2] >> 27) & 1;
        case PF_FASTFAIL_AVAILABLE: return TRUE;
        case PF_RDRAND_INSTRUCTION_AVAILABLE: return (Basic[2] >> 30) & 1;
        case PF_SSSE3_INSTRUCTIONS_AVAILABLE: return (Basic[2] >> 9) & 1;
        case PF_SSE4_1_INSTRUCTIONS_AVAILABLE: return (Basic[2] >> 19) & 1;
        case PF_SSE4_2_INSTRUCTIONS_AVAILABLE: return (Basic[2] >> 20) & 1;
        case PF_AVX_INSTRUCTIONS_AVAILABLE: return (Basic[2] >> 28) & 1;
        case PF_AVX2_INSTRUCTIONS_AVAILABLE: return (Extended[1] >> 5) & 1;
        case PF_AVX512F_INSTRUCTIONS_AVAILABLE: return (Extended[1] >> 16) & 1;
    }
    return FALSE;
}

VOID
WINAPI
BTCpuUpdateProcessorInformation(
    _Inout_ PSYSTEM_PROCESSOR_INFORMATION Information)
{
    Information->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    Felix86NtProcessor(&Information->ProcessorLevel, &Information->ProcessorRevision);
}

VOID
WINAPI
BTCpuFlushInstructionCache2(
    _In_ const VOID *Address,
    _In_ SIZE_T Size)
{
    FelixInvalidate((PVOID)Address, Size);
}

VOID
WINAPI
BTCpuFlushInstructionCacheHeavy(
    _In_ const VOID *Address,
    _In_ SIZE_T Size)
{
    FelixInvalidate((PVOID)Address, Size);
}

VOID
WINAPI
BTCpuNotifyMemoryDirty(
    _In_ PVOID Address,
    _In_ SIZE_T Size)
{
    FelixInvalidate(Address, Size);
}

VOID
WINAPI
BTCpuNotifyMemoryFree(
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG FreeType,
    _In_ BOOL After,
    _In_ NTSTATUS Status)
{
    PFELIX_THREAD Thread = FelixCurrentThread();
    PVOID Base;
    SIZE_T Length;

    if (!After)
    {
        if ((FreeType & MEM_RELEASE) && !Size)
        {
            FelixQueryAllocation(Address, &Base, &Length);
            if (Thread)
            {
                Thread->UnmapBase = Base;
                Thread->UnmapSize = Length;
            }
        }
        return;
    }

    if (NT_SUCCESS(Status))
    {
        if ((FreeType & MEM_RELEASE) && !Size && Thread)
            FelixInvalidate(Thread->UnmapBase, Thread->UnmapSize);
        else
            FelixInvalidate(Address, Size);
    }
    if (Thread) Thread->UnmapSize = 0;
}

VOID
WINAPI
BTCpuNotifyMemoryProtect(
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG NewProtection,
    _In_ BOOL After,
    _In_ NTSTATUS Status)
{
    if (After && NT_SUCCESS(Status)) FelixInvalidate(Address, Size);
}

VOID
WINAPI
BTCpuNotifyMemoryAlloc(
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG Type,
    _In_ ULONG Protection,
    _In_ BOOL After,
    _In_ NTSTATUS Status)
{
}

NTSTATUS
WINAPI
BTCpuNotifyMapViewOfSection(
    _In_opt_ PVOID Unknown1,
    _In_ PVOID Address,
    _In_opt_ PVOID Unknown2,
    _In_ SIZE_T Size,
    _In_ ULONG AllocationType,
    _In_ ULONG Protection)
{
    FelixInvalidate(Address, Size);
    return STATUS_SUCCESS;
}

VOID
WINAPI
BTCpuNotifyUnmapViewOfSection(
    _In_ PVOID Address,
    _In_ BOOL After,
    _In_ NTSTATUS Status)
{
    PFELIX_THREAD Thread = FelixCurrentThread();

    if (!Thread) return;
    if (!After)
    {
        FelixQueryAllocation(Address, &Thread->UnmapBase, &Thread->UnmapSize);
        return;
    }
    if (NT_SUCCESS(Status)) FelixInvalidate(Thread->UnmapBase, Thread->UnmapSize);
    Thread->UnmapSize = 0;
}

VOID
WINAPI
BTCpuNotifyReadFile(
    _In_ HANDLE File,
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ BOOL After,
    _In_ NTSTATUS Status)
{
    if (After && NT_SUCCESS(Status)) FelixInvalidate(Address, Size);
}

VOID
WINAPI
BTCpuNotifyProcessExecuteFlagsChange(
    _In_ ULONG Flags)
{
}
