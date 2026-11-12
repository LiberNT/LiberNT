/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the firmware table validation tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef ACPIVAL_TEST_SHIM_H
#define ACPIVAL_TEST_SHIM_H

#include <stddef.h>
#include <stdint.h>

#define HALP_ACPI_HOST 1

#define IN
#define OUT
#define OPTIONAL

#define _In_
#define _Out_
#define _Out_opt_
#define _In_reads_bytes_(size)
#define _Out_writes_(size)

#define TRUE 1
#define FALSE 0

#define RTL_NUMBER_OF(array) (sizeof(array) / sizeof((array)[0]))

typedef void VOID;
typedef void *PVOID;
typedef const char *PCSTR;
typedef uint8_t UCHAR, *PUCHAR, BOOLEAN;
typedef uint32_t ULONG, *PULONG;

VOID
DbgPrint(
    PCSTR Format,
    ...);

#endif
