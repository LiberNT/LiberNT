/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth extensible transport miniport and radio definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <bthdef.h>
#include <bthioctl.h>
#include <bthxddi.h>
#include <ntddser.h>

#define BTHMINI_TAG                     'iMhB'
#define BTHMINI_EVENT_READS             2
#define BTHMINI_ACL_READS               2
#define BTHMINI_MAX_EVENT               (2 + 255)
#define BTHMINI_MAX_RX_PACKETS          128
#define BTHMINI_COMMAND_TIMEOUT_MS      5000
#define BTHMINI_INQUIRY_LENGTH          0x04
#define BTHMINI_MAX_DEVICES             64
#define BTHMINI_DEFAULT_COD             0x000104

#define LOCAL_RADIO_DISCOVERABLE        0x0001
#define LOCAL_RADIO_CONNECTABLE         0x0002

#define IOCTL_WINEBTH_RADIO_SET_FLAG            CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_RADIO_START_DISCOVERY     CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_RADIO_STOP_DISCOVERY      CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_AUTH_REGISTER             CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa8, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_RADIO_SEND_AUTH_RESPONSE  CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa9, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_RADIO_START_AUTH          CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xaa, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_RADIO_REMOVE_DEVICE       CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xab, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define BTHMINI_AUTH_DEVICE_NAME                L"\\Device\\WINEBTHAUTH"
#define BTHMINI_AUTH_LINK_NAME                  L"\\??\\WINEBTHAUTH"
#define BTHMINI_PAIRING_TIMEOUT_MS              60000
#define BTHMINI_PROMPT_TIMEOUT_MS               30000

#define BTHMINI_AUTH_NUMERIC_COMPARISON         3
#define BTHMINI_AUTH_PASSKEY_NOTIFICATION       4

DEFINE_GUID(GUID_WINEBTH_AUTHENTICATION_REQUEST, 0xca67235f, 0xf621, 0x4c27, 0x85, 0x65, 0xa4, 0xd5, 0x5e, 0xa1, 0x26, 0xe8);

#pragma pack(push, 1)
typedef struct _BTHMINI_AUTH_REQUEST
{
    BTH_DEVICE_INFO DeviceInfo;
    ULONG Method;
    ULONG NumericValue;
} BTHMINI_AUTH_REQUEST, *PBTHMINI_AUTH_REQUEST;

typedef struct _BTHMINI_AUTH_RESPONSE
{
    BTH_ADDR Address;
    ULONG Method;
    ULONG NumericValue;
    ULONG Negative : 1;
    ULONG Authenticated : 1;
} BTHMINI_AUTH_RESPONSE, *PBTHMINI_AUTH_RESPONSE;
#pragma pack(pop)

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
#define HCI_IO_CAPABILITY_REPLY         HCI_OP(0x01, 0x002B)
#define HCI_USER_CONFIRMATION_REPLY     HCI_OP(0x01, 0x002C)
#define HCI_USER_CONFIRMATION_NEG_REPLY HCI_OP(0x01, 0x002D)
#define HCI_USER_PASSKEY_NEG_REPLY      HCI_OP(0x01, 0x002F)
#define HCI_IO_CAPABILITY_NEG_REPLY     HCI_OP(0x01, 0x0034)
#define HCI_WRITE_DEFAULT_LINK_POLICY   HCI_OP(0x02, 0x000F)
#define HCI_SET_EVENT_MASK              HCI_OP(0x03, 0x0001)
#define HCI_RESET                       HCI_OP(0x03, 0x0003)
#define HCI_WRITE_LOCAL_NAME            HCI_OP(0x03, 0x0013)
#define HCI_WRITE_SCAN_ENABLE           HCI_OP(0x03, 0x001A)
#define HCI_WRITE_CLASS_OF_DEVICE       HCI_OP(0x03, 0x0024)
#define HCI_WRITE_CURRENT_IAC_LAP       HCI_OP(0x03, 0x003A)
#define HCI_WRITE_INQUIRY_MODE          HCI_OP(0x03, 0x0045)
#define HCI_WRITE_EIR                   HCI_OP(0x03, 0x0052)
#define HCI_WRITE_SIMPLE_PAIRING_MODE   HCI_OP(0x03, 0x0056)
#define HCI_READ_LOCAL_VERSION          HCI_OP(0x04, 0x0001)
#define HCI_READ_LOCAL_COMMANDS         HCI_OP(0x04, 0x0002)
#define HCI_READ_LOCAL_FEATURES         HCI_OP(0x04, 0x0003)
#define HCI_READ_BUFFER_SIZE            HCI_OP(0x04, 0x0005)
#define HCI_READ_BD_ADDR                HCI_OP(0x04, 0x0009)

