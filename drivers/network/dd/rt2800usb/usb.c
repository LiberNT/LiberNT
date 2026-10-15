/*
 * PROJECT:     LiberNT Ralink RT2800 USB Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB transport: vendor requests, register access, bulk receive and transmit
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rt2800usb.h"
#include <rsna_ccmp.h>

#define NDEBUG
#include <debug.h>

#define RT_VENDOR_CHUNK     64

static IO_COMPLETION_ROUTINE RtReceiveComplete;
static IO_COMPLETION_ROUTINE RtTransmitComplete;

static
NTSTATUS
RtSubmitUrbSync(
    _In_ PRT_ADAPTER Adapter,
    _Inout_ PURB Urb)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        Adapter->LowerDeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Urb;

    Status = IoCallDriver(Adapter->LowerDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

NTSTATUS
RtVendorRequest(
    _In_ PRT_ADAPTER Adapter,
    _In_ UCHAR Request,
    _In_ BOOLEAN In,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _Inout_updates_bytes_opt_(Length) PVOID Buffer,
    _In_ USHORT Length)
{
    PVOID Bounce = NULL;
    NTSTATUS Status;
    URB Urb;

    if (Length)
    {
        Bounce = RtAllocate(Length);
        if (!Bounce)
            return STATUS_INSUFFICIENT_RESOURCES;

        if (!In)
            RtlCopyMemory(Bounce, Buffer, Length);
    }

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb.UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_VENDOR_DEVICE;
    Urb.UrbControlVendorClassRequest.TransferFlags =
        In ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) : USBD_TRANSFER_DIRECTION_OUT;
    Urb.UrbControlVendorClassRequest.Request = Request;
    Urb.UrbControlVendorClassRequest.Value = Value;
    Urb.UrbControlVendorClassRequest.Index = Index;
    Urb.UrbControlVendorClassRequest.TransferBuffer = Bounce;
    Urb.UrbControlVendorClassRequest.TransferBufferLength = Length;

    Status = RtSubmitUrbSync(Adapter, &Urb);
    if (NT_SUCCESS(Status) && In && Length)
    {
        if (Urb.UrbControlVendorClassRequest.TransferBufferLength != Length)
            Status = STATUS_DEVICE_DATA_ERROR;
        else
            RtlCopyMemory(Buffer, Bounce, Length);
    }

    RtFree(Bounce);
    return Status;
}

ULONG
RtRead(
    _In_ PRT_ADAPTER Adapter,
    _In_ USHORT Offset)
{
    ULONG Value = 0;

    if (!NT_SUCCESS(RtVendorRequest(Adapter, RT_USB_MULTI_READ, TRUE, 0, Offset, &Value, sizeof(Value))))
        return MAXULONG;

    return Value;
}

VOID
RtWrite(
    _In_ PRT_ADAPTER Adapter,
    _In_ USHORT Offset,
    _In_ ULONG Value)
{
    RtVendorRequest(Adapter, RT_USB_MULTI_WRITE, FALSE, 0, Offset, &Value, sizeof(Value));
}

VOID
RtWriteBlock(
    _In_ PRT_ADAPTER Adapter,
    _In_ USHORT Offset,
    _In_reads_bytes_(Length) const VOID* Buffer,
    _In_ ULONG Length)
{
    const UCHAR* Bytes = Buffer;
    ULONG Chunk;

    while (Length)
    {
        Chunk = min(Length, RT_VENDOR_CHUNK);
        RtVendorRequest(Adapter, RT_USB_MULTI_WRITE, FALSE, 0, Offset, (PVOID)Bytes, (USHORT)Chunk);
        Bytes += Chunk;
        Offset = (USHORT)(Offset + Chunk);
        Length -= Chunk;
    }
}

static
NTSTATUS
RtGetDescriptor(
    _In_ PRT_ADAPTER Adapter,
    _In_ UCHAR Type,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Returned)
{
    NTSTATUS Status;
    URB Urb;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest(&Urb,
                                 sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 Type,
                                 0,
                                 0,
                                 Buffer,
                                 NULL,
                                 Length,
                                 NULL);

    Status = RtSubmitUrbSync(Adapter, &Urb);
    *Returned = Urb.UrbControlDescriptorRequest.TransferBufferLength;
    return Status;
}

NTSTATUS
RtUsbInitialize(
    _In_ PRT_ADAPTER Adapter)
{
    USB_CONFIGURATION_DESCRIPTOR Header;
    USBD_INTERFACE_LIST_ENTRY Interfaces[2];
    PUSB_INTERFACE_DESCRIPTOR Interface;
    PUSBD_INTERFACE_INFORMATION Info;
    PUSBD_PIPE_INFORMATION Pipe;
    NTSTATUS Status;
    ULONG Returned, i;
    PURB Urb;

    Status = RtGetDescriptor(Adapter, USB_CONFIGURATION_DESCRIPTOR_TYPE, &Header, sizeof(Header), &Returned);
    if (!NT_SUCCESS(Status) || Returned < sizeof(Header) || Header.wTotalLength < sizeof(Header))
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;

    Adapter->ConfigurationDescriptor = RtAllocate(Header.wTotalLength);
    if (!Adapter->ConfigurationDescriptor)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtGetDescriptor(Adapter,
                             USB_CONFIGURATION_DESCRIPTOR_TYPE,
                             Adapter->ConfigurationDescriptor,
                             Header.wTotalLength,
                             &Returned);
    if (!NT_SUCCESS(Status) || Returned != Header.wTotalLength)
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;

    Interface = USBD_ParseConfigurationDescriptorEx(Adapter->ConfigurationDescriptor,
                                                    Adapter->ConfigurationDescriptor,
                                                    0,
                                                    0,
                                                    -1,
                                                    -1,
                                                    -1);
    if (!Interface)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    RtlZeroMemory(Interfaces, sizeof(Interfaces));
    Interfaces[0].InterfaceDescriptor = Interface;

    Urb = USBD_CreateConfigurationRequestEx(Adapter->ConfigurationDescriptor, Interfaces);
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;

    Info = Interfaces[0].Interface;
    for (i = 0; i < Info->NumberOfPipes; i++)
        Info->Pipes[i].MaximumTransferSize = RT_RX_BUFFER_SIZE;

    Status = RtSubmitUrbSync(Adapter, Urb);
    if (NT_SUCCESS(Status))
    {
        Adapter->ConfigurationHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;
        Info = Interfaces[0].Interface;
        for (i = 0; i < Info->NumberOfPipes; i++)
        {
            Pipe = &Info->Pipes[i];
            if (Pipe->PipeType != UsbdPipeTypeBulk)
                continue;

            if (USB_ENDPOINT_DIRECTION_IN(Pipe->EndpointAddress))
            {
                if (!Adapter->BulkInPipe)
                    Adapter->BulkInPipe = Pipe->PipeHandle;
            }
            else if (!Adapter->BulkOutPipe)
            {
                Adapter->BulkOutPipe = Pipe->PipeHandle;
                Adapter->BulkOutPacketSize = Pipe->MaximumPacketSize;
            }
        }
    }

    ExFreePool(Urb);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!Adapter->BulkInPipe || !Adapter->BulkOutPipe)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    for (i = 0; i < RT_RX_URBS; i++)
    {
        Adapter->Rx[i].Adapter = Adapter;
        Adapter->Rx[i].Index = i;
        Adapter->Rx[i].Buffer = RtAllocate(RT_RX_BUFFER_SIZE);
        Adapter->Rx[i].Irp = IoAllocateIrp(Adapter->LowerDeviceObject->StackSize, FALSE);
        if (!Adapter->Rx[i].Buffer || !Adapter->Rx[i].Irp)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < RT_TX_URBS; i++)
    {
        Adapter->Tx[i].Adapter = Adapter;
        Adapter->Tx[i].Index = i;
        Adapter->Tx[i].Buffer = RtAllocate(RT_TX_BUFFER_SIZE);
        Adapter->Tx[i].Irp = IoAllocateIrp(Adapter->LowerDeviceObject->StackSize, FALSE);
        if (!Adapter->Tx[i].Buffer || !Adapter->Tx[i].Irp)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

VOID
RtUsbCleanup(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < RT_RX_URBS; i++)
    {
        if (Adapter->Rx[i].Irp)
            IoFreeIrp(Adapter->Rx[i].Irp);
        RtFree(Adapter->Rx[i].Buffer);
    }

    for (i = 0; i < RT_TX_URBS; i++)
    {
        if (Adapter->Tx[i].Irp)
            IoFreeIrp(Adapter->Tx[i].Irp);
        RtFree(Adapter->Tx[i].Buffer);
    }

    RtFree(Adapter->ConfigurationDescriptor);
    Adapter->ConfigurationDescriptor = NULL;
}

static
VOID
RtSubmitBulk(
    _In_ PRT_URB_CONTEXT Context,
    _In_ USBD_PIPE_HANDLE Pipe,
    _In_ ULONG Length,
    _In_ BOOLEAN In,
    _In_ PIO_COMPLETION_ROUTINE Completion)
{
    PIO_STACK_LOCATION IoStack;

    IoReuseIrp(Context->Irp, STATUS_SUCCESS);
    RtlZeroMemory(&Context->Urb, sizeof(Context->Urb));
    UsbBuildInterruptOrBulkTransferRequest(&Context->Urb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Pipe,
                                           Context->Buffer,
                                           NULL,
                                           Length,
                                           In ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK)
                                              : USBD_TRANSFER_DIRECTION_OUT,
                                           NULL);

    IoStack = IoGetNextIrpStackLocation(Context->Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = &Context->Urb;
    IoSetCompletionRoutine(Context->Irp, Completion, Context, TRUE, TRUE, TRUE);

    IoCallDriver(Context->Adapter->LowerDeviceObject, Context->Irp);
}

static
ULONG
RtFrameHeaderLength(
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    ULONG HeaderLength = DOT11_HEADER_LENGTH;

    if (Length < 2)
        return Length;

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_CTRL)
        return min(Length, 16);

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_DATA)
    {
        if ((Frame[1] & (DOT11_FC1_TODS | DOT11_FC1_FROMDS)) == (DOT11_FC1_TODS | DOT11_FC1_FROMDS))
            HeaderLength += RT_ADDRESS_LENGTH;
        if (Frame[0] & DOT11_QOS_SUBTYPE)
        {
            HeaderLength += 2;
            if (Frame[1] & 0x80)
                HeaderLength += 4;
        }
    }
    else if (Frame[1] & 0x80)
    {
        HeaderLength += 4;
    }

    return min(HeaderLength, Length);
}

static
BOOLEAN
RtSoftwareDecrypt(
    _In_ PRT_ADAPTER Adapter,
    _Inout_updates_bytes_(*Length) PUCHAR Frame,
    _Inout_ PULONG Length)
{
    ULONG HeaderLength = RtFrameHeaderLength(Frame, *Length);
    PUCHAR Body = Frame + HeaderLength;
    BOOLEAN Group = (Frame[4] & 0x01) != 0;
    ULONGLONG Pn = 0;
    UCHAR KeyId, Tk[16];
    BOOLEAN Valid;

    if (*Length < HeaderLength + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN)
        return FALSE;
    KeyId = Body[3] >> 6;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->SoftwareCrypto && (Group ? Adapter->GroupKeyValid[KeyId] : Adapter->MgmtKeyValid);
    if (Valid)
        RtlCopyMemory(Tk, Group ? Adapter->GroupKey[KeyId] : Adapter->MgmtTk, sizeof(Tk));
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Valid = RsnaCcmpDecrypt(Tk, Frame, HeaderLength, Body, *Length - HeaderLength, Body, &Pn) == 0;
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    if (!Valid)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Group)
    {
        Valid = Pn > Adapter->GroupRxPn[KeyId];
        if (Valid)
            Adapter->GroupRxPn[KeyId] = Pn;
    }
    else
    {
        Valid = Pn > Adapter->DataRxPn;
        if (Valid)
            Adapter->DataRxPn = Pn;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Frame[1] &= ~DOT11_FC1_PROTECTED;
    *Length -= RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN;
    return TRUE;
}

static
BOOLEAN
RtSoftwareEncrypt(
    _In_ PRT_ADAPTER Adapter,
    _Inout_updates_bytes_(*Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN) PUCHAR Frame,
    _Inout_ PULONG Length)
{
    ULONG HeaderLength, BodyLength;
    ULONGLONG Pn = 0;
    UCHAR Tk[16];
    BOOLEAN Valid;

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) != DOT11_FC0_TYPE_DATA || (Frame[4] & 0x01))
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->SoftwareCrypto && Adapter->MgmtKeyValid;
    if (Valid)
    {
        Pn = ++Adapter->MgmtTxPn;
        RtlCopyMemory(Tk, Adapter->MgmtTk, sizeof(Tk));
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    HeaderLength = RtFrameHeaderLength(Frame, *Length);
    BodyLength = *Length - HeaderLength;
    RtlMoveMemory(Frame + HeaderLength + RSNA_CCMP_HDR_LEN, Frame + HeaderLength, BodyLength);
    Frame[1] |= DOT11_FC1_PROTECTED;
    RsnaCcmpEncrypt(Tk, Frame, HeaderLength, Pn, 0,
                    Frame + HeaderLength + RSNA_CCMP_HDR_LEN, BodyLength,
                    Frame + HeaderLength);
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    *Length += RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN;
    return TRUE;
}

static
VOID
RtProcessReceiveBuffer(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG RxInfo, PacketLength, Rxwi0, Rxwi1, Rxwi2, Rxd, MpduLength, HeaderLength, Pad;
    PUCHAR Mpdu, Frame;
    LONG Rssi;

    while (Length >= RT_RXINFO_SIZE + RT_RXWI_SIZE + RT_RXD_SIZE)
    {
        RtlCopyMemory(&RxInfo, Buffer, sizeof(RxInfo));
        PacketLength = RxInfo & RT_RXINFO_PKT_LEN_MASK;
        if (PacketLength < RT_RXWI_SIZE || RT_RXINFO_SIZE + PacketLength + RT_RXD_SIZE > Length)
            return;

        RtlCopyMemory(&Rxwi0, Buffer + RT_RXINFO_SIZE, sizeof(Rxwi0));
        RtlCopyMemory(&Rxwi1, Buffer + RT_RXINFO_SIZE + 4, sizeof(Rxwi1));
        RtlCopyMemory(&Rxwi2, Buffer + RT_RXINFO_SIZE + 8, sizeof(Rxwi2));
        RtlCopyMemory(&Rxd, Buffer + RT_RXINFO_SIZE + PacketLength, sizeof(Rxd));

        MpduLength = (Rxwi0 >> RT_RXWI_W0_LENGTH_SHIFT) & RT_RXWI_W0_LENGTH_MASK;
        Mpdu = Buffer + RT_RXINFO_SIZE + RT_RXWI_SIZE;

        if (!(Rxd & RT_RXD_CRC_ERROR) &&
            MpduLength >= 10 &&
            MpduLength <= RT_MAX_80211_FRAME &&
            MpduLength + RT_RXWI_SIZE <= PacketLength)
        {
            HeaderLength = RtFrameHeaderLength(Mpdu, MpduLength);
            Pad = (Rxd & RT_RXD_L2PAD) ? ((4 - (HeaderLength & 3)) & 3) : 0;

            if (MpduLength + Pad + RT_RXWI_SIZE <= PacketLength)
            {
                Frame = Mpdu + Pad;
                if (Pad)
                    RtlMoveMemory(Frame, Mpdu, HeaderLength);

                if ((Rxd & RT_RXD_DECRYPTED) && !(Rxd & RT_RXD_CIPHER_ERROR_MASK))
                    Frame[1] &= ~DOT11_FC1_PROTECTED;

                Rssi = RtRssiFromRxwi(Adapter, Rxwi2, Adapter->CurrentChannel);

                if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_MGMT)
                {
                    RtMlmeReceive(Adapter, Frame, MpduLength, Rssi,
                                  (Rxd & RT_RXD_DECRYPTED) && !(Rxd & RT_RXD_CIPHER_ERROR_MASK));
                }
                else if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_DATA &&
                         (Frame[1] & DOT11_FC1_PROTECTED) &&
                         Adapter->SoftwareCrypto)
                {
                    if (RtSoftwareDecrypt(Adapter, Frame, &MpduLength))
                    {
                        RtReceiveData(Adapter,
                                      Frame,
                                      MpduLength,
                                      Rssi,
                                      ((Rxwi1 >> RT_RXWI_W1_PHYMODE_SHIFT) << 8) | ((Rxwi1 >> RT_RXWI_W1_MCS_SHIFT) & 0x7f));
                    }
                }
                else if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_DATA &&
                         !(Rxd & RT_RXD_CIPHER_ERROR_MASK) &&
                         (!(Frame[1] & DOT11_FC1_PROTECTED)))
                {
                    RtReceiveData(Adapter,
                                  Frame,
                                  MpduLength,
                                  Rssi,
                                  ((Rxwi1 >> RT_RXWI_W1_PHYMODE_SHIFT) << 8) | ((Rxwi1 >> RT_RXWI_W1_MCS_SHIFT) & 0x7f));
                }
            }
        }
        else if (Rxd & RT_RXD_CRC_ERROR)
        {
            Adapter->RxError++;
        }

        PacketLength = RT_RXINFO_SIZE + PacketLength + RT_RXD_SIZE;
        PacketLength = (PacketLength + 3) & ~3;
        if (PacketLength >= Length)
            return;

        Buffer += PacketLength;
        Length -= PacketLength;
    }
}

static
NTSTATUS
NTAPI
RtReceiveComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PRT_URB_CONTEXT Rx = Context;
    PRT_ADAPTER Adapter = Rx->Adapter;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Irp->IoStatus.Status) && !Adapter->Halting)
    {
        RtProcessReceiveBuffer(Adapter,
                               Rx->Buffer,
                               Rx->Urb.UrbBulkOrInterruptTransfer.TransferBufferLength);
    }

    if (!Adapter->Halting && Irp->IoStatus.Status != STATUS_CANCELLED &&
        Irp->IoStatus.Status != STATUS_DEVICE_NOT_CONNECTED &&
        Irp->IoStatus.Status != STATUS_NO_SUCH_DEVICE)
    {
        RtSubmitBulk(Rx, Adapter->BulkInPipe, RT_RX_BUFFER_SIZE, TRUE, RtReceiveComplete);
    }
    else
    {
        Rx->InUse = FALSE;
        InterlockedDecrement(&Adapter->RxPending);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
RtStartReceive(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < RT_RX_URBS; i++)
    {
        if (Adapter->Rx[i].InUse)
            continue;

        Adapter->Rx[i].InUse = TRUE;
        InterlockedIncrement(&Adapter->RxPending);
        RtSubmitBulk(&Adapter->Rx[i], Adapter->BulkInPipe, RT_RX_BUFFER_SIZE, TRUE, RtReceiveComplete);
    }

    return STATUS_SUCCESS;
}

VOID
RtStopIo(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG i, Wait;

    for (i = 0; i < RT_RX_URBS; i++)
    {
        if (Adapter->Rx[i].InUse)
            IoCancelIrp(Adapter->Rx[i].Irp);
    }

    for (i = 0; i < RT_TX_URBS; i++)
    {
        if (Adapter->Tx[i].InUse)
            IoCancelIrp(Adapter->Tx[i].Irp);
    }

    for (Wait = 0; Wait < 500 && (Adapter->RxPending || Adapter->TxPending); Wait++)
        RtSleep(10);
}

static
NTSTATUS
NTAPI
RtTransmitComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PRT_URB_CONTEXT Tx = Context;
    PRT_ADAPTER Adapter = Tx->Adapter;
    PNET_BUFFER_LIST Nbl = Tx->Nbl;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Irp->IoStatus.Status))
        Adapter->TxOk++;
    else
        Adapter->TxError++;

    Tx->Nbl = NULL;
    NdisAcquireSpinLock(&Adapter->TxLock);
    Tx->InUse = FALSE;
    NdisReleaseSpinLock(&Adapter->TxLock);
    InterlockedDecrement(&Adapter->TxPending);

    if (Nbl)
    {
        NET_BUFFER_LIST_STATUS(Nbl) = NT_SUCCESS(Irp->IoStatus.Status) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Nbl, NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
    }

    if (!Adapter->Halting)
        RtTxPump(Adapter, TRUE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
RtTransmitRate(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN Management,
    _Out_ PULONG PhyMode,
    _Out_ PULONG Mcs)
{
    if (Management || Adapter->LinkState != RtLinkAssociated)
    {
        if (Adapter->CurrentChannel > 14)
        {
            *PhyMode = RT_PHY_OFDM;
            *Mcs = 0;
        }
        else
        {
            *PhyMode = RT_PHY_CCK;
            *Mcs = 0;
        }
        return;
    }

    *PhyMode = RT_PHY_OFDM;
    *Mcs = 7;
}

static
PRT_URB_CONTEXT
RtAcquireTx(
    _In_ PRT_ADAPTER Adapter)
{
    PRT_URB_CONTEXT Tx = NULL;
    ULONG i;

    NdisAcquireSpinLock(&Adapter->TxLock);
    for (i = 0; i < RT_TX_URBS; i++)
    {
        if (!Adapter->Tx[i].InUse)
        {
            Tx = &Adapter->Tx[i];
            Tx->InUse = TRUE;
            break;
        }
    }
    NdisReleaseSpinLock(&Adapter->TxLock);

    return Tx;
}

static
VOID
RtReleaseTx(
    _In_ PRT_ADAPTER Adapter,
    _In_ PRT_URB_CONTEXT Tx)
{
    NdisAcquireSpinLock(&Adapter->TxLock);
    Tx->InUse = FALSE;
    NdisReleaseSpinLock(&Adapter->TxLock);
}

static
VOID
RtSubmitTx(
    _In_ PRT_ADAPTER Adapter,
    _In_ PRT_URB_CONTEXT Tx,
    _In_ ULONG Length,
    _In_ BOOLEAN Management,
    _In_ BOOLEAN Exempt,
    _In_opt_ PNET_BUFFER_LIST Nbl)
{
    ULONG TxInfo, Txwi[4], PhyMode, Mcs, Total, Wcid;
    PUCHAR Out = Tx->Buffer;
    PUCHAR Frame = Out + RT_TXINFO_SIZE + RT_TXWI_SIZE;
    BOOLEAN Unicast, Protect;

    Unicast = !(Frame[4] & 0x01);
    Protect = !Management &&
              !Exempt &&
              Adapter->PairwiseKeyInstalled &&
              (Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_DATA &&
              Unicast;
    Wcid = Unicast && Adapter->LinkState == RtLinkAssociated ? RT_AP_WCID : 0xff;
    if (Frame[1] & DOT11_FC1_PROTECTED)
        Wcid = 0xff;

    RtTransmitRate(Adapter, Management, &PhyMode, &Mcs);

    Txwi[0] = (PhyMode << RT_TXWI_W0_PHYMODE_SHIFT) | (Mcs << RT_TXWI_W0_MCS_SHIFT) | (3 << RT_TXWI_W0_TX_OP_SHIFT);
    Txwi[1] = (Unicast ? RT_TXWI_W1_ACK : 0) |
              RT_TXWI_W1_NSEQ |
              (Wcid << RT_TXWI_W1_WCID_SHIFT) |
              (Length << RT_TXWI_W1_LENGTH_SHIFT) |
              (1u << RT_TXWI_W1_PACKETID_SHIFT);
    Txwi[2] = 0;
    Txwi[3] = 0;

    Total = (RT_TXINFO_SIZE + RT_TXWI_SIZE + Length + 3) & ~3;
    TxInfo = ((Total - RT_TXINFO_SIZE) & RT_TXINFO_PKT_LEN_MASK) | RT_TXINFO_QSEL_EDCA;

    RtlCopyMemory(Out, &TxInfo, sizeof(TxInfo));
    RtlCopyMemory(Out + RT_TXINFO_SIZE, Txwi, sizeof(Txwi));
    RtlZeroMemory(Out + RT_TXINFO_SIZE + RT_TXWI_SIZE + Length, Total + 4 - (RT_TXINFO_SIZE + RT_TXWI_SIZE + Length));
    if (Protect)
        Frame[1] |= DOT11_FC1_PROTECTED;

    Tx->Nbl = Nbl;
    InterlockedIncrement(&Adapter->TxPending);
    Adapter->TxBytes += Length;
    RtSubmitBulk(Tx, Adapter->BulkOutPipe, Total + 4, FALSE, RtTransmitComplete);
}

NTSTATUS
RtSendFrame(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ BOOLEAN Management,
    _In_opt_ PNET_BUFFER_LIST Nbl)
{
    PRT_URB_CONTEXT Tx;

    if (Length < DOT11_HEADER_LENGTH || Length > RT_MAX_80211_FRAME)
        return STATUS_INVALID_PARAMETER;

    Tx = RtAcquireTx(Adapter);
    if (!Tx)
        return STATUS_DEVICE_BUSY;

    RtlCopyMemory(Tx->Buffer + RT_TXINFO_SIZE + RT_TXWI_SIZE, Frame, Length);
    RtSubmitTx(Adapter, Tx, Length, Management, FALSE, Nbl);
    return STATUS_PENDING;
}

NTSTATUS
RtSendNetBuffer(
    _In_ PRT_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ BOOLEAN Exempt,
    _In_opt_ PNET_BUFFER_LIST Nbl)
{
    ULONG Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
    PRT_URB_CONTEXT Tx;
    PUCHAR Storage, Data;

    if (Length < DOT11_HEADER_LENGTH || Length > RT_MAX_80211_FRAME)
        return STATUS_INVALID_PARAMETER;

    Tx = RtAcquireTx(Adapter);
    if (!Tx)
        return STATUS_DEVICE_BUSY;

    Storage = Tx->Buffer + RT_TXINFO_SIZE + RT_TXWI_SIZE;
    Data = NdisGetDataBuffer(NetBuffer, Length, Storage, 1, 0);
    if (!Data)
    {
        RtReleaseTx(Adapter, Tx);
        return STATUS_INVALID_PARAMETER;
    }

    if (Data != Storage)
        RtlCopyMemory(Storage, Data, Length);

    if (!Exempt)
        RtSoftwareEncrypt(Adapter, Storage, &Length);

    RtSubmitTx(Adapter, Tx, Length, FALSE, Exempt, Nbl);
    return STATUS_PENDING;
}

ULONG
RtFreeTxCount(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG Count = 0, i;

    NdisAcquireSpinLock(&Adapter->TxLock);
    for (i = 0; i < RT_TX_URBS; i++)
    {
        if (!Adapter->Tx[i].InUse)
            Count++;
    }
    NdisReleaseSpinLock(&Adapter->TxLock);

    return Count;
}
