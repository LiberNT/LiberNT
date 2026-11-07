/*
 * PROJECT:     LiberNT Storport NVMe miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Platforms whose NVMe controllers are all PCI functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "stornvme.h"

ULONG
NvmeRegisterPlatformAdapters(_In_ PVOID DriverObject, _In_ PVOID RegistryPath, _In_ PHW_INITIALIZATION_DATA PciInitData)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(PciInitData);
    return STATUS_NOT_SUPPORTED;
}
