/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS system libraries
 * FILE:        lib/dnsapi/dnsapi/query.c
 * PURPOSE:     DNSAPI functions built on the ADNS library.
 * PROGRAMER:   Art Yerkes
 * UPDATE HISTORY:
 *              12/15/03 -- Created
 */

#include "precomp.h"
#include <winreg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <strsafe.h>

#define NDEBUG
#include <debug.h>

static
BOOL
ParseIpv4Address(
    _In_ PCWSTR AddressString,
    _Out_ PIN_ADDR pAddress)
{
    PCWSTR pTerminator = NULL;
    NTSTATUS Status;

    Status = RtlIpv4StringToAddressW(AddressString,
                                     TRUE,
                                     &pTerminator,
                                     pAddress);
    if (NT_SUCCESS(Status) && pTerminator != NULL && *pTerminator == L'\0')
        return TRUE;

    return FALSE;
}


static
BOOL
ParseIpv6Address(
    _In_ PCWSTR AddressString,
    _Out_ PIN6_ADDR pAddress)
{
    PCWSTR pTerminator = NULL;
    NTSTATUS Status;

    Status = RtlIpv6StringToAddressW(AddressString,
                                     &pTerminator,
                                     pAddress);
    if (NT_SUCCESS(Status) && pTerminator != NULL && *pTerminator == L'\0')
        return TRUE;

    return FALSE;
}


static
PDNS_RECORDW
CreateRecordForIpAddress(
    _In_ PCWSTR Name,
    _In_ WORD Type)
{
    IN_ADDR Ip4Address;
    IN6_ADDR Ip6Address;
    PDNS_RECORDW pRecord = NULL;

    if (Type == DNS_TYPE_A)
    {
        if (ParseIpv4Address(Name, &Ip4Address))
        {
            pRecord = RtlAllocateHeap(RtlGetProcessHeap(),
                                      HEAP_ZERO_MEMORY,
                                      sizeof(DNS_RECORDW));
            if (pRecord == NULL)
                return NULL;

            pRecord->pName = RtlAllocateHeap(RtlGetProcessHeap(),
                                             0,
                                             (wcslen(Name) + 1) * sizeof(WCHAR));
            if (pRecord == NULL)
            {
                RtlFreeHeap(RtlGetProcessHeap(), 0, pRecord);
                return NULL;
            }

            wcscpy(pRecord->pName, Name);
            pRecord->wType = DNS_TYPE_A;
            pRecord->wDataLength = sizeof(DNS_A_DATA);
            pRecord->Flags.S.Section = DnsSectionQuestion;
            pRecord->Flags.S.CharSet = DnsCharSetUnicode;
            pRecord->dwTtl = 7 * 24 * 60 * 60;

            pRecord->Data.A.IpAddress = Ip4Address.S_un.S_addr;

            return pRecord;
        }
    }
    else if (Type == DNS_TYPE_AAAA)
    {
        if (ParseIpv6Address(Name, &Ip6Address))
        {
            pRecord = RtlAllocateHeap(RtlGetProcessHeap(),
                                      HEAP_ZERO_MEMORY,
                                      sizeof(DNS_RECORDW));
            if (pRecord == NULL)
                return NULL;

            pRecord->pName = RtlAllocateHeap(RtlGetProcessHeap(),
                                             0,
                                             (wcslen(Name) + 1) * sizeof(WCHAR));
            if (pRecord == NULL)
            {
                RtlFreeHeap(RtlGetProcessHeap(), 0, pRecord);
                return NULL;
            }

            wcscpy(pRecord->pName, Name);
            pRecord->wType = DNS_TYPE_AAAA;
            pRecord->wDataLength = sizeof(DNS_AAAA_DATA);
            pRecord->Flags.S.Section = DnsSectionQuestion;
            pRecord->Flags.S.CharSet = DnsCharSetUnicode;
            pRecord->dwTtl = 7 * 24 * 60 * 60;

            CopyMemory(&pRecord->Data.AAAA.Ip6Address,
                       &Ip6Address.u.Byte,
                       sizeof(IN6_ADDR));

            return pRecord;
        }
    }

    return NULL;
}


