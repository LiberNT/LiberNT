#include "precomp.h"

#include <poclass.h>

#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
DRIVER_INITIALIZE DriverEntry;

CODE_SEG("PAGE")
DRIVER_ADD_DEVICE Bus_AddDevice;

extern struct acpi_device *sleep_button;
extern struct acpi_device *power_button;

static KGUARDED_MUTEX BuspResourceLock;

ACPI_STATUS
BuspGetCurrentResources(
    _In_ ACPI_HANDLE Handle,
    _Inout_ ACPI_BUFFER *Buffer)
{
    ACPI_STATUS Status;

    KeAcquireGuardedMutex(&BuspResourceLock);
    Status = AcpiGetCurrentResources(Handle, Buffer);
    KeReleaseGuardedMutex(&BuspResourceLock);
    return Status;
}

ACPI_STATUS
BuspGetPossibleResources(
    _In_ ACPI_HANDLE Handle,
    _Inout_ ACPI_BUFFER *Buffer)
{
    ACPI_STATUS Status;

    KeAcquireGuardedMutex(&BuspResourceLock);
    Status = AcpiGetPossibleResources(Handle, Buffer);
    KeReleaseGuardedMutex(&BuspResourceLock);
    return Status;
}

ACPI_STATUS
BuspSetCurrentResources(
    _In_ ACPI_HANDLE Handle,
    _In_ ACPI_BUFFER *Buffer)
{
    ACPI_STATUS Status;

    KeAcquireGuardedMutex(&BuspResourceLock);
    Status = AcpiSetCurrentResources(Handle, Buffer);
    KeReleaseGuardedMutex(&BuspResourceLock);
    return Status;
}

ACPI_STATUS
BuspGetIrqRoutingTable(
    _In_ ACPI_HANDLE Handle,
    _Inout_ ACPI_BUFFER *Buffer)
{
    ACPI_STATUS Status;

    KeAcquireGuardedMutex(&BuspResourceLock);
    Status = AcpiGetIrqRoutingTable(Handle, Buffer);
    KeReleaseGuardedMutex(&BuspResourceLock);
    return Status;
}

ACPI_STATUS
BuspWalkResources(
    _In_ ACPI_HANDLE Handle,
    _In_ char *Name,
    _In_ ACPI_WALK_RESOURCE_CALLBACK Callback,
    _In_opt_ void *Context)
{
    ACPI_STATUS Status;

    KeAcquireGuardedMutex(&BuspResourceLock);
    Status = AcpiWalkResources(Handle, Name, Callback, Context);
    KeReleaseGuardedMutex(&BuspResourceLock);
    return Status;
}

static
NTSTATUS
AcpiGetSystemTableIoctl(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpStack)
{
    PACPI_GET_SYSTEM_TABLE_INPUT Input;
    ACPI_TABLE_HEADER *Table;
    ACPI_STATUS AcpiStatus;
    CHAR Signature[5];
    ULONG InputLength;
    ULONG OutputLength;
    ULONG TableLength;

    InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    if (InputLength < sizeof(*Input))
        return STATUS_INVALID_PARAMETER;
    Input = Irp->AssociatedIrp.SystemBuffer;
    if (!Input || Input->Instance == 0)
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(Signature, Input->Signature, sizeof(Input->Signature));
    Signature[4] = ANSI_NULL;
    AcpiStatus = AcpiGetTable(Signature, Input->Instance, &Table);
    if (AcpiStatus == AE_NOT_FOUND)
        return STATUS_NOT_FOUND;
    if (ACPI_FAILURE(AcpiStatus) || !Table)
        return STATUS_UNSUCCESSFUL;
    TableLength = Table->Length;
    if (TableLength < sizeof(*Table))
    {
        AcpiPutTable(Table);
        return STATUS_ACPI_INVALID_DATA;
    }
    if (OutputLength < TableLength)
    {
        if (OutputLength < sizeof(*Table))
        {
            AcpiPutTable(Table);
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Table, sizeof(*Table));
        Irp->IoStatus.Information = sizeof(*Table);
        AcpiPutTable(Table);
        return STATUS_BUFFER_OVERFLOW;
    }
    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Table, TableLength);
    Irp->IoStatus.Information = TableLength;
    AcpiPutTable(Table);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiEnumSystemTablesIoctl(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpStack)
{
    PACPI_ENUM_SYSTEM_TABLES_ENTRY Entries;
    ACPI_TABLE_HEADER *Table;
    ACPI_STATUS AcpiStatus;
    ULONG OutputLength;
    ULONG Count = 0;
    ULONG Index;
    ULONG Capacity;

    OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    Entries = Irp->AssociatedIrp.SystemBuffer;
    Capacity = OutputLength / sizeof(*Entries);

    /*
     * Walk the loaded table list by index.  A signature is not unique, so the
     * instance number of each repeat is reported alongside it; that pair is
     * what IOCTL_ACPI_GET_SYSTEM_TABLE takes back.
     */
    for (Index = 0; ; Index++)
    {
        ULONG Instance = 1;
        ULONG Earlier;

        AcpiStatus = AcpiGetTableByIndex(Index, &Table);
        if (AcpiStatus == AE_BAD_PARAMETER || AcpiStatus == AE_NOT_FOUND)
            break;
        if (ACPI_FAILURE(AcpiStatus) || !Table)
            break;

        if (Count < Capacity)
        {
            for (Earlier = 0; Earlier < Count; Earlier++)
            {
                if (RtlCompareMemory(Entries[Earlier].Signature,
                                     Table->Signature,
                                     sizeof(Entries[Earlier].Signature)) ==
                    sizeof(Entries[Earlier].Signature))
                {
                    Instance++;
                }
            }

            RtlCopyMemory(Entries[Count].Signature,
                          Table->Signature,
                          sizeof(Entries[Count].Signature));
            Entries[Count].Instance = Instance;
        }

        AcpiPutTable(Table);
        Count++;
    }

    if (Count > Capacity)
    {
        Irp->IoStatus.Information = Capacity * sizeof(*Entries);
        return STATUS_BUFFER_OVERFLOW;
    }

    Irp->IoStatus.Information = Count * sizeof(*Entries);
    return STATUS_SUCCESS;
}



