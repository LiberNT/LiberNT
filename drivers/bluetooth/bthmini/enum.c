/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service enumeration of paired devices for Bluetooth profile drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"

#define NDEBUG
#include <debug.h>

#define BTH_SDP_ATTRIBUTE_SERVICE_NAME      0x0100
#define BTH_SDP_ATTRIBUTE_PNP_VENDOR_ID     0x0201
#define BTH_SDP_ATTRIBUTE_PNP_PRODUCT_ID    0x0202
#define BTH_SDP_ATTRIBUTE_PNP_VERSION       0x0203
#define BTH_SDP_ATTRIBUTE_PNP_VENDOR_SOURCE 0x0205
#define BTHMINI_MAX_RECORD                  0x4000

typedef struct _BTHMINI_SERVICE
{
    LIST_ENTRY Link;
    BTH_ADDR Address;
    USHORT Uuid;
    WDFDEVICE Pdo;
} BTHMINI_SERVICE, *PBTHMINI_SERVICE;

static const USHORT BthEnumProfiles[] =
{
    BTH_HID_UUID16,
    AudioSinkServiceClassID_UUID16,
    AVRemoteControlServiceClassID_UUID16
};

typedef struct _BTHMINI_DISCOVERY
{
    ULONG Index;
    PUCHAR Records[RTL_NUMBER_OF(BthEnumProfiles)];
    ULONG RecordLengths[RTL_NUMBER_OF(BthEnumProfiles)];
} BTHMINI_DISCOVERY, *PBTHMINI_DISCOVERY;

static
VOID
BthEnumServiceGuid(
    _In_ USHORT Uuid,
    _Out_ GUID *Guid)
{
    *Guid = Bluetooth_Base_UUID;
    Guid->Data1 = Uuid;
}

static
VOID
BthEnumValueName(
    _In_ USHORT Uuid,
    _Out_writes_(16) PWCHAR Name)
{
    RtlStringCchPrintfW(Name, 16, L"Service%04x", Uuid);
}

static
VOID
BthEnumSaveRecord(
    _In_ BTH_ADDR Address,
    _In_ USHORT Uuid,
    _In_reads_bytes_(Length) const UCHAR *Record,
    _In_ ULONG Length)
{
    UNICODE_STRING ValueName;
    WCHAR Device[13], Name[16];
    HANDLE Key;

    BthFormatAddress(Address, Device);
    if (!NT_SUCCESS(BthOpenParameters(L"Devices", Device, &Key)))
        return;
    BthEnumValueName(Uuid, Name);
    RtlInitUnicodeString(&ValueName, Name);
    ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, (PVOID)Record, Length);
    ZwClose(Key);
}

static
PUCHAR
BthEnumLoadRecord(
    _In_ BTH_ADDR Address,
    _In_ USHORT Uuid,
    _Out_ PULONG Length)
{
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    UNICODE_STRING ValueName;
    WCHAR Device[13], Name[16];
    PUCHAR Record = NULL;
    ULONG Size;
    HANDLE Key;

    *Length = 0;
    BthFormatAddress(Address, Device);
    if (!NT_SUCCESS(BthOpenParameters(L"Devices", Device, &Key)))
        return NULL;
    BthEnumValueName(Uuid, Name);
    RtlInitUnicodeString(&ValueName, Name);
    Value = ExAllocatePoolWithTag(PagedPool, sizeof(*Value) + BTHMINI_MAX_RECORD, BTHMINI_TAG);
    if (Value &&
        NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value,
                                   sizeof(*Value) + BTHMINI_MAX_RECORD, &Size)) &&
        Value->Type == REG_BINARY && Value->DataLength)
    {
        Record = ExAllocatePoolWithTag(PagedPool, Value->DataLength, BTHMINI_TAG);
        if (Record)
        {
            RtlCopyMemory(Record, Value->Data, Value->DataLength);
            *Length = Value->DataLength;
        }
    }
    if (Value)
        ExFreePoolWithTag(Value, BTHMINI_TAG);
    ZwClose(Key);
    return Record;
}

static
VOID
BthEnumReadServiceName(
    _In_reads_bytes_(Length) const UCHAR *Record,
    _In_ ULONG Length,
    _Out_writes_(BTH_MAX_SERVICE_NAME_SIZE) PCHAR Name)
{
    const UCHAR *Value;
    ULONG ValueLength, Header;

    Name[0] = '\0';
    if (!BthSdpFindAttribute(Record, Length, BTH_SDP_ATTRIBUTE_SERVICE_NAME, &Value, &ValueLength) ||
        ValueLength < 2 || (Value[0] >> 3) != 4)
    {
        return;
    }
    Header = (Value[0] & 7) == 5 ? 2 : (Value[0] & 7) == 6 ? 3 : (Value[0] & 7) == 7 ? 5 : 0;
    if (!Header || Header > ValueLength)
        return;
    RtlCopyMemory(Name, Value + Header, min(ValueLength - Header, BTH_MAX_SERVICE_NAME_SIZE - 1));
    Name[min(ValueLength - Header, BTH_MAX_SERVICE_NAME_SIZE - 1)] = '\0';
}

