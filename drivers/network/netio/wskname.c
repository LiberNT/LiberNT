/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WSK name resolution: WskGetAddressInfo, WskFreeAddressInfo and WskGetNameInfo
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <wdmsec.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <wsk.h>
#include <ntstrsafe.h>

#include "wskname.h"

#define NDEBUG
#include <reactos/debug.h>

/* Exported by the kernel; the prototypes are the ones sdk/include/ndk/rtlfuncs.h declares. */
NTSYSAPI
PWSTR
NTAPI
RtlIpv4AddressToStringW(
    _In_ const struct in_addr *Addr,
    _Out_writes_(16) PWCHAR S);

NTSYSAPI
NTSTATUS
NTAPI
RtlIpv4StringToAddressW(
    _In_ PCWSTR String,
    _In_ BOOLEAN Strict,
    _Out_ PCWSTR *Terminator,
    _Out_ struct in_addr *Addr);

NTSYSAPI
PWSTR
NTAPI
RtlIpv6AddressToStringW(
    _In_ const struct in6_addr *Addr,
    _Out_writes_(46) PWSTR S);

NTSYSAPI
NTSTATUS
NTAPI
RtlIpv6StringToAddressW(
    _In_ PCWSTR String,
    _Out_ PCWSTR *Terminator,
    _Out_ struct in6_addr *Addr);

typedef struct _WSKNAME_WORK
{
    WORK_QUEUE_ITEM Item;
    PIRP Irp;
    WSKNAME_REQUEST Request;
    PADDRINFOEXW *Result;
    PUNICODE_STRING NodeName;
    PUNICODE_STRING ServiceName;
    ADDRINFOEXW Hints;
    BOOLEAN HasHints;
} WSKNAME_WORK, *PWSKNAME_WORK;

static
NTSTATUS
WskNameCopyString(
    _In_opt_ PCUNICODE_STRING String,
    _Out_writes_(Count) PWCHAR Buffer,
    _In_ ULONG Count)
{
    ULONG Length;

    Buffer[0] = UNICODE_NULL;
    if (String == NULL || String->Buffer == NULL || String->Length == 0)
        return STATUS_SUCCESS;

    Length = String->Length / sizeof(WCHAR);
    if (Length >= Count)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(Buffer, String->Buffer, Length * sizeof(WCHAR));
    Buffer[Length] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
WskNameFillString(
    _Inout_opt_ PUNICODE_STRING String,
    _In_ PCWSTR Text)
{
    SIZE_T Length = wcslen(Text) * sizeof(WCHAR);

    if (String == NULL)
        return STATUS_SUCCESS;
    if (Length > String->MaximumLength)
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(String->Buffer, Text, Length);
    String->Length = (USHORT)Length;
    return STATUS_SUCCESS;
}

/* A service the caller spelled out as a number needs no resolver. */
static
BOOLEAN
WskNameNumericPort(
    _In_ PCWSTR Service,
    _Out_ PUSHORT Port)
{
    ULONG Value = 0;

    *Port = 0;
    if (Service[0] == UNICODE_NULL)
        return TRUE;
    for (; *Service != UNICODE_NULL; Service++)
    {
        if (*Service < L'0' || *Service > L'9')
            return FALSE;
        Value = Value * 10 + (ULONG)(*Service - L'0');
        if (Value > 65535)
            return FALSE;
    }
    *Port = (USHORT)Value;
    return TRUE;
}

/* An address the caller spelled out needs no resolver either. */
static
BOOLEAN
WskNameNumericNode(
    _In_ PCWSTR Node,
    _In_ LONG Family,
    _In_ USHORT Port,
    _Out_ PSOCKADDR_INET Address)
{
    PCWSTR Terminator;
    NTSTATUS Status;

    RtlZeroMemory(Address, sizeof(*Address));
    if (Family == AF_UNSPEC || Family == AF_INET)
    {
        Status = RtlIpv4StringToAddressW(Node, TRUE, &Terminator, &Address->Ipv4.sin_addr);
        if (NT_SUCCESS(Status) && *Terminator == UNICODE_NULL)
        {
            Address->Ipv4.sin_family = AF_INET;
            Address->Ipv4.sin_port = RtlUshortByteSwap(Port);
            return TRUE;
        }
    }
    if (Family == AF_UNSPEC || Family == AF_INET6)
    {
        Status = RtlIpv6StringToAddressW(Node, &Terminator, &Address->Ipv6.sin6_addr);
        if (NT_SUCCESS(Status) && *Terminator == UNICODE_NULL)
        {
            Address->Ipv6.sin6_family = AF_INET6;
            Address->Ipv6.sin6_port = RtlUshortByteSwap(Port);
            return TRUE;
        }
    }
    return FALSE;
}

static
ULONG
WskNameAddressLength(
    _In_ const SOCKADDR_INET *Address)
{
    return Address->si_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN);
}

