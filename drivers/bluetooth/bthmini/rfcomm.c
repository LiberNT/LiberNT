/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RFCOMM multiplexer with credit-based flow control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define RFCOMM_SABM                 0x2F
#define RFCOMM_UA                   0x63
#define RFCOMM_DM                   0x0F
#define RFCOMM_DISC                 0x43
#define RFCOMM_UIH                  0xEF
#define RFCOMM_PF                   0x10
#define RFCOMM_MCC_PN               0x20
#define RFCOMM_MCC_MSC              0x38
#define RFCOMM_MCC_FCON             0x28
#define RFCOMM_MCC_FCOFF            0x18
#define RFCOMM_MCC_RPN              0x24
#define RFCOMM_MCC_RLS              0x14
#define RFCOMM_MCC_TEST             0x08
#define RFCOMM_MCC_NSC              0x04
#define RFCOMM_CFC_REQUEST          0xF0
#define RFCOMM_CFC_ACCEPT           0xE0
#define RFCOMM_V24_SIGNALS          0x8D
#define RFCOMM_INITIAL_CREDITS      7
#define RFCOMM_TIMEOUT_MS           20000
#define RFCOMM_MAX_FRAME_SIZE       (L2CAP_LOCAL_MTU - 5)

typedef struct _BTHMINI_DLC_BUFFER
{
    LIST_ENTRY Link;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} BTHMINI_DLC_BUFFER, *PBTHMINI_DLC_BUFFER;

static UCHAR BthRfcommCrcTable[256];

static
ULONGLONG
BthRfcommNow(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
UCHAR
BthRfcommFcs(
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Fcs = 0xFF;
    ULONG Index;

    if (!BthRfcommCrcTable[1])
    {
        for (Index = 0; Index < 256; Index++)
        {
            UCHAR Crc = (UCHAR)Index;
            ULONG Bit;

            for (Bit = 0; Bit < 8; Bit++)
                Crc = (Crc & 1) ? (UCHAR)((Crc >> 1) ^ 0xE0) : (UCHAR)(Crc >> 1);
            BthRfcommCrcTable[Index] = Crc;
        }
    }
    for (Index = 0; Index < Length; Index++)
        Fcs = BthRfcommCrcTable[Fcs ^ Data[Index]];
    return (UCHAR)(0xFF - Fcs);
}

static
UCHAR
BthRfcommAddress(
    _In_ BOOLEAN CommandResponse,
    _In_ UCHAR Dlci)
{
    return (UCHAR)(0x01 | (CommandResponse ? 0x02 : 0) | (Dlci << 2));
}

static
NTSTATUS
BthRfcommSendFrame(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_RFCOMM_SESSION Session,
    _In_ UCHAR Address,
    _In_ UCHAR Control,
    _In_ BOOLEAN HasCredits,
    _In_ UCHAR Credits,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PUCHAR Frame;
    ULONG Size = 0, HeaderLength;
    NTSTATUS Status;

    if (!Session->Channel || Session->Channel->State != BthChannelOpen)
        return STATUS_INVALID_DEVICE_STATE;
    Frame = ExAllocatePoolWithTag(NonPagedPool, 6 + Length, BTHMINI_TAG);
    if (!Frame)
        return STATUS_INSUFFICIENT_RESOURCES;
    Frame[Size++] = Address;
    Frame[Size++] = Control;
    if (Length <= 127)
    {
        Frame[Size++] = (UCHAR)((Length << 1) | 1);
    }
    else
    {
        Frame[Size++] = (UCHAR)(Length << 1);
        Frame[Size++] = (UCHAR)(Length >> 7);
    }
    HeaderLength = Size;
    if (HasCredits)
        Frame[Size++] = Credits;
    if (Length)
        RtlCopyMemory(Frame + Size, Data, Length);
    Size += Length;
    Frame[Size++] = BthRfcommFcs(Frame, (Control & ~RFCOMM_PF) == RFCOMM_UIH ? 2 : HeaderLength);
    Status = BthL2capSend(Dev, Session->Channel, Frame, Size);
    ExFreePoolWithTag(Frame, BTHMINI_TAG);
    return Status;
}

static
VOID
BthRfcommSendControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_RFCOMM_SESSION Session,
    _In_ UCHAR Dlci,
    _In_ UCHAR Control)
{
    BOOLEAN Command = Control == RFCOMM_SABM || Control == RFCOMM_DISC;
    BOOLEAN CommandResponse = Command ? Session->Initiator : !Session->Initiator;

    BthRfcommSendFrame(Dev, Session, BthRfcommAddress(CommandResponse, Dlci), (UCHAR)(Control | RFCOMM_PF),
                       FALSE, 0, NULL, 0);
}

