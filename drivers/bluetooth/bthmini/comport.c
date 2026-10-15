/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Serial Port Profile COM ports over RFCOMM
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "bthmini.h"
#include <devguid.h>

#define NDEBUG
#include <debug.h>

#define BTH_SERIAL_PORT_UUID16      0x1101
#define BTH_COM_FIRST_PORT          3
#define BTH_COM_LAST_PORT           256
#define BTH_COM_MODEM_STATUS        (SERIAL_CTS_STATE | SERIAL_DSR_STATE | SERIAL_DCD_STATE)

typedef struct _BTHMINI_COMPORT_CONTEXT
{
    PBTHMINI_COMPORT Port;
} BTHMINI_COMPORT_CONTEXT, *PBTHMINI_COMPORT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHMINI_COMPORT_CONTEXT, BthComGetContext)

static LONG BthComNextIndex = -1;

static
ULONGLONG
BthComNow(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
BOOLEAN
BthComPortNameInUse(
    _In_ ULONG Number)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    WCHAR Buffer[32];
    HANDLE Link;

    RtlStringCchPrintfW(Buffer, RTL_NUMBER_OF(Buffer), L"\\DosDevices\\COM%lu", Number);
    RtlInitUnicodeString(&Name, Buffer);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenSymbolicLinkObject(&Link, SYMBOLIC_LINK_QUERY, &Attributes)))
        return FALSE;
    ZwClose(Link);
    return TRUE;
}

static
VOID
BthComSetSerialComm(
    _In_ PBTHMINI_COMPORT Port,
    _In_ BOOLEAN Present)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\SERIALCOMM");
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ValueName;
    WCHAR Device[32], PortName[16];
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateKey(&Key, KEY_SET_VALUE, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL)))
        return;
    RtlStringCchPrintfW(Device, RTL_NUMBER_OF(Device), L"\\Device\\BthModem%lu", Port->Index);
    RtlInitUnicodeString(&ValueName, Device);
    if (Present)
    {
        RtlStringCchPrintfW(PortName, RTL_NUMBER_OF(PortName), L"COM%lu", Port->PortNumber);
        ZwSetValueKey(Key, &ValueName, 0, REG_SZ, PortName, (ULONG)(wcslen(PortName) + 1) * sizeof(WCHAR));
    }
    else
    {
        ZwDeleteValueKey(Key, &ValueName);
    }
    ZwClose(Key);
}

static
ULONG
BthComCopyOut(
    _In_ PBTHMINI_COMPORT Port,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Copied = 0;

    while (Copied < Length && Port->RxCount)
    {
        ULONG Chunk = min(Length - Copied, min(Port->RxCount, sizeof(Port->RxBuffer) - Port->RxHead));

        RtlCopyMemory(Buffer + Copied, Port->RxBuffer + Port->RxHead, Chunk);
        Port->RxHead = (Port->RxHead + Chunk) % sizeof(Port->RxBuffer);
        Port->RxCount -= Chunk;
        Copied += Chunk;
    }
    if (Copied && Port->Dlc && Port->Open)
        BthRfcommGrantCredits(Port->Dev, Port->Dlc);
    return Copied;
}

static
BOOLEAN
BthComReadImmediate(
    _In_ PBTHMINI_COMPORT Port)
{
    return Port->Timeouts.ReadIntervalTimeout == MAXULONG &&
           !Port->Timeouts.ReadTotalTimeoutMultiplier && !Port->Timeouts.ReadTotalTimeoutConstant;
}

