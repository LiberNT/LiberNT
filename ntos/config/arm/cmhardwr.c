/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Machine-dependent registry configuration for ARM
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    /* TODO: nothing is written about the processors of a 32-bit ARM machine */
    return STATUS_SUCCESS;
}
