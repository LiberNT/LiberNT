/*
 * PROJECT:     LiberNT Apple Power Management Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device power votes of the Apple silicon power management processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define APPLE_PMP_DEVICE_NAME L"\\Device\\ApplePmp"

#define APPLE_PMP_DEVICE_GRAPHICS 5

typedef struct _APPLE_PMP_POWER_REQUEST
{
    USHORT Device;
    BOOLEAN Enable;
} APPLE_PMP_POWER_REQUEST, *PAPPLE_PMP_POWER_REQUEST;

#define IOCTL_APPLE_PMP_SET_POWER CTL_CODE(FILE_DEVICE_UNKNOWN, 0x910, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define APPLE_PMP_COMMAND_MARKER    (1ULL << 53)
#define APPLE_PMP_COMMAND_SHIFT     44
#define APPLE_PMP_SET_POWER         0x0f

FORCEINLINE
ULONG64
ApplePmpPowerMessage(
    _In_ UCHAR Command,
    _In_ USHORT Device,
    _In_ BOOLEAN Enable)
{
    return APPLE_PMP_COMMAND_MARKER | ((ULONG64)Command << APPLE_PMP_COMMAND_SHIFT) |
           ((ULONG64)Device << 40) | ((ULONG64)(Enable ? 1 : 0) << 32);
}

/* The processor answers a power command with the next opcode and no request id of its own. */
FORCEINLINE
ULONG64
ApplePmpPowerReply(
    _In_ UCHAR Command)
{
    return APPLE_PMP_COMMAND_MARKER | ((ULONG64)(Command + 1) << APPLE_PMP_COMMAND_SHIFT);
}
