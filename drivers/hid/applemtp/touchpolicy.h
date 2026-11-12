/*
 * PROJECT:     LiberNT Apple MTP Keyboard and Trackpad Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     When the trackpad firmware may be sent, given the SMC that drives its reset line
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _APPLE_MTP_TOUCH_START
{
    BOOLEAN Waiting;
    ULONG64 WaitStartMs;
} APPLE_MTP_TOUCH_START, *PAPPLE_MTP_TOUCH_START;

BOOLEAN
AppleMtpTouchMayStart(
    _Inout_ PAPPLE_MTP_TOUCH_START Start,
    _In_ BOOLEAN SmcReady,
    _In_ ULONG64 NowMs,
    _In_ ULONG LimitMs);
