/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDI host framework: registration, command engine and adapter lifecycle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_wdi.h"

C_ASSERT(sizeof(WDI_MESSAGE_HEADER) == 16);
C_ASSERT(FIELD_OFFSET(NDIS_WDI_DATA_API, TxDequeueIndication) == sizeof(NDIS_OBJECT_HEADER));
C_ASSERT(FIELD_OFFSET(NDIS_MINIPORT_WDI_DATA_HANDLERS, TxAbortHandler) == sizeof(NDIS_OBJECT_HEADER));
C_ASSERT(FIELD_OFFSET(NDIS_MINIPORT_DRIVER_WDI_CHARACTERISTICS, AllocateAdapterHandler) == 8);
C_ASSERT(FIELD_OFFSET(NDIS_WDI_INIT_PARAMETERS, OpenAdapterCompleteHandler) == 8);
C_ASSERT(FIELD_OFFSET(WDI_FRAME_METADATA, u) == (sizeof(PVOID) == 8 ? 32 : 16));
C_ASSERT(sizeof(WDI_TX_METADATA) == 64);

#define NDIS6_WDI_M3_TIMEOUT_MS         30000
#define NDIS6_WDI_OPEN_TIMEOUT_MS       60000
#define NDIS6_WDI_TASK_SLOT_TIMEOUT_MS  60000
#define NDIS6_WDI_GET_CAPACITY          4096
#define NDIS6_WDI_GET_MAX_CAPACITY      (1024 * 1024)

ULONG
Ndis6WdiRead16(
    _In_reads_bytes_(2) const UCHAR *Data)
{
    return (ULONG)Data[0] | ((ULONG)Data[1] << 8);
}

ULONG
Ndis6WdiRead32(
    _In_reads_bytes_(4) const UCHAR *Data)
{
    return (ULONG)Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
}

ULONG64
Ndis6WdiRead64(
    _In_reads_bytes_(8) const UCHAR *Data)
{
    return (ULONG64)Ndis6WdiRead32(Data) | ((ULONG64)Ndis6WdiRead32(Data + 4) << 32);
}

BOOLEAN
Ndis6WdiTlvNext(
    _Inout_ PUCHAR *Cursor,
    _In_ PUCHAR End,
    _Out_ PNDIS6_WDI_TLV Tlv)
{
    PUCHAR Position = *Cursor;
    ULONG Length;

    if (Position == NULL || Position >= End || (ULONG_PTR)(End - Position) < 4)
        return FALSE;

    Length = Ndis6WdiRead16(Position + 2);
    if ((ULONG_PTR)(End - Position) - 4 < Length)
        return FALSE;

    Tlv->Type = (USHORT)Ndis6WdiRead16(Position);
    Tlv->Length = (USHORT)Length;
    Tlv->Value = Position + 4;
    *Cursor = Position + 4 + Length;
    return TRUE;
}

BOOLEAN
Ndis6WdiTlvFind(
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ USHORT Type,
    _Out_ PNDIS6_WDI_TLV Tlv)
{
    PUCHAR Cursor = Buffer;

    if (Buffer == NULL)
        return FALSE;

    while (Ndis6WdiTlvNext(&Cursor, Buffer + Length, Tlv))
    {
        if (Tlv->Type == Type)
            return TRUE;
    }

    return FALSE;
}

VOID
Ndis6WdiTlvInit(
    _Out_ PNDIS6_WDI_TLV_BUILDER Builder)
{
    RtlZeroMemory(Builder, sizeof(*Builder));
}

VOID
Ndis6WdiTlvFree(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder)
{
    if (Builder->Buffer != NULL)
    {
        RtlSecureZeroMemory(Builder->Buffer, Builder->Capacity);
        ExFreePoolWithTag(Builder->Buffer, NDIS6_WDI_TAG);
    }
    RtlZeroMemory(Builder, sizeof(*Builder));
}

static
BOOLEAN
Ndis6WdiTlvReserve(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_ ULONG Extra)
{
    ULONG Needed;
    ULONG Capacity;
    PUCHAR Buffer;

    if (Builder->Failed)
        return FALSE;

    if (Extra > MAXULONG - Builder->Length)
    {
        Builder->Failed = TRUE;
        return FALSE;
    }

    Needed = Builder->Length + Extra;
    if (Needed <= Builder->Capacity)
        return TRUE;

    Capacity = Builder->Capacity != 0 ? Builder->Capacity : 256;
    while (Capacity < Needed)
    {
        if (Capacity > MAXULONG / 2)
        {
            Builder->Failed = TRUE;
            return FALSE;
        }
        Capacity *= 2;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Capacity, NDIS6_WDI_TAG);
    if (Buffer == NULL)
    {
        Builder->Failed = TRUE;
        return FALSE;
    }

    RtlZeroMemory(Buffer, Capacity);
    if (Builder->Buffer != NULL)
    {
        RtlCopyMemory(Buffer, Builder->Buffer, Builder->Length);
        RtlSecureZeroMemory(Builder->Buffer, Builder->Capacity);
        ExFreePoolWithTag(Builder->Buffer, NDIS6_WDI_TAG);
    }

    Builder->Buffer = Buffer;
    Builder->Capacity = Capacity;
    return TRUE;
}

VOID
Ndis6WdiTlvAppend(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    if (Length == 0 || !Ndis6WdiTlvReserve(Builder, Length))
        return;

    if (Data != NULL)
        RtlCopyMemory(Builder->Buffer + Builder->Length, Data, Length);
    else
        RtlZeroMemory(Builder->Buffer + Builder->Length, Length);
    Builder->Length += Length;
}

VOID
Ndis6WdiTlvAppend8(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_ UCHAR Value)
{
    Ndis6WdiTlvAppend(Builder, &Value, sizeof(Value));
}

VOID
Ndis6WdiTlvAppend16(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_ USHORT Value)
{
    UCHAR Bytes[2];

    Bytes[0] = (UCHAR)Value;
    Bytes[1] = (UCHAR)(Value >> 8);
    Ndis6WdiTlvAppend(Builder, Bytes, sizeof(Bytes));
}

VOID
Ndis6WdiTlvAppend32(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_ ULONG Value)
{
    UCHAR Bytes[4];

    Bytes[0] = (UCHAR)Value;
    Bytes[1] = (UCHAR)(Value >> 8);
    Bytes[2] = (UCHAR)(Value >> 16);
    Bytes[3] = (UCHAR)(Value >> 24);
    Ndis6WdiTlvAppend(Builder, Bytes, sizeof(Bytes));
}

ULONG
Ndis6WdiTlvOpen(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_ USHORT Type)
{
    ULONG Offset = Builder->Length;

    Ndis6WdiTlvAppend16(Builder, Type);
    Ndis6WdiTlvAppend16(Builder, 0);
    return Offset;
}

VOID
Ndis6WdiTlvClose(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_ ULONG Offset)
{
    ULONG Length;

    if (Builder->Failed || Offset + 4 > Builder->Length)
        return;

    Length = Builder->Length - Offset - 4;
    if (Length > MAXUSHORT)
    {
        Builder->Failed = TRUE;
        return;
    }

    Builder->Buffer[Offset + 2] = (UCHAR)Length;
    Builder->Buffer[Offset + 3] = (UCHAR)(Length >> 8);
}

VOID
Ndis6WdiTlvAdd(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Builder,
    _In_ USHORT Type,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    ULONG Offset = Ndis6WdiTlvOpen(Builder, Type);

    Ndis6WdiTlvAppend(Builder, Data, Length);
    Ndis6WdiTlvClose(Builder, Offset);
}

