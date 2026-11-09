/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The devices user mode reaches WMI through, and what it may ask them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "wmipriv.h"
#include <wmiioctl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PDEVICE_OBJECT WmipServiceDeviceObject = NULL;
PDEVICE_OBJECT WmipAdminDeviceObject = NULL;

FAST_IO_DISPATCH WmipFastIoDispatch;

/* PRIVATE DATA ***************************************************************/

#define WMIP_TAG 'imWD'

/* How many listeners of one event are called from a single delivery */
#define WMIP_NOTIFY_FANOUT 16

typedef struct _WMIP_OPEN_GUID_BLOCK
{
    POBJECT_ATTRIBUTES ObjectAttributes;
    ACCESS_MASK DesiredAccess;
    ULONG Reserved1;
    ULONG Reserved2;
} WMIP_OPEN_GUID_BLOCK, *PWMIP_OPEN_GUID_BLOCK;

typedef struct _WMIP_OPEN_GUID_RESULT
{
    ULONG Reserved1;
    ULONG Reserved2;
    HANDLE Handle;
    ULONG Reserved3;
} WMIP_OPEN_GUID_RESULT, *PWMIP_OPEN_GUID_RESULT;

/* The callbacks the kernel holds while it is listening for a dock change */
static PCALLBACK_OBJECT WmipProfileChangeCallback = NULL;
static PVOID WmipProfileChangeRegistration = NULL;

/* PRIVATE FUNCTIONS **********************************************************/

/* Finishes a request off with the status and the count the caller gets */
static
NTSTATUS
WmipComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

static
NTSTATUS
WmipReferenceFromWnode(
    _In_ PWNODE_HEADER Wnode,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_ PWMIP_GUID_OBJECT *GuidObject)
{
    *GuidObject = NULL;

    if (Wnode->KernelHandle == NULL) return STATUS_INVALID_HANDLE;

    return ObReferenceObjectByHandle(Wnode->KernelHandle,
                                     DesiredAccess,
                                     WmipGuidObjectType,
                                     AccessMode,
                                     (PVOID *)GuidObject,
                                     NULL);
}

/* The instance name a node carries, as a string pointing into that node */
static
BOOLEAN
WmipInstanceNameFromWnode(
    _In_ PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ ULONG OffsetInstanceName,
    _Out_ PUNICODE_STRING InstanceName)
{
    PUSHORT Length;

    RtlInitEmptyUnicodeString(InstanceName, NULL, 0);

    if (OffsetInstanceName == 0) return FALSE;
    if (OffsetInstanceName + sizeof(USHORT) > BufferSize) return FALSE;

    Length = (PUSHORT)((PUCHAR)Buffer + OffsetInstanceName);

    if (OffsetInstanceName + sizeof(USHORT) + *Length > BufferSize) return FALSE;

    InstanceName->Buffer = (PWSTR)(Length + 1);
    InstanceName->Length = *Length;
    InstanceName->MaximumLength = *Length;

    return TRUE;
}

/* Opens an identifier for a caller that handed in the name of one */
static
NTSTATUS
WmipIoControlOpenBlock(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PWMIP_OPEN_GUID_BLOCK Open = Irp->AssociatedIrp.SystemBuffer;
    PWMIP_OPEN_GUID_RESULT Result = Irp->AssociatedIrp.SystemBuffer;
    KPROCESSOR_MODE AccessMode;
    ACCESS_MASK DesiredAccess;
    NTSTATUS Status;
    PVOID GuidObject;
    HANDLE Handle;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Open) ||
        Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Result))
    {
        return STATUS_INVALID_PARAMETER;
    }

    AccessMode = Irp->RequestorMode;
    DesiredAccess = Open->DesiredAccess;

    if (Open->ObjectAttributes == NULL) return STATUS_INVALID_PARAMETER;

    Status = WmipOpenGuidObjectByName(Open->ObjectAttributes,
                                      DesiredAccess,
                                      AccessMode,
                                      &Handle,
                                      &GuidObject);
    if (!NT_SUCCESS(Status)) return Status;

    ObDereferenceObject(GuidObject);

    RtlZeroMemory(Result, sizeof(*Result));
    Result->Handle = Handle;

    Irp->IoStatus.Information = sizeof(*Result);
    return STATUS_SUCCESS;
}

