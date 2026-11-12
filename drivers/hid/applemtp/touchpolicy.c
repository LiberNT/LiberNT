/*
 * PROJECT:     LiberNT Apple MTP Keyboard and Trackpad Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     When the trackpad firmware may be sent, given the SMC that drives its reset line
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "touchpolicy.h"

BOOLEAN
AppleMtpTouchMayStart(
    _Inout_ PAPPLE_MTP_TOUCH_START Start,
    _In_ BOOLEAN SmcReady,
    _In_ ULONG64 NowMs,
    _In_ ULONG LimitMs)
{
    if (!Start->Waiting)
    {
        Start->Waiting = TRUE;
        Start->WaitStartMs = NowMs;
    }
    ASSERT(NowMs >= Start->WaitStartMs);
    if (!SmcReady && NowMs - Start->WaitStartMs < LimitMs)
        return FALSE;
    Start->Waiting = FALSE;
    return TRUE;
}