/* DnsQuery ****************************
 * Begin a DNS query, and allow the result to be placed in the application
 * supplied result pointer.  The result can be manipulated with the record
 * functions.
 *
 * Name                 -- The DNS object to be queried.
 * Type                 -- The type of records to be returned.  These are
 *                          listed in windns.h
 * Options              -- Query options.  DNS_QUERY_STANDARD is the base
 *                          state, and every other option takes precedence.
 *                          multiple options can be combined.  Listed in
 *                          windns.h
 * Servers              -- List of alternate servers (optional)
 * QueryResultSet       -- Pointer to the result pointer that will be filled
 *                          when the response is available.
 * Reserved             -- Response as it appears on the wire.  Optional.
 */


static PWCHAR
DnsCToW(const CHAR *NarrowString)
{
    PWCHAR WideString;
    int WideLen = MultiByteToWideChar(CP_ACP,
                                      0,
                                      NarrowString,
                                      -1,
                                      NULL,
                                      0);
    if (WideLen == 0)
        return NULL;
    WideString = RtlAllocateHeap(RtlGetProcessHeap(), 0, WideLen * sizeof(WCHAR));
    if (WideString == NULL)
    {
        return NULL;
    }
    MultiByteToWideChar(CP_ACP,
                        0,
                        NarrowString,
                        -1,
                        WideString,
                        WideLen);

    return WideString;
}


static PWCHAR
DnsUTF8ToW(const CHAR *NarrowString)
{
    PWCHAR WideString;
    int WideLen = MultiByteToWideChar(CP_UTF8,
                                      0,
                                      NarrowString,
                                      -1,
                                      NULL,
                                      0);
    if (WideLen == 0)
        return NULL;
    WideString = RtlAllocateHeap(RtlGetProcessHeap(), 0, WideLen * sizeof(WCHAR));
    if (WideString == NULL)
    {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8,
                        0,
                        NarrowString,
                        -1,
                        WideString,
                        WideLen);

    return WideString;
}

DNS_STATUS WINAPI
DnsQuery_CodePage(UINT CodePage,
           LPCSTR Name,
           WORD Type,
           DWORD Options,
           PVOID Extra,
           PDNS_RECORD *QueryResultSet,
           PVOID *Reserved)
{
    PWCHAR Buffer;
    DNS_STATUS Status;
    PDNS_RECORD QueryResultWide = NULL;

    if (Name == NULL)
        return ERROR_INVALID_PARAMETER;
    if (QueryResultSet == NULL)
        return ERROR_INVALID_PARAMETER;

    switch (CodePage)
    {
    case CP_ACP:
        Buffer = DnsCToW(Name);
        break;

    case CP_UTF8:
        Buffer = DnsUTF8ToW(Name);
        break;

    default:
        return ERROR_INVALID_PARAMETER;
    }

    Status = DnsQuery_W(Buffer, Type, Options, Extra, &QueryResultWide, Reserved);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);

    *QueryResultSet = NULL;
    if (Status == ERROR_SUCCESS && QueryResultWide)
    {
        *QueryResultSet = DnsRecordSetCopyEx(QueryResultWide, DnsCharSetUnicode,
                                             CodePage == CP_ACP ? DnsCharSetAnsi : DnsCharSetUtf8);
        DnsRecordListFree(QueryResultWide, DnsFreeRecordList);
        if (!*QueryResultSet)
            Status = ERROR_OUTOFMEMORY;
    }

    return Status;
}

DNS_STATUS WINAPI
DnsQuery_A(LPCSTR Name,
           WORD Type,
           DWORD Options,
           PVOID Extra,
           PDNS_RECORD *QueryResultSet,
           PVOID *Reserved)
{
    return DnsQuery_CodePage(CP_ACP, Name, Type, Options, Extra, QueryResultSet, Reserved);
}

DNS_STATUS WINAPI
DnsQuery_UTF8(LPCSTR Name,
              WORD Type,
              DWORD Options,
              PVOID Extra,
              PDNS_RECORD *QueryResultSet,
              PVOID *Reserved)
{
    return DnsQuery_CodePage(CP_UTF8, Name, Type, Options, Extra, QueryResultSet, Reserved);
}

static
BOOL
IsResolverMarshalledType(WORD Type)
{
    switch (Type)
    {
    case DNS_TYPE_A:
    case DNS_TYPE_NS:
    case DNS_TYPE_MD:
    case DNS_TYPE_MF:
    case DNS_TYPE_CNAME:
    case DNS_TYPE_SOA:
    case DNS_TYPE_MB:
    case DNS_TYPE_MG:
    case DNS_TYPE_MR:
    case DNS_TYPE_PTR:
    case DNS_TYPE_MINFO:
    case DNS_TYPE_MX:
    case DNS_TYPE_RP:
    case DNS_TYPE_AFSDB:
    case DNS_TYPE_RT:
    case DNS_TYPE_AAAA:
    case DNS_TYPE_SRV:
        return TRUE;
    default:
        return FALSE;
    }
}

