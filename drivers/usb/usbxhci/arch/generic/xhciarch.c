/*
 * PROJECT:     LiberNT USB xHCI Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Architecture defaults for xHCI controllers with coherent DMA
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../../usbxhci.h"

ULONG
XHCI_ArchDefaultQuirks(VOID)
{
    return 0;
}
