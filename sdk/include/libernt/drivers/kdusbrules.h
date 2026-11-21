/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     CDC rules of the USB network debugger, kept free of hardware access so they can be tested
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KD_USB_ETHERNET_HEADER          14
#define KD_USB_NOTIFICATION_SIZE        8
#define KD_USB_NOTIFICATION_TYPE        0xA1
#define KD_USB_NOTIFY_NETWORK_CONNECTION 0x00

/* wNdpOutPayloadRemainder aligns the datagram payload, which starts after the Ethernet header */
FORCEINLINE
ULONG
KdUsbNcmDatagramOffset(
    _In_ ULONG Minimum,
    _In_ ULONG Divisor,
    _In_ ULONG Remainder)
{
    while ((Minimum + KD_USB_ETHERNET_HEADER) % Divisor != Remainder)
        Minimum++;

    return Minimum;
}

/* A notification of exactly wMaxPacketSize does not end the transfer, so one transfer can hold several */
FORCEINLINE
BOOLEAN
KdUsbParseNotifications(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Connected)
{
    ULONG Offset = 0;
    BOOLEAN Seen = FALSE;

    *Connected = FALSE;
    while (Offset + KD_USB_NOTIFICATION_SIZE <= Length)
    {
        if (Data[Offset] == KD_USB_NOTIFICATION_TYPE && Data[Offset + 1] == KD_USB_NOTIFY_NETWORK_CONNECTION)
        {
            Seen = TRUE;
            *Connected = Data[Offset + 2] != 0 || Data[Offset + 3] != 0;
        }

        Offset += KD_USB_NOTIFICATION_SIZE + (Data[Offset + 6] | ((ULONG)Data[Offset + 7] << 8));
    }

    return Seen;
}
