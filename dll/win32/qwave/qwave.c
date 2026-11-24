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
#include <winsvc.h>
#include <ws2tcpip.h>
#include <qos2.h>
#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <libernt/qwavedrv.h>
#include "qwave_private.h"

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
C_ASSERT(QOSQueryFlowFundamentals == QWAVE_QUERY_FUNDAMENTALS);
C_ASSERT(QOSQueryPacketPriority == QWAVE_QUERY_PACKET_PRIORITY);
C_ASSERT(QOSQueryOutgoingRate == QWAVE_QUERY_OUTGOING_RATE);
C_ASSERT(QOS_QUERYFLOW_FRESH == QWAVE_QUERYFLOW_FRESH);
C_ASSERT(sizeof(QOS_PACKET_PRIORITY) == sizeof(QWAVE_PACKET_PRIORITY));
C_ASSERT(sizeof(QOS_FLOW_FUNDAMENTALS) == sizeof(QWAVE_FUNDAMENTALS));
C_ASSERT(FIELD_OFFSET(QOS_FLOW_FUNDAMENTALS, BottleneckBandwidth) == FIELD_OFFSET(QWAVE_FUNDAMENTALS, BottleneckBandwidth));
C_ASSERT(FIELD_OFFSET(QOS_FLOW_FUNDAMENTALS, AvailableBandwidthSet) == FIELD_OFFSET(QWAVE_FUNDAMENTALS, AvailableBandwidthSet));
C_ASSERT(FIELD_OFFSET(QOS_FLOW_FUNDAMENTALS, AvailableBandwidth) == FIELD_OFFSET(QWAVE_FUNDAMENTALS, AvailableBandwidth));
C_ASSERT(FIELD_OFFSET(QOS_FLOW_FUNDAMENTALS, RTTSet) == FIELD_OFFSET(QWAVE_FUNDAMENTALS, RttSet));
C_ASSERT(FIELD_OFFSET(QOS_FLOW_FUNDAMENTALS, RTT) == FIELD_OFFSET(QWAVE_FUNDAMENTALS, Rtt));

#define QWAVE_SERVICE_NAME L"QWAVE"
#define QWAVE_SERVICE_START_POLLS 200
#define QWAVE_SERVICE_START_POLL_MS 10

C_ASSERT(QOSNotifyCongested == QWAVE_NOTIFY_CONGESTED);
C_ASSERT(QOSNotifyUncongested == QWAVE_NOTIFY_UNCONGESTED);
C_ASSERT(QOSNotifyAvailable == QWAVE_NOTIFY_AVAILABLE);

static SERVICE_STATUS_HANDLE QwaveServiceStatusHandle;
static HANDLE QwaveController;

static
BOOL
QwaveFail(
    _In_ DWORD Error)
{
    SetLastError(Error);
    return FALSE;
}

