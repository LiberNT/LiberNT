#include "precomp.h"

#include <poclass.h>

#define NDEBUG
#include <debug.h>

typedef struct _ACPI_NOTIFICATION_TARGET
{
    PDEVICE_NOTIFY_CALLBACK Callback;
    PVOID Context;
} ACPI_NOTIFICATION_TARGET, *PACPI_NOTIFICATION_TARGET;

typedef struct _ACPI_GPE_INTERFACE_CONTEXT
{
    ACPI_HANDLE GpeDevice;
    ULONG GpeNumber;
    PGPE_SERVICE_ROUTINE ServiceRoutine;
    PVOID ServiceContext;
} ACPI_GPE_INTERFACE_CONTEXT, *PACPI_GPE_INTERFACE_CONTEXT;

#define ACPI_NOTIFY_STACK_TARGETS 4

BOOLEAN AcpiHardwareIdContains(PPDO_DEVICE_DATA DeviceData, PCWSTR HardwareId)
{
    PWSTR Current;

    if (!DeviceData || !DeviceData->HardwareIDs || !HardwareId)
        return FALSE;

    for (Current = DeviceData->HardwareIDs; *Current; Current += wcslen(Current) + 1)
    {
        if (wcsstr(Current, HardwareId))
            return TRUE;
    }
    return FALSE;
}

static VOID AcpiFanActiveCooling(PVOID Context, BOOLEAN Engaged)
{
    PDEVICE_OBJECT DeviceObject = Context;
    PPDO_DEVICE_DATA DeviceData;

    if (!DeviceObject)
        return;

    DeviceData = DeviceObject->DeviceExtension;
    if (!DeviceData->AcpiHandle)
        return;

    if (!NT_SUCCESS(AcpiThermalSetPower(DeviceData->AcpiHandle, Engaged)))
        DPRINT1("ACPI: Fan active cooling %s failed\n", Engaged ? "on" : "off");
}

static
NTSTATUS
AcpiStatusToNtStatus(ACPI_STATUS Status)
{
    if (ACPI_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

    switch (Status)
    {
        case AE_ALREADY_EXISTS:
            return STATUS_SUCCESS;

        case AE_NO_MEMORY:
            return STATUS_INSUFFICIENT_RESOURCES;

        case AE_NOT_FOUND:
        case AE_NOT_EXIST:
            return STATUS_OBJECT_NAME_NOT_FOUND;

        case AE_BAD_PARAMETER:
            return STATUS_INVALID_PARAMETER;

        default:
            return STATUS_UNSUCCESSFUL;
    }
}

static
NTSTATUS
AcpiInterfaceInstallNotifyHandlers(PPDO_DEVICE_DATA DeviceData);

static
VOID
AcpiInterfaceRemoveNotifyHandlers(PPDO_DEVICE_DATA DeviceData);

static
VOID
ACPI_SYSTEM_XFACE
AcpiInterfaceNotifyThunk(ACPI_HANDLE Handle,
                         UINT32 Value,
                         PVOID Context);

static
PPDO_DEVICE_DATA
AcpiPdoFromContext(PVOID Context)
{
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)Context;

    if (!DeviceObject || !DeviceObject->DeviceExtension)
    {
        return NULL;
    }

    return (PPDO_DEVICE_DATA)DeviceObject->DeviceExtension;
}

#if defined(_M_ARM64) || defined(__aarch64__)
typedef struct _ACPI_DMA_WINDOW_CONTEXT
{
    HAL_ACPI_DMA_WINDOW Windows[HAL_ACPI_MAX_DMA_WINDOWS];
    ULONG WindowCount;
    BOOLEAN Overflow;
} ACPI_DMA_WINDOW_CONTEXT, *PACPI_DMA_WINDOW_CONTEXT;

static VOID
AcpiDmaAddWindow(
    _Inout_ PACPI_DMA_WINDOW_CONTEXT WindowContext,
    _In_ ULONGLONG DeviceBase,
    _In_ ULONGLONG Length,
    _In_ LONGLONG TranslationOffset)
{
    PHAL_ACPI_DMA_WINDOW Window;
    ULONGLONG CpuBase;
    ULONGLONG Magnitude;

    if (!Length || DeviceBase > MAXULONGLONG - (Length - 1))
    {
        WindowContext->Overflow = TRUE;
        return;
    }
    if (WindowContext->WindowCount == HAL_ACPI_MAX_DMA_WINDOWS)
    {
        WindowContext->Overflow = TRUE;
        return;
    }

    if (TranslationOffset < 0)
    {
        Magnitude = (ULONGLONG)(-(TranslationOffset + 1));
        Magnitude++;
        if (DeviceBase < Magnitude)
        {
            WindowContext->Overflow = TRUE;
            return;
        }
        CpuBase = DeviceBase - Magnitude;
    }
    else
    {
        if (DeviceBase > MAXULONGLONG - (ULONGLONG)TranslationOffset)
        {
            WindowContext->Overflow = TRUE;
            return;
        }
        CpuBase = DeviceBase + (ULONGLONG)TranslationOffset;
    }

    if (CpuBase > MAXULONGLONG - (Length - 1))
    {
        WindowContext->Overflow = TRUE;
        return;
    }

    Window = &WindowContext->Windows[WindowContext->WindowCount++];
    Window->DeviceBase = DeviceBase;
    Window->CpuBase = CpuBase;
    Window->Length = Length;
}

