/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACL connections, Secure Simple Pairing and link keys
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"
#include <wdmsec.h>

#define NDEBUG
#include <debug.h>

static const WCHAR BthServicesKey[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services";

static
NTSTATUS
BthCreateKey(
    _In_opt_ HANDLE Parent,
    _In_ PCWSTR Name,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING String;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&Attributes, &String, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parent, NULL);
    return ZwCreateKey(Key, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
}

static
NTSTATUS
BthOpenParameters(
    _In_ PCWSTR Subkey,
    _In_opt_ PCWSTR Leaf,
    _Out_ PHANDLE Key)
{
    PCWSTR Path[] = { BthServicesKey, L"BTHPORT", L"Parameters", Subkey, Leaf };
    HANDLE Current = NULL, Next;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    *Key = NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(Path) && Path[Index]; Index++)
    {
        Status = BthCreateKey(Current, Path[Index], &Next);
        if (Current)
            ZwClose(Current);
        if (!NT_SUCCESS(Status))
            return Status;
        Current = Next;
    }
    *Key = Current;
    return Status;
}

static
VOID
BthFormatAddress(
    _In_ BTH_ADDR Address,
    _Out_writes_(13) PWCHAR Buffer)
{
    RtlStringCchPrintfW(Buffer, 13, L"%012I64x", Address & 0xFFFFFFFFFFFFULL);
}

static
BOOLEAN
BthParseAddress(
    _In_reads_(Length) PCWCH Name,
    _In_ ULONG Length,
    _Out_ BTH_ADDR *Address)
{
    ULONG Index;

    *Address = 0;
    if (Length != 12)
        return FALSE;
    for (Index = 0; Index < 12; Index++)
    {
        WCHAR Char = Name[Index];
        ULONG Digit;

        if (Char >= L'0' && Char <= L'9')
            Digit = Char - L'0';
        else if (Char >= L'a' && Char <= L'f')
            Digit = Char - L'a' + 10;
        else if (Char >= L'A' && Char <= L'F')
            Digit = Char - L'A' + 10;
        else
            return FALSE;
        *Address = (*Address << 4) | Digit;
    }
    return TRUE;
}

static
VOID
BthConnSaveDevice(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    UNICODE_STRING ValueName;
    WCHAR Radio[13], Device[13];
    HANDLE Key;
    ULONG Cod = Remote->ClassOfDevice;

    BthFormatAddress(Dev->Address, Radio);
    BthFormatAddress(Remote->Address, Device);
    if (NT_SUCCESS(BthOpenParameters(L"Keys", Radio, &Key)))
    {
        RtlInitUnicodeString(&ValueName, Device);
        ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, Remote->LinkKey, sizeof(Remote->LinkKey));
        ZwClose(Key);
    }
    if (NT_SUCCESS(BthOpenParameters(L"Devices", Device, &Key)))
    {
        RtlInitUnicodeString(&ValueName, L"Name");
        ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, Remote->Name, (ULONG)strlen(Remote->Name) + 1);
        RtlInitUnicodeString(&ValueName, L"COD");
        ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Cod, sizeof(Cod));
        ZwClose(Key);
    }
}

static
VOID
BthConnDeleteDevice(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    UNICODE_STRING ValueName;
    WCHAR Radio[13], Device[13];
    HANDLE Key;

    BthFormatAddress(Dev->Address, Radio);
    BthFormatAddress(Remote->Address, Device);
    if (NT_SUCCESS(BthOpenParameters(L"Keys", Radio, &Key)))
    {
        RtlInitUnicodeString(&ValueName, Device);
        ZwDeleteValueKey(Key, &ValueName);
        ZwClose(Key);
    }
    if (NT_SUCCESS(BthOpenParameters(L"Devices", Device, &Key)))
    {
        ZwDeleteKey(Key);
        ZwClose(Key);
    }
}

