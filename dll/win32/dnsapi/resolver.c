/*
 * PROJECT:     LiberNT DNS API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DNS stub resolver over IPv4 and IPv6 servers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

#include <winreg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <strsafe.h>

#define NDEBUG
#include <debug.h>

#include "resolver_core.h"

#define DNS_PORT 53
#define LLMNR_PORT 5355
#define LLMNR_RESEND 420
#define LLMNR_ROUNDS 2
#define DNS_MAX_ADAPTERS 16
#define DNS_MAX_SERVERS 32
#define DNS_MAX_CANDIDATES 8
#define DNS_UDP_SIZE 1232
#define DNS_TCP_SIZE 65535
#define DNS_TCP_TIMEOUT 4000

typedef struct _RESOLVER_SERVER
{
    SOCKADDR_INET Address;
    ULONG Adapter;
    ULONG Rank;
    BOOL Sent;
    BOOL Answered;
    BOOL Failed;
} RESOLVER_SERVER, *PRESOLVER_SERVER;

typedef struct _RESOLVER_PRIORITY
{
    SOCKADDR_INET Address;
    LONG Priority;
} RESOLVER_PRIORITY, *PRESOLVER_PRIORITY;

typedef struct _RESOLVER_ADAPTER
{
    ULONG Metric;
    ULONG FirstServer;
    ULONG ServerCount;
    BOOL Negative;
    WCHAR Suffix[MAX_DNS_SUFFIX_STRING_LENGTH];
    WCHAR SearchList[1024];
} RESOLVER_ADAPTER, *PRESOLVER_ADAPTER;

typedef struct _RESOLVER_CONFIG
{
    RESOLVER_ADAPTER Adapters[DNS_MAX_ADAPTERS];
    ULONG AdapterCount;
    RESOLVER_SERVER Servers[DNS_MAX_SERVERS];
    ULONG ServerCount;
    WCHAR Candidates[DNS_MAX_CANDIDATES][DNS_MAX_NAME_BUFFER_LENGTH];
    ULONG CandidateCount;
} RESOLVER_CONFIG, *PRESOLVER_CONFIG;

typedef struct _RESOLVER_REPLY
{
    DNS_STATUS Status;
    BOOL Final;
    PDNS_RECORDW Records;
} RESOLVER_REPLY, *PRESOLVER_REPLY;

static LONG ResolverWinsockStarted;
static RTL_SRWLOCK ResolverPriorityLock = RTL_SRWLOCK_INIT;
static RESOLVER_PRIORITY ResolverPriorities[DNS_MAX_SERVERS];
static ULONG ResolverPriorityCount;
static ULONGLONG ResolverPriorityReset;

static
VOID
ResolverStartWinsock(VOID)
{
    WSADATA Data;

    if (InterlockedCompareExchange(&ResolverWinsockStarted, 1, 0) == 0)
        WSAStartup(MAKEWORD(2, 2), &Data);
}

static
int __cdecl
ResolverCompareAdapters(const void *Left, const void *Right)
{
    const RESOLVER_ADAPTER *A = Left, *B = Right;

    return A->Metric < B->Metric ? -1 : A->Metric > B->Metric;
}

static
VOID
ResolverAddCandidate(PRESOLVER_CONFIG Config, PCWSTR Name, PCWSTR Suffix)
{
    WCHAR Buffer[DNS_MAX_NAME_BUFFER_LENGTH];
    ULONG i;

    if (Config->CandidateCount == DNS_MAX_CANDIDATES)
        return;
    if (Suffix && *Suffix)
    {
        if (FAILED(StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%s.%s", Name, Suffix)))
            return;
    }
    else if (FAILED(StringCchCopyW(Buffer, ARRAYSIZE(Buffer), Name)))
    {
        return;
    }

    for (i = 0; i < Config->CandidateCount; i++)
    {
        if (!_wcsicmp(Config->Candidates[i], Buffer))
            return;
    }
    StringCchCopyW(Config->Candidates[Config->CandidateCount++], DNS_MAX_NAME_BUFFER_LENGTH, Buffer);
}

static
VOID
ResolverReadRegistryString(PCWSTR Value, PWSTR Buffer, DWORD Count)
{
    DWORD Size = (Count - 1) * sizeof(WCHAR), Type;
    HKEY Key;

    Buffer[0] = UNICODE_NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                      0, KEY_READ, &Key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(Key, Value, NULL, &Type, (PBYTE)Buffer, &Size) == ERROR_SUCCESS && Type == REG_SZ)
        Buffer[Size / sizeof(WCHAR)] = UNICODE_NULL;
    else
        Buffer[0] = UNICODE_NULL;
    RegCloseKey(Key);
}

static
DNS_STATUS
ResolverLoadConfig(PRESOLVER_CONFIG Config, PCWSTR Name)
{
    ULONG Size = 16384, i;
    PIP_ADAPTER_ADDRESSES List, Adapter;
    DWORD Error;
    WCHAR Domain[MAX_DNS_SUFFIX_STRING_LENGTH], SearchList[1024];
    SIZE_T Length = wcslen(Name);
    BOOL Absolute = Length && Name[Length - 1] == L'.';
    BOOL SingleLabel = !wcschr(Name, L'.');

    RtlZeroMemory(Config, sizeof(*Config));

    for (;;)
    {
        List = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!List)
            return ERROR_OUTOFMEMORY;
        Error = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME,
                                     NULL, List, &Size);
        if (Error != ERROR_BUFFER_OVERFLOW)
            break;
        RtlFreeHeap(RtlGetProcessHeap(), 0, List);
    }
    if (Error != ERROR_SUCCESS)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, List);
        return Error == ERROR_NO_DATA ? DNS_ERROR_NO_DNS_SERVERS : Error;
    }

    for (Adapter = List; Adapter && Config->AdapterCount < DNS_MAX_ADAPTERS; Adapter = Adapter->Next)
    {
        PIP_ADAPTER_DNS_SERVER_ADDRESS Server;
        PIP_ADAPTER_DNS_SUFFIX Suffix;
        PRESOLVER_ADAPTER Entry;

        if (Adapter->OperStatus != IfOperStatusUp || Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            !Adapter->FirstDnsServerAddress)
            continue;

        Entry = &Config->Adapters[Config->AdapterCount];
        Entry->Metric = Adapter->Ipv4Metric;
        if (Adapter->Ipv6Metric && (!Entry->Metric || Adapter->Ipv6Metric < Entry->Metric))
            Entry->Metric = Adapter->Ipv6Metric;
        if (Adapter->DnsSuffix)
            StringCchCopyW(Entry->Suffix, ARRAYSIZE(Entry->Suffix), Adapter->DnsSuffix);
        for (Suffix = Adapter->FirstDnsSuffix; Suffix; Suffix = Suffix->Next)
        {
            if (Entry->SearchList[0])
                StringCchCatW(Entry->SearchList, ARRAYSIZE(Entry->SearchList), L",");
            StringCchCatW(Entry->SearchList, ARRAYSIZE(Entry->SearchList), Suffix->String);
        }

        Entry->FirstServer = Config->ServerCount;
        for (Server = Adapter->FirstDnsServerAddress; Server && Config->ServerCount < DNS_MAX_SERVERS; Server = Server->Next)
        {
            PRESOLVER_SERVER Slot = &Config->Servers[Config->ServerCount];
            INT Length = Server->Address.iSockaddrLength;

            if (!Server->Address.lpSockaddr || Length > (INT)sizeof(Slot->Address))
                continue;
            if (Server->Address.lpSockaddr->sa_family != AF_INET && Server->Address.lpSockaddr->sa_family != AF_INET6)
                continue;
            RtlZeroMemory(Slot, sizeof(*Slot));
            RtlCopyMemory(&Slot->Address, Server->Address.lpSockaddr, Length);
            if (Slot->Address.si_family == AF_INET6)
            {
                Slot->Address.Ipv6.sin6_port = htons(DNS_PORT);
                if (IN6_IS_ADDR_LINKLOCAL(&Slot->Address.Ipv6.sin6_addr) && !Slot->Address.Ipv6.sin6_scope_id)
                    Slot->Address.Ipv6.sin6_scope_id = Adapter->Ipv6IfIndex;
            }
            else
            {
                Slot->Address.Ipv4.sin_port = htons(DNS_PORT);
            }
            Slot->Adapter = Config->AdapterCount;
            Config->ServerCount++;
            Entry->ServerCount++;
        }
        if (Entry->ServerCount)
            Config->AdapterCount++;
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, List);

    if (!Config->AdapterCount)
        return DNS_ERROR_NO_DNS_SERVERS;

    qsort(Config->Adapters, Config->AdapterCount, sizeof(Config->Adapters[0]), ResolverCompareAdapters);
    {
        RESOLVER_SERVER Saved[DNS_MAX_SERVERS];
        ULONG j;

        RtlCopyMemory(Saved, Config->Servers, sizeof(Saved));
        Config->ServerCount = 0;
        for (i = 0; i < Config->AdapterCount; i++)
        {
            ULONG First = Config->Adapters[i].FirstServer;

            Config->Adapters[i].FirstServer = Config->ServerCount;
            for (j = 0; j < Config->Adapters[i].ServerCount; j++)
            {
                Config->Servers[Config->ServerCount] = Saved[First + j];
                Config->Servers[Config->ServerCount].Adapter = i;
                Config->ServerCount++;
            }
        }
    }

    if (Absolute)
    {
        WCHAR Trimmed[DNS_MAX_NAME_BUFFER_LENGTH];

        StringCchCopyNW(Trimmed, ARRAYSIZE(Trimmed), Name, Length - 1);
        ResolverAddCandidate(Config, Trimmed, NULL);
        return ERROR_SUCCESS;
    }

    if (!SingleLabel)
        ResolverAddCandidate(Config, Name, NULL);

    ResolverReadRegistryString(L"SearchList", SearchList, ARRAYSIZE(SearchList));
    if (SearchList[0])
    {
        PWSTR Context = NULL, Token = wcstok_s(SearchList, L", ", &Context);

        while (Token)
        {
            ResolverAddCandidate(Config, Name, Token);
            Token = wcstok_s(NULL, L", ", &Context);
        }
    }
    else
    {
        ResolverReadRegistryString(L"Domain", Domain, ARRAYSIZE(Domain));
        if (Domain[0])
            ResolverAddCandidate(Config, Name, Domain);
        for (i = 0; i < Config->AdapterCount; i++)
        {
            if (Config->Adapters[i].Suffix[0])
                ResolverAddCandidate(Config, Name, Config->Adapters[i].Suffix);
        }
        for (i = 0; i < Config->AdapterCount; i++)
        {
            PWSTR Context = NULL, Token = wcstok_s(Config->Adapters[i].SearchList, L", ", &Context);

            while (Token)
            {
                ResolverAddCandidate(Config, Name, Token);
                Token = wcstok_s(NULL, L", ", &Context);
            }
        }
    }

    if (!Config->CandidateCount && !SingleLabel)
        ResolverAddCandidate(Config, Name, NULL);
    return ERROR_SUCCESS;
}

static
INT
ResolverBuildQuery(PCWSTR Name, WORD Type, WORD Id, BOOL Recursion, PUCHAR Packet, INT Capacity)
{
    CHAR Utf8[DNS_MAX_NAME_BUFFER_LENGTH * 3];
    PCHAR Label, Next;
    INT Offset = 12;

    if (!WideCharToMultiByte(CP_UTF8, 0, Name, -1, Utf8, sizeof(Utf8), NULL, NULL))
        return -1;

    Packet[0] = (UCHAR)(Id >> 8);
    Packet[1] = (UCHAR)Id;
    Packet[2] = Recursion ? 0x01 : 0x00;
    Packet[3] = 0;
    Packet[4] = 0; Packet[5] = 1;
    Packet[6] = Packet[7] = Packet[8] = Packet[9] = Packet[10] = Packet[11] = 0;

    for (Label = Utf8; *Label; Label = Next)
    {
        SIZE_T Length;

        Next = strchr(Label, '.');
        Length = Next ? (SIZE_T)(Next - Label) : strlen(Label);
        if (!Length || Length > 63 || Offset + (INT)Length + 1 >= Capacity - 5)
            return -1;
        Packet[Offset++] = (UCHAR)Length;
        RtlCopyMemory(Packet + Offset, Label, Length);
        Offset += (INT)Length;
        if (!Next)
            break;
        Next++;
    }
    Packet[Offset++] = 0;
    Packet[Offset++] = (UCHAR)(Type >> 8);
    Packet[Offset++] = (UCHAR)Type;
    Packet[Offset++] = 0;
    Packet[Offset++] = 1;
    return Offset;
}

static
INT
ResolverReadName(const UCHAR *Packet, INT Length, INT Offset, PWSTR Name, INT Count)
{
    CHAR Utf8[DNS_MAX_NAME_BUFFER_LENGTH * 3];
    INT Out = 0, End = -1, Jumps = 0;

    for (;;)
    {
        UCHAR Label;

        if (Offset >= Length)
            return -1;
        Label = Packet[Offset];
        if ((Label & 0xC0) == 0xC0)
        {
            if (Offset + 1 >= Length || ++Jumps > 32)
                return -1;
            if (End < 0)
                End = Offset + 2;
            Offset = ((Label & 0x3F) << 8) | Packet[Offset + 1];
            continue;
        }
        if (Label & 0xC0)
            return -1;
        Offset++;
        if (!Label)
            break;
        if (Offset + Label > Length || Out + Label + 2 >= (INT)sizeof(Utf8))
            return -1;
        if (Out)
            Utf8[Out++] = '.';
        RtlCopyMemory(Utf8 + Out, Packet + Offset, Label);
        Out += Label;
        Offset += Label;
    }
    Utf8[Out] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, Utf8, -1, Name, Count))
        return -1;
    return End >= 0 ? End : Offset;
}

static
PWSTR
ResolverDupName(PCWSTR Name)
{
    SIZE_T Size = (wcslen(Name) + 1) * sizeof(WCHAR);
    PWSTR Copy = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);

    if (Copy)
        RtlCopyMemory(Copy, Name, Size);
    return Copy;
}

static
BOOL
ResolverReadDataName(const UCHAR *Packet, INT Length, INT Offset, PWSTR *Result)
{
    WCHAR Name[DNS_MAX_NAME_BUFFER_LENGTH];

    if (ResolverReadName(Packet, Length, Offset, Name, ARRAYSIZE(Name)) < 0)
        return FALSE;
    *Result = ResolverDupName(Name);
    return *Result != NULL;
}

static
WORD
ResolverWord(const UCHAR *P)
{
    return (WORD)((P[0] << 8) | P[1]);
}

static
DWORD
ResolverDword(const UCHAR *P)
{
    return ((DWORD)P[0] << 24) | ((DWORD)P[1] << 16) | ((DWORD)P[2] << 8) | P[3];
}

static
PDNS_RECORDW
ResolverParseRecord(const UCHAR *Packet, INT Length, PCWSTR Owner, WORD Type, DWORD Ttl, INT Data, WORD DataLength)
{
    PDNS_RECORDW Record;
    SIZE_T Size = sizeof(DNS_RECORDW);
    DWORD Strings = 0;
    BOOL Ok = TRUE;

    if (Type == DNS_TYPE_TEXT)
    {
        INT Offset = Data;

        while (Offset < Data + DataLength)
        {
            Offset += Packet[Offset] + 1;
            Strings++;
        }
        if (Offset != Data + DataLength || !Strings)
            return NULL;
        Size = FIELD_OFFSET(DNS_RECORDW, Data.TXT.pStringArray[Strings]);
    }

    Record = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, max(Size, sizeof(DNS_RECORDW)));
    if (!Record)
        return NULL;
    Record->pName = ResolverDupName(Owner);
    Record->wType = Type;
    Record->Flags.S.Section = DnsSectionAnswer;
    Record->Flags.S.CharSet = DnsCharSetUnicode;
    Record->dwTtl = Ttl;
    if (!Record->pName)
        Ok = FALSE;

    switch (Type)
    {
    case DNS_TYPE_A:
        Ok = Ok && DataLength == 4;
        if (Ok)
            RtlCopyMemory(&Record->Data.A.IpAddress, Packet + Data, 4);
        Record->wDataLength = sizeof(DNS_A_DATA);
        break;

    case DNS_TYPE_AAAA:
        Ok = Ok && DataLength == 16;
        if (Ok)
            RtlCopyMemory(&Record->Data.AAAA.Ip6Address, Packet + Data, 16);
        Record->wDataLength = sizeof(DNS_AAAA_DATA);
        break;

    case DNS_TYPE_CNAME:
    case DNS_TYPE_PTR:
    case DNS_TYPE_NS:
        Ok = Ok && ResolverReadDataName(Packet, Length, Data, &Record->Data.PTR.pNameHost);
        Record->wDataLength = sizeof(DNS_PTR_DATAW);
        break;

    case DNS_TYPE_MX:
        Ok = Ok && DataLength > 2 && ResolverReadDataName(Packet, Length, Data + 2, &Record->Data.MX.pNameExchange);
        if (Ok)
            Record->Data.MX.wPreference = ResolverWord(Packet + Data);
        Record->wDataLength = sizeof(DNS_MX_DATAW);
        break;

    case DNS_TYPE_SRV:
        Ok = Ok && DataLength > 6 && ResolverReadDataName(Packet, Length, Data + 6, &Record->Data.SRV.pNameTarget);
        if (Ok)
        {
            Record->Data.SRV.wPriority = ResolverWord(Packet + Data);
            Record->Data.SRV.wWeight = ResolverWord(Packet + Data + 2);
            Record->Data.SRV.wPort = ResolverWord(Packet + Data + 4);
        }
        Record->wDataLength = sizeof(DNS_SRV_DATAW);
        break;

    case DNS_TYPE_SOA:
    {
        WCHAR Name[DNS_MAX_NAME_BUFFER_LENGTH];
        INT Offset = ResolverReadName(Packet, Length, Data, Name, ARRAYSIZE(Name));

        Ok = Ok && Offset > 0 && (Record->Data.SOA.pNamePrimaryServer = ResolverDupName(Name)) != NULL;
        Offset = Ok ? ResolverReadName(Packet, Length, Offset, Name, ARRAYSIZE(Name)) : -1;
        Ok = Ok && Offset > 0 && Offset + 20 <= Data + DataLength &&
             (Record->Data.SOA.pNameAdministrator = ResolverDupName(Name)) != NULL;
        if (Ok)
        {
            Record->Data.SOA.dwSerialNo = ResolverDword(Packet + Offset);
            Record->Data.SOA.dwRefresh = ResolverDword(Packet + Offset + 4);
            Record->Data.SOA.dwRetry = ResolverDword(Packet + Offset + 8);
            Record->Data.SOA.dwExpire = ResolverDword(Packet + Offset + 12);
            Record->Data.SOA.dwDefaultTtl = ResolverDword(Packet + Offset + 16);
        }
        Record->wDataLength = sizeof(DNS_SOA_DATAW);
        break;
    }

    case DNS_TYPE_TEXT:
    {
        INT Offset = Data;
        DWORD i;

        Record->Data.TXT.dwStringCount = Strings;
        for (i = 0; Ok && i < Strings; i++)
        {
            UCHAR StringLength = Packet[Offset++];
            INT Chars = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)Packet + Offset, StringLength, NULL, 0);
            PWSTR String = RtlAllocateHeap(RtlGetProcessHeap(), 0, (Chars + 1) * sizeof(WCHAR));

            if (!String)
            {
                Ok = FALSE;
                break;
            }
            MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)Packet + Offset, StringLength, String, Chars);
            String[Chars] = UNICODE_NULL;
            Record->Data.TXT.pStringArray[i] = String;
            Offset += StringLength;
        }
        Record->wDataLength = (WORD)(FIELD_OFFSET(DNS_TXT_DATAW, pStringArray[Strings]));
        break;
    }

    default:
        Ok = FALSE;
        break;
    }

    if (!Ok)
    {
        Record->pNext = NULL;
        DnsIntFreeRecordList((PDNS_RECORD)Record);
        return NULL;
    }
    return Record;
}

static
VOID
ResolverParseReply(const UCHAR *Packet, INT Length, WORD Id, PCWSTR Name, WORD Type, PRESOLVER_REPLY Reply)
{
    WCHAR Owner[DNS_MAX_NAME_BUFFER_LENGTH], Target[DNS_MAX_NAME_BUFFER_LENGTH];
    PDNS_RECORDW *Tail = &Reply->Records;
    WORD Questions, Answers, i;
    INT Offset;
    BOOL Matched = FALSE;

    Reply->Status = ERROR_SUCCESS;
    Reply->Final = FALSE;
    Reply->Records = NULL;

    if (Length < 12 || ResolverWord(Packet) != Id || !(Packet[2] & 0x80))
        return;

    Questions = ResolverWord(Packet + 4);
    Answers = ResolverWord(Packet + 6);
    Offset = 12;
    if (Questions > 1 || (Questions == 0 && !(Packet[3] & 0x0F)))
        return;
    if (Questions == 1)
    {
        Offset = ResolverReadName(Packet, Length, Offset, Owner, ARRAYSIZE(Owner));
        if (Offset < 0 || Offset + 4 > Length || _wcsicmp(Owner, Name) || ResolverWord(Packet + Offset) != Type)
            return;
        Offset += 4;
    }

    switch (Packet[3] & 0x0F)
    {
    case 0:
        break;
    case 3:
        Reply->Status = DNS_ERROR_RCODE_NAME_ERROR;
        Reply->Final = TRUE;
        return;
    case 1:
        Reply->Status = DNS_ERROR_RCODE_FORMAT_ERROR;
        return;
    case 5:
        Reply->Status = DNS_ERROR_RCODE_REFUSED;
        return;
    default:
        Reply->Status = DNS_ERROR_RCODE_SERVER_FAILURE;
        return;
    }

    StringCchCopyW(Target, ARRAYSIZE(Target), Name);
    for (i = 0; i < Answers; i++)
    {
        WORD RecordType, Class, DataLength;
        DWORD Ttl;
        PDNS_RECORDW Record;

        Offset = ResolverReadName(Packet, Length, Offset, Owner, ARRAYSIZE(Owner));
        if (Offset < 0 || Offset + 10 > Length)
            break;
        RecordType = ResolverWord(Packet + Offset);
        Class = ResolverWord(Packet + Offset + 2);
        Ttl = ResolverDword(Packet + Offset + 4);
        DataLength = ResolverWord(Packet + Offset + 8);
        Offset += 10;
        if (Offset + DataLength > Length)
            break;

        if (Class == 1 && (RecordType == Type || RecordType == DNS_TYPE_CNAME))
        {
            Record = ResolverParseRecord(Packet, Length, Owner, RecordType, Ttl, Offset, DataLength);
            if (Record)
            {
                *Tail = Record;
                Tail = &Record->pNext;
                if (RecordType == DNS_TYPE_CNAME && !_wcsicmp(Owner, Target))
                    StringCchCopyW(Target, ARRAYSIZE(Target), Record->Data.CNAME.pNameHost);
                if (RecordType == Type)
                    Matched = TRUE;
            }
        }
        Offset += DataLength;
    }

    Reply->Final = TRUE;
    if (!Matched)
    {
        DnsIntFreeRecordList((PDNS_RECORD)Reply->Records);
        Reply->Records = NULL;
        Reply->Status = DNS_INFO_NO_RECORDS;
    }
}

static
INT
ResolverSockaddrLength(const SOCKADDR_INET *Address)
{
    return Address->si_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN);
}

static
BOOL
ResolverSameAddress(const SOCKADDR_INET *A, const SOCKADDR_INET *B)
{
    if (A->si_family != B->si_family)
        return FALSE;
    if (A->si_family == AF_INET)
        return A->Ipv4.sin_addr.s_addr == B->Ipv4.sin_addr.s_addr && A->Ipv4.sin_port == B->Ipv4.sin_port;
    return !memcmp(&A->Ipv6.sin6_addr, &B->Ipv6.sin6_addr, sizeof(IN6_ADDR)) && A->Ipv6.sin6_port == B->Ipv6.sin6_port;
}

static
ULONG
ResolverPriorityTimeLimit(VOID)
{
    DWORD Limit = DNS_RESOLVER_CORE_PRIORITY_TIME_LIMIT, Value, Size = sizeof(Value), Type;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
                      0, KEY_READ, &Key) != ERROR_SUCCESS)
        return Limit;
    if (RegQueryValueExW(Key, L"ServerPriorityTimeLimit", NULL, &Type, (PBYTE)&Value, &Size) == ERROR_SUCCESS &&
        Type == REG_DWORD && Size == sizeof(Value))
    {
        Limit = Value;
    }
    RegCloseKey(Key);
    return Limit;
}

static
LONG
ResolverFindPriority(const SOCKADDR_INET *Address)
{
    ULONG i;

    for (i = 0; i < ResolverPriorityCount; i++)
    {
        if (ResolverSameAddress(&ResolverPriorities[i].Address, Address))
            return (LONG)i;
    }
    return -1;
}

static
VOID
ResolverSetPriority(const SOCKADDR_INET *Address, BOOL Answered)
{
    LONG Priority = DnsResolverCorePriorityAfter(Answered != FALSE), Slot;

    RtlAcquireSRWLockExclusive(&ResolverPriorityLock);
    Slot = ResolverFindPriority(Address);
    if (Priority == DNS_RESOLVER_CORE_PRIORITY_DEFAULT)
    {
        if (Slot >= 0)
            ResolverPriorities[Slot] = ResolverPriorities[--ResolverPriorityCount];
    }
    else
    {
        if (Slot < 0 && ResolverPriorityCount < ARRAYSIZE(ResolverPriorities))
        {
            Slot = (LONG)ResolverPriorityCount++;
            ResolverPriorities[Slot].Address = *Address;
        }
        if (Slot >= 0)
            ResolverPriorities[Slot].Priority = Priority;
    }
    ASSERT(ResolverPriorityCount <= ARRAYSIZE(ResolverPriorities));
    RtlReleaseSRWLockExclusive(&ResolverPriorityLock);
}

static
VOID
ResolverRankServers(PRESOLVER_CONFIG Config)
{
    ULONG TimeLimit = ResolverPriorityTimeLimit();
    ULONGLONG Now = GetTickCount64();
    LONG Priorities[DNS_MAX_SERVERS];
    ULONG i, j;

    RtlAcquireSRWLockExclusive(&ResolverPriorityLock);
    if (DnsResolverCorePrioritiesExpired(Now, ResolverPriorityReset, TimeLimit))
    {
        ResolverPriorityCount = 0;
        ResolverPriorityReset = Now;
    }
    for (i = 0; i < Config->ServerCount; i++)
    {
        LONG Slot = ResolverFindPriority(&Config->Servers[i].Address);

        Priorities[i] = Slot >= 0 ? ResolverPriorities[Slot].Priority : DNS_RESOLVER_CORE_PRIORITY_DEFAULT;
    }
    RtlReleaseSRWLockExclusive(&ResolverPriorityLock);

    for (i = 0; i < Config->AdapterCount; i++)
    {
        PRESOLVER_ADAPTER Adapter = &Config->Adapters[i];

        ASSERT(Adapter->FirstServer + Adapter->ServerCount <= Config->ServerCount);
        for (j = 0; j < Adapter->ServerCount; j++)
        {
            Config->Servers[Adapter->FirstServer + j].Rank =
                DnsResolverCoreServerRank(&Priorities[Adapter->FirstServer], Adapter->ServerCount, j);
        }
    }
}

static
VOID
ResolverReduceSilentServers(PRESOLVER_CONFIG Config)
{
    ULONG i;

    for (i = 0; i < Config->ServerCount; i++)
    {
        if (Config->Servers[i].Sent && !Config->Servers[i].Answered && !Config->Servers[i].Failed)
            ResolverSetPriority(&Config->Servers[i].Address, FALSE);
    }
}

static
BOOL
ResolverTcpQuery(const SOCKADDR_INET *Server, const UCHAR *Query, INT QueryLength, WORD Id, PCWSTR Name, WORD Type,
                 PRESOLVER_REPLY Reply)
{
    SOCKET Socket = socket(Server->si_family, SOCK_STREAM, IPPROTO_TCP);
    DWORD Timeout = DNS_TCP_TIMEOUT;
    UCHAR *Buffer = NULL;
    UCHAR Prefix[2];
    INT Got = 0, Total = 0, n;
    BOOL Done = FALSE;

    if (Socket == INVALID_SOCKET)
        return FALSE;
    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (PCHAR)&Timeout, sizeof(Timeout));
    setsockopt(Socket, SOL_SOCKET, SO_SNDTIMEO, (PCHAR)&Timeout, sizeof(Timeout));
    Prefix[0] = (UCHAR)(QueryLength >> 8);
    Prefix[1] = (UCHAR)QueryLength;
    if (connect(Socket, (const SOCKADDR *)Server, ResolverSockaddrLength(Server)) == 0 &&
        send(Socket, (PCHAR)Prefix, 2, 0) == 2 && send(Socket, (PCHAR)Query, QueryLength, 0) == QueryLength)
    {
        while (Got < 2 && (n = recv(Socket, (PCHAR)Prefix + Got, 2 - Got, 0)) > 0)
            Got += n;
        if (Got == 2)
        {
            Total = (Prefix[0] << 8) | Prefix[1];
            Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, max(Total, 1));
            Got = 0;
            while (Buffer && Got < Total && (n = recv(Socket, (PCHAR)Buffer + Got, Total - Got, 0)) > 0)
                Got += n;
            if (Buffer && Got == Total)
            {
                ResolverParseReply(Buffer, Total, Id, Name, Type, Reply);
                Done = Reply->Final || Reply->Status != ERROR_SUCCESS;
            }
        }
    }
    if (Buffer)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
    closesocket(Socket);
    return Done;
}

static
VOID
ResolverSendRound(PRESOLVER_CONFIG Config, SOCKET Sockets[2], const UCHAR *Query, INT QueryLength, ULONG Round)
{
    ULONG i;

    for (i = 0; i < Config->ServerCount; i++)
    {
        PRESOLVER_SERVER Server = &Config->Servers[i];
        PRESOLVER_ADAPTER Adapter = &Config->Adapters[Server->Adapter];
        SOCKET Socket = Sockets[Server->Address.si_family == AF_INET6];

        if (Adapter->Negative || Server->Failed || Socket == INVALID_SOCKET)
            continue;
        if (!DnsResolverCoreAttemptAsksServer(Round, Server->Adapter, Server->Rank, Adapter->ServerCount))
            continue;
        sendto(Socket, (PCHAR)Query, QueryLength, 0, (const SOCKADDR *)&Server->Address,
               ResolverSockaddrLength(&Server->Address));
        Server->Sent = TRUE;
    }
}

static
BOOL
ResolverFailOver(PRESOLVER_CONFIG Config, SOCKET Sockets[2], const UCHAR *Query, INT QueryLength)
{
    BOOL Pending = FALSE;
    ULONG i;

    for (i = 0; i < Config->ServerCount; i++)
    {
        PRESOLVER_SERVER Server = &Config->Servers[i];
        SOCKET Socket = Sockets[Server->Address.si_family == AF_INET6];

        if (Config->Adapters[Server->Adapter].Negative || Server->Failed || Socket == INVALID_SOCKET)
            continue;
        if (Server->Sent)
        {
            Pending = TRUE;
            continue;
        }
        sendto(Socket, (PCHAR)Query, QueryLength, 0, (const SOCKADDR *)&Server->Address,
               ResolverSockaddrLength(&Server->Address));
        Server->Sent = TRUE;
        return TRUE;
    }
    return Pending;
}

static
LONG
ResolverFindServer(PRESOLVER_CONFIG Config, const SOCKADDR_INET *From)
{
    ULONG i;

    for (i = 0; i < Config->ServerCount; i++)
    {
        if (ResolverSameAddress(&Config->Servers[i].Address, From))
            return (LONG)i;
    }
    return -1;
}

static
BOOL
ResolverAllNegative(PRESOLVER_CONFIG Config)
{
    ULONG i;

    for (i = 0; i < Config->AdapterCount; i++)
    {
        if (!Config->Adapters[i].Negative)
            return FALSE;
    }
    return TRUE;
}

static
DNS_STATUS
ResolverQueryName(PRESOLVER_CONFIG Config, PCWSTR Name, WORD Type, DWORD Options, PDNS_RECORDW *Result)
{
    UCHAR Query[512], Packet[DNS_UDP_SIZE + 256];
    SOCKET Sockets[2];
    DNS_STATUS Negative = ERROR_SUCCESS, Failure = ERROR_TIMEOUT;
    WORD Id;
    INT QueryLength;
    ULONG Round, i;

    for (i = 0; i < Config->AdapterCount; i++)
        Config->Adapters[i].Negative = FALSE;
    for (i = 0; i < Config->ServerCount; i++)
    {
        Config->Servers[i].Sent = FALSE;
        Config->Servers[i].Answered = FALSE;
        Config->Servers[i].Failed = FALSE;
    }
    ResolverRankServers(Config);

    Id = (WORD)(GetTickCount() ^ (GetCurrentThreadId() << 5) ^ (ULONG_PTR)Name);
    QueryLength = ResolverBuildQuery(Name, Type, Id, !(Options & DNS_QUERY_NO_RECURSION), Query, sizeof(Query));
    if (QueryLength < 0)
        return ERROR_INVALID_NAME;

    Sockets[0] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Sockets[1] = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

    for (Round = 0; Round < DNS_RESOLVER_CORE_ATTEMPTS; Round++)
    {
        DWORD Deadline;

        ResolverSendRound(Config, Sockets, Query, QueryLength, Round);
        Deadline = GetTickCount() + DnsResolverCoreAttemptTimeout(Round);

        for (;;)
        {
            DWORD Now = GetTickCount();
            LONG Remaining = (LONG)(Deadline - Now);
            struct timeval Wait;
            fd_set Read;
            INT Ready;

            if (Remaining <= 0)
                break;
            FD_ZERO(&Read);
            if (Sockets[0] != INVALID_SOCKET)
                FD_SET(Sockets[0], &Read);
            if (Sockets[1] != INVALID_SOCKET)
                FD_SET(Sockets[1], &Read);
            Wait.tv_sec = Remaining / 1000;
            Wait.tv_usec = (Remaining % 1000) * 1000;
            Ready = select(0, &Read, NULL, NULL, &Wait);
            if (Ready <= 0)
                break;

            for (i = 0; i < 2; i++)
            {
                SOCKADDR_INET From;
                INT FromLength = sizeof(From), Received;
                RESOLVER_REPLY Reply;
                LONG Server;

                if (Sockets[i] == INVALID_SOCKET || !FD_ISSET(Sockets[i], &Read))
                    continue;
                Received = recvfrom(Sockets[i], (PCHAR)Packet, sizeof(Packet), 0, (SOCKADDR *)&From, &FromLength);
                if (Received <= 0 || (Server = ResolverFindServer(Config, &From)) < 0)
                    continue;

                ResolverParseReply(Packet, Received, Id, Name, Type, &Reply);
                if (Reply.Final && !Config->Servers[Server].Answered)
                {
                    Config->Servers[Server].Answered = TRUE;
                    ResolverSetPriority(&Config->Servers[Server].Address, TRUE);
                }
                if (Reply.Final && (Packet[2] & 0x02))
                {
                    DnsIntFreeRecordList((PDNS_RECORD)Reply.Records);
                    Reply.Records = NULL;
                    Reply.Final = FALSE;
                    if (!ResolverTcpQuery(&Config->Servers[Server].Address, Query, QueryLength, Id, Name, Type, &Reply))
                        continue;
                }

                if (Reply.Final && Reply.Status == ERROR_SUCCESS)
                {
                    *Result = Reply.Records;
                    closesocket(Sockets[0]);
                    closesocket(Sockets[1]);
                    return ERROR_SUCCESS;
                }
                if (Reply.Final)
                {
                    if (Negative == ERROR_SUCCESS)
                        Negative = Reply.Status;
                    Config->Adapters[Config->Servers[Server].Adapter].Negative = TRUE;
                    if (ResolverAllNegative(Config))
                    {
                        closesocket(Sockets[0]);
                        closesocket(Sockets[1]);
                        return Negative;
                    }
                }
                else if (Reply.Status != ERROR_SUCCESS)
                {
                    /* TODO: an error answer changes the server's priority on Windows; here it only moves this query on */
                    Failure = Reply.Status;
                    Config->Servers[Server].Failed = TRUE;
                    if (!ResolverFailOver(Config, Sockets, Query, QueryLength))
                    {
                        closesocket(Sockets[0]);
                        closesocket(Sockets[1]);
                        return Negative != ERROR_SUCCESS ? Negative : Failure;
                    }
                }
            }
        }
        ResolverReduceSilentServers(Config);
    }

    closesocket(Sockets[0]);
    closesocket(Sockets[1]);
    return Negative != ERROR_SUCCESS ? Negative : Failure;
}

