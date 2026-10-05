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
    Irp->IoStatus.Information = TableLength;
    if (OutputLength < TableLength)
    {
        AcpiPutTable(Table);
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Table, TableLength);
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

    Irp->IoStatus.Information = Count * sizeof(*Entries);
    if (Count > Capacity)
        return STATUS_BUFFER_TOO_SMALL;

    return STATUS_SUCCESS;
}

UNICODE_STRING ProcessorHardwareIds = {0, 0, NULL};
LPWSTR ProcessorIdString = NULL;
LPWSTR ProcessorNameString = NULL;


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
        IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_ACPI_QUERY_PCI_NOTIFY_INTERFACE)
    {
        Status = AcpiQueryPciNotifyInterfaceIoctl(Irp, IrpSp);
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
    BOOLEAN IsThermalQuery;

    irpStack = IoGetCurrentIrpStackLocation (Irp);
    ASSERT (IRP_MJ_DEVICE_CONTROL == irpStack->MajorFunction);
    IsThermalQuery = irpStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_THERMAL_QUERY_INFORMATION;

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

            case IOCTL_ACPI_SET_POWER_FOR_PCI:
                status = AcpiSetPowerForPciDeviceIoctl(fdoData, Irp);
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
        else
        {
            IoMarkIrpPending(Irp);
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
    else
    {
        if (!IsThermalQuery)
            IoMarkIrpPending(Irp);
    }

    return status;
}

static
CODE_SEG("INIT")
NTSTATUS
AcpiRegOpenKey(IN HANDLE ParentKeyHandle,
               IN LPCWSTR KeyName,
               IN ACCESS_MASK DesiredAccess,
               OUT HANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, KeyName);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               ParentKeyHandle,
                               NULL);

    return ZwOpenKey(KeyHandle,
                     DesiredAccess,
                     &ObjectAttributes);
}

static
CODE_SEG("INIT")
NTSTATUS
AcpiRegQueryValue(IN HANDLE KeyHandle,
                  IN LPWSTR ValueName,
                  OUT PULONG Type OPTIONAL,
                  OUT PVOID Data OPTIONAL,
                  IN OUT PULONG DataLength OPTIONAL)
{
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo;
    UNICODE_STRING Name;
    ULONG BufferLength = 0;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, ValueName);

    if (DataLength != NULL)
        BufferLength = *DataLength;

    /* Check if the caller provided a valid buffer */
    if ((Data != NULL) && (BufferLength != 0))
    {
        BufferLength += FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);

        /* Allocate memory for the value */
        ValueInfo = ExAllocatePoolWithTag(PagedPool, BufferLength, 'MpcA');
        if (ValueInfo == NULL)
            return STATUS_NO_MEMORY;
    }
    else
    {
        /* Caller didn't provide a valid buffer, assume he wants the size only */
        ValueInfo = NULL;
        BufferLength = 0;
    }

    /* Query the value */
    Status = ZwQueryValueKey(KeyHandle,
                             &Name,
                             KeyValuePartialInformation,
                             ValueInfo,
                             BufferLength,
                             &BufferLength);

    if (DataLength != NULL)
        *DataLength = BufferLength;

    /* Check if we have the size only */
    if (ValueInfo == NULL)
    {
        /* Check for unexpected status */
        if ((Status != STATUS_BUFFER_OVERFLOW) &&
            (Status != STATUS_BUFFER_TOO_SMALL))
        {
            return Status;
        }

        /* All is well */
        Status = STATUS_SUCCESS;
    }
    /* Otherwise the caller wanted data back, check if we got it */
    else if (NT_SUCCESS(Status))
    {
        if (Type != NULL)
            *Type = ValueInfo->Type;

        /* Copy it */
        RtlMoveMemory(Data, ValueInfo->Data, ValueInfo->DataLength);

        /* if the type is REG_SZ and data is not 0-terminated
         * and there is enough space in the buffer NT appends a \0 */
        if (((ValueInfo->Type == REG_SZ) ||
             (ValueInfo->Type == REG_EXPAND_SZ) ||
             (ValueInfo->Type == REG_MULTI_SZ)) &&
            (ValueInfo->DataLength <= *DataLength - sizeof(WCHAR)))
        {
            WCHAR *ptr = (WCHAR *)((ULONG_PTR)Data + ValueInfo->DataLength);
            if ((ptr > (WCHAR *)Data) && ptr[-1])
                *ptr = 0;
        }
    }

    /* Free the memory and return status */
    if (ValueInfo != NULL)
    {
        ExFreePoolWithTag(ValueInfo, 'MpcA');
    }

    return Status;
}

