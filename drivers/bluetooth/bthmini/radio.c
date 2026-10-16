/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth radio: discovery, remote devices and user requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

BTH_ADDR
BthReadAddress(
    _In_reads_(6) const UCHAR *Data)
{
    BTH_ADDR Address = 0;
    ULONG Index;

    for (Index = 0; Index < 6; Index++)
        Address |= (BTH_ADDR)Data[Index] << (Index * 8);
    return Address;
}

VOID
BthWriteAddress(
    _Out_writes_(6) PUCHAR Data,
    _In_ BTH_ADDR Address)
{
    ULONG Index;

    for (Index = 0; Index < 6; Index++)
        Data[Index] = (UCHAR)(Address >> (Index * 8));
}

PBTHMINI_REMOTE
BthRadioFindRemote(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if (Remote->Address == Address)
            return Remote;
    }
    return NULL;
}

PBTHMINI_REMOTE
BthRadioFindHandle(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if ((Remote->Flags & BDIF_CONNECTED) && Remote->Handle == Handle)
            return Remote;
    }
    return NULL;
}

PBTHMINI_REMOTE
BthRadioGetRemote(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address)
{
    PBTHMINI_REMOTE Remote = BthRadioFindRemote(Dev, Address);

    if (Remote || Dev->RemoteCount >= BTHMINI_MAX_DEVICES)
        return Remote;
    Remote = ExAllocatePoolZero(NonPagedPool, sizeof(*Remote), BTHMINI_TAG);
    if (!Remote)
        return NULL;
    Remote->Address = Address;
    Remote->Flags = BDIF_ADDRESS | BDIF_BR;
    InsertTailList(&Dev->Remotes, &Remote->Link);
    Dev->RemoteCount++;
    return Remote;
}

VOID
BthRadioFreeRemotes(
    _In_ PBTHMINI_DEVICE Dev)
{
    BthConnCancelAll(Dev);
    while (!IsListEmpty(&Dev->Remotes))
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(RemoveHeadList(&Dev->Remotes), BTHMINI_REMOTE, Link);

        BthSmpLinkDown(Dev, Remote, STATUS_CANCELLED);
        BthGattFree(Remote);
        ExFreePoolWithTag(Remote, BTHMINI_TAG);
    }
    Dev->RemoteCount = 0;
    Dev->Discovering = FALSE;
    Dev->LeScanning = FALSE;
    Dev->Inquiring = FALSE;
    Dev->NameRequestActive = FALSE;
}

VOID
BthRadioFillInfo(
    _In_ PBTHMINI_REMOTE Remote,
    _Out_ PBTH_DEVICE_INFO Info)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->flags = Remote->Flags;
    Info->address = Remote->Address;
    Info->classOfDevice = Remote->ClassOfDevice;
    RtlCopyMemory(Info->name, Remote->Name, sizeof(Info->name));
}

VOID
BthRadioReportInRange(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ ULONG PreviousFlags)
{
    UCHAR Buffer[FIELD_OFFSET(TARGET_DEVICE_CUSTOM_NOTIFICATION, CustomDataBuffer) + sizeof(BTH_RADIO_IN_RANGE)];
    PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification = (PTARGET_DEVICE_CUSTOM_NOTIFICATION)Buffer;
    PBTH_RADIO_IN_RANGE InRange = (PBTH_RADIO_IN_RANGE)Notification->CustomDataBuffer;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Notification->Version = 1;
    Notification->Size = sizeof(Buffer);
    Notification->Event = GUID_BLUETOOTH_RADIO_IN_RANGE;
    Notification->NameBufferOffset = -1;
    BthRadioFillInfo(Remote, &InRange->deviceInfo);
    InRange->previousDeviceFlags = PreviousFlags;
    IoReportTargetDeviceChangeAsynchronous(WdfDeviceWdmGetPhysicalDevice(Dev->Device), Notification, NULL, NULL);
}

static
VOID
BthRadioStartInquiry(
    _In_ PBTHMINI_DEVICE Dev)
{
    static const UCHAR Parameters[] = { 0x33, 0x8B, 0x9E, BTHMINI_INQUIRY_LENGTH, 0x00 };

    if (Dev->Inquiring || Dev->NameRequestActive)
        return;
    if (NT_SUCCESS(BthHciQueueCommand(Dev, HCI_INQUIRY, Parameters, sizeof(Parameters))))
        Dev->Inquiring = TRUE;
}

