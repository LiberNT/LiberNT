/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth Low Energy attribute protocol, GATT client and server, and LE device enumeration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthmini.h"
#include <wdmsec.h>
#include <devguid.h>
#include <drivers/bthleio.h>

#define NDEBUG
#include <debug.h>

#define LE_CID_ATT                              0x0004

#define ATT_ERROR_RESPONSE                      0x01
#define ATT_EXCHANGE_MTU_REQUEST                0x02
#define ATT_EXCHANGE_MTU_RESPONSE               0x03
#define ATT_FIND_INFORMATION_REQUEST            0x04
#define ATT_FIND_INFORMATION_RESPONSE           0x05
#define ATT_FIND_BY_TYPE_VALUE_REQUEST          0x06
#define ATT_FIND_BY_TYPE_VALUE_RESPONSE         0x07
#define ATT_READ_BY_TYPE_REQUEST                0x08
#define ATT_READ_BY_TYPE_RESPONSE               0x09
#define ATT_READ_REQUEST                        0x0A
#define ATT_READ_RESPONSE                       0x0B
#define ATT_READ_BLOB_REQUEST                   0x0C
#define ATT_READ_BLOB_RESPONSE                  0x0D
#define ATT_READ_BY_GROUP_TYPE_REQUEST          0x10
#define ATT_READ_BY_GROUP_TYPE_RESPONSE         0x11
#define ATT_WRITE_REQUEST                       0x12
#define ATT_WRITE_RESPONSE                      0x13
#define ATT_PREPARE_WRITE_REQUEST               0x16
#define ATT_PREPARE_WRITE_RESPONSE              0x17
#define ATT_EXECUTE_WRITE_REQUEST               0x18
#define ATT_EXECUTE_WRITE_RESPONSE              0x19
#define ATT_HANDLE_VALUE_NOTIFICATION           0x1B
#define ATT_HANDLE_VALUE_INDICATION             0x1D
#define ATT_HANDLE_VALUE_CONFIRMATION           0x1E
#define ATT_READ_MULTIPLE_VARIABLE_RESPONSE     0x21
#define ATT_MULTIPLE_HANDLE_VALUE_NOTIFICATION  0x23
#define ATT_WRITE_COMMAND                       0x52
#define ATT_COMMAND_FLAG                        0x40

#define ATT_ERROR_INVALID_HANDLE                0x01
#define ATT_ERROR_READ_NOT_PERMITTED            0x02
#define ATT_ERROR_WRITE_NOT_PERMITTED           0x03
#define ATT_ERROR_INVALID_PDU                   0x04
#define ATT_ERROR_INSUFFICIENT_AUTHENTICATION   0x05
#define ATT_ERROR_REQUEST_NOT_SUPPORTED         0x06
#define ATT_ERROR_INVALID_OFFSET                0x07
#define ATT_ERROR_INSUFFICIENT_AUTHORIZATION    0x08
#define ATT_ERROR_ATTRIBUTE_NOT_FOUND           0x0A
#define ATT_ERROR_ATTRIBUTE_NOT_LONG            0x0B
#define ATT_ERROR_INVALID_VALUE_LENGTH          0x0D
#define ATT_ERROR_INSUFFICIENT_ENCRYPTION       0x0F
#define ATT_ERROR_UNSUPPORTED_GROUP_TYPE        0x10

#define GATT_UUID_PRIMARY_SERVICE               0x2800
#define GATT_UUID_SECONDARY_SERVICE             0x2801
#define GATT_UUID_INCLUDE                       0x2802
#define GATT_UUID_CHARACTERISTIC                0x2803
#define GATT_UUID_CCCD                          0x2902
#define GATT_UUID_DEVICE_NAME                   0x2A00
#define GATT_UUID_APPEARANCE                    0x2A01
#define GATT_UUID_SERVICE_CHANGED               0x2A05
#define GATT_UUID_PNP_ID                        0x2A50
#define GATT_SERVICE_GAP                        0x1800
#define GATT_SERVICE_GATT                       0x1801
#define GATT_SERVICE_DEVICE_INFORMATION         0x180A

#define GATT_PROP_BROADCAST                     0x01
#define GATT_PROP_READ                          0x02
#define GATT_PROP_WRITE_WITHOUT_RESPONSE        0x04
#define GATT_PROP_WRITE                         0x08
#define GATT_PROP_NOTIFY                        0x10
#define GATT_PROP_INDICATE                      0x20
#define GATT_PROP_SIGNED_WRITE                  0x40
#define GATT_PROP_EXTENDED                      0x80

#define GATT_CCCD_NOTIFY                        0x0001
#define GATT_CCCD_INDICATE                      0x0002

#define GATT_DEFAULT_MTU                        23
#define GATT_LOCAL_MTU                          BTHLEENUM_ATT_MTU_INITIAL_NEGOTIATION
#define GATT_MAX_VALUE                          512
#define GATT_TIMEOUT_MS                         30000
#define GATT_MAX_SUBSCRIPTIONS                  32
#define GATT_EVENT_RING                         32
#define GATT_CACHE_VERSION                      2
#define GATT_MAX_INCLUDE_DEPTH                  8
#define GATT_APPEARANCE_COMPUTER                0x0080

#define GATT_LOCAL_SERVICE_CHANGED_CCCD         0x0009
#define GATT_LOCAL_LAST_HANDLE                  0x0009

typedef struct _BTHMINI_GATT BTHMINI_GATT, *PBTHMINI_GATT;

typedef enum _BTHMINI_GATT_PHASE
{
    GattIdle,
    GattServices,
    GattIncludes,
    GattIncludeUuids,
    GattCharacteristics,
    GattDescriptors,
    GattPnpId
} BTHMINI_GATT_PHASE;

typedef enum _BTHMINI_ATT_OP_TYPE
{
    AttOpRead,
    AttOpWrite,
    AttOpWriteCommand,
    AttOpExecute,
    AttOpPrepareWrite,
    AttOpDatabase
} BTHMINI_ATT_OP_TYPE;

typedef struct _BTHMINI_ATT_OP
{
    LIST_ENTRY Link;
    BTHMINI_ATT_OP_TYPE Type;
    WDFREQUEST Request;
    USHORT Handle;
    USHORT Offset;
    UCHAR AttError;
    BOOLEAN Prepare;
    BOOLEAN Executing;
    NTSTATUS DeferredStatus;
    ULONG Length;
    ULONG Capacity;
    ULONGLONG Deadline;
    UCHAR Data[GATT_MAX_VALUE];
} BTHMINI_ATT_OP, *PBTHMINI_ATT_OP;

typedef struct _BTHMINI_GATT_SERVICE
{
    BTH_LE_GATT_SERVICE Info;
    USHORT EndHandle;
    BOOLEAN Secondary;
    BOOLEAN UuidPending;
} BTHMINI_GATT_SERVICE, *PBTHMINI_GATT_SERVICE;

typedef struct _BTHMINI_GATT_INCLUDE
{
    USHORT ServiceHandle;
    USHORT IncludeHandle;
    USHORT StartHandle;
} BTHMINI_GATT_INCLUDE, *PBTHMINI_GATT_INCLUDE;

typedef struct _BTHMINI_GATT_CHARACTERISTIC
{
    BTH_LE_GATT_CHARACTERISTIC Info;
    USHORT EndHandle;
} BTHMINI_GATT_CHARACTERISTIC, *PBTHMINI_GATT_CHARACTERISTIC;

typedef struct _BTHMINI_GATT_VALUE
{
    USHORT Handle;
    USHORT Length;
    UCHAR Data[GATT_MAX_VALUE];
} BTHMINI_GATT_VALUE, *PBTHMINI_GATT_VALUE;

typedef struct _BTHMINI_GATT_EVENT
{
    USHORT Handle;
    USHORT Length;
    UCHAR Data[GATT_MAX_VALUE];
} BTHMINI_GATT_EVENT, *PBTHMINI_GATT_EVENT;

typedef struct _BTHMINI_GATT_SUBSCRIPTION
{
    USHORT Handle;
    USHORT Service;
    PFN_BTHMINI_GATT_NOTIFY Callback;
    PVOID Context;
} BTHMINI_GATT_SUBSCRIPTION, *PBTHMINI_GATT_SUBSCRIPTION;

typedef struct _BTHMINI_GATT_CACHE_HEADER
{
    ULONG Version;
    ULONG ServiceCount;
    ULONG CharacteristicCount;
    ULONG DescriptorCount;
    BOOLEAN HasPnpId;
    UCHAR VendorIdSource;
    USHORT VendorId;
    USHORT ProductId;
    USHORT Version2;
    ULONG IncludeCount;
} BTHMINI_GATT_CACHE_HEADER, *PBTHMINI_GATT_CACHE_HEADER;

typedef struct _BTHMINI_GATT_PDO
{
    LIST_ENTRY Link;
    USHORT Service;
    WDFDEVICE Device;
} BTHMINI_GATT_PDO, *PBTHMINI_GATT_PDO;

struct _BTHMINI_GATT
{
    USHORT Mtu;
    BOOLEAN Ready;
    BOOLEAN MtuExchanged;
    BOOLEAN Discovered;
    BOOLEAN Rediscover;
    BTHMINI_GATT_PHASE Phase;
    ULONG Index;
    USHORT Cursor;
    UCHAR PendingOpcode;
    ULONGLONG Deadline;
    PBTHMINI_ATT_OP Current;
    LIST_ENTRY Operations;
    ULONG ServiceCount;
    ULONG ServiceCapacity;
    PBTHMINI_GATT_SERVICE Services;
    ULONG IncludeCount;
    ULONG IncludeCapacity;
    PBTHMINI_GATT_INCLUDE Includes;
    ULONG CharacteristicCount;
    ULONG CharacteristicCapacity;
    PBTHMINI_GATT_CHARACTERISTIC Characteristics;
    ULONG DescriptorCount;
    ULONG DescriptorCapacity;
    PBTHMINI_GATT_DESCRIPTOR Descriptors;
    PBTHMINI_GATT_VALUE Values;
    ULONG ValueCount;
    BOOLEAN HasPnpId;
    UCHAR VendorIdSource;
    USHORT VendorId;
    USHORT ProductId;
    USHORT Version;
    BTHMINI_GATT_SUBSCRIPTION Subscriptions[GATT_MAX_SUBSCRIPTIONS];
    USHORT ServiceChangedCccd;
    PBTHMINI_GATT_EVENT Events;
    ULONG EventSequence;
    WDFQUEUE Waiters;
    WDFDEVICE DevicePdo;
    LIST_ENTRY ServicePdos;
};

typedef struct _BTHMINI_LE_PDO
{
    PBTHMINI_DEVICE Dev;
    BTH_ADDR Address;
    USHORT Service;
} BTHMINI_LE_PDO, *PBTHMINI_LE_PDO;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHMINI_LE_PDO, BthGattGetPdo)

static
ULONGLONG
BthGattNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
USHORT
BthGattGet16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

static
VOID
BthGattPut16(
    _Out_writes_(2) PUCHAR Data,
    _In_ USHORT Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
}

static
BOOLEAN
BthGattReadUuid(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PBTH_LE_UUID Uuid)
{
    ULONG Index;

    RtlZeroMemory(Uuid, sizeof(*Uuid));
    if (Length == 2)
    {
        Uuid->IsShortUuid = TRUE;
        Uuid->Value.ShortUuid = BthGattGet16(Data);
        return TRUE;
    }
    if (Length != 16)
        return FALSE;
    Uuid->Value.LongUuid.Data1 = (ULONG)Data[12] | ((ULONG)Data[13] << 8) | ((ULONG)Data[14] << 16) |
                                 ((ULONG)Data[15] << 24);
    Uuid->Value.LongUuid.Data2 = BthGattGet16(Data + 10);
    Uuid->Value.LongUuid.Data3 = BthGattGet16(Data + 8);
    for (Index = 0; Index < 8; Index++)
        Uuid->Value.LongUuid.Data4[Index] = Data[7 - Index];
    return TRUE;
}

static
VOID
BthGattUuidToGuid(
    _In_ const BTH_LE_UUID *Uuid,
    _Out_ GUID *Guid)
{
    if (Uuid->IsShortUuid)
    {
        *Guid = BTH_LE_ATT_BLUETOOTH_BASE_GUID;
        Guid->Data1 = Uuid->Value.ShortUuid;
    }
    else
    {
        *Guid = Uuid->Value.LongUuid;
    }
}

static
BOOLEAN
BthGattIsShort(
    _In_ const BTH_LE_UUID *Uuid,
    _In_ USHORT Value)
{
    BTH_LE_UUID Short;

    Short.IsShortUuid = TRUE;
    Short.Value.ShortUuid = Value;
    return IsBthLEUuidMatch(*Uuid, Short);
}