static
VOID
BthEnumReadPnpInfo(
    _In_reads_bytes_opt_(Length) const UCHAR *Record,
    _In_ ULONG Length,
    _Inout_ PBTHMINI_SERVICE_PDO Pdo)
{
    const UCHAR *Value;
    ULONG ValueLength, Number;

    if (!Record)
        return;
    if (BthSdpFindAttribute(Record, Length, BTH_SDP_ATTRIBUTE_PNP_VENDOR_ID, &Value, &ValueLength) &&
        BthSdpGetUint(Value, ValueLength, &Number))
        Pdo->VendorId = (USHORT)Number;
    if (BthSdpFindAttribute(Record, Length, BTH_SDP_ATTRIBUTE_PNP_PRODUCT_ID, &Value, &ValueLength) &&
        BthSdpGetUint(Value, ValueLength, &Number))
        Pdo->ProductId = (USHORT)Number;
    if (BthSdpFindAttribute(Record, Length, BTH_SDP_ATTRIBUTE_PNP_VERSION, &Value, &ValueLength) &&
        BthSdpGetUint(Value, ValueLength, &Number))
        Pdo->Version = (USHORT)Number;
    if (BthSdpFindAttribute(Record, Length, BTH_SDP_ATTRIBUTE_PNP_VENDOR_SOURCE, &Value, &ValueLength) &&
        BthSdpGetUint(Value, ValueLength, &Number))
        Pdo->VendorIdSource = (USHORT)Number;
    Pdo->HasPnpInfo = Pdo->VendorIdSource != 0;
}

static
ULONG
BthEnumBrbSize(
    _In_ BRB_TYPE Type)
{
    switch (Type)
    {
        case BRB_HCI_GET_LOCAL_BD_ADDR:
            return sizeof(struct _BRB_GET_LOCAL_BD_ADDR);
        case BRB_L2CA_REGISTER_SERVER:
            return sizeof(struct _BRB_L2CA_REGISTER_SERVER);
        case BRB_L2CA_UNREGISTER_SERVER:
            return sizeof(struct _BRB_L2CA_UNREGISTER_SERVER);
        case BRB_L2CA_OPEN_CHANNEL:
        case BRB_L2CA_OPEN_CHANNEL_RESPONSE:
            return sizeof(struct _BRB_L2CA_OPEN_CHANNEL);
        case BRB_L2CA_CLOSE_CHANNEL:
            return sizeof(struct _BRB_L2CA_CLOSE_CHANNEL);
        case BRB_L2CA_ACL_TRANSFER:
            return sizeof(struct _BRB_L2CA_ACL_TRANSFER);
        case BRB_L2CA_UPDATE_CHANNEL:
            return sizeof(struct _BRB_L2CA_UPDATE_CHANNEL);
        case BRB_L2CA_PING:
            return sizeof(struct _BRB_L2CA_PING);
        case BRB_REGISTER_PSM:
        case BRB_UNREGISTER_PSM:
            return sizeof(struct _BRB_PSM);
        case BRB_SCO_REGISTER_SERVER:
            return sizeof(struct _BRB_SCO_REGISTER_SERVER);
        case BRB_SCO_UNREGISTER_SERVER:
            return sizeof(struct _BRB_SCO_UNREGISTER_SERVER);
        case BRB_SCO_OPEN_CHANNEL:
        case BRB_SCO_OPEN_CHANNEL_RESPONSE:
            return sizeof(struct _BRB_SCO_OPEN_CHANNEL);
        case BRB_SCO_CLOSE_CHANNEL:
            return sizeof(struct _BRB_SCO_CLOSE_CHANNEL);
        case BRB_SCO_TRANSFER:
            return sizeof(struct _BRB_SCO_TRANSFER);
        case BRB_SCO_GET_CHANNEL_INFO:
            return sizeof(struct _BRB_SCO_GET_CHANNEL_INFO);
        case BRB_SCO_GET_SYSTEM_INFO:
            return sizeof(struct _BRB_SCO_GET_SYSTEM_INFO);
        case BRB_SCO_FLUSH_CHANNEL:
            return sizeof(struct _BRB_SCO_FLUSH_CHANNEL);
        case BRB_L2CA_OPEN_ENHANCED_CHANNEL:
        case BRB_L2CA_OPEN_ENHANCED_CHANNEL_RESPONSE:
            return sizeof(struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL);
        case BRB_ACL_GET_MODE:
            return sizeof(struct _BRB_ACL_GET_MODE);
        case BRB_ACL_ENTER_ACTIVE_MODE:
            return sizeof(struct _BRB_ACL_ENTER_ACTIVE_MODE);
        case BRB_GET_DEVICE_INTERFACE_STRING:
            return sizeof(struct _BRB_GET_DEVICE_INTERFACE_STRING);
        default:
            return sizeof(BRB);
    }
}

static
VOID
BthEnumInitializeBrb(
    _Inout_updates_(_Inexpressible_("varies")) PBRB Brb,
    _In_ BRB_TYPE Type)
{
    ULONG Size = BthEnumBrbSize(Type);

    RtlZeroMemory(Brb, Size);
    Brb->BrbHeader.Length = Size;
    Brb->BrbHeader.Type = (USHORT)Type;
}

static
PBRB
BthEnumAllocateBrb(
    _In_ BRB_TYPE Type,
    _In_ ULONG Tag)
{
    PBRB Brb = ExAllocatePoolWithTag(NonPagedPool, sizeof(BRB), Tag);

    if (Brb)
        BthEnumInitializeBrb(Brb, Type);
    return Brb;
}

static
VOID
BthEnumFreeBrb(
    _In_ __drv_freesMem(Mem) PBRB Brb)
{
    ExFreePool(Brb);
}

static
VOID
BthEnumReuseBrb(
    _Inout_ PBRB Brb,
    _In_ BRB_TYPE Type)
{
    BthEnumInitializeBrb(Brb, Type);
}

static
BOOLEAN
BthEnumIsVersionAvailable(
    _In_ UCHAR MajorVersion,
    _In_ UCHAR MinorVersion)
{
    return MajorVersion < 2 || (MajorVersion == 2 && MinorVersion <= 1);
}

