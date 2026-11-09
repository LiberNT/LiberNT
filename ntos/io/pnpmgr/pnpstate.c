/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Moving a device node from enumerated to started, and back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <umpnpmgr/sysguid.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

KSPIN_LOCK IopDeviceActionLock;
LIST_ENTRY IopDeviceActionRequestList;
BOOLEAN IopDeviceActionInProgress = FALSE;
WORK_QUEUE_ITEM IopDeviceActionWorkItem;

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopEnumRootName = RTL_CONSTANT_STRING(ENUM_ROOT);

static const UNICODE_STRING IopServicesName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services");

static const UNICODE_STRING IopControlClassName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class");

static BOOLEAN IopRemovingRenamedStack = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

BOOLEAN
PiIsRemovingRenamedStack(VOID)
{
    return IopRemovingRenamedStack;
}

static
NTSTATUS
PiIrpQueryId(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWSTR *Id)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    *Id = NULL;

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_ID;
    StackLocation.Parameters.QueryId.IdType = IdType;

    return IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                              &StackLocation,
                              (PVOID *)Id);
}

static
NTSTATUS
PiSetMultiSzValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _In_ PWSTR MultiSz)
{
    UNICODE_STRING Name;
    PWCHAR Cursor = MultiSz;
    ULONG Length = 0;

    if (MultiSz == NULL) return STATUS_SUCCESS;

    while (*Cursor != UNICODE_NULL)
    {
        ULONG One = (ULONG)((wcslen(Cursor) + 1) * sizeof(WCHAR));

        Length += One;
        Cursor += One / sizeof(WCHAR);
    }

    Length += sizeof(WCHAR);

    RtlInitUnicodeString(&Name, ValueName);

    return ZwSetValueKey(Key, &Name, 0, REG_MULTI_SZ, MultiSz, Length);
}