static
PNDIS6_WDI_ADAPTER
Ndis6WdiFromHandle(
    _In_ NDIS_HANDLE MiniportHandle)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportHandle;
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_WDI_ADAPTER Wdi;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NULL;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL || !Ext->DriverBlock->IsWdi)
        return NULL;

    Wdi = (PNDIS6_WDI_ADAPTER)Ext->MiniportAdapterContext;
    if (Wdi == NULL || Wdi->Signature != NDIS6_WDI_SIGNATURE)
        return NULL;

    return Wdi;
}

static
NDIS_STATUS
Ndis6WdiTaskCompletionCode(
    _In_ ULONG MessageId)
{
    switch (MessageId)
    {
        case WDI_TASK_OPEN: return NDIS_STATUS_WDI_INDICATION_OPEN_COMPLETE;
        case WDI_TASK_CLOSE: return NDIS_STATUS_WDI_INDICATION_CLOSE_COMPLETE;
        case WDI_TASK_SCAN: return NDIS_STATUS_WDI_INDICATION_SCAN_COMPLETE;
        case WDI_TASK_CONNECT: return NDIS_STATUS_WDI_INDICATION_CONNECT_COMPLETE;
        case WDI_TASK_DOT11_RESET: return NDIS_STATUS_WDI_INDICATION_DOT11_RESET_COMPLETE;
        case WDI_TASK_DISCONNECT: return NDIS_STATUS_WDI_INDICATION_DISCONNECT_COMPLETE;
        case WDI_TASK_SET_RADIO_STATE: return NDIS_STATUS_WDI_INDICATION_SET_RADIO_STATE_COMPLETE;
        case WDI_TASK_CREATE_PORT: return NDIS_STATUS_WDI_INDICATION_CREATE_PORT_COMPLETE;
        case WDI_TASK_DELETE_PORT: return NDIS_STATUS_WDI_INDICATION_DELETE_PORT_COMPLETE;
        case WDI_TASK_CHANGE_OPERATION_MODE: return NDIS_STATUS_WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE;
        case WDI_TASK_ROAM: return NDIS_STATUS_WDI_INDICATION_ROAM_COMPLETE;
        default: return 0;
    }
}

static
ULONG
Ndis6WdiTaskTimeoutMs(
    _In_ ULONG MessageId)
{
    switch (MessageId)
    {
        case WDI_TASK_SCAN:
        case WDI_TASK_CONNECT:
            return 30000;
        default:
            return 10000;
    }
}

static
VOID
Ndis6WdiFreeCommand(
    _In_ PNDIS6_WDI_COMMAND Command)
{
    if (Command->Buffer != NULL)
    {
        RtlSecureZeroMemory(Command->Buffer, Command->BufferLength);
        ExFreePoolWithTag(Command->Buffer, NDIS6_WDI_TAG);
    }
    if (Command->M4Tlvs != NULL)
        ExFreePoolWithTag(Command->M4Tlvs, NDIS6_WDI_TAG);
    ExFreePoolWithTag(Command, NDIS6_WDI_TAG);
}

static
PNDIS6_WDI_COMMAND
Ndis6WdiAllocateCommand(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG MessageId,
    _In_ WDI_PORT_ID PortId,
    _In_opt_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _In_ ULONG OutputCapacity)
{
    PNDIS6_WDI_COMMAND Command;
    PWDI_MESSAGE_HEADER Header;
    ULONG TlvLength = Tlvs != NULL ? Tlvs->Length : 0;
    ULONG Size;

    if (Tlvs != NULL && Tlvs->Failed)
        return NULL;
    if (TlvLength > MAXULONG - sizeof(WDI_MESSAGE_HEADER))
        return NULL;

    Size = sizeof(WDI_MESSAGE_HEADER) + TlvLength;
    if (Size < OutputCapacity)
        Size = OutputCapacity;

    Command = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Command), NDIS6_WDI_TAG);
    if (Command == NULL)
        return NULL;
    RtlZeroMemory(Command, sizeof(*Command));

    Command->Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, NDIS6_WDI_TAG);
    if (Command->Buffer == NULL)
    {
        ExFreePoolWithTag(Command, NDIS6_WDI_TAG);
        return NULL;
    }
    RtlZeroMemory(Command->Buffer, Size);
    Command->BufferLength = Size;
    Command->MessageId = MessageId;

    do
    {
        Command->TransactionId = (UINT32)InterlockedIncrement(&Wdi->NextTransactionId);
    } while (Command->TransactionId == WDI_TRANSACTION_ID_UNSOLICIT);

    Header = (PWDI_MESSAGE_HEADER)Command->Buffer;
    Header->PortId = PortId;
    Header->TransactionId = Command->TransactionId;
    if (TlvLength != 0)
        RtlCopyMemory(Command->Buffer + sizeof(WDI_MESSAGE_HEADER), Tlvs->Buffer, TlvLength);

    KeInitializeEvent(&Command->M3Event, NotificationEvent, FALSE);
    KeInitializeEvent(&Command->M4Event, NotificationEvent, FALSE);

    Command->Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Command->Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Command->Request.Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    Command->Request.RequestType = NdisRequestMethod;
    Command->Request.PortNumber = NDIS_DEFAULT_PORT_NUMBER;
    Command->Request.RequestId = (PVOID)(ULONG_PTR)Command->TransactionId;
    Command->Request.DATA.METHOD_INFORMATION.Oid = WDI_DEFINE_OID(MessageId);
    Command->Request.DATA.METHOD_INFORMATION.InformationBuffer = Command->Buffer;
    Command->Request.DATA.METHOD_INFORMATION.InputBufferLength = sizeof(WDI_MESSAGE_HEADER) + TlvLength;
    Command->Request.DATA.METHOD_INFORMATION.OutputBufferLength = Size;
    return Command;
}

