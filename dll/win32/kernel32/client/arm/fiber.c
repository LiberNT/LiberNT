/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 fiber context switch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

NTSYSAPI
DECLSPEC_NORETURN
VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _EXCEPTION_RECORD *ExceptionRecord);

DECLSPEC_NORETURN
VOID
WINAPI
BaseFiberStartup(VOID)
{
    PFIBER Fiber = (PFIBER)NtCurrentTeb()->NtTib.FiberData;

    BaseThreadStartup((LPTHREAD_START_ROUTINE)(ULONG_PTR)Fiber->FiberContext.R0,
                      (LPVOID)(ULONG_PTR)Fiber->FiberContext.R1);
}

VOID
WINAPI
SwitchToFiber(_In_ LPVOID Fiber)
{
    PTEB Teb = NtCurrentTeb();
    PFIBER CurrentFiber = (PFIBER)Teb->NtTib.FiberData;
    PFIBER NewFiber = (PFIBER)Fiber;
    volatile BOOLEAN Switched = FALSE;

    ASSERT(CurrentFiber != NULL && NewFiber != NULL);

    CurrentFiber->FlsData = Teb->FlsData;
    CurrentFiber->ActivationContextStackPointer = Teb->ActivationContextStackPointer;
    CurrentFiber->ExceptionList = Teb->NtTib.ExceptionList;
    CurrentFiber->StackBase = Teb->NtTib.StackBase;
    CurrentFiber->StackLimit = Teb->NtTib.StackLimit;
    CurrentFiber->DeallocationStack = Teb->DeallocationStack;
    CurrentFiber->GuaranteedStackBytes = Teb->GuaranteedStackBytes;

    /* A later switch back resumes here, on this fiber's own stack. */
    CurrentFiber->FiberContext.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&CurrentFiber->FiberContext);
    if (Switched)
        return;
    Switched = TRUE;

    Teb->NtTib.FiberData = NewFiber;
    Teb->NtTib.ExceptionList = NewFiber->ExceptionList;
    Teb->NtTib.StackBase = NewFiber->StackBase;
    Teb->NtTib.StackLimit = NewFiber->StackLimit;
    Teb->DeallocationStack = NewFiber->DeallocationStack;
    Teb->GuaranteedStackBytes = NewFiber->GuaranteedStackBytes;
    Teb->ActivationContextStackPointer = NewFiber->ActivationContextStackPointer;
    Teb->FlsData = NewFiber->FlsData;

    RtlRestoreContext(&NewFiber->FiberContext, NULL);
}
