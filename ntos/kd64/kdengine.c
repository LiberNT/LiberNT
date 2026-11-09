/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The conversation with the host: what stopped, and what to do next
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#include "../kd/kd.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * The stop the host is being told about, and whatever travels with it. They
 * are kept apart from the request and reply buffers because a state change is
 * repeated as often as the host needs it, long after a reply has reused them.
 */
static DBGKD_ANY_WAIT_STATE_CHANGE KdpStateChange;
static CHAR KdpStateChangeData[KDP_MSG_BUFFER_SIZE];

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Puts an exception into the shape the host reads, whatever this kernel's
 * pointer width is.
 */
static
VOID
KdpExceptionRecordToWire(
    _In_ PEXCEPTION_RECORD Record,
    _Out_ PEXCEPTION_RECORD64 Wire)
{
    ULONG Index;

    Wire->ExceptionCode = Record->ExceptionCode;
    Wire->ExceptionFlags = Record->ExceptionFlags;
    Wire->ExceptionRecord = (ULONG64)(ULONG_PTR)Record->ExceptionRecord;
    Wire->ExceptionAddress = (ULONG64)(ULONG_PTR)Record->ExceptionAddress;
    Wire->NumberParameters = Record->NumberParameters;
    Wire->__unusedAlignment = 0;

    for (Index = 0; Index < EXCEPTION_MAXIMUM_PARAMETERS; Index++)
    {
        Wire->ExceptionInformation[Index] =
            (ULONG64)Record->ExceptionInformation[Index];
    }
}

/*
 * Sends the answer to one request. The header always goes back, so that the
 * host can match the answer to what it asked and read the status; the body
 * only goes when the request has one to return.
 */
VOID
NTAPI
KdpSendManipulateReply(
    _In_ PDBGKD_MANIPULATE_STATE64 State,
    _In_opt_ PSTRING Data)
{
    STRING Header;

    Header.Length = sizeof(DBGKD_MANIPULATE_STATE64);
    Header.MaximumLength = sizeof(DBGKD_MANIPULATE_STATE64);
    Header.Buffer = (PCHAR)State;

    KdSendPacket(PACKET_TYPE_KD_STATE_MANIPULATE, &Header, Data, &KdpContext);
}

/*
 * Fills in what every stop has to say: which processor stopped, what it was
 * running, and the instructions around the stop so the host has something to
 * show before it has read any memory of its own.
 */
VOID
NTAPI
KdpSetCommonState(
    _In_ ULONG NewState,
    _In_ PCONTEXT Context,
    _Out_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange)
{
    ULONG64 ProgramCounter;
    ULONG Copied = 0;

    KdpZeroMemory(WaitStateChange, sizeof(DBGKD_ANY_WAIT_STATE_CHANGE));

    ProgramCounter = (ULONG64)(ULONG_PTR)KeGetContextPc(Context);

    WaitStateChange->NewState = NewState;
    WaitStateChange->ProcessorLevel = KeProcessorLevel;
    WaitStateChange->Processor = (USHORT)KeGetCurrentPrcb()->Number;
    WaitStateChange->NumberProcessors = (ULONG)(CCHAR)KeNumberProcessors;
    WaitStateChange->Thread = (ULONG64)(ULONG_PTR)KeGetCurrentThread();
    WaitStateChange->ProgramCounter = ProgramCounter;

    /*
     * The window of instructions is best effort: a program counter that is not
     * readable is exactly the case the host most needs to be told about, so a
     * short or empty window is reported instead of being treated as a failure.
     */
    KdpCopyMemoryChunks(ProgramCounter,
                        &WaitStateChange->ControlReport.InstructionStream[0],
                        DBGKD_MAXSTREAM,
                        0,
                        MMDBG_COPY_UNSAFE,
                        &Copied);

    WaitStateChange->ControlReport.InstructionCount = (USHORT)Copied;

    /* Whatever else this processor has to report about the stop */
    KdpSetContextState(WaitStateChange, Context);
}