BOOL
QwaveToAddress(
    _In_ const SOCKADDR *Address,
    _Out_ PQWAVE_ADDRESS Out)
{
    RtlZeroMemory(Out, sizeof(*Out));
    if (Address->sa_family == AF_INET)
    {
        const SOCKADDR_IN *In = (const SOCKADDR_IN *)Address;

        Out->Family = AF_INET;
        Out->Port = In->sin_port;
        RtlCopyMemory(Out->Address, &In->sin_addr, sizeof(In->sin_addr));
        return TRUE;
    }
    if (Address->sa_family == AF_INET6)
    {
        const SOCKADDR_IN6 *In6 = (const SOCKADDR_IN6 *)Address;

        Out->Family = AF_INET6;
        Out->Port = In6->sin6_port;
        Out->ScopeId = In6->sin6_scope_id;
        RtlCopyMemory(Out->Address, &In6->sin6_addr, sizeof(In6->sin6_addr));
        return TRUE;
    }
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

static
NTSTATUS
QwaveOpenDevice(
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(QWAVE_DEVICE_NAME);
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return NtCreateFile(Handle, GENERIC_READ, &Attributes, &Iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_OPEN, 0, NULL, 0);
}

static
DWORD
QwaveWaitForService(
    _In_ SC_HANDLE Service)
{
    SERVICE_STATUS_PROCESS Status;
    DWORD Needed, Poll;

    for (Poll = 0; Poll < QWAVE_SERVICE_START_POLLS; Poll++)
    {
        if (!QueryServiceStatusEx(Service, SC_STATUS_PROCESS_INFO, (LPBYTE)&Status, sizeof(Status), &Needed))
            return GetLastError();
        if (Status.dwCurrentState == SERVICE_RUNNING)
            return ERROR_SUCCESS;
        if (Status.dwCurrentState == SERVICE_STOPPED)
        {
            if (Status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR)
                return Status.dwServiceSpecificExitCode;
            return Status.dwWin32ExitCode ? Status.dwWin32ExitCode : ERROR_SERVICE_DEPENDENCY_FAIL;
        }
        Sleep(QWAVE_SERVICE_START_POLL_MS);
    }
    return ERROR_SERVICE_REQUEST_TIMEOUT;
}

static
DWORD
QwaveStartService(VOID)
{
    SC_HANDLE Scm, Service;
    DWORD Error = ERROR_SUCCESS;

    Scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!Scm)
        return ERROR_SERVICE_DEPENDENCY_FAIL;
    Service = OpenServiceW(Scm, QWAVE_SERVICE_NAME, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!Service)
    {
        CloseServiceHandle(Scm);
        return ERROR_SERVICE_DEPENDENCY_FAIL;
    }
    if (!StartServiceW(Service, 0, NULL) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
        Error = GetLastError();
    if (Error == ERROR_SUCCESS)
        Error = QwaveWaitForService(Service);
    CloseServiceHandle(Service);
    CloseServiceHandle(Scm);
    return Error;
}

BOOL
WINAPI
QOSCreateHandle(
    _In_ PQOS_VERSION Version,
    _Out_ PHANDLE QOSHandle)
{
    HANDLE Handle;
    NTSTATUS Status;
    DWORD Error;

    if (!Version || !QOSHandle || Version->MajorVersion != 1 || Version->MinorVersion != 0)
        return QwaveFail(ERROR_INVALID_PARAMETER);

    Status = QwaveOpenDevice(&Handle);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        Error = QwaveStartService();
        if (Error != ERROR_SUCCESS)
            return QwaveFail(Error);
        Status = QwaveOpenDevice(&Handle);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
            return QwaveFail(ERROR_SERVICE_DEPENDENCY_FAIL);
    }
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
        DestAddr = (PSOCKADDR)&Peer;
    }
    if (!QwaveToAddress(DestAddr, &Input.Destination))
        return QwaveFail(ERROR_INVALID_PARAMETER);

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
    PQWAVE_FLOW_LIST List;
    ULONG Capacity, Length, Total, Returned;

    if (!Size || (*Size != 0 && !Buffer))
        return QwaveFail(ERROR_INVALID_PARAMETER);

    Capacity = min(*Size / sizeof(QOS_FLOWID), (MAXULONG - FIELD_OFFSET(QWAVE_FLOW_LIST, FlowIds)) / sizeof(ULONG));
    Length = FIELD_OFFSET(QWAVE_FLOW_LIST, FlowIds) + Capacity * sizeof(ULONG);
    List = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!List)
        return QwaveFail(ERROR_NOT_ENOUGH_MEMORY);
    if (!QwaveControl(QOSHandle, IOCTL_QWAVE_ENUMERATE_FLOWS, NULL, 0, List, Length, NULL))
    {
        DWORD Error = GetLastError();

        HeapFree(GetProcessHeap(), 0, List);
        return QwaveFail(Error);
    }
    Total = List->Total;
    Returned = List->Returned;
    if (Total > Capacity)
    {
        HeapFree(GetProcessHeap(), 0, List);
        *Size = Total * sizeof(QOS_FLOWID);
        return QwaveFail(ERROR_INSUFFICIENT_BUFFER);
    }
    RtlCopyMemory(Buffer, List->FlowIds, Returned * sizeof(QOS_FLOWID));
    HeapFree(GetProcessHeap(), 0, List);
    *Size = Returned * sizeof(QOS_FLOWID);
    return TRUE;
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
    QWAVE_QUERY_FLOW_INPUT Input;
    ULONG Needed;

    if (!Size)
        return QwaveFail(ERROR_INVALID_PARAMETER);
    switch (Operation)
    {
        case QOSQueryFlowFundamentals:
            Needed = sizeof(QOS_FLOW_FUNDAMENTALS);
            break;
        case QOSQueryPacketPriority:
            Needed = sizeof(QOS_PACKET_PRIORITY);
            break;
        case QOSQueryOutgoingRate:
            Needed = sizeof(UINT64);
            break;
        default:
            return QwaveFail(ERROR_INVALID_PARAMETER);
    }
    if (*Size < Needed)
    {
        *Size = Needed;
        return QwaveFail(ERROR_INSUFFICIENT_BUFFER);
    }
    if (!Buffer)
        return QwaveFail(ERROR_INVALID_PARAMETER);

    Input.FlowId = FlowId;
    Input.Operation = (ULONG)Operation;
    Input.Flags = Flags;
    Input.Reserved = 0;
    *Size = Needed;
    return QwaveControl(QOSHandle, IOCTL_QWAVE_QUERY_FLOW, &Input, sizeof(Input), Buffer, Needed, Overlapped);
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
    QWAVE_NOTIFY_INPUT Input = { 0 };
    ULONG OutputSize = 0;

    if (Operation == QOSNotifyAvailable)
    {
        if (!Size || *Size < sizeof(UINT64) || !Buffer)
        {
            if (Size)
                *Size = sizeof(UINT64);
            return QwaveFail((Size && *Size < sizeof(UINT64)) ? ERROR_INSUFFICIENT_BUFFER : ERROR_INVALID_PARAMETER);
        }
        Input.Threshold = *(PUINT64)Buffer;
        OutputSize = sizeof(UINT64);
    }
    else if (Operation == QOSNotifyCongested || Operation == QOSNotifyUncongested)
    {
        if (Buffer)
            return QwaveFail(ERROR_INVALID_PARAMETER);
    }
    else
    {
        return QwaveFail(ERROR_INVALID_PARAMETER);
    }

    Input.FlowId = FlowId;
    Input.Operation = (ULONG)Operation;
    Input.Flags = Flags;
    if (Size)
        *Size = OutputSize;
    return QwaveControl(QOSHandle, IOCTL_QWAVE_NOTIFY_FLOW, &Input, sizeof(Input), OutputSize ? Buffer : NULL,
                        OutputSize, Overlapped);
}