static
VOID
BthComServiceReads(
    _In_ PBTHMINI_COMPORT Port,
    _In_ BOOLEAN Expired)
{
    for (;;)
    {
        WDFREQUEST Request;
        PUCHAR Buffer;
        size_t Length;
        ULONG Copied;
        ULONG_PTR Done;

        if (!NT_SUCCESS(WdfIoQueueFindRequest(Port->ReadQueue, NULL, NULL, NULL, &Request)))
            return;
        if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, 1, (PVOID *)&Buffer, &Length)))
        {
            WdfObjectDereference(Request);
            return;
        }
        Done = (ULONG_PTR)WdfRequestGetInformation(Request);
        if (!Port->RxCount && !Expired && Port->Open)
        {
            WdfObjectDereference(Request);
            return;
        }
        Copied = BthComCopyOut(Port, Buffer + Done, (ULONG)(Length - Done));
        WdfRequestSetInformation(Request, Done + Copied);
        if (Done + Copied < Length && !Expired && Port->Open && !Port->Timeouts.ReadIntervalTimeout)
        {
            WdfObjectDereference(Request);
            return;
        }
        {
            WDFREQUEST Found = Request;

            if (NT_SUCCESS(WdfIoQueueRetrieveFoundRequest(Port->ReadQueue, Found, &Request)))
                WdfRequestCompleteWithInformation(Request, Port->Open || Done + Copied ? STATUS_SUCCESS : STATUS_DEVICE_NOT_CONNECTED,
                                                  Done + Copied);
            WdfObjectDereference(Found);
        }
        Expired = FALSE;
        Port->ReadDeadline = 0;
    }
}

static
VOID
BthComServiceWaits(
    _In_ PBTHMINI_COMPORT Port)
{
    WDFREQUEST Request;

    if (!(Port->WaitMask & Port->Events))
        return;
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Port->WaitQueue, &Request)))
    {
        PULONG Mask;

        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID *)&Mask, NULL)))
        {
            *Mask = Port->WaitMask & Port->Events;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
        }
        else
        {
            WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        }
    }
    Port->Events = 0;
}

static
VOID
BthComDlcOpened(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ NTSTATUS Status)
{
    PBTHMINI_COMPORT Port = Dlc->Context;
    WDFREQUEST Create = Port->PendingCreate;

    UNREFERENCED_PARAMETER(Dev);
    Port->PendingCreate = NULL;
    if (NT_SUCCESS(Status))
    {
        Port->Open = TRUE;
        DPRINT1("BTHMINI: COM%lu connected\n", Port->PortNumber);
    }
    else
    {
        Port->Dlc = NULL;
        DPRINT1("BTHMINI: COM%lu connection failed 0x%08lx\n", Port->PortNumber, Status);
    }
    if (Create)
        WdfRequestComplete(Create, NT_SUCCESS(Status) ? STATUS_SUCCESS :
                                   (Status == STATUS_IO_TIMEOUT ? STATUS_IO_TIMEOUT : STATUS_DEVICE_NOT_CONNECTED));
}

static
VOID
BthComDlcReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_COMPORT Port = Dlc->Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Dev);
    for (Index = 0; Index < Length && Port->RxCount < sizeof(Port->RxBuffer); Index++)
    {
        Port->RxBuffer[(Port->RxHead + Port->RxCount) % sizeof(Port->RxBuffer)] = Data[Index];
        Port->RxCount++;
    }
    if (Index < Length)
        DPRINT1("BTHMINI: COM%lu receive buffer full, %lu bytes dropped\n", Port->PortNumber, Length - Index);
    Port->Events |= SERIAL_EV_RXCHAR;
    BthComServiceReads(Port, FALSE);
    BthComServiceWaits(Port);
}

static
VOID
BthComDlcClosed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_DLC Dlc,
    _In_ NTSTATUS Status)
{
    PBTHMINI_COMPORT Port = Dlc->Context;

    UNREFERENCED_PARAMETER(Dev);
    Port->Dlc = NULL;
    Port->Open = FALSE;
    Port->Events |= SERIAL_EV_RLSD;
    DPRINT1("BTHMINI: COM%lu disconnected 0x%08lx\n", Port->PortNumber, Status);
    BthComServiceReads(Port, TRUE);
    BthComServiceWaits(Port);
}

static
ULONG
BthComDlcRoom(
    _In_ PBTHMINI_DLC Dlc)
{
    PBTHMINI_COMPORT Port = Dlc->Context;

    return sizeof(Port->RxBuffer) - Port->RxCount;
}

static const BTHMINI_DLC_CALLBACKS BthComDlcCallbacks =
{
    BthComDlcOpened,
    BthComDlcReceive,
    BthComDlcClosed,
    BthComDlcRoom
};