static
VOID
BthRfcommSendMcc(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_RFCOMM_SESSION Session,
    _In_ UCHAR Type,
    _In_ BOOLEAN Command,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ UCHAR Length)
{
    UCHAR Message[2 + 16];

    Message[0] = (UCHAR)((Type << 2) | (Command ? 0x02 : 0) | 0x01);
    Message[1] = (UCHAR)((Length << 1) | 1);
    RtlCopyMemory(Message + 2, Data, min(Length, 16));
    BthRfcommSendFrame(Dev, Session, BthRfcommAddress(Session->Initiator, 0), RFCOMM_UIH, FALSE, 0,
                       Message, 2 + min(Length, 16));
}

static
VOID
BthRfcommSendParameters(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ BOOLEAN Command,
    _In_ UCHAR Flow)
{
    UCHAR Pn[8];

    Pn[0] = Dlc->Dlci;
    Pn[1] = Flow;
    Pn[2] = 7;
    Pn[3] = 0;
    Pn[4] = (UCHAR)Dlc->FrameSize;
    Pn[5] = (UCHAR)(Dlc->FrameSize >> 8);
    Pn[6] = 0;
    Pn[7] = RFCOMM_INITIAL_CREDITS;
    BthRfcommSendMcc(Dev, Dlc->Session, RFCOMM_MCC_PN, Command, Pn, sizeof(Pn));
}

static
VOID
BthRfcommSendModemStatus(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ BOOLEAN Command)
{
    UCHAR Msc[2];

    Msc[0] = (UCHAR)((Dlc->Dlci << 2) | 0x03);
    Msc[1] = RFCOMM_V24_SIGNALS;
    BthRfcommSendMcc(Dev, Dlc->Session, RFCOMM_MCC_MSC, Command, Msc, sizeof(Msc));
}

static
PBTHMINI_DLC
BthRfcommFindDlc(
    _In_ PBTHMINI_RFCOMM_SESSION Session,
    _In_ UCHAR Dlci)
{
    PLIST_ENTRY Entry;

    for (Entry = Session->Dlcs.Flink; Entry != &Session->Dlcs; Entry = Entry->Flink)
    {
        PBTHMINI_DLC Dlc = CONTAINING_RECORD(Entry, BTHMINI_DLC, Link);

        if (Dlc->Dlci == Dlci)
            return Dlc;
    }
    return NULL;
}

static
PBTHMINI_RFCOMM_SESSION
BthRfcommFindSession(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Sessions.Flink; Entry != &Dev->Sessions; Entry = Entry->Flink)
    {
        PBTHMINI_RFCOMM_SESSION Session = CONTAINING_RECORD(Entry, BTHMINI_RFCOMM_SESSION, Link);

        if (Session->Remote == Remote && !Session->Closing)
            return Session;
    }
    return NULL;
}

static
VOID
BthRfcommFreeSession(
    _In_ PBTHMINI_RFCOMM_SESSION Session)
{
    RemoveEntryList(&Session->Link);
    ExFreePoolWithTag(Session, BTHMINI_TAG);
}

static
VOID
BthRfcommCloseDlc(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ NTSTATUS Status)
{
    PBTHMINI_RFCOMM_SESSION Session = Dlc->Session;
    BOOLEAN WasOpen = Dlc->State == BthDlcOpen || Dlc->State == BthDlcWaitDisconnect;

    RemoveEntryList(&Dlc->Link);
    while (!IsListEmpty(&Dlc->TxQueue))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dlc->TxQueue), BTHMINI_DLC_BUFFER, Link), BTHMINI_TAG);
    if (WasOpen)
    {
        if (Dlc->Callbacks->Closed)
            Dlc->Callbacks->Closed(Dev, Dlc, Status);
    }
    else if (Dlc->Callbacks->Opened)
    {
        Dlc->Callbacks->Opened(Dev, Dlc, NT_SUCCESS(Status) ? STATUS_CONNECTION_ABORTED : Status);
    }
    ExFreePoolWithTag(Dlc, BTHMINI_TAG);

    if (IsListEmpty(&Session->Dlcs) && Session->Initiator && !Session->Closing)
    {
        Session->Closing = TRUE;
        if (Session->Channel && Session->Open)
        {
            BthRfcommSendControl(Dev, Session, 0, RFCOMM_DISC);
            Session->Deadline = BthRfcommNow() + RFCOMM_TIMEOUT_MS;
        }
        else if (Session->Channel)
        {
            BthL2capDisconnect(Dev, Session->Channel);
        }
    }
}

