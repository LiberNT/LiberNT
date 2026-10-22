/*
 * PROJECT:     LiberNT Arm PL061 GPIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GpioClx client for the Arm PrimeCell PL061 GPIO controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <gpioclx.h>

#define NDEBUG
#include <debug.h>

#define PL061GPIO_TAG           'GlpP'
#define PL061GPIO_PINS          8
#define PL061GPIO_PIN_MASK      0xFF
#define PL061GPIO_REGISTER_SPAN 0x1000

#define PL061GPIO_DATA          0x000
#define PL061GPIO_DIR           0x400
#define PL061GPIO_IS            0x404
#define PL061GPIO_IBE           0x408
#define PL061GPIO_IEV           0x40C
#define PL061GPIO_IE            0x410
#define PL061GPIO_RIS           0x414
#define PL061GPIO_MIS           0x418
#define PL061GPIO_IC            0x41C

typedef struct _PL061GPIO_CONTEXT
{
    PUCHAR Base;
    ULONG Length;
    ULONG Enabled;
    ULONG Level;
    ULONG BothEdges;
    ULONG High;
    ULONG Direction;
} PL061GPIO_CONTEXT, *PPL061GPIO_CONTEXT;

static
ULONG
Pl061GpioRead(
    _In_ PPL061GPIO_CONTEXT Context,
    _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)(Context->Base + Register)) & PL061GPIO_PIN_MASK;
}

static
VOID
Pl061GpioWrite(
    _In_ PPL061GPIO_CONTEXT Context,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->Base + Register), Value & PL061GPIO_PIN_MASK);
}

static
VOID
Pl061GpioProgramTriggers(
    _In_ PPL061GPIO_CONTEXT Context)
{
    Pl061GpioWrite(Context, PL061GPIO_IS, Context->Level);
    Pl061GpioWrite(Context, PL061GPIO_IBE, Context->BothEdges);
    Pl061GpioWrite(Context, PL061GPIO_IEV, Context->High);
}

static
VOID
Pl061GpioQuiesce(
    _In_ PPL061GPIO_CONTEXT Context)
{
    Pl061GpioWrite(Context, PL061GPIO_IE, 0);
    Pl061GpioWrite(Context, PL061GPIO_IC, PL061GPIO_PIN_MASK);
}

static
NTSTATUS
NTAPI
Pl061GpioPrepareController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    ULONG Index;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);
    RtlZeroMemory(Context, sizeof(*Context));
    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);

        if (!Resource || Resource->Type != CmResourceTypeMemory)
            continue;
        if (Resource->u.Memory.Length < PL061GPIO_REGISTER_SPAN)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Context->Base = MmMapIoSpace(Resource->u.Memory.Start, Resource->u.Memory.Length, MmNonCached);
        if (!Context->Base)
            return STATUS_INSUFFICIENT_RESOURCES;
        Context->Length = Resource->u.Memory.Length;
        DPRINT1("PL061GPIO: controller at %I64x\n", Resource->u.Memory.Start.QuadPart);
        return STATUS_SUCCESS;
    }
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
NTSTATUS
NTAPI
Pl061GpioReleaseController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(Device);
    if (Context->Base)
    {
        Pl061GpioQuiesce(Context);
        MmUnmapIoSpace(Context->Base, Context->Length);
    }
    RtlZeroMemory(Context, sizeof(*Context));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioStartController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN RestoreContext,
    _In_ WDF_POWER_DEVICE_STATE PreviousPowerState)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(PreviousPowerState);
    if (!RestoreContext)
    {
        Context->Enabled = 0;
        Context->Level = 0;
        Context->BothEdges = 0;
        Context->High = 0;
        Context->Direction = Pl061GpioRead(Context, PL061GPIO_DIR);
    }
    Pl061GpioQuiesce(Context);
    Pl061GpioWrite(Context, PL061GPIO_DIR, Context->Direction);
    Pl061GpioProgramTriggers(Context);
    Pl061GpioWrite(Context, PL061GPIO_IE, Context->Enabled);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioStopController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN SaveContext,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(SaveContext);
    UNREFERENCED_PARAMETER(TargetState);
    Pl061GpioQuiesce(ContextPointer);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioQueryControllerBasicInformation(
    _In_ PVOID ContextPointer,
    _Out_ PCLIENT_CONTROLLER_BASIC_INFORMATION Information)
{
    UNREFERENCED_PARAMETER(ContextPointer);
    Information->Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Information->Size = sizeof(*Information);
    Information->TotalPins = PL061GPIO_PINS;
    Information->NumberOfPinsPerBank = PL061GPIO_PINS;
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
Pl061GpioQuerySetControllerInformation(
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
Pl061GpioEnableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    ULONG Bit;

    if (Parameters->BankId != 0 || Parameters->PinNumber >= PL061GPIO_PINS)
        return STATUS_INVALID_PARAMETER;
    if (Parameters->InterruptMode == LevelSensitive && Parameters->Polarity == InterruptActiveBoth)
        return STATUS_NOT_SUPPORTED;
    Bit = 1UL << Parameters->PinNumber;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, 0);
    Context->Direction &= ~Bit;
    Pl061GpioWrite(Context, PL061GPIO_DIR, Context->Direction);
    if (Parameters->InterruptMode == LevelSensitive)
        Context->Level |= Bit;
    else
        Context->Level &= ~Bit;
    if (Parameters->InterruptMode == Latched && Parameters->Polarity == InterruptActiveBoth)
        Context->BothEdges |= Bit;
    else
        Context->BothEdges &= ~Bit;
    if (Parameters->Polarity == InterruptActiveHigh)
        Context->High |= Bit;
    else
        Context->High &= ~Bit;
    Pl061GpioProgramTriggers(Context);
    Pl061GpioWrite(Context, PL061GPIO_IC, Bit);
    Context->Enabled |= Bit;
    Pl061GpioWrite(Context, PL061GPIO_IE, Context->Enabled);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, 0);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioDisableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISABLE_INTERRUPT_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    ULONG Bit;

    if (Parameters->BankId != 0 || Parameters->PinNumber >= PL061GPIO_PINS)
        return STATUS_INVALID_PARAMETER;
    Bit = 1UL << Parameters->PinNumber;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, 0);
    Context->Enabled &= ~Bit;
    Pl061GpioWrite(Context, PL061GPIO_IE, Context->Enabled);
    Pl061GpioWrite(Context, PL061GPIO_IC, Bit);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, 0);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioUnmaskInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    ULONG Bit;

    if (Parameters->BankId != 0 || Parameters->PinNumber >= PL061GPIO_PINS)
        return STATUS_INVALID_PARAMETER;
    Bit = 1UL << Parameters->PinNumber;
    Pl061GpioWrite(Context, PL061GPIO_IE, Pl061GpioRead(Context, PL061GPIO_IE) | (Context->Enabled & Bit));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioMaskInterrupts(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_MASK_INTERRUPT_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    ULONG Mask = (ULONG)Parameters->PinMask & PL061GPIO_PIN_MASK;

    Parameters->FailedMask = Parameters->PinMask & ~(ULONG64)PL061GPIO_PIN_MASK;
    if (Parameters->BankId != 0)
    {
        Parameters->FailedMask = Parameters->PinMask;
        return STATUS_INVALID_PARAMETER;
    }
    if (Mask)
        Pl061GpioWrite(Context, PL061GPIO_IE, Pl061GpioRead(Context, PL061GPIO_IE) & ~Mask);
    return Parameters->FailedMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioQueryActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;

    Parameters->ActiveMask = 0;
    if (Parameters->BankId != 0)
        return STATUS_INVALID_PARAMETER;
    Parameters->ActiveMask = Pl061GpioRead(Context, PL061GPIO_RIS) & Parameters->EnabledMask;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioClearActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    ULONG Mask = (ULONG)Parameters->ClearActiveMask & PL061GPIO_PIN_MASK;

    Parameters->FailedClearMask = Parameters->ClearActiveMask & ~(ULONG64)PL061GPIO_PIN_MASK;
    if (Parameters->BankId != 0)
    {
        Parameters->FailedClearMask = Parameters->ClearActiveMask;
        return STATUS_INVALID_PARAMETER;
    }
    if (Mask)
        Pl061GpioWrite(Context, PL061GPIO_IC, Mask);
    return Parameters->FailedClearMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioQueryEnabledInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ENABLED_INTERRUPTS_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;

    Parameters->EnabledMask = 0;
    if (Parameters->BankId != 0)
        return STATUS_INVALID_PARAMETER;
    Parameters->EnabledMask = Context->Enabled;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Pl061GpioPinMask(
    _In_ BANK_ID Bank,
    _In_reads_(PinCount) PPIN_NUMBER Pins,
    _In_ USHORT PinCount,
    _Out_ PULONG Mask)
{
    USHORT Index;

    *Mask = 0;
    if (Bank != 0)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < PinCount; Index++)
    {
        if (Pins[Index] >= PL061GPIO_PINS)
            return STATUS_INVALID_PARAMETER;
        *Mask |= 1UL << Pins[Index];
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioConnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_CONNECT_IO_PINS_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    NTSTATUS Status;
    ULONG Mask;

    if (Parameters->ConnectMode != ConnectModeInput && Parameters->ConnectMode != ConnectModeOutput)
        return STATUS_INVALID_PARAMETER;
    Status = Pl061GpioPinMask(Parameters->BankId, Parameters->PinNumberTable, Parameters->PinCount, &Mask);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Parameters->ConnectMode == ConnectModeOutput)
        Context->Direction |= Mask;
    else
        Context->Direction &= ~Mask;
    Pl061GpioWrite(Context, PL061GPIO_DIR, Context->Direction);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioDisconnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISCONNECT_IO_PINS_PARAMETERS Parameters)
{
    PPL061GPIO_CONTEXT Context = ContextPointer;
    NTSTATUS Status;
    ULONG Mask;

    if (Parameters->DisconnectFlags.PreserveConfiguration || Parameters->DisconnectMode != ConnectModeOutput)
        return STATUS_SUCCESS;
    Status = Pl061GpioPinMask(Parameters->BankId, Parameters->PinNumberTable, Parameters->PinCount, &Mask);
    if (NT_SUCCESS(Status))
    {
        Context->Direction &= ~Mask;
        Pl061GpioWrite(Context, PL061GPIO_DIR, Context->Direction);
    }
    return Status;
}

static
NTSTATUS
NTAPI
Pl061GpioReadPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_READ_PINS_MASK_PARAMETERS Parameters)
{
    if (Parameters->BankId != 0)
        return STATUS_INVALID_PARAMETER;
    *Parameters->PinValues = Pl061GpioRead(ContextPointer, PL061GPIO_DATA + (PL061GPIO_PIN_MASK << 2));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioWritePins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_WRITE_PINS_MASK_PARAMETERS Parameters)
{
    ULONG Mask;

    if (Parameters->BankId != 0 ||
        ((Parameters->SetMask | Parameters->ClearMask) & ~(ULONG64)PL061GPIO_PIN_MASK))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Mask = (ULONG)(Parameters->SetMask | Parameters->ClearMask);
    if (Mask)
        Pl061GpioWrite(ContextPointer, PL061GPIO_DATA + (Mask << 2), (ULONG)Parameters->SetMask);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
Pl061GpioEvtDeviceAdd(
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
Pl061GpioEvtDriverUnload(
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

    WDF_DRIVER_CONFIG_INIT(&Config, Pl061GpioEvtDeviceAdd);
    Config.DriverPoolTag = PL061GPIO_TAG;
    Config.EvtDriverUnload = Pl061GpioEvtDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Version = GPIO_CLIENT_VERSION;
    Packet.Size = sizeof(Packet);
    Packet.Flags = GPIO_CLIENT_REGISTRATION_FLAGS_NONE;
    Packet.ControllerContextSize = sizeof(PL061GPIO_CONTEXT);
    Packet.CLIENT_PrepareController = Pl061GpioPrepareController;
    Packet.CLIENT_ReleaseController = Pl061GpioReleaseController;
    Packet.CLIENT_StartController = Pl061GpioStartController;
    Packet.CLIENT_StopController = Pl061GpioStopController;
    Packet.CLIENT_QueryControllerBasicInformation = Pl061GpioQueryControllerBasicInformation;
    Packet.CLIENT_QuerySetControllerInformation = Pl061GpioQuerySetControllerInformation;
    Packet.CLIENT_EnableInterrupt = Pl061GpioEnableInterrupt;
    Packet.CLIENT_DisableInterrupt = Pl061GpioDisableInterrupt;
    Packet.CLIENT_UnmaskInterrupt = Pl061GpioUnmaskInterrupt;
    Packet.CLIENT_MaskInterrupts = Pl061GpioMaskInterrupts;
    Packet.CLIENT_QueryActiveInterrupts = Pl061GpioQueryActiveInterrupts;
    Packet.CLIENT_ClearActiveInterrupts = Pl061GpioClearActiveInterrupts;
    Packet.CLIENT_QueryEnabledInterrupts = Pl061GpioQueryEnabledInterrupts;
    Packet.CLIENT_ConnectIoPins = Pl061GpioConnectIoPins;
    Packet.CLIENT_DisconnectIoPins = Pl061GpioDisconnectIoPins;
    Packet.CLIENT_ReadGpioPinsUsingMask = Pl061GpioReadPins;
    Packet.CLIENT_WriteGpioPinsUsingMask = Pl061GpioWritePins;
    return GPIO_CLX_RegisterClient(Driver, &Packet, RegistryPath);
}