VOID
BthConnLoadPaired(
    _In_ PBTHMINI_DEVICE Dev)
{
    UCHAR Buffer[sizeof(KEY_VALUE_FULL_INFORMATION) + 64 * sizeof(WCHAR) + 16];
    PKEY_VALUE_FULL_INFORMATION Value = (PKEY_VALUE_FULL_INFORMATION)Buffer;
    WCHAR Radio[13];
    HANDLE Key, DeviceKey;
    ULONG Index, Size;

    BthFormatAddress(Dev->Address, Radio);
    if (!NT_SUCCESS(BthOpenParameters(L"Keys", Radio, &Key)))
        return;

    for (Index = 0; NT_SUCCESS(ZwEnumerateValueKey(Key, Index, KeyValueFullInformation, Value, sizeof(Buffer), &Size)); Index++)
    {
        PKEY_VALUE_PARTIAL_INFORMATION Data;
        UCHAR DataBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + BTH_MAX_NAME_SIZE];
        UNICODE_STRING ValueName;
        PBTHMINI_REMOTE Remote;
        WCHAR Device[13];
        BTH_ADDR Address;

        if (Value->Type != REG_BINARY || Value->DataLength != 16 ||
            !BthParseAddress(Value->Name, Value->NameLength / sizeof(WCHAR), &Address))
            continue;
        Remote = BthRadioGetRemote(Dev, Address);
        if (!Remote)
            break;
        RtlCopyMemory(Remote->LinkKey, (PUCHAR)Value + Value->DataOffset, sizeof(Remote->LinkKey));
        Remote->HasLinkKey = TRUE;
        Remote->Flags |= BDIF_PAIRED | BDIF_PERSONAL;

        BthFormatAddress(Address, Device);
        if (!NT_SUCCESS(BthOpenParameters(L"Devices", Device, &DeviceKey)))
            continue;
        Data = (PKEY_VALUE_PARTIAL_INFORMATION)DataBuffer;
        RtlInitUnicodeString(&ValueName, L"Name");
        if (NT_SUCCESS(ZwQueryValueKey(DeviceKey, &ValueName, KeyValuePartialInformation, Data, sizeof(DataBuffer), &Size)) &&
            Data->Type == REG_BINARY && Data->DataLength)
        {
            RtlZeroMemory(Remote->Name, sizeof(Remote->Name));
            RtlCopyMemory(Remote->Name, Data->Data, min(Data->DataLength, sizeof(Remote->Name) - 1));
            if (Remote->Name[0])
                Remote->Flags |= BDIF_NAME;
        }
        RtlInitUnicodeString(&ValueName, L"COD");
        if (NT_SUCCESS(ZwQueryValueKey(DeviceKey, &ValueName, KeyValuePartialInformation, Data, sizeof(DataBuffer), &Size)) &&
            Data->Type == REG_DWORD && Data->DataLength == sizeof(ULONG))
        {
            Remote->ClassOfDevice = *(PULONG)Data->Data;
            Remote->Flags |= BDIF_COD;
        }
        ZwClose(DeviceKey);
    }
    ZwClose(Key);
}

static
VOID
BthConnFinishAuthentication(
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status)
{
    WDFREQUEST Request = Remote->AuthRequest;

    Remote->Pairing = FALSE;
    Remote->AuthRequest = NULL;
    if (Request)
    {
        DPRINT1("BTHMINI: pairing with %012I64x finished 0x%08lx\n", Remote->Address, Status);
        WdfRequestComplete(Request, Status);
    }
}

static
NTSTATUS
BthConnStatus(
    _In_ UCHAR HciStatus)
{
    switch (HciStatus)
    {
        case 0x00:
            return STATUS_SUCCESS;
        case 0x04:
        case 0x08:
        case 0x22:
            return STATUS_IO_TIMEOUT;
        case 0x05:
        case 0x06:
        case 0x18:
            return STATUS_ACCESS_DENIED;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}

static
NTSTATUS
BthConnAuthenticate(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    UCHAR Parameters[2];

    Parameters[0] = (UCHAR)Remote->Handle;
    Parameters[1] = (UCHAR)(Remote->Handle >> 8);
    return BthHciQueueCommand(Dev, HCI_AUTHENTICATION_REQUESTED, Parameters, sizeof(Parameters));
}

static
NTSTATUS
BthConnConnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    UCHAR Parameters[13];
    BOOLEAN Seen = (Remote->Flags & BDIF_VISIBLE) != 0;

    if (Dev->Inquiring)
    {
        Dev->Inquiring = FALSE;
        BthHciCommand(Dev, HCI_INQUIRY_CANCEL, NULL, 0, NULL, 0);
    }
    BthWriteAddress(Parameters, Remote->Address);
    Parameters[6] = 0x18;
    Parameters[7] = 0xCC;
    Parameters[8] = Seen ? Remote->PageScanRepetitionMode : 0x02;
    Parameters[9] = 0;
    Parameters[10] = Seen ? (UCHAR)Remote->ClockOffset : 0;
    Parameters[11] = Seen ? (UCHAR)((Remote->ClockOffset >> 8) | 0x80) : 0;
    Parameters[12] = 0x01;
    Remote->Connecting = TRUE;
    return BthHciQueueCommand(Dev, HCI_CREATE_CONNECTION, Parameters, sizeof(Parameters));
}