static
NTSTATUS
PiBuildInstancePath(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PWSTR DeviceId,
    _In_ PWSTR InstanceId)
{
    UNICODE_STRING Prefix;
    UNICODE_STRING Path;
    USHORT Length;
    PWCHAR Buffer;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(&Prefix, NULL, 0);

    if (InstanceId == NULL || *InstanceId == UNICODE_NULL)
    {
        IopGetParentIdPrefix(DeviceNode, &Prefix);
    }

    Length = (USHORT)((wcslen(DeviceId) + 1) * sizeof(WCHAR)) +
             Prefix.Length +
             (USHORT)((InstanceId != NULL)
                          ? wcslen(InstanceId) * sizeof(WCHAR)
                          : 0) +
             sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(NonPagedPool, Length, TAG_IO_DEVNODE);
    if (Buffer == NULL)
    {
        if (Prefix.Buffer != NULL) ExFreePool(Prefix.Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Path.Buffer = Buffer;
    Path.Length = 0;
    Path.MaximumLength = Length;

    RtlAppendUnicodeToString(&Path, DeviceId);
    RtlAppendUnicodeToString(&Path, L"\\");

    if (Prefix.Length != 0)
    {
        RtlAppendUnicodeStringToString(&Path, &Prefix);
        ExFreePool(Prefix.Buffer);
    }

    if (InstanceId != NULL) RtlAppendUnicodeToString(&Path, InstanceId);

    DeviceNode->InstancePath = Path;

    return STATUS_SUCCESS;
}

static
NTSTATUS
PiQueryAndSaveIds(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PHANDLE InstanceKey)
{
    PWSTR DeviceId = NULL;
    PWSTR InstanceId = NULL;
    PWSTR HardwareIds = NULL;
    PWSTR CompatibleIds = NULL;
    UNICODE_STRING KeyPath;
    NTSTATUS Status;
    HANDLE EnumKey;
    PWCHAR Buffer;
    USHORT Length;

    PAGED_CODE();

    *InstanceKey = NULL;

    Status = PiIrpQueryId(DeviceNode, BusQueryDeviceID, &DeviceId);
    if (!NT_SUCCESS(Status) || DeviceId == NULL)
    {
        PiSetDevNodeProblem(DeviceNode, CM_PROB_INVALID_DATA);
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    Status = PiIrpQueryId(DeviceNode, BusQueryInstanceID, &InstanceId);
    if (!NT_SUCCESS(Status)) InstanceId = NULL;

    Status = PiBuildInstancePath(DeviceNode, DeviceId, InstanceId);

    if (InstanceId != NULL) ExFreePool(InstanceId);
    ExFreePool(DeviceId);

    if (!NT_SUCCESS(Status)) return Status;

    Length = IopEnumRootName.Length + sizeof(WCHAR) +
             DeviceNode->InstancePath.Length + sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    KeyPath.Buffer = Buffer;
    KeyPath.Length = 0;
    KeyPath.MaximumLength = Length;

    RtlAppendUnicodeStringToString(&KeyPath,
                                   (PUNICODE_STRING)&IopEnumRootName);
    RtlAppendUnicodeToString(&KeyPath, L"\\");
    RtlAppendUnicodeStringToString(&KeyPath, &DeviceNode->InstancePath);

    Status = IopCreateDeviceKeyPath(&KeyPath,
                                    REG_OPTION_NON_VOLATILE,
                                    &EnumKey);

    ExFreePoolWithTag(Buffer, TAG_IO);

    if (!NT_SUCCESS(Status)) return Status;

    Status = PiIrpQueryId(DeviceNode, BusQueryHardwareIDs, &HardwareIds);
    if (NT_SUCCESS(Status) && HardwareIds != NULL)
    {
        PiSetMultiSzValue(EnumKey, L"HardwareID", HardwareIds);
        ExFreePool(HardwareIds);
    }

    Status = PiIrpQueryId(DeviceNode, BusQueryCompatibleIDs, &CompatibleIds);
    if (NT_SUCCESS(Status) && CompatibleIds != NULL)
    {
        PiSetMultiSzValue(EnumKey, L"CompatibleIDs", CompatibleIds);
        ExFreePool(CompatibleIds);
    }

    PiSetDevNodeText(DeviceNode, EnumKey);
    IopSetDeviceInstanceData(EnumKey, DeviceNode);

    PiSetDevNodeFlag(DeviceNode, DNF_IDS_QUERIED);

    *InstanceKey = EnumKey;

    return STATUS_SUCCESS;
}

NTSTATUS
FASTCALL
IopAttachFilterDrivers(
    IN PDEVICE_NODE DeviceNode,
    IN HANDLE EnumSubKey,
    IN HANDLE ClassKey,
    IN BOOLEAN Lower)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    PUNICODE_STRING Filters = NULL;
    PDRIVER_OBJECT DriverObject;
    ULONG FilterCount = 0;
    NTSTATUS Status;
    ULONG Index;
    HANDLE Keys[2];
    ULONG Which;

    PAGED_CODE();

    Keys[0] = EnumSubKey;
    Keys[1] = ClassKey;

    for (Which = 0; Which < RTL_NUMBER_OF(Keys); Which++)
    {
        if (Keys[Which] == NULL) continue;

        Status = IopGetRegistryValue(Keys[Which],
                                     Lower ? L"LowerFilters"
                                           : L"UpperFilters",
                                     &Information);
        if (!NT_SUCCESS(Status)) continue;

        Status = PnpRegMultiSzToUnicodeStrings(Information,
                                               &Filters,
                                               &FilterCount);
        ExFreePool(Information);
        Information = NULL;

        if (!NT_SUCCESS(Status)) continue;

        for (Index = 0; Index < FilterCount; Index++)
        {
            HANDLE ServiceHandle;

            Status = IopOpenRegistryKeyEx(&ServiceHandle,
                                          NULL,
                                          (PUNICODE_STRING)&IopServicesName,
                                          KEY_READ);
            if (!NT_SUCCESS(Status)) continue;

            Status = IopOpenRegistryKeyEx(&ServiceHandle,
                                          ServiceHandle,
                                          &Filters[Index],
                                          KEY_READ);
            if (!NT_SUCCESS(Status)) continue;

            Status = IopLoadDriverForPnp(ServiceHandle, &DriverObject);
            ZwClose(ServiceHandle);

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("PnP: the filter %wZ did not load (%lx)\n",
                        &Filters[Index],
                        Status);
                continue;
            }

            if (IoGetDrvObjExtension(DriverObject)->AddDevice != NULL)
            {
                IoGetDrvObjExtension(DriverObject)->AddDevice(
                    DriverObject,
                    DeviceNode->PhysicalDeviceObject);
            }

            ObDereferenceObject(DriverObject);
        }

        PnpFreeUnicodeStringList(Filters, FilterCount);
        Filters = NULL;
        FilterCount = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
IopConfigureDeviceFromDriverDatabase(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    PDRIVER_OBJECT DriverObject;
    UNICODE_STRING ServiceName;
    UNICODE_STRING ClassName;
    NTSTATUS Status;
    HANDLE ServicesKey = NULL;
    HANDLE ServiceKey = NULL;
    HANDLE ClassKey = NULL;
    PWCHAR Buffer;

    PAGED_CODE();

    Status = IopGetRegistryValue(InstanceKey, L"ClassGUID", &Information);
    if (NT_SUCCESS(Status))
    {
        if (Information->Type == REG_SZ &&
            Information->DataLength > sizeof(WCHAR))
        {
            ClassName.Buffer = (PWCHAR)((PUCHAR)Information +
                                        Information->DataOffset);
            ClassName.Length =
                (USHORT)(Information->DataLength - sizeof(WCHAR));
            ClassName.MaximumLength = ClassName.Length;

            Status = IopOpenRegistryKeyEx(
                &ClassKey,
                NULL,
                (PUNICODE_STRING)&IopControlClassName,
                KEY_READ);
            if (NT_SUCCESS(Status))
            {
                HANDLE Specific;

                Status = IopOpenRegistryKeyEx(&Specific,
                                              ClassKey,
                                              &ClassName,
                                              KEY_READ);
                ZwClose(ClassKey);

                ClassKey = NT_SUCCESS(Status) ? Specific : NULL;
            }
            else
            {
                ClassKey = NULL;
            }
        }

        ExFreePool(Information);
        Information = NULL;
    }

    IopAttachFilterDrivers(DeviceNode, InstanceKey, ClassKey, TRUE);

    Status = IopGetRegistryValue(InstanceKey, L"Service", &Information);
    if (!NT_SUCCESS(Status) ||
        Information->Type != REG_SZ ||
        Information->DataLength <= sizeof(WCHAR))
    {
        if (Information != NULL) ExFreePool(Information);
        if (ClassKey != NULL) ZwClose(ClassKey);

        PiSetDevNodeFlag(DeviceNode, DNF_NO_SERVICE);

        return STATUS_SUCCESS;
    }

    ServiceName.Buffer = (PWCHAR)((PUCHAR)Information +
                                  Information->DataOffset);
    ServiceName.Length = (USHORT)(Information->DataLength - sizeof(WCHAR));
    ServiceName.MaximumLength = ServiceName.Length;

    Buffer = ExAllocatePoolZero(NonPagedPool,
                                ServiceName.Length + sizeof(WCHAR),
                                TAG_IO_DEVNODE);
    if (Buffer != NULL)
    {
        RtlCopyMemory(Buffer, ServiceName.Buffer, ServiceName.Length);

        DeviceNode->ServiceName.Buffer = Buffer;
        DeviceNode->ServiceName.Length = ServiceName.Length;
        DeviceNode->ServiceName.MaximumLength =
            ServiceName.Length + sizeof(WCHAR);
    }

    Status = IopOpenRegistryKeyEx(&ServicesKey,
                                  NULL,
                                  (PUNICODE_STRING)&IopServicesName,
                                  KEY_READ);
    if (NT_SUCCESS(Status))
    {
        Status = IopOpenRegistryKeyEx(&ServiceKey,
                                      ServicesKey,
                                      &ServiceName,
                                      KEY_READ);
        ZwClose(ServicesKey);
    }

    ExFreePool(Information);

    if (!NT_SUCCESS(Status))
    {
        if (ClassKey != NULL) ZwClose(ClassKey);
        PiSetDevNodeProblem(DeviceNode, CM_PROB_DEVICE_NOT_THERE);
        return Status;
    }

    Status = IopLoadDriverForPnp(ServiceKey, &DriverObject);
    ZwClose(ServiceKey);

    if (!NT_SUCCESS(Status))
    {
        if (ClassKey != NULL) ZwClose(ClassKey);
        PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_DRIVER_ENTRY);
        return Status;
    }

    if (IoGetDrvObjExtension(DriverObject)->AddDevice != NULL)
    {
        Status = IoGetDrvObjExtension(DriverObject)->AddDevice(
            DriverObject,
            DeviceNode->PhysicalDeviceObject);
        if (!NT_SUCCESS(Status))
        {
            PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_ADD);
        }
    }

    ObDereferenceObject(DriverObject);

    IopAttachFilterDrivers(DeviceNode, InstanceKey, ClassKey, FALSE);

    if (ClassKey != NULL) ZwClose(ClassKey);

    return Status;
}

VOID
PiDevNodeAddDrivers(
    _In_ PDEVICE_NODE DeviceNode)
{
    NTSTATUS Status;
    HANDLE InstanceKey = NULL;

    PAGED_CODE();

    if (!(DeviceNode->Flags & DNF_IDS_QUERIED))
    {
        Status = PiQueryAndSaveIds(DeviceNode, &InstanceKey);
        if (!NT_SUCCESS(Status)) return;
    }
    else
    {
        Status = PnpDeviceObjectToDeviceInstance(
            DeviceNode->PhysicalDeviceObject,
            &InstanceKey,
            KEY_ALL_ACCESS);
        if (!NT_SUCCESS(Status)) return;
    }

    Status = IopConfigureDeviceFromDriverDatabase(DeviceNode, InstanceKey);

    ZwClose(InstanceKey);

    if (NT_SUCCESS(Status) && !(DeviceNode->Flags & DNF_HAS_PROBLEM))
    {
        PiSetDevNodeState(DeviceNode, DeviceNodeDriversAdded);
        return;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeInitialized);
}

static
VOID
PiProcessStartDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopAssignDeviceResources(DeviceNode);
    if (!NT_SUCCESS(Status))
    {
        PiSetDevNodeProblem(DeviceNode, CM_PROB_NORMAL_CONFLICT);
        return;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeResourcesAssigned);

    PiSetDevNodeState(DeviceNode, DeviceNodeStartPending);

    Status = PiIrpStartDevice(DeviceNode);

    PiSetDevNodeState(DeviceNode, DeviceNodeStartCompletion);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnP: %wZ did not start (%lx)\n",
                &DeviceNode->InstancePath,
                Status);

        IopResDbRelease(DeviceNode);
        PiSetDevNodeProblem(DeviceNode, CM_PROB_FAILED_START);
        PiSetDevNodeState(DeviceNode, DeviceNodeDriversAdded);

        return;
    }

    PiSetDevNodeState(DeviceNode, DeviceNodeStartPostWork);

    IopUpdateResourceMapForPnPDevice(DeviceNode);

    PiSetDevNodeState(DeviceNode, DeviceNodeStarted);

    IopQueueDeviceInstallEvent(&GUID_DEVICE_ARRIVAL,
                               &DeviceNode->InstancePath);
}