static
ULONG
ResolverLlmnrLinks(PULONG Links4, PULONG Count4, PULONG Links6, PULONG Count6)
{
    ULONG Size = 16384;
    PIP_ADAPTER_ADDRESSES List, Adapter;
    DWORD Error;

    *Count4 = *Count6 = 0;
    for (;;)
    {
        List = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!List)
            return 0;
        Error = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                     GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME, NULL, List, &Size);
        if (Error != ERROR_BUFFER_OVERFLOW)
            break;
        RtlFreeHeap(RtlGetProcessHeap(), 0, List);
    }
    if (Error != ERROR_SUCCESS)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, List);
        return 0;
    }

    for (Adapter = List; Adapter; Adapter = Adapter->Next)
    {
        PIP_ADAPTER_UNICAST_ADDRESS Unicast;
        BOOL Have4 = FALSE, Have6 = FALSE;

        if (Adapter->OperStatus != IfOperStatusUp || Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (Unicast = Adapter->FirstUnicastAddress; Unicast; Unicast = Unicast->Next)
        {
            const SOCKADDR *Address = Unicast->Address.lpSockaddr;

            if (!Address)
                continue;
            if (Address->sa_family == AF_INET && !Have4 && *Count4 < DNS_MAX_ADAPTERS)
            {
                Links4[(*Count4)++] = ((const SOCKADDR_IN *)Address)->sin_addr.s_addr;
                Have4 = TRUE;
            }
            else if (Address->sa_family == AF_INET6 && !Have6 && Adapter->Ipv6IfIndex && *Count6 < DNS_MAX_ADAPTERS)
            {
                Links6[(*Count6)++] = Adapter->Ipv6IfIndex;
                Have6 = TRUE;
            }
        }
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, List);
    return *Count4 + *Count6;
}

