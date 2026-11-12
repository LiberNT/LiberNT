/*
 * PROJECT:     LiberNT Tegra USB transceiver driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Clock and transceiver of a Tegra210 USB 2.0 controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define TEGRAUSBPHY_TAG                 'PUgT'

/*
 * The clock and reset controller of the system on chip. A boot loader may hand
 * the machine over with the loops of the transceiver stopped, in which case the
 * controller answers nothing, so this driver sits below the host controller
 * driver and starts whatever the firmware left stopped before it is reached.
 * Everything here is read first and only written when it is not already set,
 * so a controller the firmware did bring up is left exactly as it is.
 */
#define TEGRA_CLK_RST_BASE              0x60006000ULL
#define TEGRA_CLK_RST_SIZE              0x1000

#define TEGRA_RST_DEV_L_CLR             0x304
#define TEGRA_CLK_OUT_ENB_L             0x010
#define TEGRA_CLK_ENB_L_SET             0x320
#define TEGRA_RST_DEV_H_CLR             0x314
#define TEGRA_CLK_OUT_ENB_H             0x014
#define TEGRA_CLK_ENB_H_SET             0x328

#define TEGRA_PLLU_BASE                 0x0C0
#define TEGRA_PLLU_MISC                 0x0CC
#define TEGRA_PLLU_BASE_LOCK            (1UL << 27)
#define TEGRA_PLLU_BASE_OVERRIDE        (1UL << 24)
#define TEGRA_PLLU_BASE_CLKENABLE_48M   (1UL << 25)
#define TEGRA_PLLU_BASE_CLKENABLE_USB   (1UL << 21)
#define TEGRA_PLLU_BASE_ENABLE          (1UL << 30)
#define TEGRA_PLLU_MISC_IDDQ            (1UL << 31)
#define TEGRA_PLLU_MISC_LOCK_ENABLE     (1UL << 29)

#define TEGRA_UTMIP_PLL_CFG1            0x484
#define TEGRA_UTMIP_PLL_CFG2            0x488
#define TEGRA_UTMIPLL_HW_PWRDN_CFG0     0x52C
#define TEGRA_XUSB_PLL_CFG0             0x534

#define TEGRA_UTMIP_CFG1_PLLU_POWERDOWN         (1UL << 16)
#define TEGRA_UTMIP_CFG1_PLL_ENABLE_POWERUP     (1UL << 15)
#define TEGRA_UTMIP_CFG1_PLL_ENABLE_POWERDOWN   (1UL << 14)
#define TEGRA_UTMIP_CFG1_PLL_ACTIVE_POWERDOWN   (1UL << 12)

#define TEGRA_UTMIP_CFG2_SAMP_D_POWERUP         (1UL << 25)
#define TEGRA_UTMIP_CFG2_SAMP_D_POWERDOWN       (1UL << 24)
#define TEGRA_UTMIP_CFG2_SAMP_C_POWERUP         (1UL << 5)
#define TEGRA_UTMIP_CFG2_SAMP_C_POWERDOWN       (1UL << 4)
#define TEGRA_UTMIP_CFG2_SAMP_B_POWERUP         (1UL << 3)
#define TEGRA_UTMIP_CFG2_SAMP_B_POWERDOWN       (1UL << 2)
#define TEGRA_UTMIP_CFG2_SAMP_A_POWERUP         (1UL << 1)
#define TEGRA_UTMIP_CFG2_SAMP_A_POWERDOWN       (1UL << 0)

#define TEGRA_UTMIPLL_LOCK              (1UL << 31)
#define TEGRA_UTMIPLL_SEQ_ENABLE        (1UL << 24)
#define TEGRA_UTMIPLL_USE_LOCKDET       (1UL << 6)
#define TEGRA_UTMIPLL_SEQ_IN_SWCTL      (1UL << 4)
#define TEGRA_UTMIPLL_CLK_ENABLE_SWCTL  (1UL << 2)
#define TEGRA_UTMIPLL_IDDQ_OVERRIDE     (1UL << 1)
#define TEGRA_UTMIPLL_IDDQ_SWCTL        (1UL << 0)