static
NTSTATUS
BthGattStatus(
    _In_ UCHAR Error)
{
    switch (Error)
    {
        case ATT_ERROR_INVALID_HANDLE:
        case ATT_ERROR_ATTRIBUTE_NOT_FOUND:
            return STATUS_NOT_FOUND;
        case ATT_ERROR_READ_NOT_PERMITTED:
        case ATT_ERROR_WRITE_NOT_PERMITTED:
            return STATUS_PRIVILEGE_NOT_HELD;
        case ATT_ERROR_INSUFFICIENT_AUTHENTICATION:
        case ATT_ERROR_INSUFFICIENT_AUTHORIZATION:
        case ATT_ERROR_INSUFFICIENT_ENCRYPTION:
            return STATUS_ACCESS_DENIED;
        case ATT_ERROR_REQUEST_NOT_SUPPORTED:
            return STATUS_NOT_SUPPORTED;
        case ATT_ERROR_INVALID_OFFSET:
        case ATT_ERROR_INVALID_VALUE_LENGTH:
            return STATUS_INVALID_BUFFER_SIZE;
        case ATT_ERROR_INVALID_PDU:
            return STATUS_INVALID_PARAMETER;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}

static
PVOID
BthGattGrow(
    _Inout_ PVOID *Array,
    _Inout_ PULONG Capacity,
    _In_ ULONG Count,
    _In_ ULONG ElementSize)
{
    PVOID NewArray;
    ULONG NewCapacity;

    if (Count < *Capacity)
        return (PUCHAR)*Array + Count * ElementSize;
    NewCapacity = *Capacity ? *Capacity * 2 : 8;
    if (NewCapacity > 0x10000)
        return NULL;
    NewArray = ExAllocatePoolZero(NonPagedPool, NewCapacity * ElementSize, BTHMINI_TAG);
    if (!NewArray)
        return NULL;
    if (*Array)
    {
        RtlCopyMemory(NewArray, *Array, Count * ElementSize);
        ExFreePoolWithTag(*Array, BTHMINI_TAG);
    }
    *Array = NewArray;
    *Capacity = NewCapacity;
    return (PUCHAR)NewArray + Count * ElementSize;
}

static
VOID
BthGattClearDatabase(
    _Inout_ PBTHMINI_GATT Gatt)
{
    if (Gatt->Services)
        ExFreePoolWithTag(Gatt->Services, BTHMINI_TAG);
    if (Gatt->Includes)
        ExFreePoolWithTag(Gatt->Includes, BTHMINI_TAG);
    if (Gatt->Characteristics)
        ExFreePoolWithTag(Gatt->Characteristics, BTHMINI_TAG);
    if (Gatt->Descriptors)
        ExFreePoolWithTag(Gatt->Descriptors, BTHMINI_TAG);
    if (Gatt->Values)
        ExFreePoolWithTag(Gatt->Values, BTHMINI_TAG);
    Gatt->Services = NULL;
    Gatt->Includes = NULL;
    Gatt->Characteristics = NULL;
    Gatt->Descriptors = NULL;
    Gatt->Values = NULL;
    Gatt->ServiceCount = Gatt->ServiceCapacity = 0;
    Gatt->IncludeCount = Gatt->IncludeCapacity = 0;
    Gatt->CharacteristicCount = Gatt->CharacteristicCapacity = 0;
    Gatt->DescriptorCount = Gatt->DescriptorCapacity = 0;
    Gatt->ValueCount = 0;
    Gatt->HasPnpId = FALSE;
    Gatt->Discovered = FALSE;
}

static
PBTHMINI_GATT
BthGattGet(
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    if (Gatt)
        return Gatt;
    Gatt = ExAllocatePoolZero(NonPagedPool, sizeof(*Gatt), BTHMINI_TAG);
    if (!Gatt)
        return NULL;
    Gatt->Mtu = GATT_DEFAULT_MTU;
    InitializeListHead(&Gatt->Operations);
    InitializeListHead(&Gatt->ServicePdos);
    Remote->Gatt = Gatt;
    return Gatt;
}

static
PBTHMINI_GATT_SERVICE
BthGattFindService(
    _In_ PBTHMINI_GATT Gatt,
    _In_ USHORT Handle)
{
    ULONG Index;

    for (Index = 0; Index < Gatt->ServiceCount; Index++)
    {
        if (Gatt->Services[Index].Info.AttributeHandle == Handle)
            return &Gatt->Services[Index];
    }
    return NULL;
}

static
BOOLEAN
BthGattServiceIncludes(
    _In_ PBTHMINI_GATT Gatt,
    _In_ USHORT ServiceHandle,
    _In_ USHORT IncludedHandle,
    _In_ ULONG Depth)
{
    ULONG Index;

    if (Depth > GATT_MAX_INCLUDE_DEPTH)
        return FALSE;
    for (Index = 0; Index < Gatt->IncludeCount; Index++)
    {
        if (Gatt->Includes[Index].ServiceHandle != ServiceHandle)
            continue;
        if (Gatt->Includes[Index].StartHandle == IncludedHandle ||
            BthGattServiceIncludes(Gatt, Gatt->Includes[Index].StartHandle, IncludedHandle, Depth + 1))
            return TRUE;
    }
    return FALSE;
}

static
PBTHMINI_GATT_CHARACTERISTIC
BthGattFindValueHandle(
    _In_ PBTHMINI_GATT Gatt,
    _In_ USHORT Handle)
{
    ULONG Index;

    for (Index = 0; Index < Gatt->CharacteristicCount; Index++)
    {
        if (Gatt->Characteristics[Index].Info.CharacteristicValueHandle == Handle)
            return &Gatt->Characteristics[Index];
    }
    return NULL;
}

static
PBTHMINI_GATT_VALUE
BthGattFindValue(
    _In_ PBTHMINI_GATT Gatt,
    _In_ USHORT Handle)
{
    ULONG Index;

    for (Index = 0; Index < Gatt->ValueCount; Index++)
    {
        if (Gatt->Values[Index].Handle == Handle)
            return &Gatt->Values[Index];
    }
    return NULL;
}

static
VOID
BthGattStoreValue(
    _In_ PBTHMINI_GATT Gatt,
    _In_ USHORT Handle,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_GATT_VALUE Value;

    ULONG Index;

    for (Index = 0; Index < Gatt->DescriptorCount; Index++)
    {
        if (Gatt->Descriptors[Index].AttributeHandle == Handle)
            break;
    }
    if (Index == Gatt->DescriptorCount && !BthGattFindValueHandle(Gatt, Handle))
        return;
    Value = BthGattFindValue(Gatt, Handle);
    if (!Value)
    {
        if (Gatt->ValueCount >= Gatt->CharacteristicCount + Gatt->DescriptorCount)
            return;
        if (!Gatt->Values)
        {
            Gatt->Values = ExAllocatePoolZero(NonPagedPool,
                                              (Gatt->CharacteristicCount + Gatt->DescriptorCount) * sizeof(*Value),
                                              BTHMINI_TAG);
            if (!Gatt->Values)
                return;
        }
        Value = &Gatt->Values[Gatt->ValueCount++];
        Value->Handle = Handle;
    }
    Value->Length = (USHORT)min(Length, sizeof(Value->Data));
    RtlCopyMemory(Value->Data, Data, Value->Length);
}

static
NTSTATUS
BthGattSend(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Pdu,
    _In_ ULONG Length)
{
    return BthL2capSendLe(Dev, Remote, LE_CID_ATT, Pdu, Length);
}

static
VOID
BthGattRequest(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Pdu,
    _In_ ULONG Length)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    Gatt->PendingOpcode = Pdu[0];
    Gatt->Deadline = BthGattNowMs() + GATT_TIMEOUT_MS;
    if (!NT_SUCCESS(BthGattSend(Dev, Remote, Pdu, Length)))
        Gatt->Deadline = BthGattNowMs();
}

static
VOID
BthGattRequestRange(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Opcode,
    _In_ USHORT Start,
    _In_ USHORT End,
    _In_ USHORT Type)
{
    UCHAR Pdu[7];
    ULONG Length = 5;

    Pdu[0] = Opcode;
    BthGattPut16(Pdu + 1, Start);
    BthGattPut16(Pdu + 3, End);
    if (Type)
    {
        BthGattPut16(Pdu + 5, Type);
        Length = 7;
    }
    BthGattRequest(Dev, Remote, Pdu, Length);
}

static
VOID
BthGattCompleteOp(
    _In_ PBTHMINI_ATT_OP Op,
    _In_ NTSTATUS Status)
{
    WDFREQUEST Request = Op->Request;

    if (Request)
    {
        WDF_REQUEST_PARAMETERS Parameters;
        ULONG_PTR Information = 0;

        WDF_REQUEST_PARAMETERS_INIT(&Parameters);
        WdfRequestGetParameters(Request, &Parameters);
        if (Parameters.Type == WdfRequestTypeDeviceControlInternal)
        {
            PBTHMINI_GATT_REQUEST Gatt = Parameters.Parameters.Others.Arg1;

            if (Op->Type == AttOpRead && NT_SUCCESS(Status))
            {
                RtlCopyMemory(Gatt->Buffer, Op->Data, min(Op->Length, Gatt->Length));
                Gatt->Length = min(Op->Length, Gatt->Length);
            }
        }
        else if (Parameters.Parameters.DeviceIoControl.IoControlCode == IOCTL_BTHLE_GATT_READ_VALUE ||
                 Parameters.Parameters.DeviceIoControl.IoControlCode == IOCTL_BTHLE_GATT_WRITE_VALUE ||
                 Parameters.Parameters.DeviceIoControl.IoControlCode == IOCTL_BTHLE_GATT_EXECUTE_WRITE)
        {
            BOOLEAN Execute = Parameters.Parameters.DeviceIoControl.IoControlCode == IOCTL_BTHLE_GATT_EXECUTE_WRITE;
            size_t OutputLength;
            PVOID Output;

            if (Op->AttError)
                Status = STATUS_SUCCESS;
            if (NT_SUCCESS(Status) &&
                NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, Execute ? sizeof(BTHLE_GATT_EXECUTE_WRITE) :
                                                                             FIELD_OFFSET(BTHLE_GATT_VALUE, Data),
                                                          &Output, &OutputLength)))
            {
                if (Execute)
                {
                    ((PBTHLE_GATT_EXECUTE_WRITE)Output)->AttError = Op->AttError;
                    Information = sizeof(BTHLE_GATT_EXECUTE_WRITE);
                }
                else
                {
                    PBTHLE_GATT_VALUE Value = Output;
                    ULONG Size = Op->Type == AttOpRead && !Op->AttError ? Op->Length : 0;

                    Value->AttError = Op->AttError;
                    Value->Size = Size;
                    Information = FIELD_OFFSET(BTHLE_GATT_VALUE, Data);
                    if (OutputLength >= FIELD_OFFSET(BTHLE_GATT_VALUE, Data[Size]))
                    {
                        RtlCopyMemory(Value->Data, Op->Data, Size);
                        Information = FIELD_OFFSET(BTHLE_GATT_VALUE, Data[Size]);
                    }
                    else
                    {
                        Status = STATUS_MORE_ENTRIES;
                    }
                }
            }
        }
        else if (Op->Type == AttOpRead && NT_SUCCESS(Status))
        {
            BTHMINI_GATT_READ_VALUE *Params;
            size_t OutputLength;

            if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(*Params), (PVOID *)&Params, &OutputLength)))
            {
                ULONG Needed = FIELD_OFFSET(BTHMINI_GATT_READ_VALUE, Buffer[Op->Length]);

                Params->Size = Op->Length;
                if (OutputLength >= Needed)
                {
                    RtlCopyMemory(Params->Buffer, Op->Data, Op->Length);
                    Information = Needed;
                }
                else
                {
                    Status = STATUS_MORE_ENTRIES;
                    Information = sizeof(*Params);
                }
            }
        }
        WdfRequestCompleteWithInformation(Request, Status, Information);
    }
    ExFreePoolWithTag(Op, BTHMINI_TAG);
}

static
VOID
BthGattFailOperations(
    _In_ PBTHMINI_GATT Gatt,
    _In_ NTSTATUS Status)
{
    if (Gatt->Current)
    {
        BthGattCompleteOp(Gatt->Current, Status);
        Gatt->Current = NULL;
    }
    while (!IsListEmpty(&Gatt->Operations))
        BthGattCompleteOp(CONTAINING_RECORD(RemoveHeadList(&Gatt->Operations), BTHMINI_ATT_OP, Link), Status);
}

static
VOID
BthGattSaveCache(
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    PBTHMINI_GATT_CACHE_HEADER Header;
    UNICODE_STRING ValueName;
    WCHAR Device[13];
    PUCHAR Buffer, Cursor;
    ULONG Length;
    HANDLE Key;

    Length = sizeof(*Header) + Gatt->ServiceCount * sizeof(BTHMINI_GATT_SERVICE) +
             Gatt->CharacteristicCount * sizeof(BTHMINI_GATT_CHARACTERISTIC) +
             Gatt->DescriptorCount * sizeof(BTHMINI_GATT_DESCRIPTOR) +
             Gatt->IncludeCount * sizeof(BTHMINI_GATT_INCLUDE);
    Buffer = ExAllocatePoolZero(PagedPool, Length, BTHMINI_TAG);
    if (!Buffer)
        return;
    Header = (PBTHMINI_GATT_CACHE_HEADER)Buffer;
    Header->Version = GATT_CACHE_VERSION;
    Header->ServiceCount = Gatt->ServiceCount;
    Header->CharacteristicCount = Gatt->CharacteristicCount;
    Header->DescriptorCount = Gatt->DescriptorCount;
    Header->HasPnpId = Gatt->HasPnpId;
    Header->VendorIdSource = Gatt->VendorIdSource;
    Header->VendorId = Gatt->VendorId;
    Header->ProductId = Gatt->ProductId;
    Header->Version2 = Gatt->Version;
    Header->IncludeCount = Gatt->IncludeCount;
    Cursor = Buffer + sizeof(*Header);
    RtlCopyMemory(Cursor, Gatt->Services, Gatt->ServiceCount * sizeof(BTHMINI_GATT_SERVICE));
    Cursor += Gatt->ServiceCount * sizeof(BTHMINI_GATT_SERVICE);
    RtlCopyMemory(Cursor, Gatt->Characteristics, Gatt->CharacteristicCount * sizeof(BTHMINI_GATT_CHARACTERISTIC));
    Cursor += Gatt->CharacteristicCount * sizeof(BTHMINI_GATT_CHARACTERISTIC);
    RtlCopyMemory(Cursor, Gatt->Descriptors, Gatt->DescriptorCount * sizeof(BTHMINI_GATT_DESCRIPTOR));
    Cursor += Gatt->DescriptorCount * sizeof(BTHMINI_GATT_DESCRIPTOR);
    RtlCopyMemory(Cursor, Gatt->Includes, Gatt->IncludeCount * sizeof(BTHMINI_GATT_INCLUDE));

    BthFormatAddress(Remote->Address, Device);
    if (NT_SUCCESS(BthOpenParameters(L"Devices", Device, &Key)))
    {
        RtlInitUnicodeString(&ValueName, L"GattCache");
        ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, Buffer, Length);
        ZwClose(Key);
    }
    ExFreePoolWithTag(Buffer, BTHMINI_TAG);
}