static
NDIS_STATUS
Ndis6WdiIssue(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS6_WDI_COMMAND Command,
    _Out_ PBOOLEAN Abandoned)
{
    LARGE_INTEGER Timeout;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    *Abandoned = FALSE;

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    InsertTailList(&Wdi->CommandList, &Command->Link);
    Command->Listed = TRUE;
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

    Status = Wdi->Ihv->OidRequestHandler(Wdi->IhvContext, &Command->Request);
    if (Status != NDIS_STATUS_PENDING)
    {
        KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
        Command->M3Status = Status;
        Command->Flags |= NDIS6_WDI_CMD_M3_DONE;
        KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
        return Status;
    }

    Timeout.QuadPart = -(LONGLONG)NDIS6_WDI_M3_TIMEOUT_MS * 10000;
    if (KeWaitForSingleObject(&Command->M3Event, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
    {
        KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
        if (!(Command->Flags & NDIS6_WDI_CMD_M3_DONE))
        {
            Command->Flags |= NDIS6_WDI_CMD_ABANDONED;
            KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
            DbgPrint("WDI: command %lu (transaction %lu) did not complete\n", Command->MessageId, Command->TransactionId);
            *Abandoned = TRUE;
            return NDIS_STATUS_REQUEST_ABORTED;
        }
        KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
    }

    return Command->M3Status;
}

static
VOID
Ndis6WdiUnlistCommand(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS6_WDI_COMMAND Command)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    if (Command->Listed)
    {
        RemoveEntryList(&Command->Link);
        Command->Listed = FALSE;
    }
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
}

static
NDIS_STATUS
Ndis6WdiResponseStatus(
    _In_ PNDIS6_WDI_COMMAND Command,
    _In_ NDIS_STATUS Status)
{
    PWDI_MESSAGE_HEADER Header = (PWDI_MESSAGE_HEADER)Command->Buffer;

    if (Status == NDIS_STATUS_SUCCESS &&
        Command->Request.DATA.METHOD_INFORMATION.BytesWritten >= sizeof(WDI_MESSAGE_HEADER) &&
        Header->Status != NDIS_STATUS_SUCCESS)
    {
        return Header->Status;
    }

    return Status;
}

NDIS_STATUS
Ndis6WdiProperty(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG MessageId,
    _In_ WDI_PORT_ID PortId,
    _In_opt_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _Outptr_opt_result_bytebuffer_(*ResponseLength) PUCHAR *Response,
    _Out_opt_ PULONG ResponseLength)
{
    PNDIS6_WDI_COMMAND Command;
    ULONG Capacity = Response != NULL ? NDIS6_WDI_GET_CAPACITY : 0;
    ULONG Attempt;
    ULONG Written;
    BOOLEAN Abandoned;
    NDIS_STATUS Status = NDIS_STATUS_FAILURE;

    if (Response != NULL)
        *Response = NULL;
    if (ResponseLength != NULL)
        *ResponseLength = 0;

    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        Command = Ndis6WdiAllocateCommand(Wdi, MessageId, PortId, Tlvs, Capacity);
        if (Command == NULL)
            return NDIS_STATUS_RESOURCES;

        KeWaitForSingleObject(&Wdi->CommandMutex, Executive, KernelMode, FALSE, NULL);
        Status = Ndis6WdiIssue(Wdi, Command, &Abandoned);
        KeReleaseMutex(&Wdi->CommandMutex, FALSE);
        if (Abandoned)
            return Status;

        Ndis6WdiUnlistCommand(Wdi, Command);

        if ((Status == NDIS_STATUS_BUFFER_TOO_SHORT ||
             Status == NDIS_STATUS_BUFFER_OVERFLOW ||
             Status == NDIS_STATUS_INVALID_LENGTH) &&
            Response != NULL && Attempt == 0 &&
            Command->Request.DATA.METHOD_INFORMATION.BytesNeeded > Capacity &&
            Command->Request.DATA.METHOD_INFORMATION.BytesNeeded <= NDIS6_WDI_GET_MAX_CAPACITY)
        {
            Capacity = Command->Request.DATA.METHOD_INFORMATION.BytesNeeded;
            Ndis6WdiFreeCommand(Command);
            continue;
        }

        Status = Ndis6WdiResponseStatus(Command, Status);
        if (Status == NDIS_STATUS_SUCCESS && Response != NULL)
        {
            Written = Command->Request.DATA.METHOD_INFORMATION.BytesWritten;
            if (Written > Command->BufferLength)
                Written = Command->BufferLength;
            if (Written > sizeof(WDI_MESSAGE_HEADER))
            {
                Written -= sizeof(WDI_MESSAGE_HEADER);
                *Response = ExAllocatePoolWithTag(NonPagedPool, Written, NDIS6_WDI_TAG);
                if (*Response == NULL)
                {
                    Status = NDIS_STATUS_RESOURCES;
                }
                else
                {
                    RtlCopyMemory(*Response, Command->Buffer + sizeof(WDI_MESSAGE_HEADER), Written);
                    if (ResponseLength != NULL)
                        *ResponseLength = Written;
                }
            }
        }

        if (Status != NDIS_STATUS_SUCCESS)
            DbgPrint("WDI: property %lu failed 0x%08lx\n", MessageId, Status);

        Ndis6WdiFreeCommand(Command);
        return Status;
    }

    return Status;
}

