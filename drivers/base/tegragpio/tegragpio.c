/*
 * PROJECT:     LiberNT Tegra GPIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GpioClx client for the Tegra210 GPIO controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <gpioclx.h>

#define NDEBUG
#include <debug.h>

#define TEGRAGPIO_TAG               'pGgT'

/*
 * 256 pins in 8 banks of 4 ports, each port holding 8 pins in the low byte of
 * its registers. A GpioClx bank is one controller bank, so pin p of a bank
 * lives in port p / 8 at bit p % 8.
 */
#define TEGRAGPIO_BANKS             8
#define TEGRAGPIO_PORTS_PER_BANK    4
#define TEGRAGPIO_PINS_PER_PORT     8
#define TEGRAGPIO_PINS_PER_BANK     (TEGRAGPIO_PORTS_PER_BANK * TEGRAGPIO_PINS_PER_PORT)
#define TEGRAGPIO_REGISTER_SPAN     (TEGRAGPIO_BANKS * 0x100)

#define TEGRAGPIO_CNF               0x00
#define TEGRAGPIO_OE                0x10
#define TEGRAGPIO_OUT               0x20
#define TEGRAGPIO_IN                0x30
#define TEGRAGPIO_INT_STA           0x40
#define TEGRAGPIO_INT_ENB           0x50
#define TEGRAGPIO_INT_LVL           0x60
#define TEGRAGPIO_INT_CLR           0x70
#define TEGRAGPIO_MSK_CNF           0x80
#define TEGRAGPIO_MSK_OE            0x90
#define TEGRAGPIO_MSK_OUT           0xA0
#define TEGRAGPIO_MSK_INT_ENB       0xD0

/* GPIO_INT_LVL holds three fields of one bit per pin. */
#define TEGRAGPIO_LVL_HIGH_SHIFT    0
#define TEGRAGPIO_LVL_EDGE_SHIFT    8
#define TEGRAGPIO_LVL_DELTA_SHIFT   16

#define TEGRAGPIO_PORT_OFFSET(Bank, Port) (((ULONG)(Bank) << 8) + ((ULONG)(Port) << 2))

typedef struct _TEGRAGPIO_CONTEXT
{
    PUCHAR Base;
    ULONG Length;
    ULONG BankInterrupt[TEGRAGPIO_BANKS];
    ULONG Enabled[TEGRAGPIO_BANKS];
} TEGRAGPIO_CONTEXT, *PTEGRAGPIO_CONTEXT;

static
ULONG
TegraGpioReadPort(
    _In_ PTEGRAGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Port,
    _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)(Context->Base + TEGRAGPIO_PORT_OFFSET(Bank, Port) + Register));
}

static
VOID
TegraGpioWritePort(
    _In_ PTEGRAGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Port,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->Base + TEGRAGPIO_PORT_OFFSET(Bank, Port) + Register), Value);
}

/* Gathers the eight pins of each port of a bank into one value. */
static
ULONG
TegraGpioReadBank(
    _In_ PTEGRAGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Register)
{
    ULONG Value = 0;
    ULONG Port;

    for (Port = 0; Port < TEGRAGPIO_PORTS_PER_BANK; Port++)
    {
        Value |= (TegraGpioReadPort(Context, Bank, Port, Register) & 0xFF) <<
                 (Port * TEGRAGPIO_PINS_PER_PORT);
    }

    return Value;
}

/*
 * Writes the pins the mask names through the register that carries its own
 * write mask in the next byte, so pins of the same port that belong to another
 * device are left alone without a read.
 */
static
VOID
TegraGpioWriteBankMasked(
    _In_ PTEGRAGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG MaskedRegister,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    ULONG Port;

    for (Port = 0; Port < TEGRAGPIO_PORTS_PER_BANK; Port++)
    {
        ULONG Shift = Port * TEGRAGPIO_PINS_PER_PORT;
        ULONG PortMask = (Mask >> Shift) & 0xFF;

        if (!PortMask)
            continue;

        TegraGpioWritePort(Context, Bank, Port, MaskedRegister,
                           (PortMask << 8) | ((Value >> Shift) & PortMask));
    }
}

