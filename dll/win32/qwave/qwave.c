/*
 * PROJECT:     LiberNT qWAVE
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     QoS2 API (qos2.h) over the qWAVE flow driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <ws2tcpip.h>
#include <qos2.h>
#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <libernt/qwavedrv.h>

C_ASSERT(QOS_NON_ADAPTIVE_FLOW == QWAVE_NON_ADAPTIVE_FLOW);
C_ASSERT(QOSTrafficTypeControl == QWAVE_TRAFFIC_TYPE_LAST);
C_ASSERT(QOSSetTrafficType == QWAVE_SET_TRAFFIC_TYPE);
C_ASSERT(QOSSetOutgoingRate == QWAVE_SET_OUTGOING_RATE);
C_ASSERT(QOSSetOutgoingDSCPValue == QWAVE_SET_OUTGOING_DSCP);
C_ASSERT(QOSUseNonConformantMarkings == QWAVE_SHAPING_LAST);
C_ASSERT(QOSFlowRateUserCaused == QWAVE_FLOWRATE_REASON_LAST);
C_ASSERT(sizeof(QOS_FLOWRATE_OUTGOING) == sizeof(QWAVE_FLOWRATE));
C_ASSERT(FIELD_OFFSET(QOS_FLOWRATE_OUTGOING, ShapingBehavior) == FIELD_OFFSET(QWAVE_FLOWRATE, ShapingBehavior));
C_ASSERT(FIELD_OFFSET(QOS_FLOWRATE_OUTGOING, Reason) == FIELD_OFFSET(QWAVE_FLOWRATE, Reason));

static
BOOL
QwaveFail(
    _In_ DWORD Error)
{
    SetLastError(Error);
    return FALSE;
}

static
BOOL
QwaveControl(
    _In_ HANDLE QOSHandle,
    _In_ DWORD Code,
    _In_reads_bytes_(InputSize) PVOID Input,
    _In_ DWORD InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID Output,
    _In_ DWORD OutputSize,
    _Inout_opt_ LPOVERLAPPED Overlapped)
{
    DWORD Returned = 0;

    return DeviceIoControl(QOSHandle, Code, Input, InputSize, Output, OutputSize, &Returned, Overlapped);
}

BOOL
WINAPI
QOSCreateHandle(
    _In_ PQOS_VERSION Version,
    _Out_ PHANDLE QOSHandle)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(QWAVE_DEVICE_NAME);
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;

    if (!Version || !QOSHandle || Version->MajorVersion != 1 || Version->MinorVersion != 0)
        return QwaveFail(ERROR_INVALID_PARAMETER);

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateFile(&Handle, GENERIC_READ, &Attributes, &Iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN, 0, NULL, 0);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        return QwaveFail(ERROR_SERVICE_DEPENDENCY_FAIL);
    if (!NT_SUCCESS(Status))
        return QwaveFail(RtlNtStatusToDosError(Status));

    *QOSHandle = Handle;
    return TRUE;
}

BOOL
WINAPI
QOSCloseHandle(
    _In_ HANDLE QOSHandle)
{
    return CloseHandle(QOSHandle);
}

BOOL
WINAPI
QOSAddSocketToFlow(
    _In_ HANDLE QOSHandle,
    _In_ SOCKET Socket,
    _In_opt_ PSOCKADDR DestAddr,
    _In_ QOS_TRAFFIC_TYPE TrafficType,
    _In_opt_ DWORD Flags,
    _Inout_ PQOS_FLOWID FlowId)
{
    QWAVE_ADD_SOCKET_INPUT Input = { 0 };
    WSAPROTOCOL_INFOW Protocol;
    SOCKADDR_STORAGE Peer;
    ULONG NewFlowId = 0;
    int Length;

    if (!FlowId)
        return QwaveFail(ERROR_INVALID_PARAMETER);

    Length = sizeof(Protocol);
    if (getsockopt(Socket, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&Protocol, &Length) != 0)
    {
        /* TODO: INFO_NEEDED: the error Windows reports for a handle that is not a socket */
        return QwaveFail(WSAGetLastError());
    }
    if (Protocol.iAddressFamily != AF_INET && Protocol.iAddressFamily != AF_INET6)
        return QwaveFail(ERROR_INVALID_PARAMETER);

    if (DestAddr)
    {
        if (DestAddr->sa_family != Protocol.iAddressFamily)
            return QwaveFail(ERROR_INVALID_PARAMETER);
        if (DestAddr->sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&((PSOCKADDR_IN6)DestAddr)->sin6_addr))
            return QwaveFail(ERROR_INVALID_PARAMETER);
    }
    else
    {
        Length = sizeof(Peer);
        if (getpeername(Socket, (PSOCKADDR)&Peer, &Length) != 0)
            return QwaveFail(ERROR_INVALID_PARAMETER);
    }

    Input.Socket = (ULONG64)Socket;
    Input.FlowId = *FlowId;
    Input.TrafficType = (ULONG)TrafficType;
    Input.Flags = Flags;
    if (!QwaveControl(QOSHandle, IOCTL_QWAVE_ADD_SOCKET, &Input, sizeof(Input), &NewFlowId, sizeof(NewFlowId), NULL))
        return FALSE;

    *FlowId = NewFlowId;
    return TRUE;
}

