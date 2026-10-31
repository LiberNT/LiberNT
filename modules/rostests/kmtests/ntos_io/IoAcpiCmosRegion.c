/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SystemCMOS operation region access window of the ACPI driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <cmos_core.h>

START_TEST(IoAcpiCmosRegion)
{
    ok_eq_ulong(AcpiCmosCoreTypeFromHardwareId("PNP0B00"), (ULONG)ACPI_CMOS_TYPE_STD_PCAT);
    ok_eq_ulong(AcpiCmosCoreTypeFromHardwareId("PNP0B01"), (ULONG)ACPI_CMOS_TYPE_INTEL_PIIX4);
    ok_eq_ulong(AcpiCmosCoreTypeFromHardwareId("PNP0B02"), (ULONG)ACPI_CMOS_TYPE_DALLAS_1501);

    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_STD_PCAT, 0x00, 1), 1UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_STD_PCAT, 0x09, 2), 2UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_STD_PCAT, 0x3E, 4), 2UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_STD_PCAT, 0x3F, 1), 1UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_STD_PCAT, 0x40, 1), 0UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_STD_PCAT, 0x80, 1), 0UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_STD_PCAT, 0xFFFFFFFF, 8), 0UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_INTEL_PIIX4, 0x10, 1), 0UL);
    ok_eq_ulong(AcpiCmosCoreWindow(ACPI_CMOS_TYPE_DALLAS_1501, 0x10, 1), 0UL);
}
