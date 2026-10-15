/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACL data path and L2CAP connection-oriented channels
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define L2CAP_SIGNALING_CID             0x0001
#define L2CAP_FIRST_DYNAMIC_CID         0x0040
#define L2CAP_COMMAND_REJECT            0x01
#define L2CAP_CONNECTION_REQUEST        0x02
#define L2CAP_CONNECTION_RESPONSE       0x03
#define L2CAP_CONFIGURATION_REQUEST     0x04
#define L2CAP_CONFIGURATION_RESPONSE    0x05
#define L2CAP_DISCONNECTION_REQUEST     0x06
#define L2CAP_DISCONNECTION_RESPONSE    0x07
#define L2CAP_ECHO_REQUEST              0x08
#define L2CAP_ECHO_RESPONSE             0x09
#define L2CAP_INFORMATION_REQUEST       0x0A
#define L2CAP_INFORMATION_RESPONSE      0x0B
#define L2CAP_OPTION_MTU                0x01
#define L2CAP_OPTION_RFC                0x04
#define L2CAP_RESULT_SUCCESS            0x0000
#define L2CAP_RESULT_PENDING            0x0001
#define L2CAP_RESULT_BAD_PSM            0x0002
#define L2CAP_RESULT_SECURITY_BLOCK     0x0003
#define L2CAP_RESULT_NO_RESOURCES       0x0004
#define L2CAP_CONFIG_UNACCEPTABLE       0x0001
#define L2CAP_SIGNAL_TIMEOUT_MS         30000
#define L2CAP_MAX_CHANNELS_PER_LINK     16
#define ACL_PB_CONTINUING               0x01
#define ACL_PB_START                    0x02

static
USHORT
BthGet16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

static
VOID
BthPut16(
    _Out_writes_(2) PUCHAR Data,
    _In_ USHORT Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
}

static
ULONGLONG
BthNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
BthAclKick(
    _In_ PBTHMINI_DEVICE Dev)
{
    while (Dev->AclCredits && !IsListEmpty(&Dev->AclQueue))
    {
        PBTHMINI_ACL_PACKET Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->AclQueue), BTHMINI_ACL_PACKET, Link);
        USHORT Handle = BthGet16(Packet->Data) & 0x0FFF;
        PBTHMINI_REMOTE Remote = BthRadioFindHandle(Dev, Handle);

        if (Remote && NT_SUCCESS(BthMiniSendPacket(Dev, HciPacketAclData, Packet->Data, Packet->Length)))
        {
            Dev->AclCredits--;
            Remote->AclOutstanding++;
        }
        ExFreePoolWithTag(Packet, BTHMINI_TAG);
    }
}

static
NTSTATUS
BthAclQueueFrame(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Handle,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    while (Offset < Length)
    {
        ULONG Chunk = min(Length - Offset, Dev->AclMtu);
        PBTHMINI_ACL_PACKET Packet;

        Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(BTHMINI_ACL_PACKET, Data[4 + Chunk]), BTHMINI_TAG);
        if (!Packet)
            return STATUS_INSUFFICIENT_RESOURCES;
        BthPut16(Packet->Data, (USHORT)(Handle | ((Offset ? ACL_PB_CONTINUING : ACL_PB_START) << 12)));
        BthPut16(Packet->Data + 2, (USHORT)Chunk);
        RtlCopyMemory(Packet->Data + 4, Frame + Offset, Chunk);
        Packet->Length = 4 + Chunk;
        InsertTailList(&Dev->AclQueue, &Packet->Link);
        Offset += Chunk;
    }
    BthAclKick(Dev);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthL2capSendFrame(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Cid,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    PUCHAR Frame;
    NTSTATUS Status;

    if (Length > L2CAP_MAX_FRAME || !(Remote->Flags & BDIF_CONNECTED))
        return STATUS_INVALID_PARAMETER;
    Frame = ExAllocatePoolWithTag(NonPagedPool, 4 + Length, BTHMINI_TAG);
    if (!Frame)
        return STATUS_INSUFFICIENT_RESOURCES;
    BthPut16(Frame, (USHORT)Length);
    BthPut16(Frame + 2, Cid);
    RtlCopyMemory(Frame + 4, Payload, Length);
    Status = BthAclQueueFrame(Dev, Remote->Handle, Frame, 4 + Length);
    ExFreePoolWithTag(Frame, BTHMINI_TAG);
    return Status;
}