static
UCHAR
BthRfcommCreditsToGrant(
    _In_ PBTHMINI_DLC Dlc)
{
    ULONG Room, Grant;

    if (!Dlc->CreditFlow || Dlc->RxCredits >= RFCOMM_INITIAL_CREDITS)
        return 0;
    Grant = RFCOMM_INITIAL_CREDITS - Dlc->RxCredits;
    if (Dlc->Callbacks->Room)
    {
        Room = Dlc->Callbacks->Room(Dlc) / Dlc->FrameSize;
        Grant = Room > Dlc->RxCredits ? min(Grant, Room - Dlc->RxCredits) : 0;
    }
    return (UCHAR)Grant;
}

static
VOID
BthRfcommFlushTx(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc)
{
    while (Dlc->State == BthDlcOpen && !IsListEmpty(&Dlc->TxQueue) && (!Dlc->CreditFlow || Dlc->TxCredits))
    {
        PBTHMINI_DLC_BUFFER Buffer = CONTAINING_RECORD(RemoveHeadList(&Dlc->TxQueue), BTHMINI_DLC_BUFFER, Link);
        UCHAR Grant = BthRfcommCreditsToGrant(Dlc);

        Dlc->RxCredits += Grant;
        BthRfcommSendFrame(Dev, Dlc->Session, BthRfcommAddress(Dlc->Session->Initiator, Dlc->Dlci),
                           (UCHAR)(RFCOMM_UIH | (Grant ? RFCOMM_PF : 0)), Grant != 0, Grant, Buffer->Data, Buffer->Length);
        if (Dlc->CreditFlow)
            Dlc->TxCredits--;
        ExFreePoolWithTag(Buffer, BTHMINI_TAG);
    }
}

VOID
BthRfcommGrantCredits(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc)
{
    UCHAR Grant;

    if (Dlc->State != BthDlcOpen || !Dlc->CreditFlow || Dlc->RxCredits >= RFCOMM_INITIAL_CREDITS / 2)
        return;
    Grant = BthRfcommCreditsToGrant(Dlc);
    if (!Grant)
        return;
    Dlc->RxCredits += Grant;
    BthRfcommSendFrame(Dev, Dlc->Session, BthRfcommAddress(Dlc->Session->Initiator, Dlc->Dlci),
                       RFCOMM_UIH | RFCOMM_PF, TRUE, Grant, NULL, 0);
}

static
VOID
BthRfcommStartDlc(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc)
{
    Dlc->State = BthDlcWaitParameters;
    Dlc->Deadline = BthRfcommNow() + RFCOMM_TIMEOUT_MS;
    Dlc->FrameSize = (USHORT)min(RFCOMM_MAX_FRAME_SIZE, Dlc->Session->Channel->RemoteMtu - 5);
    BthRfcommSendParameters(Dev, Dlc, TRUE, RFCOMM_CFC_REQUEST);
}

