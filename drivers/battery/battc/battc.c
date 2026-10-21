/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            drivers/battery/battc/battc.c
 * PURPOSE:         Battery Class Driver
 * PROGRAMMERS:     Cameron Gutman (cameron.gutman@reactos.org)
 *                  Ahmed ARIF (arif.ing@outlook.com)
 */

#include <battc.h>

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath)
{
    DPRINT("Battery class driver initialized\n");

    return STATUS_SUCCESS;
}

BCLASSAPI
NTSTATUS
NTAPI
BatteryClassUnload(PVOID ClassData)
{
    PBATTERY_CLASS_DATA BattClass;

    DPRINT("Battery %p is being unloaded\n", ClassData);

    BattClass = ClassData;
    if (BattClass->InterfaceName.Length != 0)
    {
        if (!NT_SUCCESS(IoSetDeviceInterfaceState(&BattClass->InterfaceName, FALSE)))
            DPRINT1("IoSetDeviceInterfaceState(%wZ) failed\n", &BattClass->InterfaceName);
        RtlFreeUnicodeString(&BattClass->InterfaceName);
    }

    if (BattClass->WmiGuidList != NULL)
        ExFreePoolWithTag(BattClass->WmiGuidList, BATTERY_CLASS_DATA_TAG);

    ExFreePoolWithTag(BattClass,
                      BATTERY_CLASS_DATA_TAG);

    return STATUS_SUCCESS;
}

static const GUID *BatteryWmiGuids[] =
{
    &BATTERY_STATUS_WMI_GUID,
    &BATTERY_RUNTIME_WMI_GUID,
    &BATTERY_TEMPERATURE_WMI_GUID,
    &BATTERY_FULL_CHARGED_CAPACITY_WMI_GUID,
    &BATTERY_CYCLE_COUNT_WMI_GUID,
    &BATTERY_STATIC_DATA_WMI_GUID
};

BCLASSAPI
NTSTATUS
NTAPI
BatteryClassSystemControl(PVOID ClassData,
                          PVOID WmiLibContext,
                          PDEVICE_OBJECT DeviceObject,
                          PIRP Irp,
                          PVOID Disposition)
{
    PBATTERY_CLASS_DATA BattClass;
    PWMILIB_CONTEXT WmiLib;
    PWMIGUIDREGINFO GuidList;
    ULONG Count, i;

    BattClass = ClassData;
    WmiLib = WmiLibContext;

    ExAcquireFastMutex(&BattClass->Mutex);
    if (WmiLib->GuidList != BattClass->WmiGuidList)
    {
        Count = WmiLib->GuidCount + RTL_NUMBER_OF(BatteryWmiGuids);
        GuidList = ExAllocatePoolWithTag(PagedPool,
                                         Count * sizeof(WMIGUIDREGINFO),
                                         BATTERY_CLASS_DATA_TAG);
        if (GuidList != NULL)
        {
            if (WmiLib->GuidCount != 0)
                RtlCopyMemory(GuidList, WmiLib->GuidList, WmiLib->GuidCount * sizeof(WMIGUIDREGINFO));

            for (i = 0; i < RTL_NUMBER_OF(BatteryWmiGuids); i++)
            {
                GuidList[WmiLib->GuidCount + i].Guid = BatteryWmiGuids[i];
                GuidList[WmiLib->GuidCount + i].InstanceCount = 1;
                GuidList[WmiLib->GuidCount + i].Flags = 0;
            }

            if (BattClass->WmiGuidList != NULL)
                ExFreePoolWithTag(BattClass->WmiGuidList, BATTERY_CLASS_DATA_TAG);

            BattClass->WmiGuidList = GuidList;
            BattClass->WmiMiniportGuidCount = WmiLib->GuidCount;
            WmiLib->GuidList = GuidList;
            WmiLib->GuidCount = Count;
        }
    }
    ExReleaseFastMutex(&BattClass->Mutex);

    return WmiSystemControl(WmiLib,
                            DeviceObject,
                            Irp,
                            (PSYSCTL_IRP_DISPOSITION)Disposition);
}