static
VOID
Ndis6WdiReleaseTaskSlot(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS6_WDI_COMMAND Command)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    if (Wdi->ActiveTask == Command)
    {
        Wdi->ActiveTask = NULL;
        KeSetEvent(&Wdi->TaskIdleEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
    KeSetEvent(&Wdi->WorkerEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
Ndis6WdiFinishTask(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS6_WDI_COMMAND Command,
    _In_ NDIS_STATUS Status)
{
    Ndis6WdiReleaseTaskSlot(Wdi, Command);

    if (Status != NDIS_STATUS_SUCCESS)
        DbgPrint("WDI: task %lu completed 0x%08lx\n", Command->MessageId, Status);

    if (Command->Callback != NULL)
        Command->Callback(Wdi, Status, Command->M4Tlvs, Command->M4Length, Command->CallbackContext);

    Ndis6WdiFreeCommand(Command);
}

static VOID Ndis6WdiCheckTaskDeadline(_In_ PNDIS6_WDI_ADAPTER Wdi);

static
BOOLEAN
Ndis6WdiClaimTaskSlot(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS6_WDI_COMMAND Command)
{
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline = KeQueryInterruptTime() + (ULONGLONG)NDIS6_WDI_TASK_SLOT_TIMEOUT_MS * 10000;
    KIRQL OldIrql;

    for (;;)
    {
        KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
        if (Wdi->ActiveTask == NULL)
        {
            Wdi->ActiveTask = Command;
            KeClearEvent(&Wdi->TaskIdleEvent);
            KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
            return TRUE;
        }
        KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

        if (KeQueryInterruptTime() >= Deadline)
            return FALSE;

        Timeout.QuadPart = -500LL * 10000;
        KeWaitForSingleObject(&Wdi->TaskIdleEvent, Executive, KernelMode, FALSE, &Timeout);
        Ndis6WdiCheckTaskDeadline(Wdi);
    }
}

NDIS_STATUS
Ndis6WdiTask(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG MessageId,
    _In_ WDI_PORT_ID PortId,
    _In_opt_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _In_ BOOLEAN Wait,
    _In_opt_ NDIS6_WDI_TASK_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    PNDIS6_WDI_COMMAND Command;
    LARGE_INTEGER Timeout;
    BOOLEAN Abandoned;
    BOOLEAN Finish = FALSE;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    Command = Ndis6WdiAllocateCommand(Wdi, MessageId, PortId, Tlvs, 0);
    if (Command == NULL)
        return NDIS_STATUS_RESOURCES;

    Command->CompletionCode = Ndis6WdiTaskCompletionCode(MessageId);
    Command->Synchronous = Wait;
    Command->Callback = Callback;
    Command->CallbackContext = Context;

    if (!Ndis6WdiClaimTaskSlot(Wdi, Command))
    {
        DbgPrint("WDI: task %lu could not start, another task is still running\n", MessageId);
        Ndis6WdiFreeCommand(Command);
        return NDIS_STATUS_DOT11_MEDIA_IN_USE;
    }

    Command->Deadline = KeQueryInterruptTime() + (ULONGLONG)Ndis6WdiTaskTimeoutMs(MessageId) * 10000;

    KeWaitForSingleObject(&Wdi->CommandMutex, Executive, KernelMode, FALSE, NULL);
    Status = Ndis6WdiIssue(Wdi, Command, &Abandoned);
    KeReleaseMutex(&Wdi->CommandMutex, FALSE);

    if (Abandoned)
    {
        Ndis6WdiReleaseTaskSlot(Wdi, Command);
        return Status;
    }

    Status = Ndis6WdiResponseStatus(Command, Status);
    if (Status != NDIS_STATUS_SUCCESS || Command->CompletionCode == 0)
    {
        Ndis6WdiUnlistCommand(Wdi, Command);
        Ndis6WdiReleaseTaskSlot(Wdi, Command);
        if (Status != NDIS_STATUS_SUCCESS)
            DbgPrint("WDI: task %lu rejected 0x%08lx\n", MessageId, Status);
        if (Command->CompletionCode == 0 && Callback != NULL)
            Callback(Wdi, Status, NULL, 0, Context);
        Ndis6WdiFreeCommand(Command);
        return Status;
    }

    if (Wait)
    {
        Timeout.QuadPart = -(LONGLONG)Ndis6WdiTaskTimeoutMs(MessageId) * 10000;
        KeWaitForSingleObject(&Command->M4Event, Executive, KernelMode, FALSE, &Timeout);

        KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
        if (Command->Listed)
        {
            RemoveEntryList(&Command->Link);
            Command->Listed = FALSE;
        }
        if (Command->Flags & NDIS6_WDI_CMD_M4_DONE)
        {
            Status = Command->M4Status;
        }
        else
        {
            Command->Flags |= NDIS6_WDI_CMD_ABANDONED;
            Status = NDIS_STATUS_REQUEST_ABORTED;
        }
        KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

        Ndis6WdiFinishTask(Wdi, Command, Status);
        return Status;
    }

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    Command->Flags |= NDIS6_WDI_CMD_ISSUED;
    if (Command->Flags & NDIS6_WDI_CMD_M4_DONE)
    {
        if (Command->Listed)
        {
            RemoveEntryList(&Command->Link);
            Command->Listed = FALSE;
        }
        Finish = TRUE;
    }
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

    if (Finish)
        Ndis6WdiFinishTask(Wdi, Command, Command->M4Status);

    return NDIS_STATUS_SUCCESS;
}

static
VOID
Ndis6WdiCheckTaskDeadline(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    PNDIS6_WDI_COMMAND Expired = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    if (Wdi->ActiveTask != NULL &&
        !Wdi->ActiveTask->Synchronous &&
        (Wdi->ActiveTask->Flags & NDIS6_WDI_CMD_ISSUED) &&
        (Wdi->ActiveTask->Flags & NDIS6_WDI_CMD_M3_DONE) &&
        !(Wdi->ActiveTask->Flags & NDIS6_WDI_CMD_M4_DONE) &&
        Wdi->ActiveTask->Listed &&
        KeQueryInterruptTime() >= Wdi->ActiveTask->Deadline)
    {
        Expired = Wdi->ActiveTask;
        RemoveEntryList(&Expired->Link);
        Expired->Listed = FALSE;
        Expired->Flags |= NDIS6_WDI_CMD_ABANDONED;
    }
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

    if (Expired != NULL)
    {
        DbgPrint("WDI: task %lu (transaction %lu) timed out\n", Expired->MessageId, Expired->TransactionId);
        Ndis6WdiFinishTask(Wdi, Expired, NDIS_STATUS_FAILURE);
    }
}

static
BOOLEAN
Ndis6WdiCompleteTaskIndication(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Code,
    _In_ PWDI_MESSAGE_HEADER Header,
    _In_reads_bytes_(Length) PUCHAR Tlvs,
    _In_ ULONG Length)
{
    PNDIS6_WDI_COMMAND Command = NULL;
    PNDIS6_WDI_COMMAND Finish = NULL;
    PUCHAR Copy = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Length != 0)
    {
        Copy = ExAllocatePoolWithTag(NonPagedPool, Length, NDIS6_WDI_TAG);
        if (Copy != NULL)
            RtlCopyMemory(Copy, Tlvs, Length);
    }

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    for (Entry = Wdi->CommandList.Flink; Entry != &Wdi->CommandList; Entry = Entry->Flink)
    {
        PNDIS6_WDI_COMMAND Candidate = CONTAINING_RECORD(Entry, NDIS6_WDI_COMMAND, Link);

        if (Candidate->TransactionId == Header->TransactionId &&
            Candidate->CompletionCode == Code &&
            !(Candidate->Flags & (NDIS6_WDI_CMD_ABANDONED | NDIS6_WDI_CMD_M4_DONE)))
        {
            Command = Candidate;
            break;
        }
    }

    if (Command != NULL)
    {
        Command->M4Status = Header->Status;
        Command->M4Tlvs = Copy;
        Command->M4Length = Copy != NULL ? Length : 0;
        Command->Flags |= NDIS6_WDI_CMD_M4_DONE;
        Copy = NULL;

        if (Command->Synchronous)
        {
            KeSetEvent(&Command->M4Event, IO_NO_INCREMENT, FALSE);
        }
        else if (Command->Flags & NDIS6_WDI_CMD_ISSUED)
        {
            RemoveEntryList(&Command->Link);
            Command->Listed = FALSE;
            Finish = Command;
        }
    }
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

    if (Copy != NULL)
        ExFreePoolWithTag(Copy, NDIS6_WDI_TAG);

    if (Finish != NULL)
        Ndis6WdiFinishTask(Wdi, Finish, Finish->M4Status);

    return Command != NULL;
}

BOOLEAN
Ndis6WdiOidRequestComplete(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_WDI_ADAPTER Wdi = Ndis6WdiFromHandle(Adapter);
    PNDIS6_WDI_COMMAND Command = NULL;
    PNDIS6_WDI_COMMAND Free = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Wdi == NULL)
        return FALSE;

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    for (Entry = Wdi->CommandList.Flink; Entry != &Wdi->CommandList; Entry = Entry->Flink)
    {
        PNDIS6_WDI_COMMAND Candidate = CONTAINING_RECORD(Entry, NDIS6_WDI_COMMAND, Link);

        if (&Candidate->Request == OidRequest)
        {
            Command = Candidate;
            break;
        }
    }

    if (Command != NULL)
    {
        Command->M3Status = Status;
        Command->Flags |= NDIS6_WDI_CMD_M3_DONE;
        if (Command->Flags & NDIS6_WDI_CMD_ABANDONED)
        {
            RemoveEntryList(&Command->Link);
            Command->Listed = FALSE;
            Free = Command;
        }
        else
        {
            KeSetEvent(&Command->M3Event, IO_NO_INCREMENT, FALSE);
        }
    }
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

    if (Free != NULL)
        Ndis6WdiFreeCommand(Free);

    return Command != NULL;
}

BOOLEAN
Ndis6WdiIndicateStatus(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PNDIS6_WDI_ADAPTER Wdi;
    PNDIS6_WDI_INDICATION Item;
    KIRQL OldIrql;

    if (((ULONG)StatusIndication->StatusCode & 0xFFFF0000U) != WDI_INDICATION_PREFIX)
        return FALSE;

    Wdi = Ndis6WdiFromHandle(Adapter);
    if (Wdi == NULL)
        return FALSE;

    if (StatusIndication->StatusBuffer == NULL ||
        StatusIndication->StatusBufferSize < sizeof(WDI_MESSAGE_HEADER) ||
        Wdi->IndicationStop)
    {
        return TRUE;
    }

    Item = ExAllocatePoolWithTag(NonPagedPool,
                                 FIELD_OFFSET(NDIS6_WDI_INDICATION, Data) + StatusIndication->StatusBufferSize,
                                 NDIS6_WDI_TAG);
    if (Item == NULL)
    {
        DbgPrint("WDI: dropped indication 0x%08lx\n", StatusIndication->StatusCode);
        return TRUE;
    }

    Item->Code = StatusIndication->StatusCode;
    Item->Length = StatusIndication->StatusBufferSize;
    RtlCopyMemory(Item->Data, StatusIndication->StatusBuffer, Item->Length);

    KeAcquireSpinLock(&Wdi->IndicationLock, &OldIrql);
    InsertTailList(&Wdi->IndicationList, &Item->Link);
    KeReleaseSpinLock(&Wdi->IndicationLock, OldIrql);
    KeSetEvent(&Wdi->IndicationEvent, IO_NO_INCREMENT, FALSE);
    return TRUE;
}

static
VOID
Ndis6WdiProcessIndication(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS6_WDI_INDICATION Item)
{
    PWDI_MESSAGE_HEADER Header = (PWDI_MESSAGE_HEADER)Item->Data;
    PUCHAR Tlvs = Item->Data + sizeof(WDI_MESSAGE_HEADER);
    ULONG Length = Item->Length - sizeof(WDI_MESSAGE_HEADER);

    if (Header->TransactionId != WDI_TRANSACTION_ID_UNSOLICIT &&
        Ndis6WdiCompleteTaskIndication(Wdi, Item->Code, Header, Tlvs, Length))
    {
        return;
    }

    Ndis6WdiDot11Indication(Wdi, (ULONG)Item->Code & 0xFFFF, Tlvs, Length);
}

static
VOID
NTAPI
Ndis6WdiIndicationThread(
    _In_ PVOID Context)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)Context;
    PNDIS6_WDI_INDICATION Item;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    for (;;)
    {
        KeWaitForSingleObject(&Wdi->IndicationEvent, Executive, KernelMode, FALSE, NULL);

        for (;;)
        {
            KeAcquireSpinLock(&Wdi->IndicationLock, &OldIrql);
            if (IsListEmpty(&Wdi->IndicationList))
            {
                KeReleaseSpinLock(&Wdi->IndicationLock, OldIrql);
                break;
            }
            Entry = RemoveHeadList(&Wdi->IndicationList);
            KeReleaseSpinLock(&Wdi->IndicationLock, OldIrql);

            Item = CONTAINING_RECORD(Entry, NDIS6_WDI_INDICATION, Link);
            Ndis6WdiProcessIndication(Wdi, Item);
            ExFreePoolWithTag(Item, NDIS6_WDI_TAG);
        }

        if (Wdi->IndicationStop)
            break;
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
PNDIS6_WDI_JOB
Ndis6WdiNextJob(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    PNDIS6_WDI_JOB Job = NULL;
    BOOLEAN TaskBusy;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
    TaskBusy = (Wdi->ActiveTask != NULL);
    KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);

    if (TaskBusy)
        return NULL;

    KeAcquireSpinLock(&Wdi->JobLock, &OldIrql);
    if (!IsListEmpty(&Wdi->JobList))
    {
        Entry = RemoveHeadList(&Wdi->JobList);
        Job = CONTAINING_RECORD(Entry, NDIS6_WDI_JOB, Link);
        Wdi->RunningJob = Job->Request;
    }
    KeReleaseSpinLock(&Wdi->JobLock, OldIrql);

    return Job;
}