CODE_SEG("PAGE")
NTSTATUS
NTAPI
Bus_AddDevice(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT PhysicalDeviceObject
    )

{
    NTSTATUS            status;
    PDEVICE_OBJECT      deviceObject = NULL;
    PFDO_DEVICE_DATA    deviceData = NULL;
#ifndef NDEBUG
    PWCHAR              deviceName = NULL;
    ULONG               nameLength;
#endif

    PAGED_CODE ();

    DPRINT("Add Device: 0x%p\n",  PhysicalDeviceObject);

    DPRINT("#################### Bus_CreateClose Creating FDO Device ####################\n");
    status = IoCreateDevice(DriverObject,
                      sizeof(FDO_DEVICE_DATA),
                      NULL,
                      FILE_DEVICE_ACPI,
                      FILE_DEVICE_SECURE_OPEN,
                      FALSE,
                      &deviceObject);
    if (!NT_SUCCESS(status))
    {
        DPRINT1("IoCreateDevice() failed with status 0x%X\n", status);
        goto End;
    }

    deviceData = (PFDO_DEVICE_DATA) deviceObject->DeviceExtension;
    RtlZeroMemory (deviceData, sizeof (FDO_DEVICE_DATA));

    //
    // Set the initial state of the FDO
    //

    INITIALIZE_PNP_STATE(deviceData->Common);

    deviceData->Common.IsFDO = TRUE;

    deviceData->Common.Self = deviceObject;

    ExInitializeFastMutex (&deviceData->Mutex);

    InitializeListHead (&deviceData->ListOfPDOs);

    // Set the PDO for use with PlugPlay functions

    deviceData->UnderlyingPDO = PhysicalDeviceObject;

    //
    // Set the initial powerstate of the FDO
    //

    deviceData->Common.DevicePowerState = PowerDeviceUnspecified;
    deviceData->Common.SystemPowerState = PowerSystemWorking;

    deviceObject->Flags |= DO_POWER_PAGABLE;

    //
    // Attach our FDO to the device stack.
    // The return value of IoAttachDeviceToDeviceStack is the top of the
    // attachment chain.  This is where all the IRPs should be routed.
    //

    deviceData->NextLowerDriver = IoAttachDeviceToDeviceStack (
                                    deviceObject,
                                    PhysicalDeviceObject);

    if (NULL == deviceData->NextLowerDriver) {

        status = STATUS_NO_SUCH_DEVICE;
        goto End;
    }


#ifndef NDEBUG
    //
    // We will demonstrate here the step to retrieve the name of the PDO
    //

    status = IoGetDeviceProperty (PhysicalDeviceObject,
                                  DevicePropertyPhysicalDeviceObjectName,
                                  0,
                                  NULL,
                                  &nameLength);

    if (status != STATUS_BUFFER_TOO_SMALL)
    {
        DPRINT1("AddDevice:IoGDP failed (0x%x)\n", status);
        goto End;
    }

    deviceName = ExAllocatePoolWithTag(NonPagedPool, nameLength, 'MpcA');

    if (NULL == deviceName) {
        DPRINT1("AddDevice: no memory to alloc for deviceName(0x%x)\n", nameLength);
        status =  STATUS_INSUFFICIENT_RESOURCES;
        goto End;
    }

    status = IoGetDeviceProperty (PhysicalDeviceObject,
                         DevicePropertyPhysicalDeviceObjectName,
                         nameLength,
                         deviceName,
                         &nameLength);

    if (!NT_SUCCESS (status)) {

        DPRINT1("AddDevice:IoGDP(2) failed (0x%x)", status);
        goto End;
    }

    DPRINT("AddDevice: %p to %p->%p (%ws) \n",
                   deviceObject,
                   deviceData->NextLowerDriver,
                   PhysicalDeviceObject,
                   deviceName);

#endif

    //
    // We are done with initializing, so let's indicate that and return.
    // This should be the final step in the AddDevice process.
    //
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

End:
#ifndef NDEBUG
    if (deviceName){
        ExFreePoolWithTag(deviceName, 'MpcA');
    }
#endif
    if (!NT_SUCCESS(status) && deviceObject){
        if (deviceData && deviceData->NextLowerDriver){
            IoDetachDevice (deviceData->NextLowerDriver);
        }
        IoDeleteDevice (deviceObject);
    }
    return status;

}

