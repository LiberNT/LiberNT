/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Where a trap meets the debugger
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#include "../kd/kd.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Which service a breakpoint exception is asking for, or BREAKPOINT_BREAK when
 * it is asking for nothing and is simply a stop.
 */
static
ULONG
KdpDebugService(
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    if (ExceptionRecord->ExceptionCode != (NTSTATUS)STATUS_BREAKPOINT)
        return BREAKPOINT_BREAK;

    if (ExceptionRecord->NumberParameters == 0)
        return BREAKPOINT_BREAK;

    return (ULONG)ExceptionRecord->ExceptionInformation[0];
}

/*
 * A service request is made by an instruction that the architecture follows
 * with a breakpoint, so that a machine with no debugger stops instead of
 * running on. Once the request has been served that breakpoint is stepped
 * over, because it was never meant to be hit.
 */
static
VOID
KdpStepPastServiceBreakpoint(
    _Inout_ PCONTEXT Context)
{
    KeSetContextPc(Context, KeGetContextPc(Context) + KD_BREAKPOINT_SIZE);
}

/*
 * The component and the level a print service is asking for. The older form of
 * the request carries neither, and what it prints is what a caller with no
 * component of its own prints: the default component, at the level that is
 * always shown.
 */
static
VOID
KdpPrintServiceComponent(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Out_ PULONG ComponentId,
    _Out_ PULONG Level)
{
    if (ExceptionRecord->NumberParameters > 4)
    {
        *ComponentId = (ULONG)ExceptionRecord->ExceptionInformation[3];
        *Level = (ULONG)ExceptionRecord->ExceptionInformation[4];
        return;
    }

    *ComponentId = DPFLTR_DEFAULT_ID;
    *Level = DPFLTR_ERROR_LEVEL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Whether this exception is the system asking the debugger for something,
 * rather than something having gone wrong. The caller uses this to send the
 * exception to the debugger before anything else looks at it.
 */
BOOLEAN
NTAPI
KdIsThisAKdTrap(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);

    switch (KdpDebugService(ExceptionRecord))
    {
        case BREAKPOINT_PRINT:
        case BREAKPOINT_PROMPT:
        case BREAKPOINT_LOAD_SYMBOLS:
        case BREAKPOINT_UNLOAD_SYMBOLS:
        case BREAKPOINT_COMMAND_STRING:
            return TRUE;

        default:
            return FALSE;
    }
}

/*
 * Stops the machine, tells the host, and comes back with the host's answer.
 * TRUE means the host dealt with the exception and the system is to carry on
 * as if it had not happened.
 */
BOOLEAN
NTAPI
KdpReport(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PCONTEXT ContextRecord,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SecondChanceException)
{
    BOOLEAN Enable;
    BOOLEAN Handled;

    /* An exception in a process somebody else is debugging is not ours */
    if ((PreviousMode == UserMode) && KdIgnoreUmExceptions)
        return FALSE;

    if (!KdDebuggerEnabled)
    {
        /*
         * The machine may have been started with a debugger that waits for
         * something to go wrong before it speaks.
         */
        if (!KdAutoEnableOnEvent)
            return FALSE;

        if (!NT_SUCCESS(KdEnableDebuggerWithLock(FALSE)))
            return FALSE;
    }

    Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);

    KdpReportedMode = PreviousMode;
    KdpReportedFirstChance = !SecondChanceException;

    Handled = KdpReportExceptionStateChange(ExceptionRecord,
                                            ContextRecord,
                                            SecondChanceException);

    /* Breakpoints the host set while stopped may still be owed to memory */
    KdSetOwedBreakpoints();

    KdExitDebugger(Enable);

    /*
     * The machine was held for as long as somebody took to read a screen, and
     * whoever keeps the time has to be told.
     */
    if (KdpTimeSlipEvent != NULL)
        KeInsertQueueDpc(&KdpTimeSlipDpc, NULL, NULL);

    return Handled;
}

/*
 * The trap handler's way into the debugger while it is enabled. A request is
 * served and the system carries on; anything else stops the machine.
 */
