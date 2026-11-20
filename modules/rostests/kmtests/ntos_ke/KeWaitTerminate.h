/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Messages between the parts of the test of waits ended by thread termination
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#define IOCTL_WAIT_TERMINATE_WAIT  1
#define IOCTL_WAIT_TERMINATE_QUERY 2

typedef struct _WAIT_TERMINATE_CASE
{
    ULONG Index;
    ULONG Alertable;
    ULONG UserMode;
    ULONG Entered;
    LONG Status;
    ULONG Milliseconds;
} WAIT_TERMINATE_CASE, *PWAIT_TERMINATE_CASE;

#define WAIT_TERMINATE_CASES 4
#define WAIT_TERMINATE_TIMEOUT_MS 3000
