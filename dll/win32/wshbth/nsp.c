/*
 * PROJECT:     LiberNT Windows Sockets Helper for Bluetooth
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NS_BTH name space provider: device inquiry, SDP service lookup and WSASetService
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wshbth.h"

#define WSHBTH_STREAM_INITIAL       4096
#define WSHBTH_STREAM_MAX           0x10000
#define WSHBTH_MAX_HANDLES          256
#define WSHBTH_SDP_UINT             1
#define WSHBTH_SDP_UUID             3
#define WSHBTH_SDP_STRING           4
#define WSHBTH_SDP_SEQUENCE         6
#define WSHBTH_SDP_ALTERNATIVE      7
#define WSHBTH_ATTR_PROTOCOLS       0x0004
#define WSHBTH_ATTR_NAME            0x0100
#define WSHBTH_ATTR_DESCRIPTION     0x0101
#define WSHBTH_UUID_RFCOMM          0x0003

typedef struct _WSHBTH_RECORD
{
    ULONG Offset;
    ULONG Length;
} WSHBTH_RECORD, *PWSHBTH_RECORD;

typedef struct _WSHBTH_LOOKUP
{
    BOOL Devices;
    BOOL HandleArray;
    ULONG Index;
    ULONG Count;
    BTH_ADDR Local;
    BTH_ADDR Remote;
    GUID ServiceClass;
    BLUETOOTH_DEVICE_INFO *DeviceList;
    PUCHAR Stream;
    ULONG StreamLength;
    PWSHBTH_RECORD Records;
} WSHBTH_LOOKUP, *PWSHBTH_LOOKUP;

typedef struct _WSHBTH_SERVICE
{
    struct _WSHBTH_SERVICE *Next;
    GUID ServiceClass;
    ULONG Port;
    HANDLE_SDP Handle;
} WSHBTH_SERVICE, *PWSHBTH_SERVICE;

typedef struct _WSHBTH_PACKER
{
    PUCHAR Base;
    ULONG Size;
    ULONG Used;
} WSHBTH_PACKER, *PWSHBTH_PACKER;

static const GUID WshBthBaseUuid = { 0x00000000, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
static CRITICAL_SECTION WshBthServiceLock;
static PWSHBTH_SERVICE WshBthServices;
static HANDLE WshBthServiceRadio;

static
INT
WshBthFail(
    _In_ INT Error)
{
    SetLastError(Error);
    return SOCKET_ERROR;
}

static
ULONG
WshBthSdpElement(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PUCHAR Type,
    _Out_ PULONG ValueOffset,
    _Out_ PULONG ValueLength)
{
    static const UCHAR FixedSizes[] = { 1, 2, 4, 8, 16 };
    UCHAR Index;

    if (!Length)
        return 0;
    *Type = Data[0] >> 3;
    Index = Data[0] & 7;
    if (*Type == 0)
    {
        *ValueOffset = 1;
        *ValueLength = 0;
        return 1;
    }
    if (Index <= 4)
    {
        *ValueOffset = 1;
        *ValueLength = FixedSizes[Index];
    }
    else if (Index == 5 && Length >= 2)
    {
        *ValueOffset = 2;
        *ValueLength = Data[1];
    }
    else if (Index == 6 && Length >= 3)
    {
        *ValueOffset = 3;
        *ValueLength = (Data[1] << 8) | Data[2];
    }
    else if (Index == 7 && Length >= 5)
    {
        *ValueOffset = 5;
        *ValueLength = ((ULONG)Data[1] << 24) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 8) | Data[4];
    }
    else
    {
        return 0;
    }
    if (*ValueLength > Length - *ValueOffset)
        return 0;
    return *ValueOffset + *ValueLength;
}

static
BOOL
WshBthFindAttribute(
    _In_reads_bytes_(Length) const UCHAR *Record,
    _In_ ULONG Length,
    _In_ USHORT Id,
    _Out_ const UCHAR **Value,
    _Out_ PULONG ValueLength)
{
    ULONG ListOffset, ListLength, Offset, Size;
    UCHAR Type;

    if (!WshBthSdpElement(Record, Length, &Type, &ListOffset, &ListLength) || Type != WSHBTH_SDP_SEQUENCE)
        return FALSE;
    for (Offset = ListOffset; Offset < ListOffset + ListLength; Offset += Size)
    {
        ULONG IdOffset, IdLength, ElementOffset, ElementLength, IdSize;
        UCHAR IdType, ElementType;

        IdSize = WshBthSdpElement(Record + Offset, ListOffset + ListLength - Offset, &IdType, &IdOffset, &IdLength);
        if (!IdSize || IdType != WSHBTH_SDP_UINT || IdLength != 2)
            return FALSE;
        Size = WshBthSdpElement(Record + Offset + IdSize, ListOffset + ListLength - Offset - IdSize, &ElementType,
                                &ElementOffset, &ElementLength);
        if (!Size)
            return FALSE;
        if (((Record[Offset + 1] << 8) | Record[Offset + 2]) == Id)
        {
            *Value = Record + Offset + IdSize;
            *ValueLength = Size;
            return TRUE;
        }
        Size += IdSize;
    }
    return FALSE;
}

static
ULONG
WshBthRfcommChannel(
    _In_reads_bytes_(Length) const UCHAR *Record,
    _In_ ULONG Length)
{
    ULONG ListOffset, ListLength, Offset, Size, ProtocolsLength;
    const UCHAR *Protocols;
    UCHAR Type;

    if (!WshBthFindAttribute(Record, Length, WSHBTH_ATTR_PROTOCOLS, &Protocols, &ProtocolsLength) ||
        !WshBthSdpElement(Protocols, ProtocolsLength, &Type, &ListOffset, &ListLength) || Type != WSHBTH_SDP_SEQUENCE)
        return 0;
    for (Offset = ListOffset; Offset < ListOffset + ListLength; Offset += Size)
    {
        ULONG InnerOffset, InnerLength, UuidOffset, UuidLength, UuidSize, ChannelOffset, ChannelLength;
        UCHAR InnerType, UuidType, ChannelType;
        const UCHAR *Inner;

        Size = WshBthSdpElement(Protocols + Offset, ListOffset + ListLength - Offset, &InnerType, &InnerOffset,
                                &InnerLength);
        if (!Size || InnerType != WSHBTH_SDP_SEQUENCE)
            return 0;
        Inner = Protocols + Offset + InnerOffset;
        UuidSize = WshBthSdpElement(Inner, InnerLength, &UuidType, &UuidOffset, &UuidLength);
        if (!UuidSize || UuidType != WSHBTH_SDP_UUID || UuidLength != 2 ||
            ((Inner[UuidOffset] << 8) | Inner[UuidOffset + 1]) != WSHBTH_UUID_RFCOMM)
            continue;
        if (WshBthSdpElement(Inner + UuidSize, InnerLength - UuidSize, &ChannelType, &ChannelOffset, &ChannelLength) &&
            ChannelType == WSHBTH_SDP_UINT && ChannelLength == 1)
            return Inner[UuidSize + ChannelOffset];
    }
    return 0;
}

static
LPWSTR
WshBthAttributeString(
    _In_reads_bytes_(Length) const UCHAR *Record,
    _In_ ULONG Length,
    _In_ USHORT Id)
{
    ULONG ValueLength, Offset, StringLength;
    const UCHAR *Value;
    LPWSTR String;
    UCHAR Type;
    int Chars;

    if (!WshBthFindAttribute(Record, Length, Id, &Value, &ValueLength) ||
        !WshBthSdpElement(Value, ValueLength, &Type, &Offset, &StringLength) || Type != WSHBTH_SDP_STRING)
        return NULL;
    while (StringLength && !Value[Offset + StringLength - 1])
        StringLength--;
    Chars = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)Value + Offset, StringLength, NULL, 0);
    String = HeapAlloc(GetProcessHeap(), 0, (Chars + 1) * sizeof(WCHAR));
    if (!String)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)Value + Offset, StringLength, String, Chars);
    String[Chars] = UNICODE_NULL;
    return String;
}

static
PVOID
WshBthPack(
    _Inout_ PWSHBTH_PACKER Packer,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length,
    _In_ ULONG Alignment)
{
    ULONG Offset = (Packer->Used + Alignment - 1) & ~(Alignment - 1);
    PVOID Target;

    Packer->Used = Offset + Length;
    if (Packer->Used > Packer->Size)
        return NULL;
    Target = Packer->Base + Offset;
    if (Data)
        RtlCopyMemory(Target, Data, Length);
    else
        RtlZeroMemory(Target, Length);
    return Target;
}

static
LPWSTR
WshBthPackString(
    _Inout_ PWSHBTH_PACKER Packer,
    _In_opt_ LPCWSTR String)
{
    if (!String)
        return NULL;
    return WshBthPack(Packer, String, (ULONG)(wcslen(String) + 1) * sizeof(WCHAR), sizeof(WCHAR));
}

static
VOID
WshBthQueryUuid(
    _In_ const GUID *Guid,
    _Out_ SdpQueryUuid *Uuid)
{
    RtlZeroMemory(Uuid, sizeof(*Uuid));
    if (Guid->Data2 == WshBthBaseUuid.Data2 && Guid->Data3 == WshBthBaseUuid.Data3 &&
        RtlEqualMemory(Guid->Data4, WshBthBaseUuid.Data4, sizeof(Guid->Data4)))
    {
        if (Guid->Data1 <= 0xFFFF)
        {
            Uuid->u.uuid16 = (USHORT)Guid->Data1;
            Uuid->uuidType = SDP_ST_UUID16;
        }
        else
        {
            Uuid->u.uuid32 = Guid->Data1;
            Uuid->uuidType = SDP_ST_UUID32;
        }
        return;
    }
    Uuid->u.uuid128 = *Guid;
    Uuid->uuidType = SDP_ST_UUID128;
}

static
INT
WshBthLookupDevices(
    _In_ LPWSAQUERYSETW Query,
    _In_ DWORD Flags,
    _Inout_ PWSHBTH_LOOKUP Lookup)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS Params = { sizeof(Params) };
    BLUETOOTH_DEVICE_INFO Info = { sizeof(Info) };
    ULONG Seconds = SDP_DEFAULT_INQUIRY_SECONDS, Capacity = 0;
    HBLUETOOTH_DEVICE_FIND Find;
    HANDLE Radio;

    if (Query->lpBlob && Query->lpBlob->pBlobData)
    {
        PBTH_QUERY_DEVICE Device = (PBTH_QUERY_DEVICE)Query->lpBlob->pBlobData;

        if (Query->lpBlob->cbSize != sizeof(BTH_QUERY_DEVICE) || Device->length > SDP_MAX_INQUIRY_SECONDS)
            return WSAEINVAL;
        if (Device->length)
            Seconds = Device->length;
    }
    Radio = WshBthOpenRadio(&Lookup->Local);
    if (!Radio)
        return WSASERVICE_NOT_FOUND;
    Params.hRadio = Radio;
    Params.fReturnAuthenticated = TRUE;
    Params.fReturnRemembered = TRUE;
    Params.fReturnUnknown = TRUE;
    Params.fReturnConnected = TRUE;
    Params.fIssueInquiry = (Flags & LUP_FLUSHCACHE) != 0;
    Params.cTimeoutMultiplier = (UCHAR)min(48, max(1, (Seconds * 100 + 127) / 128));
    Find = BluetoothFindFirstDevice(&Params, &Info);
    if (!Find)
    {
        CloseHandle(Radio);
        return NO_ERROR;
    }
    do
    {
        if (Lookup->Count == Capacity)
        {
            BLUETOOTH_DEVICE_INFO *List;

            Capacity = Capacity ? Capacity * 2 : 16;
            List = Lookup->DeviceList ? HeapReAlloc(GetProcessHeap(), 0, Lookup->DeviceList, Capacity * sizeof(*List)) :
                                        HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(*List));
            if (!List)
                break;
            Lookup->DeviceList = List;
        }
        Lookup->DeviceList[Lookup->Count++] = Info;
        Info.dwSize = sizeof(Info);
    } while (BluetoothFindNextDevice(Find, &Info));
    BluetoothFindDeviceClose(Find);
    CloseHandle(Radio);
    return NO_ERROR;
}

static
BOOL
WshBthSdpIoctl(
    _In_ HANDLE Radio,
    _In_ DWORD Code,
    _In_reads_bytes_(InputLength) PVOID Input,
    _In_ DWORD InputLength,
    _Out_ PUCHAR *Stream,
    _Out_ PULONG StreamLength)
{
    ULONG Size = WSHBTH_STREAM_INITIAL;
    DWORD Returned;

    *Stream = NULL;
    *StreamLength = 0;
    for (;;)
    {
        PBTH_SDP_STREAM_RESPONSE Response = HeapAlloc(GetProcessHeap(), 0, FIELD_OFFSET(BTH_SDP_STREAM_RESPONSE, response[Size]));
        ULONG Required;

        if (!Response)
            return FALSE;
        if (!DeviceIoControl(Radio, Code, Input, InputLength, Response,
                             FIELD_OFFSET(BTH_SDP_STREAM_RESPONSE, response[Size]), &Returned, NULL))
        {
            HeapFree(GetProcessHeap(), 0, Response);
            return FALSE;
        }
        if (Response->requiredSize <= Response->responseSize)
        {
            RtlMoveMemory(Response, Response->response, Response->responseSize);
            *StreamLength = Response->responseSize;
            *Stream = (PUCHAR)Response;
            return TRUE;
        }
        Required = Response->requiredSize;
        HeapFree(GetProcessHeap(), 0, Response);
        if (Size >= WSHBTH_STREAM_MAX)
            return FALSE;
        Size = min(Required, WSHBTH_STREAM_MAX);
    }
}

static
INT
WshBthSplitRecords(
    _Inout_ PWSHBTH_LOOKUP Lookup,
    _In_ BOOL Single)
{
    ULONG ListOffset, ListLength, Offset, Size, Count = 0;
    UCHAR Type;

    if (!Lookup->StreamLength)
        return NO_ERROR;
    if (Single)
    {
        Lookup->Records = HeapAlloc(GetProcessHeap(), 0, sizeof(*Lookup->Records));
        if (!Lookup->Records)
            return WSA_NOT_ENOUGH_MEMORY;
        Lookup->Records[0].Offset = 0;
        Lookup->Records[0].Length = Lookup->StreamLength;
        Lookup->Count = 1;
        return NO_ERROR;
    }
    if (!WshBthSdpElement(Lookup->Stream, Lookup->StreamLength, &Type, &ListOffset, &ListLength) ||
        Type != WSHBTH_SDP_SEQUENCE)
        return WSAEINVAL;
    for (Offset = ListOffset; Offset < ListOffset + ListLength; Offset += Size, Count++)
    {
        ULONG ValueOffset, ValueLength;

        Size = WshBthSdpElement(Lookup->Stream + Offset, ListOffset + ListLength - Offset, &Type, &ValueOffset,
                                &ValueLength);
        if (!Size)
            return WSAEINVAL;
    }
    if (!Count)
        return NO_ERROR;
    Lookup->Records = HeapAlloc(GetProcessHeap(), 0, Count * sizeof(*Lookup->Records));
    if (!Lookup->Records)
        return WSA_NOT_ENOUGH_MEMORY;
    for (Offset = ListOffset; Offset < ListOffset + ListLength; Offset += Size)
    {
        ULONG ValueOffset, ValueLength;

        Size = WshBthSdpElement(Lookup->Stream + Offset, ListOffset + ListLength - Offset, &Type, &ValueOffset,
                                &ValueLength);
        Lookup->Records[Lookup->Count].Offset = Offset;
        Lookup->Records[Lookup->Count].Length = Size;
        Lookup->Count++;
    }
    return NO_ERROR;
}

static
INT
WshBthLookupServices(
    _In_ LPWSAQUERYSETW Query,
    _In_ DWORD Flags,
    _Inout_ PWSHBTH_LOOKUP Lookup)
{
    BTH_SDP_CONNECT Connect;
    HANDLE_SDP Connection;
    PBTH_QUERY_SERVICE Advanced = NULL;
    ULONG Type = SDP_SERVICE_SEARCH_ATTRIBUTE_REQUEST, Port, Ranges, Index;
    SdpAttributeRange FullRange = { 0, 0xFFFF };
    const SdpAttributeRange *Range = &FullRange;
    PVOID Request = NULL;
    DWORD RequestLength = 0, Returned;
    HANDLE Radio;
    BOOL Ok = FALSE;
    INT Error = NO_ERROR;

    if (!WshBthParseAddress(Query->lpszContext, &Lookup->Remote, &Port))
        return WSAEINVAL;
    if (Query->lpBlob && Query->lpBlob->pBlobData)
    {
        Advanced = (PBTH_QUERY_SERVICE)Query->lpBlob->pBlobData;
        if (Query->lpBlob->cbSize < FIELD_OFFSET(BTH_QUERY_SERVICE, pRange) ||
            (Advanced->type != SDP_SERVICE_SEARCH_REQUEST &&
             (Advanced->numRange == 0 ||
              Query->lpBlob->cbSize < FIELD_OFFSET(BTH_QUERY_SERVICE, pRange[Advanced->numRange]))))
            return WSAEINVAL;
        Type = Advanced->type;
        Ranges = Advanced->numRange;
        if (Type != SDP_SERVICE_SEARCH_REQUEST && Type != SDP_SERVICE_ATTRIBUTE_REQUEST &&
            Type != SDP_SERVICE_SEARCH_ATTRIBUTE_REQUEST)
            return WSAEINVAL;
        Range = Advanced->pRange;
    }
    else
    {
        if (!Query->lpServiceClassId)
            return WSAEINVAL;
        Lookup->ServiceClass = *Query->lpServiceClassId;
        Ranges = 1;
    }
    Radio = WshBthOpenRadio(&Lookup->Local);
    if (!Radio)
        return WSASERVICE_NOT_FOUND;
    if (Lookup->Remote != Lookup->Local && !(Flags & LUP_FLUSHCACHE))
    {
        CloseHandle(Radio);
        return NO_ERROR;
    }
    Connection = HANDLE_SDP_LOCAL;
    if (Lookup->Remote != Lookup->Local)
    {
        RtlZeroMemory(&Connect, sizeof(Connect));
        Connect.bthAddress = Lookup->Remote;
        if (!DeviceIoControl(Radio, IOCTL_BTH_SDP_CONNECT, &Connect, sizeof(Connect), &Connect, sizeof(Connect),
                             &Returned, NULL))
        {
            CloseHandle(Radio);
            return WSASERVICE_NOT_FOUND;
        }
        Connection = Connect.hConnection;
    }

    if (Type == SDP_SERVICE_SEARCH_REQUEST)
    {
        BTH_SDP_SERVICE_SEARCH_REQUEST Search;
        PULONG Handles = HeapAlloc(GetProcessHeap(), 0, WSHBTH_MAX_HANDLES * sizeof(ULONG));

        RtlZeroMemory(&Search, sizeof(Search));
        Search.hConnection = Connection;
        RtlCopyMemory(Search.uuids, Advanced->uuids, sizeof(Search.uuids));
        if (Handles && DeviceIoControl(Radio, IOCTL_BTH_SDP_SERVICE_SEARCH, &Search, sizeof(Search), Handles,
                                       WSHBTH_MAX_HANDLES * sizeof(ULONG), &Returned, NULL))
        {
            Lookup->Stream = (PUCHAR)Handles;
            Lookup->StreamLength = Returned;
            Lookup->HandleArray = TRUE;
            Lookup->Count = 1;
            Ok = TRUE;
        }
        else if (Handles)
        {
            HeapFree(GetProcessHeap(), 0, Handles);
        }
    }
    else if (Type == SDP_SERVICE_ATTRIBUTE_REQUEST)
    {
        PBTH_SDP_ATTRIBUTE_SEARCH_REQUEST Search;

        RequestLength = FIELD_OFFSET(BTH_SDP_ATTRIBUTE_SEARCH_REQUEST, range[Ranges]);
        Request = Search = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequestLength);
        if (Search)
        {
            Search->hConnection = Connection;
            Search->recordHandle = Advanced->serviceHandle;
            for (Index = 0; Index < Ranges; Index++)
                Search->range[Index] = Range[Index];
            Ok = WshBthSdpIoctl(Radio, IOCTL_BTH_SDP_ATTRIBUTE_SEARCH, Search, RequestLength, &Lookup->Stream,
                                &Lookup->StreamLength);
            if (Ok)
                Error = WshBthSplitRecords(Lookup, TRUE);
        }
    }
    else
    {
        PBTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST Search;

        RequestLength = FIELD_OFFSET(BTH_SDP_SERVICE_ATTRIBUTE_SEARCH_REQUEST, range[Ranges]);
        Request = Search = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequestLength);
        if (Search)
        {
            Search->hConnection = Connection;
            if (Advanced)
                RtlCopyMemory(Search->uuids, Advanced->uuids, sizeof(Search->uuids));
            else
                WshBthQueryUuid(&Lookup->ServiceClass, &Search->uuids[0]);
            for (Index = 0; Index < Ranges; Index++)
                Search->range[Index] = Range[Index];
            Ok = WshBthSdpIoctl(Radio, IOCTL_BTH_SDP_SERVICE_ATTRIBUTE_SEARCH, Search, RequestLength, &Lookup->Stream,
                                &Lookup->StreamLength);
            if (Ok)
                Error = WshBthSplitRecords(Lookup, FALSE);
        }
    }
    if (Request)
        HeapFree(GetProcessHeap(), 0, Request);
    if (Connection != HANDLE_SDP_LOCAL)
        DeviceIoControl(Radio, IOCTL_BTH_SDP_DISCONNECT, &Connection, sizeof(Connection), NULL, 0, &Returned, NULL);
    CloseHandle(Radio);
    if (!Ok)
        return GetLastError() == ERROR_NOT_ENOUGH_MEMORY ? WSA_NOT_ENOUGH_MEMORY : WSASERVICE_NOT_FOUND;
    return Error;
}

static
INT
WSAAPI
WshBthLookupServiceBegin(
    _In_ LPGUID ProviderId,
    _In_ LPWSAQUERYSETW Restrictions,
    _In_opt_ LPWSASERVICECLASSINFOW ServiceClassInfo,
    _In_ DWORD ControlFlags,
    _Out_ LPHANDLE Lookup)
{
    PWSHBTH_LOOKUP Context;
    INT Error;

    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(ServiceClassInfo);
    if (!Restrictions || !Lookup || Restrictions->dwSize < sizeof(WSAQUERYSETW) ||
        (Restrictions->dwNameSpace != NS_BTH && Restrictions->dwNameSpace != NS_ALL))
        return WshBthFail(WSAEINVAL);
    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
        return WshBthFail(WSA_NOT_ENOUGH_MEMORY);
    Context->Devices = (ControlFlags & LUP_CONTAINERS) != 0;
    Error = Context->Devices ? WshBthLookupDevices(Restrictions, ControlFlags, Context) :
                               WshBthLookupServices(Restrictions, ControlFlags, Context);
    if (Error != NO_ERROR)
    {
        if (Context->DeviceList)
            HeapFree(GetProcessHeap(), 0, Context->DeviceList);
        if (Context->Stream)
            HeapFree(GetProcessHeap(), 0, Context->Stream);
        if (Context->Records)
            HeapFree(GetProcessHeap(), 0, Context->Records);
        HeapFree(GetProcessHeap(), 0, Context);
        return WshBthFail(Error);
    }
    *Lookup = Context;
    return NO_ERROR;
}

static
INT
WshBthDeviceResult(
    _In_ PWSHBTH_LOOKUP Lookup,
    _In_ DWORD Flags,
    _Inout_ PWSHBTH_PACKER Packer,
    _Out_ LPWSAQUERYSETW Results)
{
    const BLUETOOTH_DEVICE_INFO *Device = &Lookup->DeviceList[Lookup->Index];

    if (Device->fConnected)
        Results->dwOutputFlags |= BTHNS_RESULT_DEVICE_CONNECTED;
    if (Device->fRemembered)
        Results->dwOutputFlags |= BTHNS_RESULT_DEVICE_REMEMBERED;
    if (Device->fAuthenticated)
        Results->dwOutputFlags |= BTHNS_RESULT_DEVICE_AUTHENTICATED;
    if (Flags & LUP_RETURN_NAME)
        Results->lpszServiceInstanceName = WshBthPackString(Packer, Device->szName);
    if (Flags & LUP_RETURN_TYPE)
    {
        GUID Class = { 0 };

        Class.Data1 = Device->ulClassofDevice;
        Results->lpServiceClassId = WshBthPack(Packer, &Class, sizeof(Class), sizeof(ULONG));
    }
    if (Flags & LUP_RETURN_ADDR)
    {
        CSADDR_INFO *Address = WshBthPack(Packer, NULL, sizeof(*Address), sizeof(PVOID));
        PSOCKADDR_BTH Remote = WshBthPack(Packer, NULL, sizeof(*Remote), sizeof(ULONG));
        PSOCKADDR_BTH Local = (Flags & LUP_RES_SERVICE) ? WshBthPack(Packer, NULL, sizeof(*Local), sizeof(ULONG)) : NULL;

        if (Address && Remote)
        {
            Remote->addressFamily = AF_BTH;
            Remote->btAddr = Device->Address.ullLong;
            Address->RemoteAddr.lpSockaddr = (LPSOCKADDR)Remote;
            Address->RemoteAddr.iSockaddrLength = sizeof(*Remote);
            Address->iSocketType = SOCK_STREAM;
            Address->iProtocol = BTHPROTO_RFCOMM;
            if (Local)
            {
                Local->addressFamily = AF_BTH;
                Local->btAddr = Lookup->Local;
                Address->LocalAddr.lpSockaddr = (LPSOCKADDR)Local;
                Address->LocalAddr.iSockaddrLength = sizeof(*Local);
            }
            Results->dwNumberOfCsAddrs = 1;
            Results->lpcsaBuffer = Address;
        }
    }
    if (Flags & LUP_RETURN_BLOB)
    {
        BLOB *Blob = WshBthPack(Packer, NULL, sizeof(*Blob), sizeof(PVOID));
        PBTH_DEVICE_INFO Info = WshBthPack(Packer, NULL, sizeof(*Info), sizeof(ULONGLONG));

        if (Blob && Info)
        {
            Info->flags = BDIF_ADDRESS | BDIF_COD;
            if (Device->fConnected)
                Info->flags |= BDIF_CONNECTED;
            if (Device->fRemembered)
                Info->flags |= BDIF_PERSONAL;
            if (Device->fAuthenticated)
                Info->flags |= BDIF_PAIRED;
            Info->address = Device->Address.ullLong;
            Info->classOfDevice = Device->ulClassofDevice;
            if (Flags & LUP_RETURN_NAME)
            {
                WideCharToMultiByte(CP_UTF8, 0, Device->szName, -1, Info->name, sizeof(Info->name), NULL, NULL);
                Info->flags |= BDIF_NAME;
            }
            Blob->cbSize = sizeof(*Info);
            Blob->pBlobData = (PBYTE)Info;
            Results->lpBlob = Blob;
        }
    }
    return NO_ERROR;
}

static
INT
WshBthServiceResult(
    _In_ PWSHBTH_LOOKUP Lookup,
    _In_ DWORD Flags,
    _Inout_ PWSHBTH_PACKER Packer,
    _Out_ LPWSAQUERYSETW Results)
{
    const UCHAR *Record = Lookup->Stream;
    ULONG Length = Lookup->StreamLength;
    LPWSTR Name = NULL, Comment = NULL;

    if (!Lookup->HandleArray)
    {
        Record += Lookup->Records[Lookup->Index].Offset;
        Length = Lookup->Records[Lookup->Index].Length;
        if (Flags & LUP_RETURN_NAME)
            Name = WshBthAttributeString(Record, Length, WSHBTH_ATTR_NAME);
        if (Flags & LUP_RETURN_COMMENT)
            Comment = WshBthAttributeString(Record, Length, WSHBTH_ATTR_DESCRIPTION);
        Results->lpszServiceInstanceName = WshBthPackString(Packer, Name);
        Results->lpszComment = WshBthPackString(Packer, Comment);
        if (Name)
            HeapFree(GetProcessHeap(), 0, Name);
        if (Comment)
            HeapFree(GetProcessHeap(), 0, Comment);
        if (Flags & LUP_RETURN_TYPE)
            Results->lpServiceClassId = WshBthPack(Packer, NULL, sizeof(GUID), sizeof(ULONG));
        if (Flags & LUP_RETURN_ADDR)
        {
            CSADDR_INFO *Address = WshBthPack(Packer, NULL, sizeof(*Address), sizeof(PVOID));
            PSOCKADDR_BTH Remote = WshBthPack(Packer, NULL, sizeof(*Remote), sizeof(ULONG));
            PSOCKADDR_BTH Local = (Flags & LUP_RES_SERVICE) ? WshBthPack(Packer, NULL, sizeof(*Local), sizeof(ULONG)) :
                                                              NULL;

            if (Address && Remote)
            {
                Remote->addressFamily = AF_BTH;
                Remote->btAddr = Lookup->Remote;
                Remote->serviceClassId = Lookup->ServiceClass;
                Remote->port = WshBthRfcommChannel(Record, Length);
                Address->RemoteAddr.lpSockaddr = (LPSOCKADDR)Remote;
                Address->RemoteAddr.iSockaddrLength = sizeof(*Remote);
                Address->iSocketType = SOCK_STREAM;
                Address->iProtocol = BTHPROTO_RFCOMM;
                if (Local)
                {
                    Local->addressFamily = AF_BTH;
                    Local->btAddr = Lookup->Local;
                    Address->LocalAddr.lpSockaddr = (LPSOCKADDR)Local;
                    Address->LocalAddr.iSockaddrLength = sizeof(*Local);
                }
                Results->dwNumberOfCsAddrs = 1;
                Results->lpcsaBuffer = Address;
            }
        }
    }
    if ((Flags & LUP_RETURN_BLOB) || Lookup->HandleArray)
    {
        BLOB *Blob = WshBthPack(Packer, NULL, sizeof(*Blob), sizeof(PVOID));
        PUCHAR Data = WshBthPack(Packer, Record, Length, sizeof(ULONG));

        if (Blob && Data)
        {
            Blob->cbSize = Length;
            Blob->pBlobData = Data;
            Results->lpBlob = Blob;
        }
    }
    return NO_ERROR;
}

static
INT
WSAAPI
WshBthLookupServiceNext(
    _In_ HANDLE Lookup,
    _In_ DWORD ControlFlags,
    _Inout_ LPDWORD BufferLength,
    _Out_writes_bytes_to_(*BufferLength, *BufferLength) LPWSAQUERYSETW Results)
{
    PWSHBTH_LOOKUP Context = Lookup;
    WSHBTH_PACKER Packer;
    WCHAR Context2[32];
    INT Error;

    if (!Context || !BufferLength)
        return WshBthFail(WSA_INVALID_HANDLE);
    if (ControlFlags & LUP_FLUSHPREVIOUS)
        Context->Index++;
    if (Context->Index >= Context->Count)
        return WshBthFail(Context->Index == 0 ? WSASERVICE_NOT_FOUND : WSA_E_NO_MORE);
    Packer.Base = (PUCHAR)Results;
    Packer.Size = Results ? *BufferLength : 0;
    Packer.Used = sizeof(WSAQUERYSETW);
    if (Results && Packer.Size >= sizeof(WSAQUERYSETW))
    {
        RtlZeroMemory(Results, sizeof(*Results));
        Results->dwSize = sizeof(*Results);
        Results->dwNameSpace = NS_BTH;
    }
    else
    {
        Packer.Size = 0;
        Results = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Results));
        if (!Results)
            return WshBthFail(WSA_NOT_ENOUGH_MEMORY);
    }
    if (!Context->Devices && WshBthFormatAddress(Context->Remote, 0, Context2, RTL_NUMBER_OF(Context2)))
        Results->lpszContext = WshBthPackString(&Packer, Context2);
    Error = Context->Devices ? WshBthDeviceResult(Context, ControlFlags, &Packer, Results) :
                               WshBthServiceResult(Context, ControlFlags, &Packer, Results);
    if (Packer.Base != (PUCHAR)Results)
        HeapFree(GetProcessHeap(), 0, Results);
    if (Error != NO_ERROR)
        return WshBthFail(Error);
    if (Packer.Used > Packer.Size)
    {
        *BufferLength = Packer.Used;
        return WshBthFail(WSAEFAULT);
    }
    Context->Index++;
    return NO_ERROR;
}

static
INT
WSAAPI
WshBthLookupServiceEnd(
    _In_ HANDLE Lookup)
{
    PWSHBTH_LOOKUP Context = Lookup;

    if (!Context)
        return WshBthFail(WSA_INVALID_HANDLE);
    if (Context->DeviceList)
        HeapFree(GetProcessHeap(), 0, Context->DeviceList);
    if (Context->Stream)
        HeapFree(GetProcessHeap(), 0, Context->Stream);
    if (Context->Records)
        HeapFree(GetProcessHeap(), 0, Context->Records);
    HeapFree(GetProcessHeap(), 0, Context);
    return NO_ERROR;
}

static
ULONG
WshBthPutUuid(
    _Out_writes_bytes_(17) PUCHAR Output,
    _In_ const GUID *Guid)
{
    SdpQueryUuid Uuid;

    WshBthQueryUuid(Guid, &Uuid);
    if (Uuid.uuidType == SDP_ST_UUID16)
    {
        Output[0] = 0x19;
        Output[1] = (UCHAR)(Uuid.u.uuid16 >> 8);
        Output[2] = (UCHAR)Uuid.u.uuid16;
        return 3;
    }
    if (Uuid.uuidType == SDP_ST_UUID32)
    {
        Output[0] = 0x1A;
        Output[1] = (UCHAR)(Uuid.u.uuid32 >> 24);
        Output[2] = (UCHAR)(Uuid.u.uuid32 >> 16);
        Output[3] = (UCHAR)(Uuid.u.uuid32 >> 8);
        Output[4] = (UCHAR)Uuid.u.uuid32;
        return 5;
    }
    Output[0] = 0x1C;
    Output[1] = (UCHAR)(Guid->Data1 >> 24);
    Output[2] = (UCHAR)(Guid->Data1 >> 16);
    Output[3] = (UCHAR)(Guid->Data1 >> 8);
    Output[4] = (UCHAR)Guid->Data1;
    Output[5] = (UCHAR)(Guid->Data2 >> 8);
    Output[6] = (UCHAR)Guid->Data2;
    Output[7] = (UCHAR)(Guid->Data3 >> 8);
    Output[8] = (UCHAR)Guid->Data3;
    RtlCopyMemory(Output + 9, Guid->Data4, 8);
    return 17;
}

static
ULONG
WshBthPutString(
    _Out_writes_bytes_(Capacity) PUCHAR Output,
    _In_ ULONG Capacity,
    _In_ USHORT Id,
    _In_opt_ LPCWSTR String)
{
    int Length;

    if (!String || !*String || Capacity < 5)
        return 0;
    Length = WideCharToMultiByte(CP_UTF8, 0, String, -1, (LPSTR)Output + 5, min(Capacity - 5, 255), NULL, NULL);
    if (Length <= 1)
        return 0;
    Length--;
    Output[0] = 0x09;
    Output[1] = (UCHAR)(Id >> 8);
    Output[2] = (UCHAR)Id;
    Output[3] = 0x25;
    Output[4] = (UCHAR)Length;
    return 5 + Length;
}

static
ULONG
WshBthBuildRecord(
    _In_ LPWSAQUERYSETW Info,
    _In_ ULONG Port,
    _Out_writes_bytes_(Capacity) PUCHAR Record,
    _In_ ULONG Capacity)
{
    static const UCHAR Browse[] = { 0x09, 0x00, 0x05, 0x35, 0x03, 0x19, 0x10, 0x02 };
    static const UCHAR Language[] = { 0x09, 0x00, 0x06, 0x35, 0x09, 0x09, 0x65, 0x6E, 0x09, 0x00, 0x6A, 0x09, 0x01, 0x00 };
    ULONG Used = 3, Body;

    Record[Used++] = 0x09;
    Record[Used++] = 0x00;
    Record[Used++] = 0x01;
    Record[Used++] = 0x35;
    Body = WshBthPutUuid(Record + Used + 1, Info->lpServiceClassId);
    Record[Used++] = (UCHAR)Body;
    Used += Body;
    Record[Used++] = 0x09;
    Record[Used++] = 0x00;
    Record[Used++] = 0x04;
    Record[Used++] = 0x35;
    Record[Used++] = 0x0C;
    Record[Used++] = 0x35;
    Record[Used++] = 0x03;
    Record[Used++] = 0x19;
    Record[Used++] = 0x01;
    Record[Used++] = 0x00;
    Record[Used++] = 0x35;
    Record[Used++] = 0x05;
    Record[Used++] = 0x19;
    Record[Used++] = 0x00;
    Record[Used++] = 0x03;
    Record[Used++] = 0x08;
    Record[Used++] = (UCHAR)Port;
    RtlCopyMemory(Record + Used, Browse, sizeof(Browse));
    Used += sizeof(Browse);
    RtlCopyMemory(Record + Used, Language, sizeof(Language));
    Used += sizeof(Language);
    Used += WshBthPutString(Record + Used, Capacity - Used, WSHBTH_ATTR_NAME, Info->lpszServiceInstanceName);
    Used += WshBthPutString(Record + Used, Capacity - Used, WSHBTH_ATTR_DESCRIPTION, Info->lpszComment);
    Record[0] = 0x36;
    Record[1] = (UCHAR)((Used - 3) >> 8);
    Record[2] = (UCHAR)(Used - 3);
    return Used;
}

static
HANDLE
WshBthServiceRadioLocked(VOID)
{
    if (!WshBthServiceRadio)
        WshBthServiceRadio = WshBthOpenRadio(NULL);
    return WshBthServiceRadio;
}

static
INT
WSAAPI
WshBthSetService(
    _In_ LPGUID ProviderId,
    _In_opt_ LPWSASERVICECLASSINFOW ServiceClassInfo,
    _In_ LPWSAQUERYSETW Info,
    _In_ WSAESETSERVICEOP Operation,
    _In_ DWORD ControlFlags)
{
    PBTH_SET_SERVICE Set = NULL;
    HANDLE_SDP Handle;
    DWORD Returned;
    HANDLE Radio;
    ULONG Port = 0;
    INT Error = NO_ERROR;

    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(ServiceClassInfo);
    if (!Info || ControlFlags || (Operation != RNRSERVICE_REGISTER && Operation != RNRSERVICE_DELETE))
        return WshBthFail(WSAEINVAL);
    if (Info->lpBlob && Info->lpBlob->pBlobData)
    {
        Set = (PBTH_SET_SERVICE)Info->lpBlob->pBlobData;
        if (Info->lpBlob->cbSize < FIELD_OFFSET(BTH_SET_SERVICE, pRecord) || !Set->pSdpVersion || !Set->pRecordHandle)
            return WshBthFail(WSAEINVAL);
        if (*Set->pSdpVersion != BTH_SDP_VERSION)
        {
            *Set->pSdpVersion = BTH_SDP_VERSION;
            return WshBthFail(WSAEINVAL);
        }
    }
    else
    {
        if (!Info->lpServiceClassId)
            return WshBthFail(WSAEINVAL);
        if (Info->dwNumberOfCsAddrs && Info->lpcsaBuffer && Info->lpcsaBuffer->LocalAddr.lpSockaddr &&
            Info->lpcsaBuffer->LocalAddr.iSockaddrLength >= (INT)sizeof(SOCKADDR_BTH) &&
            Info->lpcsaBuffer->LocalAddr.lpSockaddr->sa_family == AF_BTH)
            Port = ((PSOCKADDR_BTH)Info->lpcsaBuffer->LocalAddr.lpSockaddr)->port;
    }

    EnterCriticalSection(&WshBthServiceLock);
    Radio = WshBthServiceRadioLocked();
    if (!Radio)
    {
        Error = WSAENETDOWN;
    }
    else if (Operation == RNRSERVICE_DELETE)
    {
        if (Set)
        {
            Handle = (HANDLE_SDP)(ULONG_PTR)*Set->pRecordHandle;
            if (!DeviceIoControl(Radio, IOCTL_BTH_SDP_REMOVE_RECORD, &Handle, sizeof(Handle), NULL, 0, &Returned, NULL))
                Error = WSAEINVAL;
        }
        else
        {
            PWSHBTH_SERVICE *Link = &WshBthServices;

            Error = WSASERVICE_NOT_FOUND;
            while (*Link)
            {
                PWSHBTH_SERVICE Service = *Link;

                if (!IsEqualGUID(&Service->ServiceClass, Info->lpServiceClassId) || (Port && Service->Port != Port))
                {
                    Link = &Service->Next;
                    continue;
                }
                DeviceIoControl(Radio, IOCTL_BTH_SDP_REMOVE_RECORD, &Service->Handle, sizeof(Service->Handle), NULL, 0,
                                &Returned, NULL);
                *Link = Service->Next;
                HeapFree(GetProcessHeap(), 0, Service);
                Error = NO_ERROR;
            }
        }
    }
    else if (Set)
    {
        ULONG Length = FIELD_OFFSET(BTH_SDP_RECORD, record[Set->ulRecordLength]);
        PBTH_SDP_RECORD Record;

        if (Info->lpBlob->cbSize < FIELD_OFFSET(BTH_SET_SERVICE, pRecord[Set->ulRecordLength]) ||
            !(Record = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length)))
        {
            Error = WSAEINVAL;
        }
        else
        {
            Record->fCodService = Set->fCodService;
            Record->recordLength = Set->ulRecordLength;
            RtlCopyMemory(Record->record, Set->pRecord, Set->ulRecordLength);
            if (DeviceIoControl(Radio, IOCTL_BTH_SDP_SUBMIT_RECORD_WITH_INFO, Record, Length, &Handle, sizeof(Handle),
                                &Returned, NULL))
                *Set->pRecordHandle = (HANDLE)(ULONG_PTR)Handle;
            else
                Error = WSAEINVAL;
            HeapFree(GetProcessHeap(), 0, Record);
        }
    }
    else
    {
        UCHAR Record[1024];
        ULONG Length;
        PWSHBTH_SERVICE Service;

        if (!Port || Port > 30)
        {
            Error = WSAEINVAL;
        }
        else if (!(Service = HeapAlloc(GetProcessHeap(), 0, sizeof(*Service))))
        {
            Error = WSA_NOT_ENOUGH_MEMORY;
        }
        else
        {
            Length = WshBthBuildRecord(Info, Port, Record, sizeof(Record));
            if (DeviceIoControl(Radio, IOCTL_BTH_SDP_SUBMIT_RECORD, Record, Length, &Service->Handle,
                                sizeof(Service->Handle), &Returned, NULL))
            {
                Service->ServiceClass = *Info->lpServiceClassId;
                Service->Port = Port;
                Service->Next = WshBthServices;
                WshBthServices = Service;
            }
            else
            {
                HeapFree(GetProcessHeap(), 0, Service);
                Error = WSAEINVAL;
            }
        }
    }
    LeaveCriticalSection(&WshBthServiceLock);
    return Error == NO_ERROR ? NO_ERROR : WshBthFail(Error);
}

static
INT
WSAAPI
WshBthCleanup(
    _In_ LPGUID ProviderId)
{
    UNREFERENCED_PARAMETER(ProviderId);
    return NO_ERROR;
}

static
INT
WSAAPI
WshBthInstallServiceClass(
    _In_ LPGUID ProviderId,
    _In_ LPWSASERVICECLASSINFOW ServiceClassInfo)
{
    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(ServiceClassInfo);
    return WshBthFail(WSAEOPNOTSUPP);
}

static
INT
WSAAPI
WshBthRemoveServiceClass(
    _In_ LPGUID ProviderId,
    _In_ LPGUID ServiceClassId)
{
    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(ServiceClassId);
    return WshBthFail(WSAEOPNOTSUPP);
}

static
INT
WSAAPI
WshBthGetServiceClassInfo(
    _In_ LPGUID ProviderId,
    _In_ LPDWORD BufferSize,
    _In_ LPWSASERVICECLASSINFOW ServiceClassInfo)
{
    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(ServiceClassInfo);
    return WshBthFail(WSAEOPNOTSUPP);
}

static
INT
WSAAPI
WshBthIoctl(
    _In_ HANDLE Lookup,
    _In_ DWORD ControlCode,
    _In_reads_bytes_(InputLength) LPVOID Input,
    _In_ DWORD InputLength,
    _Out_writes_bytes_to_(OutputLength, *Returned) LPVOID Output,
    _In_ DWORD OutputLength,
    _Out_ LPDWORD Returned,
    _In_opt_ LPWSACOMPLETION Completion,
    _In_ LPWSATHREADID ThreadId)
{
    UNREFERENCED_PARAMETER(Lookup);
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(Input);
    UNREFERENCED_PARAMETER(InputLength);
    UNREFERENCED_PARAMETER(Output);
    UNREFERENCED_PARAMETER(OutputLength);
    UNREFERENCED_PARAMETER(Completion);
    UNREFERENCED_PARAMETER(ThreadId);
    if (Returned)
        *Returned = 0;
    return WshBthFail(WSAEOPNOTSUPP);
}

VOID
WshBthNspInitialize(VOID)
{
    InitializeCriticalSection(&WshBthServiceLock);
}

INT
WSAAPI
NSPStartup(
    _In_ LPGUID ProviderId,
    _Inout_ LPNSP_ROUTINE Routines)
{
    UNREFERENCED_PARAMETER(ProviderId);
    if (!Routines)
        return WshBthFail(WSAEINVAL);
    Routines->cbSize = sizeof(*Routines);
    Routines->dwMajorVersion = 4;
    Routines->dwMinorVersion = 4;
    Routines->NSPCleanup = WshBthCleanup;
    Routines->NSPLookupServiceBegin = WshBthLookupServiceBegin;
    Routines->NSPLookupServiceNext = WshBthLookupServiceNext;
    Routines->NSPLookupServiceEnd = WshBthLookupServiceEnd;
    Routines->NSPSetService = WshBthSetService;
    Routines->NSPInstallServiceClass = WshBthInstallServiceClass;
    Routines->NSPRemoveServiceClass = WshBthRemoveServiceClass;
    Routines->NSPGetServiceClassInfo = WshBthGetServiceClassInfo;
    Routines->NSPIoctl = WshBthIoctl;
    return NO_ERROR;
}