BOOLEAN
BthRadioRequestName(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    UCHAR Parameters[10];

    if (Dev->NameRequestActive || (Remote->Flags & BDIF_NAME) || Remote->NameRequested)
        return FALSE;
    Remote->NameRequested = TRUE;
    BthWriteAddress(Parameters, Remote->Address);
    Parameters[6] = Remote->PageScanRepetitionMode;
    Parameters[7] = 0;
    Parameters[8] = (UCHAR)Remote->ClockOffset;
    Parameters[9] = (UCHAR)((Remote->ClockOffset >> 8) | 0x80);
    if (!NT_SUCCESS(BthHciQueueCommand(Dev, HCI_REMOTE_NAME_REQUEST, Parameters, sizeof(Parameters))))
        return FALSE;
    Dev->NameRequestActive = TRUE;
    return TRUE;
}

static
VOID
BthRadioContinueDiscovery(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;

    if (Dev->Inquiring || Dev->NameRequestActive)
        return;
    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if ((Remote->Flags & BDIF_VISIBLE) && BthRadioRequestName(Dev, Remote))
            return;
    }
    if (Dev->Discovering)
        BthRadioStartInquiry(Dev);
}

static
VOID
BthRadioParseEir(
    _Inout_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Eir,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    while (Offset < Length && Eir[Offset])
    {
        ULONG FieldLength = Eir[Offset];
        UCHAR Type;

        if (Offset + 1 + FieldLength > Length)
            break;
        Type = Eir[Offset + 1];
        if ((Type == 0x09 || (Type == 0x08 && !(Remote->Flags & BDIF_NAME))) && FieldLength > 1)
        {
            ULONG NameLength = min(FieldLength - 1, BTH_MAX_NAME_SIZE - 1);

            RtlZeroMemory(Remote->Name, sizeof(Remote->Name));
            RtlCopyMemory(Remote->Name, Eir + Offset + 2, NameLength);
            Remote->Flags |= BDIF_NAME;
            if (Type == 0x08)
                Remote->Flags |= BDIF_SHORT_NAME;
            else
                Remote->Flags &= ~BDIF_SHORT_NAME;
        }
        Offset += 1 + FieldLength;
    }
    Remote->Flags |= BDIF_EIR;
}

static
VOID
BthRadioInquiryResult(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_(14) const UCHAR *Response,
    _In_ UCHAR ClassOffset,
    _In_ BOOLEAN HasRssi,
    _In_reads_bytes_opt_(EirLength) const UCHAR *Eir,
    _In_ ULONG EirLength)
{
    PBTHMINI_REMOTE Remote = BthRadioGetRemote(Dev, BthReadAddress(Response));
    ULONG Previous;

    if (!Remote)
        return;
    Previous = Remote->Flags;
    Remote->PageScanRepetitionMode = Response[6];
    Remote->ClassOfDevice = Response[ClassOffset] | (Response[ClassOffset + 1] << 8) | (Response[ClassOffset + 2] << 16);
    Remote->ClockOffset = (USHORT)(Response[ClassOffset + 3] | (Response[ClassOffset + 4] << 8));
    Remote->Flags |= BDIF_COD | BDIF_VISIBLE;
    if (HasRssi)
        Remote->Flags |= BDIF_RSSI;
    if (Eir)
        BthRadioParseEir(Remote, Eir, EirLength);
    if (Remote->Flags != Previous)
        BthRadioReportInRange(Dev, Remote, Previous);
}

