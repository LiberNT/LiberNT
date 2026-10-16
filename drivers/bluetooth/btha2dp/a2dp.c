/*
 * PROJECT:     LiberNT Bluetooth A2DP audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A2DP source: AVDTP signaling, stream setup and SBC media transport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <bthdef.h>
#include <bthioctl.h>
#include <bthddi.h>
#include <bthguid.h>
#include <bthminiddi.h>
#include <bthvolume.h>
#include "sbc.h"
#include "btha2dp.h"

#define NDEBUG
#include <debug.h>

#define AVDTP_MESSAGE_COMMAND               0x00
#define AVDTP_MESSAGE_GENERAL_REJECT        0x01
#define AVDTP_MESSAGE_ACCEPT                0x02
#define AVDTP_MESSAGE_REJECT                0x03
#define AVDTP_PACKET_SINGLE                 0x00
#define AVDTP_PACKET_START                  0x01
#define AVDTP_PACKET_CONTINUE               0x02
#define AVDTP_PACKET_END                    0x03

#define AVDTP_DISCOVER                      0x01
#define AVDTP_GET_CAPABILITIES              0x02
#define AVDTP_SET_CONFIGURATION             0x03
#define AVDTP_GET_CONFIGURATION             0x04
#define AVDTP_RECONFIGURE                   0x05
#define AVDTP_OPEN                          0x06
#define AVDTP_START                         0x07
#define AVDTP_CLOSE                         0x08
#define AVDTP_SUSPEND                       0x09
#define AVDTP_ABORT                         0x0A
#define AVDTP_SECURITY_CONTROL              0x0B
#define AVDTP_GET_ALL_CAPABILITIES          0x0C
#define AVDTP_DELAYREPORT                   0x0D

#define AVDTP_ERROR_BAD_LENGTH              0x11
#define AVDTP_ERROR_BAD_ACP_SEID            0x12
#define AVDTP_ERROR_SEP_IN_USE              0x13
#define AVDTP_ERROR_SEP_NOT_IN_USE          0x14
#define AVDTP_ERROR_BAD_SERV_CATEGORY       0x17
#define AVDTP_ERROR_BAD_PAYLOAD_FORMAT      0x18
#define AVDTP_ERROR_NOT_SUPPORTED_COMMAND   0x19
#define AVDTP_ERROR_INVALID_CAPABILITIES    0x1A
#define AVDTP_ERROR_UNSUPPORTED_CONFIG      0x29
#define AVDTP_ERROR_BAD_STATE               0x31

#define AVDTP_CATEGORY_MEDIA_TRANSPORT      0x01
#define AVDTP_CATEGORY_CONTENT_PROTECTION   0x04
#define AVDTP_CATEGORY_MEDIA_CODEC          0x07
#define AVDTP_CATEGORY_DELAY_REPORTING      0x08
#define AVDTP_MEDIA_AUDIO                   0x00
#define AVDTP_CODEC_SBC                     0x00
#define AVDTP_TSEP_SINK                     0x01

#define A2DP_SBC_44100                      0x20
#define A2DP_SBC_48000                      0x10
#define A2DP_SBC_MONO                       0x08
#define A2DP_SBC_DUAL                       0x04
#define A2DP_SBC_STEREO                     0x02
#define A2DP_SBC_JOINT                      0x01
#define A2DP_SBC_BLOCKS_4                   0x80
#define A2DP_SBC_BLOCKS_8                   0x40
#define A2DP_SBC_BLOCKS_12                  0x20
#define A2DP_SBC_BLOCKS_16                  0x10
#define A2DP_SBC_SUBBANDS_8                 0x04
#define A2DP_SBC_SNR                        0x02
#define A2DP_SBC_LOUDNESS                   0x01
#define A2DP_SBC_MIN_BITPOOL                2
#define A2DP_SBC_MAX_BITPOOL                53
#define A2DP_SBC_BITPOOL_48000_JOINT        51
#define A2DP_SBC_BITPOOL_48000_MONO         29

#define A2DP_LOCAL_SEID                     1
#define A2DP_SIGNAL_TIMEOUT_MS              5000
#define A2DP_SUSPEND_DELAY_MS               5000
#define A2DP_RECONNECT_DELAY_MS             10000
#define A2DP_CONNECT_ATTEMPTS               3
#define A2DP_READS                          2
#define A2DP_SENDS                          8
#define A2DP_TRANSFER_SIZE                  2048
#define A2DP_SIGNAL_SIZE                    1024
#define A2DP_RTP_HEADER                     12
#define A2DP_RTP_PAYLOAD_TYPE               96
#define A2DP_MAX_FRAMES_PER_PACKET          15

#define A2DP_STATE_IDLE                     0
#define A2DP_STATE_CONFIGURED               1
#define A2DP_STATE_OPEN                     2
#define A2DP_STATE_STREAMING                3

#define A2DP_EVENT_DATA                     0
#define A2DP_EVENT_CONNECT                  1
#define A2DP_EVENT_DISCONNECT               2

typedef struct _A2DP_EVENT
{
    LIST_ENTRY Link;
    ULONG Type;
    L2CAP_CHANNEL_HANDLE Handle;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} A2DP_EVENT, *PA2DP_EVENT;

typedef struct _A2DP_TRANSFER
{
    PA2DP_CONTEXT Context;
    PIRP Irp;
    volatile LONG Busy;
    L2CAP_CHANNEL_HANDLE Handle;
    BRB Brb;
    UCHAR Buffer[A2DP_TRANSFER_SIZE];
} A2DP_TRANSFER, *PA2DP_TRANSFER;

struct _A2DP_CONTEXT
{
    PDEVICE_OBJECT Pdo;
    BTH_PROFILE_DRIVER_INTERFACE Profile;
    BOOLEAN HaveProfile;
    BTH_ADDR Address;
    KSPIN_LOCK Lock;
    LIST_ENTRY Events;
    KEVENT Wake;
    PKTHREAD Thread;
    volatile BOOLEAN Stopping;
    BOOLEAN Shutdown;
    volatile LONG IoOutstanding;
    KEVENT IoDrained;
    BOOLEAN PsmRegistered;
    L2CAP_SERVER_HANDLE Server;
    HANDLE_SDP SdpRecord;

    L2CAP_CHANNEL_HANDLE Signaling;
    L2CAP_CHANNEL_HANDLE Transport;
    L2CAP_CHANNEL_HANDLE PendingIncoming;
    USHORT TransportMtu;
    A2DP_TRANSFER Reads[A2DP_READS];
    A2DP_TRANSFER Sends[A2DP_SENDS];

    UCHAR NextLabel;
    UCHAR PendingLabel;
    UCHAR PendingSignal;
    BOOLEAN PendingWaiting;
    UCHAR ResponseType;
    ULONG ResponseLength;
    UCHAR Response[A2DP_SIGNAL_SIZE];
    UCHAR Fragment[A2DP_SIGNAL_SIZE];
    ULONG FragmentLength;
    UCHAR FragmentPackets;

    UCHAR State;
    UCHAR RemoteSeid;
    BOOLEAN DelayReporting;
    UCHAR Codec[4];
    SBC_ENCODER Encoder;
    ULONG FrameLength;
    ULONG FrameSamples;
    USHORT Sequence;
    ULONG Timestamp;
    ULONGLONG RetryAfter;
    ULONGLONG IdleSince;
    ULONG ConnectAttempts;
    UCHAR Packet[A2DP_TRANSFER_SIZE];
    ULONG PacketLength;
    ULONG PacketFrames;
    ULONG PacketTimestamp;

    PUCHAR Buffer;
    ULONG BufferSize;
    ULONG PeriodBytes;
    volatile BOOLEAN Running;
    LARGE_INTEGER QpcFrequency;
    LONGLONG StartQpc;
    ULONGLONG BaseBytes;
    ULONGLONG EncodedSamples;
    ULONGLONG PositionBytes;
    PKEVENT NotificationEvent;
    volatile LONG Volume[BTHA2DP_CHANNELS];
    volatile LONG Mute;
    volatile LONG AbsoluteVolume;
    volatile LONG SentLevel;
    PCALLBACK_OBJECT VolumeSetCallback;
    PCALLBACK_OBJECT VolumeStateCallback;
    PVOID VolumeStateRegistration;
};


static CALLBACK_FUNCTION A2dpVolumeState;
static IO_COMPLETION_ROUTINE A2dpReadComplete;
static IO_COMPLETION_ROUTINE A2dpSendComplete;
static IO_COMPLETION_ROUTINE A2dpAsyncComplete;

static
ULONG
A2dpPump(
    _In_ PA2DP_CONTEXT Context);

static
ULONGLONG
A2dpNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
A2dpIoDone(
    _In_ PA2DP_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->IoOutstanding) == 0)
        KeSetEvent(&Context->IoDrained, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
A2dpCallPdo(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, Context->Pdo, NULL, 0, NULL, 0, TRUE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Argument1;
    IoStack->Parameters.Others.Argument2 = Argument2;
    Status = IoCallDriver(Context->Pdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
A2dpCallPdoBuffered(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, Context->Pdo, Input, InputLength, Output, OutputLength, FALSE,
                                        &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(Context->Pdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
A2dpSubmitSync(
    _In_ PA2DP_CONTEXT Context,
    _Inout_ PBRB Brb)
{
    return A2dpCallPdo(Context, IOCTL_INTERNAL_BTH_SUBMIT_BRB, Brb, NULL);
}

static
VOID
A2dpSubmitAsync(
    _In_ PA2DP_CONTEXT Context,
    _In_ PIRP Irp,
    _Inout_ PBRB Brb,
    _In_ PIO_COMPLETION_ROUTINE Completion,
    _In_ PVOID CompletionContext)
{
    PIO_STACK_LOCATION IoStack;

    IoReuseIrp(Irp, STATUS_SUCCESS);
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_BTH_SUBMIT_BRB;
    IoStack->Parameters.Others.Argument1 = Brb;
    IoSetCompletionRoutine(Irp, Completion, CompletionContext, TRUE, TRUE, TRUE);
    InterlockedIncrement(&Context->IoOutstanding);
    IoCallDriver(Context->Pdo, Irp);
}

static
VOID
A2dpQueueEvent(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONG Type,
    _In_ L2CAP_CHANNEL_HANDLE Handle,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PA2DP_EVENT Event;
    KIRQL OldIrql;

    Event = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(A2DP_EVENT, Data[Length]), BTHA2DP_TAG);
    if (!Event)
        return;
    Event->Type = Type;
    Event->Handle = Handle;
    Event->Length = Length;
    if (Length)
        RtlCopyMemory(Event->Data, Data, Length);
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    InsertTailList(&Context->Events, &Event->Link);
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    KeSetEvent(&Context->Wake, IO_NO_INCREMENT, FALSE);
}

static
VOID
A2dpChannelIndication(
    _In_ PVOID IndicationContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    PA2DP_CONTEXT Context = IndicationContext;

    if (Indication == IndicationRemoteDisconnect)
        A2dpQueueEvent(Context, A2DP_EVENT_DISCONNECT, Parameters->ConnectionHandle, NULL, 0);
}

static
VOID
A2dpPrepareOpen(
    _In_ PA2DP_CONTEXT Context,
    _Inout_ struct _BRB_L2CA_OPEN_CHANNEL *Open)
{
    Open->BtAddress = Context->Address;
    Open->ChannelFlags = CF_ROLE_EITHER | CF_LINK_AUTHENTICATED | CF_LINK_ENCRYPTED;
    Open->CallbackFlags = CALLBACK_DISCONNECT;
    Open->Callback = A2dpChannelIndication;
    Open->CallbackContext = Context;
    Open->ReferenceObject = Context->Pdo;
    Open->IncomingQueueDepth = 8;
}

static
NTSTATUS
NTAPI
A2dpAsyncComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID CompletionContext)
{
    PA2DP_TRANSFER Transfer = CompletionContext;
    PA2DP_CONTEXT Context = Transfer->Context;
    struct _BRB_L2CA_OPEN_CHANNEL *Open = &Transfer->Brb.BrbL2caOpenChannel;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Open->Hdr.Type == BRB_L2CA_OPEN_CHANNEL_RESPONSE && NT_SUCCESS(Irp->IoStatus.Status) &&
        Open->Response == CONNECT_RSP_RESULT_SUCCESS)
    {
        A2dpQueueEvent(Context, A2DP_EVENT_CONNECT, Open->ChannelHandle, (const UCHAR *)&Open->OutResults.Params.Mtu,
                       sizeof(Open->OutResults.Params.Mtu));
    }
    IoFreeIrp(Irp);
    ExFreePoolWithTag(Transfer, BTHA2DP_TAG);
    A2dpIoDone(Context);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
A2dpServerIndication(
    _In_ PVOID IndicationContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    PA2DP_CONTEXT Context = IndicationContext;
    struct _BRB_L2CA_OPEN_CHANNEL *Open;
    PA2DP_TRANSFER Transfer;
    BOOLEAN Accept;

    if (Indication != IndicationRemoteConnect || Parameters->BtAddress != Context->Address ||
        Parameters->Parameters.Connect.Request.PSM != PSM_AVDTP)
        return;
    Accept = !Context->Stopping && (!Context->Signaling || !Context->Transport);
    Transfer = ExAllocatePoolZero(NonPagedPool, FIELD_OFFSET(A2DP_TRANSFER, Buffer), BTHA2DP_TAG);
    if (!Transfer)
        return;
    Transfer->Context = Context;
    Transfer->Irp = IoAllocateIrp(Context->Pdo->StackSize, FALSE);
    if (!Transfer->Irp)
    {
        ExFreePoolWithTag(Transfer, BTHA2DP_TAG);
        return;
    }
    Context->Profile.BthInitializeBrb(&Transfer->Brb, BRB_L2CA_OPEN_CHANNEL_RESPONSE);
    Open = &Transfer->Brb.BrbL2caOpenChannel;
    A2dpPrepareOpen(Context, Open);
    Open->ChannelHandle = Parameters->ConnectionHandle;
    Open->Response = Accept ? CONNECT_RSP_RESULT_SUCCESS : CONNECT_RSP_RESULT_NO_RESOURCES;
    Open->ResponseStatus = CONNECT_RSP_STATUS_NO_INFORMATION;
    A2dpSubmitAsync(Context, Transfer->Irp, &Transfer->Brb, A2dpAsyncComplete, Transfer);
}

static
BOOLEAN
A2dpPostRead(
    _In_ PA2DP_TRANSFER Transfer)
{
    PA2DP_CONTEXT Context = Transfer->Context;
    struct _BRB_L2CA_ACL_TRANSFER *Acl = &Transfer->Brb.BrbL2caAclTransfer;

    if (Context->Stopping || !Transfer->Handle)
        return FALSE;
    Context->Profile.BthReuseBrb(&Transfer->Brb, BRB_L2CA_ACL_TRANSFER);
    Acl->BtAddress = Context->Address;
    Acl->ChannelHandle = Transfer->Handle;
    Acl->TransferFlags = ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK;
    Acl->BufferSize = sizeof(Transfer->Buffer);
    Acl->Buffer = Transfer->Buffer;
    A2dpSubmitAsync(Context, Transfer->Irp, &Transfer->Brb, A2dpReadComplete, Transfer);
    return TRUE;
}

static
NTSTATUS
NTAPI
A2dpReadComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID CompletionContext)
{
    PA2DP_TRANSFER Transfer = CompletionContext;
    PA2DP_CONTEXT Context = Transfer->Context;
    ULONG Length = Transfer->Brb.BrbL2caAclTransfer.BufferSize;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (NT_SUCCESS(Irp->IoStatus.Status) && Length)
        A2dpQueueEvent(Context, A2DP_EVENT_DATA, Transfer->Handle, Transfer->Buffer, Length);
    if (!NT_SUCCESS(Irp->IoStatus.Status) || !A2dpPostRead(Transfer))
        InterlockedExchange(&Transfer->Busy, 0);
    A2dpIoDone(Context);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
A2dpStartReads(
    _In_ PA2DP_CONTEXT Context)
{
    ULONG Index;

    for (Index = 0; Index < A2DP_READS; Index++)
    {
        PA2DP_TRANSFER Transfer = &Context->Reads[Index];

        if (InterlockedCompareExchange(&Transfer->Busy, 1, 0) != 0)
            continue;
        Transfer->Handle = Context->Signaling;
        if (!A2dpPostRead(Transfer))
            InterlockedExchange(&Transfer->Busy, 0);
    }
}

static
NTSTATUS
NTAPI
A2dpSendComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID CompletionContext)
{
    PA2DP_TRANSFER Transfer = CompletionContext;
    PA2DP_CONTEXT Context = Transfer->Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    InterlockedExchange(&Transfer->Busy, 0);
    A2dpIoDone(Context);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
A2dpSendSync(
    _In_ PA2DP_CONTEXT Context,
    _In_ L2CAP_CHANNEL_HANDLE Handle,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    struct _BRB_L2CA_ACL_TRANSFER *Acl;
    PUCHAR Copy;
    NTSTATUS Status;
    BRB Brb;

    if (!Handle)
        return STATUS_DEVICE_NOT_CONNECTED;
    Copy = ExAllocatePoolWithTag(NonPagedPool, Length, BTHA2DP_TAG);
    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Data, Length);
    Context->Profile.BthInitializeBrb(&Brb, BRB_L2CA_ACL_TRANSFER);
    Acl = &Brb.BrbL2caAclTransfer;
    Acl->BtAddress = Context->Address;
    Acl->ChannelHandle = Handle;
    Acl->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
    Acl->BufferSize = Length;
    Acl->Buffer = Copy;
    Status = A2dpSubmitSync(Context, &Brb);
    ExFreePoolWithTag(Copy, BTHA2DP_TAG);
    return Status;
}

static
VOID
A2dpCloseChannel(
    _In_ PA2DP_CONTEXT Context,
    _In_ L2CAP_CHANNEL_HANDLE Handle)
{
    BRB Brb;

    if (!Handle)
        return;
    Context->Profile.BthInitializeBrb(&Brb, BRB_L2CA_CLOSE_CHANNEL);
    Brb.BrbL2caCloseChannel.BtAddress = Context->Address;
    Brb.BrbL2caCloseChannel.ChannelHandle = Handle;
    A2dpSubmitSync(Context, &Brb);
}

static
NTSTATUS
A2dpOpenChannel(
    _In_ PA2DP_CONTEXT Context,
    _Out_ L2CAP_CHANNEL_HANDLE *Handle,
    _Out_opt_ PUSHORT Mtu)
{
    struct _BRB_L2CA_OPEN_CHANNEL *Open;
    NTSTATUS Status;
    BRB Brb;

    *Handle = NULL;
    Context->Profile.BthInitializeBrb(&Brb, BRB_L2CA_OPEN_CHANNEL);
    Open = &Brb.BrbL2caOpenChannel;
    A2dpPrepareOpen(Context, Open);
    Open->Psm = PSM_AVDTP;
    {
        IO_STATUS_BLOCK IoStatus;
        PIO_STACK_LOCATION IoStack;
        KEVENT Event;
        PIRP Irp;

        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_BTH_SUBMIT_BRB, Context->Pdo, NULL, 0, NULL, 0, TRUE,
                                            &Event, &IoStatus);
        if (!Irp)
            return STATUS_INSUFFICIENT_RESOURCES;
        IoStack = IoGetNextIrpStackLocation(Irp);
        IoStack->Parameters.Others.Argument1 = &Brb;
        Status = IoCallDriver(Context->Pdo, Irp);
        if (Status == STATUS_PENDING)
        {
            LARGE_INTEGER Timeout;

            Timeout.QuadPart = -10 * 10000;
            while (KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
                A2dpPump(Context);
            Status = IoStatus.Status;
        }
    }
    if (!NT_SUCCESS(Status))
        return Status;
    *Handle = Open->ChannelHandle;
    if (Mtu)
        *Mtu = Open->OutResults.Params.Mtu;
    return STATUS_SUCCESS;
}

static
VOID
A2dpResetStream(
    _In_ PA2DP_CONTEXT Context)
{
    L2CAP_CHANNEL_HANDLE Transport = Context->Transport;

    Context->Transport = NULL;
    Context->TransportMtu = 0;
    Context->State = A2DP_STATE_IDLE;
    Context->RemoteSeid = 0;
    Context->DelayReporting = FALSE;
    Context->PacketLength = 0;
    Context->PacketFrames = 0;
    if (Transport)
        A2dpCloseChannel(Context, Transport);
}

static
VOID
A2dpDisconnect(
    _In_ PA2DP_CONTEXT Context)
{
    L2CAP_CHANNEL_HANDLE Signaling = Context->Signaling;

    A2dpResetStream(Context);
    Context->Signaling = NULL;
    Context->PendingWaiting = FALSE;
    Context->FragmentLength = 0;
    if (Signaling)
        A2dpCloseChannel(Context, Signaling);
}

static
NTSTATUS
A2dpSendSignal(
    _In_ PA2DP_CONTEXT Context,
    _In_ UCHAR Label,
    _In_ UCHAR MessageType,
    _In_ UCHAR Signal,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ ULONG Length)
{
    UCHAR Message[2 + 64];

    if (Length > sizeof(Message) - 2)
        return STATUS_INVALID_BUFFER_SIZE;
    Message[0] = (UCHAR)((Label << 4) | (AVDTP_PACKET_SINGLE << 2) | MessageType);
    Message[1] = Signal & 0x3F;
    if (Length)
        RtlCopyMemory(Message + 2, Parameters, Length);
    return A2dpSendSync(Context, Context->Signaling, Message, 2 + Length);
}

static
VOID
A2dpReject(
    _In_ PA2DP_CONTEXT Context,
    _In_ UCHAR Label,
    _In_ UCHAR Signal,
    _In_ UCHAR Prefix,
    _In_ BOOLEAN HasPrefix,
    _In_ UCHAR Error)
{
    UCHAR Parameters[2];

    Parameters[0] = HasPrefix ? Prefix : Error;
    Parameters[1] = Error;
    A2dpSendSignal(Context, Label, AVDTP_MESSAGE_REJECT, Signal, Parameters, HasPrefix ? 2 : 1);
}

static
ULONG
A2dpBuildCapabilities(
    _Out_writes_bytes_(16) PUCHAR Output,
    _In_ BOOLEAN All)
{
    ULONG Length = 0;

    Output[Length++] = AVDTP_CATEGORY_MEDIA_TRANSPORT;
    Output[Length++] = 0;
    Output[Length++] = AVDTP_CATEGORY_MEDIA_CODEC;
    Output[Length++] = 6;
    Output[Length++] = AVDTP_MEDIA_AUDIO << 4;
    Output[Length++] = AVDTP_CODEC_SBC;
    Output[Length++] = A2DP_SBC_48000 | A2DP_SBC_MONO | A2DP_SBC_DUAL | A2DP_SBC_STEREO | A2DP_SBC_JOINT;
    Output[Length++] = A2DP_SBC_BLOCKS_4 | A2DP_SBC_BLOCKS_8 | A2DP_SBC_BLOCKS_12 | A2DP_SBC_BLOCKS_16 |
                       A2DP_SBC_SUBBANDS_8 | A2DP_SBC_SNR | A2DP_SBC_LOUDNESS;
    Output[Length++] = A2DP_SBC_MIN_BITPOOL;
    Output[Length++] = A2DP_SBC_MAX_BITPOOL;
    if (All)
    {
        Output[Length++] = AVDTP_CATEGORY_DELAY_REPORTING;
        Output[Length++] = 0;
    }
    return Length;
}

static
UCHAR
A2dpSingleBit(
    _In_ UCHAR Value)
{
    return Value && !(Value & (Value - 1));
}

static
BOOLEAN
A2dpApplyCodec(
    _In_ PA2DP_CONTEXT Context,
    _In_reads_(4) const UCHAR *Codec)
{
    static const UCHAR Blocks[] = { 4, 8, 12, 16 };
    UCHAR Mode, BlockIndex;

    if (!(Codec[0] & A2DP_SBC_48000) || !A2dpSingleBit(Codec[0] & 0xF0) || !A2dpSingleBit(Codec[0] & 0x0F) ||
        !A2dpSingleBit(Codec[1] & 0xF0) || (Codec[1] & 0x0C) != A2DP_SBC_SUBBANDS_8 ||
        !A2dpSingleBit(Codec[1] & 0x03) || Codec[2] < A2DP_SBC_MIN_BITPOOL || Codec[3] < Codec[2] ||
        Codec[2] > A2DP_SBC_MAX_BITPOOL)
    {
        return FALSE;
    }
    Mode = (Codec[0] & A2DP_SBC_JOINT) ? SBC_MODE_JOINT_STEREO : (Codec[0] & A2DP_SBC_STEREO) ? SBC_MODE_STEREO :
           (Codec[0] & A2DP_SBC_DUAL) ? SBC_MODE_DUAL_CHANNEL : SBC_MODE_MONO;
    BlockIndex = (Codec[1] & A2DP_SBC_BLOCKS_4) ? 0 : (Codec[1] & A2DP_SBC_BLOCKS_8) ? 1 :
                 (Codec[1] & A2DP_SBC_BLOCKS_12) ? 2 : 3;
    if (!SbcInitialize(&Context->Encoder, SBC_FREQUENCY_48000, Blocks[BlockIndex], Mode,
                       (Codec[1] & A2DP_SBC_SNR) ? SBC_ALLOCATION_SNR : SBC_ALLOCATION_LOUDNESS,
                       (UCHAR)min(Codec[3], A2DP_SBC_MAX_BITPOOL)))
    {
        return FALSE;
    }
    RtlCopyMemory(Context->Codec, Codec, 4);
    Context->FrameLength = SbcFrameLength(&Context->Encoder);
    Context->FrameSamples = SbcSamplesPerFrame(&Context->Encoder);
    DPRINT1("BTHA2DP: SBC 48 kHz mode %u, %u blocks, bitpool %u, frame %lu bytes\n", Mode, Blocks[BlockIndex],
            Context->Encoder.Bitpool, Context->FrameLength);
    return TRUE;
}

static
UCHAR
A2dpParseConfiguration(
    _In_ PA2DP_CONTEXT Context,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PUCHAR Category,
    _In_ BOOLEAN Reconfigure)
{
    BOOLEAN Transport = FALSE, Codec = FALSE, Delay = FALSE;
    ULONG Offset = 0;

    *Category = 0;
    while (Offset + 2 <= Length)
    {
        UCHAR Type = Data[Offset], Size = Data[Offset + 1];

        if (Offset + 2 + Size > Length)
            return AVDTP_ERROR_BAD_LENGTH;
        *Category = Type;
        switch (Type)
        {
            case AVDTP_CATEGORY_MEDIA_TRANSPORT:
                if (Reconfigure)
                    return AVDTP_ERROR_INVALID_CAPABILITIES;
                if (Size)
                    return AVDTP_ERROR_BAD_LENGTH;
                Transport = TRUE;
                break;

            case AVDTP_CATEGORY_MEDIA_CODEC:
                if (Size != 6 || (Data[Offset + 2] >> 4) != AVDTP_MEDIA_AUDIO || Data[Offset + 3] != AVDTP_CODEC_SBC ||
                    !A2dpApplyCodec(Context, Data + Offset + 4))
                    return AVDTP_ERROR_UNSUPPORTED_CONFIG;
                Codec = TRUE;
                break;

            case AVDTP_CATEGORY_DELAY_REPORTING:
                if (Reconfigure)
                    return AVDTP_ERROR_INVALID_CAPABILITIES;
                Delay = TRUE;
                break;

            default:
                return Type > AVDTP_CATEGORY_DELAY_REPORTING || !Type ? AVDTP_ERROR_BAD_SERV_CATEGORY :
                                                                        AVDTP_ERROR_UNSUPPORTED_CONFIG;
        }
        Offset += 2 + Size;
    }
    if (Offset != Length)
        return AVDTP_ERROR_BAD_LENGTH;
    *Category = 0;
    if (!Codec || (!Reconfigure && !Transport))
        return AVDTP_ERROR_BAD_PAYLOAD_FORMAT;
    if (!Reconfigure)
        Context->DelayReporting = Delay;
    return 0;
}

static
VOID
A2dpHandleCommand(
    _In_ PA2DP_CONTEXT Context,
    _In_ UCHAR Label,
    _In_ UCHAR Signal,
    _In_reads_bytes_(Length) const UCHAR *Parameters,
    _In_ ULONG Length)
{
    UCHAR Response[16], Seid = Length ? Parameters[0] >> 2 : 0, Category;
    ULONG ResponseLength = 0;
    UCHAR Error;

    switch (Signal)
    {
        case AVDTP_DISCOVER:
            Response[0] = (A2DP_LOCAL_SEID << 2) | (Context->State != A2DP_STATE_IDLE ? 0x02 : 0);
            Response[1] = (AVDTP_MEDIA_AUDIO << 4);
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, Response, 2);
            return;

        case AVDTP_GET_CAPABILITIES:
        case AVDTP_GET_ALL_CAPABILITIES:
            if (Length != 1)
            {
                A2dpReject(Context, Label, Signal, 0, FALSE, AVDTP_ERROR_BAD_LENGTH);
                return;
            }
            if (Seid != A2DP_LOCAL_SEID)
            {
                A2dpReject(Context, Label, Signal, 0, FALSE, AVDTP_ERROR_BAD_ACP_SEID);
                return;
            }
            ResponseLength = A2dpBuildCapabilities(Response, Signal == AVDTP_GET_ALL_CAPABILITIES);
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, Response, ResponseLength);
            return;

        case AVDTP_SET_CONFIGURATION:
            if (Length < 2)
            {
                A2dpReject(Context, Label, Signal, 0, TRUE, AVDTP_ERROR_BAD_LENGTH);
                return;
            }
            if (Seid != A2DP_LOCAL_SEID)
            {
                A2dpReject(Context, Label, Signal, 0, TRUE, AVDTP_ERROR_BAD_ACP_SEID);
                return;
            }
            if (Context->State != A2DP_STATE_IDLE)
            {
                A2dpReject(Context, Label, Signal, 0, TRUE, AVDTP_ERROR_SEP_IN_USE);
                return;
            }
            Error = A2dpParseConfiguration(Context, Parameters + 2, Length - 2, &Category, FALSE);
            if (Error)
            {
                A2dpReject(Context, Label, Signal, Category, TRUE, Error);
                return;
            }
            Context->RemoteSeid = Parameters[1] >> 2;
            Context->State = A2DP_STATE_CONFIGURED;
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            DPRINT1("BTHA2DP: %012I64x configured our stream (remote SEID %u)\n", Context->Address, Context->RemoteSeid);
            return;

        case AVDTP_GET_CONFIGURATION:
            if (Seid != A2DP_LOCAL_SEID || Context->State == A2DP_STATE_IDLE)
            {
                A2dpReject(Context, Label, Signal, 0, FALSE, Seid != A2DP_LOCAL_SEID ? AVDTP_ERROR_BAD_ACP_SEID :
                                                                                      AVDTP_ERROR_BAD_STATE);
                return;
            }
            Response[0] = AVDTP_CATEGORY_MEDIA_TRANSPORT;
            Response[1] = 0;
            Response[2] = AVDTP_CATEGORY_MEDIA_CODEC;
            Response[3] = 6;
            Response[4] = AVDTP_MEDIA_AUDIO << 4;
            Response[5] = AVDTP_CODEC_SBC;
            RtlCopyMemory(Response + 6, Context->Codec, 4);
            ResponseLength = 10;
            if (Context->DelayReporting)
            {
                Response[10] = AVDTP_CATEGORY_DELAY_REPORTING;
                Response[11] = 0;
                ResponseLength = 12;
            }
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, Response, ResponseLength);
            return;

        case AVDTP_RECONFIGURE:
            if (Seid != A2DP_LOCAL_SEID || Context->State != A2DP_STATE_OPEN)
            {
                A2dpReject(Context, Label, Signal, 0, TRUE, Seid != A2DP_LOCAL_SEID ? AVDTP_ERROR_BAD_ACP_SEID :
                                                                                     AVDTP_ERROR_SEP_NOT_IN_USE);
                return;
            }
            Error = A2dpParseConfiguration(Context, Parameters + 1, Length - 1, &Category, TRUE);
            if (Error)
            {
                A2dpReject(Context, Label, Signal, Category, TRUE, Error);
                return;
            }
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            return;

        case AVDTP_OPEN:
            if (Seid != A2DP_LOCAL_SEID || Context->State != A2DP_STATE_CONFIGURED)
            {
                A2dpReject(Context, Label, Signal, 0, FALSE, Seid != A2DP_LOCAL_SEID ? AVDTP_ERROR_BAD_ACP_SEID :
                                                                                      AVDTP_ERROR_BAD_STATE);
                return;
            }
            Context->State = A2DP_STATE_OPEN;
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            return;

        case AVDTP_START:
            if (Seid != A2DP_LOCAL_SEID || Context->State != A2DP_STATE_OPEN || !Context->Transport)
            {
                A2dpReject(Context, Label, Signal, Length ? Parameters[0] : 0, TRUE,
                           Seid != A2DP_LOCAL_SEID ? AVDTP_ERROR_BAD_ACP_SEID : AVDTP_ERROR_BAD_STATE);
                return;
            }
            Context->State = A2DP_STATE_STREAMING;
            Context->IdleSince = A2dpNowMs();
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            return;

        case AVDTP_SUSPEND:
            if (Seid != A2DP_LOCAL_SEID || Context->State != A2DP_STATE_STREAMING)
            {
                A2dpReject(Context, Label, Signal, Length ? Parameters[0] : 0, TRUE,
                           Seid != A2DP_LOCAL_SEID ? AVDTP_ERROR_BAD_ACP_SEID : AVDTP_ERROR_BAD_STATE);
                return;
            }
            Context->State = A2DP_STATE_OPEN;
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            return;

        case AVDTP_CLOSE:
            if (Seid != A2DP_LOCAL_SEID || Context->State < A2DP_STATE_OPEN)
            {
                A2dpReject(Context, Label, Signal, 0, FALSE, Seid != A2DP_LOCAL_SEID ? AVDTP_ERROR_BAD_ACP_SEID :
                                                                                      AVDTP_ERROR_BAD_STATE);
                return;
            }
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            A2dpResetStream(Context);
            return;

        case AVDTP_ABORT:
            if (Seid != A2DP_LOCAL_SEID)
                return;
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            A2dpResetStream(Context);
            return;

        case AVDTP_DELAYREPORT:
            if (Seid != A2DP_LOCAL_SEID || Length < 3)
            {
                A2dpReject(Context, Label, Signal, 0, FALSE, Seid != A2DP_LOCAL_SEID ? AVDTP_ERROR_BAD_ACP_SEID :
                                                                                      AVDTP_ERROR_BAD_LENGTH);
                return;
            }
            DPRINT1("BTHA2DP: sink delay %u.%u ms\n", ((Parameters[1] << 8) | Parameters[2]) / 10,
                    ((Parameters[1] << 8) | Parameters[2]) % 10);
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_ACCEPT, Signal, NULL, 0);
            return;

        case AVDTP_SECURITY_CONTROL:
            A2dpReject(Context, Label, Signal, 0, FALSE, AVDTP_ERROR_BAD_STATE);
            return;

        default:
            A2dpSendSignal(Context, Label, AVDTP_MESSAGE_GENERAL_REJECT, Signal, NULL, 0);
            return;
    }
}

static
VOID
A2dpHandleSignal(
    _In_ PA2DP_CONTEXT Context,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Label, PacketType, MessageType, Signal;
    const UCHAR *Message = Data;
    ULONG MessageLength = Length;

    if (!Length)
        return;
    Label = Data[0] >> 4;
    PacketType = (Data[0] >> 2) & 3;
    MessageType = Data[0] & 3;
    if (PacketType == AVDTP_PACKET_START)
    {
        if (Length < 3)
            return;
        Context->Fragment[0] = (UCHAR)(Data[0] & ~0x0C);
        Context->Fragment[1] = Data[2];
        Context->FragmentLength = min(Length - 3, sizeof(Context->Fragment) - 2);
        RtlCopyMemory(Context->Fragment + 2, Data + 3, Context->FragmentLength);
        Context->FragmentLength += 2;
        Context->FragmentPackets = Data[1] ? Data[1] - 1 : 0;
        return;
    }
    if (PacketType == AVDTP_PACKET_CONTINUE || PacketType == AVDTP_PACKET_END)
    {
        ULONG Copy;

        if (!Context->FragmentLength || !Context->FragmentPackets)
            return;
        Copy = min(Length - 1, sizeof(Context->Fragment) - Context->FragmentLength);
        RtlCopyMemory(Context->Fragment + Context->FragmentLength, Data + 1, Copy);
        Context->FragmentLength += Copy;
        Context->FragmentPackets--;
        if (PacketType == AVDTP_PACKET_CONTINUE)
            return;
        Message = Context->Fragment;
        MessageLength = Context->FragmentLength;
        Context->FragmentLength = 0;
    }
    if (MessageLength < 2)
        return;
    Signal = Message[1] & 0x3F;

    if (MessageType == AVDTP_MESSAGE_COMMAND)
    {
        A2dpHandleCommand(Context, Label, Signal, Message + 2, MessageLength - 2);
        return;
    }
    if (!Context->PendingWaiting || Label != Context->PendingLabel || Signal != Context->PendingSignal)
        return;
    Context->ResponseType = MessageType;
    Context->ResponseLength = min(MessageLength - 2, sizeof(Context->Response));
    RtlCopyMemory(Context->Response, Message + 2, Context->ResponseLength);
    Context->PendingWaiting = FALSE;
}

static
VOID
A2dpProcessEvents(
    _In_ PA2DP_CONTEXT Context)
{
    for (;;)
    {
        PA2DP_EVENT Event = NULL;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Context->Lock, &OldIrql);
        if (!IsListEmpty(&Context->Events))
            Event = CONTAINING_RECORD(RemoveHeadList(&Context->Events), A2DP_EVENT, Link);
        KeReleaseSpinLock(&Context->Lock, OldIrql);
        if (!Event)
            return;

        switch (Event->Type)
        {
            case A2DP_EVENT_DATA:
                if (Event->Handle == Context->Signaling)
                    A2dpHandleSignal(Context, Event->Data, Event->Length);
                break;

            case A2DP_EVENT_CONNECT:
                if (!Context->Signaling)
                {
                    Context->Signaling = Event->Handle;
                    DPRINT1("BTHA2DP: %012I64x opened AVDTP signaling\n", Context->Address);
                    A2dpStartReads(Context);
                }
                else if (!Context->Transport && Context->State == A2DP_STATE_OPEN)
                {
                    Context->Transport = Event->Handle;
                    Context->TransportMtu = *(PUSHORT)Event->Data;
                    DPRINT1("BTHA2DP: %012I64x opened the media transport (MTU %u)\n", Context->Address,
                            Context->TransportMtu);
                }
                else
                {
                    A2dpCloseChannel(Context, Event->Handle);
                }
                break;

            case A2DP_EVENT_DISCONNECT:
                if (Event->Handle == Context->Transport)
                {
                    Context->Transport = NULL;
                    A2dpCloseChannel(Context, Event->Handle);
                    if (Context->State >= A2DP_STATE_OPEN)
                        Context->State = A2DP_STATE_IDLE;
                }
                else if (Event->Handle == Context->Signaling)
                {
                    Context->Signaling = NULL;
                    A2dpCloseChannel(Context, Event->Handle);
                    A2dpResetStream(Context);
                    Context->PendingWaiting = FALSE;
                    DPRINT1("BTHA2DP: %012I64x closed AVDTP signaling\n", Context->Address);
                }
                break;
        }
        ExFreePoolWithTag(Event, BTHA2DP_TAG);
    }
}

static
NTSTATUS
A2dpTransact(
    _In_ PA2DP_CONTEXT Context,
    _In_ UCHAR Signal,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ ULONG Length)
{
    ULONGLONG Deadline = A2dpNowMs() + A2DP_SIGNAL_TIMEOUT_MS;
    NTSTATUS Status;

    Context->PendingLabel = Context->NextLabel;
    Context->NextLabel = (Context->NextLabel + 1) & 0x0F;
    Context->PendingSignal = Signal;
    Context->PendingWaiting = TRUE;
    Status = A2dpSendSignal(Context, Context->PendingLabel, AVDTP_MESSAGE_COMMAND, Signal, Parameters, Length);
    if (!NT_SUCCESS(Status))
    {
        Context->PendingWaiting = FALSE;
        return Status;
    }
    while (Context->PendingWaiting && !Context->Stopping && Context->Signaling)
    {
        LARGE_INTEGER Timeout;
        ULONGLONG Now = A2dpNowMs();

        if (Now >= Deadline)
        {
            Context->PendingWaiting = FALSE;
            DPRINT1("BTHA2DP: AVDTP signal 0x%02x timed out\n", Signal);
            return STATUS_IO_TIMEOUT;
        }
        Timeout.QuadPart = -(LONGLONG)min(Deadline - Now, A2dpPump(Context)) * 10000;
        KeWaitForSingleObject(&Context->Wake, Executive, KernelMode, FALSE, &Timeout);
        A2dpProcessEvents(Context);
    }
    if (Context->PendingWaiting)
    {
        Context->PendingWaiting = FALSE;
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    return Context->ResponseType == AVDTP_MESSAGE_ACCEPT ? STATUS_SUCCESS : STATUS_REQUEST_NOT_ACCEPTED;
}

static
BOOLEAN
A2dpChooseConfiguration(
    _In_reads_bytes_(Length) const UCHAR *Capabilities,
    _In_ ULONG Length,
    _Out_writes_(4) PUCHAR Codec,
    _Out_ PBOOLEAN DelayReporting)
{
    BOOLEAN Found = FALSE;
    ULONG Offset = 0;

    *DelayReporting = FALSE;
    while (Offset + 2 <= Length && Offset + 2 + Capabilities[Offset + 1] <= Length)
    {
        const UCHAR *Element = Capabilities + Offset + 2;
        UCHAR Size = Capabilities[Offset + 1];

        if (Capabilities[Offset] == AVDTP_CATEGORY_DELAY_REPORTING)
            *DelayReporting = TRUE;
        if (Capabilities[Offset] == AVDTP_CATEGORY_MEDIA_CODEC && Size == 6 && (Element[0] >> 4) == AVDTP_MEDIA_AUDIO &&
            Element[1] == AVDTP_CODEC_SBC && (Element[2] & A2DP_SBC_48000) && (Element[3] & A2DP_SBC_SUBBANDS_8) &&
            (Element[2] & 0x0F) && (Element[3] & 0xF0) && (Element[3] & 0x03))
        {
            UCHAR Modes = Element[2] & 0x0F, BlockBits = Element[3] & 0xF0, MinBitpool = Element[4], MaxBitpool = Element[5];

            Codec[0] = A2DP_SBC_48000 | ((Modes & A2DP_SBC_JOINT) ? A2DP_SBC_JOINT : (Modes & A2DP_SBC_STEREO) ?
                                         A2DP_SBC_STEREO : (Modes & A2DP_SBC_DUAL) ? A2DP_SBC_DUAL : A2DP_SBC_MONO);
            Codec[1] = ((BlockBits & A2DP_SBC_BLOCKS_16) ? A2DP_SBC_BLOCKS_16 : (BlockBits & A2DP_SBC_BLOCKS_12) ?
                        A2DP_SBC_BLOCKS_12 : (BlockBits & A2DP_SBC_BLOCKS_8) ? A2DP_SBC_BLOCKS_8 : A2DP_SBC_BLOCKS_4) |
                       A2DP_SBC_SUBBANDS_8 | ((Element[3] & A2DP_SBC_LOUDNESS) ? A2DP_SBC_LOUDNESS : A2DP_SBC_SNR);
            Codec[2] = max(MinBitpool, A2DP_SBC_MIN_BITPOOL);
            Codec[3] = (UCHAR)min(MaxBitpool, (Codec[0] & (A2DP_SBC_JOINT | A2DP_SBC_STEREO)) ?
                                              A2DP_SBC_BITPOOL_48000_JOINT : A2DP_SBC_BITPOOL_48000_MONO);
            Found = Codec[3] >= Codec[2];
        }
        Offset += 2 + Size;
    }
    return Found;
}

static
NTSTATUS
A2dpSetupStream(
    _In_ PA2DP_CONTEXT Context)
{
    UCHAR Parameters[16], Seids[32], SeidCount = 0, Index, Codec[4];
    BOOLEAN DelayReporting;
    NTSTATUS Status;
    ULONG Length;

    if (!Context->Signaling)
    {
        L2CAP_CHANNEL_HANDLE Handle;

        Status = A2dpOpenChannel(Context, &Handle, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        A2dpProcessEvents(Context);
        if (Context->Signaling)
        {
            A2dpCloseChannel(Context, Handle);
        }
        else
        {
            Context->Signaling = Handle;
            A2dpStartReads(Context);
        }
    }
    if (Context->State != A2DP_STATE_IDLE)
        return STATUS_SUCCESS;

    Status = A2dpTransact(Context, AVDTP_DISCOVER, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Length = 0; Length + 1 < Context->ResponseLength && SeidCount < sizeof(Seids); Length += 2)
    {
        if (!(Context->Response[Length] & 0x02) && (Context->Response[Length + 1] >> 4) == AVDTP_MEDIA_AUDIO &&
            ((Context->Response[Length + 1] >> 3) & 1) == AVDTP_TSEP_SINK)
            Seids[SeidCount++] = Context->Response[Length] >> 2;
    }
    for (Index = 0; Index < SeidCount && Context->State == A2DP_STATE_IDLE; Index++)
    {
        Parameters[0] = (UCHAR)(Seids[Index] << 2);
        Status = A2dpTransact(Context, AVDTP_GET_ALL_CAPABILITIES, Parameters, 1);
        if (Status == STATUS_REQUEST_NOT_ACCEPTED || (NT_SUCCESS(Status) && !Context->ResponseLength))
            Status = A2dpTransact(Context, AVDTP_GET_CAPABILITIES, Parameters, 1);
        if (!NT_SUCCESS(Status))
            continue;
        if (!A2dpChooseConfiguration(Context->Response, Context->ResponseLength, Codec, &DelayReporting))
            continue;
        if (Context->State != A2DP_STATE_IDLE)
            break;

        Parameters[0] = (UCHAR)(Seids[Index] << 2);
        Parameters[1] = A2DP_LOCAL_SEID << 2;
        Parameters[2] = AVDTP_CATEGORY_MEDIA_TRANSPORT;
        Parameters[3] = 0;
        Parameters[4] = AVDTP_CATEGORY_MEDIA_CODEC;
        Parameters[5] = 6;
        Parameters[6] = AVDTP_MEDIA_AUDIO << 4;
        Parameters[7] = AVDTP_CODEC_SBC;
        RtlCopyMemory(Parameters + 8, Codec, 4);
        Length = 12;
        if (DelayReporting)
        {
            Parameters[12] = AVDTP_CATEGORY_DELAY_REPORTING;
            Parameters[13] = 0;
            Length = 14;
        }
        if (!A2dpApplyCodec(Context, Codec))
            continue;
        Status = A2dpTransact(Context, AVDTP_SET_CONFIGURATION, Parameters, Length);
        if (!NT_SUCCESS(Status))
            continue;
        Context->RemoteSeid = Seids[Index];
        Context->DelayReporting = DelayReporting;
        Context->State = A2DP_STATE_CONFIGURED;
    }
    if (Context->State != A2DP_STATE_CONFIGURED)
        return STATUS_NOT_SUPPORTED;

    Parameters[0] = (UCHAR)(Context->RemoteSeid << 2);
    Status = A2dpTransact(Context, AVDTP_OPEN, Parameters, 1);
    if (!NT_SUCCESS(Status))
    {
        A2dpResetStream(Context);
        return Status;
    }
    Context->State = A2DP_STATE_OPEN;
    {
        L2CAP_CHANNEL_HANDLE Handle;
        USHORT Mtu;

        Status = A2dpOpenChannel(Context, &Handle, &Mtu);
        if (!NT_SUCCESS(Status))
        {
            Parameters[0] = (UCHAR)(Context->RemoteSeid << 2);
            A2dpTransact(Context, AVDTP_ABORT, Parameters, 1);
            A2dpResetStream(Context);
            return Status;
        }
        Context->Transport = Handle;
        Context->TransportMtu = Mtu;
    }
    DPRINT1("BTHA2DP: stream to %012I64x open (remote SEID %u, MTU %u)\n", Context->Address, Context->RemoteSeid,
            Context->TransportMtu);
    return STATUS_SUCCESS;
}

static
VOID
A2dpSetStreaming(
    _In_ PA2DP_CONTEXT Context,
    _In_ BOOLEAN Start)
{
    UCHAR Parameter = (UCHAR)(Context->RemoteSeid << 2);

    if (Start && Context->State == A2DP_STATE_OPEN && Context->Transport)
    {
        if (NT_SUCCESS(A2dpTransact(Context, AVDTP_START, &Parameter, 1)))
        {
            Context->State = A2DP_STATE_STREAMING;
            DPRINT1("BTHA2DP: streaming to %012I64x\n", Context->Address);
        }
    }
    else if (!Start && Context->State == A2DP_STATE_STREAMING)
    {
        if (NT_SUCCESS(A2dpTransact(Context, AVDTP_SUSPEND, &Parameter, 1)))
            Context->State = A2DP_STATE_OPEN;
    }
}

static
VOID
A2dpFlushPacket(
    _In_ PA2DP_CONTEXT Context)
{
    ULONG Index;

    if (!Context->PacketFrames)
        return;
    if (Context->State == A2DP_STATE_STREAMING && Context->Transport)
    {
        for (Index = 0; Index < A2DP_SENDS; Index++)
        {
            PA2DP_TRANSFER Transfer = &Context->Sends[Index];
            struct _BRB_L2CA_ACL_TRANSFER *Acl = &Transfer->Brb.BrbL2caAclTransfer;
            PUCHAR Packet = Transfer->Buffer;

            if (InterlockedCompareExchange(&Transfer->Busy, 1, 0) != 0)
                continue;
            Packet[0] = 0x80;
            Packet[1] = A2DP_RTP_PAYLOAD_TYPE;
            Packet[2] = (UCHAR)(Context->Sequence >> 8);
            Packet[3] = (UCHAR)Context->Sequence;
            Packet[4] = (UCHAR)(Context->PacketTimestamp >> 24);
            Packet[5] = (UCHAR)(Context->PacketTimestamp >> 16);
            Packet[6] = (UCHAR)(Context->PacketTimestamp >> 8);
            Packet[7] = (UCHAR)Context->PacketTimestamp;
            Packet[8] = 0;
            Packet[9] = 0;
            Packet[10] = 0;
            Packet[11] = 1;
            Packet[12] = (UCHAR)Context->PacketFrames;
            RtlCopyMemory(Packet + A2DP_RTP_HEADER + 1, Context->Packet, Context->PacketLength);
            Context->Profile.BthReuseBrb(&Transfer->Brb, BRB_L2CA_ACL_TRANSFER);
            Acl->BtAddress = Context->Address;
            Acl->ChannelHandle = Context->Transport;
            Acl->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
            Acl->BufferSize = A2DP_RTP_HEADER + 1 + Context->PacketLength;
            Acl->Buffer = Packet;
            A2dpSubmitAsync(Context, Transfer->Irp, &Transfer->Brb, A2dpSendComplete, Transfer);
            break;
        }
    }
    Context->Sequence++;
    Context->PacketLength = 0;
    Context->PacketFrames = 0;
}

static
VOID
A2dpEncodeFrame(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONGLONG Offset)
{
    SHORT Pcm[SBC_MAX_BLOCKS * SBC_SUBBANDS * SBC_MAX_CHANNELS];
    UCHAR Frame[SBC_MAX_FRAME_LENGTH];
    LONG Gain[BTHA2DP_CHANNELS];
    ULONG Sample, Channel, Frames = Context->FrameSamples, Length;
    KIRQL OldIrql;
    ULONG Payload = Context->TransportMtu > A2DP_RTP_HEADER + 1 ?
                    min((ULONG)Context->TransportMtu, A2DP_TRANSFER_SIZE) - A2DP_RTP_HEADER - 1 : 0;

    if (Context->State != A2DP_STATE_STREAMING || !Payload)
    {
        Context->Timestamp += Frames;
        return;
    }
    for (Channel = 0; Channel < BTHA2DP_CHANNELS; Channel++)
    {
        LONG Level = Context->Mute ? BTHA2DP_VOLUME_MINIMUM :
                     Context->AbsoluteVolume ? BTHA2DP_VOLUME_MAXIMUM : Context->Volume[Channel];
        LONG Gain16 = 0x10000;
        LONG Decibels = -Level;

        while (Decibels >= 6 * 0x10000 && Gain16)
        {
            Gain16 >>= 1;
            Decibels -= 6 * 0x10000;
        }
        Gain[Channel] = Level <= BTHA2DP_VOLUME_MINIMUM ? 0 :
                        (LONG)(((LONGLONG)Gain16 * (0x10000 - Decibels / 12)) >> 16);
    }
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    if (!Context->Buffer || !Context->BufferSize)
    {
        KeReleaseSpinLock(&Context->Lock, OldIrql);
        Context->Timestamp += Frames;
        return;
    }
    for (Sample = 0; Sample < Frames; Sample++)
    {
        PSHORT Source = (PSHORT)(Context->Buffer + (ULONG)((Offset + Sample * BTHA2DP_BLOCK_ALIGN) % Context->BufferSize));
        LONG Left = (Source[0] * Gain[0]) >> 16, Right = (Source[1] * Gain[1]) >> 16;

        if (Context->Encoder.Channels == 1)
        {
            Pcm[Sample] = (SHORT)((Left + Right) / 2);
        }
        else
        {
            Pcm[Sample * 2] = (SHORT)Left;
            Pcm[Sample * 2 + 1] = (SHORT)Right;
        }
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    Length = SbcEncodeFrame(&Context->Encoder, Pcm, Frame);
    if (Length > Payload)
    {
        Context->Timestamp += Frames;
        return;
    }
    if (Context->PacketFrames &&
        (Context->PacketLength + Length > Payload || Context->PacketFrames == A2DP_MAX_FRAMES_PER_PACKET))
    {
        A2dpFlushPacket(Context);
    }
    if (!Context->PacketFrames)
        Context->PacketTimestamp = Context->Timestamp;
    RtlCopyMemory(Context->Packet + Context->PacketLength, Frame, Length);
    Context->PacketLength += Length;
    Context->PacketFrames++;
    Context->Timestamp += Frames;
    if (Context->PacketLength + Length > Payload || Context->PacketFrames == A2DP_MAX_FRAMES_PER_PACKET)
        A2dpFlushPacket(Context);
}

static
ULONG
A2dpPump(
    _In_ PA2DP_CONTEXT Context)
{
    LARGE_INTEGER Now;
    ULONGLONG Due, Previous, Current;
    KIRQL OldIrql;

    if (!Context->Running || !Context->Buffer || !Context->FrameSamples)
        return 100;
    Now = KeQueryPerformanceCounter(NULL);
    Due = (ULONGLONG)(Now.QuadPart - Context->StartQpc) * BTHA2DP_SAMPLE_RATE / (ULONGLONG)Context->QpcFrequency.QuadPart;
    while (Context->EncodedSamples + Context->FrameSamples <= Due)
    {
        ULONGLONG Offset = Context->BaseBytes + Context->EncodedSamples * BTHA2DP_BLOCK_ALIGN;

        A2dpEncodeFrame(Context, Offset);
        Context->EncodedSamples += Context->FrameSamples;
        Previous = Offset / Context->PeriodBytes;
        Current = (Offset + Context->FrameSamples * BTHA2DP_BLOCK_ALIGN) / Context->PeriodBytes;
        KeAcquireSpinLock(&Context->Lock, &OldIrql);
        Context->PositionBytes = Offset + Context->FrameSamples * BTHA2DP_BLOCK_ALIGN;
        if (Current != Previous && Context->NotificationEvent)
            KeSetEvent(Context->NotificationEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Context->Lock, OldIrql);
    }
    return (ULONG)max(1, (Context->EncodedSamples + Context->FrameSamples - Due) * 1000 / BTHA2DP_SAMPLE_RATE);
}

static
VOID
NTAPI
A2dpWorker(
    _In_ PVOID StartContext)
{
    PA2DP_CONTEXT Context = StartContext;

    while (!Context->Stopping)
    {
        LARGE_INTEGER Timeout;
        ULONG WaitMs;
        ULONGLONG Now;

        A2dpProcessEvents(Context);
        Now = A2dpNowMs();
        if ((Context->Running || Context->ConnectAttempts < A2DP_CONNECT_ATTEMPTS) &&
            Context->State < A2DP_STATE_OPEN && Now >= Context->RetryAfter)
        {
            if (NT_SUCCESS(A2dpSetupStream(Context)))
            {
                Context->ConnectAttempts = A2DP_CONNECT_ATTEMPTS;
            }
            else
            {
                Context->ConnectAttempts++;
                Context->RetryAfter = A2dpNowMs() + A2DP_RECONNECT_DELAY_MS;
            }
        }
        if (Context->Running)
        {
            Context->IdleSince = Now;
            if (Context->State == A2DP_STATE_OPEN)
                A2dpSetStreaming(Context, TRUE);
        }
        else if (Context->State == A2DP_STATE_STREAMING && Now >= Context->IdleSince + A2DP_SUSPEND_DELAY_MS)
        {
            A2dpFlushPacket(Context);
            A2dpSetStreaming(Context, FALSE);
        }
        WaitMs = A2dpPump(Context);
        Timeout.QuadPart = -(LONGLONG)min(WaitMs, 100) * 10000;
        KeWaitForSingleObject(&Context->Wake, Executive, KernelMode, FALSE, &Timeout);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
A2dpQueryProfileInterface(
    _In_ PA2DP_CONTEXT Context)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Context->Pdo, NULL, 0, NULL, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IoStack->Parameters.QueryInterface.InterfaceType = &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE;
    IoStack->Parameters.QueryInterface.Size = sizeof(Context->Profile);
    IoStack->Parameters.QueryInterface.Version = BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI;
    IoStack->Parameters.QueryInterface.Interface = (PINTERFACE)&Context->Profile;
    IoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Status = IoCallDriver(Context->Pdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
VOID
A2dpPublishRecord(
    _In_ PA2DP_CONTEXT Context)
{
    static const UCHAR Record[] =
    {
        0x35, 0x38,
        0x09, 0x00, 0x01, 0x35, 0x03, 0x19, 0x11, 0x0A,
        0x09, 0x00, 0x04, 0x35, 0x10,
        0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x19,
        0x35, 0x06, 0x19, 0x00, 0x19, 0x09, 0x01, 0x03,
        0x09, 0x00, 0x05, 0x35, 0x03, 0x19, 0x10, 0x02,
        0x09, 0x00, 0x09, 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0D, 0x09, 0x01, 0x03,
        0x09, 0x03, 0x11, 0x09, 0x00, 0x01
    };
    HANDLE_SDP Handle = 0;

    if (NT_SUCCESS(A2dpCallPdoBuffered(Context, IOCTL_BTH_SDP_SUBMIT_RECORD, (PVOID)Record, sizeof(Record), &Handle,
                                       sizeof(Handle))))
        Context->SdpRecord = Handle;
}

static
NTSTATUS
A2dpRegister(
    _In_ PA2DP_CONTEXT Context)
{
    struct _BRB_L2CA_REGISTER_SERVER *Server;
    BTH_DEVICE_INFO DeviceInfo;
    NTSTATUS Status;
    BRB Brb;

    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    Status = A2dpCallPdo(Context, IOCTL_INTERNAL_BTHENUM_GET_DEVINFO, &DeviceInfo, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Context->Address = DeviceInfo.address;

    Context->Profile.BthInitializeBrb(&Brb, BRB_REGISTER_PSM);
    Brb.BrbPsm.Psm = PSM_AVDTP;
    Status = A2dpSubmitSync(Context, &Brb);
    if (!NT_SUCCESS(Status))
        return Status;
    Context->PsmRegistered = TRUE;

    Context->Profile.BthInitializeBrb(&Brb, BRB_L2CA_REGISTER_SERVER);
    Server = &Brb.BrbL2caRegisterServer;
    Server->BtAddress = Context->Address;
    Server->IndicationCallback = A2dpServerIndication;
    Server->IndicationCallbackContext = Context;
    Server->ReferenceObject = Context->Pdo;
    Status = A2dpSubmitSync(Context, &Brb);
    if (NT_SUCCESS(Status))
        Context->Server = Server->ServerHandle;
    A2dpPublishRecord(Context);
    return Status;
}

static
VOID
A2dpUnregister(
    _In_ PA2DP_CONTEXT Context)
{
    BRB Brb;

    if (Context->SdpRecord)
        A2dpCallPdoBuffered(Context, IOCTL_BTH_SDP_REMOVE_RECORD, &Context->SdpRecord, sizeof(Context->SdpRecord), NULL,
                            0);
    Context->SdpRecord = 0;
    if (Context->Server)
    {
        Context->Profile.BthInitializeBrb(&Brb, BRB_L2CA_UNREGISTER_SERVER);
        Brb.BrbL2caUnregisterServer.BtAddress = Context->Address;
        Brb.BrbL2caUnregisterServer.ServerHandle = Context->Server;
        A2dpSubmitSync(Context, &Brb);
        Context->Server = NULL;
    }
    if (Context->PsmRegistered)
    {
        Context->Profile.BthInitializeBrb(&Brb, BRB_UNREGISTER_PSM);
        Brb.BrbPsm.Psm = PSM_AVDTP;
        A2dpSubmitSync(Context, &Brb);
        Context->PsmRegistered = FALSE;
    }
}

NTSTATUS
A2dpCreate(
    _In_ PDEVICE_OBJECT PhysicalDevice,
    _Out_ PA2DP_CONTEXT *Created)
{
    OBJECT_ATTRIBUTES Attributes;
    PA2DP_CONTEXT Context;
    HANDLE Thread;
    ULONG Index;
    NTSTATUS Status;

    *Created = NULL;
    Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), BTHA2DP_TAG);
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    Context->Pdo = PhysicalDevice;
    KeInitializeSpinLock(&Context->Lock);
    InitializeListHead(&Context->Events);
    KeInitializeEvent(&Context->Wake, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->IoDrained, NotificationEvent, FALSE);
    KeQueryPerformanceCounter(&Context->QpcFrequency);
    for (Index = 0; Index < A2DP_READS; Index++)
    {
        Context->Reads[Index].Context = Context;
        Context->Reads[Index].Irp = IoAllocateIrp(PhysicalDevice->StackSize, FALSE);
    }
    for (Index = 0; Index < A2DP_SENDS; Index++)
    {
        Context->Sends[Index].Context = Context;
        Context->Sends[Index].Irp = IoAllocateIrp(PhysicalDevice->StackSize, FALSE);
    }

    Context->SentLevel = -1;
    Status = A2dpQueryProfileInterface(Context);
    if (NT_SUCCESS(Status))
    {
        Context->HaveProfile = TRUE;
        Status = A2dpRegister(Context);
    }
    if (NT_SUCCESS(Status) && NT_SUCCESS(BthOpenVolumeCallback(BTH_VOLUME_SET_CALLBACK, &Context->VolumeSetCallback)) &&
        NT_SUCCESS(BthOpenVolumeCallback(BTH_VOLUME_STATE_CALLBACK, &Context->VolumeStateCallback)))
    {
        Context->VolumeStateRegistration = ExRegisterCallback(Context->VolumeStateCallback, A2dpVolumeState, Context);
    }
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, A2dpWorker, Context);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Thread, THREAD_ALL_ACCESS, *PsThreadType, KernelMode,
                                           (PVOID *)&Context->Thread, NULL);
        ZwClose(Thread);
    }
    if (!NT_SUCCESS(Status))
    {
        A2dpDestroy(Context);
        return Status;
    }
    DPRINT1("BTHA2DP: A2DP source ready for %012I64x\n", Context->Address);
    *Created = Context;
    return STATUS_SUCCESS;
}

VOID
A2dpShutdown(
    _In_ PA2DP_CONTEXT Context)
{
    LARGE_INTEGER Timeout;
    ULONG Index;

    if (Context->Shutdown)
        return;
    Context->Shutdown = TRUE;
    if (Context->VolumeStateRegistration)
        ExUnregisterCallback(Context->VolumeStateRegistration);
    Context->VolumeStateRegistration = NULL;
    if (Context->VolumeStateCallback)
        ObDereferenceObject(Context->VolumeStateCallback);
    Context->VolumeStateCallback = NULL;
    if (Context->VolumeSetCallback)
        ObDereferenceObject(Context->VolumeSetCallback);
    Context->VolumeSetCallback = NULL;
    Context->Stopping = TRUE;
    KeSetEvent(&Context->Wake, IO_NO_INCREMENT, FALSE);
    if (Context->Thread)
    {
        KeWaitForSingleObject(Context->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Context->Thread);
        Context->Thread = NULL;
    }
    if (Context->HaveProfile)
    {
        if (Context->Signaling && Context->State != A2DP_STATE_IDLE && Context->RemoteSeid)
        {
            UCHAR Parameter = (UCHAR)(Context->RemoteSeid << 2);

            A2dpSendSignal(Context, Context->NextLabel, AVDTP_MESSAGE_COMMAND, AVDTP_CLOSE, &Parameter, 1);
        }
        A2dpDisconnect(Context);
        A2dpUnregister(Context);
    }
    Timeout.QuadPart = -(LONGLONG)5000 * 10000;
    KeClearEvent(&Context->IoDrained);
    if (Context->IoOutstanding)
    {
        for (Index = 0; Index < A2DP_READS; Index++)
        {
            if (Context->Reads[Index].Irp)
                IoCancelIrp(Context->Reads[Index].Irp);
        }
        KeWaitForSingleObject(&Context->IoDrained, Executive, KernelMode, FALSE, &Timeout);
    }
    while (!IsListEmpty(&Context->Events))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Context->Events), A2DP_EVENT, Link), BTHA2DP_TAG);
    for (Index = 0; Index < A2DP_READS && !Context->IoOutstanding; Index++)
    {
        if (Context->Reads[Index].Irp)
            IoFreeIrp(Context->Reads[Index].Irp);
        Context->Reads[Index].Irp = NULL;
    }
    for (Index = 0; Index < A2DP_SENDS && !Context->IoOutstanding; Index++)
    {
        if (Context->Sends[Index].Irp)
            IoFreeIrp(Context->Sends[Index].Irp);
        Context->Sends[Index].Irp = NULL;
    }
    if (Context->HaveProfile && Context->Profile.Interface.InterfaceDereference)
        Context->Profile.Interface.InterfaceDereference(Context->Profile.Interface.Context);
    Context->HaveProfile = FALSE;
}

VOID
A2dpDestroy(
    _In_ PA2DP_CONTEXT Context)
{
    A2dpShutdown(Context);
    if (!Context->IoOutstanding)
        ExFreePoolWithTag(Context, BTHA2DP_TAG);
}

NTSTATUS
A2dpAttachBuffer(
    _In_ PA2DP_CONTEXT Context,
    _In_ PUCHAR Buffer,
    _In_ ULONG Size,
    _In_ ULONG NotificationCount)
{
    KIRQL OldIrql;

    if (!Buffer || !Size || Size % BTHA2DP_BLOCK_ALIGN || !NotificationCount || Size % NotificationCount)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->Buffer = Buffer;
    Context->BufferSize = Size;
    Context->PeriodBytes = Size / NotificationCount;
    Context->PositionBytes = 0;
    Context->BaseBytes = 0;
    Context->EncodedSamples = 0;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    return STATUS_SUCCESS;
}

VOID
A2dpDetachBuffer(
    _In_ PA2DP_CONTEXT Context)
{
    KIRQL OldIrql;

    A2dpSetRunning(Context, FALSE, TRUE);
    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->Buffer = NULL;
    Context->BufferSize = 0;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    KeSetEvent(&Context->Wake, IO_NO_INCREMENT, FALSE);
}

VOID
A2dpSetRunning(
    _In_ PA2DP_CONTEXT Context,
    _In_ BOOLEAN Running,
    _In_ BOOLEAN Reset)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    if (Running && !Context->Running)
    {
        Context->BaseBytes = Context->PositionBytes;
        Context->EncodedSamples = 0;
        Context->StartQpc = KeQueryPerformanceCounter(NULL).QuadPart;
    }
    if (!Running && Reset)
        Context->PositionBytes = 0;
    Context->Running = Running;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    KeSetEvent(&Context->Wake, IO_NO_INCREMENT, FALSE);
}

VOID
A2dpGetPosition(
    _In_ PA2DP_CONTEXT Context,
    _Out_ PULONGLONG PlayOffset,
    _Out_ PULONGLONG WriteOffset)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    if (!Context->BufferSize)
    {
        *PlayOffset = 0;
        *WriteOffset = 0;
    }
    else
    {
        *PlayOffset = Context->PositionBytes % Context->BufferSize;
        *WriteOffset = (Context->PositionBytes + Context->FrameSamples * BTHA2DP_BLOCK_ALIGN) % Context->BufferSize;
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);
}

NTSTATUS
A2dpSetNotificationEvent(
    _In_ PA2DP_CONTEXT Context,
    _In_opt_ PKEVENT Event,
    _In_ BOOLEAN Register)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    if (Register)
    {
        if (!Context->NotificationEvent)
            Context->NotificationEvent = Event;
        else if (Context->NotificationEvent != Event)
            Status = STATUS_DEVICE_BUSY;
    }
    else if (Context->NotificationEvent == Event)
    {
        Context->NotificationEvent = NULL;
    }
    else
    {
        Status = STATUS_NOT_FOUND;
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    return Status;
}

static
VOID
NTAPI
A2dpVolumeState(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PA2DP_CONTEXT Context = CallbackContext;
    PBTH_ABSOLUTE_VOLUME Volume = Argument1;
    ULONG Channel;

    UNREFERENCED_PARAMETER(Argument2);
    if (!Context || !Volume || Volume->Address != Context->Address)
        return;
    InterlockedExchange(&Context->AbsoluteVolume, Volume->Supported);
    if (!Volume->Supported)
    {
        InterlockedExchange(&Context->SentLevel, -1);
        return;
    }
    InterlockedExchange(&Context->SentLevel, min(Volume->Level, BTH_VOLUME_MAXIMUM_LEVEL));
    for (Channel = 0; Channel < BTHA2DP_CHANNELS; Channel++)
    {
        InterlockedExchange(&Context->Volume[Channel],
                            BTHA2DP_VOLUME_MINIMUM +
                            (LONG)(((LONGLONG)(BTHA2DP_VOLUME_MAXIMUM - BTHA2DP_VOLUME_MINIMUM) *
                                    min(Volume->Level, BTH_VOLUME_MAXIMUM_LEVEL)) / BTH_VOLUME_MAXIMUM_LEVEL));
    }
}

static
UCHAR
A2dpAbsoluteVolumeFromLevel(
    _In_ LONG Level)
{
    LONGLONG Range = (LONGLONG)BTHA2DP_VOLUME_MAXIMUM - BTHA2DP_VOLUME_MINIMUM;

    return (UCHAR)((((LONGLONG)Level - BTHA2DP_VOLUME_MINIMUM) * BTH_VOLUME_MAXIMUM_LEVEL + Range / 2) / Range);
}

VOID
A2dpSetVolume(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONG Channel,
    _In_ LONG Level)
{
    BTH_ABSOLUTE_VOLUME Volume;

    Level = max(BTHA2DP_VOLUME_MINIMUM, min(BTHA2DP_VOLUME_MAXIMUM, Level));
    if (Channel >= BTHA2DP_CHANNELS)
        return;
    InterlockedExchange(&Context->Volume[Channel], Level);
    if (!Context->AbsoluteVolume || !Context->VolumeSetCallback)
        return;
    Volume.Address = Context->Address;
    Volume.Supported = TRUE;
    Volume.Level = A2dpAbsoluteVolumeFromLevel(max(Context->Volume[0], Context->Volume[1]));
    if (InterlockedExchange(&Context->SentLevel, Volume.Level) != Volume.Level)
        ExNotifyCallback(Context->VolumeSetCallback, &Volume, NULL);
}

LONG
A2dpGetVolume(
    _In_ PA2DP_CONTEXT Context,
    _In_ ULONG Channel)
{
    return Channel < BTHA2DP_CHANNELS ? Context->Volume[Channel] : 0;
}

VOID
A2dpSetMute(
    _In_ PA2DP_CONTEXT Context,
    _In_ BOOLEAN Mute)
{
    InterlockedExchange(&Context->Mute, Mute ? TRUE : FALSE);
}

BOOLEAN
A2dpGetMute(
    _In_ PA2DP_CONTEXT Context)
{
    return Context->Mute != 0;
}

BOOLEAN
A2dpIsConnected(
    _In_ PA2DP_CONTEXT Context)
{
    return Context->Signaling != NULL;
}
