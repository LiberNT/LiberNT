/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Encrypted debugger session: Noise NNpsk0 handshake and reliable terminal streams
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdnet.h"

#define KDS_VERSION             1
#define KDS_TYPE_HELLO          1
#define KDS_TYPE_INIT           2
#define KDS_TYPE_RESPONSE       3
#define KDS_TYPE_DATA           4

#define KDS_HEADER_SIZE         20
#define KDS_RENDEZVOUS_SIZE     8
#define KDS_NONCE_SIZE          16
#define KDS_HELLO_BODY          36
#define KDS_HELLO_SIZE          (KDS_HEADER_SIZE + KDS_HELLO_BODY + KDNET_TAG_SIZE)
#define KDS_INIT_PAYLOAD        16
#define KDS_INIT_SIZE           (KDS_HEADER_SIZE + KDNET_DH_SIZE + KDS_INIT_PAYLOAD + KDNET_TAG_SIZE)
#define KDS_RESPONSE_PAYLOAD    24
#define KDS_RESPONSE_SIZE       (KDS_HEADER_SIZE + KDNET_DH_SIZE + KDS_RESPONSE_PAYLOAD + KDNET_TAG_SIZE)
#define KDS_DATA_HEADER         (KDS_HEADER_SIZE + 8)
#define KDS_STREAM_HEADER       24
#define KDS_SEGMENT             (KDNET_DATAGRAM_SIZE - KDS_DATA_HEADER - KDS_STREAM_HEADER - KDNET_TAG_SIZE)
#define KDS_WINDOW              0x8000
#define KDS_RECEIVE_SIZE        1500

#define KDS_CONTROL_GAP         0x0001
#define KDS_CONTROL_INFO        0x0002
#define KDS_CONTROL_SIZE        0x0001
#define KDS_CONTROL_MISSING     0x0002
#define KDS_CONTROL_QUERY       0x0004
#define KDS_HELLO_ATTACHED      0x0001
#define KDS_HELLO_QUERY         0x0002
#define KDS_HELLO_STOPPED       0x0004
#define KDS_HELLO_CRASHED       0x0008
#define KDS_INFO_HEADER         4

#define KDS_RETRANSMIT          (250 * KDNET_MILLISECOND)
#define KDS_RETRANSMIT_MAXIMUM  (2 * KDNET_SECOND)
#define KDS_RETRANSMIT_FAST     (20 * KDNET_MILLISECOND)
#define KDS_COALESCE            (2 * KDNET_MILLISECOND)
#define KDS_KEEPALIVE           (10 * KDNET_SECOND)
#define KDS_TIMEOUT             (60 * KDNET_SECOND)
#define KDS_NO_OFFSET           (~0ULL)

typedef struct _KDNET_SESSION
{
    BOOLEAN Active;
    BOOLEAN Received;
    BOOLEAN AckPending;
    BOOLEAN QueryPending;
    USHORT QueryClass;
    ULONG QueryOffset;
    USHORT Columns;
    USHORT Rows;
    ULONG Id;
    ULONG PeerIp;
    USHORT PeerPort;
    UCHAR ReceiveKey[KDNET_KEY_SIZE];
    UCHAR SendKey[KDNET_KEY_SIZE];
    ULONG64 ReceiveCounter;
    ULONG64 SendCounter;
    ULONG64 SendBase;
    ULONG64 SendNext;
    ULONG64 SendHighest;
    ULONG64 ReceiveNext;
    ULONG64 GapOffset;
    ULONG64 LastReceive;
    ULONG64 LastSend;
    ULONG64 LastProgress;
    ULONG64 LastData;
    ULONG64 Retransmit;
    UCHAR InitDigest[KDNET_HASH_SIZE];
    UCHAR Response[KDS_RESPONSE_SIZE];
} KDNET_SESSION;

CHAR KdSessionKeyText[KDNET_KEY_TEXT_SIZE];
KD_TERMINAL_HOST KdNetHost;
BOOLEAN KdNetStopped;