static
NTSTATUS
BthComIoctl(
    _In_ PBTHMINI_COMPORT Port,
    _In_ WDFREQUEST Request,
    _In_ ULONG Code,
    _Out_ PULONG_PTR Information)
{
    PVOID Buffer;
    NTSTATUS Status;

    *Information = 0;
    switch (Code)
    {
        case IOCTL_SERIAL_SET_BAUD_RATE:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_BAUD_RATE), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Port->BaudRate = *(PSERIAL_BAUD_RATE)Buffer;
            return Status;

        case IOCTL_SERIAL_GET_BAUD_RATE:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_BAUD_RATE), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_BAUD_RATE)Buffer = Port->BaudRate;
                *Information = sizeof(SERIAL_BAUD_RATE);
            }
            return Status;

        case IOCTL_SERIAL_SET_LINE_CONTROL:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Port->LineControl = *(PSERIAL_LINE_CONTROL)Buffer;
            return Status;

        case IOCTL_SERIAL_GET_LINE_CONTROL:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_LINE_CONTROL)Buffer = Port->LineControl;
                *Information = sizeof(SERIAL_LINE_CONTROL);
            }
            return Status;

        case IOCTL_SERIAL_SET_HANDFLOW:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_HANDFLOW), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Port->HandFlow = *(PSERIAL_HANDFLOW)Buffer;
            return Status;

        case IOCTL_SERIAL_GET_HANDFLOW:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_HANDFLOW), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_HANDFLOW)Buffer = Port->HandFlow;
                *Information = sizeof(SERIAL_HANDFLOW);
            }
            return Status;

        case IOCTL_SERIAL_SET_CHARS:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_CHARS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Port->Chars = *(PSERIAL_CHARS)Buffer;
            return Status;

        case IOCTL_SERIAL_GET_CHARS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_CHARS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_CHARS)Buffer = Port->Chars;
                *Information = sizeof(SERIAL_CHARS);
            }
            return Status;

        case IOCTL_SERIAL_SET_TIMEOUTS:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_TIMEOUTS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Port->Timeouts = *(PSERIAL_TIMEOUTS)Buffer;
            return Status;

        case IOCTL_SERIAL_GET_TIMEOUTS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_TIMEOUTS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PSERIAL_TIMEOUTS)Buffer = Port->Timeouts;
                *Information = sizeof(SERIAL_TIMEOUTS);
            }
            return Status;

        case IOCTL_SERIAL_GET_MODEMSTATUS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PULONG)Buffer = Port->Open ? BTH_COM_MODEM_STATUS : 0;
                *Information = sizeof(ULONG);
            }
            return Status;

        case IOCTL_SERIAL_GET_DTRRTS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PULONG)Buffer = SERIAL_DTR_STATE | SERIAL_RTS_STATE;
                *Information = sizeof(ULONG);
            }
            return Status;

        case IOCTL_SERIAL_GET_COMMSTATUS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_STATUS), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                RtlZeroMemory(Buffer, sizeof(SERIAL_STATUS));
                ((PSERIAL_STATUS)Buffer)->AmountInInQueue = Port->RxCount;
                *Information = sizeof(SERIAL_STATUS);
            }
            return Status;

        case IOCTL_SERIAL_GET_PROPERTIES:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_COMMPROP), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                PSERIAL_COMMPROP Properties = Buffer;

                RtlZeroMemory(Properties, sizeof(*Properties));
                Properties->PacketLength = sizeof(*Properties);
                Properties->PacketVersion = 2;
                Properties->ServiceMask = SERIAL_SP_SERIALCOMM;
                Properties->MaxTxQueue = 0;
                Properties->MaxRxQueue = sizeof(Port->RxBuffer);
                Properties->MaxBaud = SERIAL_BAUD_USER;
                Properties->ProvSubType = SERIAL_SP_RS232;
                Properties->ProvCapabilities = SERIAL_PCF_DTRDSR | SERIAL_PCF_RTSCTS | SERIAL_PCF_CD |
                                               SERIAL_PCF_TOTALTIMEOUTS | SERIAL_PCF_INTTIMEOUTS;
                Properties->SettableParams = SERIAL_SP_BAUD | SERIAL_SP_DATABITS | SERIAL_SP_STOPBITS |
                                             SERIAL_SP_PARITY | SERIAL_SP_HANDSHAKING;
                Properties->SettableBaud = SERIAL_BAUD_USER | SERIAL_BAUD_115200 | SERIAL_BAUD_57600 | SERIAL_BAUD_9600;
                Properties->SettableData = SERIAL_DATABITS_8;
                Properties->SettableStopParity = SERIAL_STOPBITS_10 | SERIAL_PARITY_NONE;
                Properties->CurrentRxQueue = sizeof(Port->RxBuffer);
                *Information = sizeof(*Properties);
            }
            return Status;

        case IOCTL_SERIAL_SET_WAIT_MASK:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                WDFREQUEST Waiting;

                Port->WaitMask = *(PULONG)Buffer;
                while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Port->WaitQueue, &Waiting)))
                {
                    PULONG Mask;

                    if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Waiting, sizeof(ULONG), (PVOID *)&Mask, NULL)))
                    {
                        *Mask = 0;
                        WdfRequestCompleteWithInformation(Waiting, STATUS_SUCCESS, sizeof(ULONG));
                    }
                    else
                    {
                        WdfRequestComplete(Waiting, STATUS_CANCELLED);
                    }
                }
                Port->Events = 0;
            }
            return Status;

        case IOCTL_SERIAL_GET_WAIT_MASK:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PULONG)Buffer = Port->WaitMask;
                *Information = sizeof(ULONG);
            }
            return Status;

        case IOCTL_SERIAL_WAIT_ON_MASK:
            if (!Port->WaitMask)
                return STATUS_INVALID_PARAMETER;
            if (Port->RxCount && (Port->WaitMask & SERIAL_EV_RXCHAR))
                Port->Events |= SERIAL_EV_RXCHAR;
            Status = WdfRequestForwardToIoQueue(Request, Port->WaitQueue);
            if (NT_SUCCESS(Status))
            {
                BthComServiceWaits(Port);
                return STATUS_PENDING;
            }
            return Status;

        case IOCTL_SERIAL_PURGE:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (!NT_SUCCESS(Status))
                return Status;
            if (*(PULONG)Buffer & SERIAL_PURGE_RXABORT)
            {
                WDFREQUEST Pending;

                while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Port->ReadQueue, &Pending)))
                    WdfRequestCompleteWithInformation(Pending, STATUS_CANCELLED, WdfRequestGetInformation(Pending));
            }
            if (*(PULONG)Buffer & SERIAL_PURGE_RXCLEAR)
            {
                Port->RxHead = 0;
                Port->RxCount = 0;
            }
            return STATUS_SUCCESS;

        case IOCTL_SERIAL_SET_QUEUE_SIZE:
        case IOCTL_SERIAL_SET_DTR:
        case IOCTL_SERIAL_CLR_DTR:
        case IOCTL_SERIAL_SET_RTS:
        case IOCTL_SERIAL_CLR_RTS:
        case IOCTL_SERIAL_SET_XON:
        case IOCTL_SERIAL_SET_XOFF:
        case IOCTL_SERIAL_SET_BREAK_ON:
        case IOCTL_SERIAL_SET_BREAK_OFF:
        case IOCTL_SERIAL_RESET_DEVICE:
        case IOCTL_SERIAL_CLEAR_STATS:
            return STATUS_SUCCESS;

        case IOCTL_SERIAL_CONFIG_SIZE:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (NT_SUCCESS(Status))
            {
                *(PULONG)Buffer = 0;
                *Information = sizeof(ULONG);
            }
            return Status;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
