/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 user APCs, user exception dispatch and user mode callbacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kiarm.h"
#define NDEBUG
#include <debug.h>

/*
 * User stack layouts the ntdll dispatchers read (Windows on ARM, as Wine's
 * KiUserApcDispatcher/KiUserExceptionDispatcher/KiUserCallbackDispatcher do):
 *   APC:       routine, context, argument 1, argument 2, alertable, pad, CONTEXT
 *   exception: CONTEXT, EXCEPTION_RECORD
 *   callback:  UCALLOUT_FRAME
 */
typedef struct _KI_ARM_USER_APC_FRAME
{
    ULONG NormalRoutine;
    ULONG NormalContext;
    ULONG SystemArgument1;
    ULONG SystemArgument2;
    ULONG Alertable;
    ULONG Pad;
    CONTEXT Context;
} KI_ARM_USER_APC_FRAME, *PKI_ARM_USER_APC_FRAME;

typedef struct _KI_ARM_USER_EXCEPTION_FRAME
{
    CONTEXT Context;
    EXCEPTION_RECORD ExceptionRecord;
} KI_ARM_USER_EXCEPTION_FRAME, *PKI_ARM_USER_EXCEPTION_FRAME;

C_ASSERT(sizeof(CONTEXT) == 0x1A0);
C_ASSERT(FIELD_OFFSET(KI_ARM_USER_APC_FRAME, Context) == 0x18);
C_ASSERT(FIELD_OFFSET(KI_ARM_USER_EXCEPTION_FRAME, ExceptionRecord) == 0x1A0);

VOID NTAPI KiArmEnterCallout(_Inout_ PKCALLOUT_FRAME CalloutFrame);

/* Point a user trap frame at a Thumb entry of ntdll with a fresh CPSR. */
static
VOID
KiArmRedirectUserTrapFrame(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG_PTR Entry,
    _In_ ULONG_PTR Stack)
{
    TrapFrame->Sp = (ULONG)Stack;
    TrapFrame->Pc = (ULONG)(Entry & ~(ULONG_PTR)1);
    TrapFrame->Cpsr = CPSR_MODE_USER | CPSR_THUMB;
}

VOID
NTAPI
KiInitializeUserApc(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKNORMAL_ROUTINE NormalRoutine,
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2)
{
    KI_ARM_USER_APC_FRAME Local;
    PKI_ARM_USER_APC_FRAME UserFrame;
    EXCEPTION_RECORD Record;
    ULONG_PTR Stack;

    ASSERT(KiIsUserModeTrap(TrapFrame));
    RtlZeroMemory(&Local, sizeof(Local));
    Local.Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Local.Context);
    Local.NormalRoutine = (ULONG)(ULONG_PTR)NormalRoutine;
    Local.NormalContext = (ULONG)(ULONG_PTR)NormalContext;
    Local.SystemArgument1 = (ULONG)(ULONG_PTR)SystemArgument1;
    Local.SystemArgument2 = (ULONG)(ULONG_PTR)SystemArgument2;
    Local.Alertable = TRUE;

    Stack = ((ULONG_PTR)Local.Context.Sp - sizeof(Local)) & ~(ULONG_PTR)7;
    UserFrame = (PKI_ARM_USER_APC_FRAME)Stack;
    _SEH2_TRY
    {
        ProbeForWrite(UserFrame, sizeof(*UserFrame), 8);
        RtlCopyMemory(UserFrame, &Local, sizeof(*UserFrame));
        KiArmRedirectUserTrapFrame(TrapFrame, (ULONG_PTR)KeUserApcDispatcher, Stack);
    }
    _SEH2_EXCEPT((RtlCopyMemory(&Record, _SEH2_GetExceptionInformation()->ExceptionRecord, sizeof(Record)),
                  EXCEPTION_EXECUTE_HANDLER))
    {
        Record.ExceptionAddress = (PVOID)(ULONG_PTR)TrapFrame->Pc;
        KiDispatchException(&Record, ExceptionFrame, TrapFrame, UserMode, TRUE);
    }
    _SEH2_END;
}