static
VOID
BthL2capSignal(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Code,
    _In_ UCHAR Identifier,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ USHORT Length)
{
    UCHAR Command[4 + 64];

    if (Length > sizeof(Command) - 4)
        return;
    Command[0] = Code;
    Command[1] = Identifier;
    BthPut16(Command + 2, Length);
    RtlCopyMemory(Command + 4, Data, Length);
    if (!NT_SUCCESS(BthL2capSendFrame(Dev, Remote, L2CAP_SIGNALING_CID, Command, 4 + Length)))
        DPRINT1("BTHMINI: L2CAP signal 0x%02x not sent\n", Code);
}

static
UCHAR
BthL2capNextIdentifier(
    _In_ PBTHMINI_DEVICE Dev)
{
    if (++Dev->SignalId == 0)
        Dev->SignalId = 1;
    return Dev->SignalId;
}

static
USHORT
BthL2capAllocateCid(
    _In_ PBTHMINI_DEVICE Dev)
{
    for (;;)
    {
        PLIST_ENTRY Entry;
        USHORT Cid = Dev->NextCid++;

        if (Dev->NextCid < L2CAP_FIRST_DYNAMIC_CID)
            Dev->NextCid = L2CAP_FIRST_DYNAMIC_CID;
        if (Cid < L2CAP_FIRST_DYNAMIC_CID)
            continue;
        for (Entry = Dev->Channels.Flink; Entry != &Dev->Channels; Entry = Entry->Flink)
        {
            if (CONTAINING_RECORD(Entry, BTHMINI_CHANNEL, Link)->LocalCid == Cid)
                break;
        }
        if (Entry == &Dev->Channels)
            return Cid;
    }
}

static
PBTHMINI_CHANNEL
BthL2capFindLocal(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT LocalCid)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Channels.Flink; Entry != &Dev->Channels; Entry = Entry->Flink)
    {
        PBTHMINI_CHANNEL Channel = CONTAINING_RECORD(Entry, BTHMINI_CHANNEL, Link);

        if (Channel->Remote == Remote && Channel->LocalCid == LocalCid)
            return Channel;
    }
    return NULL;
}

static
VOID
BthL2capClose(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    BOOLEAN WasOpen = Channel->State == BthChannelOpen || Channel->State == BthChannelWaitDisconnect;

    RemoveEntryList(&Channel->Link);
    if (!WasOpen && Channel->Outgoing && Channel->Callbacks->Opened)
        Channel->Callbacks->Opened(Dev, Channel, NT_SUCCESS(Status) ? STATUS_CONNECTION_ABORTED : Status);
    else if (Channel->Callbacks->Closed)
        Channel->Callbacks->Closed(Dev, Channel, Status);
    ExFreePoolWithTag(Channel, BTHMINI_TAG);
}

static
VOID
BthL2capSendConfiguration(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel)
{
    UCHAR Request[8];

    Channel->Identifier = BthL2capNextIdentifier(Dev);
    BthPut16(Request, Channel->RemoteCid);
    BthPut16(Request + 2, 0);
    Request[4] = L2CAP_OPTION_MTU;
    Request[5] = 2;
    BthPut16(Request + 6, L2CAP_LOCAL_MTU);
    Channel->Deadline = BthNowMs() + L2CAP_SIGNAL_TIMEOUT_MS;
    BthL2capSignal(Dev, Channel->Remote, L2CAP_CONFIGURATION_REQUEST, Channel->Identifier, Request, sizeof(Request));
}

static
VOID
BthL2capConfigured(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel)
{
    if (Channel->State != BthChannelConfig || !Channel->LocalConfigured || !Channel->RemoteConfigured)
        return;
    Channel->State = BthChannelOpen;
    DPRINT1("BTHMINI: L2CAP PSM 0x%04x open, CID 0x%04x -> 0x%04x, MTU %u\n",
            Channel->Psm, Channel->LocalCid, Channel->RemoteCid, Channel->RemoteMtu);
    if (Channel->Callbacks->Opened)
        Channel->Callbacks->Opened(Dev, Channel, STATUS_SUCCESS);
}