VOID
BthComProcessRequest(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_COMPORT Port,
    _In_ WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS Parameters;
    ULONG_PTR Information = 0;
    PBTHMINI_REMOTE Remote;
    PVOID Buffer;
    size_t Length;
    NTSTATUS Status;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    switch (Parameters.Type)
    {
        case WdfRequestTypeCreate:
            if (Port->Dlc || Port->PendingCreate)
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }
            Remote = BthRadioFindRemote(Dev, Port->Address);
            if (!Remote || !(Remote->Flags & BDIF_PAIRED))
            {
                Status = STATUS_DEVICE_NOT_CONNECTED;
                break;
            }
            Port->RxHead = 0;
            Port->RxCount = 0;
            Port->Events = 0;
            Port->CleanupRequested = 0;
            Port->PendingCreate = Request;
            Status = BthRfcommConnect(Dev, Remote, Port->ServerChannel, &BthComDlcCallbacks, Port, &Port->Dlc);
            if (Status == STATUS_PENDING)
                return;
            Port->PendingCreate = NULL;
            Port->Dlc = NULL;
            break;

        case WdfRequestTypeRead:
            Status = WdfRequestRetrieveOutputBuffer(Request, 1, &Buffer, &Length);
            if (!NT_SUCCESS(Status))
                break;
            if (Port->RxCount || BthComReadImmediate(Port) || !Port->Open)
            {
                Information = BthComCopyOut(Port, Buffer, (ULONG)Length);
                Status = Port->Open || Information ? STATUS_SUCCESS : STATUS_DEVICE_NOT_CONNECTED;
                break;
            }
            WdfRequestSetInformation(Request, 0);
            Status = WdfRequestForwardToIoQueue(Request, Port->ReadQueue);
            if (!NT_SUCCESS(Status))
                break;
            if (Port->Timeouts.ReadTotalTimeoutConstant || Port->Timeouts.ReadTotalTimeoutMultiplier)
                Port->ReadDeadline = BthComNow() + Port->Timeouts.ReadTotalTimeoutConstant +
                                     (ULONGLONG)Port->Timeouts.ReadTotalTimeoutMultiplier * Length;
            return;

        case WdfRequestTypeWrite:
            Status = WdfRequestRetrieveInputBuffer(Request, 0, &Buffer, &Length);
            if (!NT_SUCCESS(Status))
                break;
            if (!Port->Open || !Port->Dlc)
            {
                Status = STATUS_DEVICE_NOT_CONNECTED;
                break;
            }
            Status = Length ? BthRfcommSend(Dev, Port->Dlc, Buffer, (ULONG)Length) : STATUS_SUCCESS;
            if (NT_SUCCESS(Status))
                Information = Length;
            break;

        case WdfRequestTypeDeviceControl:
            Status = BthComIoctl(Port, Request, Parameters.Parameters.DeviceIoControl.IoControlCode, &Information);
            if (Status == STATUS_PENDING)
                return;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    WdfRequestCompleteWithInformation(Request, Status, NT_SUCCESS(Status) ? Information : 0);
}