#define HCI_EV_INQUIRY_COMPLETE         0x01
#define HCI_EV_INQUIRY_RESULT           0x02
#define HCI_EV_CONNECTION_COMPLETE      0x03
#define HCI_EV_CONNECTION_REQUEST       0x04
#define HCI_EV_DISCONNECTION_COMPLETE   0x05
#define HCI_EV_AUTHENTICATION_COMPLETE  0x06
#define HCI_EV_REMOTE_NAME_COMPLETE     0x07
#define HCI_EV_ENCRYPTION_CHANGE        0x08
#define HCI_EV_COMMAND_COMPLETE         0x0E
#define HCI_EV_COMMAND_STATUS           0x0F
#define HCI_EV_HARDWARE_ERROR           0x10
#define HCI_EV_NUMBER_OF_COMPLETED      0x13
#define HCI_EV_PIN_CODE_REQUEST         0x16
#define HCI_EV_LINK_KEY_REQUEST         0x17
#define HCI_EV_LINK_KEY_NOTIFICATION    0x18
#define HCI_EV_INQUIRY_RESULT_RSSI      0x22
#define HCI_EV_EXTENDED_INQUIRY_RESULT  0x2F
#define HCI_EV_ENCRYPTION_KEY_REFRESH   0x30
#define HCI_EV_IO_CAPABILITY_REQUEST    0x31
#define HCI_EV_IO_CAPABILITY_RESPONSE   0x32
#define HCI_EV_USER_CONFIRMATION        0x33
#define HCI_EV_USER_PASSKEY_REQUEST     0x34
#define HCI_EV_SIMPLE_PAIRING_COMPLETE  0x36
#define HCI_EV_USER_PASSKEY_NOTIFICATION 0x3B

#define HCI_IO_DISPLAY_YES_NO           0x01
#define HCI_IO_NO_INPUT_NO_OUTPUT       0x03
#define HCI_AUTH_MITM_DEDICATED_BONDING 0x03
#define HCI_PAIRING_NOT_ALLOWED         0x18
#define HCI_REMOTE_USER_TERMINATED      0x13

#define HCI_SCAN_INQUIRY                0x01
#define HCI_SCAN_PAGE                   0x02

#define L2CAP_PSM_SDP                   0x0001
#define L2CAP_PSM_RFCOMM                0x0003
#define L2CAP_DEFAULT_MTU               672
#define L2CAP_LOCAL_MTU                 1013
#define L2CAP_MAX_FRAME                 0xFFFF

#define LMP_FEATURE(Features, Byte, Bit)    (((Features) >> ((Byte) * 8 + (Bit))) & 1)

typedef struct _BTHMINI_RX_PACKET
{
    LIST_ENTRY Link;
    UCHAR Type;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} BTHMINI_RX_PACKET, *PBTHMINI_RX_PACKET;

typedef struct _BTHMINI_COMMAND
{
    LIST_ENTRY Link;
    USHORT Opcode;
    UCHAR Length;
    BOOLEAN Async;
    BOOLEAN Done;
    NTSTATUS Status;
    ULONG ResultLength;
    UCHAR Parameters[255];
    UCHAR Result[255];
} BTHMINI_COMMAND, *PBTHMINI_COMMAND;