static ACPI_STATUS
AcpiDmaResourceCallback(
    _In_ ACPI_RESOURCE *Resource,
    _Inout_ PVOID Context)
{
    PACPI_DMA_WINDOW_CONTEXT WindowContext = Context;

    switch (Resource->Type)
    {
        case ACPI_RESOURCE_TYPE_ADDRESS16:
        case ACPI_RESOURCE_TYPE_ADDRESS32:
        case ACPI_RESOURCE_TYPE_ADDRESS64:
        case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
            break;

        default:
            return AE_OK;
    }

    if (Resource->Data.Address.ResourceType != ACPI_MEMORY_RANGE || Resource->Data.Address.ProducerConsumer != ACPI_PRODUCER)
        return AE_OK;

    switch (Resource->Type)
    {
        case ACPI_RESOURCE_TYPE_ADDRESS16:
            AcpiDmaAddWindow(WindowContext, Resource->Data.Address16.Address.Minimum, Resource->Data.Address16.Address.AddressLength, (SHORT)Resource->Data.Address16.Address.TranslationOffset);
            break;

        case ACPI_RESOURCE_TYPE_ADDRESS32:
            AcpiDmaAddWindow(WindowContext, Resource->Data.Address32.Address.Minimum, Resource->Data.Address32.Address.AddressLength, (LONG)Resource->Data.Address32.Address.TranslationOffset);
            break;

        case ACPI_RESOURCE_TYPE_ADDRESS64:
            AcpiDmaAddWindow(WindowContext, Resource->Data.Address64.Address.Minimum, Resource->Data.Address64.Address.AddressLength, (LONGLONG)Resource->Data.Address64.Address.TranslationOffset);
            break;

        case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
            AcpiDmaAddWindow(WindowContext, Resource->Data.ExtAddress64.Address.Minimum, Resource->Data.ExtAddress64.Address.AddressLength, (LONGLONG)Resource->Data.ExtAddress64.Address.TranslationOffset);
            break;

        default:
            break;
    }

    return WindowContext->Overflow ? AE_CTRL_TERMINATE : AE_OK;
}

static BOOLEAN
AcpiGetDmaWindows(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _Out_writes_(HAL_ACPI_MAX_DMA_WINDOWS) HAL_ACPI_DMA_WINDOW *Windows,
    _Out_ PULONG WindowCount)
{
    ACPI_DMA_WINDOW_CONTEXT WindowContext;
    ACPI_HANDLE CurrentHandle;
    ACPI_HANDLE DmaHandle;
    ACPI_HANDLE ParentHandle;
    ACPI_STATUS Status;

    *WindowCount = 0;
    if (!DeviceData || !DeviceData->AcpiHandle)
        return TRUE;

    CurrentHandle = DeviceData->AcpiHandle;
    while (CurrentHandle)
    {
        DmaHandle = NULL;
        Status = AcpiGetHandle(CurrentHandle, METHOD_NAME__DMA, &DmaHandle);
        if (Status == AE_NOT_FOUND)
            goto GetParent;
        if (ACPI_FAILURE(Status))
        {
            DPRINT1("ACPI: failed to locate inherited _DMA (Status 0x%08lx)\n", Status);
            return FALSE;
        }

        RtlZeroMemory(&WindowContext, sizeof(WindowContext));
        Status = AcpiWalkResources(CurrentHandle, METHOD_NAME__DMA, AcpiDmaResourceCallback, &WindowContext);
        if (ACPI_FAILURE(Status) || WindowContext.Overflow || !WindowContext.WindowCount)
        {
            DPRINT1("ACPI: inherited _DMA is invalid (Status 0x%08lx, windows %lu, overflow %u)\n", Status, WindowContext.WindowCount, WindowContext.Overflow);
            return FALSE;
        }

        RtlCopyMemory(Windows, WindowContext.Windows, WindowContext.WindowCount * sizeof(*Windows));
        *WindowCount = WindowContext.WindowCount;
        return TRUE;

GetParent:
        ParentHandle = NULL;
        Status = AcpiGetParent(CurrentHandle, &ParentHandle);
        if (ACPI_FAILURE(Status) || !ParentHandle || ParentHandle == CurrentHandle)
            break;
        CurrentHandle = ParentHandle;
    }

    return TRUE;
}

static BOOLEAN
NTAPI
AcpiBusTranslateBusAddress(
    _Inout_opt_ PVOID Context,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _In_ ULONG Length,
    _Out_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Length);

    if (!AddressSpace || !TranslatedAddress)
        return FALSE;

    *TranslatedAddress = BusAddress;
    return TRUE;
}

static PDMA_ADAPTER
NTAPI
AcpiBusGetDmaAdapter(
    _Inout_opt_ PVOID Context,
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _Out_ PULONG NumberOfMapRegisters)
{
    HAL_ACPI_DMA_WINDOW Windows[HAL_ACPI_MAX_DMA_WINDOWS];
    PPDO_DEVICE_DATA DeviceData = AcpiPdoFromContext(Context);
    PDMA_ADAPTER DmaAdapter;
    ULONG WindowCount;

    DmaAdapter = (PDMA_ADAPTER)HalGetAdapter(DeviceDescription, NumberOfMapRegisters);
    if (!DmaAdapter)
        return NULL;

    if (!AcpiGetDmaWindows(DeviceData, Windows, &WindowCount))
    {
        DPRINT1("ACPI: refusing unsafe DMA adapter for %p because _DMA could not be parsed\n", Context);
        DmaAdapter->DmaOperations->PutDmaAdapter(DmaAdapter);
        return NULL;
    }

    if (WindowCount && !HalpConfigureDmaAdapter(DmaAdapter, Windows, WindowCount))
    {
        DmaAdapter->DmaOperations->PutDmaAdapter(DmaAdapter);
        return NULL;
    }

    DPRINT1("ACPI: DMA adapter for %p uses %lu inherited _DMA window(s)\n", Context, WindowCount);
    return DmaAdapter;
}

static ULONG
NTAPI
AcpiBusGetSetDeviceData(
    _Inout_opt_ PVOID Context,
    _In_ ULONG DataType,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(DataType);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}
#endif

static
UINT32
ACPI_SYSTEM_XFACE
AcpiInterfaceGpeThunk(
    ACPI_HANDLE GpeDevice,
    UINT32 GpeNumber,
    PVOID Context)
{
    PACPI_GPE_INTERFACE_CONTEXT GpeContext = Context;

    UNREFERENCED_PARAMETER(GpeDevice);
    UNREFERENCED_PARAMETER(GpeNumber);
    if (GpeContext->ServiceRoutine(GpeContext, GpeContext->ServiceContext))
        return ACPI_INTERRUPT_HANDLED;
    return ACPI_INTERRUPT_NOT_HANDLED;
}