static
VOID
TegraGpioWriteBank(
    _In_ PTEGRAGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Register,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    ULONG Port;

    for (Port = 0; Port < TEGRAGPIO_PORTS_PER_BANK; Port++)
    {
        ULONG Shift = Port * TEGRAGPIO_PINS_PER_PORT;
        ULONG PortMask = (Mask >> Shift) & 0xFF;

        if (!PortMask)
            continue;

        TegraGpioWritePort(Context, Bank, Port, Register, (Value >> Shift) & PortMask);
    }
}

/* GPIO_INT_LVL carries the three fields of all eight pins of a port at once. */
static
VOID
TegraGpioSetTrigger(
    _In_ PTEGRAGPIO_CONTEXT Context,
    _In_ BANK_ID Bank,
    _In_ ULONG Pin,
    _In_ KINTERRUPT_MODE Mode,
    _In_ KINTERRUPT_POLARITY Polarity)
{
    ULONG Port = Pin / TEGRAGPIO_PINS_PER_PORT;
    ULONG Bit = 1UL << (Pin % TEGRAGPIO_PINS_PER_PORT);
    ULONG Value = TegraGpioReadPort(Context, Bank, Port, TEGRAGPIO_INT_LVL);

    Value &= ~((Bit << TEGRAGPIO_LVL_HIGH_SHIFT) |
               (Bit << TEGRAGPIO_LVL_EDGE_SHIFT) |
               (Bit << TEGRAGPIO_LVL_DELTA_SHIFT));

    if (Mode == Latched)
        Value |= Bit << TEGRAGPIO_LVL_EDGE_SHIFT;

    if (Polarity == InterruptActiveBoth)
        Value |= Bit << TEGRAGPIO_LVL_DELTA_SHIFT;
    else if (Polarity != InterruptActiveLow)
        Value |= Bit << TEGRAGPIO_LVL_HIGH_SHIFT;

    TegraGpioWritePort(Context, Bank, Port, TEGRAGPIO_INT_LVL, Value);
}

static
VOID
TegraGpioQuiesce(
    _In_ PTEGRAGPIO_CONTEXT Context)
{
    BANK_ID Bank;

    for (Bank = 0; Bank < TEGRAGPIO_BANKS; Bank++)
    {
        TegraGpioWriteBankMasked(Context, Bank, TEGRAGPIO_MSK_INT_ENB, MAXULONG, 0);
        TegraGpioWriteBank(Context, Bank, TEGRAGPIO_INT_CLR, MAXULONG, MAXULONG);
    }
}