static
VOID
BthL2capStartConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel)
{
    UCHAR Request[4];

    Channel->State = BthChannelWaitConnect;
    Channel->Identifier = BthL2capNextIdentifier(Dev);
    Channel->Deadline = BthNowMs() + L2CAP_SIGNAL_TIMEOUT_MS;
    BthPut16(Request, Channel->Psm);
    BthPut16(Request + 2, Channel->LocalCid);
    BthL2capSignal(Dev, Channel->Remote, L2CAP_CONNECTION_REQUEST, Channel->Identifier, Request, sizeof(Request));
}

static
ULONG
BthL2capCountChannels(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = Dev->Channels.Flink; Entry != &Dev->Channels; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, BTHMINI_CHANNEL, Link)->Remote == Remote)
            Count++;
    }
    return Count;
}

static
const BTHMINI_CHANNEL_CALLBACKS *
BthL2capServer(
    _In_ USHORT Psm)
{
    switch (Psm)
    {
        case L2CAP_PSM_SDP:
            return BthSdpServerCallbacks();
        case L2CAP_PSM_RFCOMM:
            return BthRfcommServerCallbacks();
        default:
            return NULL;
    }
}

static
VOID
BthL2capConnectionRequest(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Identifier,
    _In_reads_(4) const UCHAR *Request)
{
    const BTHMINI_CHANNEL_CALLBACKS *Server;
    PBTHMINI_CHANNEL Channel = NULL;
    USHORT Psm = BthGet16(Request), RemoteCid = BthGet16(Request + 2), Result;
    UCHAR Response[8];

    Server = BthL2capServer(Psm);
    if (!Server)
        Result = L2CAP_RESULT_BAD_PSM;
    else if (Psm != L2CAP_PSM_SDP && !Remote->Encrypted)
        Result = L2CAP_RESULT_SECURITY_BLOCK;
    else if (BthL2capCountChannels(Dev, Remote) >= L2CAP_MAX_CHANNELS_PER_LINK)
        Result = L2CAP_RESULT_NO_RESOURCES;
    else if (!(Channel = ExAllocatePoolZero(NonPagedPool, sizeof(*Channel), BTHMINI_TAG)))
        Result = L2CAP_RESULT_NO_RESOURCES;
    else
        Result = L2CAP_RESULT_SUCCESS;

    if (Channel)
    {
        Channel->Remote = Remote;
        Channel->Callbacks = Server;
        Channel->Psm = Psm;
        Channel->LocalCid = BthL2capAllocateCid(Dev);
        Channel->RemoteCid = RemoteCid;
        Channel->RemoteMtu = L2CAP_DEFAULT_MTU;
        Channel->State = BthChannelConfig;
        InsertTailList(&Dev->Channels, &Channel->Link);
    }
    BthPut16(Response, Channel ? Channel->LocalCid : 0);
    BthPut16(Response + 2, RemoteCid);
    BthPut16(Response + 4, Result);
    BthPut16(Response + 6, 0);
    BthL2capSignal(Dev, Remote, L2CAP_CONNECTION_RESPONSE, Identifier, Response, sizeof(Response));
    if (Channel)
        BthL2capSendConfiguration(Dev, Channel);
    else
        DPRINT1("BTHMINI: L2CAP PSM 0x%04x refused, result %u\n", Psm, Result);
}