DNS_STATUS
WINAPI
DnsQuery_W(LPCWSTR Name,
           WORD Type,
           DWORD Options,
           PVOID Extra,
           PDNS_RECORD *QueryResultSet,
           PVOID *Reserved)
{
    DWORD dwRecords = 0;
    PDNS_RECORDW pRecord = NULL;
    size_t NameLen, i;
    DNS_STATUS Status = ERROR_SUCCESS;

    DPRINT("DnsQuery_W()\n");

    if ((Name == NULL) ||
        (QueryResultSet == NULL))
        return ERROR_INVALID_PARAMETER;

    *QueryResultSet = NULL;

    /* Create an A or AAAA record for an IP4 or IP6 address */
    pRecord = CreateRecordForIpAddress(Name,
                                       Type);
    if (pRecord != NULL)
    {
        *QueryResultSet = (PDNS_RECORD)pRecord;
        return ERROR_SUCCESS;
    }

    /*
     * Check allowed characters
     * According to RFC a-z,A-Z,0-9,-,_, but can't start or end with - or _
     */
    NameLen = wcslen(Name);
    if (NameLen &&
        (Name[0] == L'-' || Name[0] == L'_' || Name[NameLen - 1] == L'-' ||
         Name[NameLen - 1] == L'_' || wcsstr(Name, L"..") != NULL))
    {
        return ERROR_INVALID_NAME;
    }

    i = 0;
    while (i < NameLen)
    {
        if (!((Name[i] >= L'a' && Name[i] <= L'z') ||
              (Name[i] >= L'A' && Name[i] <= L'Z') ||
              (Name[i] >= L'0' && Name[i] <= L'9') ||
              Name[i] == L'-' || Name[i] == L'_' || Name[i] == L'.'))
        {
            return DNS_ERROR_INVALID_NAME_CHAR;
        }

        i++;
    }

    if (!IsResolverMarshalledType(Type))
        return Query_Main(Name, Type, Options, QueryResultSet);

    RpcTryExcept
    {
        Status = R_ResolverQuery(NULL,
                                 Name,
                                 Type,
                                 Options,
                                 &dwRecords,
                                 (DNS_RECORDW **)QueryResultSet);
        DPRINT("R_ResolverQuery() returned %lu\n", Status);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = RpcExceptionCode();
        DPRINT("Exception returned %lu\n", Status);
    }
    RpcEndExcept;

    if (Status == RPC_S_SERVER_UNAVAILABLE || Status == RPC_S_UNKNOWN_IF || Status == EPT_S_NOT_REGISTERED)
        Status = Query_Main(Name, Type, Options, QueryResultSet);

    return Status;
}

WCHAR
*xstrsave(const WCHAR *str)
{
    WCHAR *p;
    size_t len = 0;

    /* FIXME: how much instead of MAX_PATH? */
    StringCbLengthW(str, MAX_PATH, &len);
    len+=sizeof(WCHAR);

    p = RtlAllocateHeap(RtlGetProcessHeap(), 0, len);

    if (p)
        StringCbCopyW(p, len, str);

    return p;
}

CHAR
*xstrsaveA(const CHAR *str)
{
    CHAR *p;
    size_t len = 0;

    /* FIXME: how much instead of MAX_PATH? */
    StringCbLengthA(str, MAX_PATH, &len);
    len++;

    p = RtlAllocateHeap(RtlGetProcessHeap(), 0, len);

    if (p)
        StringCbCopyA(p, len, str);

    return p;
}


