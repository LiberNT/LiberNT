/*
 * PROJECT:     LiberNT SpacemiT K1 I2C driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpbCx controller driver for the SpacemiT K1 TWSI I2C controllers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <spb.h>
#include <spbcx.h>
#include <reshub.h>

#define NDEBUG
#include <debug.h>

#define K1XI2C_TAG              'cIiK'
#define K1XI2C_SIZE             0x38

#define K1XI2C_CONTROL          0x00
#define K1XI2C_STATUS           0x04
#define K1XI2C_DATA             0x0C
#define K1XI2C_CR_START         (1UL << 0)
#define K1XI2C_CR_STOP          (1UL << 1)
#define K1XI2C_CR_NAK           (1UL << 2)
#define K1XI2C_CR_BYTE          (1UL << 3)
#define K1XI2C_CR_FIFO          (1UL << 5)
#define K1XI2C_CR_DMA           (1UL << 7)
#define K1XI2C_CR_MASTER_ABORT  (1UL << 12)
#define K1XI2C_CR_CLOCK         (1UL << 13)
#define K1XI2C_CR_UNIT          (1UL << 14)
#define K1XI2C_CR_INTERRUPTS    0xFFFC0000UL
#define K1XI2C_SR_NAK           (1UL << 14)
#define K1XI2C_SR_UNIT_BUSY     (1UL << 15)
#define K1XI2C_SR_BUS_BUSY      (1UL << 16)
#define K1XI2C_SR_LOST          (1UL << 18)
#define K1XI2C_SR_TX_EMPTY      (1UL << 19)
#define K1XI2C_SR_RX_FULL       (1UL << 20)
#define K1XI2C_SR_BUS_ERROR     (1UL << 22)
#define K1XI2C_SR_EVENTS        0xFFFC0000UL

#define K1XI2C_ACPI_SERIAL_BUS_TAG  0x8E
#define K1XI2C_ACPI_SERIAL_BUS_I2C  1
#define K1XI2C_ACPI_I2C_SPEED       12
#define K1XI2C_ACPI_I2C_ADDRESS     16
#define K1XI2C_ACPI_I2C_LENGTH      18

typedef struct _K1XI2C
{
    PUCHAR Base;
    ULONG Length;
    ULONG64 PhysicalBase;
} K1XI2C, *PK1XI2C;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(K1XI2C, K1xI2cGetContext)

typedef struct _K1XI2C_TARGET
{
    USHORT Address;
    ULONG Speed;
} K1XI2C_TARGET, *PK1XI2C_TARGET;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(K1XI2C_TARGET, K1xI2cGetTarget)

static
ULONG
K1xI2cRead(
    _In_ PK1XI2C I2c,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(I2c->Base + Offset));
}

static
VOID
K1xI2cWrite(
    _In_ PK1XI2C I2c,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(I2c->Base + Offset), Value);
}

static
ULONG
K1xI2cControlBase(
    _In_ PK1XI2C I2c)
{
    return (K1xI2cRead(I2c, K1XI2C_CONTROL) &
            ~(K1XI2C_CR_START | K1XI2C_CR_STOP | K1XI2C_CR_NAK | K1XI2C_CR_BYTE |
              K1XI2C_CR_FIFO | K1XI2C_CR_DMA | K1XI2C_CR_MASTER_ABORT | K1XI2C_CR_INTERRUPTS)) |
           K1XI2C_CR_UNIT | K1XI2C_CR_CLOCK;
}

static
NTSTATUS
K1xI2cWaitIdle(
    _In_ PK1XI2C I2c)
{
    ULONG Loops = 20000;

    while (Loops--)
    {
        if (!(K1xI2cRead(I2c, K1XI2C_STATUS) & (K1XI2C_SR_UNIT_BUSY | K1XI2C_SR_BUS_BUSY)))
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(5);
    }
    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
K1xI2cTransferByte(
    _In_ PK1XI2C I2c,
    _In_ ULONG Flags,
    _In_ ULONG Event,
    _Inout_ PUCHAR Byte)
{
    ULONG Loops = 20000, Status;

    if (Event == K1XI2C_SR_TX_EMPTY)
        K1xI2cWrite(I2c, K1XI2C_DATA, *Byte);
    K1xI2cWrite(I2c, K1XI2C_CONTROL, K1xI2cControlBase(I2c) | Flags | K1XI2C_CR_BYTE);
    while (Loops--)
    {
        Status = K1xI2cRead(I2c, K1XI2C_STATUS);
        if (Status & (K1XI2C_SR_LOST | K1XI2C_SR_BUS_ERROR))
        {
            K1xI2cWrite(I2c, K1XI2C_STATUS, Status & K1XI2C_SR_EVENTS);
            K1xI2cWrite(I2c, K1XI2C_CONTROL, K1xI2cControlBase(I2c) | K1XI2C_CR_MASTER_ABORT);
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        if (Status & Event)
        {
            K1xI2cWrite(I2c, K1XI2C_STATUS, Event);
            if (Event == K1XI2C_SR_RX_FULL)
                *Byte = (UCHAR)K1xI2cRead(I2c, K1XI2C_DATA);
            else if ((Status & K1XI2C_SR_NAK) && !(Flags & K1XI2C_CR_STOP))
                return STATUS_NO_SUCH_DEVICE;
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(5);
    }
    K1xI2cWrite(I2c, K1XI2C_CONTROL, K1xI2cControlBase(I2c) | K1XI2C_CR_MASTER_ABORT);
    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
K1xI2cTransfer(
    _In_ PK1XI2C I2c,
    _In_ USHORT Address,
    _In_ SPB_TRANSFER_DIRECTION Direction,
    _Inout_updates_(Length) PUCHAR Buffer,
    _In_ size_t Length,
    _In_ BOOLEAN Stop)
{
    UCHAR Byte = (UCHAR)((Address << 1) | (Direction == SpbTransferDirectionFromDevice ? 1 : 0));
    NTSTATUS Status;
    size_t Index;

    Status = K1xI2cTransferByte(I2c, K1XI2C_CR_START | (Length == 0 && Stop ? K1XI2C_CR_STOP : 0),
                                K1XI2C_SR_TX_EMPTY, &Byte);
    for (Index = 0; NT_SUCCESS(Status) && Index < Length; Index++)
    {
        BOOLEAN Last = Index + 1 == Length;

        if (Direction == SpbTransferDirectionFromDevice)
        {
            Status = K1xI2cTransferByte(I2c, Last ? (K1XI2C_CR_NAK | (Stop ? K1XI2C_CR_STOP : 0)) : 0,
                                        K1XI2C_SR_RX_FULL, &Buffer[Index]);
        }
        else
        {
            Byte = Buffer[Index];
            Status = K1xI2cTransferByte(I2c, Last && Stop ? K1XI2C_CR_STOP : 0, K1XI2C_SR_TX_EMPTY, &Byte);
        }
    }
    return Status;
}

static
NTSTATUS
K1xI2cEvtTargetConnect(
    _In_ WDFDEVICE Device,
    _In_ SPBTARGET Target)
{
    PK1XI2C_TARGET Context = K1xI2cGetTarget(Target);
    SPB_CONNECTION_PARAMETERS Parameters;
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties;
    PUCHAR Descriptor;

    UNREFERENCED_PARAMETER(Device);
    SPB_CONNECTION_PARAMETERS_INIT(&Parameters);
    SpbTargetGetConnectionParameters(Target, &Parameters);
    Properties = Parameters.ConnectionParameters;
    if (!Properties || Properties->PropertiesLength < K1XI2C_ACPI_I2C_LENGTH)
        return STATUS_INVALID_PARAMETER;
    Descriptor = Properties->ConnectionProperties;
    if (Descriptor[0] != K1XI2C_ACPI_SERIAL_BUS_TAG || Descriptor[5] != K1XI2C_ACPI_SERIAL_BUS_I2C)
        return STATUS_NOT_SUPPORTED;
    Context->Speed = (ULONG)Descriptor[K1XI2C_ACPI_I2C_SPEED] |
                     ((ULONG)Descriptor[K1XI2C_ACPI_I2C_SPEED + 1] << 8) |
                     ((ULONG)Descriptor[K1XI2C_ACPI_I2C_SPEED + 2] << 16) |
                     ((ULONG)Descriptor[K1XI2C_ACPI_I2C_SPEED + 3] << 24);
    Context->Address = (USHORT)(Descriptor[K1XI2C_ACPI_I2C_ADDRESS] |
                                (Descriptor[K1XI2C_ACPI_I2C_ADDRESS + 1] << 8));
    if (Context->Address > 0x7F)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static
VOID
K1xI2cCompleteRequest(
    _In_ SPBREQUEST Request,
    _In_ NTSTATUS Status,
    _In_ size_t Transferred)
{
    WdfRequestSetInformation((WDFREQUEST)Request, NT_SUCCESS(Status) ? Transferred : 0);
    SpbRequestComplete(Request, Status);
}

static
VOID
K1xI2cRunSequence(
    _In_ WDFDEVICE Device,
    _In_ SPBTARGET Target,
    _In_ SPBREQUEST Request,
    _In_ ULONG TransferCount)
{
    PK1XI2C I2c = K1xI2cGetContext(Device);
    PK1XI2C_TARGET Context = K1xI2cGetTarget(Target);
    NTSTATUS Status;
    size_t Transferred = 0;
    ULONG Index;

    Status = K1xI2cWaitIdle(I2c);
    if (NT_SUCCESS(Status))
        K1xI2cWrite(I2c, K1XI2C_STATUS, K1XI2C_SR_EVENTS);
    for (Index = 0; NT_SUCCESS(Status) && Index < TransferCount; Index++)
    {
        SPB_TRANSFER_DESCRIPTOR Descriptor;
        PMDL Mdl = NULL;
        PUCHAR Buffer = NULL;

        SPB_TRANSFER_DESCRIPTOR_INIT(&Descriptor);
        SpbRequestGetTransferParameters(Request, Index, &Descriptor, &Mdl);
        if (Descriptor.TransferLength)
        {
            if (!Mdl)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Buffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority | MdlMappingNoExecute);
            if (!Buffer)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
        }
        if (Descriptor.DelayInUs)
            KeStallExecutionProcessor(Descriptor.DelayInUs);
        Status = K1xI2cTransfer(I2c, Context->Address, Descriptor.Direction, Buffer,
                                Descriptor.TransferLength, Index + 1 == TransferCount);
        if (NT_SUCCESS(Status))
            Transferred += Descriptor.TransferLength;
    }
    if (!NT_SUCCESS(Status))
        DPRINT1("K1XI2C: %I64x address 0x%x transfer failed 0x%08lx\n", I2c->PhysicalBase, Context->Address, Status);
    K1xI2cCompleteRequest(Request, Status, Transferred);
}

static
VOID
K1xI2cEvtIoRead(
    _In_ WDFDEVICE Device,
    _In_ SPBTARGET Target,
    _In_ SPBREQUEST Request,
    _In_ size_t Length)
{
    UNREFERENCED_PARAMETER(Length);
    K1xI2cRunSequence(Device, Target, Request, 1);
}

static
VOID
K1xI2cEvtIoWrite(
    _In_ WDFDEVICE Device,
    _In_ SPBTARGET Target,
    _In_ SPBREQUEST Request,
    _In_ size_t Length)
{
    UNREFERENCED_PARAMETER(Length);
    K1xI2cRunSequence(Device, Target, Request, 1);
}

static
VOID
K1xI2cEvtIoSequence(
    _In_ WDFDEVICE Device,
    _In_ SPBTARGET Target,
    _In_ SPBREQUEST Request,
    _In_ ULONG TransferCount)
{
    K1xI2cRunSequence(Device, Target, Request, TransferCount);
}

static
NTSTATUS
K1xI2cEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PK1XI2C I2c = K1xI2cGetContext(Device);
    ULONG Index;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);

        if (!Resource || Resource->Type != CmResourceTypeMemory || I2c->Base)
            continue;
        if (Resource->u.Memory.Length < K1XI2C_SIZE)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        I2c->Base = MmMapIoSpace(Resource->u.Memory.Start, Resource->u.Memory.Length, MmNonCached);
        if (!I2c->Base)
            return STATUS_INSUFFICIENT_RESOURCES;
        I2c->Length = Resource->u.Memory.Length;
        I2c->PhysicalBase = (ULONG64)Resource->u.Memory.Start.QuadPart;
    }
    if (!I2c->Base)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

static
NTSTATUS
K1xI2cEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PK1XI2C I2c = K1xI2cGetContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    if (I2c->Base)
        MmUnmapIoSpace(I2c->Base, I2c->Length);
    I2c->Base = NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
K1xI2cEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPower;
    WDF_OBJECT_ATTRIBUTES Attributes;
    SPB_CONTROLLER_CONFIG Config;
    WDFDEVICE Device;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    Status = SpbDeviceInitConfig(DeviceInit);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPower);
    PnpPower.EvtDevicePrepareHardware = K1xI2cEvtPrepareHardware;
    PnpPower.EvtDeviceReleaseHardware = K1xI2cEvtReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPower);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, K1XI2C);
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    SPB_CONTROLLER_CONFIG_INIT(&Config);
    Config.PowerManaged = WdfFalse;
    Config.EvtSpbTargetConnect = K1xI2cEvtTargetConnect;
    Config.EvtSpbIoRead = K1xI2cEvtIoRead;
    Config.EvtSpbIoWrite = K1xI2cEvtIoWrite;
    Config.EvtSpbIoSequence = K1xI2cEvtIoSequence;
    Status = SpbDeviceInitialize(Device, &Config);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, K1XI2C_TARGET);
    SpbControllerSetTargetAttributes(Device, &Attributes);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    WDF_DRIVER_CONFIG_INIT(&Config, K1xI2cEvtDeviceAdd);
    Config.DriverPoolTag = K1XI2C_TAG;
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
