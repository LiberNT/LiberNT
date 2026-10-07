/*
 * PROJECT:     LiberNT VirtIO Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled VirtIO 1.0 network device driver over the PCI transport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kd1af4.h"

#define VIO_VENDOR                  0x1AF4
#define VIO_DEVICE_TRANSITIONAL     0x1000
#define VIO_DEVICE_MODERN           0x1041
#define VIO_QUEUE_PAGES             2
#define VIO_CAPABILITY_LIMIT        48

static
UCHAR
VioRead8(
    _In_ PUCHAR Address)
{
    return KdNetExtensibilityImports->ReadRegisterUChar(Address);
}

static
USHORT
VioRead16(
    _In_ PUCHAR Address)
{
    return KdNetExtensibilityImports->ReadRegisterUShort((PUSHORT)Address);
}

static
ULONG
VioRead32(
    _In_ PUCHAR Address)
{
    return KdNetExtensibilityImports->ReadRegisterULong((PULONG)Address);
}

static
VOID
VioWrite8(
    _In_ PUCHAR Address,
    _In_ UCHAR Value)
{
    KdNetExtensibilityImports->WriteRegisterUChar(Address, Value);
}

static
VOID
VioWrite16(
    _In_ PUCHAR Address,
    _In_ USHORT Value)
{
    KdNetExtensibilityImports->WriteRegisterUShort((PUSHORT)Address, Value);
}

static
VOID
VioWrite32(
    _In_ PUCHAR Address,
    _In_ ULONG Value)
{
    KdNetExtensibilityImports->WriteRegisterULong((PULONG)Address, Value);
}

static
VOID
VioWrite64(
    _In_ PUCHAR Address,
    _In_ ULONG64 Value)
{
    VioWrite32(Address, (ULONG)Value);
    VioWrite32(Address + 4, (ULONG)(Value >> 32));
}

static
VOID
VioStall(
    _In_ ULONG Microseconds)
{
    KdNetExtensibilityImports->StallExecutionProcessor(Microseconds);
}

static
ULONG64
VioPhysical(
    _In_ PVOID Address)
{
    return (ULONG64)KdNetExtensibilityImports->GetPhysicalAddress(Address).QuadPart;
}

static
ULONG
VioAlignedAdapterSize(VOID)
{
    return (sizeof(VIO_ADAPTER) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

BOOLEAN
VioIsSupportedDevice(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    return Device->VendorID == VIO_VENDOR &&
           (Device->DeviceID == VIO_DEVICE_TRANSITIONAL || Device->DeviceID == VIO_DEVICE_MODERN);
}

ULONG
NTAPI
VioGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    UNREFERENCED_PARAMETER(Device);

    return VioAlignedAdapterSize() + 2 * VIO_QUEUE_PAGES * PAGE_SIZE +
           (VIO_RX_COUNT + VIO_TX_COUNT) * VIO_BUFFER_SIZE;
}

static
BOOLEAN
VioFindCapability(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _In_ UCHAR Type,
    _Out_ PUCHAR *Address,
    _Out_opt_ PULONG Extra)
{
    UCHAR Capability[16], Pointer = 0, Bar;
    ULONG Offset, Length, Count;
    USHORT Status = 0;

    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Status,
                                                  FIELD_OFFSET(PCI_COMMON_HEADER, Status), sizeof(Status));
    if (!(Status & PCI_STATUS_CAPABILITIES_LIST))
        return FALSE;

    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Pointer,
                                                  FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.CapabilitiesPtr),
                                                  sizeof(Pointer));
    for (Count = 0; Count < VIO_CAPABILITY_LIMIT; Count++)
    {
        Pointer &= ~3;
        if (Pointer < PCI_COMMON_HDR_LENGTH)
            break;

        if (KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, Capability,
                                                          Pointer, sizeof(Capability)) != sizeof(Capability))
        {
            break;
        }

        Bar = Capability[4];
        if (Capability[0] == VIO_PCI_CAPABILITY_VENDOR && Capability[3] == Type &&
            Bar < MAXIMUM_DEBUG_BARS && Device->BaseAddress[Bar].Valid &&
            Device->BaseAddress[Bar].Type == CmResourceTypeMemory)
        {
            Offset = Capability[8] | (Capability[9] << 8) | (Capability[10] << 16) | ((ULONG)Capability[11] << 24);
            Length = Capability[12] | (Capability[13] << 8) | (Capability[14] << 16) | ((ULONG)Capability[15] << 24);
            if (Offset <= Device->BaseAddress[Bar].Length &&
                Length <= Device->BaseAddress[Bar].Length - Offset)
            {
                *Address = Device->BaseAddress[Bar].TranslatedAddress + Offset;
                if (Extra)
                {
                    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, Extra,
                                                                  Pointer + sizeof(Capability), sizeof(*Extra));
                }

                return TRUE;
            }
        }

        Pointer = Capability[1];
    }

    return FALSE;
}

static
VOID
VioNotify(
    _In_ PVIO_QUEUE Queue)
{
    VioWrite16(Queue->Notify, Queue->Number);
}

static
VOID
VioOffer(
    _In_ PVIO_QUEUE Queue,
    _In_ USHORT Descriptor)
{
    Queue->Driver->Ring[Queue->Available % Queue->Size] = Descriptor;
    KeMemoryBarrier();
    Queue->Available++;
    Queue->Driver->Index = Queue->Available;
    KeMemoryBarrier();
}

static
BOOLEAN
VioSetupQueue(
    _In_ PVIO_ADAPTER Adapter,
    _Out_ PVIO_QUEUE Queue,
    _In_ USHORT Number,
    _In_ USHORT Wanted,
    _In_ PUCHAR Memory)
{
    USHORT Size, Maximum;
    ULONG i;

    VioWrite16(Adapter->Common + VIO_COMMON_QUEUE_SELECT, Number);
    Maximum = VioRead16(Adapter->Common + VIO_COMMON_QUEUE_SIZE);
    for (Size = Wanted; Size > Maximum; Size >>= 1)
        NOTHING;

    if (!Size)
        return FALSE;

    for (i = 0; i < VIO_QUEUE_PAGES * PAGE_SIZE; i++)
        ((volatile UCHAR *)Memory)[i] = 0;

    Queue->Size = Size;
    Queue->Number = Number;
    Queue->Available = 0;
    Queue->LastUsed = 0;
    Queue->Descriptors = (PVOID)Memory;
    Queue->Driver = (PVOID)(Memory + Wanted * sizeof(VIO_DESCRIPTOR));
    Queue->Device = (PVOID)(Memory + PAGE_SIZE);
    Queue->Driver->Flags = VIO_AVAILABLE_NO_INTERRUPT;

    VioWrite16(Adapter->Common + VIO_COMMON_QUEUE_SIZE, Size);
    VioWrite16(Adapter->Common + VIO_COMMON_QUEUE_VECTOR, VIO_NO_VECTOR);
    VioWrite64(Adapter->Common + VIO_COMMON_QUEUE_DESCRIPTORS, VioPhysical((PVOID)Queue->Descriptors));
    VioWrite64(Adapter->Common + VIO_COMMON_QUEUE_DRIVER, VioPhysical((PVOID)Queue->Driver));
    VioWrite64(Adapter->Common + VIO_COMMON_QUEUE_DEVICE, VioPhysical((PVOID)Queue->Device));
    Queue->Notify = Adapter->Notify +
                    VioRead16(Adapter->Common + VIO_COMMON_QUEUE_NOTIFY_OFFSET) * Adapter->NotifyMultiplier;
    VioWrite16(Adapter->Common + VIO_COMMON_QUEUE_ENABLE, 1);
    return TRUE;
}

static
VOID
VioUpdateLink(
    _In_ PVIO_ADAPTER Adapter)
{
    PKDNET_SHARED_DATA KdNet = Adapter->KdNet;

    if (Adapter->HasStatus &&
        !(VioRead16(Adapter->Config + VIO_NET_CONFIG_STATUS) & VIO_NET_STATUS_LINK_UP))
    {
        *KdNet->LinkState = 0;
        return;
    }

    KdNet->LinkSpeed = 1000;
    KdNet->LinkDuplex = 1;
    *KdNet->LinkState = 1;
}

static
VOID
VioReclaim(
    _In_ PVIO_ADAPTER Adapter)
{
    PVIO_QUEUE Queue = &Adapter->Tx;
    ULONG Id;

    while (Queue->LastUsed != Queue->Device->Index)
    {
        Id = Queue->Device->Ring[Queue->LastUsed % Queue->Size].Id;
        if (Id < VIO_TX_COUNT)
            Adapter->TxSubmitted[Id] = FALSE;

        Queue->LastUsed++;
    }
}

NTSTATUS
NTAPI
VioInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet)
{
    PDEBUG_DEVICE_DESCRIPTOR Device = KdNet->Device;
    PVIO_ADAPTER Adapter = KdNet->Hardware;
    PUCHAR Hardware = KdNet->Hardware, Common, Notify, Config;
    ULONG Multiplier = 0, Low, High, i;
    USHORT Command;
    UCHAR Status;

    if (!VioFindCapability(Device, VIO_PCI_CAP_COMMON, &Common, NULL) ||
        !VioFindCapability(Device, VIO_PCI_CAP_NOTIFY, &Notify, &Multiplier) ||
        !VioFindCapability(Device, VIO_PCI_CAP_DEVICE, &Config, NULL))
    {
        return STATUS_NOT_SUPPORTED;
    }

    for (i = 0; i < sizeof(*Adapter); i++)
        ((volatile UCHAR *)Adapter)[i] = 0;

    Adapter->KdNet = KdNet;
    Adapter->Common = Common;
    Adapter->Notify = Notify;
    Adapter->Config = Config;
    Adapter->NotifyMultiplier = Multiplier;
    Adapter->RxBuffers = Hardware + VioAlignedAdapterSize() + 2 * VIO_QUEUE_PAGES * PAGE_SIZE;
    Adapter->TxBuffers = Adapter->RxBuffers + VIO_RX_COUNT * VIO_BUFFER_SIZE;

    KdNetExtensibilityImports->GetPciDataByOffset(Device->Bus, Device->Slot, &Command,
                                                  FIELD_OFFSET(PCI_COMMON_HEADER, Command), sizeof(Command));
    Command |= PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
    KdNetExtensibilityImports->SetPciDataByOffset(Device->Bus, Device->Slot, &Command,
                                                  FIELD_OFFSET(PCI_COMMON_HEADER, Command), sizeof(Command));

    VioWrite8(Common + VIO_COMMON_DEVICE_STATUS, 0);
    for (i = 0; i < 1000; i++)
    {
        if (!VioRead8(Common + VIO_COMMON_DEVICE_STATUS))
            break;

        VioStall(100);
    }

    if (i == 1000)
        return STATUS_IO_TIMEOUT;

    Status = VIO_STATUS_ACKNOWLEDGE;
    VioWrite8(Common + VIO_COMMON_DEVICE_STATUS, Status);
    Status |= VIO_STATUS_DRIVER;
    VioWrite8(Common + VIO_COMMON_DEVICE_STATUS, Status);

    VioWrite32(Common + VIO_COMMON_DEVICE_FEATURE_SELECT, 0);
    Low = VioRead32(Common + VIO_COMMON_DEVICE_FEATURE);
    VioWrite32(Common + VIO_COMMON_DEVICE_FEATURE_SELECT, 1);
    High = VioRead32(Common + VIO_COMMON_DEVICE_FEATURE);
    if (!(High & VIO_FEATURE_VERSION_1) || !(Low & VIO_NET_FEATURE_MAC))
        return STATUS_NOT_SUPPORTED;

    Low &= VIO_NET_FEATURE_MAC | VIO_NET_FEATURE_STATUS;
    VioWrite32(Common + VIO_COMMON_DRIVER_FEATURE_SELECT, 0);
    VioWrite32(Common + VIO_COMMON_DRIVER_FEATURE, Low);
    VioWrite32(Common + VIO_COMMON_DRIVER_FEATURE_SELECT, 1);
    VioWrite32(Common + VIO_COMMON_DRIVER_FEATURE, VIO_FEATURE_VERSION_1);
    Status |= VIO_STATUS_FEATURES_OK;
    VioWrite8(Common + VIO_COMMON_DEVICE_STATUS, Status);
    if (!(VioRead8(Common + VIO_COMMON_DEVICE_STATUS) & VIO_STATUS_FEATURES_OK))
        return STATUS_NOT_SUPPORTED;

    Adapter->HasStatus = (Low & VIO_NET_FEATURE_STATUS) != 0;
    if (!VioSetupQueue(Adapter, &Adapter->Rx, 0, VIO_RX_COUNT, Hardware + VioAlignedAdapterSize()) ||
        !VioSetupQueue(Adapter, &Adapter->Tx, 1, VIO_TX_COUNT,
                       Hardware + VioAlignedAdapterSize() + VIO_QUEUE_PAGES * PAGE_SIZE))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (i = 0; i < Adapter->Rx.Size; i++)
    {
        Adapter->Rx.Descriptors[i].Address = VioPhysical(Adapter->RxBuffers + i * VIO_BUFFER_SIZE);
        Adapter->Rx.Descriptors[i].Length = VIO_BUFFER_SIZE;
        Adapter->Rx.Descriptors[i].Flags = VIO_DESCRIPTOR_WRITE;
        Adapter->Rx.Descriptors[i].Next = 0;
        VioOffer(&Adapter->Rx, (USHORT)i);
    }

    Status |= VIO_STATUS_DRIVER_OK;
    VioWrite8(Common + VIO_COMMON_DEVICE_STATUS, Status);
    VioNotify(&Adapter->Rx);

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        KdNet->TargetMacAddress[i] = VioRead8(Config + VIO_NET_CONFIG_MAC + i);

    VioUpdateLink(Adapter);
    return STATUS_SUCCESS;
}

VOID
NTAPI
VioShutdownController(
    _In_ PVOID Context)
{
    PVIO_ADAPTER Adapter = Context;
    ULONG i;

    for (i = 0; i < 10000; i++)
    {
        VioReclaim(Adapter);
        if (Adapter->Tx.LastUsed == Adapter->Tx.Available)
            break;

        VioStall(10);
    }

    VioWrite8(Adapter->Common + VIO_COMMON_DEVICE_STATUS, 0);
    *Adapter->KdNet->LinkState = 0;
}

NTSTATUS
NTAPI
VioGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length)
{
    PVIO_ADAPTER Adapter = Context;
    PVIO_QUEUE Queue = &Adapter->Rx;
    ULONG Id, Size, Count;

    for (Count = 0; Count < Queue->Size; Count++)
    {
        if (Queue->LastUsed == Queue->Device->Index)
            break;

        Id = Queue->Device->Ring[Queue->LastUsed % Queue->Size].Id;
        Size = Queue->Device->Ring[Queue->LastUsed % Queue->Size].Length;
        Queue->LastUsed++;
        if (Id >= Queue->Size)
            continue;

        if (Size >= VIO_NET_HEADER_SIZE + 14 && Size <= VIO_BUFFER_SIZE)
        {
            Adapter->RxLength[Id] = (USHORT)(Size - VIO_NET_HEADER_SIZE);
            *Handle = Id;
            *Packet = Adapter->RxBuffers + Id * VIO_BUFFER_SIZE + VIO_NET_HEADER_SIZE;
            *Length = Size - VIO_NET_HEADER_SIZE;
            return STATUS_SUCCESS;
        }

        VioOffer(Queue, (USHORT)Id);
        VioNotify(Queue);
    }

    if ((Adapter->LinkPoll++ & 0xFF) == 0)
        VioUpdateLink(Adapter);

    return STATUS_IO_TIMEOUT;
}

VOID
NTAPI
VioReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PVIO_ADAPTER Adapter = Context;
    ULONG Id = Handle & ~HANDLE_FLAGS;

    if (Id >= Adapter->Rx.Size)
        return;

    VioOffer(&Adapter->Rx, (USHORT)Id);
    VioNotify(&Adapter->Rx);
}

NTSTATUS
NTAPI
VioGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle)
{
    PVIO_ADAPTER Adapter = Context;
    ULONG Index = Adapter->TxNext;

    VioReclaim(Adapter);
    if (Adapter->TxSubmitted[Index])
        return STATUS_IO_TIMEOUT;

    Adapter->TxNext = (Index + 1) % Adapter->Tx.Size;
    *Handle = Index | TRANSMIT_HANDLE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
VioSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length)
{
    PVIO_ADAPTER Adapter = Context;
    PVIO_QUEUE Queue = &Adapter->Tx;
    ULONG Index = Handle & ~HANDLE_FLAGS, i;
    PUCHAR Buffer;

    if (!(Handle & TRANSMIT_HANDLE) || Index >= Queue->Size || Length > VIO_MAX_FRAME)
        return STATUS_INVALID_PARAMETER;

    Buffer = Adapter->TxBuffers + Index * VIO_BUFFER_SIZE;
    for (i = 0; i < VIO_NET_HEADER_SIZE; i++)
        Buffer[i] = 0;

    Queue->Descriptors[Index].Address = VioPhysical(Buffer);
    Queue->Descriptors[Index].Length = VIO_NET_HEADER_SIZE + Length;
    Queue->Descriptors[Index].Flags = 0;
    Queue->Descriptors[Index].Next = 0;
    Adapter->TxSubmitted[Index] = TRUE;
    VioOffer(Queue, (USHORT)Index);
    VioNotify(Queue);

    if (Handle & TRANSMIT_ASYNC)
        return STATUS_SUCCESS;

    for (i = 0; i < 10000; i++)
    {
        VioReclaim(Adapter);
        if (!Adapter->TxSubmitted[Index])
            return STATUS_SUCCESS;

        VioStall(10);
    }

    return STATUS_IO_TIMEOUT;
}

PVOID
NTAPI
VioGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PVIO_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
    {
        return Index < VIO_TX_COUNT ?
               Adapter->TxBuffers + Index * VIO_BUFFER_SIZE + VIO_NET_HEADER_SIZE : NULL;
    }

    return Index < VIO_RX_COUNT ?
           Adapter->RxBuffers + Index * VIO_BUFFER_SIZE + VIO_NET_HEADER_SIZE : NULL;
}

ULONG
NTAPI
VioGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle)
{
    PVIO_ADAPTER Adapter = Context;
    ULONG Index = Handle & ~HANDLE_FLAGS;

    if (Handle & TRANSMIT_HANDLE)
        return VIO_MAX_FRAME;

    return Index < VIO_RX_COUNT ? Adapter->RxLength[Index] : 0;
}
