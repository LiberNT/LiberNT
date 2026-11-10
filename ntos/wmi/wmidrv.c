/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/wmi/wmidrv.c
 * PURPOSE:         I/O Windows Management Instrumentation (WMI) Support
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include <wmistr.h>
#include <wmiioctl.h>
#include "wmip.h"

#define NDEBUG
#include <debug.h>

// FIXME: these should go to a shared header
typedef struct _WMIP_REGISTER_GUIDS
{
    POBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Unknown04;
    ULONG Unknown08;
    ULONG Unknown0C;
    ULONG Unknown10;
    ULONG Unknown14;

    WMIREGINFOW RegInfo;
} WMIP_REGISTER_GUIDS, *PWMIP_REGISTER_GUIDS;

typedef struct _WMIP_RESULT
{
    HANDLE Handle;
    ULONG Unknown04;
    TRACEHANDLE TraceHandle;
    BOOLEAN Unknown10;
} WMIP_RESULT, *PWMIP_RESULT;

typedef struct _WMI_UNREGISTER_GUID
{
    GUID Guid;
    ULONG Unknown10;
    ULONG Unknown14;
    ULONG Unknown18;
    ULONG Unknown1C;
} WMI_UNREGISTER_GUID, *PWMI_UNREGISTER_GUID;

typedef struct _WMI_GUID_OBJECT_ENTRY
{
    HANDLE Handle;
    ULONG Unknown04;
} WMI_GUID_OBJECT_ENTRY, *PWMI_GUID_OBJECT_ENTRY;

typedef struct _WMI_NOTIFICATION
{
    ULONG NumberOfGuidObjects;
    ULONG Unknown04;
    ULONG Unknown08;
    ULONG Unknown0C;
    ULONG Unknown10;
    ULONG Unknown14;
    WMI_GUID_OBJECT_ENTRY GuidObjects[0];
} WMI_NOTIFICATION, *PWMI_NOTIFICATION;

typedef struct _WMI_SET_MARK
{
    ULONG Flags;
    WCHAR Mark[1];
} WMI_SET_MARK, *PWMI_SET_MARK;

PDEVICE_OBJECT WmipServiceDeviceObject;
PDEVICE_OBJECT WmipAdminDeviceObject;
FAST_IO_DISPATCH WmipFastIoDispatch;


/* FUNCTIONS *****************************************************************/

DRIVER_DISPATCH WmipOpenCloseCleanup;
DRIVER_DISPATCH WmipIoControl;
DRIVER_DISPATCH WmipSystemControl;
DRIVER_DISPATCH WmipShutdown;