static const CHAR KdpMagic[4] = {'L', 'K', 'D', 'N'};
static const CHAR KdpProtocolName[] = "Noise_NNpsk0_25519_ChaChaPoly_SHA256";
static const CHAR KdpPrologueLabel[] = "LiberNT KDNET 1";
static const CHAR KdpKeyAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static UCHAR KdpPresharedKey[KDNET_KEY_SIZE];
static UCHAR KdpHelloKey[KDNET_KEY_SIZE];
static UCHAR KdpRendezvous[KDS_RENDEZVOUS_SIZE];
static UCHAR KdpDeviceNonce[KDS_NONCE_SIZE];
static ULONG64 KdpHelloNext;

static UCHAR KdpOutput[KDNET_OUTPUT_SIZE];
static ULONG64 KdpOutputHead;
static UCHAR KdpInput[KDNET_INPUT_SIZE];
static ULONG KdpInputHead;
static ULONG KdpInputCount;

static KDNET_SESSION KdpSession;
static UCHAR KdpPlain[KDS_RECEIVE_SIZE];
static UCHAR KdpSendPlain[KDS_STREAM_HEADER + KDS_SEGMENT];

static
ULONG64
KdpOutputTail(VOID)
{
    return KdpOutputHead > KDNET_OUTPUT_SIZE ? KdpOutputHead - KDNET_OUTPUT_SIZE : 0;
}

static
VOID
KdpHeader(
    _Out_writes_(KDS_HEADER_SIZE) PUCHAR Data,
    _In_ UCHAR Type,
    _In_ ULONG SessionId)
{
    RtlCopyMemory(Data, KdpMagic, sizeof(KdpMagic));
    Data[4] = KDS_VERSION;
    Data[5] = Type;
    Data[6] = 0;
    Data[7] = 0;
    RtlCopyMemory(Data + 8, KdpRendezvous, KDS_RENDEZVOUS_SIZE);
    KdNetPutLe32(Data + 16, SessionId);
}

static
VOID
KdpMixHash(
    _Inout_updates_(KDNET_HASH_SIZE) PUCHAR Hash,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    KdCryptoHash(Hash, KDNET_HASH_SIZE, Data, Length, Hash);
}

static
VOID
KdpSendHello(VOID)
{
    UCHAR Mac[KDNET_HASH_SIZE];
    ULONG Destination, Handle;
    USHORT Flags;
    PUCHAR Data;

    Destination = KdNetOptions.HostIp ? KdNetOptions.HostIp : KDNET_IP_BROADCAST;
    Data = KdUdpBegin(Destination, &Handle);
    if (!Data)
        return;

    Flags = KdpSession.Active ? KDS_HELLO_ATTACHED : 0;
    if (KdNetHost.QueryInformation)
        Flags |= KDS_HELLO_QUERY;

    if (KdNetStopped)
        Flags |= KDS_HELLO_STOPPED;

    if (KdNetHost.QueryState && (KdNetHost.QueryState() & KD_TERMINAL_STATE_CRASHED))
        Flags |= KDS_HELLO_CRASHED;

    KdpHeader(Data, KDS_TYPE_HELLO, 0);
    RtlCopyMemory(Data + 20, KdpDeviceNonce, KDS_NONCE_SIZE);
    RtlCopyMemory(Data + 36, KdNicAddress, MAC_ADDRESS_SIZE);
    KdNetPutLe16(Data + 42, Flags);
    KdNetPutLe32(Data + 44, (ULONG)(KdNetTime() / KDNET_SECOND));
    KdNetPutLe64(Data + 48, KdpOutputHead);
    KdCryptoHmac(KdpHelloKey, sizeof(KdpHelloKey), Data, KDS_HEADER_SIZE + KDS_HELLO_BODY, NULL, 0, Mac);
    RtlCopyMemory(Data + KDS_HEADER_SIZE + KDS_HELLO_BODY, Mac, KDNET_TAG_SIZE);
    KdUdpEnd(Handle, Destination, KdNetOptions.HostPort, KdNetOptions.HostPort, KDS_HELLO_SIZE);
}