ULONG
BthComProcess(
    _In_ PBTHMINI_DEVICE Dev)
{
    ULONGLONG Now = BthComNow(), Next = Now + 1000;
    PLIST_ENTRY Entry;

    for (Entry = Dev->ComPorts.Flink; Entry != &Dev->ComPorts; Entry = Entry->Flink)
    {
        PBTHMINI_COMPORT Port = CONTAINING_RECORD(Entry, BTHMINI_COMPORT, Link);
        WDFREQUEST Request;

        if (InterlockedExchange(&Port->CleanupRequested, 0))
        {
            if (Port->Dlc)
                BthRfcommDisconnect(Dev, Port->Dlc);
            Port->Open = FALSE;
            Port->RxHead = 0;
            Port->RxCount = 0;
        }
        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Port->Queue, &Request)))
            BthComProcessRequest(Dev, Port, Request);
        if (Port->ReadDeadline)
        {
            if (Now >= Port->ReadDeadline)
                BthComServiceReads(Port, TRUE);
            else
                Next = min(Next, Port->ReadDeadline);
        }
    }
    return (ULONG)(Next - Now);
}

static
VOID
NTAPI
BthComEvtQueueReady(
    _In_ WDFQUEUE Queue,
    _In_ WDFCONTEXT Context)
{
    PBTHMINI_DEVICE Dev = Context;

    UNREFERENCED_PARAMETER(Queue);
    KeSetEvent(&Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
BthComEvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject)
{
    PBTHMINI_COMPORT Port = BthComGetContext(WdfFileObjectGetDevice(FileObject))->Port;

    InterlockedExchange(&Port->CleanupRequested, 1);
    KeSetEvent(&Port->Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
NTAPI
BthComEvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PBTHMINI_COMPORT Port = BthComGetContext(Device)->Port;
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"PortName");
    WCHAR PortName[16];
    WDFKEY Key;

    UNREFERENCED_PARAMETER(PreviousState);
    RtlStringCchPrintfW(PortName, RTL_NUMBER_OF(PortName), L"COM%lu", Port->PortNumber);
    if (NT_SUCCESS(WdfDeviceOpenRegistryKey(Device, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &Key)))
    {
        UNICODE_STRING Value;

        RtlInitUnicodeString(&Value, PortName);
        if (!NT_SUCCESS(WdfRegistryAssignUnicodeString(Key, &ValueName, &Value)))
            DPRINT1("BTHMINI: COM%lu port name not stored\n", Port->PortNumber);
        WdfRegistryClose(Key);
    }
    BthComSetSerialComm(Port, TRUE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BthComEvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(TargetState);
    BthComSetSerialComm(BthComGetContext(Device)->Port, FALSE);
    return STATUS_SUCCESS;
}

static
PBTHMINI_COMPORT
BthComFind(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ BTH_ADDR Address,
    _In_ UCHAR ServerChannel)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->ComPorts.Flink; Entry != &Dev->ComPorts; Entry = Entry->Flink)
    {
        PBTHMINI_COMPORT Port = CONTAINING_RECORD(Entry, BTHMINI_COMPORT, Link);

        if (Port->Address == Address && Port->ServerChannel == ServerChannel)
            return Port;
    }
    return NULL;
}

