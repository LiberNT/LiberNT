/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RNDIS, CDC-ECM, CDC-NCM and CDC-EEM setup and framing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdusbnet.h"

#define USB_CLASS_OUT           0x21
#define USB_CLASS_IN            0xA1
#define USB_LANGUAGE_ENGLISH    0x0409
#define USB_MAC_STRING_SIZE     (2 + 12 * sizeof(WCHAR))
#define RNDIS_RESPONSE_POLLS    50
#define RNDIS_RESPONSE_DELAY    10000

static const UCHAR EemSentinel[EEM_CRC_SIZE] = { 0xDE, 0xAD, 0xBE, 0xEF };

static
BOOLEAN
UsbNetValidAddress(
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *Address)
{
    ULONG i;
    UCHAR Any = 0, All = 0xFF;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
    {
        Any |= Address[i];
        All &= Address[i];
    }

    return Any && All != 0xFF && !(Address[0] & 1);
}

static
VOID
UsbNetSetAddress(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *Address)
{
    ULONG i;

    if (!UsbNetValidAddress(Address))
        return;

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
        Adapter->KdNet->TargetMacAddress[i] = Address[i];
}

static
NTSTATUS
UsbNetRndisRequest(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Length,
    _Out_ PULONG Response)
{
    PUCHAR Data = Adapter->Control;
    ULONG Type = UsbNetGet32(Data) | RNDIS_COMPLETION;
    ULONG Request = UsbNetGet32(Data + 8);
    NTSTATUS Status;
    ULONG Got, i;

    *Response = 0;
    Status = XhciControl(Adapter, USB_CLASS_OUT, CDC_SEND_ENCAPSULATED_COMMAND, 0,
                         Adapter->Function.ControlInterface, (USHORT)Length, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < RNDIS_RESPONSE_POLLS; i++)
    {
        Status = XhciControl(Adapter, USB_CLASS_IN, CDC_GET_ENCAPSULATED_RESPONSE, 0,
                             Adapter->Function.ControlInterface, RNDIS_RESPONSE_SIZE, &Got);
        if (NT_SUCCESS(Status) && Got >= 16 && UsbNetGet32(Data) == Type && UsbNetGet32(Data + 8) == Request)
        {
            if (UsbNetGet32(Data + 4) > Got || UsbNetGet32(Data + 12) != RNDIS_STATUS_SUCCESS)
                return STATUS_DEVICE_PROTOCOL_ERROR;

            *Response = Got;
            return STATUS_SUCCESS;
        }

        XhciStall(RNDIS_RESPONSE_DELAY);
    }

    return STATUS_IO_TIMEOUT;
}

static
VOID
UsbNetRndisHeader(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Type,
    _In_ ULONG Length)
{
    PUCHAR Data = Adapter->Control;
    ULONG i;

    for (i = 0; i < Length; i++)
        Data[i] = 0;

    UsbNetPut32(Data, Type);
    UsbNetPut32(Data + 4, Length);
    UsbNetPut32(Data + 8, ++Adapter->RndisRequest);
}

