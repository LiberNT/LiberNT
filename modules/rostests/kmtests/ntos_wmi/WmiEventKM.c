/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WMI events: delivery to every listener and events named from static instance names
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <wmistr.h>

#define KMT_WMI_LISTENER_COUNT 24
#define KMT_WMI_TAG 'mWmK'

static const GUID KmtSmBiosGuid = { 0x8f680850, 0xa584, 0x11d1, { 0xbf, 0x38, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0x10 } };
static const GUID KmtWmiEventGuid = { 0x3c71e2a4, 0x5b0d, 0x4f6e, { 0x8d, 0x12, 0x6a, 0x09, 0xe4, 0x7b, 0xc3, 0x58 } };
static const WCHAR KmtWmiBaseName[] = L"KmtWmiEvent";

typedef struct _KMT_WMI_LISTENERS
{
    KEVENT AllCalled;
    volatile LONG Calls;
    LONG Expected;
} KMT_WMI_LISTENERS, *PKMT_WMI_LISTENERS;

typedef struct _KMT_WMI_NAMED
{
    KEVENT Delivered;
    ULONG Flags;
    ULONG Data;
    WCHAR Name[32];
    USHORT NameLength;
} KMT_WMI_NAMED, *PKMT_WMI_NAMED;

extern PDRIVER_OBJECT KmtDriverObject;

static PDEVICE_OBJECT KmtWmiDevice;
static KEVENT KmtWmiEventsEnabled;

