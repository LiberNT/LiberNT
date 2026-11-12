/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The tree of device nodes and what each node remembers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <devpkey.h>
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
    DeviceNode->RemovalPolicy = RemovalPolicyExpectNoRemoval;
    DeviceNode->HardwareRemovalPolicy = RemovalPolicyExpectNoRemoval;
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

    PAGED_CODE();
    ASSERT(DeviceNode->Parent == NULL);
    ASSERT(!ExIsResourceAcquiredSharedLite(&IopDeviceTreeResource));

    /* The tree's walkers hold its resource: a node joins the tree only while none of them runs */
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&IopDeviceTreeResource, TRUE);
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
    ExReleaseResourceLite(&IopDeviceTreeResource);
    KeLeaveCriticalRegion();
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
        if (DeviceNode->State != DeviceNodeDeleted)
        {
            ObDereferenceObject(DeviceNode->PhysicalDeviceObject);
        }
        DeviceNode->PhysicalDeviceObject = NULL;
    }

    InterlockedDecrement((PLONG)&IopNumberDeviceNodes);

    ExFreePoolWithTag(DeviceNode, TAG_IO_DEVNODE);

    return STATUS_SUCCESS;
}

VOID
NTAPI
PiUpdateServiceEnum(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Add)
{
    static const WCHAR ServicesPath[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    UNICODE_STRING KeyName, ValueName, CountName, NextName;
    PKEY_VALUE_FULL_INFORMATION Information;
    WCHAR IndexBuffer[12], NextBuffer[12];
    ULONG Count = 0, Index, Found, Hash = 0, i;
    HANDLE EnumKey;
    NTSTATUS Status;
    PKMUTEX Lock;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(DeviceNode != NULL);

    if (DeviceNode->ServiceName.Length == 0 || DeviceNode->InstancePath.Length == 0) return;

    KeyName.MaximumLength = (USHORT)(sizeof(ServicesPath) + DeviceNode->ServiceName.Length + sizeof(L"\\Enum"));
    KeyName.Length = 0;
    KeyName.Buffer = ExAllocatePoolWithTag(PagedPool, KeyName.MaximumLength, TAG_IO);
    if (KeyName.Buffer == NULL) return;
    RtlAppendUnicodeToString(&KeyName, ServicesPath);
    RtlAppendUnicodeStringToString(&KeyName, &DeviceNode->ServiceName);
    RtlAppendUnicodeToString(&KeyName, L"\\Enum");

    for (i = 0; i < DeviceNode->ServiceName.Length / sizeof(WCHAR); i++)
        Hash = Hash * 31 + RtlUpcaseUnicodeChar(DeviceNode->ServiceName.Buffer[i]);
    Lock = &IopServiceEnumLocks[Hash % IOP_SERVICE_ENUM_LOCK_COUNT];
    KeWaitForSingleObject(Lock, Executive, KernelMode, FALSE, NULL);

    Status = Add ? IopCreateRegistryKeyEx(&EnumKey, NULL, &KeyName, KEY_ALL_ACCESS, REG_OPTION_VOLATILE, NULL)
                 : IopOpenRegistryKeyEx(&EnumKey, NULL, &KeyName, KEY_ALL_ACCESS);
    ExFreePoolWithTag(KeyName.Buffer, TAG_IO);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(Lock, FALSE);
        return;
    }

    if (NT_SUCCESS(IopGetRegistryValue(EnumKey, L"Count", &Information)))
    {
        if (Information->Type == REG_DWORD && Information->DataLength == sizeof(ULONG))
            Count = *(PULONG)((PUCHAR)Information + Information->DataOffset);
        ExFreePool(Information);
    }

    Found = MAXULONG;
    for (Index = 0; Index < Count && Found == MAXULONG; Index++)
    {
        RtlStringCbPrintfW(IndexBuffer, sizeof(IndexBuffer), L"%lu", Index);
        if (!NT_SUCCESS(IopGetRegistryValue(EnumKey, IndexBuffer, &Information))) continue;
        if (Information->Type == REG_SZ)
        {
            UNICODE_STRING Listed;

            Listed.Buffer = (PWCHAR)((PUCHAR)Information + Information->DataOffset);
            Listed.Length = (USHORT)Information->DataLength;
            while (Listed.Length >= sizeof(WCHAR) && Listed.Buffer[Listed.Length / sizeof(WCHAR) - 1] == UNICODE_NULL)
                Listed.Length -= sizeof(WCHAR);
            Listed.MaximumLength = Listed.Length;
            if (RtlEqualUnicodeString(&Listed, &DeviceNode->InstancePath, TRUE)) Found = Index;
        }
        ExFreePool(Information);
    }

    RtlInitUnicodeString(&CountName, L"Count");
    RtlInitUnicodeString(&NextName, L"NextInstance");

    if (Add && Found == MAXULONG)
    {
        RtlStringCbPrintfW(IndexBuffer, sizeof(IndexBuffer), L"%lu", Count);
        RtlInitUnicodeString(&ValueName, IndexBuffer);
        Status = ZwSetValueKey(EnumKey, &ValueName, 0, REG_SZ, DeviceNode->InstancePath.Buffer,
                               DeviceNode->InstancePath.Length + sizeof(WCHAR));
        if (NT_SUCCESS(Status))
        {
            Count++;
            ZwSetValueKey(EnumKey, &CountName, 0, REG_DWORD, &Count, sizeof(Count));
            ZwSetValueKey(EnumKey, &NextName, 0, REG_DWORD, &Count, sizeof(Count));
        }
    }
    else if (!Add && Found != MAXULONG)
    {
        ASSERT(Found < Count);
        for (Index = Found + 1; Index < Count; Index++)
        {
            RtlStringCbPrintfW(IndexBuffer, sizeof(IndexBuffer), L"%lu", Index);
            if (!NT_SUCCESS(IopGetRegistryValue(EnumKey, IndexBuffer, &Information))) continue;
            RtlStringCbPrintfW(NextBuffer, sizeof(NextBuffer), L"%lu", Index - 1);
            RtlInitUnicodeString(&ValueName, NextBuffer);
            ZwSetValueKey(EnumKey, &ValueName, 0, Information->Type,
                          (PUCHAR)Information + Information->DataOffset, Information->DataLength);
            ExFreePool(Information);
        }
        RtlStringCbPrintfW(IndexBuffer, sizeof(IndexBuffer), L"%lu", Count - 1);
        RtlInitUnicodeString(&ValueName, IndexBuffer);
        ZwDeleteValueKey(EnumKey, &ValueName);
        Count--;
        ZwSetValueKey(EnumKey, &CountName, 0, REG_DWORD, &Count, sizeof(Count));
        ZwSetValueKey(EnumKey, &NextName, 0, REG_DWORD, &Count, sizeof(Count));
    }

    ZwClose(EnumKey);
    KeReleaseMutex(Lock, FALSE);
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

    if (NewState == DeviceNodeRemoved && Previous != DeviceNodeRemoved)
        PiUpdateServiceEnum(DeviceNode, FALSE);

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
    ULONG TextSize, ResultLength;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PiIrpQueryDeviceText(DeviceNode,
                                  PsDefaultSystemLocaleId,
                                  DeviceTextDescription,
                                  &DeviceText);
    if (NT_SUCCESS(Status) && DeviceText != NULL)
    {
        TextSize = (ULONG)((wcslen(DeviceText) + 1) * sizeof(WCHAR));
        PiSetInstanceProperty(InstanceKey,
                              &DEVPKEY_Device_BusReportedDeviceDesc,
                              DEVPROP_TYPE_STRING,
                              TextSize,
                              DeviceText);

        RtlInitUnicodeString(&ValueName, L"DeviceDesc");
        if (ZwQueryValueKey(InstanceKey,
                            &ValueName,
                            KeyValuePartialInformation,
                            NULL,
                            0,
                            &ResultLength) == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            ZwSetValueKey(InstanceKey,
                          &ValueName,
                          0,
                          REG_SZ,
                          DeviceText,
                          TextSize);
        }

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

/* Whatever device of a stack was closed, the device node hangs off the bottom of the stack */
VOID
NTAPI
PiDeviceObjectClosed(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT PhysicalDeviceObject = DeviceObject;
    PDEVICE_NODE DeviceNode;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    while (IoGetDevObjExtension(PhysicalDeviceObject)->AttachedTo != NULL)
    {
        PhysicalDeviceObject = IoGetDevObjExtension(PhysicalDeviceObject)->AttachedTo;
    }
    ObReferenceObject(PhysicalDeviceObject);

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    DeviceNode = IoGetDevObjExtension(PhysicalDeviceObject)->DeviceNode;

    if (DeviceNode != NULL &&
        (DeviceNode->State == DeviceNodeRemovePendingCloses ||
         DeviceNode->State == DeviceNodeDeletePendingCloses))
    {
        PiQueueDeviceAction(PhysicalDeviceObject,
                            PiActionCloseRemovedDevice,
                            NULL,
                            NULL);
    }

    ObDereferenceObject(PhysicalDeviceObject);
}

static
NTSTATUS
IopWalkDeviceTree(
    PDEVICETREE_TRAVERSE_CONTEXT Context)
{
    PDEVICE_NODE Node;
    NTSTATUS Status;

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

NTSTATUS
IopTraverseDeviceTree(
    PDEVICETREE_TRAVERSE_CONTEXT Context)
{
    NTSTATUS Status;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&IopDeviceTreeResource, TRUE);
    Status = IopWalkDeviceTree(Context);
    ExReleaseResourceLite(&IopDeviceTreeResource);
    KeLeaveCriticalRegion();

    return Status;
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