static
NTSTATUS
BatteryClassCopyWmiData(PVOID Data,
                        ULONG DataSize,
                        PUCHAR Buffer,
                        ULONG BufferSize,
                        PULONG Size)
{
    *Size = DataSize;
    if (BufferSize < DataSize)
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Buffer, Data, DataSize);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BatteryClassQueryInfo(PBATTERY_CLASS_DATA BattClass,
                      ULONG Tag,
                      BATTERY_QUERY_INFORMATION_LEVEL Level,
                      PVOID Buffer,
                      ULONG BufferLength,
                      PULONG ReturnedLength)
{
    ULONG Length = 0;
    NTSTATUS Status;

    Status = BattClass->MiniportInfo.QueryInformation(BattClass->MiniportInfo.Context,
                                                      Tag,
                                                      Level,
                                                      0,
                                                      Buffer,
                                                      BufferLength,
                                                      &Length);
    if (ReturnedLength != NULL)
        *ReturnedLength = Length;
    return Status;
}

static
ULONG
BatteryClassAppendWmiString(PBATTERY_CLASS_DATA BattClass,
                            ULONG Tag,
                            BATTERY_QUERY_INFORMATION_LEVEL Level,
                            PUCHAR Buffer,
                            ULONG BufferSize,
                            ULONG Offset)
{
    WCHAR String[MAX_BATTERY_STRING_SIZE];
    ULONG Returned;
    USHORT Length = 0;

    if (NT_SUCCESS(BatteryClassQueryInfo(BattClass, Tag, Level, String, sizeof(String), &Returned)))
    {
        Returned = min(Returned, sizeof(String)) / sizeof(WCHAR);
        while (Length / sizeof(WCHAR) < Returned && String[Length / sizeof(WCHAR)] != UNICODE_NULL)
            Length += sizeof(WCHAR);
    }

    if (Offset + sizeof(USHORT) + Length <= BufferSize)
    {
        RtlCopyMemory(Buffer + Offset, &Length, sizeof(USHORT));
        RtlCopyMemory(Buffer + Offset + sizeof(USHORT), String, Length);
    }

    return Offset + sizeof(USHORT) + Length;
}

