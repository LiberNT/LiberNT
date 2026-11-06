/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     xHCI controller shared between the USB network debugger and the OS driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KD_USB_SHARE_SIGNATURE      'SBUK'
#define KD_USB_SHARE_MAX_SLOTS      4
#define KD_USB_SHARE_VALUE          L"Share"
#define KD_USB_SHARE_KEY_PATH       L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\usbxhci\\Debug"

typedef struct _KD_USB_SHARE
{
    ULONG Signature;
    ULONG Size;
    ULONG Port;
    ULONG Interrupter;
    ULONG Phone;
    ULONG SlotCount;
    UCHAR Slots[KD_USB_SHARE_MAX_SLOTS];
    ULONG64 DeviceContexts[KD_USB_SHARE_MAX_SLOTS];
    ULONG64 ContextAddresses[KD_USB_SHARE_MAX_SLOTS];
    ULONG Endpoints[KD_USB_SHARE_MAX_SLOTS];
    ULONG64 Dcbaa;
    ULONG64 ScratchpadArray;
} KD_USB_SHARE, *PKD_USB_SHARE;
