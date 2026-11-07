/*
 * PROJECT:     LiberNT Apple SMC Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface of the Apple silicon System Management Controller driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define APPLE_SMC_DEVICE_NAME L"\\Device\\AppleSmc"

#define APPLE_SMC_KEY(a, b, c, d) (((ULONG)(UCHAR)(a) << 24) | ((ULONG)(UCHAR)(b) << 16) | \
                                   ((ULONG)(UCHAR)(c) << 8) | (ULONG)(UCHAR)(d))

#define APPLE_SMC_MAX_DATA 32

#define IOCTL_APPLE_SMC_READ_KEY  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_APPLE_SMC_WRITE_KEY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_APPLE_SMC_SET_GPIO  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _APPLE_SMC_KEY_DATA
{
    ULONG Key;
    ULONG Length;
    UCHAR Data[APPLE_SMC_MAX_DATA];
} APPLE_SMC_KEY_DATA, *PAPPLE_SMC_KEY_DATA;

typedef struct _APPLE_SMC_GPIO
{
    BOOLEAN LowBank;
    UCHAR Pin;
    BOOLEAN Value;
} APPLE_SMC_GPIO, *PAPPLE_SMC_GPIO;
