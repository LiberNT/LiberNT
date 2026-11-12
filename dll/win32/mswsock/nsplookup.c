#include "precomp.h"
#include <ws2tcpip.h>

#include <stdlib.h>
#include <ws2spi.h>
#include <nspapi.h>
#include <winuser.h>
#include <windns.h>
#include <guiddef.h>
#include <svcguid.h>
#include <iptypes.h>
#include <strsafe.h>
#include <winreg.h>
#include <iphlpapi.h>

#include "mswhelper.h"

#define NDEBUG
#include <debug.h>

#define NSP_CALLID_DNS 0x0001
#define NSP_CALLID_HOSTNAME 0x0002
#define NSP_CALLID_HOSTBYNAME 0x0003
#define NSP_CALLID_SERVICEBYNAME 0x0004
#define NSP_CALLID_NLA 0x0005

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif // BUFSIZ
#ifndef WS2_INTERNAL_MAX_ALIAS
#define WS2_INTERNAL_MAX_ALIAS 512
#endif // WS2_INTERNAL_MAX_ALIAS

//#define IP_LOCALHOST 0x0100007F

//#define NSP_REDIRECT

#define NSP_FAMILY_INET  0x0001
#define NSP_FAMILY_INET6 0x0002
#define NSP_MAX_ADDRESSES 64

typedef struct {
  WCHAR* hostnameW;
  DWORD addr4;
  SOCKADDR_STORAGE* addresses;
  DWORD addressCount;
  WCHAR* servnameW;
  WCHAR* servprotoW;
  CHAR** servaliasesA; /* array */
  WORD servport;
} WSHOSTINFOINTERN, *PWSHOSTINFOINTERN;

typedef struct {
  GUID providerId; /* Provider-ID */
  DWORD dwControlFlags; /* dwControlFlags (WSALookupServiceBegin) */
  DWORD CallID; /* List for LookupServiceNext-Calls */
  DWORD CallIDCounter; /* call-count of the current CallID. */
  WCHAR* hostnameW; /* hostbyname */
  DWORD families;
#ifdef NSP_REDIRECT
  HANDLE rdrLookup;
  NSP_ROUTINE rdrproc;
#endif
} WSHANDLEINTERN, *PWSHANDLEINTERN;

static const GUID guid_NULL = {0};
static const GUID guid_HOSTNAME = SVCID_HOSTNAME;
static const GUID guid_INET_HOSTADDRBYINETSTRING = SVCID_INET_HOSTADDRBYINETSTRING;
static const GUID guid_INET_HOSTADDRBYNAME = SVCID_INET_HOSTADDRBYNAME;
static const GUID guid_INET_SERVICEBYNAME = SVCID_INET_SERVICEBYNAME;

/* GUIDs - maybe they should be loaded from registry? */
/* Namespace: 32 */
static const GUID guid_mswsock_TcpIp = {/*Data1:*/ 0x22059D40,
                                        /*Data2:*/ 0x7E9E,
                                        /*Data3:*/ 0x11CF,
                                        /*Data4:*/ {0xAE, 0x5A, 0x00, 0xAA, 0x00, 0xA7, 0x11, 0x2B}};

/* {6642243A-3BA8-4AA6-BAA5-2E0BD71FDD83} */
/* Namespace: 15 */
static const GUID guid_mswsock_NLA = {/*Data1:*/ 0x6642243A,
                                      /*Data2:*/ 0x3BA8,
                                      /*Data3:*/ 0x4AA6,
                                      /*Data4:*/ {0xBA, 0xA5, 0x2E, 0x0B, 0xD7, 0x1F, 0xDD, 0x83}};

static const GUID guid_NLA_SERVICE_CLASS = {0x0037E515, 0xB5C9, 0x4A43, {0xBA, 0xDA, 0x8B, 0x48, 0xA8, 0x7A, 0xD2, 0x39}};

#ifdef NSP_REDIRECT

typedef INT
(CALLBACK *lpRdrNSPStartup)(
  LPGUID lpProviderId,
  LPNSP_ROUTINE lpRout);

const rdrLib = "mswsock.dll-original";
lpRdrNSPStartup rdrNSPStartup;
HANDLE hLib;
NSP_ROUTINE rdrproc_tcpip;
NSP_ROUTINE rdrproc_nla;

#endif /* NSP_REDIRECT */

/* Forwards */
INT
WINAPI
mswNSPStartup(
  LPGUID lpProviderId,
  LPNSP_ROUTINE lpRout);

INT
NSP_LookupServiceBeginW(
  PWSHANDLEINTERN data,
  CHAR* hostnameA,
  WCHAR* hostnameW,
  DWORD CallID);

INT
NSP_LookupServiceNextW(
  _In_ PWSHANDLEINTERN data,
  _In_ DWORD dwControlFlags,
  _Inout_ LPWSAQUERYSETW lpRes,
  _Inout_ LPDWORD lpResLen);

INT
NSP_GetHostNameHeapAllocW(
  _Out_ WCHAR** hostname);

INT
NSP_GetHostByNameHeapAllocW(
  _In_ PWSHANDLEINTERN data,
  _In_ DWORD dwControlFlags,
  _Out_ PWSHOSTINFOINTERN hostinfo);

INT
NSP_GetServiceByNameHeapAllocW(
  _In_ PWSHANDLEINTERN data,
  _In_ DWORD dwControlFlags,
  _Out_ PWSHOSTINFOINTERN hostinfo);

/* Implementations - Internal */

INT
WSAAPI
mwsNSPCleanUp(_In_ LPGUID lpProviderId)
{
    //WSASetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    //return ERROR_CALL_NOT_IMPLEMENTED;
    return ERROR_SUCCESS;
}

INT
mwsNSPInit(VOID)
{
    return ERROR_SUCCESS;
}

