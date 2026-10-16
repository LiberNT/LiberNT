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
#define L2CAP_STATUS_AUTHENTICATION_PENDING 0x0001
#define L2CAP_STATUS_AUTHORIZATION_PENDING  0x0002
#define L2CAP_CONFIG_UNACCEPTABLE       0x0001
#define L2CAP_SIGNAL_TIMEOUT_MS         30000
#define L2CAP_MAX_CHANNELS_PER_LINK     16
#define ACL_PB_CONTINUING               0x01
#define ACL_PB_START                    0x02
#define ACL_PB_START_NON_FLUSHABLE      0x00

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
    while ((Dev->LePackets ? Dev->LeCredits : Dev->AclCredits) && !IsListEmpty(&Dev->LeAclQueue))
    {
        PBTHMINI_ACL_PACKET Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->LeAclQueue), BTHMINI_ACL_PACKET, Link);
        PBTHMINI_REMOTE Remote = BthLeFindHandle(Dev, BthGet16(Packet->Data) & 0x0FFF);

        if (Remote && NT_SUCCESS(BthMiniSendPacket(Dev, HciPacketAclData, Packet->Data, Packet->Length)))
        {
            if (Dev->LePackets)
                Dev->LeCredits--;
            else
                Dev->AclCredits--;
            Remote->LeAclOutstanding++;
        }
        ExFreePoolWithTag(Packet, BTHMINI_TAG);
    }
}

static
NTSTATUS
BthAclQueueFrame(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Handle,
    _In_ BOOLEAN Le,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    ULONG Offset = 0, Mtu = Le && Dev->LePackets ? Dev->LeMtu : Dev->AclMtu;
    UCHAR Start = Le ? ACL_PB_START_NON_FLUSHABLE : ACL_PB_START;

    if (!Mtu)
        return STATUS_INVALID_DEVICE_STATE;
    while (Offset < Length)
    {
        ULONG Chunk = min(Length - Offset, Mtu);
        PBTHMINI_ACL_PACKET Packet;

        Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(BTHMINI_ACL_PACKET, Data[4 + Chunk]), BTHMINI_TAG);
        if (!Packet)
            return STATUS_INSUFFICIENT_RESOURCES;
        BthPut16(Packet->Data, (USHORT)(Handle | ((Offset ? ACL_PB_CONTINUING : Start) << 12)));
        BthPut16(Packet->Data + 2, (USHORT)Chunk);
        RtlCopyMemory(Packet->Data + 4, Frame + Offset, Chunk);
        Packet->Length = 4 + Chunk;
        InsertTailList(Le ? &Dev->LeAclQueue : &Dev->AclQueue, &Packet->Link);
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
    Status = BthAclQueueFrame(Dev, Remote->Handle, FALSE, Frame, 4 + Length);
    ExFreePoolWithTag(Frame, BTHMINI_TAG);
    return Status;
}

NTSTATUS
BthL2capSendLe(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Cid,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    PUCHAR Frame;
    NTSTATUS Status;

    if (Length > L2CAP_MAX_FRAME || !Remote->LeConnected)
        return STATUS_INVALID_PARAMETER;
    Frame = ExAllocatePoolWithTag(NonPagedPool, 4 + Length, BTHMINI_TAG);
    if (!Frame)
        return STATUS_INSUFFICIENT_RESOURCES;
    BthPut16(Frame, (USHORT)Length);
    BthPut16(Frame + 2, Cid);
    RtlCopyMemory(Frame + 4, Payload, Length);
    Status = BthAclQueueFrame(Dev, Remote->LeHandle, TRUE, Frame, 4 + Length);
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
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Psm,
    _Out_ PBOOLEAN Deferred)
{
    *Deferred = FALSE;
    switch (Psm)
    {
        case L2CAP_PSM_SDP:
            return BthSdpServerCallbacks();
        case L2CAP_PSM_RFCOMM:
            return BthRfcommServerCallbacks();
        default:
            *Deferred = TRUE;
            return BthBrbServer(Dev, Psm);
    }
}

static
VOID
BthL2capRespond(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ USHORT Result,
    _In_ USHORT Status)
{
    UCHAR Response[8];

    BthPut16(Response, Result == L2CAP_RESULT_SUCCESS || Result == L2CAP_RESULT_PENDING ? Channel->LocalCid : 0);
    BthPut16(Response + 2, Channel->RemoteCid);
    BthPut16(Response + 4, Result);
    BthPut16(Response + 6, Status);
    BthL2capSignal(Dev, Channel->Remote, L2CAP_CONNECTION_RESPONSE, Channel->RequestIdentifier, Response, sizeof(Response));
}

static
VOID
BthL2capDropIncoming(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ USHORT Result)
{
    DPRINT1("BTHMINI: L2CAP PSM 0x%04x refused, result %u\n", Channel->Psm, Result);
    BthL2capRespond(Dev, Channel, Result, 0);
    BthL2capClose(Dev, Channel, STATUS_CONNECTION_REFUSED);
}