static
VOID
BthRfcommMcc(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_RFCOMM_SESSION Session,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Type, ValueLength;
    BOOLEAN Command;
    const UCHAR *Value;
    PBTHMINI_DLC Dlc;

    if (Length < 2)
        return;
    Type = Data[0] >> 2;
    Command = (Data[0] & 0x02) != 0;
    ValueLength = Data[1] >> 1;
    Value = Data + 2;
    if (2 + (ULONG)ValueLength > Length)
        return;

    switch (Type)
    {
        case RFCOMM_MCC_PN:
            if (ValueLength < 8)
                break;
            Dlc = BthRfcommFindDlc(Session, Value[0] & 0x3F);
            if (!Command)
            {
                if (!Dlc || Dlc->State != BthDlcWaitParameters)
                    break;
                Dlc->CreditFlow = (Value[1] & 0xF0) == RFCOMM_CFC_ACCEPT;
                Dlc->FrameSize = (USHORT)min(Dlc->FrameSize, Value[4] | (Value[5] << 8));
                Dlc->TxCredits = Dlc->CreditFlow ? (Value[7] & 0x07) : 0;
                Dlc->RxCredits = Dlc->CreditFlow ? RFCOMM_INITIAL_CREDITS : 0;
                Dlc->State = BthDlcWaitUa;
                Dlc->Deadline = BthRfcommNow() + RFCOMM_TIMEOUT_MS;
                BthRfcommSendControl(Dev, Session, Dlc->Dlci, RFCOMM_SABM);
            }
            else if (Dlc)
            {
                Dlc->CreditFlow = (Value[1] & 0xF0) == RFCOMM_CFC_REQUEST;
                Dlc->FrameSize = (USHORT)min(Dlc->FrameSize, Value[4] | (Value[5] << 8));
                BthRfcommSendParameters(Dev, Dlc, FALSE, Dlc->CreditFlow ? RFCOMM_CFC_ACCEPT : 0);
            }
            else
            {
                BthRfcommSendControl(Dev, Session, Value[0] & 0x3F, RFCOMM_DM);
            }
            break;

        case RFCOMM_MCC_MSC:
            if (Command && ValueLength >= 2)
                BthRfcommSendMcc(Dev, Session, RFCOMM_MCC_MSC, FALSE, Value, ValueLength);
            break;

        case RFCOMM_MCC_RPN:
        case RFCOMM_MCC_RLS:
        case RFCOMM_MCC_TEST:
        case RFCOMM_MCC_FCON:
        case RFCOMM_MCC_FCOFF:
            if (Command)
                BthRfcommSendMcc(Dev, Session, Type, FALSE, Value, ValueLength);
            break;

        default:
            if (Command)
            {
                UCHAR Nsc = Data[0];

                BthRfcommSendMcc(Dev, Session, RFCOMM_MCC_NSC, FALSE, &Nsc, 1);
            }
            break;
    }
}

static
VOID
BthRfcommReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_RFCOMM_SESSION Session = Channel->Context;
    ULONG HeaderLength, InfoLength, Offset;
    UCHAR Dlci, Control, Credits = 0;
    PBTHMINI_DLC Dlc;
    PLIST_ENTRY Entry;

    if (!Session || Length < 4)
        return;
    Dlci = Data[0] >> 2;
    Control = Data[1];
    if (Data[2] & 1)
    {
        InfoLength = Data[2] >> 1;
        HeaderLength = 3;
    }
    else
    {
        InfoLength = (Data[2] >> 1) | (Data[3] << 7);
        HeaderLength = 4;
    }
    Offset = HeaderLength;
    if ((Control & ~RFCOMM_PF) == RFCOMM_UIH && (Control & RFCOMM_PF) && Dlci)
    {
        if (Offset >= Length)
            return;
        Credits = Data[Offset++];
    }
    if (Offset + InfoLength + 1 > Length)
        return;
    if (BthRfcommFcs(Data, (Control & ~RFCOMM_PF) == RFCOMM_UIH ? 2 : HeaderLength) != Data[Offset + InfoLength])
        return;

    Dlc = Dlci ? BthRfcommFindDlc(Session, Dlci) : NULL;
    switch (Control & ~RFCOMM_PF)
    {
        case RFCOMM_SABM:
            if (!Dlci)
            {
                Session->Open = TRUE;
                BthRfcommSendControl(Dev, Session, 0, RFCOMM_UA);
            }
            else
            {
                BthRfcommSendControl(Dev, Session, Dlci, RFCOMM_DM);
            }
            break;

        case RFCOMM_UA:
            if (!Dlci)
            {
                if (Session->Closing)
                {
                    BthL2capDisconnect(Dev, Session->Channel);
                    break;
                }
                Session->Open = TRUE;
                for (Entry = Session->Dlcs.Flink; Entry != &Session->Dlcs; Entry = Entry->Flink)
                {
                    Dlc = CONTAINING_RECORD(Entry, BTHMINI_DLC, Link);
                    if (Dlc->State == BthDlcWaitSession)
                        BthRfcommStartDlc(Dev, Dlc);
                }
            }
            else if (Dlc && Dlc->State == BthDlcWaitUa)
            {
                Dlc->State = BthDlcOpen;
                DPRINT1("BTHMINI: RFCOMM channel %u open, frame %u, credits %lu\n",
                        Dlci >> 1, Dlc->FrameSize, Dlc->TxCredits);
                BthRfcommSendModemStatus(Dev, Dlc, TRUE);
                if (Dlc->Callbacks->Opened)
                    Dlc->Callbacks->Opened(Dev, Dlc, STATUS_SUCCESS);
                BthRfcommFlushTx(Dev, Dlc);
            }
            else if (Dlc && Dlc->State == BthDlcWaitDisconnect)
            {
                BthRfcommCloseDlc(Dev, Dlc, STATUS_SUCCESS);
            }
            break;

        case RFCOMM_DM:
            if (!Dlci)
            {
                while (!IsListEmpty(&Session->Dlcs))
                    BthRfcommCloseDlc(Dev, CONTAINING_RECORD(Session->Dlcs.Flink, BTHMINI_DLC, Link), STATUS_CONNECTION_REFUSED);
            }
            else if (Dlc)
            {
                DPRINT1("BTHMINI: RFCOMM channel %u refused\n", Dlci >> 1);
                BthRfcommCloseDlc(Dev, Dlc, STATUS_CONNECTION_REFUSED);
            }
            break;

        case RFCOMM_DISC:
            BthRfcommSendControl(Dev, Session, Dlci, RFCOMM_UA);
            if (!Dlci)
            {
                while (!IsListEmpty(&Session->Dlcs))
                    BthRfcommCloseDlc(Dev, CONTAINING_RECORD(Session->Dlcs.Flink, BTHMINI_DLC, Link), STATUS_REMOTE_DISCONNECT);
            }
            else if (Dlc)
            {
                BthRfcommCloseDlc(Dev, Dlc, STATUS_REMOTE_DISCONNECT);
            }
            break;

        case RFCOMM_UIH:
            if (!Dlci)
            {
                BthRfcommMcc(Dev, Session, Data + Offset, InfoLength);
                break;
            }
            if (!Dlc || Dlc->State != BthDlcOpen)
                break;
            if (Credits)
            {
                Dlc->TxCredits += Credits;
                BthRfcommFlushTx(Dev, Dlc);
            }
            if (InfoLength)
            {
                if (Dlc->CreditFlow && Dlc->RxCredits)
                    Dlc->RxCredits--;
                if (Dlc->Callbacks->Receive)
                    Dlc->Callbacks->Receive(Dev, Dlc, Data + Offset, InfoLength);
                BthRfcommGrantCredits(Dev, Dlc);
            }
            break;

        default:
            break;
    }
}