static
VOID
BthL2capConfigurationRequest(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Identifier,
    _In_reads_bytes_(Length) const UCHAR *Request,
    _In_ USHORT Length)
{
    PBTHMINI_CHANNEL Channel = BthL2capFindLocal(Dev, Remote, BthGet16(Request));
    USHORT Result = 0, Offset = 4;
    UCHAR Response[6 + 11];
    USHORT ResponseLength = 6;

    if (!Channel || Channel->State != BthChannelConfig)
    {
        UCHAR Reject[6];

        BthPut16(Reject, 0x0002);
        BthPut16(Reject + 2, BthGet16(Request));
        BthPut16(Reject + 4, 0);
        BthL2capSignal(Dev, Remote, L2CAP_COMMAND_REJECT, Identifier, Reject, sizeof(Reject));
        return;
    }

    while (Offset + 2 <= Length)
    {
        UCHAR Type = Request[Offset], OptionLength = Request[Offset + 1];
        const UCHAR *Value = Request + Offset + 2;

        if (Offset + 2 + OptionLength > Length)
            break;
        if ((Type & 0x7F) == L2CAP_OPTION_MTU && OptionLength >= 2)
        {
            Channel->RemoteMtu = BthGet16(Value);
        }
        else if ((Type & 0x7F) == L2CAP_OPTION_RFC && OptionLength >= 1 && Value[0] != 0)
        {
            Result = L2CAP_CONFIG_UNACCEPTABLE;
            Response[6] = L2CAP_OPTION_RFC;
            Response[7] = 9;
            RtlZeroMemory(Response + 8, 9);
            ResponseLength = 6 + 11;
        }
        Offset += 2 + OptionLength;
    }

    BthPut16(Response, Channel->RemoteCid);
    BthPut16(Response + 2, 0);
    BthPut16(Response + 4, Result);
    BthL2capSignal(Dev, Remote, L2CAP_CONFIGURATION_RESPONSE, Identifier, Response, ResponseLength);
    if (Result == 0 && !(BthGet16(Request + 2) & 1))
    {
        Channel->RemoteConfigured = TRUE;
        BthL2capConfigured(Dev, Channel);
    }
}

static
VOID
BthL2capReject(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Identifier)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Channels.Flink; Entry != &Dev->Channels; Entry = Entry->Flink)
    {
        PBTHMINI_CHANNEL Channel = CONTAINING_RECORD(Entry, BTHMINI_CHANNEL, Link);

        if (Channel->Remote == Remote && Channel->Identifier == Identifier &&
            Channel->State != BthChannelOpen && Channel->State != BthChannelWaitLink)
        {
            BthL2capClose(Dev, Channel, STATUS_CONNECTION_REFUSED);
            return;
        }
    }
}