static
VOID
NTAPI
BthEnumEvtPdoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PBTHMINI_SERVICE_PDO Pdo = BthEnumGetPdo(WdfIoQueueGetDevice(Queue));
    WDF_REQUEST_FORWARD_OPTIONS Forward;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode)
    {
        case IOCTL_BTH_GET_LOCAL_INFO:
        case IOCTL_BTH_GET_RADIO_INFO:
        case IOCTL_BTH_GET_DEVICE_INFO:
        case IOCTL_BTH_DISCONNECT_DEVICE:
        case IOCTL_BTH_SDP_CONNECT:
        case IOCTL_BTH_SDP_DISCONNECT:
        case IOCTL_BTH_SDP_SERVICE_SEARCH:
        case IOCTL_BTH_SDP_ATTRIBUTE_SEARCH:
        case IOCTL_BTH_SDP_SERVICE_ATTRIBUTE_SEARCH:
        case IOCTL_BTH_SDP_SUBMIT_RECORD:
        case IOCTL_BTH_SDP_REMOVE_RECORD:
        case IOCTL_BTH_SDP_SUBMIT_RECORD_WITH_INFO:
        case IOCTL_BTH_GET_HOST_SUPPORTED_FEATURES:
            BthMiniGetRequest(Request)->Address = Pdo->Address;
            WDF_REQUEST_FORWARD_OPTIONS_INIT(&Forward);
            Status = WdfRequestForwardToParentDeviceIoQueue(Request, Pdo->Dev->Queue, &Forward);
            if (NT_SUCCESS(Status))
                return;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    WdfRequestComplete(Request, Status);
}

