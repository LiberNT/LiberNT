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
#include <initguid.h>
#include <poclass.h>

#define SMC_TAG 'cmSA'

#define SMC_EVENT_BUTTON            0x7201
#define SMC_EVENT_LID               0x7203
#define SMC_BUTTON_POWER            0x01
#define SMC_BUTTON_TOUCHID          0x06

#define SMC_TYPE_DEVICE             0
#define SMC_TYPE_BATTERY            1
#define SMC_TYPE_POWER_BUTTON       2
#define SMC_TYPE_LID                3
#define SMC_CHILDREN                3

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
#define SMC_POLL_MS                 50
#define SMC_POWER_LOG_MS            30000

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
    ULONG Type;
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
    BOOLEAN Present[SMC_CHILDREN];
    PDEVICE_OBJECT Children[SMC_CHILDREN];
    BOOLEAN ChildrenReported;

    KSPIN_LOCK ButtonLock;
    PIRP ButtonIrp[SMC_CHILDREN];
    ULONG PowerPresses;
    LONG LidClosed;
    LONG LidReported;

    PKTHREAD Thread;
    KEVENT StopEvent;
    KEVENT WorkEvent;
    volatile LONG Stop;
    BOOLEAN PowerLogging;
} SMC_DEVICE, *PSMC_DEVICE;

typedef struct _SMC_CHILD
{
    ULONG Type;
    PDEVICE_OBJECT Self;
    PSMC_DEVICE Parent;
    UNICODE_STRING Interface;
} SMC_CHILD, *PSMC_CHILD;

typedef struct _SMC_CHILD_INFO
{
    PCWSTR Id;
    PCWSTR Description;
    const GUID *Interface;
    DEVICE_TYPE DeviceType;
} SMC_CHILD_INFO;

static const SMC_CHILD_INFO SmcChildInfo[SMC_CHILDREN] =
{
    { L"APPLESMC\\BATTERY", L"Apple silicon internal battery", NULL, FILE_DEVICE_BATTERY },
    { L"APPLESMC\\POWERBUTTON", L"Apple silicon power button", &GUID_DEVICE_SYS_BUTTON, FILE_DEVICE_UNKNOWN },
    { L"APPLESMC\\LID", L"Apple silicon lid switch", &GUID_DEVICE_LID, FILE_DEVICE_UNKNOWN },
};

#define SMC_IS_CHILD(DeviceObject) (*(PULONG)(DeviceObject)->DeviceExtension != SMC_TYPE_DEVICE)

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
BOOLEAN
SmcTakeButtonEvent(
    _Inout_ PSMC_DEVICE Device,
    _In_ ULONG Type,
    _Out_opt_ PULONG Event)
{
    if (Type == SMC_TYPE_POWER_BUTTON && Device->PowerPresses)
    {
        if (Event)
        {
            Device->PowerPresses--;
            *Event = SYS_BUTTON_POWER;
        }
        return TRUE;
    }
    if (Type == SMC_TYPE_LID && Device->LidClosed != Device->LidReported)
    {
        if (Event)
        {
            *Event = SYS_BUTTON_LID | (Device->LidClosed ? SYS_BUTTON_LID_CLOSED : SYS_BUTTON_LID_OPEN) |
                     (Device->LidReported < 0 ? SYS_BUTTON_LID_INITIAL : SYS_BUTTON_LID_CHANGED);
            Device->LidReported = Device->LidClosed;
        }
        return TRUE;
    }
    return FALSE;
}

