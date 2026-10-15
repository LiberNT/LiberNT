/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-mode IP Helper entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <netiodef.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#define IPHLPAPI_DLL_LINKAGE
#include <netioapi.h>
#include <tdiinfo.h>
#include <tcpioctl.h>

#define NETIO_IPHLP_TAG 'hIeN'
#define NETIO_MAX_ENTITIES 256
#define NETIO_MAX_ROUTES 128

typedef struct _NETIO_TDI_ROUTE
{
    ULONG Dest;
    ULONG Index;
    ULONG Metric1;
    ULONG Metric2;
    ULONG Metric3;
    ULONG Metric4;
    ULONG NextHop;
    ULONG Type;
    ULONG Proto;
    ULONG Age;
    ULONG Mask;
    ULONG Metric5;
    ULONG Info;
} NETIO_TDI_ROUTE, *PNETIO_TDI_ROUTE;

C_ASSERT(sizeof(MIB_IPFORWARD_ROW2) == 0x68);
C_ASSERT(FIELD_OFFSET(MIB_IPFORWARD_ROW2, SitePrefixLength) == 0x48);
C_ASSERT(FIELD_OFFSET(MIB_IPFORWARD_ROW2, ValidLifetime) == 0x4C);
C_ASSERT(FIELD_OFFSET(MIB_IPFORWARD_ROW2, Loopback) == 0x5C);
C_ASSERT(sizeof(MIB_UNICASTIPADDRESS_ROW) == 0x50);
C_ASSERT(FIELD_OFFSET(MIB_UNICASTIPADDRESS_ROW, PrefixOrigin) == 0x2C);
C_ASSERT(FIELD_OFFSET(MIB_UNICASTIPADDRESS_ROW, OnLinkPrefixLength) == 0x3C);
C_ASSERT(sizeof(MIB_IPINTERFACE_ROW) == 0xA8);
C_ASSERT(FIELD_OFFSET(MIB_IPINTERFACE_ROW, BaseReachableTime) == 0x3C);
C_ASSERT(FIELD_OFFSET(MIB_IPINTERFACE_ROW, NlMtu) == 0x98);

VOID
NTAPI
InitializeIpForwardEntry(
    _Out_ PMIB_IPFORWARD_ROW2 Row)
{
    RtlZeroMemory(Row, sizeof(*Row));
    Row->SitePrefixLength = 0xFF;
    Row->ValidLifetime = 0xFFFFFFFF;
    Row->PreferredLifetime = 0xFFFFFFFF;
    Row->Metric = 0xFFFFFFFF;
    Row->Protocol = (NL_ROUTE_PROTOCOL)0xFFFFFFFF;
    Row->Loopback = (BOOLEAN)0xFF;
    Row->AutoconfigureAddress = (BOOLEAN)0xFF;
    Row->Publish = (BOOLEAN)0xFF;
    Row->Immortal = (BOOLEAN)0xFF;
}

VOID
NTAPI
InitializeUnicastIpAddressEntry(
    _Out_ PMIB_UNICASTIPADDRESS_ROW Row)
{
    RtlZeroMemory(Row, sizeof(*Row));
    Row->PrefixOrigin = IpPrefixOriginUnchanged;
    Row->SuffixOrigin = IpSuffixOriginUnchanged;
    Row->ValidLifetime = 0xFFFFFFFF;
    Row->PreferredLifetime = 0xFFFFFFFF;
    Row->OnLinkPrefixLength = 0xFF;
}

VOID
NTAPI
InitializeIpInterfaceEntry(
    _Inout_ PMIB_IPINTERFACE_ROW Row)
{
    RtlFillMemory(Row, sizeof(*Row), 0xFF);
    Row->Family = AF_UNSPEC;
    Row->InterfaceLuid.Value = 0;
    Row->InterfaceIndex = 0;
    Row->BaseReachableTime = 0;
    Row->RetransmitTime = 0;
    Row->PathMtuDiscoveryTimeout = 0;
    Row->NlMtu = 0;
}

VOID
NTAPI
FreeMibTable(
    _In_ PVOID Memory)
{
    if (Memory != NULL)
        ExFreePoolWithTag(Memory, NETIO_IPHLP_TAG);
}

