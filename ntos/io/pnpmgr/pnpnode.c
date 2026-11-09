/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The tree of device nodes and what each node remembers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PDEVICE_NODE IopRootDeviceNode = NULL;
PDRIVER_OBJECT IopRootDriverObject = NULL;
ULONG IopNumberDeviceNodes = 0;

LIST_ENTRY PpDeviceReferenceTable;
KGUARDED_MUTEX PpDeviceReferenceTableLock;

/* PRIVATE DATA ***************************************************************/

static const CHAR *IopDevNodeStateNames[] =
{
    "DeviceNodeUnspecified",
    "DeviceNodeUninitialized",
    "DeviceNodeInitialized",
    "DeviceNodeDriversAdded",
    "DeviceNodeResourcesAssigned",
    "DeviceNodeStartPending",
    "DeviceNodeStartCompletion",
    "DeviceNodeStartPostWork",
    "DeviceNodeStarted",
    "DeviceNodeQueryStopped",
    "DeviceNodeStopped",
    "DeviceNodeRestartCompletion",
    "DeviceNodeEnumeratePending",
    "DeviceNodeEnumerateCompletion",
    "DeviceNodeAwaitingQueuedDeletion",
    "DeviceNodeAwaitingQueuedRemoval",
    "DeviceNodeQueryRemoved",
    "DeviceNodeRemovePendingCloses",
    "DeviceNodeRemoved",
    "DeviceNodeDeletePendingCloses",
    "DeviceNodeDeleted"
};

/* PRIVATE FUNCTIONS **********************************************************/

PCSTR
PipGetDeviceNodeStateName(
    _In_ PNP_DEVNODE_STATE State)
{
    ULONG Index;

    if (State < DeviceNodeUnspecified || State > DeviceNodeDeleted)
    {
        return "DeviceNodeUnknown";
    }

    Index = (ULONG)State - (ULONG)DeviceNodeUnspecified;

    return IopDevNodeStateNames[Index];
}

VOID
PpInitializeDeviceReferenceTable(VOID)
{
    InitializeListHead(&PpDeviceReferenceTable);
    KeInitializeGuardedMutex(&PpDeviceReferenceTableLock);
}

LONG
PiCompareInstancePath(
    _In_ PCUNICODE_STRING First,
    _In_ PCUNICODE_STRING Second)
{
    return RtlCompareUnicodeString((PUNICODE_STRING)First,
                                   (PUNICODE_STRING)Second,
                                   TRUE);
}

PIOP_DEVICE_REFERENCE
PiAllocateGenericTableEntry(
    _In_ PUNICODE_STRING InstancePath)
{
    PIOP_DEVICE_REFERENCE Entry;
    PWCHAR Buffer;

    PAGED_CODE();

    Entry = ExAllocatePoolZero(PagedPool,
                               sizeof(IOP_DEVICE_REFERENCE),
                               TAG_IO_DEVNODE);
    if (Entry == NULL) return NULL;

    Buffer = ExAllocatePoolZero(PagedPool,
                                InstancePath->Length + sizeof(WCHAR),
                                TAG_IO_DEVNODE);
    if (Buffer == NULL)
    {
        ExFreePoolWithTag(Entry, TAG_IO_DEVNODE);
        return NULL;
    }

    RtlCopyMemory(Buffer, InstancePath->Buffer, InstancePath->Length);

    Entry->InstancePath.Buffer = Buffer;
    Entry->InstancePath.Length = InstancePath->Length;
    Entry->InstancePath.MaximumLength =
        InstancePath->Length + sizeof(WCHAR);

    return Entry;
}

VOID
PiFreeGenericTableEntry(
    _In_ PIOP_DEVICE_REFERENCE Entry)
{
    PAGED_CODE();

    if (Entry->InstancePath.Buffer != NULL)
    {
        ExFreePoolWithTag(Entry->InstancePath.Buffer, TAG_IO_DEVNODE);
    }

    ExFreePoolWithTag(Entry, TAG_IO_DEVNODE);
}

PDEVICE_NODE
FASTCALL
IopGetDeviceNode(
    IN PDEVICE_OBJECT DeviceObject)
{
    if (DeviceObject == NULL) return NULL;
    if (DeviceObject->DeviceObjectExtension == NULL) return NULL;

    return IoGetDevObjExtension(DeviceObject)->DeviceNode;
}