static
VOID
PiProcessNewChildren(
    _In_ PDEVICE_NODE DeviceNode)
{
    PDEVICE_RELATIONS Relations;
    PDEVICE_NODE ChildNode;
    PDEVICE_OBJECT Child;
    ULONG Index;

    PAGED_CODE();

    Relations = DeviceNode->OverUsed1.PendingDeviceRelations;
    if (Relations == NULL) return;

    DeviceNode->OverUsed1.PendingDeviceRelations = NULL;

    for (Index = 0; Index < Relations->Count; Index++)
    {
        Child = Relations->Objects[Index];

        ChildNode = IopGetDeviceNode(Child);
        if (ChildNode != NULL)
        {
            ObDereferenceObject(Child);
            continue;
        }

        ChildNode = PipAllocateDeviceNode(Child);
        if (ChildNode == NULL)
        {
            ObDereferenceObject(Child);
            continue;
        }

        PiSetDevNodeFlag(ChildNode, DNF_ENUMERATED);
        PiInsertDevNode(ChildNode, DeviceNode);

        Child->Flags |= DO_BUS_ENUMERATED_DEVICE;

        ObDereferenceObject(Child);
    }

    ExFreePool(Relations);
}

static
VOID
PiProcessEnumeration(
    _In_ PDEVICE_NODE DeviceNode)
{
    NTSTATUS Status;

    PAGED_CODE();

    PiSetDevNodeState(DeviceNode, DeviceNodeEnumeratePending);

    Status = PiIrpQueryDeviceRelations(DeviceNode, BusRelations);

    PiSetDevNodeState(DeviceNode, DeviceNodeEnumerateCompletion);

    if (NT_SUCCESS(Status)) PiProcessNewChildren(DeviceNode);

    PiClearDevNodeFlag(DeviceNode, DNF_REENUMERATE);
    PiSetDevNodeState(DeviceNode, DeviceNodeStarted);
}

