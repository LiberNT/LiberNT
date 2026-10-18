#include "precomp.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
Bus_Power (
     PDEVICE_OBJECT DeviceObject,
     PIRP Irp
    )
{
    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status;
    PCOMMON_DEVICE_DATA commonData;

    status = STATUS_SUCCESS;
    irpStack = IoGetCurrentIrpStackLocation (Irp);
    ASSERT (IRP_MJ_POWER == irpStack->MajorFunction);

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;

    if (commonData->IsFDO) {

            DPRINT("FDO %s IRP:0x%p %s %s\n",
            PowerMinorFunctionString(irpStack->MinorFunction), Irp,
            DbgSystemPowerString(commonData->SystemPowerState),
            DbgDevicePowerString(commonData->DevicePowerState));


        status = Bus_FDO_Power ((PFDO_DEVICE_DATA)DeviceObject->DeviceExtension,
                                Irp);
    } else {

            DPRINT("PDO %s IRP:0x%p %s %s\n",
            PowerMinorFunctionString(irpStack->MinorFunction), Irp,
            DbgSystemPowerString(commonData->SystemPowerState),
            DbgDevicePowerString(commonData->DevicePowerState));

        status = Bus_PDO_Power ((PPDO_DEVICE_DATA)DeviceObject->DeviceExtension,
                                Irp);
    }

    return status;
}


BOOLEAN AcpiPmHalSleep;
static UINT8 AcpiPmSleepState;
static PPM_DISPATCH_TABLE AcpiPmHalDispatchTable;

static
NTSTATUS
NTAPI
AcpiPmGetSleepTypeData(
    _In_ SYSTEM_POWER_STATE SystemState,
    _Out_ PUCHAR SleepTypeA,
    _Out_ PUCHAR SleepTypeB)
{
    UINT8 SleepState;

    switch (SystemState)
    {
        case PowerSystemSleeping1: SleepState = ACPI_STATE_S1; break;
        case PowerSystemSleeping2: SleepState = ACPI_STATE_S2; break;
        case PowerSystemSleeping3: SleepState = ACPI_STATE_S3; break;
        case PowerSystemHibernate: SleepState = ACPI_STATE_S4; break;
        case PowerSystemShutdown: SleepState = ACPI_STATE_S5; break;
        default: return STATUS_INVALID_PARAMETER;
    }

    if (ACPI_FAILURE(AcpiGetSleepTypeData(SleepState, SleepTypeA, SleepTypeB)))
        return STATUS_NOT_SUPPORTED;

    return STATUS_SUCCESS;
}

static PM_DISPATCH_TABLE AcpiPmDispatchTable =
{
    ACPI_PM_DISPATCH_SIGNATURE,
    ACPI_PM_DISPATCH_VERSION,
    { (PVOID)AcpiPmGetSleepTypeData }
};

VOID
AcpiPmRegisterWithHal(VOID)
{
    NTSTATUS Status;

    Status = HalInitPowerManagement(&AcpiPmDispatchTable, &AcpiPmHalDispatchTable);
    if (!NT_SUCCESS(Status))
        DPRINT1("The HAL does not handle ACPI sleep states: 0x%08lx\n", Status);
}

static
NTSTATUS
AcpiPmPrepareSleep(
    _In_ UINT8 SleepState)
{
    ACPI_STATUS AcpiStatus;

    AcpiStatus = AcpiEnterSleepStatePrep(SleepState);
    if (ACPI_FAILURE(AcpiStatus))
    {
        DPRINT1("Preparing S%u failed: %s\n", SleepState, AcpiFormatException(AcpiStatus));
        return STATUS_UNSUCCESSFUL;
    }

    AcpiPmHalSleep = TRUE;
    AcpiStatus = AcpiEnterSleepState(SleepState);
    AcpiPmHalSleep = FALSE;
    if (ACPI_FAILURE(AcpiStatus))
    {
        DPRINT1("Arming S%u failed: %s\n", SleepState, AcpiFormatException(AcpiStatus));
        AcpiLeaveSleepStatePrep(SleepState);
        AcpiLeaveSleepState(SleepState);
        return STATUS_UNSUCCESSFUL;
    }

    AcpiPmSleepState = SleepState;
    return STATUS_SUCCESS;
}