static
BOOL
IsLocalHostName(LPCWSTR Name)
{
    WCHAR Host[MAX_HOSTNAME_LEN + 1], Domain[MAX_DOMAIN_NAME_LEN + 1], Full[MAX_HOSTNAME_LEN + MAX_DOMAIN_NAME_LEN + 2];
    PFIXED_INFO Info;
    ULONG Size = 0;
    SIZE_T Length = wcslen(Name);
    WCHAR Query[DNS_MAX_NAME_BUFFER_LENGTH];
    BOOL Found = FALSE;

    if (!Length || Length >= ARRAYSIZE(Query))
        return FALSE;
    StringCchCopyW(Query, ARRAYSIZE(Query), Name);
    if (Query[Length - 1] == L'.')
        Query[Length - 1] = UNICODE_NULL;

    if (GetNetworkParams(NULL, &Size) != ERROR_BUFFER_OVERFLOW)
        return FALSE;
    Info = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
    if (!Info)
        return FALSE;
    if (GetNetworkParams(Info, &Size) == ERROR_SUCCESS)
    {
        MultiByteToWideChar(CP_ACP, 0, Info->HostName, -1, Host, ARRAYSIZE(Host));
        MultiByteToWideChar(CP_ACP, 0, Info->DomainName, -1, Domain, ARRAYSIZE(Domain));
        Found = !_wcsicmp(Query, Host);
        if (!Found && Domain[0] &&
            SUCCEEDED(StringCchPrintfW(Full, ARRAYSIZE(Full), L"%s.%s", Host, Domain)))
            Found = !_wcsicmp(Query, Full);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
    return Found;
}

static
BOOL
AppendAddressRecord(LPCWSTR Name, WORD Type, const VOID *Address, PDNS_RECORD **Tail, DWORD Ttl, DWORD Section)
{
    PDNS_RECORD Record = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DNS_RECORD));

    if (!Record)
        return FALSE;
    Record->pName = (LPSTR)xstrsave(Name);
    if (!Record->pName)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Record);
        return FALSE;
    }
    Record->wType = Type;
    Record->Flags.S.Section = Section;
    Record->Flags.S.CharSet = DnsCharSetUnicode;
    Record->dwTtl = Ttl;
    if (Type == DNS_TYPE_A)
    {
        Record->wDataLength = sizeof(DNS_A_DATA);
        CopyMemory(&Record->Data.A.IpAddress, Address, sizeof(IP4_ADDRESS));
    }
    else
    {
        Record->wDataLength = sizeof(DNS_AAAA_DATA);
        CopyMemory(&Record->Data.AAAA.Ip6Address, Address, sizeof(IP6_ADDRESS));
    }
    **Tail = Record;
    *Tail = &Record->pNext;
    return TRUE;
}

static
DNS_STATUS
QueryLocalName(LPCWSTR Name, WORD Type, PDNS_RECORD *Result)
{
    static const IN6_ADDR Loopback6 = {{IN6ADDR_LOOPBACK_INIT}};
    PDNS_RECORD *Tail = Result;
    PIP_ADAPTER_ADDRESSES List, Adapter;
    ULONG Size = 16384;
    DWORD Error;
    IP4_ADDRESS Loopback4 = htonl(INADDR_LOOPBACK);

    if (!Name[0])
    {
        WCHAR Host[MAX_HOSTNAME_LEN + 1];
        DWORD HostLength = ARRAYSIZE(Host);

        /* FIXME: the AAAA address and the other record types of the empty name are not measured on Windows */
        if (Type != DNS_TYPE_A && Type != DNS_TYPE_AAAA)
            return DNS_INFO_NO_RECORDS;
        if (!GetComputerNameExW(ComputerNameDnsHostname, Host, &HostLength))
            return GetLastError();
        if (Type == DNS_TYPE_A)
            return AppendAddressRecord(Host, Type, &Loopback4, &Tail, 1200, DnsSectionQuestion) ? ERROR_SUCCESS : ERROR_OUTOFMEMORY;
        return AppendAddressRecord(Host, Type, &Loopback6, &Tail, 1200, DnsSectionQuestion) ? ERROR_SUCCESS : ERROR_OUTOFMEMORY;
    }

    if (!_wcsicmp(Name, L"localhost") || !_wcsicmp(Name, L"localhost."))
    {
        if (Type == DNS_TYPE_A)
            return AppendAddressRecord(Name, Type, &Loopback4, &Tail, 1200, DnsSectionQuestion) ? ERROR_SUCCESS : ERROR_OUTOFMEMORY;
        if (Type == DNS_TYPE_AAAA)
            return AppendAddressRecord(Name, Type, &Loopback6, &Tail, 1200, DnsSectionQuestion) ? ERROR_SUCCESS : ERROR_OUTOFMEMORY;
        return DNS_INFO_NO_RECORDS;
    }

    if ((Type != DNS_TYPE_A && Type != DNS_TYPE_AAAA) || !IsLocalHostName(Name))
        return ERROR_FILE_NOT_FOUND;

    for (;;)
    {
        List = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!List)
            return ERROR_OUTOFMEMORY;
        Error = GetAdaptersAddresses(Type == DNS_TYPE_A ? AF_INET : AF_INET6,
                                     GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_DNS_SERVER |
                                     GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                                     NULL, List, &Size);
        if (Error != ERROR_BUFFER_OVERFLOW)
            break;
        RtlFreeHeap(RtlGetProcessHeap(), 0, List);
    }

    if (Error == ERROR_SUCCESS)
    {
        for (Adapter = List; Adapter; Adapter = Adapter->Next)
        {
            PIP_ADAPTER_UNICAST_ADDRESS Unicast;

            if (Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || Adapter->OperStatus != IfOperStatusUp)
                continue;
            for (Unicast = Adapter->FirstUnicastAddress; Unicast; Unicast = Unicast->Next)
            {
                const SOCKADDR *Address = Unicast->Address.lpSockaddr;

                if (Unicast->DadState != IpDadStatePreferred && Unicast->DadState != IpDadStateDeprecated)
                    continue;
                if (Type == DNS_TYPE_A && Address->sa_family == AF_INET)
                    AppendAddressRecord(Name, Type, &((const SOCKADDR_IN *)Address)->sin_addr, &Tail, 0, DnsSectionAnswer);
                else if (Type == DNS_TYPE_AAAA && Address->sa_family == AF_INET6)
                    AppendAddressRecord(Name, Type, &((const SOCKADDR_IN6 *)Address)->sin6_addr, &Tail, 0, DnsSectionAnswer);
            }
        }
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, List);

    if (!*Result)
    {
        if (Type == DNS_TYPE_A)
            return AppendAddressRecord(Name, Type, &Loopback4, &Tail, 0, DnsSectionAnswer) ? ERROR_SUCCESS : ERROR_OUTOFMEMORY;
        return AppendAddressRecord(Name, Type, &Loopback6, &Tail, 0, DnsSectionAnswer) ? ERROR_SUCCESS : ERROR_OUTOFMEMORY;
    }
    return ERROR_SUCCESS;
}

