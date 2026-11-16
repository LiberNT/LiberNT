/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sizes of the platform extension plug-in registration blocks by version
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define POP_PEP_VERSION_2 2

FORCEINLINE
ULONG
PopPepInformationSize(
    _In_ USHORT Version)
{
    if (Version == PEP_INFORMATION_V3)
        return sizeof(PEP_INFORMATION);
    if (Version == POP_PEP_VERSION_2)
        return FIELD_OFFSET(PEP_INFORMATION, AcceptAcpiNotification);
    return 0;
}

FORCEINLINE
ULONG
PopPepKernelInformationSize(
    _In_ USHORT Version)
{
    if (Version == PEP_KERNEL_INFORMATION_V3)
        return sizeof(PEP_KERNEL_INFORMATION);
    if (Version == POP_PEP_VERSION_2)
        return FIELD_OFFSET(PEP_KERNEL_INFORMATION, RequestCommon);
    return 0;
}
