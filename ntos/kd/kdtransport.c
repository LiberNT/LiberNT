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

/* PRIVATE DATA ***************************************************************/

#define KDP_FILE_IO_POLL_US         10
#define KDP_FILE_IO_SERIAL_POLLS    100000
#define KDP_FILE_IO_NET_POLLS       400000
#define KDP_FILE_IO_MAXIMUM_SKIPPED (4 * PACKET_MAX_SIZE)

static ULONG KdpFileIoPacketId = INITIAL_PACKET_ID;

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
KdpFileIoPut(
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ ULONG Length)
{
    if (KdpDebugMode.Serial)
        KdPortPutBufferEx(&SerialPortInfo, Buffer, Length);

    if (KdpDebugMode.Net)
        KdpNetSendBytes(Buffer, Length);
}

static
BOOLEAN
KdpFileIoGetByte(
    _Out_ PUCHAR Byte)
{
    ULONG Polls = KdpDebugMode.Net ? KDP_FILE_IO_NET_POLLS : KDP_FILE_IO_SERIAL_POLLS;
    ULONG Poll;

    for (Poll = 0; Poll < Polls; Poll++)
    {
        if (KdpDebugMode.Serial && KdPortGetByteEx(&SerialPortInfo, Byte))
            return TRUE;

        if (KdpDebugMode.Net && KdpNetGetByte(Byte))
            return TRUE;

        KeStallExecutionProcessor(KDP_FILE_IO_POLL_US);
    }

    return FALSE;
}

static
ULONG
KdpFileIoChecksum(
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ ULONG Length)
{
    ULONG Checksum = 0;
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        Checksum += (UCHAR)Buffer[Index];

    return Checksum;
}

static
VOID
KdpSendFileIoPacket(
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData)
{
    KD_PACKET Packet;
    UCHAR Trailer = PACKET_TRAILING_BYTE;
    ULONG DataLength = 0;

    if ((MessageHeader == NULL) ||
        (MessageHeader->Buffer == NULL) ||
        (MessageHeader->Length != sizeof(DBGKD_FILE_IO)) ||
        (!KdpDebugMode.Serial && !KdpDebugMode.Net))
    {
        return;
    }

    if ((MessageData != NULL) && (MessageData->Buffer != NULL))
        DataLength = MessageData->Length;

    if (MessageHeader->Length + DataLength > PACKET_MAX_SIZE)
        return;

    KdpFileIoPacketId++;

    Packet.PacketLeader = PACKET_LEADER;
    Packet.PacketType = PACKET_TYPE_KD_FILE_IO;
    Packet.ByteCount = (USHORT)(MessageHeader->Length + DataLength);
    Packet.PacketId = KdpFileIoPacketId;
    Packet.Checksum = KdpFileIoChecksum(MessageHeader->Buffer, MessageHeader->Length);
    if (DataLength != 0)
        Packet.Checksum += KdpFileIoChecksum(MessageData->Buffer, DataLength);

    KdpFileIoPut((PCCH)&Packet, sizeof(Packet));
    KdpFileIoPut(MessageHeader->Buffer, MessageHeader->Length);
    if (DataLength != 0)
        KdpFileIoPut(MessageData->Buffer, DataLength);
    KdpFileIoPut((PCCH)&Trailer, sizeof(Trailer));
}

static
BOOLEAN
KdpFileIoFindLeader(
    _Out_ PUCHAR NextByte,
    _Inout_ PULONG Skipped)
{
    ULONG Matched = 0;
    UCHAR Byte;

    while (*Skipped < KDP_FILE_IO_MAXIMUM_SKIPPED)
    {
        if (!KdpFileIoGetByte(&Byte))
            return FALSE;

        (*Skipped)++;

        if (Byte == (UCHAR)PACKET_LEADER)
        {
            Matched++;
            continue;
        }

        if (Matched >= sizeof(ULONG))
        {
            *NextByte = Byte;
            return TRUE;
        }

        Matched = 0;
    }

    return FALSE;
}

static
KDSTATUS
KdpReceiveFileIoPacket(
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_opt_ PULONG DataLength)
{
    KD_PACKET Packet;
    PUCHAR Fields = (PUCHAR)&Packet.PacketType;
    ULONG Skipped = 0;
    ULONG Index;
    ULONG Copied;
    ULONG Checksum;
    UCHAR Byte;

    if ((MessageHeader == NULL) ||
        (MessageHeader->Buffer == NULL) ||
        (MessageHeader->MaximumLength < sizeof(DBGKD_FILE_IO)) ||
        (MessageData == NULL) ||
        (MessageData->Buffer == NULL) ||
        (!KdpDebugMode.Serial && !KdpDebugMode.Net))
    {
        return KdPacketTimedOut;
    }

    while (KdpFileIoFindLeader(&Fields[0], &Skipped))
    {
        for (Index = 1; Index < sizeof(Packet) - FIELD_OFFSET(KD_PACKET, PacketType); Index++)
        {
            if (!KdpFileIoGetByte(&Fields[Index]))
                return KdPacketTimedOut;
        }

        Skipped += Index;

        if ((Packet.PacketType != PACKET_TYPE_KD_FILE_IO) ||
            (Packet.ByteCount < sizeof(DBGKD_FILE_IO)) ||
            (Packet.ByteCount > PACKET_MAX_SIZE))
        {
            continue;
        }

        Checksum = 0;
        Copied = 0;
        for (Index = 0; Index < Packet.ByteCount; Index++)
        {
            if (!KdpFileIoGetByte(&Byte))
                return KdPacketTimedOut;

            Checksum += Byte;
            if (Index < sizeof(DBGKD_FILE_IO))
                MessageHeader->Buffer[Index] = (CHAR)Byte;
            else if (Copied < MessageData->MaximumLength)
                MessageData->Buffer[Copied++] = (CHAR)Byte;
        }

        if (!KdpFileIoGetByte(&Byte))
            return KdPacketTimedOut;

        Skipped += Packet.ByteCount + 1;

        if ((Byte != PACKET_TRAILING_BYTE) ||
            (Checksum != Packet.Checksum) ||
            (Packet.PacketId != KdpFileIoPacketId))
        {
            continue;
        }

        MessageHeader->Length = sizeof(DBGKD_FILE_IO);
        MessageData->Length = (USHORT)Copied;
        if (DataLength != NULL)
            *DataLength = Packet.ByteCount - sizeof(DBGKD_FILE_IO);

        return KdPacketReceived;
    }

    return KdPacketTimedOut;
}

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

        case PACKET_TYPE_KD_FILE_IO:
            KdpSendFileIoPacket(MessageHeader, MessageData);
            return;

        default:
            /* TODO: trace output and control requests have nowhere to go without a host and are dropped */
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

        case PACKET_TYPE_KD_FILE_IO:
            return KdpReceiveFileIoPacket(MessageHeader, MessageData, DataLength);

        default:
            return KdPacketTimedOut;
    }
}

/* EOF */