static
VOID
NTAPI
BthEnumEvtPdoIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PBTHMINI_SERVICE_PDO Pdo = BthEnumGetPdo(WdfIoQueueGetDevice(Queue));
    WDF_REQUEST_FORWARD_OPTIONS Forward;
    WDF_REQUEST_PARAMETERS Parameters;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    switch (IoControlCode)
    {
        case IOCTL_INTERNAL_BTH_SUBMIT_BRB:
        case IOCTL_INTERNAL_BTHENUM_GET_DEVINFO:
            if (!Parameters.Parameters.Others.Arg1)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            BthMiniGetRequest(Request)->Address = Pdo->Address;
            WDF_REQUEST_FORWARD_OPTIONS_INIT(&Forward);
            Status = WdfRequestForwardToParentDeviceIoQueue(Request, Pdo->Dev->Queue, &Forward);
            if (NT_SUCCESS(Status))
                return;
            break;

        case IOCTL_INTERNAL_BTHENUM_GET_ENUMINFO:
        {
            PBTH_ENUMERATOR_INFO Info = Parameters.Parameters.Others.Arg1;

            if (!Info)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            RtlZeroMemory(Info, sizeof(*Info));
            Info->EnumeratorType = ENUMERATOR_TYPE_SERVICE;
            Info->Action = ENUMERATOR_ACTION_CREATE;
            Info->Flags = Pdo->Local ? BTH_ENUMERATORFL_INCOMING : BTH_ENUMERATORFL_OUTGOING | BTH_ENUMERATORFL_INCOMING;
            if (Pdo->Local)
                Info->Guid = Pdo->ServiceClass;
            else
                BthEnumServiceGuid(Pdo->Uuid, &Info->Guid);
            Info->Vid = Pdo->VendorId;
            Info->Pid = Pdo->ProductId;
            Info->VidType = Pdo->VendorIdSource;
            Info->LocalMfg = Pdo->Dev->Manufacturer;
            RtlStringCbCopyA(Info->SdpPriLangServiceName, sizeof(Info->SdpPriLangServiceName), Pdo->ServiceName);
            RtlStringCbPrintfW(Info->ServiceName, sizeof(Info->ServiceName), L"%hs", Pdo->ServiceName);
            RtlStringCbPrintfW(Info->DeviceString, sizeof(Info->DeviceString), L"%hs", Pdo->DeviceName);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_INTERNAL_BTHMINI_GET_SERVICE_RECORD:
        {
            PULONG Length = Parameters.Parameters.Others.Arg2;

            if (!Length)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (!Parameters.Parameters.Others.Arg1 || *Length < Pdo->RecordLength)
            {
                *Length = Pdo->RecordLength;
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            RtlCopyMemory(Parameters.Parameters.Others.Arg1, Pdo->Record, Pdo->RecordLength);
            *Length = Pdo->RecordLength;
            Status = STATUS_SUCCESS;
            break;
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    WdfRequestComplete(Request, Status);
}

static
VOID
NTAPI
BthEnumEvtPdoCleanup(
    _In_ WDFOBJECT Object)
{
    PBTHMINI_SERVICE_PDO Pdo = BthEnumGetPdo((WDFDEVICE)Object);

    if (Pdo->Record)
        ExFreePoolWithTag(Pdo->Record, BTHMINI_TAG);
    Pdo->Record = NULL;
}

static
NTSTATUS
BthEnumCreatePdo(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Uuid,
    _In_reads_bytes_(RecordLength) const UCHAR *Record,
    _In_ ULONG RecordLength,
    _In_reads_bytes_opt_(PnpLength) const UCHAR *PnpRecord,
    _In_ ULONG PnpLength)
{
    DECLARE_UNICODE_STRING_SIZE(DeviceId, 128);
    DECLARE_UNICODE_STRING_SIZE(LocalId, 128);
    DECLARE_UNICODE_STRING_SIZE(CompatibleId, 96);
    DECLARE_UNICODE_STRING_SIZE(InstanceId, 32);
    DECLARE_UNICODE_STRING_SIZE(Description, BTH_MAX_NAME_SIZE);
    BTH_PROFILE_DRIVER_INTERFACE Interface;
    WDF_QUERY_INTERFACE_CONFIG InterfaceConfig;
    WDF_DEVICE_PNP_CAPABILITIES Capabilities;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PBTHMINI_SERVICE_PDO Pdo;
    PBTHMINI_SERVICE Service;
    PWDFDEVICE_INIT Init;
    BTHMINI_SERVICE_PDO Info;
    PLIST_ENTRY Entry;
    WDFDEVICE Child;
    WCHAR Guid[40];
    GUID ServiceGuid;
    NTSTATUS Status;

    for (Entry = Dev->Services.Flink; Entry != &Dev->Services; Entry = Entry->Flink)
    {
        Service = CONTAINING_RECORD(Entry, BTHMINI_SERVICE, Link);
        if (Service->Address == Remote->Address && Service->Uuid == Uuid)
            return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Info, sizeof(Info));
    BthEnumReadPnpInfo(PnpRecord, PnpLength, &Info);
    BthEnumReadServiceName(Record, RecordLength, Info.ServiceName);
    RtlCopyMemory(Info.DeviceName, Remote->Name, sizeof(Info.DeviceName));
    BthEnumServiceGuid(Uuid, &ServiceGuid);
    Status = RtlStringCchPrintfW(Guid, RTL_NUMBER_OF(Guid),
                                 L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                                 ServiceGuid.Data1, ServiceGuid.Data2, ServiceGuid.Data3,
                                 ServiceGuid.Data4[0], ServiceGuid.Data4[1], ServiceGuid.Data4[2], ServiceGuid.Data4[3],
                                 ServiceGuid.Data4[4], ServiceGuid.Data4[5], ServiceGuid.Data4[6], ServiceGuid.Data4[7]);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&LocalId, L"BTHENUM\\%ws_LOCALMFG&%04x", Guid, Dev->Manufacturer);
    if (NT_SUCCESS(Status) && Info.HasPnpInfo)
        Status = RtlUnicodeStringPrintf(&DeviceId, L"BTHENUM\\%ws_VID&%04x%04x_PID&%04x", Guid,
                                        Info.VendorIdSource, Info.VendorId, Info.ProductId);
    else if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&DeviceId, L"%wZ", &LocalId);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&CompatibleId, L"BTHENUM\\%ws", Guid);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&InstanceId, L"%012I64X_C00000000", Remote->Address);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&Description, L"%hs", Info.ServiceName[0] ? Info.ServiceName : Remote->Name);
    if (!NT_SUCCESS(Status))
        return Status;

    Init = WdfPdoInitAllocate(Dev->Device);
    if (!Init)
        return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_BLUETOOTH);
    WdfPdoInitAllowForwardingRequestToParent(Init);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_REQUEST);
    WdfDeviceInitSetRequestAttributes(Init, &Attributes);
    Status = WdfPdoInitAssignDeviceID(Init, &DeviceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddHardwareID(Init, &DeviceId);
    if (NT_SUCCESS(Status) && Info.HasPnpInfo)
        Status = WdfPdoInitAddHardwareID(Init, &LocalId);
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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_SERVICE_PDO);
    Attributes.EvtCleanupCallback = BthEnumEvtPdoCleanup;
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    Pdo = BthEnumGetPdo(Child);
    *Pdo = Info;
    Pdo->Dev = Dev;
    Pdo->Address = Remote->Address;
    Pdo->Uuid = Uuid;
    Pdo->Record = ExAllocatePoolWithTag(NonPagedPool, RecordLength, BTHMINI_TAG);
    if (!Pdo->Record)
    {
        WdfObjectDelete(Child);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(Pdo->Record, Record, RecordLength);
    Pdo->RecordLength = RecordLength;

    WDF_DEVICE_PNP_CAPABILITIES_INIT(&Capabilities);
    Capabilities.Removable = WdfTrue;
    Capabilities.SurpriseRemovalOK = WdfTrue;
    Capabilities.UniqueID = WdfTrue;
    WdfDeviceSetPnpCapabilities(Child, &Capabilities);

    RtlZeroMemory(&Interface, sizeof(Interface));
    Interface.Interface.Size = sizeof(Interface);
    Interface.Interface.Version = BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI;
    Interface.Interface.Context = Dev;
    Interface.Interface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
    Interface.Interface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
    Interface.BthAllocateBrb = BthEnumAllocateBrb;
    Interface.BthFreeBrb = BthEnumFreeBrb;
    Interface.BthInitializeBrb = BthEnumInitializeBrb;
    Interface.BthReuseBrb = BthEnumReuseBrb;
    Interface.IsBluetoothVersionAvailable = BthEnumIsVersionAvailable;
    WDF_QUERY_INTERFACE_CONFIG_INIT(&InterfaceConfig, &Interface.Interface, &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE, NULL);
    Status = WdfDeviceAddQueryInterface(Child, &InterfaceConfig);

    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
        QueueConfig.EvtIoInternalDeviceControl = BthEnumEvtPdoIoctl;
        QueueConfig.EvtIoDeviceControl = BthEnumEvtPdoDeviceControl;
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    }
    Service = NT_SUCCESS(Status) ? ExAllocatePoolZero(NonPagedPool, sizeof(*Service), BTHMINI_TAG) : NULL;
    if (NT_SUCCESS(Status) && !Service)
        Status = STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Dev->Device, Child);
    if (!NT_SUCCESS(Status))
    {
        if (Service)
            ExFreePoolWithTag(Service, BTHMINI_TAG);
        WdfObjectDelete(Child);
        return Status;
    }
    Service->Address = Remote->Address;
    Service->Uuid = Uuid;
    Service->Pdo = Child;
    InsertTailList(&Dev->Services, &Service->Link);
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    DPRINT1("BTHMINI: %wZ for %012I64x enumerated\n", &DeviceId, Remote->Address);
    return STATUS_SUCCESS;
}

