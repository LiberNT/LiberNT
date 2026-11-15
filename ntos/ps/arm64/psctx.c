/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ps/arm64/psctx.c
 * PURPOSE:         Process Manager: Set/Get Context for ARM64
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/**
 * @brief Get a thread's context from its trap frame
 *
 * This function extracts the thread context from the trap frame and exception
 * frame stored on the thread's kernel stack. On ARM64, the exception frame
 * contains the callee-saved registers (X19-X28, FP, LR) while the trap frame
 * contains the volatile registers and control state.
 *
 * @param TrapFrame Pointer to the thread's trap frame
 * @param NonVolatileContext Pointer to the exception frame (callee-saved regs)
 * @param Context Output context structure to fill
 */
VOID
NTAPI
PspGetContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PVOID NonVolatileContext,
    _Inout_ PCONTEXT Context)
{
    PKEXCEPTION_FRAME ExceptionFrame = (PKEXCEPTION_FRAME)NonVolatileContext;

    /*
     * ARM64: Use KeTrapFrameToContext to properly extract all register state.
     * The exception frame contains X19-X28, FP, LR, and FP state (FPCR/FPSR).
     * The trap frame contains X0-X18, SP, PC, SPSR, and debug registers.
     */
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, Context);
}

/**
 * @brief Set a thread's context into its trap frame
 *
 * This function updates the trap frame and exception frame with values from
 * the provided context structure. The Mode parameter controls which registers
 * can be modified - certain control registers cannot be changed from user mode.
 *
 * @param TrapFrame Pointer to the thread's trap frame to update
 * @param NonVolatileContext Pointer to the exception frame to update
 * @param Context Input context structure with new values
 * @param Mode Processor mode of the caller (affects which fields can be set)
 */
VOID
NTAPI
PspSetContext(
    _Out_ PKTRAP_FRAME TrapFrame,
    _Out_ PVOID NonVolatileContext,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE Mode)
{
    PKEXCEPTION_FRAME ExceptionFrame = (PKEXCEPTION_FRAME)NonVolatileContext;

    /*
     * ARM64: Use KeContextToTrapFrame to properly set all register state.
     * The Mode parameter ensures that:
     * - User mode callers cannot modify privileged SPSR bits
     * - Kernel mode callers have full access to modify the context
     * - The ContextFlags field controls which register groups are updated
     */
    KeContextToTrapFrame(Context, ExceptionFrame, TrapFrame,
                         Context->ContextFlags, Mode);
}

/**
 * @brief APC routine to get or set another thread's context
 *
 * This function is queued as a kernel APC to the target thread. When the APC
 * executes, we are running in the context of the target thread and can safely
 * access its trap frame on the kernel stack.
 *
 * The APC parameters encode whether this is a get or set operation:
 * - SystemArgument1 != 0: Set context (copy Context to trap frame)
 * - SystemArgument1 == 0: Get context (copy trap frame to Context)
 * - SystemArgument2: Pointer to the target KTHREAD
 *
 * After completion, the Event in GET_SET_CTX_CONTEXT is signaled to wake
 * the waiting caller in NtGetContextThread/NtSetContextThread.
 */