static
NTSTATUS
AcpiInterfaceConnectGpe(
    PPDO_DEVICE_DATA DeviceData,
    ULONG GpeNumber,
    KINTERRUPT_MODE Mode,
    BOOLEAN Shareable,
    PGPE_SERVICE_ROUTINE ServiceRoutine,
    PVOID ServiceContext,
    PACPI_GPE_INTERFACE_CONTEXT *ObjectContext)
{
    PACPI_GPE_INTERFACE_CONTEXT GpeContext;
    ACPI_STATUS Status;
    UINT32 Type;

    UNREFERENCED_PARAMETER(Shareable);
    if (!DeviceData || !ServiceRoutine || !ObjectContext)
        return STATUS_INVALID_PARAMETER;
    GpeContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*GpeContext), ACPI_NOTIFY_TAG);
    if (!GpeContext)
        return STATUS_INSUFFICIENT_RESOURCES;
    GpeContext->GpeDevice = NULL;
    GpeContext->GpeNumber = GpeNumber;
    GpeContext->ServiceRoutine = ServiceRoutine;
    GpeContext->ServiceContext = ServiceContext;
    Type = Mode == Latched ? ACPI_GPE_EDGE_TRIGGERED : ACPI_GPE_LEVEL_TRIGGERED;
    Status = AcpiInstallGpeHandler(GpeContext->GpeDevice, GpeNumber, Type, AcpiInterfaceGpeThunk, GpeContext);
    if (ACPI_FAILURE(Status))
    {
        ExFreePoolWithTag(GpeContext, ACPI_NOTIFY_TAG);
        return AcpiStatusToNtStatus(Status);
    }
    *ObjectContext = GpeContext;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiInterfaceDisconnectGpe(
    PACPI_GPE_INTERFACE_CONTEXT GpeContext)
{
    ACPI_STATUS Status;

    if (!GpeContext)
        return STATUS_INVALID_PARAMETER;
    AcpiDisableGpe(GpeContext->GpeDevice, GpeContext->GpeNumber);
    Status = AcpiRemoveGpeHandler(GpeContext->GpeDevice, GpeContext->GpeNumber, AcpiInterfaceGpeThunk);
    if (ACPI_SUCCESS(Status) || Status == AE_NOT_EXIST)
        ExFreePoolWithTag(GpeContext, ACPI_NOTIFY_TAG);
    return AcpiStatusToNtStatus(Status == AE_NOT_EXIST ? AE_OK : Status);
}

VOID
NTAPI
AcpiInterfaceReference(PVOID Context)
{
    PPDO_DEVICE_DATA DeviceData;

    DeviceData = AcpiPdoFromContext(Context);
    if (!DeviceData)
    {
        return;
    }

    InterlockedIncrement((PLONG)&DeviceData->InterfaceRefCount);
}

VOID
NTAPI
AcpiInterfaceDereference(PVOID Context)
{
    PPDO_DEVICE_DATA DeviceData;

    DeviceData = AcpiPdoFromContext(Context);
    if (!DeviceData)
    {
        return;
    }

    InterlockedDecrement((PLONG)&DeviceData->InterfaceRefCount);
}

static
NTSTATUS
AcpiInterfaceInstallNotifyHandlers(PPDO_DEVICE_DATA DeviceData)
{
    ACPI_STATUS Status;

    if (!DeviceData || !DeviceData->AcpiHandle)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DeviceData->NotificationHandlersInstalled)
    {
        return STATUS_SUCCESS;
    }

    Status = AcpiInstallNotifyHandler(DeviceData->AcpiHandle,
                                      ACPI_SYSTEM_NOTIFY,
                                      AcpiInterfaceNotifyThunk,
                                      DeviceData);
    if (ACPI_FAILURE(Status) && Status != AE_ALREADY_EXISTS)
    {
        return AcpiStatusToNtStatus(Status);
    }

    Status = AcpiInstallNotifyHandler(DeviceData->AcpiHandle,
                                      ACPI_DEVICE_NOTIFY,
                                      AcpiInterfaceNotifyThunk,
                                      DeviceData);
    if (ACPI_FAILURE(Status) && Status != AE_ALREADY_EXISTS)
    {
        AcpiRemoveNotifyHandler(DeviceData->AcpiHandle,
                                ACPI_SYSTEM_NOTIFY,
                                AcpiInterfaceNotifyThunk);
        return AcpiStatusToNtStatus(Status);
    }

    DeviceData->NotificationHandlersInstalled = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
AcpiInterfaceRemoveNotifyHandlers(PPDO_DEVICE_DATA DeviceData)
{
    ACPI_STATUS Status;

    if (!DeviceData || !DeviceData->AcpiHandle)
    {
        return;
    }

    if (!DeviceData->NotificationHandlersInstalled)
    {
        return;
    }

    Status = AcpiRemoveNotifyHandler(DeviceData->AcpiHandle,
                                     ACPI_SYSTEM_NOTIFY,
                                     AcpiInterfaceNotifyThunk);
    if (ACPI_FAILURE(Status) && Status != AE_NOT_EXIST)
    {
        DPRINT1("AcpiRemoveNotifyHandler (system) failed: %s\n",
                AcpiFormatException(Status));
    }

    Status = AcpiRemoveNotifyHandler(DeviceData->AcpiHandle,
                                     ACPI_DEVICE_NOTIFY,
                                     AcpiInterfaceNotifyThunk);
    if (ACPI_FAILURE(Status) && Status != AE_NOT_EXIST)
    {
        DPRINT1("AcpiRemoveNotifyHandler (device) failed: %s\n",
                AcpiFormatException(Status));
    }

    DeviceData->NotificationHandlersInstalled = FALSE;
}