static
BOOLEAN
KdpSendSealed(
    _In_ ULONG Length,
    _In_ ULONG64 Now)
{
    ULONG Handle;
    PUCHAR Data;

    Data = KdUdpBegin(KdpSession.PeerIp, &Handle);
    if (!Data)
        return FALSE;

    KdpHeader(Data, KDS_TYPE_DATA, KdpSession.Id);
    KdNetPutLe64(Data + KDS_HEADER_SIZE, KdpSession.SendCounter);
    KdCryptoSeal(KdpSession.SendKey, KdpSession.SendCounter, Data, KDS_DATA_HEADER,
                 KdpSendPlain, KDS_STREAM_HEADER + Length, Data + KDS_DATA_HEADER);
    KdpSession.SendCounter++;
    KdUdpEnd(Handle, KdpSession.PeerIp, KdpSession.PeerPort, KdNetOptions.HostPort,
             KDS_DATA_HEADER + KDS_STREAM_HEADER + Length + KDNET_TAG_SIZE);
    KdpSession.LastSend = Now;
    KdpSession.AckPending = FALSE;
    return TRUE;
}

static
BOOLEAN
KdpSendData(
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_ ULONG64 Now)
{
    ULONG i;

    KdNetPutLe64(KdpSendPlain, Offset);
    KdNetPutLe64(KdpSendPlain + 8, KdpSession.ReceiveNext);
    KdNetPutLe16(KdpSendPlain + 16, Offset == KdpSession.GapOffset ? KDS_CONTROL_GAP : 0);
    KdNetPutLe16(KdpSendPlain + 18, 0);
    KdNetPutLe32(KdpSendPlain + 20, 0);
    for (i = 0; i < Length; i++)
        KdpSendPlain[KDS_STREAM_HEADER + i] = KdpOutput[(ULONG)((Offset + i) & (KDNET_OUTPUT_SIZE - 1))];

    return KdpSendSealed(Length, Now);
}

static
VOID
KdpSendInformation(
    _In_ ULONG64 Now)
{
    ULONG Length = 0, Total = 0;

    if (KdNetHost.QueryInformation)
    {
        Length = KdNetHost.QueryInformation(KdpSession.QueryClass, KdpSession.QueryOffset,
                                            KdpSendPlain + KDS_STREAM_HEADER + KDS_INFO_HEADER,
                                            KDS_SEGMENT - KDS_INFO_HEADER, &Total);
        if (Length > KDS_SEGMENT - KDS_INFO_HEADER)
            Length = 0;
    }

    KdNetPutLe64(KdpSendPlain, KdpSession.SendNext);
    KdNetPutLe64(KdpSendPlain + 8, KdpSession.ReceiveNext);
    KdNetPutLe16(KdpSendPlain + 16, KDS_CONTROL_INFO);
    KdNetPutLe16(KdpSendPlain + 18, KdpSession.QueryClass);
    KdNetPutLe32(KdpSendPlain + 20, KdpSession.QueryOffset);
    KdNetPutLe32(KdpSendPlain + KDS_STREAM_HEADER, Total);
    KdpSendSealed(KDS_INFO_HEADER + Length, Now);
}

static
VOID
KdpSendResponse(
    _In_ ULONG DestinationIp,
    _In_ USHORT DestinationPort)
{
    ULONG Handle;
    PUCHAR Data;

    Data = KdUdpBegin(DestinationIp, &Handle);
    if (!Data)
        return;

    RtlCopyMemory(Data, KdpSession.Response, KDS_RESPONSE_SIZE);
    KdUdpEnd(Handle, DestinationIp, DestinationPort, KdNetOptions.HostPort, KDS_RESPONSE_SIZE);
}