static
VOID
PiProcessRemoval(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Surprise)
{
    IO_STACK_LOCATION StackLocation;
    PDEVICE_NODE Child;
    PDEVICE_NODE Next;

    PAGED_CODE();

    Child = DeviceNode->Child;
    while (Child != NULL)
    {
        Next = Child->Sibling;
        PiProcessRemoval(Child, Surprise);
        Child = Next;
    }

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = Surprise ? IRP_MN_SURPRISE_REMOVAL
                                           : IRP_MN_REMOVE_DEVICE;

    IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                       &StackLocation,
                       NULL);

    IopDisableDeviceInterfaces(DeviceNode);
    IopResDbRelease(DeviceNode);

    PiSetDevNodeState(DeviceNode, DeviceNodeRemoved);

    IopQueueDeviceInstallEvent(Surprise ? &GUID_DEVICE_SURPRISE_REMOVAL
                                        : &GUID_DEVICE_SAFE_REMOVAL,
                               &DeviceNode->InstancePath);
}

VOID
PiEnumerationFinished(
    _In_opt_ PDEVICE_NODE DeviceNode)
{
    UNREFERENCED_PARAMETER(DeviceNode);

    if (!PnPBootDriversInitialized) return;

    IopQueueDeviceInstallEvent(&GUID_DEVICE_ENUMERATED, NULL);
}