static
VOID
ACPI_SYSTEM_XFACE
AcpiInterfaceNotifyThunk(ACPI_HANDLE Handle,
                         UINT32 Value,
                         PVOID Context)
{
    PPDO_DEVICE_DATA DeviceData;
    ACPI_NOTIFICATION_TARGET StackTargets[ACPI_NOTIFY_STACK_TARGETS];
    PACPI_NOTIFICATION_TARGET Targets;
    ULONG TargetCount;
    ULONG Index;
    KIRQL OldIrql;
    PLIST_ENTRY Link;

    UNREFERENCED_PARAMETER(Handle);

    DeviceData = (PPDO_DEVICE_DATA)Context;
    if (!DeviceData)
    {
        return;
    }

    Targets = StackTargets;
    TargetCount = 0;

    KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

    for (Link = DeviceData->NotificationList.Flink;
         Link != &DeviceData->NotificationList;
         Link = Link->Flink)
    {
        TargetCount++;
    }

    if (TargetCount == 0)
    {
        KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
        return;
    }

    if (TargetCount > RTL_NUMBER_OF(StackTargets))
    {
        Targets = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(ACPI_NOTIFICATION_TARGET) * TargetCount,
                                        ACPI_NOTIFY_TAG);
        if (!Targets)
        {
            KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
            return;
        }
    }

    Index = 0;
    for (Link = DeviceData->NotificationList.Flink;
         Link != &DeviceData->NotificationList;
         Link = Link->Flink)
    {
        PACPI_NOTIFICATION_ENTRY Entry;

        Entry = CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry);
        Targets[Index].Callback = Entry->Callback;
        Targets[Index].Context = Entry->Context;
        Index++;
    }

    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

    for (Index = 0; Index < TargetCount; Index++)
    {
        if (Targets[Index].Callback)
        {
            Targets[Index].Callback(Targets[Index].Context, Value);
        }
    }

    if (Targets != StackTargets)
    {
        ExFreePoolWithTag(Targets, ACPI_NOTIFY_TAG);
    }
}

/*
 * ACPI_INTERFACE_STANDARD callbacks (take PDEVICE_OBJECT as context)
 */
NTSTATUS
NTAPI
AcpiInterfaceConnectVector(PDEVICE_OBJECT Context,
                           ULONG GpeNumber,
                           KINTERRUPT_MODE Mode,
                           BOOLEAN Shareable,
                           PGPE_SERVICE_ROUTINE ServiceRoutine,
                           PVOID ServiceContext,
                           PVOID ObjectContext)
{
  PPDO_DEVICE_DATA DeviceData = AcpiPdoFromContext(Context);
  PACPI_GPE_INTERFACE_CONTEXT GpeContext;
  NTSTATUS Status;

  if (!ObjectContext)
      return STATUS_INVALID_PARAMETER;
  Status = AcpiInterfaceConnectGpe(DeviceData, GpeNumber, Mode, Shareable, ServiceRoutine, ServiceContext, &GpeContext);
  if (NT_SUCCESS(Status))
      *(PVOID *)ObjectContext = GpeContext;
  return Status;
}

NTSTATUS
NTAPI
AcpiInterfaceDisconnectVector(PVOID ObjectContext)
{
  return AcpiInterfaceDisconnectGpe(ObjectContext);
}

NTSTATUS
NTAPI
AcpiInterfaceEnableEvent(PDEVICE_OBJECT Context,
                         PVOID ObjectContext)
{
  PACPI_GPE_INTERFACE_CONTEXT GpeContext = ObjectContext;

  UNREFERENCED_PARAMETER(Context);
  if (!GpeContext)
      return STATUS_INVALID_PARAMETER;
  return AcpiStatusToNtStatus(AcpiEnableGpe(GpeContext->GpeDevice, GpeContext->GpeNumber));
}

NTSTATUS
NTAPI
AcpiInterfaceDisableEvent(PDEVICE_OBJECT Context,
                          PVOID ObjectContext)
{
  PACPI_GPE_INTERFACE_CONTEXT GpeContext = ObjectContext;

  UNREFERENCED_PARAMETER(Context);
  if (!GpeContext)
      return STATUS_INVALID_PARAMETER;
  return AcpiStatusToNtStatus(AcpiDisableGpe(GpeContext->GpeDevice, GpeContext->GpeNumber));
}

NTSTATUS
NTAPI
AcpiInterfaceClearStatus(PDEVICE_OBJECT Context,
                         PVOID ObjectContext)
{
  PACPI_GPE_INTERFACE_CONTEXT GpeContext = ObjectContext;

  UNREFERENCED_PARAMETER(Context);
  if (!GpeContext)
      return STATUS_INVALID_PARAMETER;
  return AcpiStatusToNtStatus(AcpiClearGpe(GpeContext->GpeDevice, GpeContext->GpeNumber));
}

/*
 * ACPI_INTERFACE_STANDARD2 callbacks (take PVOID context directly)
 */
static
PPDO_DEVICE_DATA
AcpiPdoFromContext2(PVOID Context)
{
    /*
     * For ACPI_INTERFACE_STANDARD2, Context is the PDO itself (Common.Self).
     * Same as AcpiPdoFromContext but named differently for clarity.
     */
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)Context;

    if (!DeviceObject || !DeviceObject->DeviceExtension)
    {
        return NULL;
    }

    return (PPDO_DEVICE_DATA)DeviceObject->DeviceExtension;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