/*
 * One block holds the whole chain, its addresses and the canonical name, so
 * WskFreeAddressInfo is a single free of the pointer the client was given.
 */
static
NTSTATUS
WskNameBuildResult(
    _In_reads_(Count) const SOCKADDR_INET *Addresses,
    _In_ ULONG Count,
    _In_opt_ PCWSTR CanonicalName,
    _In_ LONG SocketType,
    _In_ LONG Protocol,
    _Outptr_ PADDRINFOEXW *Result)
{
    SIZE_T NameLength = CanonicalName != NULL && CanonicalName[0] != UNICODE_NULL
                            ? (wcslen(CanonicalName) + 1) * sizeof(WCHAR)
                            : 0;
    SIZE_T Size = Count * (sizeof(ADDRINFOEXW) + sizeof(SOCKADDR_INET)) + NameLength;
    PADDRINFOEXW Entries;
    PUCHAR Buffer;
    ULONG Index;

    *Result = NULL;
    if (Count == 0)
        return STATUS_NOT_FOUND;

    Entries = ExAllocatePoolZero(NonPagedPoolNx, Size, TAG_WSKNAME);
    if (Entries == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Buffer = (PUCHAR)(Entries + Count);
    for (Index = 0; Index < Count; Index++)
    {
        PSOCKADDR_INET Address = (PSOCKADDR_INET)(Buffer + Index * sizeof(SOCKADDR_INET));

        RtlCopyMemory(Address, &Addresses[Index], sizeof(*Address));
        Entries[Index].ai_family = Address->si_family;
        Entries[Index].ai_socktype = SocketType;
        Entries[Index].ai_protocol = Protocol;
        Entries[Index].ai_addrlen = WskNameAddressLength(Address);
        Entries[Index].ai_addr = (struct sockaddr *)Address;
        Entries[Index].ai_next = Index + 1 < Count ? &Entries[Index + 1] : NULL;
    }
    if (NameLength != 0)
    {
        PWCHAR Name = (PWCHAR)(Buffer + Count * sizeof(SOCKADDR_INET));

        RtlCopyMemory(Name, CanonicalName, NameLength);
        Entries[0].ai_canonname = Name;
    }

    *Result = Entries;
    return STATUS_SUCCESS;
}

/* The client hands over an IRP of its own, so its completion routine needs a location. */
static
VOID
WskNameComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    IoSetNextIrpStackLocation(Irp);
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static
NTSTATUS
WskNameResolveAddress(
    _In_ PWSKNAME_WORK Work,
    _Outptr_ PADDRINFOEXW *Result)
{
    WSKNAME_REPLY Reply;
    NTSTATUS Status;

    Status = WskNameForward(&Work->Request, &Reply);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!NT_SUCCESS((NTSTATUS)Reply.Status))
        return (NTSTATUS)Reply.Status;
    if (Reply.AddressCount > WSKNAME_MAX_REPLY_ADDRESSES)
        return STATUS_INVALID_PARAMETER;

    Reply.Name[RTL_NUMBER_OF(Reply.Name) - 1] = UNICODE_NULL;
    return WskNameBuildResult(Reply.Address,
                              Reply.AddressCount,
                              (Work->Hints.ai_flags & AI_CANONNAME) ? Reply.Name : NULL,
                              Work->Request.SocketType,
                              Work->Request.Protocol,
                              Result);
}