static
VOID
KdpHandleInit(
    _In_ ULONG SourceIp,
    _In_ USHORT SourcePort,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _In_ ULONG64 Now)
{
    UCHAR Hash[KDNET_HASH_SIZE], Chain[KDNET_HASH_SIZE], Key[KDNET_KEY_SIZE], Digest[KDNET_HASH_SIZE];
    UCHAR Prologue[sizeof(KdpPrologueLabel) - 1 + KDS_RENDEZVOUS_SIZE + KDS_NONCE_SIZE];
    UCHAR Payload[KDS_RESPONSE_PAYLOAD], Public[KDNET_DH_SIZE], Secret[KDNET_DH_SIZE];
    PUCHAR Remote = Data + KDS_HEADER_SIZE, Cipher = Remote + KDNET_DH_SIZE, Response;
    ULONG64 Start, Tail;
    USHORT Columns, Rows;
    ULONG Id;

    if (Length != KDS_INIT_SIZE)
        return;

    KdCryptoHash(Data, Length, NULL, 0, Digest);
    if (KdpSession.Active && KdCryptoEqual(Digest, KdpSession.InitDigest, sizeof(Digest)))
    {
        KdpSendResponse(SourceIp, SourcePort);
        return;
    }

    KdCryptoHash((const UCHAR *)KdpProtocolName, sizeof(KdpProtocolName) - 1, NULL, 0, Hash);
    RtlCopyMemory(Chain, Hash, sizeof(Chain));
    RtlCopyMemory(Prologue, KdpPrologueLabel, sizeof(KdpPrologueLabel) - 1);
    RtlCopyMemory(Prologue + sizeof(KdpPrologueLabel) - 1, KdpRendezvous, KDS_RENDEZVOUS_SIZE);
    RtlCopyMemory(Prologue + sizeof(KdpPrologueLabel) - 1 + KDS_RENDEZVOUS_SIZE, KdpDeviceNonce, KDS_NONCE_SIZE);
    KdpMixHash(Hash, Prologue, sizeof(Prologue));

    KdCryptoHkdf(Chain, KdpPresharedKey, sizeof(KdpPresharedKey), Chain, Secret, Key);
    KdpMixHash(Hash, Secret, sizeof(Secret));
    KdpMixHash(Hash, Remote, KDNET_DH_SIZE);
    KdCryptoHkdf(Chain, Remote, KDNET_DH_SIZE, Chain, Key, NULL);
    if (!KdCryptoOpen(Key, 0, Hash, sizeof(Hash), Cipher, KDS_INIT_PAYLOAD + KDNET_TAG_SIZE, Payload))
        goto Cleanup;

    KdpMixHash(Hash, Cipher, KDS_INIT_PAYLOAD + KDNET_TAG_SIZE);
    Start = KdNetGetLe64(Payload);
    Columns = KdNetGetLe16(Payload + 8);
    Rows = KdNetGetLe16(Payload + 10);

    if (!KdCryptoExchange(Remote, Public, Secret))
        goto Cleanup;

    KdpMixHash(Hash, Public, sizeof(Public));
    KdCryptoHkdf(Chain, Public, sizeof(Public), Chain, Key, NULL);
    KdCryptoHkdf(Chain, Secret, sizeof(Secret), Chain, Key, NULL);

    Tail = KdpOutputTail();
    if (Start == KDS_NO_OFFSET || Start > KdpOutputHead)
        Start = KdpOutputHead;
    else if (Start < Tail)
        Start = Tail;

    do
    {
        KdCryptoRandom((PUCHAR)&Id, sizeof(Id));
    } while (!Id);

    KdNetPutLe64(Payload, Start);
    KdNetPutLe64(Payload + 8, KdpOutputHead);
    KdNetPutLe32(Payload + 16, Id);
    KdNetPutLe32(Payload + 20, 0);

    Response = KdpSession.Response;
    KdpHeader(Response, KDS_TYPE_RESPONSE, Id);
    RtlCopyMemory(Response + KDS_HEADER_SIZE, Public, sizeof(Public));
    KdCryptoSeal(Key, 0, Hash, sizeof(Hash), Payload, sizeof(Payload),
                 Response + KDS_HEADER_SIZE + KDNET_DH_SIZE);
    KdCryptoHkdf(Chain, NULL, 0, KdpSession.ReceiveKey, KdpSession.SendKey, NULL);

    RtlCopyMemory(KdpSession.InitDigest, Digest, sizeof(Digest));
    KdpSession.Active = TRUE;
    KdpSession.Received = FALSE;
    KdpSession.AckPending = FALSE;
    KdpSession.QueryPending = FALSE;
    KdpSession.Columns = Columns;
    KdpSession.Rows = Rows;
    KdpSession.Id = Id;
    KdpSession.PeerIp = SourceIp;
    KdpSession.PeerPort = SourcePort;
    KdpSession.ReceiveCounter = 0;
    KdpSession.SendCounter = 0;
    KdpSession.SendBase = Start;
    KdpSession.SendNext = Start;
    KdpSession.SendHighest = Start;
    KdpSession.ReceiveNext = 0;
    KdpSession.GapOffset = KDS_NO_OFFSET;
    KdpSession.LastReceive = Now;
    KdpSession.LastSend = Now;
    KdpSession.LastProgress = Now;
    KdpSession.LastData = 0;
    KdpSession.Retransmit = KDS_RETRANSMIT;
    KdpInputHead = 0;
    KdpInputCount = 0;

    KdCryptoRandom(KdpDeviceNonce, sizeof(KdpDeviceNonce));
    KdpHelloNext = Now;
    KdpSendResponse(SourceIp, SourcePort);

Cleanup:
    KdCryptoWipe(Chain, sizeof(Chain));
    KdCryptoWipe(Key, sizeof(Key));
    KdCryptoWipe(Secret, sizeof(Secret));
}