static
NTSTATUS
BatteryClassQueryWmiData(PBATTERY_CLASS_DATA BattClass,
                         ULONG Tag,
                         ULONG Index,
                         PUCHAR Buffer,
                         ULONG BufferSize,
                         PULONG Size)
{
    BATTERY_INFORMATION BattInfo;
    BATTERY_STATUS BattStatus;
    NTSTATUS Status;
    ULONG Value;

    switch (Index)
    {
        case 0:
        {
            BATTERY_WMI_STATUS WmiStatus;

            Status = BattClass->MiniportInfo.QueryStatus(BattClass->MiniportInfo.Context,
                                                         Tag,
                                                         &BattStatus);
            if (!NT_SUCCESS(Status))
                return Status;

            RtlZeroMemory(&WmiStatus, sizeof(WmiStatus));
            WmiStatus.Tag = Tag;
            WmiStatus.RemainingCapacity = BattStatus.Capacity;
            WmiStatus.Voltage = BattStatus.Voltage;
            WmiStatus.PowerOnline = (BattStatus.PowerState & BATTERY_POWER_ON_LINE) != 0;
            WmiStatus.Charging = (BattStatus.PowerState & BATTERY_CHARGING) != 0;
            WmiStatus.Discharging = (BattStatus.PowerState & BATTERY_DISCHARGING) != 0;
            WmiStatus.Critical = (BattStatus.PowerState & BATTERY_CRITICAL) != 0;
            if ((ULONG)BattStatus.Rate != BATTERY_UNKNOWN_RATE)
            {
                if (BattStatus.Rate > 0)
                    WmiStatus.ChargeRate = BattStatus.Rate;
                else
                    WmiStatus.DischargeRate = -BattStatus.Rate;
            }
            return BatteryClassCopyWmiData(&WmiStatus, sizeof(WmiStatus), Buffer, BufferSize, Size);
        }

        case 1:
        {
            BATTERY_WMI_RUNTIME WmiRuntime;

            Status = BatteryClassQueryInfo(BattClass, Tag, BatteryEstimatedTime, &Value, sizeof(Value), NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            WmiRuntime.Tag = Tag;
            WmiRuntime.EstimatedRuntime = Value;
            return BatteryClassCopyWmiData(&WmiRuntime, sizeof(WmiRuntime), Buffer, BufferSize, Size);
        }

        case 2:
        {
            BATTERY_WMI_TEMPERATURE WmiTemperature;

            Status = BatteryClassQueryInfo(BattClass, Tag, BatteryTemperature, &Value, sizeof(Value), NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            WmiTemperature.Tag = Tag;
            WmiTemperature.Temperature = Value;
            return BatteryClassCopyWmiData(&WmiTemperature, sizeof(WmiTemperature), Buffer, BufferSize, Size);
        }

        case 3:
        {
            BATTERY_WMI_FULL_CHARGED_CAPACITY WmiCapacity;

            Status = BatteryClassQueryInfo(BattClass, Tag, BatteryInformation, &BattInfo, sizeof(BattInfo), NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            WmiCapacity.Tag = Tag;
            WmiCapacity.FullChargedCapacity = BattInfo.FullChargedCapacity;
            return BatteryClassCopyWmiData(&WmiCapacity, sizeof(WmiCapacity), Buffer, BufferSize, Size);
        }

        case 4:
        {
            BATTERY_WMI_CYCLE_COUNT WmiCycleCount;

            Status = BatteryClassQueryInfo(BattClass, Tag, BatteryInformation, &BattInfo, sizeof(BattInfo), NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            WmiCycleCount.Tag = Tag;
            WmiCycleCount.CycleCount = BattInfo.CycleCount;
            return BatteryClassCopyWmiData(&WmiCycleCount, sizeof(WmiCycleCount), Buffer, BufferSize, Size);
        }

        case 5:
        {
            BATTERY_WMI_STATIC_DATA WmiStatic;
            BATTERY_MANUFACTURE_DATE Date;
            WCHAR DateText[RTL_NUMBER_OF(WmiStatic.ManufactureDate) + 1];
            ULONG Offset;

            Status = BatteryClassQueryInfo(BattClass, Tag, BatteryInformation, &BattInfo, sizeof(BattInfo), NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            RtlZeroMemory(&WmiStatic, sizeof(WmiStatic));
            WmiStatic.Tag = Tag;
            if (NT_SUCCESS(BatteryClassQueryInfo(BattClass, Tag, BatteryManufactureDate, &Date, sizeof(Date), NULL)) &&
                NT_SUCCESS(RtlStringCchPrintfW(DateText, RTL_NUMBER_OF(DateText), L"%04u%02u%02u000000.000000+000",
                                               Date.Year, Date.Month, Date.Day)))
            {
                RtlCopyMemory(WmiStatic.ManufactureDate, DateText, sizeof(WmiStatic.ManufactureDate));
            }
            if (!NT_SUCCESS(BatteryClassQueryInfo(BattClass, Tag, BatteryGranularityInformation,
                                                  WmiStatic.Granularity, sizeof(WmiStatic.Granularity), NULL)))
            {
                RtlZeroMemory(WmiStatic.Granularity, sizeof(WmiStatic.Granularity));
            }
            WmiStatic.Capabilities = BattInfo.Capabilities;
            WmiStatic.Technology = BattInfo.Technology;
            RtlCopyMemory(&WmiStatic.Chemistry, BattInfo.Chemistry, sizeof(WmiStatic.Chemistry));
            WmiStatic.DesignedCapacity = BattInfo.DesignedCapacity;
            WmiStatic.DefaultAlert1 = BattInfo.DefaultAlert1;
            WmiStatic.DefaultAlert2 = BattInfo.DefaultAlert2;
            WmiStatic.CriticalBias = BattInfo.CriticalBias;

            Offset = FIELD_OFFSET(BATTERY_WMI_STATIC_DATA, Strings);
            if (Offset <= BufferSize)
                RtlCopyMemory(Buffer, &WmiStatic, Offset);
            Offset = BatteryClassAppendWmiString(BattClass, Tag, BatteryDeviceName, Buffer, BufferSize, Offset);
            Offset = BatteryClassAppendWmiString(BattClass, Tag, BatteryManufactureName, Buffer, BufferSize, Offset);
            Offset = BatteryClassAppendWmiString(BattClass, Tag, BatterySerialNumber, Buffer, BufferSize, Offset);
            Offset = BatteryClassAppendWmiString(BattClass, Tag, BatteryUniqueID, Buffer, BufferSize, Offset);

            *Size = Offset;
            return Offset <= BufferSize ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
        }

        default:
            return STATUS_WMI_GUID_NOT_FOUND;
    }
}

BCLASSAPI
NTSTATUS
NTAPI
BatteryClassQueryWmiDataBlock(PVOID ClassData,
                              PDEVICE_OBJECT DeviceObject,
                              PIRP Irp,
                              ULONG GuidIndex,
                              PULONG InstanceLengthArray,
                              ULONG OutBufferSize,
                              PUCHAR Buffer)
{
    PBATTERY_CLASS_DATA BattClass;
    ULONG Tag, Size = 0;
    NTSTATUS Status;

    BattClass = ClassData;

    if (GuidIndex < BattClass->WmiMiniportGuidCount ||
        GuidIndex - BattClass->WmiMiniportGuidCount >= RTL_NUMBER_OF(BatteryWmiGuids))
    {
        return STATUS_WMI_GUID_NOT_FOUND;
    }

    Status = BattClass->MiniportInfo.QueryTag(BattClass->MiniportInfo.Context, &Tag);
    if (NT_SUCCESS(Status))
    {
        Status = BatteryClassQueryWmiData(BattClass,
                                          Tag,
                                          GuidIndex - BattClass->WmiMiniportGuidCount,
                                          Buffer,
                                          InstanceLengthArray != NULL ? OutBufferSize : 0,
                                          &Size);
    }

    if (NT_SUCCESS(Status))
        InstanceLengthArray[0] = Size;

    return WmiCompleteRequest(DeviceObject, Irp, Status, Size, IO_NO_INCREMENT);
}

BCLASSAPI
NTSTATUS
NTAPI
BatteryClassStatusNotify(
    _In_ PVOID ClassData)
{
    PBATTERY_CLASS_DATA BattClass;
    BATTERY_WAIT_STATUS BattWait;
    BATTERY_STATUS BattStatus;
    ULONG Tag;
    NTSTATUS Status;

    DPRINT("Received battery status notification from %p\n", ClassData);

    BattClass = ClassData;

    ExAcquireFastMutex(&BattClass->Mutex);
    if (!BattClass->Waiting)
    {
        ExReleaseFastMutex(&BattClass->Mutex);
        return STATUS_SUCCESS;
    }

    BattWait = BattClass->EventTriggerContext;

    switch (BattClass->EventTrigger)
    {
        case EVENT_BATTERY_TAG:
            ExReleaseFastMutex(&BattClass->Mutex);
            Status = BattClass->MiniportInfo.QueryTag(BattClass->MiniportInfo.Context,
                                                      &Tag);
            if (!NT_SUCCESS(Status))
                return Status;

            ExAcquireFastMutex(&BattClass->Mutex);
            KeSetEvent(&BattClass->WaitEvent, IO_NO_INCREMENT, FALSE);
            ExReleaseFastMutex(&BattClass->Mutex);
            break;

        case EVENT_BATTERY_STATUS:
            ExReleaseFastMutex(&BattClass->Mutex);
            Status = BattClass->MiniportInfo.QueryStatus(BattClass->MiniportInfo.Context,
                                                         BattWait.BatteryTag,
                                                         &BattStatus);
            if (!NT_SUCCESS(Status))
                return Status;

            ExAcquireFastMutex(&BattClass->Mutex);

            if (BattWait.PowerState != BattStatus.PowerState ||
                BattWait.HighCapacity < BattStatus.Capacity ||
                BattWait.LowCapacity > BattStatus.Capacity)
            {
                KeSetEvent(&BattClass->WaitEvent, IO_NO_INCREMENT, FALSE);
            }

            ExReleaseFastMutex(&BattClass->Mutex);
            break;

        default:
            ExReleaseFastMutex(&BattClass->Mutex);
            ASSERT(FALSE);
            break;
    }

    return STATUS_SUCCESS;
}

BCLASSAPI
NTSTATUS
NTAPI
BatteryClassInitializeDevice(PBATTERY_MINIPORT_INFO MiniportInfo,
                             PVOID *ClassData)
{
    NTSTATUS Status;
    PBATTERY_CLASS_DATA BattClass;

    BattClass = ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(BATTERY_CLASS_DATA),
                                      BATTERY_CLASS_DATA_TAG);
    if (BattClass == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(BattClass, sizeof(BATTERY_CLASS_DATA));

    RtlCopyMemory(&BattClass->MiniportInfo,
                  MiniportInfo,
                  sizeof(BattClass->MiniportInfo));

    KeInitializeEvent(&BattClass->WaitEvent, SynchronizationEvent, FALSE);

    ExInitializeFastMutex(&BattClass->Mutex);

    if (MiniportInfo->Pdo != NULL)
    {
        Status = IoRegisterDeviceInterface(MiniportInfo->Pdo,
                                           &GUID_DEVICE_BATTERY,
                                           NULL,
                                           &BattClass->InterfaceName);
        if (NT_SUCCESS(Status))
        {
            DPRINT("Initialized battery interface: %wZ\n", &BattClass->InterfaceName);
            Status = IoSetDeviceInterfaceState(&BattClass->InterfaceName, TRUE);
            if (Status == STATUS_OBJECT_NAME_EXISTS)
            {
                DPRINT1("Got STATUS_OBJECT_NAME_EXISTS for SetDeviceInterfaceState\n");
                Status = STATUS_SUCCESS;
            }
        }
        else
        {
            DPRINT1("IoRegisterDeviceInterface failed (0x%x)\n", Status);
        }

        if (!NT_SUCCESS(Status))
        {
            if (BattClass->InterfaceName.Length != 0)
                RtlFreeUnicodeString(&BattClass->InterfaceName);

            ExFreePoolWithTag(BattClass, BATTERY_CLASS_DATA_TAG);
            return Status;
        }
    }

    *ClassData = BattClass;

    return STATUS_SUCCESS;
}

BCLASSAPI
NTSTATUS
NTAPI
BatteryClassIoctl(PVOID ClassData,
                  PIRP Irp)
{
    PBATTERY_CLASS_DATA BattClass;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;
    ULONG WaitTime;
    BATTERY_WAIT_STATUS BattWait;
    PBATTERY_QUERY_INFORMATION BattQueryInfo;
    PBATTERY_SET_INFORMATION BattSetInfo;
    LARGE_INTEGER Timeout;
    PBATTERY_STATUS BattStatus;
    BATTERY_NOTIFY BattNotify;
    BOOLEAN NotifySet;
    ULONG ReturnedLength = 0;

    DPRINT("BatteryClassIoctl(%p %p)\n", ClassData, Irp);

    BattClass = ClassData;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Irp->IoStatus.Information = 0;

    DPRINT("Received IOCTL %x for %p\n", IrpSp->Parameters.DeviceIoControl.IoControlCode,
           ClassData);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_BATTERY_QUERY_TAG:
            if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof(ULONG) && IrpSp->Parameters.DeviceIoControl.InputBufferLength != 0) ||
                IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            WaitTime = IrpSp->Parameters.DeviceIoControl.InputBufferLength == sizeof(ULONG) ? *(PULONG)Irp->AssociatedIrp.SystemBuffer : 0;

            Timeout.QuadPart = -10000LL * (LONGLONG)WaitTime;

            Status = BattClass->MiniportInfo.QueryTag(BattClass->MiniportInfo.Context,
                                                      (PULONG)Irp->AssociatedIrp.SystemBuffer);
            if (!NT_SUCCESS(Status))
            {
                ExAcquireFastMutex(&BattClass->Mutex);
                BattClass->EventTrigger = EVENT_BATTERY_TAG;
                BattClass->Waiting = TRUE;
                ExReleaseFastMutex(&BattClass->Mutex);

                Status = KeWaitForSingleObject(&BattClass->WaitEvent,
                                               Executive,
                                               KernelMode,
                                               FALSE,
                                               WaitTime != -1 ? &Timeout : NULL);

                ExAcquireFastMutex(&BattClass->Mutex);
                BattClass->Waiting = FALSE;
                ExReleaseFastMutex(&BattClass->Mutex);

                if (Status == STATUS_SUCCESS)
                {
                    Status = BattClass->MiniportInfo.QueryTag(BattClass->MiniportInfo.Context,
                                                              (PULONG)Irp->AssociatedIrp.SystemBuffer);
                    if (NT_SUCCESS(Status))
                        Irp->IoStatus.Information = sizeof(ULONG);
                }
                else
                {
                    Status = STATUS_NO_SUCH_DEVICE;
                }
            }
            else
            {
                Irp->IoStatus.Information = sizeof(ULONG);
            }
            break;

        case IOCTL_BATTERY_QUERY_STATUS:
            if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(BattWait) ||
                IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(BATTERY_STATUS))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            BattWait = *(PBATTERY_WAIT_STATUS)Irp->AssociatedIrp.SystemBuffer;

            if (BattWait.Timeout != 0)
            {
                BattNotify.PowerState = BattWait.PowerState;
                BattNotify.HighCapacity = BattWait.HighCapacity;
                BattNotify.LowCapacity = BattWait.LowCapacity;

                Status = BattClass->MiniportInfo.SetStatusNotify(BattClass->MiniportInfo.Context,
                                                                 BattWait.BatteryTag,
                                                                 &BattNotify);
                NotifySet = NT_SUCCESS(Status);
                if (!NotifySet)
                {
                    DPRINT("SetStatusNotify failed (0x%x)\n", Status);
                    // HACK: Continue anyway; the non-zero timeout will limit polling rate.
                    // FIXME: Hardcoded (wait for 5 seconds) because ACPI notifications don't work...
                    BattWait.Timeout = 5000;
                    // FIXME 2: All these IOCTLs handled in BatteryClassIoctl() should actually
                    // be queued and be serviced by a worker thread that also handles the slow
                    // battery polling, in case the battery doesn't support status notifications.
                }

                ExAcquireFastMutex(&BattClass->Mutex);
                BattClass->EventTrigger = EVENT_BATTERY_STATUS;
                BattClass->EventTriggerContext = BattWait;
                BattClass->Waiting = TRUE;
                ExReleaseFastMutex(&BattClass->Mutex);

                Timeout.QuadPart = -10000LL * (LONGLONG)BattWait.Timeout;
                Status = KeWaitForSingleObject(&BattClass->WaitEvent,
                                               Executive,
                                               KernelMode,
                                               FALSE,
                                               BattWait.Timeout != -1 ? &Timeout : NULL);
                if (Status == STATUS_TIMEOUT)
                    Status = STATUS_SUCCESS;

                ExAcquireFastMutex(&BattClass->Mutex);
                BattClass->Waiting = FALSE;
                ExReleaseFastMutex(&BattClass->Mutex);

                if (NotifySet &&
                    !NT_SUCCESS(BattClass->MiniportInfo.DisableStatusNotify(BattClass->MiniportInfo.Context)))
                    DPRINT1("DisableStatusNotify failed\n");
            }

            /* Zero the output buffer to prevent leakage of kernel data */
            BattStatus = Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(BattStatus, sizeof(BATTERY_STATUS));

            Status = BattClass->MiniportInfo.QueryStatus(BattClass->MiniportInfo.Context,
                                                         BattWait.BatteryTag,
                                                         BattStatus);
            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Information = sizeof(BATTERY_STATUS);
            }
            break;

        case IOCTL_BATTERY_QUERY_INFORMATION:
            if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*BattQueryInfo))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            BattQueryInfo = Irp->AssociatedIrp.SystemBuffer;

            Status = BattClass->MiniportInfo.QueryInformation(BattClass->MiniportInfo.Context,
                                                              BattQueryInfo->BatteryTag,
                                                              BattQueryInfo->InformationLevel,
                                                              BattQueryInfo->AtRate,
                                                              Irp->AssociatedIrp.SystemBuffer,
                                                              IrpSp->Parameters.DeviceIoControl.OutputBufferLength,
                                                              &ReturnedLength);
            Irp->IoStatus.Information = ReturnedLength;
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("QueryInformation failed (0x%x)\n", Status);
            }
            break;

        case IOCTL_BATTERY_SET_INFORMATION:
            if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*BattSetInfo))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            BattSetInfo = Irp->AssociatedIrp.SystemBuffer;

            if (BattClass->MiniportInfo.SetInformation == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }

            Status = BattClass->MiniportInfo.SetInformation(BattClass->MiniportInfo.Context,
                                                            BattSetInfo->BatteryTag,
                                                            BattSetInfo->InformationLevel,
                                                            BattSetInfo->Buffer);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SetInformation failed (0x%x)\n", Status);
            }
            break;

        default:
            DPRINT1("Received unsupported IRP %x\n", IrpSp->Parameters.DeviceIoControl.IoControlCode);
            /* Do NOT complete the irp */
            return STATUS_NOT_SUPPORTED;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