static
NTSTATUS
BthComCreatePort(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR ServerChannel)
{
    WCHAR DeviceName[32], HardwareId[96], InstanceId[32], Description[64], LinkName[32];
    UNICODE_STRING Name, Hardware, Compatible, Instance, Text, Link;
    WDF_PNPPOWER_EVENT_CALLBACKS Callbacks;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_FILEOBJECT_CONFIG FileConfig;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PBTHMINI_COMPORT Port;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Child;
    ULONG Number;
    NTSTATUS Status;

    if (BthComFind(Dev, Remote->Address, ServerChannel))
        return STATUS_SUCCESS;
    for (Number = BTH_COM_FIRST_PORT; Number <= BTH_COM_LAST_PORT && BthComPortNameInUse(Number); Number++)
        ;
    if (Number > BTH_COM_LAST_PORT)
        return STATUS_INSUFFICIENT_RESOURCES;

    Port = ExAllocatePoolZero(NonPagedPool, sizeof(*Port), BTHMINI_TAG);
    if (!Port)
        return STATUS_INSUFFICIENT_RESOURCES;
    Port->Dev = Dev;
    Port->Address = Remote->Address;
    Port->ServerChannel = ServerChannel;
    Port->PortNumber = Number;
    Port->Index = (ULONG)InterlockedIncrement(&BthComNextIndex);
    Port->BaudRate.BaudRate = 115200;
    Port->LineControl.WordLength = 8;
    Port->LineControl.StopBits = STOP_BIT_1;
    Port->LineControl.Parity = NO_PARITY;
    Port->HandFlow.ControlHandShake = SERIAL_DTR_CONTROL;
    Port->HandFlow.FlowReplace = SERIAL_RTS_CONTROL;

    RtlStringCchPrintfW(DeviceName, RTL_NUMBER_OF(DeviceName), L"\\Device\\BthModem%lu", Port->Index);
    RtlStringCchPrintfW(HardwareId, RTL_NUMBER_OF(HardwareId),
                        L"BTHENUM\\{00001101-0000-1000-8000-00805f9b34fb}_LOCALMFG&%04x", Dev->Manufacturer);
    RtlStringCchPrintfW(InstanceId, RTL_NUMBER_OF(InstanceId), L"%012I64X_C%08X", Remote->Address, ServerChannel);
    RtlStringCchPrintfW(Description, RTL_NUMBER_OF(Description), L"Standard Serial over Bluetooth link (COM%lu)", Number);
    RtlStringCchPrintfW(LinkName, RTL_NUMBER_OF(LinkName), L"\\DosDevices\\COM%lu", Number);
    RtlInitUnicodeString(&Name, DeviceName);
    RtlInitUnicodeString(&Hardware, HardwareId);
    RtlInitUnicodeString(&Compatible, L"BTHENUM\\{00001101-0000-1000-8000-00805f9b34fb}");
    RtlInitUnicodeString(&Instance, InstanceId);
    RtlInitUnicodeString(&Text, Description);
    RtlInitUnicodeString(&Link, LinkName);

    Init = WdfPdoInitAllocate(Dev->Device);
    if (!Init)
    {
        ExFreePoolWithTag(Port, BTHMINI_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_SERIAL_PORT);
    WdfDeviceInitSetExclusive(Init, TRUE);
    Status = WdfDeviceInitAssignName(Init, &Name);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignRawDevice(Init, &GUID_DEVCLASS_PORTS);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignDeviceID(Init, &Hardware);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddHardwareID(Init, &Hardware);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddHardwareID(Init, &Compatible);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddCompatibleID(Init, &Compatible);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignInstanceID(Init, &Instance);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddDeviceText(Init, &Text, &Text, 0x409);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        ExFreePoolWithTag(Port, BTHMINI_TAG);
        return Status;
    }
    WdfPdoInitSetDefaultLocale(Init, 0x409);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Callbacks);
    Callbacks.EvtDeviceD0Entry = BthComEvtD0Entry;
    Callbacks.EvtDeviceD0Exit = BthComEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(Init, &Callbacks);
    WDF_FILEOBJECT_CONFIG_INIT(&FileConfig, NULL, NULL, BthComEvtFileCleanup);
    WdfDeviceInitSetFileObjectConfig(Init, &FileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_COMPORT_CONTEXT);
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        ExFreePoolWithTag(Port, BTHMINI_TAG);
        return Status;
    }
    BthComGetContext(Child)->Port = Port;
    Port->Device = Child;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchManual);
    QueueConfig.PowerManaged = WdfFalse;
    Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Port->Queue);
    if (NT_SUCCESS(Status))
        Status = WdfDeviceConfigureRequestDispatching(Child, Port->Queue, WdfRequestTypeCreate);
    if (NT_SUCCESS(Status))
        Status = WdfIoQueueReadyNotify(Port->Queue, BthComEvtQueueReady, Dev);
    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
        QueueConfig.PowerManaged = WdfFalse;
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Port->ReadQueue);
    }
    if (NT_SUCCESS(Status))
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Port->WaitQueue);
    if (NT_SUCCESS(Status))
        Status = WdfDeviceCreateSymbolicLink(Child, &Link);
    if (NT_SUCCESS(Status))
        Status = WdfDeviceCreateDeviceInterface(Child, &GUID_DEVINTERFACE_COMPORT, NULL);
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Dev->Device, Child);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Child);
        ExFreePoolWithTag(Port, BTHMINI_TAG);
        return Status;
    }
    InsertTailList(&Dev->ComPorts, &Port->Link);
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    DPRINT1("BTHMINI: COM%lu is the serial port of %012I64x channel %u\n", Number, Remote->Address, ServerChannel);
    return STATUS_SUCCESS;
}

static
VOID
BthComSdpDone(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_opt_ PVOID Context,
    _In_ NTSTATUS Status,
    _In_ UCHAR RfcommChannel)
{
    UNREFERENCED_PARAMETER(Context);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHMINI: %012I64x has no serial port service (0x%08lx)\n", Remote->Address, Status);
        return;
    }
    Status = BthComCreatePort(Dev, Remote, RfcommChannel);
    if (!NT_SUCCESS(Status))
        DPRINT1("BTHMINI: serial port for %012I64x not created 0x%08lx\n", Remote->Address, Status);
}

VOID
BthComDevicePaired(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    if (!NT_SUCCESS(BthSdpQueryRfcommChannel(Dev, Remote, BTH_SERIAL_PORT_UUID16, BthComSdpDone, NULL)))
        DPRINT1("BTHMINI: service discovery on %012I64x not started\n", Remote->Address);
}