static
NTSTATUS
NTAPI
AcpiInterface2ConnectVector(
    PVOID Context,
    ULONG GpeNumber,
    KINTERRUPT_MODE Mode,
    BOOLEAN Shareable,
    PGPE_SERVICE_ROUTINE ServiceRoutine,
    PVOID ServiceContext,
    PVOID *ObjectContext)
{
    return AcpiInterfaceConnectGpe(AcpiPdoFromContext2(Context), GpeNumber, Mode, Shareable, ServiceRoutine, ServiceContext, (PACPI_GPE_INTERFACE_CONTEXT *)ObjectContext);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
static
NTSTATUS
NTAPI
AcpiInterface2DisconnectVector(
    PVOID Context,
    PVOID ObjectContext)
{
    UNREFERENCED_PARAMETER(Context);
    return AcpiInterfaceDisconnectGpe(ObjectContext);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
static
NTSTATUS
NTAPI
AcpiInterface2EnableEvent(
    PVOID Context,
    PVOID ObjectContext)
{
    PACPI_GPE_INTERFACE_CONTEXT GpeContext = ObjectContext;

    UNREFERENCED_PARAMETER(Context);
    if (!GpeContext)
        return STATUS_INVALID_PARAMETER;
    return AcpiStatusToNtStatus(AcpiEnableGpe(GpeContext->GpeDevice, GpeContext->GpeNumber));
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
static
NTSTATUS
NTAPI
AcpiInterface2DisableEvent(
    PVOID Context,
    PVOID ObjectContext)
{
    PACPI_GPE_INTERFACE_CONTEXT GpeContext = ObjectContext;

    UNREFERENCED_PARAMETER(Context);
    if (!GpeContext)
        return STATUS_INVALID_PARAMETER;
    return AcpiStatusToNtStatus(AcpiDisableGpe(GpeContext->GpeDevice, GpeContext->GpeNumber));
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
static
NTSTATUS
NTAPI
AcpiInterface2ClearStatus(
    PVOID Context,
    PVOID ObjectContext)
{
    PACPI_GPE_INTERFACE_CONTEXT GpeContext = ObjectContext;

    UNREFERENCED_PARAMETER(Context);
    if (!GpeContext)
        return STATUS_INVALID_PARAMETER;
    return AcpiStatusToNtStatus(AcpiClearGpe(GpeContext->GpeDevice, GpeContext->GpeNumber));
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
static
NTSTATUS
NTAPI
AcpiInterface2RegisterNotifications(
    PVOID Context,
    PDEVICE_NOTIFY_CALLBACK2 NotificationHandler,
    PVOID NotificationContext)
{
    PPDO_DEVICE_DATA DeviceData;
    PACPI_NOTIFICATION_ENTRY Entry;
    KIRQL OldIrql;
    PLIST_ENTRY Link;
    BOOLEAN NeedInstall;
    NTSTATUS Status;

    if (!NotificationHandler)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DeviceData = AcpiPdoFromContext2(Context);
    if (!DeviceData)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * For STANDARD2, we store the callback with its new signature.
     * We reuse ACPI_NOTIFICATION_ENTRY since the callback pointer size is the same.
     * The thunk handles the signature difference.
     */
    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(ACPI_NOTIFICATION_ENTRY),
                                  ACPI_NOTIFY_TAG);
    if (!Entry)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Store as PDEVICE_NOTIFY_CALLBACK (same size as PDEVICE_NOTIFY_CALLBACK2) */
    Entry->Callback = (PDEVICE_NOTIFY_CALLBACK)(ULONG_PTR)NotificationHandler;
    Entry->Context = NotificationContext;

    KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

    NeedInstall = (DeviceData->NotificationRegistrationCount == 0);
    if (NeedInstall)
    {
        KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

        Status = AcpiInterfaceInstallNotifyHandlers(DeviceData);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);
            return Status;
        }

        KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
    }

    /* Check for duplicate */
    for (Link = DeviceData->NotificationList.Flink;
         Link != &DeviceData->NotificationList;
         Link = Link->Flink)
    {
        PACPI_NOTIFICATION_ENTRY Existing;

        Existing = CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry);
        if (Existing->Callback == Entry->Callback &&
            Existing->Context == Entry->Context)
        {
            KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
            ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);
            return STATUS_SUCCESS;
        }
    }

    InsertTailList(&DeviceData->NotificationList, &Entry->ListEntry);
    DeviceData->NotificationRegistrationCount++;
    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

    AcpiInterfaceReference(DeviceData->Common.Self);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static
VOID
NTAPI
AcpiInterface2UnregisterNotifications(
    PVOID Context)
{
    PPDO_DEVICE_DATA DeviceData;
    KIRQL OldIrql;
    PLIST_ENTRY Link, Next;
    BOOLEAN RemoveHandlers = FALSE;
    LIST_ENTRY FreeList;

    DeviceData = AcpiPdoFromContext2(Context);
    if (!DeviceData)
    {
        return;
    }

    InitializeListHead(&FreeList);

    KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

    /*
     * STANDARD2 UnregisterForDeviceNotifications takes no callback parameter,
     * so it unregisters ALL notifications for this context.
     */
    Link = DeviceData->NotificationList.Flink;
    while (Link != &DeviceData->NotificationList)
    {
        Next = Link->Flink;
        RemoveEntryList(Link);
        InsertTailList(&FreeList, Link);
        if (DeviceData->NotificationRegistrationCount > 0)
        {
            DeviceData->NotificationRegistrationCount--;
        }
        Link = Next;
    }

    RemoveHandlers = (DeviceData->NotificationRegistrationCount == 0);

    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

    /* Free entries and dereference outside lock */
    while (!IsListEmpty(&FreeList))
    {
        Link = RemoveHeadList(&FreeList);
        ExFreePoolWithTag(CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry),
                          ACPI_NOTIFY_TAG);
        AcpiInterfaceDereference(DeviceData->Common.Self);
    }

    if (RemoveHandlers)
    {
        AcpiInterfaceRemoveNotifyHandlers(DeviceData);
    }
}

NTSTATUS
NTAPI
AcpiInterfaceNotificationsRegister(PDEVICE_OBJECT Context,
                                   PDEVICE_NOTIFY_CALLBACK NotificationHandler,
                                   PVOID NotificationContext)
{
  PPDO_DEVICE_DATA DeviceData;
  PACPI_NOTIFICATION_ENTRY Entry;
  KIRQL OldIrql;
  PLIST_ENTRY Link;
  BOOLEAN NeedInstall;
  NTSTATUS Status;

  if (!NotificationHandler)
  {
    return STATUS_INVALID_PARAMETER;
  }

  DeviceData = AcpiPdoFromContext(Context);
  if (!DeviceData)
  {
    return STATUS_INVALID_PARAMETER;
  }

  Entry = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(ACPI_NOTIFICATION_ENTRY),
                                ACPI_NOTIFY_TAG);
  if (!Entry)
  {
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  Entry->Callback = NotificationHandler;
  Entry->Context = NotificationContext;

  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

  NeedInstall = (DeviceData->NotificationRegistrationCount == 0);
  if (NeedInstall)
  {
    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

    Status = AcpiInterfaceInstallNotifyHandlers(DeviceData);
    if (!NT_SUCCESS(Status))
    {
      ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);
      return Status;
    }

    KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
  }

  for (Link = DeviceData->NotificationList.Flink;
       Link != &DeviceData->NotificationList;
       Link = Link->Flink)
  {
    PACPI_NOTIFICATION_ENTRY Existing;

    Existing = CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry);
    if (Existing->Callback == NotificationHandler &&
        Existing->Context == NotificationContext)
    {
      KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
      ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);
      return STATUS_SUCCESS;
    }
  }

  InsertTailList(&DeviceData->NotificationList, &Entry->ListEntry);
  DeviceData->NotificationRegistrationCount++;
  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  AcpiInterfaceReference(DeviceData->Common.Self);

  return STATUS_SUCCESS;
}