/* Answers a request for every instance of one data block */
static
NTSTATUS
WmipIoControlQueryAllData(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PWNODE_HEADER Wnode = Irp->AssociatedIrp.SystemBuffer;
    PWMIP_GUID_OBJECT GuidObject;
    NTSTATUS Status;
    ULONG Size;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Wnode))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipReferenceFromWnode(Wnode,
                                    WMIGUID_QUERY,
                                    Irp->RequestorMode,
                                    &GuidObject);
    if (!NT_SUCCESS(Status)) return Status;

    Size = Stack->Parameters.DeviceIoControl.OutputBufferLength;

    Status = WmipQueryAllData(&GuidObject->Guid,
                              &Size,
                              Irp->AssociatedIrp.SystemBuffer);

    ObDereferenceObject(GuidObject);

    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
    {
        Irp->IoStatus.Information = Size;
    }

    return Status;
}

/* Answers a request for one named instance of one data block */
static
NTSTATUS
WmipIoControlSingleInstance(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PWNODE_SINGLE_INSTANCE Wnode = Irp->AssociatedIrp.SystemBuffer;
    PWMIP_GUID_OBJECT GuidObject;
    UNICODE_STRING InstanceName;
    NTSTATUS Status;
    ULONG Size;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Wnode))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!WmipInstanceNameFromWnode(Wnode,
                                   Stack->Parameters.DeviceIoControl.InputBufferLength,
                                   Wnode->OffsetInstanceName,
                                   &InstanceName))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipReferenceFromWnode(&Wnode->WnodeHeader,
                                    WMIGUID_QUERY,
                                    Irp->RequestorMode,
                                    &GuidObject);
    if (!NT_SUCCESS(Status)) return Status;

    Size = Stack->Parameters.DeviceIoControl.OutputBufferLength;

    Status = WmipQuerySingleInstance(&GuidObject->Guid,
                                     &InstanceName,
                                     &Size,
                                     Irp->AssociatedIrp.SystemBuffer);

    ObDereferenceObject(GuidObject);

    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
    {
        Irp->IoStatus.Information = Size;
    }

    return Status;
}

/* Changes a whole instance, or one item of it */
static
NTSTATUS
WmipIoControlSetInstance(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ BOOLEAN SingleItem)
{
    PWNODE_SINGLE_ITEM Wnode = Irp->AssociatedIrp.SystemBuffer;
    ULONG BufferSize = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PWMIP_GUID_OBJECT GuidObject;
    UNICODE_STRING InstanceName;
    NTSTATUS Status;
    ULONG DataOffset;
    ULONG DataSize;
    ULONG ItemId;

    if (SingleItem)
    {
        if (BufferSize < sizeof(WNODE_SINGLE_ITEM)) return STATUS_INVALID_PARAMETER;

        DataOffset = Wnode->DataBlockOffset;
        DataSize = Wnode->SizeDataItem;
        ItemId = Wnode->ItemId;
    }
    else
    {
        PWNODE_SINGLE_INSTANCE Instance = Irp->AssociatedIrp.SystemBuffer;

        if (BufferSize < sizeof(WNODE_SINGLE_INSTANCE))
        {
            return STATUS_INVALID_PARAMETER;
        }

        DataOffset = Instance->DataBlockOffset;
        DataSize = Instance->SizeDataBlock;
        ItemId = 0;
    }

    if (DataOffset > BufferSize || DataSize > BufferSize - DataOffset)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!WmipInstanceNameFromWnode(Wnode,
                                   BufferSize,
                                   Wnode->OffsetInstanceName,
                                   &InstanceName))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipReferenceFromWnode(&Wnode->WnodeHeader,
                                    WMIGUID_SET,
                                    Irp->RequestorMode,
                                    &GuidObject);
    if (!NT_SUCCESS(Status)) return Status;

    Status = WmipSetInstance(&GuidObject->Guid,
                             &InstanceName,
                             SingleItem,
                             ItemId,
                             Wnode->WnodeHeader.Version,
                             DataSize,
                             (PUCHAR)Wnode + DataOffset);

    ObDereferenceObject(GuidObject);

    return Status;
}

