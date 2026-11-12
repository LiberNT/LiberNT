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
#define MSIX_TABLE 4
#define MSIX_ENABLE 0x8000
#define MSIX_TABLE_SIZE 0x7FF
#define MSI_ENABLE 0x0001
#define MAXIMUM_ENTRIES 64
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

/* The local APIC ID each active processor reports, so a message's destination names a processor */
static
VOID
ReadApicIds(
    _Out_writes_(MAXIMUM_PROCESSORS) PULONG ApicIds)
{
    KAFFINITY Active = KeQueryActiveProcessors();
    INT Registers[4];
    ULONG Processor;

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        ApicIds[Processor] = MAXULONG;
        if (!(Active & AFFINITY_MASK(Processor))) continue;

        KeSetSystemAffinityThread(AFFINITY_MASK(Processor));
        __cpuid(Registers, 1);
        ApicIds[Processor] = ((ULONG)Registers[1] >> 24) & 0xFF;
    }

    KeRevertToUserAffinityThread();
}

/* The processor each unmasked MSI-X table entry is delivered to, MAXULONG when masked or not an active processor */
static
VOID
ReadDestinations(
    _In_ ULONG Slot,
    _In_ UCHAR Msix,
    _In_ ULONG Count,
    _Out_writes_(Count) PULONG Processors)
{
    ULONG ApicIds[MAXIMUM_PROCESSORS];
    ULONG Table = 0, Bar[2] = { 0, 0 };
    ULONG Index, Processor, Destination;
    PHYSICAL_ADDRESS Address;
    PULONG Entries;

    for (Index = 0; Index < Count; Index++) Processors[Index] = MAXULONG;

    HalGetBusDataByOffset(PCIConfiguration, 0, Slot, &Table, Msix + MSIX_TABLE, sizeof(Table));
    HalGetBusDataByOffset(PCIConfiguration, 0, Slot, &Bar[0],
                          FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[Table & 7]), sizeof(ULONG));
    if ((Bar[0] & 6) == 4)
    {
        HalGetBusDataByOffset(PCIConfiguration, 0, Slot, &Bar[1],
                              FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[(Table & 7) + 1]), sizeof(ULONG));
    }

    Address.LowPart = (Bar[0] & ~0xFUL) + (Table & ~7UL);
    Address.HighPart = (LONG)Bar[1];

    Entries = MmMapIoSpace(Address, Count * 16, MmNonCached);
    ok(Entries != NULL, "the MSI-X table at 0x%I64x cannot be mapped\n", Address.QuadPart);
    if (Entries == NULL) return;

    ReadApicIds(ApicIds);

    for (Index = 0; Index < Count; Index++)
    {
        if (READ_REGISTER_ULONG(&Entries[Index * 4 + 3]) & 1) continue;

        Destination = (READ_REGISTER_ULONG(&Entries[Index * 4]) >> 12) & 0xFF;
        for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
        {
            if (ApicIds[Processor] == Destination)
            {
                Processors[Index] = Processor;
                break;
            }
        }
    }

    MmUnmapIoSpace(Entries, Count * 16);
}

/* Measures only; IoAffinityPolicy runs it after each restart and checks the results it leaves */
START_TEST(HalMessageState)
{
    ULONG Processors[MAXIMUM_ENTRIES];
    ULONG Slot, MsixEnabled, MsiEnabled, IntxDisabled, Count;
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

    if (!MsixEnabled) return;

    Count = min((ULONG)(Control & MSIX_TABLE_SIZE) + 1, MAXIMUM_ENTRIES);
    ReadDestinations(Slot, Msix, Count, Processors);
    WriteResult(L"Processors", REG_BINARY, Processors, Count * sizeof(ULONG));
}
