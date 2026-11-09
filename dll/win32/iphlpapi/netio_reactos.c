/*
 * PROJECT:     LiberNT IP Helper API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     NETIO unicast address and route configuration APIs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "iphlpapi_private.h"
#include <netioapi.h>
#include <netiodef.h>
#include <tcpioctl.h>
#include <tcpip_undoc.h>

WINE_DEFAULT_DEBUG_CHANNEL(iphlpapi);

#define IPHLP_UNCHANGED_BOOLEAN 0xFF

VOID WINAPI InitializeUnicastIpAddressEntry(PMIB_UNICASTIPADDRESS_ROW Row)
{
    TRACE("(%p)\n", Row);

    memset(Row, 0, sizeof(*Row));
    Row->PrefixOrigin = IpPrefixOriginUnchanged;
    Row->SuffixOrigin = IpSuffixOriginUnchanged;
    Row->ValidLifetime = 0xFFFFFFFF;
    Row->PreferredLifetime = 0xFFFFFFFF;
    Row->OnLinkPrefixLength = 0xFF;
}

VOID WINAPI InitializeIpForwardEntry(PMIB_IPFORWARD_ROW2 Row)
{
    TRACE("(%p)\n", Row);

    memset(Row, 0, sizeof(*Row));
    Row->SitePrefixLength = 0xFF;
    Row->ValidLifetime = 0xFFFFFFFF;
    Row->PreferredLifetime = 0xFFFFFFFF;
    Row->Metric = 0xFFFFFFFF;
    Row->Protocol = (NL_ROUTE_PROTOCOL)-1;
    Row->Loopback = IPHLP_UNCHANGED_BOOLEAN;
    Row->AutoconfigureAddress = IPHLP_UNCHANGED_BOOLEAN;
    Row->Publish = IPHLP_UNCHANGED_BOOLEAN;
    Row->Immortal = IPHLP_UNCHANGED_BOOLEAN;
}

static DWORD resolve_interface(const NET_LUID *Luid, NET_IFINDEX Index, NET_IFINDEX *Result)
{
    NET_LUID Found;

    if (Luid->Value)
    {
        if (ConvertInterfaceLuidToIndex(Luid, Result) != NO_ERROR || !*Result)
            return ERROR_FILE_NOT_FOUND;
        return NO_ERROR;
    }
    if (!Index || ConvertInterfaceIndexToLuid(Index, &Found) != NO_ERROR)
        return ERROR_FILE_NOT_FOUND;
    *Result = Index;
    return NO_ERROR;
}

static NTSTATUS ip6_ioctl(ULONG Code, PVOID Input, ULONG InputLength)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Ip6");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE File;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&File, SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = NtDeviceIoControlFile(File, NULL, NULL, NULL, &Iosb, Code, Input, InputLength, NULL, 0);
    NtClose(File);
    return Status;
}

static VOID ip4_route_set_lifetime(const MIB_IPFORWARDROW *Route, ULONG Lifetime)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Ip");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    IP4_ROUTE_INFO Info;
    HANDLE File;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenFile(&File, SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               FILE_SYNCHRONOUS_IO_NONALERT)))
        return;
    memset(&Info, 0, sizeof(Info));
    Info.Index = Route->dwForwardIfIndex;
    Info.Destination = Route->dwForwardDest;
    Info.Mask = Route->dwForwardMask;
    Info.NextHop = Route->dwForwardNextHop;
    Info.ValidLifetime = Lifetime;
    NtDeviceIoControlFile(File, NULL, NULL, NULL, &Iosb, IOCTL_IP_SET_ROUTE, &Info, sizeof(Info), NULL, 0);
    NtClose(File);
}

static DWORD ip4_interface_set(IP4_INTERFACE_INFO *Info)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Ip");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE File;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&File, SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    Status = NtDeviceIoControlFile(File, NULL, NULL, NULL, &Iosb, IOCTL_IP_SET_INTERFACE, Info, sizeof(*Info), NULL, 0);
    NtClose(File);
    if (Status == STATUS_NOT_FOUND)
        return ERROR_FILE_NOT_FOUND;
    if (Status == STATUS_INVALID_PARAMETER)
        return ERROR_INVALID_PARAMETER;
    return RtlNtStatusToDosError(Status);
}

static DWORD ip6_status_to_error(NTSTATUS Status, DWORD NotFound)
{
    switch (Status)
    {
    case STATUS_SUCCESS:
        return NO_ERROR;
    case STATUS_OBJECT_NAME_COLLISION:
        return ERROR_OBJECT_ALREADY_EXISTS;
    case STATUS_NOT_FOUND:
        return NotFound;
    case STATUS_INVALID_PARAMETER:
        return ERROR_INVALID_PARAMETER;
    case STATUS_INSUFFICIENT_RESOURCES:
        return ERROR_NOT_ENOUGH_MEMORY;
    default:
        return RtlNtStatusToDosError(Status);
    }
}

static BOOL ipv6_prefix_is_masked(const IN6_ADDR *Prefix, UINT8 Length)
{
    UINT i;

    for (i = 0; i < 16; i++)
    {
        UINT8 Mask = Length >= (i + 1) * 8 ? 0xFF : Length <= i * 8 ? 0 : (UINT8)(0xFF << (8 - (Length - i * 8)));

        if (Prefix->u.Byte[i] & ~Mask)
            return FALSE;
    }
    return TRUE;
}

static DWORD validate_unicast(const MIB_UNICASTIPADDRESS_ROW *Row, NET_IFINDEX *Index, BOOL Create)
{
    const IN6_ADDR *Address = &Row->Address.Ipv6.sin6_addr;
    DWORD Error;

    if (!Row)
        return ERROR_INVALID_PARAMETER;
    if (Row->Address.si_family != AF_INET && Row->Address.si_family != AF_INET6)
        return ERROR_INVALID_PARAMETER;

    Error = resolve_interface(&Row->InterfaceLuid, Row->InterfaceIndex, Index);
    if (Error)
        return Error;

    if (!Create)
        return NO_ERROR;
    if (Row->ValidLifetime < Row->PreferredLifetime)
        return ERROR_INVALID_PARAMETER;
    if (Row->Address.si_family == AF_INET6)
    {
        if (IN6_IS_ADDR_MULTICAST(Address) || IN6_IS_ADDR_UNSPECIFIED(Address) || IN6_IS_ADDR_LOOPBACK(Address))
            return ERROR_INVALID_PARAMETER;
        if (Row->OnLinkPrefixLength != 0xFF && Row->OnLinkPrefixLength > 128)
            return ERROR_INVALID_PARAMETER;
    }
    else if (Row->OnLinkPrefixLength != 0xFF && Row->OnLinkPrefixLength > 32)
    {
        return ERROR_INVALID_PARAMETER;
    }
    return NO_ERROR;
}

static void fill_address_set(IP6_ADDRESS_SET *Set, ULONG Operation, NET_IFINDEX Index, const MIB_UNICASTIPADDRESS_ROW *Row)
{
    memset(Set, 0, sizeof(*Set));
    Set->Operation = Operation;
    Set->Index = Index;
    memcpy(Set->Address, &Row->Address.Ipv6.sin6_addr, sizeof(Set->Address));
    Set->PrefixLength = Row->OnLinkPrefixLength == 0xFF ? 128 : Row->OnLinkPrefixLength;
    Set->ValidLifetime = Row->ValidLifetime;
    Set->PreferredLifetime = Row->PreferredLifetime;
    Set->PrefixOrigin = Row->PrefixOrigin == IpPrefixOriginUnchanged ? IpPrefixOriginManual : Row->PrefixOrigin;
    Set->SuffixOrigin = Row->SuffixOrigin == IpSuffixOriginUnchanged ? IpSuffixOriginManual : Row->SuffixOrigin;
    Set->SkipAsSource = Row->SkipAsSource;
}

static IPMask ipv4_mask(UINT8 Length)
{
    if (Length == 0xFF)
        Length = 32;
    return Length ? htonl(0xFFFFFFFFu << (32 - Length)) : 0;
}

DWORD WINAPI CreateUnicastIpAddressEntry(const MIB_UNICASTIPADDRESS_ROW *Row)
{
    IP6_ADDRESS_SET Set;
    NET_IFINDEX Index;
    ULONG Context, Instance;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = validate_unicast(Row, &Index, TRUE);
    if (Error)
        return Error;

    if (Row->Address.si_family == AF_INET)
        return AddIPAddress(Row->Address.Ipv4.sin_addr.s_addr, ipv4_mask(Row->OnLinkPrefixLength), Index, &Context, &Instance);

    fill_address_set(&Set, Ip6SetAdd, Index, Row);
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_ADDRESS, &Set, sizeof(Set)), ERROR_FILE_NOT_FOUND);
}

DWORD WINAPI SetUnicastIpAddressEntry(const MIB_UNICASTIPADDRESS_ROW *Row)
{
    MIB_UNICASTIPADDRESS_ROW Current;
    IP6_ADDRESS_SET Set;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = validate_unicast(Row, &Index, FALSE);
    if (Error)
        return Error;
    if (Row->Address.si_family != AF_INET6)
        return ERROR_NOT_SUPPORTED;

    Current = *Row;
    Current.InterfaceLuid.Value = 0;
    Current.InterfaceIndex = Index;
    if (GetUnicastIpAddressEntry(&Current) != NO_ERROR)
        return ERROR_INVALID_PARAMETER;
    if (Row->ValidLifetime < Row->PreferredLifetime)
        return ERROR_INVALID_PARAMETER;

    fill_address_set(&Set, Ip6SetUpdate, Index, Row);
    if (Row->OnLinkPrefixLength == 0xFF)
        Set.PrefixLength = Current.OnLinkPrefixLength;
    if (Row->PrefixOrigin == IpPrefixOriginUnchanged)
        Set.PrefixOrigin = Current.PrefixOrigin;
    if (Row->SuffixOrigin == IpSuffixOriginUnchanged)
        Set.SuffixOrigin = Current.SuffixOrigin;
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_ADDRESS, &Set, sizeof(Set)), ERROR_INVALID_PARAMETER);
}

DWORD WINAPI DeleteUnicastIpAddressEntry(const MIB_UNICASTIPADDRESS_ROW *Row)
{
    IP6_ADDRESS_SET Set;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = validate_unicast(Row, &Index, FALSE);
    if (Error)
        return Error;

    if (Row->Address.si_family == AF_INET)
    {
        Error = DeleteIPAddress(Row->Address.Ipv4.sin_addr.s_addr);
        return Error == ERROR_NOT_FOUND ? ERROR_INVALID_PARAMETER : Error;
    }

    fill_address_set(&Set, Ip6SetDelete, Index, Row);
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_ADDRESS, &Set, sizeof(Set)), ERROR_INVALID_PARAMETER);
}

static DWORD validate_forward(const MIB_IPFORWARD_ROW2 *Row, NET_IFINDEX *Index)
{
    ADDRESS_FAMILY Family;
    DWORD Error;

    if (!Row)
        return ERROR_INVALID_PARAMETER;
    Family = Row->DestinationPrefix.Prefix.si_family;
    if (Family != AF_INET && Family != AF_INET6)
        return ERROR_INVALID_PARAMETER;
    if (Row->NextHop.si_family && Row->NextHop.si_family != Family)
        return ERROR_INVALID_PARAMETER;
    if (Row->DestinationPrefix.PrefixLength > (Family == AF_INET6 ? 128 : 32))
        return ERROR_INVALID_PARAMETER;
    if (Family == AF_INET6 && !ipv6_prefix_is_masked(&Row->DestinationPrefix.Prefix.Ipv6.sin6_addr,
                                                     Row->DestinationPrefix.PrefixLength))
        return ERROR_INVALID_PARAMETER;
    if (Family == AF_INET && Row->DestinationPrefix.PrefixLength < 32 &&
        (ntohl(Row->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr) &
         (Row->DestinationPrefix.PrefixLength ? 0xFFFFFFFFu >> Row->DestinationPrefix.PrefixLength : 0xFFFFFFFFu)))
        return ERROR_INVALID_PARAMETER;

    Error = resolve_interface(&Row->InterfaceLuid, Row->InterfaceIndex, Index);
    return Error;
}

static void fill_route_set(IP6_ROUTE_SET *Set, ULONG Operation, NET_IFINDEX Index, const MIB_IPFORWARD_ROW2 *Row)
{
    memset(Set, 0, sizeof(*Set));
    Set->Operation = Operation;
    Set->Index = Index;
    memcpy(Set->Prefix, &Row->DestinationPrefix.Prefix.Ipv6.sin6_addr, sizeof(Set->Prefix));
    Set->PrefixLength = Row->DestinationPrefix.PrefixLength;
    if (Row->NextHop.si_family == AF_INET6)
        memcpy(Set->NextHop, &Row->NextHop.Ipv6.sin6_addr, sizeof(Set->NextHop));
    Set->Metric = Row->Metric == 0xFFFFFFFF ? 256 : Row->Metric;
    Set->Protocol = Row->Protocol == (NL_ROUTE_PROTOCOL)-1 ? MIB_IPPROTO_NETMGMT : Row->Protocol;
    Set->ValidLifetime = Row->ValidLifetime;
    Set->PreferredLifetime = Row->PreferredLifetime;
    Set->Publish = Row->Publish == IPHLP_UNCHANGED_BOOLEAN ? FALSE : Row->Publish;
}

static void fill_ipv4_route(MIB_IPFORWARDROW *Route, NET_IFINDEX Index, const MIB_IPFORWARD_ROW2 *Row)
{
    memset(Route, 0, sizeof(*Route));
    Route->dwForwardDest = Row->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr;
    Route->dwForwardMask = ipv4_mask(Row->DestinationPrefix.PrefixLength);
    Route->dwForwardNextHop = Row->NextHop.si_family == AF_INET ? Row->NextHop.Ipv4.sin_addr.s_addr : 0;
    Route->dwForwardIfIndex = Index;
    Route->dwForwardType = Route->dwForwardNextHop ? MIB_IPROUTE_TYPE_INDIRECT : MIB_IPROUTE_TYPE_DIRECT;
    Route->dwForwardProto = MIB_IPPROTO_NETMGMT;
    Route->dwForwardMetric1 = Row->Metric == 0xFFFFFFFF ? 256 : Row->Metric;
}

static BOOL forward_matches(const MIB_IPFORWARD_ROW2 *Entry, NET_IFINDEX Index, const MIB_IPFORWARD_ROW2 *Row)
{
    ADDRESS_FAMILY Family = Row->DestinationPrefix.Prefix.si_family;

    if (Entry->InterfaceIndex != Index || Entry->DestinationPrefix.PrefixLength != Row->DestinationPrefix.PrefixLength)
        return FALSE;
    if (Family == AF_INET6)
    {
        static const IN6_ADDR Any;
        const IN6_ADDR *NextHop = Row->NextHop.si_family == AF_INET6 ? &Row->NextHop.Ipv6.sin6_addr : &Any;

        return !memcmp(&Entry->DestinationPrefix.Prefix.Ipv6.sin6_addr, &Row->DestinationPrefix.Prefix.Ipv6.sin6_addr, sizeof(IN6_ADDR)) &&
               !memcmp(&Entry->NextHop.Ipv6.sin6_addr, NextHop, sizeof(IN6_ADDR));
    }
    return Entry->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr == Row->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr &&
           Entry->NextHop.Ipv4.sin_addr.s_addr == (Row->NextHop.si_family == AF_INET ? Row->NextHop.Ipv4.sin_addr.s_addr : 0);
}

DWORD WINAPI GetIpForwardEntry2(PMIB_IPFORWARD_ROW2 Row)
{
    PMIB_IPFORWARD_TABLE2 Table;
    NET_IFINDEX Index;
    DWORD Error;
    ULONG i;

    TRACE("(%p)\n", Row);

    Error = validate_forward(Row, &Index);
    if (Error)
        return Error;
    Error = GetIpForwardTable2(Row->DestinationPrefix.Prefix.si_family, &Table);
    if (Error)
        return Error;
    Error = ERROR_NOT_FOUND;
    for (i = 0; i < Table->NumEntries; i++)
    {
        if (forward_matches(&Table->Table[i], Index, Row))
        {
            *Row = Table->Table[i];
            Error = NO_ERROR;
            break;
        }
    }
    FreeMibTable(Table);
    return Error;
}

DWORD WINAPI CreateIpForwardEntry2(const MIB_IPFORWARD_ROW2 *Row)
{
    MIB_IPFORWARDROW Route;
    IP6_ROUTE_SET Set;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = validate_forward(Row, &Index);
    if (Error)
        return Error;

    if (Row->DestinationPrefix.Prefix.si_family == AF_INET)
    {
        MIB_IPFORWARD_ROW2 Existing = *Row;

        if (GetIpForwardEntry2(&Existing) == NO_ERROR)
            return ERROR_OBJECT_ALREADY_EXISTS;
        fill_ipv4_route(&Route, Index, Row);
        Error = CreateIpForwardEntry(&Route);
        if (Error == NO_ERROR && Row->ValidLifetime != 0xFFFFFFFF)
            ip4_route_set_lifetime(&Route, Row->ValidLifetime);
        return Error;
    }

    fill_route_set(&Set, Ip6SetAdd, Index, Row);
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_ROUTE, &Set, sizeof(Set)), ERROR_FILE_NOT_FOUND);
}

DWORD WINAPI SetIpForwardEntry2(const MIB_IPFORWARD_ROW2 *Row)
{
    MIB_IPFORWARD_ROW2 Current;
    MIB_IPFORWARDROW Route;
    IP6_ROUTE_SET Set;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = validate_forward(Row, &Index);
    if (Error)
        return Error;
    Current = *Row;
    Error = GetIpForwardEntry2(&Current);
    if (Error)
        return Error;

    if (Row->DestinationPrefix.Prefix.si_family == AF_INET)
    {
        fill_ipv4_route(&Route, Index, Row);
        return SetIpForwardEntry(&Route);
    }

    fill_route_set(&Set, Ip6SetUpdate, Index, Row);
    if (Row->Metric == 0xFFFFFFFF)
        Set.Metric = Current.Metric;
    if (Row->Publish == IPHLP_UNCHANGED_BOOLEAN)
        Set.Publish = Current.Publish;
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_ROUTE, &Set, sizeof(Set)), ERROR_NOT_FOUND);
}

DWORD WINAPI DeleteIpForwardEntry2(const MIB_IPFORWARD_ROW2 *Row)
{
    MIB_IPFORWARDROW Route;
    IP6_ROUTE_SET Set;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = validate_forward(Row, &Index);
    if (Error)
        return Error;

    if (Row->DestinationPrefix.Prefix.si_family == AF_INET)
    {
        MIB_IPFORWARD_ROW2 Existing = *Row;

        if (GetIpForwardEntry2(&Existing) != NO_ERROR)
            return ERROR_NOT_FOUND;
        fill_ipv4_route(&Route, Index, Row);
        return DeleteIpForwardEntry(&Route);
    }

    fill_route_set(&Set, Ip6SetDelete, Index, Row);
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_ROUTE, &Set, sizeof(Set)), ERROR_NOT_FOUND);
}

#define IPHLP_UNCHANGED_ULONG 0xFFFFFFFF

static BOOL interface_value_changed(ULONG Requested, ULONG Current, BOOL ZeroUnchanged)
{
    if (Requested == IPHLP_UNCHANGED_ULONG || (ZeroUnchanged && !Requested))
        return FALSE;
    return Requested != Current;
}

static BOOL interface_flag_changed(BOOLEAN Requested, BOOLEAN Current)
{
    return Requested != IPHLP_UNCHANGED_BOOLEAN && !Requested != !Current;
}

static BOOLEAN interface_flag(BOOLEAN Requested, BOOLEAN Current)
{
    return Requested == IPHLP_UNCHANGED_BOOLEAN ? Current : !!Requested;
}

static ULONG interface_value(ULONG Requested, ULONG Current, BOOL ZeroUnchanged)
{
    return interface_value_changed(Requested, Current, ZeroUnchanged) ? Requested : Current;
}

static void interface_zones(const MIB_IPINTERFACE_ROW *Row, const MIB_IPINTERFACE_ROW *Current, ULONG *Zones)
{
    UINT Level;

    for (Level = 0; Level < ScopeLevelCount; Level++)
    {
        ULONG Zone = Row->ZoneIndices[Level] == IPHLP_UNCHANGED_ULONG ? Current->ZoneIndices[Level] : Row->ZoneIndices[Level];

        Zones[Level] = Zone == Current->ZoneIndices[Level] ? 0 : Zone;
    }
}

static HKEY ip4_interface_key(NET_IFINDEX Index, REGSAM Access)
{
    WCHAR Path[128];
    NET_LUID Luid;
    GUID Guid;
    HKEY Key;
    LSTATUS Error;

    if (ConvertInterfaceIndexToLuid(Index, &Luid) != NO_ERROR || ConvertInterfaceLuidToGuid(&Luid, &Guid) != NO_ERROR)
        return NULL;
    _snwprintf(Path, ARRAYSIZE(Path),
               L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\"
               L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               Guid.Data1, Guid.Data2, Guid.Data3, Guid.Data4[0], Guid.Data4[1], Guid.Data4[2],
               Guid.Data4[3], Guid.Data4[4], Guid.Data4[5], Guid.Data4[6], Guid.Data4[7]);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
    if (Access & KEY_SET_VALUE)
        Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, Path, 0, NULL, 0, Access, NULL, &Key, NULL);
    else
        Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, Access, &Key);
    return Error == ERROR_SUCCESS ? Key : NULL;
}

static BOOLEAN ip4_dhcpv6_flag(HKEY Key, PCWSTR Name)
{
    DWORD Value = TRUE, Size = sizeof(Value);

    if (Key)
        RegGetValueW(Key, NULL, Name, RRF_RT_REG_DWORD, NULL, &Value, &Size);
    return Value != 0;
}

static DWORD ip4_set_dhcpv6_flags(NET_IFINDEX Index, BOOLEAN Managed, BOOLEAN Other)
{
    DWORD ManagedValue = Managed, OtherValue = Other;
    HKEY Key = ip4_interface_key(Index, KEY_SET_VALUE);
    LSTATUS Error;

    if (!Key)
        return ERROR_ACCESS_DENIED;
    Error = RegSetValueExW(Key, L"ManagedAddressConfiguration", 0, REG_DWORD, (BYTE *)&ManagedValue, sizeof(DWORD));
    if (Error == ERROR_SUCCESS)
        Error = RegSetValueExW(Key, L"OtherStatefulConfiguration", 0, REG_DWORD, (BYTE *)&OtherValue, sizeof(DWORD));
    RegCloseKey(Key);
    return Error;
}

DWORD WINAPI SetIpInterfaceEntry(PMIB_IPINTERFACE_ROW Row)
{
    MIB_IPINTERFACE_ROW Current;
    BOOLEAN Automatic, Advertising, Managed, Other;
    NET_IFINDEX Index;
    ULONG Metric;
    DWORD Error;

    TRACE("(%p)\n", Row);

    if (!Row || (Row->Family != AF_INET && Row->Family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    if (!Row->InterfaceLuid.Value && !Row->InterfaceIndex)
        return ERROR_INVALID_PARAMETER;
    Error = resolve_interface(&Row->InterfaceLuid, Row->InterfaceIndex, &Index);
    if (Error)
        return Error;

    InitializeIpInterfaceEntry(&Current);
    Current.Family = Row->Family;
    Current.InterfaceIndex = Index;
    Error = GetIpInterfaceEntry(&Current);
    if (Error)
        return Error == ERROR_NOT_FOUND ? ERROR_FILE_NOT_FOUND : Error;

    if (Row->Family == AF_INET && Row->SitePrefixLength && Row->SitePrefixLength != IPHLP_UNCHANGED_ULONG)
        return ERROR_INVALID_PARAMETER;
    if (Row->RouterDiscoveryBehavior != RouterDiscoveryUnchanged &&
        (ULONG)Row->RouterDiscoveryBehavior > RouterDiscoveryDhcp)
        return ERROR_INVALID_PARAMETER;
    if (interface_flag_changed(Row->UseNeighborUnreachabilityDetection, Current.UseNeighborUnreachabilityDetection) ||
        interface_value_changed(Row->LinkLocalAddressBehavior, Current.LinkLocalAddressBehavior, FALSE))
        return ERROR_INVALID_PARAMETER;
    if (Row->NlMtu && Row->NlMtu != IPHLP_UNCHANGED_ULONG && Row->NlMtu < (Row->Family == AF_INET6 ? 1280u : 576u))
        return ERROR_INVALID_PARAMETER;

    Advertising = interface_flag(Row->AdvertisingEnabled, Current.AdvertisingEnabled);
    Managed = interface_flag(Row->ManagedAddressConfigurationSupported, Current.ManagedAddressConfigurationSupported);
    Other = interface_flag(Row->OtherStatefulConfigurationSupported, Current.OtherStatefulConfigurationSupported);
    if (Row->Family == AF_INET && Advertising)
        return ERROR_INVALID_PARAMETER;
    if (Row->Family == AF_INET6 && !Advertising && !Current.AdvertisingEnabled &&
        (Managed != Current.ManagedAddressConfigurationSupported || Other != Current.OtherStatefulConfigurationSupported))
        return ERROR_INVALID_PARAMETER;

    Automatic = interface_flag(Row->UseAutomaticMetric, Current.UseAutomaticMetric);
    if (Automatic)
        Metric = 0;
    else if (Row->Metric == IPHLP_UNCHANGED_ULONG ||
             (Row->UseAutomaticMetric == IPHLP_UNCHANGED_BOOLEAN && Current.UseAutomaticMetric))
        Metric = Current.Metric;
    else
        Metric = Row->Metric;

    if (Row->Family == AF_INET)
    {
        IP4_INTERFACE_INFO Set;

        memset(&Set, 0, sizeof(Set));
        Set.Index = Index;
        Set.UseAutomaticMetric = Automatic;
        Set.Metric = Metric;
        Set.Mtu = interface_value(Row->NlMtu, Current.NlMtu, TRUE);
        Set.RetransmitTime = interface_value(Row->RetransmitTime, Current.RetransmitTime, TRUE);
        Set.BaseReachableTime = interface_value(Row->BaseReachableTime, Current.BaseReachableTime, TRUE);
        Set.DadTransmits = interface_value(Row->DadTransmits, Current.DadTransmits, FALSE);
        Set.RouterDiscoveryBehavior = interface_value(Row->RouterDiscoveryBehavior, Current.RouterDiscoveryBehavior, FALSE);
        Set.PathMtuDiscoveryTimeout = interface_value(Row->PathMtuDiscoveryTimeout, Current.PathMtuDiscoveryTimeout, TRUE);
        Set.LinkLocalAddressTimeout = interface_value(Row->LinkLocalAddressTimeout, Current.LinkLocalAddressTimeout, FALSE);
        Set.SitePrefixLength = Row->SitePrefixLength == IPHLP_UNCHANGED_ULONG ? Current.SitePrefixLength : 0;
        interface_zones(Row, &Current, Set.ZoneIndices);
        Set.Forwarding = interface_flag(Row->ForwardingEnabled, Current.ForwardingEnabled);
        Set.WeakHostSend = interface_flag(Row->WeakHostSend, Current.WeakHostSend);
        Set.WeakHostReceive = interface_flag(Row->WeakHostReceive, Current.WeakHostReceive);
        Set.DisableDefaultRoutes = interface_flag(Row->DisableDefaultRoutes, Current.DisableDefaultRoutes);
        Set.AdvertiseDefaultRoute = interface_flag(Row->AdvertiseDefaultRoute, Current.AdvertiseDefaultRoute);
        Error = ip4_interface_set(&Set);
        if (Error == NO_ERROR &&
            (Managed != Current.ManagedAddressConfigurationSupported || Other != Current.OtherStatefulConfigurationSupported))
        {
            Error = ip4_set_dhcpv6_flags(Index, Managed, Other);
        }
    }
    else
    {
        IP6_INTERFACE_SET Set;

        memset(&Set, 0, sizeof(Set));
        Set.Index = Index;
        Set.UseAutomaticMetric = Automatic;
        Set.Metric = Metric;
        Set.Mtu = (!Row->NlMtu || Row->NlMtu == IPHLP_UNCHANGED_ULONG) ? 0 : Row->NlMtu;
        Set.RouterDiscoveryBehavior = interface_value(Row->RouterDiscoveryBehavior, Current.RouterDiscoveryBehavior, FALSE);
        Set.RouterDiscovery = Set.RouterDiscoveryBehavior != RouterDiscoveryDisabled;
        Set.DadTransmits = interface_value(Row->DadTransmits, Current.DadTransmits, FALSE);
        Set.BaseReachableTime = interface_value(Row->BaseReachableTime, Current.BaseReachableTime, TRUE);
        Set.RetransmitTime = interface_value(Row->RetransmitTime, Current.RetransmitTime, TRUE);
        Set.SitePrefixLength = interface_value(Row->SitePrefixLength, Current.SitePrefixLength, FALSE);
        Set.PathMtuDiscoveryTimeout = interface_value(Row->PathMtuDiscoveryTimeout, Current.PathMtuDiscoveryTimeout, TRUE);
        Set.LinkLocalAddressTimeout = interface_value(Row->LinkLocalAddressTimeout, Current.LinkLocalAddressTimeout, FALSE);
        interface_zones(Row, &Current, Set.ZoneIndices);
        Set.Forwarding = interface_flag(Row->ForwardingEnabled, Current.ForwardingEnabled);
        Set.WeakHostSend = interface_flag(Row->WeakHostSend, Current.WeakHostSend);
        Set.WeakHostReceive = interface_flag(Row->WeakHostReceive, Current.WeakHostReceive);
        Set.Advertising = Advertising;
        Set.AdvertiseDefaultRoute = interface_flag(Row->AdvertiseDefaultRoute, Current.AdvertiseDefaultRoute);
        Set.DisableDefaultRoutes = interface_flag(Row->DisableDefaultRoutes, Current.DisableDefaultRoutes);
        Set.ManagedAddressConfiguration = Managed;
        Set.OtherStatefulConfiguration = Other;
        if (Set.SitePrefixLength > 128)
            return ERROR_INVALID_PARAMETER;
        Error = ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_INTERFACE, &Set, sizeof(Set)), ERROR_FILE_NOT_FOUND);
    }
    if (Error == NO_ERROR)
        ConvertInterfaceIndexToLuid(Index, &Row->InterfaceLuid);
    return Error;
}

static DWORD fill_neighbor_set(IP6_NEIGHBOR_SET *Set, ULONG Operation, const MIB_IPNET_ROW2 *Row, NET_IFINDEX *Index)
{
    DWORD Error;

    if (!Row || (Row->Address.si_family != AF_INET && Row->Address.si_family != AF_INET6))
        return ERROR_INVALID_PARAMETER;
    Error = resolve_interface(&Row->InterfaceLuid, Row->InterfaceIndex, Index);
    if (Error)
        return Error;
    if (Row->Address.si_family == AF_INET6 &&
        (IN6_IS_ADDR_UNSPECIFIED(&Row->Address.Ipv6.sin6_addr) || IN6_IS_ADDR_LOOPBACK(&Row->Address.Ipv6.sin6_addr)))
        return ERROR_INVALID_PARAMETER;

    memset(Set, 0, sizeof(*Set));
    Set->Operation = Operation;
    Set->Index = *Index;
    memcpy(Set->Address, &Row->Address.Ipv6.sin6_addr, sizeof(Set->Address));
    Set->PhysicalAddressLength = min(Row->PhysicalAddressLength, sizeof(Set->PhysicalAddress));
    memcpy(Set->PhysicalAddress, Row->PhysicalAddress, Set->PhysicalAddressLength);
    Set->Permanent = TRUE;
    return NO_ERROR;
}

static void fill_ipv4_neighbor(MIB_IPNETROW *Entry, NET_IFINDEX Index, const MIB_IPNET_ROW2 *Row)
{
    memset(Entry, 0, sizeof(*Entry));
    Entry->dwIndex = Index;
    Entry->dwPhysAddrLen = min(Row->PhysicalAddressLength, MAXLEN_PHYSADDR);
    memcpy(Entry->bPhysAddr, Row->PhysicalAddress, Entry->dwPhysAddrLen);
    Entry->dwAddr = Row->Address.Ipv4.sin_addr.s_addr;
    Entry->dwType = MIB_IPNET_TYPE_STATIC;
}

static BOOL neighbor_exists(const MIB_IPNET_ROW2 *Row, NET_IFINDEX Index)
{
    MIB_IPNET_ROW2 Existing = *Row;

    Existing.InterfaceLuid.Value = 0;
    Existing.InterfaceIndex = Index;
    return GetIpNetEntry2(&Existing) == NO_ERROR;
}

DWORD WINAPI CreateIpNetEntry2(const MIB_IPNET_ROW2 *Row)
{
    IP6_NEIGHBOR_SET Set;
    MIB_IPNETROW Entry;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = fill_neighbor_set(&Set, Ip6SetAdd, Row, &Index);
    if (Error)
        return Error;
    if (Row->Address.si_family == AF_INET)
    {
        if (neighbor_exists(Row, Index))
            return ERROR_OBJECT_ALREADY_EXISTS;
        fill_ipv4_neighbor(&Entry, Index, Row);
        return CreateIpNetEntry(&Entry);
    }
    if (IN6_IS_ADDR_MULTICAST(&Row->Address.Ipv6.sin6_addr))
        return NO_ERROR;
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_NEIGHBOR, &Set, sizeof(Set)), ERROR_FILE_NOT_FOUND);
}

DWORD WINAPI SetIpNetEntry2(PMIB_IPNET_ROW2 Row)
{
    IP6_NEIGHBOR_SET Set;
    MIB_IPNETROW Entry;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = fill_neighbor_set(&Set, Ip6SetUpdate, Row, &Index);
    if (Error)
        return Error;
    if (Row->Address.si_family == AF_INET)
    {
        if (!neighbor_exists(Row, Index))
            return ERROR_NOT_FOUND;
        fill_ipv4_neighbor(&Entry, Index, Row);
        return SetIpNetEntry(&Entry);
    }
    if (IN6_IS_ADDR_MULTICAST(&Row->Address.Ipv6.sin6_addr))
        return neighbor_exists(Row, Index) ? NO_ERROR : ERROR_NOT_FOUND;
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_NEIGHBOR, &Set, sizeof(Set)), ERROR_NOT_FOUND);
}

DWORD WINAPI DeleteIpNetEntry2(const MIB_IPNET_ROW2 *Row)
{
    IP6_NEIGHBOR_SET Set;
    MIB_IPNETROW Entry;
    NET_IFINDEX Index;
    DWORD Error;

    TRACE("(%p)\n", Row);

    Error = fill_neighbor_set(&Set, Ip6SetDelete, Row, &Index);
    if (Error)
        return Error;
    if (Row->Address.si_family == AF_INET)
    {
        if (!neighbor_exists(Row, Index))
            return ERROR_NOT_FOUND;
        fill_ipv4_neighbor(&Entry, Index, Row);
        return DeleteIpNetEntry(&Entry);
    }
    return ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_NEIGHBOR, &Set, sizeof(Set)), ERROR_NOT_FOUND);
}

DWORD WINAPI FlushIpNetTable2(ADDRESS_FAMILY Family, NET_IFINDEX InterfaceIndex)
{
    IP6_NEIGHBOR_SET Set;
    NET_LUID Luid;
    DWORD Error;

    TRACE("(%u %lu)\n", Family, InterfaceIndex);

    if (Family != AF_UNSPEC && Family != AF_INET && Family != AF_INET6)
        return ERROR_INVALID_PARAMETER;
    if (InterfaceIndex && ConvertInterfaceIndexToLuid(InterfaceIndex, &Luid) != NO_ERROR)
        return ERROR_FILE_NOT_FOUND;

    if (Family != AF_INET6)
    {
        PMIB_IPNETTABLE Table = NULL;
        ULONG Size = 0, i, j;

        if (GetIpNetTable(NULL, &Size, FALSE) == ERROR_INSUFFICIENT_BUFFER && (Table = HeapAlloc(GetProcessHeap(), 0, Size)))
        {
            if (GetIpNetTable(Table, &Size, FALSE) == NO_ERROR)
            {
                for (i = 0; i < Table->dwNumEntries; i++)
                {
                    NET_IFINDEX Index = Table->table[i].dwIndex;

                    if (InterfaceIndex && Index != InterfaceIndex)
                        continue;
                    for (j = 0; j < i && Table->table[j].dwIndex != Index; j++);
                    if (j == i)
                        FlushIpNetTable(Index);
                }
            }
            HeapFree(GetProcessHeap(), 0, Table);
        }
    }
    if (Family == AF_INET)
        return NO_ERROR;

    memset(&Set, 0, sizeof(Set));
    Set.Operation = Ip6SetFlush;
    Set.Index = InterfaceIndex;
    Error = ip6_status_to_error(ip6_ioctl(IOCTL_IP6_SET_NEIGHBOR, &Set, sizeof(Set)), ERROR_FILE_NOT_FOUND);
    return Error;
}

static BOOL ip4_interface_info(NET_IFINDEX Index, IP4_INTERFACE_INFO *Info)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Ip");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE File;

    memset(Info, 0, sizeof(*Info));
    Info->Index = Index;
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenFile(&File, SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               FILE_SYNCHRONOUS_IO_NONALERT)))
        return FALSE;
    Status = NtDeviceIoControlFile(File, NULL, NULL, NULL, &Iosb, IOCTL_IP_QUERY_INTERFACE, Info, sizeof(*Info),
                                   Info, sizeof(*Info));
    NtClose(File);
    return NT_SUCCESS(Status);
}

static BOOL ip6_interface_row(NET_IFINDEX Index, IP6_INTERFACE_ROW *Row)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Ip6");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    IP6_TABLE_HEADER *Header;
    const IP6_INTERFACE_ROW *Rows;
    ULONG Size = sizeof(IP6_TABLE_HEADER) + 32 * sizeof(IP6_INTERFACE_ROW), i;
    BOOL Found = FALSE;
    HANDLE File;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenFile(&File, SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               FILE_SYNCHRONOUS_IO_NONALERT)))
        return FALSE;
    Header = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Header)
    {
        *(ULONG *)Header = Ip6TableInterfaces;
        if (NT_SUCCESS(NtDeviceIoControlFile(File, NULL, NULL, NULL, &Iosb, IOCTL_IP6_QUERY_TABLE, Header, sizeof(ULONG),
                                             Header, Size)))
        {
            Rows = (const IP6_INTERFACE_ROW *)(Header + 1);
            for (i = 0; i < Header->Count && !Found; i++)
            {
                if (Rows[i].Index != Index)
                    continue;
                *Row = Rows[i];
                Found = TRUE;
            }
        }
        HeapFree(GetProcessHeap(), 0, Header);
    }
    NtClose(File);
    return Found;
}

VOID ip_interface_extras(ADDRESS_FAMILY Family, MIB_IPINTERFACE_ROW *Row)
{
    IP6_INTERFACE_ROW Row6;
    IP4_INTERFACE_INFO Info4;

    Row->UseAutomaticMetric = TRUE;
    Row->UseNeighborUnreachabilityDetection = TRUE;
    Row->ManagedAddressConfigurationSupported = Family == AF_INET;
    Row->OtherStatefulConfigurationSupported = Family == AF_INET;
    Row->ForwardingEnabled = FALSE;
    Row->WeakHostSend = FALSE;
    Row->WeakHostReceive = FALSE;
    Row->AdvertisingEnabled = FALSE;
    Row->AdvertiseDefaultRoute = FALSE;
    Row->DisableDefaultRoutes = FALSE;
    if (Family == AF_INET6 && ip6_interface_row(Row->InterfaceIndex, &Row6))
    {
        Row->UseAutomaticMetric = Row6.UseAutomaticMetric;
        Row->ManagedAddressConfigurationSupported = Row6.ManagedAddressConfiguration;
        Row->OtherStatefulConfigurationSupported = Row6.OtherStatefulConfiguration;
        Row->ForwardingEnabled = Row6.Forwarding;
        Row->WeakHostSend = Row6.WeakHostSend;
        Row->WeakHostReceive = Row6.WeakHostReceive;
        Row->AdvertisingEnabled = Row6.Advertising;
        Row->AdvertiseDefaultRoute = Row6.AdvertiseDefaultRoute;
        Row->DisableDefaultRoutes = Row6.DisableDefaultRoutes;
    }
    else if (Family == AF_INET && ip4_interface_info(Row->InterfaceIndex, &Info4))
    {
        HKEY Key = ip4_interface_key(Row->InterfaceIndex, KEY_QUERY_VALUE);

        Row->UseAutomaticMetric = Info4.UseAutomaticMetric;
        Row->ManagedAddressConfigurationSupported = ip4_dhcpv6_flag(Key, L"ManagedAddressConfiguration");
        Row->OtherStatefulConfigurationSupported = ip4_dhcpv6_flag(Key, L"OtherStatefulConfiguration");
        if (Key)
            RegCloseKey(Key);
        Row->ForwardingEnabled = Info4.Forwarding;
        Row->WeakHostSend = Info4.WeakHostSend;
        Row->WeakHostReceive = Info4.WeakHostReceive;
        Row->AdvertisingEnabled = FALSE;
        Row->AdvertiseDefaultRoute = Info4.AdvertiseDefaultRoute;
        Row->DisableDefaultRoutes = Info4.DisableDefaultRoutes;
    }
}

static NTSTATUS path_device_ioctl(ADDRESS_FAMILY Family, ULONG Code, PVOID Output, ULONG OutputLength)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    HANDLE File;

    RtlInitUnicodeString(&Name, Family == AF_INET6 ? L"\\Device\\Ip6" : L"\\Device\\Ip");
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&File, SYNCHRONIZE, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = NtDeviceIoControlFile(File, NULL, NULL, NULL, &Iosb, Code, NULL, 0, Output, OutputLength);
    NtClose(File);
    return Status;
}

static DWORD path_query(ADDRESS_FAMILY Family, IP_PATH_TABLE **Result)
{
    ULONG Capacity = 64, Size;
    IP_PATH_TABLE *Table;
    NTSTATUS Status;

    for (;;)
    {
        Size = FIELD_OFFSET(IP_PATH_TABLE, Rows) + Capacity * sizeof(IP_PATH_ROW);
        Table = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (!Table)
            return ERROR_NOT_ENOUGH_MEMORY;
        Status = path_device_ioctl(Family, IOCTL_IP_QUERY_PATHS, Table, Size);
        if (Status != STATUS_BUFFER_OVERFLOW)
            break;
        Capacity = Table->NumEntries + 16;
        HeapFree(GetProcessHeap(), 0, Table);
    }
    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Table);
        return RtlNtStatusToDosError(Status);
    }
    *Result = Table;
    return NO_ERROR;
}

static VOID path_address(ADDRESS_FAMILY Family, const UCHAR *Address, SOCKADDR_INET *Result)
{
    memset(Result, 0, sizeof(*Result));
    Result->si_family = Family;
    if (Family == AF_INET6)
        memcpy(&Result->Ipv6.sin6_addr, Address, sizeof(IN6_ADDR));
    else
        memcpy(&Result->Ipv4.sin_addr, Address, sizeof(IN_ADDR));
}

static VOID path_row(ADDRESS_FAMILY Family, const IP_PATH_ROW *Path, MIB_IPPATH_ROW *Row)
{
    memset(Row, 0, sizeof(*Row));
    path_address(Family, Path->Source, &Row->Source);
    path_address(Family, Path->Destination, &Row->Destination);
    path_address(Family, Path->NextHop, &Row->CurrentNextHop);
    Row->InterfaceIndex = Path->Index;
    ConvertInterfaceIndexToLuid(Path->Index, &Row->InterfaceLuid);
    Row->PathMtu = Path->PathMtu;
    Row->RttMean = 3000;
    Row->RttDeviation = 0;
    Row->LastReachable = Path->LastReachable;
    Row->IsReachable = Path->IsReachable;
    Row->LinkTransmitSpeed = Path->LinkSpeed;
    Row->LinkReceiveSpeed = Path->LinkSpeed;
}

DWORD WINAPI GetIpPathTable(ADDRESS_FAMILY Family, PMIB_IPPATH_TABLE *Table)
{
    static const ADDRESS_FAMILY Families[] = { AF_INET, AF_INET6 };
    IP_PATH_TABLE *Paths[ARRAYSIZE(Families)] = { NULL };
    MIB_IPPATH_TABLE *Result;
    ULONG Count = 0, i, j;
    DWORD Error = NO_ERROR;

    TRACE("(%u %p)\n", Family, Table);

    if (!Table || (Family != AF_UNSPEC && Family != AF_INET && Family != AF_INET6))
        return ERROR_INVALID_PARAMETER;

    for (i = 0; i < ARRAYSIZE(Families) && !Error; i++)
    {
        if (Family != AF_UNSPEC && Family != Families[i])
            continue;
        Error = path_query(Families[i], &Paths[i]);
        if (!Error)
            Count += Paths[i]->NumEntries;
    }
    if (!Error)
    {
        Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, FIELD_OFFSET(MIB_IPPATH_TABLE, Table[Count]));
        if (Result)
        {
            for (i = 0; i < ARRAYSIZE(Families); i++)
            {
                for (j = 0; Paths[i] && j < Paths[i]->NumEntries; j++)
                    path_row(Families[i], &Paths[i]->Rows[j], &Result->Table[Result->NumEntries++]);
            }
            *Table = Result;
        }
        else
            Error = ERROR_NOT_ENOUGH_MEMORY;
    }
    for (i = 0; i < ARRAYSIZE(Families); i++)
        HeapFree(GetProcessHeap(), 0, Paths[i]);
    return Error;
}

DWORD WINAPI GetIpPathEntry(PMIB_IPPATH_ROW Row)
{
    ADDRESS_FAMILY Family;
    IP_PATH_TABLE *Paths;
    NET_IFINDEX Index;
    DWORD Error;
    ULONG i;

    TRACE("(%p)\n", Row);

    if (!Row)
        return ERROR_INVALID_PARAMETER;
    Family = Row->Destination.si_family;
    if ((Family != AF_INET && Family != AF_INET6) || (!Row->InterfaceLuid.Value && !Row->InterfaceIndex))
        return ERROR_INVALID_PARAMETER;
    Error = resolve_interface(&Row->InterfaceLuid, Row->InterfaceIndex, &Index);
    if (Error)
        return Error;
    if (Row->Source.si_family != AF_UNSPEC)
        return ERROR_NOT_FOUND;
    Error = path_query(Family, &Paths);
    if (Error)
        return Error;
    Error = ERROR_NOT_FOUND;
    for (i = 0; i < Paths->NumEntries; i++)
    {
        const IP_PATH_ROW *Path = &Paths->Rows[i];
        BOOL Same = Family == AF_INET6 ?
                    !memcmp(Path->Destination, &Row->Destination.Ipv6.sin6_addr, sizeof(IN6_ADDR)) :
                    !memcmp(Path->Destination, &Row->Destination.Ipv4.sin_addr, sizeof(IN_ADDR));

        if (Path->Index == Index && Same)
        {
            path_row(Family, Path, Row);
            Error = NO_ERROR;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, Paths);
    return Error;
}

DWORD WINAPI FlushIpPathTable(ADDRESS_FAMILY Family)
{
    NTSTATUS Status = STATUS_SUCCESS;

    TRACE("(%u)\n", Family);

    if (Family != AF_UNSPEC && Family != AF_INET && Family != AF_INET6)
        return ERROR_INVALID_PARAMETER;
    if (Family != AF_INET6)
        Status = path_device_ioctl(AF_INET, IOCTL_IP_FLUSH_PATHS, NULL, 0);
    if (NT_SUCCESS(Status) && Family != AF_INET)
        Status = path_device_ioctl(AF_INET6, IOCTL_IP_FLUSH_PATHS, NULL, 0);
    return NT_SUCCESS(Status) ? NO_ERROR : RtlNtStatusToDosError(Status);
}
