/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reads how the test VM's e1000e adapter signals its interrupts, for IoAffinityPolicy
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define E1000E_VENDOR 0x8086
#define E1000E_DEVICE 0x10D3
#define MESSAGE_CONTROL 2
#define MSIX_ENABLE 0x8000
#define MSI_ENABLE 0x0001
#define RESULT_KEY L"\\Registry\\Machine\\SOFTWARE\\KmtIoAffinityPolicy"

static
BOOLEAN
FindAdapter(
    _Out_ PULONG Slot)
{
    PCI_SLOT_NUMBER Candidate;
    USHORT Ids[2];
    ULONG Device, Function;

    for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
    {
        for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
        {
            Candidate.u.AsULONG = 0;
            Candidate.u.bits.DeviceNumber = Device;
            Candidate.u.bits.FunctionNumber = Function;

            if (HalGetBusDataByOffset(PCIConfiguration, 0, Candidate.u.AsULONG, Ids, 0, sizeof(Ids)) != sizeof(Ids))
                continue;

            if (Ids[0] == E1000E_VENDOR && Ids[1] == E1000E_DEVICE)
            {
                *Slot = Candidate.u.AsULONG;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static
UCHAR
FindCapability(
    _In_ ULONG Slot,
    _In_ UCHAR Id)
{
    UCHAR Pointer = 0;
    UCHAR Header[2];
    ULONG Steps;

    HalGetBusDataByOffset(PCIConfiguration, 0, Slot, &Pointer, FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.CapabilitiesPtr), 1);

    for (Steps = 0; Pointer != 0 && Steps < 48; Steps++)
    {
        if (HalGetBusDataByOffset(PCIConfiguration, 0, Slot, Header, Pointer, sizeof(Header)) != sizeof(Header))
            return 0;
        if (Header[0] == Id)
            return Pointer;
        Pointer = Header[1];
    }

    return 0;
}

static
USHORT
ReadControl(
    _In_ ULONG Slot,
    _In_ UCHAR Capability)
{
    USHORT Control = 0;

    if (Capability != 0)
        HalGetBusDataByOffset(PCIConfiguration, 0, Slot, &Control, Capability + MESSAGE_CONTROL, sizeof(Control));

    return Control;
}

static
VOID
WriteResult(
    _In_ PCWSTR Name,
    _In_ ULONG Type,
    _In_reads_bytes_(Size) PVOID Data,
    _In_ ULONG Size)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(RESULT_KEY);
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING ValueName;
    HANDLE Key;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateKey(&Key, KEY_SET_VALUE, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwSetValueKey(Key, &ValueName, 0, Type, Data, Size);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ZwClose(Key);
}

/* Measures only; IoAffinityPolicy runs it after each restart and checks the results it leaves */
START_TEST(HalMessageState)
{
    ULONG Slot, MsixEnabled, MsiEnabled, IntxDisabled;
    USHORT Control, Command = 0;
    UCHAR Msix;

    if (skip(FindAdapter(&Slot), "No e1000e adapter on bus 0\n"))
        return;

    Msix = FindCapability(Slot, PCI_CAPABILITY_ID_MSIX);
    ok(Msix != 0, "the adapter has no MSI-X capability\n");

    Control = ReadControl(Slot, Msix);
    MsixEnabled = (Control & MSIX_ENABLE) ? 1 : 0;
    MsiEnabled = (ReadControl(Slot, FindCapability(Slot, PCI_CAPABILITY_ID_MSI)) & MSI_ENABLE) ? 1 : 0;
    HalGetBusDataByOffset(PCIConfiguration, 0, Slot, &Command, FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(Command));
    IntxDisabled = (Command & PCI_DISABLE_LEVEL_INTERRUPT) ? 1 : 0;

    WriteResult(L"MsixEnabled", REG_DWORD, &MsixEnabled, sizeof(MsixEnabled));
    WriteResult(L"MsiEnabled", REG_DWORD, &MsiEnabled, sizeof(MsiEnabled));
    WriteResult(L"IntxDisabled", REG_DWORD, &IntxDisabled, sizeof(IntxDisabled));
}
