/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The debug link when the kernel itself is the other end
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"

/*
 * Inside the kernel these names reach the interactive debugger first. This is
 * the link itself, which is what the interactive debugger speaks over, so the
 * names are taken back for the definitions below.
 */
#ifdef KDBG
#undef KdSendPacket
#undef KdReceivePacket
#endif

#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Says what stopped the machine, for a machine with nobody attached to tell.
 */
static
VOID
KdpReportStopOnProviders(
    _In_ PSTRING MessageHeader)
{
    PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange;

    if ((MessageHeader == NULL) ||
        (MessageHeader->Buffer == NULL) ||
        (MessageHeader->Length < sizeof(DBGKD_ANY_WAIT_STATE_CHANGE)))
    {
        return;
    }

    WaitStateChange = (PDBGKD_ANY_WAIT_STATE_CHANGE)MessageHeader->Buffer;

    if (WaitStateChange->NewState != DbgKdExceptionStateChange)
        return;

    KdIoPrintf("\nStop %08lx at %p on processor %u (%s chance)\n",
               WaitStateChange->u.Exception.ExceptionRecord.ExceptionCode,
               (PVOID)(ULONG_PTR)WaitStateChange->ProgramCounter,
               WaitStateChange->Processor,
               WaitStateChange->u.Exception.FirstChance ? "first" : "second");
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Sends one packet. Output goes to every provider; a stop is described in
 * words, since there is nobody on the other end to show it any other way.
 */
VOID
NTAPI
KdSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    switch (PacketType)
    {
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

            /* A prompt is shown the same way as anything else printed */
            if ((MessageData != NULL) && (MessageData->Buffer != NULL))
                KdpProvidersPrint(MessageData->Buffer, MessageData->Length);

            return;
        }

        case PACKET_TYPE_KD_STATE_CHANGE64:
            KdpReportStopOnProviders(MessageHeader);
            return;

        default:
            /* TODO: trace output, control requests and host file I/O have
             * nowhere to go without a host and are dropped */
            return;
    }
}

/*
 * Answers for the host that is not there. A machine with no debugger attached
 * still has to be told what to do when it stops, and what it is told is to
 * leave the exception to whoever else handles exceptions.
 */
KDSTATUS
NTAPI
KdReceivePacket(
    _In_ ULONG PacketType,
    _Out_opt_ PSTRING MessageHeader,
    _Out_opt_ PSTRING MessageData,
    _Out_opt_ PULONG DataLength,
    _Inout_opt_ PKD_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    switch (PacketType)
    {
        case PACKET_TYPE_KD_POLL_BREAKIN:

            /* The only place a request can come from is the terminal */
            return KdpTerminalPollBreakIn() ? KdPacketReceived : KdPacketTimedOut;

        case PACKET_TYPE_KD_STATE_MANIPULATE:
        {
            PDBGKD_MANIPULATE_STATE64 State;

            if ((MessageHeader == NULL) ||
                (MessageHeader->Buffer == NULL) ||
                (MessageHeader->MaximumLength < sizeof(DBGKD_MANIPULATE_STATE64)))
            {
                return KdPacketTimedOut;
            }

            State = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;

            RtlZeroMemory(State, sizeof(DBGKD_MANIPULATE_STATE64));
            State->ApiNumber = DbgKdContinueApi2;
            State->ProcessorLevel = KeProcessorLevel;
            State->Processor = (USHORT)KeGetCurrentPrcb()->Number;
            State->ReturnStatus = STATUS_SUCCESS;
            State->u.Continue2.ContinueStatus = (NTSTATUS)DBG_EXCEPTION_NOT_HANDLED;

            MessageHeader->Length = sizeof(DBGKD_MANIPULATE_STATE64);

            if (MessageData != NULL)
                MessageData->Length = 0;

            if (DataLength != NULL)
                *DataLength = 0;

            return KdPacketReceived;
        }

        case PACKET_TYPE_KD_DEBUG_IO:
        {
            PDBGKD_DEBUG_IO DebugIo;
            SIZE_T Read;

            if ((MessageHeader == NULL) ||
                (MessageHeader->Buffer == NULL) ||
                (MessageHeader->MaximumLength < sizeof(DBGKD_DEBUG_IO)) ||
                (MessageData == NULL) ||
                (MessageData->Buffer == NULL))
            {
                return KdPacketTimedOut;
            }

            /* Whoever is watching the output is the one who gets to answer */
            Read = KdIoReadLine(MessageData->Buffer, MessageData->MaximumLength);

            DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;
            DebugIo->ApiNumber = DbgKdGetStringApi;
            DebugIo->ProcessorLevel = KeProcessorLevel;
            DebugIo->Processor = (USHORT)KeGetCurrentPrcb()->Number;
            DebugIo->u.GetString.LengthOfPromptString = 0;
            DebugIo->u.GetString.LengthOfStringRead = (ULONG)Read;

            MessageHeader->Length = sizeof(DBGKD_DEBUG_IO);
            MessageData->Length = (USHORT)Read;

            if (DataLength != NULL)
                *DataLength = (ULONG)Read;

            return KdPacketReceived;
        }

        default:
            return KdPacketTimedOut;
    }
}

/* EOF */
