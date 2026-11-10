/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The device classes of NtPlugPlayControl the user-mode Plug and Play service drives
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <cfg.h>
#define NDEBUG
#include <debug.h>

/* MAX_DEVICE_ID_LEN of cfgmgr32.h, the terminator included */
#define PI_MAX_DEVICE_ID_CHARS 200

/* CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES of cfgmgr32.h */
#define PI_INTERFACE_LIST_ALL_DEVICES 0x00000001

/* The relation numbers the service sends for CM_GETIDLIST_FILTER_*RELATIONS */
#define PI_RELATIONS_EJECT   0
#define PI_RELATIONS_REMOVAL 1
#define PI_RELATIONS_POWER   2
#define PI_RELATIONS_BUS     3

/* No property the service reads needs more than this */
#define PI_MAX_PROPERTY_BYTES 0x10000

/* CAPTURE AND RETURN *********************************************************/

static
NTSTATUS
PiCaptureUnicodeString(
    _In_ PCUNICODE_STRING Source,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ USHORT MaximumLength,
    _Out_ PUNICODE_STRING Captured)
{
    UNICODE_STRING Local = *Source;
    NTSTATUS Status = STATUS_SUCCESS;
    PWCHAR Buffer;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(Captured, NULL, 0);

    if ((Local.Length == 0) || (Local.Length & 1) ||
        (Local.Length > MaximumLength) || (Local.Buffer == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, Local.Length + sizeof(WCHAR), TAG_IO);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(Local.Buffer, Local.Length, sizeof(WCHAR));

        RtlCopyMemory(Buffer, Local.Buffer, Local.Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_IO);
        return Status;
    }

    Buffer[Local.Length / sizeof(WCHAR)] = UNICODE_NULL;

    Captured->Buffer = Buffer;
    Captured->Length = Local.Length;
    Captured->MaximumLength = Local.Length + sizeof(WCHAR);

    return STATUS_SUCCESS;
}

static
VOID
PiFreeCapturedString(
    _Inout_ PUNICODE_STRING Captured)
{
    if (Captured->Buffer != NULL) ExFreePoolWithTag(Captured->Buffer, TAG_IO);
    RtlInitEmptyUnicodeString(Captured, NULL, 0);
}

static
NTSTATUS
PiCaptureGuid(
    _In_opt_ const GUID *Source,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ GUID *Captured)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Source == NULL) return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead((PVOID)Source, sizeof(GUID), sizeof(UCHAR));

        *Captured = *Source;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
PiCopyOut(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Length == 0) return STATUS_SUCCESS;
    if (Destination == NULL) return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForWrite(Destination, Length, sizeof(UCHAR));

        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* Writes Text and its terminator; Length is in characters, the terminator counted */