VOID
NTAPI
AcpiInterfaceNotificationsUnregister(PDEVICE_OBJECT Context,
                                     PDEVICE_NOTIFY_CALLBACK NotificationHandler)
{
  PPDO_DEVICE_DATA DeviceData;
  KIRQL OldIrql;
  PLIST_ENTRY Link, Next;
  BOOLEAN RemoveHandlers = FALSE;

  if (!NotificationHandler)
  {
    return;
  }

  DeviceData = AcpiPdoFromContext(Context);
  if (!DeviceData)
  {
    return;
  }

  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

  Link = DeviceData->NotificationList.Flink;
  while (Link != &DeviceData->NotificationList)
  {
    PACPI_NOTIFICATION_ENTRY Entry;

    Entry = CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry);
    Next = Link->Flink;

    if (Entry->Callback == NotificationHandler)
    {
      RemoveEntryList(Link);
      if (DeviceData->NotificationRegistrationCount > 0)
      {
        DeviceData->NotificationRegistrationCount--;
      }
      RemoveHandlers = RemoveHandlers ||
                       (DeviceData->NotificationRegistrationCount == 0);
      ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);

      AcpiInterfaceDereference(DeviceData->Common.Self);
    }

    Link = Next;
  }

  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  if (RemoveHandlers)
  {
    AcpiInterfaceRemoveNotifyHandlers(DeviceData);
  }
}

VOID
AcpiInterfaceResetNotifications(PPDO_DEVICE_DATA DeviceData)
{
  LIST_ENTRY FreeList;
  PLIST_ENTRY Link;
  KIRQL OldIrql;

  if (!DeviceData)
  {
    return;
  }

  InitializeListHead(&FreeList);

  AcpiInterfaceRemoveNotifyHandlers(DeviceData);

  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
  DeviceData->NotificationRegistrationCount = 0;

  while (!IsListEmpty(&DeviceData->NotificationList))
  {
    Link = RemoveHeadList(&DeviceData->NotificationList);
    InsertTailList(&FreeList, Link);
  }

  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  while (!IsListEmpty(&FreeList))
  {
    Link = RemoveHeadList(&FreeList);
    AcpiInterfaceDereference(DeviceData->Common.Self);
    ExFreePoolWithTag(CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry),
                      ACPI_NOTIFY_TAG);
  }
}

NTSTATUS
Bus_PDO_QueryInterface(PPDO_DEVICE_DATA DeviceData,
                       PIRP Irp)
{
  PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
  PACPI_INTERFACE_STANDARD AcpiInterface;
  PACPI_INTERFACE_STANDARD2 AcpiInterface2;
  PTHERMAL_COOLING_INTERFACE ThermalInterface;
#if defined(_M_ARM64) || defined(__aarch64__)
  PBUS_INTERFACE_STANDARD BusInterface;
#endif

  if (IrpSp->Parameters.QueryInterface.Version != 1)
  {
      DPRINT1("Invalid version number: %d\n",
              IrpSp->Parameters.QueryInterface.Version);
      return STATUS_INVALID_PARAMETER;
  }

  if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType, &GUID_THERMAL_COOLING_INTERFACE, sizeof(GUID)) == sizeof(GUID))
  {
      if (!AcpiHardwareIdContains(DeviceData, L"PNP0C0B"))
          return STATUS_NOT_SUPPORTED;
      if (IrpSp->Parameters.QueryInterface.Size < sizeof(THERMAL_COOLING_INTERFACE))
          return STATUS_BUFFER_TOO_SMALL;

      ThermalInterface = (PTHERMAL_COOLING_INTERFACE)IrpSp->Parameters.QueryInterface.Interface;
      RtlZeroMemory(ThermalInterface, sizeof(*ThermalInterface));
      ThermalInterface->Size = sizeof(*ThermalInterface);
      ThermalInterface->Version = THERMAL_COOLING_INTERFACE_VERSION;
      ThermalInterface->Context = DeviceData->Common.Self;
      ThermalInterface->InterfaceReference = AcpiInterfaceReference;
      ThermalInterface->InterfaceDereference = AcpiInterfaceDereference;
      ThermalInterface->Flags = ThermalDeviceFlagActiveCooling;
      ThermalInterface->ActiveCooling = AcpiFanActiveCooling;
      AcpiInterfaceReference(DeviceData->Common.Self);
      return STATUS_SUCCESS;
  }
  else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                        &GUID_ACPI_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
  {
      DPRINT("GUID_ACPI_INTERFACE_STANDARD\n");

      if (IrpSp->Parameters.QueryInterface.Size < sizeof(ACPI_INTERFACE_STANDARD))
      {
          DPRINT1("Buffer too small! (%d)\n", IrpSp->Parameters.QueryInterface.Size);
          return STATUS_BUFFER_TOO_SMALL;
      }

     AcpiInterface = (PACPI_INTERFACE_STANDARD)IrpSp->Parameters.QueryInterface.Interface;

     AcpiInterface->Size = sizeof(ACPI_INTERFACE_STANDARD);
     AcpiInterface->Version = 1;
     AcpiInterface->Context = DeviceData->Common.Self;
     AcpiInterface->InterfaceReference = AcpiInterfaceReference;
     AcpiInterface->InterfaceDereference = AcpiInterfaceDereference;
     AcpiInterface->GpeConnectVector = AcpiInterfaceConnectVector;
     AcpiInterface->GpeDisconnectVector = AcpiInterfaceDisconnectVector;
     AcpiInterface->GpeEnableEvent = AcpiInterfaceEnableEvent;
     AcpiInterface->GpeDisableEvent = AcpiInterfaceDisableEvent;
     AcpiInterface->GpeClearStatus = AcpiInterfaceClearStatus;
     AcpiInterface->RegisterForDeviceNotifications = AcpiInterfaceNotificationsRegister;
     AcpiInterface->UnregisterForDeviceNotifications = AcpiInterfaceNotificationsUnregister;

     AcpiInterfaceReference(AcpiInterface->Context);

     return STATUS_SUCCESS;
  }
  else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                             &GUID_ACPI_INTERFACE_STANDARD2, sizeof(GUID)) == sizeof(GUID))
  {
      DPRINT("GUID_ACPI_INTERFACE_STANDARD2\n");

      if (IrpSp->Parameters.QueryInterface.Size < sizeof(ACPI_INTERFACE_STANDARD2))
      {
          DPRINT1("Buffer too small! (%d)\n", IrpSp->Parameters.QueryInterface.Size);
          return STATUS_BUFFER_TOO_SMALL;
      }

      AcpiInterface2 = (PACPI_INTERFACE_STANDARD2)IrpSp->Parameters.QueryInterface.Interface;

      AcpiInterface2->Size = sizeof(ACPI_INTERFACE_STANDARD2);
      AcpiInterface2->Version = 1;
      AcpiInterface2->Context = DeviceData->Common.Self;
      AcpiInterface2->InterfaceReference = AcpiInterfaceReference;
      AcpiInterface2->InterfaceDereference = AcpiInterfaceDereference;
      AcpiInterface2->GpeConnectVector = AcpiInterface2ConnectVector;
      AcpiInterface2->GpeDisconnectVector = AcpiInterface2DisconnectVector;
      AcpiInterface2->GpeEnableEvent = AcpiInterface2EnableEvent;
      AcpiInterface2->GpeDisableEvent = AcpiInterface2DisableEvent;
      AcpiInterface2->GpeClearStatus = AcpiInterface2ClearStatus;
      AcpiInterface2->RegisterForDeviceNotifications = AcpiInterface2RegisterNotifications;
      AcpiInterface2->UnregisterForDeviceNotifications = AcpiInterface2UnregisterNotifications;

      AcpiInterfaceReference(AcpiInterface2->Context);

      return STATUS_SUCCESS;
  }