static
BOOLEAN
BthGattLoadCache(
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    PKEY_VALUE_PARTIAL_INFORMATION Value = NULL;
    BTHMINI_GATT_CACHE_HEADER Header;
    UNICODE_STRING ValueName;
    WCHAR Device[13];
    ULONG Size = 0, Expected;
    const UCHAR *Cursor;
    BOOLEAN Loaded = FALSE;
    HANDLE Key;

    BthFormatAddress(Remote->Address, Device);
    if (!NT_SUCCESS(BthOpenParameters(L"Devices", Device, &Key)))
        return FALSE;
    RtlInitUnicodeString(&ValueName, L"GattCache");
    if (ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, NULL, 0, &Size) != STATUS_BUFFER_TOO_SMALL || !Size)
        goto Exit;
    Value = ExAllocatePoolWithTag(PagedPool, Size, BTHMINI_TAG);
    if (!Value || !NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, Size, &Size)) ||
        Value->Type != REG_BINARY || Value->DataLength < sizeof(Header))
        goto Exit;
    RtlCopyMemory(&Header, Value->Data, sizeof(Header));
    if (Header.Version != GATT_CACHE_VERSION || Header.ServiceCount > 0x1000 || Header.CharacteristicCount > 0x4000 ||
        Header.DescriptorCount > 0x4000 || Header.IncludeCount > 0x1000)
        goto Exit;
    Expected = sizeof(Header) + Header.ServiceCount * sizeof(BTHMINI_GATT_SERVICE) +
               Header.CharacteristicCount * sizeof(BTHMINI_GATT_CHARACTERISTIC) +
               Header.DescriptorCount * sizeof(BTHMINI_GATT_DESCRIPTOR) +
               Header.IncludeCount * sizeof(BTHMINI_GATT_INCLUDE);
    if (Value->DataLength != Expected)
        goto Exit;

    BthGattClearDatabase(Gatt);
    Cursor = Value->Data + sizeof(Header);
    if (Header.ServiceCount)
    {
        Gatt->Services = ExAllocatePoolWithTag(NonPagedPool, Header.ServiceCount * sizeof(BTHMINI_GATT_SERVICE), BTHMINI_TAG);
        if (!Gatt->Services)
            goto Exit;
        RtlCopyMemory(Gatt->Services, Cursor, Header.ServiceCount * sizeof(BTHMINI_GATT_SERVICE));
        Gatt->ServiceCount = Gatt->ServiceCapacity = Header.ServiceCount;
    }
    Cursor += Header.ServiceCount * sizeof(BTHMINI_GATT_SERVICE);
    if (Header.CharacteristicCount)
    {
        Gatt->Characteristics = ExAllocatePoolWithTag(NonPagedPool,
                                                      Header.CharacteristicCount * sizeof(BTHMINI_GATT_CHARACTERISTIC),
                                                      BTHMINI_TAG);
        if (!Gatt->Characteristics)
        {
            BthGattClearDatabase(Gatt);
            goto Exit;
        }
        RtlCopyMemory(Gatt->Characteristics, Cursor, Header.CharacteristicCount * sizeof(BTHMINI_GATT_CHARACTERISTIC));
        Gatt->CharacteristicCount = Gatt->CharacteristicCapacity = Header.CharacteristicCount;
    }
    Cursor += Header.CharacteristicCount * sizeof(BTHMINI_GATT_CHARACTERISTIC);
    if (Header.DescriptorCount)
    {
        Gatt->Descriptors = ExAllocatePoolWithTag(NonPagedPool, Header.DescriptorCount * sizeof(BTHMINI_GATT_DESCRIPTOR),
                                                  BTHMINI_TAG);
        if (!Gatt->Descriptors)
        {
            BthGattClearDatabase(Gatt);
            goto Exit;
        }
        RtlCopyMemory(Gatt->Descriptors, Cursor, Header.DescriptorCount * sizeof(BTHMINI_GATT_DESCRIPTOR));
        Gatt->DescriptorCount = Gatt->DescriptorCapacity = Header.DescriptorCount;
    }
    Cursor += Header.DescriptorCount * sizeof(BTHMINI_GATT_DESCRIPTOR);
    if (Header.IncludeCount)
    {
        Gatt->Includes = ExAllocatePoolWithTag(NonPagedPool, Header.IncludeCount * sizeof(BTHMINI_GATT_INCLUDE),
                                               BTHMINI_TAG);
        if (!Gatt->Includes)
        {
            BthGattClearDatabase(Gatt);
            goto Exit;
        }
        RtlCopyMemory(Gatt->Includes, Cursor, Header.IncludeCount * sizeof(BTHMINI_GATT_INCLUDE));
        Gatt->IncludeCount = Gatt->IncludeCapacity = Header.IncludeCount;
    }
    Gatt->HasPnpId = Header.HasPnpId;
    Gatt->VendorIdSource = Header.VendorIdSource;
    Gatt->VendorId = Header.VendorId;
    Gatt->ProductId = Header.ProductId;
    Gatt->Version = Header.Version2;
    Gatt->Discovered = TRUE;
    Loaded = TRUE;

Exit:
    if (Value)
        ExFreePoolWithTag(Value, BTHMINI_TAG);
    ZwClose(Key);
    return Loaded;
}

static
VOID
BthGattDeleteCache(
    _In_ PBTHMINI_REMOTE Remote)
{
    UNICODE_STRING ValueName;
    WCHAR Device[13];
    HANDLE Key;

    BthFormatAddress(Remote->Address, Device);
    if (NT_SUCCESS(BthOpenParameters(L"Devices", Device, &Key)))
    {
        RtlInitUnicodeString(&ValueName, L"GattCache");
        ZwDeleteValueKey(Key, &ValueName);
        ZwClose(Key);
    }
}

static
VOID
NTAPI
BthGattEvtPdoIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PBTHMINI_LE_PDO Pdo = BthGattGetPdo(WdfIoQueueGetDevice(Queue));
    WDF_REQUEST_FORWARD_OPTIONS Forward;
    PBTHMINI_REQUEST Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(IoControlCode);

    Context = BthMiniGetRequest(Request);
    Context->Address = Pdo->Address;
    Context->Gatt = TRUE;
    Context->Service = Pdo->Service;
    WDF_REQUEST_FORWARD_OPTIONS_INIT(&Forward);
    Status = WdfRequestForwardToParentDeviceIoQueue(Request, Pdo->Dev->Queue, &Forward);
    if (!NT_SUCCESS(Status))
        WdfRequestComplete(Request, Status);
}

static
NTSTATUS
BthGattCreatePdo(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_opt_ PBTHMINI_GATT_SERVICE Service,
    _Out_ WDFDEVICE *Device)
{
    DECLARE_UNICODE_STRING_SIZE(DeviceId, 160);
    DECLARE_UNICODE_STRING_SIZE(HardwareId, 160);
    DECLARE_UNICODE_STRING_SIZE(ShortId, 160);
    DECLARE_UNICODE_STRING_SIZE(CompatibleId, 96);
    DECLARE_UNICODE_STRING_SIZE(InstanceId, 32);
    DECLARE_UNICODE_STRING_SIZE(Description, BTH_MAX_NAME_SIZE + 16);
    PBTHMINI_GATT Gatt = Remote->Gatt;
    WDF_DEVICE_PNP_CAPABILITIES Capabilities;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PWDFDEVICE_INIT Init;
    PBTHMINI_LE_PDO Pdo;
    WDFDEVICE Child;
    NTSTATUS Status;

    *Device = NULL;
    if (Service)
    {
        GUID Guid;
        WCHAR Uuid[40];

        BthGattUuidToGuid(&Service->Info.ServiceUuid, &Guid);
        Status = RtlStringCchPrintfW(Uuid, RTL_NUMBER_OF(Uuid), L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                                     Guid.Data1, Guid.Data2, Guid.Data3, Guid.Data4[0], Guid.Data4[1], Guid.Data4[2],
                                     Guid.Data4[3], Guid.Data4[4], Guid.Data4[5], Guid.Data4[6], Guid.Data4[7]);
        if (NT_SUCCESS(Status) && Gatt->HasPnpId)
        {
            Status = RtlUnicodeStringPrintf(&HardwareId, L"BTHLEDevice\\%ws_Dev_VID&%02x%04x_PID&%04x_REV&%04x", Uuid,
                                            Gatt->VendorIdSource, Gatt->VendorId, Gatt->ProductId, Gatt->Version);
            if (NT_SUCCESS(Status))
                Status = RtlUnicodeStringPrintf(&ShortId, L"BTHLEDevice\\%ws_Dev_VID&%02x%04x_PID&%04x", Uuid,
                                                Gatt->VendorIdSource, Gatt->VendorId, Gatt->ProductId);
            if (NT_SUCCESS(Status))
                Status = RtlUnicodeStringPrintf(&DeviceId, L"%wZ_%012I64x", &HardwareId, Remote->Address);
        }
        else if (NT_SUCCESS(Status))
        {
            Status = RtlUnicodeStringPrintf(&DeviceId, L"BTHLEDevice\\%ws_%012I64x", Uuid, Remote->Address);
        }
        if (NT_SUCCESS(Status))
            Status = RtlUnicodeStringPrintf(&CompatibleId, L"BTHLEDevice\\%ws", Uuid);
        if (NT_SUCCESS(Status))
            Status = RtlUnicodeStringPrintf(&InstanceId, L"%04x", Service->Info.AttributeHandle);
    }
    else
    {
        Status = RtlUnicodeStringPrintf(&DeviceId, L"BTHLE\\Dev_%012I64x", Remote->Address);
        if (NT_SUCCESS(Status))
            Status = RtlUnicodeStringPrintf(&CompatibleId, L"BTHLE\\GenericDevice");
        if (NT_SUCCESS(Status))
            Status = RtlUnicodeStringPrintf(&InstanceId, L"%012I64X", Dev->Address);
    }
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&Description, L"%hs", Remote->Name[0] ? Remote->Name : "Bluetooth LE Device");
    if (!NT_SUCCESS(Status))
        return Status;

    Init = WdfPdoInitAllocate(Dev->Device);
    if (!Init)
        return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_BLUETOOTH);
    WdfPdoInitAllowForwardingRequestToParent(Init);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_REQUEST);
    WdfDeviceInitSetRequestAttributes(Init, &Attributes);
    Status = WdfDeviceInitAssignSDDLString(Init, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignRawDevice(Init, &GUID_DEVCLASS_BLUETOOTH);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignDeviceID(Init, &DeviceId);
    if (NT_SUCCESS(Status) && HardwareId.Length)
        Status = WdfPdoInitAddHardwareID(Init, &HardwareId);
    if (NT_SUCCESS(Status) && ShortId.Length)
        Status = WdfPdoInitAddHardwareID(Init, &ShortId);
    if (NT_SUCCESS(Status) && !HardwareId.Length)
        Status = WdfPdoInitAddHardwareID(Init, &DeviceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddCompatibleID(Init, &CompatibleId);
    if (NT_SUCCESS(Status) && Service)
    {
        DECLARE_CONST_UNICODE_STRING(GenericId, L"BTHLEDevice\\GenericATTService");

        Status = WdfPdoInitAddCompatibleID(Init, &GenericId);
    }
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignInstanceID(Init, &InstanceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddDeviceText(Init, &Description, &Description, 0x409);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    WdfPdoInitSetDefaultLocale(Init, 0x409);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_LE_PDO);
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    Pdo = BthGattGetPdo(Child);
    Pdo->Dev = Dev;
    Pdo->Address = Remote->Address;
    Pdo->Service = Service ? Service->Info.AttributeHandle : 0;

    WDF_DEVICE_PNP_CAPABILITIES_INIT(&Capabilities);
    Capabilities.Removable = WdfTrue;
    Capabilities.SurpriseRemovalOK = WdfTrue;
    Capabilities.UniqueID = WdfTrue;
    WdfDeviceSetPnpCapabilities(Child, &Capabilities);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoDeviceControl = BthGattEvtPdoIoctl;
    QueueConfig.EvtIoInternalDeviceControl = BthGattEvtPdoIoctl;
    QueueConfig.PowerManaged = WdfFalse;
    Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (NT_SUCCESS(Status))
        Status = WdfDeviceCreateDeviceInterface(Child, Service ? &GUID_BLUETOOTH_GATT_SERVICE_DEVICE_INTERFACE :
                                                                &GUID_BLUETOOTHLE_DEVICE_INTERFACE, NULL);
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Dev->Device, Child);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Child);
        return Status;
    }
    *Device = Child;
    DPRINT1("BTHMINI: %wZ enumerated\n", &DeviceId);
    return STATUS_SUCCESS;
}

static
VOID
BthGattSyncPdos(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    BOOLEAN Changed = FALSE;
    PLIST_ENTRY Entry;
    ULONG Index;

    if (!Gatt->DevicePdo && NT_SUCCESS(BthGattCreatePdo(Dev, Remote, NULL, &Gatt->DevicePdo)))
        Changed = TRUE;

    Entry = Gatt->ServicePdos.Flink;
    while (Entry != &Gatt->ServicePdos)
    {
        PBTHMINI_GATT_PDO ServicePdo = CONTAINING_RECORD(Entry, BTHMINI_GATT_PDO, Link);

        Entry = Entry->Flink;
        if (Gatt->Discovered && BthGattFindService(Gatt, ServicePdo->Service))
            continue;
        RemoveEntryList(&ServicePdo->Link);
        if (!NT_SUCCESS(WdfPdoMarkMissing(ServicePdo->Device)))
            DPRINT1("BTHMINI: GATT service 0x%04x PDO not marked missing\n", ServicePdo->Service);
        ExFreePoolWithTag(ServicePdo, BTHMINI_TAG);
        Changed = TRUE;
    }

    for (Index = 0; Gatt->Discovered && Index < Gatt->ServiceCount; Index++)
    {
        PBTHMINI_GATT_SERVICE Service = &Gatt->Services[Index];
        PBTHMINI_GATT_PDO ServicePdo;
        BOOLEAN Found = FALSE;

        if (Service->Secondary)
            continue;
        for (Entry = Gatt->ServicePdos.Flink; Entry != &Gatt->ServicePdos; Entry = Entry->Flink)
        {
            if (CONTAINING_RECORD(Entry, BTHMINI_GATT_PDO, Link)->Service == Service->Info.AttributeHandle)
            {
                Found = TRUE;
                break;
            }
        }
        if (Found)
            continue;
        ServicePdo = ExAllocatePoolZero(NonPagedPool, sizeof(*ServicePdo), BTHMINI_TAG);
        if (!ServicePdo)
            break;
        if (!NT_SUCCESS(BthGattCreatePdo(Dev, Remote, Service, &ServicePdo->Device)))
        {
            ExFreePoolWithTag(ServicePdo, BTHMINI_TAG);
            continue;
        }
        ServicePdo->Service = Service->Info.AttributeHandle;
        InsertTailList(&Gatt->ServicePdos, &ServicePdo->Link);
        Changed = TRUE;
    }
    if (Changed)
        IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
}

static
VOID
BthGattRemovePdos(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_GATT Gatt)
{
    BOOLEAN Changed = FALSE;

    while (!IsListEmpty(&Gatt->ServicePdos))
    {
        PBTHMINI_GATT_PDO ServicePdo = CONTAINING_RECORD(RemoveHeadList(&Gatt->ServicePdos), BTHMINI_GATT_PDO, Link);

        if (!NT_SUCCESS(WdfPdoMarkMissing(ServicePdo->Device)))
            DPRINT1("BTHMINI: GATT service 0x%04x PDO not marked missing\n", ServicePdo->Service);
        ExFreePoolWithTag(ServicePdo, BTHMINI_TAG);
        Changed = TRUE;
    }
    if (Gatt->DevicePdo)
    {
        if (!NT_SUCCESS(WdfPdoMarkMissing(Gatt->DevicePdo)))
            DPRINT1("BTHMINI: LE device PDO not marked missing\n");
        Gatt->DevicePdo = NULL;
        Changed = TRUE;
    }
    if (Changed)
        IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
}