VOID
PiDevNodeStateMachine(
    _In_ PDEVICE_NODE RootNode)
{
    PDEVICE_NODE Node = RootNode;
    BOOLEAN Progress;

    PAGED_CODE();

    while (TRUE)
    {
        Progress = FALSE;

        switch (Node->State)
        {
            case DeviceNodeUninitialized:
                PiSetDevNodeState(Node, DeviceNodeInitialized);
                Progress = TRUE;
                break;

            case DeviceNodeInitialized:
                if (Node->Flags & DNF_HAS_PROBLEM) break;

                PiDevNodeAddDrivers(Node);
                Progress = (Node->State == DeviceNodeDriversAdded);
                break;

            case DeviceNodeDriversAdded:
                if (Node->Flags & DNF_HAS_PROBLEM) break;

                PiProcessStartDevice(Node);
                Progress = (Node->State == DeviceNodeStarted);
                break;

            case DeviceNodeStarted:
                if (Node->Flags & DNF_REENUMERATE)
                {
                    PiProcessEnumeration(Node);
                    Progress = TRUE;
                }
                break;

            case DeviceNodeAwaitingQueuedRemoval:
                PiProcessRemoval(Node, FALSE);
                Progress = TRUE;
                break;

            case DeviceNodeAwaitingQueuedDeletion:
                PiProcessRemoval(Node, TRUE);
                Progress = TRUE;
                break;

            default:
                break;
        }

        if (Progress) continue;

        if (Node->Child != NULL)
        {
            Node = Node->Child;
            continue;
        }

        while (Node != RootNode && Node->Sibling == NULL)
        {
            Node = Node->Parent;
            if (Node == NULL) return;
        }

        if (Node == RootNode) break;

        Node = Node->Sibling;
    }

    PiEnumerationFinished(RootNode);
}

/* DEVICE ACTIONS *************************************************************/

