/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth request blocks submitted by profile drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define BTHMINI_FIRST_DYNAMIC_PSM   0x1001
#define BTHMINI_DEFAULT_RX_DEPTH    32
#define L2CAP_RESULT_SUCCESS        0x0000
#define L2CAP_RESULT_BAD_PSM        0x0002
#define L2CAP_RESULT_SECURITY_BLOCK 0x0003
#define L2CAP_RESULT_NO_RESOURCES   0x0004

typedef struct _BTHMINI_PROFILE_SERVER
{
    LIST_ENTRY Link;
    BTH_ADDR Address;
    PFNBTHPORT_INDICATION_CALLBACK Callback;
    PVOID Context;
    PVOID ReferenceObject;
} BTHMINI_PROFILE_SERVER, *PBTHMINI_PROFILE_SERVER;

typedef struct _BTHMINI_PROFILE_PSM
{
    LIST_ENTRY Link;
    USHORT Psm;
    ULONG References;
} BTHMINI_PROFILE_PSM, *PBTHMINI_PROFILE_PSM;

typedef struct _BTHMINI_RX_DATA
{
    LIST_ENTRY Link;
    ULONG Length;
    ULONG Offset;
    UCHAR Data[ANYSIZE_ARRAY];
} BTHMINI_RX_DATA, *PBTHMINI_RX_DATA;

typedef struct _BTHMINI_PROFILE_CHANNEL
{
    LIST_ENTRY Link;
    PBTHMINI_CHANNEL Channel;
    BTH_ADDR Address;
    USHORT Psm;
    BOOLEAN Incoming;
    BOOLEAN Opened;
    BOOLEAN CloseRequested;
    BOOLEAN KeepNew;
    ULONG CallbackFlags;
    PFNBTHPORT_INDICATION_CALLBACK Callback;
    PVOID CallbackContext;
    PVOID ReferenceObject;
    WDFREQUEST OpenRequest;
    WDFQUEUE Reads;
    LIST_ENTRY RxData;
    ULONG RxCount;
    ULONG RxDepth;
} BTHMINI_PROFILE_CHANNEL, *PBTHMINI_PROFILE_CHANNEL;

static
PBRB
BthBrbFromRequest(
    _In_ WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS Parameters;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    return Parameters.Parameters.Others.Arg1;
}

static
VOID
BthBrbComplete(
    _In_ WDFREQUEST Request,
    _In_ PBRB Brb,
    _In_ NTSTATUS Status)
{
    Brb->BrbHeader.Status = Status;
    Brb->BrbHeader.BtStatus = NT_SUCCESS(Status) ? BTH_ERROR_SUCCESS : BTH_ERROR_UNSPECIFIED;
    WdfRequestComplete(Request, Status);
}

static
VOID
BthBrbReference(
    _In_opt_ PVOID Object)
{
    if (Object)
        ObReferenceObject(Object);
}

static
VOID
BthBrbDereference(
    _In_opt_ PVOID Object)
{
    if (Object)
        ObDereferenceObject(Object);
}

static
PBTHMINI_PROFILE_PSM
BthBrbFindPsm(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Psm)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->ProfilePsms.Flink; Entry != &Dev->ProfilePsms; Entry = Entry->Flink)
    {
        PBTHMINI_PROFILE_PSM Registration = CONTAINING_RECORD(Entry, BTHMINI_PROFILE_PSM, Link);

        if (Registration->Psm == Psm)
            return Registration;
    }
    return NULL;
}

static
PBTHMINI_PROFILE_SERVER
BthBrbFindServer(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address)
{
    PBTHMINI_PROFILE_SERVER Any = NULL;
    PLIST_ENTRY Entry;

    for (Entry = Dev->ProfileServers.Flink; Entry != &Dev->ProfileServers; Entry = Entry->Flink)
    {
        PBTHMINI_PROFILE_SERVER Server = CONTAINING_RECORD(Entry, BTHMINI_PROFILE_SERVER, Link);

        if (Server->Address == Address)
            return Server;
        if (!Server->Address && !Any)
            Any = Server;
    }
    return Any;
}