static
VOID
BthGattFinishDiscovery(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    Gatt->Phase = GattIdle;
    Gatt->Discovered = TRUE;
    Gatt->Rediscover = FALSE;
    DPRINT1("BTHMINI: GATT database of %012I64x: %lu services, %lu characteristics, %lu descriptors\n",
            Remote->Address, Gatt->ServiceCount, Gatt->CharacteristicCount, Gatt->DescriptorCount);
    if (Remote->HasLeKey)
    {
        BthGattSaveCache(Remote);
        BthGattSyncPdos(Dev, Remote);
    }
}

static
VOID
BthGattDiscoverStep(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    for (;;)
    {
        switch (Gatt->Phase)
        {
            case GattServices:
                BthGattRequestRange(Dev, Remote, ATT_READ_BY_GROUP_TYPE_REQUEST, Gatt->Cursor, 0xFFFF,
                                    GATT_UUID_PRIMARY_SERVICE);
                return;

            case GattIncludes:
                if (Gatt->Index < Gatt->ServiceCount)
                {
                    PBTHMINI_GATT_SERVICE Service = &Gatt->Services[Gatt->Index];

                    if (Gatt->Cursor < Service->Info.AttributeHandle)
                        Gatt->Cursor = Service->Info.AttributeHandle;
                    if (Gatt->Cursor <= Service->EndHandle)
                    {
                        BthGattRequestRange(Dev, Remote, ATT_READ_BY_TYPE_REQUEST, Gatt->Cursor, Service->EndHandle,
                                            GATT_UUID_INCLUDE);
                        return;
                    }
                    Gatt->Index++;
                    Gatt->Cursor = 0;
                    continue;
                }
                Gatt->Phase = GattIncludeUuids;
                Gatt->Index = 0;
                continue;

            case GattIncludeUuids:
                for (; Gatt->Index < Gatt->ServiceCount; Gatt->Index++)
                {
                    if (Gatt->Services[Gatt->Index].UuidPending)
                    {
                        UCHAR Pdu[3];

                        Pdu[0] = ATT_READ_REQUEST;
                        BthGattPut16(Pdu + 1, Gatt->Services[Gatt->Index].Info.AttributeHandle);
                        BthGattRequest(Dev, Remote, Pdu, sizeof(Pdu));
                        return;
                    }
                }
                Gatt->Phase = GattCharacteristics;
                Gatt->Index = 0;
                Gatt->Cursor = 0;
                continue;

            case GattCharacteristics:
                if (Gatt->Index < Gatt->ServiceCount)
                {
                    PBTHMINI_GATT_SERVICE Service = &Gatt->Services[Gatt->Index];

                    if (Gatt->Cursor < Service->Info.AttributeHandle)
                        Gatt->Cursor = Service->Info.AttributeHandle;
                    if (Gatt->Cursor <= Service->EndHandle)
                    {
                        BthGattRequestRange(Dev, Remote, ATT_READ_BY_TYPE_REQUEST, Gatt->Cursor, Service->EndHandle,
                                            GATT_UUID_CHARACTERISTIC);
                        return;
                    }
                    Gatt->Index++;
                    Gatt->Cursor = 0;
                    continue;
                }
                {
                    ULONG Index;

                    for (Index = 0; Index < Gatt->CharacteristicCount; Index++)
                    {
                        PBTHMINI_GATT_CHARACTERISTIC Characteristic = &Gatt->Characteristics[Index];
                        PBTHMINI_GATT_SERVICE Service = BthGattFindService(Gatt, Characteristic->Info.ServiceHandle);

                        Characteristic->EndHandle = Service ? Service->EndHandle : Characteristic->Info.CharacteristicValueHandle;
                        if (Index + 1 < Gatt->CharacteristicCount &&
                            Gatt->Characteristics[Index + 1].Info.ServiceHandle == Characteristic->Info.ServiceHandle)
                        {
                            Characteristic->EndHandle = Gatt->Characteristics[Index + 1].Info.AttributeHandle - 1;
                        }
                    }
                }
                Gatt->Phase = GattDescriptors;
                Gatt->Index = 0;
                Gatt->Cursor = 0;
                continue;

            case GattDescriptors:
                if (Gatt->Index < Gatt->CharacteristicCount)
                {
                    PBTHMINI_GATT_CHARACTERISTIC Characteristic = &Gatt->Characteristics[Gatt->Index];

                    if (Gatt->Cursor <= Characteristic->Info.CharacteristicValueHandle)
                        Gatt->Cursor = Characteristic->Info.CharacteristicValueHandle + 1;
                    if (Characteristic->Info.CharacteristicValueHandle < 0xFFFF &&
                        Gatt->Cursor <= Characteristic->EndHandle)
                    {
                        BthGattRequestRange(Dev, Remote, ATT_FIND_INFORMATION_REQUEST, Gatt->Cursor,
                                            Characteristic->EndHandle, 0);
                        return;
                    }
                    Gatt->Index++;
                    Gatt->Cursor = 0;
                    continue;
                }
                Gatt->Phase = GattPnpId;
                continue;

            case GattPnpId:
            {
                ULONG Index;

                for (Index = 0; Index < Gatt->CharacteristicCount; Index++)
                {
                    PBTHMINI_GATT_CHARACTERISTIC Characteristic = &Gatt->Characteristics[Index];
                    PBTHMINI_GATT_SERVICE Service = BthGattFindService(Gatt, Characteristic->Info.ServiceHandle);

                    if (Service && BthGattIsShort(&Service->Info.ServiceUuid, GATT_SERVICE_DEVICE_INFORMATION) &&
                        BthGattIsShort(&Characteristic->Info.CharacteristicUuid, GATT_UUID_PNP_ID) &&
                        Characteristic->Info.IsReadable && !Gatt->HasPnpId)
                    {
                        UCHAR Pdu[3];

                        Pdu[0] = ATT_READ_REQUEST;
                        BthGattPut16(Pdu + 1, Characteristic->Info.CharacteristicValueHandle);
                        BthGattRequest(Dev, Remote, Pdu, sizeof(Pdu));
                        return;
                    }
                }
                BthGattFinishDiscovery(Dev, Remote);
                return;
            }

            default:
                return;
        }
    }
}

static
VOID
BthGattDiscoveryResponse(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    BOOLEAN Done = Data[0] == ATT_ERROR_RESPONSE;
    USHORT Last = 0;
    ULONG Offset, Entry;

    switch (Gatt->Phase)
    {
        case GattServices:
            if (!Done && Data[0] == ATT_READ_BY_GROUP_TYPE_RESPONSE && Length >= 2 && (Data[1] == 6 || Data[1] == 20))
            {
                Entry = Data[1];
                for (Offset = 2; Offset + Entry <= Length; Offset += Entry)
                {
                    PBTHMINI_GATT_SERVICE Service;
                    USHORT Start = BthGattGet16(Data + Offset), End = BthGattGet16(Data + Offset + 2);

                    if (Start < Gatt->Cursor || End < Start)
                    {
                        Done = TRUE;
                        break;
                    }
                    Service = BthGattGrow((PVOID *)&Gatt->Services, &Gatt->ServiceCapacity, Gatt->ServiceCount,
                                          sizeof(*Service));
                    if (!Service)
                    {
                        Done = TRUE;
                        break;
                    }
                    RtlZeroMemory(Service, sizeof(*Service));
                    BthGattReadUuid(Data + Offset + 4, Entry - 4, &Service->Info.ServiceUuid);
                    Service->Info.AttributeHandle = Start;
                    Service->EndHandle = End;
                    Gatt->ServiceCount++;
                    Last = End;
                }
                if (Last == 0xFFFF || !Last)
                    Done = TRUE;
                else
                    Gatt->Cursor = Last + 1;
            }
            else
            {
                Done = TRUE;
            }
            if (Done)
            {
                Gatt->Phase = GattIncludes;
                Gatt->Index = 0;
                Gatt->Cursor = 0;
            }
            break;

        case GattIncludes:
            if (!Done && Data[0] == ATT_READ_BY_TYPE_RESPONSE && Length >= 2 && (Data[1] == 6 || Data[1] == 8))
            {
                USHORT ServiceHandle = Gatt->Services[Gatt->Index].Info.AttributeHandle;
                USHORT ServiceEnd = Gatt->Services[Gatt->Index].EndHandle;

                Entry = Data[1];
                for (Offset = 2; Offset + Entry <= Length; Offset += Entry)
                {
                    PBTHMINI_GATT_INCLUDE Include;
                    USHORT Handle = BthGattGet16(Data + Offset);
                    USHORT Start = BthGattGet16(Data + Offset + 2), End = BthGattGet16(Data + Offset + 4);

                    if (Handle < Gatt->Cursor || Handle > ServiceEnd || End < Start)
                    {
                        Done = TRUE;
                        break;
                    }
                    Include = BthGattGrow((PVOID *)&Gatt->Includes, &Gatt->IncludeCapacity, Gatt->IncludeCount,
                                          sizeof(*Include));
                    if (!Include)
                    {
                        Done = TRUE;
                        break;
                    }
                    Include->ServiceHandle = ServiceHandle;
                    Include->IncludeHandle = Handle;
                    Include->StartHandle = Start;
                    Gatt->IncludeCount++;
                    if (!BthGattFindService(Gatt, Start))
                    {
                        PBTHMINI_GATT_SERVICE Included = BthGattGrow((PVOID *)&Gatt->Services, &Gatt->ServiceCapacity,
                                                                     Gatt->ServiceCount, sizeof(*Included));

                        if (!Included)
                        {
                            Done = TRUE;
                            break;
                        }
                        RtlZeroMemory(Included, sizeof(*Included));
                        Included->Info.AttributeHandle = Start;
                        Included->EndHandle = End;
                        Included->Secondary = TRUE;
                        if (Entry == 8)
                            BthGattReadUuid(Data + Offset + 6, 2, &Included->Info.ServiceUuid);
                        else
                            Included->UuidPending = TRUE;
                        Gatt->ServiceCount++;
                    }
                    Last = Handle;
                }
                if (!Done && Last && Last < ServiceEnd)
                    Gatt->Cursor = Last + 1;
                else
                    Done = TRUE;
            }
            else
            {
                Done = TRUE;
            }
            if (Done)
            {
                Gatt->Index++;
                Gatt->Cursor = 0;
            }
            break;

        case GattIncludeUuids:
            if (Data[0] == ATT_READ_RESPONSE && Length == 17)
                BthGattReadUuid(Data + 1, 16, &Gatt->Services[Gatt->Index].Info.ServiceUuid);
            Gatt->Services[Gatt->Index].UuidPending = FALSE;
            Gatt->Index++;
            break;

        case GattCharacteristics:
            if (!Done && Data[0] == ATT_READ_BY_TYPE_RESPONSE && Length >= 2 && (Data[1] == 7 || Data[1] == 21))
            {
                PBTHMINI_GATT_SERVICE Service = &Gatt->Services[Gatt->Index];

                Entry = Data[1];
                for (Offset = 2; Offset + Entry <= Length; Offset += Entry)
                {
                    PBTHMINI_GATT_CHARACTERISTIC Characteristic;
                    USHORT Handle = BthGattGet16(Data + Offset);
                    UCHAR Properties = Data[Offset + 2];

                    if (Handle < Gatt->Cursor || Handle > Service->EndHandle)
                    {
                        Done = TRUE;
                        break;
                    }
                    Characteristic = BthGattGrow((PVOID *)&Gatt->Characteristics, &Gatt->CharacteristicCapacity,
                                                 Gatt->CharacteristicCount, sizeof(*Characteristic));
                    if (!Characteristic)
                    {
                        Done = TRUE;
                        break;
                    }
                    RtlZeroMemory(Characteristic, sizeof(*Characteristic));
                    Characteristic->Info.ServiceHandle = Service->Info.AttributeHandle;
                    Characteristic->Info.AttributeHandle = Handle;
                    Characteristic->Info.CharacteristicValueHandle = BthGattGet16(Data + Offset + 3);
                    BthGattReadUuid(Data + Offset + 5, Entry - 5, &Characteristic->Info.CharacteristicUuid);
                    Characteristic->Info.IsBroadcastable = (Properties & GATT_PROP_BROADCAST) != 0;
                    Characteristic->Info.IsReadable = (Properties & GATT_PROP_READ) != 0;
                    Characteristic->Info.IsWritableWithoutResponse = (Properties & GATT_PROP_WRITE_WITHOUT_RESPONSE) != 0;
                    Characteristic->Info.IsWritable = (Properties & GATT_PROP_WRITE) != 0;
                    Characteristic->Info.IsNotifiable = (Properties & GATT_PROP_NOTIFY) != 0;
                    Characteristic->Info.IsIndicatable = (Properties & GATT_PROP_INDICATE) != 0;
                    Characteristic->Info.IsSignedWritable = (Properties & GATT_PROP_SIGNED_WRITE) != 0;
                    Characteristic->Info.HasExtendedProperties = (Properties & GATT_PROP_EXTENDED) != 0;
                    Gatt->CharacteristicCount++;
                    Last = Handle;
                }
                if (!Done && Last && Last < Service->EndHandle)
                    Gatt->Cursor = Last + 1;
                else
                    Done = TRUE;
            }
            else
            {
                Done = TRUE;
            }
            if (Done)
            {
                Gatt->Index++;
                Gatt->Cursor = 0;
            }
            break;

        case GattDescriptors:
            if (!Done && Data[0] == ATT_FIND_INFORMATION_RESPONSE && Length >= 2 && (Data[1] == 1 || Data[1] == 2))
            {
                PBTHMINI_GATT_CHARACTERISTIC Characteristic = &Gatt->Characteristics[Gatt->Index];

                Entry = Data[1] == 1 ? 4 : 18;
                for (Offset = 2; Offset + Entry <= Length; Offset += Entry)
                {
                    PBTHMINI_GATT_DESCRIPTOR Descriptor;
                    USHORT Handle = BthGattGet16(Data + Offset);

                    if (Handle < Gatt->Cursor || Handle > Characteristic->EndHandle)
                    {
                        Done = TRUE;
                        break;
                    }
                    Descriptor = BthGattGrow((PVOID *)&Gatt->Descriptors, &Gatt->DescriptorCapacity,
                                             Gatt->DescriptorCount, sizeof(*Descriptor));
                    if (!Descriptor)
                    {
                        Done = TRUE;
                        break;
                    }
                    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
                    Descriptor->CharacteristicHandle = Characteristic->Info.AttributeHandle;
                    Descriptor->AttributeHandle = Handle;
                    BthGattReadUuid(Data + Offset + 2, Entry - 2, &Descriptor->DescriptorUuid);
                    Gatt->DescriptorCount++;
                    Last = Handle;
                }
                if (!Done && Last && Last < Characteristic->EndHandle)
                    Gatt->Cursor = Last + 1;
                else
                    Done = TRUE;
            }
            else
            {
                Done = TRUE;
            }
            if (Done)
            {
                Gatt->Index++;
                Gatt->Cursor = 0;
            }
            break;

        case GattPnpId:
            if (Data[0] == ATT_READ_RESPONSE && Length >= 8)
            {
                Gatt->VendorIdSource = Data[1];
                Gatt->VendorId = BthGattGet16(Data + 2);
                Gatt->ProductId = BthGattGet16(Data + 4);
                Gatt->Version = BthGattGet16(Data + 6);
            }
            Gatt->HasPnpId = Data[0] == ATT_READ_RESPONSE && Length >= 8;
            if (!Gatt->HasPnpId)
            {
                BthGattFinishDiscovery(Dev, Remote);
                return;
            }
            break;

        default:
            break;
    }
    BthGattDiscoverStep(Dev, Remote);
}