VOID
BthRadioEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length)
{
    const UCHAR *Parameters = Event + 2;
    ULONG ParameterLength = Length - 2;
    PBTHMINI_REMOTE Remote;
    ULONG Index, Previous;

    if (BthLeLinkEvent(Dev, Event, Length) || BthConnEvent(Dev, Event, Length) || BthL2capEvent(Dev, Event, Length) ||
        BthLeEvent(Dev, Event, Length))
        return;

    switch (Event[0])
    {
        case HCI_EV_INQUIRY_COMPLETE:
            Dev->Inquiring = FALSE;
            BthRadioContinueDiscovery(Dev);
            break;

        case HCI_EV_INQUIRY_RESULT:
            for (Index = 0; ParameterLength >= 1 && Index < Parameters[0] && 1 + (Index + 1) * 14 <= ParameterLength; Index++)
                BthRadioInquiryResult(Dev, Parameters + 1 + Index * 14, 9, FALSE, NULL, 0);
            break;

        case HCI_EV_INQUIRY_RESULT_RSSI:
            for (Index = 0; ParameterLength >= 1 && Index < Parameters[0] && 1 + (Index + 1) * 14 <= ParameterLength; Index++)
                BthRadioInquiryResult(Dev, Parameters + 1 + Index * 14, 8, TRUE, NULL, 0);
            break;

        case HCI_EV_EXTENDED_INQUIRY_RESULT:
            if (ParameterLength >= 15)
                BthRadioInquiryResult(Dev, Parameters + 1, 8, TRUE, Parameters + 15, ParameterLength - 15);
            break;

        case HCI_EV_REMOTE_NAME_COMPLETE:
            Dev->NameRequestActive = FALSE;
            if (ParameterLength >= 7)
            {
                Remote = BthRadioFindRemote(Dev, BthReadAddress(Parameters + 1));
                if (Remote && !Parameters[0])
                {
                    Previous = Remote->Flags;
                    RtlZeroMemory(Remote->Name, sizeof(Remote->Name));
                    RtlCopyMemory(Remote->Name, Parameters + 7, min(ParameterLength - 7, BTH_MAX_NAME_SIZE - 1));
                    Remote->Flags = (Remote->Flags | BDIF_NAME) & ~BDIF_SHORT_NAME;
                    if (Remote->Flags != Previous)
                        BthRadioReportInRange(Dev, Remote, Previous);
                }
            }
            BthRadioContinueDiscovery(Dev);
            break;

        default:
            break;
    }
}

VOID
BthRadioCommandFailed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode)
{
    BthConnCommandFailed(Dev, Opcode);
    BthLeCommandFailed(Dev, Opcode);
    if (Opcode == HCI_INQUIRY)
    {
        Dev->Inquiring = FALSE;
    }
    else if (Opcode == HCI_REMOTE_NAME_REQUEST)
    {
        Dev->NameRequestActive = FALSE;
        BthRadioContinueDiscovery(Dev);
    }
}

static
NTSTATUS
BthRadioGetLocalInfo(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Out_ PULONG_PTR Information)
{
    PBTH_LOCAL_RADIO_INFO Info;
    NTSTATUS Status;

    Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Info), (PVOID *)&Info, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_USER_BUFFER;
    RtlZeroMemory(Info, sizeof(*Info));
    Info->localInfo.flags = BDIF_ADDRESS | BDIF_COD | BDIF_NAME;
    Info->localInfo.address = Dev->Address;
    Info->localInfo.classOfDevice = Dev->ClassOfDevice;
    RtlCopyMemory(Info->localInfo.name, Dev->Name, sizeof(Info->localInfo.name));
    if (Dev->ScanEnable & HCI_SCAN_PAGE)
        Info->flags |= LOCAL_RADIO_CONNECTABLE;
    if (Dev->ScanEnable & HCI_SCAN_INQUIRY)
        Info->flags |= LOCAL_RADIO_DISCOVERABLE;
    Info->hciRevision = Dev->HciRevision;
    Info->hciVersion = Dev->HciVersion;
    Info->radioInfo.lmpSupportedFeatures = Dev->LmpFeatures;
    Info->radioInfo.mfg = Dev->Manufacturer;
    Info->radioInfo.lmpSubversion = Dev->LmpSubversion;
    Info->radioInfo.lmpVersion = Dev->LmpVersion;
    *Information = sizeof(*Info);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthRadioGetRadioInfo(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Out_ PULONG_PTR Information)
{
    PBTHMINI_REMOTE Remote;
    PBTH_RADIO_INFO Info;
    BTH_ADDR *Address;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Address), (PVOID *)&Address, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_USER_BUFFER;
    Remote = BthRadioFindRemote(Dev, *Address);
    if (!Remote || !Remote->HasRemoteFeatures || !Remote->HasRemoteVersion)
        return STATUS_DEVICE_NOT_CONNECTED;
    Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Info), (PVOID *)&Info, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_USER_BUFFER;
    RtlZeroMemory(Info, sizeof(*Info));
    Info->lmpSupportedFeatures = Remote->RemoteLmpFeatures;
    Info->mfg = Remote->RemoteManufacturer;
    Info->lmpSubversion = Remote->RemoteLmpSubversion;
    Info->lmpVersion = Remote->RemoteLmpVersion;
    *Information = sizeof(*Info);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthRadioGetHostFeatures(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Out_ PULONG_PTR Information)
{
    PBTH_HOST_FEATURE_MASK Features;
    NTSTATUS Status;

    Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Features), (PVOID *)&Features, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_USER_BUFFER;
    RtlZeroMemory(Features, sizeof(*Features));
    if (Dev->LeEnabled)
        Features->Mask |= BTH_HOST_FEATURE_LOW_ENERGY;
    *Information = sizeof(*Features);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthRadioGetDeviceInfo(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _Out_ PULONG_PTR Information)
{
    PBTH_DEVICE_INFO_LIST List;
    PLIST_ENTRY Entry;
    size_t Size;
    SIZE_T Room;
    NTSTATUS Status;

    Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*List), (PVOID *)&List, &Size);
    if (!NT_SUCCESS(Status))
        return Status == STATUS_BUFFER_TOO_SMALL ? STATUS_INVALID_BUFFER_SIZE : Status;

    Room = (Size - sizeof(*List)) / sizeof(BTH_DEVICE_INFO) + 1;
    List->numOfDevices = 0;
    *Information = sizeof(*List);
    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        List->numOfDevices++;
        if (!Room)
            continue;
        BthRadioFillInfo(Remote, &List->deviceList[List->numOfDevices - 1]);
        if (List->numOfDevices > 1)
            *Information += sizeof(BTH_DEVICE_INFO);
        Room--;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthRadioDisconnect(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHMINI_REMOTE Remote;
    BTH_ADDR *Address;
    UCHAR Parameters[3];
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Address), (PVOID *)&Address, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_USER_BUFFER;
    Remote = BthRadioFindRemote(Dev, *Address);
    if (!Remote || !(Remote->Flags & BDIF_CONNECTED))
        return STATUS_DEVICE_NOT_CONNECTED;
    Parameters[0] = (UCHAR)Remote->Handle;
    Parameters[1] = (UCHAR)(Remote->Handle >> 8);
    Parameters[2] = 0x13;
    return BthHciCommand(Dev, HCI_DISCONNECT, Parameters, sizeof(Parameters), NULL, 0);
}