static
VOID
PiRunDeviceAction(
    _In_ PIOP_DEVICE_ACTION_REQUEST Request)
{
    PDEVICE_NODE DeviceNode;
    PI_QUERY_REMOVE_DATA RemoveData;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    DeviceNode = IopGetDeviceNode(Request->DeviceObject);
    if (DeviceNode == NULL)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else
    {
        switch (Request->Action)
        {
            case PiActionEnumRootDevices:
            case PiActionEnumDeviceTree:
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionAddBootDevices:
                if (DeviceNode->State == DeviceNodeInitialized)
                {
                    PiDevNodeAddDrivers(DeviceNode);
                }
                break;

            case PiActionStartDevice:
                if (DeviceNode->State == DeviceNodeDriversAdded)
                {
                    PiProcessStartDevice(DeviceNode);
                }
                break;

            case PiActionResetDevice:
                PiClearDevNodeProblem(DeviceNode);
                PiSetDevNodeState(DeviceNode, DeviceNodeUninitialized);
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionQueryState:
            {
                PNP_DEVICE_STATE DeviceState = 0;

                Status = PiIrpQueryPnPDeviceState(DeviceNode, &DeviceState);
                if (!NT_SUCCESS(Status)) break;

                if (DeviceState & PNP_DEVICE_DONT_DISPLAY_IN_UI)
                {
                    IopDeviceNodeSetUserFlag(DeviceNode,
                                             DNUF_DONT_SHOW_IN_UI);
                }

                if (DeviceState & PNP_DEVICE_NOT_DISABLEABLE)
                {
                    IopDeviceNodeSetUserFlag(DeviceNode,
                                             DNUF_NOT_DISABLEABLE);
                }

                if (DeviceState & PNP_DEVICE_REMOVED)
                {
                    PiSetDevNodeState(DeviceNode,
                                      DeviceNodeAwaitingQueuedRemoval);
                    PiDevNodeStateMachine(DeviceNode);
                }

                break;
            }

            case PiActionQueryRemoveDevice:
                Status = PiQueryRemoveDevice(Request->DeviceObject,
                                             &RemoveData);
                break;

            case PiActionRemoveDevice:
                PiSetDevNodeState(DeviceNode,
                                  DeviceNodeAwaitingQueuedRemoval);
                PiDevNodeStateMachine(DeviceNode);
                break;

            case PiActionCloseRemovedDevice:
                if (DeviceNode->State == DeviceNodeRemovePendingCloses ||
                    DeviceNode->State == DeviceNodeDeletePendingCloses)
                {
                    PiSetDevNodeState(DeviceNode, DeviceNodeRemoved);
                }
                break;

            case PiActionEjectDevice:
                Status = PiQueryRemoveDevice(Request->DeviceObject,
                                             &RemoveData);
                if (!NT_SUCCESS(Status)) break;

                PiSetDevNodeState(DeviceNode,
                                  DeviceNodeAwaitingQueuedRemoval);
                PiDevNodeStateMachine(DeviceNode);
                break;

            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
        }
    }

    if (Request->CompletionStatus != NULL)
    {
        *Request->CompletionStatus = Status;
    }

    if (Request->CompletionEvent != NULL)
    {
        KeSetEvent(Request->CompletionEvent, IO_NO_INCREMENT, FALSE);
    }

    ObDereferenceObject(Request->DeviceObject);
    ExFreePoolWithTag(Request, TAG_PNP_DEVACTION);
}

static
VOID
NTAPI
PiDeviceActionWorker(
    _In_ PVOID Context)
{
    PIOP_DEVICE_ACTION_REQUEST Request;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Context);

    while (TRUE)
    {
        KeAcquireSpinLock(&IopDeviceActionLock, &OldIrql);

        if (IsListEmpty(&IopDeviceActionRequestList))
        {
            IopDeviceActionInProgress = FALSE;
            KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);
            return;
        }

        Entry = RemoveHeadList(&IopDeviceActionRequestList);

        KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);

        Request = CONTAINING_RECORD(Entry,
                                    IOP_DEVICE_ACTION_REQUEST,
                                    ListEntry);

        PiRunDeviceAction(Request);
    }
}

VOID
PiQueueDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action,
    _In_opt_ PKEVENT CompletionEvent,
    _Out_opt_ NTSTATUS *CompletionStatus)
{
    PIOP_DEVICE_ACTION_REQUEST Request;
    BOOLEAN Queue = FALSE;
    KIRQL OldIrql;

    Request = ExAllocatePoolZero(NonPagedPool,
                                 sizeof(IOP_DEVICE_ACTION_REQUEST),
                                 TAG_PNP_DEVACTION);
    if (Request == NULL)
    {
        if (CompletionStatus != NULL)
        {
            *CompletionStatus = STATUS_INSUFFICIENT_RESOURCES;
        }

        if (CompletionEvent != NULL)
        {
            KeSetEvent(CompletionEvent, IO_NO_INCREMENT, FALSE);
        }

        return;
    }

    ObReferenceObject(DeviceObject);

    Request->DeviceObject = DeviceObject;
    Request->Action = Action;
    Request->CompletionEvent = CompletionEvent;
    Request->CompletionStatus = CompletionStatus;

    KeAcquireSpinLock(&IopDeviceActionLock, &OldIrql);

    InsertTailList(&IopDeviceActionRequestList, &Request->ListEntry);

    if (!IopDeviceActionInProgress)
    {
        IopDeviceActionInProgress = TRUE;
        Queue = TRUE;
    }

    KeReleaseSpinLock(&IopDeviceActionLock, OldIrql);

    if (!Queue) return;

    ExInitializeWorkItem(&IopDeviceActionWorkItem,
                         PiDeviceActionWorker,
                         NULL);
    ExQueueWorkItem(&IopDeviceActionWorkItem, DelayedWorkQueue);
}

NTSTATUS
PiPerformSyncDeviceAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_ACTION Action)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    KEVENT Event;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    PiQueueDeviceAction(DeviceObject, Action, &Event, &Status);

    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    return Status;
}