static
VOID
BthL2capSignaling(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    while (Length >= 4)
    {
        UCHAR Code = Data[0], Identifier = Data[1], Response[12];
        USHORT CommandLength = BthGet16(Data + 2);
        const UCHAR *Command = Data + 4;
        PBTHMINI_CHANNEL Channel;

        if (4 + (ULONG)CommandLength > Length)
            return;
        switch (Code)
        {
            case L2CAP_CONNECTION_REQUEST:
                if (CommandLength >= 4)
                    BthL2capConnectionRequest(Dev, Remote, Identifier, Command);
                break;

            case L2CAP_CONNECTION_RESPONSE:
                if (CommandLength < 8)
                    break;
                Channel = BthL2capFindLocal(Dev, Remote, BthGet16(Command + 2));
                if (!Channel || Channel->State != BthChannelWaitConnect)
                    break;
                if (BthGet16(Command + 4) == L2CAP_RESULT_PENDING)
                {
                    Channel->Deadline = BthNowMs() + L2CAP_SIGNAL_TIMEOUT_MS;
                }
                else if (BthGet16(Command + 4) == L2CAP_RESULT_SUCCESS)
                {
                    Channel->RemoteCid = BthGet16(Command);
                    Channel->State = BthChannelConfig;
                    BthL2capSendConfiguration(Dev, Channel);
                }
                else
                {
                    DPRINT1("BTHMINI: L2CAP PSM 0x%04x refused by peer, result %u\n", Channel->Psm, BthGet16(Command + 4));
                    BthL2capClose(Dev, Channel, STATUS_CONNECTION_REFUSED);
                }
                break;

            case L2CAP_CONFIGURATION_REQUEST:
                if (CommandLength >= 4)
                    BthL2capConfigurationRequest(Dev, Remote, Identifier, Command, CommandLength);
                break;

            case L2CAP_CONFIGURATION_RESPONSE:
                if (CommandLength < 6)
                    break;
                Channel = BthL2capFindLocal(Dev, Remote, BthGet16(Command));
                if (!Channel || Channel->State != BthChannelConfig || Channel->Identifier != Identifier)
                    break;
                if (BthGet16(Command + 4) == 0)
                {
                    Channel->LocalConfigured = TRUE;
                    BthL2capConfigured(Dev, Channel);
                }
                else
                {
                    BthL2capClose(Dev, Channel, STATUS_CONNECTION_REFUSED);
                }
                break;

            case L2CAP_DISCONNECTION_REQUEST:
                if (CommandLength < 4)
                    break;
                Channel = BthL2capFindLocal(Dev, Remote, BthGet16(Command));
                if (!Channel)
                {
                    BthPut16(Response, 0x0002);
                    RtlCopyMemory(Response + 2, Command, 4);
                    BthL2capSignal(Dev, Remote, L2CAP_COMMAND_REJECT, Identifier, Response, 6);
                    break;
                }
                RtlCopyMemory(Response, Command, 4);
                BthL2capSignal(Dev, Remote, L2CAP_DISCONNECTION_RESPONSE, Identifier, Response, 4);
                BthL2capClose(Dev, Channel, STATUS_REMOTE_DISCONNECT);
                break;

            case L2CAP_DISCONNECTION_RESPONSE:
                if (CommandLength < 4)
                    break;
                Channel = BthL2capFindLocal(Dev, Remote, BthGet16(Command + 2));
                if (Channel && Channel->State == BthChannelWaitDisconnect)
                    BthL2capClose(Dev, Channel, STATUS_SUCCESS);
                break;

            case L2CAP_ECHO_REQUEST:
                BthL2capSignal(Dev, Remote, L2CAP_ECHO_RESPONSE, Identifier, Command, (USHORT)min(CommandLength, 64));
                break;

            case L2CAP_INFORMATION_REQUEST:
                if (CommandLength < 2)
                    break;
                RtlCopyMemory(Response, Command, 2);
                if (BthGet16(Command) == 0x0002)
                {
                    BthPut16(Response + 2, 0);
                    RtlZeroMemory(Response + 4, 4);
                    BthL2capSignal(Dev, Remote, L2CAP_INFORMATION_RESPONSE, Identifier, Response, 8);
                }
                else
                {
                    BthPut16(Response + 2, 1);
                    BthL2capSignal(Dev, Remote, L2CAP_INFORMATION_RESPONSE, Identifier, Response, 4);
                }
                break;

            case L2CAP_COMMAND_REJECT:
                BthL2capReject(Dev, Remote, Identifier);
                break;

            case L2CAP_ECHO_RESPONSE:
            case L2CAP_INFORMATION_RESPONSE:
                break;

            default:
                BthPut16(Response, 0x0000);
                BthL2capSignal(Dev, Remote, L2CAP_COMMAND_REJECT, Identifier, Response, 2);
                break;
        }
        Data += 4 + CommandLength;
        Length -= 4 + CommandLength;
    }
}

static
VOID
BthL2capFrame(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    USHORT PayloadLength = BthGet16(Frame), Cid = BthGet16(Frame + 2);
    PBTHMINI_CHANNEL Channel;

    if (4 + (ULONG)PayloadLength != Length)
        return;
    if (Cid == L2CAP_SIGNALING_CID)
    {
        BthL2capSignaling(Dev, Remote, Frame + 4, PayloadLength);
        return;
    }
    Channel = BthL2capFindLocal(Dev, Remote, Cid);
    if (Channel && Channel->State == BthChannelOpen && Channel->Callbacks->Receive)
        Channel->Callbacks->Receive(Dev, Channel, Frame + 4, PayloadLength);
}

static
VOID
BthL2capDropReassembly(
    _In_ PBTHMINI_REMOTE Remote)
{
    if (Remote->RxFrame)
        ExFreePoolWithTag(Remote->RxFrame, BTHMINI_TAG);
    Remote->RxFrame = NULL;
    Remote->RxExpected = 0;
    Remote->RxLength = 0;
}

