/*
 * PROJECT:     LiberNT Bluetooth RFCOMM sockets
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     TDI address definitions shared by bthmini.sys, wshbth.dll and tdihelpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _TDIBTH_H
#define _TDIBTH_H

#define BTHTDI_DEVICE_NAME L"\\Device\\BthTdi"
#define TDI_ADDRESS_TYPE_BTH 32

#pragma pack(push, 1)
typedef struct _TDI_ADDRESS_BTH {
    ULONGLONG btAddr;
    GUID serviceClassId;
    ULONG port;
} TDI_ADDRESS_BTH, *PTDI_ADDRESS_BTH;
#pragma pack(pop)

#define TDI_ADDRESS_LENGTH_BTH sizeof(TDI_ADDRESS_BTH)

#endif