NTSTATUS
NTAPI
WmipOpenCloseCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAGED_CODE();

    /* No work to do, just return success */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmiTraceEvent(
    PVOID InputBuffer,
    KPROCESSOR_MODE PreviousMode)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmiTraceUserMessage(
    PVOID InputBuffer,
    ULONG InputBufferLength)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipCaptureGuidObjectAttributes(
    _In_ POBJECT_ATTRIBUTES GuidObjectAttributes,
    _Out_ POBJECT_ATTRIBUTES CapuredObjectAttributes,
    _Out_ PUNICODE_STRING CapturedObjectName,
    _Out_ PWSTR ObjectNameBuffer,
    _In_ KPROCESSOR_MODE AccessMode)
{
    ASSERT(AccessMode != KernelMode);

    _SEH2_TRY
    {
        /* Probe and copy the object attributes structure */
        ProbeForRead(GuidObjectAttributes,
                     sizeof(OBJECT_ATTRIBUTES),
                     sizeof(PVOID));
        *CapuredObjectAttributes = *GuidObjectAttributes;

        /* Probe and copy the object name UNICODE_STRING */
        ProbeForRead(CapuredObjectAttributes->ObjectName,
                     sizeof(UNICODE_STRING),
                     sizeof(PVOID));
        *CapturedObjectName = *CapuredObjectAttributes->ObjectName;

        /* Check if the object name has the expected length */
        if (CapturedObjectName->Length != 45 * sizeof(WCHAR))
        {
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
        }

        /* Probe and copy the object name buffer */
        ProbeForRead(CapturedObjectName->Buffer,
                     CapturedObjectName->Length,
                     sizeof(WCHAR));
        RtlCopyMemory(ObjectNameBuffer,
                      CapturedObjectName->Buffer,
                      CapturedObjectName->Length);

        /* Fix pointers */
        CapturedObjectName->Buffer = ObjectNameBuffer;
        CapuredObjectAttributes->ObjectName = CapturedObjectName;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("Got exception!\n");
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

static
BOOLEAN
WmipIs32BitCaller(
    _In_ PIRP Irp)
{
    return sizeof(PVOID) != sizeof(ULONG) &&
           Irp->RequestorMode != KernelMode &&
           PsGetCurrentProcessWow64Process() != NULL;
}

static
NTSTATUS
WmipCaptureGuidObjectAttributes32(
    _In_ ULONG GuidObjectAttributes,
    _Out_ POBJECT_ATTRIBUTES CapturedObjectAttributes,
    _Out_ PUNICODE_STRING CapturedObjectName,
    _Out_ PWSTR ObjectNameBuffer)
{
    OBJECT_ATTRIBUTES32 Attributes32;
    UNICODE_STRING32 Name32;

    _SEH2_TRY
    {
        ProbeForRead(ULongToPtr(GuidObjectAttributes), sizeof(OBJECT_ATTRIBUTES32), sizeof(ULONG));
        Attributes32 = *(POBJECT_ATTRIBUTES32)ULongToPtr(GuidObjectAttributes);
        ProbeForRead(ULongToPtr(Attributes32.ObjectName), sizeof(UNICODE_STRING32), sizeof(ULONG));
        Name32 = *(PUNICODE_STRING32)ULongToPtr(Attributes32.ObjectName);
        if (Name32.Length != 45 * sizeof(WCHAR))
        {
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
        }
        ProbeForRead(ULongToPtr(Name32.Buffer), Name32.Length, sizeof(WCHAR));
        RtlCopyMemory(ObjectNameBuffer, ULongToPtr(Name32.Buffer), Name32.Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    CapturedObjectName->Length = Name32.Length;
    CapturedObjectName->MaximumLength = Name32.Length;
    CapturedObjectName->Buffer = ObjectNameBuffer;
    InitializeObjectAttributes(CapturedObjectAttributes,
                               CapturedObjectName,
                               Attributes32.Attributes,
                               ULongToHandle(Attributes32.RootDirectory),
                               ULongToPtr(Attributes32.SecurityDescriptor));
    CapturedObjectAttributes->SecurityQualityOfService = ULongToPtr(Attributes32.SecurityQualityOfService);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipRegisterGuids(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID Buffer,
    _In_ ULONG InputLength,
    _Inout_ PULONG OutputLength)
{
    PWMIP_REGISTER_GUIDS RegisterGuids = (PWMIP_REGISTER_GUIDS)Buffer;
    PWMIP_RESULT Result = (PWMIP_RESULT)Buffer;
    OBJECT_ATTRIBUTES LocalObjectAttributes;
    UNICODE_STRING LocalObjectName;
    WCHAR LocalObjectNameBuffer[45 + 1];
    KPROCESSOR_MODE PreviousMode;
    HANDLE GuidObjectHandle;
    PVOID GuidObject;
    NTSTATUS Status;

    /* Make sure the input buffer is large enough */
    if ((InputLength < sizeof(WMIP_REGISTER_GUIDS)) ||
        (RegisterGuids->RegInfo.BufferSize >
         (InputLength - FIELD_OFFSET(WMIP_REGISTER_GUIDS, RegInfo))))
    {
        return STATUS_UNSUCCESSFUL;
    }

    /* Make sure we have a resonable GUID count */
    if ((RegisterGuids->RegInfo.GuidCount == 0) ||
        (RegisterGuids->RegInfo.GuidCount > 0x10000))
    {
        return STATUS_UNSUCCESSFUL;
    }

    /* Capture object attributes */
    PreviousMode = ExGetPreviousMode();
    Status = WmipCaptureGuidObjectAttributes(RegisterGuids->ObjectAttributes,
                                             &LocalObjectAttributes,
                                             &LocalObjectName,
                                             LocalObjectNameBuffer,
                                             PreviousMode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipCaptureGuidObjectAttributes failed: 0x%lx\n", Status);
        return Status;
    }

    /* Open a new GUID object */
    Status = WmipOpenGuidObjectByName(&LocalObjectAttributes,
                                      SPECIFIC_RIGHTS_ALL,
                                      PreviousMode,
                                      &GuidObjectHandle,
                                      &GuidObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipOpenGuidObjectByName failed: 0x%lx\n", Status);
        return Status;
    }

    /* Dereference the GUID object */
    ObDereferenceObject(GuidObject);

    /* Return the handle (user mode will close it) */
    Result->Handle = GuidObjectHandle;
    Result->TraceHandle = 0;
    *OutputLength = 24;

    return STATUS_SUCCESS;
}


static
NTSTATUS
WmipUnregisterGuids(
    _In_ PVOID Buffer,
    _In_ ULONG InputLength,
    _Inout_ PULONG OutputLength)
{
    /* For now we have nothing to do */
    return STATUS_SUCCESS;
}

VOID
NTAPI
WmipClearIrpObjectList(
    _In_ PIRP Irp)
{
    PWMIP_IRP_CONTEXT IrpContext;
    PLIST_ENTRY ListEntry;
    PWMIP_GUID_OBJECT GuidObject;

    /* Get the IRP context */
    IrpContext = (PWMIP_IRP_CONTEXT)Irp->Tail.Overlay.DriverContext;

    /* Loop all GUID objects attached to this IRP */
    for (ListEntry = IrpContext->GuidObjectListHead.Flink;
         ListEntry != &IrpContext->GuidObjectListHead;
         ListEntry = ListEntry->Flink)
    {
        /* Get the GUID object */
        GuidObject = CONTAINING_RECORD(ListEntry, WMIP_GUID_OBJECT, IrpLink);

        /* Make sure the IRP matches and clear it */
        ASSERT(GuidObject->Irp == Irp);
        GuidObject->Irp = NULL;

        /* Remove the entry */
        RemoveEntryList(ListEntry);
    }
}

VOID
NTAPI
WmipNotificationIrpCancel(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    /* Clear the list */
    WmipClearIrpObjectList(Irp);

    /* Release the cancel spin lock */
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    /* Set the status to cancelled and complete the IRP */
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
WmipInitializeIrpContext(
    PWMIP_IRP_CONTEXT IrpContext)
{
    /* Initialize the list head for GUID objects */
    InitializeListHead(&IrpContext->GuidObjectListHead);
}

static
NTSTATUS
WmipReceiveNotifications(
    _Inout_ PIRP Irp,
    _In_ PVOID Buffer,
    _In_ ULONG InputLength,
    _Inout_ PULONG OutputLength)
{
    PWMI_NOTIFICATION Notification;
    PWMIP_IRP_CONTEXT IrpContext;
    NTSTATUS Status;

    //__debugbreak();
    if ((InputLength < sizeof(WMI_NOTIFICATION)) || (*OutputLength < 0x38))
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    /// FIXME: For now we don't do any actual work, but simply pretend we are
    /// waiting for notifications. We won't ever deliver any though.
    Notification = (PWMI_NOTIFICATION)Buffer;
    DBG_UNREFERENCED_LOCAL_VARIABLE(Notification);

    // loop all objects
        // reference the object
            // on failure, fail the whole request

    // loop all objects
        // update the irp (synchronization!)
            // if we had one before complete the old irp with an error

    /* Get the IRP context and initialize it */
    IrpContext = (PWMIP_IRP_CONTEXT)Irp->Tail.Overlay.DriverContext;
    WmipInitializeIrpContext(IrpContext);

    // loop all objects
        // insert the objects into the IRP list

    /* Set our cancel routine for cleanup */
    IoSetCancelRoutine(Irp, WmipNotificationIrpCancel);

    /* Check if the IRP is already being cancelled */
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        Status = STATUS_CANCELLED;
    }
    else
    {
        /* Mark the IRP as pending */
        IoMarkIrpPending(Irp);
        Status = STATUS_PENDING;
    }

    return Status;
}

typedef struct _WMI_OPEN_GUID_FOR_EVENTS
{
    POBJECT_ATTRIBUTES ObjectAttributes;
    ACCESS_MASK DesiredAccess;
    ULONG Unknown08;
    ULONG Unknown0C;
} WMI_OPEN_GUID_FOR_EVENTS, *PWMI_OPEN_GUID_FOR_EVENTS;

typedef struct _WMIP_RESULT2
{
    ULONG Unknown00;
    ULONG Unknown04;
    HANDLE Handle;
    ULONG Unknown0C;
} WMIP_RESULT2, *PWMIP_RESULT2;

typedef struct _WMI_OPEN_GUID_FOR_EVENTS32
{
    ULONG ObjectAttributes;
    ACCESS_MASK DesiredAccess;
    ULONG Unknown08;
    ULONG Unknown0C;
} WMI_OPEN_GUID_FOR_EVENTS32, *PWMI_OPEN_GUID_FOR_EVENTS32;

typedef struct _WMIP_RESULT2_32
{
    ULONG Unknown00;
    ULONG Unknown04;
    ULONG Handle;
    ULONG Unknown0C;
} WMIP_RESULT2_32, *PWMIP_RESULT2_32;

static
NTSTATUS
WmipOpenGuidForEvents(
    PVOID Buffer,
    ULONG InputLength,
    PULONG OutputLength,
    BOOLEAN Is32Bit)
{
    PWMI_OPEN_GUID_FOR_EVENTS OpenGuidForEvents = Buffer;
    PWMI_OPEN_GUID_FOR_EVENTS32 OpenGuidForEvents32 = Buffer;
    PWMIP_RESULT2 Result = (PWMIP_RESULT2)Buffer;
    OBJECT_ATTRIBUTES LocalObjectAttributes;
    UNICODE_STRING LocalObjectName;
    WCHAR LocalObjectNameBuffer[45 + 1];
    KPROCESSOR_MODE PreviousMode;
    ACCESS_MASK DesiredAccess;
    HANDLE GuidObjectHandle;
    PVOID GuidObject;
    NTSTATUS Status;

    PreviousMode = ExGetPreviousMode();
    if (Is32Bit)
    {
        if ((InputLength != sizeof(WMI_OPEN_GUID_FOR_EVENTS32)) ||
            (*OutputLength != sizeof(WMIP_RESULT2_32)))
        {
            return STATUS_UNSUCCESSFUL;
        }
        DesiredAccess = OpenGuidForEvents32->DesiredAccess;
        Status = WmipCaptureGuidObjectAttributes32(OpenGuidForEvents32->ObjectAttributes,
                                                   &LocalObjectAttributes,
                                                   &LocalObjectName,
                                                   LocalObjectNameBuffer);
    }
    else
    {
        if ((InputLength != sizeof(WMI_OPEN_GUID_FOR_EVENTS)) ||
            (*OutputLength != sizeof(WMIP_RESULT2)))
        {
            return STATUS_UNSUCCESSFUL;
        }
        DesiredAccess = OpenGuidForEvents->DesiredAccess;
        Status = WmipCaptureGuidObjectAttributes(OpenGuidForEvents->ObjectAttributes,
                                                 &LocalObjectAttributes,
                                                 &LocalObjectName,
                                                 LocalObjectNameBuffer,
                                                 PreviousMode);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ProbeAndCaptureGuidObjectAttributes failed: 0x%lx\n", Status);
        return Status;
    }

    /* Open a new GUID object */
    Status = WmipOpenGuidObjectByName(&LocalObjectAttributes,
                                      DesiredAccess,
                                      PreviousMode,
                                      &GuidObjectHandle,
                                      &GuidObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipOpenGuidObjectByName failed: 0x%lx\n", Status);
        return Status;
    }

    if (Is32Bit)
        ((PWMIP_RESULT2_32)Buffer)->Handle = HandleToUlong(GuidObjectHandle);
    else
        Result->Handle = GuidObjectHandle;

    ObDereferenceObject(GuidObject);

    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipReferenceWnodeObject(
    _In_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG MinimumLength,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PVOID *GuidObject)
{
    PWNODE_HEADER Header = Buffer;

    *GuidObject = NULL;
    if (InputLength < MinimumLength || Header->BufferSize < MinimumLength || Header->BufferSize > InputLength)
        return STATUS_INVALID_PARAMETER;
    return ObReferenceObjectByHandle(Header->KernelHandle,
                                     DesiredAccess,
                                     WmipGuidObjectType,
                                     ExGetPreviousMode(),
                                     GuidObject,
                                     NULL);
}

static
NTSTATUS
WmipCaptureWnodeInstanceName(
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ ULONG Offset,
    _Out_ PUNICODE_STRING Name)
{
    USHORT NameLength;

    RtlZeroMemory(Name, sizeof(*Name));
    if (Offset < sizeof(WNODE_HEADER) || Offset > Length - sizeof(USHORT))
        return STATUS_INVALID_PARAMETER;
    NameLength = *(USHORT UNALIGNED *)((PUCHAR)Buffer + Offset);
    if ((NameLength & 1) != 0 || NameLength > Length - Offset - sizeof(USHORT))
        return STATUS_INVALID_PARAMETER;
    Name->Buffer = ExAllocatePoolWithTag(PagedPool, NameLength + sizeof(WCHAR), 'nImW');
    if (Name->Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Name->Buffer, (PUCHAR)Buffer + Offset + sizeof(USHORT), NameLength);
    Name->Buffer[NameLength / sizeof(WCHAR)] = UNICODE_NULL;
    Name->Length = NameLength;
    Name->MaximumLength = NameLength + sizeof(WCHAR);
    return STATUS_SUCCESS;
}

static
BOOLEAN
WmipWnodeRangeValid(
    _In_ ULONG Length,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    return Size == 0 || (Offset >= sizeof(WNODE_HEADER) && Offset <= Length && Size <= Length - Offset);
}

static
NTSTATUS
WmipFinishQuery(
    _In_ NTSTATUS Status,
    _In_ ULONG Needed,
    _Out_writes_bytes_(OutputLength) PVOID Buffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG Information)
{
    *Information = 0;
    if (NT_SUCCESS(Status))
    {
        *Information = Needed;
        return Status;
    }
    if (Status != STATUS_BUFFER_TOO_SMALL || OutputLength < sizeof(ULONG))
        return Status;
    *(PULONG)Buffer = Needed;
    *Information = sizeof(ULONG);
    return STATUS_BUFFER_OVERFLOW;
}

static
NTSTATUS
WmipIoctlQueryAllData(
    _Inout_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG Information)
{
    PVOID GuidObject;
    ULONG Size = OutputLength;
    NTSTATUS Status;

    *Information = 0;
    Status = WmipReferenceWnodeObject(Buffer, InputLength, sizeof(WNODE_HEADER), WMIGUID_QUERY, &GuidObject);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IoWMIQueryAllData(GuidObject, &Size, OutputLength ? Buffer : NULL);
    ObDereferenceObject(GuidObject);
    return WmipFinishQuery(Status, Size, Buffer, OutputLength, Information);
}

static
NTSTATUS
WmipIoctlQuerySingleInstance(
    _Inout_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG Information)
{
    PWNODE_SINGLE_INSTANCE Wnode = Buffer;
    UNICODE_STRING Name;
    PVOID GuidObject;
    ULONG Size = OutputLength;
    NTSTATUS Status;

    *Information = 0;
    Status = WmipReferenceWnodeObject(Buffer, InputLength, sizeof(WNODE_SINGLE_INSTANCE), WMIGUID_QUERY, &GuidObject);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WmipCaptureWnodeInstanceName(Buffer, Wnode->WnodeHeader.BufferSize, Wnode->OffsetInstanceName, &Name);
    if (NT_SUCCESS(Status))
    {
        Status = IoWMIQuerySingleInstance(GuidObject, &Name, &Size, OutputLength ? Buffer : NULL);
        ExFreePoolWithTag(Name.Buffer, 'nImW');
    }
    ObDereferenceObject(GuidObject);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    return WmipFinishQuery(Status, Size, Buffer, OutputLength, Information);
}

static
NTSTATUS
WmipIoctlSetSingleInstance(
    _In_ PVOID Buffer,
    _In_ ULONG InputLength,
    _Out_ PULONG Information)
{
    PWNODE_SINGLE_INSTANCE Wnode = Buffer;
    UNICODE_STRING Name;
    PVOID GuidObject;
    NTSTATUS Status;

    *Information = 0;
    Status = WmipReferenceWnodeObject(Buffer, InputLength, sizeof(WNODE_SINGLE_INSTANCE), WMIGUID_SET, &GuidObject);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!WmipWnodeRangeValid(Wnode->WnodeHeader.BufferSize, Wnode->DataBlockOffset, Wnode->SizeDataBlock))
        Status = STATUS_INVALID_PARAMETER;
    else
        Status = WmipCaptureWnodeInstanceName(Buffer, Wnode->WnodeHeader.BufferSize, Wnode->OffsetInstanceName, &Name);
    if (NT_SUCCESS(Status))
    {
        Status = IoWMISetSingleInstance(GuidObject, &Name, Wnode->WnodeHeader.Version, Wnode->SizeDataBlock,
                                        (PUCHAR)Buffer + Wnode->DataBlockOffset);
        ExFreePoolWithTag(Name.Buffer, 'nImW');
    }
    ObDereferenceObject(GuidObject);
    return Status;
}

static
NTSTATUS
WmipIoctlSetSingleItem(
    _In_ PVOID Buffer,
    _In_ ULONG InputLength,
    _Out_ PULONG Information)
{
    PWNODE_SINGLE_ITEM Wnode = Buffer;
    UNICODE_STRING Name;
    PVOID GuidObject;
    NTSTATUS Status;

    *Information = 0;
    Status = WmipReferenceWnodeObject(Buffer, InputLength, sizeof(WNODE_SINGLE_ITEM), WMIGUID_SET, &GuidObject);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!WmipWnodeRangeValid(Wnode->WnodeHeader.BufferSize, Wnode->DataBlockOffset, Wnode->SizeDataItem))
        Status = STATUS_INVALID_PARAMETER;
    else
        Status = WmipCaptureWnodeInstanceName(Buffer, Wnode->WnodeHeader.BufferSize, Wnode->OffsetInstanceName, &Name);
    if (NT_SUCCESS(Status))
    {
        Status = IoWMISetSingleItem(GuidObject, &Name, Wnode->ItemId, Wnode->WnodeHeader.Version, Wnode->SizeDataItem,
                                    (PUCHAR)Buffer + Wnode->DataBlockOffset);
        ExFreePoolWithTag(Name.Buffer, 'nImW');
    }
    ObDereferenceObject(GuidObject);
    return Status;
}

static
NTSTATUS
WmipIoctlExecuteMethod(
    _Inout_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG Information)
{
    PWNODE_METHOD_ITEM Wnode = Buffer;
    UNICODE_STRING Name;
    PVOID GuidObject;
    PUCHAR Data = NULL;
    ULONG InSize = 0, OutSize = OutputLength, Capacity;
    NTSTATUS Status;

    *Information = 0;
    Status = WmipReferenceWnodeObject(Buffer, InputLength, sizeof(WNODE_METHOD_ITEM), WMIGUID_EXECUTE, &GuidObject);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!WmipWnodeRangeValid(Wnode->WnodeHeader.BufferSize, Wnode->DataBlockOffset, Wnode->SizeDataBlock))
        Status = STATUS_INVALID_PARAMETER;
    else
        Status = WmipCaptureWnodeInstanceName(Buffer, Wnode->WnodeHeader.BufferSize, Wnode->OffsetInstanceName, &Name);
    if (NT_SUCCESS(Status))
    {
        InSize = Wnode->SizeDataBlock;
        Capacity = max(InSize, OutputLength);
        if (Capacity != 0)
        {
            Data = ExAllocatePoolWithTag(PagedPool, Capacity, 'mImW');
            if (Data == NULL)
                Status = STATUS_INSUFFICIENT_RESOURCES;
            else if (InSize != 0)
                RtlCopyMemory(Data, (PUCHAR)Buffer + Wnode->DataBlockOffset, InSize);
        }
        if (NT_SUCCESS(Status))
            Status = IoWMIExecuteMethod(GuidObject, &Name, Wnode->MethodId, InSize, &OutSize, Data);
        if (NT_SUCCESS(Status) && OutSize != 0)
            RtlCopyMemory(Buffer, Data, OutSize);
        if (Data != NULL)
            ExFreePoolWithTag(Data, 'mImW');
        ExFreePoolWithTag(Name.Buffer, 'nImW');
    }
    ObDereferenceObject(GuidObject);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    return WmipFinishQuery(Status, OutSize, Buffer, OutputLength, Information);
}

static
NTSTATUS
WmipOpenBlock(
    PVOID Buffer,
    ULONG InputLength,
    PULONG OutputLength,
    BOOLEAN Is32Bit)
{
    PWMIP_RESULT2 Result = (PWMIP_RESULT2)Buffer;
    PWMIP_GUID_OBJECT GuidObject;
    HANDLE Handle;
    NTSTATUS Status;

    Status = WmipOpenGuidForEvents(Buffer, InputLength, OutputLength, Is32Bit);
    if (!NT_SUCCESS(Status))
        return Status;
    Handle = Is32Bit ? ULongToHandle(((PWMIP_RESULT2_32)Buffer)->Handle) : Result->Handle;
    Status = ObReferenceObjectByHandle(Handle, 0, WmipGuidObjectType, ExGetPreviousMode(), (PVOID *)&GuidObject, NULL);
    if (NT_SUCCESS(Status))
    {
        if (!WmipIsDataBlockRegistered(&GuidObject->Guid))
            Status = STATUS_WMI_GUID_NOT_FOUND;
        ObDereferenceObject(GuidObject);
    }
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, ExGetPreviousMode());
        if (Is32Bit)
            ((PWMIP_RESULT2_32)Buffer)->Handle = 0;
        else
            Result->Handle = NULL;
    }
    return Status;
}

static
NTSTATUS
WmiSetMark(
    PWMI_SET_MARK Buffer,
    ULONG Length)
{
    UNIMPLEMENTED;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
WmipIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStackLocation;
    ULONG IoControlCode;
    PVOID Buffer;
    ULONG InputLength, OutputLength;
    NTSTATUS Status;
    PAGED_CODE();

    /* Get the current stack location */
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);

    /* Get the io control parameters */
    IoControlCode = IoStackLocation->Parameters.DeviceIoControl.IoControlCode;
    Buffer = Irp->AssociatedIrp.SystemBuffer;
    InputLength = IoStackLocation->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = IoStackLocation->Parameters.DeviceIoControl.OutputBufferLength;

    switch (IoControlCode)
    {

        case IOCTL_WMI_REGISTER_GUIDS:
        {
            Status = WmipRegisterGuids(DeviceObject,
                                       Buffer,
                                       InputLength,
                                       &OutputLength);
            break;
        }

        case IOCTL_WMI_UNREGISTER_GUIDS:
        {
            Status = WmipUnregisterGuids(Buffer,
                                         InputLength,
                                         &OutputLength);
            break;
        }

        case IOCTL_WMI_RECEIVE_NOTIFICATIONS:
        {
            Status = WmipReceiveNotifications(Irp,
                                              Buffer,
                                              InputLength,
                                              &OutputLength);
            break;
        }

        case IOCTL_WMI_QUERY_ALL_DATA:
        {
            Status = WmipIoctlQueryAllData(Buffer, InputLength, OutputLength, &OutputLength);
            break;
        }

        case IOCTL_WMI_SINGLE_INSTANCE:
        {
            Status = WmipIoctlQuerySingleInstance(Buffer, InputLength, OutputLength, &OutputLength);
            break;
        }

        case IOCTL_WMI_SET_SINGLE_INSTANCE:
        {
            Status = WmipIoctlSetSingleInstance(Buffer, InputLength, &OutputLength);
            break;
        }

        case IOCTL_WMI_SET_SINGLE_ITEM:
        {
            Status = WmipIoctlSetSingleItem(Buffer, InputLength, &OutputLength);
            break;
        }

        case IOCTL_WMI_EXECUTE_METHOD:
        {
            Status = WmipIoctlExecuteMethod(Buffer, InputLength, OutputLength, &OutputLength);
            break;
        }

        case 0x228168:
        {
            DPRINT1("IOCTL 0x228168 is unimplemented, ignoring\n");
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_WMI_OPEN_BLOCK:
        {
            Status = WmipOpenBlock(Buffer, InputLength, &OutputLength, WmipIs32BitCaller(Irp));
            break;
        }

        case IOCTL_WMI_OPEN_GUID_FOR_EVENTS:
        {
            Status = WmipOpenGuidForEvents(Buffer, InputLength, &OutputLength, WmipIs32BitCaller(Irp));
            break;
        }

        case IOCTL_WMI_SET_MARK:
        {
            if (InputLength < FIELD_OFFSET(WMI_SET_MARK, Mark))
            {
                Status = STATUS_UNSUCCESSFUL;
                break;
            }

            Status = WmiSetMark(Buffer, InputLength);
            break;
        }

        default:
            DPRINT1("Unsupported yet IOCTL: 0x%lx\n", IoControlCode);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    if (Status == STATUS_PENDING)
        return Status;

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = !NT_ERROR(Status) ? OutputLength : 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
WmipSystemControl(
  _Inout_  PDEVICE_OBJECT DeviceObject,
  _Inout_  PIRP Irp)
{
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
WmipShutdown(
  _Inout_  PDEVICE_OBJECT DeviceObject,
  _Inout_  PIRP Irp)
{
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

_Function_class_(FAST_IO_DEVICE_CONTROL)
_IRQL_requires_same_
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
    PAGED_CODE();

    if (IoControlCode == IOCTL_WMI_TRACE_EVENT)
    {
        if (InputBufferLength < 0x30)
        {
            DPRINT1("Buffer too small\n");
            return FALSE;
        }

        IoStatus->Status = WmiTraceEvent(InputBuffer, ExGetPreviousMode());
        return TRUE;
    }
    else if (IoControlCode == IOCTL_WMI_TRACE_USER_MESSAGE)
    {
        if (InputBufferLength < 0x30)
        {
            DPRINT1("Buffer too small\n");
            return FALSE;
        }

        IoStatus->Status = WmiTraceUserMessage(InputBuffer, InputBufferLength);
        return TRUE;
    }

    DPRINT1("Invalid io control code for fast dispatch: 0x%lx\n", IoControlCode);
    return FALSE;
}

NTSTATUS
NTAPI
WmipDockUndockEventCallback(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
NTSTATUS
NTAPI
WmipDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    static UNICODE_STRING ServiceDeviceName = RTL_CONSTANT_STRING(L"\\Device\\WMIDataDevice");
    static UNICODE_STRING ServiceDosDeviceName = RTL_CONSTANT_STRING(L"\\DosDevices\\WMIDataDevice");
    static UNICODE_STRING AdminDeviceName = RTL_CONSTANT_STRING(L"\\Device\\WMIAdminDevice");
    static UNICODE_STRING AdminDosDeviceName = RTL_CONSTANT_STRING(L"\\DosDevices\\WMIAdminDevice");
    NTSTATUS Status;
    PAGED_CODE();

    /* Create the service device object */
    Status = IoCreateDevice(DriverObject,
                            0,
                            &ServiceDeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            0,
                            &WmipServiceDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create service device: 0x%lx\n", Status);
        return Status;
    }

    /* Create a symbolic link for the service device */
    Status = IoCreateSymbolicLink(&ServiceDosDeviceName, &ServiceDeviceName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateSymbolicLink() failed: 0x%lx\n", Status);
        IoDeleteDevice(WmipServiceDeviceObject);
        return Status;
    }

    /* Create the admin device object */
    Status = IoCreateDevice(DriverObject,
                            0,
                            &AdminDeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            0,
                            &WmipAdminDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create admin device: 0x%lx\n", Status);
        IoDeleteDevice(WmipServiceDeviceObject);
        IoDeleteSymbolicLink(&ServiceDosDeviceName);
        return Status;
    }

    /* Create a symbolic link for the admin device */
    Status = IoCreateSymbolicLink(&AdminDosDeviceName, &AdminDeviceName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateSymbolicLink() failed: 0x%lx\n", Status);
        IoDeleteSymbolicLink(&ServiceDosDeviceName);
        IoDeleteDevice(WmipServiceDeviceObject);
        IoDeleteDevice(WmipAdminDeviceObject);
        return Status;
    }

    /* Initialize dispatch routines */
    DriverObject->MajorFunction[IRP_MJ_CREATE] = WmipOpenCloseCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = WmipOpenCloseCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = WmipIoControl;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = WmipOpenCloseCleanup;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = WmipSystemControl;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = WmipShutdown;

    /* Initialize fast dispatch */
    RtlZeroMemory(&WmipFastIoDispatch, sizeof(WmipFastIoDispatch));
    WmipFastIoDispatch.SizeOfFastIoDispatch = sizeof(WmipFastIoDispatch);
    WmipFastIoDispatch.FastIoDeviceControl = WmipFastIoDeviceControl;
    DriverObject->FastIoDispatch = &WmipFastIoDispatch;

    /* Register the WMI service device */
    IoWMIRegistrationControl(WmipServiceDeviceObject, WMIREG_ACTION_REGISTER);

    /* Register a shutdown notification */
    IoRegisterShutdownNotification(WmipServiceDeviceObject);

    /* Initialization is done */
    WmipServiceDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    WmipAdminDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}