static
NTSTATUS
WskNameResolveName(
    _In_ PWSKNAME_WORK Work)
{
    WSKNAME_REPLY Reply;
    NTSTATUS Status;

    Status = WskNameForward(&Work->Request, &Reply);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!NT_SUCCESS((NTSTATUS)Reply.Status))
        return (NTSTATUS)Reply.Status;

    Reply.Name[RTL_NUMBER_OF(Reply.Name) - 1] = UNICODE_NULL;
    Reply.Service[RTL_NUMBER_OF(Reply.Service) - 1] = UNICODE_NULL;
    Status = WskNameFillString(Work->NodeName, Reply.Name);
    if (NT_SUCCESS(Status))
        Status = WskNameFillString(Work->ServiceName, Reply.Service);
    return Status;
}

static
VOID
NTAPI
WskNameWorker(
    _In_ PVOID Parameter)
{
    PWSKNAME_WORK Work = Parameter;
    PADDRINFOEXW Result = NULL;
    NTSTATUS Status;

    if (Work->Request.Operation == WskNameAddressInfo)
    {
        Status = WskNameResolveAddress(Work, &Result);
        if (NT_SUCCESS(Status))
            *Work->Result = Result;
    }
    else
    {
        Status = WskNameResolveName(Work);
    }

    WskNameComplete(Work->Irp, Status);
    ExFreePoolWithTag(Work, TAG_WSKNAME);
}

/* WskNameForward waits for the resolver, so it never runs on the caller's thread. */
static
NTSTATUS
WskNameQueue(
    _In_ PWSKNAME_WORK Work)
{
    IoMarkIrpPending(Work->Irp);
    ExInitializeWorkItem(&Work->Item, WskNameWorker, Work);
    ExQueueWorkItem(&Work->Item, DelayedWorkQueue);
    return STATUS_PENDING;
}

static
PWSKNAME_WORK
WskNameAllocateWork(
    _In_ PIRP Irp,
    _In_opt_ PADDRINFOEXW Hints)
{
    PWSKNAME_WORK Work = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Work), TAG_WSKNAME);

    if (Work == NULL)
        return NULL;

    Work->Irp = Irp;
    if (Hints != NULL)
    {
        Work->Hints = *Hints;
        Work->HasHints = TRUE;
    }
    Work->Request.Family = Work->Hints.ai_family;
    Work->Request.Flags = Work->Hints.ai_flags;
    Work->Request.SocketType = Work->Hints.ai_socktype;
    Work->Request.Protocol = Work->Hints.ai_protocol;
    return Work;
}