static
VOID
KdpHandleData(
    _In_ ULONG SourceIp,
    _In_ USHORT SourcePort,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length,
    _In_ ULONG64 Now)
{
    ULONG64 Counter, Sequence, Acknowledged;
    ULONG PayloadLength, Skip;
    USHORT Control;
    PUCHAR Payload;

    if (!KdpSession.Active || Length < KDS_DATA_HEADER + KDS_STREAM_HEADER + KDNET_TAG_SIZE ||
        Length > KDS_RECEIVE_SIZE || KdNetGetLe32(Data + 16) != KdpSession.Id)
    {
        return;
    }

    Counter = KdNetGetLe64(Data + KDS_HEADER_SIZE);
    if (KdpSession.Received && Counter <= KdpSession.ReceiveCounter)
        return;

    if (!KdCryptoOpen(KdpSession.ReceiveKey, Counter, Data, KDS_DATA_HEADER,
                      Data + KDS_DATA_HEADER, Length - KDS_DATA_HEADER, KdpPlain))
    {
        return;
    }

    KdpSession.Received = TRUE;
    KdpSession.ReceiveCounter = Counter;
    KdpSession.LastReceive = Now;
    KdpSession.PeerIp = SourceIp;
    KdpSession.PeerPort = SourcePort;

    Sequence = KdNetGetLe64(KdpPlain);
    Acknowledged = KdNetGetLe64(KdpPlain + 8);
    Control = KdNetGetLe16(KdpPlain + 16);
    if (Control & KDS_CONTROL_SIZE)
    {
        KdpSession.Columns = KdNetGetLe16(KdpPlain + 18);
        KdpSession.Rows = KdNetGetLe16(KdpPlain + 20);
    }
    else if (Control & KDS_CONTROL_QUERY)
    {
        KdpSession.QueryClass = KdNetGetLe16(KdpPlain + 18);
        KdpSession.QueryOffset = KdNetGetLe32(KdpPlain + 20);
        KdpSession.QueryPending = TRUE;
    }

    if (Acknowledged > KdpSession.SendBase && Acknowledged <= KdpSession.SendHighest)
    {
        KdpSession.SendBase = Acknowledged;
        if (KdpSession.SendNext < Acknowledged)
            KdpSession.SendNext = Acknowledged;

        if (KdpSession.GapOffset != KDS_NO_OFFSET && Acknowledged > KdpSession.GapOffset)
            KdpSession.GapOffset = KDS_NO_OFFSET;

        KdpSession.LastProgress = Now;
        KdpSession.Retransmit = KDS_RETRANSMIT;
    }
    else if ((Control & KDS_CONTROL_MISSING) && Acknowledged == KdpSession.SendBase &&
             KdpSession.SendBase < KdpSession.SendNext &&
             Now - KdpSession.LastProgress >= KDS_RETRANSMIT_FAST)
    {
        KdpSession.SendNext = KdpSession.SendBase;
        KdpSession.LastProgress = Now;
    }

    PayloadLength = Length - KDS_DATA_HEADER - KDS_STREAM_HEADER - KDNET_TAG_SIZE;
    if (!PayloadLength)
        return;

    KdpSession.AckPending = TRUE;
    if (Sequence > KdpSession.ReceiveNext || KdpSession.ReceiveNext - Sequence >= PayloadLength)
        return;

    Skip = (ULONG)(KdpSession.ReceiveNext - Sequence);
    Payload = KdpPlain + KDS_STREAM_HEADER + Skip;
    PayloadLength -= Skip;
    while (PayloadLength && KdpInputCount < KDNET_INPUT_SIZE)
    {
        KdpInput[(KdpInputHead + KdpInputCount) % KDNET_INPUT_SIZE] = *Payload++;
        KdpInputCount++;
        KdpSession.ReceiveNext++;
        PayloadLength--;
    }
}