INT
WSAAPI
mwsNSPLookupServiceBegin(_In_ LPGUID lpProviderId,
                         _In_ LPWSAQUERYSETW lpqsRestrictions,
                         _In_ LPWSASERVICECLASSINFOW lpServiceClassInfo,
                         _In_ DWORD dwControlFlags,
                         _Out_ LPHANDLE lphLookup)
{
    PWSHANDLEINTERN pLook;
    int wsaErr;

    if (!IsEqualGUID(lpProviderId, &guid_mswsock_TcpIp) &&
        !IsEqualGUID(lpProviderId, &guid_mswsock_NLA))
    {
        return ERROR_CALL_NOT_IMPLEMENTED;
    }

    /* allocate internal structure */
    pLook = HeapAlloc(GetProcessHeap(), 0, sizeof(WSHANDLEINTERN));
    if (!pLook)
    {
        WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    *lphLookup = (HANDLE)pLook;

    RtlZeroMemory(pLook, sizeof(*pLook));

    /* Anyway the ControlFlags "should" be needed
       in NSPLookupServiceNext. (see doku) But
       thats not the fact ATM. */
    pLook->dwControlFlags = dwControlFlags;
    pLook->providerId = *lpProviderId;

#ifdef NSP_REDIRECT

    if (IsEqualGUID(lpProviderId, &guid_mswsock_TcpIp))
    {
        pLook->rdrproc = rdrproc_tcpip;
    }
    else if (IsEqualGUID(lpProviderId, &guid_mswsock_NLA))
    {
        pLook->rdrproc = rdrproc_nla;
    }
    else
    {
        return ERROR_CALL_NOT_IMPLEMENTED;
    }

    if (pLook->rdrproc.NSPLookupServiceBegin(lpProviderId,
                                             lpqsRestrictions,
                                             lpServiceClassInfo,
                                             dwControlFlags,
                                             &pLook->rdrLookup) == NO_ERROR)
    {
        wsaErr = NO_ERROR;
    }
    else
    {
        wsaErr = WSAGetLastError();
    }

    /*
    if (res)
        res = WSAGetLastError();
    */

#else /* NSP_REDIRECT */

    wsaErr = ERROR_CALL_NOT_IMPLEMENTED;
    if (IsEqualGUID(lpProviderId, &guid_mswsock_NLA))
    {
        if ((lpqsRestrictions->dwNameSpace != NS_NLA && lpqsRestrictions->dwNameSpace != NS_ALL) ||
            (lpqsRestrictions->lpServiceClassId &&
             !IsEqualGUID(lpqsRestrictions->lpServiceClassId, &guid_NLA_SERVICE_CLASS)))
        {
            wsaErr = WSASERVICE_NOT_FOUND;
        }
        else
        {
            pLook->CallID = NSP_CALLID_NLA;
            wsaErr = NO_ERROR;
        }
    }
    else if (lpqsRestrictions->lpServiceClassId == NULL)
    {
        wsaErr = ERROR_CALL_NOT_IMPLEMENTED;
    }
    else if (IsEqualGUID(lpqsRestrictions->lpServiceClassId, &guid_NULL))
    {
        wsaErr = ERROR_CALL_NOT_IMPLEMENTED;
    }
    else if (IsEqualGUID(lpqsRestrictions->lpServiceClassId, &guid_HOSTNAME))
    {
        wsaErr = NSP_LookupServiceBeginW(pLook,
                                         NULL,
                                         NULL,
                                         NSP_CALLID_HOSTNAME);
    }
    else if (IsEqualGUID(lpqsRestrictions->lpServiceClassId,
                         &guid_INET_HOSTADDRBYNAME))
    {
       DWORD i;

       for (i = 0; lpqsRestrictions->lpafpProtocols && i < lpqsRestrictions->dwNumberOfProtocols; i++)
       {
           if (lpqsRestrictions->lpafpProtocols[i].iAddressFamily == AF_INET)
               pLook->families |= NSP_FAMILY_INET;
           else if (lpqsRestrictions->lpafpProtocols[i].iAddressFamily == AF_INET6)
               pLook->families |= NSP_FAMILY_INET6;
       }
       if (!pLook->families)
           pLook->families = NSP_FAMILY_INET;
       wsaErr = NSP_LookupServiceBeginW(pLook,
                                        NULL,
                                        lpqsRestrictions->lpszServiceInstanceName,
                                        NSP_CALLID_HOSTBYNAME);
    }
    else if (IsEqualGUID(lpqsRestrictions->lpServiceClassId,
                         &guid_INET_SERVICEBYNAME))
    {
       wsaErr = NSP_LookupServiceBeginW(pLook,
                                        NULL,
                                        lpqsRestrictions->lpszServiceInstanceName,
                                        NSP_CALLID_SERVICEBYNAME);
    }
    else if (IsEqualGUID(lpqsRestrictions->lpServiceClassId,
                         &guid_INET_HOSTADDRBYINETSTRING))
    {
        wsaErr = ERROR_CALL_NOT_IMPLEMENTED;
    }

#endif /* NSP_REDIRECT */

    if (wsaErr != NO_ERROR)
    {
        HeapFree(GetProcessHeap(), 0, pLook);
        *lphLookup = NULL;
        WSASetLastError(wsaErr);
        return SOCKET_ERROR;
    }
    return NO_ERROR;
}

static
INT
NSP_LookupServiceNextNla(_In_ PWSHANDLEINTERN data,
                         _In_ DWORD dwControlFlags,
                         _Inout_ LPWSAQUERYSETW lpRes,
                         _Inout_ LPDWORD lpResLen)
{
    PIP_ADAPTER_ADDRESSES Adapters = NULL, Adapter;
    ULONG AdaptersLength = 0x4000;
    ULONG Result;
    DWORD Index = 0;
    DWORD NameBytes = 0;
    DWORD Required;
    INT wsaErr = WSA_E_NO_MORE;

    for (;;)
    {
        Adapters = HeapAlloc(GetProcessHeap(), 0, AdaptersLength);
        if (!Adapters)
            return WSA_NOT_ENOUGH_MEMORY;
        Result = GetAdaptersAddresses(AF_UNSPEC,
                                      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      NULL,
                                      Adapters,
                                      &AdaptersLength);
        if (Result != ERROR_BUFFER_OVERFLOW)
            break;
        HeapFree(GetProcessHeap(), 0, Adapters);
    }
    if (Result != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Adapters);
        return (Result == ERROR_NO_DATA) ? WSA_E_NO_MORE : WSASYSCALLFAILURE;
    }

    for (Adapter = Adapters; Adapter; Adapter = Adapter->Next)
    {
        if (Adapter->OperStatus != IfOperStatusUp ||
            Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            Adapter->FirstUnicastAddress == NULL)
        {
            continue;
        }
        if (Index++ != data->CallIDCounter)
            continue;

        if ((dwControlFlags & LUP_RETURN_NAME) && Adapter->FriendlyName)
            NameBytes = (DWORD)(wcslen(Adapter->FriendlyName) + 1) * sizeof(WCHAR);
        Required = sizeof(WSAQUERYSETW) + NameBytes;
        if (*lpResLen < Required)
        {
            *lpResLen = Required;
            wsaErr = WSAEFAULT;
            break;
        }

        lpRes->dwSize = sizeof(WSAQUERYSETW);
        lpRes->dwNameSpace = NS_NLA;
        if (NameBytes)
        {
            lpRes->lpszServiceInstanceName = (LPWSTR)(lpRes + 1);
            RtlCopyMemory(lpRes->lpszServiceInstanceName, Adapter->FriendlyName, NameBytes);
        }
        data->CallIDCounter++;
        wsaErr = NO_ERROR;
        break;
    }

    HeapFree(GetProcessHeap(), 0, Adapters);
    return wsaErr;
}

INT
WSAAPI
mwsNSPLookupServiceNext(_In_ HANDLE hLookup,
                        _In_ DWORD dwControlFlags,
                        _Inout_ LPDWORD lpdwBufferLength,
                        //_Out_writes_bytes_to_(*lpdwBufferLength, *lpdwBufferLength)
                        LPWSAQUERYSETW lpqsResults)
{
    PWSHANDLEINTERN pLook = hLookup;
    int wsaErr = 0;

#ifdef NSP_REDIRECT

    INT res = pLook->rdrproc.NSPLookupServiceNext(pLook->rdrLookup,
                                                  dwControlFlags,
                                                  lpdwBufferLength,
                                                  lpqsResults);
    wsaErr = WSAGetLastError();
    if (res != ERROR_SUCCESS)
    {
        wsaErr = WSAGetLastError();

        if (wsaErr == 0)
            wsaErr = 0xFFFFFFFF;
    }

#else /* NSP_REDIRECT */

    if ((lpdwBufferLength == NULL) || (*lpdwBufferLength == 0))
    {
        wsaErr = WSA_NOT_ENOUGH_MEMORY;
        goto End;
    }

    RtlZeroMemory(lpqsResults, *lpdwBufferLength);
    lpqsResults->dwSize = sizeof(*lpqsResults);

    if (pLook->CallID == NSP_CALLID_NLA)
    {
        wsaErr = NSP_LookupServiceNextNla(pLook,
                                          dwControlFlags,
                                          lpqsResults,
                                          lpdwBufferLength);
        goto End;
    }

    wsaErr = NSP_LookupServiceNextW(pLook,
                                    dwControlFlags,
                                    lpqsResults,
                                    lpdwBufferLength);


#endif /* NSP_REDIRECT */

End:
    if (wsaErr != 0)
    {
        WSASetLastError(wsaErr);
        return SOCKET_ERROR;
    }
    return NO_ERROR;
}

