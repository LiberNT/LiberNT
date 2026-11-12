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

#define APPLE_PMP_REQUEST_SHIFT         48
#define APPLE_PMP_REQUEST_ACK           0x1
#define APPLE_PMP_REQUEST_ADDRESS_MASK  0xFFFFFFFFFFFFULL
#define APPLE_PMP_REQUEST_SIZE_MASK     0xFFFFFFULL
#define APPLE_PMP_REQUEST_INDEX_MASK    0xFFFFULL
#define APPLE_PMP_GET_IOVA_TABLE        0x10
#define APPLE_PMP_MALLOC                0x12
#define APPLE_PMP_FREE                  0x14
#define APPLE_PMP_SET_BUF               0x30
#define APPLE_PMP_REGISTER_IOREG        0x32
#define APPLE_PMP_SET_IOREG             0x34
#define APPLE_PMP_UPDATE_IOREG          0x36
#define APPLE_PMP_CONFIRM_IOREG         0x37
#define APPLE_PMP_IOREG_NAME_SIZE       0x30
#define APPLE_PMP_IOREG_SIZE_OFFSET     0x40
#define APPLE_PMP_IOREG_MESSAGE_SIZE    0x44
#define APPLE_PMP_BOOTARGS_OFFSET       0x22c
#define APPLE_PMP_BOOTARGS_SIZE         0x230

FORCEINLINE
ULONG
ApplePmpRequestOpcode(
    _In_ ULONG64 Message)
{
    return (ULONG)(Message >> APPLE_PMP_REQUEST_SHIFT) & 0xff;
}

FORCEINLINE
ULONG64
ApplePmpRequestReply(
    _In_ ULONG Opcode,
    _In_ ULONG64 Payload)
{
    return ((ULONG64)(Opcode | APPLE_PMP_REQUEST_ACK) << APPLE_PMP_REQUEST_SHIFT) | Payload;
}

FORCEINLINE
BOOLEAN
ApplePmpTunableName(
    _In_reads_bytes_(APPLE_PMP_IOREG_NAME_SIZE) const UCHAR *Message,
    _Out_writes_z_(PropertySize) PCHAR Property,
    _In_ SIZE_T PropertySize)
{
    static const CHAR Prefix[] = "apple,tunable-";
    SIZE_T Length = 0;

    while (Length < APPLE_PMP_IOREG_NAME_SIZE && Message[Length])
        Length++;
    if (!Length || sizeof(Prefix) + Length > PropertySize)
        return FALSE;
    RtlCopyMemory(Property, Prefix, sizeof(Prefix) - 1);
    RtlCopyMemory(Property + sizeof(Prefix) - 1, Message, Length);
    Property[sizeof(Prefix) - 1 + Length] = '\0';
    return TRUE;
}