static
BOOL
QwaveTrackClient(
    _In_ HANDLE QOSHandle,
    _In_ PSOCKADDR DestAddr,
    _In_ DWORD Flags,
    _In_ BOOL Start)
{
    QWAVE_TRACK_INPUT Input = { 0 };

    if (!DestAddr || !QwaveToAddress(DestAddr, &Input.Client))
        return QwaveFail(ERROR_INVALID_PARAMETER);
    Input.Start = Start;
    Input.Flags = Flags;
    return QwaveControl(QOSHandle, IOCTL_QWAVE_TRACK_CLIENT, &Input, sizeof(Input), NULL, 0, NULL);
}

BOOL
WINAPI
QOSStartTrackingClient(
    _In_ HANDLE QOSHandle,
    _In_ PSOCKADDR DestAddr,
    _Reserved_ DWORD Flags)
{
    return QwaveTrackClient(QOSHandle, DestAddr, Flags, TRUE);
}

BOOL
WINAPI
QOSStopTrackingClient(
    _In_ HANDLE QOSHandle,
    _In_ PSOCKADDR DestAddr,
    _Reserved_ DWORD Flags)
{
    return QwaveTrackClient(QOSHandle, DestAddr, Flags, FALSE);
}

VOID
QwaveReportServiceStatus(
    _In_ DWORD State,
    _In_ DWORD ExitCode)
{
    SERVICE_STATUS Status = { 0 };

    Status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    Status.dwCurrentState = State;
    Status.dwControlsAccepted = (State == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    Status.dwWin32ExitCode = ExitCode;
    SetServiceStatus(QwaveServiceStatusHandle, &Status);
}

static
DWORD
WINAPI
QwaveServiceControl(
    _In_ DWORD Control,
    _In_ DWORD EventType,
    _In_ LPVOID EventData,
    _In_ LPVOID Context)
{
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventData);
    UNREFERENCED_PARAMETER(Context);

    switch (Control)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            QwaveReportServiceStatus(SERVICE_STOP_PENDING, ERROR_SUCCESS);
            QwaveStopController(QwaveController);
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

VOID
WINAPI
ServiceMain(
    _In_ DWORD Argc,
    _In_ LPWSTR *Argv)
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    DWORD Error;

    QwaveServiceStatusHandle = RegisterServiceCtrlHandlerExW(QWAVE_SERVICE_NAME, QwaveServiceControl, NULL);
    if (!QwaveServiceStatusHandle)
        return;
    QwaveReportServiceStatus(SERVICE_START_PENDING, ERROR_SUCCESS);
    Error = QwaveStartController(QwaveReportServiceStatus, &QwaveController);
    if (Error != ERROR_SUCCESS)
    {
        QwaveReportServiceStatus(SERVICE_STOPPED, Error);
        return;
    }
    QwaveReportServiceStatus(SERVICE_RUNNING, ERROR_SUCCESS);
}