static
NTSTATUS
BthRadioSetFlag(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PULONG Flags;
    UCHAR ScanEnable = Dev->ScanEnable;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Flags), (PVOID *)&Flags, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    switch (*Flags & 0x3)
    {
        case LOCAL_RADIO_DISCOVERABLE:
            if (*Flags & 0x4)
                ScanEnable |= HCI_SCAN_INQUIRY | HCI_SCAN_PAGE;
            else
                ScanEnable &= ~HCI_SCAN_INQUIRY;
            break;

        case LOCAL_RADIO_CONNECTABLE:
            ScanEnable = (*Flags & 0x4) ? (ScanEnable | HCI_SCAN_PAGE) : 0;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }
    return ScanEnable == Dev->ScanEnable ? STATUS_SUCCESS : BthHciWriteScanEnable(Dev, ScanEnable);
}

static
NTSTATUS
BthRadioStopDiscovery(
    _In_ PBTHMINI_DEVICE Dev)
{
    Dev->Discovering = FALSE;
    BthLeSetScan(Dev, FALSE);
    if (!Dev->Inquiring)
        return STATUS_SUCCESS;
    Dev->Inquiring = FALSE;
    BthHciCommand(Dev, HCI_INQUIRY_CANCEL, NULL, 0, NULL, 0);
    BthRadioContinueDiscovery(Dev);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthRadioSubmitRecord(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request,
    _In_ BOOLEAN WithInfo,
    _Out_ PULONG_PTR Information)
{
    PHANDLE_SDP Output;
    PUCHAR Input;
    size_t InputLength;
    const UCHAR *Record;
    ULONG RecordLength, Handle, CodService = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, WithInfo ? FIELD_OFFSET(BTH_SDP_RECORD, record) : 2,
                                           (PVOID *)&Input, &InputLength);
    if (NT_SUCCESS(Status))
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Output), (PVOID *)&Output, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;
    if (WithInfo)
    {
        PBTH_SDP_RECORD Info = (PBTH_SDP_RECORD)Input;

        if (Info->recordLength > InputLength - FIELD_OFFSET(BTH_SDP_RECORD, record))
            return STATUS_INVALID_PARAMETER;
        Record = Info->record;
        RecordLength = Info->recordLength;
        CodService = Info->fCodService;
    }
    else
    {
        Record = Input;
        RecordLength = (ULONG)InputLength;
    }
    Status = BthSdpAddRecord(Dev, Record, RecordLength, WdfRequestGetFileObject(Request), &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    if (CodService && !(Dev->ClassOfDevice & (CodService << COD_SERVICE_BIT_OFFSET)))
    {
        UCHAR Parameters[3];

        Dev->ClassOfDevice |= (CodService << COD_SERVICE_BIT_OFFSET) & COD_SERVICE_MASK;
        Parameters[0] = (UCHAR)Dev->ClassOfDevice;
        Parameters[1] = (UCHAR)(Dev->ClassOfDevice >> 8);
        Parameters[2] = (UCHAR)(Dev->ClassOfDevice >> 16);
        BthHciQueueCommand(Dev, HCI_WRITE_CLASS_OF_DEVICE, Parameters, sizeof(Parameters));
    }
    *Output = Handle;
    *Information = sizeof(*Output);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthRadioRemoveRecord(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PHANDLE_SDP Handle;

    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(*Handle), (PVOID *)&Handle, NULL)))
        return STATUS_INVALID_PARAMETER;
    if (*Handle > 0xFFFFFFFF)
        return STATUS_INVALID_PARAMETER;
    return BthSdpRemoveRecord(Dev, (ULONG)*Handle);
}