static
PBTHMINI_PROFILE_CHANNEL
BthBrbFindChannel(
    _In_ PBTHMINI_DEVICE Dev,
    _In_opt_ L2CAP_CHANNEL_HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->ProfileChannels.Flink; Entry != &Dev->ProfileChannels; Entry = Entry->Flink)
    {
        PBTHMINI_PROFILE_CHANNEL Profile = CONTAINING_RECORD(Entry, BTHMINI_PROFILE_CHANNEL, Link);

        if (Profile == Handle)
            return Profile;
    }
    return NULL;
}

static
PBTHMINI_PROFILE_CHANNEL
BthBrbCreateChannel(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address,
    _In_ USHORT Psm,
    _In_ BOOLEAN Incoming)
{
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PBTHMINI_PROFILE_CHANNEL Profile;

    Profile = ExAllocatePoolZero(NonPagedPool, sizeof(*Profile), BTHMINI_TAG);
    if (!Profile)
        return NULL;
    WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
    QueueConfig.PowerManaged = WdfFalse;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Dev->Device;
    if (!NT_SUCCESS(WdfIoQueueCreate(Dev->Device, &QueueConfig, &Attributes, &Profile->Reads)))
    {
        ExFreePoolWithTag(Profile, BTHMINI_TAG);
        return NULL;
    }
    Profile->Address = Address;
    Profile->Psm = Psm;
    Profile->Incoming = Incoming;
    Profile->RxDepth = BTHMINI_DEFAULT_RX_DEPTH;
    InitializeListHead(&Profile->RxData);
    InsertTailList(&Dev->ProfileChannels, &Profile->Link);
    return Profile;
}

static
VOID
BthBrbFailReads(
    _In_ PBTHMINI_PROFILE_CHANNEL Profile,
    _In_ NTSTATUS Status)
{
    WDFREQUEST Request;

    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Profile->Reads, &Request)))
        BthBrbComplete(Request, BthBrbFromRequest(Request), Status);
    while (!IsListEmpty(&Profile->RxData))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Profile->RxData), BTHMINI_RX_DATA, Link), BTHMINI_TAG);
    Profile->RxCount = 0;
}

static
VOID
BthBrbFreeChannel(
    _In_ PBTHMINI_PROFILE_CHANNEL Profile)
{
    RemoveEntryList(&Profile->Link);
    if (Profile->Channel)
        Profile->Channel->Context = NULL;
    BthBrbFailReads(Profile, STATUS_CANCELLED);
    WdfObjectDelete(Profile->Reads);
    BthBrbDereference(Profile->ReferenceObject);
    ExFreePoolWithTag(Profile, BTHMINI_TAG);
}

static
VOID
BthBrbIndicateDisconnect(
    _In_ PBTHMINI_PROFILE_CHANNEL Profile,
    _In_ L2CAP_DISCONNECT_REASON Reason)
{
    INDICATION_PARAMETERS Parameters;

    if (!Profile->Callback || !(Profile->CallbackFlags & CALLBACK_DISCONNECT))
        return;
    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.ConnectionHandle = Profile;
    Parameters.BtAddress = Profile->Address;
    Parameters.Parameters.Disconnect.Reason = Reason;
    Parameters.Parameters.Disconnect.CloseNow = FALSE;
    Profile->Callback(Profile->CallbackContext, IndicationRemoteDisconnect, &Parameters);
}

static
PVOID
BthBrbTransferBuffer(
    _In_ struct _BRB_L2CA_ACL_TRANSFER *Transfer)
{
    if (Transfer->BufferMDL)
        return MmGetSystemAddressForMdlSafe(Transfer->BufferMDL, NormalPagePriority);
    return Transfer->Buffer;
}