/*
 * Answers one request from the host. Returns TRUE while the host is still in
 * charge, and FALSE once it has told the machine to go on; *WaitResult then
 * says how.
 */
static
BOOLEAN
KdpDispatchManipulate(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PSTRING Data,
    _Inout_ PCONTEXT Context,
    _Out_ KCONTINUE_STATUS *WaitResult)
{
    *WaitResult = ContinueSuccess;

    switch (State->ApiNumber)
    {
        case DbgKdReadVirtualMemoryApi:
            KdpReadVirtualMemory(State, Data, Context);
            return TRUE;

        case DbgKdWriteVirtualMemoryApi:
            KdpWriteVirtualMemory(State, Data, Context);
            return TRUE;

        case DbgKdReadPhysicalMemoryApi:
            KdpReadPhysicalMemory(State, Data, Context);
            return TRUE;

        case DbgKdWritePhysicalMemoryApi:
            KdpWritePhysicalMemory(State, Data, Context);
            return TRUE;

        case DbgKdGetContextApi:
            KdpGetContext(State, Data, Context);
            return TRUE;

        case DbgKdSetContextApi:
            KdpSetContext(State, Data, Context);
            return TRUE;

        case DbgKdGetContextExApi:
            KdpGetContextEx(State, Data, Context);
            return TRUE;

        case DbgKdSetContextExApi:
            KdpSetContextEx(State, Data, Context);
            return TRUE;

        case DbgKdWriteBreakPointApi:
            KdpWriteBreakpoint(State, Data, Context);
            return TRUE;

        case DbgKdRestoreBreakPointApi:
            KdpRestoreBreakpoint(State, Data, Context);
            return TRUE;

        case DbgKdWriteBreakPointExApi:
            /* The bulk form resumes the machine when it was served */
            if (KdpWriteBreakPointEx(State, Data, Context))
            {
                *WaitResult =
                    (State->u.BreakPointEx.ContinueStatus == (NTSTATUS)DBG_CONTINUE)
                    ? ContinueSuccess : ContinueError;
                return FALSE;
            }
            return TRUE;

        case DbgKdRestoreBreakPointExApi:
            if (KdpRestoreBreakPointEx(State, Data, Context))
            {
                *WaitResult =
                    (State->u.BreakPointEx.ContinueStatus == (NTSTATUS)DBG_CONTINUE)
                    ? ContinueSuccess : ContinueError;
                return FALSE;
            }
            return TRUE;

        case DbgKdWriteCustomBreakpointApi:
            KdpWriteCustomBreakpoint(State, Data, Context);
            return TRUE;

        case DbgKdReadControlSpaceApi:
            KdpReadControlSpace(State, Data, Context);
            return TRUE;

        case DbgKdWriteControlSpaceApi:
            KdpWriteControlSpace(State, Data, Context);
            return TRUE;

        case DbgKdReadIoSpaceApi:
            KdpReadIoSpace(State, Data, Context, FALSE);
            return TRUE;

        case DbgKdWriteIoSpaceApi:
            KdpWriteIoSpace(State, Data, Context, FALSE);
            return TRUE;

        case DbgKdReadIoSpaceExtendedApi:
            KdpReadIoSpace(State, Data, Context, TRUE);
            return TRUE;

        case DbgKdWriteIoSpaceExtendedApi:
            KdpWriteIoSpace(State, Data, Context, TRUE);
            return TRUE;

        case DbgKdReadMachineSpecificRegister:
            KdpReadMachineSpecificRegister(State, Data, Context);
            return TRUE;

        case DbgKdWriteMachineSpecificRegister:
            KdpWriteMachineSpecificRegister(State, Data, Context);
            return TRUE;

        case DbgKdGetBusDataApi:
            KdpGetBusData(State, Data, Context);
            return TRUE;

        case DbgKdSetBusDataApi:
            KdpSetBusData(State, Data, Context);
            return TRUE;

        case DbgKdSearchMemoryApi:
            KdpSearchMemory(State, Data, Context);
            return TRUE;

        case DbgKdFillMemoryApi:
            KdpFillMemory(State, Data, Context);
            return TRUE;

        case DbgKdQueryMemoryApi:
            KdpQueryMemory(State, Context);
            return TRUE;

        case DbgKdCheckLowMemoryApi:
            KdpCheckLowMemory(State);
            return TRUE;

        case DbgKdGetVersionApi:
            KdpGetVersion(State);
            return TRUE;

        case DbgKdClearAllInternalBreakpointsApi:
            /*
             * The host does not expect an answer to this one. LiberNT keeps no
             * internal breakpoints, so there is nothing to clear either.
             */
            KdpNumInternalBreakpoints = 0;
            return TRUE;

        case DbgKdCauseBugCheckApi:
            /* This one does not come back */
            KdpCauseBugCheck(State);
            return TRUE;

        case DbgKdRebootApi:
            /* Neither does this one */
            HalReturnToFirmware(HalRebootRoutine);
            return TRUE;

        case DbgKdContinueApi:
            *WaitResult = (State->u.Continue.ContinueStatus == (NTSTATUS)DBG_CONTINUE)
                          ? ContinueSuccess : ContinueError;
            return FALSE;

        case DbgKdContinueApi2:
            /* The host may change how this processor runs on the way out */
            KdpGetStateChange(State, Context);
            *WaitResult = (State->u.Continue2.ContinueStatus == (NTSTATUS)DBG_CONTINUE)
                          ? ContinueSuccess : ContinueError;
            return FALSE;

        case DbgKdSwitchProcessor:
            /* Another processor takes over the conversation */
            *WaitResult = ContinueNextProcessor;
            return FALSE;

        default:
            /*
             * Special calls, internal breakpoints, page-in requests and
             * partition switching are all answered the same way: this kernel
             * does not do them.
             */
            /* TODO: internal breakpoints, special calls, DbgKdPageInApi and
             * DbgKdSwitchPartition are reported unsupported rather than served */
            KdpNotSupported(State);
            return TRUE;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Tells the host what happened and then serves it until it says to go on.
 * The caller owns the debug link for the whole exchange.
 */
KCONTINUE_STATUS
NTAPI
KdpSendWaitContinue(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PCONTEXT Context)
{
    STRING Header;
    STRING Data;
    PDBGKD_MANIPULATE_STATE64 State;
    KDSTATUS Status;
    KCONTINUE_STATUS WaitResult = ContinueSuccess;
    ULONG Length;
    ULONG Retries = KdpContext.KdpDefaultRetries;
    BOOLEAN Announce = TRUE;

    /* The request arrives in the message buffer and the answer is built there */
    State = (PDBGKD_MANIPULATE_STATE64)KdpMessageBuffer;

    /* Whatever the host last asked about, the machine is stopped now */
    KdpContextSent = FALSE;

    /* These are the registers the report is about */
    KdpReportedContext = Context;

    for (;;)
    {
        if (Announce)
        {
            KdSendPacket(PacketType, MessageHeader, MessageData, &KdpContext);
            Announce = FALSE;
        }

        Header.Length = 0;
        Header.MaximumLength = sizeof(DBGKD_MANIPULATE_STATE64);
        Header.Buffer = (PCHAR)State;

        Data.Length = 0;
        Data.MaximumLength = KDP_RESPONSE_SIZE;
        Data.Buffer = KdpResponseBuffer;

        Length = 0;
        Status = KdReceivePacket(PACKET_TYPE_KD_STATE_MANIPULATE,
                                 &Header,
                                 &Data,
                                 &Length,
                                 &KdpContext);

        if (Status == KdPacketNeedsResend)
        {
            /* The host missed what happened: say it again */
            Announce = TRUE;
            continue;
        }

        if (Status == KdPacketTimedOut)
        {
            /*
             * Nothing came back. Either the host is slow or it is gone; the
             * state change is repeated a bounded number of times and then the
             * machine goes on by itself rather than stopping forever.
             */
            if (Retries == 0)
            {
                KdDebuggerNotPresent = TRUE;
                KdpContext.KdpControlCPending = FALSE;
                return ContinueSuccess;
            }

            Retries--;
            Announce = TRUE;
            continue;
        }

        if (Status != KdPacketReceived)
            continue;

        /* Something answered, so there is a host on the other end */
        KdDebuggerNotPresent = FALSE;
        Retries = KdpContext.KdpDefaultRetries;

        if (Header.Length < sizeof(DBGKD_MANIPULATE_STATE64))
        {
            /* A short header says nothing that can be acted on */
            continue;
        }

        Data.Length = (USHORT)Length;

        if (!KdpDispatchManipulate(State, &Data, Context, &WaitResult))
            return WaitResult;
    }
}

/*
 * The machine stopped on an exception. Reports it and waits for the host's
 * decision; TRUE means the host dealt with it.
 */
BOOLEAN
NTAPI
KdpReportExceptionStateChange(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN SecondChanceException)
{
    STRING Header;
    PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange;
    KCONTINUE_STATUS Status;

    WaitStateChange = &KdpStateChange;

    Header.Length = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.MaximumLength = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.Buffer = (PCHAR)WaitStateChange;

    for (;;)
    {
        KdpSetCommonState(DbgKdExceptionStateChange, Context, WaitStateChange);

        KdpExceptionRecordToWire(ExceptionRecord,
                                 &WaitStateChange->u.Exception.ExceptionRecord);
        WaitStateChange->u.Exception.FirstChance = !SecondChanceException;

        Status = KdpSendWaitContinue(PACKET_TYPE_KD_STATE_CHANGE64,
                                     &Header,
                                     NULL,
                                     Context);

        /*
         * A processor switch means this processor is no longer the one the
         * host is talking to; the caller sorts that out and comes back here
         * when it is this processor's turn again.
         */
        if (Status != ContinueProcessorReselected)
            return (Status == ContinueSuccess);
    }
}

/*
 * A module was loaded or unloaded. The host is told so it can find symbols,
 * and it may stop the machine right there when it was asked to.
 */
VOID
NTAPI
KdpReportLoadSymbolsStateChange(
    _In_ PSTRING PathName,
    _In_ PKD_SYMBOLS_INFO SymbolInfo,
    _In_ BOOLEAN Unload,
    _Inout_ PCONTEXT Context)
{
    STRING Header;
    STRING Data;
    PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange;
    KCONTINUE_STATUS Status;
    ULONG PathLength = 0;

    WaitStateChange = &KdpStateChange;

    Header.Length = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.MaximumLength = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.Buffer = (PCHAR)WaitStateChange;

    for (;;)
    {
        KdpSetCommonState(DbgKdLoadSymbolsStateChange, Context, WaitStateChange);

        WaitStateChange->u.LoadSymbols.UnloadSymbols = Unload;
        WaitStateChange->u.LoadSymbols.BaseOfDll =
            (ULONG64)(ULONG_PTR)SymbolInfo->BaseOfDll;
        WaitStateChange->u.LoadSymbols.ProcessId = SymbolInfo->ProcessId;
        WaitStateChange->u.LoadSymbols.CheckSum = SymbolInfo->CheckSum;
        WaitStateChange->u.LoadSymbols.SizeOfImage = SymbolInfo->SizeOfImage;

        /*
         * The path travels after the header. It is sent as it was handed in,
         * terminator included, because that is what the host expects to find.
         */
        if ((PathName != NULL) && (PathName->Buffer != NULL))
            PathLength = PathName->Length;

        WaitStateChange->u.LoadSymbols.PathNameLength = PathLength;

        Data.Length = (USHORT)PathLength;
        Data.MaximumLength = (USHORT)PathLength;
        Data.Buffer = (PathLength != 0) ? PathName->Buffer : NULL;

        Status = KdpSendWaitContinue(PACKET_TYPE_KD_STATE_CHANGE64,
                                     &Header,
                                     (PathLength != 0) ? &Data : NULL,
                                     Context);

        if (Status != ContinueProcessorReselected)
            return;
    }
}

/*
 * A component asked the host to run a debugger command on its behalf.
 */
VOID
NTAPI
KdpReportCommandStringStateChange(
    _In_ PSTRING NameString,
    _In_ PSTRING CommandString,
    _Inout_ PCONTEXT Context)
{
    STRING Header;
    STRING Data;
    PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange;
    KCONTINUE_STATUS Status;
    ULONG Length;
    ULONG NameLength;
    ULONG CommandLength;

    WaitStateChange = &KdpStateChange;

    Header.Length = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.MaximumLength = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.Buffer = (PCHAR)WaitStateChange;

    for (;;)
    {
        KdpSetCommonState(DbgKdCommandStringStateChange, Context, WaitStateChange);

        WaitStateChange->u.CommandString.Flags = 0;
        WaitStateChange->u.CommandString.Reserved1 = 0;
        KdpZeroMemory(&WaitStateChange->u.CommandString.Reserved2[0],
                      sizeof(WaitStateChange->u.CommandString.Reserved2));

        /*
         * Both strings follow the header, one after the other, each with its
         * terminator, since the host tells them apart by scanning for it.
         */
        NameLength = (NameString != NULL) ? NameString->Length : 0;
        CommandLength = (CommandString != NULL) ? CommandString->Length : 0;

        Length = 0;
        if (NameLength + CommandLength + 2 <= sizeof(KdpStateChangeData))
        {
            if (NameLength != 0)
            {
                KdpMoveMemory(&KdpStateChangeData[Length],
                              NameString->Buffer,
                              NameLength);
                Length += NameLength;
            }
            KdpStateChangeData[Length++] = ANSI_NULL;

            if (CommandLength != 0)
            {
                KdpMoveMemory(&KdpStateChangeData[Length],
                              CommandString->Buffer,
                              CommandLength);
                Length += CommandLength;
            }
            KdpStateChangeData[Length++] = ANSI_NULL;
        }

        Data.Length = (USHORT)Length;
        Data.MaximumLength = (USHORT)Length;
        Data.Buffer = KdpStateChangeData;

        Status = KdpSendWaitContinue(PACKET_TYPE_KD_STATE_CHANGE64,
                                     &Header,
                                     &Data,
                                     Context);

        if (Status != ContinueProcessorReselected)
            return;
    }
}

/*
 * Another processor wants to be the one the host talks to. Reports the stop
 * from this processor and lets the host choose.
 */
KCONTINUE_STATUS
NTAPI
KdReportProcessorChange(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PCONTEXT Context = &Prcb->ProcessorState.ContextFrame;
    STRING Header;
    PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange;
    EXCEPTION_RECORD64 Exception;

    WaitStateChange = &KdpStateChange;

    Header.Length = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.MaximumLength = sizeof(DBGKD_ANY_WAIT_STATE_CHANGE);
    Header.Buffer = (PCHAR)WaitStateChange;

    KdpSetCommonState(DbgKdExceptionStateChange, Context, WaitStateChange);

    /*
     * Nothing went wrong on this processor; it is reported as a plain
     * breakpoint so that the host shows it as a stop and not as a fault.
     */
    KdpZeroMemory(&Exception, sizeof(Exception));
    Exception.ExceptionCode = STATUS_BREAKPOINT;
    Exception.ExceptionAddress = WaitStateChange->ProgramCounter;

    WaitStateChange->u.Exception.ExceptionRecord = Exception;
    WaitStateChange->u.Exception.FirstChance = TRUE;

    return KdpSendWaitContinue(PACKET_TYPE_KD_STATE_CHANGE64,
                               &Header,
                               NULL,
                               Context);
}

/* EOF */