static
VOID
BthEnumCreateFromCache(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Uuid)
{
    PUCHAR Record, Pnp;
    ULONG Length, PnpLength;
    NTSTATUS Status;

    Record = BthEnumLoadRecord(Remote->Address, Uuid, &Length);
    if (!Record)
        return;
    Pnp = BthEnumLoadRecord(Remote->Address, BTH_PNP_UUID16, &PnpLength);
    Status = BthEnumCreatePdo(Dev, Remote, Uuid, Record, Length, Pnp, PnpLength);
    if (!NT_SUCCESS(Status))
        DPRINT1("BTHMINI: service 0x%04x of %012I64x not enumerated 0x%08lx\n", Uuid, Remote->Address, Status);
    if (Pnp)
        ExFreePoolWithTag(Pnp, BTHMINI_TAG);
    ExFreePoolWithTag(Record, BTHMINI_TAG);
}

VOID
BthEnumLoad(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;
    ULONG Index;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);

        if (!(Remote->Flags & BDIF_PAIRED))
            continue;
        for (Index = 0; Index < RTL_NUMBER_OF(BthEnumProfiles); Index++)
            BthEnumCreateFromCache(Dev, Remote, BthEnumProfiles[Index]);
    }
}

static BTHMINI_SDP_RECORDS_CALLBACK BthEnumRecordsDone;

static
USHORT
BthEnumDiscoveryUuid(
    _In_ PBTHMINI_DISCOVERY Discovery)
{
    return Discovery->Index < RTL_NUMBER_OF(BthEnumProfiles) ? BthEnumProfiles[Discovery->Index] : BTH_PNP_UUID16;
}

static
BOOLEAN
BthEnumDiscoveryFound(
    _In_ PBTHMINI_DISCOVERY Discovery)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(BthEnumProfiles); Index++)
    {
        if (Discovery->Records[Index])
            return TRUE;
    }
    return FALSE;
}

static
VOID
BthEnumFreeDiscovery(
    _In_ PBTHMINI_DISCOVERY Discovery)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(BthEnumProfiles); Index++)
    {
        if (Discovery->Records[Index])
            ExFreePoolWithTag(Discovery->Records[Index], BTHMINI_TAG);
    }
    ExFreePoolWithTag(Discovery, BTHMINI_TAG);
}

static
VOID
BthEnumCreateDiscovered(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ PBTHMINI_DISCOVERY Discovery,
    _In_reads_bytes_opt_(PnpLength) const UCHAR *Pnp,
    _In_ ULONG PnpLength)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(BthEnumProfiles); Index++)
    {
        NTSTATUS Status;

        if (!Discovery->Records[Index])
            continue;
        Status = BthEnumCreatePdo(Dev, Remote, BthEnumProfiles[Index], Discovery->Records[Index],
                                  Discovery->RecordLengths[Index], Pnp, PnpLength);
        if (!NT_SUCCESS(Status))
            DPRINT1("BTHMINI: service 0x%04x of %012I64x not enumerated 0x%08lx\n", BthEnumProfiles[Index],
                    Remote->Address, Status);
    }
    BthEnumFreeDiscovery(Discovery);
}

static
VOID
BthEnumNextQuery(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ PBTHMINI_DISCOVERY Discovery)
{
    for (;;)
    {
        USHORT Uuid = BthEnumDiscoveryUuid(Discovery);

        if (Uuid == BTH_PNP_UUID16 && !BthEnumDiscoveryFound(Discovery))
            break;
        if (NT_SUCCESS(BthSdpQueryRecords(Dev, Remote, Uuid, BthEnumRecordsDone, Discovery)))
            return;
        DPRINT1("BTHMINI: service 0x%04x query on %012I64x not started\n", Uuid, Remote->Address);
        if (Uuid == BTH_PNP_UUID16)
        {
            BthEnumCreateDiscovered(Dev, Remote, Discovery, NULL, 0);
            return;
        }
        Discovery->Index++;
    }
    BthEnumFreeDiscovery(Discovery);
}

static
VOID
BthEnumRecordsDone(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_opt_ PVOID Context,
    _In_ NTSTATUS Status,
    _In_reads_bytes_opt_(Length) const UCHAR *Records,
    _In_ ULONG Length)
{
    PBTHMINI_DISCOVERY Discovery = Context;
    USHORT Uuid = BthEnumDiscoveryUuid(Discovery);
    const UCHAR *Record = NULL;
    ULONG Offset = 0, RecordLength = 0;

    if (NT_SUCCESS(Status) && Records)
        BthSdpNextRecord(Records, Length, &Offset, &Record, &RecordLength);
    if (Record)
        BthEnumSaveRecord(Remote->Address, Uuid, Record, RecordLength);

    if (Uuid != BTH_PNP_UUID16)
    {
        if (Record)
        {
            Discovery->Records[Discovery->Index] = ExAllocatePoolWithTag(NonPagedPool, RecordLength, BTHMINI_TAG);
            if (Discovery->Records[Discovery->Index])
            {
                RtlCopyMemory(Discovery->Records[Discovery->Index], Record, RecordLength);
                Discovery->RecordLengths[Discovery->Index] = RecordLength;
            }
        }
        Discovery->Index++;
        BthEnumNextQuery(Dev, Remote, Discovery);
        return;
    }
    BthEnumCreateDiscovered(Dev, Remote, Discovery, Record, RecordLength);
}