NTSTATUS
NTAPI
GetIfTable2(
    _Outptr_ PMIB_IF_TABLE2 *Table)
{
    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetIfEntry2(
    _Inout_ PMIB_IF_ROW2 Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetIpInterfaceTable(
    _In_ ADDRESS_FAMILY Family,
    _Outptr_ PMIB_IPINTERFACE_TABLE *Table)
{
    UNREFERENCED_PARAMETER(Family);

    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetIpInterfaceEntry(
    _Inout_ PMIB_IPINTERFACE_ROW Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
SetIpInterfaceEntry(
    _Inout_ PMIB_IPINTERFACE_ROW Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetUnicastIpAddressTable(
    _In_ ADDRESS_FAMILY Family,
    _Outptr_ PMIB_UNICASTIPADDRESS_TABLE *Table)
{
    UNREFERENCED_PARAMETER(Family);

    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
GetUnicastIpAddressEntry(
    _Inout_ PMIB_UNICASTIPADDRESS_ROW Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CreateUnicastIpAddressEntry(
    _In_ CONST MIB_UNICASTIPADDRESS_ROW *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
DeleteUnicastIpAddressEntry(
    _In_ CONST MIB_UNICASTIPADDRESS_ROW *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NetioTcpQuery(
    _In_ HANDLE Tcp,
    _In_ ULONG Entity,
    _In_ ULONG Instance,
    _In_ ULONG Class,
    _In_ ULONG Id,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Returned)
{
    TCP_REQUEST_QUERY_INFORMATION_EX Request;
    IO_STATUS_BLOCK Io;
    NTSTATUS Status;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.ID.toi_entity.tei_entity = Entity;
    Request.ID.toi_entity.tei_instance = Instance;
    Request.ID.toi_class = Class;
    Request.ID.toi_type = INFO_TYPE_PROVIDER;
    Request.ID.toi_id = Id;

    Status = ZwDeviceIoControlFile(Tcp, NULL, NULL, NULL, &Io, IOCTL_TCP_QUERY_INFORMATION_EX,
                                   &Request, sizeof(Request), Buffer, Length);
    *Returned = NT_SUCCESS(Status) ? (ULONG)Io.Information : 0;
    return Status;
}

static
ULONG
NetioPrefixLength(
    _In_ ULONG NetworkMask)
{
    ULONG Mask = RtlUlongByteSwap(NetworkMask), Length = 0;

    while (Length < 32 && (Mask & (0x80000000UL >> Length)))
        Length++;
    return Length;
}

NTSTATUS
NTAPI
GetBestRoute2(
    _In_opt_ PNET_LUID InterfaceLuid,
    _In_ NET_IFINDEX InterfaceIndex,
    _In_opt_ CONST SOCKADDR_INET *SourceAddress,
    _In_ CONST SOCKADDR_INET *DestinationAddress,
    _In_ ULONG AddressSortOptions,
    _Out_ PMIB_IPFORWARD_ROW2 BestRoute,
    _Out_ SOCKADDR_INET *BestSourceAddress)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Tcp");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Io;
    HANDLE Tcp;
    TDIEntityID *Entities;
    PNETIO_TDI_ROUTE Routes;
    IPAddrEntry Address;
    NETIO_TDI_ROUTE Best;
    ULONG Destination, Returned, EntityCount, RouteCount, i, j, Prefix, BestPrefix = 0;
    ULONG BestAddress = 0, BestIndex = 0;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(AddressSortOptions);

    if (DestinationAddress == NULL || BestRoute == NULL || BestSourceAddress == NULL)
        return STATUS_INVALID_PARAMETER;
    if (DestinationAddress->si_family != AF_INET ||
        (SourceAddress != NULL && SourceAddress->si_family != AF_UNSPEC && SourceAddress->si_family != AF_INET))
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (InterfaceLuid != NULL && InterfaceIndex == 0)
        InterfaceIndex = (NET_IFINDEX)InterfaceLuid->Info.NetLuidIndex;

    Destination = DestinationAddress->Ipv4.sin_addr.s_addr;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&Tcp, GENERIC_READ | SYNCHRONIZE, &Attributes, &Io, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    Entities = ExAllocatePoolWithTag(PagedPool, NETIO_MAX_ENTITIES * sizeof(TDIEntityID), NETIO_IPHLP_TAG);
    Routes = ExAllocatePoolWithTag(PagedPool, NETIO_MAX_ROUTES * sizeof(NETIO_TDI_ROUTE), NETIO_IPHLP_TAG);
    if (Entities == NULL || Routes == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    Status = NetioTcpQuery(Tcp, GENERIC_ENTITY, 0, INFO_CLASS_GENERIC, ENTITY_LIST_ID,
                           Entities, NETIO_MAX_ENTITIES * sizeof(TDIEntityID), &Returned);
    if (!NT_SUCCESS(Status))
        goto Done;
    EntityCount = Returned / sizeof(TDIEntityID);

    for (i = 0; i < EntityCount; i++)
    {
        if (Entities[i].tei_entity != CL_NL_ENTITY)
            continue;

        if (!NT_SUCCESS(NetioTcpQuery(Tcp, CL_NL_ENTITY, Entities[i].tei_instance, INFO_CLASS_PROTOCOL,
                                      IP_MIB_ADDRTABLE_ENTRY_ID, &Address, sizeof(Address), &Returned)) ||
            Returned < sizeof(Address) || Address.iae_addr == 0)
        {
            continue;
        }
        if (InterfaceIndex != 0 && Address.iae_index != InterfaceIndex)
            continue;
        if (SourceAddress != NULL && SourceAddress->si_family == AF_INET &&
            SourceAddress->Ipv4.sin_addr.s_addr != 0 &&
            SourceAddress->Ipv4.sin_addr.s_addr != Address.iae_addr)
        {
            continue;
        }

        if (!NT_SUCCESS(NetioTcpQuery(Tcp, CL_NL_ENTITY, Entities[i].tei_instance, INFO_CLASS_PROTOCOL,
                                      IP_MIB_ARPTABLE_ENTRY_ID, Routes,
                                      NETIO_MAX_ROUTES * sizeof(NETIO_TDI_ROUTE), &Returned)))
        {
            continue;
        }
        RouteCount = Returned / sizeof(NETIO_TDI_ROUTE);

        for (j = 0; j < RouteCount; j++)
        {
            if ((Destination & Routes[j].Mask) != (Routes[j].Dest & Routes[j].Mask))
                continue;
            Prefix = NetioPrefixLength(Routes[j].Mask);
            if (Found && (Prefix < BestPrefix || (Prefix == BestPrefix && Routes[j].Metric1 >= Best.Metric1)))
                continue;
            Best = Routes[j];
            BestPrefix = Prefix;
            BestAddress = Address.iae_addr;
            BestIndex = Address.iae_index;
            Found = TRUE;
        }
    }

    if (!Found)
    {
        Status = STATUS_NOT_FOUND;
        goto Done;
    }

    InitializeIpForwardEntry(BestRoute);
    BestRoute->InterfaceIndex = BestIndex;
    BestRoute->InterfaceLuid.Info.NetLuidIndex = BestIndex;
    BestRoute->DestinationPrefix.Prefix.si_family = AF_INET;
    BestRoute->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr = Best.Dest & Best.Mask;
    BestRoute->DestinationPrefix.PrefixLength = (UINT8)BestPrefix;
    BestRoute->NextHop.si_family = AF_INET;
    BestRoute->NextHop.Ipv4.sin_addr.s_addr = Best.NextHop == BestAddress ? 0 : Best.NextHop;
    BestRoute->Metric = Best.Metric1;
    BestRoute->Protocol = MIB_IPPROTO_NETMGMT;
    BestRoute->Loopback = (Best.Dest & 0xFF) == 127;
    BestRoute->AutoconfigureAddress = FALSE;
    BestRoute->Publish = FALSE;
    BestRoute->Immortal = FALSE;

    RtlZeroMemory(BestSourceAddress, sizeof(*BestSourceAddress));
    BestSourceAddress->si_family = AF_INET;
    BestSourceAddress->Ipv4.sin_addr.s_addr = BestAddress;
    Status = STATUS_SUCCESS;

Done:
    if (Routes)
        ExFreePoolWithTag(Routes, NETIO_IPHLP_TAG);
    if (Entities)
        ExFreePoolWithTag(Entities, NETIO_IPHLP_TAG);
    ZwClose(Tcp);
    return Status;
}

NTSTATUS
NTAPI
GetIpForwardTable2(
    _In_ ADDRESS_FAMILY Family,
    _Outptr_ PMIB_IPFORWARD_TABLE2 *Table)
{
    UNREFERENCED_PARAMETER(Family);

    if (Table == NULL)
        return STATUS_INVALID_PARAMETER;
    *Table = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CreateIpForwardEntry2(
    _In_ CONST MIB_IPFORWARD_ROW2 *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
DeleteIpForwardEntry2(
    _In_ CONST MIB_IPFORWARD_ROW2 *Row)
{
    if (Row == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
ConvertInterfaceLuidToGuid(
    _In_ CONST NET_LUID *InterfaceLuid,
    _Out_ GUID *InterfaceGuid)
{
    if ((InterfaceLuid == NULL) || (InterfaceGuid == NULL))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(InterfaceGuid, sizeof(*InterfaceGuid));
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NotifyIpInterfaceChange(
    _In_ ADDRESS_FAMILY Family,
    _In_ PIPINTERFACE_CHANGE_CALLBACK Callback,
    _In_opt_ PVOID CallerContext,
    _In_ BOOLEAN InitialNotification,
    _Inout_ HANDLE *NotificationHandle)
{
    UNREFERENCED_PARAMETER(Family);
    UNREFERENCED_PARAMETER(CallerContext);
    UNREFERENCED_PARAMETER(InitialNotification);

    if ((Callback == NULL) || (NotificationHandle == NULL))
        return STATUS_INVALID_PARAMETER;
    *NotificationHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NotifyUnicastIpAddressChange(
    _In_ ADDRESS_FAMILY Family,
    _In_ PUNICAST_IPADDRESS_CHANGE_CALLBACK Callback,
    _In_opt_ PVOID CallerContext,
    _In_ BOOLEAN InitialNotification,
    _Inout_ HANDLE *NotificationHandle)
{
    UNREFERENCED_PARAMETER(Family);
    UNREFERENCED_PARAMETER(CallerContext);
    UNREFERENCED_PARAMETER(InitialNotification);

    if ((Callback == NULL) || (NotificationHandle == NULL))
        return STATUS_INVALID_PARAMETER;
    *NotificationHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CancelMibChangeNotify2(
    _In_ HANDLE NotificationHandle)
{
    if (NotificationHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_INVALID_HANDLE;
}

NTSTATUS
NTAPI
SetInterfaceDnsSettings(
    _In_ GUID Interface,
    _In_ CONST VOID *Settings)
{
    UNREFERENCED_PARAMETER(Interface);

    if (Settings == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}