static
VOID
BthL2capProceedIncoming(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel)
{
    if (Channel->Deferred)
    {
        Channel->State = BthChannelWaitAccept;
        Channel->Deadline = BthNowMs() + L2CAP_SIGNAL_TIMEOUT_MS;
        BthL2capRespond(Dev, Channel, L2CAP_RESULT_PENDING, L2CAP_STATUS_AUTHORIZATION_PENDING);
        BthBrbRemoteConnect(Dev, Channel);
        return;
    }
    Channel->State = BthChannelConfig;
    BthL2capRespond(Dev, Channel, L2CAP_RESULT_SUCCESS, 0);
    BthL2capSendConfiguration(Dev, Channel);
}

VOID
BthL2capAccept(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ USHORT Result)
{
    if (Channel->State != BthChannelWaitAccept)
        return;
    if (Result != L2CAP_RESULT_SUCCESS)
    {
        BthL2capDropIncoming(Dev, Channel, Result);
        return;
    }
    Channel->State = BthChannelConfig;
    BthL2capRespond(Dev, Channel, L2CAP_RESULT_SUCCESS, 0);
    BthL2capSendConfiguration(Dev, Channel);
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
    PBTHMINI_CHANNEL Channel;
    USHORT Psm = BthGet16(Request), RemoteCid = BthGet16(Request + 2), Result;
    BOOLEAN Deferred;
    UCHAR Response[8];
    NTSTATUS Status;

    Server = BthL2capServer(Dev, Psm, &Deferred);
    if (!Server)
        Result = L2CAP_RESULT_BAD_PSM;
    else if (BthL2capCountChannels(Dev, Remote) >= L2CAP_MAX_CHANNELS_PER_LINK)
        Result = L2CAP_RESULT_NO_RESOURCES;
    else
        Result = L2CAP_RESULT_SUCCESS;
    Channel = Result == L2CAP_RESULT_SUCCESS ? ExAllocatePoolZero(NonPagedPool, sizeof(*Channel), BTHMINI_TAG) : NULL;
    if (!Channel)
    {
        if (Result == L2CAP_RESULT_SUCCESS)
            Result = L2CAP_RESULT_NO_RESOURCES;
        BthPut16(Response, 0);
        BthPut16(Response + 2, RemoteCid);
        BthPut16(Response + 4, Result);
        BthPut16(Response + 6, 0);
        BthL2capSignal(Dev, Remote, L2CAP_CONNECTION_RESPONSE, Identifier, Response, sizeof(Response));
        DPRINT1("BTHMINI: L2CAP PSM 0x%04x refused, result %u\n", Psm, Result);
        return;
    }

    Channel->Remote = Remote;
    Channel->Callbacks = Server;
    Channel->Psm = Psm;
    Channel->Deferred = Deferred;
    Channel->LocalCid = BthL2capAllocateCid(Dev);
    Channel->RemoteCid = RemoteCid;
    Channel->RemoteMtu = L2CAP_DEFAULT_MTU;
    Channel->RequestIdentifier = Identifier;
    Channel->State = BthChannelWaitSecurity;
    Channel->Deadline = BthNowMs() + L2CAP_SIGNAL_TIMEOUT_MS;
    InsertTailList(&Dev->Channels, &Channel->Link);

    Status = BthConnEnsureLink(Dev, Remote, Psm != L2CAP_PSM_SDP);
    if (Status == STATUS_SUCCESS)
        BthL2capProceedIncoming(Dev, Channel);
    else if (Status == STATUS_PENDING)
        BthL2capRespond(Dev, Channel, L2CAP_RESULT_PENDING, L2CAP_STATUS_AUTHENTICATION_PENDING);
    else
        BthL2capDropIncoming(Dev, Channel, L2CAP_RESULT_SECURITY_BLOCK);
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
BthL2capDropFrame(
    _Inout_ PUCHAR *Frame,
    _Inout_ PULONG Expected,
    _Inout_ PULONG Received)
{
    if (*Frame)
        ExFreePoolWithTag(*Frame, BTHMINI_TAG);
    *Frame = NULL;
    *Expected = 0;
    *Received = 0;
}

static
VOID
BthL2capDropReassembly(
    _In_ PBTHMINI_REMOTE Remote)
{
    BthL2capDropFrame(&Remote->RxFrame, &Remote->RxExpected, &Remote->RxLength);
}

static
VOID
BthL2capDeliver(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ BOOLEAN Le,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    if (!Le)
    {
        BthL2capFrame(Dev, Remote, Frame, Length);
        return;
    }
    if (4 + (ULONG)BthGet16(Frame) == Length)
        BthLeFrame(Dev, Remote, BthGet16(Frame + 2), Frame + 4, Length - 4);
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
    BOOLEAN Le = FALSE;
    PUCHAR *RxFrame;
    PULONG RxExpected, RxLength;

    if (Length < 4)
        return;
    Handle = BthGet16(Data) & 0x0FFF;
    Boundary = (Data[1] >> 4) & 0x03;
    AclLength = BthGet16(Data + 2);
    if (4 + (ULONG)AclLength > Length)
        return;
    Remote = BthRadioFindHandle(Dev, Handle);
    if (!Remote)
    {
        Remote = BthLeFindHandle(Dev, Handle);
        Le = TRUE;
    }
    if (!Remote)
        return;
    RxFrame = Le ? &Remote->LeRxFrame : &Remote->RxFrame;
    RxExpected = Le ? &Remote->LeRxExpected : &Remote->RxExpected;
    RxLength = Le ? &Remote->LeRxLength : &Remote->RxLength;
    Data += 4;

    if (Boundary != ACL_PB_CONTINUING)
    {
        ULONG FrameLength;

        BthL2capDropFrame(RxFrame, RxExpected, RxLength);
        if (AclLength < 4)
            return;
        FrameLength = 4 + BthGet16(Data);
        if (AclLength >= FrameLength)
        {
            BthL2capDeliver(Dev, Remote, Le, Data, FrameLength);
            return;
        }
        *RxFrame = ExAllocatePoolWithTag(NonPagedPool, FrameLength, BTHMINI_TAG);
        if (!*RxFrame)
            return;
        *RxExpected = FrameLength;
        *RxLength = 0;
    }
    if (!*RxFrame)
        return;
    if (AclLength > *RxExpected - *RxLength)
    {
        BthL2capDropFrame(RxFrame, RxExpected, RxLength);
        return;
    }
    RtlCopyMemory(*RxFrame + *RxLength, Data, AclLength);
    *RxLength += AclLength;
    if (*RxLength == *RxExpected)
    {
        PUCHAR Frame = *RxFrame;
        ULONG FrameLength = *RxLength;

        *RxFrame = NULL;
        *RxExpected = 0;
        *RxLength = 0;
        BthL2capDeliver(Dev, Remote, Le, Frame, FrameLength);
        ExFreePoolWithTag(Frame, BTHMINI_TAG);
    }
}

VOID
BthL2capLeLinkDown(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PLIST_ENTRY Entry, Next;

    BthL2capDropFrame(&Remote->LeRxFrame, &Remote->LeRxExpected, &Remote->LeRxLength);
    for (Entry = Dev->LeAclQueue.Flink; Entry != &Dev->LeAclQueue; Entry = Next)
    {
        PBTHMINI_ACL_PACKET Packet = CONTAINING_RECORD(Entry, BTHMINI_ACL_PACKET, Link);

        Next = Entry->Flink;
        if ((BthGet16(Packet->Data) & 0x0FFF) == Remote->LeHandle)
        {
            RemoveEntryList(&Packet->Link);
            ExFreePoolWithTag(Packet, BTHMINI_TAG);
        }
    }
    if (Dev->LePackets)
        Dev->LeCredits = (USHORT)min(Dev->LePackets, Dev->LeCredits + Remote->LeAclOutstanding);
    else
        Dev->AclCredits = (USHORT)min(Dev->AclPackets, Dev->AclCredits + Remote->LeAclOutstanding);
    Remote->LeAclOutstanding = 0;
    BthAclKick(Dev);
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
        if (Channel->State == BthChannelWaitSecurity)
        {
            if (NT_SUCCESS(Status) && (Remote->Encrypted || Channel->Psm == L2CAP_PSM_SDP))
                BthL2capProceedIncoming(Dev, Channel);
            else if (!NT_SUCCESS(Status))
                BthL2capDropIncoming(Dev, Channel, L2CAP_RESULT_SECURITY_BLOCK);
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
            if (Channel->State == BthChannelWaitSecurity || Channel->State == BthChannelWaitAccept)
                BthL2capRespond(Dev, Channel, L2CAP_RESULT_NO_RESOURCES, 0);
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
    while (!IsListEmpty(&Dev->LeAclQueue))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dev->LeAclQueue), BTHMINI_ACL_PACKET, Link), BTHMINI_TAG);
    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        BthL2capDropReassembly(Remote);
        BthL2capDropFrame(&Remote->LeRxFrame, &Remote->LeRxExpected, &Remote->LeRxLength);
    }
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
        USHORT Handle = BthGet16(Entry) & 0x0FFF, Count = BthGet16(Entry + 2);
        PBTHMINI_REMOTE Remote = BthRadioFindHandle(Dev, Handle);

        if (Remote)
        {
            Remote->AclOutstanding = Remote->AclOutstanding > Count ? Remote->AclOutstanding - Count : 0;
            Dev->AclCredits = (USHORT)min(Dev->AclPackets, Dev->AclCredits + Count);
            continue;
        }
        Remote = BthLeFindHandle(Dev, Handle);
        if (Remote)
            Remote->LeAclOutstanding = Remote->LeAclOutstanding > Count ? Remote->LeAclOutstanding - Count : 0;
        if (Remote && Dev->LePackets)
            Dev->LeCredits = (USHORT)min(Dev->LePackets, Dev->LeCredits + Count);
        else
            Dev->AclCredits = (USHORT)min(Dev->AclPackets, Dev->AclCredits + Count);
    }
    BthAclKick(Dev);
    return TRUE;
}