static
VOID
BthGattStartOp(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ PBTHMINI_ATT_OP Op)
{
    UCHAR Pdu[3 + GATT_MAX_VALUE];
    PBTHMINI_GATT Gatt = Remote->Gatt;

    switch (Op->Type)
    {
        case AttOpRead:
            Pdu[0] = Op->Length ? ATT_READ_BLOB_REQUEST : ATT_READ_REQUEST;
            BthGattPut16(Pdu + 1, Op->Handle);
            if (Op->Length)
            {
                BthGattPut16(Pdu + 3, (USHORT)Op->Length);
                BthGattRequest(Dev, Remote, Pdu, 5);
            }
            else
            {
                BthGattRequest(Dev, Remote, Pdu, 3);
            }
            return;

        case AttOpExecute:
            Pdu[0] = ATT_EXECUTE_WRITE_REQUEST;
            Pdu[1] = Op->Data[0] ? 0x01 : 0x00;
            BthGattRequest(Dev, Remote, Pdu, 2);
            return;

        case AttOpWrite:
            if (Op->Executing)
            {
                Pdu[0] = ATT_EXECUTE_WRITE_REQUEST;
                Pdu[1] = NT_SUCCESS(Op->DeferredStatus) ? 0x01 : 0x00;
                BthGattRequest(Dev, Remote, Pdu, 2);
                return;
            }
            if (Op->Prepare || Op->Length > (ULONG)Gatt->Mtu - 3)
            {
                ULONG Chunk = min(Op->Length - Op->Offset, (ULONG)Gatt->Mtu - 5);

                Pdu[0] = ATT_PREPARE_WRITE_REQUEST;
                BthGattPut16(Pdu + 1, Op->Handle);
                BthGattPut16(Pdu + 3, Op->Offset);
                RtlCopyMemory(Pdu + 5, Op->Data + Op->Offset, Chunk);
                BthGattRequest(Dev, Remote, Pdu, 5 + Chunk);
                return;
            }
            Pdu[0] = ATT_WRITE_REQUEST;
            BthGattPut16(Pdu + 1, Op->Handle);
            RtlCopyMemory(Pdu + 3, Op->Data, Op->Length);
            BthGattRequest(Dev, Remote, Pdu, 3 + Op->Length);
            return;

        case AttOpWriteCommand:
            if (Op->Length > (ULONG)Gatt->Mtu - 3)
            {
                Gatt->Current = NULL;
                BthGattCompleteOp(Op, STATUS_INVALID_BUFFER_SIZE);
                return;
            }
            Pdu[0] = ATT_WRITE_COMMAND;
            BthGattPut16(Pdu + 1, Op->Handle);
            RtlCopyMemory(Pdu + 3, Op->Data, Op->Length);
            Gatt->Current = NULL;
            BthGattCompleteOp(Op, BthGattSend(Dev, Remote, Pdu, 3 + Op->Length));
            return;

        default:
            Gatt->Current = NULL;
            BthGattCompleteOp(Op, STATUS_INVALID_PARAMETER);
            return;
    }
}

static
VOID
BthGattOpResponse(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    PBTHMINI_ATT_OP Op = Gatt->Current;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Op)
        return;
    if (Data[0] == ATT_ERROR_RESPONSE)
    {
        if (Op->Type == AttOpRead && Op->Length && Length >= 5 &&
            (Data[4] == ATT_ERROR_ATTRIBUTE_NOT_LONG || Data[4] == ATT_ERROR_INVALID_OFFSET))
        {
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = Length >= 5 ? BthGattStatus(Data[4]) : STATUS_UNSUCCESSFUL;
            Op->AttError = Length >= 5 ? Data[4] : ATT_ERROR_INVALID_PDU;
            if (Op->Type == AttOpWrite && !Op->Prepare && Op->Offset && !Op->Executing)
            {
                Op->Executing = TRUE;
                Op->DeferredStatus = Status;
                BthGattStartOp(Dev, Remote, Op);
                return;
            }
        }
    }
    else if (Op->Type == AttOpWrite && Data[0] == ATT_PREPARE_WRITE_RESPONSE)
    {
        ULONG Chunk = min(Op->Length - Op->Offset, (ULONG)Gatt->Mtu - 5);

        if (Length != 5 + Chunk || BthGattGet16(Data + 1) != Op->Handle || BthGattGet16(Data + 3) != Op->Offset ||
            !RtlEqualMemory(Data + 5, Op->Data + Op->Offset, Chunk))
        {
            Op->Executing = TRUE;
            Op->DeferredStatus = STATUS_DATA_ERROR;
            BthGattStartOp(Dev, Remote, Op);
            return;
        }
        Op->Offset += (USHORT)Chunk;
        if (Op->Offset < Op->Length)
        {
            BthGattStartOp(Dev, Remote, Op);
            return;
        }
        if (!Op->Prepare)
        {
            Op->Executing = TRUE;
            Op->DeferredStatus = STATUS_SUCCESS;
            BthGattStartOp(Dev, Remote, Op);
            return;
        }
    }
    else if (Op->Type == AttOpWrite && Data[0] == ATT_EXECUTE_WRITE_RESPONSE)
    {
        Status = Op->DeferredStatus;
    }
    else if (Op->Type == AttOpRead)
    {
        ULONG Chunk = min(Length - 1, Op->Capacity - Op->Length);

        RtlCopyMemory(Op->Data + Op->Length, Data + 1, Chunk);
        Op->Length += Chunk;
        if (Length - 1 == (ULONG)Gatt->Mtu - 1 && Op->Length < Op->Capacity)
        {
            BthGattStartOp(Dev, Remote, Op);
            return;
        }
    }
    if (NT_SUCCESS(Status) && Op->Type == AttOpRead)
        BthGattStoreValue(Gatt, Op->Handle, Op->Data, Op->Length);
    Gatt->Current = NULL;
    BthGattCompleteOp(Op, Status);
}

static
VOID
BthGattKick(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    if (!Gatt || !Gatt->Ready || Gatt->PendingOpcode || Gatt->Current)
        return;
    if (!Gatt->MtuExchanged)
    {
        UCHAR Pdu[3];

        Gatt->MtuExchanged = TRUE;
        Pdu[0] = ATT_EXCHANGE_MTU_REQUEST;
        BthGattPut16(Pdu + 1, GATT_LOCAL_MTU);
        BthGattRequest(Dev, Remote, Pdu, sizeof(Pdu));
        return;
    }
    if (Gatt->Phase == GattIdle && (!Gatt->Discovered || Gatt->Rediscover) &&
        (Remote->HasLeKey || !IsListEmpty(&Gatt->Operations)))
    {
        BthGattClearDatabase(Gatt);
        Gatt->Phase = GattServices;
        Gatt->Cursor = 1;
        Gatt->Index = 0;
    }
    if (Gatt->Phase != GattIdle)
    {
        BthGattDiscoverStep(Dev, Remote);
        if (Gatt->PendingOpcode || Gatt->Phase != GattIdle)
            return;
    }
    while (!Gatt->PendingOpcode && !Gatt->Current && !IsListEmpty(&Gatt->Operations))
    {
        PBTHMINI_ATT_OP Op = CONTAINING_RECORD(RemoveHeadList(&Gatt->Operations), BTHMINI_ATT_OP, Link);

        if (Op->Type == AttOpDatabase)
        {
            BthGattControl(Dev, Op->Request);
            Op->Request = NULL;
            BthGattCompleteOp(Op, STATUS_SUCCESS);
            continue;
        }
        Gatt->Current = Op;
        BthGattStartOp(Dev, Remote, Op);
    }
}

static
NTSTATUS
BthGattQueue(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ BTHMINI_ATT_OP_TYPE Type,
    _In_opt_ WDFREQUEST Request,
    _In_ USHORT Handle,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_GATT Gatt = BthGattGet(Remote);
    PBTHMINI_ATT_OP Op;
    NTSTATUS Status;

    if (!Gatt)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Length > GATT_MAX_VALUE)
        return STATUS_INVALID_BUFFER_SIZE;
    Op = ExAllocatePoolZero(NonPagedPool, sizeof(*Op), BTHMINI_TAG);
    if (!Op)
        return STATUS_INSUFFICIENT_RESOURCES;
    Op->Type = Type == AttOpPrepareWrite ? AttOpWrite : Type;
    Op->Prepare = Type == AttOpPrepareWrite;
    Op->Request = Request;
    Op->Handle = Handle;
    Op->Deadline = BthGattNowMs() + GATT_TIMEOUT_MS;
    if (Type == AttOpRead)
    {
        Op->Capacity = Length ? Length : GATT_MAX_VALUE;
    }
    else if (Data)
    {
        RtlCopyMemory(Op->Data, Data, Length);
        Op->Length = Length;
    }
    InsertTailList(&Gatt->Operations, &Op->Link);
    if (Gatt->Ready)
    {
        BthGattKick(Dev, Remote);
        return STATUS_PENDING;
    }
    Status = BthLeConnect(Dev, Remote);
    if (!NT_SUCCESS(Status) && Status != STATUS_DEVICE_BUSY)
    {
        RemoveEntryList(&Op->Link);
        ExFreePoolWithTag(Op, BTHMINI_TAG);
        return Status;
    }
    return STATUS_PENDING;
}

static
BOOLEAN
BthGattFillNotification(
    _In_ PBTHMINI_GATT Gatt,
    _Inout_ PBTHLE_GATT_NOTIFICATION Notification)
{
    ULONG Oldest = Gatt->EventSequence >= GATT_EVENT_RING ? Gatt->EventSequence - GATT_EVENT_RING + 1 : 1;
    ULONG Next = Notification->Sequence + 1;
    PBTHMINI_GATT_EVENT Event;

    Notification->Lost = FALSE;
    if (!Notification->Sequence || Notification->Sequence > Gatt->EventSequence)
    {
        Notification->Sequence = Gatt->EventSequence;
        Notification->Handle = 0;
        Notification->Size = 0;
        return TRUE;
    }
    if (Notification->Sequence == Gatt->EventSequence)
        return FALSE;
    if (Next < Oldest)
    {
        Next = Oldest;
        Notification->Lost = TRUE;
    }
    Event = &Gatt->Events[Next % GATT_EVENT_RING];
    Notification->Sequence = Next;
    Notification->Handle = Event->Handle;
    Notification->Size = Event->Length;
    RtlCopyMemory(Notification->Data, Event->Data, Event->Length);
    return TRUE;
}

static
VOID
BthGattFlushWaiters(
    _In_ PBTHMINI_GATT Gatt,
    _In_ NTSTATUS Status)
{
    WDFREQUEST Request;

    if (!Gatt->Waiters)
        return;
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Gatt->Waiters, &Request)))
    {
        PBTHLE_GATT_NOTIFICATION Notification;

        if (NT_SUCCESS(Status) &&
            NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(*Notification), (PVOID *)&Notification, NULL)) &&
            BthGattFillNotification(Gatt, Notification))
        {
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(*Notification));
            continue;
        }
        WdfRequestComplete(Request, NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status);
    }
}

static
VOID
BthGattDeleteWaiters(
    _In_ PBTHMINI_GATT Gatt)
{
    BthGattFlushWaiters(Gatt, STATUS_DEVICE_NOT_CONNECTED);
    if (Gatt->Waiters)
        WdfObjectDelete(Gatt->Waiters);
    Gatt->Waiters = NULL;
    if (Gatt->Events)
        ExFreePoolWithTag(Gatt->Events, BTHMINI_TAG);
    Gatt->Events = NULL;
}

static
VOID
BthGattValue(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Handle,
    _In_reads_bytes_(Length) const UCHAR *Value,
    _In_ ULONG Length)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    PBTHMINI_GATT_CHARACTERISTIC Characteristic;
    ULONG Index;

    if (!Gatt)
        return;
    BthGattStoreValue(Gatt, Handle, Value, Length);
    if (Gatt->Events)
    {
        PBTHMINI_GATT_EVENT Event = &Gatt->Events[++Gatt->EventSequence % GATT_EVENT_RING];

        Event->Handle = Handle;
        Event->Length = (USHORT)min(Length, GATT_MAX_VALUE);
        RtlCopyMemory(Event->Data, Value, Event->Length);
        BthGattFlushWaiters(Gatt, STATUS_SUCCESS);
    }
    for (Index = 0; Index < GATT_MAX_SUBSCRIPTIONS; Index++)
    {
        PBTHMINI_GATT_SUBSCRIPTION Subscription = &Gatt->Subscriptions[Index];

        if (Subscription->Callback && Subscription->Handle == Handle)
            Subscription->Callback(Subscription->Context, Handle, Value, Length);
    }
    Characteristic = BthGattFindValueHandle(Gatt, Handle);
    if (Characteristic && BthGattIsShort(&Characteristic->Info.CharacteristicUuid, GATT_UUID_SERVICE_CHANGED))
    {
        DPRINT1("BTHMINI: GATT database of %012I64x changed\n", Remote->Address);
        Gatt->Rediscover = TRUE;
        BthGattKick(Dev, Remote);
    }
}

static
VOID
BthGattServerError(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ UCHAR Opcode,
    _In_ USHORT Handle,
    _In_ UCHAR Error)
{
    UCHAR Pdu[5];

    Pdu[0] = ATT_ERROR_RESPONSE;
    Pdu[1] = Opcode;
    BthGattPut16(Pdu + 2, Handle);
    Pdu[4] = Error;
    BthGattSend(Dev, Remote, Pdu, sizeof(Pdu));
}

