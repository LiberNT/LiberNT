/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger with a started PCI device node and its interrupt vector to look at
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

PVOID KdbTestDeviceNode;
ULONG_PTR KdbTestInterruptVector;

static
BOOLEAN
KdbTestFindInterrupt(
    _In_ PCM_RESOURCE_LIST List,
    _Out_ PULONG_PTR Vector)
{
    PCM_FULL_RESOURCE_DESCRIPTOR Full = &List->List[0];
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG FullIndex;
    ULONG Index;

    for (FullIndex = 0; FullIndex < List->Count; FullIndex++)
    {
        Partial = &Full->PartialResourceList.PartialDescriptors[0];

        for (Index = 0; Index < Full->PartialResourceList.Count; Index++)
        {
            if (Partial->Type == CmResourceTypeInterrupt)
            {
                *Vector = Partial->u.Interrupt.Vector;
                return TRUE;
            }

            if (Partial->Type == CmResourceTypeDeviceSpecific)
                Partial = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)((PUCHAR)(Partial + 1) + Partial->u.DeviceSpecificData.DataSize);
            else
                Partial++;
        }

        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)Partial;
    }

    return FALSE;
}

START_TEST(KdbDeviceCommands)
{
    UNICODE_STRING PciName = RTL_CONSTANT_STRING(L"\\Driver\\PCI");
    PDEVICE_OBJECT Pdo = NULL;
    PDEVICE_OBJECT *List;
    PDRIVER_OBJECT PciDriver;
    ULONG Count = 0;
    ULONG Index;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&PciName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&PciDriver);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'dKdK');
    ok(List != NULL, "No memory for %lu device objects\n", Count);

    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(PciDriver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count);
        ok_eq_hex(Status, STATUS_SUCCESS);

        for (Index = 0; NT_SUCCESS(Status) && (Index < Count); Index++)
        {
            PEXTENDED_DEVOBJ_EXTENSION Extension = (PEXTENDED_DEVOBJ_EXTENSION)List[Index]->DeviceObjectExtension;
            PDEVICE_NODE Node = Extension->DeviceNode;

            if ((Pdo == NULL) &&
                (Node != NULL) &&
                (Node->ResourceListTranslated != NULL) &&
                KdbTestFindInterrupt(Node->ResourceListTranslated, &KdbTestInterruptVector))
            {
                KdbTestDeviceNode = Node;
                Pdo = List[Index];
                continue;
            }

            ObDereferenceObject(List[Index]);
        }

        ExFreePoolWithTag(List, 'dKdK');
    }

    ObDereferenceObject(PciDriver);

    ok(KdbTestDeviceNode != NULL, "No started PCI device has an interrupt\n");
    trace("Device node %p, interrupt vector 0x%Ix\n", KdbTestDeviceNode, KdbTestInterruptVector);

    /* The debugger's start-up file looks at the device tree and the interrupt here */
    DbgBreakPoint();

    if (Pdo != NULL)
        ObDereferenceObject(Pdo);
}
