/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI GOP-specific boot UI helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF (arif.ing@outlook.com)
 */

#pragma once

#include <ntddk.h>
#include <reactos/arc/arc.h>

#ifdef __cplusplus
extern "C" {
#endif

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvGopHandleBootBitmap(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvQueryBgrtInfo(
    _Out_opt_ PLOADER_PARAMETER_BGRT BgrtInfo);

VOID
NTAPI
InbvGopLogoComplete(VOID);

VOID
NTAPI
InbvGopLogoStop(VOID);

BOOLEAN
NTAPI
InbvGopCaptureScreen(VOID);

BOOLEAN
NTAPI
InbvGopDisplayStatus(
    _In_ PCSTR String);

VOID
NTAPI
InbvGopClearStatus(VOID);

#ifdef __cplusplus
}
#endif