/* Calls a method on one named instance */
static
NTSTATUS
WmipIoControlExecuteMethod(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PWNODE_METHOD_ITEM Wnode = Irp->AssociatedIrp.SystemBuffer;
    ULONG BufferSize = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PWMIP_GUID_OBJECT GuidObject;
    UNICODE_STRING InstanceName;
    NTSTATUS Status;
    ULONG OutSize;

    if (BufferSize < sizeof(*Wnode)) return STATUS_INVALID_PARAMETER;

    if (Wnode->DataBlockOffset > BufferSize ||
        Wnode->SizeDataBlock > BufferSize - Wnode->DataBlockOffset)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!WmipInstanceNameFromWnode(Wnode,
                                   BufferSize,
                                   Wnode->OffsetInstanceName,
                                   &InstanceName))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipReferenceFromWnode(&Wnode->WnodeHeader,
                                    WMIGUID_EXECUTE,
                                    Irp->RequestorMode,
                                    &GuidObject);
    if (!NT_SUCCESS(Status)) return Status;

    OutSize = Stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutSize > Wnode->DataBlockOffset)
    {
        OutSize -= Wnode->DataBlockOffset;
    }
    else
    {
        OutSize = 0;
    }

    Status = WmipExecuteMethod(&GuidObject->Guid,
                               &InstanceName,
                               Wnode->MethodId,
                               Wnode->SizeDataBlock,
                               &OutSize,
                               (PUCHAR)Wnode + Wnode->DataBlockOffset);

    ObDereferenceObject(GuidObject);

    if (NT_SUCCESS(Status))
    {
        Wnode->SizeDataBlock = OutSize;
        Irp->IoStatus.Information = Wnode->DataBlockOffset + OutSize;
    }

    return Status;
}

static
NTSTATUS
WmipIoControlReceiveNotifications(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PWNODE_HEADER Wnode = Irp->AssociatedIrp.SystemBuffer;
    PWMIP_GUID_OBJECT GuidObject;
    NTSTATUS Status;
    KIRQL OldIrql;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Wnode))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WmipReferenceFromWnode(Wnode,
                                    WMIGUID_NOTIFICATION,
                                    Irp->RequestorMode,
                                    &GuidObject);
    if (!NT_SUCCESS(Status)) return Status;

    IoAcquireCancelSpinLock(&OldIrql);

    if (Irp->Cancel)
    {
        IoReleaseCancelSpinLock(OldIrql);
        ObDereferenceObject(GuidObject);
        return STATUS_CANCELLED;
    }

    if (GuidObject->Irp != NULL)
    {
        IoReleaseCancelSpinLock(OldIrql);
        ObDereferenceObject(GuidObject);
        return STATUS_DEVICE_BUSY;
    }

    GuidObject->Irp = Irp;
    Irp->Tail.Overlay.DriverContext[0] = GuidObject;
    IoSetCancelRoutine(Irp, WmipNotificationIrpCancel);
    IoMarkIrpPending(Irp);

    IoReleaseCancelSpinLock(OldIrql);

    WmipSetEventConsumer(GuidObject, TRUE);

    ObDereferenceObject(GuidObject);

    return STATUS_PENDING;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
