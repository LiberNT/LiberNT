/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite MSI programming of started PCI functions against their assigned messages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <initguid.h>
#include <devpkey.h>

#define NDEBUG
#include <debug.h>

#define MSI_CAPABILITY_ID 0x05
#define MSI_CONTROL_ENABLE 0x0001
#define MSI_CONTROL_CAPABLE_SHIFT 1
#define MSI_CONTROL_ENABLED_SHIFT 4
#define MSI_CONTROL_COUNT_MASK 0x7
#define CAPABILITY_LIST_STATUS 0x0010
#define TAG_TEST 'emPI'

static UNICODE_STRING PciName = RTL_CONSTANT_STRING(L"\\Driver\\pci");
static UNICODE_STRING ResourceMapName =
    RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\RESOURCEMAP\\PnP Manager\\PnpManager");

static
UCHAR
FindMsiCapability(
    _In_ ULONG Bus,
    _In_ ULONG Slot)
{
    USHORT Status = 0;
    UCHAR Pointer = 0;
    UCHAR Header[2];
    ULONG Steps;

    if (HalGetBusDataByOffset(PCIConfiguration, Bus, Slot, &Status, FIELD_OFFSET(PCI_COMMON_HEADER, Status), sizeof(Status)) != sizeof(Status) ||
        !(Status & CAPABILITY_LIST_STATUS) ||
        HalGetBusDataByOffset(PCIConfiguration, Bus, Slot, &Pointer, FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.CapabilitiesPtr), sizeof(Pointer)) != sizeof(Pointer))
    {
        return 0;
    }

    for (Steps = 0; Steps < 48 && Pointer >= sizeof(PCI_COMMON_HEADER); Steps++)
    {
        Pointer &= ~3;
        if (HalGetBusDataByOffset(PCIConfiguration, Bus, Slot, Header, Pointer, sizeof(Header)) != sizeof(Header))
            return 0;
        if (Header[0] == MSI_CAPABILITY_ID)
            return Pointer;
        Pointer = Header[1];
    }

    return 0;
}

/* The resource map names a device by its instance path; Windows names it after the PDO with ".Raw" appended */
static
PKEY_VALUE_PARTIAL_INFORMATION
QueryAssignedResources(
    _In_ HANDLE MapKey,
    _In_ PDEVICE_OBJECT Pdo)
{
    PKEY_VALUE_PARTIAL_INFORMATION Value = NULL;
    UNICODE_STRING ValueName;
    WCHAR Name[256 + 4];
    POBJECT_NAME_INFORMATION ObjectName;
    ULONG Length, Attempt, Required;
    DEVPROPTYPE Type;
    NTSTATUS Status;

    for (Attempt = 0; Attempt < 2 && Value == NULL; Attempt++)
    {
        RtlZeroMemory(Name, sizeof(Name));
        if (Attempt == 0)
        {
            Status = IoGetDevicePropertyData(Pdo, &DEVPKEY_Device_InstanceId, LOCALE_NEUTRAL, 0,
                                             sizeof(Name) - sizeof(WCHAR), Name, &Required, &Type);
            if (!NT_SUCCESS(Status) || Type != DEVPROP_TYPE_STRING)
                continue;
        }
        else
        {
            ObjectName = (POBJECT_NAME_INFORMATION)Name;
            Status = ObQueryNameString(Pdo, ObjectName, sizeof(Name) - 5 * sizeof(WCHAR), &Required);
            if (!NT_SUCCESS(Status) || ObjectName->Name.Length == 0)
                continue;
            Length = ObjectName->Name.Length / sizeof(WCHAR);
            if (Length + 5 > RTL_NUMBER_OF(Name))
                continue;
            RtlMoveMemory(Name, ObjectName->Name.Buffer, Length * sizeof(WCHAR));
            RtlCopyMemory(&Name[Length], L".Raw", 5 * sizeof(WCHAR));
        }

        RtlInitUnicodeString(&ValueName, Name);
        Status = ZwQueryValueKey(MapKey, &ValueName, KeyValuePartialInformation, NULL, 0, &Length);
        if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
            continue;

        Value = ExAllocatePoolWithTag(PagedPool, Length, TAG_TEST);
        if (Value == NULL)
            break;
        Status = ZwQueryValueKey(MapKey, &ValueName, KeyValuePartialInformation, Value, Length, &Length);
        if (!NT_SUCCESS(Status) || Value->Type != REG_RESOURCE_LIST || Value->DataLength < sizeof(CM_RESOURCE_LIST))
        {
            ExFreePoolWithTag(Value, TAG_TEST);
            Value = NULL;
        }
    }

    return Value;
}