static
VOID
BthBrbDeliver(
    _In_ PBTHMINI_PROFILE_CHANNEL Profile)
{
    while (!IsListEmpty(&Profile->RxData))
    {
        PBTHMINI_RX_DATA Data = CONTAINING_RECORD(Profile->RxData.Flink, BTHMINI_RX_DATA, Link);
        struct _BRB_L2CA_ACL_TRANSFER *Transfer;
        WDFREQUEST Request;
        PVOID Buffer;
        ULONG Copy;

        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Profile->Reads, &Request)))
            return;
        Transfer = &BthBrbFromRequest(Request)->BrbL2caAclTransfer;
        Buffer = BthBrbTransferBuffer(Transfer);
        if (!Buffer)
        {
            BthBrbComplete(Request, (PBRB)Transfer, STATUS_INSUFFICIENT_RESOURCES);
            continue;
        }
        Copy = min(Transfer->BufferSize, Data->Length - Data->Offset);
        RtlCopyMemory(Buffer, Data->Data + Data->Offset, Copy);
        Data->Offset += Copy;
        Transfer->BufferSize = Copy;
        Transfer->RemainingBufferSize = Data->Length - Data->Offset;
        if (Data->Offset == Data->Length)
        {
            RemoveEntryList(&Data->Link);
            Profile->RxCount--;
            ExFreePoolWithTag(Data, BTHMINI_TAG);
        }
        BthBrbComplete(Request, (PBRB)Transfer, STATUS_SUCCESS);
    }
}

static
VOID
BthBrbChannelOpened(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_PROFILE_CHANNEL Profile = Channel->Context;
    struct _BRB_L2CA_OPEN_CHANNEL *Open;
    WDFREQUEST Request;

    UNREFERENCED_PARAMETER(Dev);
    if (!Profile)
        return;
    Request = Profile->OpenRequest;
    Profile->OpenRequest = NULL;
    if (!NT_SUCCESS(Status))
    {
        Profile->Channel = NULL;
        Channel->Context = NULL;
        if (Request)
            BthBrbComplete(Request, BthBrbFromRequest(Request), Status);
        BthBrbFreeChannel(Profile);
        return;
    }

    Profile->Opened = TRUE;
    if (!Request)
        return;
    Open = &BthBrbFromRequest(Request)->BrbL2caOpenChannel;
    Open->ChannelHandle = Profile;
    Open->OutResults.Params.Flags = CFG_MTU;
    Open->OutResults.Params.Mtu = Channel->RemoteMtu;
    Open->InResults.Params.Flags = CFG_MTU;
    Open->InResults.Params.Mtu = L2CAP_LOCAL_MTU;
    BthBrbComplete(Request, (PBRB)Open, STATUS_SUCCESS);
}

static
VOID
BthBrbChannelReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_PROFILE_CHANNEL Profile = Channel->Context;
    PBTHMINI_RX_DATA Packet;

    UNREFERENCED_PARAMETER(Dev);
    if (!Profile || !Profile->Opened)
        return;
    if (Profile->RxCount >= Profile->RxDepth)
    {
        if (!Profile->KeepNew)
            return;
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Profile->RxData), BTHMINI_RX_DATA, Link), BTHMINI_TAG);
        Profile->RxCount--;
    }
    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(BTHMINI_RX_DATA, Data) + Length, BTHMINI_TAG);
    if (!Packet)
        return;
    Packet->Length = Length;
    Packet->Offset = 0;
    RtlCopyMemory(Packet->Data, Data, Length);
    InsertTailList(&Profile->RxData, &Packet->Link);
    Profile->RxCount++;

    if (Profile->Callback && (Profile->CallbackFlags & CALLBACK_RECV_PACKET))
    {
        INDICATION_PARAMETERS Parameters;

        RtlZeroMemory(&Parameters, sizeof(Parameters));
        Parameters.ConnectionHandle = Profile;
        Parameters.BtAddress = Profile->Address;
        Parameters.Parameters.RecvPacket.PacketLength = Length;
        Parameters.Parameters.RecvPacket.TotalQueueLength = Profile->RxCount;
        Profile->Callback(Profile->CallbackContext, IndicationRecvPacket, &Parameters);
    }
    BthBrbDeliver(Profile);
}