#define TEGRA_XUSB_PLL_UTMIPLL_LOCK_DLY 0x3FF

/* Clock and reset bit of each USB 2.0 controller, by its register window */
#define TEGRA_USBD_BASE                 0x7D000000ULL
#define TEGRA_USB2_BASE                 0x7D004000ULL
#define TEGRA_PERIPH_USBD               22
#define TEGRA_PERIPH_USB2               58

#define TEGRA_PLL_LOCK_TIMEOUT_US       1000

typedef struct _TEGRAUSBPHY_DEVICE
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Lower;
    ULONG Peripheral;
    BOOLEAN Started;
} TEGRAUSBPHY_DEVICE, *PTEGRAUSBPHY_DEVICE;

static
ULONG
TegraUsbPhyRead(
    _In_ PUCHAR Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Base + Offset));
}

static
VOID
TegraUsbPhyWrite(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Base + Offset), Value);
}

/* Which controller the resources describe, and so which clock it runs on */
static
ULONG
TegraUsbPhyPeripheralOf(
    _In_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_LIST List;
    ULONG Index;

    if (!Resources || Resources->Count == 0)
        return MAXULONG;

    List = &Resources->List[0].PartialResourceList;
    for (Index = 0; Index < List->Count; Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &List->PartialDescriptors[Index];

        if (Descriptor->Type != CmResourceTypeMemory)
            continue;

        if ((ULONGLONG)Descriptor->u.Memory.Start.QuadPart == TEGRA_USBD_BASE)
            return TEGRA_PERIPH_USBD;

        if ((ULONGLONG)Descriptor->u.Memory.Start.QuadPart == TEGRA_USB2_BASE)
            return TEGRA_PERIPH_USB2;

        return MAXULONG;
    }

    return MAXULONG;
}

static
BOOLEAN
TegraUsbPhyClockRunning(
    _In_ PUCHAR Base,
    _In_ ULONG Peripheral)
{
    ULONG Enable = (Peripheral < 32) ? TEGRA_CLK_OUT_ENB_L : TEGRA_CLK_OUT_ENB_H;

    return (TegraUsbPhyRead(Base, Enable) & (1UL << (Peripheral % 32))) != 0;
}

static
VOID
TegraUsbPhyStartClock(
    _In_ PUCHAR Base,
    _In_ ULONG Peripheral)
{
    ULONG Set = (Peripheral < 32) ? TEGRA_CLK_ENB_L_SET : TEGRA_CLK_ENB_H_SET;
    ULONG Clear = (Peripheral < 32) ? TEGRA_RST_DEV_L_CLR : TEGRA_RST_DEV_H_CLR;
    ULONG Bit = 1UL << (Peripheral % 32);

    TegraUsbPhyWrite(Base, Set, Bit);
    KeStallExecutionProcessor(2);
    TegraUsbPhyWrite(Base, Clear, Bit);
}

/*
 * Starts PLLU and UTMIPLL if they are stopped. Both are left alone when they
 * are already locked, so this costs two reads on a machine whose firmware
 * brought the port up.
 */