static
VOID
NTAPI
Ndis6WdiWorkerThread(
    _In_ PVOID Context)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)Context;
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER WaitTimeout;
    PNDIS6_WDI_JOB Job;
    ULONGLONG Deadline;
    ULONGLONG Now;
    KIRQL OldIrql;

    for (;;)
    {
        WaitTimeout = NULL;
        KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
        Deadline = (Wdi->ActiveTask != NULL && !Wdi->ActiveTask->Synchronous) ? Wdi->ActiveTask->Deadline : 0;
        KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
        if (Deadline != 0)
        {
            Now = KeQueryInterruptTime();
            Timeout.QuadPart = Deadline > Now ? -(LONGLONG)(Deadline - Now) : -1;
            WaitTimeout = &Timeout;
        }

        KeWaitForSingleObject(&Wdi->WorkerEvent, Executive, KernelMode, FALSE, WaitTimeout);
        if (Wdi->WorkerStop)
            break;

        Ndis6WdiCheckTaskDeadline(Wdi);

        while (!Wdi->WorkerStop && (Job = Ndis6WdiNextJob(Wdi)) != NULL)
        {
            Ndis6WdiDot11RunJob(Wdi, Job->Request);
            KeAcquireSpinLock(&Wdi->JobLock, &OldIrql);
            Wdi->RunningJob = NULL;
            KeReleaseSpinLock(&Wdi->JobLock, OldIrql);
            ExFreePoolWithTag(Job, NDIS6_WDI_TAG);
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NDIS_STATUS
Ndis6WdiStartThread(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PKSTART_ROUTINE Routine,
    _Out_ PKTHREAD *Thread)
{
    HANDLE Handle;
    NTSTATUS Status;

    *Thread = NULL;
    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL, Routine, Wdi);
    if (!NT_SUCCESS(Status))
        return NDIS_STATUS_RESOURCES;

    Status = ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID *)Thread, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        *Thread = NULL;
        return NDIS_STATUS_FAILURE;
    }

    return NDIS_STATUS_SUCCESS;
}

static
VOID
Ndis6WdiJoinThread(
    _Inout_ PKTHREAD *Thread)
{
    if (*Thread == NULL)
        return;

    KeWaitForSingleObject(*Thread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(*Thread);
    *Thread = NULL;
}

static
VOID
Ndis6WdiStopWorker(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    PNDIS6_WDI_JOB Job;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    InterlockedExchange(&Wdi->WorkerStop, 1);
    KeSetEvent(&Wdi->WorkerEvent, IO_NO_INCREMENT, FALSE);
    Ndis6WdiJoinThread(&Wdi->WorkerThread);

    for (;;)
    {
        KeAcquireSpinLock(&Wdi->JobLock, &OldIrql);
        if (IsListEmpty(&Wdi->JobList))
        {
            KeReleaseSpinLock(&Wdi->JobLock, OldIrql);
            break;
        }
        Entry = RemoveHeadList(&Wdi->JobList);
        KeReleaseSpinLock(&Wdi->JobLock, OldIrql);

        Job = CONTAINING_RECORD(Entry, NDIS6_WDI_JOB, Link);
        NdisMOidRequestComplete(Wdi->Adapter, Job->Request, NDIS_STATUS_REQUEST_ABORTED);
        ExFreePoolWithTag(Job, NDIS6_WDI_TAG);
    }
}

static
VOID
Ndis6WdiStopIndications(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    InterlockedExchange(&Wdi->IndicationStop, 1);
    KeSetEvent(&Wdi->IndicationEvent, IO_NO_INCREMENT, FALSE);
    Ndis6WdiJoinThread(&Wdi->IndicationThread);

    for (;;)
    {
        KeAcquireSpinLock(&Wdi->IndicationLock, &OldIrql);
        if (IsListEmpty(&Wdi->IndicationList))
        {
            KeReleaseSpinLock(&Wdi->IndicationLock, OldIrql);
            break;
        }
        Entry = RemoveHeadList(&Wdi->IndicationList);
        KeReleaseSpinLock(&Wdi->IndicationLock, OldIrql);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, NDIS6_WDI_INDICATION, Link), NDIS6_WDI_TAG);
    }
}

VOID
Ndis6WdiIndicateStatusUp(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Code,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    Indication.SourceHandle = Wdi->Adapter;
    Indication.PortNumber = NDIS_DEFAULT_PORT_NUMBER;
    Indication.StatusCode = Code;
    Indication.StatusBuffer = Buffer;
    Indication.StatusBufferSize = Length;
    NdisMIndicateStatusEx(Wdi->Adapter, &Indication);
}

VOID
Ndis6WdiIndicateLinkState(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ BOOLEAN Connected)
{
    NDIS_LINK_STATE LinkState;
    KIRQL OldIrql;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    LinkState.MediaConnectState = Connected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    LinkState.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

    if (Connected)
    {
        KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
        if (Wdi->Dot11.TxLinkSpeed != 0)
            LinkState.XmitLinkSpeed = Wdi->Dot11.TxLinkSpeed;
        if (Wdi->Dot11.RxLinkSpeed != 0)
            LinkState.RcvLinkSpeed = Wdi->Dot11.RxLinkSpeed;
        KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
    }

    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_LINK_STATE, &LinkState, sizeof(LinkState));
}