VOID
BthAclReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_REMOTE Remote;
    USHORT Handle, AclLength;
    UCHAR Boundary;

    if (Length < 4)
        return;
    Handle = BthGet16(Data) & 0x0FFF;
    Boundary = (Data[1] >> 4) & 0x03;
    AclLength = BthGet16(Data + 2);
    if (4 + (ULONG)AclLength > Length)
        return;
    Remote = BthRadioFindHandle(Dev, Handle);
    if (!Remote)
        return;
    Data += 4;

    if (Boundary != ACL_PB_CONTINUING)
    {
        ULONG FrameLength;

        BthL2capDropReassembly(Remote);
        if (AclLength < 4)
            return;
        FrameLength = 4 + BthGet16(Data);
        if (AclLength >= FrameLength)
        {
            BthL2capFrame(Dev, Remote, Data, FrameLength);
            return;
        }
        Remote->RxFrame = ExAllocatePoolWithTag(NonPagedPool, FrameLength, BTHMINI_TAG);
        if (!Remote->RxFrame)
            return;
        Remote->RxExpected = FrameLength;
        Remote->RxLength = 0;
    }
    if (!Remote->RxFrame)
        return;
    if (AclLength > Remote->RxExpected - Remote->RxLength)
    {
        BthL2capDropReassembly(Remote);
        return;
    }
    RtlCopyMemory(Remote->RxFrame + Remote->RxLength, Data, AclLength);
    Remote->RxLength += AclLength;
    if (Remote->RxLength == Remote->RxExpected)
    {
        PUCHAR Frame = Remote->RxFrame;
        ULONG FrameLength = Remote->RxLength;

        Remote->RxFrame = NULL;
        Remote->RxExpected = 0;
        Remote->RxLength = 0;
        BthL2capFrame(Dev, Remote, Frame, FrameLength);
        ExFreePoolWithTag(Frame, BTHMINI_TAG);
    }
}

NTSTATUS
BthL2capConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Psm,
    _In_ const BTHMINI_CHANNEL_CALLBACKS *Callbacks,
    _In_opt_ PVOID Context,
    _Out_opt_ PBTHMINI_CHANNEL *Result)
{
    PBTHMINI_CHANNEL Channel;
    NTSTATUS Status;

    if (Result)
        *Result = NULL;
    Channel = ExAllocatePoolZero(NonPagedPool, sizeof(*Channel), BTHMINI_TAG);
    if (!Channel)
        return STATUS_INSUFFICIENT_RESOURCES;
    Channel->Remote = Remote;
    Channel->Callbacks = Callbacks;
    Channel->Context = Context;
    Channel->Psm = Psm;
    Channel->Outgoing = TRUE;
    Channel->LocalCid = BthL2capAllocateCid(Dev);
    Channel->RemoteMtu = L2CAP_DEFAULT_MTU;
    Channel->State = BthChannelWaitLink;
    Channel->Deadline = BthNowMs() + L2CAP_SIGNAL_TIMEOUT_MS;
    InsertTailList(&Dev->Channels, &Channel->Link);

    Status = BthConnEnsureLink(Dev, Remote, Psm != L2CAP_PSM_SDP);
    if (Status == STATUS_SUCCESS)
        BthL2capStartConnect(Dev, Channel);
    else if (Status != STATUS_PENDING)
    {
        RemoveEntryList(&Channel->Link);
        ExFreePoolWithTag(Channel, BTHMINI_TAG);
        return Status;
    }
    if (Result)
        *Result = Channel;
    return STATUS_PENDING;
}

NTSTATUS
BthL2capSend(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    if (Channel->State != BthChannelOpen)
        return STATUS_INVALID_DEVICE_STATE;
    if (Length > Channel->RemoteMtu)
        return STATUS_INVALID_BUFFER_SIZE;
    return BthL2capSendFrame(Dev, Channel->Remote, Channel->RemoteCid, Data, Length);
}

VOID
BthL2capDisconnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel)
{
    UCHAR Request[4];

    if (Channel->State == BthChannelWaitDisconnect)
        return;
    if (Channel->State == BthChannelWaitLink || Channel->State == BthChannelWaitConnect)
    {
        BthL2capClose(Dev, Channel, STATUS_CANCELLED);
        return;
    }
    Channel->State = BthChannelWaitDisconnect;
    Channel->Identifier = BthL2capNextIdentifier(Dev);
    Channel->Deadline = BthNowMs() + L2CAP_SIGNAL_TIMEOUT_MS;
    BthPut16(Request, Channel->RemoteCid);
    BthPut16(Request + 2, Channel->LocalCid);
    BthL2capSignal(Dev, Channel->Remote, L2CAP_DISCONNECTION_REQUEST, Channel->Identifier, Request, sizeof(Request));
}

