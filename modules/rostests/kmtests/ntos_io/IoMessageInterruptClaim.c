/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A message interrupt does not hold the interrupt line its vector number names
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

/* ok_eq_hex evaluates its value twice: a call with an effect goes through this once */
#define ok_once(call, expected) \
    do { NTSTATUS Once_ = (call); ok(Once_ == (expected), #call " = 0x%08lx, expected 0x%08lx\n", Once_, (NTSTATUS)(expected)); } while (0)

#define TEST_VECTOR 0xDE

static
VOID
DescribeInterrupt(
    _Out_ PCM_RESOURCE_LIST Claim,
    _In_ BOOLEAN Message)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;

    RtlZeroMemory(Claim, sizeof(*Claim));
    Claim->Count = 1;
    Claim->List[0].InterfaceType = Internal;
    Claim->List[0].PartialResourceList.Version = 1;
    Claim->List[0].PartialResourceList.Revision = 1;
    Claim->List[0].PartialResourceList.Count = 1;

    Descriptor = &Claim->List[0].PartialResourceList.PartialDescriptors[0];
    Descriptor->Type = CmResourceTypeInterrupt;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    if (Message)
    {
        Descriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED | CM_RESOURCE_INTERRUPT_MESSAGE;
        Descriptor->u.MessageInterrupt.Raw.MessageCount = 1;
        Descriptor->u.MessageInterrupt.Raw.Vector = TEST_VECTOR;
        Descriptor->u.MessageInterrupt.Raw.Affinity = 1;
    }
    else
    {
        Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Descriptor->u.Interrupt.Level = TEST_VECTOR;
        Descriptor->u.Interrupt.Vector = TEST_VECTOR;
        Descriptor->u.Interrupt.Affinity = 1;
    }
}

static
NTSTATUS
Claim(
    _In_opt_ PDEVICE_OBJECT Device,
    _In_ BOOLEAN Message,
    _Out_ PBOOLEAN Conflict)
{
    CM_RESOURCE_LIST List;

    DescribeInterrupt(&List, Message);
    if (Device != NULL)
        return IoReportResourceUsage(NULL, KmtDriverObject, NULL, 0, Device, &List, sizeof(List), FALSE, Conflict);
    return IoReportResourceUsage(NULL, KmtDriverObject, &List, sizeof(List), NULL, NULL, 0, FALSE, Conflict);
}

static
VOID
Release(
    _In_opt_ PDEVICE_OBJECT Device)
{
    CM_RESOURCE_LIST List;
    BOOLEAN Conflict;

    RtlZeroMemory(&List, sizeof(List));
    List.Count = 1;
    List.List[0].InterfaceType = Internal;
    List.List[0].PartialResourceList.Version = 1;
    List.List[0].PartialResourceList.Revision = 1;
    if (Device != NULL)
        IoReportResourceUsage(NULL, KmtDriverObject, NULL, 0, Device, &List, sizeof(List), FALSE, &Conflict);
    else
        IoReportResourceUsage(NULL, KmtDriverObject, &List, sizeof(List), NULL, NULL, 0, FALSE, &Conflict);
}

START_TEST(IoMessageInterruptClaim)
{
    PDEVICE_OBJECT Device = KmtDriverObject->DeviceObject;
    BOOLEAN Conflict;

    if (skip(Device != NULL, "The test driver has no device\n"))
        return;

    /* Two claims of one line conflict */
    ok_once(Claim(NULL, FALSE, &Conflict), STATUS_SUCCESS);
    ok_once(Claim(Device, FALSE, &Conflict), STATUS_CONFLICTING_ADDRESSES);
    ok(Conflict, "two exclusive claims of line 0x%x did not conflict\n", TEST_VECTOR);

    /* A message with the same number leaves the line free */
    ok_once(Claim(NULL, TRUE, &Conflict), STATUS_SUCCESS);
    ok_once(Claim(Device, FALSE, &Conflict), STATUS_SUCCESS);
    ok(!Conflict, "a message with vector 0x%x took line 0x%x\n", TEST_VECTOR, TEST_VECTOR);

    Release(Device);
    Release(NULL);
}
