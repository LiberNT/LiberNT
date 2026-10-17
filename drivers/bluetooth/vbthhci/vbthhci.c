/*
 * PROJECT:     LiberNT Virtual Bluetooth Controller
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software-only Bluetooth HCI controller with simulated peers for testing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <bthxddi.h>

#define NDEBUG
#include <debug.h>

#define VBTH_TAG                        'hTBV'
#define VBTH_MAX_ACL                    1021
#define VBTH_ACL_PACKETS                8
#define VBTH_MAX_QUEUED                 128
#define VBTH_MAX_CONNECTIONS            4
#define VBTH_PDO_COMPATIBLE_ID          L"MS_BTHX_BTHMINI"

#define HCI_OP(Ogf, Ocf)                ((USHORT)(((Ogf) << 10) | (Ocf)))
#define HCI_INQUIRY                     HCI_OP(0x01, 0x0001)
#define HCI_INQUIRY_CANCEL              HCI_OP(0x01, 0x0002)
#define HCI_CREATE_CONNECTION           HCI_OP(0x01, 0x0005)
#define HCI_DISCONNECT                  HCI_OP(0x01, 0x0006)
#define HCI_ACCEPT_CONNECTION_REQUEST   HCI_OP(0x01, 0x0009)
#define HCI_REJECT_CONNECTION_REQUEST   HCI_OP(0x01, 0x000A)
#define HCI_LINK_KEY_REQUEST_REPLY      HCI_OP(0x01, 0x000B)
#define HCI_LINK_KEY_REQUEST_NEG_REPLY  HCI_OP(0x01, 0x000C)
#define HCI_PIN_CODE_REQUEST_NEG_REPLY  HCI_OP(0x01, 0x000E)
#define HCI_AUTHENTICATION_REQUESTED    HCI_OP(0x01, 0x0011)
#define HCI_SET_CONNECTION_ENCRYPTION   HCI_OP(0x01, 0x0013)
#define HCI_REMOTE_NAME_REQUEST         HCI_OP(0x01, 0x0019)
#define HCI_READ_REMOTE_FEATURES        HCI_OP(0x01, 0x001B)
#define HCI_READ_REMOTE_VERSION         HCI_OP(0x01, 0x001D)
#define HCI_IO_CAPABILITY_REPLY         HCI_OP(0x01, 0x002B)
#define HCI_USER_CONFIRMATION_REPLY     HCI_OP(0x01, 0x002C)
#define HCI_USER_CONFIRMATION_NEG_REPLY HCI_OP(0x01, 0x002D)
#define HCI_USER_PASSKEY_NEG_REPLY      HCI_OP(0x01, 0x002F)
#define HCI_IO_CAPABILITY_NEG_REPLY     HCI_OP(0x01, 0x0034)
#define HCI_RESET                       HCI_OP(0x03, 0x0003)
#define HCI_WRITE_INQUIRY_MODE          HCI_OP(0x03, 0x0045)
#define HCI_READ_LOCAL_VERSION          HCI_OP(0x04, 0x0001)
#define HCI_READ_LOCAL_COMMANDS         HCI_OP(0x04, 0x0002)
#define HCI_READ_LOCAL_FEATURES         HCI_OP(0x04, 0x0003)
#define HCI_READ_LOCAL_EXT_FEATURES     HCI_OP(0x04, 0x0004)
#define HCI_READ_BUFFER_SIZE            HCI_OP(0x04, 0x0005)
#define HCI_READ_BD_ADDR                HCI_OP(0x04, 0x0009)

#define HCI_EV_INQUIRY_COMPLETE         0x01
#define HCI_EV_CONNECTION_COMPLETE      0x03
#define HCI_EV_DISCONNECTION_COMPLETE   0x05
#define HCI_EV_AUTHENTICATION_COMPLETE  0x06
#define HCI_EV_REMOTE_NAME_COMPLETE     0x07
#define HCI_EV_ENCRYPTION_CHANGE        0x08
#define HCI_EV_REMOTE_FEATURES_COMPLETE 0x0B
#define HCI_EV_REMOTE_VERSION_COMPLETE  0x0C
#define HCI_EV_COMMAND_COMPLETE         0x0E
#define HCI_EV_COMMAND_STATUS           0x0F
#define HCI_EV_NUMBER_OF_COMPLETED      0x13
#define HCI_EV_LINK_KEY_REQUEST         0x17
#define HCI_EV_LINK_KEY_NOTIFICATION    0x18
#define HCI_EV_INQUIRY_RESULT_RSSI      0x22
#define HCI_EV_EXTENDED_INQUIRY_RESULT  0x2F
#define HCI_EV_IO_CAPABILITY_REQUEST    0x31
#define HCI_EV_IO_CAPABILITY_RESPONSE   0x32
#define HCI_EV_USER_CONFIRMATION        0x33
#define HCI_EV_SIMPLE_PAIRING_COMPLETE  0x36

#define HCI_SUCCESS                     0x00
#define HCI_UNKNOWN_CONNECTION          0x02
#define HCI_PAGE_TIMEOUT                0x04
#define HCI_AUTHENTICATION_FAILURE      0x05
#define HCI_LOCAL_HOST_TERMINATED       0x16
#define HCI_PAIRING_NOT_ALLOWED         0x18

#define HCI_IO_DISPLAY_YES_NO           0x01
#define HCI_IO_NO_INPUT_NO_OUTPUT       0x03

#define L2CAP_SIGNALING_CID             0x0001
#define L2CAP_COMMAND_REJECT            0x01
#define L2CAP_CONNECTION_REQUEST        0x02
#define L2CAP_CONNECTION_RESPONSE       0x03
#define L2CAP_DISCONNECTION_REQUEST     0x06
#define L2CAP_DISCONNECTION_RESPONSE    0x07
#define L2CAP_INFORMATION_REQUEST       0x0A
#define L2CAP_INFORMATION_RESPONSE      0x0B
#define L2CAP_PSM_NOT_SUPPORTED         0x0002

#define VBTH_TAG_NONE                   0
#define VBTH_TAG_INQUIRY                1

typedef enum _VBTH_KIND
{
    VBthKindEvent,
    VBthKindAcl,
    VBthKinds
} VBTH_KIND;

typedef struct _VBTH_PEER
{
    UCHAR Address[6];
    ULONG ClassOfDevice;
    UCHAR IoCapability;
    CHAR Rssi;
    const char *Name;
} VBTH_PEER;

static const VBTH_PEER VBthPeers[] =
{
    { { 0x01, 0x00, 0x5A, 0x42, 0x54, 0x02 }, 0x240404, HCI_IO_NO_INPUT_NO_OUTPUT, -45, "LiberNT Headphones" },
    { { 0x02, 0x00, 0x5A, 0x42, 0x54, 0x02 }, 0x5A020C, HCI_IO_DISPLAY_YES_NO, -60, "LiberNT Phone" },
};

static const UCHAR VBthLocalAddress[6] = { 0x00, 0x00, 0x5A, 0x42, 0x54, 0x02 };

static const UCHAR VBthFeatures[8] = { 0xFF, 0x00, 0x00, 0x40, 0x00, 0x00, 0x09, 0x00 };

typedef struct _VBTH_PACKET
{
    LIST_ENTRY Link;
    ULONGLONG Due;
    VBTH_KIND Kind;
    ULONG Tag;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} VBTH_PACKET, *PVBTH_PACKET;

typedef struct _VBTH_CONNECTION
{
    BOOLEAN Used;
    USHORT Handle;
    ULONG Peer;
    UCHAR HostIoCapability;
} VBTH_CONNECTION, *PVBTH_CONNECTION;

typedef struct _VBTH_DEVICE
{
    WDFDEVICE Device;
    WDFDEVICE Child;
    WDFTIMER Timer;
    KSPIN_LOCK Lock;
    LIST_ENTRY Pending;
    LIST_ENTRY Ready[VBthKinds];
    ULONG ReadyCount[VBthKinds];
    WDFQUEUE Queue[VBthKinds];
    BOOLEAN Delivering[VBthKinds];
    BOOLEAN Running;
    UCHAR InquiryMode;
    USHORT NextHandle;
    VBTH_CONNECTION Connections[VBTH_MAX_CONNECTIONS];
} VBTH_DEVICE, *PVBTH_DEVICE;

typedef struct _VBTH_PDO
{
    PVBTH_DEVICE Parent;
} VBTH_PDO, *PVBTH_PDO;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VBTH_DEVICE, VBthGetDevice)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VBTH_PDO, VBthGetPdo)

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD VBthEvtDeviceAdd;
static EVT_WDF_DEVICE_D0_ENTRY VBthEvtD0Entry;
static EVT_WDF_DEVICE_D0_EXIT VBthEvtD0Exit;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VBthEvtPdoIoctl;
static EVT_WDF_TIMER VBthEvtTimer;

static
ULONGLONG
VBthNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
VBthDeliver(
    _In_ PVBTH_DEVICE Dev,
    _In_ VBTH_KIND Kind)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    if (Dev->Delivering[Kind])
    {
        KeReleaseSpinLock(&Dev->Lock, OldIrql);
        return;
    }
    Dev->Delivering[Kind] = TRUE;
    for (;;)
    {
        PBTHX_HCI_READ_WRITE_CONTEXT Context;
        PVBTH_PACKET Packet;
        WDFREQUEST Request;
        WDFMEMORY Memory;
        size_t Size;

        if (!Dev->Running || !Dev->Queue[Kind] || IsListEmpty(&Dev->Ready[Kind]) ||
            !NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Dev->Queue[Kind], &Request)))
        {
            Dev->Delivering[Kind] = FALSE;
            KeReleaseSpinLock(&Dev->Lock, OldIrql);
            return;
        }
        Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->Ready[Kind]), VBTH_PACKET, Link);
        Dev->ReadyCount[Kind]--;
        KeReleaseSpinLock(&Dev->Lock, OldIrql);

        if (NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &Memory)))
        {
            Context = WdfMemoryGetBuffer(Memory, &Size);
            if (Size >= FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length)
            {
                Context->DataLen = Packet->Length;
                Context->Type = Kind == VBthKindEvent ? HciPacketEvent : HciPacketAclData;
                RtlCopyMemory(Context->Data, Packet->Data, Packet->Length);
                WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                                  FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length);
            }
            else
            {
                WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
            }
        }
        else
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        }
        ExFreePoolWithTag(Packet, VBTH_TAG);
        KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    }
}

static
VOID
VBthPump(
    _In_ PVBTH_DEVICE Dev)
{
    ULONGLONG Now = VBthNowMs(), Next = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    while (!IsListEmpty(&Dev->Pending))
    {
        PVBTH_PACKET Packet = CONTAINING_RECORD(Dev->Pending.Flink, VBTH_PACKET, Link);

        if (Packet->Due > Now)
        {
            Next = Packet->Due - Now;
            break;
        }
        RemoveEntryList(&Packet->Link);
        if (Dev->ReadyCount[Packet->Kind] >= VBTH_MAX_QUEUED)
        {
            ExFreePoolWithTag(Packet, VBTH_TAG);
            continue;
        }
        InsertTailList(&Dev->Ready[Packet->Kind], &Packet->Link);
        Dev->ReadyCount[Packet->Kind]++;
    }
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
    if (Next && Dev->Timer)
        WdfTimerStart(Dev->Timer, WDF_REL_TIMEOUT_IN_MS(Next));
    VBthDeliver(Dev, VBthKindEvent);
    VBthDeliver(Dev, VBthKindAcl);
}

static
VOID
VBthSchedule(
    _In_ PVBTH_DEVICE Dev,
    _In_ VBTH_KIND Kind,
    _In_ ULONG DelayMs,
    _In_ ULONG Tag,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PVBTH_PACKET Packet;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(VBTH_PACKET, Data) + Length, VBTH_TAG);
    if (!Packet)
        return;
    Packet->Due = VBthNowMs() + DelayMs;
    Packet->Kind = Kind;
    Packet->Tag = Tag;
    Packet->Length = Length;
    RtlCopyMemory(Packet->Data, Data, Length);

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    for (Entry = Dev->Pending.Blink; Entry != &Dev->Pending; Entry = Entry->Blink)
    {
        if (CONTAINING_RECORD(Entry, VBTH_PACKET, Link)->Due <= Packet->Due)
            break;
    }
    InsertHeadList(Entry, &Packet->Link);
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
}

static
VOID
VBthEvent(
    _In_ PVBTH_DEVICE Dev,
    _In_ ULONG DelayMs,
    _In_ ULONG Tag,
    _In_ UCHAR Code,
    _In_reads_bytes_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length)
{
    UCHAR Event[2 + 255];

    Event[0] = Code;
    Event[1] = Length;
    if (Length)
        RtlCopyMemory(Event + 2, Parameters, Length);
    VBthSchedule(Dev, VBthKindEvent, DelayMs, Tag, Event, 2 + Length);
}

static
VOID
VBthCommandComplete(
    _In_ PVBTH_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_(Length) const UCHAR *Return,
    _In_ UCHAR Length)
{
    UCHAR Parameters[255];

    Parameters[0] = 1;
    Parameters[1] = (UCHAR)Opcode;
    Parameters[2] = (UCHAR)(Opcode >> 8);
    RtlCopyMemory(Parameters + 3, Return, Length);
    VBthEvent(Dev, 0, VBTH_TAG_NONE, HCI_EV_COMMAND_COMPLETE, Parameters, (UCHAR)(3 + Length));
}

static
VOID
VBthCommandStatus(
    _In_ PVBTH_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_ UCHAR Status)
{
    UCHAR Parameters[4];

    Parameters[0] = Status;
    Parameters[1] = 1;
    Parameters[2] = (UCHAR)Opcode;
    Parameters[3] = (UCHAR)(Opcode >> 8);
    VBthEvent(Dev, 0, VBTH_TAG_NONE, HCI_EV_COMMAND_STATUS, Parameters, sizeof(Parameters));
}

static
VOID
VBthCompleteAddress(
    _In_ PVBTH_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_(6) const UCHAR *Address)
{
    UCHAR Return[7];

    Return[0] = HCI_SUCCESS;
    RtlCopyMemory(Return + 1, Address, 6);
    VBthCommandComplete(Dev, Opcode, Return, sizeof(Return));
}

static
LONG
VBthFindPeer(
    _In_reads_bytes_(6) const UCHAR *Address)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(VBthPeers); Index++)
    {
        if (RtlEqualMemory(VBthPeers[Index].Address, Address, 6))
            return (LONG)Index;
    }
    return -1;
}

static
PVBTH_CONNECTION
VBthFindHandle(
    _In_ PVBTH_DEVICE Dev,
    _In_ USHORT Handle)
{
    ULONG Index;

    for (Index = 0; Index < VBTH_MAX_CONNECTIONS; Index++)
    {
        if (Dev->Connections[Index].Used && Dev->Connections[Index].Handle == Handle)
            return &Dev->Connections[Index];
    }
    return NULL;
}

static
PVBTH_CONNECTION
VBthFindPeerConnection(
    _In_ PVBTH_DEVICE Dev,
    _In_ ULONG Peer)
{
    ULONG Index;

    for (Index = 0; Index < VBTH_MAX_CONNECTIONS; Index++)
    {
        if (Dev->Connections[Index].Used && Dev->Connections[Index].Peer == Peer)
            return &Dev->Connections[Index];
    }
    return NULL;
}

static
VOID
VBthHandleEvent(
    _In_ PVBTH_DEVICE Dev,
    _In_ ULONG DelayMs,
    _In_ UCHAR Code,
    _In_ USHORT Handle,
    _In_ UCHAR Status)
{
    UCHAR Parameters[3];

    Parameters[0] = Status;
    Parameters[1] = (UCHAR)Handle;
    Parameters[2] = (UCHAR)(Handle >> 8);
    VBthEvent(Dev, DelayMs, VBTH_TAG_NONE, Code, Parameters, sizeof(Parameters));
}

static
VOID
VBthInquiry(
    _In_ PVBTH_DEVICE Dev)
{
    UCHAR Parameters[255];
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(VBthPeers); Index++)
    {
        const VBTH_PEER *Peer = &VBthPeers[Index];
        ULONG NameLength = (ULONG)strlen(Peer->Name);

        RtlZeroMemory(Parameters, sizeof(Parameters));
        Parameters[0] = 1;
        RtlCopyMemory(Parameters + 1, Peer->Address, 6);
        Parameters[7] = 0x01;
        Parameters[8] = 0x00;
        if (Dev->InquiryMode == 2)
        {
            Parameters[9] = (UCHAR)Peer->ClassOfDevice;
            Parameters[10] = (UCHAR)(Peer->ClassOfDevice >> 8);
            Parameters[11] = (UCHAR)(Peer->ClassOfDevice >> 16);
            Parameters[12] = 0x00;
            Parameters[13] = 0x00;
            Parameters[14] = (UCHAR)Peer->Rssi;
            Parameters[15] = (UCHAR)(NameLength + 1);
            Parameters[16] = 0x09;
            RtlCopyMemory(Parameters + 17, Peer->Name, NameLength);
            VBthEvent(Dev, 300 + Index * 200, VBTH_TAG_INQUIRY, HCI_EV_EXTENDED_INQUIRY_RESULT, Parameters, 255);
        }
        else
        {
            Parameters[8] = (UCHAR)Peer->ClassOfDevice;
            Parameters[9] = (UCHAR)(Peer->ClassOfDevice >> 8);
            Parameters[10] = (UCHAR)(Peer->ClassOfDevice >> 16);
            Parameters[11] = 0x00;
            Parameters[12] = 0x00;
            Parameters[13] = (UCHAR)Peer->Rssi;
            VBthEvent(Dev, 300 + Index * 200, VBTH_TAG_INQUIRY, HCI_EV_INQUIRY_RESULT_RSSI, Parameters, 15);
        }
    }
    Parameters[0] = HCI_SUCCESS;
    VBthEvent(Dev, 1500, VBTH_TAG_INQUIRY, HCI_EV_INQUIRY_COMPLETE, Parameters, 1);
}

static
VOID
VBthCancelTagged(
    _In_ PVBTH_DEVICE Dev,
    _In_ ULONG Tag)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    for (Entry = Dev->Pending.Flink; Entry != &Dev->Pending;)
    {
        PVBTH_PACKET Packet = CONTAINING_RECORD(Entry, VBTH_PACKET, Link);

        Entry = Entry->Flink;
        if (Tag == VBTH_TAG_NONE || Packet->Tag == Tag)
        {
            RemoveEntryList(&Packet->Link);
            ExFreePoolWithTag(Packet, VBTH_TAG);
        }
    }
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
}

static
VOID
VBthPairingComplete(
    _In_ PVBTH_DEVICE Dev,
    _In_ PVBTH_CONNECTION Connection,
    _In_ BOOLEAN Accepted)
{
    const VBTH_PEER *Peer = &VBthPeers[Connection->Peer];
    UCHAR Parameters[23];
    ULONG Index;

    Parameters[0] = Accepted ? HCI_SUCCESS : HCI_AUTHENTICATION_FAILURE;
    RtlCopyMemory(Parameters + 1, Peer->Address, 6);
    VBthEvent(Dev, 20, VBTH_TAG_NONE, HCI_EV_SIMPLE_PAIRING_COMPLETE, Parameters, 7);
    if (Accepted)
    {
        RtlCopyMemory(Parameters, Peer->Address, 6);
        for (Index = 0; Index < 16; Index++)
            Parameters[6 + Index] = (UCHAR)(0xA5 ^ (Index * 17) ^ Peer->Address[0]);
        Parameters[22] = (Peer->IoCapability == HCI_IO_DISPLAY_YES_NO &&
                          Connection->HostIoCapability == HCI_IO_DISPLAY_YES_NO) ? 0x05 : 0x04;
        VBthEvent(Dev, 30, VBTH_TAG_NONE, HCI_EV_LINK_KEY_NOTIFICATION, Parameters, 23);
    }
    VBthHandleEvent(Dev, 40, HCI_EV_AUTHENTICATION_COMPLETE, Connection->Handle,
                    Accepted ? HCI_SUCCESS : HCI_AUTHENTICATION_FAILURE);
}

static
VOID
VBthCommand(
    _In_ PVBTH_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Return[255], Parameters[255];
    PVBTH_CONNECTION Connection;
    const UCHAR *In;
    USHORT Opcode, Handle;
    ULONG Index;
    LONG Peer;

    if (Length < 3 || Length < 3u + Data[2])
        return;
    Opcode = (USHORT)(Data[0] | (Data[1] << 8));
    In = Data + 3;
    RtlZeroMemory(Return, sizeof(Return));

    switch (Opcode)
    {
        case HCI_RESET:
            VBthCancelTagged(Dev, VBTH_TAG_NONE);
            RtlZeroMemory(Dev->Connections, sizeof(Dev->Connections));
            Dev->InquiryMode = 0;
            VBthCommandComplete(Dev, Opcode, Return, 1);
            break;

        case HCI_READ_LOCAL_VERSION:
            Return[1] = 0x09;
            Return[4] = 0x09;
            Return[5] = 0xFF;
            Return[6] = 0xFF;
            VBthCommandComplete(Dev, Opcode, Return, 9);
            break;

        case HCI_READ_LOCAL_COMMANDS:
            RtlFillMemory(Return + 1, 64, 0xFF);
            VBthCommandComplete(Dev, Opcode, Return, 65);
            break;

        case HCI_READ_LOCAL_FEATURES:
            RtlCopyMemory(Return + 1, VBthFeatures, sizeof(VBthFeatures));
            VBthCommandComplete(Dev, Opcode, Return, 9);
            break;

        case HCI_READ_LOCAL_EXT_FEATURES:
            Return[1] = Data[2] ? In[0] : 0;
            VBthCommandComplete(Dev, Opcode, Return, 11);
            break;

        case HCI_READ_BUFFER_SIZE:
            Return[1] = (UCHAR)VBTH_MAX_ACL;
            Return[2] = (UCHAR)(VBTH_MAX_ACL >> 8);
            Return[4] = VBTH_ACL_PACKETS;
            VBthCommandComplete(Dev, Opcode, Return, 8);
            break;

        case HCI_READ_BD_ADDR:
            RtlCopyMemory(Return + 1, VBthLocalAddress, 6);
            VBthCommandComplete(Dev, Opcode, Return, 7);
            break;

        case HCI_WRITE_INQUIRY_MODE:
            Dev->InquiryMode = Data[2] ? In[0] : 0;
            VBthCommandComplete(Dev, Opcode, Return, 1);
            break;

        case HCI_INQUIRY:
            VBthCommandStatus(Dev, Opcode, HCI_SUCCESS);
            VBthInquiry(Dev);
            break;

        case HCI_INQUIRY_CANCEL:
            VBthCancelTagged(Dev, VBTH_TAG_INQUIRY);
            VBthCommandComplete(Dev, Opcode, Return, 1);
            break;

        case HCI_CREATE_CONNECTION:
            if (Data[2] < 6)
                break;
            VBthCommandStatus(Dev, Opcode, HCI_SUCCESS);
            Peer = VBthFindPeer(In);
            Connection = NULL;
            for (Index = 0; Peer >= 0 && Index < VBTH_MAX_CONNECTIONS && !Connection; Index++)
            {
                if (!Dev->Connections[Index].Used)
                    Connection = &Dev->Connections[Index];
            }
            RtlZeroMemory(Parameters, 11);
            RtlCopyMemory(Parameters + 3, In, 6);
            Parameters[9] = 0x01;
            if (!Connection)
            {
                Parameters[0] = HCI_PAGE_TIMEOUT;
            }
            else
            {
                Connection->Used = TRUE;
                Connection->Peer = (ULONG)Peer;
                Connection->Handle = Dev->NextHandle++ & 0x0FFF;
                Connection->HostIoCapability = 0;
                Parameters[1] = (UCHAR)Connection->Handle;
                Parameters[2] = (UCHAR)(Connection->Handle >> 8);
            }
            VBthEvent(Dev, 100, VBTH_TAG_NONE, HCI_EV_CONNECTION_COMPLETE, Parameters, 11);
            break;

        case HCI_DISCONNECT:
            if (Data[2] < 2)
                break;
            Handle = (USHORT)((In[0] | (In[1] << 8)) & 0x0FFF);
            Connection = VBthFindHandle(Dev, Handle);
            VBthCommandStatus(Dev, Opcode, Connection ? HCI_SUCCESS : HCI_UNKNOWN_CONNECTION);
            if (Connection)
            {
                Connection->Used = FALSE;
                Parameters[0] = HCI_SUCCESS;
                Parameters[1] = (UCHAR)Handle;
                Parameters[2] = (UCHAR)(Handle >> 8);
                Parameters[3] = HCI_LOCAL_HOST_TERMINATED;
                VBthEvent(Dev, 50, VBTH_TAG_NONE, HCI_EV_DISCONNECTION_COMPLETE, Parameters, 4);
            }
            break;

        case HCI_ACCEPT_CONNECTION_REQUEST:
        case HCI_REJECT_CONNECTION_REQUEST:
            VBthCommandStatus(Dev, Opcode, HCI_SUCCESS);
            break;

        case HCI_AUTHENTICATION_REQUESTED:
            if (Data[2] < 2)
                break;
            Connection = VBthFindHandle(Dev, (USHORT)((In[0] | (In[1] << 8)) & 0x0FFF));
            VBthCommandStatus(Dev, Opcode, Connection ? HCI_SUCCESS : HCI_UNKNOWN_CONNECTION);
            if (Connection)
                VBthEvent(Dev, 20, VBTH_TAG_NONE, HCI_EV_LINK_KEY_REQUEST, VBthPeers[Connection->Peer].Address, 6);
            break;

        case HCI_LINK_KEY_REQUEST_REPLY:
        case HCI_LINK_KEY_REQUEST_NEG_REPLY:
            if (Data[2] < 6)
                break;
            VBthCompleteAddress(Dev, Opcode, In);
            Peer = VBthFindPeer(In);
            Connection = Peer >= 0 ? VBthFindPeerConnection(Dev, (ULONG)Peer) : NULL;
            if (!Connection)
                break;
            if (Opcode == HCI_LINK_KEY_REQUEST_REPLY)
                VBthHandleEvent(Dev, 20, HCI_EV_AUTHENTICATION_COMPLETE, Connection->Handle, HCI_SUCCESS);
            else
                VBthEvent(Dev, 20, VBTH_TAG_NONE, HCI_EV_IO_CAPABILITY_REQUEST, In, 6);
            break;

        case HCI_PIN_CODE_REQUEST_NEG_REPLY:
            if (Data[2] >= 6)
                VBthCompleteAddress(Dev, Opcode, In);
            break;

        case HCI_IO_CAPABILITY_REPLY:
            if (Data[2] < 9)
                break;
            VBthCompleteAddress(Dev, Opcode, In);
            Peer = VBthFindPeer(In);
            Connection = Peer >= 0 ? VBthFindPeerConnection(Dev, (ULONG)Peer) : NULL;
            if (!Connection)
                break;
            Connection->HostIoCapability = In[6];
            RtlCopyMemory(Parameters, In, 6);
            Parameters[6] = VBthPeers[Peer].IoCapability;
            Parameters[7] = 0x00;
            Parameters[8] = VBthPeers[Peer].IoCapability == HCI_IO_DISPLAY_YES_NO ? 0x03 : 0x02;
            VBthEvent(Dev, 20, VBTH_TAG_NONE, HCI_EV_IO_CAPABILITY_RESPONSE, Parameters, 9);
            RtlCopyMemory(Parameters, In, 6);
            Parameters[6] = 0x40;
            Parameters[7] = 0xE2;
            Parameters[8] = 0x01;
            Parameters[9] = 0x00;
            VBthEvent(Dev, 40, VBTH_TAG_NONE, HCI_EV_USER_CONFIRMATION, Parameters, 10);
            break;

        case HCI_USER_CONFIRMATION_REPLY:
        case HCI_USER_CONFIRMATION_NEG_REPLY:
        case HCI_USER_PASSKEY_NEG_REPLY:
            if (Data[2] < 6)
                break;
            VBthCompleteAddress(Dev, Opcode, In);
            Peer = VBthFindPeer(In);
            Connection = Peer >= 0 ? VBthFindPeerConnection(Dev, (ULONG)Peer) : NULL;
            if (Connection)
                VBthPairingComplete(Dev, Connection, Opcode == HCI_USER_CONFIRMATION_REPLY);
            break;

        case HCI_IO_CAPABILITY_NEG_REPLY:
            if (Data[2] < 6)
                break;
            VBthCompleteAddress(Dev, Opcode, In);
            Peer = VBthFindPeer(In);
            Connection = Peer >= 0 ? VBthFindPeerConnection(Dev, (ULONG)Peer) : NULL;
            if (Connection)
                VBthHandleEvent(Dev, 20, HCI_EV_AUTHENTICATION_COMPLETE, Connection->Handle, HCI_PAIRING_NOT_ALLOWED);
            break;

        case HCI_SET_CONNECTION_ENCRYPTION:
            if (Data[2] < 3)
                break;
            Handle = (USHORT)((In[0] | (In[1] << 8)) & 0x0FFF);
            Connection = VBthFindHandle(Dev, Handle);
            VBthCommandStatus(Dev, Opcode, Connection ? HCI_SUCCESS : HCI_UNKNOWN_CONNECTION);
            if (Connection)
            {
                Parameters[0] = HCI_SUCCESS;
                Parameters[1] = (UCHAR)Handle;
                Parameters[2] = (UCHAR)(Handle >> 8);
                Parameters[3] = In[2];
                VBthEvent(Dev, 20, VBTH_TAG_NONE, HCI_EV_ENCRYPTION_CHANGE, Parameters, 4);
            }
            break;

        case HCI_REMOTE_NAME_REQUEST:
            if (Data[2] < 6)
                break;
            VBthCommandStatus(Dev, Opcode, HCI_SUCCESS);
            Peer = VBthFindPeer(In);
            RtlZeroMemory(Parameters, 255);
            Parameters[0] = Peer >= 0 ? HCI_SUCCESS : HCI_PAGE_TIMEOUT;
            RtlCopyMemory(Parameters + 1, In, 6);
            if (Peer >= 0)
                RtlCopyMemory(Parameters + 7, VBthPeers[Peer].Name, strlen(VBthPeers[Peer].Name));
            VBthEvent(Dev, 100, VBTH_TAG_NONE, HCI_EV_REMOTE_NAME_COMPLETE, Parameters, 255);
            break;

        case HCI_READ_REMOTE_FEATURES:
        case HCI_READ_REMOTE_VERSION:
            if (Data[2] < 2)
                break;
            Handle = (USHORT)((In[0] | (In[1] << 8)) & 0x0FFF);
            Connection = VBthFindHandle(Dev, Handle);
            VBthCommandStatus(Dev, Opcode, Connection ? HCI_SUCCESS : HCI_UNKNOWN_CONNECTION);
            if (!Connection)
                break;
            RtlZeroMemory(Parameters, 11);
            Parameters[1] = (UCHAR)Handle;
            Parameters[2] = (UCHAR)(Handle >> 8);
            if (Opcode == HCI_READ_REMOTE_FEATURES)
            {
                RtlCopyMemory(Parameters + 3, VBthFeatures, sizeof(VBthFeatures));
                VBthEvent(Dev, 20, VBTH_TAG_NONE, HCI_EV_REMOTE_FEATURES_COMPLETE, Parameters, 11);
            }
            else
            {
                Parameters[3] = 0x09;
                Parameters[4] = 0xFF;
                Parameters[5] = 0xFF;
                VBthEvent(Dev, 20, VBTH_TAG_NONE, HCI_EV_REMOTE_VERSION_COMPLETE, Parameters, 8);
            }
            break;

        default:
            VBthCommandComplete(Dev, Opcode, Return, 1);
            break;
    }
}

static
VOID
VBthSignal(
    _In_ PVBTH_DEVICE Dev,
    _In_ USHORT Handle,
    _In_ UCHAR Code,
    _In_ UCHAR Identifier,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ USHORT Length)
{
    UCHAR Packet[4 + 4 + 4 + 16];
    USHORT L2capLength = (USHORT)(4 + Length);

    if (Length > 16)
        return;
    Packet[0] = (UCHAR)Handle;
    Packet[1] = (UCHAR)((Handle >> 8) | 0x20);
    Packet[2] = (UCHAR)(4 + L2capLength);
    Packet[3] = 0;
    Packet[4] = (UCHAR)L2capLength;
    Packet[5] = 0;
    Packet[6] = (UCHAR)L2CAP_SIGNALING_CID;
    Packet[7] = 0;
    Packet[8] = Code;
    Packet[9] = Identifier;
    Packet[10] = (UCHAR)Length;
    Packet[11] = 0;
    RtlCopyMemory(Packet + 12, Payload, Length);
    VBthSchedule(Dev, VBthKindAcl, 10, VBTH_TAG_NONE, Packet, 12 + Length);
}

static
VOID
VBthAcl(
    _In_ PVBTH_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Completed[5], Reply[8];
    const UCHAR *Command;
    USHORT Handle, Cid, L2capLength, CommandLength;

    if (Length < 4)
        return;
    Handle = (USHORT)((Data[0] | (Data[1] << 8)) & 0x0FFF);
    Completed[0] = 1;
    Completed[1] = (UCHAR)Handle;
    Completed[2] = (UCHAR)(Handle >> 8);
    Completed[3] = 1;
    Completed[4] = 0;
    VBthEvent(Dev, 5, VBTH_TAG_NONE, HCI_EV_NUMBER_OF_COMPLETED, Completed, sizeof(Completed));

    if (!VBthFindHandle(Dev, Handle) || ((Data[1] >> 4) & 0x03) == 0x01 || Length < 12)
        return;
    L2capLength = (USHORT)(Data[4] | (Data[5] << 8));
    Cid = (USHORT)(Data[6] | (Data[7] << 8));
    if (Cid != L2CAP_SIGNALING_CID || L2capLength < 4 || Length < 8u + L2capLength)
        return;
    Command = Data + 8;
    CommandLength = (USHORT)(Command[2] | (Command[3] << 8));
    if (CommandLength > L2capLength - 4)
        return;

    switch (Command[0])
    {
        case L2CAP_CONNECTION_REQUEST:
            if (CommandLength < 4)
                return;
            Reply[0] = 0;
            Reply[1] = 0;
            Reply[2] = Command[6];
            Reply[3] = Command[7];
            Reply[4] = (UCHAR)L2CAP_PSM_NOT_SUPPORTED;
            Reply[5] = 0;
            Reply[6] = 0;
            Reply[7] = 0;
            VBthSignal(Dev, Handle, L2CAP_CONNECTION_RESPONSE, Command[1], Reply, 8);
            break;

        case L2CAP_INFORMATION_REQUEST:
            if (CommandLength < 2)
                return;
            Reply[0] = Command[4];
            Reply[1] = Command[5];
            Reply[2] = 0x01;
            Reply[3] = 0x00;
            VBthSignal(Dev, Handle, L2CAP_INFORMATION_RESPONSE, Command[1], Reply, 4);
            break;

        case L2CAP_DISCONNECTION_REQUEST:
            if (CommandLength < 4)
                return;
            VBthSignal(Dev, Handle, L2CAP_DISCONNECTION_RESPONSE, Command[1], Command + 4, 4);
            break;

        case L2CAP_COMMAND_REJECT:
        case L2CAP_CONNECTION_RESPONSE:
        case L2CAP_DISCONNECTION_RESPONSE:
        case L2CAP_INFORMATION_RESPONSE:
            break;

        default:
            Reply[0] = 0;
            Reply[1] = 0;
            VBthSignal(Dev, Handle, L2CAP_COMMAND_REJECT, Command[1], Reply, 2);
            break;
    }
}

static
VOID
NTAPI
VBthEvtTimer(
    _In_ WDFTIMER Timer)
{
    VBthPump(VBthGetDevice(WdfTimerGetParentObject(Timer)));
}

static
VOID
NTAPI
VBthEvtPdoIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PVBTH_DEVICE Dev = VBthGetPdo(WdfIoQueueGetDevice(Queue))->Parent;
    WDFMEMORY Input, Output;
    PVOID InBuffer, OutBuffer;
    size_t InSize, OutSize;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode)
    {
        case IOCTL_BTHX_GET_VERSION:
            Status = WdfRequestRetrieveOutputMemory(Request, &Output);
            if (!NT_SUCCESS(Status))
                break;
            OutBuffer = WdfMemoryGetBuffer(Output, &OutSize);
            if (OutSize < sizeof(BTHX_VERSION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            ((PBTHX_VERSION)OutBuffer)->Version = BTHX_DDI_VERSION_1;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(BTHX_VERSION));
            return;

        case IOCTL_BTHX_SET_VERSION:
            Status = WdfRequestRetrieveInputMemory(Request, &Input);
            if (!NT_SUCCESS(Status))
                break;
            InBuffer = WdfMemoryGetBuffer(Input, &InSize);
            Status = InSize >= sizeof(BTHX_VERSION) && ((PBTHX_VERSION)InBuffer)->Version == BTHX_DDI_VERSION_1 ?
                     STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
            break;

        case IOCTL_BTHX_QUERY_CAPABILITIES:
            Status = WdfRequestRetrieveOutputMemory(Request, &Output);
            if (!NT_SUCCESS(Status))
                break;
            OutBuffer = WdfMemoryGetBuffer(Output, &OutSize);
            if (OutSize < sizeof(BTHX_CAPABILITIES))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            RtlZeroMemory(OutBuffer, sizeof(BTHX_CAPABILITIES));
            ((PBTHX_CAPABILITIES)OutBuffer)->MaxAclTransferInSize = VBTH_MAX_ACL;
            ((PBTHX_CAPABILITIES)OutBuffer)->ScoSupport = ScoSupportNone;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(BTHX_CAPABILITIES));
            return;

        case IOCTL_BTHX_WRITE_HCI:
        {
            PBTHX_HCI_READ_WRITE_CONTEXT Context;

            Status = WdfRequestRetrieveInputMemory(Request, &Input);
            if (!NT_SUCCESS(Status))
                break;
            Context = WdfMemoryGetBuffer(Input, &InSize);
            if (InSize < FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) ||
                Context->DataLen > InSize - FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) ||
                (Context->Type != HciPacketCommand && Context->Type != HciPacketAclData))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (!Dev->Running)
            {
                Status = STATUS_DEVICE_NOT_READY;
                break;
            }
            if (Context->Type == HciPacketCommand)
                VBthCommand(Dev, Context->Data, Context->DataLen);
            else
                VBthAcl(Dev, Context->Data, Context->DataLen);
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, InSize);
            VBthPump(Dev);
            return;
        }

        case IOCTL_BTHX_READ_HCI:
        {
            VBTH_KIND Kind;

            Status = WdfRequestRetrieveInputMemory(Request, &Input);
            if (!NT_SUCCESS(Status))
                break;
            InBuffer = WdfMemoryGetBuffer(Input, &InSize);
            if (InSize < sizeof(UCHAR))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (*(PUCHAR)InBuffer == HciPacketEvent)
                Kind = VBthKindEvent;
            else if (*(PUCHAR)InBuffer == HciPacketAclData)
                Kind = VBthKindAcl;
            else
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = WdfRequestForwardToIoQueue(Request, Dev->Queue[Kind]);
            if (NT_SUCCESS(Status))
            {
                VBthDeliver(Dev, Kind);
                return;
            }
            break;
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    WdfRequestComplete(Request, Status);
}

static
NTSTATUS
VBthCreatePdo(
    _In_ PVBTH_DEVICE Dev)
{
    DECLARE_CONST_UNICODE_STRING(HardwareId, L"VBTHHCI\\RADIO");
    DECLARE_CONST_UNICODE_STRING(CompatibleId, VBTH_PDO_COMPATIBLE_ID);
    DECLARE_CONST_UNICODE_STRING(InstanceId, L"00");
    DECLARE_CONST_UNICODE_STRING(Description, L"Bluetooth Radio");
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Child;
    WDFQUEUE Queue;
    NTSTATUS Status;

    Init = WdfPdoInitAllocate(Dev->Device);
    if (!Init)
        return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_BLUETOOTH);
    Status = WdfPdoInitAssignDeviceID(Init, &HardwareId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddHardwareID(Init, &HardwareId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddCompatibleID(Init, &CompatibleId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignInstanceID(Init, &InstanceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddDeviceText(Init, &Description, &Description, 0x409);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    WdfPdoInitSetDefaultLocale(Init, 0x409);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, VBTH_PDO);
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    VBthGetPdo(Child)->Parent = Dev;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoDeviceControl = VBthEvtPdoIoctl;
    QueueConfig.EvtIoInternalDeviceControl = VBthEvtPdoIoctl;
    Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Queue);
    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->Queue[VBthKindEvent]);
    }
    if (NT_SUCCESS(Status))
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->Queue[VBthKindAcl]);
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Dev->Device, Child);
    if (!NT_SUCCESS(Status))
    {
        Dev->Queue[VBthKindEvent] = Dev->Queue[VBthKindAcl] = NULL;
        WdfObjectDelete(Child);
        return Status;
    }
    Dev->Child = Child;
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
VBthEvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PVBTH_DEVICE Dev = VBthGetDevice(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    Dev->Running = TRUE;
    if (!Dev->Child && !NT_SUCCESS(VBthCreatePdo(Dev)))
        DPRINT1("VBTHHCI: radio PDO not created\n");
    VBthPump(Dev);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
VBthEvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PVBTH_DEVICE Dev = VBthGetDevice(Device);
    ULONG Kind;

    UNREFERENCED_PARAMETER(TargetState);
    Dev->Running = FALSE;
    WdfTimerStop(Dev->Timer, TRUE);
    VBthCancelTagged(Dev, VBTH_TAG_NONE);
    for (Kind = 0; Kind < VBthKinds; Kind++)
    {
        while (!IsListEmpty(&Dev->Ready[Kind]))
            ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dev->Ready[Kind]), VBTH_PACKET, Link), VBTH_TAG);
        Dev->ReadyCount[Kind] = 0;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
VBthEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS Callbacks;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_TIMER_CONFIG TimerConfig;
    PVBTH_DEVICE Dev;
    WDFDEVICE Device;
    NTSTATUS Status;
    ULONG Kind;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Callbacks);
    Callbacks.EvtDeviceD0Entry = VBthEvtD0Entry;
    Callbacks.EvtDeviceD0Exit = VBthEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &Callbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, VBTH_DEVICE);
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Dev = VBthGetDevice(Device);
    Dev->Device = Device;
    Dev->NextHandle = 0x0001;
    KeInitializeSpinLock(&Dev->Lock);
    InitializeListHead(&Dev->Pending);
    for (Kind = 0; Kind < VBthKinds; Kind++)
        InitializeListHead(&Dev->Ready[Kind]);

    WDF_TIMER_CONFIG_INIT(&TimerConfig, VBthEvtTimer);
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    return WdfTimerCreate(&TimerConfig, &Attributes, &Dev->Timer);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    WDF_DRIVER_CONFIG_INIT(&Config, VBthEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
