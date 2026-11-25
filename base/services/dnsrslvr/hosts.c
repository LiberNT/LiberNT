/*
 * PROJECT:     LiberNT DNS Client service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loads the hosts file into the resolver cache and reloads it when it changes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <inaddr.h>
#include <in6addr.h>

#define NDEBUG
#include <debug.h>

#define HOSTS_TTL 86400

static WCHAR HostsDirectory[MAX_PATH];
static WCHAR HostsPath[MAX_PATH];
static HANDLE HostsChange = INVALID_HANDLE_VALUE;
static HANDLE HostsStopEvent;
static HANDLE HostsThread;

static BOOL HostsResolvePaths(VOID)
{
    DWORD Size = sizeof(HostsDirectory);
    UINT Length;
    size_t End;

    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"System\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                     L"DatabasePath",
                     RRF_RT_REG_SZ,
                     NULL,
                     HostsDirectory,
                     &Size) != ERROR_SUCCESS || !HostsDirectory[0])
    {
        Length = GetSystemDirectoryW(HostsDirectory, ARRAYSIZE(HostsDirectory));
        if (!Length || Length >= ARRAYSIZE(HostsDirectory) ||
            FAILED(StringCchCatW(HostsDirectory, ARRAYSIZE(HostsDirectory), L"\\drivers\\etc")))
        {
            return FALSE;
        }
    }

    if (FAILED(StringCchLengthW(HostsDirectory, ARRAYSIZE(HostsDirectory), &End)) || End == 0 ||
        FAILED(StringCchCopyW(HostsPath, ARRAYSIZE(HostsPath), HostsDirectory)))
    {
        return FALSE;
    }
    if (HostsDirectory[End - 1] != L'\\' &&
        FAILED(StringCchCatW(HostsPath, ARRAYSIZE(HostsPath), L"\\")))
    {
        return FALSE;
    }
    return SUCCEEDED(StringCchCatW(HostsPath, ARRAYSIZE(HostsPath), L"hosts"));
}

static DWORD HostsReadFile(PSTR *Contents)
{
    LARGE_INTEGER Size;
    HANDLE File;
    PSTR Buffer;
    DWORD Done = 0, Chunk, Error = ERROR_SUCCESS;

    *Contents = NULL;
    File = CreateFileW(HostsPath,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
        return GetLastError();

    if (!GetFileSizeEx(File, &Size))
    {
        Error = GetLastError();
        CloseHandle(File);
        return Error;
    }
    if (Size.QuadPart >= MAXDWORD)
    {
        CloseHandle(File);
        return ERROR_FILE_TOO_LARGE;
    }

    Buffer = HeapAlloc(GetProcessHeap(), 0, Size.LowPart + 1);
    if (!Buffer)
    {
        CloseHandle(File);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    while (Done < Size.LowPart)
    {
        if (!ReadFile(File, Buffer + Done, Size.LowPart - Done, &Chunk, NULL))
        {
            Error = GetLastError();
            break;
        }
        if (Chunk == 0)
            break;
        Done += Chunk;
    }
    CloseHandle(File);

    if (Error != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return Error;
    }

    ASSERT(Done <= Size.LowPart);
    Buffer[Done] = '\0';
    *Contents = Buffer;
    return ERROR_SUCCESS;
}

static VOID HostsInitRecord(PDNS_RECORDW Record, PWSTR Name, WORD Type, WORD DataLength)
{
    ZeroMemory(Record, sizeof(*Record));
    Record->pName = Name;
    Record->wType = Type;
    Record->wDataLength = DataLength;
    Record->Flags.S.Section = DnsSectionAnswer;
    Record->Flags.S.CharSet = DnsCharSetUnicode;
    Record->dwTtl = HOSTS_TTL;
}

static VOID HostsAddPointer(PWSTR ReverseName, PWSTR HostName)
{
    DNS_RECORDW Record;

    HostsInitRecord(&Record, ReverseName, DNS_TYPE_PTR, sizeof(DNS_PTR_DATAW));
    Record.Data.PTR.pNameHost = HostName;
    DnsIntCacheAddEntry(ReverseName, DNS_TYPE_PTR, &Record, TRUE);
}

static VOID HostsAddIpv4(PWSTR HostName, const IN_ADDR *Address)
{
    DNS_RECORDW Record;
    WCHAR ReverseName[32];

    HostsInitRecord(&Record, HostName, DNS_TYPE_A, sizeof(DNS_A_DATA));
    Record.Data.A.IpAddress = Address->S_un.S_addr;
    DnsIntCacheAddEntry(HostName, DNS_TYPE_A, &Record, TRUE);

    if (SUCCEEDED(StringCchPrintfW(ReverseName, ARRAYSIZE(ReverseName), L"%u.%u.%u.%u.in-addr.arpa.",
                                   Address->S_un.S_un_b.s_b4, Address->S_un.S_un_b.s_b3,
                                   Address->S_un.S_un_b.s_b2, Address->S_un.S_un_b.s_b1)))
    {
        HostsAddPointer(ReverseName, HostName);
    }
}

static VOID HostsAddIpv6(PWSTR HostName, const IN6_ADDR *Address)
{
    static const WCHAR Hex[] = L"0123456789abcdef";
    DNS_RECORDW Record;
    WCHAR ReverseName[sizeof(IN6_ADDR) * 4 + ARRAYSIZE(L"ip6.arpa.")];
    PWSTR Cursor = ReverseName;
    ULONG Index;

    HostsInitRecord(&Record, HostName, DNS_TYPE_AAAA, sizeof(DNS_AAAA_DATA));
    CopyMemory(&Record.Data.AAAA.Ip6Address, Address, sizeof(IN6_ADDR));
    DnsIntCacheAddEntry(HostName, DNS_TYPE_AAAA, &Record, TRUE);

    for (Index = sizeof(IN6_ADDR); Index-- > 0;)
    {
        *Cursor++ = Hex[Address->u.Byte[Index] & 0xF];
        *Cursor++ = L'.';
        *Cursor++ = Hex[Address->u.Byte[Index] >> 4];
        *Cursor++ = L'.';
    }
    ASSERT(Cursor == ReverseName + sizeof(IN6_ADDR) * 4);
    CopyMemory(Cursor, L"ip6.arpa.", sizeof(L"ip6.arpa."));
    HostsAddPointer(ReverseName, HostName);
}

static VOID HostsAddLine(PCSTR Address, PCSTR Name)
{
    WCHAR HostName[DNS_MAX_NAME_BUFFER_LENGTH];
    PCSTR Terminator;
    IN_ADDR Ipv4;
    IN6_ADDR Ipv6;

    if (!MultiByteToWideChar(CP_ACP, 0, Name, -1, HostName, ARRAYSIZE(HostName)))
        return;

    if (NT_SUCCESS(RtlIpv4StringToAddressA(Address, TRUE, &Terminator, &Ipv4)) && *Terminator == '\0')
        HostsAddIpv4(HostName, &Ipv4);
    else if (NT_SUCCESS(RtlIpv6StringToAddressA(Address, &Terminator, &Ipv6)) && *Terminator == '\0')
        HostsAddIpv6(HostName, &Ipv6);
}

static VOID HostsParse(PSTR Contents)
{
    static const CHAR Blank[] = " \t";
    PSTR Line = Contents, Next, Address, Name, End;

    while (*Line)
    {
        Next = Line + strcspn(Line, "\r\n");
        if (*Next)
            *Next++ = '\0';

        End = strchr(Line, '#');
        if (End)
            *End = '\0';

        Address = Line + strspn(Line, Blank);
        End = Address + strcspn(Address, Blank);
        if (End != Address && *End)
        {
            *End++ = '\0';
            Name = End + strspn(End, Blank);
            Name[strcspn(Name, Blank)] = '\0';
            if (*Name)
                HostsAddLine(Address, Name);
        }
        Line = Next;
    }
}

static VOID HostsLoad(VOID)
{
    PSTR Contents;
    DWORD Error;

    Error = HostsReadFile(&Contents);
    if (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND)
    {
        DnsIntCacheFlush(CACHE_FLUSH_HOSTS_FILE_ENTRIES);
        return;
    }
    if (Error != ERROR_SUCCESS)
        return;

    ASSERT(Contents != NULL);
    DnsIntCacheFlush(CACHE_FLUSH_HOSTS_FILE_ENTRIES);
    HostsParse(Contents);
    HeapFree(GetProcessHeap(), 0, Contents);
}

static DWORD WINAPI HostsThreadRoutine(LPVOID Parameter)
{
    HANDLE Events[2];

    UNREFERENCED_PARAMETER(Parameter);
    ASSERT(HostsStopEvent != NULL && HostsChange != INVALID_HANDLE_VALUE);

    Events[0] = HostsStopEvent;
    Events[1] = HostsChange;
    while (WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        if (!FindNextChangeNotification(HostsChange))
            break;
        HostsLoad();
    }
    return 0;
}

VOID HostsStart(VOID)
{
    ASSERT(HostsThread == NULL && HostsStopEvent == NULL && HostsChange == INVALID_HANDLE_VALUE);

    if (!HostsResolvePaths())
        return;

    HostsChange = FindFirstChangeNotificationW(HostsDirectory,
                                               FALSE,
                                               FILE_NOTIFY_CHANGE_FILE_NAME |
                                               FILE_NOTIFY_CHANGE_SIZE |
                                               FILE_NOTIFY_CHANGE_LAST_WRITE);
    HostsLoad();
    if (HostsChange == INVALID_HANDLE_VALUE)
        return;

    HostsStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (HostsStopEvent)
        HostsThread = CreateThread(NULL, 0, HostsThreadRoutine, NULL, 0, NULL);
    if (!HostsThread)
        HostsStop();
}

VOID HostsStop(VOID)
{
    if (HostsThread)
    {
        ASSERT(HostsStopEvent != NULL);
        SetEvent(HostsStopEvent);
        WaitForSingleObject(HostsThread, INFINITE);
        CloseHandle(HostsThread);
        HostsThread = NULL;
    }
    if (HostsStopEvent)
    {
        CloseHandle(HostsStopEvent);
        HostsStopEvent = NULL;
    }
    if (HostsChange != INVALID_HANDLE_VALUE)
    {
        FindCloseChangeNotification(HostsChange);
        HostsChange = INVALID_HANDLE_VALUE;
    }
}