static
ULONG
BthGattLocalAttribute(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT Handle,
    _Out_ PUSHORT Type,
    _Out_writes_bytes_(GATT_MAX_VALUE) PUCHAR Value,
    _Out_ PBOOLEAN Readable)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    *Readable = TRUE;
    switch (Handle)
    {
        case 0x0001:
            *Type = GATT_UUID_PRIMARY_SERVICE;
            BthGattPut16(Value, GATT_SERVICE_GAP);
            return 2;
        case 0x0002:
            *Type = GATT_UUID_CHARACTERISTIC;
            Value[0] = GATT_PROP_READ;
            BthGattPut16(Value + 1, 0x0003);
            BthGattPut16(Value + 3, GATT_UUID_DEVICE_NAME);
            return 5;
        case 0x0003:
            *Type = GATT_UUID_DEVICE_NAME;
            RtlCopyMemory(Value, Dev->Name, strnlen(Dev->Name, BTH_MAX_NAME_SIZE));
            return (ULONG)strnlen(Dev->Name, BTH_MAX_NAME_SIZE);
        case 0x0004:
            *Type = GATT_UUID_CHARACTERISTIC;
            Value[0] = GATT_PROP_READ;
            BthGattPut16(Value + 1, 0x0005);
            BthGattPut16(Value + 3, GATT_UUID_APPEARANCE);
            return 5;
        case 0x0005:
            *Type = GATT_UUID_APPEARANCE;
            BthGattPut16(Value, GATT_APPEARANCE_COMPUTER);
            return 2;
        case 0x0006:
            *Type = GATT_UUID_PRIMARY_SERVICE;
            BthGattPut16(Value, GATT_SERVICE_GATT);
            return 2;
        case 0x0007:
            *Type = GATT_UUID_CHARACTERISTIC;
            Value[0] = GATT_PROP_INDICATE;
            BthGattPut16(Value + 1, 0x0008);
            BthGattPut16(Value + 3, GATT_UUID_SERVICE_CHANGED);
            return 5;
        case 0x0008:
            *Type = GATT_UUID_SERVICE_CHANGED;
            *Readable = FALSE;
            return 0;
        case GATT_LOCAL_SERVICE_CHANGED_CCCD:
            *Type = GATT_UUID_CCCD;
            BthGattPut16(Value, Gatt ? Gatt->ServiceChangedCccd : 0);
            return 2;
        default:
            *Type = 0;
            *Readable = FALSE;
            return 0;
    }
}

static
USHORT
BthGattLocalGroupEnd(
    _In_ USHORT Handle)
{
    return Handle == 0x0001 ? 0x0005 : GATT_LOCAL_LAST_HANDLE;
}

static
VOID
BthGattServer(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_GATT Gatt = BthGattGet(Remote);
    UCHAR Response[GATT_LOCAL_MTU], Value[GATT_MAX_VALUE];
    ULONG Mtu = Gatt ? Gatt->Mtu : GATT_DEFAULT_MTU, Used, ValueLength;
    USHORT Start, End, Handle, Type;
    BOOLEAN Readable;

    switch (Data[0])
    {
        case ATT_EXCHANGE_MTU_REQUEST:
            if (Length < 3)
                break;
            if (Gatt)
                Gatt->Mtu = (USHORT)max(GATT_DEFAULT_MTU, min(BthGattGet16(Data + 1), GATT_LOCAL_MTU));
            Response[0] = ATT_EXCHANGE_MTU_RESPONSE;
            BthGattPut16(Response + 1, GATT_LOCAL_MTU);
            BthGattSend(Dev, Remote, Response, 3);
            return;

        case ATT_FIND_INFORMATION_REQUEST:
            if (Length < 5)
                break;
            Start = BthGattGet16(Data + 1);
            End = BthGattGet16(Data + 3);
            if (!Start || Start > End)
            {
                BthGattServerError(Dev, Remote, Data[0], Start, ATT_ERROR_INVALID_HANDLE);
                return;
            }
            Response[0] = ATT_FIND_INFORMATION_RESPONSE;
            Response[1] = 1;
            Used = 2;
            for (Handle = Start; Handle <= min(End, GATT_LOCAL_LAST_HANDLE) && Used + 4 <= Mtu; Handle++)
            {
                BthGattLocalAttribute(Dev, Remote, Handle, &Type, Value, &Readable);
                BthGattPut16(Response + Used, Handle);
                BthGattPut16(Response + Used + 2, Type);
                Used += 4;
            }
            if (Used == 2)
                BthGattServerError(Dev, Remote, Data[0], Start, ATT_ERROR_ATTRIBUTE_NOT_FOUND);
            else
                BthGattSend(Dev, Remote, Response, Used);
            return;

        case ATT_FIND_BY_TYPE_VALUE_REQUEST:
            if (Length < 7)
                break;
            Start = BthGattGet16(Data + 1);
            End = BthGattGet16(Data + 3);
            if (!Start || Start > End)
            {
                BthGattServerError(Dev, Remote, Data[0], Start, ATT_ERROR_INVALID_HANDLE);
                return;
            }
            Response[0] = ATT_FIND_BY_TYPE_VALUE_RESPONSE;
            Used = 1;
            for (Handle = Start; Handle <= min(End, GATT_LOCAL_LAST_HANDLE) && Used + 4 <= Mtu; Handle++)
            {
                ValueLength = BthGattLocalAttribute(Dev, Remote, Handle, &Type, Value, &Readable);
                if (Type != BthGattGet16(Data + 5) || ValueLength != Length - 7 ||
                    !RtlEqualMemory(Value, Data + 7, ValueLength))
                    continue;
                BthGattPut16(Response + Used, Handle);
                BthGattPut16(Response + Used + 2, Type == GATT_UUID_PRIMARY_SERVICE ? BthGattLocalGroupEnd(Handle) : Handle);
                Used += 4;
            }
            if (Used == 1)
                BthGattServerError(Dev, Remote, Data[0], Start, ATT_ERROR_ATTRIBUTE_NOT_FOUND);
            else
                BthGattSend(Dev, Remote, Response, Used);
            return;

        case ATT_READ_BY_TYPE_REQUEST:
        case ATT_READ_BY_GROUP_TYPE_REQUEST:
        {
            BTH_LE_UUID Wanted;
            ULONG Entry = 0;

            if ((Length != 7 && Length != 21) || !BthGattReadUuid(Data + 5, Length - 5, &Wanted))
                break;
            Start = BthGattGet16(Data + 1);
            End = BthGattGet16(Data + 3);
            if (!Start || Start > End)
            {
                BthGattServerError(Dev, Remote, Data[0], Start, ATT_ERROR_INVALID_HANDLE);
                return;
            }
            if (Data[0] == ATT_READ_BY_GROUP_TYPE_REQUEST && !BthGattIsShort(&Wanted, GATT_UUID_PRIMARY_SERVICE) &&
                !BthGattIsShort(&Wanted, GATT_UUID_SECONDARY_SERVICE))
            {
                BthGattServerError(Dev, Remote, Data[0], Start, ATT_ERROR_UNSUPPORTED_GROUP_TYPE);
                return;
            }
            Response[0] = Data[0] + 1;
            Used = 2;
            for (Handle = Start; Handle <= min(End, GATT_LOCAL_LAST_HANDLE); Handle++)
            {
                ULONG Header = Data[0] == ATT_READ_BY_GROUP_TYPE_REQUEST ? 4 : 2;

                ValueLength = BthGattLocalAttribute(Dev, Remote, Handle, &Type, Value, &Readable);
                if (!Type || !BthGattIsShort(&Wanted, Type))
                    continue;
                if (!Readable)
                {
                    if (Used == 2)
                    {
                        BthGattServerError(Dev, Remote, Data[0], Handle, ATT_ERROR_READ_NOT_PERMITTED);
                        return;
                    }
                    break;
                }
                ValueLength = min(ValueLength, min(Mtu - 2 - Header, 255 - Header));
                if (Entry && Entry != Header + ValueLength)
                    break;
                if (Used + Header + ValueLength > Mtu)
                    break;
                Entry = Header + ValueLength;
                BthGattPut16(Response + Used, Handle);
                if (Header == 4)
                    BthGattPut16(Response + Used + 2, BthGattLocalGroupEnd(Handle));
                RtlCopyMemory(Response + Used + Header, Value, ValueLength);
                Used += Entry;
            }
            if (Used == 2)
            {
                BthGattServerError(Dev, Remote, Data[0], Start, ATT_ERROR_ATTRIBUTE_NOT_FOUND);
                return;
            }
            Response[1] = (UCHAR)Entry;
            BthGattSend(Dev, Remote, Response, Used);
            return;
        }

        case ATT_READ_REQUEST:
        case ATT_READ_BLOB_REQUEST:
        {
            USHORT Offset = 0;

            if (Length < 3 || (Data[0] == ATT_READ_BLOB_REQUEST && Length < 5))
                break;
            Handle = BthGattGet16(Data + 1);
            if (Data[0] == ATT_READ_BLOB_REQUEST)
                Offset = BthGattGet16(Data + 3);
            ValueLength = BthGattLocalAttribute(Dev, Remote, Handle, &Type, Value, &Readable);
            if (!Type)
            {
                BthGattServerError(Dev, Remote, Data[0], Handle, ATT_ERROR_INVALID_HANDLE);
                return;
            }
            if (!Readable)
            {
                BthGattServerError(Dev, Remote, Data[0], Handle, ATT_ERROR_READ_NOT_PERMITTED);
                return;
            }
            if (Offset > ValueLength)
            {
                BthGattServerError(Dev, Remote, Data[0], Handle, ATT_ERROR_INVALID_OFFSET);
                return;
            }
            Response[0] = Data[0] + 1;
            Used = min(ValueLength - Offset, Mtu - 1);
            RtlCopyMemory(Response + 1, Value + Offset, Used);
            BthGattSend(Dev, Remote, Response, 1 + Used);
            return;
        }

        case ATT_WRITE_REQUEST:
        case ATT_WRITE_COMMAND:
            if (Length < 3)
                break;
            Handle = BthGattGet16(Data + 1);
            if (Handle == GATT_LOCAL_SERVICE_CHANGED_CCCD && Length == 5 && Gatt)
            {
                Gatt->ServiceChangedCccd = BthGattGet16(Data + 3) & GATT_CCCD_INDICATE;
                if (Data[0] == ATT_WRITE_REQUEST)
                {
                    Response[0] = ATT_WRITE_RESPONSE;
                    BthGattSend(Dev, Remote, Response, 1);
                }
                return;
            }
            if (Data[0] == ATT_WRITE_REQUEST)
                BthGattServerError(Dev, Remote, Data[0], Handle,
                                   Handle && Handle <= GATT_LOCAL_LAST_HANDLE ? ATT_ERROR_WRITE_NOT_PERMITTED :
                                                                               ATT_ERROR_INVALID_HANDLE);
            return;

        default:
            if (!(Data[0] & ATT_COMMAND_FLAG))
                BthGattServerError(Dev, Remote, Data[0], 0, ATT_ERROR_REQUEST_NOT_SUPPORTED);
            return;
    }
    BthGattServerError(Dev, Remote, Data[0], 0, ATT_ERROR_INVALID_PDU);
}

VOID
BthGattReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    UCHAR Opcode;

    if (!Length)
        return;
    Opcode = Data[0];
    if (Opcode == ATT_HANDLE_VALUE_NOTIFICATION || Opcode == ATT_HANDLE_VALUE_INDICATION)
    {
        if (Length >= 3)
            BthGattValue(Dev, Remote, BthGattGet16(Data + 1), Data + 3, Length - 3);
        if (Opcode == ATT_HANDLE_VALUE_INDICATION)
        {
            UCHAR Confirmation = ATT_HANDLE_VALUE_CONFIRMATION;

            BthGattSend(Dev, Remote, &Confirmation, 1);
        }
        return;
    }
    if (Opcode == ATT_MULTIPLE_HANDLE_VALUE_NOTIFICATION)
    {
        ULONG Offset = 1;

        while (Offset + 4 <= Length)
        {
            USHORT Handle = BthGattGet16(Data + Offset), ValueLength = BthGattGet16(Data + Offset + 2);

            if (Offset + 4 + ValueLength > Length)
                break;
            BthGattValue(Dev, Remote, Handle, Data + Offset + 4, ValueLength);
            Offset += 4 + ValueLength;
        }
        return;
    }
    if (Opcode == ATT_HANDLE_VALUE_CONFIRMATION)
        return;
    if ((Opcode & 1) && (Opcode < ATT_HANDLE_VALUE_NOTIFICATION || Opcode == ATT_READ_MULTIPLE_VARIABLE_RESPONSE))
    {
        UCHAR Expected;

        if (!Gatt || !Gatt->PendingOpcode)
            return;
        Expected = Opcode == ATT_ERROR_RESPONSE ? (Length >= 2 ? Data[1] : 0) : (UCHAR)(Opcode - 1);
        if (Expected != Gatt->PendingOpcode)
            return;
        Gatt->PendingOpcode = 0;
        if (Expected == ATT_EXCHANGE_MTU_REQUEST)
        {
            if (Opcode == ATT_EXCHANGE_MTU_RESPONSE && Length >= 3)
                Gatt->Mtu = (USHORT)max(GATT_DEFAULT_MTU, min(BthGattGet16(Data + 1), GATT_LOCAL_MTU));
        }
        else if (Gatt->Phase != GattIdle)
        {
            BthGattDiscoveryResponse(Dev, Remote, Data, Length);
        }
        else
        {
            BthGattOpResponse(Dev, Remote, Data, Length);
        }
        BthGattKick(Dev, Remote);
        return;
    }
    BthGattServer(Dev, Remote, Data, Length);
}

VOID
BthGattLinkReady(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    if (!Gatt && Remote->HasLeKey)
        Gatt = BthGattGet(Remote);
    if (!Gatt || Gatt->Ready || !Remote->LeConnected)
        return;
    Gatt->Ready = TRUE;
    Gatt->MtuExchanged = FALSE;
    Gatt->Mtu = GATT_DEFAULT_MTU;
    Gatt->PendingOpcode = 0;
    BthGattKick(Dev, Remote);
}

VOID
BthGattLinkDown(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ NTSTATUS Status)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    UNREFERENCED_PARAMETER(Dev);

    if (!Gatt)
        return;
    Gatt->Ready = FALSE;
    Gatt->PendingOpcode = 0;
    if (Gatt->Phase != GattIdle)
    {
        Gatt->Phase = GattIdle;
        if (!Gatt->Discovered)
            BthGattClearDatabase(Gatt);
        BthGattLoadCache(Remote);
    }
    BthGattFailOperations(Gatt, NT_SUCCESS(Status) ? STATUS_DEVICE_NOT_CONNECTED : Status);
}

VOID
BthGattTick(
    _In_ PBTHMINI_DEVICE Dev)
{
    ULONGLONG Now = BthGattNowMs();
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);
        PBTHMINI_GATT Gatt = Remote->Gatt;

        if (!Gatt)
            continue;
        if (Gatt->PendingOpcode && Now >= Gatt->Deadline)
        {
            DPRINT1("BTHMINI: ATT request 0x%02x to %012I64x timed out\n", Gatt->PendingOpcode, Remote->Address);
            BthLeDisconnect(Dev, Remote);
            BthGattLinkDown(Dev, Remote, STATUS_IO_TIMEOUT);
            continue;
        }
        if (!Gatt->Ready && !IsListEmpty(&Gatt->Operations))
        {
            PBTHMINI_ATT_OP Op = CONTAINING_RECORD(Gatt->Operations.Flink, BTHMINI_ATT_OP, Link);

            if (Now >= Op->Deadline)
                BthGattFailOperations(Gatt, STATUS_IO_TIMEOUT);
            else if (!Remote->LeConnected && !Remote->LeConnecting)
                BthLeConnect(Dev, Remote);
        }
    }
}

