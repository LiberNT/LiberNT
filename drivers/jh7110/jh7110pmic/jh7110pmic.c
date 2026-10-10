/*
 * PROJECT:     LiberNT StarFive JH7110 PMIC driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     X-Powers AXP15060 PMIC bus on the StarFive JH7110 DesignWare I2C controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define JH_SYSCRG_BASE              0x13020000ULL
#define JH_SYSCRG_SIZE              0x400
#define JH_SYSCRG_RESET_ASSERT      0x2F8
#define JH_SYSCRG_RESET_STATUS      0x308
#define JH_CLK_ENABLE               (1UL << 31)

#define DW_I2C_SIZE                 0x100
#define DW_IC_CON                   0x00
#define DW_IC_TAR                   0x04
#define DW_IC_DATA_CMD              0x10
#define DW_IC_RAW_INTR_STAT         0x34
#define DW_IC_CLR_INTR              0x40
#define DW_IC_ENABLE                0x6C
#define DW_IC_STATUS                0x70
#define DW_IC_TX_ABRT_SOURCE        0x80
#define DW_IC_ENABLE_STATUS         0x9C
#define DW_IC_CON_MASTER            (1UL << 0)
#define DW_IC_CON_SPEED_STD         (1UL << 1)
#define DW_IC_CON_RESTART_EN        (1UL << 5)
#define DW_IC_CON_SLAVE_DISABLE     (1UL << 6)
#define DW_IC_DATA_CMD_READ         (1UL << 8)
#define DW_IC_DATA_CMD_STOP         (1UL << 9)
#define DW_IC_INTR_TX_ABRT          (1UL << 6)
#define DW_IC_STATUS_RFNE           (1UL << 3)
#define DW_I2C_TIMEOUT_US           20000

#define AXP15060_PWR_OUT_CTRL1      0x10
#define AXP15060_PWR_OUT_CTRL2      0x11

typedef struct _JH7110PMIC_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PUCHAR Registers;
    FAST_MUTEX Lock;
    ULONG ClockId;
    ULONG ResetId;
    UCHAR Address;
    BOOLEAN HasPmic;
} JH7110PMIC_EXTENSION, *PJH7110PMIC_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static ULONG
JhPmicCell(_In_reads_bytes_(4) const UCHAR *Bytes)
{
    return ((ULONG)Bytes[0] << 24) | ((ULONG)Bytes[1] << 16) | ((ULONG)Bytes[2] << 8) | Bytes[3];
}

static BOOLEAN
JhPmicQuery(_In_ HANDLE Key, _In_z_ PCWSTR Name, _Out_writes_bytes_(Size) PUCHAR Buffer, _In_ ULONG Size,
            _Out_ PULONG Length)
{
    UCHAR Storage[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 128];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
    UNICODE_STRING ValueName;
    ULONG Result;

    *Length = 0;
    RtlInitUnicodeString(&ValueName, Name);
    if (!NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Information, sizeof(Storage),
                                    &Result)) ||
        Information->DataLength > Size)
        return FALSE;
    RtlCopyMemory(Buffer, Information->Data, Information->DataLength);
    *Length = Information->DataLength;
    return TRUE;
}

static BOOLEAN
JhPmicListContains(_In_reads_bytes_(Length) const UCHAR *List, _In_ ULONG Length, _In_z_ const CHAR *Wanted)
{
    SIZE_T WantedLength = strlen(Wanted);
    ULONG Offset = 0;

    while (Offset < Length)
    {
        SIZE_T Entry = 0;

        while (Offset + Entry < Length && List[Offset + Entry])
            Entry++;
        if (Entry == WantedLength && !memcmp(List + Offset, Wanted, WantedLength))
            return TRUE;
        Offset += (ULONG)Entry + 1;
    }
    return FALSE;
}

static VOID
JhPmicReadConfiguration(_In_ PJH7110PMIC_EXTENSION Extension)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 128 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Buffer;
    UCHAR Value[128];
    HANDLE Parameters;
    ULONG Index, Result, Length;

    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(Extension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_READ,
                                            &Parameters)))
        return;
    if (!JhPmicQuery(Parameters, L"clocks", Value, sizeof(Value), &Length) || Length < 8)
    {
        ZwClose(Parameters);
        return;
    }
    Extension->ClockId = JhPmicCell(Value + 4);
    if (!JhPmicQuery(Parameters, L"resets", Value, sizeof(Value), &Length) || Length < 8)
    {
        ZwClose(Parameters);
        return;
    }
    Extension->ResetId = JhPmicCell(Value + 4);

    for (Index = 0; !Extension->HasPmic; ++Index)
    {
        OBJECT_ATTRIBUTES Attributes;
        UNICODE_STRING Name;
        HANDLE Child;

        if (!NT_SUCCESS(ZwEnumerateKey(Parameters, Index, KeyBasicInformation, Basic, sizeof(Buffer) - sizeof(WCHAR),
                                       &Result)))
            break;
        Name.Buffer = Basic->Name;
        Name.Length = (USHORT)Basic->NameLength;
        Name.MaximumLength = (USHORT)Basic->NameLength;
        InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parameters, NULL);
        if (!NT_SUCCESS(ZwOpenKey(&Child, KEY_READ, &Attributes)))
            continue;
        if (JhPmicQuery(Child, L"compatible", Value, sizeof(Value), &Length) &&
            JhPmicListContains(Value, Length, "x-powers,axp15060") &&
            JhPmicQuery(Child, L"reg", Value, sizeof(Value), &Length) && Length >= 4 && JhPmicCell(Value) < 0x80)
        {
            Extension->Address = (UCHAR)JhPmicCell(Value);
            Extension->HasPmic = TRUE;
        }
        ZwClose(Child);
    }
    ZwClose(Parameters);
}

static BOOLEAN
JhPmicEnableBus(_In_ PJH7110PMIC_EXTENSION Extension)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR SysCrg;
    PULONG Clock, Assert, Status;
    ULONG Mask = 1UL << (Extension->ResetId % 32), Elapsed;
    BOOLEAN Released = FALSE;

    Address.QuadPart = JH_SYSCRG_BASE;
    SysCrg = MmMapIoSpace(Address, JH_SYSCRG_SIZE, MmNonCached);
    if (!SysCrg)
        return FALSE;
    Clock = (PULONG)(SysCrg + Extension->ClockId * sizeof(ULONG));
    Assert = (PULONG)(SysCrg + JH_SYSCRG_RESET_ASSERT + (Extension->ResetId / 32) * sizeof(ULONG));
    Status = (PULONG)(SysCrg + JH_SYSCRG_RESET_STATUS + (Extension->ResetId / 32) * sizeof(ULONG));
    WRITE_REGISTER_ULONG(Clock, READ_REGISTER_ULONG(Clock) | JH_CLK_ENABLE);
    WRITE_REGISTER_ULONG(Assert, READ_REGISTER_ULONG(Assert) & ~Mask);
    for (Elapsed = 0; Elapsed < 1000; Elapsed += 10)
    {
        if (READ_REGISTER_ULONG(Status) & Mask)
        {
            Released = TRUE;
            break;
        }
        KeStallExecutionProcessor(10);
    }
    MmUnmapIoSpace(SysCrg, JH_SYSCRG_SIZE);
    return Released;
}

static ULONG
JhI2cRead(_In_ PJH7110PMIC_EXTENSION Extension, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Extension->Registers + Offset));
}

static VOID
JhI2cWrite(_In_ PJH7110PMIC_EXTENSION Extension, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + Offset), Value);
}

static BOOLEAN
JhI2cDisable(_In_ PJH7110PMIC_EXTENSION Extension)
{
    ULONG Elapsed;

    JhI2cWrite(Extension, DW_IC_ENABLE, 0);
    for (Elapsed = 0; Elapsed < DW_I2C_TIMEOUT_US; Elapsed += 10)
    {
        if (!(JhI2cRead(Extension, DW_IC_ENABLE_STATUS) & 1))
            return TRUE;
        KeStallExecutionProcessor(10);
    }
    return FALSE;
}

static NTSTATUS
JhPmicReadRegister(_In_ PJH7110PMIC_EXTENSION Extension, _In_ UCHAR Register, _Out_ PUCHAR Value)
{
    NTSTATUS Status = STATUS_IO_TIMEOUT;
    ULONG Elapsed;

    ExAcquireFastMutex(&Extension->Lock);
    if (!JhI2cDisable(Extension))
        goto Exit;
    JhI2cWrite(Extension, DW_IC_CON, DW_IC_CON_MASTER | DW_IC_CON_SPEED_STD | DW_IC_CON_RESTART_EN |
                                     DW_IC_CON_SLAVE_DISABLE);
    JhI2cWrite(Extension, DW_IC_TAR, Extension->Address);
    (VOID)JhI2cRead(Extension, DW_IC_CLR_INTR);
    JhI2cWrite(Extension, DW_IC_ENABLE, 1);
    JhI2cWrite(Extension, DW_IC_DATA_CMD, Register);
    JhI2cWrite(Extension, DW_IC_DATA_CMD, DW_IC_DATA_CMD_READ | DW_IC_DATA_CMD_STOP);
    for (Elapsed = 0; Elapsed < DW_I2C_TIMEOUT_US; Elapsed += 10)
    {
        if (JhI2cRead(Extension, DW_IC_STATUS) & DW_IC_STATUS_RFNE)
        {
            *Value = (UCHAR)JhI2cRead(Extension, DW_IC_DATA_CMD);
            Status = STATUS_SUCCESS;
            break;
        }
        if (JhI2cRead(Extension, DW_IC_RAW_INTR_STAT) & DW_IC_INTR_TX_ABRT)
        {
            DPRINT1("JH7110PMIC: read 0x%02x aborted (source 0x%lx)\n", Register,
                    JhI2cRead(Extension, DW_IC_TX_ABRT_SOURCE));
            (VOID)JhI2cRead(Extension, DW_IC_CLR_INTR);
            Status = STATUS_IO_DEVICE_ERROR;
            break;
        }
        KeStallExecutionProcessor(10);
    }
    (VOID)JhI2cDisable(Extension);
Exit:
    ExReleaseFastMutex(&Extension->Lock);
    return Status;
}

static NTSTATUS
JhPmicStart(_In_ PJH7110PMIC_EXTENSION Extension, _In_ PIO_STACK_LOCATION Stack)
{
    PCM_RESOURCE_LIST Resources = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    UCHAR Control1, Control2;
    ULONG Index;

    JhPmicReadConfiguration(Extension);
    if (!Extension->HasPmic)
        return STATUS_SUCCESS;
    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor =
            &Resources->List[0].PartialResourceList.PartialDescriptors[Index];

        if (Descriptor->Type == CmResourceTypeMemory && !Extension->Registers &&
            Descriptor->u.Memory.Length >= DW_I2C_SIZE)
        {
            Extension->Registers = MmMapIoSpace(Descriptor->u.Memory.Start, DW_I2C_SIZE, MmNonCached);
        }
    }
    if (!Extension->Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!JhPmicEnableBus(Extension))
        return STATUS_IO_TIMEOUT;

    if (NT_SUCCESS(JhPmicReadRegister(Extension, AXP15060_PWR_OUT_CTRL1, &Control1)) &&
        NT_SUCCESS(JhPmicReadRegister(Extension, AXP15060_PWR_OUT_CTRL2, &Control2)))
    {
        DPRINT1("JH7110PMIC: AXP15060 at 0x%02x, output control 0x%02x 0x%02x\n", Extension->Address, Control1,
                Control2);
        return STATUS_SUCCESS;
    }
    return STATUS_IO_DEVICE_ERROR;
}

static VOID
JhPmicRelease(_In_ PJH7110PMIC_EXTENSION Extension)
{
    if (Extension->Registers)
    {
        MmUnmapIoSpace(Extension->Registers, DW_I2C_SIZE);
        Extension->Registers = NULL;
    }
    Extension->HasPmic = FALSE;
}

static NTSTATUS NTAPI
JhPmicDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110PMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = JhPmicStart(Extension, Stack);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("JH7110PMIC: start failed 0x%08lx\n", Status);
                JhPmicRelease(Extension);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            JhPmicRelease(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            JhPmicRelease(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
JhPmicDispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110PMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
JhPmicDispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110PMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
JhPmicAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PJH7110PMIC_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(JH7110PMIC_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = Fdo;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    ExInitializeFastMutex(&Extension->Lock);
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = JhPmicDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_PNP] = JhPmicDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = JhPmicDispatchPower;
    DriverObject->DriverExtension->AddDevice = JhPmicAddDevice;
    return STATUS_SUCCESS;
}