static
VOID
BthRfcommOpened(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_RFCOMM_SESSION Session = Channel->Context;

    if (!NT_SUCCESS(Status))
    {
        Session->Channel = NULL;
        Session->Closing = TRUE;
        while (!IsListEmpty(&Session->Dlcs))
            BthRfcommCloseDlc(Dev, CONTAINING_RECORD(Session->Dlcs.Flink, BTHMINI_DLC, Link), Status);
        BthRfcommFreeSession(Session);
        return;
    }
    Session->Deadline = BthRfcommNow() + RFCOMM_TIMEOUT_MS;
    BthRfcommSendControl(Dev, Session, 0, RFCOMM_SABM);
}

static
VOID
BthRfcommClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_RFCOMM_SESSION Session = Channel->Context;

    if (!Session)
        return;
    Session->Channel = NULL;
    Session->Closing = TRUE;
    while (!IsListEmpty(&Session->Dlcs))
        BthRfcommCloseDlc(Dev, CONTAINING_RECORD(Session->Dlcs.Flink, BTHMINI_DLC, Link), Status);
    BthRfcommFreeSession(Session);
}

static
VOID
BthRfcommServerOpened(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_RFCOMM_SESSION Session;

    if (!NT_SUCCESS(Status))
        return;
    Session = ExAllocatePoolZero(NonPagedPool, sizeof(*Session), BTHMINI_TAG);
    if (!Session)
    {
        BthL2capDisconnect(Dev, Channel);
        return;
    }
    Session->Remote = Channel->Remote;
    Session->Channel = Channel;
    InitializeListHead(&Session->Dlcs);
    InsertTailList(&Dev->Sessions, &Session->Link);
    Channel->Context = Session;
}

static const BTHMINI_CHANNEL_CALLBACKS BthRfcommClientCallbacks =
{
    BthRfcommOpened,
    BthRfcommReceive,
    BthRfcommClosed
};

static const BTHMINI_CHANNEL_CALLBACKS BthRfcommServer =
{
    BthRfcommServerOpened,
    BthRfcommReceive,
    BthRfcommClosed
};

const BTHMINI_CHANNEL_CALLBACKS *
BthRfcommServerCallbacks(VOID)
{
    return &BthRfcommServer;
}