static
VOID
BthGattFillDeviceInfo(
    _In_ PBTHMINI_REMOTE Remote,
    _Out_ PBTHMINI_GATT_DEVICE_INFO Info)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    RtlZeroMemory(Info, sizeof(*Info));
    Info->Address = Remote->Address;
    Info->HasPnpId = Gatt->HasPnpId;
    Info->VendorIdSource = Gatt->VendorIdSource;
    Info->VendorId = Gatt->VendorId;
    Info->ProductId = Gatt->ProductId;
    Info->Version = Gatt->Version;
    RtlCopyMemory(Info->Name, Remote->Name, sizeof(Info->Name));
}

static
NTSTATUS
BthGattInternal(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT ServiceHandle,
    _In_ WDFREQUEST Request,
    _Inout_ PBTHMINI_GATT_REQUEST Gatt)
{
    PBTHMINI_GATT State = Remote->Gatt;
    PBTHMINI_GATT_SERVICE Service = BthGattFindService(State, ServiceHandle);
    PBTHMINI_GATT_CHARACTERISTIC Characteristic;
    ULONG Index, Count = 0;

    if (!Service)
        return STATUS_DEVICE_NOT_CONNECTED;
    switch (Gatt->Operation)
    {
        case BthMiniGattGetDeviceInfo:
            if (!Gatt->Buffer || Gatt->Length < sizeof(BTHMINI_GATT_DEVICE_INFO))
                return STATUS_BUFFER_TOO_SMALL;
            BthGattFillDeviceInfo(Remote, Gatt->Buffer);
            Gatt->Length = sizeof(BTHMINI_GATT_DEVICE_INFO);
            return STATUS_SUCCESS;

        case BthMiniGattGetCharacteristics:
            for (Index = 0; Index < State->CharacteristicCount; Index++)
            {
                if (State->Characteristics[Index].Info.ServiceHandle != ServiceHandle)
                    continue;
                if (Gatt->Buffer && Count < Gatt->Length)
                    ((PBTH_LE_GATT_CHARACTERISTIC)Gatt->Buffer)[Count] = State->Characteristics[Index].Info;
                Count++;
            }
            Index = Gatt->Length;
            Gatt->Length = Count;
            return Count > Index || (!Gatt->Buffer && Count) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;

        case BthMiniGattGetDescriptors:
            for (Index = 0; Index < State->DescriptorCount; Index++)
            {
                PBTHMINI_GATT_DESCRIPTOR Descriptor = &State->Descriptors[Index];

                if (Descriptor->AttributeHandle < Service->Info.AttributeHandle ||
                    Descriptor->AttributeHandle > Service->EndHandle ||
                    (Gatt->Handle && Descriptor->CharacteristicHandle != Gatt->Handle))
                    continue;
                if (Gatt->Buffer && Count < Gatt->Length)
                    ((PBTHMINI_GATT_DESCRIPTOR)Gatt->Buffer)[Count] = *Descriptor;
                Count++;
            }
            Index = Gatt->Length;
            Gatt->Length = Count;
            return Count > Index || (!Gatt->Buffer && Count) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;

        case BthMiniGattRead:
            if (Gatt->Handle < Service->Info.AttributeHandle || Gatt->Handle > Service->EndHandle)
                return STATUS_INVALID_PARAMETER;
            if (!Gatt->Buffer || !Gatt->Length)
                return STATUS_INVALID_PARAMETER;
            return BthGattQueue(Dev, Remote, AttOpRead, Request, Gatt->Handle, NULL, min(Gatt->Length, GATT_MAX_VALUE));

        case BthMiniGattWrite:
        case BthMiniGattWriteWithoutResponse:
            if (Gatt->Handle < Service->Info.AttributeHandle || Gatt->Handle > Service->EndHandle)
                return STATUS_INVALID_PARAMETER;
            if (Gatt->Length && !Gatt->Buffer)
                return STATUS_INVALID_PARAMETER;
            return BthGattQueue(Dev, Remote, Gatt->Operation == BthMiniGattWrite ? AttOpWrite : AttOpWriteCommand,
                                Request, Gatt->Handle, Gatt->Buffer, Gatt->Length);

        case BthMiniGattSubscribe:
        {
            PBTHMINI_GATT_SUBSCRIPTION Free = NULL;
            USHORT Cccd = 0;
            UCHAR Value[2];

            Characteristic = BthGattFindValueHandle(State, Gatt->Handle);
            if (!Characteristic || Characteristic->Info.ServiceHandle != ServiceHandle || !Gatt->Notify ||
                (!Characteristic->Info.IsNotifiable && !Characteristic->Info.IsIndicatable))
                return STATUS_INVALID_PARAMETER;
            for (Index = 0; Index < State->DescriptorCount; Index++)
            {
                if (State->Descriptors[Index].CharacteristicHandle == Characteristic->Info.AttributeHandle &&
                    BthGattIsShort(&State->Descriptors[Index].DescriptorUuid, GATT_UUID_CCCD))
                {
                    Cccd = State->Descriptors[Index].AttributeHandle;
                    break;
                }
            }
            for (Index = 0; Index < GATT_MAX_SUBSCRIPTIONS; Index++)
            {
                PBTHMINI_GATT_SUBSCRIPTION Subscription = &State->Subscriptions[Index];

                if (Subscription->Callback && Subscription->Handle == Gatt->Handle &&
                    Subscription->Context == Gatt->NotifyContext)
                {
                    Free = Subscription;
                    break;
                }
                if (!Subscription->Callback && !Free)
                    Free = Subscription;
            }
            if (!Free)
                return STATUS_INSUFFICIENT_RESOURCES;
            Free->Handle = Gatt->Handle;
            Free->Service = ServiceHandle;
            Free->Callback = Gatt->Notify;
            Free->Context = Gatt->NotifyContext;
            if (!Cccd)
                return STATUS_SUCCESS;
            BthGattPut16(Value, Characteristic->Info.IsNotifiable ? GATT_CCCD_NOTIFY : GATT_CCCD_INDICATE);
            return BthGattQueue(Dev, Remote, AttOpWrite, Request, Cccd, Value, sizeof(Value));
        }

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static
NTSTATUS
BthGattUserControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote,
    _In_ USHORT ServiceHandle,
    _In_ WDFREQUEST Request,
    _In_ ULONG IoControlCode,
    _Out_ PULONG_PTR Information)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    ULONG Index, Count = 0, Room;
    size_t OutputLength;
    NTSTATUS Status;

    *Information = 0;
    switch (IoControlCode)
    {
        case IOCTL_WINEBTH_LE_DEVICE_GET_GATT_SERVICES:
        {
            BTHMINI_GATT_SERVICES *Params;

            if (ServiceHandle)
                return STATUS_INVALID_DEVICE_REQUEST;
            Status = WdfRequestRetrieveOutputBuffer(Request, FIELD_OFFSET(BTHMINI_GATT_SERVICES, Services),
                                                    (PVOID *)&Params, &OutputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            Room = (ULONG)((OutputLength - FIELD_OFFSET(BTHMINI_GATT_SERVICES, Services)) /
                           sizeof(BTH_LE_GATT_SERVICE));
            for (Index = 0; Index < Gatt->ServiceCount; Index++)
            {
                if (Gatt->Services[Index].Secondary)
                    continue;
                if (Count < Room)
                    Params->Services[Count] = Gatt->Services[Index].Info;
                Count++;
            }
            Params->Count = Count;
            *Information = FIELD_OFFSET(BTHMINI_GATT_SERVICES, Services[min(Count, Room)]);
            return Count > Room ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;
        }

        case IOCTL_WINEBTH_LE_DEVICE_GET_GATT_CHARACTERISTICS:
        {
            BTHMINI_GATT_CHARACTERISTICS *Params;
            PBTHMINI_GATT_SERVICE Service;

            Status = WdfRequestRetrieveOutputBuffer(Request,
                                                    FIELD_OFFSET(BTHMINI_GATT_CHARACTERISTICS,
                                                                 Characteristics),
                                                    (PVOID *)&Params, &OutputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            if (ServiceHandle)
            {
                Service = BthGattFindService(Gatt, ServiceHandle);
            }
            else
            {
                Service = BthGattFindService(Gatt, Params->Service.AttributeHandle);
                if (Service && !IsBthLEUuidMatch(Service->Info.ServiceUuid, Params->Service.ServiceUuid))
                    Service = NULL;
            }
            if (!Service)
                return STATUS_INVALID_PARAMETER;
            Room = (ULONG)((OutputLength - FIELD_OFFSET(BTHMINI_GATT_CHARACTERISTICS,
                                                        Characteristics)) / sizeof(BTH_LE_GATT_CHARACTERISTIC));
            for (Index = 0; Index < Gatt->CharacteristicCount; Index++)
            {
                if (Gatt->Characteristics[Index].Info.ServiceHandle != Service->Info.AttributeHandle)
                    continue;
                if (Count < Room)
                    Params->Characteristics[Count] = Gatt->Characteristics[Index].Info;
                Count++;
            }
            Params->Count = Count;
            *Information = FIELD_OFFSET(BTHMINI_GATT_CHARACTERISTICS,
                                        Characteristics[min(Count, Room)]);
            return Count > Room ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;
        }

        case IOCTL_WINEBTH_GATT_SERVICE_READ_CHARACTERISITIC_VALUE:
        {
            BTHMINI_GATT_READ_VALUE *Params;
            PBTHMINI_GATT_CHARACTERISTIC Characteristic = NULL;
            PBTHMINI_GATT_VALUE Value;
            ULONG Needed;

            if (!ServiceHandle)
                return STATUS_INVALID_DEVICE_REQUEST;
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Params), (PVOID *)&Params, &OutputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            for (Index = 0; Index < Gatt->CharacteristicCount; Index++)
            {
                PBTHMINI_GATT_CHARACTERISTIC Candidate = &Gatt->Characteristics[Index];

                if (Candidate->Info.ServiceHandle == ServiceHandle && Candidate->Info.AttributeHandle == Params->Handle &&
                    IsBthLEUuidMatch(Candidate->Info.CharacteristicUuid, Params->Uuid))
                {
                    Characteristic = Candidate;
                    break;
                }
            }
            if (!Characteristic)
                return STATUS_NOT_FOUND;
            if (!Characteristic->Info.IsReadable)
                return STATUS_PRIVILEGE_NOT_HELD;
            Value = BthGattFindValue(Gatt, Characteristic->Info.CharacteristicValueHandle);
            if (Params->FromDevice || !Value)
                return BthGattQueue(Dev, Remote, AttOpRead, Request, Characteristic->Info.CharacteristicValueHandle, NULL, 0);
            Needed = FIELD_OFFSET(BTHMINI_GATT_READ_VALUE, Buffer[Value->Length]);
            Params->Size = Value->Length;
            if (OutputLength < Needed)
            {
                *Information = sizeof(*Params);
                return STATUS_MORE_ENTRIES;
            }
            RtlCopyMemory(Params->Buffer, Value->Data, Value->Length);
            *Information = Needed;
            return STATUS_SUCCESS;
        }

        case IOCTL_BTHLE_GATT_GET_INCLUDED_SERVICES:
        {
            PBTHLE_GATT_INCLUDED_SERVICES Params;
            PBTHMINI_GATT_SERVICE Parent;

            Status = WdfRequestRetrieveOutputBuffer(Request, FIELD_OFFSET(BTHLE_GATT_INCLUDED_SERVICES, Services),
                                                    (PVOID *)&Params, &OutputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            Parent = BthGattFindService(Gatt, Params->ServiceHandle ? Params->ServiceHandle : ServiceHandle);
            if (!Parent)
                return STATUS_INVALID_PARAMETER;
            if (ServiceHandle && Parent->Info.AttributeHandle != ServiceHandle &&
                !BthGattServiceIncludes(Gatt, ServiceHandle, Parent->Info.AttributeHandle, 0))
                return STATUS_ACCESS_DENIED;
            Room = (ULONG)((OutputLength - FIELD_OFFSET(BTHLE_GATT_INCLUDED_SERVICES, Services)) /
                           sizeof(BTH_LE_GATT_SERVICE));
            for (Index = 0; Index < Gatt->IncludeCount; Index++)
            {
                PBTHMINI_GATT_SERVICE Included;

                if (Gatt->Includes[Index].ServiceHandle != Parent->Info.AttributeHandle)
                    continue;
                Included = BthGattFindService(Gatt, Gatt->Includes[Index].StartHandle);
                if (!Included)
                    continue;
                if (Count < Room)
                    Params->Services[Count] = Included->Info;
                Count++;
            }
            Params->Count = Count;
            *Information = FIELD_OFFSET(BTHLE_GATT_INCLUDED_SERVICES, Services[min(Count, Room)]);
            return Count > Room ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;
        }

        case IOCTL_BTHLE_GATT_GET_DESCRIPTORS:
        {
            PBTHLE_GATT_DESCRIPTORS Params;
            PBTHMINI_GATT_CHARACTERISTIC Characteristic = NULL;

            Status = WdfRequestRetrieveOutputBuffer(Request, FIELD_OFFSET(BTHLE_GATT_DESCRIPTORS, Descriptors),
                                                    (PVOID *)&Params, &OutputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            for (Index = 0; Index < Gatt->CharacteristicCount; Index++)
            {
                if (Gatt->Characteristics[Index].Info.AttributeHandle == Params->CharacteristicHandle &&
                    (!ServiceHandle || Gatt->Characteristics[Index].Info.ServiceHandle == ServiceHandle))
                {
                    Characteristic = &Gatt->Characteristics[Index];
                    break;
                }
            }
            if (!Characteristic)
                return STATUS_INVALID_PARAMETER;
            Room = (ULONG)((OutputLength - FIELD_OFFSET(BTHLE_GATT_DESCRIPTORS, Descriptors)) /
                           sizeof(BTH_LE_GATT_DESCRIPTOR));
            for (Index = 0; Index < Gatt->DescriptorCount; Index++)
            {
                PBTHMINI_GATT_DESCRIPTOR Descriptor = &Gatt->Descriptors[Index];

                if (Descriptor->CharacteristicHandle != Characteristic->Info.AttributeHandle)
                    continue;
                if (Count < Room)
                {
                    PBTH_LE_GATT_DESCRIPTOR Out = &Params->Descriptors[Count];
                    USHORT Type = Descriptor->DescriptorUuid.IsShortUuid ? Descriptor->DescriptorUuid.Value.ShortUuid : 0;

                    RtlZeroMemory(Out, sizeof(*Out));
                    Out->ServiceHandle = Characteristic->Info.ServiceHandle;
                    Out->CharacteristicHandle = Characteristic->Info.AttributeHandle;
                    Out->DescriptorUuid = Descriptor->DescriptorUuid;
                    Out->AttributeHandle = Descriptor->AttributeHandle;
                    if (Type >= BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_EXTENDED_PROPERTIES &&
                        Type <= BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_AGGREGATE_FORMAT)
                        Out->DescriptorType = (BTH_LE_GATT_DESCRIPTOR_TYPE)(Type - BTH_LE_GATT_CHARACTERISTIC_DESCRIPTOR_EXTENDED_PROPERTIES);
                    else
                        Out->DescriptorType = CustomDescriptor;
                }
                Count++;
            }
            Params->Count = Count;
            *Information = FIELD_OFFSET(BTHLE_GATT_DESCRIPTORS, Descriptors[min(Count, Room)]);
            return Count > Room ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;
        }

        case IOCTL_BTHLE_GATT_READ_VALUE:
        case IOCTL_BTHLE_GATT_WRITE_VALUE:
        {
            PBTHMINI_GATT_SERVICE Service = BthGattFindService(Gatt, ServiceHandle);
            PBTHLE_GATT_VALUE Params;
            PBTHMINI_GATT_VALUE Value;
            size_t InputLength;

            if (!Service)
                return STATUS_INVALID_DEVICE_REQUEST;
            Status = WdfRequestRetrieveInputBuffer(Request, FIELD_OFFSET(BTHLE_GATT_VALUE, Data), (PVOID *)&Params,
                                                   &InputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            if (Params->Handle <= Service->Info.AttributeHandle || Params->Handle > Service->EndHandle)
                return STATUS_INVALID_PARAMETER;
            if (IoControlCode == IOCTL_BTHLE_GATT_WRITE_VALUE)
            {
                if (Params->Size > GATT_MAX_VALUE || InputLength < FIELD_OFFSET(BTHLE_GATT_VALUE, Data[Params->Size]))
                    return STATUS_INVALID_PARAMETER;
                if (Params->Flags & BTHLE_GATT_WRITE_SIGNED)
                    return STATUS_NOT_SUPPORTED;
                return BthGattQueue(Dev, Remote, (Params->Flags & BTHLE_GATT_WRITE_WITHOUT_RESPONSE) ? AttOpWriteCommand :
                                                 (Params->Flags & BTHLE_GATT_WRITE_PREPARE) ? AttOpPrepareWrite : AttOpWrite,
                                    Request, Params->Handle, Params->Data, Params->Size);
            }
            Value = BthGattFindValue(Gatt, Params->Handle);
            if ((Params->Flags & BTHLE_GATT_READ_FROM_DEVICE) || (!Value && !(Params->Flags & BTHLE_GATT_READ_FROM_CACHE)))
                return BthGattQueue(Dev, Remote, AttOpRead, Request, Params->Handle, NULL, 0);
            Status = WdfRequestRetrieveOutputBuffer(Request, FIELD_OFFSET(BTHLE_GATT_VALUE, Data), (PVOID *)&Params,
                                                    &OutputLength);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            Params->AttError = 0;
            Params->Size = Value ? Value->Length : 0;
            *Information = FIELD_OFFSET(BTHLE_GATT_VALUE, Data);
            if (OutputLength < FIELD_OFFSET(BTHLE_GATT_VALUE, Data[Params->Size]))
                return STATUS_MORE_ENTRIES;
            if (Value)
                RtlCopyMemory(Params->Data, Value->Data, Value->Length);
            *Information = FIELD_OFFSET(BTHLE_GATT_VALUE, Data[Params->Size]);
            return STATUS_SUCCESS;
        }

        case IOCTL_BTHLE_GATT_EXECUTE_WRITE:
        {
            PBTHLE_GATT_EXECUTE_WRITE Params;
            UCHAR Commit;

            if (!ServiceHandle)
                return STATUS_INVALID_DEVICE_REQUEST;
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*Params), (PVOID *)&Params, NULL);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            Commit = Params->Commit ? 1 : 0;
            return BthGattQueue(Dev, Remote, AttOpExecute, Request, 0, &Commit, 1);
        }

        case IOCTL_BTHLE_GATT_WAIT_NOTIFICATION:
        {
            PBTHLE_GATT_NOTIFICATION Params;

            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Params), (PVOID *)&Params, NULL);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_USER_BUFFER;
            if (!Gatt->Events)
            {
                Gatt->Events = ExAllocatePoolZero(NonPagedPool, GATT_EVENT_RING * sizeof(BTHMINI_GATT_EVENT), BTHMINI_TAG);
                if (!Gatt->Events)
                    return STATUS_INSUFFICIENT_RESOURCES;
            }
            if (BthGattFillNotification(Gatt, Params))
            {
                *Information = sizeof(*Params);
                return STATUS_SUCCESS;
            }
            if (!Gatt->Waiters)
            {
                WDF_IO_QUEUE_CONFIG QueueConfig;
                WDF_OBJECT_ATTRIBUTES Attributes;

                WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
                QueueConfig.PowerManaged = WdfFalse;
                WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
                Attributes.ParentObject = Dev->Device;
                Status = WdfIoQueueCreate(Dev->Device, &QueueConfig, &Attributes, &Gatt->Waiters);
                if (!NT_SUCCESS(Status))
                {
                    Gatt->Waiters = NULL;
                    return Status;
                }
            }
            Status = WdfRequestForwardToIoQueue(Request, Gatt->Waiters);
            return NT_SUCCESS(Status) ? STATUS_PENDING : Status;
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
VOID
BthGattUnsubscribe(
    _In_ PBTHMINI_GATT Gatt,
    _In_ USHORT Handle,
    _In_opt_ PVOID Context)
{
    PLIST_ENTRY Entry;
    ULONG Index;

    for (Index = 0; Index < GATT_MAX_SUBSCRIPTIONS; Index++)
    {
        PBTHMINI_GATT_SUBSCRIPTION Subscription = &Gatt->Subscriptions[Index];

        if (Subscription->Callback && Subscription->Handle == Handle && Subscription->Context == Context)
            RtlZeroMemory(Subscription, sizeof(*Subscription));
    }
    Entry = Gatt->Operations.Flink;
    while (Entry != &Gatt->Operations)
    {
        PBTHMINI_ATT_OP Op = CONTAINING_RECORD(Entry, BTHMINI_ATT_OP, Link);
        WDF_REQUEST_PARAMETERS Parameters;
        PBTHMINI_GATT_REQUEST Pending;

        Entry = Entry->Flink;
        if (!Op->Request)
            continue;
        WDF_REQUEST_PARAMETERS_INIT(&Parameters);
        WdfRequestGetParameters(Op->Request, &Parameters);
        Pending = Parameters.Parameters.Others.Arg1;
        if (Parameters.Type != WdfRequestTypeDeviceControlInternal || !Pending ||
            Pending->Operation != BthMiniGattSubscribe || Pending->Handle != Handle || Pending->NotifyContext != Context)
            continue;
        RemoveEntryList(&Op->Link);
        BthGattCompleteOp(Op, STATUS_CANCELLED);
    }
}

VOID
BthGattControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHMINI_REQUEST Context = BthMiniGetRequest(Request);
    WDF_REQUEST_PARAMETERS Parameters;
    PBTHMINI_REMOTE Remote;
    PBTHMINI_GATT Gatt;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    Remote = BthRadioFindRemote(Dev, Context->Address);
    Gatt = Remote ? Remote->Gatt : NULL;
    if (Parameters.Type == WdfRequestTypeDeviceControlInternal &&
        Parameters.Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_BTHMINI_GATT &&
        Parameters.Parameters.Others.Arg1 &&
        ((PBTHMINI_GATT_REQUEST)Parameters.Parameters.Others.Arg1)->Operation == BthMiniGattUnsubscribe)
    {
        PBTHMINI_GATT_REQUEST GattRequest = Parameters.Parameters.Others.Arg1;

        if (Gatt)
            BthGattUnsubscribe(Gatt, GattRequest->Handle, GattRequest->NotifyContext);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }
    if (!Gatt || !Remote->HasLeKey)
    {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        return;
    }
    if (!Gatt->Discovered || Gatt->Phase != GattIdle)
    {
        Status = BthGattQueue(Dev, Remote, AttOpDatabase, Request, 0, NULL, 0);
        if (!NT_SUCCESS(Status))
            WdfRequestComplete(Request, Status);
        return;
    }

    if (Parameters.Type == WdfRequestTypeDeviceControlInternal)
    {
        PBTHMINI_GATT_REQUEST GattRequest = Parameters.Parameters.Others.Arg1;

        if (Parameters.Parameters.DeviceIoControl.IoControlCode != IOCTL_INTERNAL_BTHMINI_GATT || !GattRequest)
            Status = STATUS_NOT_SUPPORTED;
        else
            Status = BthGattInternal(Dev, Remote, Context->Service, Request, GattRequest);
    }
    else if (Parameters.Type == WdfRequestTypeDeviceControl)
    {
        Status = BthGattUserControl(Dev, Remote, Context->Service, Request,
                                    Parameters.Parameters.DeviceIoControl.IoControlCode, &Information);
    }
    else
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    if (Status != STATUS_PENDING)
        WdfRequestCompleteWithInformation(Request, Status, Information);
}

VOID
BthGattPaired(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = BthGattGet(Remote);

    if (!Gatt)
        return;
    if (Gatt->Discovered && Gatt->Phase == GattIdle)
        BthGattSaveCache(Remote);
    BthGattSyncPdos(Dev, Remote);
    if (Remote->LeConnected)
        BthGattLinkReady(Dev, Remote);
}

VOID
BthGattLoad(
    _In_ PBTHMINI_DEVICE Dev)
{
    PLIST_ENTRY Entry;

    for (Entry = Dev->Remotes.Flink; Entry != &Dev->Remotes; Entry = Entry->Flink)
    {
        PBTHMINI_REMOTE Remote = CONTAINING_RECORD(Entry, BTHMINI_REMOTE, Link);
        PBTHMINI_GATT Gatt;

        if (!Remote->HasLeKey)
            continue;
        Gatt = BthGattGet(Remote);
        if (!Gatt)
            continue;
        BthGattLoadCache(Remote);
        BthGattSyncPdos(Dev, Remote);
    }
}

VOID
BthGattRemoveDevice(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    BthGattDeleteCache(Remote);
    if (!Gatt)
        return;
    BthGattRemovePdos(Dev, Gatt);
    BthGattDeleteWaiters(Gatt);
    RtlZeroMemory(Gatt->Subscriptions, sizeof(Gatt->Subscriptions));
    BthGattFailOperations(Gatt, STATUS_DEVICE_NOT_CONNECTED);
    BthGattClearDatabase(Gatt);
}

static
VOID
BthGattReadParameter(
    _In_ WDFKEY Key,
    _In_ PCWSTR Name,
    _In_ ULONG Minimum,
    _In_ ULONG Maximum,
    _Inout_ PUSHORT Value,
    _Inout_ PBOOLEAN Found)
{
    UNICODE_STRING ValueName;
    ULONG Data;

    RtlInitUnicodeString(&ValueName, Name);
    if (NT_SUCCESS(WdfRegistryQueryULong(Key, &ValueName, &Data)) && Data >= Minimum && Data <= Maximum)
    {
        *Value = (USHORT)Data;
        *Found = TRUE;
    }
}

BOOLEAN
BthGattConnectionParameters(
    _In_ PBTHMINI_REMOTE Remote,
    _Inout_ PBTHMINI_LE_PARAMETERS Parameters)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Entry;

    if (!Gatt)
        return FALSE;
    for (Entry = Gatt->ServicePdos.Flink; Entry != &Gatt->ServicePdos; Entry = Entry->Flink)
    {
        PBTHMINI_GATT_PDO ServicePdo = CONTAINING_RECORD(Entry, BTHMINI_GATT_PDO, Link);
        WDFKEY Key;

        if (!NT_SUCCESS(WdfDeviceOpenRegistryKey(ServicePdo->Device, PLUGPLAY_REGKEY_DEVICE, KEY_READ,
                                                 WDF_NO_OBJECT_ATTRIBUTES, &Key)))
            continue;
        BthGattReadParameter(Key, L"HighDutyCycleScanInterval", 0x0004, 0x4000, &Parameters->HighDutyScanInterval, &Found);
        BthGattReadParameter(Key, L"HighDutyCycleScanWindow", 0x0004, 0x4000, &Parameters->HighDutyScanWindow, &Found);
        BthGattReadParameter(Key, L"LowDutyCycleScanInterval", 0x0004, 0x4000, &Parameters->LowDutyScanInterval, &Found);
        BthGattReadParameter(Key, L"LowDutyCycleScanWindow", 0x0004, 0x4000, &Parameters->LowDutyScanWindow, &Found);
        BthGattReadParameter(Key, L"ConnectionIntervalMin", 0x0006, 0x0C80, &Parameters->IntervalMin, &Found);
        BthGattReadParameter(Key, L"ConnectionIntervalMax", 0x0006, 0x0C80, &Parameters->IntervalMax, &Found);
        BthGattReadParameter(Key, L"ConnectionLatency", 0x0000, 0x01F3, &Parameters->Latency, &Found);
        BthGattReadParameter(Key, L"LinkSupervisionTimeout", 0x000A, 0x0C80, &Parameters->SupervisionTimeout, &Found);
        WdfRegistryClose(Key);
        if (Found)
            break;
    }
    if (Parameters->HighDutyScanWindow > Parameters->HighDutyScanInterval)
        Parameters->HighDutyScanWindow = Parameters->HighDutyScanInterval;
    if (Parameters->LowDutyScanWindow > Parameters->LowDutyScanInterval)
        Parameters->LowDutyScanWindow = Parameters->LowDutyScanInterval;
    if (Parameters->IntervalMax < Parameters->IntervalMin)
        Parameters->IntervalMax = Parameters->IntervalMin;
    return Found;
}

VOID
BthGattFree(
    _In_ PBTHMINI_REMOTE Remote)
{
    PBTHMINI_GATT Gatt = Remote->Gatt;

    if (!Gatt)
        return;
    Remote->Gatt = NULL;
    BthGattDeleteWaiters(Gatt);
    BthGattFailOperations(Gatt, STATUS_DEVICE_NOT_CONNECTED);
    BthGattClearDatabase(Gatt);
    while (!IsListEmpty(&Gatt->ServicePdos))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Gatt->ServicePdos), BTHMINI_GATT_PDO, Link), BTHMINI_TAG);
    ExFreePoolWithTag(Gatt, BTHMINI_TAG);
}
