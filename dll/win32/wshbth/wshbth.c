/*
 * PROJECT:     LiberNT Windows Sockets Helper for Bluetooth
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AF_BTH RFCOMM socket helper for msafd
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wshbth.h"
#include <stdio.h>

typedef struct _WSHBTH_CONTEXT
{
    INT AddressFamily;
    INT SocketType;
    INT Protocol;
    DWORD Flags;
    ULONG Authenticate;
    ULONG Encrypt;
    ULONG Mtu;
    ULONG MtuMax;
    ULONG MtuMin;
} WSHBTH_CONTEXT, *PWSHBTH_CONTEXT;

static const GUID WshBthProviderGuid = { 0x9fc48064, 0x7298, 0x43e4, { 0xb7, 0xbd, 0x18, 0x1f, 0x20, 0x89, 0x79, 0x2a } };

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        WshBthNspInitialize();
    }
    return TRUE;
}

BOOL
WshBthFormatAddress(
    _In_ BTH_ADDR Address,
    _In_ ULONG Port,
    _Out_writes_(Length) LPWSTR String,
    _In_ DWORD Length)
{
    int Written;

    if (Port && Port != BT_PORT_ANY)
        Written = _snwprintf(String, Length, L"(%02X:%02X:%02X:%02X:%02X:%02X):%lu", (UINT)((Address >> 40) & 0xFF),
                             (UINT)((Address >> 32) & 0xFF), (UINT)((Address >> 24) & 0xFF),
                             (UINT)((Address >> 16) & 0xFF), (UINT)((Address >> 8) & 0xFF), (UINT)(Address & 0xFF),
                             Port);
    else
        Written = _snwprintf(String, Length, L"(%02X:%02X:%02X:%02X:%02X:%02X)", (UINT)((Address >> 40) & 0xFF),
                             (UINT)((Address >> 32) & 0xFF), (UINT)((Address >> 24) & 0xFF),
                             (UINT)((Address >> 16) & 0xFF), (UINT)((Address >> 8) & 0xFF), (UINT)(Address & 0xFF));
    return Written > 0 && (DWORD)Written < Length;
}

BOOL
WshBthParseAddress(
    _In_ LPCWSTR String,
    _Out_ BTH_ADDR *Address,
    _Out_ PULONG Port)
{
    BOOL Parenthesis = FALSE;
    BTH_ADDR Value = 0;
    ULONG Index;

    *Address = 0;
    *Port = 0;
    if (!String)
        return FALSE;
    while (*String == L' ')
        String++;
    if (*String == L'(')
    {
        Parenthesis = TRUE;
        String++;
    }
    for (Index = 0; Index < 6; Index++)
    {
        ULONG Digit, Byte = 0;

        for (Digit = 0; Digit < 2; Digit++, String++)
        {
            WCHAR Char = *String;

            if (Char >= L'0' && Char <= L'9')
                Byte = (Byte << 4) | (Char - L'0');
            else if (Char >= L'a' && Char <= L'f')
                Byte = (Byte << 4) | (Char - L'a' + 10);
            else if (Char >= L'A' && Char <= L'F')
                Byte = (Byte << 4) | (Char - L'A' + 10);
            else
                return FALSE;
        }
        Value = (Value << 8) | Byte;
        if (Index < 5)
        {
            if (*String != L':' && *String != L'-')
                return FALSE;
            String++;
        }
    }
    if (Parenthesis)
    {
        if (*String != L')')
            return FALSE;
        String++;
    }
    if (*String == L':')
    {
        String++;
        if (*String < L'0' || *String > L'9')
            return FALSE;
        while (*String >= L'0' && *String <= L'9')
            *Port = *Port * 10 + (*String++ - L'0');
    }
    while (*String == L' ')
        String++;
    if (*String)
        return FALSE;
    *Address = Value;
    return TRUE;
}

HANDLE
WshBthOpenRadio(
    _Out_opt_ BTH_ADDR *Address)
{
    BLUETOOTH_FIND_RADIO_PARAMS Params = { sizeof(Params) };
    BLUETOOTH_RADIO_INFO Info = { sizeof(Info) };
    HBLUETOOTH_RADIO_FIND Find;
    HANDLE Radio = NULL;

    if (Address)
        *Address = 0;
    Find = BluetoothFindFirstRadio(&Params, &Radio);
    if (!Find)
        return NULL;
    BluetoothFindRadioClose(Find);
    if (Address && BluetoothGetRadioInfo(Radio, &Info) == ERROR_SUCCESS)
        *Address = Info.address.ullLong;
    return Radio;
}

INT
EXPORT
WSHOpenSocket2(
    _Inout_ PINT AddressFamily,
    _Inout_ PINT SocketType,
    _Inout_ PINT Protocol,
    _In_ GROUP Group,
    _In_ DWORD Flags,
    _Out_ PUNICODE_STRING TransportDeviceName,
    _Out_ PVOID *HelperDllSocketContext,
    _Out_ PDWORD NotificationEvents)
{
    static const WCHAR DeviceName[] = BTHTDI_DEVICE_NAME;
    PWSHBTH_CONTEXT Context;

    UNREFERENCED_PARAMETER(Group);
    if (*AddressFamily != AF_BTH)
        return WSAEAFNOSUPPORT;
    if (*SocketType != SOCK_STREAM && *SocketType != 0)
        return WSAESOCKTNOSUPPORT;
    if (*Protocol != BTHPROTO_RFCOMM && *Protocol != 0)
        return WSAEPROTONOSUPPORT;
    *SocketType = SOCK_STREAM;
    *Protocol = BTHPROTO_RFCOMM;
    TransportDeviceName->Buffer = HeapAlloc(GetProcessHeap(), 0, sizeof(DeviceName));
    if (!TransportDeviceName->Buffer)
        return WSAENOBUFS;
    RtlCopyMemory(TransportDeviceName->Buffer, DeviceName, sizeof(DeviceName));
    TransportDeviceName->Length = sizeof(DeviceName) - sizeof(WCHAR);
    TransportDeviceName->MaximumLength = sizeof(DeviceName);
    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
    {
        HeapFree(GetProcessHeap(), 0, TransportDeviceName->Buffer);
        TransportDeviceName->Buffer = NULL;
        return WSAENOBUFS;
    }
    Context->AddressFamily = *AddressFamily;
    Context->SocketType = *SocketType;
    Context->Protocol = *Protocol;
    Context->Flags = Flags;
    Context->Mtu = RFCOMM_MAX_MTU;
    Context->MtuMax = RFCOMM_MAX_MTU;
    Context->MtuMin = RFCOMM_MIN_MTU;
    *HelperDllSocketContext = Context;
    *NotificationEvents = WSH_NOTIFY_CLOSE;
    return NO_ERROR;
}

INT
EXPORT
WSHOpenSocket(
    _Inout_ PINT AddressFamily,
    _Inout_ PINT SocketType,
    _Inout_ PINT Protocol,
    _Out_ PUNICODE_STRING TransportDeviceName,
    _Out_ PVOID HelperDllSocketContext,
    _Out_ PDWORD NotificationEvents)
{
    return WSHOpenSocket2(AddressFamily, SocketType, Protocol, 0, 0, TransportDeviceName,
                          (PVOID *)HelperDllSocketContext, NotificationEvents);
}

INT
EXPORT
WSHNotify(
    _In_ PVOID HelperDllSocketContext,
    _In_ SOCKET SocketHandle,
    _In_ HANDLE TdiAddressObjectHandle,
    _In_ HANDLE TdiConnectionObjectHandle,
    _In_ DWORD NotifyEvent)
{
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    if (NotifyEvent == WSH_NOTIFY_CLOSE)
        HeapFree(GetProcessHeap(), 0, HelperDllSocketContext);
    return NO_ERROR;
}

INT
EXPORT
WSHGetSockaddrType(
    _In_ PSOCKADDR Sockaddr,
    _In_ DWORD SockaddrLength,
    _Out_ PSOCKADDR_INFO SockaddrInfo)
{
    PSOCKADDR_BTH Bth = (PSOCKADDR_BTH)Sockaddr;

    if (!Bth || !SockaddrInfo || SockaddrLength < sizeof(SOCKADDR_BTH) || Bth->addressFamily != AF_BTH)
        return WSAEINVAL;
    SockaddrInfo->AddressInfo = Bth->btAddr ? SockaddrAddressInfoNormal : SockaddrAddressInfoWildcard;
    SockaddrInfo->EndpointInfo = (Bth->port && Bth->port != BT_PORT_ANY) ? SockaddrEndpointInfoNormal :
                                                                           SockaddrEndpointInfoWildcard;
    return NO_ERROR;
}

INT
EXPORT
WSHGetWildcardSockaddr(
    _In_ PVOID HelperDllSocketContext,
    _Out_ PSOCKADDR Sockaddr,
    _Inout_ PINT SockaddrLength)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    if (!Sockaddr || !SockaddrLength || *SockaddrLength < (INT)sizeof(SOCKADDR_BTH))
        return WSAEFAULT;
    RtlZeroMemory(Sockaddr, sizeof(SOCKADDR_BTH));
    ((PSOCKADDR_BTH)Sockaddr)->addressFamily = AF_BTH;
    *SockaddrLength = sizeof(SOCKADDR_BTH);
    return NO_ERROR;
}

INT
EXPORT
WSHGetBroadcastSockaddr(
    _In_ PVOID HelperDllSocketContext,
    _Out_ PSOCKADDR Sockaddr,
    _Inout_ PINT SockaddrLength)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(Sockaddr);
    UNREFERENCED_PARAMETER(SockaddrLength);
    return WSAEOPNOTSUPP;
}

DWORD
EXPORT
WSHGetWinsockMapping(
    _Out_ PWINSOCK_MAPPING Mapping,
    _In_ DWORD MappingLength)
{
    DWORD Size = FIELD_OFFSET(WINSOCK_MAPPING, Mapping[2]);

    if (MappingLength < Size)
        return Size;
    Mapping->Rows = 2;
    Mapping->Columns = 3;
    Mapping->Mapping[0].AddressFamily = AF_BTH;
    Mapping->Mapping[0].SocketType = SOCK_STREAM;
    Mapping->Mapping[0].Protocol = BTHPROTO_RFCOMM;
    Mapping->Mapping[1].AddressFamily = AF_BTH;
    Mapping->Mapping[1].SocketType = 0;
    Mapping->Mapping[1].Protocol = BTHPROTO_RFCOMM;
    return Size;
}

INT
EXPORT
WSHGetSocketInformation(
    _In_ PVOID HelperDllSocketContext,
    _In_ SOCKET SocketHandle,
    _In_ HANDLE TdiAddressObjectHandle,
    _In_ HANDLE TdiConnectionObjectHandle,
    _In_ INT Level,
    _In_ INT OptionName,
    _Out_ PCHAR OptionValue,
    _Inout_ PINT OptionLength)
{
    PWSHBTH_CONTEXT Context = HelperDllSocketContext;

    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    if (Level != SOL_RFCOMM)
        return WSAENOPROTOOPT;
    if (OptionName != SO_BTH_MTU)
        return WSAENOPROTOOPT;
    if (!OptionValue || !OptionLength || *OptionLength < (INT)sizeof(ULONG))
        return WSAEFAULT;
    *(PULONG)OptionValue = Context->Mtu;
    *OptionLength = sizeof(ULONG);
    return NO_ERROR;
}

INT
EXPORT
WSHSetSocketInformation(
    _In_ PVOID HelperDllSocketContext,
    _In_ SOCKET SocketHandle,
    _In_ HANDLE TdiAddressObjectHandle,
    _In_ HANDLE TdiConnectionObjectHandle,
    _In_ INT Level,
    _In_ INT OptionName,
    _In_ PCHAR OptionValue,
    _In_ INT OptionLength)
{
    PWSHBTH_CONTEXT Context = HelperDllSocketContext;
    ULONG Value;

    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    if (Level == SOL_SOCKET)
        return NO_ERROR;
    if (Level != SOL_RFCOMM)
        return WSAENOPROTOOPT;
    if (!OptionValue || OptionLength < (INT)sizeof(ULONG))
        return WSAEFAULT;
    Value = *(PULONG)OptionValue;
    switch ((ULONG)OptionName)
    {
        case SO_BTH_AUTHENTICATE:
            if (!Value)
                return WSAEINVAL;
            Context->Authenticate = TRUE;
            return NO_ERROR;

        case SO_BTH_ENCRYPT:
            if (!Value)
                return WSAEINVAL;
            Context->Encrypt = TRUE;
            return NO_ERROR;

        case SO_BTH_MTU:
        case SO_BTH_MTU_MAX:
        case SO_BTH_MTU_MIN:
            if (Value < RFCOMM_MIN_MTU || Value > RFCOMM_MAX_MTU)
                return WSAEINVAL;
            if ((ULONG)OptionName == SO_BTH_MTU)
                Context->Mtu = Value;
            else if ((ULONG)OptionName == SO_BTH_MTU_MAX)
                Context->MtuMax = Value;
            else
                Context->MtuMin = Value;
            return NO_ERROR;

        default:
            return WSAENOPROTOOPT;
    }
}

INT
EXPORT
WSHAddressToString(
    _In_ LPSOCKADDR Address,
    _In_ INT AddressLength,
    _In_opt_ LPWSAPROTOCOL_INFOW ProtocolInfo,
    _Out_writes_opt_(*AddressStringLength) LPWSTR AddressString,
    _Inout_ LPDWORD AddressStringLength)
{
    PSOCKADDR_BTH Bth = (PSOCKADDR_BTH)Address;
    WCHAR Buffer[32];
    DWORD Needed;

    UNREFERENCED_PARAMETER(ProtocolInfo);
    if (!Bth || !AddressStringLength || AddressLength < (INT)sizeof(SOCKADDR_BTH) || Bth->addressFamily != AF_BTH)
        return WSAEINVAL;
    if (!WshBthFormatAddress(Bth->btAddr, Bth->port, Buffer, RTL_NUMBER_OF(Buffer)))
        return WSAEINVAL;
    Needed = (DWORD)wcslen(Buffer) + 1;
    if (!AddressString || *AddressStringLength < Needed)
    {
        *AddressStringLength = Needed;
        return WSAEFAULT;
    }
    RtlCopyMemory(AddressString, Buffer, Needed * sizeof(WCHAR));
    *AddressStringLength = Needed;
    return NO_ERROR;
}

INT
EXPORT
WSHStringToAddress(
    _In_ LPWSTR AddressString,
    _In_ DWORD AddressFamily,
    _In_opt_ LPWSAPROTOCOL_INFOW ProtocolInfo,
    _Out_writes_bytes_(*AddressLength) LPSOCKADDR Address,
    _Inout_ LPDWORD AddressLength)
{
    PSOCKADDR_BTH Bth = (PSOCKADDR_BTH)Address;
    BTH_ADDR Value;
    ULONG Port;

    UNREFERENCED_PARAMETER(ProtocolInfo);
    if (AddressFamily != AF_BTH)
        return WSAEAFNOSUPPORT;
    if (!Bth || !AddressLength || *AddressLength < sizeof(SOCKADDR_BTH))
    {
        if (AddressLength)
            *AddressLength = sizeof(SOCKADDR_BTH);
        return WSAEFAULT;
    }
    if (!WshBthParseAddress(AddressString, &Value, &Port))
        return WSAEINVAL;
    RtlZeroMemory(Bth, sizeof(*Bth));
    Bth->addressFamily = AF_BTH;
    Bth->btAddr = Value;
    Bth->port = Port;
    *AddressLength = sizeof(SOCKADDR_BTH);
    return NO_ERROR;
}

INT
EXPORT
WSHIoctl(
    _In_ PVOID HelperDllSocketContext,
    _In_ SOCKET SocketHandle,
    _In_ HANDLE TdiAddressObjectHandle,
    _In_ HANDLE TdiConnectionObjectHandle,
    _In_ DWORD IoControlCode,
    _In_ LPVOID InputBuffer,
    _In_ DWORD InputBufferLength,
    _In_ LPVOID OutputBuffer,
    _In_ DWORD OutputBufferLength,
    _Out_ LPDWORD NumberOfBytesReturned,
    _In_ LPWSAOVERLAPPED Overlapped,
    _In_ LPWSAOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine,
    _Out_ LPBOOL NeedsCompletion)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    UNREFERENCED_PARAMETER(IoControlCode);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(Overlapped);
    UNREFERENCED_PARAMETER(CompletionRoutine);
    if (NumberOfBytesReturned)
        *NumberOfBytesReturned = 0;
    if (NeedsCompletion)
        *NeedsCompletion = TRUE;
    return WSAEINVAL;
}

INT
EXPORT
WSHJoinLeaf(
    _In_ PVOID HelperDllSocketContext,
    _In_ SOCKET SocketHandle,
    _In_ HANDLE TdiAddressObjectHandle,
    _In_ HANDLE TdiConnectionObjectHandle,
    _In_ PVOID LeafHelperDllSocketContext,
    _In_ SOCKET LeafSocketHandle,
    _In_ PSOCKADDR Sockaddr,
    _In_ DWORD SockaddrLength,
    _In_ LPWSABUF CallerData,
    _In_ LPWSABUF CalleeData,
    _In_ LPQOS SocketQOS,
    _In_ LPQOS GroupQOS,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    UNREFERENCED_PARAMETER(LeafHelperDllSocketContext);
    UNREFERENCED_PARAMETER(LeafSocketHandle);
    UNREFERENCED_PARAMETER(Sockaddr);
    UNREFERENCED_PARAMETER(SockaddrLength);
    UNREFERENCED_PARAMETER(CallerData);
    UNREFERENCED_PARAMETER(CalleeData);
    UNREFERENCED_PARAMETER(SocketQOS);
    UNREFERENCED_PARAMETER(GroupQOS);
    UNREFERENCED_PARAMETER(Flags);
    return WSAEOPNOTSUPP;
}

INT
EXPORT
WSHEnumProtocols(
    _In_opt_ LPINT Protocols,
    _In_opt_ LPWSTR TransportKeyName,
    _Inout_ LPVOID ProtocolBuffer,
    _Inout_ LPDWORD BufferLength)
{
    UNREFERENCED_PARAMETER(Protocols);
    UNREFERENCED_PARAMETER(TransportKeyName);
    UNREFERENCED_PARAMETER(ProtocolBuffer);
    UNREFERENCED_PARAMETER(BufferLength);
    return WSAEOPNOTSUPP;
}

INT
EXPORT
WSHGetProviderGuid(
    _In_ LPWSTR ProviderName,
    _Out_ LPGUID ProviderGuid)
{
    UNREFERENCED_PARAMETER(ProviderName);
    if (!ProviderGuid)
        return WSAEFAULT;
    *ProviderGuid = WshBthProviderGuid;
    return NO_ERROR;
}

INT
EXPORT
WSHGetWSAProtocolInfo(
    _In_ LPWSTR ProviderName,
    _Out_ LPWSAPROTOCOL_INFOW *ProtocolInfo,
    _Out_ LPDWORD ProtocolInfoEntries)
{
    UNREFERENCED_PARAMETER(ProviderName);
    UNREFERENCED_PARAMETER(ProtocolInfo);
    UNREFERENCED_PARAMETER(ProtocolInfoEntries);
    return WSAEOPNOTSUPP;
}