VOID
KdSessionInput(
    _In_ ULONG SourceIp,
    _In_ USHORT SourcePort,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    ULONG64 Now = KdNetTime();

    KdCryptoAddEntropy(&Now, sizeof(Now));
    if (Length < KDS_HEADER_SIZE || Data[4] != KDS_VERSION ||
        RtlCompareMemory(Data, KdpMagic, sizeof(KdpMagic)) != sizeof(KdpMagic) ||
        RtlCompareMemory(Data + 8, KdpRendezvous, KDS_RENDEZVOUS_SIZE) != KDS_RENDEZVOUS_SIZE)
    {
        return;
    }

    if (Data[5] == KDS_TYPE_INIT)
        KdpHandleInit(SourceIp, SourcePort, Data, Length, Now);
    else if (Data[5] == KDS_TYPE_DATA)
        KdpHandleData(SourceIp, SourcePort, Data, Length, Now);
}

VOID
KdSessionTimer(
    _In_ ULONG64 Now)
{
    ULONG64 Tail, Pending;
    ULONG Length;

    if (!KdIpAddress)
        return;

    if (Now >= KdpHelloNext)
    {
        KdpSendHello();
        KdpHelloNext = Now + (KdpSession.Active ? 2 : 1) * KDNET_SECOND;
    }

    if (!KdpSession.Active)
        return;

    if (Now - KdpSession.LastReceive > KDS_TIMEOUT)
    {
        KdCryptoWipe(&KdpSession, sizeof(KdpSession));
        KdpHelloNext = Now;
        return;
    }

    Tail = KdpOutputTail();
    if (KdpSession.SendBase < Tail)
    {
        KdpSession.SendBase = Tail;
        KdpSession.SendNext = Tail;
        KdpSession.GapOffset = Tail;
        if (KdpSession.SendHighest < Tail)
            KdpSession.SendHighest = Tail;
    }

    if (KdpSession.SendBase < KdpSession.SendNext &&
        Now - KdpSession.LastProgress >= KdpSession.Retransmit)
    {
        KdpSession.SendNext = KdpSession.SendBase;
        KdpSession.LastProgress = Now;
        KdpSession.Retransmit = min(KdpSession.Retransmit * 2, KDS_RETRANSMIT_MAXIMUM);
    }

    while (KdpSession.SendNext < KdpOutputHead &&
           KdpSession.SendNext - KdpSession.SendBase < KDS_WINDOW)
    {
        Pending = KdpOutputHead - KdpSession.SendNext;
        if (Pending < KDS_SEGMENT && KdpSession.SendBase != KdpSession.SendNext &&
            Now - KdpSession.LastData < KDS_COALESCE)
        {
            break;
        }

        Length = (ULONG)min(Pending, KDS_SEGMENT);
        if (!KdpSendData(KdpSession.SendNext, Length, Now))
            break;

        if (KdpSession.SendBase == KdpSession.SendNext)
            KdpSession.LastProgress = Now;

        KdpSession.SendNext += Length;
        if (KdpSession.SendHighest < KdpSession.SendNext)
            KdpSession.SendHighest = KdpSession.SendNext;

        KdpSession.LastData = Now;
    }

    if (KdpSession.QueryPending)
    {
        KdpSession.QueryPending = FALSE;
        KdpSendInformation(Now);
    }

    if (KdpSession.AckPending || Now - KdpSession.LastSend >= KDS_KEEPALIVE)
        KdpSendData(KdpSession.SendNext, 0, Now);
}