static
VOID
SmcCompleteButton(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG Event)
{
    Irp->IoStatus.Information = 0;
    if (NT_SUCCESS(Status))
    {
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = Event;
        Irp->IoStatus.Information = sizeof(ULONG);
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
SmcFinishButtonWaits(
    _Inout_ PSMC_DEVICE Device,
    _In_ ULONG FlushMask)
{
    ULONG Index, Event = 0;
    BOOLEAN Flush;
    KIRQL OldIrql;
    PIRP Irp;

    for (Index = 0; Index < SMC_CHILDREN; ++Index)
    {
        Flush = (FlushMask & (1UL << Index)) != 0;
        if (FlushMask && !Flush)
            continue;
        KeAcquireSpinLock(&Device->ButtonLock, &OldIrql);
        Irp = Device->ButtonIrp[Index];
        if (Irp && (Flush || SmcTakeButtonEvent(Device, Index + 1, NULL)) && IoSetCancelRoutine(Irp, NULL))
        {
            Device->ButtonIrp[Index] = NULL;
            if (!Flush)
                SmcTakeButtonEvent(Device, Index + 1, &Event);
        }
        else
        {
            Irp = NULL;
        }
        KeReleaseSpinLock(&Device->ButtonLock, OldIrql);
        if (Irp)
            SmcCompleteButton(Irp, Flush ? STATUS_NO_SUCH_DEVICE : STATUS_SUCCESS, Event);
    }
}

static
VOID
SmcButtonNotification(
    _Inout_ PSMC_DEVICE Device,
    _In_ ULONG Event)
{
    UCHAR Data = (UCHAR)(Event >> 8);
    BOOLEAN Changed = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->ButtonLock, &OldIrql);
    if ((Event >> 16) == SMC_EVENT_BUTTON && (Data == SMC_BUTTON_POWER || Data == SMC_BUTTON_TOUCHID) &&
        (Event & 0xff) && Device->Present[SMC_TYPE_POWER_BUTTON - 1])
    {
        Device->PowerPresses = 1;
        Changed = TRUE;
    }
    else if ((Event >> 16) == SMC_EVENT_LID && Device->Present[SMC_TYPE_LID - 1])
    {
        Device->LidClosed = (Data != 0);
        Changed = TRUE;
    }
    KeReleaseSpinLock(&Device->ButtonLock, OldIrql);
    if (Changed)
        SmcFinishButtonWaits(Device, 0);
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
        SmcButtonNotification(Device, (ULONG)(Message >> 32));
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
    if (Node == FDT_NO_NODE)
        return 0;
    Cells = FdtGetProperty(&Device->Tree.Fdt, Node, "nvmem-cells", &CellsLength);
    Names = FdtGetProperty(&Device->Tree.Fdt, Node, "nvmem-cell-names", &NamesLength);
    for (Index = 0; Cells && Index < CellsLength / sizeof(ULONG); ++Index)
    {
        Name = FdtNextString(Names, NamesLength, &Cursor, &NameLength);
        if (Name && NameLength == strlen(Wanted) && !memcmp(Name, Wanted, NameLength))
            return FdtReadBigEndian32(&Cells[Index]);
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
SmcSetNotifications(
    _Inout_ PSMC_DEVICE Device,
    _In_ BOOLEAN Enable)
{
    UCHAR Flag = Enable ? 1 : 0;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ExAcquireFastMutex(&Device->Lock);
    Status = Device->Initialized ? SmcWriteKey(Device, APPLE_SMC_KEY('N', 'T', 'A', 'P'), &Flag, sizeof(Flag))
                                 : STATUS_DEVICE_NOT_READY;
    ExReleaseFastMutex(&Device->Lock);
    if (!NT_SUCCESS(Status))
        DbgPrint("applesmc: notifications could not be %s: %lx\n", Enable ? "enabled" : "disabled", Status);
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

    SmcSetNotifications(Device, FALSE);
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
    if (Node == FDT_NO_NODE || !FdtReadU32(&Device->Tree.Fdt, Node, "nvmem-cells", &Phandle))
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
LONG
SmcFloatToMilli(
    _In_ ULONG Bits)
{
    ULONG Exponent = (Bits >> 23) & 0xff;
    ULONG64 Value;
    LONG Shift;

    if (!Exponent)
        return 0;
    if (Exponent == 0xff)
        return (Bits & 0x80000000) ? -MAXLONG : MAXLONG;
    Value = (ULONG64)((Bits & 0x7fffff) | 0x800000) * 1000;
    Shift = (LONG)Exponent - 150;
    if (Shift >= 0)
        Value = (Shift > 8) ? MAXLONG : (Value << Shift);
    else
        Value = (Shift <= -64) ? 0 : ((Value + (1ULL << (-Shift - 1))) >> -Shift);
    if (Value > MAXLONG)
        Value = MAXLONG;
    return (Bits & 0x80000000) ? -(LONG)Value : (LONG)Value;
}

#define SMC_WATTS(Milli) ((Milli) < 0 ? "-" : ""), (LONG)(((Milli) < 0 ? -(Milli) : (Milli)) / 1000), \
                         (LONG)(((Milli) < 0 ? -(Milli) : (Milli)) % 1000)

static
VOID
SmcLogPower(
    _Inout_ PSMC_DEVICE Device)
{
    ULONG System = 0, DcIn = 0, Charger = 0;
    LONG Battery = 0, SystemMilli, DcInMilli;
    UCHAR Percent = 0, Charging = 0, Full = 0;
    PCSTR State;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ExAcquireFastMutex(&Device->Lock);
    Status = SmcReadKey(Device, APPLE_SMC_KEY('P', 'S', 'T', 'R'), (PUCHAR)&System, sizeof(System));
    if (NT_SUCCESS(Status))
        Status = SmcReadKey(Device, APPLE_SMC_KEY('B', '0', 'A', 'P'), (PUCHAR)&Battery, sizeof(Battery));
    if (NT_SUCCESS(Status))
    {
        SmcReadKey(Device, APPLE_SMC_KEY('P', 'D', 'T', 'R'), (PUCHAR)&DcIn, sizeof(DcIn));
        SmcReadKey(Device, APPLE_SMC_KEY('B', 'U', 'I', 'C'), &Percent, sizeof(Percent));
        SmcReadKey(Device, APPLE_SMC_KEY('C', 'H', 'I', 'S'), (PUCHAR)&Charger, sizeof(Charger));
        SmcReadKey(Device, APPLE_SMC_KEY('C', 'H', 'S', 'C'), &Charging, sizeof(Charging));
        SmcReadKey(Device, APPLE_SMC_KEY('B', 'S', 'F', 'C'), &Full, sizeof(Full));
    }
    ExReleaseFastMutex(&Device->Lock);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applesmc: power keys unreadable (%lx), no power summary\n", Status);
        Device->PowerLogging = FALSE;
        return;
    }
    SystemMilli = SmcFloatToMilli(System);
    DcInMilli = SmcFloatToMilli(DcIn);
    if (!Charger)
        State = "discharging";
    else if (Full)
        State = "full on AC";
    else if (Charging)
        State = "charging";
    else
        State = "on AC, not charging";
    DbgPrint("applesmc: power: system %s%ld.%03ld W, DC in %s%ld.%03ld W, battery %s%ld.%03ld W at %u%%, %s\n",
             SMC_WATTS(SystemMilli), SMC_WATTS(DcInMilli), SMC_WATTS(Battery), Percent, State);
}

static
VOID
NTAPI
SmcWorker(
    _In_ PVOID Context)
{
    PSMC_DEVICE Device = Context;
    PVOID Objects[2] = { &Device->StopEvent, &Device->WorkEvent };
    LARGE_INTEGER Interval;
    ULONG64 Now, PowerDue = SmcNowMs() + SMC_POWER_LOG_MS;
    ULONG WaitMs;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    while (!Device->Stop)
    {
        ExAcquireFastMutex(&Device->Lock);
        AppleRtkitPoll(&Device->Rtkit);
        ExReleaseFastMutex(&Device->Lock);
        Now = SmcNowMs();
        if (Device->PowerLogging && Now >= PowerDue)
        {
            SmcLogPower(Device);
            PowerDue = Now + SMC_POWER_LOG_MS;
        }
        WaitMs = AppleRtkitPollInterval(&Device->Rtkit, SMC_POLL_MS);
        if (Device->PowerLogging)
            WaitMs = (ULONG)min((ULONG64)WaitMs, PowerDue > Now ? PowerDue - Now : 0);
        Interval.QuadPart = -(LONGLONG)WaitMs * 10000;
        KeWaitForMultipleObjects(RTL_NUMBER_OF(Objects), Objects, WaitAny, Executive, KernelMode, FALSE,
                                 WaitMs == APPLE_RTKIT_NO_POLL ? NULL : &Interval, NULL);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
SmcStopHardware(
    _Inout_ PSMC_DEVICE Device)
{
    if (Device->Thread)
        SmcSetNotifications(Device, FALSE);
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
    AppleRtkitDisconnectInterrupt(&Device->Rtkit);
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
    _Inout_ PSMC_DEVICE Device,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    ULONG MboxNode, Phandle, Data = 0, Gsi;
    ULONG64 Deadline;
    HANDLE ThreadHandle;
    BOOLEAN Edge;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!Device->Thread && !Device->Rtkit.Interrupt);

    Status = FdtDriverOpen(Device->Pdo, &Device->Tree);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applesmc: device tree unavailable: %lx\n", Status);
        return Status;
    }
    if (!FdtReadU32(&Device->Tree.Fdt, Device->Tree.Node, "mboxes", &Phandle) ||
        (MboxNode = FdtDriverFindPhandle(&Device->Tree, Phandle)) == FDT_NO_NODE)
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
    KeInitializeEvent(&Device->WorkEvent, SynchronizationEvent, FALSE);
    Status = AppleRtkitQueryInterrupt(&Device->Tree, Device->Tree.Node, &Gsi, &Edge);
    if (NT_SUCCESS(Status))
        AppleRtkitConnectInterrupt(&Device->Rtkit, Raw, Translated, Gsi, &Device->WorkEvent);
    else
        DbgPrint("applesmc: the SMC mailbox declares no receive interrupt (%lx), polling\n", Status);
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
    Device->Present[SMC_TYPE_BATTERY - 1] = NT_SUCCESS(SmcReadKey(Device, APPLE_SMC_KEY('B', 'U', 'I', 'C'),
                                                                  (PUCHAR)&Data, 1));
    Device->Present[SMC_TYPE_POWER_BUTTON - 1] = NT_SUCCESS(SmcReadKey(Device, APPLE_SMC_KEY('b', 'H', 'L', 'D'),
                                                                       (PUCHAR)&Data, sizeof(Data)));
    Data = 0;
    Device->Present[SMC_TYPE_LID - 1] = NT_SUCCESS(SmcReadKey(Device, APPLE_SMC_KEY('M', 'S', 'L', 'D'),
                                                              (PUCHAR)&Data, 1));
    Device->LidClosed = (Data & 0xff) != 0;
    Device->LidReported = -1;
    DbgPrint("applesmc: power button %s, lid %s\n", Device->Present[SMC_TYPE_POWER_BUTTON - 1] ? "present" : "absent",
             Device->Present[SMC_TYPE_LID - 1] ? (Device->LidClosed ? "closed" : "open") : "absent");
    SmcLogKey(Device, APPLE_SMC_KEY('A', 'C', '-', 'W'), 1);
    SmcSyncClock(Device);
    SmcInitBootState(Device);

    KeInitializeEvent(&Device->StopEvent, NotificationEvent, FALSE);
    Device->Stop = 0;
    Device->PowerLogging = TRUE;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, SmcWorker, Device);
    if (!NT_SUCCESS(Status))
        return Status;
    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&Device->Thread, NULL);
    ZwClose(ThreadHandle);
    ExAcquireFastMutex(&Device->Lock);
    Device->Ready = TRUE;
    ExReleaseFastMutex(&Device->Lock);
    SmcSetNotifications(Device, TRUE);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SmcCancelButtonWait(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_CHILD Child = DeviceObject->DeviceExtension;
    PSMC_DEVICE Device = Child->Parent;
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    if (Device)
    {
        KeAcquireSpinLock(&Device->ButtonLock, &OldIrql);
        if (Device->ButtonIrp[Child->Type - 1] == Irp)
            Device->ButtonIrp[Child->Type - 1] = NULL;
        KeReleaseSpinLock(&Device->ButtonLock, OldIrql);
    }
    SmcCompleteButton(Irp, STATUS_CANCELLED, 0);
}

static
NTSTATUS
SmcChildDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_CHILD Child = DeviceObject->DeviceExtension;
    PSMC_DEVICE Device = Child->Parent;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG Event;
    KIRQL OldIrql;

    if (Child->Type == SMC_TYPE_BATTERY || Stack->MajorFunction != IRP_MJ_DEVICE_CONTROL ||
        (Code != IOCTL_GET_SYS_BUTTON_CAPS && Code != IOCTL_GET_SYS_BUTTON_EVENT))
    {
        return SmcCompleteChild(Irp, Irp->IoStatus.Status);
    }
    if (Irp->RequestorMode != KernelMode)
    {
        Irp->IoStatus.Information = 0;
        return SmcCompleteChild(Irp, STATUS_SUCCESS);
    }
    if (!Device)
    {
        SmcCompleteButton(Irp, STATUS_NO_SUCH_DEVICE, 0);
        return STATUS_NO_SUCH_DEVICE;
    }
    if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
    {
        SmcCompleteButton(Irp, STATUS_BUFFER_TOO_SMALL, 0);
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (Code == IOCTL_GET_SYS_BUTTON_CAPS)
    {
        SmcCompleteButton(Irp, STATUS_SUCCESS, (Child->Type == SMC_TYPE_LID) ? SYS_BUTTON_LID : SYS_BUTTON_POWER);
        return STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&Device->ButtonLock, &OldIrql);
    if (SmcTakeButtonEvent(Device, Child->Type, &Event))
    {
        KeReleaseSpinLock(&Device->ButtonLock, OldIrql);
        SmcCompleteButton(Irp, STATUS_SUCCESS, Event);
        return STATUS_SUCCESS;
    }
    if (Device->ButtonIrp[Child->Type - 1])
    {
        KeReleaseSpinLock(&Device->ButtonLock, OldIrql);
        SmcCompleteButton(Irp, STATUS_DEVICE_BUSY, 0);
        return STATUS_DEVICE_BUSY;
    }
    IoMarkIrpPending(Irp);
    IoSetCancelRoutine(Irp, SmcCancelButtonWait);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        KeReleaseSpinLock(&Device->ButtonLock, OldIrql);
        SmcCompleteButton(Irp, STATUS_CANCELLED, 0);
        return STATUS_PENDING;
    }
    if (!Irp->Cancel)
        Device->ButtonIrp[Child->Type - 1] = Irp;
    KeReleaseSpinLock(&Device->ButtonLock, OldIrql);
    return STATUS_PENDING;
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
        return SmcChildDeviceControl(DeviceObject, Irp);
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
SmcCopyId(
    _In_z_ PCWSTR Source,
    _In_ BOOLEAN MultiString,
    _Out_ PULONG_PTR Information)
{
    SIZE_T Size = (wcslen(Source) + (MultiString ? 2 : 1)) * sizeof(WCHAR);
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, SMC_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Copy, Size);
    RtlCopyMemory(Copy, Source, wcslen(Source) * sizeof(WCHAR));
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static
VOID
SmcCreateChildren(
    _Inout_ PSMC_DEVICE Device)
{
    PDEVICE_OBJECT Child;
    PSMC_CHILD Extension;
    BOOLEAN Created = FALSE;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < SMC_CHILDREN; ++Index)
    {
        if (!Device->Present[Index] || Device->Children[Index])
            continue;
        Status = IoCreateDevice(Device->DeviceObject->DriverObject, sizeof(SMC_CHILD), NULL,
                                SmcChildInfo[Index].DeviceType, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                                FALSE, &Child);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("applesmc: %S device not created: %lx\n", SmcChildInfo[Index].Id, Status);
            continue;
        }
        Extension = Child->DeviceExtension;
        RtlZeroMemory(Extension, sizeof(*Extension));
        Extension->Type = Index + 1;
        Extension->Self = Child;
        Extension->Parent = Device;
        Child->Flags |= DO_POWER_PAGABLE;
        Child->Flags &= ~DO_DEVICE_INITIALIZING;
        Device->Children[Index] = Child;
        Created = TRUE;
    }
    if (Created)
    {
        Device->ChildrenReported = TRUE;
        IoInvalidateDeviceRelations(Device->Pdo, BusRelations);
    }
}

