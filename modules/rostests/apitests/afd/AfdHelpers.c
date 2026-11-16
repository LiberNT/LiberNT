/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Utility function definitions for calling AFD
 * COPYRIGHT:   Copyright 2015-2018 Thomas Faber (thomas.faber@reactos.org)
 *              Copyright 2019 Pierre Schweitzer (pierre@reactos.org)
 */

#include "precomp.h"

#define DD_UDP_DEVICE_NAME L"\\Device\\Udp"

NTSTATUS
AfdCreateSocket(
    _Out_ PHANDLE SocketHandle,
    _In_ int AddressFamily,
    _In_ int SocketType,
    _In_ int Protocol)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    PFILE_FULL_EA_INFORMATION EaBuffer = NULL;
    ULONG EaLength;
    PAFD_CREATE_PACKET AfdPacket;
    ULONG SizeOfPacket;
    ANSI_STRING EaName = RTL_CONSTANT_STRING(AfdCommand);
    UNICODE_STRING TcpTransportName = RTL_CONSTANT_STRING(DD_TCP_DEVICE_NAME);
    UNICODE_STRING UdpTransportName = RTL_CONSTANT_STRING(DD_UDP_DEVICE_NAME);
    UNICODE_STRING TransportName = SocketType == SOCK_STREAM ? TcpTransportName : UdpTransportName;
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\Afd\\Endpoint");

    *SocketHandle = NULL;

    SizeOfPacket = FIELD_OFFSET(AFD_CREATE_PACKET, TransportName) + TransportName.Length + sizeof(UNICODE_NULL);
    EaLength = SizeOfPacket + FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + EaName.Length + sizeof(ANSI_NULL);

    /* Set up EA Buffer */
    EaBuffer = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, EaLength);
    if (!EaBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    EaBuffer->NextEntryOffset = 0;
    EaBuffer->Flags = 0;
    EaBuffer->EaNameLength = EaName.Length;
    RtlCopyMemory(EaBuffer->EaName,
                  EaName.Buffer,
                  EaName.Length + sizeof(ANSI_NULL));
    EaBuffer->EaValueLength = SizeOfPacket;

    AfdPacket = (PAFD_CREATE_PACKET)(EaBuffer->EaName + EaBuffer->EaNameLength + sizeof(ANSI_NULL));
    AfdPacket->GroupID = 0;
    if (SocketType == SOCK_DGRAM)
    {
        AfdPacket->EndpointFlags = AFD_ENDPOINT_CONNECTIONLESS;
    }
    else if (SocketType == SOCK_STREAM)
    {
        AfdPacket->EndpointFlags = AFD_ENDPOINT_MESSAGE_ORIENTED;
    }
    AfdPacket->AddressFamily = AddressFamily;
    AfdPacket->SocketType = SocketType;
    AfdPacket->Protocol = Protocol;
    AfdPacket->SizeOfTransportName = TransportName.Length;
    RtlCopyMemory(AfdPacket->TransportName,
                  TransportName.Buffer,
                  TransportName.Length + sizeof(UNICODE_NULL));

    InitializeObjectAttributes(&ObjectAttributes,
                               &DeviceName,
                               OBJ_CASE_INSENSITIVE | OBJ_INHERIT,
                               0,
                               0);

    Status = NtCreateFile(SocketHandle,
                          GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN_IF,
                          0,
                          EaBuffer,
                          EaLength);

    RtlFreeHeap(RtlGetProcessHeap(), 0, EaBuffer);

    return Status;
}


