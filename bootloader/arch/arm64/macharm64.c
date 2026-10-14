/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 machine initialization for UEFI
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <bl.h>
#include <arm64.h>
#include <blefi.h>
#include <reactos/arm64/early_uart.h>

/* External UEFI globals */
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
