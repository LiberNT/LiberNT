/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Commands shared by the ForegroundAfterHide test and its helper process
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#define FGHIDE_CONTROL_CLASS L"ForegroundAfterHideControl"
#define FGHIDE_WINDOW_CLASS  L"ForegroundAfterHideWindow"
#define FGHIDE_READY_EVENT   L"ForegroundAfterHideReady"

#define FGHIDE_CREATE   (WM_APP + 1)
#define FGHIDE_DESTROY  (WM_APP + 2)
#define FGHIDE_ALLOW    (WM_APP + 3)
#define FGHIDE_ACTIVATE (WM_APP + 4)
#define FGHIDE_SLEEP    (WM_APP + 5)
#define FGHIDE_HIDE     (WM_APP + 6)
#define FGHIDE_DISABLE  (WM_APP + 7)
#define FGHIDE_MINIMIZE (WM_APP + 8)