PDEVICE_NODE
PipAllocateDeviceNode(
    IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_NODE DeviceNode;

    PAGED_CODE();

    DeviceNode = ExAllocatePoolZero(NonPagedPool,
                                    sizeof(DEVICE_NODE),
                                    TAG_IO_DEVNODE);
    if (DeviceNode == NULL) return NULL;

    if (PhysicalDeviceObject != NULL)
    {
        if (IoGetDevObjExtension(PhysicalDeviceObject)->DeviceNode != NULL)
        {
            ExFreePoolWithTag(DeviceNode, TAG_IO_DEVNODE);
            return NULL;
        }

        DeviceNode->PhysicalDeviceObject = PhysicalDeviceObject;
        IoGetDevObjExtension(PhysicalDeviceObject)->DeviceNode = DeviceNode;

        ObReferenceObject(PhysicalDeviceObject);
    }

    DeviceNode->InterfaceType = InterfaceTypeUndefined;
    DeviceNode->ChildInterfaceType = InterfaceTypeUndefined;
    DeviceNode->ChildBusTypeIndex = (USHORT)-1;
    DeviceNode->State = DeviceNodeUninitialized;
    DeviceNode->PreviousState = DeviceNodeUnspecified;
    DeviceNode->CompletionStatus = STATUS_SUCCESS;

    InitializeListHead(&DeviceNode->DeviceArbiterList);
    InitializeListHead(&DeviceNode->DeviceTranslatorList);
    InitializeListHead(&DeviceNode->TargetDeviceNotify);
    InitializeListHead(&DeviceNode->DockInfo.ListEntry);
    InitializeListHead(&DeviceNode->PendedSetInterfaceState);
    InitializeListHead(&DeviceNode->LegacyBusListEntry);

    InterlockedIncrement((PLONG)&IopNumberDeviceNodes);

    return DeviceNode;
}

