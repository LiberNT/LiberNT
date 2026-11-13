/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Debug output on the COM1 port of x86 PCs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <cportlib/cportlib.h>
#include <cportlib/uartinfo.h>

#define FW_DEBUG_COM_PORT 1

static CPPORT FwDebugPort;
static BOOLEAN FwDebugPortProbed;

BOOLEAN
FwArchMemoryUsable(
    _In_ ULONGLONG Address,
    _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Size);
    return TRUE;
}

BOOLEAN
FwArchDebugPutChar(
    _In_ UCHAR Character)
{
    if (!FwDebugPortProbed)
    {
        FwDebugPortProbed = TRUE;
        if (!NT_SUCCESS(CpInitialize(&FwDebugPort,
                                     UlongToPtr(BaseArray[FW_DEBUG_COM_PORT]),
                                     DEFAULT_DEBUG_BAUD_RATE)))
        {
            FwDebugPort.Address = NULL;
        }
    }

    if (!FwDebugPort.Address)
        return FALSE;

    CpPutByte(&FwDebugPort, Character);
    return TRUE;
}