static
NTSTATUS
PiReturnString(
    _In_ PCUNICODE_STRING Text,
    _Out_writes_opt_(*Length) PWCHAR Destination,
    _Inout_ PULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Needed = Text->Length / sizeof(WCHAR) + 1;
    WCHAR Terminator = UNICODE_NULL;
    NTSTATUS Status;

    if ((Destination == NULL) || (*Length < Needed))
    {
        *Length = Needed;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = PiCopyOut(Destination, Text->Buffer, Text->Length, PreviousMode);
    if (NT_SUCCESS(Status))
    {
        Status = PiCopyOut(Destination + Needed - 1,
                           &Terminator,
                           sizeof(Terminator),
                           PreviousMode);
    }

    if (NT_SUCCESS(Status)) *Length = Needed;

    return Status;
}

/* The device a request names; the caller drops the reference */
static
NTSTATUS
PiReferenceDeviceByInstance(
    _In_ PCUNICODE_STRING Instance,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    UNICODE_STRING Captured;
    NTSTATUS Status;

    *DeviceObject = NULL;

    Status = PiCaptureUnicodeString(Instance,
                                    PreviousMode,
                                    (PI_MAX_DEVICE_ID_CHARS - 1) * sizeof(WCHAR),
                                    &Captured);
    if (!NT_SUCCESS(Status)) return Status;

    *DeviceObject = IopGetDeviceObjectFromDeviceInstance(&Captured);
    PiFreeCapturedString(&Captured);

    if (*DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    ASSERT(IopGetDeviceNode(*DeviceObject) != NULL);
    return STATUS_SUCCESS;
}

/* STATUS *********************************************************************/

static
BOOLEAN
PiIsDevNodeStarted(
    _In_ PNP_DEVNODE_STATE State)
{
    switch (State)
    {
        case DeviceNodeStartPostWork:
        case DeviceNodeStarted:
        case DeviceNodeQueryStopped:
        case DeviceNodeRestartCompletion:
        case DeviceNodeEnumeratePending:
        case DeviceNodeEnumerateCompletion:
            return TRUE;

        default:
            return FALSE;
    }
}

/* The DN_ flags of cfg.h a device instance reports */
VOID
PiQueryDevNodeStatus(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PULONG DeviceStatus,
    _Out_ PULONG DeviceProblem)
{
    PNP_DEVNODE_STATE State;
    ULONG Flags, UserFlags, Problem;
    BOOLEAN RootEnumerated;
    UCHAR RemovalPolicy;
    ULONG Status;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    State = DeviceNode->State;
    Flags = DeviceNode->Flags;
    UserFlags = DeviceNode->UserFlags;
    Problem = DeviceNode->Problem;
    RemovalPolicy = DeviceNode->RemovalPolicy;
    RootEnumerated = (DeviceNode->Parent == IopRootDeviceNode);
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    Status = DN_NT_ENUMERATOR | DN_NT_DRIVER;

    if (RootEnumerated) Status |= DN_ROOT_ENUMERATED;

    if ((State >= DeviceNodeDriversAdded) && (State <= DeviceNodeQueryRemoved))
        Status |= DN_DRIVER_LOADED;

    if (PiIsDevNodeStarted(State)) Status |= DN_STARTED;

    if (Flags & DNF_HAS_PROBLEM) Status |= DN_HAS_PROBLEM;
    if (Flags & DNF_HAS_PRIVATE_PROBLEM) Status |= DN_PRIVATE_PROBLEM;
    if (Flags & DNF_LEGACY_DRIVER) Status |= DN_LEGACY_DRIVER;
    if (Flags & DNF_DRIVER_BLOCKED) Status |= DN_DRIVER_BLOCKED;
    if (Flags & DNF_CHILD_WITH_INVALID_ID) Status |= DN_CHILD_WITH_INVALID_ID;

    if ((DeviceNode != IopRootDeviceNode) && !(UserFlags & DNUF_NOT_DISABLEABLE))
        Status |= DN_DISABLEABLE;

    if ((RemovalPolicy == RemovalPolicyExpectOrderlyRemoval) ||
        (RemovalPolicy == RemovalPolicyExpectSurpriseRemoval))
    {
        Status |= DN_REMOVABLE;
    }

    if (UserFlags & DNUF_WILL_BE_REMOVED) Status |= DN_WILL_BE_REMOVED;
    if (UserFlags & DNUF_NEED_RESTART) Status |= DN_NEED_RESTART;
    if (UserFlags & DNUF_DONT_SHOW_IN_UI) Status |= DN_NO_SHOW_IN_DM;

    *DeviceStatus = Status;
    *DeviceProblem = (Flags & DNF_HAS_PROBLEM) ? Problem : 0;
}

static
NTSTATUS
PiControlDeviceStatus(
    _Inout_ PPLUGPLAY_CONTROL_STATUS_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceNode = IopGetDeviceNode(DeviceObject);

    switch (Data->Operation)
    {
        case PNP_GET_DEVICE_STATUS:
            PiQueryDevNodeStatus(DeviceNode, &Data->DeviceStatus, &Data->DeviceProblem);
            break;

        case PNP_SET_DEVICE_STATUS:
            if (Data->DeviceStatus & DN_HAS_PROBLEM)
                PiSetDevNodeProblem(DeviceNode, Data->DeviceProblem);
            if (Data->DeviceStatus & DN_WILL_BE_REMOVED)
                IopDeviceNodeSetUserFlag(DeviceNode, DNUF_WILL_BE_REMOVED);
            if (Data->DeviceStatus & DN_NEED_RESTART)
                IopDeviceNodeSetUserFlag(DeviceNode, DNUF_NEED_RESTART);
            break;

        case PNP_CLEAR_DEVICE_STATUS:
            /* A problem the caller did not see is not cleared on its behalf */
            if ((Data->DeviceStatus & DN_HAS_PROBLEM) &&
                ((Data->DeviceProblem == 0) || (Data->DeviceProblem == DeviceNode->Problem)))
            {
                PiClearDevNodeProblem(DeviceNode);
            }
            if (Data->DeviceStatus & DN_WILL_BE_REMOVED)
                IopDeviceNodeClearUserFlag(DeviceNode, DNUF_WILL_BE_REMOVED);
            if (Data->DeviceStatus & DN_NEED_RESTART)
                IopDeviceNodeClearUserFlag(DeviceNode, DNUF_NEED_RESTART);
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    ObDereferenceObject(DeviceObject);
    return Status;
}

/* TREE ***********************************************************************/

static
NTSTATUS
PiControlStartDevice(
    _In_ PPLUGPLAY_CONTROL_DEVICE_CONTROL_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * The state machine loads what is now installed for the device and starts
     * it, unless it still has a problem. A device taken down so that its
     * driver could be installed comes back through a fresh initialization.
     */
    Status = PiPerformSyncDeviceAction(DeviceObject,
                                       (IopGetDeviceNode(DeviceObject)->State == DeviceNodeRemoved) ?
                                           PiActionResetDevice : PiActionEnumDeviceTree);

    ObDereferenceObject(DeviceObject);
    return Status;
}

static
NTSTATUS
PiControlInitializeDevice(
    _In_ PPLUGPLAY_CONTROL_DEVICE_CONTROL_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PDEVICE_OBJECT DeviceObject;
    UNICODE_STRING Captured;
    NTSTATUS Status;

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (NT_SUCCESS(Status))
    {
        ObDereferenceObject(DeviceObject);
        return STATUS_SUCCESS;
    }

    if (Status != STATUS_NO_SUCH_DEVICE) return Status;

    /* Only the root enumerator takes a device on the registry's word */
    Status = PiCaptureUnicodeString(&Data->DeviceInstance,
                                    PreviousMode,
                                    (PI_MAX_DEVICE_ID_CHARS - 1) * sizeof(WCHAR),
                                    &Captured);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PnpRootCreateInstance(&Captured);
    PiFreeCapturedString(&Captured);

    if (!NT_SUCCESS(Status)) return Status;

    PiSetDevNodeFlag(IopRootDeviceNode, DNF_REENUMERATE);

    return PiPerformSyncDeviceAction(IopRootDeviceNode->PhysicalDeviceObject,
                                     PiActionEnumDeviceTree);
}

static
NTSTATUS
PiControlDeregisterDevice(
    _In_ PPLUGPLAY_CONTROL_DEVICE_CONTROL_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;
    KIRQL OldIrql;

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceNode = IopGetDeviceNode(DeviceObject);

    /* The registration of a device a driver still serves stays */
    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    if ((DeviceNode->State >= DeviceNodeDriversAdded) &&
        (DeviceNode->State <= DeviceNodeQueryRemoved))
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        DeviceNode->Flags |= DNF_UNINSTALLED;
    }
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    ObDereferenceObject(DeviceObject);
    return Status;
}

static
NTSTATUS
PiControlGetDeviceDepth(
    _Inout_ PPLUGPLAY_CONTROL_DEPTH_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    Data->Depth = IopGetDeviceNode(DeviceObject)->Level;

    ObDereferenceObject(DeviceObject);
    return STATUS_SUCCESS;
}

static
NTSTATUS
PiControlGetRelatedDevice(
    _Inout_ PPLUGPLAY_CONTROL_RELATED_DEVICE_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PDEVICE_OBJECT DeviceObject, RelatedObject = NULL;
    PDEVICE_NODE DeviceNode, Related;
    NTSTATUS Status;
    KIRQL OldIrql;

    if ((Data->Relation != PNP_GET_PARENT_DEVICE) &&
        (Data->Relation != PNP_GET_CHILD_DEVICE) &&
        (Data->Relation != PNP_GET_SIBLING_DEVICE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PiReferenceDeviceByInstance(&Data->TargetDeviceInstance, PreviousMode, &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceNode = IopGetDeviceNode(DeviceObject);

    /* The node found is held by its device object once the tree lock is gone */
    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    Related = (Data->Relation == PNP_GET_PARENT_DEVICE) ? DeviceNode->Parent :
              (Data->Relation == PNP_GET_CHILD_DEVICE) ? DeviceNode->Child :
              DeviceNode->Sibling;
    if ((Related != NULL) && (Related->PhysicalDeviceObject != NULL))
    {
        RelatedObject = Related->PhysicalDeviceObject;
        ObReferenceObject(RelatedObject);
    }
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    ObDereferenceObject(DeviceObject);

    if (RelatedObject == NULL) return STATUS_NO_SUCH_DEVICE;

    Related = IopGetDeviceNode(RelatedObject);
    ASSERT(Related != NULL);

    Status = PiReturnString(&Related->InstancePath,
                            Data->RelatedDeviceInstance,
                            &Data->RelatedDeviceInstanceLength,
                            PreviousMode);

    ObDereferenceObject(RelatedObject);
    return Status;
}

/* Returns the instance paths of the devices in Objects as one multi-string, in bytes */
static
NTSTATUS
PiReturnInstanceList(
    _In_reads_(Count) PDEVICE_OBJECT *Objects,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(*Size) PWCHAR Destination,
    _Inout_ PULONG Size,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Needed = sizeof(WCHAR);
    ULONG Offset = 0;
    PWCHAR List;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
        Needed += IopGetDeviceNode(Objects[Index])->InstancePath.Length + sizeof(WCHAR);

    if ((Destination == NULL) || (*Size < Needed))
    {
        Status = (Destination == NULL) ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
        *Size = Needed;
        return Status;
    }

    List = ExAllocatePoolZero(PagedPool, Needed, TAG_IO);
    if (List == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Count; Index++)
    {
        PUNICODE_STRING Path = &IopGetDeviceNode(Objects[Index])->InstancePath;

        RtlCopyMemory((PUCHAR)List + Offset, Path->Buffer, Path->Length);
        Offset += Path->Length + sizeof(WCHAR);
    }

    ASSERT(Offset + sizeof(WCHAR) == Needed);

    Status = PiCopyOut(Destination, List, Needed, PreviousMode);
    if (NT_SUCCESS(Status)) *Size = Needed;

    ExFreePoolWithTag(List, TAG_IO);
    return Status;
}

/* The device's children as the tree holds them now, each referenced */
static
NTSTATUS
PiReferenceChildren(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PDEVICE_OBJECT **Objects,
    _Out_ PULONG Count)
{
    PDEVICE_OBJECT *Array = NULL;
    PDEVICE_NODE Child;
    ULONG Capacity = 0;
    ULONG Found;
    KIRQL OldIrql;

    for (;;)
    {
        Found = 0;

        KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
        for (Child = DeviceNode->Child; Child != NULL; Child = Child->Sibling)
        {
            if (Child->PhysicalDeviceObject == NULL) continue;

            if (Found < Capacity)
            {
                Array[Found] = Child->PhysicalDeviceObject;
                ObReferenceObject(Array[Found]);
            }

            Found++;
        }
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

        if (Found <= Capacity) break;

        /* The tree grew past the array: give back what was taken and size it again */
        while (Capacity != 0) ObDereferenceObject(Array[--Capacity]);
        if (Array != NULL) ExFreePoolWithTag(Array, TAG_IO);

        Capacity = Found + 4;
        Array = ExAllocatePoolWithTag(NonPagedPool, Capacity * sizeof(PDEVICE_OBJECT), TAG_IO);
        if (Array == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    }

    *Objects = Array;
    *Count = Found;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PiControlQueryDeviceRelations(
    _Inout_ PPLUGPLAY_CONTROL_DEVICE_RELATIONS_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PDEVICE_RELATIONS Relations = NULL;
    IO_STACK_LOCATION Stack;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT *Objects = NULL;
    ULONG Count = 0;
    NTSTATUS Status;
    ULONG Index;

    if (Data->Relations > PI_RELATIONS_BUS) return STATUS_INVALID_PARAMETER;

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    if (Data->Relations == PI_RELATIONS_BUS)
    {
        Status = PiReferenceChildren(IopGetDeviceNode(DeviceObject), &Objects, &Count);
    }
    else
    {
        RtlZeroMemory(&Stack, sizeof(Stack));
        Stack.MajorFunction = IRP_MJ_PNP;
        Stack.MinorFunction = IRP_MN_QUERY_DEVICE_RELATIONS;
        Stack.Parameters.QueryDeviceRelations.Type =
            (Data->Relations == PI_RELATIONS_EJECT) ? EjectionRelations :
            (Data->Relations == PI_RELATIONS_REMOVAL) ? RemovalRelations :
            PowerRelations;

        Status = IopSynchronousCall(DeviceObject, &Stack, (PVOID *)&Relations);

        /* A stack that reports none has none to give */
        if (!NT_SUCCESS(Status) || (Relations == NULL) || (Relations->Count == 0))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
        else
        {
            Objects = Relations->Objects;
            Count = Relations->Count;
            for (Index = 0; Index < Count; Index++)
            {
                if (IopGetDeviceNode(Objects[Index]) == NULL)
                {
                    Status = STATUS_OBJECT_NAME_NOT_FOUND;
                    break;
                }
            }
        }
    }

    if (NT_SUCCESS(Status))
    {
        Status = PiReturnInstanceList(Objects,
                                      Count,
                                      Data->Buffer,
                                      &Data->BufferSize,
                                      PreviousMode);
    }

    for (Index = 0; (Objects != NULL) && (Index < Count); Index++)
        ObDereferenceObject(Objects[Index]);

    if (Relations != NULL)
        ExFreePool(Relations);
    else if (Objects != NULL)
        ExFreePoolWithTag(Objects, TAG_IO);

    ObDereferenceObject(DeviceObject);
    return Status;
}

static
NTSTATUS
PiControlRetrieveDock(
    _Inout_ PPLUGPLAY_CONTROL_RETRIEVE_DOCK_DATA Data)
{
    UNREFERENCED_PARAMETER(Data);

    /* TODO: dock devices are not tracked, so no dock is ever reported */
    return STATUS_NO_SUCH_DEVICE;
}

/* INTERFACES *****************************************************************/

/* Hands a multi-string, Needed characters long, to a caller that sized its buffer in characters */
static
NTSTATUS
PiReturnMultiString(
    _In_reads_(Needed) PCWSTR Source,
    _In_ ULONG Needed,
    _Inout_ PPLUGPLAY_CONTROL_INTERFACE_DEVICE_LIST_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;

    if ((Data->Buffer == NULL) || (Data->BufferSize < Needed))
    {
        Data->BufferSize = Needed;
        return STATUS_SUCCESS;
    }

    Status = PiCopyOut(Data->Buffer, Source, Needed * sizeof(WCHAR), PreviousMode);
    if (NT_SUCCESS(Status)) Data->BufferSize = Needed;

    return Status;
}

static
NTSTATUS
PiControlGetInterfaceDeviceList(
    _Inout_ PPLUGPLAY_CONTROL_INTERFACE_DEVICE_LIST_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    static const WCHAR Empty[1] = { UNICODE_NULL };
    PDEVICE_OBJECT DeviceObject = NULL;
    PZZWSTR List = NULL;
    PCWSTR Entry;
    GUID Guid;
    NTSTATUS Status;

    Status = PiCaptureGuid(Data->FilterGuid, PreviousMode, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    if (Data->DeviceInstance.Length != 0)
    {
        Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);

        /* A device that is not there has no interfaces */
        if (Status == STATUS_NO_SUCH_DEVICE)
            return PiReturnMultiString(Empty, 1, Data, PreviousMode);

        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = IoGetDeviceInterfaces(&Guid,
                                   DeviceObject,
                                   (Data->Flags & PI_INTERFACE_LIST_ALL_DEVICES) ?
                                       DEVICE_INTERFACE_INCLUDE_NONACTIVE : 0,
                                   &List);
    if (DeviceObject != NULL) ObDereferenceObject(DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    for (Entry = List; *Entry != UNICODE_NULL; Entry += wcslen(Entry) + 1);

    Status = PiReturnMultiString(List, (ULONG)(Entry - List) + 1, Data, PreviousMode);

    ExFreePool(List);
    return Status;
}

static
NTSTATUS
PiControlGetInterfaceDeviceAlias(
    _Inout_ PPLUGPLAY_CONTROL_INTERFACE_ALIAS_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNICODE_STRING Link, Alias;
    ULONG Length;
    GUID Guid;
    NTSTATUS Status;

    Status = PiCaptureGuid(Data->AliasInterfaceClassGuid, PreviousMode, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PiCaptureUnicodeString(&Data->SymbolicLinkName,
                                    PreviousMode,
                                    MAXUSHORT - sizeof(WCHAR),
                                    &Link);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IoGetDeviceInterfaceAlias(&Link, &Guid, &Alias);
    PiFreeCapturedString(&Link);
    if (!NT_SUCCESS(Status)) return Status;

    /* The service is told the length of the alias without its terminator */
    Length = Data->AliasSymbolicLinkNameLength;
    Status = PiReturnString(&Alias, Data->AliasSymbolicLinkName, &Length, PreviousMode);
    if (NT_SUCCESS(Status)) Data->AliasSymbolicLinkNameLength = Length - 1;

    RtlFreeUnicodeString(&Alias);
    return Status;
}

static
NTSTATUS
PiControlDeviceClassAssociation(
    _Inout_ PPLUGPLAY_CONTROL_CLASS_ASSOCIATION_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNICODE_STRING Reference = { 0 }, Link;
    PDEVICE_OBJECT DeviceObject;
    GUID Guid;
    NTSTATUS Status;

    if (!Data->Register)
    {
        UNICODE_STRING Named;

        if ((Data->SymbolicLinkNameLength < 2) ||
            (Data->SymbolicLinkNameLength > MAXUSHORT / sizeof(WCHAR)))
        {
            return STATUS_INVALID_PARAMETER;
        }

        Named.Buffer = Data->SymbolicLinkName;
        Named.Length = (USHORT)((Data->SymbolicLinkNameLength - 1) * sizeof(WCHAR));
        Named.MaximumLength = Named.Length;

        Status = PiCaptureUnicodeString(&Named, PreviousMode, MAXUSHORT - sizeof(WCHAR), &Link);
        if (!NT_SUCCESS(Status)) return Status;

        /* TODO: remove the interface's registration from the registry, not only its state */
        Status = IoSetDeviceInterfaceState(&Link, FALSE);
        PiFreeCapturedString(&Link);

        return (Status == STATUS_OBJECT_NAME_NOT_FOUND) ? STATUS_SUCCESS : Status;
    }

    Status = PiCaptureGuid(Data->InterfaceGuid, PreviousMode, &Guid);
    if (!NT_SUCCESS(Status)) return Status;

    if (Data->Reference.Length != 0)
    {
        Status = PiCaptureUnicodeString(&Data->Reference,
                                        PreviousMode,
                                        MAXUSHORT - sizeof(WCHAR),
                                        &Reference);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (NT_SUCCESS(Status))
    {
        Status = IoRegisterDeviceInterface(DeviceObject,
                                           &Guid,
                                           (Reference.Length != 0) ? &Reference : NULL,
                                           &Link);
        ObDereferenceObject(DeviceObject);

        if (NT_SUCCESS(Status))
        {
            Status = PiReturnString(&Link,
                                    Data->SymbolicLinkName,
                                    &Data->SymbolicLinkNameLength,
                                    PreviousMode);
            RtlFreeUnicodeString(&Link);
        }
    }

    PiFreeCapturedString(&Reference);
    return Status;
}

/* PROPERTIES *****************************************************************/

static
VOID
PiBuildPowerData(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDEVICE_CAPABILITIES Capabilities,
    _Out_ PCM_POWER_DATA PowerData)
{
    PDEVICE_OBJECT_POWER_EXTENSION Dope = IoGetDevObjExtension(DeviceObject)->Dope;
    ULONG Index;

    RtlZeroMemory(PowerData, sizeof(*PowerData));
    PowerData->PD_Size = sizeof(*PowerData);
    PowerData->PD_MostRecentPowerState = (Dope != NULL) ? Dope->State : PowerDeviceUnspecified;

    /* Every device has D0 and D3; the others are what its stack declares */
    PowerData->PD_Capabilities = PDCAP_D0_SUPPORTED | PDCAP_D3_SUPPORTED;
    if (Capabilities->DeviceD1) PowerData->PD_Capabilities |= PDCAP_D1_SUPPORTED;
    if (Capabilities->DeviceD2) PowerData->PD_Capabilities |= PDCAP_D2_SUPPORTED;
    if (Capabilities->WakeFromD0) PowerData->PD_Capabilities |= PDCAP_WAKE_FROM_D0_SUPPORTED;
    if (Capabilities->WakeFromD1) PowerData->PD_Capabilities |= PDCAP_WAKE_FROM_D1_SUPPORTED;
    if (Capabilities->WakeFromD2) PowerData->PD_Capabilities |= PDCAP_WAKE_FROM_D2_SUPPORTED;
    if (Capabilities->WakeFromD3) PowerData->PD_Capabilities |= PDCAP_WAKE_FROM_D3_SUPPORTED;
    if (Capabilities->WarmEjectSupported) PowerData->PD_Capabilities |= PDCAP_WARM_EJECT_SUPPORTED;

    PowerData->PD_D1Latency = Capabilities->D1Latency;
    PowerData->PD_D2Latency = Capabilities->D2Latency;
    PowerData->PD_D3Latency = Capabilities->D3Latency;

    for (Index = 0; Index < PowerSystemMaximum; Index++)
        PowerData->PD_PowerStateMapping[Index] = Capabilities->DeviceState[Index];

    PowerData->PD_DeepestSystemWake = Capabilities->SystemWake;
}

static
NTSTATUS
PiControlProperty(
    _Inout_ PPLUGPLAY_CONTROL_PROPERTY_DATA Data,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    DEVICE_REGISTRY_PROPERTY Property;
    DEVICE_CAPABILITIES Capabilities;
    CM_POWER_DATA PowerData;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_NODE DeviceNode;
    ULONG Policy;
    const VOID *Value = NULL;
    PVOID Buffer = NULL;
    ULONG Length = 0;
    ULONG Allocated;
    NTSTATUS Status;

    switch (Data->Property)
    {
        case PNP_PROPERTY_UI_NUMBER: Property = DevicePropertyUINumber; break;
        case PNP_PROPERTY_PHYSICAL_DEVICE_OBJECT_NAME: Property = DevicePropertyPhysicalDeviceObjectName; break;
        case PNP_PROPERTY_BUSTYPEGUID: Property = DevicePropertyBusTypeGuid; break;
        case PNP_PROPERTY_LEGACYBUSTYPE: Property = DevicePropertyLegacyBusType; break;
        case PNP_PROPERTY_BUSNUMBER: Property = DevicePropertyBusNumber; break;
        case PNP_PROPERTY_REMOVAL_POLICY: Property = DevicePropertyRemovalPolicy; break;
        case PNP_PROPERTY_ADDRESS: Property = DevicePropertyAddress; break;
        case PNP_PROPERTY_ENUMERATOR_NAME: Property = DevicePropertyEnumeratorName; break;
        case PNP_PROPERTY_INSTALL_STATE: Property = DevicePropertyInstallState; break;
        case PNP_PROPERTY_CONTAINERID: Property = DevicePropertyContainerID; break;

        case PNP_PROPERTY_POWER_DATA:
        case PNP_PROPERTY_REMOVAL_POLICY_HARDWARE_DEFAULT:
            Property = (DEVICE_REGISTRY_PROPERTY)-1;
            break;

        case PNP_PROPERTY_LOCATION_PATHS:
            /* TODO: location paths need the bus drivers' location interface */
            return STATUS_OBJECT_NAME_NOT_FOUND;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    Status = PiReferenceDeviceByInstance(&Data->DeviceInstance, PreviousMode, &DeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceNode = IopGetDeviceNode(DeviceObject);

    if (Data->Property == PNP_PROPERTY_POWER_DATA)
    {
        Status = IopQueryDeviceCapabilities(DeviceNode, &Capabilities);
        if (NT_SUCCESS(Status))
        {
            PiBuildPowerData(DeviceObject, &Capabilities, &PowerData);
            Value = &PowerData;
            Length = sizeof(PowerData);
        }
    }
    else if (Data->Property == PNP_PROPERTY_REMOVAL_POLICY_HARDWARE_DEFAULT)
    {
        Policy = DeviceNode->HardwareRemovalPolicy;
        Value = &Policy;
        Length = sizeof(Policy);
    }
    else
    {
        Allocated = min(Data->BufferSize, PI_MAX_PROPERTY_BYTES);
        if (Allocated != 0)
        {
            Buffer = ExAllocatePoolWithTag(PagedPool, Allocated, TAG_IO);
            if (Buffer == NULL)
            {
                ObDereferenceObject(DeviceObject);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }

        Status = IoGetDeviceProperty(DeviceObject, Property, Allocated, Buffer, &Length);
        Value = Buffer;

        /* A property the device does not have is a value that is not there */
        if ((Status == STATUS_INVALID_PARAMETER_2) || (Status == STATUS_OBJECT_NAME_NOT_FOUND) ||
            (Status == STATUS_INVALID_DEVICE_REQUEST) || (Status == STATUS_NOT_SUPPORTED))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }

    ObDereferenceObject(DeviceObject);

    if (NT_SUCCESS(Status) && (Data->BufferSize < Length))
        Status = STATUS_BUFFER_TOO_SMALL;

    if (NT_SUCCESS(Status))
        Status = PiCopyOut(Data->Buffer, Value, Length, PreviousMode);

    if (NT_SUCCESS(Status) || (Status == STATUS_BUFFER_TOO_SMALL))
        Data->BufferSize = Length;

    if (Buffer != NULL) ExFreePoolWithTag(Buffer, TAG_IO);
    return Status;
}

/* ENTRY **********************************************************************/

/* Answers a control class whose data the caller already captured; FALSE when the class is not one of these */
BOOLEAN
PiPlugPlayControlDevice(
    _In_ PLUGPLAY_CONTROL_CLASS PlugPlayControlClass,
    _Inout_ PVOID Data,
    _In_ ULONG DataSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PNTSTATUS Status)
{
    ULONG Required;

    PAGED_CODE();

    switch (PlugPlayControlClass)
    {
        case PlugPlayControlStartDevice:
        case PlugPlayControlInitializeDevice:
        case PlugPlayControlDeregisterDevice:
            Required = sizeof(PLUGPLAY_CONTROL_DEVICE_CONTROL_DATA);
            break;
        case PlugPlayControlDeviceStatus: Required = sizeof(PLUGPLAY_CONTROL_STATUS_DATA); break;
        case PlugPlayControlGetDeviceDepth: Required = sizeof(PLUGPLAY_CONTROL_DEPTH_DATA); break;
        case PlugPlayControlGetRelatedDevice: Required = sizeof(PLUGPLAY_CONTROL_RELATED_DEVICE_DATA); break;
        case PlugPlayControlQueryDeviceRelations: Required = sizeof(PLUGPLAY_CONTROL_DEVICE_RELATIONS_DATA); break;
        case PlugPlayControlRetrieveDock: Required = sizeof(PLUGPLAY_CONTROL_RETRIEVE_DOCK_DATA); break;
        case PlugPlayControlGetInterfaceDeviceList: Required = sizeof(PLUGPLAY_CONTROL_INTERFACE_DEVICE_LIST_DATA); break;
        case PlugPlayControlGetInterfaceDeviceAlias: Required = sizeof(PLUGPLAY_CONTROL_INTERFACE_ALIAS_DATA); break;
        case PlugPlayControlDeviceClassAssociation: Required = sizeof(PLUGPLAY_CONTROL_CLASS_ASSOCIATION_DATA); break;
        case PlugPlayControlProperty: Required = sizeof(PLUGPLAY_CONTROL_PROPERTY_DATA); break;
        default: return FALSE;
    }

    if (DataSize < Required)
    {
        *Status = STATUS_INVALID_PARAMETER;
        return TRUE;
    }

    switch (PlugPlayControlClass)
    {
        case PlugPlayControlStartDevice: *Status = PiControlStartDevice(Data, PreviousMode); break;
        case PlugPlayControlInitializeDevice: *Status = PiControlInitializeDevice(Data, PreviousMode); break;
        case PlugPlayControlDeregisterDevice: *Status = PiControlDeregisterDevice(Data, PreviousMode); break;
        case PlugPlayControlDeviceStatus: *Status = PiControlDeviceStatus(Data, PreviousMode); break;
        case PlugPlayControlGetDeviceDepth: *Status = PiControlGetDeviceDepth(Data, PreviousMode); break;
        case PlugPlayControlGetRelatedDevice: *Status = PiControlGetRelatedDevice(Data, PreviousMode); break;
        case PlugPlayControlQueryDeviceRelations: *Status = PiControlQueryDeviceRelations(Data, PreviousMode); break;
        case PlugPlayControlRetrieveDock: *Status = PiControlRetrieveDock(Data); break;
        case PlugPlayControlGetInterfaceDeviceList: *Status = PiControlGetInterfaceDeviceList(Data, PreviousMode); break;
        case PlugPlayControlGetInterfaceDeviceAlias: *Status = PiControlGetInterfaceDeviceAlias(Data, PreviousMode); break;
        case PlugPlayControlDeviceClassAssociation: *Status = PiControlDeviceClassAssociation(Data, PreviousMode); break;
        case PlugPlayControlProperty: *Status = PiControlProperty(Data, PreviousMode); break;
        default: ASSERT(FALSE); *Status = STATUS_INVALID_PARAMETER; break;
    }

    return TRUE;
}

/* EOF */