NTSTATUS
BthRfcommConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR ServerChannel,
    _In_ const BTHMINI_DLC_CALLBACKS *Callbacks,
    _In_opt_ PVOID Context,
    _Out_opt_ PBTHMINI_DLC *Result)
{
    PBTHMINI_RFCOMM_SESSION Session = BthRfcommFindSession(Dev, Remote);
    PBTHMINI_DLC Dlc;
    NTSTATUS Status;

    if (Result)
        *Result = NULL;
    if (!ServerChannel || ServerChannel > 30)
        return STATUS_INVALID_PARAMETER;
    if (!Session)
    {
        Session = ExAllocatePoolZero(NonPagedPool, sizeof(*Session), BTHMINI_TAG);
        if (!Session)
            return STATUS_INSUFFICIENT_RESOURCES;
        Session->Remote = Remote;
        Session->Initiator = TRUE;
        InitializeListHead(&Session->Dlcs);
        InsertTailList(&Dev->Sessions, &Session->Link);
        Status = BthL2capConnect(Dev, Remote, L2CAP_PSM_RFCOMM, &BthRfcommClientCallbacks, Session, &Session->Channel);
        if (!NT_SUCCESS(Status))
        {
            BthRfcommFreeSession(Session);
            return Status;
        }
    }

    Dlc = ExAllocatePoolZero(NonPagedPool, sizeof(*Dlc), BTHMINI_TAG);
    if (!Dlc)
        return STATUS_INSUFFICIENT_RESOURCES;
    Dlc->Session = Session;
    Dlc->Callbacks = Callbacks;
    Dlc->Context = Context;
    Dlc->Dlci = (UCHAR)((ServerChannel << 1) | (Session->Initiator ? 0 : 1));
    Dlc->State = BthDlcWaitSession;
    Dlc->Deadline = BthRfcommNow() + RFCOMM_TIMEOUT_MS + 30000;
    InitializeListHead(&Dlc->TxQueue);
    InsertTailList(&Session->Dlcs, &Dlc->Link);
    if (Session->Open)
        BthRfcommStartDlc(Dev, Dlc);
    if (Result)
        *Result = Dlc;
    return STATUS_PENDING;
}

NTSTATUS
BthRfcommSend(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    if (Dlc->State != BthDlcOpen)
        return STATUS_INVALID_DEVICE_STATE;
    while (Offset < Length)
    {
        ULONG Chunk = min(Length - Offset, Dlc->FrameSize);
        PBTHMINI_DLC_BUFFER Buffer;

        Buffer = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(BTHMINI_DLC_BUFFER, Data[Chunk]), BTHMINI_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        Buffer->Length = Chunk;
        RtlCopyMemory(Buffer->Data, Data + Offset, Chunk);
        InsertTailList(&Dlc->TxQueue, &Buffer->Link);
        Offset += Chunk;
    }
    BthRfcommFlushTx(Dev, Dlc);
    return STATUS_SUCCESS;
}

VOID
BthRfcommDisconnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc)
{
    if (Dlc->State == BthDlcOpen)
    {
        Dlc->State = BthDlcWaitDisconnect;
        Dlc->Deadline = BthRfcommNow() + RFCOMM_TIMEOUT_MS;
        BthRfcommSendControl(Dev, Dlc->Session, Dlc->Dlci, RFCOMM_DISC);
    }
    else if (Dlc->State != BthDlcWaitDisconnect)
    {
        BthRfcommCloseDlc(Dev, Dlc, STATUS_CANCELLED);
    }
}

VOID
BthRfcommTick(
    _In_ PBTHMINI_DEVICE Dev)
{
    ULONGLONG Now = BthRfcommNow();
    PLIST_ENTRY SessionEntry = Dev->Sessions.Flink;

    while (SessionEntry != &Dev->Sessions)
    {
        PBTHMINI_RFCOMM_SESSION Session = CONTAINING_RECORD(SessionEntry, BTHMINI_RFCOMM_SESSION, Link);
        PLIST_ENTRY Entry = Session->Dlcs.Flink;

        SessionEntry = SessionEntry->Flink;
        while (Entry != &Session->Dlcs)
        {
            PBTHMINI_DLC Dlc = CONTAINING_RECORD(Entry, BTHMINI_DLC, Link);

            Entry = Entry->Flink;
            if (Dlc->State != BthDlcOpen && Now >= Dlc->Deadline)
            {
                DPRINT1("BTHMINI: RFCOMM DLCI %u timed out in state %u\n", Dlc->Dlci, Dlc->State);
                BthRfcommCloseDlc(Dev, Dlc, STATUS_IO_TIMEOUT);
            }
        }
        if (Session->Closing && Session->Channel && Session->Deadline && Now >= Session->Deadline)
        {
            Session->Deadline = 0;
            BthL2capDisconnect(Dev, Session->Channel);
        }
    }
}