static
VOID
NTAPI
KmtWmiCountCallback(
    _In_ PVOID Wnode,
    _In_ PVOID Context)
{
    PKMT_WMI_LISTENERS Listeners = Context;

    UNREFERENCED_PARAMETER(Wnode);

    if (InterlockedIncrement(&Listeners->Calls) == Listeners->Expected)
        KeSetEvent(&Listeners->AllCalled, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
KmtWmiNamedCallback(
    _In_ PVOID Wnode,
    _In_ PVOID Context)
{
    PWNODE_SINGLE_INSTANCE Instance = Wnode;
    PKMT_WMI_NAMED Named = Context;
    USHORT Length;

    Named->Flags = Instance->WnodeHeader.Flags;
    if (Instance->SizeDataBlock == sizeof(ULONG))
        Named->Data = *(PULONG)((PUCHAR)Instance + Instance->DataBlockOffset);
    if (!(Instance->WnodeHeader.Flags & WNODE_FLAG_STATIC_INSTANCE_NAMES) && Instance->OffsetInstanceName)
    {
        Length = *(PUSHORT)((PUCHAR)Instance + Instance->OffsetInstanceName);
        if (Length <= sizeof(Named->Name))
        {
            RtlCopyMemory(Named->Name, (PUCHAR)Instance + Instance->OffsetInstanceName + sizeof(USHORT), Length);
            Named->NameLength = Length;
        }
    }

    KeSetEvent(&Named->Delivered, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
NTAPI
KmtWmiSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG NameOffset = sizeof(WMIREGINFOW) + sizeof(WMIREGGUIDW);
    ULONG Size = NameOffset + sizeof(USHORT) + sizeof(KmtWmiBaseName);
    NTSTATUS Status = Irp->IoStatus.Status;
    ULONG_PTR Information = Irp->IoStatus.Information;
    PWMIREGINFOW RegInfo;
    PUCHAR Buffer;

    if (DeviceObject == KmtWmiDevice && Stack->Parameters.WMI.ProviderId == (ULONG_PTR)DeviceObject)
    {
        Buffer = Stack->Parameters.WMI.Buffer;
        switch (Stack->MinorFunction)
        {
            case IRP_MN_REGINFO:
            case IRP_MN_REGINFO_EX:
                if (Stack->Parameters.WMI.BufferSize < Size)
                {
                    *(PULONG)Buffer = Size;
                    Information = sizeof(ULONG);
                    Status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                RtlZeroMemory(Buffer, Size);
                RegInfo = (PWMIREGINFOW)Buffer;
                RegInfo->BufferSize = Size;
                RegInfo->GuidCount = 1;
                RegInfo->WmiRegGuid[0].Guid = KmtWmiEventGuid;
                RegInfo->WmiRegGuid[0].Flags = WMIREG_FLAG_INSTANCE_BASENAME | WMIREG_FLAG_EVENT_ONLY_GUID;
                RegInfo->WmiRegGuid[0].InstanceCount = 1;
                RegInfo->WmiRegGuid[0].BaseNameOffset = NameOffset;
                *(PUSHORT)(Buffer + NameOffset) = sizeof(KmtWmiBaseName) - sizeof(WCHAR);
                RtlCopyMemory(Buffer + NameOffset + sizeof(USHORT), KmtWmiBaseName, sizeof(KmtWmiBaseName));
                Information = Size;
                Status = STATUS_SUCCESS;
                break;

            case IRP_MN_ENABLE_EVENTS:
                KeSetEvent(&KmtWmiEventsEnabled, IO_NO_INCREMENT, FALSE);
                Status = STATUS_SUCCESS;
                break;

            case IRP_MN_DISABLE_EVENTS:
                Status = STATUS_SUCCESS;
                break;

            default:
                break;
        }
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
PWNODE_HEADER
KmtWmiAllocateEventItem(
    _In_ LPCGUID Guid)
{
    PWNODE_HEADER Wnode;

    Wnode = ExAllocatePoolZero(NonPagedPoolNx, sizeof(WNODE_HEADER) + sizeof(ULONG), KMT_WMI_TAG);
    if (Wnode)
    {
        Wnode->BufferSize = sizeof(WNODE_HEADER) + sizeof(ULONG);
        Wnode->Guid = *Guid;
        Wnode->Flags = WNODE_FLAG_EVENT_ITEM;
    }

    return Wnode;
}

static
VOID
TestEveryListener(VOID)
{
    PKMT_WMI_LISTENERS Listeners;
    PVOID Objects[KMT_WMI_LISTENER_COUNT];
    LARGE_INTEGER Timeout;
    PWNODE_HEADER Wnode;
    ULONG Opened = 0, i;
    NTSTATUS Status = STATUS_SUCCESS;

    Listeners = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Listeners), KMT_WMI_TAG);
    if (skip(Listeners != NULL, "out of memory\n"))
        return;

    KeInitializeEvent(&Listeners->AllCalled, NotificationEvent, FALSE);
    Listeners->Expected = KMT_WMI_LISTENER_COUNT;

    for (i = 0; i < KMT_WMI_LISTENER_COUNT; i++)
    {
        Status = IoWMIOpenBlock(&KmtSmBiosGuid, WMIGUID_NOTIFICATION | SYNCHRONIZE, &Objects[i]);
        if (!NT_SUCCESS(Status))
            break;
        Opened++;
        Status = IoWMISetNotificationCallback(Objects[i], KmtWmiCountCallback, Listeners);
        if (!NT_SUCCESS(Status))
            break;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Opened, (ULONG)KMT_WMI_LISTENER_COUNT);

    Wnode = KmtWmiAllocateEventItem(&KmtSmBiosGuid);
    if (!skip(Wnode != NULL, "out of memory\n"))
    {
        Status = IoWMIWriteEvent(Wnode);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            ExFreePoolWithTag(Wnode, KMT_WMI_TAG);

        Timeout.QuadPart = -2000 * 10000LL;
        KeWaitForSingleObject(&Listeners->AllCalled, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_long(Listeners->Calls, (LONG)KMT_WMI_LISTENER_COUNT);
    }

    for (i = 0; i < Opened; i++)
    {
        IoWMISetNotificationCallback(Objects[i], NULL, NULL);
        ObDereferenceObject(Objects[i]);
    }

    ExFreePoolWithTag(Listeners, KMT_WMI_TAG);
}

static
VOID
TestStaticInstanceName(VOID)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KmtWmiEventProvider");
    UNICODE_STRING Prefix, Name;
    PDRIVER_DISPATCH SavedSystemControl;
    PWNODE_SINGLE_INSTANCE Event;
    PKMT_WMI_NAMED Named;
    LARGE_INTEGER Timeout;
    PVOID Object;
    ULONG Size;
    NTSTATUS Status;

    Named = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Named), KMT_WMI_TAG);
    if (skip(Named != NULL, "out of memory\n"))
        return;
    KeInitializeEvent(&Named->Delivered, NotificationEvent, FALSE);
    KeInitializeEvent(&KmtWmiEventsEnabled, NotificationEvent, FALSE);

    Status = IoWMIOpenBlock(&KmtWmiEventGuid, WMIGUID_NOTIFICATION | SYNCHRONIZE, &Object);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Named, KMT_WMI_TAG);
        return;
    }
    Status = IoWMISetNotificationCallback(Object, KmtWmiNamedCallback, Named);
    ok_eq_hex(Status, STATUS_SUCCESS);

    SavedSystemControl = KmtDriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    KmtDriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = KmtWmiSystemControl;

    Status = IoCreateDevice(KmtDriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &KmtWmiDevice);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        KmtWmiDevice->Flags &= ~DO_DEVICE_INITIALIZING;

        Status = IoWMIRegistrationControl(KmtWmiDevice, WMIREG_ACTION_REGISTER);
        ok_eq_hex(Status, STATUS_SUCCESS);

        /* The listener asks the provider for its events once the block is registered */
        Timeout.QuadPart = -2000 * 10000LL;
        Status = KeWaitForSingleObject(&KmtWmiEventsEnabled, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Size = FIELD_OFFSET(WNODE_SINGLE_INSTANCE, VariableData) + sizeof(ULONG);
        Event = ExAllocatePoolZero(NonPagedPoolNx, Size, KMT_WMI_TAG);
        if (Status == STATUS_SUCCESS && !skip(Event != NULL, "out of memory\n"))
        {
            Event->WnodeHeader.BufferSize = Size;
            Event->WnodeHeader.ProviderId = IoWMIDeviceObjectToProviderId(KmtWmiDevice);
            Event->WnodeHeader.Guid = KmtWmiEventGuid;
            Event->WnodeHeader.Flags = WNODE_FLAG_EVENT_ITEM | WNODE_FLAG_SINGLE_INSTANCE |
                                       WNODE_FLAG_STATIC_INSTANCE_NAMES;
            Event->InstanceIndex = 0;
            Event->DataBlockOffset = FIELD_OFFSET(WNODE_SINGLE_INSTANCE, VariableData);
            Event->SizeDataBlock = sizeof(ULONG);
            *(PULONG)((PUCHAR)Event + Event->DataBlockOffset) = 0xC0FFEE;

            /* The event is named from the static names it was registered with, and its buffer is WMI's */
            Status = IoWMIWriteEvent(Event);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (!NT_SUCCESS(Status))
                ExFreePoolWithTag(Event, KMT_WMI_TAG);

            Timeout.QuadPart = -2000 * 10000LL;
            Status = KeWaitForSingleObject(&Named->Delivered, Executive, KernelMode, FALSE, &Timeout);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Named->Flags & WNODE_FLAG_STATIC_INSTANCE_NAMES, 0UL);
            ok_eq_ulong(Named->Data, 0xC0FFEEUL);

            RtlInitUnicodeString(&Prefix, KmtWmiBaseName);
            Name.Buffer = Named->Name;
            Name.Length = Name.MaximumLength = Named->NameLength;
            ok(RtlPrefixUnicodeString(&Prefix, &Name, FALSE) && Name.Length > Prefix.Length,
               "instance name %wZ\n", &Name);
        }

        IoWMIRegistrationControl(KmtWmiDevice, WMIREG_ACTION_DEREGISTER);
        IoDeleteDevice(KmtWmiDevice);
        KmtWmiDevice = NULL;
    }

    IoWMISetNotificationCallback(Object, NULL, NULL);
    ObDereferenceObject(Object);
    KmtDriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = SavedSystemControl;

    ExFreePoolWithTag(Named, KMT_WMI_TAG);
}

START_TEST(WmiEventKM)
{
    TestEveryListener();
    TestStaticInstanceName();
}