NTSTATUS
BthConnEnsureLink(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ BOOLEAN Secure)
{
    if (!(Remote->Flags & BDIF_CONNECTED))
    {
        if (Remote->Connecting)
            return STATUS_PENDING;
        return NT_SUCCESS(BthConnConnect(Dev, Remote)) ? STATUS_PENDING : STATUS_UNSUCCESSFUL;
    }
    if (!Secure || Remote->Encrypted)
        return STATUS_SUCCESS;
    if (!Remote->HasLinkKey)
        return STATUS_ACCESS_DENIED;
    if (!Remote->Securing && !Remote->Pairing)
    {
        Remote->Securing = TRUE;
        if (!NT_SUCCESS(BthConnAuthenticate(Dev, Remote)))
        {
            Remote->Securing = FALSE;
            return STATUS_UNSUCCESSFUL;
        }
    }
    return STATUS_PENDING;
}

NTSTATUS
BthConnStartAuthentication(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHMINI_REMOTE Remote;
    BTH_ADDR *Parameter;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Parameter), (PVOID *)&Parameter, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_BUFFER_SIZE;
    Remote = BthRadioGetRemote(Dev, RtlUlonglongByteSwap(*Parameter) >> 16);
    if (!Remote)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Remote->Flags & BDIF_PAIRED)
        return STATUS_NO_MORE_ENTRIES;
    if (Remote->AuthRequest)
        return STATUS_DEVICE_BUSY;

    Remote->Pairing = TRUE;
    Remote->AuthRequest = Request;
    Remote->AuthDeadline = KeQueryInterruptTime() / 10000 + BTHMINI_PAIRING_TIMEOUT_MS;
    if (Remote->Flags & BDIF_CONNECTED)
        Status = BthConnAuthenticate(Dev, Remote);
    else if (!Remote->Connecting)
        Status = BthConnConnect(Dev, Remote);
    if (!NT_SUCCESS(Status))
    {
        Remote->Pairing = FALSE;
        Remote->AuthRequest = NULL;
        return Status;
    }
    DPRINT1("BTHMINI: pairing with %012I64x\n", Remote->Address);
    return STATUS_PENDING;
}

NTSTATUS
BthConnRemoveDevice(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHMINI_REMOTE Remote;
    BTH_ADDR *Address;
    ULONG Previous;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Address), (PVOID *)&Address, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_BUFFER_SIZE;
    Remote = BthRadioFindRemote(Dev, *Address & 0xFFFFFFFFFFFFULL);
    if (!Remote || !(Remote->Flags & BDIF_PAIRED))
        return STATUS_NOT_FOUND;

    if (Remote->Flags & BDIF_CONNECTED)
    {
        UCHAR Parameters[3];

        Parameters[0] = (UCHAR)Remote->Handle;
        Parameters[1] = (UCHAR)(Remote->Handle >> 8);
        Parameters[2] = HCI_REMOTE_USER_TERMINATED;
        BthHciQueueCommand(Dev, HCI_DISCONNECT, Parameters, sizeof(Parameters));
    }
    BthConnDeleteDevice(Dev, Remote);
    Previous = Remote->Flags;
    Remote->HasLinkKey = FALSE;
    RtlZeroMemory(Remote->LinkKey, sizeof(Remote->LinkKey));
    Remote->Flags &= ~(BDIF_PAIRED | BDIF_PERSONAL | BDIF_SSP_PAIRED | BDIF_SSP_MITM_PROTECTED);
    BthRadioReportInRange(Dev, Remote, Previous);
    return STATUS_SUCCESS;
}