#if defined(_M_ARM64) || defined(__aarch64__)
  else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType, &GUID_BUS_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
  {
      if (IrpSp->Parameters.QueryInterface.Size < sizeof(BUS_INTERFACE_STANDARD))
          return STATUS_BUFFER_TOO_SMALL;

      BusInterface = (PBUS_INTERFACE_STANDARD)IrpSp->Parameters.QueryInterface.Interface;
      RtlZeroMemory(BusInterface, sizeof(*BusInterface));
      BusInterface->Size = sizeof(*BusInterface);
      BusInterface->Version = 1;
      BusInterface->Context = DeviceData->Common.Self;
      BusInterface->InterfaceReference = AcpiInterfaceReference;
      BusInterface->InterfaceDereference = AcpiInterfaceDereference;
      BusInterface->TranslateBusAddress = AcpiBusTranslateBusAddress;
      BusInterface->GetDmaAdapter = AcpiBusGetDmaAdapter;
      BusInterface->SetBusData = AcpiBusGetSetDeviceData;
      BusInterface->GetBusData = AcpiBusGetSetDeviceData;
      AcpiInterfaceReference(BusInterface->Context);
      return STATUS_SUCCESS;
  }
#endif
  else
  {
      DPRINT1("Invalid GUID\n");
      return STATUS_NOT_SUPPORTED;
  }
}

/* ======================================================================
 * Notify interface for the namespace node of a PCI function
 * (IOCTL_ACPI_QUERY_PCI_NOTIFY_INTERFACE).
 *
 * A PCI function's node has no ACPI PDO, so the per-PDO notification list
 * above does not apply.  Each interface handed out is its own context: one
 * registration, installed on the node for system and device notifies, kept
 * alive by the interface's references.
 * ====================================================================== */

#define ACPI_PCI_NOTIFY_CONTEXT_SIGNATURE 'NPcA'

typedef struct _ACPI_PCI_NOTIFY_CONTEXT
{
    ULONG Signature;
    volatile LONG RefCount;
    ACPI_HANDLE Handle;
    KSPIN_LOCK Lock;
    PDEVICE_NOTIFY_CALLBACK2 Callback;
    PVOID CallbackContext;
    BOOLEAN Installed;
} ACPI_PCI_NOTIFY_CONTEXT, *PACPI_PCI_NOTIFY_CONTEXT;

static PACPI_PCI_NOTIFY_CONTEXT
AcpiPciNotifyFromContext(PVOID Context)
{
    PACPI_PCI_NOTIFY_CONTEXT Notify = (PACPI_PCI_NOTIFY_CONTEXT)Context;

    return (Notify != NULL && Notify->Signature == ACPI_PCI_NOTIFY_CONTEXT_SIGNATURE) ? Notify : NULL;
}

static
VOID
ACPI_SYSTEM_XFACE
AcpiPciNotifyThunk(ACPI_HANDLE Handle, UINT32 Value, PVOID Context)
{
    PACPI_PCI_NOTIFY_CONTEXT Notify = AcpiPciNotifyFromContext(Context);
    PDEVICE_NOTIFY_CALLBACK2 Callback;
    PVOID CallbackContext;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Handle);
    if (Notify == NULL)
        return;
    KeAcquireSpinLock(&Notify->Lock, &OldIrql);
    Callback = Notify->Callback;
    CallbackContext = Notify->CallbackContext;
    KeReleaseSpinLock(&Notify->Lock, OldIrql);
    if (Callback != NULL)
        Callback(CallbackContext, Value);
}

static VOID
AcpiPciNotifyRemoveHandlers(PACPI_PCI_NOTIFY_CONTEXT Notify)
{
    if (!Notify->Installed)
        return;
    AcpiRemoveNotifyHandler(Notify->Handle, ACPI_DEVICE_NOTIFY, AcpiPciNotifyThunk);
    AcpiRemoveNotifyHandler(Notify->Handle, ACPI_SYSTEM_NOTIFY, AcpiPciNotifyThunk);
    Notify->Installed = FALSE;
}

