/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loader hook for architectures without hybrid images
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

NTSTATUS
LdrpArchApplyImageView(
    _In_ PVOID ImageBase)
{
    UNREFERENCED_PARAMETER(ImageBase);
    return STATUS_SUCCESS;
}