INT
WSAAPI
mwsNSPIoCtl(_In_ HANDLE hLookup,
            _In_ DWORD dwControlCode,
            _In_reads_bytes_(cbInBuffer) LPVOID lpvInBuffer,
            _In_ DWORD cbInBuffer,
            _Out_writes_bytes_to_(cbOutBuffer, *lpcbBytesReturned) LPVOID lpvOutBuffer,
            _In_ DWORD cbOutBuffer,
            _Out_ LPDWORD lpcbBytesReturned,
            _In_opt_ LPWSACOMPLETION lpCompletion,
            _In_ LPWSATHREADID lpThreadId)
{
    WSASetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

INT
WSAAPI
mwsNSPLookupServiceEnd(_In_ HANDLE hLookup)
{
    PWSHANDLEINTERN pLook;
    HANDLE hHeap;
    INT res;

    res = NO_ERROR;
    pLook = (PWSHANDLEINTERN)hLookup;
    hHeap = GetProcessHeap();

#ifdef NSP_REDIRECT
    res = pLook->rdrproc.NSPLookupServiceEnd(pLook->rdrLookup);
#endif

    if (pLook->hostnameW != NULL)
        HeapFree(hHeap, 0, pLook->hostnameW);

    HeapFree(hHeap, 0, pLook);
    return res;
}

INT
WSAAPI
mwsNSPSetService(_In_ LPGUID lpProviderId,
                 _In_ LPWSASERVICECLASSINFOW lpServiceClassInfo,
                 _In_ LPWSAQUERYSETW lpqsRegInfo,
                 _In_ WSAESETSERVICEOP essOperation,
                 _In_ DWORD dwControlFlags)
{
    WSASetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

INT
WSAAPI
mwsNSPInstallServiceClass(_In_ LPGUID lpProviderId,
                          _In_ LPWSASERVICECLASSINFOW lpServiceClassInfo)
{
    WSASetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

INT
WSAAPI
mwsNSPRemoveServiceClass(_In_ LPGUID lpProviderId,
                         _In_ LPGUID lpServiceClassId)
{
    WSASetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

INT
WSAAPI
mwsNSPGetServiceClassInfo(_In_ LPGUID lpProviderId,
                          _In_ LPDWORD lpdwBufSize,
                          _In_ LPWSASERVICECLASSINFOW lpServiceClassInfo)
{
    WSASetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
    hostnameA / hostnameW
    * only used by HOSTBYNAME
    * only one should be set

*/
INT
NSP_LookupServiceBeginW(PWSHANDLEINTERN data,
                        CHAR* hostnameA,
                        WCHAR* hostnameW,
                        DWORD CallID)
{
    HANDLE hHeap;

    if (data->CallID != 0)
        return WSAEFAULT;

    data->CallID = CallID;

    if ((CallID == NSP_CALLID_HOSTBYNAME) ||
        (CallID == NSP_CALLID_SERVICEBYNAME))
    {
        hHeap = GetProcessHeap();

        if (data->hostnameW != NULL)
            HeapFree(hHeap, 0, data->hostnameW);

        if (hostnameA != NULL)
        {
            data->hostnameW = StrA2WHeapAlloc(hHeap, hostnameA);
        }
        else
        {
            data->hostnameW = StrCpyHeapAllocW(hHeap, hostnameW);
        }
    }

    WSASetLastError(0);

    return ERROR_SUCCESS;
}

INT
NSP_GetHostNameHeapAllocW(_Out_ WCHAR** hostname)
{
    WCHAR* name;
    HANDLE hHeap = GetProcessHeap();
    DWORD bufCharLen = MAX_COMPUTERNAME_LENGTH + 1;
    DWORD bufByteLen = bufCharLen * sizeof(WCHAR);

    name = HeapAlloc(hHeap, 0, bufByteLen);

    if (!GetComputerNameExW(ComputerNameDnsHostname,
                            name,
                            &bufCharLen))
    {
        HeapFree(hHeap, 0, name);
        WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    *hostname = name;
    return ERROR_SUCCESS;
}

static
BOOL
NSP_AddAddress(_Inout_ PWSHOSTINFOINTERN hostinfo,
               _In_ const SOCKADDR *addr,
               _In_ INT addrLength)
{
    DWORD i;

    if (!hostinfo->addresses)
    {
        hostinfo->addresses = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NSP_MAX_ADDRESSES * sizeof(SOCKADDR_STORAGE));
        if (!hostinfo->addresses)
            return FALSE;
    }
    if (hostinfo->addressCount == NSP_MAX_ADDRESSES || addrLength > (INT)sizeof(SOCKADDR_STORAGE))
        return TRUE;
    for (i = 0; i < hostinfo->addressCount; i++)
    {
        if (!memcmp(&hostinfo->addresses[i], addr, addrLength))
            return TRUE;
    }
    RtlCopyMemory(&hostinfo->addresses[hostinfo->addressCount++], addr, addrLength);
    if (addr->sa_family == AF_INET && !hostinfo->addr4)
        hostinfo->addr4 = ((const SOCKADDR_IN *)addr)->sin_addr.s_addr;
    return TRUE;
}

static
BOOL
NSP_IsLocalHostName(_In_ PCWSTR name)
{
    WCHAR host[MAX_COMPUTERNAME_LENGTH + 64];
    DWORD length = ARRAYSIZE(host);

    if (!name[0])
        return TRUE;
    if (GetComputerNameExW(ComputerNameDnsHostname, host, &length) && !_wcsicmp(name, host))
        return TRUE;
    length = ARRAYSIZE(host);
    return GetComputerNameExW(ComputerNameDnsFullyQualified, host, &length) && !_wcsicmp(name, host);
}

static
INT
NSP_GetLocalHostAddresses(_In_ PWSHANDLEINTERN data,
                          _Out_ PWSHOSTINFOINTERN hostinfo)
{
    ULONG size = 16384;
    PIP_ADAPTER_ADDRESSES list, adapter;
    DWORD err;
    SOCKADDR_IN loopback4 = {AF_INET};
    SOCKADDR_IN6 loopback6 = {AF_INET6};
    BOOL wantInet = (data->families & NSP_FAMILY_INET) != 0;
    BOOL wantInet6 = (data->families & NSP_FAMILY_INET6) != 0;

    for (;;)
    {
        list = HeapAlloc(GetProcessHeap(), 0, size);
        if (!list)
            return WSA_NOT_ENOUGH_MEMORY;
        err = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                   GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
                                   NULL, list, &size);
        if (err != ERROR_BUFFER_OVERFLOW)
            break;
        HeapFree(GetProcessHeap(), 0, list);
    }

    if (err == ERROR_SUCCESS)
    {
        for (adapter = list; adapter; adapter = adapter->Next)
        {
            PIP_ADAPTER_UNICAST_ADDRESS unicast;

            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->OperStatus != IfOperStatusUp)
                continue;
            for (unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
            {
                const SOCKADDR *addr = unicast->Address.lpSockaddr;

                if (unicast->DadState != IpDadStatePreferred && unicast->DadState != IpDadStateDeprecated)
                    continue;
                if ((addr->sa_family == AF_INET && wantInet) || (addr->sa_family == AF_INET6 && wantInet6))
                    NSP_AddAddress(hostinfo, addr, unicast->Address.iSockaddrLength);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, list);

    if (!hostinfo->addressCount)
    {
        if (wantInet6)
        {
            loopback6.sin6_addr.s6_addr[15] = 1;
            NSP_AddAddress(hostinfo, (SOCKADDR *)&loopback6, sizeof(loopback6));
        }
        if (wantInet)
        {
            loopback4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            NSP_AddAddress(hostinfo, (SOCKADDR *)&loopback4, sizeof(loopback4));
        }
    }

    hostinfo->hostnameW = StrCpyHeapAllocW(GetProcessHeap(), data->hostnameW);
    return hostinfo->hostnameW ? ERROR_SUCCESS : WSA_NOT_ENOUGH_MEMORY;
}

static
BOOL
NSP_HasGlobalIpv6(VOID)
{
    ULONG size = 16384;
    PIP_ADAPTER_ADDRESSES list, adapter;
    BOOL found = FALSE;
    DWORD err;

    for (;;)
    {
        list = HeapAlloc(GetProcessHeap(), 0, size);
        if (!list)
            return FALSE;
        err = GetAdaptersAddresses(AF_INET6, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                   GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
                                   NULL, list, &size);
        if (err != ERROR_BUFFER_OVERFLOW)
            break;
        HeapFree(GetProcessHeap(), 0, list);
    }

    for (adapter = (err == ERROR_SUCCESS) ? list : NULL; adapter && !found; adapter = adapter->Next)
    {
        PIP_ADAPTER_UNICAST_ADDRESS unicast;

        if (adapter->OperStatus != IfOperStatusUp)
            continue;
        for (unicast = adapter->FirstUnicastAddress; unicast && !found; unicast = unicast->Next)
        {
            const SOCKADDR_IN6 *addr = (const SOCKADDR_IN6 *)unicast->Address.lpSockaddr;

            found = (unicast->DadState == IpDadStatePreferred || unicast->DadState == IpDadStateDeprecated) &&
                    !IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr) && !IN6_IS_ADDR_LOOPBACK(&addr->sin6_addr);
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
    return found;
}

static
DNS_STATUS
NSP_QueryAddresses(_In_ PWSHANDLEINTERN data,
                   _In_ DWORD dwQueryFlags,
                   _In_ WORD type,
                   _Inout_ PWSHOSTINFOINTERN hostinfo,
                   _Inout_ PWCHAR *Aliases,
                   _Inout_ int *AliasIndex)
{
    PDNS_RECORDW dp = NULL, curr;
    DNS_STATUS status;

    status = DnsQuery_W(data->hostnameW, type, dwQueryFlags, NULL, (PDNS_RECORD *)&dp, NULL);
    if (status != ERROR_SUCCESS)
        return status;

    for (curr = dp; curr; curr = curr->pNext)
    {
        if (curr->wType == DNS_TYPE_CNAME && *AliasIndex < WS2_INTERNAL_MAX_ALIAS - 1)
        {
            BOOL known = FALSE;
            int i;

            for (i = 0; i < *AliasIndex; i++)
                known = known || !_wcsicmp(Aliases[i], curr->pName);
            if (!known)
                Aliases[(*AliasIndex)++] = StrCpyHeapAllocW(GetProcessHeap(), curr->pName);
        }
        else if (curr->wType == DNS_TYPE_A)
        {
            SOCKADDR_IN sa = {AF_INET};

            sa.sin_addr.s_addr = curr->Data.A.IpAddress;
            NSP_AddAddress(hostinfo, (SOCKADDR *)&sa, sizeof(sa));
        }
        else if (curr->wType == DNS_TYPE_AAAA)
        {
            SOCKADDR_IN6 sa = {AF_INET6};

            RtlCopyMemory(&sa.sin6_addr, &curr->Data.AAAA.Ip6Address, sizeof(sa.sin6_addr));
            NSP_AddAddress(hostinfo, (SOCKADDR *)&sa, sizeof(sa));
        }
        else
        {
            continue;
        }

        if ((curr->wType == DNS_TYPE_A || curr->wType == DNS_TYPE_AAAA) && !hostinfo->hostnameW)
            hostinfo->hostnameW = StrCpyHeapAllocW(GetProcessHeap(), curr->pName);
    }

    DnsRecordListFree((PDNS_RECORD)dp, DnsFreeRecordList);
    return ERROR_SUCCESS;
}

#define NSP_NBNS_PORT 137
#define NSP_NBNS_TRIES 3
#define NSP_NBNS_TIMEOUT 750
#define NSP_NBNS_LINKS 16

typedef struct _NSP_NBNS_QUERY
{
    LONG References;
    UCHAR Packet[50];
    ULONG Addresses[NSP_MAX_ADDRESSES];
    ULONG AddressCount;
} NSP_NBNS_QUERY, *PNSP_NBNS_QUERY;

static
VOID
NSP_NbnsRelease(PNSP_NBNS_QUERY Query)
{
    if (!InterlockedDecrement(&Query->References))
        HeapFree(GetProcessHeap(), 0, Query);
}

static
ULONG
NSP_NbnsBroadcasts(PULONG Broadcasts)
{
    ULONG Size = 16384, Count = 0;
    PIP_ADAPTER_ADDRESSES List, Adapter;
    DWORD Error;

    for (;;)
    {
        List = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!List)
            return 0;
        Error = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                     GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME, NULL, List, &Size);
        if (Error != ERROR_BUFFER_OVERFLOW)
            break;
        HeapFree(GetProcessHeap(), 0, List);
    }
    for (Adapter = (Error == ERROR_SUCCESS) ? List : NULL; Adapter; Adapter = Adapter->Next)
    {
        PIP_ADAPTER_UNICAST_ADDRESS Unicast;

        if (Adapter->OperStatus != IfOperStatusUp || Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (Unicast = Adapter->FirstUnicastAddress; Unicast && Count < NSP_NBNS_LINKS; Unicast = Unicast->Next)
        {
            ULONG Address, Mask;

            if (!Unicast->Address.lpSockaddr || Unicast->Address.lpSockaddr->sa_family != AF_INET ||
                Unicast->OnLinkPrefixLength >= 32)
                continue;
            Address = ((const SOCKADDR_IN *)Unicast->Address.lpSockaddr)->sin_addr.s_addr;
            Mask = Unicast->OnLinkPrefixLength ? htonl(0xFFFFFFFFUL << (32 - Unicast->OnLinkPrefixLength)) : 0;
            Broadcasts[Count++] = Address | ~Mask;
        }
    }
    HeapFree(GetProcessHeap(), 0, List);
    return Count;
}

static
VOID
NSP_NbnsParse(PNSP_NBNS_QUERY Query, const UCHAR *Reply, INT Length)
{
    INT Offset = 12, Data;
    USHORT Answers, RdLength;

    if (Length < 12 || memcmp(Reply, Query->Packet, 2) || !(Reply[2] & 0x80) || (Reply[2] & 0x78) || (Reply[3] & 0x0F))
        return;
    Answers = (USHORT)((Reply[6] << 8) | Reply[7]);
    if (!Answers)
        return;
    if (Offset < Length && (Reply[Offset] & 0xC0) == 0xC0)
        Offset += 2;
    else if (Offset + 34 <= Length && Reply[Offset] == 0x20)
        Offset += 34;
    else
        return;
    if (Offset + 10 > Length || Reply[Offset] != 0x00 || Reply[Offset + 1] != 0x20)
        return;
    RdLength = (USHORT)((Reply[Offset + 8] << 8) | Reply[Offset + 9]);
    Data = Offset + 10;
    if (Data + RdLength > Length)
        return;
    for (; RdLength >= 6 && Query->AddressCount < NSP_MAX_ADDRESSES; Data += 6, RdLength -= 6)
        RtlCopyMemory(&Query->Addresses[Query->AddressCount++], Reply + Data + 2, sizeof(ULONG));
}

static
DWORD
WINAPI
NSP_NbnsQueryThread(LPVOID Parameter)
{
    PNSP_NBNS_QUERY Query = Parameter;
    ULONG Broadcasts[NSP_NBNS_LINKS], Count, i;
    SOCKADDR_IN Target = {AF_INET};
    UCHAR Reply[576];
    BOOL Yes = TRUE;
    HANDLE Mutex = NULL;
    SOCKET s;
    INT Try;

    Count = NSP_NbnsBroadcasts(Broadcasts);
    s = Count ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : INVALID_SOCKET;
    if (s != INVALID_SOCKET)
    {
        SOCKADDR_IN Local = {AF_INET};

        Mutex = CreateMutexW(NULL, FALSE, L"Global\\LiberNTNetbiosNameService");
        if (Mutex)
            WaitForSingleObject(Mutex, NSP_NBNS_TRIES * NSP_NBNS_TIMEOUT * 4);
        Local.sin_port = htons(NSP_NBNS_PORT);
        bind(s, (SOCKADDR *)&Local, sizeof(Local));
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&Yes, sizeof(Yes));
    }
    Target.sin_port = htons(NSP_NBNS_PORT);
    for (Try = 0; s != INVALID_SOCKET && Try < NSP_NBNS_TRIES && !Query->AddressCount; Try++)
    {
        DWORD Deadline = GetTickCount() + NSP_NBNS_TIMEOUT;

        for (i = 0; i < Count; i++)
        {
            Target.sin_addr.s_addr = Broadcasts[i];
            sendto(s, (char *)Query->Packet, sizeof(Query->Packet), 0, (SOCKADDR *)&Target, sizeof(Target));
        }
        while (!Query->AddressCount)
        {
            LONG Remaining = (LONG)(Deadline - GetTickCount());
            struct timeval Wait;
            fd_set Read;
            INT Received;

            if (Remaining <= 0)
                break;
            FD_ZERO(&Read);
            FD_SET(s, &Read);
            Wait.tv_sec = Remaining / 1000;
            Wait.tv_usec = (Remaining % 1000) * 1000;
            if (select(0, &Read, NULL, NULL, &Wait) <= 0)
                break;
            Received = recvfrom(s, (char *)Reply, sizeof(Reply), 0, NULL, NULL);
            if (Received > 0)
                NSP_NbnsParse(Query, Reply, Received);
        }
    }
    if (s != INVALID_SOCKET)
        closesocket(s);
    if (Mutex)
    {
        ReleaseMutex(Mutex);
        CloseHandle(Mutex);
    }
    NSP_NbnsRelease(Query);
    return 0;
}

static
HANDLE
NSP_NbnsStart(_In_ PCWSTR Name, _Out_ PNSP_NBNS_QUERY *Result)
{
    PNSP_NBNS_QUERY Query;
    UCHAR Label[16];
    HANDLE Thread;
    SIZE_T Length = wcslen(Name), i;

    *Result = NULL;
    if (!Length || Length > 15)
        return NULL;
    Query = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Query));
    if (!Query)
        return NULL;
    memset(Label, ' ', 15);
    Label[15] = 0x00;
    for (i = 0; i < Length; i++)
    {
        if (Name[i] > 0x7E || wcschr(L"\\/:*?\"<>|", Name[i]))
        {
            HeapFree(GetProcessHeap(), 0, Query);
            return NULL;
        }
        Label[i] = (UCHAR)toupper(Name[i]);
    }
    Query->Packet[0] = (UCHAR)(GetTickCount() >> 3);
    Query->Packet[1] = (UCHAR)GetCurrentThreadId();
    Query->Packet[2] = 0x01;
    Query->Packet[3] = 0x10;
    Query->Packet[5] = 1;
    Query->Packet[12] = 0x20;
    for (i = 0; i < 16; i++)
    {
        Query->Packet[13 + i * 2] = (UCHAR)('A' + (Label[i] >> 4));
        Query->Packet[14 + i * 2] = (UCHAR)('A' + (Label[i] & 0x0F));
    }
    Query->Packet[47] = 0x20;
    Query->Packet[49] = 0x01;
    Query->References = 2;
    Thread = CreateThread(NULL, 0, NSP_NbnsQueryThread, Query, 0, NULL);
    if (!Thread)
    {
        HeapFree(GetProcessHeap(), 0, Query);
        return NULL;
    }
    *Result = Query;
    return Thread;
}

static
VOID
NSP_NbnsFinish(_In_ HANDLE Thread, _In_ PNSP_NBNS_QUERY Query, _In_ BOOL Wait, _Inout_ PWSHOSTINFOINTERN hostinfo)
{
    ULONG i;

    if (!Thread)
        return;
    if (WaitForSingleObject(Thread, Wait ? INFINITE : 0) == WAIT_OBJECT_0)
    {
        for (i = 0; i < Query->AddressCount; i++)
        {
            SOCKADDR_IN sa = {AF_INET};

            sa.sin_addr.s_addr = Query->Addresses[i];
            NSP_AddAddress(hostinfo, (SOCKADDR *)&sa, sizeof(sa));
        }
    }
    CloseHandle(Thread);
    NSP_NbnsRelease(Query);
}

typedef struct _NSP_AAAA_QUERY
{
    PWSHANDLEINTERN data;
    DWORD dwQueryFlags;
    WSHOSTINFOINTERN hostinfo;
    PWCHAR Aliases[WS2_INTERNAL_MAX_ALIAS];
    int AliasIndex;
    DNS_STATUS status;
} NSP_AAAA_QUERY, *PNSP_AAAA_QUERY;

static
DWORD
WINAPI
NSP_AaaaQueryThread(LPVOID Parameter)
{
    PNSP_AAAA_QUERY Query = Parameter;

    Query->status = NSP_QueryAddresses(Query->data, Query->dwQueryFlags, DNS_TYPE_AAAA,
                                       &Query->hostinfo, Query->Aliases, &Query->AliasIndex);
    return 0;
}

static
INT
NSP_QueryFamilies(_In_ PWSHANDLEINTERN data,
                  _In_ DWORD dwQueryFlags,
                  _In_ BOOL Aaaa,
                  _Inout_ PWSHOSTINFOINTERN hostinfo,
                  _Inout_ PWCHAR *Aliases,
                  _Inout_ int *AliasIndex,
                  _Out_ DNS_STATUS *status4,
                  _Out_ DNS_STATUS *status6)
{
    HANDLE hHeap = GetProcessHeap();

    *status4 = DNS_INFO_NO_RECORDS;
    *status6 = DNS_INFO_NO_RECORDS;
    if (Aaaa)
    {
        PNSP_AAAA_QUERY Query = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(*Query));
        HANDLE Thread = NULL;
        DWORD k;

        if (!Query)
            return WSA_NOT_ENOUGH_MEMORY;
        Query->data = data;
        Query->dwQueryFlags = dwQueryFlags;
        Query->status = DNS_INFO_NO_RECORDS;
        if (data->families & NSP_FAMILY_INET)
            Thread = CreateThread(NULL, 0, NSP_AaaaQueryThread, Query, 0, NULL);
        if (!Thread)
            NSP_AaaaQueryThread(Query);
        else if (data->families & NSP_FAMILY_INET)
            *status4 = NSP_QueryAddresses(data, dwQueryFlags, DNS_TYPE_A, hostinfo, Aliases, AliasIndex);
        if (Thread)
        {
            WaitForSingleObject(Thread, INFINITE);
            CloseHandle(Thread);
        }
        *status6 = Query->status;

        for (k = Query->hostinfo.addressCount; k > 0; k--)
        {
            SOCKADDR_STORAGE *Address = &Query->hostinfo.addresses[k - 1];

            if (hostinfo->addressCount < NSP_MAX_ADDRESSES && NSP_AddAddress(hostinfo, (SOCKADDR *)Address, sizeof(SOCKADDR_IN6)))
            {
                RtlMoveMemory(&hostinfo->addresses[1], &hostinfo->addresses[0],
                              (hostinfo->addressCount - 1) * sizeof(SOCKADDR_STORAGE));
                RtlCopyMemory(&hostinfo->addresses[0], Address, sizeof(SOCKADDR_STORAGE));
            }
        }
        if (Query->hostinfo.hostnameW)
        {
            if (!hostinfo->hostnameW)
                hostinfo->hostnameW = Query->hostinfo.hostnameW;
            else
                HeapFree(hHeap, 0, Query->hostinfo.hostnameW);
        }
        for (k = 0; k < (DWORD)Query->AliasIndex; k++)
        {
            if (*AliasIndex < WS2_INTERNAL_MAX_ALIAS - 1)
                Aliases[(*AliasIndex)++] = Query->Aliases[k];
            else
                HeapFree(hHeap, 0, Query->Aliases[k]);
        }
        if (Query->hostinfo.addresses)
            HeapFree(hHeap, 0, Query->hostinfo.addresses);
        HeapFree(hHeap, 0, Query);
    }
    else if (data->families & NSP_FAMILY_INET)
    {
        *status4 = NSP_QueryAddresses(data, dwQueryFlags, DNS_TYPE_A, hostinfo, Aliases, AliasIndex);
    }

    return ERROR_SUCCESS;
}

INT
NSP_GetHostByNameHeapAllocW(_In_ PWSHANDLEINTERN data,
                            _In_ DWORD dwControlFlags,
                            _Out_ PWSHOSTINFOINTERN hostinfo)
{
    HANDLE hHeap = GetProcessHeap();
    DNS_STATUS status4 = DNS_INFO_NO_RECORDS, status6 = DNS_INFO_NO_RECORDS;
    DWORD dwQueryFlags = DNS_QUERY_STANDARD;
    PWCHAR Aliases[WS2_INTERNAL_MAX_ALIAS] = { 0 };
    PNSP_NBNS_QUERY Nbns = NULL;
    HANDLE NbnsThread = NULL;
    BOOL SingleLabel;
    int AliasIndex = 0, i;
    INT result, error;

    if (data->hostnameW == NULL)
        return ERROR_INVALID_PARAMETER;

    if (NSP_IsLocalHostName(data->hostnameW))
        return NSP_GetLocalHostAddresses(data, hostinfo);

    SingleLabel = !wcschr(data->hostnameW, L'.');
    error = NSP_QueryFamilies(data, SingleLabel ? DNS_QUERY_NO_MULTICAST : dwQueryFlags,
                              (data->families & NSP_FAMILY_INET6) &&
                              (data->families == NSP_FAMILY_INET6 || NSP_HasGlobalIpv6()),
                              hostinfo, Aliases, &AliasIndex, &status4, &status6);
    if (!error && SingleLabel && !hostinfo->addressCount)
    {
        if (data->families & NSP_FAMILY_INET)
            NbnsThread = NSP_NbnsStart(data->hostnameW, &Nbns);
        error = NSP_QueryFamilies(data, DNS_QUERY_MULTICAST_ONLY, (data->families & NSP_FAMILY_INET6) != 0,
                                  hostinfo, Aliases, &AliasIndex, &status4, &status6);
    }
    if (error)
    {
        NSP_NbnsFinish(NbnsThread, Nbns, FALSE, hostinfo);
        for (i = 0; i < AliasIndex; i++)
            HeapFree(hHeap, 0, Aliases[i]);
        return error;
    }

    NSP_NbnsFinish(NbnsThread, Nbns, !hostinfo->addressCount, hostinfo);
    if (SingleLabel && status4 == ERROR_INVALID_NAME)
        status4 = DNS_ERROR_RCODE_NAME_ERROR;
    if (SingleLabel && status6 == ERROR_INVALID_NAME)
        status6 = DNS_ERROR_RCODE_NAME_ERROR;

    if (status4 == ERROR_INVALID_NAME || status6 == ERROR_INVALID_NAME)
    {
        WSASetLastError(WSAEFAULT);
        result = ERROR_INVALID_PARAMETER;
    }
    else if (!hostinfo->addressCount)
    {
        result = (status4 == ERROR_TIMEOUT || status6 == ERROR_TIMEOUT ||
                  status4 == DNS_ERROR_RCODE_SERVER_FAILURE || status6 == DNS_ERROR_RCODE_SERVER_FAILURE) ?
                 WSATRY_AGAIN : WSAHOST_NOT_FOUND;
    }
    else
    {
        if (!hostinfo->hostnameW)
            hostinfo->hostnameW = StrCpyHeapAllocW(hHeap, data->hostnameW);
        if (AliasIndex)
            hostinfo->servaliasesA = StrAryCpyHeapAllocWToA(hHeap, (WCHAR**)&Aliases);
        result = hostinfo->hostnameW ? ERROR_SUCCESS : WSA_NOT_ENOUGH_MEMORY;
    }

    for (i = 0; i < AliasIndex; i++)
        HeapFree(hHeap, 0, Aliases[i]);
    return result;
}

#define SKIPWS(ptr, act) \
{while(*ptr && isspace(*ptr)) ptr++; if(!*ptr) act;}

#define SKIPANDMARKSTR(ptr, act) \
{while(*ptr && !isspace(*ptr)) ptr++; \
 if(!*ptr) {act;} else { *ptr = 0; ptr++; }}

static
BOOL
DecodeServEntFromString(IN PCHAR ServiceString,
                        OUT PCHAR *ServiceName,
                        OUT PCHAR *PortNumberStr,
                        OUT PCHAR *ProtocolStr,
                        IN PCHAR *Aliases,
                        IN DWORD MaxAlias)
{
    UINT NAliases = 0;

    //WS_DbgPrint(MAX_TRACE, ("Parsing service ent [%s]\n", ServiceString));

    SKIPWS(ServiceString, return FALSE);
    *ServiceName = ServiceString;
    SKIPANDMARKSTR(ServiceString, return FALSE);
    SKIPWS(ServiceString, return FALSE);
    *PortNumberStr = ServiceString;
    SKIPANDMARKSTR(ServiceString, ;);

    while (*ServiceString && NAliases < MaxAlias - 1)
    {
        SKIPWS(ServiceString, break);
        if (*ServiceString)
        {
            SKIPWS(ServiceString, {});
            if (strlen(ServiceString))
            {
                //WS_DbgPrint(MAX_TRACE, ("Alias: %s\n", ServiceString));
                *Aliases++ = ServiceString;
                NAliases++;
            }
            SKIPANDMARKSTR(ServiceString, ;);
        }
    }
    *Aliases = NULL;

    *ProtocolStr = strchr(*PortNumberStr, '/');

    if (!*ProtocolStr)
        return FALSE;

    **ProtocolStr = 0;
    (*ProtocolStr)++;

    //WS_DbgPrint(MAX_TRACE, ("Parsing done: %s %s %s %d\n",
    //           *ServiceName, *ProtocolStr, *PortNumberStr,
    //            NAliases));

    return TRUE;
}

HANDLE
WSAAPI
OpenNetworkDatabase(_In_ LPCWSTR Name)
{
    PWSTR ExpandedPath;
    PWSTR DatabasePath;
    INT ErrorCode;
    HKEY DatabaseKey;
    DWORD RegType;
    DWORD RegSize = 0;
    size_t StringLength;
    HANDLE ret;

    ExpandedPath = HeapAlloc(GetProcessHeap(), 0, MAX_PATH*sizeof(WCHAR));
    if (!ExpandedPath)
        return INVALID_HANDLE_VALUE;

    /* Open the database path key */
    ErrorCode = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                             L"System\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                             0,
                             KEY_READ,
                             &DatabaseKey);
    if (ErrorCode == NO_ERROR)
    {
        /* Read the actual path */
        ErrorCode = RegQueryValueEx(DatabaseKey,
                                    L"DatabasePath",
                                    NULL,
                                    &RegType,
                                    NULL,
                                    &RegSize);

        DatabasePath = HeapAlloc(GetProcessHeap(), 0, RegSize);
        if (!DatabasePath)
        {
            HeapFree(GetProcessHeap(), 0, ExpandedPath);
            return INVALID_HANDLE_VALUE;
        }

        /* Read the actual path */
        ErrorCode = RegQueryValueEx(DatabaseKey,
                                    L"DatabasePath",
                                    NULL,
                                    &RegType,
                                    (LPBYTE)DatabasePath,
                                    &RegSize);

        /* Close the key */
        RegCloseKey(DatabaseKey);

        /* Expand the name */
        ExpandEnvironmentStrings(DatabasePath, ExpandedPath, MAX_PATH);

        HeapFree(GetProcessHeap(), 0, DatabasePath);
    }
    else
    {
        /* Use defalt path */
        GetSystemDirectory(ExpandedPath, MAX_PATH);
        StringCchLength(ExpandedPath, MAX_PATH, &StringLength);
        if (ExpandedPath[StringLength - 1] != L'\\')
        {
            /* It isn't, so add it ourselves */
            StringCchCat(ExpandedPath, MAX_PATH, L"\\");
        }
        StringCchCat(ExpandedPath, MAX_PATH, L"DRIVERS\\ETC\\");
    }

    /* Make sure that the path is backslash-terminated */
    StringCchLength(ExpandedPath, MAX_PATH, &StringLength);
    if (ExpandedPath[StringLength - 1] != L'\\')
    {
        /* It isn't, so add it ourselves */
        StringCchCat(ExpandedPath, MAX_PATH, L"\\");
    }

    /* Add the database name */
    StringCchCat(ExpandedPath, MAX_PATH, Name);

    /* Return a handle to the file */
    ret = CreateFile(ExpandedPath,
                      FILE_READ_DATA,
                      FILE_SHARE_READ,
                      NULL,
                      OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL,
                      NULL);

    HeapFree(GetProcessHeap(), 0, ExpandedPath);
    return ret;
}

INT
NSP_GetServiceByNameHeapAllocW(_In_ PWSHANDLEINTERN data,
                               _In_ DWORD dwControlFlags,
                               _Out_ PWSHOSTINFOINTERN hostinfo)
{
    BOOL Found = FALSE;
    HANDLE ServicesFile;
    CHAR ServiceDBData[BUFSIZ * sizeof(WCHAR)] = {0};
    PCHAR ThisLine = 0, NextLine = 0, ServiceName = 0, PortNumberStr = 0,
    ProtocolStr = 0, Comment = 0, EndValid;
    PCHAR Aliases[WS2_INTERNAL_MAX_ALIAS] = {0};
    PCHAR* AliasPtr;
    UINT i = 0;
    DWORD ReadSize = 0;
    HANDLE hHeap;
    PCHAR nameA = NULL;
    PCHAR nameServiceA = NULL;
    PCHAR nameProtoA = NULL;
    INT res = WSANO_RECOVERY;

    if (!data->hostnameW)
    {
        res = WSANO_RECOVERY;
        goto End;
    }

    hHeap = GetProcessHeap();
    nameA = StrW2AHeapAlloc(hHeap, data->hostnameW);

    /* nameA has the form <service-name>/<protocol>
       we split these now */
    nameProtoA = strchr(nameA, '/');
    if (nameProtoA == NULL)
    {
        res = WSANO_RECOVERY;
        goto End;
    }

    nameProtoA++;
    i = (DWORD)(nameProtoA - nameA - 1);
    nameServiceA = (PCHAR)HeapAlloc(hHeap, 0, i + 1);
    StringCbCopyA(nameServiceA, i + 1, nameA);
    nameServiceA[i] = '\0';

    ServicesFile = OpenNetworkDatabase(L"services");
    if (ServicesFile == INVALID_HANDLE_VALUE)
    {
        return WSANO_RECOVERY;
    }

    /* Scan the services file ...
    *
    * We will be share the buffer on the lines. If the line does not fit in
    * the buffer, then moving it to the beginning of the buffer and read
    * the remnants of line from file.
    */

    /* Initial Read */
    ReadFile(ServicesFile,
             ServiceDBData,
             sizeof( ServiceDBData ) - 1,
             &ReadSize,
             NULL);

    ThisLine = NextLine = ServiceDBData;
    EndValid = ServiceDBData + ReadSize;
    ServiceDBData[sizeof(ServiceDBData) - 1] = '\0';

    while (ReadSize)
    {
        for (; *NextLine != '\r' && *NextLine != '\n'; NextLine++)
        {
            if (NextLine == EndValid)
            {
                int LineLen = NextLine - ThisLine;

                if (ThisLine == ServiceDBData)
                {
                    //WS_DbgPrint(MIN_TRACE,("Line too long"));
                    return WSANO_RECOVERY;
                }

                memmove(ServiceDBData, ThisLine, LineLen);

                ReadFile(ServicesFile,
                         ServiceDBData + LineLen,
                         sizeof( ServiceDBData )-1 - LineLen,
                         &ReadSize,
                         NULL);

                EndValid = ServiceDBData + LineLen + ReadSize;
                NextLine = ServiceDBData + LineLen;
                ThisLine = ServiceDBData;

                if (!ReadSize) break;
            }
        }

        *NextLine = '\0';
        Comment = strchr(ThisLine, '#');

        if (Comment)
            *Comment = '\0'; /* Terminate at comment start */

        if (DecodeServEntFromString(ThisLine,
                                    &ServiceName,
                                    &PortNumberStr,
                                    &ProtocolStr,
                                    Aliases,
                                    WS2_INTERNAL_MAX_ALIAS) &&
            (strlen(nameProtoA) == 0 || strcmp(ProtocolStr, nameProtoA) == 0))
        {
            Found = (strcmp(ServiceName, nameServiceA) == 0 || strcmp(PortNumberStr, nameServiceA) == 0);
            AliasPtr = Aliases;
            while ((!Found) && (*AliasPtr != NULL))
            {
                Found = (strcmp(*AliasPtr, nameServiceA) == 0);
                AliasPtr++;
            }
            if (Found)
                break;
        }
        NextLine++;
        ThisLine = NextLine;
    }

    /* This we'll do no matter what */
    CloseHandle(ServicesFile);

    if (!Found)
    {
        return WSANO_DATA;
    }

    hostinfo->addr4 = 0;
    hostinfo->servnameW = StrA2WHeapAlloc(hHeap, ServiceName);
    hostinfo->servprotoW = StrA2WHeapAlloc(hHeap, ProtocolStr);
    hostinfo->servaliasesA = StrAryCpyHeapAllocA(hHeap, (char**)&Aliases);
    hostinfo->servport = atoi(PortNumberStr);

    res = NO_ERROR;

End:
    if (nameA != NULL)
        HeapFree(hHeap, 0, nameA);

    if (nameServiceA != NULL)
        HeapFree(hHeap, 0, nameServiceA);

    return res;
}

INT
NSP_LookupServiceNextW(_In_ PWSHANDLEINTERN data,
                       _In_ DWORD dwControlFlags,
                       _Inout_ LPWSAQUERYSETW lpRes,
                       _Inout_ LPDWORD lpResLen)
{
    MSW_BUFFER buf;
    WSHOSTINFOINTERN hostinfo;
    INT result;
    HANDLE hHeap = GetProcessHeap();
    WCHAR* ServiceInstanceNameW = NULL;
    /* cleanup-vars */
    CHAR* ServiceInstanceNameA = NULL;
    CHAR* ServiceProtocolNameA = NULL;

    RtlZeroMemory(&hostinfo, sizeof(hostinfo));

    /* init and build result-buffer */
    mswBufferInit(&buf, (BYTE*)lpRes, *lpResLen);
    mswBufferIncUsed(&buf, sizeof(*lpRes));

    /* QueryDataSet-Size without "blob-data"-size! */
    lpRes->dwSize = sizeof(*lpRes);
    lpRes->dwNameSpace = NS_DNS;

    if ((data->CallID == NSP_CALLID_HOSTNAME) ||
        (data->CallID == NSP_CALLID_HOSTBYNAME) ||
        (data->CallID == NSP_CALLID_SERVICEBYNAME))
    {
        if (data->CallIDCounter >= 1)
        {
            result = WSAENOMORE;
            goto End;
        }
    }
    else
    {
        result = WSANO_RECOVERY;
        goto End;
    }
    data->CallIDCounter++;

    if (data->CallID == NSP_CALLID_HOSTNAME)
    {
        result = NSP_GetHostNameHeapAllocW(&hostinfo.hostnameW);

        if (result != ERROR_SUCCESS)
            goto End;

        hostinfo.addr4 = 0;
    }
    else if (data->CallID == NSP_CALLID_HOSTBYNAME)
    {
        result = NSP_GetHostByNameHeapAllocW(data,
                                             dwControlFlags,
                                             &hostinfo);
        if (result != ERROR_SUCCESS)
            goto End;
    }
    else
    {
        ASSERT(data->CallID == NSP_CALLID_SERVICEBYNAME);
        result = NSP_GetServiceByNameHeapAllocW(data,
                                                dwControlFlags,
                                                &hostinfo);
        if (result != ERROR_SUCCESS)
            goto End;
    }

    if (((LUP_RETURN_BLOB & data->dwControlFlags) != 0) ||
        ((LUP_RETURN_NAME & data->dwControlFlags) != 0))
    {
        if (data->CallID == NSP_CALLID_HOSTNAME || data->CallID == NSP_CALLID_HOSTBYNAME)
        {
            ServiceInstanceNameW = hostinfo.hostnameW;
            ServiceInstanceNameA = StrW2AHeapAlloc(hHeap, ServiceInstanceNameW);
            if (ServiceInstanceNameA == NULL)
            {
                result = WSAEFAULT;
                goto End;

            }
        }
        if (data->CallID == NSP_CALLID_SERVICEBYNAME)
        {
            ServiceInstanceNameW = hostinfo.servnameW;
            ServiceInstanceNameA = StrW2AHeapAlloc(hHeap, ServiceInstanceNameW);
            if (ServiceInstanceNameA == NULL)
            {
                result = WSAEFAULT;
                goto End;

            }
            ServiceProtocolNameA = StrW2AHeapAlloc(hHeap, hostinfo.servprotoW);
            if (ServiceProtocolNameA == NULL)
            {
                result = WSAEFAULT;
                goto End;

            }
        }
    }

    if ((LUP_RETURN_ADDR & data->dwControlFlags) != 0)
    {
        if (hostinfo.addressCount
            ? !mswBufferAppendAddr_CsAddrs(&buf, lpRes, hostinfo.addresses, hostinfo.addressCount)
            : !mswBufferAppendAddr_AddrInfoW(&buf, lpRes, hostinfo.addr4))
        {
            *lpResLen = buf.bytesUsed;
            result = WSAEFAULT;
            goto End;
        }
    }

    if ((LUP_RETURN_BLOB & data->dwControlFlags) != 0)
    {
        if (data->CallID == NSP_CALLID_HOSTBYNAME)
        {
            /* Write data for PBLOB (hostent) */
            if (!mswBufferAppendBlob_HostentList(&buf,
                                                 lpRes,
                                                 (LUP_RETURN_ALIASES & data->dwControlFlags) != 0 ? hostinfo.servaliasesA : NULL,
                                                 ServiceInstanceNameA,
                                                 hostinfo.addresses,
                                                 hostinfo.addressCount))
            {
                *lpResLen = buf.bytesUsed;
                result = WSAEFAULT;
                goto End;
            }
        }
        else if (data->CallID == NSP_CALLID_SERVICEBYNAME)
        {
            /* Write data for PBLOB (servent) */
            if (!mswBufferAppendBlob_Servent(&buf,
                                             lpRes,
                                             ServiceInstanceNameA,/* ServiceName */
                                             hostinfo.servaliasesA,
                                             ServiceProtocolNameA,
                                             hostinfo.servport))
            {
                *lpResLen = buf.bytesUsed;
                result = WSAEFAULT;
                goto End;
            }
        }
        else
        {
            result = WSANO_RECOVERY;
            goto End;
        }
    }

    if ((LUP_RETURN_NAME & data->dwControlFlags) != 0)
    {
        /* HostByName sets the ServiceInstanceName to a
           (UNICODE)copy of hostent.h_name */
        lpRes->lpszServiceInstanceName = (LPWSTR)mswBufferEndPtr(&buf);
        if (!mswBufferAppendStrW(&buf, ServiceInstanceNameW))
        {
            lpRes->lpszServiceInstanceName = NULL;
            *lpResLen = buf.bytesUsed;
            result = WSAEFAULT;
            goto End;
        }
    }

    *lpResLen = buf.bytesUsed;

    result = ERROR_SUCCESS;
End:
    if (result == WSAEFAULT && data->CallIDCounter)
        data->CallIDCounter--;

    /* cleanup */
    if (ServiceInstanceNameA != NULL)
        HeapFree(hHeap, 0, ServiceInstanceNameA);

    if (ServiceProtocolNameA != NULL)
        HeapFree(hHeap, 0, ServiceProtocolNameA);

    if (hostinfo.hostnameW != NULL)
        HeapFree(hHeap, 0, hostinfo.hostnameW);

    if (hostinfo.servnameW != NULL)
        HeapFree(hHeap, 0, hostinfo.servnameW);

    if (hostinfo.servprotoW != NULL)
        HeapFree(hHeap, 0, hostinfo.servprotoW);

    if (hostinfo.addresses != NULL)
        HeapFree(hHeap, 0, hostinfo.addresses);

    return result;
}

/* Implementations - Exports */
/*
 * @implemented
 */
int
WINAPI
NSPStartup(_In_ LPGUID lpProviderId,
           _Out_ LPNSP_ROUTINE lpRout)
{
    INT ret;

    if ((lpRout == NULL) ||
        (lpRout->cbSize != sizeof(NSP_ROUTINE)))
    {
        WSASetLastError(ERROR_INVALID_PARAMETER);
        return ERROR_INVALID_PARAMETER;
    }

    mwsNSPInit();

    /* set own Provider GUID - maybe we need
       here to set the original mswsock-GUID?! */

    /* Win2k3 returns
       - Version 1.1
       - no NSPIoctl
       - sets cbSize to 44! */
    lpRout->dwMajorVersion = 1;
    lpRout->dwMinorVersion = 1;
    lpRout->cbSize = sizeof(*lpRout) - sizeof(lpRout->NSPIoctl);
    lpRout->NSPCleanup = &mwsNSPCleanUp;
    lpRout->NSPLookupServiceBegin = &mwsNSPLookupServiceBegin;
    lpRout->NSPLookupServiceNext = &mwsNSPLookupServiceNext;
    lpRout->NSPLookupServiceEnd = &mwsNSPLookupServiceEnd;
    lpRout->NSPSetService = &mwsNSPSetService;
    lpRout->NSPInstallServiceClass = &mwsNSPInstallServiceClass;
    lpRout->NSPRemoveServiceClass = &mwsNSPRemoveServiceClass;
    lpRout->NSPGetServiceClassInfo = &mwsNSPGetServiceClassInfo;
    lpRout->NSPIoctl = NULL;// &mwsNSPIoCtl;

    ret = NO_ERROR;

    return ret;
}