NTSTATUS
WSKAPI
WskGetAddressInfo(
    _In_ PWSK_CLIENT Client,
    _In_opt_ PUNICODE_STRING NodeName,
    _In_opt_ PUNICODE_STRING ServiceName,
    _In_opt_ ULONG NameSpace,
    _In_opt_ GUID *Provider,
    _In_opt_ PADDRINFOEXW Hints,
    _Outptr_ PADDRINFOEXW *Result,
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _Inout_ PIRP Irp)
{
    PWSKNAME_WORK Work;
    SOCKADDR_INET Address;
    USHORT Port;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(Provider);
    UNREFERENCED_PARAMETER(OwningProcess);
    UNREFERENCED_PARAMETER(OwningThread);

    if (Irp == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Result == NULL || (NodeName == NULL && ServiceName == NULL))
    {
        WskNameComplete(Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    *Result = NULL;
    Work = WskNameAllocateWork(Irp, Hints);
    if (Work == NULL)
    {
        WskNameComplete(Irp, STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Work->Request.Operation = WskNameAddressInfo;
    Work->Request.NameSpace = NameSpace;
    Work->Result = Result;
    Status = WskNameCopyString(NodeName, Work->Request.Node, RTL_NUMBER_OF(Work->Request.Node));
    if (NT_SUCCESS(Status))
        Status = WskNameCopyString(ServiceName, Work->Request.Service, RTL_NUMBER_OF(Work->Request.Service));
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Work, TAG_WSKNAME);
        WskNameComplete(Irp, Status);
        return Status;
    }

    /* A literal address and a numeric port are answered here. */
    if (WskNameNumericPort(Work->Request.Service, &Port) &&
        (Work->Request.Node[0] == UNICODE_NULL ||
         WskNameNumericNode(Work->Request.Node, Work->Request.Family, Port, &Address)))
    {
        if (Work->Request.Node[0] == UNICODE_NULL)
        {
            /* No node at all means the loopback address of the wanted family. */
            RtlZeroMemory(&Address, sizeof(Address));
            if (Work->Request.Family == AF_INET6)
            {
                Address.Ipv6.sin6_family = AF_INET6;
                Address.Ipv6.sin6_addr.s6_bytes[15] = 1;
                Address.Ipv6.sin6_port = RtlUshortByteSwap(Port);
            }
            else
            {
                Address.Ipv4.sin_family = AF_INET;
                Address.Ipv4.sin_addr.s_addr = RtlUlongByteSwap(INADDR_LOOPBACK);
                Address.Ipv4.sin_port = RtlUshortByteSwap(Port);
            }
        }
        Status = WskNameBuildResult(&Address,
                                    1,
                                    (Work->Hints.ai_flags & AI_CANONNAME) ? Work->Request.Node : NULL,
                                    Work->Request.SocketType,
                                    Work->Request.Protocol,
                                    Result);
        ExFreePoolWithTag(Work, TAG_WSKNAME);
        WskNameComplete(Irp, Status);
        return Status;
    }

    if (Work->Hints.ai_flags & AI_NUMERICHOST)
    {
        ExFreePoolWithTag(Work, TAG_WSKNAME);
        WskNameComplete(Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    return WskNameQueue(Work);
}

VOID
WSKAPI
WskFreeAddressInfo(
    _In_ PWSK_CLIENT Client,
    _In_ PADDRINFOEXW AddrInfo)
{
    UNREFERENCED_PARAMETER(Client);

    if (AddrInfo != NULL)
        ExFreePoolWithTag(AddrInfo, TAG_WSKNAME);
}

NTSTATUS
WSKAPI
WskGetNameInfo(
    _In_ PWSK_CLIENT Client,
    _In_ PSOCKADDR SockAddr,
    _In_ ULONG SockAddrLength,
    _Out_opt_ PUNICODE_STRING NodeName,
    _Out_opt_ PUNICODE_STRING ServiceName,
    _In_ ULONG Flags,
    _In_opt_ PEPROCESS OwningProcess,
    _In_opt_ PETHREAD OwningThread,
    _Inout_ PIRP Irp)
{
    PWSKNAME_WORK Work;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(OwningProcess);
    UNREFERENCED_PARAMETER(OwningThread);

    if (Irp == NULL)
        return STATUS_INVALID_PARAMETER;
    if (SockAddr == NULL || (NodeName == NULL && ServiceName == NULL) ||
        (SockAddr->sa_family != AF_INET && SockAddr->sa_family != AF_INET6) ||
        SockAddrLength < (SockAddr->sa_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN)))
    {
        WskNameComplete(Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    Work = WskNameAllocateWork(Irp, NULL);
    if (Work == NULL)
    {
        WskNameComplete(Irp, STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Work->Request.Operation = WskNameNameInfo;
    Work->Request.Flags = (LONG)Flags;
    Work->Request.Family = SockAddr->sa_family;
    Work->NodeName = NodeName;
    Work->ServiceName = ServiceName;
    RtlCopyMemory(&Work->Request.Address,
                  SockAddr,
                  SockAddr->sa_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN));

    /* The caller that only wants numbers is answered here. */
    if (Flags & NI_NUMERICHOST)
    {
        WCHAR Text[46];
        WCHAR Port[8];
        PWSTR End;

        if (SockAddr->sa_family == AF_INET6)
            End = RtlIpv6AddressToStringW(&Work->Request.Address.Ipv6.sin6_addr, Text);
        else
            End = RtlIpv4AddressToStringW(&Work->Request.Address.Ipv4.sin_addr, Text);
        *End = UNICODE_NULL;

        Status = RtlStringCchPrintfW(Port,
                                     RTL_NUMBER_OF(Port),
                                     L"%u",
                                     RtlUshortByteSwap(Work->Request.Address.Ipv4.sin_port));
        if (NT_SUCCESS(Status))
            Status = WskNameFillString(NodeName, Text);
        if (NT_SUCCESS(Status))
            Status = WskNameFillString(ServiceName, Port);
        ExFreePoolWithTag(Work, TAG_WSKNAME);
        WskNameComplete(Irp, Status);
        return Status;
    }

    return WskNameQueue(Work);
}