static
VOID
NTAPI
Ndis6WdiOpenAdapterComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_STATUS CompletionStatus)
{
    PNDIS6_WDI_ADAPTER Wdi = Ndis6WdiFromHandle(MiniportAdapterHandle);

    if (Wdi == NULL)
        return;

    Wdi->OpenStatus = CompletionStatus;
    KeSetEvent(&Wdi->OpenEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
Ndis6WdiCloseAdapterComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_STATUS CompletionStatus)
{
    PNDIS6_WDI_ADAPTER Wdi = Ndis6WdiFromHandle(MiniportAdapterHandle);

    if (Wdi == NULL)
        return;

    Wdi->CloseStatus = CompletionStatus;
    KeSetEvent(&Wdi->CloseEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
Ndis6WdiIdleNotificationConfirm(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_DEVICE_POWER_STATE DeviceIdlePowerState)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
    UNREFERENCED_PARAMETER(DeviceIdlePowerState);
}

static
VOID
NTAPI
Ndis6WdiIdleNotificationComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
}

static
VOID
Ndis6WdiCreatePortComplete(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Status,
    _In_reads_bytes_opt_(Length) PUCHAR Tlvs,
    _In_ ULONG Length,
    _In_opt_ PVOID Context)
{
    NDIS6_WDI_TLV Tlv;

    UNREFERENCED_PARAMETER(Context);

    if (Status != NDIS_STATUS_SUCCESS || Tlvs == NULL)
        return;

    if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_PORT_ATTRIBUTES, &Tlv) && Tlv.Length >= 8)
    {
        RtlCopyMemory(Wdi->CurrentAddress, Tlv.Value, 6);
        Wdi->PortId = (WDI_PORT_ID)Ndis6WdiRead16(Tlv.Value + 6);
        Wdi->PortCreated = TRUE;
    }
}

static
NDIS_STATUS
Ndis6WdiSetAdapterConfiguration(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    static const UCHAR BarMap[][3] =
    {
        { 0, 0, 0 },
        { 1, 24, 1 },
        { 25, 49, 2 },
        { 50, 74, 3 },
        { 75, 100, 4 },
    };
    NDIS6_WDI_TLV_BUILDER Tlvs;
    UCHAR PldrSupport = FALSE;
    NDIS_STATUS Status;

    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiTlvAdd(&Tlvs, WDI_TLV_LINK_QUALITY_BAR_MAP, BarMap, sizeof(BarMap));
    if (Wdi->Wdi->WdiVersion >= WDI_VERSION_1_0_10)
        Ndis6WdiTlvAdd(&Tlvs, WDI_TLV_PLDR_SUPPORT, &PldrSupport, sizeof(PldrSupport));
    Status = Ndis6WdiProperty(Wdi, WDI_SET_ADAPTER_CONFIGURATION, WDI_PORT_ID_ADAPTER, &Tlvs, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);
    return Status;
}