BOOLEAN
KiDispatchExceptionToUser(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    PKI_ARM_USER_EXCEPTION_FRAME UserFrame;
    ULONG_PTR Stack;
    BOOLEAN Delivered = TRUE;

    Stack = ((ULONG_PTR)Context->Sp - sizeof(*UserFrame)) & ~(ULONG_PTR)7;
    UserFrame = (PKI_ARM_USER_EXCEPTION_FRAME)Stack;
    _SEH2_TRY
    {
        ProbeForWrite(UserFrame, sizeof(*UserFrame), 8);
        RtlCopyMemory(&UserFrame->Context, Context, sizeof(*Context));
        RtlCopyMemory(&UserFrame->ExceptionRecord, ExceptionRecord, sizeof(*ExceptionRecord));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Delivered = FALSE;
    }
    _SEH2_END;

    if (Delivered)
        KiArmRedirectUserTrapFrame(TrapFrame, (ULONG_PTR)KeUserExceptionDispatcher, Stack);
    return Delivered;
}

VOID
KiSetupUserCalloutFrame(
    _Out_ PUCALLOUT_FRAME UserCalloutFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ApiNumber,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    UserCalloutFrame->Buffer = Buffer;
    UserCalloutFrame->Length = BufferLength;
    UserCalloutFrame->ApiNumber = ApiNumber;
    UserCalloutFrame->OriginalLr = TrapFrame->Lr;
    UserCalloutFrame->MachineFrame.Sp = TrapFrame->Sp;
    UserCalloutFrame->MachineFrame.Pc = TrapFrame->Pc;
}

/*
 * Called by KiArmCallUserMode with the callout frame built on the kernel stack.
 * Copies the caller's user trap record below it, points it at the callback
 * dispatcher and leaves for user mode; NtCallbackReturn comes back through
 * KiCallbackReturn.
 */
DECLSPEC_NORETURN
VOID
NTAPI
KiArmEnterCallout(
    _Inout_ PKCALLOUT_FRAME CalloutFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME CallerFrame = Thread->TrapFrame;
    PKI_ARM_TRAP_RECORD Record;

    ASSERT(CallerFrame && KiIsUserModeTrap(CallerFrame));

    CalloutFrame->TrapFrame = (ULONG)(ULONG_PTR)CallerFrame;
    CalloutFrame->Param5 = (ULONG)(ULONG_PTR)Thread->CallbackStack;
    CalloutFrame->Pad = (ULONG)(ULONG_PTR)Thread->InitialStack;

    Record = (PKI_ARM_TRAP_RECORD)CalloutFrame - 1;
    RtlCopyMemory(Record, KI_ARM_TRAP_RECORD(CallerFrame), sizeof(*Record));
    Record->TrapFrame.VfpState = &Record->VfpState;
    Record->TrapFrame.TrapFrame = (ULONG)(ULONG_PTR)CallerFrame;
    Record->TrapFrame.PreviousMode = UserMode;
    KiArmRedirectUserTrapFrame(&Record->TrapFrame, (ULONG_PTR)KeUserCallbackDispatcher, CallerFrame->Sp);

    _disable();
    Thread->CallbackStack = CalloutFrame;
    Thread->InitialStack = Record + 1;
    Thread->TrapFrame = &Record->TrapFrame;
    KiArmRestoreTrapFrame(&Record->TrapFrame);
}

VOID
KiLeaveCallout(
    _Inout_ PKTHREAD Thread,
    _In_ PKCALLOUT_FRAME CalloutFrame,
    _Inout_ PKTRAP_FRAME CallerFrame,
    _In_ NTSTATUS CallbackStatus)
{
    PKTRAP_FRAME CallbackFrame = Thread->TrapFrame;
    PKI_ARM_TRAP_RECORD CallerRecord = KI_ARM_TRAP_RECORD(CallerFrame);
    ULONG Link;

    /* A callback that faulted in user mode hands back the state it reached. */
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        Link = CallerFrame->TrapFrame;
        RtlCopyMemory(CallerRecord, KI_ARM_TRAP_RECORD(CallbackFrame), sizeof(*CallerRecord));
        CallerFrame->VfpState = &CallerRecord->VfpState;
        CallerFrame->TrapFrame = Link;
    }

    Thread->InitialStack = (PVOID)(ULONG_PTR)CalloutFrame->Pad;
    Thread->CallbackStack = (PVOID)(ULONG_PTR)CalloutFrame->Param5;
}