BOOLEAN
NTAPI
KdpTrap(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PCONTEXT ContextRecord,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SecondChanceException)
{
    BOOLEAN Handled = FALSE;
    ULONG ComponentId;
    ULONG Level;
    NTSTATUS Status;
    USHORT Length;

    switch (KdpDebugService(ExceptionRecord))
    {
        case BREAKPOINT_PRINT:

            KdpPrintServiceComponent(ExceptionRecord, &ComponentId, &Level);

            Status = KdpPrint(ComponentId,
                              Level,
                              (PCHAR)ExceptionRecord->ExceptionInformation[1],
                              (USHORT)ExceptionRecord->ExceptionInformation[2],
                              PreviousMode,
                              TrapFrame,
                              ExceptionFrame,
                              &Handled);

            /* The caller reads what came of it where a return value goes */
            KeSetContextReturnRegister(ContextRecord, (ULONG_PTR)Status);
            KdpStepPastServiceBreakpoint(ContextRecord);
            return TRUE;

        case BREAKPOINT_PROMPT:

            Length = KdpPrompt((PCHAR)ExceptionRecord->ExceptionInformation[1],
                               (USHORT)ExceptionRecord->ExceptionInformation[2],
                               (PCHAR)ExceptionRecord->ExceptionInformation[3],
                               (USHORT)ExceptionRecord->ExceptionInformation[4],
                               PreviousMode,
                               TrapFrame,
                               ExceptionFrame);

            KeSetContextReturnRegister(ContextRecord, (ULONG_PTR)Length);
            KdpStepPastServiceBreakpoint(ContextRecord);
            return TRUE;

        case BREAKPOINT_LOAD_SYMBOLS:
        case BREAKPOINT_UNLOAD_SYMBOLS:

            KdpSymbol((PSTRING)ExceptionRecord->ExceptionInformation[1],
                      (PKD_SYMBOLS_INFO)ExceptionRecord->ExceptionInformation[2],
                      (KdpDebugService(ExceptionRecord) == BREAKPOINT_UNLOAD_SYMBOLS),
                      PreviousMode,
                      ContextRecord,
                      TrapFrame,
                      ExceptionFrame);

            KeSetContextReturnRegister(ContextRecord, (ULONG_PTR)STATUS_SUCCESS);
            KdpStepPastServiceBreakpoint(ContextRecord);
            return TRUE;

        case BREAKPOINT_COMMAND_STRING:

            KdpCommandString((PSTRING)ExceptionRecord->ExceptionInformation[1],
                             (PSTRING)ExceptionRecord->ExceptionInformation[2],
                             PreviousMode,
                             ContextRecord,
                             TrapFrame,
                             ExceptionFrame);

            KeSetContextReturnRegister(ContextRecord, (ULONG_PTR)STATUS_SUCCESS);
            KdpStepPastServiceBreakpoint(ContextRecord);
            return TRUE;

        default:
            break;
    }

    return KdpReport(TrapFrame,
                     ExceptionFrame,
                     ExceptionRecord,
                     ContextRecord,
                     PreviousMode,
                     SecondChanceException);
}

/*
 * The trap handler's way into a debugger that is not there. The requests still
 * have to be answered, because the code that makes them was written to be able
 * to run on a machine with no debugger at all; a real exception is left to
 * whoever handles exceptions.
 */
BOOLEAN
NTAPI
KdpStub(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PCONTEXT ContextRecord,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SecondChanceException)
{
    ULONG ComponentId;
    ULONG Level;
    STRING Output;

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(SecondChanceException);

    switch (KdpDebugService(ExceptionRecord))
    {
        case BREAKPOINT_PRINT:

            /*
             * Nobody is listening, but the log is read out of a crash dump
             * later, so what was printed is kept. Only a caller already in
             * the kernel is taken at its word about where its string is.
             */
            if (PreviousMode == KernelMode)
            {
                KdpPrintServiceComponent(ExceptionRecord, &ComponentId, &Level);

                Output.Buffer = (PCHAR)ExceptionRecord->ExceptionInformation[1];
                Output.Length = (USHORT)ExceptionRecord->ExceptionInformation[2];
                Output.MaximumLength = Output.Length;

                KdLogDbgPrint(&Output);
                KdpPrintToCallbacks(ComponentId,
                                    Level,
                                    Output.Buffer,
                                    Output.Length,
                                    PreviousMode);
            }

            KeSetContextReturnRegister(ContextRecord,
                                       (ULONG_PTR)STATUS_DEVICE_NOT_CONNECTED);
            KdpStepPastServiceBreakpoint(ContextRecord);
            return TRUE;

        case BREAKPOINT_PROMPT:

            /* Nothing can be read back, so nothing was read */
            KeSetContextReturnRegister(ContextRecord, 0);
            KdpStepPastServiceBreakpoint(ContextRecord);
            return TRUE;

        case BREAKPOINT_LOAD_SYMBOLS:
        case BREAKPOINT_UNLOAD_SYMBOLS:
        case BREAKPOINT_COMMAND_STRING:

            KeSetContextReturnRegister(ContextRecord, (ULONG_PTR)STATUS_SUCCESS);
            KdpStepPastServiceBreakpoint(ContextRecord);
            return TRUE;

        default:
            break;
    }

    return FALSE;
}

/* EOF */