typedef struct _BTHMINI_REMOTE
{
    LIST_ENTRY Link;
    BTH_ADDR Address;
    ULONG Flags;
    BTH_COD ClassOfDevice;
    UCHAR PageScanRepetitionMode;
    USHORT ClockOffset;
    USHORT Handle;
    BOOLEAN NameRequested;
    BOOLEAN Connecting;
    BOOLEAN Encrypted;
    BOOLEAN Pairing;
    BOOLEAN HasLinkKey;
    UCHAR LinkKeyType;
    UCHAR LinkKey[16];
    UCHAR RemoteIoCapability;
    UCHAR RemoteAuthentication;
    BOOLEAN Securing;
    WDFREQUEST AuthRequest;
    ULONGLONG AuthDeadline;
    BOOLEAN Prompting;
    ULONG PromptMethod;
    WDFREQUEST PromptResponse;
    ULONGLONG PromptDeadline;
    USHORT AclOutstanding;
    PUCHAR RxFrame;
    ULONG RxExpected;
    ULONG RxLength;
    CHAR Name[BTH_MAX_NAME_SIZE];
} BTHMINI_REMOTE, *PBTHMINI_REMOTE;

typedef struct _BTHMINI_DEVICE BTHMINI_DEVICE, *PBTHMINI_DEVICE;
typedef struct _BTHMINI_CHANNEL BTHMINI_CHANNEL, *PBTHMINI_CHANNEL;

typedef VOID BTHMINI_CHANNEL_EVENT(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_ NTSTATUS Status);