static
DNS_STATUS
ResolverLlmnrQuery(PCWSTR Name, WORD Type, PDNS_RECORDW *Result)
{
    ULONG Links4[DNS_MAX_ADAPTERS], Links6[DNS_MAX_ADAPTERS], Count4, Count6, i;
    UCHAR Query[512], Packet[DNS_UDP_SIZE + 256];
    SOCKADDR_IN Group4;
    SOCKADDR_IN6 Group6;
    SOCKET Sockets[2];
    DWORD Start, Hops = 1;
    INT QueryLength, Round;
    WORD Id;

    if (!ResolverLlmnrLinks(Links4, &Count4, Links6, &Count6))
        return ERROR_INVALID_NAME;

    Id = (WORD)(GetTickCount() ^ (GetCurrentThreadId() << 7) ^ (ULONG_PTR)Name ^ Type);
    QueryLength = ResolverBuildQuery(Name, Type, Id, FALSE, Query, sizeof(Query));
    if (QueryLength < 0)
        return ERROR_INVALID_NAME;

    RtlZeroMemory(&Group4, sizeof(Group4));
    Group4.sin_family = AF_INET;
    Group4.sin_port = htons(LLMNR_PORT);
    Group4.sin_addr.s_addr = htonl(0xE00000FC);
    RtlZeroMemory(&Group6, sizeof(Group6));
    Group6.sin6_family = AF_INET6;
    Group6.sin6_port = htons(LLMNR_PORT);
    Group6.sin6_addr.s6_addr[0] = 0xFF;
    Group6.sin6_addr.s6_addr[1] = 0x02;
    Group6.sin6_addr.s6_addr[13] = 0x01;
    Group6.sin6_addr.s6_addr[15] = 0x03;

    Sockets[0] = Count4 ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : INVALID_SOCKET;
    Sockets[1] = Count6 ? socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP) : INVALID_SOCKET;
    if (Sockets[0] != INVALID_SOCKET)
        setsockopt(Sockets[0], IPPROTO_IP, IP_MULTICAST_TTL, (PCHAR)&Hops, sizeof(Hops));
    if (Sockets[1] != INVALID_SOCKET)
        setsockopt(Sockets[1], IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (PCHAR)&Hops, sizeof(Hops));

    Start = GetTickCount();
    for (Round = 0; Round < LLMNR_ROUNDS; Round++)
    {
        DWORD Deadline = Start + (Round + 1) * LLMNR_RESEND;

        for (i = 0; i < Count4 && Sockets[0] != INVALID_SOCKET; i++)
        {
            setsockopt(Sockets[0], IPPROTO_IP, IP_MULTICAST_IF, (PCHAR)&Links4[i], sizeof(Links4[i]));
            sendto(Sockets[0], (PCHAR)Query, QueryLength, 0, (const SOCKADDR *)&Group4, sizeof(Group4));
        }
        for (i = 0; i < Count6 && Sockets[1] != INVALID_SOCKET; i++)
        {
            setsockopt(Sockets[1], IPPROTO_IPV6, IPV6_MULTICAST_IF, (PCHAR)&Links6[i], sizeof(Links6[i]));
            Group6.sin6_scope_id = Links6[i];
            sendto(Sockets[1], (PCHAR)Query, QueryLength, 0, (const SOCKADDR *)&Group6, sizeof(Group6));
        }

        for (;;)
        {
            LONG Remaining = (LONG)(Deadline - GetTickCount());
            struct timeval Wait;
            fd_set Read;
            ULONG s;

            if (Remaining <= 0)
                break;
            FD_ZERO(&Read);
            for (s = 0; s < 2; s++)
            {
                if (Sockets[s] != INVALID_SOCKET)
                    FD_SET(Sockets[s], &Read);
            }
            Wait.tv_sec = Remaining / 1000;
            Wait.tv_usec = (Remaining % 1000) * 1000;
            if (select(0, &Read, NULL, NULL, &Wait) <= 0)
                break;

            for (s = 0; s < 2; s++)
            {
                RESOLVER_REPLY Reply;
                INT Received;

                if (Sockets[s] == INVALID_SOCKET || !FD_ISSET(Sockets[s], &Read))
                    continue;
                Received = recvfrom(Sockets[s], (PCHAR)Packet, sizeof(Packet), 0, NULL, NULL);
                if (Received <= 0)
                    continue;
                ResolverParseReply(Packet, Received, Id, Name, Type, &Reply);
                if (Reply.Final && Reply.Status == ERROR_SUCCESS && Reply.Records)
                {
                    *Result = Reply.Records;
                    if (Sockets[0] != INVALID_SOCKET)
                        closesocket(Sockets[0]);
                    if (Sockets[1] != INVALID_SOCKET)
                        closesocket(Sockets[1]);
                    return ERROR_SUCCESS;
                }
                DnsIntFreeRecordList((PDNS_RECORD)Reply.Records);
            }
        }
    }

    if (Sockets[0] != INVALID_SOCKET)
        closesocket(Sockets[0]);
    if (Sockets[1] != INVALID_SOCKET)
        closesocket(Sockets[1]);
    return ERROR_INVALID_NAME;
}

