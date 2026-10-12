/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 machine initialization for UEFI
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <freeldr.h>
#include <disk.h>
#include <arch/arm64/arm64.h>
#include <uefildr.h>
#include <arch/uefi/machuefi.h>
#include <arch/uefi/uefisym.h>
#include <reactos/arm64/early_uart.h>
#include <debug.h>

/* External UEFI globals */
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
