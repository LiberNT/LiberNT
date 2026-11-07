/*
 * PROJECT:     LiberNT Apple SMC Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System Management Controller of Apple silicon machines
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/applesmc.h>
#include <reactos/drivers/applespmi.h>

#define SMC_TAG 'cmSA'

#define SMC_ENDPOINT                0x20
#define SMC_SHMEM_SIZE              0x1000
#define SMC_MAX_SIZE                255

#define SMC_MSG_READ_KEY            0x10
#define SMC_MSG_WRITE_KEY           0x11
#define SMC_MSG_GET_KEY_BY_INDEX    0x12
#define SMC_MSG_GET_KEY_INFO        0x13
#define SMC_MSG_INITIALIZE          0x17
#define SMC_MSG_NOTIFICATION        0x18
#define SMC_MSG_RW_KEY              0x20

#define SMC_TIMEOUT_MS              500
#define SMC_BOOT_TIMEOUT_MS         2000

#define SMC_GPIO_CMD_OUTPUT         (1UL << 24)

#define SMC_BOOT_STAGE_SHUTDOWN     0x00
#define SMC_BOOT_STAGE_KERNEL_STARTED 0x30

#define SMC_RTC_BYTES               6
#define SMC_RTC_SECOND_SHIFT        15
#define SMC_UNIX_EPOCH_SECONDS      11644473600ULL
#define SMC_RTC_FIRST_SECOND        1704067200LL
#define SMC_RTC_LAST_SECOND         4102444800LL

typedef struct _SMC_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    FDT_DRIVER_TREE Tree;

    PUCHAR Mbox;
    ULONG MboxSize;
    PUCHAR Sram;
    ULONG64 SramBase;
    ULONG SramSize;

    APPLE_RTKIT Rtkit;
    FAST_MUTEX Lock;
    PUCHAR Shmem;
    BOOLEAN Initialized;
    BOOLEAN Ready;
    BOOLEAN ReplyPending;
    ULONG64 Reply;
    ULONG MessageId;
    ULONG KeyCount;
    BOOLEAN HasBattery;
    PDEVICE_OBJECT Battery;
    BOOLEAN BatteryReported;

    PKTHREAD Thread;
    KEVENT StopEvent;
    volatile LONG Stop;
} SMC_DEVICE, *PSMC_DEVICE;

typedef struct _SMC_CHILD
{
    PDEVICE_OBJECT Self;
    PSMC_DEVICE Parent;
} SMC_CHILD, *PSMC_CHILD;

#define SMC_IS_CHILD(DeviceObject) ((DeviceObject)->DeviceType == FILE_DEVICE_BATTERY)

static
NTSTATUS
SmcCompleteChild(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
ULONG64
SmcNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
SmcSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
BOOLEAN
NTAPI
SmcShmemSetup(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Iova,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    PSMC_DEVICE Device = Context;

    UNREFERENCED_PARAMETER(Endpoint);
    *Virtual = NULL;
    *Preassigned = TRUE;
    if (!*Iova || !Device->Sram || *Iova < Device->SramBase || *Iova + Size > Device->SramBase + Device->SramSize)
        return FALSE;
    *Virtual = Device->Sram + (*Iova - Device->SramBase);
    return TRUE;
}

static
VOID
NTAPI
SmcReceive(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    PSMC_DEVICE Device = Context;

    if (Endpoint != SMC_ENDPOINT)
        return;
    if (!Device->Initialized)
    {
        if (Message >= Device->SramBase && Message + SMC_SHMEM_SIZE <= Device->SramBase + Device->SramSize)
        {
            Device->Shmem = Device->Sram + (Message - Device->SramBase);
            Device->Initialized = TRUE;
            DbgPrint("applesmc: shared memory at %I64x\n", Message);
        }
        else
        {
            DbgPrint("applesmc: shared memory %I64x is outside the SMC SRAM\n", Message);
        }
        return;
    }
    if ((Message & 0xff) == SMC_MSG_NOTIFICATION)
    {
        DbgPrint("applesmc: notification %08lx\n", (ULONG)(Message >> 32));
        return;
    }
    Device->Reply = Message;
    Device->ReplyPending = FALSE;
}

static
NTSTATUS
SmcCommand(
    _Inout_ PSMC_DEVICE Device,
    _In_ ULONG Command,
    _In_ ULONG Argument,
    _In_ ULONG Size,
    _In_ ULONG WriteSize,
    _Out_opt_ PULONG Data,
    _Out_opt_ PULONG ReturnedSize)
{
    ULONG64 Message, Deadline;

    if (!Device->Initialized)
        return STATUS_DEVICE_NOT_READY;
    Device->MessageId = (Device->MessageId + 1) & 0xf;
    Message = (ULONG64)(Command & 0xff) | ((ULONG64)(Size & 0xff) << 16) | ((ULONG64)(WriteSize & 0xff) << 24) |
              ((ULONG64)Device->MessageId << 12) | ((ULONG64)Argument << 32);
    Device->ReplyPending = TRUE;
    if (!AppleRtkitSend(&Device->Rtkit, SMC_ENDPOINT, Message))
        return STATUS_IO_DEVICE_ERROR;
    Deadline = SmcNowMs() + SMC_TIMEOUT_MS;
    while (Device->ReplyPending)
    {
        if (!AppleRtkitPoll(&Device->Rtkit))
        {
            if (SmcNowMs() > Deadline)
            {
                DbgPrint("applesmc: command %I64x timed out\n", Message);
                Device->ReplyPending = FALSE;
                return STATUS_IO_TIMEOUT;
            }
            KeStallExecutionProcessor(50);
        }
    }
    if (((Device->Reply >> 12) & 0xf) != Device->MessageId)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (Device->Reply & 0xff)
        return STATUS_UNSUCCESSFUL;
    if (Data)
        *Data = (ULONG)(Device->Reply >> 32);
    if (ReturnedSize)
        *ReturnedSize = (ULONG)((Device->Reply >> 16) & 0xff);
    return STATUS_SUCCESS;
}

static
NTSTATUS
SmcReadKey(
    _Inout_ PSMC_DEVICE Device,
    _In_ ULONG Key,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size)
{
    ULONG Data = 0, Index;
    NTSTATUS Status;

    if (!Size || Size > SMC_MAX_SIZE)
        return STATUS_INVALID_PARAMETER;
    Status = SmcCommand(Device, SMC_MSG_READ_KEY, Key, Size, 0, &Data, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Size <= 4)
        RtlCopyMemory(Buffer, &Data, Size);
    else
        for (Index = 0; Index < Size; ++Index)
            Buffer[Index] = READ_REGISTER_UCHAR(Device->Shmem + Index);
    return STATUS_SUCCESS;
}

static
NTSTATUS
SmcWriteKey(
    _Inout_ PSMC_DEVICE Device,
    _In_ ULONG Key,
    _In_reads_bytes_(Size) const UCHAR *Buffer,
    _In_ ULONG Size)
{
    ULONG Index;

    if (!Size || Size > SMC_MAX_SIZE)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Size; ++Index)
        WRITE_REGISTER_UCHAR(Device->Shmem + Index, Buffer[Index]);
    KeMemoryBarrier();
    return SmcCommand(Device, SMC_MSG_WRITE_KEY, Key, Size, 0, NULL, NULL);
}

static
CHAR
SmcHexDigit(
    _In_ ULONG Value)
{
    return (CHAR)(Value < 10 ? '0' + Value : 'a' + Value - 10);
}

static
NTSTATUS
SmcSetGpio(
    _Inout_ PSMC_DEVICE Device,
    _In_ BOOLEAN LowBank,
    _In_ UCHAR Pin,
    _In_ BOOLEAN Value)
{
    ULONG Key = APPLE_SMC_KEY('g', LowBank ? 'p' : 'P', SmcHexDigit(Pin >> 4), SmcHexDigit(Pin & 0xf));
    ULONG Command = SMC_GPIO_CMD_OUTPUT | (Value ? 1 : 0);

    return SmcWriteKey(Device, Key, (const UCHAR *)&Command, sizeof(Command));
}

static
VOID
SmcLogKey(
    _Inout_ PSMC_DEVICE Device,
    _In_ ULONG Key,
    _In_ ULONG Size)
{
    UCHAR Value[8] = { 0 };
    NTSTATUS Status = SmcReadKey(Device, Key, Value, Size);

    DbgPrint("applesmc: key %c%c%c%c = %02x %02x %02x %02x (%lx)\n", (CHAR)(Key >> 24), (CHAR)(Key >> 16),
             (CHAR)(Key >> 8), (CHAR)Key, Value[0], Value[1], Value[2], Value[3], Status);
}

static
ULONG
SmcRebootCell(
    _Inout_ PSMC_DEVICE Device,
    _In_z_ PCSTR Wanted)
{
    const ULONG *Cells;
    const CHAR *Names, *Name;
    ULONG Node, CellsLength, NamesLength, Cursor = 0, NameLength, Index;

    Node = FdtDriverFindChild(&Device->Tree, Device->Tree.Node, "apple,smc-reboot");
    if (Node == RISCV_FDT_NO_NODE)
        return 0;
    Cells = RiscvFdtGetProperty(&Device->Tree.Fdt, Node, "nvmem-cells", &CellsLength);
    Names = RiscvFdtGetProperty(&Device->Tree.Fdt, Node, "nvmem-cell-names", &NamesLength);
    for (Index = 0; Cells && Index < CellsLength / sizeof(ULONG); ++Index)
    {
        Name = RiscvFdtNextString(Names, NamesLength, &Cursor, &NameLength);
        if (Name && NameLength == strlen(Wanted) && !memcmp(Name, Wanted, NameLength))
            return RiscvFdtReadBigEndian32(&Cells[Index]);
    }
    return 0;
}

static
VOID
SmcWritePmuCell(
    _Inout_ PSMC_DEVICE Device,
    _In_z_ PCSTR Name,
    _In_ ULONG Value)
{
    ULONG Phandle = SmcRebootCell(Device, Name);
    NTSTATUS Status = Phandle ? AppleNvmemWriteCell(&Device->Tree, Phandle, Value) : STATUS_NOT_FOUND;

    DbgPrint("applesmc: PMU %s <- %lu (%lx)\n", Name, Value, Status);
}

static
VOID
SmcInitBootState(
    _Inout_ PSMC_DEVICE Device)
{
    static const PCSTR Names[] = { "boot_stage", "boot_error_count", "panic_count", "shutdown_flag" };
    APPLE_NVMEM_CELL Cell;
    ULONG Index, Phandle, Values[RTL_NUMBER_OF(Names)] = { 0 };
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(Names); ++Index)
    {
        Phandle = SmcRebootCell(Device, Names[Index]);
        Status = Phandle ? AppleNvmemReadCell(&Device->Tree, Phandle, &Cell) : STATUS_NOT_FOUND;
        if (NT_SUCCESS(Status))
            Values[Index] = AppleNvmemCellValue(&Cell);
        DbgPrint("applesmc: PMU %s at %lx = %lu (%lx)\n", Names[Index], Cell.Address, Values[Index], Status);
        if (!NT_SUCCESS(Status))
            return;
    }
    SmcWritePmuCell(Device, "boot_stage", SMC_BOOT_STAGE_KERNEL_STARTED);
    if (Values[1])
        SmcWritePmuCell(Device, "boot_error_count", 0);
    if (Values[2])
        SmcWritePmuCell(Device, "panic_count", 0);
}

static
VOID
SmcPrepareShutdown(
    _Inout_ PSMC_DEVICE Device,
    _In_ POWER_ACTION Action)
{
    BOOLEAN PowerOff = (Action != PowerActionShutdownReset);
    ULONG Warning = PowerOff ? APPLE_SMC_KEY('o', 'f', 'f', 'w') : APPLE_SMC_KEY('r', 'e', 's', 't');
    NTSTATUS Status;

    ExAcquireFastMutex(&Device->Lock);
    Status = SmcWriteKey(Device, APPLE_SMC_KEY('M', 'B', 'S', 'E'), (const UCHAR *)&Warning, sizeof(Warning));
    ExReleaseFastMutex(&Device->Lock);
    DbgPrint("applesmc: preparing for %s: %lx\n", PowerOff ? "power off" : "restart", Status);
    SmcWritePmuCell(Device, "boot_stage", SMC_BOOT_STAGE_SHUTDOWN);
    SmcWritePmuCell(Device, "shutdown_flag", PowerOff ? 1 : 0);
}

static
VOID
SmcPowerOff(
    _Inout_ PSMC_DEVICE Device)
{
    ULONG Command = APPLE_SMC_KEY('o', 'f', 'f', '1');
    NTSTATUS Status;

    ExAcquireFastMutex(&Device->Lock);
    Status = SmcWriteKey(Device, APPLE_SMC_KEY('M', 'B', 'S', 'E'), (const UCHAR *)&Command, sizeof(Command));
    ExReleaseFastMutex(&Device->Lock);
    DbgPrint("applesmc: power off requested: %lx\n", Status);
    if (NT_SUCCESS(Status))
        SmcSleepMs(100);
}

static
VOID
SmcSyncClock(
    _Inout_ PSMC_DEVICE Device)
{
    UCHAR Counter[SMC_RTC_BYTES] = { 0 };
    APPLE_NVMEM_CELL Offset;
    ULONG Node, Phandle;
    ULONG64 Ticks = 0, Bias = 0;
    LONG64 Seconds;
    LARGE_INTEGER Time;
    NTSTATUS Status;

    Node = FdtDriverFindChild(&Device->Tree, Device->Tree.Node, "apple,smc-rtc");
    if (Node == RISCV_FDT_NO_NODE || !RiscvFdtReadU32(&Device->Tree.Fdt, Node, "nvmem-cells", &Phandle))
        return;
    Status = SmcReadKey(Device, APPLE_SMC_KEY('C', 'L', 'K', 'M'), Counter, sizeof(Counter));
    if (NT_SUCCESS(Status))
        Status = AppleNvmemReadCell(&Device->Tree, Phandle, &Offset);
    if (NT_SUCCESS(Status) && Offset.Length != SMC_RTC_BYTES)
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applesmc: real-time clock unavailable: %lx\n", Status);
        return;
    }
    RtlCopyMemory(&Ticks, Counter, SMC_RTC_BYTES);
    RtlCopyMemory(&Bias, Offset.Data, SMC_RTC_BYTES);
    Seconds = (LONG64)(((Ticks + Bias) << (64 - 8 * SMC_RTC_BYTES))) >> (64 - 8 * SMC_RTC_BYTES + SMC_RTC_SECOND_SHIFT);
    DbgPrint("applesmc: real-time clock %I64d s since 1970 (counter %I64x, offset %I64x)\n", Seconds, Ticks, Bias);
    if (Seconds < SMC_RTC_FIRST_SECOND || Seconds > SMC_RTC_LAST_SECOND)
        return;
    Time.QuadPart = (LONGLONG)(((ULONG64)Seconds + SMC_UNIX_EPOCH_SECONDS) * 10000000ULL);
    Status = ZwSetSystemTime(&Time, NULL);
    if (!NT_SUCCESS(Status))
        DbgPrint("applesmc: setting the system time failed: %lx\n", Status);
}

static
VOID
NTAPI
SmcWorker(
    _In_ PVOID Context)
{
    PSMC_DEVICE Device = Context;
    LARGE_INTEGER Interval;

    Interval.QuadPart = -50 * 10000;
    while (!Device->Stop)
    {
        ExAcquireFastMutex(&Device->Lock);
        AppleRtkitPoll(&Device->Rtkit);
        ExReleaseFastMutex(&Device->Lock);
        KeWaitForSingleObject(&Device->StopEvent, Executive, KernelMode, FALSE, &Interval);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
SmcStopHardware(
    _Inout_ PSMC_DEVICE Device)
{
    ExAcquireFastMutex(&Device->Lock);
    Device->Ready = FALSE;
    ExReleaseFastMutex(&Device->Lock);
    if (Device->Thread)
    {
        InterlockedExchange(&Device->Stop, 1);
        KeSetEvent(&Device->StopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Device->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Device->Thread);
        Device->Thread = NULL;
    }
    Device->Initialized = FALSE;
    Device->Shmem = NULL;
    if (Device->Mbox)
        MmUnmapIoSpace(Device->Mbox, Device->MboxSize);
    if (Device->Sram)
        MmUnmapIoSpace(Device->Sram, Device->SramSize);
    Device->Mbox = NULL;
    Device->Sram = NULL;
    FdtDriverClose(&Device->Tree);
}

static
NTSTATUS
SmcStartHardware(
    _Inout_ PSMC_DEVICE Device)
{
    ULONG MboxNode, Phandle, Data = 0;
    ULONG64 Deadline;
    HANDLE ThreadHandle;
    NTSTATUS Status;

    Status = FdtDriverOpen(Device->Pdo, &Device->Tree);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applesmc: device tree unavailable: %lx\n", Status);
        return Status;
    }
    if (!RiscvFdtReadU32(&Device->Tree.Fdt, Device->Tree.Node, "mboxes", &Phandle) ||
        (MboxNode = FdtDriverFindPhandle(&Device->Tree, Phandle)) == RISCV_FDT_NO_NODE)
    {
        DbgPrint("applesmc: the SMC has no mailbox\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Device->Sram = FdtDriverMapReg(&Device->Tree, Device->Tree.Node, 1, &Device->SramBase, &Device->SramSize);
    Device->Mbox = FdtDriverMapReg(&Device->Tree, MboxNode, 0, NULL, &Device->MboxSize);
    DbgPrint("applesmc: SRAM %I64x+%lx at %p, mailbox at %p\n", Device->SramBase, Device->SramSize, Device->Sram,
             Device->Mbox);
    if (!Device->Sram || !Device->Mbox)
        return STATUS_INSUFFICIENT_RESOURCES;

    Device->Rtkit.Name = "applesmc";
    Device->Rtkit.Mailbox = Device->Mbox;
    Device->Rtkit.Context = Device;
    Device->Rtkit.ShmemSetup = SmcShmemSetup;
    Device->Rtkit.Receive = SmcReceive;
    Status = AppleRtkitWake(&Device->Rtkit, SMC_BOOT_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!AppleRtkitHasEndpoint(&Device->Rtkit, SMC_ENDPOINT))
    {
        DbgPrint("applesmc: the SMC firmware has no SMC endpoint\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    AppleRtkitStartEndpoint(&Device->Rtkit, SMC_ENDPOINT);
    AppleRtkitSend(&Device->Rtkit, SMC_ENDPOINT, SMC_MSG_INITIALIZE);
    Deadline = SmcNowMs() + SMC_TIMEOUT_MS;
    while (!Device->Initialized)
    {
        if (!AppleRtkitPoll(&Device->Rtkit))
        {
            if (SmcNowMs() > Deadline)
            {
                DbgPrint("applesmc: the SMC did not answer its initialization\n");
                return STATUS_IO_TIMEOUT;
            }
            SmcSleepMs(1);
        }
    }

    if (NT_SUCCESS(SmcReadKey(Device, APPLE_SMC_KEY('#', 'K', 'E', 'Y'), (PUCHAR)&Data, sizeof(Data))))
        Device->KeyCount = ((Data & 0xff) << 24) | ((Data & 0xff00) << 8) | ((Data >> 8) & 0xff00) | (Data >> 24);
    DbgPrint("applesmc: SMC is up with %lu keys\n", Device->KeyCount);
    SmcLogKey(Device, APPLE_SMC_KEY('B', 'U', 'I', 'C'), 1);
    Device->HasBattery = NT_SUCCESS(SmcReadKey(Device, APPLE_SMC_KEY('B', 'U', 'I', 'C'), (PUCHAR)&Data, 1));
    SmcLogKey(Device, APPLE_SMC_KEY('A', 'C', '-', 'W'), 1);
    SmcSyncClock(Device);
    SmcInitBootState(Device);

    KeInitializeEvent(&Device->StopEvent, NotificationEvent, FALSE);
    Device->Stop = 0;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, SmcWorker, Device);
    if (!NT_SUCCESS(Status))
        return Status;
    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&Device->Thread, NULL);
    ZwClose(ThreadHandle);
    ExAcquireFastMutex(&Device->Lock);
    Device->Ready = TRUE;
    ExReleaseFastMutex(&Device->Lock);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SmcDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    if (SMC_IS_CHILD(DeviceObject))
        return SmcCompleteChild(Irp, Irp->IoStatus.Status);
    Irp->IoStatus.Information = 0;
    if (Irp->RequestorMode != KernelMode && Stack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
        goto Done;
    ExAcquireFastMutex(&Device->Lock);
    if (!Device->Ready)
    {
        ExReleaseFastMutex(&Device->Lock);
        Status = STATUS_DEVICE_NOT_READY;
        goto Done;
    }
    switch (Code)
    {
        case IOCTL_APPLE_SMC_READ_KEY:
            if (InputLength < sizeof(APPLE_SMC_KEY_DATA) || OutputLength < sizeof(APPLE_SMC_KEY_DATA) ||
                ((PAPPLE_SMC_KEY_DATA)Buffer)->Length > APPLE_SMC_MAX_DATA)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = SmcReadKey(Device, ((PAPPLE_SMC_KEY_DATA)Buffer)->Key, ((PAPPLE_SMC_KEY_DATA)Buffer)->Data,
                                ((PAPPLE_SMC_KEY_DATA)Buffer)->Length);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(APPLE_SMC_KEY_DATA);
            break;

        case IOCTL_APPLE_SMC_WRITE_KEY:
            if (InputLength < sizeof(APPLE_SMC_KEY_DATA) || ((PAPPLE_SMC_KEY_DATA)Buffer)->Length > APPLE_SMC_MAX_DATA)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = SmcWriteKey(Device, ((PAPPLE_SMC_KEY_DATA)Buffer)->Key, ((PAPPLE_SMC_KEY_DATA)Buffer)->Data,
                                 ((PAPPLE_SMC_KEY_DATA)Buffer)->Length);
            break;

        case IOCTL_APPLE_SMC_SET_GPIO:
            if (InputLength < sizeof(APPLE_SMC_GPIO))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = SmcSetGpio(Device, ((PAPPLE_SMC_GPIO)Buffer)->LowBank, ((PAPPLE_SMC_GPIO)Buffer)->Pin,
                                ((PAPPLE_SMC_GPIO)Buffer)->Value);
            break;

        default:
            break;
    }
    ExReleaseFastMutex(&Device->Lock);

Done:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
SmcCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
SmcCopyString(
    _In_reads_bytes_(Size) const WCHAR *Source,
    _In_ SIZE_T Size,
    _Out_ PULONG_PTR Information)
{
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, SMC_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static
VOID
SmcCreateBattery(
    _Inout_ PSMC_DEVICE Device)
{
    PDEVICE_OBJECT Child;
    PSMC_CHILD Extension;
    NTSTATUS Status;

    if (!Device->HasBattery || Device->Battery)
        return;
    Status = IoCreateDevice(Device->DeviceObject->DriverObject, sizeof(SMC_CHILD), NULL, FILE_DEVICE_BATTERY,
                            FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN, FALSE, &Child);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applesmc: battery device not created: %lx\n", Status);
        return;
    }
    Extension = Child->DeviceExtension;
    Extension->Self = Child;
    Extension->Parent = Device;
    Child->Flags |= DO_POWER_PAGABLE;
    Child->Flags &= ~DO_DEVICE_INITIALIZING;
    Device->Battery = Child;
    Device->BatteryReported = TRUE;
    IoInvalidateDeviceRelations(Device->Pdo, BusRelations);
}

static
NTSTATUS
SmcChildPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_CHILD Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR Id[] = L"APPLESMC\\BATTERY";
                    Status = SmcCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                {
                    static const WCHAR Ids[] = L"APPLESMC\\BATTERY\0";
                    Status = SmcCopyString(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    static const WCHAR Id[] = L"0";
                    Status = SmcCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
            {
                static const WCHAR Text[] = L"Apple silicon internal battery";
                Status = SmcCopyString(Text, sizeof(Text), &Irp->IoStatus.Information);
            }
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 || Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                PDEVICE_RELATIONS Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), SMC_TAG);

                if (!Relations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Relations->Count = 1;
                Relations->Objects[0] = DeviceObject;
                ObReferenceObject(DeviceObject);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            if (!Extension->Parent || !Extension->Parent->BatteryReported)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                if (Extension->Parent)
                    Extension->Parent->Battery = NULL;
                IoDeleteDevice(DeviceObject);
                return Status;
            }
            break;

        default:
            break;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
SmcPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT Lower;
    KEVENT Event;
    NTSTATUS Status;

    if (SMC_IS_CHILD(DeviceObject))
        return SmcChildPnp(DeviceObject, Irp);
    Lower = Device->LowerDevice;
    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, SmcCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Lower, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
            {
                Status = SmcStartHardware(Device);
                if (!NT_SUCCESS(Status))
                    SmcStopHardware(Device);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            if (NT_SUCCESS(Status))
                SmcCreateBattery(Device);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
                PDEVICE_RELATIONS Relations;
                ULONG Count = Previous ? Previous->Count : 0;
                ULONG Extra = (Device->Battery && Device->BatteryReported) ? 1 : 0;

                Relations = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + Extra + 1]),
                                                  SMC_TAG);
                if (!Relations)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                Relations->Count = Count;
                if (Count)
                    RtlCopyMemory(Relations->Objects, Previous->Objects, Count * sizeof(PDEVICE_OBJECT));
                if (Extra)
                {
                    Relations->Objects[Relations->Count++] = Device->Battery;
                    ObReferenceObject(Device->Battery);
                }
                if (Previous)
                    ExFreePool(Previous);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            SmcStopHardware(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Device->BatteryReported = FALSE;
            if (Device->Battery)
            {
                ((PSMC_CHILD)Device->Battery->DeviceExtension)->Parent = NULL;
                IoDeleteDevice(Device->Battery);
                Device->Battery = NULL;
            }
            SmcStopHardware(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Lower, Irp);
            IoDetachDevice(Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Lower, Irp);
}

static
NTSTATUS
NTAPI
SmcPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN PowerOff = FALSE;
    NTSTATUS Status;

    if (SMC_IS_CHILD(DeviceObject))
    {
        PoStartNextPowerIrp(Irp);
        return SmcCompleteChild(Irp, STATUS_SUCCESS);
    }
    if (Stack->MinorFunction == IRP_MN_SET_POWER && Stack->Parameters.Power.Type == SystemPowerState &&
        Stack->Parameters.Power.State.SystemState == PowerSystemShutdown && Device->Initialized)
    {
        SmcPrepareShutdown(Device, Stack->Parameters.Power.ShutdownType);
        PowerOff = (Stack->Parameters.Power.ShutdownType != PowerActionShutdownReset);
    }
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(Device->LowerDevice, Irp);
    /* FIXME: the SMC powers the machine off here, before devices later in the shutdown pass get their system power IRP */
    if (PowerOff)
        SmcPowerOff(Device);
    return Status;
}

static
NTSTATUS
NTAPI
SmcCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SmcPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_DEVICE Device = DeviceObject->DeviceExtension;

    if (SMC_IS_CHILD(DeviceObject))
        return SmcCompleteChild(Irp, Irp->IoStatus.Status);
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
SmcAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_SMC_DEVICE_NAME);
    PDEVICE_OBJECT DeviceObject;
    PSMC_DEVICE Device;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(SMC_DEVICE), &Name, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    Device = DeviceObject->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->DeviceObject = DeviceObject;
    Device->Pdo = Pdo;
    ExInitializeFastMutex(&Device->Lock);
    Device->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, Pdo);
    if (!Device->LowerDevice)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }
    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SmcUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = SmcCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SmcCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SmcDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SmcDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = SmcPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SmcPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = SmcPassThrough;
    DriverObject->DriverExtension->AddDevice = SmcAddDevice;
    DriverObject->DriverUnload = SmcUnload;
    return STATUS_SUCCESS;
}