VOID
BthConnCommandFailed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode)
{
    PLIST_ENTRY Entry;

    if (Opcode != HCI_CREATE_CONNECTION && Opcode != HCI_AUTHENTICATION_REQUESTED)
        return;
    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if (Opcode == HCI_CREATE_CONNECTION && Remote->Connecting)
        {
            Remote->Connecting = FALSE;
            BthConnFinishAuthentication(Remote, STATUS_UNSUCCESSFUL);
            BthL2capLinkChanged(Dev, Remote, STATUS_UNSUCCESSFUL);
            return;
        }
        if (Opcode == HCI_AUTHENTICATION_REQUESTED && (Remote->Pairing || Remote->Securing))
        {
            Remote->Securing = FALSE;
            BthConnFinishAuthentication(Remote, STATUS_UNSUCCESSFUL);
            BthL2capLinkChanged(Dev, Remote, STATUS_ACCESS_DENIED);
            return;
        }
    }
}

VOID
BthConnTick(
    _In_ PBTHMINI_DEVICE Dev)
{
    ULONGLONG Now = KeQueryInterruptTime() / 10000;
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if (Remote->AuthRequest && Now >= Remote->AuthDeadline)
            BthConnFinishAuthentication(Remote, STATUS_IO_TIMEOUT);
    }
}

VOID
BthConnCancelAll(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
        BthConnFinishAuthentication(CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link), STATUS_CANCELLED);
}

static
VOID
BthConnReplyAddress(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_(6) const UCHAR *Address,
    _In_opt_ UCHAR Extra)
{
    UCHAR Parameters[7];

    RtlCopyMemory(Parameters, Address, 6);
    Parameters[6] = Extra;
    BthHciQueueCommand(Dev, Opcode, Parameters, Opcode == HCI_IO_CAPABILITY_NEG_REPLY ? 7 : 6);
}

