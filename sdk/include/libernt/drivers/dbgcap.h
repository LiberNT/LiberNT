/*
 * PROJECT:     LiberNT Debug Output Capture
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface between the dbgcap driver and dbglog
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define DBGCAP_DEVICE_NAME      L"\\Device\\DbgCap"
#define DBGCAP_DOS_DEVICE_NAME  L"\\DosDevices\\DbgCap"
#define DBGCAP_WIN32_NAME       L"\\\\.\\DbgCap"
#define DBGCAP_SERVICE_NAME     L"DbgCap"

#define IOCTL_DBGCAP_READ \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DBGCAP_REWIND \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _DBGCAP_READ_REQUEST
{
    ULONG TimeoutMs;
} DBGCAP_READ_REQUEST, *PDBGCAP_READ_REQUEST;

typedef struct _DBGCAP_READ_HEADER
{
    ULONG Lost;
    ULONG Count;
} DBGCAP_READ_HEADER, *PDBGCAP_READ_HEADER;

typedef struct _DBGCAP_RECORD
{
    ULONG Size;
    ULONG ProcessId;
    LARGE_INTEGER SystemTime;
    ULONG ComponentId;
    ULONG Level;
    USHORT Length;
    USHORT Reserved;
    CHAR Text[ANYSIZE_ARRAY];
} DBGCAP_RECORD, *PDBGCAP_RECORD;

#define DBGCAP_RECORD_SIZE(Length) \
    ((FIELD_OFFSET(DBGCAP_RECORD, Text) + (Length) + 7) & ~7UL)
