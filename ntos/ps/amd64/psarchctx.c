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

            if (Frame->ExceptionActive == 0)
                Request->Context.ContextFlags |= CONTEXT_SERVICE_ACTIVE;
            else if (Frame->ExceptionActive == 1)
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

    /*
     * The stack is left so that the first push lands on a sixteen-byte
     * boundary, which is what the calling convention promises a callee.
     */
    Context->Rsp = ((ULONG64)(ULONG_PTR)StackBase & ~15ULL) - 8;

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
 * Where a user thread's first return to user mode is aimed: at the native
 * image's entry, which is given the image base so it can set itself up
 * before the thread's own entry is reached.
 */
VOID
NTAPI
PspArchInitializeUserThreadStartup(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KiInitializeUserApc(ExceptionFrame,
                        TrapFrame,
                        PspSystemDllEntryPoint,
                        NULL,
                        PspSystemDllBase,
                        NULL);
}

/*
 * The live 32-bit state of a thread running under emulation. On this
 * architecture a 32-bit thread runs on the processor itself, so its state is
 * in its trap frame and the caller is told to read it there.
 */
NTSTATUS
NTAPI
PspArchCopyLiveWow64Context(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SetContext);

    return STATUS_NOT_FOUND;
}

/*
 * A 32-bit thread's debug registers are the processor's own, so they are
 * already in the state the caller was given.
 */
NTSTATUS
NTAPI
PspArchCopyWow64DebugRegisters(
    _In_ PETHREAD Thread,
    _Inout_ PWOW64_CONTEXT Context,
    _In_ BOOLEAN SetContext)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SetContext);

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
