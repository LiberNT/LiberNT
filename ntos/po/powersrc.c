/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The power source, as the system batteries report it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <batclass.h>
#define NDEBUG
#include <debug.h>

#define POP_SOURCE_TAG              'sSoP'
#define POP_SOURCE_WAIT_FOREVER     MAXULONG

typedef enum _POP_SOURCE_WAIT
{
    PopSourceWaitTag,
    PopSourceWaitStatus
} POP_SOURCE_WAIT;

typedef struct _POP_SOURCE_BATTERY
{
    LIST_ENTRY Link;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    WORK_QUEUE_ITEM WorkItem;
    POP_SOURCE_WAIT Wait;
    NTSTATUS Status;
    ULONG_PTR Information;
    ULONG Tag;
    ULONG PowerState;
    BOOLEAN Known;
    union
    {
        ULONG Timeout;
        ULONG Tag;
        BATTERY_WAIT_STATUS WaitStatus;
        BATTERY_STATUS Status;
    } Buffer;
} POP_SOURCE_BATTERY, *PPOP_SOURCE_BATTERY;

static LIST_ENTRY PopSourceBatteries;
static KGUARDED_MUTEX PopSourceLock;
static PVOID PopSourceNotification;
static ULONG PopSourceCurrent = PoAc;

static
VOID
PopSourceListen(
    _Inout_ PPOP_SOURCE_BATTERY Battery);

/* A request built for the current thread: the guarded mutex is not held, so its completion can be delivered */
static
NTSTATUS
PopSourceQuery(
    _In_ PPOP_SOURCE_BATTERY Battery,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Done;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    KeInitializeEvent(&Done, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        Battery->DeviceObject,
                                        Input,
                                        InputLength,
                                        Output,
                                        OutputLength,
                                        FALSE,
                                        &Done,
                                        &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->FileObject = Battery->FileObject;

    Status = IoCallDriver(Battery->DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && (IoStatus.Information < OutputLength))
        Status = STATUS_INVALID_DEVICE_REQUEST;

    return Status;
}

/* No system battery means the machine runs from the mains; one that reports AC power means the same */
static
VOID
PopSourceDecide(VOID)
{
    PPOP_SOURCE_BATTERY Battery;
    PLIST_ENTRY Entry;
    BOOLEAN AnyBattery = FALSE;
    BOOLEAN AcOnLine = FALSE;
    ULONG Source;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopSourceLock);
    for (Entry = PopSourceBatteries.Flink; Entry != &PopSourceBatteries; Entry = Entry->Flink)
    {
        Battery = CONTAINING_RECORD(Entry, POP_SOURCE_BATTERY, Link);
        if (!Battery->Known)
            continue;

        AnyBattery = TRUE;
        AcOnLine |= ((Battery->PowerState & BATTERY_POWER_ON_LINE) != 0);
    }

    Source = (!AnyBattery || AcOnLine) ? PoAc : PoDc;
    if (Source == PopSourceCurrent)
    {
        KeReleaseGuardedMutex(&PopSourceLock);
        return;
    }

    PopSourceCurrent = Source;
    KeReleaseGuardedMutex(&PopSourceLock);

    DPRINT1("The machine now runs on %s power\n", (Source == PoAc) ? "AC" : "battery");
    PopSetPowerSettingValue(&GUID_ACDC_POWER_SOURCE, PoAc, &Source, sizeof(Source));
}

static
VOID
PopSourceForget(
    _In_ _Post_invalid_ PPOP_SOURCE_BATTERY Battery)
{
    PAGED_CODE();

    KeAcquireGuardedMutex(&PopSourceLock);
    RemoveEntryList(&Battery->Link);
    KeReleaseGuardedMutex(&PopSourceLock);

    ObDereferenceObject(Battery->FileObject);
    ExFreePoolWithTag(Battery, POP_SOURCE_TAG);
    PopSourceDecide();
}

