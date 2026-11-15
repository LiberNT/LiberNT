/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the PCI interrupt links no _PRT names are disabled in APIC mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define ICH9_LPC_VENDOR_ID      0x8086
#define ICH9_LPC_DEVICE_ID      0x2918
#define ICH9_PIRQA_ROUT         0x60
#define ICH9_PIRQE_ROUT         0x68
#define ICH9_PIRQ_ROUT_DISABLED 0x80

START_TEST(AcpiPciLinkRouting)
{
    PCI_SLOT_NUMBER Slot;
    UCHAR Routes[8];
    USHORT Ids[2] = { 0, 0 };
    ULONG Index;

    Slot.u.AsULONG = 0;
    Slot.u.bits.DeviceNumber = 0x1F;
    Slot.u.bits.FunctionNumber = 0;
    if (HalGetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, Ids, 0, sizeof(Ids)) != sizeof(Ids) ||
        Ids[0] != ICH9_LPC_VENDOR_ID || Ids[1] != ICH9_LPC_DEVICE_ID)
    {
        skip(FALSE, "0:1f.0 is %04x:%04x, not an ICH9 LPC bridge\n", Ids[0], Ids[1]);
        return;
    }

    ok_eq_ulong(HalGetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, &Routes[0], ICH9_PIRQA_ROUT, 4), 4UL);
    ok_eq_ulong(HalGetBusDataByOffset(PCIConfiguration, 0, Slot.u.AsULONG, &Routes[4], ICH9_PIRQE_ROUT, 4), 4UL);

    /* Windows 11 (26100) on QEMU q35 leaves every route at 8a or 8b: the APIC _PRT names only the GSI links */
    for (Index = 0; Index < RTL_NUMBER_OF(Routes); Index++)
    {
        ok(Routes[Index] & ICH9_PIRQ_ROUT_DISABLED,
           "PIRQ%c route 0x%02x still sends the interrupt to a legacy IRQ\n", 'A' + (CHAR)Index, Routes[Index]);
    }
}
