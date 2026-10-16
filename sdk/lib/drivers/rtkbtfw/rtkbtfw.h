/*
 * PROJECT:     LiberNT Realtek Bluetooth firmware library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Realtek controller identification and patch download shared by the Bluetooth transports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define RTKBT_MANUFACTURER_REALTEK  0x005D

typedef enum _RTKBT_BUS
{
    RtkBtBusUart,
    RtkBtBusUsb
} RTKBT_BUS;

typedef struct _RTKBT_VERSION
{
    UCHAR HciVersion;
    USHORT HciRevision;
    UCHAR LmpVersion;
    USHORT LmpSubversion;
    USHORT Manufacturer;
} RTKBT_VERSION, *PRTKBT_VERSION;

typedef
NTSTATUS
NTAPI
RTKBT_HCI_COMMAND(
    _In_ PVOID Context,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(ParameterLength) const UCHAR *Parameters,
    _In_ UCHAR ParameterLength,
    _Out_writes_bytes_to_opt_(ResultSize, *ResultLength) PUCHAR Result,
    _In_ ULONG ResultSize,
    _Out_opt_ PULONG ResultLength);
typedef RTKBT_HCI_COMMAND *PRTKBT_HCI_COMMAND;

NTSTATUS
RtkBtReadVersion(
    _In_ PRTKBT_HCI_COMMAND Command,
    _In_ PVOID Context,
    _Out_ PRTKBT_VERSION Version);

NTSTATUS
RtkBtLoadPatch(
    _In_ PRTKBT_HCI_COMMAND Command,
    _In_ PVOID Context,
    _In_ RTKBT_BUS Bus,
    _Out_ PRTKBT_VERSION Version);
