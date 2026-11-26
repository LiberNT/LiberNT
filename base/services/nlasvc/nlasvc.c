/*
 * PROJECT:     LiberNT Network Location Awareness Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Active Internet probing of every connected interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <objbase.h>
#include <strsafe.h>

#include "probe.h"

#define NDEBUG
#include <debug.h>

#define NLA_PARAMETERS_KEY NLA_SERVICE_KEY L"\\Parameters\\Internet"
#define NLA_FAMILY_COUNT 2
#define NLA_HOST_LENGTH 256
#define NLA_TEXT_LENGTH 256
#define NLA_GUID_LENGTH 39
#define NLA_RESPONSE_LENGTH 16384
#define NLA_REQUEST_LENGTH 1024
#define NLA_MS_PER_SECOND 1000
#define NLA_DEFAULT_WEB_TIMEOUT 35
#define NLA_DEFAULT_BACKOFF_INCREMENT 5
#define NLA_DEFAULT_BACKOFF_MAX 30
#define NLA_STOP_WAIT_HINT 5000

typedef struct _NLA_PROBE_TARGET
{
    WCHAR Host[NLA_HOST_LENGTH];
    CHAR HostUtf8[NLA_HOST_LENGTH * 3];
    CHAR Path[NLA_TEXT_LENGTH * 3];
    CHAR Content[NLA_TEXT_LENGTH * 3];
} NLA_PROBE_TARGET;

typedef struct _NLA_CONFIG
{
    BOOL Enabled;
    DWORD WebTimeoutMs;
    DWORD BackOffIncrementMs;
    DWORD BackOffMaxMs;
    NLA_PROBE_TARGET Target[NLA_FAMILY_COUNT];
} NLA_CONFIG;

typedef struct _NLA_INTERFACE
{
    WCHAR Id[NLA_GUID_LENGTH + 1];
    BOOL Eligible[NLA_FAMILY_COUNT];
    SOCKADDR_STORAGE Source[NLA_FAMILY_COUNT];
    INT SourceLength[NLA_FAMILY_COUNT];
} NLA_INTERFACE;

static const WCHAR ServiceName[] = L"NlaSvc";
static const WCHAR *const FamilyValueName[NLA_FAMILY_COUNT] = { NLA_CONNECTIVITY_IPV4, NLA_CONNECTIVITY_IPV6 };
static const ADDRESS_FAMILY FamilyId[NLA_FAMILY_COUNT] = { AF_INET, AF_INET6 };

static SERVICE_STATUS_HANDLE ServiceStatusHandle;
static SERVICE_STATUS ServiceStatus;
static HANDLE StopEvent;
static HANDLE ChangeEvent;
static HKEY ConnectivityKey;

static VOID
NlaReportStatus(DWORD State, DWORD ExitCode, DWORD WaitHint)
{
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = State;
    ServiceStatus.dwControlsAccepted = (State == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    ServiceStatus.dwWin32ExitCode = ExitCode;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = (State == SERVICE_RUNNING || State == SERVICE_STOPPED) ? 0 : ServiceStatus.dwCheckPoint + 1;
    ServiceStatus.dwWaitHint = WaitHint;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
}

static DWORD WINAPI
NlaControlHandler(DWORD Control, DWORD EventType, LPVOID EventData, LPVOID Context)
{
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventData);
    UNREFERENCED_PARAMETER(Context);

    switch (Control)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            NlaReportStatus(SERVICE_STOP_PENDING, NO_ERROR, NLA_STOP_WAIT_HINT);
            SetEvent(StopEvent);
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static VOID
NlaReadString(HKEY Key, PCWSTR Name, PCWSTR Default, PWSTR Buffer, DWORD Length)
{
    DWORD Size = Length * sizeof(WCHAR);

    if (!Key || RegGetValueW(Key, NULL, Name, RRF_RT_REG_SZ, NULL, Buffer, &Size) != ERROR_SUCCESS || Buffer[0] == 0)
        StringCchCopyW(Buffer, Length, Default);
}

static DWORD
NlaReadDword(HKEY Key, PCWSTR Name, DWORD Default)
{
    DWORD Value, Size = sizeof(Value);

    if (!Key || RegGetValueW(Key, NULL, Name, RRF_RT_REG_DWORD, NULL, &Value, &Size) != ERROR_SUCCESS)
        return Default;
    return Value;
}

static VOID
NlaReadUtf8(HKEY Key, PCWSTR Name, PCWSTR Default, PSTR Buffer, INT Length)
{
    WCHAR Text[NLA_TEXT_LENGTH];

    NlaReadString(Key, Name, Default, Text, ARRAYSIZE(Text));
    if (!WideCharToMultiByte(CP_UTF8, 0, Text, -1, Buffer, Length, NULL, NULL))
        Buffer[0] = 0;
}

static DWORD
NlaSecondsToMs(DWORD Seconds)
{
    return (Seconds > MAXDWORD / NLA_MS_PER_SECOND) ? MAXDWORD : Seconds * NLA_MS_PER_SECOND;
}

static VOID
NlaLoadConfig(NLA_CONFIG *Config)
{
    static const PCWSTR HostName[NLA_FAMILY_COUNT] = { L"ActiveWebProbeHost", L"ActiveWebProbeHostV6" };
    static const PCWSTR HostDefault[NLA_FAMILY_COUNT] = { L"www.msftconnecttest.com", L"ipv6.msftconnecttest.com" };
    static const PCWSTR PathName[NLA_FAMILY_COUNT] = { L"ActiveWebProbePath", L"ActiveWebProbePathV6" };
    static const PCWSTR ContentName[NLA_FAMILY_COUNT] = { L"ActiveWebProbeContent", L"ActiveWebProbeContentV6" };
    HKEY Key = NULL;
    ULONG Family;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NLA_PARAMETERS_KEY, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        Key = NULL;

    Config->Enabled = NlaReadDword(Key, L"EnableActiveProbing", 1) != 0;
    Config->WebTimeoutMs = NlaSecondsToMs(NlaReadDword(Key, L"WebTimeout", NLA_DEFAULT_WEB_TIMEOUT));
    Config->BackOffIncrementMs = NlaSecondsToMs(NlaReadDword(Key, L"CaptivePortalTimerBackOffIncrementsInSeconds",
                                                             NLA_DEFAULT_BACKOFF_INCREMENT));
    Config->BackOffMaxMs = NlaSecondsToMs(NlaReadDword(Key, L"CaptivePortalTimerMaxInSeconds", NLA_DEFAULT_BACKOFF_MAX));
    if (Config->BackOffIncrementMs == 0)
        Config->BackOffIncrementMs = NlaSecondsToMs(NLA_DEFAULT_BACKOFF_INCREMENT);
    if (Config->BackOffMaxMs < Config->BackOffIncrementMs)
        Config->BackOffMaxMs = Config->BackOffIncrementMs;

    for (Family = 0; Family < NLA_FAMILY_COUNT; Family++)
    {
        NLA_PROBE_TARGET *Target = &Config->Target[Family];

        NlaReadString(Key, HostName[Family], HostDefault[Family], Target->Host, ARRAYSIZE(Target->Host));
        if (!WideCharToMultiByte(CP_UTF8, 0, Target->Host, -1, Target->HostUtf8, sizeof(Target->HostUtf8), NULL, NULL))
            Target->HostUtf8[0] = 0;
        NlaReadUtf8(Key, PathName[Family], L"connecttest.txt", Target->Path, sizeof(Target->Path));
        NlaReadUtf8(Key, ContentName[Family], L"Microsoft Connect Test", Target->Content, sizeof(Target->Content));
    }

    if (Key)
        RegCloseKey(Key);
}

static BOOL
NlaIsGlobalIpv6(const SOCKADDR_IN6 *Address)
{
    const UCHAR *Bytes = Address->sin6_addr.u.Byte;

    if (IN6_IS_ADDR_LINKLOCAL(&Address->sin6_addr) || IN6_IS_ADDR_SITELOCAL(&Address->sin6_addr) ||
        IN6_IS_ADDR_LOOPBACK(&Address->sin6_addr) || IN6_IS_ADDR_UNSPECIFIED(&Address->sin6_addr) ||
        IN6_IS_ADDR_MULTICAST(&Address->sin6_addr))
    {
        return FALSE;
    }
    return (Bytes[0] & 0xFE) != 0xFC;
}

static BOOL
NlaHasGateway(const IP_ADAPTER_ADDRESSES *Adapter, ADDRESS_FAMILY Family)
{
    const IP_ADAPTER_GATEWAY_ADDRESS *Gateway;

    for (Gateway = Adapter->FirstGatewayAddress; Gateway; Gateway = Gateway->Next)
    {
        if (Gateway->Address.lpSockaddr && Gateway->Address.lpSockaddr->sa_family == Family)
            return TRUE;
    }
    return FALSE;
}

static BOOL
NlaPickSource(const IP_ADAPTER_ADDRESSES *Adapter, ULONG Family, NLA_INTERFACE *Interface)
{
    const IP_ADAPTER_UNICAST_ADDRESS *Unicast;

    if (!NlaHasGateway(Adapter, FamilyId[Family]))
        return FALSE;

    for (Unicast = Adapter->FirstUnicastAddress; Unicast; Unicast = Unicast->Next)
    {
        const SOCKADDR *Address = Unicast->Address.lpSockaddr;
        INT Length = Unicast->Address.iSockaddrLength;

        if (!Address || Address->sa_family != FamilyId[Family] || Length <= 0 ||
            Length > (INT)sizeof(Interface->Source[Family]))
        {
            continue;
        }
        if (Family == 1 && !NlaIsGlobalIpv6((const SOCKADDR_IN6 *)Address))
            continue;
        if (Unicast->DadState != IpDadStatePreferred && Unicast->DadState != IpDadStateDeprecated)
            continue;

        CopyMemory(&Interface->Source[Family], Address, Length);
        Interface->SourceLength[Family] = Length;
        if (Family == 0)
            ((SOCKADDR_IN *)&Interface->Source[Family])->sin_port = 0;
        else
            ((SOCKADDR_IN6 *)&Interface->Source[Family])->sin6_port = 0;
        return TRUE;
    }
    return FALSE;
}

static NLA_INTERFACE *
NlaCollectInterfaces(ULONG *Count)
{
    IP_ADAPTER_ADDRESSES *Buffer = NULL, *Adapter;
    NLA_INTERFACE *List;
    ULONG Size = 0, Error, Total = 0, Index = 0;

    *Count = 0;
    for (;;)
    {
        Error = GetAdaptersAddresses(AF_UNSPEC,
                                     GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                     GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
                                     NULL, Buffer, &Size);
        if (Error != ERROR_BUFFER_OVERFLOW)
            break;
        HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Buffer)
            return NULL;
    }
    if (Error != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return NULL;
    }

    for (Adapter = Buffer; Adapter; Adapter = Adapter->Next)
        Total++;
    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, max(Total, 1) * sizeof(*List));
    if (!List)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return NULL;
    }

    for (Adapter = Buffer; Adapter; Adapter = Adapter->Next)
    {
        NLA_INTERFACE *Interface = &List[Index];
        GUID Id;
        ULONG Family;

        if (Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || Adapter->OperStatus != IfOperStatusUp)
            continue;
        if (ConvertInterfaceLuidToGuid(&Adapter->Luid, &Id) != NO_ERROR ||
            !StringFromGUID2(&Id, Interface->Id, ARRAYSIZE(Interface->Id)))
        {
            continue;
        }
        for (Family = 0; Family < NLA_FAMILY_COUNT; Family++)
            Interface->Eligible[Family] = NlaPickSource(Adapter, Family, Interface);
        Index++;
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    *Count = Index;
    return List;
}

static NLA_PROBE_RESULT
NlaProbeAddress(const NLA_PROBE_TARGET *Target,
                const ADDRINFOW *Address,
                const NLA_INTERFACE *Interface,
                ULONG Family,
                ULONGLONG Deadline,
                BOOL *Stopped)
{
    NLA_PROBE_RESULT Result = NlaProbeNoInternet;
    CHAR Request[NLA_REQUEST_LENGTH];
    CHAR *Response;
    SIZE_T Received = 0;
    SOCKET Socket;
    WSAEVENT Event;
    HANDLE Handles[2];
    BOOL Connected = FALSE, Finished = FALSE;
    size_t RequestLength;

    RequestLength = NlaBuildProbeRequest(Request, sizeof(Request), Target->HostUtf8, Target->Path);
    if (RequestLength == 0)
        return NlaProbeNoInternet;

    Response = HeapAlloc(GetProcessHeap(), 0, NLA_RESPONSE_LENGTH);
    if (!Response)
        return NlaProbeNoInternet;

    Socket = socket(Address->ai_family, SOCK_STREAM, IPPROTO_TCP);
    Event = WSACreateEvent();
    if (Socket == INVALID_SOCKET || Event == WSA_INVALID_EVENT ||
        bind(Socket, (const SOCKADDR *)&Interface->Source[Family], Interface->SourceLength[Family]) == SOCKET_ERROR ||
        WSAEventSelect(Socket, Event, FD_CONNECT | FD_READ | FD_CLOSE) == SOCKET_ERROR ||
        (connect(Socket, Address->ai_addr, (INT)Address->ai_addrlen) == SOCKET_ERROR &&
         WSAGetLastError() != WSAEWOULDBLOCK))
    {
        Finished = TRUE;
    }

    Handles[0] = StopEvent;
    Handles[1] = Event;
    while (!Finished)
    {
        WSANETWORKEVENTS Events;
        ULONGLONG Now = GetTickCount64();
        DWORD Wait;

        if (Now >= Deadline)
            break;
        Wait = WaitForMultipleObjects(2, Handles, FALSE, (DWORD)min(Deadline - Now, (ULONGLONG)MAXDWORD - 1));
        if (Wait == WAIT_OBJECT_0)
        {
            *Stopped = TRUE;
            break;
        }
        if (Wait != WAIT_OBJECT_0 + 1 || WSAEnumNetworkEvents(Socket, Event, &Events) == SOCKET_ERROR)
            break;

        if (Events.lNetworkEvents & FD_CONNECT)
        {
            if (Events.iErrorCode[FD_CONNECT_BIT] != 0 ||
                send(Socket, Request, (INT)RequestLength, 0) != (INT)RequestLength)
            {
                break;
            }
            Connected = TRUE;
        }
        if (Events.lNetworkEvents & (FD_READ | FD_CLOSE))
        {
            while (Received < NLA_RESPONSE_LENGTH)
            {
                INT Count = recv(Socket, Response + Received, (INT)(NLA_RESPONSE_LENGTH - Received), 0);

                if (Count > 0)
                {
                    Received += Count;
                    continue;
                }
                if (Count == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
                    Finished = TRUE;
                break;
            }
            if (Received >= NLA_RESPONSE_LENGTH || (Events.lNetworkEvents & FD_CLOSE))
                Finished = TRUE;
        }
    }

    if (Connected && !*Stopped && Received != 0)
        Result = NlaClassifyProbeResponse(Response, Received, Target->Content);

    if (Socket != INVALID_SOCKET)
        closesocket(Socket);
    if (Event != WSA_INVALID_EVENT)
        WSACloseEvent(Event);
    HeapFree(GetProcessHeap(), 0, Response);
    return Result;
}

static NLA_PROBE_RESULT
NlaProbe(const NLA_CONFIG *Config, const NLA_INTERFACE *Interface, ULONG Family, BOOL *Stopped)
{
    const NLA_PROBE_TARGET *Target = &Config->Target[Family];
    NLA_PROBE_RESULT Result = NlaProbeNoInternet;
    ADDRINFOW Hints, *Addresses, *Address;
    ULONGLONG Deadline = GetTickCount64() + Config->WebTimeoutMs;

    if (Target->Host[0] == 0 || Target->HostUtf8[0] == 0 || Target->Content[0] == 0)
        return NlaProbeNoInternet;

    ZeroMemory(&Hints, sizeof(Hints));
    Hints.ai_family = FamilyId[Family];
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_protocol = IPPROTO_TCP;
    if (GetAddrInfoW(Target->Host, L"80", &Hints, &Addresses) != 0)
        return NlaProbeNoInternet;

    for (Address = Addresses; Address && !*Stopped; Address = Address->ai_next)
    {
        Result = NlaProbeAddress(Target, Address, Interface, Family, Deadline, Stopped);
        if (Result != NlaProbeNoInternet || GetTickCount64() >= Deadline)
            break;
    }

    FreeAddrInfoW(Addresses);
    return Result;
}

static VOID
NlaStoreResult(PCWSTR Id, ULONG Family, NLA_PROBE_RESULT Result)
{
    HKEY Key;
    DWORD Value = Result, Current, Size = sizeof(Current), Type;

    if (RegCreateKeyExW(ConnectivityKey, Id, 0, NULL, REG_OPTION_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE,
                        NULL, &Key, NULL) != ERROR_SUCCESS)
    {
        return;
    }
    if (Result == NlaProbeNotRun)
    {
        RegDeleteValueW(Key, FamilyValueName[Family]);
    }
    else if (RegQueryValueExW(Key, FamilyValueName[Family], NULL, &Type, (LPBYTE)&Current, &Size) != ERROR_SUCCESS ||
             Type != REG_DWORD || Current != Value)
    {
        RegSetValueExW(Key, FamilyValueName[Family], 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    }
    RegCloseKey(Key);
}

static VOID
NlaRemoveStale(const NLA_INTERFACE *List, ULONG Count, BOOL KeepNone)
{
    WCHAR Name[NLA_GUID_LENGTH + 1];
    DWORD Index = 0, Length;

    for (;;)
    {
        ULONG Item;
        BOOL Present = FALSE;

        Length = ARRAYSIZE(Name);
        if (RegEnumKeyExW(ConnectivityKey, Index, Name, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        for (Item = 0; !KeepNone && Item < Count; Item++)
        {
            if (_wcsicmp(Name, List[Item].Id) == 0)
            {
                Present = TRUE;
                break;
            }
        }
        if (Present || RegDeleteKeyW(ConnectivityKey, Name) != ERROR_SUCCESS)
            Index++;
    }
}

static BOOL
NlaProbeAll(BOOL *Stopped)
{
    NLA_CONFIG Config;
    NLA_INTERFACE *List;
    ULONG Count, Index, Family;
    BOOL Retry = FALSE;

    NlaLoadConfig(&Config);
    List = NlaCollectInterfaces(&Count);
    if (!List)
        return TRUE;

    NlaRemoveStale(List, Count, !Config.Enabled);
    for (Index = 0; Config.Enabled && Index < Count && !*Stopped; Index++)
    {
        for (Family = 0; Family < NLA_FAMILY_COUNT && !*Stopped; Family++)
        {
            NLA_PROBE_RESULT Result = NlaProbeNotRun;

            if (List[Index].Eligible[Family])
                Result = NlaProbe(&Config, &List[Index], Family, Stopped);
            if (*Stopped)
                break;
            NlaStoreResult(List[Index].Id, Family, Result);
            if (Result == NlaProbeNoInternet || Result == NlaProbeCaptivePortal)
                Retry = TRUE;
        }
    }

    HeapFree(GetProcessHeap(), 0, List);
    return Retry;
}

static DWORD
NlaBackOff(DWORD Current)
{
    NLA_CONFIG Config;

    NlaLoadConfig(&Config);
    if (Current >= Config.BackOffMaxMs - min(Config.BackOffMaxMs, Config.BackOffIncrementMs))
        return Config.BackOffMaxMs;
    return Current + Config.BackOffIncrementMs;
}

static VOID WINAPI
NlaInterfaceChanged(PVOID Context, PMIB_IPINTERFACE_ROW Row, MIB_NOTIFICATION_TYPE Type)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Row);
    UNREFERENCED_PARAMETER(Type);
    SetEvent(ChangeEvent);
}

static VOID WINAPI
NlaAddressChanged(PVOID Context, PMIB_UNICASTIPADDRESS_ROW Row, MIB_NOTIFICATION_TYPE Type)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Row);
    UNREFERENCED_PARAMETER(Type);
    SetEvent(ChangeEvent);
}

static VOID WINAPI
NlaRouteChanged(PVOID Context, PMIB_IPFORWARD_ROW2 Row, MIB_NOTIFICATION_TYPE Type)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Row);
    UNREFERENCED_PARAMETER(Type);
    SetEvent(ChangeEvent);
}

static VOID WINAPI
NlaServiceMain(DWORD Argc, LPWSTR *Argv)
{
    HANDLE InterfaceNotify = NULL, AddressNotify = NULL, RouteNotify = NULL;
    HANDLE Handles[2];
    WSADATA WsaData;
    DWORD Error = NO_ERROR, BackOffMs = 0, Wait;
    BOOL Stopped = FALSE;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    ServiceStatusHandle = RegisterServiceCtrlHandlerExW(ServiceName, NlaControlHandler, NULL);
    if (!ServiceStatusHandle)
        return;
    NlaReportStatus(SERVICE_START_PENDING, NO_ERROR, NLA_STOP_WAIT_HINT);

    StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ChangeEvent = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (!StopEvent || !ChangeEvent)
        Error = GetLastError();
    else if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
        Error = ERROR_NOT_READY;

    if (Error == NO_ERROR)
    {
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, NLA_CONNECTIVITY_KEY);
        Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, NLA_CONNECTIVITY_KEY, 0, NULL, REG_OPTION_VOLATILE,
                                KEY_ALL_ACCESS, NULL, &ConnectivityKey, NULL);
    }
    if (Error == NO_ERROR)
    {
        NotifyIpInterfaceChange(AF_UNSPEC, NlaInterfaceChanged, NULL, FALSE, &InterfaceNotify);
        NotifyUnicastIpAddressChange(AF_UNSPEC, NlaAddressChanged, NULL, FALSE, &AddressNotify);
        NotifyRouteChange2(AF_UNSPEC, NlaRouteChanged, NULL, FALSE, &RouteNotify);
        NlaReportStatus(SERVICE_RUNNING, NO_ERROR, 0);

        Handles[0] = StopEvent;
        Handles[1] = ChangeEvent;
        for (;;)
        {
            Wait = WaitForMultipleObjects(2, Handles, FALSE, BackOffMs ? BackOffMs : INFINITE);
            if (Wait == WAIT_OBJECT_0 || Wait == WAIT_FAILED)
                break;
            if (Wait == WAIT_OBJECT_0 + 1)
                BackOffMs = 0;
            BackOffMs = NlaProbeAll(&Stopped) ? NlaBackOff(BackOffMs) : 0;
            if (Stopped)
                break;
        }

        if (RouteNotify)
            CancelMibChangeNotify2(RouteNotify);
        if (AddressNotify)
            CancelMibChangeNotify2(AddressNotify);
        if (InterfaceNotify)
            CancelMibChangeNotify2(InterfaceNotify);
        RegCloseKey(ConnectivityKey);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, NLA_CONNECTIVITY_KEY);
        ConnectivityKey = NULL;
        WSACleanup();
    }
    else
    {
        DPRINT1("NlaSvc could not start: %lu\n", Error);
    }

    NlaReportStatus(SERVICE_STOPPED, Error, 0);
}

int
wmain(int argc, WCHAR *argv[])
{
    SERVICE_TABLE_ENTRYW ServiceTable[] =
    {
        { (LPWSTR)ServiceName, NlaServiceMain },
        { NULL, NULL }
    };

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    return StartServiceCtrlDispatcherW(ServiceTable) ? 0 : (int)GetLastError();
}