VOID
BthEnumDiscover(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_DISCOVERY Discovery = ExAllocatePoolZero(NonPagedPool, sizeof(*Discovery), BTHMINI_TAG);

    if (!Discovery)
        return;
    BthEnumNextQuery(Dev, Remote, Discovery);
}

VOID
BthEnumRemoveDevice(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address)
{
    PLIST_ENTRY Entry = Dev->Services.Flink;
    BOOLEAN Removed = FALSE;

    while (Entry != &Dev->Services)
    {
        PBTHMINI_SERVICE Service = CONTAINING_RECORD(Entry, BTHMINI_SERVICE, Link);

        Entry = Entry->Flink;
        if (Service->Address != Address)
            continue;
        RemoveEntryList(&Service->Link);
        if (!NT_SUCCESS(WdfPdoMarkMissing(Service->Pdo)))
            DPRINT1("BTHMINI: service 0x%04x of %012I64x not marked missing\n", Service->Uuid, Address);
        ExFreePoolWithTag(Service, BTHMINI_TAG);
        Removed = TRUE;
    }
    if (Removed)
        IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
}

VOID
BthEnumFlush(
    _In_ PBTHMINI_DEVICE Dev)
{
    while (!IsListEmpty(&Dev->Services))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Dev->Services), BTHMINI_SERVICE, Link), BTHMINI_TAG);
}

typedef struct _BTHMINI_LOCAL_PDO
{
    LIST_ENTRY Link;
    GUID ServiceClass;
    ULONG Instance;
    WDFDEVICE Pdo;
} BTHMINI_LOCAL_PDO, *PBTHMINI_LOCAL_PDO;

static const GUID BthEnumSerialPort = { 0x00001101, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };

static
VOID
BthEnumFormatGuid(
    _In_ const GUID *Guid,
    _Out_writes_(40) PWCHAR Buffer)
{
    RtlStringCchPrintfW(Buffer, 40, L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", Guid->Data1, Guid->Data2,
                        Guid->Data3, Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3], Guid->Data4[4],
                        Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
}

static
PBTHMINI_LOCAL_PDO
BthEnumFindLocal(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ const GUID *ServiceClass,
    _In_ ULONG Instance)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->LocalServices.Flink; Entry != &Dev->LocalServices; Entry = Entry->Flink)
    {
        PBTHMINI_LOCAL_PDO Local = CONTAINING_RECORD(Entry, BTHMINI_LOCAL_PDO, Link);

        if (Local->Instance == Instance && IsEqualGUID(&Local->ServiceClass, ServiceClass))
            return Local;
    }
    return NULL;
}