WmipClearIrpObjectList(
    _In_ PWMIP_GUID_OBJECT GuidObject)
{
    PIRP Irp;
    KIRQL OldIrql;

    IoAcquireCancelSpinLock(&OldIrql);

    Irp = GuidObject->Irp;
    GuidObject->Irp = NULL;

    if (Irp != NULL) IoSetCancelRoutine(Irp, NULL);

    IoReleaseCancelSpinLock(OldIrql);

    if (Irp == NULL) return;

    Irp->Tail.Overlay.DriverContext[0] = NULL;
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/* Lets go of a waiting request the caller changed its mind about */
_Function_class_(DRIVER_CANCEL)
VOID
NTAPI
WmipNotificationIrpCancel(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWMIP_GUID_OBJECT GuidObject = Irp->Tail.Overlay.DriverContext[0];

    UNREFERENCED_PARAMETER(DeviceObject);

    if (GuidObject != NULL && GuidObject->Irp == Irp) GuidObject->Irp = NULL;

    Irp->Tail.Overlay.DriverContext[0] = NULL;

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

VOID
NTAPI
WmipDeliverNotification(
    _In_ PWNODE_HEADER Wnode)
{
    struct
    {
        WMI_NOTIFICATION_CALLBACK Callback;
        PVOID Context;
    } Callbacks[WMIP_NOTIFY_FANOUT];
    PWMIP_GUID_OBJECT GuidObject;
    PIO_STACK_LOCATION Stack;
    PLIST_ENTRY Entry;
    ULONG Collected = 0;
    PIRP Irp;
    KIRQL OldIrql;
    ULONG Size;
    ULONG i;

    ExAcquirePushLockShared(&WmipNotificationLock);

    for (Entry = WmipNotificationList.Flink;
         Entry != &WmipNotificationList;
         Entry = Entry->Flink)
    {
        GuidObject = CONTAINING_RECORD(Entry,
                                       WMIP_GUID_OBJECT,
                                       NotificationLink);

        if (!IsEqualGUID(&GuidObject->Guid, &Wnode->Guid)) continue;

        if (GuidObject->NotificationCallback != NULL &&
            Collected < WMIP_NOTIFY_FANOUT)
        {
            Callbacks[Collected].Callback = GuidObject->NotificationCallback;
            Callbacks[Collected].Context = GuidObject->NotificationContext;
            Collected++;
        }

        IoAcquireCancelSpinLock(&OldIrql);

        Irp = GuidObject->Irp;

        if (Irp != NULL)
        {
            Stack = IoGetCurrentIrpStackLocation(Irp);
            Size = Stack->Parameters.DeviceIoControl.OutputBufferLength;

            if (Wnode->BufferSize <= Size)
            {
                GuidObject->Irp = NULL;
                IoSetCancelRoutine(Irp, NULL);
            }
            else
            {
                Irp = NULL;
            }
        }

        IoReleaseCancelSpinLock(OldIrql);

        if (Irp == NULL) continue;

        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer,
                      Wnode,
                      Wnode->BufferSize);

        Irp->Tail.Overlay.DriverContext[0] = NULL;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = Wnode->BufferSize;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    ExReleasePushLockShared(&WmipNotificationLock);

    /* TODO: carry on past the first WMIP_NOTIFY_FANOUT listeners of one event */
    for (i = 0; i < Collected; i++)
    {
        Callbacks[i].Callback(Wnode, Callbacks[i].Context);
    }
}

_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipOpenCloseCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    return WmipComplete(Irp, STATUS_SUCCESS, 0);
}

/* Carries out one of the requests the WMI devices take */
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_WMI_OPEN_BLOCK:
        case IOCTL_WMI_OPEN_GUID_FOR_EVENTS:
            Status = WmipIoControlOpenBlock(Irp, Stack);
            break;

        case IOCTL_WMI_QUERY_ALL_DATA:
            Status = WmipIoControlQueryAllData(Irp, Stack);
            break;

        case IOCTL_WMI_SINGLE_INSTANCE:
            Status = WmipIoControlSingleInstance(Irp, Stack);
            break;

        case IOCTL_WMI_SET_SINGLE_INSTANCE:
            Status = WmipIoControlSetInstance(Irp, Stack, FALSE);
            break;

        case IOCTL_WMI_SET_SINGLE_ITEM:
            Status = WmipIoControlSetInstance(Irp, Stack, TRUE);
            break;

        case IOCTL_WMI_EXECUTE_METHOD:
            Status = WmipIoControlExecuteMethod(Irp, Stack);
            break;

        case IOCTL_WMI_RECEIVE_NOTIFICATIONS:
            Status = WmipIoControlReceiveNotifications(Irp, Stack);

            if (Status == STATUS_PENDING) return Status;
            break;

        default:
            /* TODO: the trace-control and registration requests of this device */
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    return WmipComplete(Irp, Status, Irp->IoStatus.Information);
}

_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PWMIREGINFO RegInfo;
    NTSTATUS Status;
    ULONG Size;

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_REGINFO:
        case IRP_MN_REGINFO_EX:
            Size = Stack->Parameters.WMI.BufferSize;

            if (Size < sizeof(WMIREGINFO) + sizeof(WMIREGGUID))
            {
                Irp->IoStatus.Information =
                    sizeof(WMIREGINFO) + sizeof(WMIREGGUID);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RegInfo = Stack->Parameters.WMI.Buffer;
            RtlZeroMemory(RegInfo, sizeof(WMIREGINFO) + sizeof(WMIREGGUID));

            RegInfo->BufferSize = sizeof(WMIREGINFO) + sizeof(WMIREGGUID);
            RegInfo->GuidCount = 1;
            RegInfo->WmiRegGuid[0].Guid = MSSmBios_RawSMBiosTables_GUID;
            RegInfo->WmiRegGuid[0].InstanceCount = 1;
            RegInfo->WmiRegGuid[0].Flags = WMIREG_FLAG_INSTANCE_LIST;

            Irp->IoStatus.Information = RegInfo->BufferSize;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_ALL_DATA:
        case IRP_MN_QUERY_SINGLE_INSTANCE:
            Size = Stack->Parameters.WMI.BufferSize;

            Status = WmipQueryKernelDataBlock((LPCGUID)Stack->Parameters.WMI.DataPath,
                                              &Size,
                                              Stack->Parameters.WMI.Buffer);
            Irp->IoStatus.Information = Size;
            break;

        case IRP_MN_ENABLE_EVENTS:
        case IRP_MN_DISABLE_EVENTS:
        case IRP_MN_ENABLE_COLLECTION:
        case IRP_MN_DISABLE_COLLECTION:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    return WmipComplete(Irp, Status, Irp->IoStatus.Information);
}