DNS_STATUS
WINAPI
Query_Main(LPCWSTR Name,
           WORD Type,
           DWORD Options,
           PDNS_RECORD *QueryResultSet)
{
    DNS_STATUS Status;

    if (Name == NULL || QueryResultSet == NULL)
        return ERROR_INVALID_PARAMETER;

    *QueryResultSet = NULL;

    Status = QueryLocalName(Name, Type, QueryResultSet);
    if (Status != ERROR_FILE_NOT_FOUND)
        return Status;

    return DnsIntResolve(Name, Type, Options, (PDNS_RECORDW *)QueryResultSet);
}

void
DnsIntFreeRecordList(PDNS_RECORD ToDelete)
{
    UINT i;
    PDNS_RECORD next = 0;

    while(ToDelete)
    {
        if(ToDelete->pName)
            RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->pName);

        switch(ToDelete->wType)
        {
            case DNS_TYPE_CNAME:
            case DNS_TYPE_PTR:
            case DNS_TYPE_NS:
            case DNS_TYPE_MB:
            case DNS_TYPE_MD:
            case DNS_TYPE_MF:
            case DNS_TYPE_MG:
            case DNS_TYPE_MR:
                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.PTR.pNameHost);
                break;

            case DNS_TYPE_MX:
                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.MX.pNameExchange);
                break;

            case DNS_TYPE_MINFO:
                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.MINFO.pNameMailbox);
                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.MINFO.pNameErrorsMailbox);
                break;

            case DNS_TYPE_SOA:
                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.SOA.pNamePrimaryServer);
                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.SOA.pNameAdministrator);
                break;

            case DNS_TYPE_SRV:
                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.SRV.pNameTarget);
                break;

            case DNS_TYPE_HINFO:
                for(i = 0; i < ToDelete->Data.TXT.dwStringCount; i++)
                    RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.TXT.pStringArray[i]);

                RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.TXT.pStringArray);
                break;

            case DNS_TYPE_TEXT:
                for(i = 0; i < ToDelete->Data.TXT.dwStringCount; i++)
                    RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete->Data.TXT.pStringArray[i]);
                break;
        }

        next = ToDelete->pNext;
        RtlFreeHeap(RtlGetProcessHeap(), 0, ToDelete);
        ToDelete = next;
    }
}

BOOL
WINAPI
DnsFlushResolverCache(VOID)
{
    DNS_STATUS Status = ERROR_SUCCESS;

    DPRINT("DnsFlushResolverCache()\n");

    RpcTryExcept
    {
        Status = R_ResolverFlushCache(NULL);
        DPRINT("R_ResolverFlushCache() returned %lu\n", Status);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = RpcExceptionCode();
        DPRINT("Exception returned %lu\n", Status);
    }
    RpcEndExcept;

    return (Status == ERROR_SUCCESS);
}