BOOLEAN
BthConnEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length)
{
    const UCHAR *Parameters = Event + 2;
    ULONG ParameterLength = Length - 2;
    PBTHMINI_REMOTE Remote;
    ULONG Previous;

    switch (Event[0])
    {
        case HCI_EV_CONNECTION_REQUEST:
            if (ParameterLength >= 10)
            {
                UCHAR Reply[7];

                RtlCopyMemory(Reply, Parameters, 6);
                if (Parameters[9] == 0x01 && (Dev->ScanEnable & HCI_SCAN_PAGE))
                {
                    Remote = BthRadioGetRemote(Dev, BthReadAddress(Parameters));
                    if (Remote)
                    {
                        Remote->ClassOfDevice = Parameters[6] | (Parameters[7] << 8) | (Parameters[8] << 16);
                        Remote->Flags |= BDIF_COD;
                    }
                    Reply[6] = 0x01;
                    BthHciQueueCommand(Dev, HCI_ACCEPT_CONNECTION_REQUEST, Reply, 7);
                }
                else
                {
                    Reply[6] = 0x0D;
                    BthHciQueueCommand(Dev, HCI_REJECT_CONNECTION_REQUEST, Reply, 7);
                }
            }
            return TRUE;

        case HCI_EV_CONNECTION_COMPLETE:
            if (ParameterLength < 11 || Parameters[9] != 0x01)
                return TRUE;
            Remote = BthRadioGetRemote(Dev, BthReadAddress(Parameters + 3));
            if (!Remote)
                return TRUE;
            Remote->Connecting = FALSE;
            if (Parameters[0])
            {
                DPRINT1("BTHMINI: connection to %012I64x failed, HCI status 0x%02x\n", Remote->Address, Parameters[0]);
                BthConnFinishAuthentication(Remote, BthConnStatus(Parameters[0]));
                BthL2capLinkChanged(Dev, Remote, BthConnStatus(Parameters[0]));
                return TRUE;
            }
            Previous = Remote->Flags;
            Remote->Handle = (USHORT)((Parameters[1] | (Parameters[2] << 8)) & 0x0FFF);
            Remote->Encrypted = Parameters[10] != 0;
            Remote->Flags |= BDIF_CONNECTED;
            BthRadioReportInRange(Dev, Remote, Previous);
            DPRINT1("BTHMINI: connected to %012I64x, handle 0x%03x\n", Remote->Address, Remote->Handle);
            if (Remote->Pairing && !NT_SUCCESS(BthConnAuthenticate(Dev, Remote)))
                BthConnFinishAuthentication(Remote, STATUS_UNSUCCESSFUL);
            BthL2capLinkChanged(Dev, Remote, STATUS_SUCCESS);
            return TRUE;

        case HCI_EV_DISCONNECTION_COMPLETE:
            if (ParameterLength < 4 || Parameters[0])
                return TRUE;
            Remote = BthRadioFindHandle(Dev, (USHORT)((Parameters[1] | (Parameters[2] << 8)) & 0x0FFF));
            if (!Remote)
                return TRUE;
            Previous = Remote->Flags;
            Remote->Flags &= ~BDIF_CONNECTED;
            Remote->Encrypted = FALSE;
            Remote->Securing = FALSE;
            BthRadioReportInRange(Dev, Remote, Previous);
            BthL2capLinkChanged(Dev, Remote, STATUS_REMOTE_DISCONNECT);
            DPRINT1("BTHMINI: disconnected from %012I64x, reason 0x%02x\n", Remote->Address, Parameters[3]);
            if (Remote->Pairing)
                BthConnFinishAuthentication(Remote, BthConnStatus(Parameters[3]));
            return TRUE;

        case HCI_EV_AUTHENTICATION_COMPLETE:
            if (ParameterLength < 3)
                return TRUE;
            Remote = BthRadioFindHandle(Dev, (USHORT)((Parameters[1] | (Parameters[2] << 8)) & 0x0FFF));
            if (!Remote)
                return TRUE;
            if (!Parameters[0] && !Remote->Encrypted)
            {
                UCHAR Encrypt[3];

                Encrypt[0] = (UCHAR)Remote->Handle;
                Encrypt[1] = (UCHAR)(Remote->Handle >> 8);
                Encrypt[2] = 0x01;
                BthHciQueueCommand(Dev, HCI_SET_CONNECTION_ENCRYPTION, Encrypt, sizeof(Encrypt));
            }
            if (Remote->Pairing)
            {
                BthConnFinishAuthentication(Remote, BthConnStatus(Parameters[0]));
                if (!Parameters[0])
                    BthComDevicePaired(Dev, Remote);
            }
            if (Parameters[0])
            {
                Remote->Securing = FALSE;
                BthL2capLinkChanged(Dev, Remote, STATUS_ACCESS_DENIED);
            }
            return TRUE;

        case HCI_EV_ENCRYPTION_CHANGE:
            if (ParameterLength >= 4)
            {
                Remote = BthRadioFindHandle(Dev, (USHORT)((Parameters[1] | (Parameters[2] << 8)) & 0x0FFF));
                if (Remote)
                {
                    Remote->Encrypted = !Parameters[0] && Parameters[3] != 0;
                    Remote->Securing = FALSE;
                    BthL2capLinkChanged(Dev, Remote, Remote->Encrypted ? STATUS_SUCCESS : STATUS_ACCESS_DENIED);
                }
            }
            return TRUE;

        case HCI_EV_ENCRYPTION_KEY_REFRESH:
            return TRUE;

        case HCI_EV_PIN_CODE_REQUEST:
            if (ParameterLength >= 6)
                BthConnReplyAddress(Dev, HCI_PIN_CODE_REQUEST_NEG_REPLY, Parameters, 0);
            return TRUE;

        case HCI_EV_LINK_KEY_REQUEST:
            if (ParameterLength < 6)
                return TRUE;
            Remote = BthRadioFindRemote(Dev, BthReadAddress(Parameters));
            if (Remote && Remote->HasLinkKey && !Remote->Pairing)
            {
                UCHAR Reply[22];

                RtlCopyMemory(Reply, Parameters, 6);
                RtlCopyMemory(Reply + 6, Remote->LinkKey, sizeof(Remote->LinkKey));
                BthHciQueueCommand(Dev, HCI_LINK_KEY_REQUEST_REPLY, Reply, sizeof(Reply));
            }
            else
            {
                BthConnReplyAddress(Dev, HCI_LINK_KEY_REQUEST_NEG_REPLY, Parameters, 0);
            }
            return TRUE;

        case HCI_EV_LINK_KEY_NOTIFICATION:
            if (ParameterLength < 23)
                return TRUE;
            Remote = BthRadioGetRemote(Dev, BthReadAddress(Parameters));
            if (!Remote)
                return TRUE;
            Previous = Remote->Flags;
            RtlCopyMemory(Remote->LinkKey, Parameters + 6, sizeof(Remote->LinkKey));
            Remote->LinkKeyType = Parameters[22];
            Remote->HasLinkKey = TRUE;
            Remote->Flags |= BDIF_PAIRED | BDIF_PERSONAL;
            if (Remote->LinkKeyType >= 0x04)
                Remote->Flags |= BDIF_SSP_PAIRED;
            if (Remote->LinkKeyType == 0x05 || Remote->LinkKeyType == 0x08)
                Remote->Flags |= BDIF_SSP_MITM_PROTECTED;
            BthConnSaveDevice(Dev, Remote);
            BthRadioReportInRange(Dev, Remote, Previous);
            DPRINT1("BTHMINI: paired with %012I64x, key type %u\n", Remote->Address, Remote->LinkKeyType);
            return TRUE;

        case HCI_EV_IO_CAPABILITY_REQUEST:
            if (ParameterLength < 6)
                return TRUE;
            Remote = BthRadioFindRemote(Dev, BthReadAddress(Parameters));
            if (Remote && Remote->Pairing)
            {
                UCHAR Reply[9];

                RtlCopyMemory(Reply, Parameters, 6);
                Reply[6] = HCI_IO_DISPLAY_YES_NO;
                Reply[7] = 0x00;
                Reply[8] = HCI_AUTH_MITM_DEDICATED_BONDING;
                BthHciQueueCommand(Dev, HCI_IO_CAPABILITY_REPLY, Reply, sizeof(Reply));
            }
            else
            {
                BthConnReplyAddress(Dev, HCI_IO_CAPABILITY_NEG_REPLY, Parameters, HCI_PAIRING_NOT_ALLOWED);
            }
            return TRUE;

        case HCI_EV_IO_CAPABILITY_RESPONSE:
            if (ParameterLength >= 9)
            {
                Remote = BthRadioFindRemote(Dev, BthReadAddress(Parameters));
                if (Remote)
                {
                    Remote->RemoteIoCapability = Parameters[6];
                    Remote->RemoteAuthentication = Parameters[8];
                }
            }
            return TRUE;

        case HCI_EV_USER_CONFIRMATION:
            if (ParameterLength < 10)
                return TRUE;
            Remote = BthRadioFindRemote(Dev, BthReadAddress(Parameters));
            if (Remote && Remote->Pairing && Remote->RemoteIoCapability == HCI_IO_NO_INPUT_NO_OUTPUT)
                BthConnReplyAddress(Dev, HCI_USER_CONFIRMATION_REPLY, Parameters, 0);
            else
                BthConnReplyAddress(Dev, HCI_USER_CONFIRMATION_NEG_REPLY, Parameters, 0);
            return TRUE;

        case HCI_EV_USER_PASSKEY_REQUEST:
            if (ParameterLength >= 6)
                BthConnReplyAddress(Dev, HCI_USER_PASSKEY_NEG_REPLY, Parameters, 0);
            return TRUE;

        case HCI_EV_SIMPLE_PAIRING_COMPLETE:
            if (ParameterLength >= 7 && Parameters[0])
            {
                Remote = BthRadioFindRemote(Dev, BthReadAddress(Parameters + 1));
                if (Remote && Remote->Pairing)
                    BthConnFinishAuthentication(Remote, BthConnStatus(Parameters[0]));
            }
            return TRUE;

        default:
            return FALSE;
    }
}

static
VOID
NTAPI
BthAuthEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    WdfRequestComplete(Request, IoControlCode == IOCTL_WINEBTH_AUTH_REGISTER ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED);
}

NTSTATUS
BthAuthCreateControlDevice(
    _In_ WDFDRIVER Driver)
{
    DECLARE_CONST_UNICODE_STRING(Name, BTHMINI_AUTH_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(Link, BTHMINI_AUTH_LINK_NAME);
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Device;
    NTSTATUS Status;

    Init = WdfControlDeviceInitAllocate(Driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
    if (!Init)
        return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_BLUETOOTH);
    Status = WdfDeviceInitAssignName(Init, &Name);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    Status = WdfDeviceCreate(&Init, WDF_NO_OBJECT_ATTRIBUTES, &Device);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    Status = WdfDeviceCreateSymbolicLink(Device, &Link);
    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
        QueueConfig.EvtIoDeviceControl = BthAuthEvtIoDeviceControl;
        Status = WdfIoQueueCreate(Device, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    }
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Device);
        return Status;
    }
    WdfControlFinishInitializing(Device);
    return STATUS_SUCCESS;
}