/* A battery with a new tag is asked again what it is and what it reports before its changes are awaited */
static
BOOLEAN
PopSourceIdentify(
    _Inout_ PPOP_SOURCE_BATTERY Battery)
{
    BATTERY_QUERY_INFORMATION Query;
    BATTERY_INFORMATION Information;
    BATTERY_WAIT_STATUS Wait;
    BATTERY_STATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&Query, sizeof(Query));
    Query.BatteryTag = Battery->Tag;
    Query.InformationLevel = BatteryInformation;
    if (!NT_SUCCESS(PopSourceQuery(Battery, IOCTL_BATTERY_QUERY_INFORMATION, &Query, sizeof(Query),
                                   &Information, sizeof(Information))) ||
        !(Information.Capabilities & BATTERY_SYSTEM_BATTERY))
    {
        return FALSE;
    }

    RtlZeroMemory(&Wait, sizeof(Wait));
    Wait.BatteryTag = Battery->Tag;
    if (!NT_SUCCESS(PopSourceQuery(Battery, IOCTL_BATTERY_QUERY_STATUS, &Wait, sizeof(Wait),
                                   &Status, sizeof(Status))))
    {
        return FALSE;
    }

    KeAcquireGuardedMutex(&PopSourceLock);
    Battery->PowerState = Status.PowerState;
    Battery->Known = TRUE;
    KeReleaseGuardedMutex(&PopSourceLock);
    return TRUE;
}

static
VOID
NTAPI
PopSourceWorker(
    _In_ PVOID Context)
{
    PPOP_SOURCE_BATTERY Battery = Context;

    PAGED_CODE();

    if (Battery->Wait == PopSourceWaitTag)
    {
        if (!NT_SUCCESS(Battery->Status) || (Battery->Information < sizeof(ULONG)) ||
            (Battery->Buffer.Tag == BATTERY_TAG_INVALID))
        {
            DPRINT1("A battery stopped answering: 0x%08lx\n", Battery->Status);
            PopSourceForget(Battery);
            return;
        }

        Battery->Tag = Battery->Buffer.Tag;
        if (!PopSourceIdentify(Battery))
        {
            PopSourceForget(Battery);
            return;
        }

        Battery->Wait = PopSourceWaitStatus;
    }
    else if (NT_SUCCESS(Battery->Status) && (Battery->Information >= sizeof(BATTERY_STATUS)))
    {
        KeAcquireGuardedMutex(&PopSourceLock);
        Battery->PowerState = Battery->Buffer.Status.PowerState;
        KeReleaseGuardedMutex(&PopSourceLock);
    }
    else if (Battery->Status == STATUS_NO_SUCH_DEVICE)
    {
        /* The tag changed: the battery left its bay, or another took its place */
        KeAcquireGuardedMutex(&PopSourceLock);
        Battery->Known = FALSE;
        KeReleaseGuardedMutex(&PopSourceLock);
        Battery->Wait = PopSourceWaitTag;
    }
    else
    {
        DPRINT1("A battery stopped reporting its status: 0x%08lx\n", Battery->Status);
        PopSourceForget(Battery);
        return;
    }

    PopSourceDecide();
    PopSourceListen(Battery);
}

static IO_COMPLETION_ROUTINE PopSourceComplete;

static
NTSTATUS
NTAPI
PopSourceComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PPOP_SOURCE_BATTERY Battery = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Battery != NULL);

    Battery->Status = Irp->IoStatus.Status;
    Battery->Information = Irp->IoStatus.Information;
    IoFreeIrp(Irp);
    ExQueueWorkItem(&Battery->WorkItem, DelayedWorkQueue);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* The battery class completes a wait once the tag exists, or once the power state differs from the one given */
