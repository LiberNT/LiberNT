/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Context creation rollback rule
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <ntdef.h>

FORCEINLINE
BOOLEAN
DxgkContextCreateCoreRollbackDestroysMiniport(
    _In_ NTSTATUS DdiCreateStatus)
{
    return NT_SUCCESS(DdiCreateStatus);
}
