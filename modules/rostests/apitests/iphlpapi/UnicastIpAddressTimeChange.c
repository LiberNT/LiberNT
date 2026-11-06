/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that unicast addresses keep their timing when the system time is set back
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

#define HOUR_100NS (36000000000LL)

static LONGLONG
QuerySystemTime100ns(VOID)
{
    FILETIME FileTime;
    ULARGE_INTEGER Time;

    GetSystemTimeAsFileTime(&FileTime);
    Time.LowPart = FileTime.dwLowDateTime;
    Time.HighPart = FileTime.dwHighDateTime;
    return (LONGLONG)Time.QuadPart;
}

static BOOL
ShiftSystemTime(
    _In_ LONGLONG Delta)
{
    FILETIME FileTime;
    SYSTEMTIME SystemTime;
    ULARGE_INTEGER Time;

    Time.QuadPart = (ULONGLONG)(QuerySystemTime100ns() + Delta);
    FileTime.dwLowDateTime = Time.LowPart;
    FileTime.dwHighDateTime = Time.HighPart;
    return FileTimeToSystemTime(&FileTime, &SystemTime) && SetSystemTime(&SystemTime);
}

static BOOL
EnableSystemTimePrivilege(VOID)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;
    BOOL Result;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return FALSE;
    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Result = LookupPrivilegeValueA(NULL, SE_SYSTEMTIME_NAME, &Privileges.Privileges[0].Luid) &&
             AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) &&
             GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Result;
}

static const MIB_UNICASTIPADDRESS_ROW *
FindRow(
    _In_ const MIB_UNICASTIPADDRESS_TABLE *Table,
    _In_ const MIB_UNICASTIPADDRESS_ROW *Row)
{
    ULONG Index;

    for (Index = 0; Index < Table->NumEntries; Index++)
    {
        const MIB_UNICASTIPADDRESS_ROW *Other = &Table->Table[Index];

        if (Other->InterfaceLuid.Value == Row->InterfaceLuid.Value &&
            !memcmp(&Other->Address.Ipv6.sin6_addr, &Row->Address.Ipv6.sin6_addr, sizeof(IN6_ADDR)))
            return Other;
    }
    return NULL;
}

static BOOL
FindLeaseInterface(
    _Out_ PNET_LUID Luid)
{
    PMIB_UNICASTIPADDRESS_TABLE Table = NULL;
    BOOL Found = FALSE;
    ULONG Index;

    if (GetUnicastIpAddressTable(AF_INET, &Table) != NO_ERROR)
        return FALSE;
    for (Index = 0; Index < Table->NumEntries && !Found; Index++)
    {
        if (Table->Table[Index].PrefixOrigin == IpPrefixOriginDhcp &&
            Table->Table[Index].DadState == IpDadStatePreferred)
        {
            *Luid = Table->Table[Index].InterfaceLuid;
            Found = TRUE;
        }
    }
    FreeMibTable(Table);
    return Found;
}

static VOID
CheckCreationTimes(
    _In_ const MIB_UNICASTIPADDRESS_TABLE *Before)
{
    PMIB_UNICASTIPADDRESS_TABLE After = NULL;
    ULONG Index, Compared = 0;

    ok_long(GetUnicastIpAddressTable(AF_INET6, &After), NO_ERROR);
    if (!After)
        return;
    for (Index = 0; Index < Before->NumEntries; Index++)
    {
        const MIB_UNICASTIPADDRESS_ROW *Row = &Before->Table[Index];
        const MIB_UNICASTIPADDRESS_ROW *Later = FindRow(After, Row);

        if (!Later)
            continue;
        Compared++;
        ok(Row->CreationTimeStamp.QuadPart == Later->CreationTimeStamp.QuadPart,
           "Row %lu creation time changed from %I64d to %I64d\n", Index,
           Row->CreationTimeStamp.QuadPart, Later->CreationTimeStamp.QuadPart);
    }
    ok(Compared != 0, "No IPv6 unicast address was present before and after the time change\n");
    FreeMibTable(After);
}

static VOID
CheckDuplicateAddressDetection(
    _In_ NET_LUID Luid)
{
    static const UCHAR Prefix[8] = { 0xFD, 0x4C, 0x49, 0x4E, 0x54, 0x50, 0x00, 0x00 };
    MIB_UNICASTIPADDRESS_ROW Row, Current;
    DWORD Status;

    InitializeUnicastIpAddressEntry(&Row);
    Row.InterfaceLuid = Luid;
    Row.Address.Ipv6.sin6_family = AF_INET6;
    memcpy(Row.Address.Ipv6.sin6_addr.s6_addr, Prefix, sizeof(Prefix));
    Row.Address.Ipv6.sin6_addr.s6_addr[15] = 0x31;
    Row.OnLinkPrefixLength = 64;

    Status = CreateUnicastIpAddressEntry(&Row);
    ok_long(Status, NO_ERROR);
    if (Status != NO_ERROR)
        return;
    Sleep(5000);
    Current = Row;
    ok_long(GetUnicastIpAddressEntry(&Current), NO_ERROR);
    ok_long(Current.DadState, IpDadStatePreferred);
    ok_long(DeleteUnicastIpAddressEntry(&Row), NO_ERROR);
}

START_TEST(UnicastIpAddressTimeChange)
{
    PMIB_UNICASTIPADDRESS_TABLE Before = NULL;
    NET_LUID Luid;
    BOOL Shifted;

    if (!EnableSystemTimePrivilege())
    {
        skip("Cannot enable SeSystemtimePrivilege\n");
        return;
    }
    if (!FindLeaseInterface(&Luid))
    {
        skip("No preferred DHCP IPv4 address to find the interface by\n");
        return;
    }
    ok_long(GetUnicastIpAddressTable(AF_INET6, &Before), NO_ERROR);
    if (!Before)
        return;

    Shifted = ShiftSystemTime(-2 * HOUR_100NS);
    ok(Shifted, "Setting the system time back failed: %lu\n", GetLastError());
    if (Shifted)
    {
        Sleep(1500);
        CheckCreationTimes(Before);
        CheckDuplicateAddressDetection(Luid);
        ok(ShiftSystemTime(2 * HOUR_100NS), "Restoring the system time failed: %lu\n", GetLastError());
    }
    FreeMibTable(Before);
}