NTSTATUS
NTAPI
ACPIDispatchCreateClose(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP Irp)
{
   Irp->IoStatus.Status = STATUS_SUCCESS;
   Irp->IoStatus.Information = 0;

   IoCompleteRequest(Irp, IO_NO_INCREMENT);

   return STATUS_SUCCESS;
}

/* What one button PDO reports, as SYS_BUTTON_* bits. */
static
ULONG
AcpiButtonCapsForPdo(
    _In_ PPDO_DEVICE_DATA PdoData)
{
    ULONG Caps = 0;

    if (wcsstr(PdoData->HardwareIDs, L"PNP0C0D"))
    {
        Caps |= SYS_BUTTON_LID;
    }
    else if (PdoData->AcpiHandle == NULL)
    {
        /* We have to return both at the same time because since we
         * have a NULL handle we are the fixed feature DO and we will
         * only be called once (not once per device)
         */
        if (power_button)
            Caps |= SYS_BUTTON_POWER;
        if (sleep_button)
            Caps |= SYS_BUTTON_SLEEP;
    }
    else if (wcsstr(PdoData->HardwareIDs, L"PNP0C0C"))
    {
        Caps |= SYS_BUTTON_POWER;
    }
    else if (wcsstr(PdoData->HardwareIDs, L"PNP0C0E"))
    {
        Caps |= SYS_BUTTON_SLEEP;
    }
    return Caps;
}

/* The SYS_BUTTON_* bit a button event stands for, or 0. */
static
ULONG
AcpiButtonEventToSysButton(
    _In_ const struct acpi_bus_event *Event)
{
    if (Event->type != ACPI_BUTTON_NOTIFY_STATUS)
        return 0;
    if (!strcmp(Event->device_class, ACPI_BUTTON_CLASS "/" ACPI_BUTTON_SUBCLASS_POWER))
        return SYS_BUTTON_POWER;
    if (!strcmp(Event->device_class, ACPI_BUTTON_CLASS "/" ACPI_BUTTON_SUBCLASS_SLEEP))
        return SYS_BUTTON_SLEEP;
    if (!strcmp(Event->device_class, ACPI_BUTTON_CLASS "/" ACPI_BUTTON_SUBCLASS_LID))
        return SYS_BUTTON_LID;
    return 0;
}

/* Event filter: Context is the SYS_BUTTON_* bits the receiver serves. */
static
BOOLEAN
AcpiButtonEventFilter(
    _In_ const struct acpi_bus_event *Event,
    _In_ void *Context)
{
    return (AcpiButtonEventToSysButton(Event) & (ULONG)(ULONG_PTR)Context) != 0;
}

/*
 * Lid state last handed to the power manager: -1 before the first report,
 * else 1 open / 0 closed.  The power manager keeps one request pending on
 * the lid at a time.
 */
