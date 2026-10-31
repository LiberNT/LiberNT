/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HalGetBusDataByOffset and HalSetBusDataByOffset bus types and absent PCI functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
BOOLEAN
FindAbsentSlot(
    _Out_ PCI_SLOT_NUMBER *Slot)
{
    ULONG Device;
    USHORT Vendor;

    Slot->u.AsULONG = 0;
    for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
    {
        Slot->u.bits.DeviceNumber = Device;
        Vendor = 0;
        if (HalGetBusDataByOffset(PCIConfiguration, 0, Slot->u.AsULONG, &Vendor, 0, sizeof(Vendor)) == sizeof(Vendor) &&
            Vendor == PCI_INVALID_VENDORID)
        {
            return TRUE;
        }
    }
    return FALSE;
}

START_TEST(HalBusData)
{
    static const BUS_DATA_TYPE OtherTypes[] = { EisaConfiguration, Pos, PNPISAConfiguration, MPIConfiguration };
    UCHAR Header[PCI_COMMON_HDR_LENGTH];
    PCI_SLOT_NUMBER Slot;
    ULONG Value;
    ULONG Index;
    ULONG Result;

    for (Index = 0; Index < RTL_NUMBER_OF(OtherTypes); Index++)
    {
        Value = 0x5A5A5A5A;
        Result = HalGetBusDataByOffset(OtherTypes[Index], 0, 0, &Value, 0, sizeof(Value));
        ok(Result == 0, "type %d: get returned %lu\n", OtherTypes[Index], Result);
        Result = HalSetBusDataByOffset(OtherTypes[Index], 0, 0, &Value, 0, sizeof(Value));
        ok(Result == 0, "type %d: set returned %lu\n", OtherTypes[Index], Result);
    }

    Value = 0;
    Result = HalGetBusDataByOffset(PCIConfiguration, 0, 0, &Value, 0, sizeof(Value));
    ok(Result == sizeof(Value), "0:0.0 get returned %lu\n", Result);
    ok((Value & 0xFFFF) != PCI_INVALID_VENDORID, "0:0.0 vendor 0x%lx\n", Value & 0xFFFF);

    if (!FindAbsentSlot(&Slot))
    {
        skip(FALSE, "every device number on bus 0 is present\n");
        return;
    }

    RtlFillMemory(Header, sizeof(Header), 0x5A);
    Result = HalGetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, Header, 0, sizeof(Header));
    ok(Result == sizeof(USHORT), "absent 0:%lx.0 header get returned %lu\n", Slot.u.bits.DeviceNumber, Result);
    ok(Header[0] == 0xFF && Header[1] == 0xFF, "absent vendor %02x%02x\n", Header[1], Header[0]);

    Value = 0x5A5A5A5A;
    Result = HalGetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, &Value, PCI_COMMON_HDR_LENGTH, sizeof(Value));
    ok(Result == 0, "absent device-specific get returned %lu\n", Result);

    Value = 0;
    Result = HalSetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, &Value, FIELD_OFFSET(PCI_COMMON_HEADER, Command), sizeof(USHORT));
    ok(Result == 0, "absent header set returned %lu\n", Result);
    Result = HalSetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, &Value, PCI_COMMON_HDR_LENGTH, sizeof(Value));
    ok(Result == 0, "absent device-specific set returned %lu\n", Result);
}