static
NTSTATUS
BthEnumCreateLocalPdo(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ const BTHMINI_LOCAL_SERVICE *Info)
{
    DECLARE_UNICODE_STRING_SIZE(DeviceId, 128);
    DECLARE_UNICODE_STRING_SIZE(CompatibleId, 96);
    DECLARE_UNICODE_STRING_SIZE(InstanceId, 32);
    DECLARE_UNICODE_STRING_SIZE(Description, BTHMINI_LOCAL_SERVICE_NAME_SIZE);
    BTH_PROFILE_DRIVER_INTERFACE Interface;
    WDF_QUERY_INTERFACE_CONFIG InterfaceConfig;
    WDF_DEVICE_PNP_CAPABILITIES Capabilities;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PBTHMINI_SERVICE_PDO Pdo;
    PBTHMINI_LOCAL_PDO Local;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Child;
    WCHAR Guid[40];
    ULONG Count = 0;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    for (Entry = Dev->LocalServices.Flink; Entry != &Dev->LocalServices; Entry = Entry->Flink)
        Count++;
    if (Count >= BTHMINI_MAX_LOCAL_SERVICES)
        return STATUS_INSUFFICIENT_RESOURCES;
    BthEnumFormatGuid(&Info->ServiceClass, Guid);
    Status = RtlUnicodeStringPrintf(&DeviceId, L"BTHENUM\\%ws_LOCALMFG&%04x", Guid, Dev->Manufacturer);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&CompatibleId, L"BTHENUM\\%ws", Guid);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&InstanceId, L"LOCAL&%08X", Info->Instance);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&Description, L"%ws",
                                        Info->DeviceString[0] ? Info->DeviceString :
                                        Info->Name[0] ? Info->Name : L"Bluetooth local service");
    if (!NT_SUCCESS(Status))
        return Status;
    Local = ExAllocatePoolZero(NonPagedPool, sizeof(*Local), BTHMINI_TAG);
    if (!Local)
        return STATUS_INSUFFICIENT_RESOURCES;
    Init = WdfPdoInitAllocate(Dev->Device);
    if (!Init)
    {
        ExFreePoolWithTag(Local, BTHMINI_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_BLUETOOTH);
    WdfPdoInitAllowForwardingRequestToParent(Init);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_REQUEST);
    WdfDeviceInitSetRequestAttributes(Init, &Attributes);
    Status = WdfPdoInitAssignDeviceID(Init, &DeviceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddHardwareID(Init, &DeviceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddCompatibleID(Init, &CompatibleId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignInstanceID(Init, &InstanceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddDeviceText(Init, &Description, &Description, 0x409);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        ExFreePoolWithTag(Local, BTHMINI_TAG);
        return Status;
    }
    WdfPdoInitSetDefaultLocale(Init, 0x409);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_SERVICE_PDO);
    Attributes.EvtCleanupCallback = BthEnumEvtPdoCleanup;
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        ExFreePoolWithTag(Local, BTHMINI_TAG);
        return Status;
    }
    Pdo = BthEnumGetPdo(Child);
    RtlZeroMemory(Pdo, sizeof(*Pdo));
    Pdo->Dev = Dev;
    Pdo->Local = TRUE;
    Pdo->ServiceClass = Info->ServiceClass;
    Pdo->Address = Info->Address;

    WDF_DEVICE_PNP_CAPABILITIES_INIT(&Capabilities);
    Capabilities.Removable = WdfTrue;
    Capabilities.SurpriseRemovalOK = WdfTrue;
    Capabilities.UniqueID = WdfTrue;
    WdfDeviceSetPnpCapabilities(Child, &Capabilities);

    RtlZeroMemory(&Interface, sizeof(Interface));
    Interface.Interface.Size = sizeof(Interface);
    Interface.Interface.Version = BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI;
    Interface.Interface.Context = Dev;
    Interface.Interface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
    Interface.Interface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
    Interface.BthAllocateBrb = BthEnumAllocateBrb;
    Interface.BthFreeBrb = BthEnumFreeBrb;
    Interface.BthInitializeBrb = BthEnumInitializeBrb;
    Interface.BthReuseBrb = BthEnumReuseBrb;
    Interface.IsBluetoothVersionAvailable = BthEnumIsVersionAvailable;
    WDF_QUERY_INTERFACE_CONFIG_INIT(&InterfaceConfig, &Interface.Interface, &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE, NULL);
    Status = WdfDeviceAddQueryInterface(Child, &InterfaceConfig);
    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
        QueueConfig.EvtIoInternalDeviceControl = BthEnumEvtPdoIoctl;
        QueueConfig.EvtIoDeviceControl = BthEnumEvtPdoDeviceControl;
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    }
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Dev->Device, Child);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Child);
        ExFreePoolWithTag(Local, BTHMINI_TAG);
        return Status;
    }
    Local->ServiceClass = Info->ServiceClass;
    Local->Instance = Info->Instance;
    Local->Pdo = Child;
    InsertTailList(&Dev->LocalServices, &Local->Link);
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    DPRINT1("BTHMINI: local service %wZ instance %lu enumerated\n", &DeviceId, Info->Instance);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthEnumApplyLocalService(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ const BTHMINI_LOCAL_SERVICE *Info)
{
    PBTHMINI_LOCAL_PDO Local;

    if (IsEqualGUID(&Info->ServiceClass, &BthEnumSerialPort))
        return BthComSetIncoming(Dev, Info->Instance, Info->Enabled != 0, Info->Name);
    Local = BthEnumFindLocal(Dev, &Info->ServiceClass, Info->Instance);
    if (Info->Enabled)
        return Local ? STATUS_SUCCESS : BthEnumCreateLocalPdo(Dev, Info);
    if (!Local)
        return STATUS_SUCCESS;
    RemoveEntryList(&Local->Link);
    if (!NT_SUCCESS(WdfPdoMarkMissing(Local->Pdo)))
        DPRINT1("BTHMINI: local service instance %lu not marked missing\n", Local->Instance);
    ExFreePoolWithTag(Local, BTHMINI_TAG);
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    return STATUS_SUCCESS;
}

static
VOID
BthEnumStoreLocalService(
    _In_ const BTHMINI_LOCAL_SERVICE *Info)
{
    UNICODE_STRING ValueName;
    WCHAR Guid[40], Instance[12];
    HANDLE Key, InstanceKey;
    ULONG Enabled = 1;

    BthEnumFormatGuid(&Info->ServiceClass, Guid);
    if (!NT_SUCCESS(BthOpenParameters(L"LocalServices", Guid, &Key)))
        return;
    RtlStringCchPrintfW(Instance, RTL_NUMBER_OF(Instance), L"%lu", Info->Instance);
    if (NT_SUCCESS(BthCreateKey(Key, Instance, &InstanceKey)))
    {
        if (Info->Enabled)
        {
            RtlInitUnicodeString(&ValueName, L"Enabled");
            ZwSetValueKey(InstanceKey, &ValueName, 0, REG_DWORD, &Enabled, sizeof(Enabled));
            RtlInitUnicodeString(&ValueName, L"Address");
            ZwSetValueKey(InstanceKey, &ValueName, 0, REG_QWORD, (PVOID)&Info->Address, sizeof(Info->Address));
            RtlInitUnicodeString(&ValueName, L"Name");
            ZwSetValueKey(InstanceKey, &ValueName, 0, REG_SZ, (PVOID)Info->Name,
                          (ULONG)(wcslen(Info->Name) + 1) * sizeof(WCHAR));
            RtlInitUnicodeString(&ValueName, L"DeviceString");
            ZwSetValueKey(InstanceKey, &ValueName, 0, REG_SZ, (PVOID)Info->DeviceString,
                          (ULONG)(wcslen(Info->DeviceString) + 1) * sizeof(WCHAR));
        }
        else
        {
            ZwDeleteKey(InstanceKey);
        }
        ZwClose(InstanceKey);
    }
    ZwClose(Key);
}

NTSTATUS
BthEnumSetLocalService(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHMINI_LOCAL_SERVICE Info;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Info), (PVOID *)&Info, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;
    Info->Name[RTL_NUMBER_OF(Info->Name) - 1] = UNICODE_NULL;
    Info->DeviceString[RTL_NUMBER_OF(Info->DeviceString) - 1] = UNICODE_NULL;
    Status = BthEnumApplyLocalService(Dev, Info);
    if (NT_SUCCESS(Status))
        BthEnumStoreLocalService(Info);
    return Status;
}