_IRQL_requires_(APC_LEVEL)
VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE* NormalRoutine,
    _Inout_ PVOID* NormalContext,
    _Inout_ PVOID* SystemArgument1,
    _Inout_ PVOID* SystemArgument2)
{
    PGET_SET_CTX_CONTEXT GetSetContext;
    PKTHREAD Thread;
    PKTRAP_FRAME TrapFrame = NULL;
    PKEXCEPTION_FRAME ExceptionFrame = NULL;
    ULONG ContextFlags;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);

    /* Get the Context Structure from the APC */
    GetSetContext = CONTAINING_RECORD(Apc, GET_SET_CTX_CONTEXT, Apc);
    Thread = (PKTHREAD)Apc->SystemArgument2;

    /* Verify we are running on the target thread */
    NT_ASSERT(KeGetCurrentThread() == Thread);

    /*
     * Prefer an active user frame regardless of the requestor's mode.  An
     * ARM64 context APC can run from a system call or from the synthesized
     * lower-EL interrupt frame, both of which contain the target's current
     * user register state.  A kernel frame is valid only for a kernel-mode
     * request; user-mode requests fall back to the permanent base frame.
     */
    TrapFrame = Thread->TrapFrame;
    if ((TrapFrame != NULL) &&
        (GetSetContext->Mode != KernelMode) &&
        (KiGetPreviousMode(TrapFrame) != UserMode))
    {
        TrapFrame = KiArm64GetUserTrapFrame(Thread);
    }

    /* Recover the permanent user frame when no suitable frame is active. */
    if (TrapFrame == NULL)
    {
        TrapFrame = KiArm64GetBaseTrapFrame(Thread);
        ExceptionFrame = KeGetExceptionFrame(Thread);
    }
    else if (KiArm64IsServiceHeaderFrame(TrapFrame))
    {
        ExceptionFrame = NULL;
    }
    else if ((ExceptionFrame = KiArm64GetAttachedExceptionFrame(TrapFrame)) == NULL)
    {
        /*
         * If we have a saved TrapFrame, the ExceptionFrame is located
         * immediately before it on the stack (lower address).
         */
        ExceptionFrame = (PKEXCEPTION_FRAME)((ULONG_PTR)TrapFrame -
                                              sizeof(KEXCEPTION_FRAME));
    }

    /* Check if it's a set or get operation */
    if (Apc->SystemArgument1 != NULL)
    {
        /* Set context: Copy from GetSetContext->Context to trap frame */
        PspSetContext(TrapFrame, ExceptionFrame,
                      &GetSetContext->Context, GetSetContext->Mode);
        if (ExceptionFrame == NULL)
        {
            KiArm64SetServiceNonvolatiles(TrapFrame, &GetSetContext->Context);
        }
    }
    else
    {
        /* Get context: Copy from trap frame to GetSetContext->Context */
        PspGetContext(TrapFrame, ExceptionFrame, &GetSetContext->Context);
        if (ExceptionFrame == NULL)
        {
            KiArm64GetServiceNonvolatiles(TrapFrame, &GetSetContext->Context);
        }

        ContextFlags = GetSetContext->Context.ContextFlags &
                       ~(CONTEXT_EXCEPTION_REPORTING | CONTEXT_SERVICE_ACTIVE | CONTEXT_EXCEPTION_ACTIVE);
        if (ContextFlags & CONTEXT_EXCEPTION_REQUEST)
        {
            ContextFlags |= CONTEXT_EXCEPTION_REPORTING;
            if (TrapFrame->ExceptionActive == KEXCEPTION_ACTIVE_SERVICE_FRAME)
            {
                ContextFlags |= CONTEXT_SERVICE_ACTIVE;
            }
            else if (TrapFrame->ExceptionActive == KEXCEPTION_ACTIVE_EXCEPTION_FRAME)
            {
                ContextFlags |= CONTEXT_EXCEPTION_ACTIVE;
            }
        }
        GetSetContext->Context.ContextFlags = ContextFlags;
    }

    /* Signal the waiting thread that we are done */
    KeSetEvent(&GetSetContext->Event, IO_NO_INCREMENT, FALSE);
}

C_ASSERT(FIELD_OFFSET(CONTEXT, ContextFlags) == FIELD_OFFSET(WOW64_CONTEXT, ContextFlags));
C_ASSERT(FIELD_OFFSET(WOW64_CONTEXT, SegSs) < sizeof(CONTEXT));

BOOLEAN
NTAPI
PsArchGetWow64StartContext(
    _In_ PCONTEXT Context,
    _Out_ PULONG Eip,
    _Out_ PULONG Eax,
    _Out_ PULONG Ebx,
    _Out_ PULONG Esp)
{
    PWOW64_CONTEXT Wow64Context = (PWOW64_CONTEXT)Context;

    *Eip = *Eax = *Ebx = *Esp = 0;
    if ((Context->ContextFlags & CONTEXT_ARM64) ||
        !(Wow64Context->ContextFlags & WOW64_CONTEXT_i386))
    {
        return FALSE;
    }

    *Eip = Wow64Context->Eip;
    *Eax = Wow64Context->Eax;
    *Ebx = Wow64Context->Ebx;
    *Esp = Wow64Context->Esp;
    return TRUE;
}

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
    Context->Pc = (ULONG64)(ULONG_PTR)ThreadStart;
    Context->X0 = (ULONG64)(ULONG_PTR)StartRoutine;
    Context->X1 = (ULONG64)(ULONG_PTR)Argument;
    Context->Sp = (ULONG64)(ULONG_PTR)StackBase & ~15ULL;
}

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

    /* TODO: get or set the live x86 state through the emulator backend when it is not in the CPU area */
    return STATUS_NOT_FOUND;
}

NTSTATUS
NTAPI
PspArchSetInstrumentationCallback(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Information);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(PreviousMode);

    return STATUS_INVALID_INFO_CLASS;
}

BOOLEAN
NTAPI
PspArchManagesExecutableWrites(VOID)
{
    return TRUE;
}