static
NDIS_STATUS
Ndis6WdiCreatePort(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    ULONG Offset;
    NDIS_STATUS Status;

    Ndis6WdiTlvInit(&Tlvs);
    Offset = Ndis6WdiTlvOpen(&Tlvs, WDI_TLV_CREATE_PORT_PARAMETERS);
    Ndis6WdiTlvAppend16(&Tlvs, WDI_OPERATION_MODE_STA);
    Ndis6WdiTlvAppend32(&Tlvs, NDIS_DEFAULT_PORT_NUMBER);
    Ndis6WdiTlvClose(&Tlvs, Offset);
    Status = Ndis6WdiTask(Wdi, WDI_TASK_CREATE_PORT, WDI_PORT_ID_ADAPTER, &Tlvs, TRUE, Ndis6WdiCreatePortComplete, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    if (Status == NDIS_STATUS_SUCCESS && !Wdi->PortCreated)
        Status = NDIS_STATUS_INVALID_DATA;
    return Status;
}

static
VOID
Ndis6WdiDeletePort(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    ULONG Offset;

    Ndis6WdiTlvInit(&Tlvs);
    Offset = Ndis6WdiTlvOpen(&Tlvs, WDI_TLV_DELETE_PORT_PARAMETERS);
    Ndis6WdiTlvAppend16(&Tlvs, Wdi->PortId);
    Ndis6WdiTlvClose(&Tlvs, Offset);
    (VOID)Ndis6WdiTask(Wdi, WDI_TASK_DELETE_PORT, WDI_PORT_ID_ADAPTER, &Tlvs, TRUE, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);
}

static
VOID
Ndis6WdiFreeCommands(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    for (;;)
    {
        KeAcquireSpinLock(&Wdi->CommandLock, &OldIrql);
        if (IsListEmpty(&Wdi->CommandList))
        {
            KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
            break;
        }
        Entry = RemoveHeadList(&Wdi->CommandList);
        KeReleaseSpinLock(&Wdi->CommandLock, OldIrql);
        Ndis6WdiFreeCommand(CONTAINING_RECORD(Entry, NDIS6_WDI_COMMAND, Link));
    }
}

static
VOID
Ndis6WdiTeardown(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ BOOLEAN SurpriseRemoved)
{
    LARGE_INTEGER Timeout;
    NDIS_STATUS Status;

    InterlockedExchange(&Wdi->Halting, 1);
    Ndis6WdiStopWorker(Wdi);

    if (Wdi->Started && Wdi->Wdi->StopOperationHandler != NULL)
        Wdi->Wdi->StopOperationHandler(Wdi->IhvContext);
    Wdi->Started = FALSE;

    if (Wdi->PortCreated)
    {
        if (!SurpriseRemoved)
        {
            Ndis6WdiDot11HaltDisconnect(Wdi);
            Ndis6WdiDeletePort(Wdi);
        }
        Ndis6WdiDataFlushQueue(Wdi, NDIS_STATUS_FAILURE);
        if (Wdi->TalStarted && Wdi->Tal.TalTxRxDeletePortHandler != NULL)
            Wdi->Tal.TalTxRxDeletePortHandler(Wdi->TalContext, Wdi->PortId);
        Wdi->PortCreated = FALSE;
    }

    Ndis6WdiDataFlushQueue(Wdi, NDIS_STATUS_FAILURE);

    if (Wdi->TalStarted && Wdi->Tal.TalTxRxStopHandler != NULL)
        Wdi->Tal.TalTxRxStopHandler(Wdi->TalContext);
    Wdi->TalStarted = FALSE;

    if (Wdi->TalInitialized)
        Wdi->Wdi->TalTxRxDeinitializeHandler(Wdi->TalContext);
    Wdi->TalInitialized = FALSE;

    if (Wdi->Opened)
    {
        KeClearEvent(&Wdi->CloseEvent);
        Status = Wdi->Wdi->CloseAdapterHandler(Wdi->IhvContext);
        if (Status == NDIS_STATUS_SUCCESS || Status == NDIS_STATUS_PENDING)
        {
            Timeout.QuadPart = -(LONGLONG)NDIS6_WDI_M3_TIMEOUT_MS * 10000;
            if (KeWaitForSingleObject(&Wdi->CloseEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
                DbgPrint("WDI: close adapter did not complete\n");
        }
        Wdi->Opened = FALSE;
    }

    if (Wdi->Allocated)
        Wdi->Wdi->FreeAdapterHandler(Wdi->IhvContext);
    Wdi->Allocated = FALSE;

    Ndis6WdiStopIndications(Wdi);
    Ndis6WdiFreeCommands(Wdi);
    Ndis6WdiDataCleanup(Wdi);
    Ndis6WdiDot11Cleanup(Wdi);
}

static
NDIS_STATUS
NTAPI
Ndis6WdiMiniportInitializeEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;
    PNDIS6_WDI_ADAPTER Wdi;
    LARGE_INTEGER Timeout;
    PUCHAR Capabilities = NULL;
    ULONG CapabilitiesLength = 0;
    NDIS_STATUS Status;

    Wdi = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Wdi), NDIS6_WDI_TAG);
    if (Wdi == NULL)
        return NDIS_STATUS_RESOURCES;
    RtlZeroMemory(Wdi, sizeof(*Wdi));

    Wdi->Signature = NDIS6_WDI_SIGNATURE;
    Wdi->Adapter = Adapter;
    Wdi->DriverBlock = Ext->DriverBlock;
    Wdi->Wdi = &Ext->DriverBlock->WdiCharacteristics;
    Wdi->Ihv = &Ext->DriverBlock->WdiMiniportCharacteristics;
    Wdi->PortId = WDI_PORT_ID_ADAPTER;
    KeInitializeEvent(&Wdi->OpenEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Wdi->CloseEvent, NotificationEvent, FALSE);
    KeInitializeMutex(&Wdi->CommandMutex, 0);
    KeInitializeSpinLock(&Wdi->CommandLock);
    InitializeListHead(&Wdi->CommandList);
    KeInitializeEvent(&Wdi->TaskIdleEvent, NotificationEvent, TRUE);
    KeInitializeSpinLock(&Wdi->JobLock);
    InitializeListHead(&Wdi->JobList);
    KeInitializeEvent(&Wdi->WorkerEvent, SynchronizationEvent, FALSE);
    KeInitializeSpinLock(&Wdi->IndicationLock);
    InitializeListHead(&Wdi->IndicationList);
    KeInitializeEvent(&Wdi->IndicationEvent, SynchronizationEvent, FALSE);
    KeInitializeSpinLock(&Wdi->StateLock);
    InitializeListHead(&Wdi->BssList);
    KeInitializeSpinLock(&Wdi->TxLock);
    KeInitializeSpinLock(&Wdi->TxDescLock);
    InitializeListHead(&Wdi->TxQueue);
    KeInitializeEvent(&Wdi->TxIdleEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Wdi->RxIdleEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Wdi->TxAbortEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Wdi->RxStopEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Wdi->RxFlushEvent, NotificationEvent, FALSE);
    Wdi->HardwareRadioOn = TRUE;
    Wdi->SoftwareRadioOn = TRUE;
    Ndis6WdiDot11Initialize(Wdi);

    Status = Ndis6WdiStartThread(Wdi, Ndis6WdiIndicationThread, &Wdi->IndicationThread);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;
    Status = Ndis6WdiStartThread(Wdi, Ndis6WdiWorkerThread, &Wdi->WorkerThread);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Wdi->InitParameters.Header.Type = NDIS_OBJECT_TYPE_WDI_INIT_PARAMETERS;
    Wdi->InitParameters.Header.Revision = NDIS_OBJECT_TYPE_WDI_INIT_PARAMETERS_REVISION_1;
    Wdi->InitParameters.Header.Size = NDIS_SIZEOF_WDI_INIT_PARAMETERS_REVISION_1;
    Wdi->InitParameters.WdiVersion = Wdi->Wdi->WdiVersion;
    Wdi->InitParameters.OpenAdapterCompleteHandler = Ndis6WdiOpenAdapterComplete;
    Wdi->InitParameters.CloseAdapterCompleteHandler = Ndis6WdiCloseAdapterComplete;
    Wdi->InitParameters.UeIdleNotificationConfirm = Ndis6WdiIdleNotificationConfirm;
    Wdi->InitParameters.UeIdleNotificationComplete = Ndis6WdiIdleNotificationComplete;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2;

    Status = Wdi->Wdi->AllocateAdapterHandler(NdisMiniportHandle,
                                              MiniportDriverContext,
                                              MiniportInitParameters,
                                              &Wdi->InitParameters,
                                              &Registration);
    DbgPrint("WDI: allocate adapter 0x%08lx (version 0x%lx)\n", Status, Wdi->Wdi->WdiVersion);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Wdi->Allocated = TRUE;
    Wdi->IhvContext = Registration.MiniportAdapterContext;
    Wdi->IhvRegistration = Registration;

    Registration.MiniportAdapterContext = Wdi;
    Status = NdisMSetMiniportAttributes(NdisMiniportHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    KeClearEvent(&Wdi->OpenEvent);
    Status = Wdi->Wdi->OpenAdapterHandler(Wdi->IhvContext, MiniportInitParameters);
    if (Status == NDIS_STATUS_SUCCESS || Status == NDIS_STATUS_PENDING)
    {
        Timeout.QuadPart = -(LONGLONG)NDIS6_WDI_OPEN_TIMEOUT_MS * 10000;
        if (KeWaitForSingleObject(&Wdi->OpenEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
            Status = NDIS_STATUS_FAILURE;
        else
            Status = Wdi->OpenStatus;
        Wdi->Opened = (Status == NDIS_STATUS_SUCCESS);
    }
    DbgPrint("WDI: open adapter 0x%08lx\n", Status);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Ndis6WdiDataInitialize(Wdi);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Ndis6WdiProperty(Wdi, WDI_GET_ADAPTER_CAPABILITIES, WDI_PORT_ID_ADAPTER, NULL, &Capabilities, &CapabilitiesLength);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6WdiDot11ParseCapabilities(Wdi, Capabilities, CapabilitiesLength);
    if (Capabilities != NULL)
        ExFreePoolWithTag(Capabilities, NDIS6_WDI_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Ndis6WdiSetAdapterConfiguration(Wdi);
    if (Status != NDIS_STATUS_SUCCESS)
        DbgPrint("WDI: adapter configuration rejected 0x%08lx\n", Status);

    Status = Ndis6WdiDataStart(Wdi);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Ndis6WdiCreatePort(Wdi);
    DbgPrint("WDI: create port 0x%08lx (port %u)\n", Status, Wdi->PortId);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    if (Wdi->Tal.TalTxRxAddPortHandler != NULL)
        Wdi->Tal.TalTxRxAddPortHandler(Wdi->TalContext, Wdi->PortId, WDI_OPERATION_MODE_STA);

    if (Wdi->Wdi->StartOperationHandler != NULL)
    {
        Status = Wdi->Wdi->StartOperationHandler(Wdi->IhvContext);
        if (Status != NDIS_STATUS_SUCCESS)
            goto Fail;
    }
    Wdi->Started = TRUE;

    Status = Ndis6WdiDot11SetGeneralAttributes(Wdi);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    DbgPrint("WDI: adapter ready %02x:%02x:%02x:%02x:%02x:%02x\n",
             Wdi->CurrentAddress[0], Wdi->CurrentAddress[1], Wdi->CurrentAddress[2],
             Wdi->CurrentAddress[3], Wdi->CurrentAddress[4], Wdi->CurrentAddress[5]);
    return NDIS_STATUS_SUCCESS;

Fail:
    DbgPrint("WDI: initialization failed 0x%08lx\n", Status);
    Ndis6WdiTeardown(Wdi, FALSE);
    Wdi->Signature = 0;
    ExFreePoolWithTag(Wdi, NDIS6_WDI_TAG);
    return Status;
}

static
VOID
NTAPI
Ndis6WdiMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;

    Ndis6WdiTeardown(Wdi, HaltAction == NdisHaltDeviceSurpriseRemoved);
    Wdi->Signature = 0;
    ExFreePoolWithTag(Wdi, NDIS6_WDI_TAG);
}

static
NDIS_STATUS
NTAPI
Ndis6WdiMiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;

    Ndis6WdiDataPause(Wdi);
    if (Wdi->Wdi->PostPauseHandler != NULL)
        (VOID)Wdi->Wdi->PostPauseHandler(Wdi->IhvContext, PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
Ndis6WdiMiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    Ndis6WdiDataRestart(Wdi);
    if (Wdi->Wdi->PostRestartHandler != NULL)
        Status = Wdi->Wdi->PostRestartHandler(Wdi->IhvContext, RestartParameters);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Ndis6WdiDataPause(Wdi);
        return Status;
    }

    Ndis6WdiIndicateLinkState(Wdi, Wdi->Dot11.Associated);
    return NDIS_STATUS_SUCCESS;
}

static
BOOLEAN
Ndis6WdiIsNonWdiOid(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_OID Oid)
{
    ULONG i;

    for (i = 0; i < Wdi->NonWdiOidCount; i++)
    {
        if (Wdi->NonWdiOids[i] == Oid)
            return TRUE;
    }
    return FALSE;
}

static
NDIS_STATUS
NTAPI
Ndis6WdiMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;
    PNDIS6_WDI_JOB Job;
    BOOLEAN Queue = FALSE;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    if (Wdi->Halting)
        return NDIS_STATUS_ADAPTER_NOT_READY;

    if (Ndis6WdiIsNonWdiOid(Wdi, OidRequest->DATA.QUERY_INFORMATION.Oid))
        return Wdi->Ihv->OidRequestHandler(Wdi->IhvContext, OidRequest);

    Status = Ndis6WdiDot11OidRequest(Wdi, OidRequest, &Queue);
    if (!Queue)
        return Status;

    Job = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Job), NDIS6_WDI_TAG);
    if (Job == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Job, sizeof(*Job));
    Job->Request = OidRequest;

    KeAcquireSpinLock(&Wdi->JobLock, &OldIrql);
    InsertTailList(&Wdi->JobList, &Job->Link);
    KeReleaseSpinLock(&Wdi->JobLock, OldIrql);
    KeSetEvent(&Wdi->WorkerEvent, IO_NO_INCREMENT, FALSE);
    return NDIS_STATUS_PENDING;
}