static
VOID
SmcSetChildInterface(
    _Inout_ PSMC_CHILD Extension,
    _In_ BOOLEAN Enable)
{
    const GUID *Interface = SmcChildInfo[Extension->Type - 1].Interface;
    NTSTATUS Status;

    if (!Interface)
        return;
    if (Enable && !Extension->Interface.Buffer)
    {
        Status = IoRegisterDeviceInterface(Extension->Self, Interface, NULL, &Extension->Interface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("applesmc: %S interface not registered: %lx\n", SmcChildInfo[Extension->Type - 1].Id, Status);
            return;
        }
    }
    if (!Extension->Interface.Buffer)
        return;
    Status = IoSetDeviceInterfaceState(&Extension->Interface, Enable);
    if (!NT_SUCCESS(Status))
        DbgPrint("applesmc: %S interface state %u not set: %lx\n", SmcChildInfo[Extension->Type - 1].Id, Enable, Status);
}

static
NTSTATUS
SmcChildPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMC_CHILD Extension = DeviceObject->DeviceExtension;
    const SMC_CHILD_INFO *Info = &SmcChildInfo[Extension->Type - 1];
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                    Status = SmcCopyId(Info->Id, FALSE, &Irp->IoStatus.Information);
                    break;
                case BusQueryHardwareIDs:
                    Status = SmcCopyId(Info->Id, TRUE, &Irp->IoStatus.Information);
                    break;
                case BusQueryInstanceID:
                    Status = SmcCopyId(L"0", FALSE, &Irp->IoStatus.Information);
                    break;
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
                Status = SmcCopyId(Info->Description, FALSE, &Irp->IoStatus.Information);
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
            Capabilities->RawDeviceOK = (Info->Interface != NULL);
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
            SmcSetChildInterface(Extension, TRUE);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            SmcSetChildInterface(Extension, FALSE);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            SmcSetChildInterface(Extension, FALSE);
            if (Extension->Parent)
                SmcFinishButtonWaits(Extension->Parent, 1UL << (Extension->Type - 1));
            Status = STATUS_SUCCESS;
            if (!Extension->Parent || !Extension->Parent->ChildrenReported)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                if (Extension->Parent)
                    Extension->Parent->Children[Extension->Type - 1] = NULL;
                RtlFreeUnicodeString(&Extension->Interface);
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
    ULONG Index;
    NTSTATUS Status;

    if (SMC_IS_CHILD(DeviceObject))
        return SmcChildPnp(DeviceObject, Irp);
    Lower = Device->LowerDevice;
    switch (Stack->MinorFunction)
    {
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, SmcCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Lower, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED)
            {
                FDT_DRIVER_TREE Tree;
                ULONG Gsi = 0;
                BOOLEAN Edge = FALSE;
                NTSTATUS FilterStatus = FdtDriverOpen(Device->Pdo, &Tree);

                if (NT_SUCCESS(FilterStatus))
                {
                    FilterStatus = AppleRtkitQueryInterrupt(&Tree, Tree.Node, &Gsi, &Edge);
                    FdtDriverClose(&Tree);
                }
                if (NT_SUCCESS(FilterStatus))
                    FilterStatus = AppleAddInterruptRequirement(Irp, Gsi, Edge);
                DbgPrint("applesmc: requesting the mailbox interrupt %lu: %lx\n", Gsi, FilterStatus);
                if (NT_SUCCESS(FilterStatus))
                    Status = STATUS_SUCCESS;
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

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
                Status = SmcStartHardware(Device, Stack->Parameters.StartDevice.AllocatedResources,
                                          Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
                if (!NT_SUCCESS(Status))
                    SmcStopHardware(Device);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            if (NT_SUCCESS(Status))
                SmcCreateChildren(Device);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
                PDEVICE_RELATIONS Relations;
                ULONG Count = Previous ? Previous->Count : 0;

                Relations = ExAllocatePoolWithTag(PagedPool,
                                                  FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + SMC_CHILDREN + 1]),
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
                for (Index = 0; Index < SMC_CHILDREN && Device->ChildrenReported; ++Index)
                {
                    if (!Device->Children[Index])
                        continue;
                    Relations->Objects[Relations->Count++] = Device->Children[Index];
                    ObReferenceObject(Device->Children[Index]);
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
            Device->ChildrenReported = FALSE;
            SmcFinishButtonWaits(Device, (1UL << SMC_CHILDREN) - 1);
            for (Index = 0; Index < SMC_CHILDREN; ++Index)
            {
                PSMC_CHILD Child;

                if (!Device->Children[Index])
                    continue;
                Child = Device->Children[Index]->DeviceExtension;
                Child->Parent = NULL;
                RtlFreeUnicodeString(&Child->Interface);
                IoDeleteDevice(Device->Children[Index]);
                Device->Children[Index] = NULL;
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
    KeInitializeSpinLock(&Device->ButtonLock);
    Device->LidReported = -1;
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