BOOL
WINAPI
DnsFlushResolverCacheEntry_A(
    _In_ LPCSTR pszEntry)
{
    DNS_STATUS Status = ERROR_SUCCESS;
    LPWSTR pszUnicodeEntry;

    DPRINT1("DnsFlushResolverCacheEntry_A(%s)\n", pszEntry);

    if (pszEntry == NULL)
        return FALSE;

    pszUnicodeEntry = DnsCToW(pszEntry);
    if (pszUnicodeEntry == NULL)
        return FALSE;

    RpcTryExcept
    {
        Status = R_ResolverFlushCacheEntry(NULL, pszUnicodeEntry, DNS_TYPE_ANY);
        DPRINT("R_ResolverFlushCacheEntry() returned %lu\n", Status);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = RpcExceptionCode();
        DPRINT("Exception returned %lu\n", Status);
    }
    RpcEndExcept;

    RtlFreeHeap(RtlGetProcessHeap(), 0, pszUnicodeEntry);

    return (Status == ERROR_SUCCESS);
}


BOOL
WINAPI
DnsFlushResolverCacheEntry_UTF8(
    _In_ LPCSTR pszEntry)
{
    DNS_STATUS Status = ERROR_SUCCESS;
    LPWSTR pszUnicodeEntry;

    DPRINT1("DnsFlushResolverCacheEntry_UTF8(%s)\n", pszEntry);

    if (pszEntry == NULL)
        return FALSE;

    pszUnicodeEntry = DnsCToW(pszEntry);
    if (pszUnicodeEntry == NULL)
        return FALSE;

    RpcTryExcept
    {
        Status = R_ResolverFlushCacheEntry(NULL, pszUnicodeEntry, DNS_TYPE_ANY);
        DPRINT("R_ResolverFlushCacheEntry() returned %lu\n", Status);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = RpcExceptionCode();
        DPRINT("Exception returned %lu\n", Status);
    }
    RpcEndExcept;

    RtlFreeHeap(RtlGetProcessHeap(), 0, pszUnicodeEntry);

    return (Status == ERROR_SUCCESS);
}


BOOL
WINAPI
DnsFlushResolverCacheEntry_W(
    _In_ LPCWSTR pszEntry)
{
    DNS_STATUS Status = ERROR_SUCCESS;

    DPRINT1("DnsFlushResolverCacheEntry_W(%S)\n", pszEntry);

    if (pszEntry == NULL)
        return FALSE;

    RpcTryExcept
    {
        Status = R_ResolverFlushCacheEntry(NULL, pszEntry, DNS_TYPE_ANY);
        DPRINT("R_ResolverFlushCacheEntry() returned %lu\n", Status);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = RpcExceptionCode();
        DPRINT("Exception returned %lu\n", Status);
    }
    RpcEndExcept;

    return (Status == ERROR_SUCCESS);
}


BOOL
WINAPI
DnsGetCacheDataTable(
    _Out_ PDNS_CACHE_ENTRY *DnsCache)
{
    DNS_STATUS Status = ERROR_SUCCESS;
    PDNS_CACHE_ENTRY CacheEntries = NULL;

    if (DnsCache == NULL)
        return FALSE;

    RpcTryExcept
    {
        Status = CRrReadCache(NULL,
                              &CacheEntries);
        DPRINT("CRrReadCache() returned %lu\n", Status);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = RpcExceptionCode();
        DPRINT1("Exception returned %lu\n", Status);
    }
    RpcEndExcept;

    if (Status != ERROR_SUCCESS)
        return FALSE;

    if (CacheEntries == NULL)
        return FALSE;

    *DnsCache = CacheEntries;

    return TRUE;
}

DWORD
WINAPI
GetCurrentTimeInSeconds(VOID)
{
    FILETIME Time;
    FILETIME Adjustment;
    ULARGE_INTEGER lTime, lAdj;
    SYSTEMTIME st = {1970, 1, 0, 1, 0, 0, 0};

    SystemTimeToFileTime(&st, &Adjustment);
    memcpy(&lAdj, &Adjustment, sizeof(lAdj));
    GetSystemTimeAsFileTime(&Time);
    memcpy(&lTime, &Time, sizeof(lTime));
    lTime.QuadPart -= lAdj.QuadPart;
    return (DWORD)(lTime.QuadPart/10000000ULL);
}