/* Lets the loggers write out what they are holding before the machine stops */
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
WmipShutdown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    EtwpShutdownLoggers();

    if (WmipProfileChangeRegistration != NULL)
    {
        ExUnregisterCallback(WmipProfileChangeRegistration);
        WmipProfileChangeRegistration = NULL;
    }

    return WmipComplete(Irp, STATUS_SUCCESS, 0);
}

BOOLEAN
NTAPI
WmipFastIoDeviceControl(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(DeviceObject);

    if (IoControlCode != IOCTL_WMI_TRACE_EVENT &&
        IoControlCode != IOCTL_WMI_TRACE_USER_MESSAGE)
    {
        return FALSE;
    }

    /* TODO: write the event out here instead of falling back to an IRP */
    IoStatus->Status = STATUS_NOT_SUPPORTED;
    IoStatus->Information = 0;

    return FALSE;
}

VOID
NTAPI
WmipDockUndockEventCallback(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    WNODE_HEADER Wnode;

    UNREFERENCED_PARAMETER(CallbackContext);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    RtlZeroMemory(&Wnode, sizeof(Wnode));

    Wnode.BufferSize = sizeof(Wnode);
    Wnode.Guid = MSAcpiInfo_GUID;
    Wnode.Flags = WNODE_FLAG_EVENT_ITEM;
    KeQuerySystemTime(&Wnode.TimeStamp);

    WmipDeliverNotification(&Wnode);
}

_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
NTSTATUS
NTAPI
WmipDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    static UNICODE_STRING ServiceName =
        RTL_CONSTANT_STRING(L"\\Device\\WMIDataDevice");
    static UNICODE_STRING ServiceLink =
        RTL_CONSTANT_STRING(L"\\DosDevices\\WMIDataDevice");
    static UNICODE_STRING AdminName =
        RTL_CONSTANT_STRING(L"\\Device\\WMIAdminDevice");
    static UNICODE_STRING AdminLink =
        RTL_CONSTANT_STRING(L"\\DosDevices\\WMIAdminDevice");
    static UNICODE_STRING ProfileCallback =
        RTL_CONSTANT_STRING(L"\\Callback\\ProfileChange");
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);

    PAGED_CODE();

    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        DriverObject->MajorFunction[i] = WmipOpenCloseCleanup;
    }

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = WmipIoControl;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = WmipSystemControl;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = WmipShutdown;

    RtlZeroMemory(&WmipFastIoDispatch, sizeof(WmipFastIoDispatch));
    WmipFastIoDispatch.SizeOfFastIoDispatch = sizeof(WmipFastIoDispatch);
    WmipFastIoDispatch.FastIoDeviceControl = WmipFastIoDeviceControl;
    DriverObject->FastIoDispatch = &WmipFastIoDispatch;

    Status = IoCreateDevice(DriverObject,
                            0,
                            &ServiceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &WmipServiceDeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IoCreateSymbolicLink(&ServiceLink, &ServiceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(WmipServiceDeviceObject);
        WmipServiceDeviceObject = NULL;
        return Status;
    }

    Status = IoCreateDevice(DriverObject,
                            0,
                            &AdminName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &WmipAdminDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteSymbolicLink(&ServiceLink);
        IoDeleteDevice(WmipServiceDeviceObject);
        WmipServiceDeviceObject = NULL;
        return Status;
    }

    IoCreateSymbolicLink(&AdminLink, &AdminName);

    WmipServiceDeviceObject->Flags |= DO_BUFFERED_IO;
    WmipAdminDeviceObject->Flags |= DO_BUFFERED_IO;

    IoRegisterShutdownNotification(WmipServiceDeviceObject);

    InitializeObjectAttributes(&Attributes,
                               &ProfileCallback,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    if (NT_SUCCESS(ExCreateCallback(&WmipProfileChangeCallback,
                                    &Attributes,
                                    TRUE,
                                    TRUE)))
    {
        WmipProfileChangeRegistration =
            ExRegisterCallback(WmipProfileChangeCallback,
                               WmipDockUndockEventCallback,
                               NULL);
    }

    WmipServiceDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    WmipAdminDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

/* EOF */