VOID
BthL2capLinkChanged(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status)
{
    PLIST_ENTRY Entry = Dev->Channels.Flink;

    while (Entry != &Dev->Channels)
    {
        PBTHMINI_CHANNEL Channel = CONTAINING_RECORD(Entry, BTHMINI_CHANNEL, Link);
        NTSTATUS LinkStatus;

        Entry = Entry->Flink;
        if (Channel->Remote != Remote)
            continue;
        if (!(Remote->Flags & BDIF_CONNECTED) && Channel->State != BthChannelWaitLink)
        {
            BthL2capClose(Dev, Channel, STATUS_REMOTE_DISCONNECT);
            continue;
        }
        if (Channel->State != BthChannelWaitLink)
            continue;
        if (!NT_SUCCESS(Status))
        {
            BthL2capClose(Dev, Channel, Status);
            continue;
        }
        LinkStatus = BthConnEnsureLink(Dev, Remote, Channel->Psm != L2CAP_PSM_SDP);
        if (LinkStatus == STATUS_SUCCESS)
            BthL2capStartConnect(Dev, Channel);
        else if (LinkStatus != STATUS_PENDING)
            BthL2capClose(Dev, Channel, LinkStatus);
    }

    if (!(Remote->Flags & BDIF_CONNECTED))
    {
        PLIST_ENTRY Next;

        BthL2capDropReassembly(Remote);
        for (Entry = Dev->AclQueue.Flink; Entry != &Dev->AclQueue; Entry = Next)
        {
            PBTHMINI_ACL_PACKET Packet = CONTAINING_RECORD(Entry, BTHMINI_ACL_PACKET, Link);

            Next = Entry->Flink;
            if ((BthGet16(Packet->Data) & 0x0FFF) == Remote->Handle)
            {
                RemoveEntryList(&Packet->Link);
                ExFreePoolWithTag(Packet, BTHMINI_TAG);
            }
        }
        Dev->AclCredits = (USHORT)min(Dev->AclPackets, Dev->AclCredits + Remote->AclOutstanding);
        Remote->AclOutstanding = 0;
        BthAclKick(Dev);
    }
}

VOID
BthL2capTick(
    _In_ PBTHMINI_DEVICE Dev)
{
    ULONGLONG Now = BthNowMs();
    PLIST_ENTRY Entry = Dev->Channels.Flink;

    while (Entry != &Dev->Channels)
    {
        PBTHMINI_CHANNEL Channel = CONTAINING_RECORD(Entry, BTHMINI_CHANNEL, Link);

        Entry = Entry->Flink;
        if (Channel->State != BthChannelOpen && Now >= Channel->Deadline)
        {
            DPRINT1("BTHMINI: L2CAP PSM 0x%04x timed out in state %u\n", Channel->Psm, Channel->State);
            BthL2capClose(Dev, Channel, STATUS_IO_TIMEOUT);
        }
    }
}

VOID
BthL2capFlush(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;

    while (!IsListEmpty(&Dev->Channels))
        BthL2capClose(Dev, CONTAINING_RECORD(Dev->Channels.Flink, BTHMINI_CHANNEL, Link), STATUS_CANCELLED);
    while (!IsListEmpty(&Dev->AclQueue))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dev->AclQueue), BTHMINI_ACL_PACKET, Link), BTHMINI_TAG);
    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
        BthL2capDropReassembly(CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link));
}

BOOLEAN
BthL2capEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length)
{
    const UCHAR *Parameters = Event + 2;
    ULONG ParameterLength = Length - 2, Index;

    if (Event[0] != HCI_EV_NUMBER_OF_COMPLETED)
        return FALSE;
    for (Index = 0; ParameterLength >= 1 && Index < Parameters[0] && 1 + (Index + 1) * 4 <= ParameterLength; Index++)
    {
        const UCHAR *Entry = Parameters + 1 + Index * 4;
        PBTHMINI_REMOTE Remote = BthRadioFindHandle(Dev, BthGet16(Entry) & 0x0FFF);
        USHORT Count = BthGet16(Entry + 2);

        if (Remote)
            Remote->AclOutstanding = Remote->AclOutstanding > Count ? Remote->AclOutstanding - Count : 0;
        Dev->AclCredits = (USHORT)min(Dev->AclPackets, Dev->AclCredits + Count);
    }
    BthAclKick(Dev);
    return TRUE;
}