static
VOID
NTAPI
Ndis6WdiMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;
    PNDIS6_WDI_JOB Job = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->JobLock, &OldIrql);
    for (Entry = Wdi->JobList.Flink; Entry != &Wdi->JobList; Entry = Entry->Flink)
    {
        PNDIS6_WDI_JOB Candidate = CONTAINING_RECORD(Entry, NDIS6_WDI_JOB, Link);

        if (Candidate->Request->RequestId == RequestId)
        {
            RemoveEntryList(&Candidate->Link);
            Job = Candidate;
            break;
        }
    }
    KeReleaseSpinLock(&Wdi->JobLock, OldIrql);

    if (Job != NULL)
    {
        NdisMOidRequestComplete(Wdi->Adapter, Job->Request, NDIS_STATUS_REQUEST_ABORTED);
        ExFreePoolWithTag(Job, NDIS6_WDI_TAG);
        return;
    }

    if (Wdi->Ihv->CancelOidRequestHandler != NULL)
        Wdi->Ihv->CancelOidRequestHandler(Wdi->IhvContext, RequestId);
}

static
VOID
NTAPI
Ndis6WdiMiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;

    if (Wdi->Ihv->DevicePnPEventNotifyHandler != NULL)
        Wdi->Ihv->DevicePnPEventNotifyHandler(Wdi->IhvContext, NetDevicePnPEvent);
}

static
VOID
NTAPI
Ndis6WdiMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PNDIS6_WDI_ADAPTER Wdi = (PNDIS6_WDI_ADAPTER)MiniportAdapterContext;

    if (Wdi->Ihv->ShutdownHandlerEx != NULL)
        Wdi->Ihv->ShutdownHandlerEx(Wdi->IhvContext, ShutdownAction);
}

NDIS_STATUS
NTAPI
NdisMRegisterWdiMiniportDriver(
    PDRIVER_OBJECT DriverObject,
    PCUNICODE_STRING RegistryPath,
    NDIS_HANDLE DriverContext,
    PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics,
    PNDIS_MINIPORT_DRIVER_WDI_CHARACTERISTICS WdiCharacteristics,
    PNDIS_HANDLE DriverHandle)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Host;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Ihv;
    ULONG Size;
    NDIS_STATUS Status;

    if (DriverHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    *DriverHandle = NULL;

    if (Characteristics == NULL || WdiCharacteristics == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (WdiCharacteristics->Header.Type != NDIS_OBJECT_TYPE_MINIPORT_WDI_CHARACTERISTICS ||
        WdiCharacteristics->Header.Revision < NDIS_MINIPORT_DRIVER_WDI_CHARACTERISTICS_REVISION_1 ||
        WdiCharacteristics->Header.Size < NDIS_SIZEOF_MINIPORT_WDI_CHARACTERISTICS_REVISION_1)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (WdiCharacteristics->WdiVersion < WDI_VERSION_1_0 ||
        WdiCharacteristics->WdiVersion > WDI_VERSION_LATEST)
    {
        return NDIS_STATUS_BAD_VERSION;
    }

    if (WdiCharacteristics->AllocateAdapterHandler == NULL ||
        WdiCharacteristics->FreeAdapterHandler == NULL ||
        WdiCharacteristics->OpenAdapterHandler == NULL ||
        WdiCharacteristics->CloseAdapterHandler == NULL ||
        WdiCharacteristics->TalTxRxInitializeHandler == NULL ||
        WdiCharacteristics->TalTxRxDeinitializeHandler == NULL)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if ((Characteristics->Header.Type != NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS &&
         Characteristics->Header.Type != NDIS_OBJECT_TYPE_DEFAULT) ||
        Characteristics->Header.Size < NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1 ||
        Characteristics->OidRequestHandler == NULL)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    Size = Characteristics->Header.Size;
    if (Size > sizeof(Ihv))
        Size = sizeof(Ihv);
    RtlZeroMemory(&Ihv, sizeof(Ihv));
    RtlCopyMemory(&Ihv, Characteristics, Size);

    RtlZeroMemory(&Host, sizeof(Host));
    Host.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Host.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    Host.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    Host.MajorNdisVersion = Ihv.MajorNdisVersion;
    Host.MinorNdisVersion = Ihv.MinorNdisVersion;
    Host.MajorDriverVersion = Ihv.MajorDriverVersion;
    Host.MinorDriverVersion = Ihv.MinorDriverVersion;
    Host.Flags = Ihv.Flags;
    Host.SetOptionsHandler = Ihv.SetOptionsHandler;
    Host.InitializeHandlerEx = Ndis6WdiMiniportInitializeEx;
    Host.HaltHandlerEx = Ndis6WdiMiniportHaltEx;
    Host.UnloadHandler = Ihv.UnloadHandler;
    Host.PauseHandler = Ndis6WdiMiniportPause;
    Host.RestartHandler = Ndis6WdiMiniportRestart;
    Host.OidRequestHandler = Ndis6WdiMiniportOidRequest;
    Host.SendNetBufferListsHandler = Ndis6WdiSendNetBufferLists;
    Host.ReturnNetBufferListsHandler = Ndis6WdiReturnNetBufferLists;
    Host.CancelSendHandler = Ndis6WdiCancelSend;
    Host.DevicePnPEventNotifyHandler = Ndis6WdiMiniportDevicePnPEventNotify;
    Host.ShutdownHandlerEx = Ndis6WdiMiniportShutdownEx;
    Host.CancelOidRequestHandler = Ndis6WdiMiniportCancelOidRequest;

    Status = Ndis6RegisterMiniportDriverInternal(DriverObject,
                                                 (PUNICODE_STRING)RegistryPath,
                                                 DriverContext,
                                                 &Host,
                                                 NULL,
                                                 WdiCharacteristics,
                                                 DriverHandle);
    if (Status == NDIS_STATUS_SUCCESS)
        ((PNDIS6_DRIVER_BLOCK)*DriverHandle)->WdiMiniportCharacteristics = Ihv;

    DbgPrint("WDI: register %wZ version 0x%lx ndis %u.%u -> 0x%08lx\n",
             RegistryPath, WdiCharacteristics->WdiVersion,
             Ihv.MajorNdisVersion, Ihv.MinorNdisVersion, Status);
    return Status;
}

VOID
NTAPI
NdisMDeregisterWdiMiniportDriver(
    NDIS_HANDLE DriverHandle)
{
    NdisMDeregisterMiniportDriver(DriverHandle);
}