VOID
KdSessionWrite(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ ULONG Length)
{
    while (Length--)
    {
        KdpOutput[(ULONG)(KdpOutputHead & (KDNET_OUTPUT_SIZE - 1))] = (UCHAR)*Buffer++;
        KdpOutputHead++;
    }
}

BOOLEAN
KdSessionRead(
    _Out_ PUCHAR Byte)
{
    if (!KdpInputCount)
        return FALSE;

    *Byte = KdpInput[KdpInputHead];
    KdpInputHead = (KdpInputHead + 1) % KDNET_INPUT_SIZE;
    KdpInputCount--;
    return TRUE;
}

BOOLEAN
KdSessionTakeBreak(VOID)
{
    BOOLEAN Break = FALSE;
    UCHAR Byte;

    while (KdSessionRead(&Byte))
    {
        if (Byte == 3)
            Break = TRUE;
    }

    return Break;
}

BOOLEAN
KdSessionAttached(
    _Out_opt_ PULONG Columns,
    _Out_opt_ PULONG Rows)
{
    if (!KdpSession.Active)
        return FALSE;

    if (Columns)
        *Columns = KdpSession.Columns;
    if (Rows)
        *Rows = KdpSession.Rows;

    return TRUE;
}

VOID
KdSessionInitialize(VOID)
{
    static const CHAR KeyLabel[] = "LiberNT KDNET v1 key";
    static const CHAR HelloLabel[] = "LiberNT KDNET v1 hello";
    static const CHAR RendezvousLabel[] = "LiberNT KDNET v1 rendezvous";
    UCHAR Random[25], Hash[KDNET_HASH_SIZE];
    CHAR Normalized[KDNET_KEY_TEXT_SIZE];
    ULONG i, Length = 0;
    PCHAR Text = KdSessionKeyText;
    CHAR Character;

    if (KdNetOptions.HaveKey)
    {
        RtlCopyMemory(KdSessionKeyText, KdNetOptions.Key, sizeof(KdSessionKeyText));
        KdSessionKeyText[sizeof(KdSessionKeyText) - 1] = ANSI_NULL;
    }
    else
    {
        KdCryptoRandom(Random, sizeof(Random));
        for (i = 0; i < sizeof(Random); i++)
        {
            if (i && i % 5 == 0)
                *Text++ = '-';

            *Text++ = KdpKeyAlphabet[Random[i] & 31];
        }

        *Text = ANSI_NULL;
        KdCryptoWipe(Random, sizeof(Random));
    }

    for (i = 0; KdSessionKeyText[i]; i++)
    {
        Character = KdSessionKeyText[i];
        if (Character == '-' || Character == ' ')
            continue;

        if (Character >= 'a' && Character <= 'z')
            Character -= 'a' - 'A';

        Normalized[Length++] = Character;
    }

    KdCryptoHash((const UCHAR *)KeyLabel, sizeof(KeyLabel) - 1,
                 (const UCHAR *)Normalized, Length, KdpPresharedKey);
    KdCryptoHash((const UCHAR *)HelloLabel, sizeof(HelloLabel) - 1,
                 KdpPresharedKey, sizeof(KdpPresharedKey), KdpHelloKey);
    KdCryptoHash((const UCHAR *)RendezvousLabel, sizeof(RendezvousLabel) - 1,
                 KdpPresharedKey, sizeof(KdpPresharedKey), Hash);
    RtlCopyMemory(KdpRendezvous, Hash, sizeof(KdpRendezvous));
    KdCryptoRandom(KdpDeviceNonce, sizeof(KdpDeviceNonce));
    KdCryptoWipe(Normalized, sizeof(Normalized));
}
