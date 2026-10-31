/*
 * PROJECT:     LiberNT ACPI Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SystemCMOS operation region handler for the RTC/CMOS devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include "cmos_core.h"

#define NDEBUG
#include <debug.h>

static
ACPI_STATUS
ACPI_SYSTEM_XFACE
AcpiCmosSpaceHandler(
    _In_ UINT32 Function,
    _In_ ACPI_PHYSICAL_ADDRESS Address,
    _In_ UINT32 BitWidth,
    _Inout_ UINT64 *Value,
    _In_ PVOID HandlerContext,
    _In_opt_ PVOID RegionContext)
{
    ULONG CmosType = (ULONG)(ULONG_PTR)HandlerContext;
    ULONG Length = BitWidth / 8;
    ULONG Offset = (ULONG)Address;
    ULONG Window;
    UCHAR Bytes[sizeof(UINT64)];

    UNREFERENCED_PARAMETER(RegionContext);

    if (Length == 0 || Length > sizeof(Bytes) || Address > MAXULONG)
        return AE_BAD_PARAMETER;

    Window = AcpiCmosCoreWindow(CmosType, Offset, Length);
    /* FIXME: RTC registers 0x00-0x09 are accessed without waiting for the update cycle */
    if ((Function & ACPI_IO_MASK) == ACPI_READ)
    {
        RtlZeroMemory(Bytes, sizeof(Bytes));
        if (Window != 0)
            HalGetBusDataByOffset(Cmos, 0, Offset, Bytes, Offset, Window);
        *Value = 0;
        RtlCopyMemory(Value, Bytes, Length);
    }
    else
    {
        RtlCopyMemory(Bytes, Value, Length);
        if (Window != 0)
            HalSetBusDataByOffset(Cmos, 0, Offset, Bytes, Offset, Window);
    }
    return AE_OK;
}

static
ACPI_STATUS
ACPI_SYSTEM_XFACE
AcpiCmosAddDevice(
    _In_ ACPI_HANDLE Handle,
    _In_ UINT32 Level,
    _In_opt_ PVOID Context,
    _Out_opt_ PVOID *ReturnValue)
{
    ACPI_STATUS Status;

    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(ReturnValue);

    Status = AcpiInstallAddressSpaceHandler(Handle, ACPI_ADR_SPACE_CMOS, AcpiCmosSpaceHandler, NULL, Context);
    if (ACPI_FAILURE(Status) && Status != AE_ALREADY_EXISTS)
        DPRINT1("ACPI: SystemCMOS handler not installed: %s\n", AcpiFormatException(Status));
    return AE_OK;
}

int
acpi_cmos_init(void)
{
    static PCSTR HardwareIds[] = { "PNP0B00", "PNP0B01", "PNP0B02" };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(HardwareIds); Index++)
    {
        AcpiGetDevices((PSTR)HardwareIds[Index],
                       AcpiCmosAddDevice,
                       (PVOID)(ULONG_PTR)AcpiCmosCoreTypeFromHardwareId(HardwareIds[Index]),
                       NULL);
    }
    AcpiCmosAddDevice(ACPI_ROOT_OBJECT, 0, (PVOID)(ULONG_PTR)ACPI_CMOS_TYPE_STD_PCAT, NULL);
    return 0;
}