DNS_STATUS
DnsIntResolve(LPCWSTR Name, WORD Type, DWORD Options, PDNS_RECORDW *Result)
{
    PRESOLVER_CONFIG Config;
    DNS_STATUS Status, First = ERROR_SUCCESS;
    ULONG i;

    *Result = NULL;
    if (Options & DNS_QUERY_NO_WIRE_QUERY)
        return DNS_ERROR_RCODE_NAME_ERROR;

    ResolverStartWinsock();
    if (Options & DNS_QUERY_MULTICAST_ONLY)
        return wcschr(Name, L'.') ? ERROR_INVALID_NAME : ResolverLlmnrQuery(Name, Type, Result);
    Config = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Config));
    if (!Config)
        return ERROR_OUTOFMEMORY;

    Status = ResolverLoadConfig(Config, Name);
    if (Status != ERROR_SUCCESS && (Status != DNS_ERROR_NO_DNS_SERVERS || wcschr(Name, L'.')))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Config);
        return Status;
    }
    if (Status != ERROR_SUCCESS)
        Config->CandidateCount = 0;

    for (i = 0; i < Config->CandidateCount; i++)
    {
        Status = ResolverQueryName(Config, Config->Candidates[i], Type, Options, Result);
        DPRINT("DnsIntResolve(%S, %u) -> %lu\n", Config->Candidates[i], Type, Status);
        if (Status == ERROR_SUCCESS)
            break;
        if (First == ERROR_SUCCESS)
            First = Status;
        if (Status != DNS_ERROR_RCODE_NAME_ERROR && Status != DNS_INFO_NO_RECORDS)
            break;
    }

    if (!wcschr(Name, L'.') && (!Config->CandidateCount || Status != ERROR_SUCCESS))
    {
        Status = (Options & DNS_QUERY_NO_MULTICAST) ? ERROR_INVALID_NAME : ResolverLlmnrQuery(Name, Type, Result);
        if (Status != ERROR_SUCCESS && First == ERROR_SUCCESS)
            First = Status;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Config);
    return Status == ERROR_SUCCESS ? Status : First;
}