NTSTATUS
AfdBind(
    _In_ HANDLE SocketHandle,
    _In_ const struct sockaddr *Address,
    _In_ ULONG AddressLength)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    PAFD_BIND_DATA BindInfo;
    ULONG BindInfoLength;
    HANDLE Event;

    Status = NtCreateEvent(&Event,
                           EVENT_ALL_ACCESS,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    BindInfoLength = FIELD_OFFSET(AFD_BIND_DATA, Address.Address[0].Address) +
                     AddressLength - FIELD_OFFSET(struct sockaddr, sa_data);
    BindInfo = RtlAllocateHeap(RtlGetProcessHeap(),
                               0,
                               BindInfoLength);
    if (!BindInfo)
    {
        NtClose(Event);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    BindInfo->ShareType = AFD_SHARE_UNIQUE;
    BindInfo->Address.TAAddressCount = 1;
    BindInfo->Address.Address[0].AddressType = Address->sa_family;
    BindInfo->Address.Address[0].AddressLength = AddressLength - FIELD_OFFSET(struct sockaddr, sa_data);
    RtlCopyMemory(&BindInfo->Address.Address[0].Address,
                  Address->sa_data,
                  BindInfo->Address.Address[0].AddressLength);

    Status = NtDeviceIoControlFile(SocketHandle,
                                   Event,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_AFD_BIND,
                                   BindInfo,
                                   BindInfoLength,
                                   BindInfo,
                                   BindInfoLength);
    if (Status == STATUS_PENDING)
    {
        NtWaitForSingleObject(Event, FALSE, NULL);
        Status = IoStatus.Status;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, BindInfo);
    NtClose(Event);

    return Status;
}

NTSTATUS
AfdConnect(
    _In_ HANDLE SocketHandle,
    _In_ const struct sockaddr *Address,
    _In_ ULONG AddressLength)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    PAFD_CONNECT_INFO ConnectInfo;
    ULONG ConnectInfoLength;
    HANDLE Event;

    Status = NtCreateEvent(&Event,
                           EVENT_ALL_ACCESS,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ASSERT(FIELD_OFFSET(AFD_CONNECT_INFO, RemoteAddress.Address[0].Address) == 20);
    ConnectInfoLength = FIELD_OFFSET(AFD_CONNECT_INFO, RemoteAddress.Address[0].Address) +
                        AddressLength - FIELD_OFFSET(struct sockaddr, sa_data);
    ConnectInfo = RtlAllocateHeap(RtlGetProcessHeap(),
                                  0,
                                  ConnectInfoLength);
    if (!ConnectInfo)
    {
        NtClose(Event);
        return STATUS_INSUFFICIENT_RESOURCES;
    }


    ConnectInfo->UseSAN = FALSE;
    ConnectInfo->Root = 0;
    ConnectInfo->Unknown = 0;
    ConnectInfo->RemoteAddress.TAAddressCount = 1;
    ConnectInfo->RemoteAddress.Address[0].AddressType = Address->sa_family;
    ConnectInfo->RemoteAddress.Address[0].AddressLength = AddressLength - FIELD_OFFSET(struct sockaddr, sa_data);
    RtlCopyMemory(&ConnectInfo->RemoteAddress.Address[0].Address,
                  Address->sa_data,
                  ConnectInfo->RemoteAddress.Address[0].AddressLength);

    Status = NtDeviceIoControlFile(SocketHandle,
                                   Event,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_AFD_CONNECT,
                                   ConnectInfo,
                                   ConnectInfoLength,
                                   NULL,
                                   0);
    if (Status == STATUS_PENDING)
    {
        NtWaitForSingleObject(Event, FALSE, NULL);
        Status = IoStatus.Status;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, ConnectInfo);
    NtClose(Event);

    return Status;
}

NTSTATUS
AfdSend(
    _In_ HANDLE SocketHandle,
    _In_ const void *Buffer,
    _In_ ULONG BufferLength)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    AFD_SEND_INFO SendInfo;
    HANDLE Event;
    AFD_WSABUF AfdBuffer;

    Status = NtCreateEvent(&Event,
                           EVENT_ALL_ACCESS,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    AfdBuffer.buf = (PVOID)Buffer;
    AfdBuffer.len = BufferLength;
    SendInfo.BufferArray = &AfdBuffer;
    SendInfo.BufferCount = 1;
    SendInfo.TdiFlags = 0;
    SendInfo.AfdFlags = 0;

    Status = NtDeviceIoControlFile(SocketHandle,
                                   Event,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_AFD_SEND,
                                   &SendInfo,
                                   sizeof(SendInfo),
                                   NULL,
                                   0);
    if (Status == STATUS_PENDING)
    {
        NtWaitForSingleObject(Event, FALSE, NULL);
        Status = IoStatus.Status;
    }

    NtClose(Event);

    return Status;
}

NTSTATUS
AfdSendTo(
    _In_ HANDLE SocketHandle,
    _In_ const void *Buffer,
    _In_ ULONG BufferLength,
    _In_ const struct sockaddr *Address,
    _In_ ULONG AddressLength)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    AFD_SEND_INFO_UDP SendInfo;
    HANDLE Event;
    AFD_WSABUF AfdBuffer;
    PTRANSPORT_ADDRESS TransportAddress;
    ULONG TransportAddressLength;

    Status = NtCreateEvent(&Event,
                           EVENT_ALL_ACCESS,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    TransportAddressLength = FIELD_OFFSET(TRANSPORT_ADDRESS, Address[0].Address) +
                             AddressLength - FIELD_OFFSET(struct sockaddr, sa_data);
    TransportAddress = RtlAllocateHeap(RtlGetProcessHeap(),
                                       0,
                                       TransportAddressLength);
    if (!TransportAddress)
    {
        NtClose(Event);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    TransportAddress->TAAddressCount = 1;
    TransportAddress->Address[0].AddressType = Address->sa_family;
    TransportAddress->Address[0].AddressLength = AddressLength - FIELD_OFFSET(struct sockaddr, sa_data);
    RtlCopyMemory(&TransportAddress->Address[0].Address,
                  Address->sa_data,
                  TransportAddress->Address[0].AddressLength);

    AfdBuffer.buf = (PVOID)Buffer;
    AfdBuffer.len = BufferLength;
    RtlZeroMemory(&SendInfo, sizeof(SendInfo));
    SendInfo.BufferArray = &AfdBuffer;
    SendInfo.BufferCount = 1;
    SendInfo.AfdFlags = 0;
    SendInfo.TdiConnection.RemoteAddress = TransportAddress;
    SendInfo.TdiConnection.RemoteAddressLength = TransportAddressLength;

    Status = NtDeviceIoControlFile(SocketHandle,
                                   Event,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_AFD_SEND_DATAGRAM,
                                   &SendInfo,
                                   sizeof(SendInfo),
                                   NULL,
                                   0);
    if (Status == STATUS_PENDING)
    {
        NtWaitForSingleObject(Event, FALSE, NULL);
        Status = IoStatus.Status;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, TransportAddress);
    NtClose(Event);

    return Status;
}

NTSTATUS
AfdSetInformation(
    _In_ HANDLE SocketHandle,
    _In_ ULONG InformationClass,
    _In_opt_ PBOOLEAN Boolean,
    _In_opt_ PULONG Ulong,
    _In_opt_ PLARGE_INTEGER LargeInteger)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    AFD_INFO InfoData;
    HANDLE Event;

    Status = NtCreateEvent(&Event,
                           EVENT_ALL_ACCESS,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    InfoData.InformationClass = InformationClass;

    if (Ulong != NULL)
    {
        InfoData.Information.Ulong = *Ulong;
    }
    if (LargeInteger != NULL)
    {
        InfoData.Information.LargeInteger = *LargeInteger;
    }
    if (Boolean != NULL)
    {
        InfoData.Information.Boolean = *Boolean;
    }

    Status = NtDeviceIoControlFile(SocketHandle,
                                   Event,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_AFD_SET_INFO,
                                   &InfoData,
                                   sizeof(InfoData),
                                   NULL,
                                   0);
    if (Status == STATUS_PENDING)
    {
        NtWaitForSingleObject(Event, FALSE, NULL);
        Status = IoStatus.Status;
    }

    NtClose(Event);

    return Status;
}

NTSTATUS
AfdGetInformation(
    _In_ HANDLE SocketHandle,
    _In_ ULONG InformationClass,
    _In_opt_ PBOOLEAN Boolean,
    _In_opt_ PULONG Ulong,
    _In_opt_ PLARGE_INTEGER LargeInteger)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    AFD_INFO InfoData;
    HANDLE Event;

    Status = NtCreateEvent(&Event,
                           EVENT_ALL_ACCESS,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    InfoData.InformationClass = InformationClass;

    Status = NtDeviceIoControlFile(SocketHandle,
                                   Event,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_AFD_GET_INFO,
                                   &InfoData,
                                   sizeof(InfoData),
                                   &InfoData,
                                   sizeof(InfoData));
    if (Status == STATUS_PENDING)
    {
        NtWaitForSingleObject(Event, FALSE, NULL);
        Status = IoStatus.Status;
    }

    NtClose(Event);

    if (Status != STATUS_SUCCESS)
    {
        return Status;
    }

    if (Ulong != NULL)
    {
        *Ulong = InfoData.Information.Ulong;
    }
    if (LargeInteger != NULL)
    {
        *LargeInteger = InfoData.Information.LargeInteger;
    }
    if (Boolean != NULL)
    {
        *Boolean = InfoData.Information.Boolean;
    }

    return Status;
}

static
NTSTATUS
AfdOpenPacketForTest(
    _In_ PFILE_FULL_EA_INFORMATION EaBuffer)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\Afd\\Endpoint");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE SocketHandle = NULL;
    ULONG EaLength;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    EaLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + EaBuffer->EaNameLength + 1 + EaBuffer->EaValueLength;
    Status = NtCreateFile(&SocketHandle, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          &ObjectAttributes, &IoStatus, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, 0,
                          EaBuffer, EaLength);
    if (NT_SUCCESS(Status))
        NtClose(SocketHandle);
    return Status;
}

START_TEST(AfdCreatePacket)
{
    DECLSPEC_ALIGN(4) UCHAR Buffer[128];
    PFILE_FULL_EA_INFORMATION EaBuffer = (PFILE_FULL_EA_INFORMATION)Buffer;
    PAFD_CREATE_PACKET Packet;
    UNICODE_STRING TransportName = RTL_CONSTANT_STRING(L"\\Device\\Tcp");
    ULONG *OldPacket;
    ULONG PacketLength;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    EaBuffer->EaNameLength = AFD_PACKET_COMMAND_LENGTH;
    RtlCopyMemory(EaBuffer->EaName, AfdCommand, AFD_PACKET_COMMAND_LENGTH + 1);
    Packet = (PAFD_CREATE_PACKET)(EaBuffer->EaName + EaBuffer->EaNameLength + 1);
    PacketLength = FIELD_OFFSET(AFD_CREATE_PACKET, TransportName) + TransportName.Length + sizeof(WCHAR);
    EaBuffer->EaValueLength = (USHORT)PacketLength;
    Packet->AddressFamily = AF_INET;
    Packet->SocketType = SOCK_STREAM;
    Packet->Protocol = IPPROTO_TCP;
    Packet->SizeOfTransportName = TransportName.Length;
    RtlCopyMemory(Packet->TransportName, TransportName.Buffer, TransportName.Length);

    Status = AfdOpenPacketForTest(EaBuffer);
    ok_eq_hex(Status, STATUS_SUCCESS);

    EaBuffer->EaValueLength = (USHORT)(PacketLength + sizeof(WCHAR));
    Status = AfdOpenPacketForTest(EaBuffer);
    ok_eq_hex(Status, STATUS_SUCCESS);
    EaBuffer->EaValueLength = (USHORT)PacketLength;

    Packet->SizeOfTransportName = TransportName.Length - 1;
    Status = AfdOpenPacketForTest(EaBuffer);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Packet->SizeOfTransportName = TransportName.Length + 20;
    Status = AfdOpenPacketForTest(EaBuffer);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);

    EaBuffer->EaValueLength = 8;
    Status = AfdOpenPacketForTest(EaBuffer);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);

    RtlZeroMemory(Packet, PacketLength);
    OldPacket = (ULONG *)Packet;
    OldPacket[2] = TransportName.Length;
    RtlCopyMemory(&OldPacket[3], TransportName.Buffer, TransportName.Length);
    EaBuffer->EaValueLength = (USHORT)(3 * sizeof(ULONG) + TransportName.Length + sizeof(WCHAR));
    Status = AfdOpenPacketForTest(EaBuffer);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
}
