/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple SN201202x (ACE3) USB-C power delivery controllers over SPMI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/applespmi.h>

#define APPLE_ACE_REG_MODE                  0x03
#define APPLE_ACE_REG_CMD1                  0x08
#define APPLE_ACE_REG_DATA1                 0x09
#define APPLE_ACE_REG_INT_EVENT1            0x14
#define APPLE_ACE_REG_INT_MASK1             0x16
#define APPLE_ACE_REG_INT_CLEAR1            0x18
#define APPLE_ACE_REG_STATUS                0x1a
#define APPLE_ACE_REG_SYSTEM_POWER_STATE    0x20
#define APPLE_ACE_REG_POWER_STATUS          0x3f
#define APPLE_ACE_REG_DATA_STATUS           0x5f

#define APPLE_ACE_INT_PLUG_EVENT            (1ULL << 1)
#define APPLE_ACE_INT_POWER_STATUS_UPDATE   (1ULL << 9)
#define APPLE_ACE_INT_DATA_STATUS_UPDATE    (1ULL << 10)

#define APPLE_ACE_STATUS_PLUG_PRESENT       0x00000001
#define APPLE_ACE_STATUS_UPSIDE_DOWN        0x00000010
#define APPLE_ACE_STATUS_PORT_ROLE          0x00000020
#define APPLE_ACE_STATUS_DATA_ROLE          0x00000040

#define APPLE_ACE_DATA_CONNECTION           0x00000001
#define APPLE_ACE_DATA_USB2                 0x00000010
#define APPLE_ACE_DATA_USB3                 0x00000020
#define APPLE_ACE_DATA_USB_DEVICE_ROLE      0x00000080
#define APPLE_ACE_DATA_DP                   0x00000100

#define APPLE_ACE_POWER_STATE_S0            0x00

#define APPLE_TYPEC_CALLBACK_NAME           L"\\Callback\\AppleTypeCDataRole"
#define APPLE_TYPEC_ROLE_NONE               0
#define APPLE_TYPEC_ROLE_HOST               1
#define APPLE_TYPEC_QUERY                   0x10

typedef struct _APPLE_ACE_PD
{
    PAPPLE_SPMI Spmi;
    UCHAR Sid;
    ULONG WakeInterrupt;
} APPLE_ACE_PD, *PAPPLE_ACE_PD;

typedef struct _APPLE_ACE_PD_STATE
{
    ULONG64 Events;
    ULONG Status;
    ULONG DataStatus;
    USHORT PowerStatus;
} APPLE_ACE_PD_STATE, *PAPPLE_ACE_PD_STATE;

NTSTATUS
NTAPI
AppleAcePdRead(
    _In_ PAPPLE_ACE_PD Pd,
    _In_ UCHAR Register,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length);

NTSTATUS
NTAPI
AppleAcePdWrite(
    _In_ PAPPLE_ACE_PD Pd,
    _In_ UCHAR Register,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length);

NTSTATUS
NTAPI
AppleAcePdCommand(
    _In_ PAPPLE_ACE_PD Pd,
    _In_reads_(4) const CHAR *Command,
    _In_reads_bytes_opt_(InputLength) const VOID *Input,
    _In_ ULONG InputLength);

NTSTATUS
NTAPI
AppleAcePdInitialize(
    _In_ PAPPLE_ACE_PD Pd,
    _Out_opt_ PUCHAR PreviousPowerState);

NTSTATUS
NTAPI
AppleAcePdPoll(
    _In_ PAPPLE_ACE_PD Pd,
    _Out_ PAPPLE_ACE_PD_STATE State);