VOID
PiInsertDevNode(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PDEVICE_NODE ParentNode)
{
    KIRQL OldIrql;

    ASSERT(DeviceNode->Parent == NULL);

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    DeviceNode->Parent = ParentNode;
    DeviceNode->Level = ParentNode->Level + 1;
    DeviceNode->Sibling = NULL;

    if (ParentNode->LastChild == NULL)
    {
        ParentNode->Child = DeviceNode;
    }
    else
    {
        ParentNode->LastChild->Sibling = DeviceNode;
    }

    ParentNode->LastChild = DeviceNode;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

VOID
PiUnlinkDevNode(
    _In_ PDEVICE_NODE DeviceNode)
{
    PDEVICE_NODE Parent = DeviceNode->Parent;
    PDEVICE_NODE Previous = NULL;
    PDEVICE_NODE Current;
    KIRQL OldIrql;

    if (Parent == NULL) return;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Current = Parent->Child;
    while (Current != NULL && Current != DeviceNode)
    {
        Previous = Current;
        Current = Current->Sibling;
    }

    if (Current == DeviceNode)
    {
        if (Previous == NULL)
        {
            Parent->Child = DeviceNode->Sibling;
        }
        else
        {
            Previous->Sibling = DeviceNode->Sibling;
        }

        if (Parent->LastChild == DeviceNode) Parent->LastChild = Previous;
    }

    DeviceNode->PreviousParent = Parent;
    DeviceNode->Parent = NULL;
    DeviceNode->Sibling = NULL;

    Parent->DeletedChildren++;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

NTSTATUS
IopFreeDeviceNode(
    IN PDEVICE_NODE DeviceNode)
{
    PAGED_CODE();

    if (DeviceNode == NULL) return STATUS_INVALID_PARAMETER;
    if (DeviceNode->Child != NULL) return STATUS_DEVICE_BUSY;

    PiUnlinkDevNode(DeviceNode);

    if (DeviceNode->ResourceList != NULL)
    {
        ExFreePool(DeviceNode->ResourceList);
        DeviceNode->ResourceList = NULL;
    }

    if (DeviceNode->ResourceListTranslated != NULL)
    {
        ExFreePool(DeviceNode->ResourceListTranslated);
        DeviceNode->ResourceListTranslated = NULL;
    }

    if (DeviceNode->ResourceRequirements != NULL)
    {
        ExFreePool(DeviceNode->ResourceRequirements);
        DeviceNode->ResourceRequirements = NULL;
    }

    if (DeviceNode->BootResources != NULL)
    {
        ExFreePool(DeviceNode->BootResources);
        DeviceNode->BootResources = NULL;
    }

    if (DeviceNode->BootResourcesTranslated != NULL)
    {
        ExFreePool(DeviceNode->BootResourcesTranslated);
        DeviceNode->BootResourcesTranslated = NULL;
    }

    if (DeviceNode->InstancePath.Buffer != NULL)
    {
        ExFreePool(DeviceNode->InstancePath.Buffer);
        RtlInitEmptyUnicodeString(&DeviceNode->InstancePath, NULL, 0);
    }

    if (DeviceNode->ServiceName.Buffer != NULL)
    {
        ExFreePool(DeviceNode->ServiceName.Buffer);
        RtlInitEmptyUnicodeString(&DeviceNode->ServiceName, NULL, 0);
    }

    if (DeviceNode->DockInfo.SerialNumber != NULL)
    {
        ExFreePool(DeviceNode->DockInfo.SerialNumber);
        DeviceNode->DockInfo.SerialNumber = NULL;
    }

    IopResDbRelease(DeviceNode);

    if (DeviceNode->PhysicalDeviceObject != NULL)
    {
        IoGetDevObjExtension(DeviceNode->PhysicalDeviceObject)->DeviceNode =
            NULL;
        ObDereferenceObject(DeviceNode->PhysicalDeviceObject);
        DeviceNode->PhysicalDeviceObject = NULL;
    }

    InterlockedDecrement((PLONG)&IopNumberDeviceNodes);

    ExFreePoolWithTag(DeviceNode, TAG_IO_DEVNODE);

    return STATUS_SUCCESS;
}

PNP_DEVNODE_STATE
PiSetDevNodeState(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PNP_DEVNODE_STATE NewState)
{
    PNP_DEVNODE_STATE Previous;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Previous = DeviceNode->State;

    if (NewState != Previous)
    {
        DeviceNode->StateHistory[DeviceNode->StateHistoryEntry] = Previous;
        DeviceNode->StateHistoryEntry =
            (DeviceNode->StateHistoryEntry + 1) % DEVNODE_HISTORY_SIZE;

        DeviceNode->PreviousState = Previous;
        DeviceNode->State = NewState;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    DPRINT("PnP: %wZ moved from %s to %s\n",
           &DeviceNode->InstancePath,
           PipGetDeviceNodeStateName(Previous),
           PipGetDeviceNodeStateName(NewState));

    return Previous;
}

VOID
PiSetDevNodeProblem(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ UINT32 Problem)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    DeviceNode->Flags |= DNF_HAS_PROBLEM;
    DeviceNode->Problem = Problem;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

VOID
PiClearDevNodeProblem(
    _In_ PDEVICE_NODE DeviceNode)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    DeviceNode->Flags &= ~DNF_HAS_PROBLEM;
    DeviceNode->Problem = 0;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

VOID
PiSetDevNodeFlag(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    DeviceNode->Flags |= Flags;
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

VOID
PiClearDevNodeFlag(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);
    DeviceNode->Flags &= ~Flags;
    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

VOID
PiSetDevNodeText(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ HANDLE InstanceKey)
{
    UNICODE_STRING ValueName;
    PWSTR DeviceText = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PiIrpQueryDeviceText(DeviceNode,
                                  PsDefaultSystemLocaleId,
                                  DeviceTextDescription,
                                  &DeviceText);
    if (NT_SUCCESS(Status) && DeviceText != NULL)
    {
        RtlInitUnicodeString(&ValueName, L"DeviceDesc");

        ZwSetValueKey(InstanceKey,
                      &ValueName,
                      0,
                      REG_SZ,
                      DeviceText,
                      (ULONG)((wcslen(DeviceText) + 1) * sizeof(WCHAR)));

        ExFreePool(DeviceText);
        DeviceText = NULL;
    }

    Status = PiIrpQueryDeviceText(DeviceNode,
                                  PsDefaultSystemLocaleId,
                                  DeviceTextLocationInformation,
                                  &DeviceText);
    if (NT_SUCCESS(Status) && DeviceText != NULL)
    {
        RtlInitUnicodeString(&ValueName, L"LocationInformation");

        ZwSetValueKey(InstanceKey,
                      &ValueName,
                      0,
                      REG_SZ,
                      DeviceText,
                      (ULONG)((wcslen(DeviceText) + 1) * sizeof(WCHAR)));

        ExFreePool(DeviceText);
    }
}

VOID
NTAPI
PiDeviceObjectClosed(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    PDEVICE_NODE DeviceNode;

    Extension = IoGetDevObjExtension(DeviceObject);
    DeviceNode = Extension->DeviceNode;

    if (DeviceNode == NULL) return;

    if (DeviceNode->State == DeviceNodeRemovePendingCloses ||
        DeviceNode->State == DeviceNodeDeletePendingCloses)
    {
        PiQueueDeviceAction(DeviceNode->PhysicalDeviceObject,
                            PiActionCloseRemovedDevice,
                            NULL,
                            NULL);
    }
}

NTSTATUS
IopTraverseDeviceTree(
    PDEVICETREE_TRAVERSE_CONTEXT Context)
{
    PDEVICE_NODE Node;
    NTSTATUS Status;

    PAGED_CODE();

    Node = Context->FirstDeviceNode;
    if (Node == NULL) return STATUS_SUCCESS;

    while (TRUE)
    {
        Context->DeviceNode = Node;

        Status = Context->Action(Node, Context->Context);
        if (Status == STATUS_UNSUCCESSFUL) return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status)) return Status;

        if (Node->Child != NULL)
        {
            Node = Node->Child;
            continue;
        }

        while (Node != Context->FirstDeviceNode && Node->Sibling == NULL)
        {
            Node = Node->Parent;
            if (Node == NULL) return STATUS_SUCCESS;
        }

        if (Node == Context->FirstDeviceNode) return STATUS_SUCCESS;

        Node = Node->Sibling;
    }
}

typedef struct _IOP_FIND_INSTANCE_CONTEXT
{
    PUNICODE_STRING InstancePath;
    PDEVICE_OBJECT DeviceObject;
} IOP_FIND_INSTANCE_CONTEXT, *PIOP_FIND_INSTANCE_CONTEXT;

NTSTATUS
IopFindDeviceInstanceTraverse(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Context)
{
    PIOP_FIND_INSTANCE_CONTEXT Find = Context;

    if (DeviceNode->InstancePath.Length == 0) return STATUS_SUCCESS;

    if (PiCompareInstancePath(&DeviceNode->InstancePath,
                              Find->InstancePath) != 0)
    {
        return STATUS_SUCCESS;
    }

    Find->DeviceObject = DeviceNode->PhysicalDeviceObject;
    ObReferenceObject(Find->DeviceObject);

    return STATUS_UNSUCCESSFUL;
}

PDEVICE_OBJECT
IopGetDeviceObjectFromDeviceInstance(
    _In_ PUNICODE_STRING DeviceInstance)
{
    DEVICETREE_TRAVERSE_CONTEXT Context;
    IOP_FIND_INSTANCE_CONTEXT Find;

    PAGED_CODE();

    if (IopRootDeviceNode == NULL) return NULL;

    Find.InstancePath = DeviceInstance;
    Find.DeviceObject = NULL;

    IopInitDeviceTreeTraverseContext(&Context,
                                     IopRootDeviceNode,
                                     IopFindDeviceInstanceTraverse,
                                     &Find);

    IopTraverseDeviceTree(&Context);

    return Find.DeviceObject;
}

NTSTATUS
IopGetSystemPowerDeviceObject(
    IN PDEVICE_OBJECT *DeviceObject)
{
    *DeviceObject = NULL;

    if (IopRootDeviceNode == NULL) return STATUS_UNSUCCESSFUL;

    *DeviceObject = IopRootDeviceNode->PhysicalDeviceObject;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopGetRelatedTargetDevice(
    IN PFILE_OBJECT FileObject,
    OUT PDEVICE_NODE *DeviceNode)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_NODE Node;

    PAGED_CODE();

    *DeviceNode = NULL;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (DeviceObject == NULL) return STATUS_NO_SUCH_DEVICE;

    DeviceObject = IopGetLowestDevice(DeviceObject);

    Node = IopGetDeviceNode(DeviceObject);
    if (Node == NULL) return STATUS_NO_SUCH_DEVICE;

    *DeviceNode = Node;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoGetRelatedTargetDevice(
    IN PFILE_OBJECT FileObject,
    OUT PDEVICE_OBJECT *DeviceObject)
{
    PDEVICE_NODE DeviceNode;
    NTSTATUS Status;

    PAGED_CODE();

    *DeviceObject = NULL;

    Status = IopGetRelatedTargetDevice(FileObject, &DeviceNode);
    if (!NT_SUCCESS(Status)) return Status;

    ObReferenceObject(DeviceNode->PhysicalDeviceObject);
    *DeviceObject = DeviceNode->PhysicalDeviceObject;

    return STATUS_SUCCESS;
}