static
CODE_SEG("INIT")
NTSTATUS
GetProcessorInformation(VOID)
{
    LPWSTR ProcessorIdentifier = NULL;
    LPWSTR ProcessorVendorIdentifier = NULL;
    LPWSTR HardwareIdsBuffer = NULL;
    HANDLE ProcessorHandle = NULL;
    ULONG Length = 0, Level1Length = 0, Level2Length = 0, Level3Length = 0;
    SIZE_T HardwareIdsLength = 0;
    SIZE_T VendorIdentifierLength;
    ULONG i;
    PWCHAR Ptr;
    NTSTATUS Status;

    DPRINT("GetProcessorInformation()\n");

    /* Open the key for CPU 0 */
    Status = AcpiRegOpenKey(NULL,
                            L"\\Registry\\Machine\\Hardware\\Description\\System\\CentralProcessor\\0",
                            KEY_READ,
                            &ProcessorHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open CentralProcessor registry key: 0x%lx\n", Status);
        goto done;
    }

    /* Query the processor identifier length */
    Status = AcpiRegQueryValue(ProcessorHandle,
                               L"Identifier",
                               NULL,
                               NULL,
                               &Length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query Identifier value: 0x%lx\n", Status);
        goto done;
    }

    /* Remember the length as fallback for level 1-3 length */
    Level1Length = Level2Length = Level3Length = Length;

    /* Allocate a buffer large enough to be zero terminated */
    Length += sizeof(UNICODE_NULL);
    ProcessorIdentifier = ExAllocatePoolWithTag(PagedPool, Length, 'IpcA');
    if (ProcessorIdentifier == NULL)
    {
        DPRINT1("Failed to allocate 0x%lx bytes\n", Length);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    /* Query the processor identifier string */
    Status = AcpiRegQueryValue(ProcessorHandle,
                               L"Identifier",
                               NULL,
                               ProcessorIdentifier,
                               &Length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query Identifier value: 0x%lx\n", Status);
        goto done;
    }

    /* Query the processor name length */
    Length = 0;
    Status = AcpiRegQueryValue(ProcessorHandle,
                               L"ProcessorNameString",
                               NULL,
                               NULL,
                               &Length);
    if (NT_SUCCESS(Status))
    {
        /* Allocate a buffer large enough to be zero terminated */
        Length += sizeof(UNICODE_NULL);
        ProcessorNameString = ExAllocatePoolWithTag(PagedPool, Length, 'IpcA');
        if (ProcessorNameString == NULL)
        {
            DPRINT1("Failed to allocate 0x%lx bytes\n", Length);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }

        /* Query the processor name string */
        Status = AcpiRegQueryValue(ProcessorHandle,
                                   L"ProcessorNameString",
                                   NULL,
                                   ProcessorNameString,
                                   &Length);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to query ProcessorNameString value: 0x%lx\n", Status);
            goto done;
        }
    }

    /* Query the vendor identifier length */
    Length = 0;
    Status = AcpiRegQueryValue(ProcessorHandle,
                               L"VendorIdentifier",
                               NULL,
                               NULL,
                               &Length);
    if (!NT_SUCCESS(Status) || (Length == 0))
    {
        DPRINT1("Failed to query VendorIdentifier value: 0x%lx\n", Status);
        goto done;
    }

    /* Allocate a buffer large enough to be zero terminated */
    Length += sizeof(UNICODE_NULL);
    ProcessorVendorIdentifier = ExAllocatePoolWithTag(PagedPool, Length, 'IpcA');
    if (ProcessorVendorIdentifier == NULL)
    {
        DPRINT1("Failed to allocate 0x%lx bytes\n", Length);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    /* Query the vendor identifier string */
    Status = AcpiRegQueryValue(ProcessorHandle,
                               L"VendorIdentifier",
                               NULL,
                               ProcessorVendorIdentifier,
                               &Length);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query VendorIdentifier value: 0x%lx\n", Status);
        goto done;
    }

    Level1Length = Level2Length = Level3Length = (ULONG)wcslen(ProcessorIdentifier);
    for (i = 0; i < wcslen(ProcessorIdentifier); i++)
    {
        if (ProcessorIdentifier[i] == L' ')
            ProcessorIdentifier[i] = L'_';
        else
            ProcessorIdentifier[i] = RtlUpcaseUnicodeChar(ProcessorIdentifier[i]);
    }

    Ptr = wcsstr(ProcessorIdentifier, L"Stepping");
    if (Ptr != NULL)
    {
        Ptr--;
        Level1Length = (ULONG)(Ptr - ProcessorIdentifier);
    }

    Ptr = wcsstr(ProcessorIdentifier, L"Model");
    if (Ptr != NULL)
    {
        Ptr--;
        Level2Length = (ULONG)(Ptr - ProcessorIdentifier);
    }

    Ptr = wcsstr(ProcessorIdentifier, L"Family");
    if (Ptr != NULL)
    {
        Ptr--;
        Level3Length = (ULONG)(Ptr - ProcessorIdentifier);
    }

    VendorIdentifierLength = (USHORT)wcslen(ProcessorVendorIdentifier);

    /* Calculate the size of the full REG_MULTI_SZ data (see swprintf below) */
    HardwareIdsLength = (23 +
                         5 + VendorIdentifierLength + 3 + Level1Length + 1 +
                         1 + VendorIdentifierLength + 3 + Level1Length + 1 +
                         5 + VendorIdentifierLength + 3 + Level2Length + 1 +
                         1 + VendorIdentifierLength + 3 + Level2Length + 1 +
                         5 + VendorIdentifierLength + 3 + Level3Length + 1 +
                         1 + VendorIdentifierLength + 3 + Level3Length + 1 +
                         1) * sizeof(WCHAR);

    /* Allocate a buffer to the data */
    HardwareIdsBuffer = ExAllocatePoolWithTag(PagedPool, HardwareIdsLength, 'IpcA');
    if (HardwareIdsBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    Length = 0;
    Length += _swprintf(&HardwareIdsBuffer[Length], L"ACPI\\VEN_ACPI&DEV_0007");
    HardwareIdsBuffer[Length++] = UNICODE_NULL;
    Length += _swprintf(&HardwareIdsBuffer[Length], L"ACPI\\%s_-_%.*s", ProcessorVendorIdentifier, Level1Length, ProcessorIdentifier);
    HardwareIdsBuffer[Length++] = UNICODE_NULL;

    Length += _swprintf(&HardwareIdsBuffer[Length], L"*%s_-_%.*s", ProcessorVendorIdentifier, Level1Length, ProcessorIdentifier);
    HardwareIdsBuffer[Length++] = UNICODE_NULL;

    Length += _swprintf(&HardwareIdsBuffer[Length], L"ACPI\\%s_-_%.*s", ProcessorVendorIdentifier, Level2Length, ProcessorIdentifier);
    HardwareIdsBuffer[Length++] = UNICODE_NULL;

    Length += _swprintf(&HardwareIdsBuffer[Length], L"*%s_-_%.*s", ProcessorVendorIdentifier, Level2Length, ProcessorIdentifier);
    HardwareIdsBuffer[Length++] = UNICODE_NULL;

    Length += _swprintf(&HardwareIdsBuffer[Length], L"ACPI\\%s_-_%.*s", ProcessorVendorIdentifier, Level3Length, ProcessorIdentifier);
    HardwareIdsBuffer[Length++] = UNICODE_NULL;

    Length += _swprintf(&HardwareIdsBuffer[Length], L"*%s_-_%.*s", ProcessorVendorIdentifier, Level3Length, ProcessorIdentifier);
    HardwareIdsBuffer[Length++] = UNICODE_NULL;
    HardwareIdsBuffer[Length++] = UNICODE_NULL;

    /* Make sure we counted correctly */
    NT_ASSERT(Length * sizeof(WCHAR) == HardwareIdsLength);

    ProcessorHardwareIds.Length = (SHORT)HardwareIdsLength;
    ProcessorHardwareIds.MaximumLength = ProcessorHardwareIds.Length;
    ProcessorHardwareIds.Buffer = HardwareIdsBuffer;

    Length = (5 + VendorIdentifierLength + 3 + Level1Length + 1) * sizeof(WCHAR);
    ProcessorIdString = ExAllocatePoolWithTag(PagedPool, Length, 'IpcA');
    if (ProcessorIdString != NULL)
    {
        Length = _swprintf(ProcessorIdString, L"ACPI\\%s_-_%.*s", ProcessorVendorIdentifier, Level1Length, ProcessorIdentifier);
        ProcessorIdString[Length++] = UNICODE_NULL;
        DPRINT("ProcessorIdString: %S\n", ProcessorIdString);
    }

done:
    if (ProcessorHandle != NULL)
        ZwClose(ProcessorHandle);

    if (ProcessorIdentifier != NULL)
        ExFreePoolWithTag(ProcessorIdentifier, 'IpcA');

    if (ProcessorVendorIdentifier != NULL)
        ExFreePoolWithTag(ProcessorVendorIdentifier, 'IpcA');

    if (!NT_SUCCESS(Status))
    {
        if (HardwareIdsBuffer != NULL)
            ExFreePoolWithTag(HardwareIdsBuffer, 'IpcA');
    }

    return Status;
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

    Status = GetProcessorInformation();
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
