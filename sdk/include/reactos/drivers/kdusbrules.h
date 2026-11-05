/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     CDC rules of the USB network debugger, kept free of hardware access so they can be tested
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KD_USB_ETHERNET_HEADER          14

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
