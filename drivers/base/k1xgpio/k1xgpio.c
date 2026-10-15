/*
 * PROJECT:     LiberNT SpacemiT K1 GPIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GpioClx client for the SpacemiT K1 GPIO controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <gpioclx.h>

#define NDEBUG
#include <debug.h>

#define K1XGPIO_TAG             'pGiK'
#define K1XGPIO_BANKS           4
#define K1XGPIO_PINS_PER_BANK   32
#define K1XGPIO_REGISTER_SPAN   0x1AC

#define K1XGPIO_GPLR            0x00
#define K1XGPIO_GPSR            0x18
#define K1XGPIO_GPCR            0x24
#define K1XGPIO_GEDR            0x48
#define K1XGPIO_GSDR            0x54
#define K1XGPIO_GCDR            0x60
#define K1XGPIO_GSRER           0x6C
#define K1XGPIO_GCRER           0x78
#define K1XGPIO_GSFER           0x84
#define K1XGPIO_GCFER           0x90
#define K1XGPIO_GAPMASK         0x9C

static const ULONG K1xGpioBankOffset[K1XGPIO_BANKS] = { 0x000, 0x004, 0x008, 0x100 };

typedef struct _K1XGPIO_CONTEXT
{
    PUCHAR Base;
    ULONG Length;
    ULONG Enabled[K1XGPIO_BANKS];
    ULONG Rising[K1XGPIO_BANKS];
    ULONG Falling[K1XGPIO_BANKS];
} K1XGPIO_CONTEXT, *PK1XGPIO_CONTEXT;

static
ULONG
K1xGpioRead(
    _In_ PK1XGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)(Context->Base + K1xGpioBankOffset[Bank] + Register));
}

static
VOID
K1xGpioWrite(
    _In_ PK1XGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->Base + K1xGpioBankOffset[Bank] + Register), Value);
}

static
VOID
K1xGpioApplyEdges(
    _In_ PK1XGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Mask)
{
    ULONG Rising = Context->Rising[Bank] & Mask;
    ULONG Falling = Context->Falling[Bank] & Mask;

    if (Mask & ~Rising)
        K1xGpioWrite(Context, Bank, K1XGPIO_GCRER, Mask & ~Rising);
    if (Mask & ~Falling)
        K1xGpioWrite(Context, Bank, K1XGPIO_GCFER, Mask & ~Falling);
    if (Rising)
        K1xGpioWrite(Context, Bank, K1XGPIO_GSRER, Rising);
    if (Falling)
        K1xGpioWrite(Context, Bank, K1XGPIO_GSFER, Falling);
}

static
VOID
K1xGpioQuiesce(
    _In_ PK1XGPIO_CONTEXT Context)
{
    BANK_ID Bank;

    for (Bank = 0; Bank < K1XGPIO_BANKS; Bank++)
    {
        K1xGpioWrite(Context, Bank, K1XGPIO_GCRER, MAXULONG);
        K1xGpioWrite(Context, Bank, K1XGPIO_GCFER, MAXULONG);
        K1xGpioWrite(Context, Bank, K1XGPIO_GEDR, MAXULONG);
    }
}

static
NTSTATUS
NTAPI
K1xGpioPrepareController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    ULONG Index;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);
    RtlZeroMemory(Context, sizeof(*Context));
    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);

        if (!Resource || Resource->Type != CmResourceTypeMemory)
            continue;
        if (Resource->u.Memory.Length < K1XGPIO_REGISTER_SPAN)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Context->Base = MmMapIoSpace(Resource->u.Memory.Start, Resource->u.Memory.Length, MmNonCached);
        if (!Context->Base)
            return STATUS_INSUFFICIENT_RESOURCES;
        Context->Length = Resource->u.Memory.Length;
        DPRINT1("K1XGPIO: controller at %I64x, %u pins\n",
                Resource->u.Memory.Start.QuadPart, K1XGPIO_BANKS * K1XGPIO_PINS_PER_BANK);
        return STATUS_SUCCESS;
    }
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
NTSTATUS
NTAPI
K1xGpioReleaseController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(Device);
    if (Context->Base)
    {
        K1xGpioQuiesce(Context);
        MmUnmapIoSpace(Context->Base, Context->Length);
    }
    RtlZeroMemory(Context, sizeof(*Context));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioStartController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN RestoreContext,
    _In_ WDF_POWER_DEVICE_STATE PreviousPowerState)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    BANK_ID Bank;

    UNREFERENCED_PARAMETER(PreviousPowerState);
    if (!RestoreContext)
    {
        RtlZeroMemory(Context->Enabled, sizeof(Context->Enabled));
        RtlZeroMemory(Context->Rising, sizeof(Context->Rising));
        RtlZeroMemory(Context->Falling, sizeof(Context->Falling));
    }
    K1xGpioQuiesce(Context);
    for (Bank = 0; Bank < K1XGPIO_BANKS; Bank++)
    {
        K1xGpioWrite(Context, Bank, K1XGPIO_GAPMASK, MAXULONG);
        if (Context->Enabled[Bank])
            K1xGpioApplyEdges(Context, Bank, Context->Enabled[Bank]);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioStopController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN SaveContext,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(SaveContext);
    UNREFERENCED_PARAMETER(TargetState);
    K1xGpioQuiesce(ContextPointer);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioQueryControllerBasicInformation(
    _In_ PVOID ContextPointer,
    _Out_ PCLIENT_CONTROLLER_BASIC_INFORMATION Information)
{
    UNREFERENCED_PARAMETER(ContextPointer);
    Information->Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Information->Size = sizeof(*Information);
    Information->TotalPins = K1XGPIO_BANKS * K1XGPIO_PINS_PER_BANK;
    Information->NumberOfPinsPerBank = K1XGPIO_PINS_PER_BANK;
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
K1xGpioQuerySetControllerInformation(
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
K1xGpioEnableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    BANK_ID Bank = Parameters->BankId;
    ULONG Bit;

    if (Bank >= K1XGPIO_BANKS || Parameters->PinNumber >= K1XGPIO_PINS_PER_BANK)
        return STATUS_INVALID_PARAMETER;
    if (Parameters->InterruptMode != Latched)
        return STATUS_NOT_SUPPORTED;
    Bit = 1UL << Parameters->PinNumber;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, Bank);
    K1xGpioWrite(Context, Bank, K1XGPIO_GCDR, Bit);
    if (Parameters->Polarity == InterruptActiveHigh || Parameters->Polarity == InterruptActiveBoth)
        Context->Rising[Bank] |= Bit;
    else
        Context->Rising[Bank] &= ~Bit;
    if (Parameters->Polarity == InterruptActiveLow || Parameters->Polarity == InterruptActiveBoth)
        Context->Falling[Bank] |= Bit;
    else
        Context->Falling[Bank] &= ~Bit;
    K1xGpioWrite(Context, Bank, K1XGPIO_GEDR, Bit);
    Context->Enabled[Bank] |= Bit;
    K1xGpioApplyEdges(Context, Bank, Bit);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Bank);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioDisableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISABLE_INTERRUPT_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    BANK_ID Bank = Parameters->BankId;
    ULONG Bit;

    if (Bank >= K1XGPIO_BANKS || Parameters->PinNumber >= K1XGPIO_PINS_PER_BANK)
        return STATUS_INVALID_PARAMETER;
    Bit = 1UL << Parameters->PinNumber;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, Bank);
    Context->Enabled[Bank] &= ~Bit;
    Context->Rising[Bank] &= ~Bit;
    Context->Falling[Bank] &= ~Bit;
    K1xGpioApplyEdges(Context, Bank, Bit);
    K1xGpioWrite(Context, Bank, K1XGPIO_GEDR, Bit);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Bank);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioUnmaskInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;

    if (Parameters->BankId >= K1XGPIO_BANKS || Parameters->PinNumber >= K1XGPIO_PINS_PER_BANK)
        return STATUS_INVALID_PARAMETER;
    K1xGpioApplyEdges(Context, Parameters->BankId, 1UL << Parameters->PinNumber);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioMaskInterrupts(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_MASK_INTERRUPT_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    ULONG Mask = (ULONG)Parameters->PinMask;

    Parameters->FailedMask = Parameters->PinMask & ~(ULONG64)MAXULONG;
    if (Parameters->BankId >= K1XGPIO_BANKS)
    {
        Parameters->FailedMask = Parameters->PinMask;
        return STATUS_INVALID_PARAMETER;
    }
    if (Mask)
    {
        K1xGpioWrite(Context, Parameters->BankId, K1XGPIO_GCRER, Mask);
        K1xGpioWrite(Context, Parameters->BankId, K1XGPIO_GCFER, Mask);
    }
    return Parameters->FailedMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioQueryActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;

    Parameters->ActiveMask = 0;
    if (Parameters->BankId >= K1XGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;
    Parameters->ActiveMask = K1xGpioRead(Context, Parameters->BankId, K1XGPIO_GEDR) & Parameters->EnabledMask;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioClearActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    ULONG Mask = (ULONG)Parameters->ClearActiveMask;

    Parameters->FailedClearMask = Parameters->ClearActiveMask & ~(ULONG64)MAXULONG;
    if (Parameters->BankId >= K1XGPIO_BANKS)
    {
        Parameters->FailedClearMask = Parameters->ClearActiveMask;
        return STATUS_INVALID_PARAMETER;
    }
    if (Mask)
        K1xGpioWrite(Context, Parameters->BankId, K1XGPIO_GEDR, Mask);
    return Parameters->FailedClearMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioQueryEnabledInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ENABLED_INTERRUPTS_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;

    Parameters->EnabledMask = 0;
    if (Parameters->BankId >= K1XGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;
    Parameters->EnabledMask = Context->Enabled[Parameters->BankId];
    return STATUS_SUCCESS;
}

static
NTSTATUS
K1xGpioPinMask(
    _In_ BANK_ID Bank,
    _In_reads_(PinCount) PPIN_NUMBER Pins,
    _In_ USHORT PinCount,
    _Out_ PULONG Mask)
{
    USHORT Index;

    *Mask = 0;
    if (Bank >= K1XGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < PinCount; Index++)
    {
        if (Pins[Index] >= K1XGPIO_PINS_PER_BANK)
            return STATUS_INVALID_PARAMETER;
        *Mask |= 1UL << Pins[Index];
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioConnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_CONNECT_IO_PINS_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    NTSTATUS Status;
    ULONG Mask;

    if (Parameters->ConnectMode != ConnectModeInput && Parameters->ConnectMode != ConnectModeOutput)
        return STATUS_INVALID_PARAMETER;
    Status = K1xGpioPinMask(Parameters->BankId, Parameters->PinNumberTable, Parameters->PinCount, &Mask);
    if (!NT_SUCCESS(Status))
        return Status;
    K1xGpioWrite(Context, Parameters->BankId,
                 Parameters->ConnectMode == ConnectModeOutput ? K1XGPIO_GSDR : K1XGPIO_GCDR, Mask);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioDisconnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISCONNECT_IO_PINS_PARAMETERS Parameters)
{
    PK1XGPIO_CONTEXT Context = ContextPointer;
    NTSTATUS Status;
    ULONG Mask;

    if (Parameters->DisconnectFlags.PreserveConfiguration || Parameters->DisconnectMode != ConnectModeOutput)
        return STATUS_SUCCESS;
    Status = K1xGpioPinMask(Parameters->BankId, Parameters->PinNumberTable, Parameters->PinCount, &Mask);
    if (NT_SUCCESS(Status))
        K1xGpioWrite(Context, Parameters->BankId, K1XGPIO_GCDR, Mask);
    return Status;
}

static
NTSTATUS
NTAPI
K1xGpioReadPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_READ_PINS_MASK_PARAMETERS Parameters)
{
    if (Parameters->BankId >= K1XGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;
    *Parameters->PinValues = K1xGpioRead(ContextPointer, Parameters->BankId, K1XGPIO_GPLR);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioWritePins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_WRITE_PINS_MASK_PARAMETERS Parameters)
{
    if (Parameters->BankId >= K1XGPIO_BANKS ||
        ((Parameters->SetMask | Parameters->ClearMask) & ~(ULONG64)MAXULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Parameters->SetMask)
        K1xGpioWrite(ContextPointer, Parameters->BankId, K1XGPIO_GPSR, (ULONG)Parameters->SetMask);
    if (Parameters->ClearMask)
        K1xGpioWrite(ContextPointer, Parameters->BankId, K1XGPIO_GPCR, (ULONG)Parameters->ClearMask);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
K1xGpioEvtDeviceAdd(
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
K1xGpioEvtDriverUnload(
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

    WDF_DRIVER_CONFIG_INIT(&Config, K1xGpioEvtDeviceAdd);
    Config.DriverPoolTag = K1XGPIO_TAG;
    Config.EvtDriverUnload = K1xGpioEvtDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Version = GPIO_CLIENT_VERSION;
    Packet.Size = sizeof(Packet);
    Packet.Flags = GPIO_CLIENT_REGISTRATION_FLAGS_NONE;
    Packet.ControllerContextSize = sizeof(K1XGPIO_CONTEXT);
    Packet.CLIENT_PrepareController = K1xGpioPrepareController;
    Packet.CLIENT_ReleaseController = K1xGpioReleaseController;
    Packet.CLIENT_StartController = K1xGpioStartController;
    Packet.CLIENT_StopController = K1xGpioStopController;
    Packet.CLIENT_QueryControllerBasicInformation = K1xGpioQueryControllerBasicInformation;
    Packet.CLIENT_QuerySetControllerInformation = K1xGpioQuerySetControllerInformation;
    Packet.CLIENT_EnableInterrupt = K1xGpioEnableInterrupt;
    Packet.CLIENT_DisableInterrupt = K1xGpioDisableInterrupt;
    Packet.CLIENT_UnmaskInterrupt = K1xGpioUnmaskInterrupt;
    Packet.CLIENT_MaskInterrupts = K1xGpioMaskInterrupts;
    Packet.CLIENT_QueryActiveInterrupts = K1xGpioQueryActiveInterrupts;
    Packet.CLIENT_ClearActiveInterrupts = K1xGpioClearActiveInterrupts;
    Packet.CLIENT_QueryEnabledInterrupts = K1xGpioQueryEnabledInterrupts;
    Packet.CLIENT_ConnectIoPins = K1xGpioConnectIoPins;
    Packet.CLIENT_DisconnectIoPins = K1xGpioDisconnectIoPins;
    Packet.CLIENT_ReadGpioPinsUsingMask = K1xGpioReadPins;
    Packet.CLIENT_WriteGpioPinsUsingMask = K1xGpioWritePins;
    return GPIO_CLX_RegisterClient(Driver, &Packet, RegistryPath);
}