static
NTSTATUS
TegraUsbPhyStartLoops(
    _In_ PUCHAR Base)
{
    ULONG Value;
    ULONG Wait;

    Value = TegraUsbPhyRead(Base, TEGRA_PLLU_BASE);
    if (!(Value & TEGRA_PLLU_BASE_LOCK))
    {
        Value = TegraUsbPhyRead(Base, TEGRA_PLLU_MISC);
        Value &= ~TEGRA_PLLU_MISC_IDDQ;
        Value |= TEGRA_PLLU_MISC_LOCK_ENABLE;
        TegraUsbPhyWrite(Base, TEGRA_PLLU_MISC, Value);
        KeStallExecutionProcessor(5);

        Value = TegraUsbPhyRead(Base, TEGRA_PLLU_BASE);
        Value |= TEGRA_PLLU_BASE_OVERRIDE | TEGRA_PLLU_BASE_ENABLE |
                 TEGRA_PLLU_BASE_CLKENABLE_USB | TEGRA_PLLU_BASE_CLKENABLE_48M;
        TegraUsbPhyWrite(Base, TEGRA_PLLU_BASE, Value);

        for (Wait = 0; Wait < TEGRA_PLL_LOCK_TIMEOUT_US; Wait++)
        {
            if (TegraUsbPhyRead(Base, TEGRA_PLLU_BASE) & TEGRA_PLLU_BASE_LOCK)
                break;

            KeStallExecutionProcessor(1);
        }

        if (!(TegraUsbPhyRead(Base, TEGRA_PLLU_BASE) & TEGRA_PLLU_BASE_LOCK))
        {
            DPRINT1("TEGRAUSBPHY: PLLU did not lock\n");
            return STATUS_DEVICE_NOT_READY;
        }

        DPRINT1("TEGRAUSBPHY: PLLU started\n");
    }

    Value = TegraUsbPhyRead(Base, TEGRA_UTMIPLL_HW_PWRDN_CFG0);
    if (Value & TEGRA_UTMIPLL_LOCK)
        return STATUS_SUCCESS;

    Value &= ~(TEGRA_UTMIPLL_IDDQ_OVERRIDE | TEGRA_UTMIPLL_IDDQ_SWCTL);
    TegraUsbPhyWrite(Base, TEGRA_UTMIPLL_HW_PWRDN_CFG0, Value);
    KeStallExecutionProcessor(10);

    Value = TegraUsbPhyRead(Base, TEGRA_UTMIP_PLL_CFG1);
    Value &= ~(TEGRA_UTMIP_CFG1_PLLU_POWERDOWN |
               TEGRA_UTMIP_CFG1_PLL_ACTIVE_POWERDOWN |
               TEGRA_UTMIP_CFG1_PLL_ENABLE_POWERDOWN);
    Value |= TEGRA_UTMIP_CFG1_PLL_ENABLE_POWERUP;
    TegraUsbPhyWrite(Base, TEGRA_UTMIP_PLL_CFG1, Value);
    KeStallExecutionProcessor(20);

    Value = TegraUsbPhyRead(Base, TEGRA_UTMIP_PLL_CFG2);
    Value &= ~(TEGRA_UTMIP_CFG2_SAMP_A_POWERDOWN | TEGRA_UTMIP_CFG2_SAMP_B_POWERDOWN |
               TEGRA_UTMIP_CFG2_SAMP_C_POWERDOWN | TEGRA_UTMIP_CFG2_SAMP_D_POWERDOWN);
    Value |= TEGRA_UTMIP_CFG2_SAMP_A_POWERUP | TEGRA_UTMIP_CFG2_SAMP_B_POWERUP |
             TEGRA_UTMIP_CFG2_SAMP_C_POWERUP | TEGRA_UTMIP_CFG2_SAMP_D_POWERUP;
    TegraUsbPhyWrite(Base, TEGRA_UTMIP_PLL_CFG2, Value);

    /* Hand the loop back to its sequencer */
    Value = TegraUsbPhyRead(Base, TEGRA_UTMIP_PLL_CFG1);
    Value &= ~(TEGRA_UTMIP_CFG1_PLL_ENABLE_POWERDOWN | TEGRA_UTMIP_CFG1_PLL_ENABLE_POWERUP);
    TegraUsbPhyWrite(Base, TEGRA_UTMIP_PLL_CFG1, Value);

    Value = TegraUsbPhyRead(Base, TEGRA_UTMIPLL_HW_PWRDN_CFG0);
    Value |= TEGRA_UTMIPLL_USE_LOCKDET;
    Value &= ~(TEGRA_UTMIPLL_CLK_ENABLE_SWCTL | TEGRA_UTMIPLL_SEQ_IN_SWCTL);
    TegraUsbPhyWrite(Base, TEGRA_UTMIPLL_HW_PWRDN_CFG0, Value);
    KeStallExecutionProcessor(1);

    Value = TegraUsbPhyRead(Base, TEGRA_XUSB_PLL_CFG0);
    Value &= ~TEGRA_XUSB_PLL_UTMIPLL_LOCK_DLY;
    TegraUsbPhyWrite(Base, TEGRA_XUSB_PLL_CFG0, Value);
    KeStallExecutionProcessor(1);

    Value = TegraUsbPhyRead(Base, TEGRA_UTMIPLL_HW_PWRDN_CFG0);
    Value |= TEGRA_UTMIPLL_SEQ_ENABLE;
    TegraUsbPhyWrite(Base, TEGRA_UTMIPLL_HW_PWRDN_CFG0, Value);

    for (Wait = 0; Wait < TEGRA_PLL_LOCK_TIMEOUT_US; Wait++)
    {
        if (TegraUsbPhyRead(Base, TEGRA_UTMIPLL_HW_PWRDN_CFG0) & TEGRA_UTMIPLL_LOCK)
        {
            DPRINT1("TEGRAUSBPHY: UTMIPLL started\n");
            return STATUS_SUCCESS;
        }

        KeStallExecutionProcessor(1);
    }

    DPRINT1("TEGRAUSBPHY: UTMIPLL did not lock\n");
    return STATUS_DEVICE_NOT_READY;
}