static
NTSTATUS
NTAPI
TegraGpioPrepareController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    ULONG Index;
    ULONG Interrupts = 0;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);
    RtlZeroMemory(Context, sizeof(*Context));

    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource =
            WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);

        if (!Resource)
            continue;

        if (Resource->Type == CmResourceTypeMemory && !Context->Base)
        {
            if (Resource->u.Memory.Length < TEGRAGPIO_REGISTER_SPAN)
                return STATUS_DEVICE_CONFIGURATION_ERROR;

            Context->Base = MmMapIoSpace(Resource->u.Memory.Start, Resource->u.Memory.Length,
                                         MmNonCached);
            if (!Context->Base)
                return STATUS_INSUFFICIENT_RESOURCES;

            Context->Length = Resource->u.Memory.Length;
        }
        else if (Resource->Type == CmResourceTypeInterrupt && Interrupts < TEGRAGPIO_BANKS)
        {
            /* One interrupt per bank, in bank order. */
            Context->BankInterrupt[Interrupts++] = Index;
        }
    }

    if (!Context->Base || !Interrupts)
    {
        if (Context->Base)
        {
            MmUnmapIoSpace(Context->Base, Context->Length);
            Context->Base = NULL;
        }

        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* A tree that gives one interrupt for the whole block shares it. */
    for (Index = Interrupts; Index < TEGRAGPIO_BANKS; Index++)
        Context->BankInterrupt[Index] = Context->BankInterrupt[Interrupts - 1];

    DPRINT1("TEGRAGPIO: controller with %u pins, %lu interrupt(s)\n",
            TEGRAGPIO_BANKS * TEGRAGPIO_PINS_PER_BANK, Interrupts);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioReleaseController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(Device);

    if (Context->Base)
    {
        TegraGpioQuiesce(Context);
        MmUnmapIoSpace(Context->Base, Context->Length);
    }

    RtlZeroMemory(Context, sizeof(*Context));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioStartController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN RestoreContext,
    _In_ WDF_POWER_DEVICE_STATE PreviousPowerState)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    BANK_ID Bank;

    UNREFERENCED_PARAMETER(PreviousPowerState);

    if (!RestoreContext)
        RtlZeroMemory(Context->Enabled, sizeof(Context->Enabled));

    TegraGpioQuiesce(Context);

    for (Bank = 0; Bank < TEGRAGPIO_BANKS; Bank++)
    {
        if (Context->Enabled[Bank])
        {
            TegraGpioWriteBankMasked(Context, Bank, TEGRAGPIO_MSK_INT_ENB,
                                     Context->Enabled[Bank], Context->Enabled[Bank]);
        }
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioStopController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN SaveContext,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(SaveContext);
    UNREFERENCED_PARAMETER(TargetState);
    TegraGpioQuiesce(ContextPointer);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioQueryControllerBasicInformation(
    _In_ PVOID ContextPointer,
    _Out_ PCLIENT_CONTROLLER_BASIC_INFORMATION Information)
{
    UNREFERENCED_PARAMETER(ContextPointer);
    Information->Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Information->Size = sizeof(*Information);
    Information->TotalPins = TEGRAGPIO_BANKS * TEGRAGPIO_PINS_PER_BANK;
    Information->NumberOfPinsPerBank = TEGRAGPIO_PINS_PER_BANK;
    Information->Flags.MemoryMappedController = TRUE;
    Information->Flags.ActiveInterruptsAutoClearOnRead = FALSE;
    Information->Flags.FormatIoRequestsAsMasks = TRUE;
    Information->Flags.DeviceIdlePowerMgmtSupported = FALSE;
    Information->Flags.BankIdlePowerMgmtSupported = FALSE;
    Information->Flags.EmulateDebouncing = TRUE;
    Information->Flags.EmulateActiveBoth = FALSE;
    Information->Flags.IndependentIoHwSupported = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioQuerySetControllerInformation(
    _In_ PVOID ContextPointer,
    _In_ PCLIENT_CONTROLLER_QUERY_SET_INFORMATION_INPUT Input,
    _Out_opt_ PCLIENT_CONTROLLER_QUERY_SET_INFORMATION_OUTPUT Output)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    ULONG RequiredSize;
    ULONG Index;

    if (!Input || !Output || Input->RequestType != QueryBankInterruptBindingInformation)
        return STATUS_NOT_SUPPORTED;

    RequiredSize = FIELD_OFFSET(CLIENT_CONTROLLER_QUERY_SET_INFORMATION_OUTPUT,
                                BankInterruptBinding.ResourceMapping) +
                   Input->BankInterruptBinding.TotalBanks * sizeof(ULONG);
    if (Output->Size < RequiredSize)
    {
        Output->Size = (USHORT)RequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Output->Version = GPIO_BANK_INTERRUPT_BINDING_INFORMATION_OUTPUT_VERSION;
    Output->Size = (USHORT)RequiredSize;
    for (Index = 0; Index < Input->BankInterruptBinding.TotalBanks; Index++)
    {
        Output->BankInterruptBinding.ResourceMapping[Index] =
            Context->BankInterrupt[Index < TEGRAGPIO_BANKS ? Index : TEGRAGPIO_BANKS - 1];
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioEnableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    BANK_ID Bank = Parameters->BankId;
    ULONG Bit;

    if (Bank >= TEGRAGPIO_BANKS || Parameters->PinNumber >= TEGRAGPIO_PINS_PER_BANK)
        return STATUS_INVALID_PARAMETER;

    Bit = 1UL << Parameters->PinNumber;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, Bank);
    TegraGpioWriteBankMasked(Context, Bank, TEGRAGPIO_MSK_CNF, Bit, Bit);
    TegraGpioWriteBankMasked(Context, Bank, TEGRAGPIO_MSK_OE, Bit, 0);
    TegraGpioSetTrigger(Context, Bank, Parameters->PinNumber,
                        Parameters->InterruptMode, Parameters->Polarity);
    TegraGpioWriteBank(Context, Bank, TEGRAGPIO_INT_CLR, Bit, Bit);
    Context->Enabled[Bank] |= Bit;
    TegraGpioWriteBankMasked(Context, Bank, TEGRAGPIO_MSK_INT_ENB, Bit, Bit);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Bank);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioDisableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISABLE_INTERRUPT_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    BANK_ID Bank = Parameters->BankId;
    ULONG Bit;

    if (Bank >= TEGRAGPIO_BANKS || Parameters->PinNumber >= TEGRAGPIO_PINS_PER_BANK)
        return STATUS_INVALID_PARAMETER;

    Bit = 1UL << Parameters->PinNumber;

    GPIO_CLX_AcquireInterruptLock(ContextPointer, Bank);
    Context->Enabled[Bank] &= ~Bit;
    TegraGpioWriteBankMasked(Context, Bank, TEGRAGPIO_MSK_INT_ENB, Bit, 0);
    TegraGpioWriteBank(Context, Bank, TEGRAGPIO_INT_CLR, Bit, Bit);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Bank);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioUnmaskInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    ULONG Bit;

    if (Parameters->BankId >= TEGRAGPIO_BANKS ||
        Parameters->PinNumber >= TEGRAGPIO_PINS_PER_BANK)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Bit = 1UL << Parameters->PinNumber;
    TegraGpioSetTrigger(Context, Parameters->BankId, Parameters->PinNumber,
                        Parameters->InterruptMode, Parameters->Polarity);
    TegraGpioWriteBankMasked(Context, Parameters->BankId, TEGRAGPIO_MSK_INT_ENB, Bit, Bit);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioMaskInterrupts(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_MASK_INTERRUPT_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    ULONG Mask = (ULONG)Parameters->PinMask;

    Parameters->FailedMask = Parameters->PinMask & ~(ULONG64)MAXULONG;
    if (Parameters->BankId >= TEGRAGPIO_BANKS)
    {
        Parameters->FailedMask = Parameters->PinMask;
        return STATUS_INVALID_PARAMETER;
    }

    if (Mask)
        TegraGpioWriteBankMasked(Context, Parameters->BankId, TEGRAGPIO_MSK_INT_ENB, Mask, 0);

    return Parameters->FailedMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioQueryActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;

    Parameters->ActiveMask = 0;
    if (Parameters->BankId >= TEGRAGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;

    Parameters->ActiveMask = TegraGpioReadBank(Context, Parameters->BankId, TEGRAGPIO_INT_STA) &
                             (ULONG)Parameters->EnabledMask;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioClearActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    ULONG Mask = (ULONG)Parameters->ClearActiveMask;

    Parameters->FailedClearMask = Parameters->ClearActiveMask & ~(ULONG64)MAXULONG;
    if (Parameters->BankId >= TEGRAGPIO_BANKS)
    {
        Parameters->FailedClearMask = Parameters->ClearActiveMask;
        return STATUS_INVALID_PARAMETER;
    }

    if (Mask)
        TegraGpioWriteBank(Context, Parameters->BankId, TEGRAGPIO_INT_CLR, Mask, Mask);

    return Parameters->FailedClearMask ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioQueryEnabledInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ENABLED_INTERRUPTS_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;

    Parameters->EnabledMask = 0;
    if (Parameters->BankId >= TEGRAGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;

    Parameters->EnabledMask = Context->Enabled[Parameters->BankId];
    return STATUS_SUCCESS;
}

static
NTSTATUS
TegraGpioPinMask(
    _In_ BANK_ID Bank,
    _In_reads_(PinCount) PPIN_NUMBER Pins,
    _In_ USHORT PinCount,
    _Out_ PULONG Mask)
{
    USHORT Index;

    *Mask = 0;
    if (Bank >= TEGRAGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < PinCount; Index++)
    {
        if (Pins[Index] >= TEGRAGPIO_PINS_PER_BANK)
            return STATUS_INVALID_PARAMETER;

        *Mask |= 1UL << Pins[Index];
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioConnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_CONNECT_IO_PINS_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    NTSTATUS Status;
    ULONG Mask;

    if (Parameters->ConnectMode != ConnectModeInput && Parameters->ConnectMode != ConnectModeOutput)
        return STATUS_INVALID_PARAMETER;

    Status = TegraGpioPinMask(Parameters->BankId, Parameters->PinNumberTable,
                              Parameters->PinCount, &Mask);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Take the pins off whatever function the firmware left them on. */
    TegraGpioWriteBankMasked(Context, Parameters->BankId, TEGRAGPIO_MSK_CNF, Mask, Mask);
    TegraGpioWriteBankMasked(Context, Parameters->BankId, TEGRAGPIO_MSK_OE, Mask,
                             Parameters->ConnectMode == ConnectModeOutput ? Mask : 0);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioDisconnectIoPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISCONNECT_IO_PINS_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    NTSTATUS Status;
    ULONG Mask;

    if (Parameters->DisconnectFlags.PreserveConfiguration ||
        Parameters->DisconnectMode != ConnectModeOutput)
    {
        return STATUS_SUCCESS;
    }

    Status = TegraGpioPinMask(Parameters->BankId, Parameters->PinNumberTable,
                              Parameters->PinCount, &Mask);
    if (NT_SUCCESS(Status))
        TegraGpioWriteBankMasked(Context, Parameters->BankId, TEGRAGPIO_MSK_OE, Mask, 0);

    return Status;
}

static
NTSTATUS
NTAPI
TegraGpioReadPins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_READ_PINS_MASK_PARAMETERS Parameters)
{
    if (Parameters->BankId >= TEGRAGPIO_BANKS)
        return STATUS_INVALID_PARAMETER;

    *Parameters->PinValues = TegraGpioReadBank(ContextPointer, Parameters->BankId, TEGRAGPIO_IN);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioWritePins(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_WRITE_PINS_MASK_PARAMETERS Parameters)
{
    PTEGRAGPIO_CONTEXT Context = ContextPointer;
    ULONG Set = (ULONG)Parameters->SetMask;
    ULONG Clear = (ULONG)Parameters->ClearMask;

    if (Parameters->BankId >= TEGRAGPIO_BANKS ||
        ((Parameters->SetMask | Parameters->ClearMask) & ~(ULONG64)MAXULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Set | Clear)
        TegraGpioWriteBankMasked(Context, Parameters->BankId, TEGRAGPIO_MSK_OUT, Set | Clear, Set);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TegraGpioEvtDeviceAdd(
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
TegraGpioEvtDriverUnload(
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

    WDF_DRIVER_CONFIG_INIT(&Config, TegraGpioEvtDeviceAdd);
    Config.DriverPoolTag = TEGRAGPIO_TAG;
    Config.EvtDriverUnload = TegraGpioEvtDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Version = GPIO_CLIENT_VERSION;
    Packet.Size = sizeof(Packet);
    Packet.Flags = GPIO_CLIENT_REGISTRATION_FLAGS_NONE;
    Packet.ControllerContextSize = sizeof(TEGRAGPIO_CONTEXT);
    Packet.CLIENT_PrepareController = TegraGpioPrepareController;
    Packet.CLIENT_ReleaseController = TegraGpioReleaseController;
    Packet.CLIENT_StartController = TegraGpioStartController;
    Packet.CLIENT_StopController = TegraGpioStopController;
    Packet.CLIENT_QueryControllerBasicInformation = TegraGpioQueryControllerBasicInformation;
    Packet.CLIENT_QuerySetControllerInformation = TegraGpioQuerySetControllerInformation;
    Packet.CLIENT_EnableInterrupt = TegraGpioEnableInterrupt;
    Packet.CLIENT_DisableInterrupt = TegraGpioDisableInterrupt;
    Packet.CLIENT_UnmaskInterrupt = TegraGpioUnmaskInterrupt;
    Packet.CLIENT_MaskInterrupts = TegraGpioMaskInterrupts;
    Packet.CLIENT_QueryActiveInterrupts = TegraGpioQueryActiveInterrupts;
    Packet.CLIENT_ClearActiveInterrupts = TegraGpioClearActiveInterrupts;
    Packet.CLIENT_QueryEnabledInterrupts = TegraGpioQueryEnabledInterrupts;
    Packet.CLIENT_ConnectIoPins = TegraGpioConnectIoPins;
    Packet.CLIENT_DisconnectIoPins = TegraGpioDisconnectIoPins;
    Packet.CLIENT_ReadGpioPinsUsingMask = TegraGpioReadPins;
    Packet.CLIENT_WriteGpioPinsUsingMask = TegraGpioWritePins;
    return GPIO_CLX_RegisterClient(Driver, &Packet, RegistryPath);
}