static
NTSTATUS
UsbNetStartRndis(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Data = Adapter->Control;
    NTSTATUS Status;
    ULONG Got, Offset, Size;

    UsbNetRndisHeader(Adapter, RNDIS_INITIALIZE_MSG, 24);
    UsbNetPut32(Data + 12, 1);
    UsbNetPut32(Data + 16, 0);
    UsbNetPut32(Data + 20, USBNET_RX_SIZE);
    Status = UsbNetRndisRequest(Adapter, 24, &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    UsbNetRndisHeader(Adapter, RNDIS_QUERY_MSG, RNDIS_REQUEST_HEADER);
    UsbNetPut32(Data + 12, RNDIS_OID_PERMANENT_ADDRESS);
    if (NT_SUCCESS(UsbNetRndisRequest(Adapter, RNDIS_REQUEST_HEADER, &Got)) && Got >= 24)
    {
        Size = UsbNetGet32(Data + 16);
        Offset = UsbNetGet32(Data + 20) + 8;
        if (Size >= MAC_ADDRESS_SIZE && Offset <= Got && MAC_ADDRESS_SIZE <= Got - Offset)
            UsbNetSetAddress(Adapter, Data + Offset);
    }

    UsbNetRndisHeader(Adapter, RNDIS_SET_MSG, RNDIS_REQUEST_HEADER + sizeof(ULONG));
    UsbNetPut32(Data + 12, RNDIS_OID_PACKET_FILTER);
    UsbNetPut32(Data + 16, sizeof(ULONG));
    UsbNetPut32(Data + 20, RNDIS_REQUEST_HEADER - 8);
    UsbNetPut32(Data + RNDIS_REQUEST_HEADER, RNDIS_FILTER_DEFAULT);
    Status = UsbNetRndisRequest(Adapter, RNDIS_REQUEST_HEADER + sizeof(ULONG), &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    Adapter->TxHeader = RNDIS_PACKET_HEADER;
    return STATUS_SUCCESS;
}

static
UCHAR
UsbNetHexDigit(
    _In_ USHORT Character)
{
    if (Character >= L'0' && Character <= L'9')
        return (UCHAR)(Character - L'0');

    if (Character >= L'A' && Character <= L'F')
        return (UCHAR)(Character - L'A' + 10);

    if (Character >= L'a' && Character <= L'f')
        return (UCHAR)(Character - L'a' + 10);

    return 0xFF;
}

static
VOID
UsbNetReadMacString(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Data = Adapter->Control;
    UCHAR Address[MAC_ADDRESS_SIZE], High, Low;
    ULONG Got, i;

    if (!Adapter->Function.MacIndex ||
        !NT_SUCCESS(XhciControl(Adapter, USB_ENDPOINT_DIRECTION_MASK, USB_REQUEST_GET_DESCRIPTOR,
                                (USB_STRING_DESCRIPTOR_TYPE << 8) | Adapter->Function.MacIndex,
                                USB_LANGUAGE_ENGLISH, USB_MAC_STRING_SIZE, &Got)) ||
        Got < USB_MAC_STRING_SIZE || Data[1] != USB_STRING_DESCRIPTOR_TYPE)
    {
        return;
    }

    for (i = 0; i < MAC_ADDRESS_SIZE; i++)
    {
        High = UsbNetHexDigit(UsbNetGet16(Data + 2 + i * 4));
        Low = UsbNetHexDigit(UsbNetGet16(Data + 4 + i * 4));
        if (High > 0xF || Low > 0xF)
            return;

        Address[i] = (UCHAR)((High << 4) | Low);
    }

    UsbNetSetAddress(Adapter, Address);
}

static
NTSTATUS
UsbNetStartNcm(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Data = Adapter->Control;
    NTSTATUS Status;
    ULONG Got, Divisor, Remainder, Alignment, Header;

    Status = XhciControl(Adapter, USB_CLASS_IN, CDC_GET_NTB_PARAMETERS, 0,
                         Adapter->Function.ControlInterface, CDC_NTB_PARAMETERS_SIZE, &Got);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Got < CDC_NTB_PARAMETERS_SIZE)
        return STATUS_DEVICE_DATA_ERROR;

    Adapter->NcmOutMaxSize = min(UsbNetGet32(Data + 16), USBNET_TX_SIZE);
    Divisor = max(UsbNetGet16(Data + 20), 1);
    Remainder = UsbNetGet16(Data + 22) % Divisor;
    Alignment = UsbNetGet16(Data + 24);
    if (Alignment < sizeof(ULONG) || (Alignment & (Alignment - 1)) || Alignment > USBNET_TX_SIZE / 4)
        Alignment = sizeof(ULONG);

    if (UsbNetGet32(Data + 4) > USBNET_RX_SIZE)
    {
        UsbNetPut32(Data, USBNET_RX_SIZE);
        UsbNetPut32(Data + 4, 0);
        Status = XhciControl(Adapter, USB_CLASS_OUT, CDC_SET_NTB_INPUT_SIZE, 0,
                             Adapter->Function.ControlInterface,
                             (Adapter->Function.NcmCapabilities & CDC_NCM_INPUT_SIZE_8) ? 8 : 4, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Adapter->NcmNdpOffset = (USHORT)((NCM_NTH16_SIZE + Alignment - 1) & ~(Alignment - 1));
    Header = Adapter->NcmNdpOffset + NCM_NDP16_SIZE;
    if (Divisor > USBNET_TX_SIZE / 4)
        return STATUS_NOT_SUPPORTED;

    while (Header % Divisor != Remainder)
        Header++;

    if (Header + USBNET_MAX_FRAME > Adapter->NcmOutMaxSize)
        return STATUS_NOT_SUPPORTED;

    Adapter->TxHeader = Header;
    return STATUS_SUCCESS;
}

NTSTATUS
UsbNetStartFunction(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUSBNET_FUNCTION Function = &Adapter->Function;
    NTSTATUS Status;

    Status = XhciControl(Adapter, 0, USB_REQUEST_SET_CONFIGURATION, Function->Configuration, 0, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Adapter->BulkIn.Address = Function->InAddress;
    Adapter->BulkIn.MaxPacket = Function->InPacket;
    Adapter->BulkIn.MaxBurst = Function->InBurst;
    Adapter->BulkOut.Address = Function->OutAddress;
    Adapter->BulkOut.MaxPacket = Function->OutPacket;
    Adapter->BulkOut.MaxBurst = Function->OutBurst;

    if (Function->Protocol == UsbNetNcm)
    {
        Status = UsbNetStartNcm(Adapter);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = XhciConfigureBulk(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Function->DataAlternate)
    {
        Status = XhciControl(Adapter, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE,
                             Function->DataAlternate, Function->DataInterface, 0, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    switch (Function->Protocol)
    {
        case UsbNetRndis:
            return UsbNetStartRndis(Adapter);

        case UsbNetEcm:
        case UsbNetNcm:
            UsbNetReadMacString(Adapter);
            XhciControl(Adapter, USB_CLASS_OUT, CDC_SET_ETHERNET_PACKET_FILTER,
                        CDC_PACKET_TYPE_DIRECTED | CDC_PACKET_TYPE_BROADCAST,
                        Function->ControlInterface, 0, NULL);
            if (Function->Protocol == UsbNetEcm)
                Adapter->TxHeader = 0;
            return STATUS_SUCCESS;

        case UsbNetEem:
            Adapter->TxHeader = EEM_HEADER_SIZE;
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
BOOLEAN
UsbNetSetFrame(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (Length < USBNET_MIN_FRAME || Length > USBNET_MAX_FRAME ||
        Offset > Adapter->RxLength || Length > Adapter->RxLength - Offset)
    {
        return FALSE;
    }

    Adapter->RxFrame = Adapter->RxBuffer + Offset;
    Adapter->RxFrameLength = Length;
    return TRUE;
}

static
BOOLEAN
UsbNetNextRndis(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Message;
    ULONG Length, Offset, Size;

    while (Adapter->RxOffset + 8 <= Adapter->RxLength)
    {
        Message = Adapter->RxBuffer + Adapter->RxOffset;
        Length = UsbNetGet32(Message + 4);
        if (Length < 8 || Length > Adapter->RxLength - Adapter->RxOffset)
            break;

        Adapter->RxOffset += Length;
        if (UsbNetGet32(Message) != RNDIS_PACKET_MSG || Length < RNDIS_PACKET_HEADER)
            continue;

        Offset = UsbNetGet32(Message + 8);
        Size = UsbNetGet32(Message + 12);
        if (Offset > Length - 8 || Size > Length - 8 - Offset)
            continue;

        if (UsbNetSetFrame(Adapter, (ULONG)(Message - Adapter->RxBuffer) + 8 + Offset, Size))
            return TRUE;
    }

    Adapter->RxOffset = Adapter->RxLength;
    return FALSE;
}

static
BOOLEAN
UsbNetSelectNdp(
    _In_ PUSBNET_ADAPTER Adapter,
    _In_ ULONG Ndp)
{
    PUCHAR Data = Adapter->RxBuffer + Ndp;
    ULONG Signature, Length;

    Adapter->RxNdp = 0;
    if (!Ndp || Ndp + NCM_NDP16_SIZE > Adapter->RxLength)
        return FALSE;

    Signature = UsbNetGet32(Data);
    Length = UsbNetGet16(Data + 4);
    if ((Signature != NCM_NDP16_SIGNATURE && Signature != NCM_NDP16_CRC_SIGNATURE) ||
        Length < NCM_NDP16_SIZE || Ndp + Length > Adapter->RxLength)
    {
        return FALSE;
    }

    Adapter->RxNdp = Ndp;
    Adapter->RxOffset = Ndp + 8;
    return TRUE;
}

static
BOOLEAN
UsbNetNextNcm(
    _In_ PUSBNET_ADAPTER Adapter)
{
    PUCHAR Data = Adapter->RxBuffer;
    ULONG Block, End, Index, Size, Next;

    if (!Adapter->RxOffset)
    {
        Adapter->RxOffset = Adapter->RxLength;
        if (Adapter->RxLength < NCM_NTH16_SIZE || UsbNetGet32(Data) != NCM_NTH16_SIGNATURE ||
            UsbNetGet16(Data + 4) != NCM_NTH16_SIZE)
        {
            return FALSE;
        }

        Block = UsbNetGet16(Data + 8);
        if (Block && Block < Adapter->RxLength)
            Adapter->RxLength = Block;

        if (!UsbNetSelectNdp(Adapter, UsbNetGet16(Data + 10)))
            return FALSE;
    }

    while (Adapter->RxNdp)
    {
        End = Adapter->RxNdp + UsbNetGet16(Data + Adapter->RxNdp + 4);
        if (Adapter->RxOffset + 4 <= End)
        {
            Index = UsbNetGet16(Data + Adapter->RxOffset);
            Size = UsbNetGet16(Data + Adapter->RxOffset + 2);
            Adapter->RxOffset += 4;
            if (!Index || !Size)
            {
                Adapter->RxOffset = End;
                continue;
            }

            if (UsbNetSetFrame(Adapter, Index, Size))
                return TRUE;

            continue;
        }

        Next = UsbNetGet16(Data + Adapter->RxNdp + 6);
        if (Next <= Adapter->RxNdp || !UsbNetSelectNdp(Adapter, Next))
            break;
    }

    Adapter->RxNdp = 0;
    Adapter->RxOffset = Adapter->RxLength;
    return FALSE;
}

static
BOOLEAN
UsbNetNextEem(
    _In_ PUSBNET_ADAPTER Adapter)
{
    ULONG Header, Size, Command;

    while (Adapter->RxOffset + EEM_HEADER_SIZE <= Adapter->RxLength)
    {
        Header = UsbNetGet16(Adapter->RxBuffer + Adapter->RxOffset);
        Adapter->RxOffset += EEM_HEADER_SIZE;
        if (Header & EEM_COMMAND)
        {
            Command = (Header >> EEM_COMMAND_SHIFT) & EEM_COMMAND_MASK;
            if (Command == 0 || Command == 1)
                Adapter->RxOffset += Header & EEM_COMMAND_LENGTH;

            continue;
        }

        Size = Header & EEM_LENGTH_MASK;
        if (!Size)
            continue;

        if (Size < EEM_CRC_SIZE || Size > Adapter->RxLength - Adapter->RxOffset)
            break;

        Adapter->RxOffset += Size;
        if (UsbNetSetFrame(Adapter, Adapter->RxOffset - Size, Size - EEM_CRC_SIZE))
            return TRUE;
    }

    Adapter->RxOffset = Adapter->RxLength;
    return FALSE;
}

BOOLEAN
UsbNetNextFrame(
    _In_ PUSBNET_ADAPTER Adapter)
{
    switch (Adapter->Function.Protocol)
    {
        case UsbNetRndis:
            return UsbNetNextRndis(Adapter);

        case UsbNetNcm:
            return UsbNetNextNcm(Adapter);

        case UsbNetEem:
            return UsbNetNextEem(Adapter);

        case UsbNetEcm:
            if (Adapter->RxOffset)
                return FALSE;

            Adapter->RxOffset = Adapter->RxLength;
            return UsbNetSetFrame(Adapter, 0, Adapter->RxLength);

        default:
            return FALSE;
    }
}

ULONG
UsbNetFrameTransmit(
    _In_ PUSBNET_ADAPTER Adapter,
    _Inout_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Total, Ndp, i;

    switch (Adapter->Function.Protocol)
    {
        case UsbNetRndis:
            for (i = 0; i < RNDIS_PACKET_HEADER; i++)
                Buffer[i] = 0;

            Total = RNDIS_PACKET_HEADER + Length;
            UsbNetPut32(Buffer, RNDIS_PACKET_MSG);
            UsbNetPut32(Buffer + 4, Total);
            UsbNetPut32(Buffer + 8, RNDIS_PACKET_HEADER - 8);
            UsbNetPut32(Buffer + 12, Length);
            return Total;

        case UsbNetEcm:
            return Length;

        case UsbNetEem:
            UsbNetPut16(Buffer, Length + EEM_CRC_SIZE);
            for (i = 0; i < EEM_CRC_SIZE; i++)
                Buffer[EEM_HEADER_SIZE + Length + i] = EemSentinel[i];

            return EEM_HEADER_SIZE + Length + EEM_CRC_SIZE;

        case UsbNetNcm:
            Total = Adapter->TxHeader + Length;
            if (Total > Adapter->NcmOutMaxSize)
                return 0;

            for (i = 0; i < Adapter->TxHeader; i++)
                Buffer[i] = 0;

            Ndp = Adapter->NcmNdpOffset;
            UsbNetPut32(Buffer, NCM_NTH16_SIGNATURE);
            UsbNetPut16(Buffer + 4, NCM_NTH16_SIZE);
            UsbNetPut16(Buffer + 6, Adapter->NcmSequence++);
            UsbNetPut16(Buffer + 8, Total);
            UsbNetPut16(Buffer + 10, Ndp);
            UsbNetPut32(Buffer + Ndp, NCM_NDP16_SIGNATURE);
            UsbNetPut16(Buffer + Ndp + 4, NCM_NDP16_SIZE);
            UsbNetPut16(Buffer + Ndp + 8, Adapter->TxHeader);
            UsbNetPut16(Buffer + Ndp + 10, Length);
            return Total;

        default:
            return 0;
    }
}