static VOID
NTAPI
AcpiPciNotifyReference(PVOID Context)
{
    PACPI_PCI_NOTIFY_CONTEXT Notify = AcpiPciNotifyFromContext(Context);

    if (Notify != NULL)
        InterlockedIncrement(&Notify->RefCount);
}

static VOID
NTAPI
AcpiPciNotifyDereference(PVOID Context)
{
    PACPI_PCI_NOTIFY_CONTEXT Notify = AcpiPciNotifyFromContext(Context);

    if (Notify == NULL || InterlockedDecrement(&Notify->RefCount) != 0)
        return;
    AcpiPciNotifyRemoveHandlers(Notify);
    Notify->Signature = 0;
    ExFreePoolWithTag(Notify, ACPI_NOTIFY_TAG);
}

static NTSTATUS
NTAPI
AcpiPciNotifyRegister(PVOID Context, PDEVICE_NOTIFY_CALLBACK2 Handler, PVOID HandlerContext)
{
    PACPI_PCI_NOTIFY_CONTEXT Notify = AcpiPciNotifyFromContext(Context);
    ACPI_STATUS Status;
    KIRQL OldIrql;

    if (Notify == NULL || Handler == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Notify->Installed)
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireSpinLock(&Notify->Lock, &OldIrql);
    Notify->Callback = Handler;
    Notify->CallbackContext = HandlerContext;
    KeReleaseSpinLock(&Notify->Lock, OldIrql);

    Status = AcpiInstallNotifyHandler(Notify->Handle, ACPI_SYSTEM_NOTIFY, AcpiPciNotifyThunk, Notify);
    if (ACPI_SUCCESS(Status))
    {
        Status = AcpiInstallNotifyHandler(Notify->Handle, ACPI_DEVICE_NOTIFY, AcpiPciNotifyThunk, Notify);
        if (ACPI_FAILURE(Status))
            AcpiRemoveNotifyHandler(Notify->Handle, ACPI_SYSTEM_NOTIFY, AcpiPciNotifyThunk);
    }
    if (ACPI_FAILURE(Status))
    {
        KeAcquireSpinLock(&Notify->Lock, &OldIrql);
        Notify->Callback = NULL;
        Notify->CallbackContext = NULL;
        KeReleaseSpinLock(&Notify->Lock, OldIrql);
        return AcpiStatusToNtStatus(Status);
    }
    Notify->Installed = TRUE;
    return STATUS_SUCCESS;
}

static VOID
NTAPI
AcpiPciNotifyUnregister(PVOID Context)
{
    PACPI_PCI_NOTIFY_CONTEXT Notify = AcpiPciNotifyFromContext(Context);
    KIRQL OldIrql;

    if (Notify == NULL)
        return;
    AcpiPciNotifyRemoveHandlers(Notify);
    KeAcquireSpinLock(&Notify->Lock, &OldIrql);
    Notify->Callback = NULL;
    Notify->CallbackContext = NULL;
    KeReleaseSpinLock(&Notify->Lock, OldIrql);
}

/* GPEs belong to the platform's own devices; this interface offers none. */
static NTSTATUS NTAPI
AcpiPciNotifyGpeConnect(PVOID Context, ULONG GpeNumber, KINTERRUPT_MODE Mode, BOOLEAN Shareable,
                        PGPE_SERVICE_ROUTINE ServiceRoutine, PVOID ServiceContext, PVOID *ObjectContext)
{
    UNREFERENCED_PARAMETER(Context); UNREFERENCED_PARAMETER(GpeNumber);
    UNREFERENCED_PARAMETER(Mode); UNREFERENCED_PARAMETER(Shareable);
    UNREFERENCED_PARAMETER(ServiceRoutine); UNREFERENCED_PARAMETER(ServiceContext);
    if (ObjectContext != NULL)
        *ObjectContext = NULL;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS NTAPI
AcpiPciNotifyGpeObject(PVOID Context, PVOID ObjectContext)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ObjectContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
AcpiQueryPciNotifyInterfaceIoctl(PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    PACPI_PCI_NOTIFY_INTERFACE_INPUT Input;
    PACPI_INTERFACE_STANDARD2 Interface;
    PACPI_PCI_NOTIFY_CONTEXT Notify;
    ACPI_HANDLE Handle;

    PAGED_CODE();

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Input) ||
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Interface) ||
        Irp->AssociatedIrp.SystemBuffer == NULL)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Input = (PACPI_PCI_NOTIFY_INTERFACE_INPUT)Irp->AssociatedIrp.SystemBuffer;
    if (Input->Signature != ACPI_PCI_NOTIFY_INTERFACE_INPUT_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    if (!AcpiFindPciDeviceInNamespace(Input->Segment, Input->Bus, Input->Device, Input->Function, &Handle))
        return STATUS_NOT_FOUND;

    Notify = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Notify), ACPI_NOTIFY_TAG);
    if (Notify == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Notify, sizeof(*Notify));
    Notify->Signature = ACPI_PCI_NOTIFY_CONTEXT_SIGNATURE;
    Notify->RefCount = 1;   /* the caller's */
    Notify->Handle = Handle;
    KeInitializeSpinLock(&Notify->Lock);

    /* The buffered output overlays the input, which has been read. */
    Interface = (PACPI_INTERFACE_STANDARD2)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Interface, sizeof(*Interface));
    Interface->Size = sizeof(*Interface);
    Interface->Version = 1;
    Interface->Context = Notify;
    Interface->InterfaceReference = AcpiPciNotifyReference;
    Interface->InterfaceDereference = AcpiPciNotifyDereference;
    Interface->GpeConnectVector = AcpiPciNotifyGpeConnect;
    Interface->GpeDisconnectVector = AcpiPciNotifyGpeObject;
    Interface->GpeEnableEvent = AcpiPciNotifyGpeObject;
    Interface->GpeDisableEvent = AcpiPciNotifyGpeObject;
    Interface->GpeClearStatus = AcpiPciNotifyGpeObject;
    Interface->RegisterForDeviceNotifications = AcpiPciNotifyRegister;
    Interface->UnregisterForDeviceNotifications = AcpiPciNotifyUnregister;
    Irp->IoStatus.Information = sizeof(*Interface);
    return STATUS_SUCCESS;
}