static
VOID
BthBrbChannelClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PBTHMINI_PROFILE_CHANNEL Profile = Channel->Context;
    WDFREQUEST Request;

    UNREFERENCED_PARAMETER(Dev);
    if (!Profile)
        return;
    Profile->Channel = NULL;
    Channel->Context = NULL;
    Request = Profile->OpenRequest;
    Profile->OpenRequest = NULL;
    if (Request)
        BthBrbComplete(Request, BthBrbFromRequest(Request), NT_SUCCESS(Status) ? STATUS_CONNECTION_ABORTED : Status);
    if (Profile->CloseRequested || !Profile->Opened)
    {
        BthBrbFreeChannel(Profile);
        return;
    }
    BthBrbFailReads(Profile, STATUS_REMOTE_DISCONNECT);
    BthBrbIndicateDisconnect(Profile, Status == STATUS_REMOTE_DISCONNECT ? L2capDisconnectRequest : HciDisconnect);
}

static const BTHMINI_CHANNEL_CALLBACKS BthBrbChannelCallbacks =
{
    BthBrbChannelOpened,
    BthBrbChannelReceive,
    BthBrbChannelClosed
};

const BTHMINI_CHANNEL_CALLBACKS *
BthBrbServer(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Psm)
{
    if (!BthBrbFindPsm(Dev, Psm) || IsListEmpty(&Dev->ProfileServers))
        return NULL;
    return &BthBrbChannelCallbacks;
}

VOID
BthBrbRemoteConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel)
{
    PBTHMINI_PROFILE_SERVER Server = BthBrbFindServer(Dev, Channel->Remote->Address);
    PBTHMINI_PROFILE_CHANNEL Profile;
    INDICATION_PARAMETERS Parameters;
    PLIST_ENTRY Entry;

    Profile = Server ? BthBrbCreateChannel(Dev, Channel->Remote->Address, Channel->Psm, TRUE) : NULL;
    if (!Profile)
    {
        BthL2capAccept(Dev, Channel, Server ? L2CAP_RESULT_NO_RESOURCES : L2CAP_RESULT_BAD_PSM);
        return;
    }
    Profile->Channel = Channel;
    Channel->Context = Profile;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.ConnectionHandle = Profile;
    Parameters.BtAddress = Profile->Address;
    Parameters.Parameters.Connect.Request.PSM = Channel->Psm;
    DPRINT1("BTHMINI: incoming L2CAP PSM 0x%04x from %012I64x offered to the profile drivers\n",
            Channel->Psm, Profile->Address);
    for (Entry = Dev->ProfileServers.Flink; Entry != &Dev->ProfileServers; Entry = Entry->Flink)
    {
        Server = CONTAINING_RECORD(Entry, BTHMINI_PROFILE_SERVER, Link);
        if (Server->Address == Profile->Address || !Server->Address)
            Server->Callback(Server->Context, IndicationRemoteConnect, &Parameters);
    }
}

