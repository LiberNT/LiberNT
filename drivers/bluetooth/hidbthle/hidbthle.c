/*
 * PROJECT:     LiberNT Bluetooth LE HID miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HID over GATT Profile (HOGP) report host miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "hidbthle.h"

#define NDEBUG
#include <debug.h>

static IO_COMPLETION_ROUTINE HidBthLeSubscribeComplete;

static
PHIDBTHLE_DEVICE_EXTENSION
HidBthLeGetExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return ((PHID_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->MiniDeviceExtension;
}

static
BOOLEAN
HidBthLeIsUuid(
    _In_ const BTH_LE_UUID *Uuid,
    _In_ USHORT Value)
{
    BTH_LE_UUID Short;

    Short.IsShortUuid = TRUE;
    Short.Value.ShortUuid = Value;
    return IsBthLEUuidMatch(*Uuid, Short);
}

static
VOID
HidBthLeIoDone(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension)
{
    if (InterlockedDecrement(&Extension->IoOutstanding) == 0)
        KeSetEvent(&Extension->IoDrained, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
HidBthLeGatt(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _Inout_ PBTHMINI_GATT_REQUEST Request)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_BTHMINI_GATT, Extension->LowerDevice, NULL, 0, NULL, 0, TRUE,
                                        &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Request;
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
HidBthLeRead(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ USHORT Handle,
    _Out_writes_bytes_(Capacity) PVOID Buffer,
    _In_ ULONG Capacity,
    _Out_ PULONG Length)
{
    BTHMINI_GATT_REQUEST Request;
    NTSTATUS Status;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Operation = BthMiniGattRead;
    Request.Handle = Handle;
    Request.Buffer = Buffer;
    Request.Length = Capacity;
    Status = HidBthLeGatt(Extension, &Request);
    *Length = NT_SUCCESS(Status) ? Request.Length : 0;
    return Status;
}

static
NTSTATUS
HidBthLeWrite(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ USHORT Handle,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ BOOLEAN WithoutResponse)
{
    BTHMINI_GATT_REQUEST Request;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Operation = WithoutResponse ? BthMiniGattWriteWithoutResponse : BthMiniGattWrite;
    Request.Handle = Handle;
    Request.Buffer = (PVOID)Data;
    Request.Length = Length;
    return HidBthLeGatt(Extension, &Request);
}

static
NTSTATUS
HidBthLeQueryList(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ BTHMINI_GATT_OPERATION Operation,
    _In_ ULONG ElementSize,
    _Out_ PVOID *List,
    _Out_ PULONG Count)
{
    BTHMINI_GATT_REQUEST Request;
    NTSTATUS Status;
    PVOID Buffer;

    *List = NULL;
    *Count = 0;
    RtlZeroMemory(&Request, sizeof(Request));
    Request.Operation = Operation;
    Status = HidBthLeGatt(Extension, &Request);
    if (NT_SUCCESS(Status))
        return STATUS_SUCCESS;
    if (Status != STATUS_BUFFER_TOO_SMALL || !Request.Length || Request.Length > 0x1000)
        return Status;
    Buffer = ExAllocatePoolZero(NonPagedPool, Request.Length * ElementSize, HIDBTHLE_TAG);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    Request.Buffer = Buffer;
    Status = HidBthLeGatt(Extension, &Request);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, HIDBTHLE_TAG);
        return Status;
    }
    *List = Buffer;
    *Count = Request.Length;
    return STATUS_SUCCESS;
}

static
PHIDBTHLE_REPORT
HidBthLeFindReport(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ UCHAR Type,
    _In_ UCHAR Id)
{
    ULONG Index;

    for (Index = 0; Index < Extension->ReportCount; Index++)
    {
        if (Extension->Reports[Index].Type == Type && Extension->Reports[Index].Id == Id)
            return &Extension->Reports[Index];
    }
    return NULL;
}

static
VOID
NTAPI
HidBthLeCancelRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDBTHLE_DEVICE_EXTENSION Extension = HidBthLeGetExtension(DeviceObject);
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
HidBthLeRemovePendingRead(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension)
{
    while (!IsListEmpty(&Extension->PendingReads))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Extension->PendingReads), IRP, Tail.Overlay.ListEntry);

        if (IoSetCancelRoutine(Irp, NULL))
            return Irp;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }
    return NULL;
}

static
ULONG
HidBthLeCopyReport(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    Length = min(Length, IoStack->Parameters.DeviceIoControl.OutputBufferLength);
    RtlCopyMemory(Irp->UserBuffer, Report, Length);
    return Length;
}

static
VOID
HidBthLeDeliverReport(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    PIRP Irp;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }
    Irp = HidBthLeRemovePendingRead(Extension);
    if (!Irp)
    {
        PUCHAR Slot;

        if (Extension->ReportQueueCount == HIDBTHLE_QUEUED_REPORTS)
        {
            Extension->ReportQueueHead = (Extension->ReportQueueHead + 1) % HIDBTHLE_QUEUED_REPORTS;
            Extension->ReportQueueCount--;
        }
        Slot = Extension->ReportQueue + ((Extension->ReportQueueHead + Extension->ReportQueueCount) %
                                         HIDBTHLE_QUEUED_REPORTS) * HIDBTHLE_SLOT_LENGTH;
        Slot[0] = (UCHAR)Length;
        Slot[1] = (UCHAR)(Length >> 8);
        RtlCopyMemory(Slot + sizeof(USHORT), Report, Length);
        Extension->ReportQueueCount++;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (Irp)
    {
        Irp->IoStatus.Information = HidBthLeCopyReport(Irp, Report, Length);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
HidBthLeFailPendingReads(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ NTSTATUS Status)
{
    KIRQL OldIrql;
    PIRP Irp;

    for (;;)
    {
        KeAcquireSpinLock(&Extension->Lock, &OldIrql);
        Irp = HidBthLeRemovePendingRead(Extension);
        Extension->ReportQueueCount = 0;
        Extension->ReportQueueHead = 0;
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        if (!Irp)
            break;
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
NTAPI
HidBthLeNotify(
    _In_ PVOID Context,
    _In_ USHORT Handle,
    _In_reads_bytes_(Length) const UCHAR *Value,
    _In_ ULONG Length)
{
    PHIDBTHLE_SUBSCRIPTION Subscription = Context;
    PHIDBTHLE_DEVICE_EXTENSION Extension = Subscription->Extension;
    UCHAR Report[1 + HIDBTHLE_MAX_VALUE];
    ULONG Offset = 0;

    UNREFERENCED_PARAMETER(Handle);
    if (Extension->UsesReportIds)
        Report[Offset++] = Subscription->Report->Id;
    Length = min(Length, HIDBTHLE_MAX_VALUE);
    RtlCopyMemory(Report + Offset, Value, Length);
    if (Offset + Length)
        HidBthLeDeliverReport(Extension, Report, Offset + Length);
}

static
NTSTATUS
NTAPI
HidBthLeSubscribeComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PHIDBTHLE_SUBSCRIPTION Subscription = Context;
    PHIDBTHLE_DEVICE_EXTENSION Extension = Subscription->Extension;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!NT_SUCCESS(Irp->IoStatus.Status))
        DPRINT1("HIDBTHLE: report 0x%04x notifications not enabled 0x%08lx\n",
                Subscription->Report->ValueHandle, Irp->IoStatus.Status);
    HidBthLeIoDone(Extension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
HidBthLeSubscribe(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ ULONG Index)
{
    PHIDBTHLE_SUBSCRIPTION Subscription = &Extension->Subscriptions[Index];
    PIO_STACK_LOCATION IoStack;

    Subscription->Extension = Extension;
    Subscription->Report = &Extension->Reports[Index];
    if (!Subscription->Irp)
        Subscription->Irp = IoAllocateIrp(Extension->LowerDevice->StackSize, FALSE);
    if (!Subscription->Irp)
        return;
    RtlZeroMemory(&Subscription->Request, sizeof(Subscription->Request));
    Subscription->Request.Operation = BthMiniGattSubscribe;
    Subscription->Request.Handle = Subscription->Report->ValueHandle;
    Subscription->Request.Notify = HidBthLeNotify;
    Subscription->Request.NotifyContext = Subscription;
    IoReuseIrp(Subscription->Irp, STATUS_SUCCESS);
    IoStack = IoGetNextIrpStackLocation(Subscription->Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_BTHMINI_GATT;
    IoStack->Parameters.Others.Argument1 = &Subscription->Request;
    IoSetCompletionRoutine(Subscription->Irp, HidBthLeSubscribeComplete, Subscription, TRUE, TRUE, TRUE);
    Subscription->Registered = TRUE;
    InterlockedIncrement(&Extension->IoOutstanding);
    IoCallDriver(Extension->LowerDevice, Subscription->Irp);
}

static
NTSTATUS
HidBthLeOpenCache(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _Out_ PHANDLE Key,
    _Out_ PUNICODE_STRING ValueName)
{
    RtlInitUnicodeString(ValueName, L"HogpCache");
    return IoOpenDeviceRegistryKey(Extension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_READ | KEY_WRITE, Key);
}

static
BOOLEAN
HidBthLeLoadCache(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension)
{
    PKEY_VALUE_PARTIAL_INFORMATION Value = NULL;
    HIDBTHLE_CACHE_HEADER Header;
    UNICODE_STRING ValueName;
    BOOLEAN Loaded = FALSE;
    const UCHAR *Cursor;
    ULONG Size = 0, Index;
    HANDLE Key;

    if (!NT_SUCCESS(HidBthLeOpenCache(Extension, &Key, &ValueName)))
        return FALSE;
    if (ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, NULL, 0, &Size) != STATUS_BUFFER_TOO_SMALL || !Size)
        goto Exit;
    Value = ExAllocatePoolWithTag(PagedPool, Size, HIDBTHLE_TAG);
    if (!Value || !NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, Size, &Size)) ||
        Value->Type != REG_BINARY || Value->DataLength < sizeof(Header))
        goto Exit;
    RtlCopyMemory(&Header, Value->Data, sizeof(Header));
    if (Header.Version != HIDBTHLE_CACHE_VERSION || Header.ReportCount != Extension->ReportCount ||
        !Header.ReportMapLength || Header.ReportMapLength > HIDBTHLE_MAX_VALUE ||
        Value->DataLength != sizeof(Header) + Header.ReportCount * sizeof(HIDBTHLE_REPORT) + Header.ReportMapLength)
        goto Exit;
    Cursor = Value->Data + sizeof(Header);
    for (Index = 0; Index < Extension->ReportCount; Index++)
    {
        HIDBTHLE_REPORT Cached;

        RtlCopyMemory(&Cached, Cursor + Index * sizeof(Cached), sizeof(Cached));
        if (Cached.ValueHandle != Extension->Reports[Index].ValueHandle ||
            Cached.ReferenceHandle != Extension->Reports[Index].ReferenceHandle)
            goto Exit;
    }
    Extension->ReportDescriptor = ExAllocatePoolWithTag(NonPagedPool, Header.ReportMapLength, HIDBTHLE_TAG);
    if (!Extension->ReportDescriptor)
        goto Exit;
    for (Index = 0; Index < Extension->ReportCount; Index++)
    {
        HIDBTHLE_REPORT Cached;

        RtlCopyMemory(&Cached, Cursor + Index * sizeof(Cached), sizeof(Cached));
        Extension->Reports[Index].Id = Cached.Id;
        Extension->Reports[Index].Type = Cached.Type;
    }
    RtlCopyMemory(Extension->ReportDescriptor, Cursor + Extension->ReportCount * sizeof(HIDBTHLE_REPORT),
                  Header.ReportMapLength);
    Extension->ReportDescriptorLength = Header.ReportMapLength;
    Extension->HidVersion = Header.HidVersion;
    Extension->CountryCode = Header.CountryCode;
    Loaded = TRUE;

Exit:
    if (Value)
        ExFreePoolWithTag(Value, HIDBTHLE_TAG);
    ZwClose(Key);
    return Loaded;
}

static
VOID
HidBthLeSaveCache(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension)
{
    PHIDBTHLE_CACHE_HEADER Header;
    UNICODE_STRING ValueName;
    PUCHAR Buffer;
    ULONG Length;
    HANDLE Key;

    Length = sizeof(*Header) + Extension->ReportCount * sizeof(HIDBTHLE_REPORT) + Extension->ReportDescriptorLength;
    Buffer = ExAllocatePoolZero(PagedPool, Length, HIDBTHLE_TAG);
    if (!Buffer)
        return;
    Header = (PHIDBTHLE_CACHE_HEADER)Buffer;
    Header->Version = HIDBTHLE_CACHE_VERSION;
    Header->HidVersion = Extension->HidVersion;
    Header->CountryCode = Extension->CountryCode;
    Header->ReportCount = (UCHAR)Extension->ReportCount;
    Header->ReportMapLength = Extension->ReportDescriptorLength;
    RtlCopyMemory(Buffer + sizeof(*Header), Extension->Reports, Extension->ReportCount * sizeof(HIDBTHLE_REPORT));
    RtlCopyMemory(Buffer + sizeof(*Header) + Extension->ReportCount * sizeof(HIDBTHLE_REPORT),
                  Extension->ReportDescriptor, Extension->ReportDescriptorLength);
    if (NT_SUCCESS(HidBthLeOpenCache(Extension, &Key, &ValueName)))
    {
        ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, Buffer, Length);
        ZwClose(Key);
    }
    ExFreePoolWithTag(Buffer, HIDBTHLE_TAG);
}

static
NTSTATUS
HidBthLeReadDevice(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ USHORT MapHandle,
    _In_ USHORT InformationHandle)
{
    UCHAR Value[HIDBTHLE_MAX_VALUE];
    ULONG Length, Index;
    NTSTATUS Status;

    Status = HidBthLeRead(Extension, MapHandle, Value, sizeof(Value), &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Length)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Extension->ReportDescriptor = ExAllocatePoolWithTag(NonPagedPool, Length, HIDBTHLE_TAG);
    if (!Extension->ReportDescriptor)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Extension->ReportDescriptor, Value, Length);
    Extension->ReportDescriptorLength = (USHORT)Length;

    for (Index = 0; Index < Extension->ReportCount; Index++)
    {
        PHIDBTHLE_REPORT Report = &Extension->Reports[Index];

        Report->Type = Report->Notifiable ? HOGP_REPORT_INPUT : Report->Writable ? HOGP_REPORT_OUTPUT : HOGP_REPORT_FEATURE;
        if (!Report->ReferenceHandle)
            continue;
        Status = HidBthLeRead(Extension, Report->ReferenceHandle, Value, 2, &Length);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Length >= 2 && Value[1] >= HOGP_REPORT_INPUT && Value[1] <= HOGP_REPORT_FEATURE)
        {
            Report->Id = Value[0];
            Report->Type = Value[1];
        }
    }

    Extension->HidVersion = 0x0111;
    if (InformationHandle && NT_SUCCESS(HidBthLeRead(Extension, InformationHandle, Value, 4, &Length)) && Length >= 3)
    {
        Extension->HidVersion = (USHORT)(Value[0] | (Value[1] << 8));
        Extension->CountryCode = Value[2];
    }
    HidBthLeSaveCache(Extension);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidBthLeDiscover(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _Out_ PUSHORT MapHandle,
    _Out_ PUSHORT InformationHandle)
{
    PBTH_LE_GATT_CHARACTERISTIC Characteristics;
    PBTHMINI_GATT_DESCRIPTOR Descriptors;
    ULONG CharacteristicCount, DescriptorCount, Index, Inner;
    NTSTATUS Status;

    *MapHandle = 0;
    *InformationHandle = 0;
    Status = HidBthLeQueryList(Extension, BthMiniGattGetCharacteristics, sizeof(*Characteristics),
                               (PVOID *)&Characteristics, &CharacteristicCount);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = HidBthLeQueryList(Extension, BthMiniGattGetDescriptors, sizeof(*Descriptors), (PVOID *)&Descriptors,
                               &DescriptorCount);
    if (!NT_SUCCESS(Status))
    {
        if (Characteristics)
            ExFreePoolWithTag(Characteristics, HIDBTHLE_TAG);
        return Status;
    }

    for (Index = 0; Index < CharacteristicCount; Index++)
    {
        PBTH_LE_GATT_CHARACTERISTIC Characteristic = &Characteristics[Index];
        PHIDBTHLE_REPORT Report;

        if (HidBthLeIsUuid(&Characteristic->CharacteristicUuid, HOGP_UUID_REPORT_MAP))
            *MapHandle = Characteristic->CharacteristicValueHandle;
        if (HidBthLeIsUuid(&Characteristic->CharacteristicUuid, HOGP_UUID_HID_INFORMATION))
            *InformationHandle = Characteristic->CharacteristicValueHandle;
        if (!HidBthLeIsUuid(&Characteristic->CharacteristicUuid, HOGP_UUID_REPORT) ||
            Extension->ReportCount == HIDBTHLE_MAX_REPORTS)
            continue;
        Report = &Extension->Reports[Extension->ReportCount++];
        Report->CharacteristicHandle = Characteristic->AttributeHandle;
        Report->ValueHandle = Characteristic->CharacteristicValueHandle;
        Report->Readable = Characteristic->IsReadable;
        Report->Writable = Characteristic->IsWritable;
        Report->WritableWithoutResponse = Characteristic->IsWritableWithoutResponse;
        Report->Notifiable = Characteristic->IsNotifiable;
        for (Inner = 0; Inner < DescriptorCount; Inner++)
        {
            if (Descriptors[Inner].CharacteristicHandle == Characteristic->AttributeHandle &&
                HidBthLeIsUuid(&Descriptors[Inner].DescriptorUuid, HOGP_UUID_REPORT_REFERENCE))
            {
                Report->ReferenceHandle = Descriptors[Inner].AttributeHandle;
                break;
            }
        }
    }
    if (Characteristics)
        ExFreePoolWithTag(Characteristics, HIDBTHLE_TAG);
    if (Descriptors)
        ExFreePoolWithTag(Descriptors, HIDBTHLE_TAG);
    return *MapHandle && Extension->ReportCount ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
VOID
HidBthLeStop(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension)
{
    LARGE_INTEGER Timeout;
    KIRQL OldIrql;
    ULONG Index;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }
    Extension->Started = FALSE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    HidBthLeFailPendingReads(Extension, STATUS_DEVICE_NOT_CONNECTED);
    for (Index = 0; Index < Extension->ReportCount; Index++)
    {
        PHIDBTHLE_SUBSCRIPTION Subscription = &Extension->Subscriptions[Index];
        BTHMINI_GATT_REQUEST Request;

        if (!Subscription->Registered)
            continue;
        RtlZeroMemory(&Request, sizeof(Request));
        Request.Operation = BthMiniGattUnsubscribe;
        Request.Handle = Subscription->Report->ValueHandle;
        Request.NotifyContext = Subscription;
        HidBthLeGatt(Extension, &Request);
        Subscription->Registered = FALSE;
    }

    Timeout.QuadPart = -(LONGLONG)30000 * 10000;
    KeClearEvent(&Extension->IoDrained);
    if (Extension->IoOutstanding &&
        KeWaitForSingleObject(&Extension->IoDrained, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
    {
        DPRINT1("HIDBTHLE: %ld requests still outstanding\n", Extension->IoOutstanding);
    }
}

static
VOID
HidBthLeFreeResources(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension)
{
    ULONG Index;

    for (Index = 0; Index < HIDBTHLE_MAX_REPORTS; Index++)
    {
        if (Extension->Subscriptions[Index].Irp && !Extension->IoOutstanding)
        {
            IoFreeIrp(Extension->Subscriptions[Index].Irp);
            Extension->Subscriptions[Index].Irp = NULL;
        }
    }
    if (Extension->ReportDescriptor)
        ExFreePoolWithTag(Extension->ReportDescriptor, HIDBTHLE_TAG);
    Extension->ReportDescriptor = NULL;
    Extension->ReportDescriptorLength = 0;
    if (Extension->ReportQueue)
        ExFreePoolWithTag(Extension->ReportQueue, HIDBTHLE_TAG);
    Extension->ReportQueue = NULL;
    Extension->ReportCount = 0;
}

static
NTSTATUS
HidBthLeStart(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension)
{
    BTHMINI_GATT_REQUEST Request;
    USHORT MapHandle, InformationHandle;
    BOOLEAN Cached;
    ULONG Index;
    NTSTATUS Status;

    HidBthLeFreeResources(Extension);
    RtlZeroMemory(&Request, sizeof(Request));
    Request.Operation = BthMiniGattGetDeviceInfo;
    Request.Buffer = &Extension->Info;
    Request.Length = sizeof(Extension->Info);
    Status = HidBthLeGatt(Extension, &Request);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = HidBthLeDiscover(Extension, &MapHandle, &InformationHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDBTHLE: %012I64x has no usable HID service 0x%08lx\n", Extension->Info.Address, Status);
        return Status;
    }
    Cached = HidBthLeLoadCache(Extension);
    if (!Cached)
    {
        Status = HidBthLeReadDevice(Extension, MapHandle, InformationHandle);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HIDBTHLE: %012I64x HID service not readable 0x%08lx\n", Extension->Info.Address, Status);
            return Status;
        }
    }
    Extension->UsesReportIds = FALSE;
    for (Index = 0; Index < Extension->ReportCount; Index++)
    {
        if (Extension->Reports[Index].Id)
            Extension->UsesReportIds = TRUE;
    }

    Extension->ReportQueue = ExAllocatePoolWithTag(NonPagedPool, HIDBTHLE_SLOT_LENGTH * HIDBTHLE_QUEUED_REPORTS,
                                                   HIDBTHLE_TAG);
    if (!Extension->ReportQueue)
        return STATUS_INSUFFICIENT_RESOURCES;
    Extension->Started = TRUE;
    for (Index = 0; Index < Extension->ReportCount; Index++)
    {
        if (Extension->Reports[Index].Type == HOGP_REPORT_INPUT && Extension->Reports[Index].Notifiable)
            HidBthLeSubscribe(Extension, Index);
    }
    DPRINT1("HIDBTHLE: %012I64x started, report map %u bytes%s, %lu reports, VID %04x PID %04x\n",
            Extension->Info.Address, Extension->ReportDescriptorLength, Cached ? " (cached)" : "",
            Extension->ReportCount, Extension->Info.VendorId, Extension->Info.ProductId);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidBthLeReadReport(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    KIRQL OldIrql;
    ULONG Length;

    if (!Irp->UserBuffer || !IoStack->Parameters.DeviceIoControl.OutputBufferLength)
    {
        Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_USER_BUFFER;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Extension->ReportQueueCount)
    {
        PUCHAR Slot = Extension->ReportQueue + Extension->ReportQueueHead * HIDBTHLE_SLOT_LENGTH;

        Length = HidBthLeCopyReport(Irp, Slot + sizeof(USHORT), Slot[0] | ((ULONG)Slot[1] << 8));
        Extension->ReportQueueHead = (Extension->ReportQueueHead + 1) % HIDBTHLE_QUEUED_REPORTS;
        Extension->ReportQueueCount--;
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Information = Length;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    IoMarkIrpPending(Irp);
    InsertTailList(&Extension->PendingReads, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, HidBthLeCancelRead);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    return STATUS_PENDING;
}

static
NTSTATUS
HidBthLeGetReport(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ UCHAR Type,
    _Inout_ PHID_XFER_PACKET Packet)
{
    PHIDBTHLE_REPORT Report = HidBthLeFindReport(Extension, Type, Packet->reportId);
    ULONG Length;
    NTSTATUS Status;

    if (!Report || !Report->Readable)
        return STATUS_INVALID_PARAMETER;
    if (!Packet->reportBuffer || Packet->reportBufferLen < 1)
        return STATUS_BUFFER_TOO_SMALL;
    Packet->reportBuffer[0] = Report->Id;
    Status = HidBthLeRead(Extension, Report->ValueHandle, Packet->reportBuffer + 1, Packet->reportBufferLen - 1, &Length);
    if (NT_SUCCESS(Status))
        Packet->reportBufferLen = 1 + Length;
    return Status;
}

static
NTSTATUS
HidBthLeSetReport(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _In_ UCHAR Type,
    _In_ PHID_XFER_PACKET Packet,
    _In_ BOOLEAN PreferCommand)
{
    PHIDBTHLE_REPORT Report = HidBthLeFindReport(Extension, Type, Packet->reportId);
    const UCHAR *Data = Packet->reportBuffer;
    ULONG Length = Packet->reportBufferLen;
    BOOLEAN WithoutResponse;

    if (!Report || (!Report->Writable && !Report->WritableWithoutResponse))
        return STATUS_INVALID_PARAMETER;
    if (Length && Data[0] == Packet->reportId)
    {
        Data++;
        Length--;
    }
    WithoutResponse = Report->WritableWithoutResponse && (PreferCommand || !Report->Writable);
    return HidBthLeWrite(Extension, Report->ValueHandle, Data, Length, WithoutResponse);
}

static
NTSTATUS
HidBthLeCompleteWithBuffer(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Irp->UserBuffer || IoStack->Parameters.DeviceIoControl.OutputBufferLength < Length)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
    }
    else
    {
        RtlCopyMemory(Irp->UserBuffer, Data, Length);
        Irp->IoStatus.Information = Length;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
HidBthLeGetString(
    _In_ PHIDBTHLE_DEVICE_EXTENSION Extension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG StringId = (ULONG)(ULONG_PTR)IoStack->Parameters.DeviceIoControl.Type3InputBuffer & 0xFFFF;
    WCHAR Name[BTH_MAX_NAME_SIZE];
    ULONG Index;

    if (StringId != HID_STRING_ID_IPRODUCT || !Extension->Info.Name[0])
    {
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_NOT_SUPPORTED;
    }
    for (Index = 0; Index < BTH_MAX_NAME_SIZE - 1 && Extension->Info.Name[Index]; Index++)
        Name[Index] = (UCHAR)Extension->Info.Name[Index];
    Name[Index] = UNICODE_NULL;
    return HidBthLeCompleteWithBuffer(Irp, Name, (Index + 1) * sizeof(WCHAR));
}

static
NTSTATUS
NTAPI
HidBthLeInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDBTHLE_DEVICE_EXTENSION Extension = HidBthLeGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG IoControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;
    PHID_XFER_PACKET Packet = Irp->UserBuffer;
    NTSTATUS Status;

    if (IoControlCode == IOCTL_HID_READ_REPORT)
        return HidBthLeReadReport(Extension, Irp);

    Irp->IoStatus.Information = 0;
    if (!Extension->Started)
    {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }

    switch (IoControlCode)
    {
        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        {
            HID_DESCRIPTOR HidDescriptor;

            RtlZeroMemory(&HidDescriptor, sizeof(HidDescriptor));
            HidDescriptor.bLength = sizeof(HidDescriptor);
            HidDescriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
            HidDescriptor.bcdHID = Extension->HidVersion;
            HidDescriptor.bCountry = Extension->CountryCode;
            HidDescriptor.bNumDescriptors = 1;
            HidDescriptor.DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            HidDescriptor.DescriptorList[0].wReportLength = Extension->ReportDescriptorLength;
            return HidBthLeCompleteWithBuffer(Irp, &HidDescriptor, sizeof(HidDescriptor));
        }

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            return HidBthLeCompleteWithBuffer(Irp, Extension->ReportDescriptor, Extension->ReportDescriptorLength);

        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        {
            HID_DEVICE_ATTRIBUTES Attributes;

            RtlZeroMemory(&Attributes, sizeof(Attributes));
            Attributes.Size = sizeof(Attributes);
            Attributes.VendorID = Extension->Info.VendorId;
            Attributes.ProductID = Extension->Info.ProductId;
            Attributes.VersionNumber = Extension->Info.Version;
            return HidBthLeCompleteWithBuffer(Irp, &Attributes, sizeof(Attributes));
        }

        case IOCTL_HID_GET_STRING:
            return HidBthLeGetString(Extension, Irp);

        case IOCTL_HID_GET_FEATURE:
        case IOCTL_HID_GET_INPUT_REPORT:
            Status = Packet ? HidBthLeGetReport(Extension, IoControlCode == IOCTL_HID_GET_FEATURE ? HOGP_REPORT_FEATURE :
                                                                                                    HOGP_REPORT_INPUT, Packet) :
                              STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Packet->reportBufferLen;
            break;

        case IOCTL_HID_SET_FEATURE:
        case IOCTL_HID_SET_OUTPUT_REPORT:
        case IOCTL_HID_WRITE_REPORT:
            Status = Packet ? HidBthLeSetReport(Extension, IoControlCode == IOCTL_HID_SET_FEATURE ? HOGP_REPORT_FEATURE :
                                                                                                    HOGP_REPORT_OUTPUT,
                                                Packet, IoControlCode == IOCTL_HID_WRITE_REPORT) :
                              STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Packet->reportBufferLen;
            break;

        case IOCTL_HID_ACTIVATE_DEVICE:
        case IOCTL_HID_DEACTIVATE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
HidBthLePnpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
HidBthLePnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDBTHLE_DEVICE_EXTENSION Extension = HidBthLeGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    KEVENT Event;
    NTSTATUS Status;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, HidBthLePnpCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
                Status = HidBthLeStart(Extension);
            if (!NT_SUCCESS(Status))
            {
                HidBthLeStop(Extension);
                HidBthLeFreeResources(Extension);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            HidBthLeStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            HidBthLeStop(Extension);
            HidBthLeFreeResources(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidBthLePower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(HidBthLeGetExtension(DeviceObject)->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidBthLePassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(HidBthLeGetExtension(DeviceObject)->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidBthLeCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HidBthLeAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDBTHLE_DEVICE_EXTENSION Extension = HidExtension->MiniDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->DeviceObject = DeviceObject;
    Extension->LowerDevice = HidExtension->NextDeviceObject;
    Extension->PhysicalDevice = HidExtension->PhysicalDeviceObject;
    KeInitializeSpinLock(&Extension->Lock);
    KeInitializeEvent(&Extension->IoDrained, NotificationEvent, FALSE);
    InitializeListHead(&Extension->PendingReads);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
HidBthLeUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    HID_MINIDRIVER_REGISTRATION Registration;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = HidBthLeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HidBthLeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HidBthLeInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HidBthLePnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HidBthLePower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HidBthLePassThrough;
    DriverObject->DriverExtension->AddDevice = HidBthLeAddDevice;
    DriverObject->DriverUnload = HidBthLeUnload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(HIDBTHLE_DEVICE_EXTENSION);
    Registration.DevicesArePolled = FALSE;
    return HidRegisterMinidriver(&Registration);
}