static volatile LONG AcpiLidReportedState = -1;

static
NTSTATUS
AcpiLidQueryOpen(
    _In_ ACPI_HANDLE Handle,
    _Out_ PULONG Open)
{
    unsigned long long Value;

    if (Handle == NULL ||
        ACPI_FAILURE(acpi_evaluate_integer(Handle, "_LID", NULL, &Value)))
    {
        return STATUS_UNSUCCESSFUL;
    }
    *Open = (Value != 0) ? 1 : 0;
    return STATUS_SUCCESS;
}

/*
 * Completes a lid request with the lid's state once it differs from what
 * was last reported: at once for the first request (the initial state) or
 * when the lid moved while no request was pending, otherwise on the next
 * Notify() that changes _LID.  Listening starts before _LID is read, so a
 * change between the two is not lost.
 */
static
NTSTATUS
AcpiLidWaitForState(
    _In_ ACPI_HANDLE Handle,
    _Out_ PULONG ButtonEvent)
{
    struct acpi_bus_event_waiter Waiter;
    struct acpi_bus_event Event;
    NTSTATUS Status;
    ULONG Open;
    LONG Reported;

    acpi_bus_event_listen(&Waiter, AcpiButtonEventFilter, (PVOID)(ULONG_PTR)SYS_BUTTON_LID);
    for (;;)
    {
        Status = AcpiLidQueryOpen(Handle, &Open);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Lid state unreadable (_LID failed)\n");
            break;
        }

        Reported = InterlockedExchange(&AcpiLidReportedState, (LONG)Open);
        if (Reported != (LONG)Open)
        {
            *ButtonEvent = SYS_BUTTON_LID |
                           (Open ? SYS_BUTTON_LID_OPEN : SYS_BUTTON_LID_CLOSED) |
                           (Reported < 0 ? SYS_BUTTON_LID_INITIAL : SYS_BUTTON_LID_CHANGED);
            break;
        }

        if (!ACPI_SUCCESS(acpi_bus_event_wait(&Waiter, &Event)))
        {
            Status = STATUS_UNSUCCESSFUL;
            break;
        }
    }
    acpi_bus_event_unlisten(&Waiter);
    return Status;
}

typedef struct _ACPI_BUTTON_WAIT
{
    PIRP Irp;
    ACPI_HANDLE Handle;
    ULONG Buttons;
} ACPI_BUTTON_WAIT, *PACPI_BUTTON_WAIT;

/*
 * Completes one IOCTL_GET_SYS_BUTTON_EVENT with the next event of the
 * buttons its device serves.  Each button device has its own request, so
 * an event is delivered on the device it belongs to.
 */