static
BOOLEAN
BthEnumParseGuid(
    _In_reads_(Length) PCWCH String,
    _In_ ULONG Length,
    _Out_ GUID *Guid)
{
    UNICODE_STRING Text;

    Text.Buffer = (PWCH)String;
    Text.Length = Text.MaximumLength = (USHORT)(Length * sizeof(WCHAR));
    return NT_SUCCESS(RtlGUIDFromString(&Text, Guid));
}

static
VOID
BthEnumReadString(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_(Capacity) PWCHAR Buffer,
    _In_ ULONG Capacity)
{
    UCHAR Data[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + BTHMINI_LOCAL_SERVICE_NAME_SIZE * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Data;
    UNICODE_STRING ValueName;
    ULONG Size;

    Buffer[0] = UNICODE_NULL;
    RtlInitUnicodeString(&ValueName, Name);
    if (NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Data), &Size)) &&
        Value->Type == REG_SZ)
    {
        ULONG Chars = min(Value->DataLength / sizeof(WCHAR), Capacity - 1);

        RtlCopyMemory(Buffer, Value->Data, Chars * sizeof(WCHAR));
        Buffer[Chars] = UNICODE_NULL;
    }
}

VOID
BthEnumLoadLocalServices(
    _In_ PBTHMINI_DEVICE Dev)
{
    UCHAR ClassData[sizeof(KEY_BASIC_INFORMATION) + 80], InstanceData[sizeof(KEY_BASIC_INFORMATION) + 40];
    PKEY_BASIC_INFORMATION ClassInfo = (PKEY_BASIC_INFORMATION)ClassData;
    PKEY_BASIC_INFORMATION InstanceInfo = (PKEY_BASIC_INFORMATION)InstanceData;
    PBTHMINI_LOCAL_SERVICE Info;
    HANDLE Root, ClassKey, InstanceKey;
    ULONG ClassIndex, InstanceIndex, Size;

    if (!NT_SUCCESS(BthOpenParameters(L"LocalServices", NULL, &Root)))
        return;
    Info = ExAllocatePoolWithTag(PagedPool, sizeof(*Info), BTHMINI_TAG);
    for (ClassIndex = 0; Info && NT_SUCCESS(ZwEnumerateKey(Root, ClassIndex, KeyBasicInformation, ClassInfo,
                                                           sizeof(ClassData) - sizeof(WCHAR), &Size)); ClassIndex++)
    {
        WCHAR ClassName[40];

        if (ClassInfo->NameLength / sizeof(WCHAR) >= RTL_NUMBER_OF(ClassName))
            continue;
        RtlCopyMemory(ClassName, ClassInfo->Name, ClassInfo->NameLength);
        ClassName[ClassInfo->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
        RtlZeroMemory(Info, sizeof(*Info));
        if (!BthEnumParseGuid(ClassName, ClassInfo->NameLength / sizeof(WCHAR), &Info->ServiceClass) ||
            !NT_SUCCESS(BthCreateKey(Root, ClassName, &ClassKey)))
            continue;
        for (InstanceIndex = 0; NT_SUCCESS(ZwEnumerateKey(ClassKey, InstanceIndex, KeyBasicInformation, InstanceInfo,
                                                          sizeof(InstanceData) - sizeof(WCHAR), &Size));
             InstanceIndex++)
        {
            UCHAR ValueData[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONGLONG)];
            PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)ValueData;
            UNICODE_STRING ValueName, Number;
            WCHAR InstanceName[12];

            if (InstanceInfo->NameLength / sizeof(WCHAR) >= RTL_NUMBER_OF(InstanceName))
                continue;
            RtlCopyMemory(InstanceName, InstanceInfo->Name, InstanceInfo->NameLength);
            InstanceName[InstanceInfo->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
            RtlInitUnicodeString(&Number, InstanceName);
            if (!NT_SUCCESS(RtlUnicodeStringToInteger(&Number, 10, &Info->Instance)) ||
                !NT_SUCCESS(BthCreateKey(ClassKey, InstanceName, &InstanceKey)))
                continue;
            RtlInitUnicodeString(&ValueName, L"Enabled");
            Info->Enabled = NT_SUCCESS(ZwQueryValueKey(InstanceKey, &ValueName, KeyValuePartialInformation, Value,
                                                       sizeof(ValueData), &Size)) &&
                            Value->Type == REG_DWORD && *(PULONG)Value->Data;
            RtlInitUnicodeString(&ValueName, L"Address");
            Info->Address = NT_SUCCESS(ZwQueryValueKey(InstanceKey, &ValueName, KeyValuePartialInformation, Value,
                                                       sizeof(ValueData), &Size)) &&
                            Value->Type == REG_QWORD ? *(ULONGLONG UNALIGNED *)Value->Data : 0;
            BthEnumReadString(InstanceKey, L"Name", Info->Name, RTL_NUMBER_OF(Info->Name));
            BthEnumReadString(InstanceKey, L"DeviceString", Info->DeviceString, RTL_NUMBER_OF(Info->DeviceString));
            ZwClose(InstanceKey);
            if (Info->Enabled && !NT_SUCCESS(BthEnumApplyLocalService(Dev, Info)))
                DPRINT1("BTHMINI: local service %ws instance %lu not restored\n", ClassName, Info->Instance);
        }
        ZwClose(ClassKey);
    }
    if (Info)
        ExFreePoolWithTag(Info, BTHMINI_TAG);
    ZwClose(Root);
}
