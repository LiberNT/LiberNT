/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Device-tree debug device handed to an extensibility module as its OEM data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _KD_FDT_DEVICE
{
    const VOID *Blob;
    ULONG Size;
    ULONG Node;
    ULONG Parent;
} KD_FDT_DEVICE, *PKD_FDT_DEVICE;