static
VOID
AcpiPmFinishWake(VOID)
{
    UINT8 SleepState = AcpiPmSleepState;

    if (SleepState == ACPI_STATE_S0)
        return;

    AcpiPmSleepState = ACPI_STATE_S0;
    AcpiLeaveSleepStatePrep(SleepState);
    AcpiLeaveSleepState(SleepState);
}

NTSTATUS
Bus_FDO_Power (
    PFDO_DEVICE_DATA    Data,
    PIRP                Irp
    )
{
    NTSTATUS            status = STATUS_SUCCESS;
    POWER_STATE         powerState;
    POWER_STATE_TYPE    powerType;
    PIO_STACK_LOCATION  stack;
    ACPI_STATUS AcpiStatus;
    SYSTEM_POWER_STATE  oldPowerState;
    UINT8               SleepTypeA, SleepTypeB;

    stack = IoGetCurrentIrpStackLocation (Irp);
    powerType = stack->Parameters.Power.Type;
    powerState = stack->Parameters.Power.State;

    if (stack->MinorFunction == IRP_MN_SET_POWER && powerType == SystemPowerState)
    {
        DPRINT("\tRequest to set System state to %s\n",
               DbgSystemPowerString(powerState.SystemState));

        oldPowerState = Data->Common.SystemPowerState;
        Data->Common.SystemPowerState = powerState.SystemState;

        switch (powerState.SystemState)
        {
            case PowerSystemWorking:
                AcpiPmFinishWake();
                break;

            case PowerSystemSleeping1:
            case PowerSystemSleeping2:
            case PowerSystemSleeping3:
                status = AcpiPmPrepareSleep(ACPI_STATE_S1 + (powerState.SystemState - PowerSystemSleeping1));
                break;

            case PowerSystemShutdown:
                if (ACPI_FAILURE(AcpiGetSleepTypeData(ACPI_STATE_S5, &SleepTypeA, &SleepTypeB)))
                    break;
                AcpiStatus = acpi_suspend(ACPI_STATE_S5);
                if (!ACPI_SUCCESS(AcpiStatus))
                {
                    DPRINT1("Failed to enter sleep state 5 (Status 0x%X)\n", AcpiStatus);
                    status = STATUS_UNSUCCESSFUL;
                }
                break;

            default:
                status = STATUS_NOT_SUPPORTED;
                break;
        }

        if (!NT_SUCCESS(status))
        {
            Data->Common.SystemPowerState = oldPowerState;
            /* A successful lower-driver completion must not hide a failed
             * platform transition. We still own this IRP at this point. */
            Irp->IoStatus.Status = status;
            PoStartNextPowerIrp(Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return status;
        }
    }

    PoStartNextPowerIrp (Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    status =  PoCallDriver (Data->NextLowerDriver, Irp);
    return status;
}


static IO_WORKITEM_ROUTINE Bus_PDO_PowerWorker;

static
VOID
NTAPI
Bus_PDO_PowerWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PIRP Irp = Context;
    PIO_WORKITEM WorkItem = Irp->Tail.Overlay.DriverContext[0];

    Bus_PDO_Power((PPDO_DEVICE_DATA)DeviceObject->DeviceExtension, Irp);
    IoFreeWorkItem(WorkItem);
}

NTSTATUS
Bus_PDO_Power (
    PPDO_DEVICE_DATA    PdoData,
    PIRP                Irp
    )
{
    NTSTATUS            status;
    PIO_STACK_LOCATION  stack;
    POWER_STATE         powerState;
    POWER_STATE_TYPE    powerType;
    ULONG               error;
    PIO_WORKITEM        WorkItem;

    stack = IoGetCurrentIrpStackLocation (Irp);
    powerType = stack->Parameters.Power.Type;
    powerState = stack->Parameters.Power.State;

    if (stack->MinorFunction == IRP_MN_SET_POWER &&
        powerType == DevicePowerState &&
        PdoData->AcpiHandle &&
        KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        WorkItem = IoAllocateWorkItem(PdoData->Common.Self);
        if (WorkItem)
        {
            Irp->Tail.Overlay.DriverContext[0] = WorkItem;
            IoMarkIrpPending(Irp);
            IoQueueWorkItem(WorkItem, Bus_PDO_PowerWorker, CriticalWorkQueue, Irp);
            return STATUS_PENDING;
        }
    }

    switch (stack->MinorFunction) {
    case IRP_MN_SET_POWER:

        DPRINT("\tSetting %s power state to %s\n",
               ((powerType == SystemPowerState) ?  "System" : "Device"),
               ((powerType == SystemPowerState) ? \
               DbgSystemPowerString(powerState.SystemState) : \
               DbgDevicePowerString(powerState.DeviceState)));

        switch (powerType) {
            case DevicePowerState:
                if (!PdoData->AcpiHandle || !acpi_bus_power_manageable(PdoData->AcpiHandle))
                {
                    PoSetPowerState(PdoData->Common.Self, DevicePowerState, powerState);
                    PdoData->Common.DevicePowerState = powerState.DeviceState;
                    status = STATUS_SUCCESS;
                    break;
                }

                switch (powerState.DeviceState)
                {
                    case PowerDeviceD0:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D0);
                      break;

                    case PowerDeviceD1:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D1);
                      break;

                    case PowerDeviceD2:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D2);
                      break;

                    case PowerDeviceD3:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D3);
                      break;

                    default:
                      error = 0;
                      break;
                }

                if (ACPI_SUCCESS(error))
                {
                    PoSetPowerState(PdoData->Common.Self, DevicePowerState, powerState);
                    PdoData->Common.DevicePowerState = powerState.DeviceState;
                    status = STATUS_SUCCESS;
                }
                else
                    status = STATUS_UNSUCCESSFUL;
                break;

            case SystemPowerState:
                PdoData->Common.SystemPowerState = powerState.SystemState;
                status = STATUS_SUCCESS;
                break;

            default:
                status = STATUS_NOT_SUPPORTED;
                break;
        }
        break;

    case IRP_MN_QUERY_POWER:
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_WAIT_WAKE:
        //
        // We cannot support wait-wake because we are root-enumerated
        // driver, and our parent, the PnP manager, doesn't support wait-wake.
        //
    case IRP_MN_POWER_SEQUENCE:
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    if (status != STATUS_NOT_SUPPORTED) {

        Irp->IoStatus.Status = status;
    }

    PoStartNextPowerIrp(Irp);
    status = Irp->IoStatus.Status;
    IoCompleteRequest (Irp, IO_NO_INCREMENT);

    return status;
}