VOID
BthRadioDeviceControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS Parameters;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    if (BthMiniGetRequest(Request) && BthMiniGetRequest(Request)->Gatt)
    {
        BthGattControl(Dev, Request);
        return;
    }
    if (Parameters.Type == WdfRequestTypeDeviceControlInternal)
    {
        BthBrbInternalControl(Dev, Request);
        return;
    }
    if (Parameters.Type != WdfRequestTypeDeviceControl)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    switch (Parameters.Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_BTH_GET_LOCAL_INFO:
            Status = BthRadioGetLocalInfo(Dev, Request, &Information);
            break;

        case IOCTL_BTH_GET_RADIO_INFO:
            Status = BthRadioGetRadioInfo(Dev, Request, &Information);
            break;

        case IOCTL_BTH_GET_DEVICE_INFO:
            Status = BthRadioGetDeviceInfo(Dev, Request, &Information);
            break;

        case IOCTL_BTH_GET_HOST_SUPPORTED_FEATURES:
            Status = BthRadioGetHostFeatures(Dev, Request, &Information);
            break;

        case IOCTL_BTH_SDP_CONNECT:
        case IOCTL_BTH_SDP_DISCONNECT:
        case IOCTL_BTH_SDP_SERVICE_SEARCH:
        case IOCTL_BTH_SDP_ATTRIBUTE_SEARCH:
        case IOCTL_BTH_SDP_SERVICE_ATTRIBUTE_SEARCH:
            Status = BthSdpClientControl(Dev, Request, Parameters.Parameters.DeviceIoControl.IoControlCode,
                                         &Information);
            if (Status == STATUS_PENDING)
                return;
            break;

        case IOCTL_BTH_DISCONNECT_DEVICE:
            Status = BthRadioDisconnect(Dev, Request);
            break;

        case IOCTL_BTH_SDP_SUBMIT_RECORD:
        case IOCTL_BTH_SDP_SUBMIT_RECORD_WITH_INFO:
            Status = BthRadioSubmitRecord(Dev, Request,
                                          Parameters.Parameters.DeviceIoControl.IoControlCode ==
                                              IOCTL_BTH_SDP_SUBMIT_RECORD_WITH_INFO,
                                          &Information);
            break;

        case IOCTL_BTH_SDP_REMOVE_RECORD:
            Status = BthRadioRemoveRecord(Dev, Request);
            break;

        case IOCTL_BTHMINI_SET_LOCAL_SERVICE:
            Status = BthEnumSetLocalService(Dev, Request);
            break;

        case IOCTL_WINEBTH_RADIO_SET_FLAG:
            Status = BthRadioSetFlag(Dev, Request);
            break;

        case IOCTL_WINEBTH_RADIO_START_DISCOVERY:
            Dev->Discovering = TRUE;
            BthLeSetScan(Dev, TRUE);
            BthRadioContinueDiscovery(Dev);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_WINEBTH_RADIO_STOP_DISCOVERY:
            Status = BthRadioStopDiscovery(Dev);
            break;

        case IOCTL_WINEBTH_RADIO_START_AUTH:
            Status = BthConnStartAuthentication(Dev, Request);
            if (Status == STATUS_PENDING)
                return;
            break;

        case IOCTL_WINEBTH_RADIO_REMOVE_DEVICE:
            Status = BthConnRemoveDevice(Dev, Request);
            break;

        case IOCTL_WINEBTH_RADIO_SEND_AUTH_RESPONSE:
            Status = BthConnSendAuthResponse(Dev, Request, &Information);
            if (Status == STATUS_PENDING)
                return;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    WdfRequestCompleteWithInformation(Request, Status, NT_SUCCESS(Status) ? Information : 0);
}