static
NTSTATUS
BthBrbRegisterPsm(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ struct _BRB_PSM *Brb)
{
    PBTHMINI_PROFILE_PSM Registration;
    USHORT Psm = Brb->Psm;

    if (!Psm)
    {
        for (Psm = Dev->NextPsm ? Dev->NextPsm : BTHMINI_FIRST_DYNAMIC_PSM; BthBrbFindPsm(Dev, Psm); Psm += 2)
            ;
        Dev->NextPsm = Psm + 2;
    }
    else if (!(Psm & 1) || Psm == L2CAP_PSM_SDP || Psm == L2CAP_PSM_RFCOMM)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Registration = BthBrbFindPsm(Dev, Psm);
    if (Registration)
    {
        Registration->References++;
        Brb->Psm = Psm;
        return STATUS_SUCCESS;
    }
    Registration = ExAllocatePoolZero(NonPagedPool, sizeof(*Registration), BTHMINI_TAG);
    if (!Registration)
        return STATUS_INSUFFICIENT_RESOURCES;
    Registration->Psm = Psm;
    Registration->References = 1;
    InsertTailList(&Dev->ProfilePsms, &Registration->Link);
    Brb->Psm = Psm;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthBrbUnregisterPsm(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ struct _BRB_PSM *Brb)
{
    PBTHMINI_PROFILE_PSM Registration = BthBrbFindPsm(Dev, Brb->Psm);

    if (!Registration)
        return STATUS_NOT_FOUND;
    if (--Registration->References)
        return STATUS_SUCCESS;
    RemoveEntryList(&Registration->Link);
    ExFreePoolWithTag(Registration, BTHMINI_TAG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthBrbRegisterServer(
    _In_ PBTHMINI_DEVICE Dev,
    _Inout_ struct _BRB_L2CA_REGISTER_SERVER *Brb)
{
    PBTHMINI_PROFILE_SERVER Server;

    if (!Brb->IndicationCallback)
        return STATUS_INVALID_PARAMETER;
    Server = ExAllocatePoolZero(NonPagedPool, sizeof(*Server), BTHMINI_TAG);
    if (!Server)
        return STATUS_INSUFFICIENT_RESOURCES;
    Server->Address = Brb->BtAddress;
    Server->Callback = Brb->IndicationCallback;
    Server->Context = Brb->IndicationCallbackContext;
    Server->ReferenceObject = Brb->ReferenceObject;
    BthBrbReference(Server->ReferenceObject);
    InsertTailList(&Dev->ProfileServers, &Server->Link);
    Brb->ServerHandle = Server;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthBrbUnregisterServer(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ struct _BRB_L2CA_UNREGISTER_SERVER *Brb)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->ProfileServers.Flink; Entry != &Dev->ProfileServers; Entry = Entry->Flink)
    {
        PBTHMINI_PROFILE_SERVER Server = CONTAINING_RECORD(Entry, BTHMINI_PROFILE_SERVER, Link);

        if (Server != Brb->ServerHandle)
            continue;
        RemoveEntryList(&Server->Link);
        BthBrbDereference(Server->ReferenceObject);
        ExFreePoolWithTag(Server, BTHMINI_TAG);
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_HANDLE;
}

static
VOID
BthBrbTakeCallbacks(
    _Inout_ PBTHMINI_PROFILE_CHANNEL Profile,
    _In_ struct _BRB_L2CA_OPEN_CHANNEL *Open)
{
    Profile->Callback = Open->Callback;
    Profile->CallbackContext = Open->CallbackContext;
    Profile->CallbackFlags = Open->CallbackFlags;
    Profile->ReferenceObject = Open->ReferenceObject;
    BthBrbReference(Profile->ReferenceObject);
    Profile->KeepNew = (Open->ChannelFlags & CF_QUEUE_KEEP_NEW) != 0;
    if (Open->IncomingQueueDepth)
        Profile->RxDepth = Open->IncomingQueueDepth;
}

static
NTSTATUS
BthBrbOpenChannel(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Inout_ struct _BRB_L2CA_OPEN_CHANNEL *Open)
{
    PBTHMINI_PROFILE_CHANNEL Profile;
    PBTHMINI_REMOTE Remote;
    NTSTATUS Status;

    if (!Open->Psm || !Open->BtAddress)
        return STATUS_INVALID_PARAMETER;
    Remote = BthRadioGetRemote(Dev, Open->BtAddress);
    if (!Remote)
        return STATUS_INSUFFICIENT_RESOURCES;
    Profile = BthBrbCreateChannel(Dev, Open->BtAddress, Open->Psm, FALSE);
    if (!Profile)
        return STATUS_INSUFFICIENT_RESOURCES;
    BthBrbTakeCallbacks(Profile, Open);
    Profile->OpenRequest = Request;
    Status = BthL2capConnect(Dev, Remote, Open->Psm, &BthBrbChannelCallbacks, Profile, &Profile->Channel);
    if (!NT_SUCCESS(Status))
    {
        Profile->OpenRequest = NULL;
        BthBrbFreeChannel(Profile);
        return Status;
    }
    return STATUS_PENDING;
}

static
NTSTATUS
BthBrbOpenChannelResponse(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Inout_ struct _BRB_L2CA_OPEN_CHANNEL *Open)
{
    PBTHMINI_PROFILE_CHANNEL Profile = BthBrbFindChannel(Dev, Open->ChannelHandle);
    PBTHMINI_CHANNEL Channel;
    USHORT Result;

    if (!Profile || !Profile->Incoming || Profile->Opened || Profile->OpenRequest)
        return STATUS_INVALID_HANDLE;
    Channel = Profile->Channel;
    if (!Channel)
    {
        BthBrbFreeChannel(Profile);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    switch (Open->Response)
    {
        case CONNECT_RSP_RESULT_SUCCESS:
            BthBrbTakeCallbacks(Profile, Open);
            Profile->OpenRequest = Request;
            BthL2capAccept(Dev, Channel, L2CAP_RESULT_SUCCESS);
            return STATUS_PENDING;
        case CONNECT_RSP_RESULT_PSM_NEG:
            Result = L2CAP_RESULT_BAD_PSM;
            break;
        case CONNECT_RSP_RESULT_SECURITY_BLOCK:
            Result = L2CAP_RESULT_SECURITY_BLOCK;
            break;
        default:
            Result = L2CAP_RESULT_NO_RESOURCES;
            break;
    }
    Profile->CloseRequested = TRUE;
    BthL2capAccept(Dev, Channel, Result);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthBrbCloseChannel(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ struct _BRB_L2CA_CLOSE_CHANNEL *Close)
{
    PBTHMINI_PROFILE_CHANNEL Profile = BthBrbFindChannel(Dev, Close->ChannelHandle);

    if (!Profile)
        return STATUS_INVALID_HANDLE;
    Profile->CloseRequested = TRUE;
    BthBrbFailReads(Profile, STATUS_CANCELLED);
    if (Profile->Channel)
        BthL2capDisconnect(Dev, Profile->Channel);
    else
        BthBrbFreeChannel(Profile);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthBrbAclTransfer(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Inout_ struct _BRB_L2CA_ACL_TRANSFER *Transfer)
{
    PBTHMINI_PROFILE_CHANNEL Profile = BthBrbFindChannel(Dev, Transfer->ChannelHandle);
    PVOID Buffer;
    NTSTATUS Status;

    if (!Profile || Profile->CloseRequested)
        return STATUS_INVALID_HANDLE;
    if (!Profile->Opened || !Profile->Channel)
        return STATUS_DEVICE_NOT_CONNECTED;

    if (Transfer->TransferFlags & ACL_TRANSFER_DIRECTION_IN)
    {
        Status = WdfRequestForwardToIoQueue(Request, Profile->Reads);
        if (!NT_SUCCESS(Status))
            return Status;
        BthBrbDeliver(Profile);
        return STATUS_PENDING;
    }

    Buffer = BthBrbTransferBuffer(Transfer);
    if (!Buffer && Transfer->BufferSize)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = BthL2capSend(Dev, Profile->Channel, Buffer, Transfer->BufferSize);
    if (NT_SUCCESS(Status))
        Transfer->RemainingBufferSize = 0;
    return Status;
}

static
VOID
BthBrbSubmit(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Inout_ PBRB Brb)
{
    NTSTATUS Status;

    switch (Brb->BrbHeader.Type)
    {
        case BRB_HCI_GET_LOCAL_BD_ADDR:
            Brb->BrbGetLocalBdAddress.BtAddress = Dev->Address;
            Status = STATUS_SUCCESS;
            break;
        case BRB_REGISTER_PSM:
            Status = BthBrbRegisterPsm(Dev, &Brb->BrbPsm);
            break;
        case BRB_UNREGISTER_PSM:
            Status = BthBrbUnregisterPsm(Dev, &Brb->BrbPsm);
            break;
        case BRB_L2CA_REGISTER_SERVER:
            Status = BthBrbRegisterServer(Dev, &Brb->BrbL2caRegisterServer);
            break;
        case BRB_L2CA_UNREGISTER_SERVER:
            Status = BthBrbUnregisterServer(Dev, &Brb->BrbL2caUnregisterServer);
            break;
        case BRB_L2CA_OPEN_CHANNEL:
            Status = BthBrbOpenChannel(Dev, Request, &Brb->BrbL2caOpenChannel);
            break;
        case BRB_L2CA_OPEN_CHANNEL_RESPONSE:
            Status = BthBrbOpenChannelResponse(Dev, Request, &Brb->BrbL2caOpenChannel);
            break;
        case BRB_L2CA_CLOSE_CHANNEL:
            Status = BthBrbCloseChannel(Dev, &Brb->BrbL2caCloseChannel);
            break;
        case BRB_L2CA_ACL_TRANSFER:
            Status = BthBrbAclTransfer(Dev, Request, &Brb->BrbL2caAclTransfer);
            break;
        case BRB_ACL_GET_MODE:
            Brb->BrbAclGetMode.AclMode = ACL_MODE_ACTIVE;
            Status = STATUS_SUCCESS;
            break;
        case BRB_ACL_ENTER_ACTIVE_MODE:
            Status = STATUS_SUCCESS;
            break;
        default:
            DPRINT1("BTHMINI: BRB type 0x%04x not supported\n", Brb->BrbHeader.Type);
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    if (Status != STATUS_PENDING)
        BthBrbComplete(Request, Brb, Status);
}

VOID
BthBrbInternalControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS Parameters;
    PBTHMINI_REMOTE Remote;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    switch (Parameters.Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTERNAL_BTH_SUBMIT_BRB:
            BthBrbSubmit(Dev, Request, Parameters.Parameters.Others.Arg1);
            return;

        case IOCTL_INTERNAL_BTHENUM_GET_DEVINFO:
            Remote = BthRadioFindRemote(Dev, BthMiniGetRequest(Request)->Address);
            if (!Remote)
            {
                WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
                return;
            }
            BthRadioFillInfo(Remote, Parameters.Parameters.Others.Arg1);
            WdfRequestComplete(Request, STATUS_SUCCESS);
            return;

        default:
            WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
            return;
    }
}

VOID
BthBrbFlush(
    _In_ PBTHMINI_DEVICE Dev)
{
    while (!IsListEmpty(&Dev->ProfileChannels))
    {
        PBTHMINI_PROFILE_CHANNEL Profile = CONTAINING_RECORD(Dev->ProfileChannels.Flink, BTHMINI_PROFILE_CHANNEL, Link);
        WDFREQUEST Request = Profile->OpenRequest;

        Profile->OpenRequest = NULL;
        if (Request)
            BthBrbComplete(Request, BthBrbFromRequest(Request), STATUS_DEVICE_NOT_CONNECTED);
        if (Profile->Opened && !Profile->CloseRequested)
            BthBrbIndicateDisconnect(Profile, RadioPoweredDown);
        BthBrbFreeChannel(Profile);
    }
}

VOID
BthBrbCleanup(
    _In_ PBTHMINI_DEVICE Dev)
{
    while (!IsListEmpty(&Dev->ProfileServers))
    {
        PBTHMINI_PROFILE_SERVER Server = CONTAINING_RECORD(RemoveHeadList(&Dev->ProfileServers),
                                                           BTHMINI_PROFILE_SERVER, Link);

        BthBrbDereference(Server->ReferenceObject);
        ExFreePoolWithTag(Server, BTHMINI_TAG);
    }
    while (!IsListEmpty(&Dev->ProfilePsms))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dev->ProfilePsms), BTHMINI_PROFILE_PSM, Link), BTHMINI_TAG);
}
