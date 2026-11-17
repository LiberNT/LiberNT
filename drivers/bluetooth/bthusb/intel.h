/*
 * PROJECT:     LiberNT Bluetooth USB transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Intel USB bootloader initialization
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

NTSTATUS BthUsbIntelInitialize(PBTHUSB_DEVICE Dev);
BOOLEAN BthUsbIntelEvent(PBTHUSB_DEVICE Dev, const UCHAR *Event, ULONG Length);
RTKBT_HCI_COMMAND BthUsbHciCommand;
