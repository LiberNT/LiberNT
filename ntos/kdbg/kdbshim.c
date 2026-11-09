/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Answering the debugger engine from inside the same kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"

/*
 * Inside this file both halves are needed: the names below are what the engine
 * calls, and what they fall back on is the link itself.
 */
#undef KdSendPacket
#undef KdReceivePacket
#undef KdD0Transition
#undef KdD3Transition
#undef KdSave
#undef KdRestore

#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* The stop the engine last reported and has not yet been answered about */
static DBGKD_ANY_WAIT_STATE_CHANGE KdbgStateChange;
static BOOLEAN KdbgStateChangePending = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Says that an image has arrived or gone, which is all there is to do about it
 * when the debugger reading the symbols is this kernel's own.
 */
static
VOID
KdbgReportLoadSymbols(
    _In_ PDBGKD_ANY_WAIT_STATE_CHANGE StateChange,
    _In_opt_ PSTRING Path)
{
    PVOID ImageBase = (PVOID)(ULONG_PTR)StateChange->u.LoadSymbols.BaseOfDll;

    if (StateChange->u.LoadSymbols.UnloadSymbols)
        return;

    /* The symbol table of a new image is made ready to be read */
    KdbSymzPrepareImage(ImageBase);

    if (!KdBreakAfterSymbolLoad)
        return;

    if ((Path != NULL) && (Path->Buffer != NULL))
        KdbPrintf("Loaded %s at %p\n", Path->Buffer, ImageBase);
    else
        KdbPrintf("Loaded an image at %p\n", ImageBase);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The engine saying something. A stop is remembered until the engine asks what
 * to do about it; output is shown through the interactive debugger, so that
 * what it prints and what the system prints are paged together; anything else
 * belongs to the link.
 */
VOID
NTAPI
KdbgSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
{
    switch (PacketType)
    {
        case PACKET_TYPE_KD_STATE_CHANGE64:

            if ((MessageHeader == NULL) ||
                (MessageHeader->Buffer == NULL) ||
                (MessageHeader->Length < sizeof(DBGKD_ANY_WAIT_STATE_CHANGE)))
            {
                return;
            }

            KdbgStateChange =
                *(PDBGKD_ANY_WAIT_STATE_CHANGE)MessageHeader->Buffer;
            KdbgStateChangePending = TRUE;

            if (KdbgStateChange.NewState == DbgKdLoadSymbolsStateChange)
                KdbgReportLoadSymbols(&KdbgStateChange, MessageData);

            return;

        case PACKET_TYPE_KD_DEBUG_IO:
        {
            PDBGKD_DEBUG_IO DebugIo;

            if ((MessageHeader == NULL) ||
                (MessageHeader->Buffer == NULL) ||
                (MessageHeader->Length < sizeof(DBGKD_DEBUG_IO)))
            {
                return;
            }

            DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;

            if ((DebugIo->ApiNumber != DbgKdPrintStringApi) &&
                (DebugIo->ApiNumber != DbgKdGetStringApi))
            {
                return;
            }

            if ((MessageData != NULL) && (MessageData->Buffer != NULL))
                KdbPutsN(MessageData->Buffer, MessageData->Length);

            return;
        }

        default:
            KdSendPacket(PacketType, MessageHeader, MessageData, Context);
            return;
    }
}

/*
 * The engine asking what to do. A stop is where the interactive debugger
 * takes over; when it lets go, the engine is told to carry on.
 */
KDSTATUS
NTAPI
KdbgReceivePacket(
    _In_ ULONG PacketType,
    _Out_opt_ PSTRING MessageHeader,
    _Out_opt_ PSTRING MessageData,
    _Out_opt_ PULONG DataLength,
    _Inout_opt_ PKD_CONTEXT Context)
{
    PDBGKD_MANIPULATE_STATE64 State;
    KD_CONTINUE_TYPE Decision = kdHandleException;

    if (PacketType != PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        return KdReceivePacket(PacketType,
                               MessageHeader,
                               MessageData,
                               DataLength,
                               Context);
    }

    if ((MessageHeader == NULL) ||
        (MessageHeader->Buffer == NULL) ||
        (MessageHeader->MaximumLength < sizeof(DBGKD_MANIPULATE_STATE64)))
    {
        return KdPacketTimedOut;
    }

    if (KdbgStateChangePending)
    {
        KdbgStateChangePending = FALSE;

        switch (KdbgStateChange.NewState)
        {
            case DbgKdExceptionStateChange:

                if (KdpReportedContext != NULL)
                {
                    Decision = KdbEnterDebuggerException(
                        &KdbgStateChange.u.Exception.ExceptionRecord,
                        KdpReportedMode,
                        KdpReportedContext,
                        (BOOLEAN)KdbgStateChange.u.Exception.FirstChance);
                }
                break;

            case DbgKdLoadSymbolsStateChange:

                /* Nothing stops for a module arriving unless it was asked to */
                Decision = kdContinue;

                if (KdBreakAfterSymbolLoad && (KdpReportedContext != NULL))
                {
                    Decision = KdbEnterDebuggerException(NULL,
                                                         KdpReportedMode,
                                                         KdpReportedContext,
                                                         TRUE);
                }
                break;

            case DbgKdCommandStringStateChange:

                /* TODO: a command string from a component is not interpreted */
                Decision = kdContinue;
                break;

            default:
                Decision = kdContinue;
                break;
        }
    }

    State = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;

    RtlZeroMemory(State, sizeof(DBGKD_MANIPULATE_STATE64));
    State->ApiNumber = DbgKdContinueApi2;
    State->ProcessorLevel = KeProcessorLevel;
    State->Processor = (USHORT)KeGetCurrentPrcb()->Number;
    State->ReturnStatus = STATUS_SUCCESS;

    /*
     * Having dealt with the stop means the system is to carry on as if it had
     * not happened; anything else leaves the exception to whoever else
     * handles exceptions.
     */
    State->u.Continue2.ContinueStatus = (Decision == kdContinue)
                                        ? (NTSTATUS)DBG_CONTINUE
                                        : (NTSTATUS)DBG_EXCEPTION_NOT_HANDLED;

    MessageHeader->Length = sizeof(DBGKD_MANIPULATE_STATE64);

    if (MessageData != NULL)
        MessageData->Length = 0;

    if (DataLength != NULL)
        *DataLength = 0;

    return KdPacketReceived;
}

/*
 * The interactive debugger has no hardware of its own to put to sleep or wake
 * up; whatever the output goes out on is the link's business.
 */
NTSTATUS
NTAPI
KdbgD0Transition(VOID)
{
    return KdD0Transition();
}

NTSTATUS
NTAPI
KdbgD3Transition(VOID)
{
    return KdD3Transition();
}

NTSTATUS
NTAPI
KdbgSave(
    _In_ BOOLEAN SleepTransition)
{
    return KdSave(SleepTransition);
}

NTSTATUS
NTAPI
KdbgRestore(
    _In_ BOOLEAN SleepTransition)
{
    return KdRestore(SleepTransition);
}

/* EOF */
