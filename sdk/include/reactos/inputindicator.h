/*
 * PROJECT:     LiberNT CTF
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Private protocol between msctf and the input indicator
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#define INPUTINDICATOR_TIP_WNDCLASS L"LiberNTTipInputModeWnd"

#define INPUTINDICATOR_COPYDATA_ID 0x4C4E4949

#define WM_INPUTINDICATOR_REFRESH (WM_USER + 1)
#define WM_INPUTINDICATOR_CLICK   (WM_USER + 2)

#define INPUTINDICATOR_CLICK_LEFT  0
#define INPUTINDICATOR_CLICK_RIGHT 1

typedef struct tagINPUTINDICATOR_TIPSTATE
{
    DWORD cbSize;
    DWORD dwThreadId;
    ULONG64 hwndTip;
    ULONG64 hIcon;
    WCHAR szTooltip[128];
} INPUTINDICATOR_TIPSTATE, *PINPUTINDICATOR_TIPSTATE;