BOOL
WINAPI
QOSRemoveSocketFromFlow(
    _In_ HANDLE QOSHandle,
    _In_opt_ SOCKET Socket,
    _In_ QOS_FLOWID FlowId,
    _Reserved_ DWORD Flags)
{
    QWAVE_REMOVE_SOCKET_INPUT Input;

    Input.Socket = (ULONG64)Socket;
    Input.FlowId = FlowId;
    Input.Flags = Flags;
    return QwaveControl(QOSHandle, IOCTL_QWAVE_REMOVE_SOCKET, &Input, sizeof(Input), NULL, 0, NULL);
}

BOOL
WINAPI
QOSSetFlow(
    _In_ HANDLE QOSHandle,
    _In_ QOS_FLOWID FlowId,
    _In_ QOS_SET_FLOW Operation,
    _In_ ULONG Size,
    _In_reads_bytes_(Size) PVOID Buffer,
    _Reserved_ DWORD Flags,
    _Out_opt_ LPOVERLAPPED Overlapped)
{
    union
    {
        QWAVE_SET_FLOW_INPUT Input;
        UCHAR Raw[FIELD_OFFSET(QWAVE_SET_FLOW_INPUT, Buffer) + QWAVE_SET_FLOW_MAX_BUFFER];
    } Request;
    ULONG Copied = min(Size, QWAVE_SET_FLOW_MAX_BUFFER);

    if (Copied != 0 && !Buffer)
        return QwaveFail(ERROR_INVALID_PARAMETER);

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Input.FlowId = FlowId;
    Request.Input.Operation = (ULONG)Operation;
    Request.Input.Flags = Flags;
    Request.Input.Size = Size;
    if (Copied != 0)
        RtlCopyMemory(&Request.Raw[FIELD_OFFSET(QWAVE_SET_FLOW_INPUT, Buffer)], Buffer, Copied);
    return QwaveControl(QOSHandle, IOCTL_QWAVE_SET_FLOW, &Request, FIELD_OFFSET(QWAVE_SET_FLOW_INPUT, Buffer) + Copied,
                        NULL, 0, Overlapped);
}

BOOL
WINAPI
QOSCancel(
    _In_ HANDLE QOSHandle,
    _In_ LPOVERLAPPED Overlapped)
{
    if (!Overlapped)
        return QwaveFail(ERROR_INVALID_PARAMETER);
    return CancelIoEx(QOSHandle, Overlapped);
}

BOOL
WINAPI
QOSEnumerateFlows(
    _In_ HANDLE QOSHandle,
    _Inout_ PULONG Size,
    _Out_writes_bytes_(*Size) PVOID Buffer)
{
    UNREFERENCED_PARAMETER(QOSHandle);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Buffer);
    /* TODO: enumerate the flows of every process for an administrator */
    return QwaveFail(ERROR_NOT_SUPPORTED);
}

BOOL
WINAPI
QOSQueryFlow(
    _In_ HANDLE QOSHandle,
    _In_ QOS_FLOWID FlowId,
    _In_ QOS_QUERY_FLOW Operation,
    _Inout_ PULONG Size,
    _Out_writes_bytes_(*Size) PVOID Buffer,
    _In_opt_ DWORD Flags,
    _Out_opt_ LPOVERLAPPED Overlapped)
{
    UNREFERENCED_PARAMETER(QOSHandle);
    UNREFERENCED_PARAMETER(FlowId);
    UNREFERENCED_PARAMETER(Operation);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Overlapped);
    /* TODO: QOSQueryPacketPriority; the fundamentals and the outgoing rate need adaptive flows */
    return QwaveFail(ERROR_NOT_SUPPORTED);
}

BOOL
WINAPI
QOSNotifyFlow(
    _In_ HANDLE QOSHandle,
    _In_ QOS_FLOWID FlowId,
    _In_ QOS_NOTIFY_FLOW Operation,
    _Inout_opt_ PULONG Size,
    _Inout_updates_bytes_opt_(*Size) PVOID Buffer,
    _Reserved_ DWORD Flags,
    _Out_opt_ LPOVERLAPPED Overlapped)
{
    UNREFERENCED_PARAMETER(QOSHandle);
    UNREFERENCED_PARAMETER(FlowId);
    UNREFERENCED_PARAMETER(Operation);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Overlapped);
    /* TODO: congestion and bandwidth notifications need adaptive flows */
    return QwaveFail(ERROR_NOT_SUPPORTED);
}

BOOL
WINAPI
QOSStartTrackingClient(
    _In_ HANDLE QOSHandle,
    _In_ PSOCKADDR DestAddr,
    _Reserved_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(QOSHandle);
    UNREFERENCED_PARAMETER(DestAddr);
    UNREFERENCED_PARAMETER(Flags);
    /* TODO: client tracking needs the network experiments of the qWAVE service */
    return QwaveFail(ERROR_NOT_SUPPORTED);
}

BOOL
WINAPI
QOSStopTrackingClient(
    _In_ HANDLE QOSHandle,
    _In_ PSOCKADDR DestAddr,
    _Reserved_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(QOSHandle);
    UNREFERENCED_PARAMETER(DestAddr);
    UNREFERENCED_PARAMETER(Flags);
    /* TODO: client tracking needs the network experiments of the qWAVE service */
    return QwaveFail(ERROR_NOT_SUPPORTED);
}
