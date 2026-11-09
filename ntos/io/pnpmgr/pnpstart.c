/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the PnP manager and the root of the device tree up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

static const UNICODE_STRING IopRootEnumName = RTL_CONSTANT_STRING(ENUM_ROOT);

static const UNICODE_STRING IopRootInstancePath =
    RTL_CONSTANT_STRING(L"HTREE\\ROOT\\0");

static const UNICODE_STRING IopControlPnpName = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Pnp");

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopUpdateRootKey(VOID)
{
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE EnumKey;
    HANDLE RootKey;
    HANDLE Instance;

    PAGED_CODE();

    Status = IopCreateDeviceKeyPath(&IopRootEnumName,
                                    REG_OPTION_NON_VOLATILE,
                                    &EnumKey);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&Name, L"HTREE");

    Status = IopCreateRegistryKeyEx(&RootKey,
                                    EnumKey,
                                    &Name,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    ZwClose(EnumKey);

    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&Name, L"ROOT");

    Status = IopCreateRegistryKeyEx(&Instance,
                                    RootKey,
                                    &Name,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    ZwClose(RootKey);

    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&Name, L"0");

    Status = IopCreateRegistryKeyEx(&RootKey,
                                    Instance,
                                    &Name,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    ZwClose(Instance);

    if (!NT_SUCCESS(Status)) return Status;

    ZwClose(RootKey);

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
PiInitPhase0(VOID)
{
    PDEVICE_OBJECT RootDevice = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    KeInitializeSpinLock(&IopDeviceActionLock);
    InitializeListHead(&IopDeviceActionRequestList);

    ExInitializeFastMutex(&PnpBusTypeGuidList.Lock);
    PnpBusTypeGuidList.AllocatedCount = 0;
    PnpBusTypeGuidList.GuidCount = 0;
    PnpBusTypeGuidList.Guids = NULL;

    ExInitializeResourceLite(&PpRegistryDeviceResource);

    PpInitializeDeviceReferenceTable();
    PnpRootInitializeDevExtension();

    Status = PnpRootDriverEntry(NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnP: the root bus driver did not come up (%lx)\n", Status);
        return;
    }

    Status = PnpRootCreateDeviceObject(&RootDevice);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PnP: the root device could not be made (%lx)\n", Status);
        return;
    }

    IopRootDeviceNode = PipAllocateDeviceNode(RootDevice);
    if (IopRootDeviceNode == NULL)
    {
        DPRINT1("PnP: the root device node could not be made\n");
        return;
    }

    IopRootDeviceNode->Parent = NULL;
    IopRootDeviceNode->Level = 0;

    PiSetDevNodeFlag(IopRootDeviceNode,
                     DNF_MADEUP | DNF_ENUMERATED | DNF_IDS_QUERIED |
                         DNF_NO_RESOURCE_REQUIRED);

    IopRootDeviceNode->InstancePath.Buffer =
        ExAllocatePoolZero(NonPagedPool,
                           IopRootInstancePath.Length + sizeof(WCHAR),
                           TAG_IO_DEVNODE);
    if (IopRootDeviceNode->InstancePath.Buffer != NULL)
    {
        RtlCopyMemory(IopRootDeviceNode->InstancePath.Buffer,
                      IopRootInstancePath.Buffer,
                      IopRootInstancePath.Length);

        IopRootDeviceNode->InstancePath.Length = IopRootInstancePath.Length;
        IopRootDeviceNode->InstancePath.MaximumLength =
            IopRootInstancePath.Length + sizeof(WCHAR);
    }

    PiSetDevNodeState(IopRootDeviceNode, DeviceNodeStarted);

    RootDevice->Flags &= ~DO_DEVICE_INITIALIZING;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PpInitSystem(VOID)
{
    PAGED_CODE();

    PiInitializeNotifications();

    PiInitPhase0();

    if (IopRootDeviceNode == NULL) return FALSE;

    if (!NT_SUCCESS(IopInitPlugPlayEvents())) return FALSE;

    if (!NT_SUCCESS(IopUpdateRootKey()))
    {
        DPRINT1("PnP: the enumeration root key could not be made\n");
    }

    return TRUE;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopInitializePlugPlayServices(VOID)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    HANDLE ControlKey;
    ULONG Value;

    PAGED_CODE();

    if (IopRootDeviceNode == NULL) return STATUS_UNSUCCESSFUL;

    Status = IopCreateRegistryKeyEx(&ControlKey,
                                    NULL,
                                    (PUNICODE_STRING)&IopControlPnpName,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, L"DeviceNodeCount");

        Value = IopNumberDeviceNodes;

        ZwSetValueKey(ControlKey,
                      &ValueName,
                      0,
                      REG_DWORD,
                      &Value,
                      sizeof(Value));

        ZwClose(ControlKey);
    }

    return PiPerformSyncDeviceAction(
        IopRootDeviceNode->PhysicalDeviceObject,
        PiActionEnumRootDevices);
}

CODE_SEG("INIT")
VOID
PnpSystemInit(VOID)
{
    PAGED_CODE();

    IopInitializePlugPlayServices();
}

VOID
PnpInit2(VOID)
{
    PAGED_CODE();

    if (IopRootDeviceNode == NULL) return;

    PiQueueDeviceAction(IopRootDeviceNode->PhysicalDeviceObject,
                        PiActionEnumDeviceTree,
                        NULL,
                        NULL);
}