typedef VOID BTHMINI_CHANNEL_RECEIVE(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

typedef struct _BTHMINI_CHANNEL_CALLBACKS
{
    BTHMINI_CHANNEL_EVENT *Opened;
    BTHMINI_CHANNEL_RECEIVE *Receive;
    BTHMINI_CHANNEL_EVENT *Closed;
} BTHMINI_CHANNEL_CALLBACKS, *PBTHMINI_CHANNEL_CALLBACKS;

typedef enum _BTHMINI_CHANNEL_STATE
{
    BthChannelWaitLink,
    BthChannelWaitConnect,
    BthChannelConfig,
    BthChannelOpen,
    BthChannelWaitDisconnect
} BTHMINI_CHANNEL_STATE;

struct _BTHMINI_CHANNEL
{
    LIST_ENTRY Link;
    PBTHMINI_REMOTE Remote;
    const BTHMINI_CHANNEL_CALLBACKS *Callbacks;
    PVOID Context;
    BTHMINI_CHANNEL_STATE State;
    USHORT Psm;
    USHORT LocalCid;
    USHORT RemoteCid;
    USHORT RemoteMtu;
    UCHAR Identifier;
    BOOLEAN Outgoing;
    BOOLEAN LocalConfigured;
    BOOLEAN RemoteConfigured;
    ULONGLONG Deadline;
};

typedef VOID BTHMINI_SDP_CALLBACK(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_opt_ PVOID Context,
    _In_ NTSTATUS Status,
    _In_ UCHAR RfcommChannel);

typedef struct _BTHMINI_RFCOMM_SESSION BTHMINI_RFCOMM_SESSION, *PBTHMINI_RFCOMM_SESSION;
typedef struct _BTHMINI_DLC BTHMINI_DLC, *PBTHMINI_DLC;

typedef VOID BTHMINI_DLC_EVENT(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ NTSTATUS Status);

typedef VOID BTHMINI_DLC_RECEIVE(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

typedef ULONG BTHMINI_DLC_ROOM(
    _In_ PBTHMINI_DLC Dlc);

typedef struct _BTHMINI_DLC_CALLBACKS
{
    BTHMINI_DLC_EVENT *Opened;
    BTHMINI_DLC_RECEIVE *Receive;
    BTHMINI_DLC_EVENT *Closed;
    BTHMINI_DLC_ROOM *Room;
} BTHMINI_DLC_CALLBACKS, *PBTHMINI_DLC_CALLBACKS;

typedef enum _BTHMINI_DLC_STATE
{
    BthDlcWaitSession,
    BthDlcWaitParameters,
    BthDlcWaitUa,
    BthDlcOpen,
    BthDlcWaitDisconnect
} BTHMINI_DLC_STATE;

struct _BTHMINI_RFCOMM_SESSION
{
    LIST_ENTRY Link;
    PBTHMINI_REMOTE Remote;
    PBTHMINI_CHANNEL Channel;
    BOOLEAN Initiator;
    BOOLEAN Open;
    BOOLEAN Closing;
    ULONGLONG Deadline;
    LIST_ENTRY Dlcs;
};

struct _BTHMINI_DLC
{
    LIST_ENTRY Link;
    PBTHMINI_RFCOMM_SESSION Session;
    const BTHMINI_DLC_CALLBACKS *Callbacks;
    PVOID Context;
    BTHMINI_DLC_STATE State;
    UCHAR Dlci;
    USHORT FrameSize;
    BOOLEAN CreditFlow;
    ULONG TxCredits;
    ULONG RxCredits;
    ULONGLONG Deadline;
    LIST_ENTRY TxQueue;
};

typedef struct _BTHMINI_COMPORT
{
    LIST_ENTRY Link;
    PBTHMINI_DEVICE Dev;
    WDFDEVICE Device;
    WDFQUEUE Queue;
    WDFQUEUE ReadQueue;
    WDFQUEUE WaitQueue;
    BTH_ADDR Address;
    UCHAR ServerChannel;
    BOOLEAN Open;
    ULONG PortNumber;
    ULONG Index;
    PBTHMINI_DLC Dlc;
    WDFREQUEST PendingCreate;
    volatile LONG CleanupRequested;
    ULONG WaitMask;
    ULONG Events;
    ULONGLONG ReadDeadline;
    SERIAL_TIMEOUTS Timeouts;
    SERIAL_BAUD_RATE BaudRate;
    SERIAL_LINE_CONTROL LineControl;
    SERIAL_HANDFLOW HandFlow;
    SERIAL_CHARS Chars;
    ULONG RxHead;
    ULONG RxCount;
    UCHAR RxBuffer[8192];
} BTHMINI_COMPORT, *PBTHMINI_COMPORT;

typedef struct _BTHMINI_ACL_PACKET
{
    LIST_ENTRY Link;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} BTHMINI_ACL_PACKET, *PBTHMINI_ACL_PACKET;

struct _BTHMINI_DEVICE
{
    WDFDEVICE Device;
    WDFIOTARGET Target;
    WDFQUEUE Queue;
    BTHX_CAPABILITIES Capabilities;
    WDFREQUEST Reads[BTHMINI_EVENT_READS + BTHMINI_ACL_READS];
    volatile LONG ReadsOutstanding;
    volatile BOOLEAN Stopping;
    KEVENT ReadsDrained;

    PKTHREAD Thread;
    KEVENT WorkEvent;
    KSPIN_LOCK RxLock;
    LIST_ENTRY RxPackets;
    ULONG RxCount;

    LIST_ENTRY CommandsPending;
    LIST_ENTRY CommandsSent;
    ULONG CommandCredits;
    BOOLEAN Ready;

    BTH_ADDR Address;
    UCHAR HciVersion;
    USHORT HciRevision;
    UCHAR LmpVersion;
    USHORT LmpSubversion;
    USHORT Manufacturer;
    ULONGLONG LmpFeatures;
    UCHAR SupportedCommands[64];
    USHORT AclMtu;
    USHORT AclPackets;
    USHORT AclCredits;
    BTH_COD ClassOfDevice;
    UCHAR ScanEnable;
    CHAR Name[BTH_MAX_NAME_SIZE];

    BOOLEAN Discovering;
    BOOLEAN Inquiring;
    BOOLEAN NameRequestActive;
    LIST_ENTRY Remotes;
    ULONG RemoteCount;

    WDFDEVICE AuthPdo;
    volatile BOOLEAN AuthForwarding;

    LIST_ENTRY Channels;
    LIST_ENTRY Sessions;
    LIST_ENTRY ComPorts;
    LIST_ENTRY AclQueue;
    USHORT NextCid;
    UCHAR SignalId;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHMINI_DEVICE, BthMiniGetDevice)

NTSTATUS
BthMiniSendPacket(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

BOOLEAN
BthMiniWaitWork(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ ULONG Milliseconds);

VOID
BthHciProcessRx(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthHciQueueCommand(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length);

NTSTATUS
BthHciCommand(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length,
    _Out_writes_bytes_opt_(ResultSize) PUCHAR Result,
    _In_ ULONG ResultSize);

VOID
BthHciFlushCommands(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthHciInitialize(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthHciWriteScanEnable(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR ScanEnable);

VOID
BthRadioEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length);

PBTHMINI_REMOTE
BthRadioFindRemote(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address);

PBTHMINI_REMOTE
BthRadioFindHandle(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Handle);

PBTHMINI_REMOTE
BthRadioGetRemote(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address);

BOOLEAN
BthRadioRequestName(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote);

VOID
BthRadioFillInfo(
    _In_ PBTHMINI_REMOTE Remote,
    _Out_ PBTH_DEVICE_INFO Info);

VOID
BthRadioReportInRange(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ ULONG PreviousFlags);

BTH_ADDR
BthReadAddress(
    _In_reads_(6) const UCHAR *Data);

VOID
BthWriteAddress(
    _Out_writes_(6) PUCHAR Data,
    _In_ BTH_ADDR Address);

BOOLEAN
BthConnEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length);

VOID
BthConnCommandFailed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode);

NTSTATUS
BthConnStartAuthentication(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request);

NTSTATUS
BthConnRemoveDevice(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request);

VOID
BthConnTick(
    _In_ PBTHMINI_DEVICE Dev);

VOID
BthConnLoadPaired(
    _In_ PBTHMINI_DEVICE Dev);

VOID
BthConnCancelAll(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthAuthCreatePdo(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthConnSendAuthResponse(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Out_ PULONG_PTR Information);

VOID
BthRadioCommandFailed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode);

VOID
BthRadioDeviceControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request);

VOID
BthRadioFreeRemotes(
    _In_ PBTHMINI_DEVICE Dev);

BOOLEAN
BthL2capEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length);

NTSTATUS
BthL2capConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Psm,
    _In_ const BTHMINI_CHANNEL_CALLBACKS *Callbacks,
    _In_opt_ PVOID Context,
    _Out_opt_ PBTHMINI_CHANNEL *Channel);

NTSTATUS
BthL2capSend(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

VOID
BthL2capDisconnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_CHANNEL Channel);

VOID
BthL2capLinkChanged(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status);

VOID
BthL2capTick(
    _In_ PBTHMINI_DEVICE Dev);

VOID
BthL2capFlush(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthConnEnsureLink(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ BOOLEAN Secure);

const BTHMINI_CHANNEL_CALLBACKS *
BthSdpServerCallbacks(VOID);

NTSTATUS
BthSdpQueryRfcommChannel(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT ServiceUuid,
    _In_ BTHMINI_SDP_CALLBACK *Callback,
    _In_opt_ PVOID Context);

const BTHMINI_CHANNEL_CALLBACKS *
BthRfcommServerCallbacks(VOID);

NTSTATUS
BthRfcommConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR ServerChannel,
    _In_ const BTHMINI_DLC_CALLBACKS *Callbacks,
    _In_opt_ PVOID Context,
    _Out_opt_ PBTHMINI_DLC *Dlc);

NTSTATUS
BthRfcommSend(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

VOID
BthRfcommGrantCredits(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc);

VOID
BthRfcommDisconnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc);

VOID
BthRfcommTick(
    _In_ PBTHMINI_DEVICE Dev);

ULONG
BthComProcess(
    _In_ PBTHMINI_DEVICE Dev);

VOID
BthComDevicePaired(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote);

VOID
BthAclReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);
