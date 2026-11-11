/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The x64 side of reading and writing a thread's register state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Which parts of a context this architecture knows how to carry */
#define PSP_ARCH_CONTEXT_VALID_FLAGS            \
    (CONTEXT_AMD64 | CONTEXT_CONTROL |          \
     CONTEXT_INTEGER | CONTEXT_SEGMENTS |       \
     CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS | \
     CONTEXT_XSTATE | CONTEXT_EXCEPTION_REQUEST | \
     CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE | \
     CONTEXT_EXCEPTION_ACTIVE)

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Runs on the thread whose state is wanted. The frame that holds the state
 * is the one the thread last left user mode through; a kernel trap taken on
 * top of it links back to that frame, so the chain is walked until the user
 * one is found.
 */
VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PGET_SET_CTX_CONTEXT Request = CONTAINING_RECORD(Apc,
                                                     GET_SET_CTX_CONTEXT,
                                                     Apc);
    PKTHREAD Thread = Apc->SystemArgument2;
    PKTRAP_FRAME Frame = Thread->TrapFrame;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(Thread == KeGetCurrentThread());

    if (Request->Mode == UserMode)
    {
        while (Frame != NULL && !KiUserTrap(Frame))
            Frame = (PKTRAP_FRAME)Frame->TrapFrame;
    }

    /*
     * A thread that has not yet run in user mode has its starting state at
     * the top of its kernel stack, which is where the frame it will return
     * through was built.
     */
    if (Frame == NULL && Thread->Teb != NULL)
        Frame = (PKTRAP_FRAME)((PUCHAR)Thread->InitialStack -
                               sizeof(KTRAP_FRAME));

    if (Frame == NULL)
    {
        Request->Status = STATUS_INVALID_DEVICE_STATE;
        KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    /* Only this architecture's own shape of a context is accepted */
    if ((Request->Context.ContextFlags & CONTEXT_AMD64) != CONTEXT_AMD64 ||
        (Request->Context.ContextFlags & ~PSP_ARCH_CONTEXT_VALID_FLAGS) != 0)
    {
        Request->Status = STATUS_INVALID_PARAMETER;
        KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    if (Apc->SystemArgument1 != NULL)
    {
        KeContextToTrapFrame(&Request->Context,
                             NULL,
                             Frame,
                             Request->Context.ContextFlags,
                             Request->Mode);
    }
    else
    {
        ULONG Requested = Request->Context.ContextFlags;

        KeTrapFrameToContext(Frame, NULL, &Request->Context);

        Request->Context.ContextFlags &= ~(CONTEXT_EXCEPTION_REPORTING |
                                           CONTEXT_SERVICE_ACTIVE |
                                           CONTEXT_EXCEPTION_ACTIVE);

        /*
         * A caller that asked why the thread is in the kernel is told: either
         * it made a system call or it took a fault. Which one decides whether
         * the state can be changed meaningfully.
         */
        if ((Requested & CONTEXT_EXCEPTION_REQUEST) != 0)
        {
            Request->Context.ContextFlags |= CONTEXT_EXCEPTION_REQUEST |
                                             CONTEXT_EXCEPTION_REPORTING;

            if (Frame->ExceptionActive == KEXCEPTION_ACTIVE_SERVICE_FRAME)
                Request->Context.ContextFlags |= CONTEXT_SERVICE_ACTIVE;
            else if (Frame->ExceptionActive == KEXCEPTION_ACTIVE_EXCEPTION_FRAME)
                Request->Context.ContextFlags |= CONTEXT_EXCEPTION_ACTIVE;
        }
    }

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}

/*
 * The state a new user thread starts in. It begins inside the native image's
 * thread entry, which is given the routine to run and its argument, and the
 * stack pointer is left where the calling convention wants it.
 */
VOID
NTAPI
PsArchInitializeUserThreadContext(
    _Out_ PCONTEXT Context,
    _In_ PVOID ThreadStart,
    _In_ PVOID StartRoutine,
    _In_opt_ PVOID Argument,
    _In_ PVOID StackBase)
{
    RtlZeroMemory(Context, sizeof(*Context));

    Context->ContextFlags = CONTEXT_FULL;
    Context->Rip = (ULONG64)(ULONG_PTR)ThreadStart;
    Context->Rcx = (ULONG64)(ULONG_PTR)StartRoutine;
    Context->Rdx = (ULONG64)(ULONG_PTR)Argument;

    /* Aligned; the kernel reserves the home area and return slot when it builds the frame */
    Context->Rsp = ((ULONG64)(ULONG_PTR)StackBase - 2 * sizeof(PVOID)) & ~15ULL;

    Context->SegCs = KGDT64_R3_CODE | RPL_MASK;
    Context->SegSs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegDs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegEs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegFs = KGDT64_R3_CMTEB | RPL_MASK;
    Context->SegGs = KGDT64_R3_DATA | RPL_MASK;
    Context->EFlags = EFLAGS_INTERRUPT_MASK;

    Context->MxCsr = INITIAL_MXCSR;
}

/*
 * Where a user thread's first return to user mode is aimed: at the loader in
 * the native image. The context the thread was created with is placed on its
 * stack and handed to the loader, which continues into it once the process is
 * set up; the image base comes along so the loader can find itself.
 */
BOOLEAN
NTAPI
PspArchManagesExecutableWrites(VOID)
{
    return FALSE;
}

VOID
NTAPI
PspArchInitializeUserThreadStartup(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    CONTEXT Context;
    PCONTEXT UserContext;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);

    UserContext = (PCONTEXT)ALIGN_DOWN_BY(TrapFrame->Rsp - sizeof(CONTEXT), 16);

    _SEH2_TRY
    {
        ProbeForWrite(UserContext, sizeof(CONTEXT), 16);
        RtlCopyMemory(UserContext, &Context, sizeof(CONTEXT));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ZwTerminateThread(NtCurrentThread(), _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Entered as if called: the slot below the context stands for a return address */
    TrapFrame->Rsp = (ULONG64)UserContext - sizeof(ULONG64);
    TrapFrame->Rip = (ULONG64)PspSystemDllEntryPoint;
    TrapFrame->Rcx = (ULONG64)UserContext;
    TrapFrame->Rdx = (ULONG64)PspSystemDllBase;

    KiInstrumentTrapFrame(TrapFrame);
}

/* The full tag word of the x87 save area, rebuilt from the abridged one and the register contents */
static
ULONG
PspFullFpuTagWord(
    _In_ PXSAVE_FORMAT FxSave)
{
    ULONG Top = (FxSave->StatusWord >> 11) & 7;
    ULONG TagWord = 0;
    ULONG Physical;

    for (Physical = 0; Physical < 8; Physical++)
    {
        PM128A Register = &FxSave->FloatRegisters[(Physical - Top) & 7];
        ULONG Exponent = (ULONG)Register->High & 0x7fff;
        ULONG Tag;

        if (!(FxSave->TagWord & (1 << Physical)))
            Tag = 3;
        else if (Exponent == 0x7fff)
            Tag = 2;
        else if (Exponent == 0)
            Tag = (Register->Low == 0) ? 1 : 2;
        else
            Tag = (Register->Low >> 63) ? 0 : 2;

        TagWord |= Tag << (2 * Physical);
    }

    return TagWord;
}

static
VOID
PspFxSaveToFpuSave(
    _In_ PXSAVE_FORMAT FxSave,
    _Out_ WOW64_FLOATING_SAVE_AREA *FpuSave)
{
    ULONG i;

    FpuSave->ControlWord = FxSave->ControlWord;
    FpuSave->StatusWord = FxSave->StatusWord;
    FpuSave->TagWord = PspFullFpuTagWord(FxSave);
    FpuSave->ErrorOffset = FxSave->ErrorOffset;
    FpuSave->ErrorSelector = FxSave->ErrorSelector | ((ULONG)(FxSave->ErrorOpcode & 0x7ff) << 16);
    FpuSave->DataOffset = FxSave->DataOffset;
    FpuSave->DataSelector = FxSave->DataSelector;
    FpuSave->Cr0NpxState = 0;

    for (i = 0; i < 8; i++)
        RtlCopyMemory(&FpuSave->RegisterArea[i * 10], &FxSave->FloatRegisters[i], 10);
}

static
VOID
PspFpuSaveToFxSave(
    _In_ WOW64_FLOATING_SAVE_AREA *FpuSave,
    _Inout_ PXSAVE_FORMAT FxSave)
{
    ULONG i;

    FxSave->ControlWord = (USHORT)FpuSave->ControlWord;
    FxSave->StatusWord = (USHORT)FpuSave->StatusWord;
    FxSave->TagWord = 0;
    for (i = 0; i < 8; i++)
    {
        if (((FpuSave->TagWord >> (2 * i)) & 3) != 3)
            FxSave->TagWord |= (UCHAR)(1 << i);
    }
    FxSave->ErrorOpcode = (USHORT)((FpuSave->ErrorSelector >> 16) & 0x7ff);
    FxSave->ErrorOffset = FpuSave->ErrorOffset;
    FxSave->ErrorSelector = (USHORT)FpuSave->ErrorSelector;
    FxSave->DataOffset = FpuSave->DataOffset;
    FxSave->DataSelector = (USHORT)FpuSave->DataSelector;

    for (i = 0; i < 8; i++)
    {
        RtlZeroMemory(&FxSave->FloatRegisters[i], sizeof(M128A));
        RtlCopyMemory(&FxSave->FloatRegisters[i], &FpuSave->RegisterArea[i * 10], 10);
    }
}

/*
 * The live 32-bit state of a thread. A 32-bit thread runs on the processor
 * itself, so while it is stopped in 32-bit code its state is the one in its
 * trap frame; stopped anywhere else, the caller is told to read the state
 * the 32-bit code left in the CPU area.
 */
NTSTATUS
NTAPI
PspArchCopyLiveWow64Context(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    ULONG Flags = Context->ContextFlags;
    CONTEXT Native;
    NTSTATUS Status;

    PAGED_CODE();

    /* A thread asking about itself is in a system call, so in 64-bit code */
    if (Thread == PsGetCurrentThread()) return STATUS_NOT_FOUND;

    RtlZeroMemory(&Native, sizeof(Native));
    Native.ContextFlags = CONTEXT_ALL | CONTEXT_EXCEPTION_REQUEST;
    Status = PsGetContextThread(Thread, &Native, UserMode);
    if (!NT_SUCCESS(Status)) return Status;

    if (Native.SegCs != (KGDT64_R3_CMCODE | RPL_MASK)) return STATUS_NOT_FOUND;

    if (!SetContext)
    {
        if ((Flags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL)
        {
            Context->Ebp = (ULONG)Native.Rbp;
            Context->Eip = (ULONG)Native.Rip;
            Context->SegCs = Native.SegCs;
            Context->EFlags = Native.EFlags;
            Context->Esp = (ULONG)Native.Rsp;
            Context->SegSs = Native.SegSs;
        }

        if ((Flags & WOW64_CONTEXT_INTEGER) == WOW64_CONTEXT_INTEGER)
        {
            Context->Edi = (ULONG)Native.Rdi;
            Context->Esi = (ULONG)Native.Rsi;
            Context->Ebx = (ULONG)Native.Rbx;
            Context->Edx = (ULONG)Native.Rdx;
            Context->Ecx = (ULONG)Native.Rcx;
            Context->Eax = (ULONG)Native.Rax;
        }

        if ((Flags & WOW64_CONTEXT_SEGMENTS) == WOW64_CONTEXT_SEGMENTS)
        {
            Context->SegGs = Native.SegGs;
            Context->SegFs = Native.SegFs;
            Context->SegEs = Native.SegEs;
            Context->SegDs = Native.SegDs;
        }

        if ((Flags & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT)
            PspFxSaveToFpuSave(&Native.FltSave, &Context->FloatSave);

        if ((Flags & WOW64_CONTEXT_DEBUG_REGISTERS) == WOW64_CONTEXT_DEBUG_REGISTERS)
        {
            Context->Dr0 = (ULONG)Native.Dr0;
            Context->Dr1 = (ULONG)Native.Dr1;
            Context->Dr2 = (ULONG)Native.Dr2;
            Context->Dr3 = (ULONG)Native.Dr3;
            Context->Dr6 = (ULONG)Native.Dr6;
            Context->Dr7 = (ULONG)Native.Dr7;
        }

        if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
            RtlCopyMemory(Context->ExtendedRegisters, &Native.FltSave, sizeof(Context->ExtendedRegisters));

        Context->ContextFlags = Flags & ~(CONTEXT_EXCEPTION_REPORTING |
                                          CONTEXT_SERVICE_ACTIVE |
                                          CONTEXT_EXCEPTION_ACTIVE);
        if ((Flags & CONTEXT_EXCEPTION_REQUEST) != 0)
        {
            Context->ContextFlags |= CONTEXT_EXCEPTION_REPORTING |
                                     (Native.ContextFlags & CONTEXT_EXCEPTION_ACTIVE);
        }

        return STATUS_SUCCESS;
    }

    if ((Flags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL)
    {
        Native.Rbp = Context->Ebp;
        Native.Rip = Context->Eip;
        Native.EFlags = Context->EFlags;
        Native.Rsp = Context->Esp;
    }

    if ((Flags & WOW64_CONTEXT_INTEGER) == WOW64_CONTEXT_INTEGER)
    {
        Native.Rdi = Context->Edi;
        Native.Rsi = Context->Esi;
        Native.Rbx = Context->Ebx;
        Native.Rdx = Context->Edx;
        Native.Rcx = Context->Ecx;
        Native.Rax = Context->Eax;
    }

    if ((Flags & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT)
        PspFpuSaveToFxSave(&Context->FloatSave, &Native.FltSave);

    if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
        RtlCopyMemory(&Native.FltSave, Context->ExtendedRegisters, sizeof(Context->ExtendedRegisters));

    if ((Flags & WOW64_CONTEXT_DEBUG_REGISTERS) == WOW64_CONTEXT_DEBUG_REGISTERS)
    {
        Native.Dr0 = Context->Dr0;
        Native.Dr1 = Context->Dr1;
        Native.Dr2 = Context->Dr2;
        Native.Dr3 = Context->Dr3;
        Native.Dr6 = Context->Dr6;
        Native.Dr7 = Context->Dr7;
    }

    Native.ContextFlags = CONTEXT_ALL;
    return PsSetContextThread(Thread, &Native, UserMode);
}

/* A 32-bit thread's debug registers are the processor's own, which its native state holds */
NTSTATUS
NTAPI
PspArchCopyWow64DebugRegisters(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    CONTEXT Native;
    NTSTATUS Status;

    PAGED_CODE();

    if ((Context->ContextFlags & WOW64_CONTEXT_DEBUG_REGISTERS) != WOW64_CONTEXT_DEBUG_REGISTERS)
        return STATUS_SUCCESS;

    RtlZeroMemory(&Native, sizeof(Native));
    Native.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if (SetContext)
    {
        Native.Dr0 = Context->Dr0;
        Native.Dr1 = Context->Dr1;
        Native.Dr2 = Context->Dr2;
        Native.Dr3 = Context->Dr3;
        Native.Dr6 = Context->Dr6;
        Native.Dr7 = Context->Dr7;
        return PsSetContextThread(Thread, &Native, UserMode);
    }

    Status = PsGetContextThread(Thread, &Native, UserMode);
    if (!NT_SUCCESS(Status)) return Status;

    Context->Dr0 = (ULONG)Native.Dr0;
    Context->Dr1 = (ULONG)Native.Dr1;
    Context->Dr2 = (ULONG)Native.Dr2;
    Context->Dr3 = (ULONG)Native.Dr3;
    Context->Dr6 = (ULONG)Native.Dr6;
    Context->Dr7 = (ULONG)Native.Dr7;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PspArchCaptureXStateContext(
    _In_ PCONTEXT Context,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SetContext,
    _Out_ PVOID *XState)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(SetContext);

    *XState = NULL;

    /* TODO: carry the extended processor state when CONTEXT_XSTATE is asked
     * for; only the parts a plain context holds are carried today */
    if ((ContextFlags & CONTEXT_XSTATE) == CONTEXT_XSTATE)
        return STATUS_NOT_SUPPORTED;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PspArchCompleteXStateContext(
    _In_opt_ PVOID XState,
    _In_ BOOLEAN CopyOut)
{
    UNREFERENCED_PARAMETER(XState);
    UNREFERENCED_PARAMETER(CopyOut);

    return STATUS_SUCCESS;
}

/*
 * Points a process's system-call return at a routine of its own, which the
 * kernel then jumps to instead of returning straight to the caller. The
 * address is only ever used in the process's own context, so a bad one harms
 * nothing but that process.
 */
NTSTATUS
NTAPI
PspArchSetInstrumentationCallback(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PROCESS_INSTRUMENTATION_CALLBACK_INFORMATION Safe;
    PVOID Callback;

    PAGED_CODE();

    /*
     * A caller may pass the bare address, which is what a 32-bit caller does,
     * or the record that names a version alongside it.
     */
    if (Length == sizeof(PVOID))
    {
        RtlZeroMemory(&Safe, sizeof(Safe));

        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(Information, sizeof(PVOID), sizeof(PVOID));
                Safe.Callback = *(PVOID*)Information;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            Safe.Callback = *(PVOID*)Information;
        }
    }
    else if (Length == sizeof(Safe))
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(Information, sizeof(Safe), sizeof(PVOID));
                Safe = *(PPROCESS_INSTRUMENTATION_CALLBACK_INFORMATION)
                           Information;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            Safe = *(PPROCESS_INSTRUMENTATION_CALLBACK_INFORMATION)Information;
        }
    }
    else
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Callback = Safe.Callback;

    /* The routine runs in user mode, so it has to be a user-mode address */
    if (Callback != NULL && (ULONG_PTR)Callback >= MmUserProbeAddress)
        return STATUS_INVALID_PARAMETER;

    Process->Pcb.InstrumentationCallback = Callback;
    return STATUS_SUCCESS;
}