static
VOID
PopSourceListen(
    _Inout_ PPOP_SOURCE_BATTERY Battery)
{
    PIO_STACK_LOCATION Stack;
    ULONG InputLength, OutputLength, IoControlCode;
    PIRP Irp;

    Irp = IoAllocateIrp(Battery->DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        Battery->Status = STATUS_INSUFFICIENT_RESOURCES;
        Battery->Wait = PopSourceWaitStatus;
        ExQueueWorkItem(&Battery->WorkItem, DelayedWorkQueue);
        return;
    }

    RtlZeroMemory(&Battery->Buffer, sizeof(Battery->Buffer));
    if (Battery->Wait == PopSourceWaitTag)
    {
        IoControlCode = IOCTL_BATTERY_QUERY_TAG;
        Battery->Buffer.Timeout = POP_SOURCE_WAIT_FOREVER;
        InputLength = sizeof(ULONG);
        OutputLength = sizeof(ULONG);
    }
    else
    {
        IoControlCode = IOCTL_BATTERY_QUERY_STATUS;
        Battery->Buffer.WaitStatus.BatteryTag = Battery->Tag;
        Battery->Buffer.WaitStatus.Timeout = POP_SOURCE_WAIT_FOREVER;
        Battery->Buffer.WaitStatus.PowerState = Battery->PowerState;
        Battery->Buffer.WaitStatus.LowCapacity = 0;
        Battery->Buffer.WaitStatus.HighCapacity = BATTERY_UNKNOWN_CAPACITY;
        InputLength = sizeof(BATTERY_WAIT_STATUS);
        OutputLength = sizeof(BATTERY_STATUS);
    }

    Irp->RequestorMode = KernelMode;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Irp->AssociatedIrp.SystemBuffer = &Battery->Buffer;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->FileObject = Battery->FileObject;
    Stack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    Stack->Parameters.DeviceIoControl.InputBufferLength = InputLength;
    Stack->Parameters.DeviceIoControl.OutputBufferLength = OutputLength;

    IoSetCompletionRoutine(Irp, PopSourceComplete, Battery, TRUE, TRUE, TRUE);
    IoCallDriver(Battery->DeviceObject, Irp);
}

static
VOID
PopSourceWatch(
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    PPOP_SOURCE_BATTERY Battery;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoGetDeviceObjectPointer(SymbolicLinkName, FILE_READ_DATA, &FileObject, &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("%wZ not opened: 0x%08lx\n", SymbolicLinkName, Status);
        return;
    }

    Battery = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Battery), POP_SOURCE_TAG);
    if (Battery == NULL)
    {
        ObDereferenceObject(FileObject);
        return;
    }

    Battery->DeviceObject = DeviceObject;
    Battery->FileObject = FileObject;
    Battery->Wait = PopSourceWaitTag;
    ExInitializeWorkItem(&Battery->WorkItem, PopSourceWorker, Battery);

    KeAcquireGuardedMutex(&PopSourceLock);
    InsertTailList(&PopSourceBatteries, &Battery->Link);
    KeReleaseGuardedMutex(&PopSourceLock);

    DPRINT1("Battery watched on %wZ\n", SymbolicLinkName);
    PopSourceListen(Battery);
}

static
NTSTATUS
NTAPI
PopSourceInterfaceChange(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;

    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    /* A battery that goes away fails its pending request, which ends its watch */
    /* TODO: a query to remove a watched battery is not answered by letting its file object go */
    if (IsEqualGUIDAligned(&Notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
        PopSourceWatch(Notification->SymbolicLinkName);

    return STATUS_SUCCESS;
}

VOID
NTAPI
PopInitializePowerSource(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    InitializeListHead(&PopSourceBatteries);
    KeInitializeGuardedMutex(&PopSourceLock);

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            (PVOID)&GUID_DEVICE_BATTERY,
                                            IopRootDriverObject,
                                            PopSourceInterfaceChange,
                                            NULL,
                                            &PopSourceNotification);
    if (!NT_SUCCESS(Status))
        DPRINT1("Batteries are not watched: 0x%08lx\n", Status);
}
