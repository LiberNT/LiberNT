/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private interface for pending desktop work
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

typedef struct _LNT_DESKTOP_WATCH *PLNT_DESKTOP_WATCH;

typedef enum _LNT_DESKTOP_WATCH_KIND
{
    LntWatchInput = 1,
    LntWatchCompositor,
    LntWatchFramebuffer
} LNT_DESKTOP_WATCH_KIND;

typedef enum _LNT_DESKTOP_WATCH_ACTION
{
    LntWatchArm,
    LntWatchComplete,
    LntWatchProgress,
    LntWatchBeginDraw,
    LntWatchEndDraw,
    LntWatchObserveRead,
    LntWatchAttach,
    LntWatchDetach,
    LntWatchPull,
    LntWatchDirtyFrame,
    LntWatchEmptyFrame,
    LntWatchPresented,
    LntWatchOccluded,
    LntWatchSuspendOutput,
    LntWatchResumeOutput
} LNT_DESKTOP_WATCH_ACTION;

struct _KEVENT;
struct _IO_STATUS_BLOCK;

PLNT_DESKTOP_WATCH NTAPI LntCreateDesktopWatch(LNT_DESKTOP_WATCH_KIND Kind,
                                                struct _KEVENT *ReadEvent,
                                                struct _IO_STATUS_BLOCK *ReadStatus);
VOID NTAPI LntUpdateDesktopWatch(PLNT_DESKTOP_WATCH Watch, LNT_DESKTOP_WATCH_ACTION Action);
VOID NTAPI LntDestroyDesktopWatch(PLNT_DESKTOP_WATCH Watch);
