/*
 * PROJECT:     LiberNT AMD GPIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GpioClx client for the AMD FCH GPIO controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <gpioclx.h>

#define NDEBUG
#include <debug.h>

#define AMDGPIO_TAG                 'oiPA'
#define AMDGPIO_PINS                184
#define AMDGPIO_PINS_PER_BANK       64
#define AMDGPIO_RESERVED_PIN        63
#define AMDGPIO_SETTLE_POLLS        1000

#define AMDGPIO_MASTER              0xFC
#define AMDGPIO_MASTER_EOI          (1UL << 29)
#define AMDGPIO_STATUS_LOW          0x2F8
#define AMDGPIO_STATUS_HIGH         0x2FC

#define AMDGPIO_LEVEL_TRIGGER       (1UL << 8)
#define AMDGPIO_ACTIVE_SHIFT        9
#define AMDGPIO_ACTIVE_MASK         (3UL << AMDGPIO_ACTIVE_SHIFT)
#define AMDGPIO_ACTIVE_HIGH         (0UL << AMDGPIO_ACTIVE_SHIFT)
#define AMDGPIO_ACTIVE_LOW          (1UL << AMDGPIO_ACTIVE_SHIFT)
#define AMDGPIO_ACTIVE_BOTH         (2UL << AMDGPIO_ACTIVE_SHIFT)
#define AMDGPIO_INTERRUPT_ENABLE    (1UL << 11)
#define AMDGPIO_INTERRUPT_DELIVERY  (1UL << 12)
#define AMDGPIO_PIN_STATE           (1UL << 16)
#define AMDGPIO_PULL_UP             (1UL << 20)
#define AMDGPIO_PULL_DOWN           (1UL << 21)
#define AMDGPIO_OUTPUT_VALUE        (1UL << 22)
#define AMDGPIO_OUTPUT_ENABLE       (1UL << 23)
#define AMDGPIO_INTERRUPT_STATUS    (1UL << 28)
#define AMDGPIO_WAKE_STATUS         (1UL << 29)
#define AMDGPIO_STATUS_BITS         (AMDGPIO_INTERRUPT_STATUS | AMDGPIO_WAKE_STATUS)

typedef struct _AMDGPIO_CONTEXT
{
    PUCHAR Base;
    ULONG Length;
    ULONG TotalPins;
    ULONG64 Enabled[(AMDGPIO_PINS + AMDGPIO_PINS_PER_BANK - 1) / AMDGPIO_PINS_PER_BANK];
} AMDGPIO_CONTEXT, *PAMDGPIO_CONTEXT;

static
ULONG
AmdGpioRead(
    _In_ PAMDGPIO_CONTEXT Context,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Context->Base + Offset));
}

static
VOID
AmdGpioWrite(
    _In_ PAMDGPIO_CONTEXT Context,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->Base + Offset), Value);
}

static
BOOLEAN
AmdGpioResolvePin(
    _In_ PAMDGPIO_CONTEXT Context,
    _In_ BANK_ID BankId,
    _In_ ULONG PinNumber,
    _Out_ PULONG Offset)
{
    ULONG Pin = (ULONG)BankId * AMDGPIO_PINS_PER_BANK + PinNumber;

    *Offset = 0;
    if (PinNumber >= AMDGPIO_PINS_PER_BANK || Pin >= Context->TotalPins || Pin == AMDGPIO_RESERVED_PIN)
        return FALSE;
    *Offset = Pin * sizeof(ULONG);
    return TRUE;
}

static
ULONG
AmdGpioReadPin(
    _In_ PAMDGPIO_CONTEXT Context,
    _In_ ULONG Offset)
{
    return AmdGpioRead(Context, Offset) & ~AMDGPIO_STATUS_BITS;
}

static
VOID
AmdGpioQuiesce(
    _In_ PAMDGPIO_CONTEXT Context,
    _In_ BOOLEAN Restore)
{
    ULONG Pin;
    ULONG Value;

    for (Pin = 0; Pin < Context->TotalPins; Pin++)
    {
        if (Pin == AMDGPIO_RESERVED_PIN)
            continue;
        Value = AmdGpioRead(Context, Pin * sizeof(ULONG));
        Value &= ~(AMDGPIO_INTERRUPT_ENABLE | AMDGPIO_INTERRUPT_DELIVERY);
        if (Restore && (Context->Enabled[Pin / AMDGPIO_PINS_PER_BANK] & (1ULL << (Pin % AMDGPIO_PINS_PER_BANK))))
            Value |= AMDGPIO_INTERRUPT_ENABLE | AMDGPIO_INTERRUPT_DELIVERY;
        AmdGpioWrite(Context, Pin * sizeof(ULONG), Value);
    }
    AmdGpioWrite(Context, AMDGPIO_MASTER, AmdGpioRead(Context, AMDGPIO_MASTER) | AMDGPIO_MASTER_EOI);
}

static
NTSTATUS
NTAPI
AmdGpioPrepareController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG Index;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);
    RtlZeroMemory(Context, sizeof(*Context));
    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);

        if (!Resource || Resource->Type != CmResourceTypeMemory)
            continue;
        if (Resource->u.Memory.Length <= AMDGPIO_STATUS_HIGH)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Context->Base = MmMapIoSpace(Resource->u.Memory.Start, Resource->u.Memory.Length, MmNonCached);
        if (!Context->Base)
            return STATUS_INSUFFICIENT_RESOURCES;
        Context->Length = Resource->u.Memory.Length;
        Context->TotalPins = min(Context->Length / sizeof(ULONG), AMDGPIO_PINS);
        DPRINT1("AMDGPIO: controller at %I64x, %lu pins\n", Resource->u.Memory.Start.QuadPart, Context->TotalPins);
        return STATUS_SUCCESS;
    }
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
NTSTATUS
NTAPI
AmdGpioReleaseController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(Device);
    if (Context->Base)
        MmUnmapIoSpace(Context->Base, Context->Length);
    RtlZeroMemory(Context, sizeof(*Context));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioStartController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN RestoreContext,
    _In_ WDF_POWER_DEVICE_STATE PreviousPowerState)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(PreviousPowerState);
    if (!RestoreContext)
        RtlZeroMemory(Context->Enabled, sizeof(Context->Enabled));
    AmdGpioQuiesce(Context, RestoreContext);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioStopController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN SaveContext,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(SaveContext);
    UNREFERENCED_PARAMETER(TargetState);
    AmdGpioQuiesce(ContextPointer, FALSE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioQueryControllerBasicInformation(
    _In_ PVOID ContextPointer,
    _Out_ PCLIENT_CONTROLLER_BASIC_INFORMATION Information)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;

    Information->Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Information->Size = sizeof(*Information);
    Information->TotalPins = (USHORT)Context->TotalPins;
    Information->NumberOfPinsPerBank = AMDGPIO_PINS_PER_BANK;
    Information->Flags.MemoryMappedController = TRUE;
    Information->Flags.ActiveInterruptsAutoClearOnRead = FALSE;
    Information->Flags.FormatIoRequestsAsMasks = TRUE;
    Information->Flags.DeviceIdlePowerMgmtSupported = FALSE;
    Information->Flags.BankIdlePowerMgmtSupported = FALSE;
    Information->Flags.EmulateDebouncing = TRUE;
    Information->Flags.EmulateActiveBoth = FALSE;
    Information->Flags.IndependentIoHwSupported = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioQuerySetControllerInformation(
    _In_ PVOID ContextPointer,
    _In_ PCLIENT_CONTROLLER_QUERY_SET_INFORMATION_INPUT Input,
    _Out_opt_ PCLIENT_CONTROLLER_QUERY_SET_INFORMATION_OUTPUT Output)
{
    ULONG RequiredSize;
    ULONG InterruptIndex = MAXULONG;
    ULONG Index;

    UNREFERENCED_PARAMETER(ContextPointer);
    if (!Input || !Output || Input->RequestType != QueryBankInterruptBindingInformation)
        return STATUS_NOT_SUPPORTED;
    RequiredSize = FIELD_OFFSET(CLIENT_CONTROLLER_QUERY_SET_INFORMATION_OUTPUT, BankInterruptBinding.ResourceMapping) +
                   Input->BankInterruptBinding.TotalBanks * sizeof(ULONG);
    if (Output->Size < RequiredSize)
    {
        Output->Size = (USHORT)RequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }
    for (Index = 0; Index < WdfCmResourceListGetCount(Input->BankInterruptBinding.ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource =
            WdfCmResourceListGetDescriptor(Input->BankInterruptBinding.ResourcesTranslated, Index);

        if (Resource && Resource->Type == CmResourceTypeInterrupt)
        {
            InterruptIndex = Index;
            break;
        }
    }
    if (InterruptIndex == MAXULONG)
        return STATUS_NOT_FOUND;
    Output->Version = GPIO_BANK_INTERRUPT_BINDING_INFORMATION_OUTPUT_VERSION;
    Output->Size = (USHORT)RequiredSize;
    for (Index = 0; Index < Input->BankInterruptBinding.TotalBanks; Index++)
        Output->BankInterruptBinding.ResourceMapping[Index] = InterruptIndex;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioEnableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG Offset;
    ULONG Value;
    ULONG Poll;

    if (!AmdGpioResolvePin(Context, Parameters->BankId, Parameters->PinNumber, &Offset))
        return STATUS_INVALID_PARAMETER;
    if (Parameters->InterruptMode == LevelSensitive && Parameters->Polarity == InterruptActiveBoth)
        return STATUS_NOT_SUPPORTED;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, Parameters->BankId);
    Value = AmdGpioReadPin(Context, Offset);
    Value &= ~(AMDGPIO_LEVEL_TRIGGER | AMDGPIO_ACTIVE_MASK | AMDGPIO_OUTPUT_ENABLE |
               AMDGPIO_INTERRUPT_ENABLE | AMDGPIO_INTERRUPT_DELIVERY);
    if (Parameters->InterruptMode == LevelSensitive)
        Value |= AMDGPIO_LEVEL_TRIGGER;
    if (Parameters->Polarity == InterruptActiveLow)
        Value |= AMDGPIO_ACTIVE_LOW;
    else if (Parameters->Polarity == InterruptActiveBoth)
        Value |= AMDGPIO_ACTIVE_BOTH;
    else
        Value |= AMDGPIO_ACTIVE_HIGH;
    if (Parameters->PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_PULLUP)
        Value = (Value & ~AMDGPIO_PULL_DOWN) | AMDGPIO_PULL_UP;
    else if (Parameters->PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_PULLDOWN)
        Value = (Value & ~AMDGPIO_PULL_UP) | AMDGPIO_PULL_DOWN;
    else if (Parameters->PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_NONE)
        Value &= ~(AMDGPIO_PULL_UP | AMDGPIO_PULL_DOWN);

    AmdGpioWrite(Context, Offset, Value | AMDGPIO_INTERRUPT_ENABLE | AMDGPIO_INTERRUPT_STATUS);
    for (Poll = 0; Poll < AMDGPIO_SETTLE_POLLS; Poll++)
    {
        if (AmdGpioRead(Context, Offset) & AMDGPIO_INTERRUPT_ENABLE)
            break;
        KeStallExecutionProcessor(1);
    }
    AmdGpioWrite(Context, Offset, Value | AMDGPIO_INTERRUPT_ENABLE | AMDGPIO_INTERRUPT_DELIVERY);
    Context->Enabled[Parameters->BankId] |= 1ULL << Parameters->PinNumber;
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Parameters->BankId);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioDisableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISABLE_INTERRUPT_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG Offset;
    ULONG Value;

    if (!AmdGpioResolvePin(Context, Parameters->BankId, Parameters->PinNumber, &Offset))
        return STATUS_INVALID_PARAMETER;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, Parameters->BankId);
    Value = AmdGpioReadPin(Context, Offset) & ~(AMDGPIO_INTERRUPT_ENABLE | AMDGPIO_INTERRUPT_DELIVERY);
    AmdGpioWrite(Context, Offset, Value | AMDGPIO_INTERRUPT_STATUS);
    Context->Enabled[Parameters->BankId] &= ~(1ULL << Parameters->PinNumber);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Parameters->BankId);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioUnmaskInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG Offset;

    if (!AmdGpioResolvePin(Context, Parameters->BankId, Parameters->PinNumber, &Offset))
        return STATUS_INVALID_PARAMETER;
    if (Context->Enabled[Parameters->BankId] & (1ULL << Parameters->PinNumber))
        AmdGpioWrite(Context, Offset, AmdGpioReadPin(Context, Offset) | AMDGPIO_INTERRUPT_DELIVERY);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioMaskInterrupts(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_MASK_INTERRUPT_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Pending = Parameters->PinMask;
    ULONG Offset;
    ULONG Pin;

    Parameters->FailedMask = 0;
    while (Pending)
    {
        Pin = (ULONG)__builtin_ctzll(Pending);
        Pending &= Pending - 1;
        if (!AmdGpioResolvePin(Context, Parameters->BankId, Pin, &Offset))
        {
            Parameters->FailedMask |= 1ULL << Pin;
            continue;
        }
        AmdGpioWrite(Context, Offset, AmdGpioReadPin(Context, Offset) & ~AMDGPIO_INTERRUPT_DELIVERY);
    }
    return Parameters->FailedMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioQueryActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Summary;
    ULONG64 Pending;
    ULONG Offset;
    ULONG Pin;
    ULONG Group;
    ULONG Value;

    Parameters->ActiveMask = 0;
    Summary = ((ULONG64)AmdGpioRead(Context, AMDGPIO_STATUS_HIGH) << 32) | AmdGpioRead(Context, AMDGPIO_STATUS_LOW);
    Pending = Parameters->EnabledMask;
    while (Pending)
    {
        Pin = (ULONG)__builtin_ctzll(Pending);
        Pending &= Pending - 1;
        if (!AmdGpioResolvePin(Context, Parameters->BankId, Pin, &Offset))
            continue;
        Group = (Offset / sizeof(ULONG)) / 4;
        if (!(Summary & (1ULL << Group)))
            continue;
        Value = AmdGpioRead(Context, Offset);
        if ((Value & AMDGPIO_STATUS_BITS) && (Value & AMDGPIO_INTERRUPT_DELIVERY))
            Parameters->ActiveMask |= 1ULL << Pin;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioClearActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Pending = Parameters->ClearActiveMask;
    ULONG Offset;
    ULONG Pin;

    Parameters->FailedClearMask = 0;
    while (Pending)
    {
        Pin = (ULONG)__builtin_ctzll(Pending);
        Pending &= Pending - 1;
        if (!AmdGpioResolvePin(Context, Parameters->BankId, Pin, &Offset))
        {
            Parameters->FailedClearMask |= 1ULL << Pin;
            continue;
        }
        AmdGpioWrite(Context, Offset, AmdGpioRead(Context, Offset));
    }
    AmdGpioWrite(Context, AMDGPIO_MASTER, AmdGpioRead(Context, AMDGPIO_MASTER) | AMDGPIO_MASTER_EOI);
    return Parameters->FailedClearMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioQueryEnabledInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ENABLED_INTERRUPTS_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;

    Parameters->EnabledMask = 0;
    if ((ULONG)Parameters->BankId * AMDGPIO_PINS_PER_BANK >= Context->TotalPins)
        return STATUS_INVALID_PARAMETER;
    Parameters->EnabledMask = Context->Enabled[Parameters->BankId];
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioConnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_CONNECT_IO_PINS_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG Offset;
    ULONG Value;
    USHORT Index;

    if (Parameters->ConnectMode != ConnectModeInput && Parameters->ConnectMode != ConnectModeOutput)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Parameters->PinCount; Index++)
    {
        if (!AmdGpioResolvePin(Context, Parameters->BankId, Parameters->PinNumberTable[Index], &Offset))
            return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < Parameters->PinCount; Index++)
    {
        AmdGpioResolvePin(Context, Parameters->BankId, Parameters->PinNumberTable[Index], &Offset);
        Value = AmdGpioReadPin(Context, Offset);
        if (Parameters->PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_PULLUP)
            Value = (Value & ~AMDGPIO_PULL_DOWN) | AMDGPIO_PULL_UP;
        else if (Parameters->PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_PULLDOWN)
            Value = (Value & ~AMDGPIO_PULL_UP) | AMDGPIO_PULL_DOWN;
        else if (Parameters->PullConfiguration == GPIO_PIN_PULL_CONFIGURATION_NONE)
            Value &= ~(AMDGPIO_PULL_UP | AMDGPIO_PULL_DOWN);
        if (Parameters->ConnectMode == ConnectModeOutput)
            Value |= AMDGPIO_OUTPUT_ENABLE;
        else
            Value &= ~AMDGPIO_OUTPUT_ENABLE;
        AmdGpioWrite(Context, Offset, Value);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioDisconnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISCONNECT_IO_PINS_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG Offset;
    USHORT Index;

    if (Parameters->DisconnectFlags.PreserveConfiguration || Parameters->DisconnectMode != ConnectModeOutput)
        return STATUS_SUCCESS;
    for (Index = 0; Index < Parameters->PinCount; Index++)
    {
        if (!AmdGpioResolvePin(Context, Parameters->BankId, Parameters->PinNumberTable[Index], &Offset))
            return STATUS_INVALID_PARAMETER;
        AmdGpioWrite(Context, Offset, AmdGpioReadPin(Context, Offset) & ~AMDGPIO_OUTPUT_ENABLE);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioReadPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_READ_PINS_MASK_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Values = 0;
    ULONG Offset;
    ULONG Pin;

    for (Pin = 0; Pin < AMDGPIO_PINS_PER_BANK; Pin++)
    {
        if (AmdGpioResolvePin(Context, Parameters->BankId, Pin, &Offset) &&
            (AmdGpioRead(Context, Offset) & AMDGPIO_PIN_STATE))
        {
            Values |= 1ULL << Pin;
        }
    }
    *Parameters->PinValues = Values;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioWritePins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_WRITE_PINS_MASK_PARAMETERS Parameters)
{
    PAMDGPIO_CONTEXT Context = ContextPointer;
    ULONG64 Pending = Parameters->SetMask | Parameters->ClearMask;
    ULONG Offset;
    ULONG Value;
    ULONG Pin;

    while (Pending)
    {
        Pin = (ULONG)__builtin_ctzll(Pending);
        Pending &= Pending - 1;
        if (!AmdGpioResolvePin(Context, Parameters->BankId, Pin, &Offset))
            return STATUS_INVALID_PARAMETER;
        Value = AmdGpioReadPin(Context, Offset);
        if (Parameters->SetMask & (1ULL << Pin))
            Value |= AMDGPIO_OUTPUT_VALUE;
        else
            Value &= ~AMDGPIO_OUTPUT_VALUE;
        AmdGpioWrite(Context, Offset, Value);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AmdGpioEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFDEVICE Device;
    NTSTATUS Status;

    Status = GPIO_CLX_ProcessAddDevicePreDeviceCreate(Driver, DeviceInit, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    return GPIO_CLX_ProcessAddDevicePostDeviceCreate(Driver, Device);
}

static
VOID
NTAPI
AmdGpioEvtDriverUnload(
    _In_ WDFDRIVER Driver)
{
    GPIO_CLX_UnregisterClient(Driver);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    GPIO_CLIENT_REGISTRATION_PACKET Packet;
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, AmdGpioEvtDeviceAdd);
    Config.DriverPoolTag = AMDGPIO_TAG;
    Config.EvtDriverUnload = AmdGpioEvtDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Version = GPIO_CLIENT_VERSION;
    Packet.Size = sizeof(Packet);
    Packet.Flags = GPIO_CLIENT_REGISTRATION_FLAGS_NONE;
    Packet.ControllerContextSize = sizeof(AMDGPIO_CONTEXT);
    Packet.CLIENT_PrepareController = AmdGpioPrepareController;
    Packet.CLIENT_ReleaseController = AmdGpioReleaseController;
    Packet.CLIENT_StartController = AmdGpioStartController;
    Packet.CLIENT_StopController = AmdGpioStopController;
    Packet.CLIENT_QueryControllerBasicInformation = AmdGpioQueryControllerBasicInformation;
    Packet.CLIENT_QuerySetControllerInformation = AmdGpioQuerySetControllerInformation;
    Packet.CLIENT_EnableInterrupt = AmdGpioEnableInterrupt;
    Packet.CLIENT_DisableInterrupt = AmdGpioDisableInterrupt;
    Packet.CLIENT_UnmaskInterrupt = AmdGpioUnmaskInterrupt;
    Packet.CLIENT_MaskInterrupts = AmdGpioMaskInterrupts;
    Packet.CLIENT_QueryActiveInterrupts = AmdGpioQueryActiveInterrupts;
    Packet.CLIENT_ClearActiveInterrupts = AmdGpioClearActiveInterrupts;
    Packet.CLIENT_QueryEnabledInterrupts = AmdGpioQueryEnabledInterrupts;
    Packet.CLIENT_ConnectIoPins = AmdGpioConnectIoPins;
    Packet.CLIENT_DisconnectIoPins = AmdGpioDisconnectIoPins;
    Packet.CLIENT_ReadGpioPinsUsingMask = AmdGpioReadPins;
    Packet.CLIENT_WriteGpioPinsUsingMask = AmdGpioWritePins;
    return GPIO_CLX_RegisterClient(Driver, &Packet, RegistryPath);
}