static
ULONG
AssignedMessageCount(
    _In_ PKEY_VALUE_PARTIAL_INFORMATION Value)
{
    PCM_RESOURCE_LIST List = (PCM_RESOURCE_LIST)Value->Data;
    PCM_FULL_RESOURCE_DESCRIPTOR Full = &List->List[0];
    PUCHAR End = Value->Data + Value->DataLength;
    ULONG i, j;

    for (i = 0; i < List->Count; i++)
    {
        if ((PUCHAR)&Full->PartialResourceList.PartialDescriptors[0] > End)
            return 0;
        for (j = 0; j < Full->PartialResourceList.Count; j++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial = &Full->PartialResourceList.PartialDescriptors[j];

            if ((PUCHAR)(Partial + 1) > End)
                return 0;
            if (Partial->Type == CmResourceTypeInterrupt && (Partial->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
                return Partial->u.MessageInterrupt.Raw.MessageCount ? Partial->u.MessageInterrupt.Raw.MessageCount : 1;
        }
        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)&Full->PartialResourceList.PartialDescriptors[Full->PartialResourceList.Count];
    }

    return 0;
}

static
ULONG
EnabledCountFor(
    _In_ ULONG Messages)
{
    ULONG Shift = 0;

    while ((1UL << Shift) < Messages && Shift < 5)
        Shift++;
    return Shift;
}

START_TEST(IoPciMessageEnable)
{
    OBJECT_ATTRIBUTES Attributes;
    PDRIVER_OBJECT PciDriver;
    PDEVICE_OBJECT *List;
    HANDLE MapKey;
    ULONG Count = 0, Index, Checked = 0;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&PciName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&PciDriver);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    InitializeObjectAttributes(&Attributes, &ResourceMapName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&MapKey, KEY_READ, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(PciDriver);
        return;
    }

    IoEnumerateDeviceObjectList(PciDriver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), TAG_TEST);
    ok(List != NULL, "no memory for %lu device objects\n", Count);
    if (List != NULL &&
        NT_SUCCESS(IoEnumerateDeviceObjectList(PciDriver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count)))
    {
        for (Index = 0; Index < Count; Index++)
        {
            PKEY_VALUE_PARTIAL_INFORMATION Value;
            ULONG Bus, Address, Length, Slot, Messages;
            PCI_SLOT_NUMBER SlotNumber;
            USHORT Control = 0;
            UCHAR Capability;

            if ((List[Index]->Flags & DO_BUS_ENUMERATED_DEVICE) &&
                NT_SUCCESS(IoGetDeviceProperty(List[Index], DevicePropertyBusNumber, sizeof(Bus), &Bus, &Length)) &&
                NT_SUCCESS(IoGetDeviceProperty(List[Index], DevicePropertyAddress, sizeof(Address), &Address, &Length)))
            {
                SlotNumber.u.AsULONG = 0;
                SlotNumber.u.bits.DeviceNumber = Address >> 16;
                SlotNumber.u.bits.FunctionNumber = Address & 0xFFFF;
                Slot = SlotNumber.u.AsULONG;

                Capability = FindMsiCapability(Bus, Slot);
                if (Capability != 0 &&
                    HalGetBusDataByOffset(PCIConfiguration, Bus, Slot, &Control, Capability + 2, sizeof(Control)) == sizeof(Control) &&
                    (Control & MSI_CONTROL_ENABLE))
                {
                    Value = QueryAssignedResources(MapKey, List[Index]);
                    ok(Value != NULL, "%02lx:%02lx.%lx signals MSI without assigned resources in the resource map\n",
                       Bus, Address >> 16, Address & 0xFFFF);
                    if (Value != NULL)
                    {
                        Messages = AssignedMessageCount(Value);
                        ok(Messages != 0, "%02lx:%02lx.%lx signals MSI but was assigned no message interrupt\n",
                           Bus, Address >> 16, Address & 0xFFFF);
                        if (Messages != 0)
                        {
                            ok((ULONG)((Control >> MSI_CONTROL_ENABLED_SHIFT) & MSI_CONTROL_COUNT_MASK) == EnabledCountFor(Messages),
                               "%02lx:%02lx.%lx enables %lu MSI messages (control 0x%04x, capable of %lu), its resources assign %lu\n",
                               Bus, Address >> 16, Address & 0xFFFF,
                               1UL << ((Control >> MSI_CONTROL_ENABLED_SHIFT) & MSI_CONTROL_COUNT_MASK), Control,
                               1UL << ((Control >> MSI_CONTROL_CAPABLE_SHIFT) & MSI_CONTROL_COUNT_MASK), Messages);
                            Checked++;
                        }
                        ExFreePoolWithTag(Value, TAG_TEST);
                    }
                }
            }
            ObDereferenceObject(List[Index]);
        }
    }

    if (Checked == 0)
        trace("No started PCI function signals MSI; nothing to compare\n");
    else
        trace("%lu PCI functions signal MSI\n", Checked);

    if (List != NULL)
        ExFreePoolWithTag(List, TAG_TEST);
    ZwClose(MapKey);
    ObDereferenceObject(PciDriver);
}