static
VOID
TegraUsbPhyPrepare(
    _In_ PTEGRAUSBPHY_DEVICE Device,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PHYSICAL_ADDRESS Physical;
    PUCHAR Base;

    if (Device->Started)
        return;

    Device->Peripheral = TegraUsbPhyPeripheralOf(Resources);
    if (Device->Peripheral == MAXULONG)
    {
        DPRINT1("TEGRAUSBPHY: not a USB 2.0 controller of this system on chip\n");
        return;
    }

    Physical.QuadPart = (LONGLONG)TEGRA_CLK_RST_BASE;
    Base = MmMapIoSpace(Physical, TEGRA_CLK_RST_SIZE, MmNonCached);
    if (!Base)
    {
        DPRINT1("TEGRAUSBPHY: the clock and reset controller could not be mapped\n");
        return;
    }

    if (NT_SUCCESS(TegraUsbPhyStartLoops(Base)))
    {
        if (!TegraUsbPhyClockRunning(Base, Device->Peripheral))
        {
            TegraUsbPhyStartClock(Base, Device->Peripheral);
            DPRINT1("TEGRAUSBPHY: clock %lu started\n", Device->Peripheral);
        }

        Device->Started = TRUE;
    }

    MmUnmapIoSpace(Base, TEGRA_CLK_RST_SIZE);
}

static
NTSTATUS
NTAPI
TegraUsbPhyPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PTEGRAUSBPHY_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MinorFunction == IRP_MN_START_DEVICE)
    {
        /* The controller has to answer before the driver above it starts */
        TegraUsbPhyPrepare(Device, Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
    }
    else if (Stack->MinorFunction == IRP_MN_REMOVE_DEVICE)
    {
        PDEVICE_OBJECT Lower = Device->Lower;
        PDEVICE_OBJECT Self = Device->Self;
        NTSTATUS Status;

        IoSkipCurrentIrpStackLocation(Irp);
        Status = IoCallDriver(Lower, Irp);
        IoDetachDevice(Lower);
        IoDeleteDevice(Self);
        return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
TegraUsbPhyPass(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PTEGRAUSBPHY_DEVICE Device = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
TegraUsbPhyPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PTEGRAUSBPHY_DEVICE Device = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
TegraUsbPhyAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT DeviceObject;
    PTEGRAUSBPHY_DEVICE Device;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(TEGRAUSBPHY_DEVICE), NULL,
                            FILE_DEVICE_CONTROLLER, FILE_DEVICE_SECURE_OPEN, FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Device = DeviceObject->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->Self = DeviceObject;

    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &Device->Lower);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DeviceObject->Flags |= Device->Lower->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
    DeviceObject->DeviceType = Device->Lower->DeviceType;
    DeviceObject->Characteristics = Device->Lower->Characteristics;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = TegraUsbPhyPass;

    DriverObject->MajorFunction[IRP_MJ_PNP] = TegraUsbPhyPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = TegraUsbPhyPower;
    DriverObject->DriverExtension->AddDevice = TegraUsbPhyAddDevice;
    return STATUS_SUCCESS;
}