#if !defined(NDEBUG) || defined(_MSC_VER)

PCHAR
PowerMinorFunctionString (
    UCHAR MinorFunction
)
{
    switch (MinorFunction)
    {
        case IRP_MN_SET_POWER:
            return "IRP_MN_SET_POWER";
        case IRP_MN_QUERY_POWER:
            return "IRP_MN_QUERY_POWER";
        case IRP_MN_POWER_SEQUENCE:
            return "IRP_MN_POWER_SEQUENCE";
        case IRP_MN_WAIT_WAKE:
            return "IRP_MN_WAIT_WAKE";

        default:
            return "unknown_power_irp";
    }
}

PCHAR
DbgSystemPowerString(
     SYSTEM_POWER_STATE Type
    )
{
    switch (Type)
    {
        case PowerSystemUnspecified:
            return "PowerSystemUnspecified";
        case PowerSystemWorking:
            return "PowerSystemWorking";
        case PowerSystemSleeping1:
            return "PowerSystemSleeping1";
        case PowerSystemSleeping2:
            return "PowerSystemSleeping2";
        case PowerSystemSleeping3:
            return "PowerSystemSleeping3";
        case PowerSystemHibernate:
            return "PowerSystemHibernate";
        case PowerSystemShutdown:
            return "PowerSystemShutdown";
        case PowerSystemMaximum:
            return "PowerSystemMaximum";
        default:
            return "UnKnown System Power State";
    }
 }

PCHAR
DbgDevicePowerString(
     DEVICE_POWER_STATE Type
    )
{
    switch (Type)
    {
        case PowerDeviceUnspecified:
            return "PowerDeviceUnspecified";
        case PowerDeviceD0:
            return "PowerDeviceD0";
        case PowerDeviceD1:
            return "PowerDeviceD1";
        case PowerDeviceD2:
            return "PowerDeviceD2";
        case PowerDeviceD3:
            return "PowerDeviceD3";
        case PowerDeviceMaximum:
            return "PowerDeviceMaximum";
        default:
            return "UnKnown Device Power State";
    }
}

#endif
