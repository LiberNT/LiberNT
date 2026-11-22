/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Request shared by the IoIs32bitProcess test driver and its callers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define IO_IS32BIT_PROBE_DEVICE L"Kmtest-IoIs32bitProbe"
#define IOCTL_IO_IS32BIT_PROBE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IO_IS32BIT_KERNEL_PROBE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _IO_IS32BIT_PROBE_RESULT
{
    ULONG NullForm;
    ULONG IrpForm;
    ULONG ProcessIsWow64;
    ULONG KernelIrpForm;
} IO_IS32BIT_PROBE_RESULT, *PIO_IS32BIT_PROBE_RESULT;

C_ASSERT(sizeof(IO_IS32BIT_PROBE_RESULT) == 4 * sizeof(ULONG));