VOID
NTAPI
ButtonWaitThread(PVOID Context)
{
    ACPI_BUTTON_WAIT Wait = *(PACPI_BUTTON_WAIT)Context;
    PIRP Irp = Wait.Irp;
    struct acpi_bus_event event;
    ULONG ButtonEvent = 0;
    NTSTATUS Status;

    ExFreePoolWithTag(Context, 'IPCA');

    if (Wait.Buttons & SYS_BUTTON_LID)
    {
        Status = AcpiLidWaitForState(Wait.Handle, &ButtonEvent);
    }
    else if (ACPI_SUCCESS(acpi_bus_receive_event_filtered(AcpiButtonEventFilter,
                                                          (PVOID)(ULONG_PTR)Wait.Buttons,
                                                          &event)))
    {
        ButtonEvent = AcpiButtonEventToSysButton(&event);
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    if (NT_SUCCESS(Status))
    {
       RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &ButtonEvent, sizeof(ButtonEvent));
       Irp->IoStatus.Information = sizeof(ULONG);
    }
    Irp->IoStatus.Status = Status;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/*
 * Internal device control.  Only the PCI notify interface is served here, on
 * the bus FDO that pci.sys reaches through the device interface; anything
 * else completes as it did before this routine existed.
 */
static
NTSTATUS
NTAPI
ACPIDispatchInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PCOMMON_DEVICE_DATA CommonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    Irp->IoStatus.Information = 0;
    if (CommonData != NULL && CommonData->IsFDO &&
        (IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_ACPI_QUERY_PCI_NOTIFY_INTERFACE ||
         IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_ACPI_QUERY_PCI_SLOT_NOTIFY_INTERFACE))
    {
        Status = AcpiQueryPciNotifyInterfaceIoctl(Irp, IrpSp);
    }
    else if (CommonData != NULL && CommonData->IsFDO &&
             IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_ACPI_QUERY_PCI_CHILD_NOTIFY_INTERFACE)
    {
        Status = AcpiQueryPciChildNotifyInterfaceIoctl(Irp, IrpSp);
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}


NTSTATUS
NTAPI
ACPIDispatchDeviceControl(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP Irp)
{
    PIO_STACK_LOCATION      irpStack;
    NTSTATUS                status = STATUS_NOT_SUPPORTED;
    PCOMMON_DEVICE_DATA     commonData;
    ULONG Caps = 0;
    HANDLE ThreadHandle;

    irpStack = IoGetCurrentIrpStackLocation (Irp);
    ASSERT (IRP_MJ_DEVICE_CONTROL == irpStack->MajorFunction);

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;

    Irp->IoStatus.Information = 0;

    /*
     * Handle FDO IOCTLs - these are for the device interface
     * that allows the PCI driver to evaluate ACPI methods.
     */
    if (commonData->IsFDO)
    {
        PFDO_DEVICE_DATA fdoData = (PFDO_DEVICE_DATA)commonData;

        switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_ACPI_GET_SYSTEM_TABLE:
                status = AcpiGetSystemTableIoctl(Irp, irpStack);
                break;

            case IOCTL_ACPI_ENUM_SYSTEM_TABLES:
                status = AcpiEnumSystemTablesIoctl(Irp, irpStack);
                break;

            case IOCTL_ACPI_EVAL_METHOD_FOR_PCI:
            {
                /*
                 * This IOCTL is sent by the PCI driver via the device interface.
                 * It includes the PCI device location (Segment:Bus:Device:Function)
                 * and the ACPI method to evaluate.
                 */
                status = AcpiEvalMethodForPciDeviceIoctl(fdoData, Irp);
                break;
            }

            case IOCTL_ACPI_EVAL_METHOD_FOR_PCI_CHILD:
                status = AcpiEvalMethodForPciChildIoctl(fdoData, Irp);
                break;

            case IOCTL_ACPI_ENUM_CHILDREN_FOR_PCI:
                status = AcpiEnumChildrenForPciDeviceIoctl(fdoData, Irp);
                break;

            case IOCTL_ACPI_SET_POWER_FOR_PCI:
                status = AcpiSetPowerForPciDeviceIoctl(fdoData, Irp);
                break;

            case IOCTL_ACPI_QUERY_WAKE_FOR_PCI:
                status = AcpiQueryWakeForPciDeviceIoctl(Irp);
                break;

            case IOCTL_ACPI_QUERY_D3COLD_FOR_PCI:
                status = AcpiQueryD3ColdForPciDeviceIoctl(Irp);
                break;

            case IOCTL_ACPI_WAIT_WAKE_FOR_PCI:
                status = AcpiWaitWakeForPciDeviceIoctl(Irp);
                break;

            default:
                DPRINT("ACPI FDO: Unknown IOCTL 0x%X\n",
                       irpStack->Parameters.DeviceIoControl.IoControlCode);
                status = STATUS_NOT_SUPPORTED;
                break;
        }

        if (status != STATUS_PENDING)
        {
            Irp->IoStatus.Status = status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
        }

        return status;
    }

    /* Handle PDO IOCTLs */
    switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_ACPI_ASYNC_EVAL_METHOD:
        {
            ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

            status = Bus_PDO_EvalMethod((PPDO_DEVICE_DATA)commonData, Irp);
            break;
        }

        case IOCTL_ACPI_EVAL_METHOD:
        {
            ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

            status = Bus_PDO_EvalMethod((PPDO_DEVICE_DATA)commonData, Irp);
            break;
        }

        case IOCTL_ACPI_EVAL_METHOD_FOR_PCI_CHILD:
        {
            ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

            status = AcpiEvalMethodForDisplayChildIoctl(
                         (PPDO_DEVICE_DATA)commonData,
                         Irp);
            break;
        }

        case IOCTL_ACPI_ASYNC_EVAL_METHOD_EX:
        case IOCTL_ACPI_EVAL_METHOD_EX:
        {
            ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

            status = Bus_PDO_EvalMethodEx((PPDO_DEVICE_DATA)commonData, Irp);
            break;
        }

        case IOCTL_ACPI_ENUM_CHILDREN:
        {
            ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

            status = Bus_PDO_EnumChildren((PPDO_DEVICE_DATA)commonData, Irp);
            break;
        }

        case IOCTL_GET_SYS_BUTTON_CAPS:
            if (Irp->RequestorMode != KernelMode)
            {
                Irp->IoStatus.Information = 0;
                status = STATUS_SUCCESS;
                break;
            }
            if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Caps = AcpiButtonCapsForPdo((PPDO_DEVICE_DATA)commonData);
            if (Caps & SYS_BUTTON_LID)
                DPRINT1("Lid button reported to power manager\n");
            if (Caps != 0)
            {
                DPRINT("Button caps 0x%lx reported to power manager\n", Caps);
                RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Caps, sizeof(Caps));
                Irp->IoStatus.Information = sizeof(Caps);
                status = STATUS_SUCCESS;
            }
            else
            {
                DPRINT1("IOCTL_GET_SYS_BUTTON_CAPS sent to a non-button device\n");
                status = STATUS_INVALID_PARAMETER;
            }
            break;

        case IOCTL_GET_SYS_BUTTON_EVENT:
            {
                PACPI_BUTTON_WAIT Wait;
                NTSTATUS ThreadStatus;

                if (Irp->RequestorMode != KernelMode)
                {
                    Irp->IoStatus.Information = 0;
                    status = STATUS_SUCCESS;
                    break;
                }
                if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
                {
                    status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                Caps = AcpiButtonCapsForPdo((PPDO_DEVICE_DATA)commonData);
                if (Caps == 0)
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                Wait = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Wait), 'IPCA');
                if (!Wait)
                {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Wait->Irp = Irp;
                Wait->Handle = ((PPDO_DEVICE_DATA)commonData)->AcpiHandle;
                Wait->Buttons = Caps;

                /* The thread may complete the request at once (the lid's
                 * initial state), so it is marked pending first and not
                 * touched again here. */
                IoMarkIrpPending(Irp);
                ThreadStatus = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, 0, 0, 0, ButtonWaitThread, Wait);
                if (NT_SUCCESS(ThreadStatus))
                {
                    ZwClose(ThreadHandle);
                }
                else
                {
                    DPRINT1("Failed to create system thread: 0x%lx\n", ThreadStatus);
                    ExFreePoolWithTag(Wait, 'IPCA');
                    Irp->IoStatus.Status = ThreadStatus;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                }
                return STATUS_PENDING;
            }

        case IOCTL_BATTERY_QUERY_TAG:
            DPRINT("IOCTL_BATTERY_QUERY_TAG is not supported!\n");
            break;

        case IOCTL_THERMAL_QUERY_INFORMATION:
        case IOCTL_THERMAL_SET_COOLING_POLICY:
        case IOCTL_RUN_ACTIVE_COOLING_METHOD:
        case IOCTL_THERMAL_SET_PASSIVE_LIMIT:
            status = AcpiThermalDeviceControl((PPDO_DEVICE_DATA)commonData, Irp);
            break;

        default:
            DPRINT1("Unsupported IOCTL: %x\n", irpStack->Parameters.DeviceIoControl.IoControlCode);
            break;
    }

    if (status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return status;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry (
    PDRIVER_OBJECT  DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS Status;
    DPRINT("Driver Entry \n");

    KeInitializeGuardedMutex(&BuspResourceLock);
    AcpiWakeInitialize();

    Status = AcpiProcessorIdsInitialize();
    if (!NT_SUCCESS(Status))
    {
        /*
         * ACPI can function without processor information - this is not fatal.
         * On ARM64, the CentralProcessor registry key may not exist yet as it's
         * architecture-specific. We'll continue loading ACPI with default values.
         */
        DPRINT1("Failed to get processor information (0x%lx) - continuing with defaults\n", Status);
        Status = STATUS_SUCCESS;
    }

    //
    // Set entry points into the driver
    //
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ACPIDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = ACPIDispatchInternalDeviceControl;
    DriverObject->MajorFunction [IRP_MJ_PNP] = Bus_PnP;
    DriverObject->MajorFunction [IRP_MJ_POWER] = Bus_Power;
    DriverObject->MajorFunction [IRP_MJ_CREATE] = ACPIDispatchCreateClose;
    DriverObject->MajorFunction [IRP_MJ_CLOSE] = ACPIDispatchCreateClose;

    DriverObject->DriverExtension->AddDevice = Bus_AddDevice;

    return STATUS_SUCCESS;
}
