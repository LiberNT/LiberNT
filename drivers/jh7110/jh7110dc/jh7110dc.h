/*
 * PROJECT:     LiberNT StarFive JH7110 display miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     JH7110 DC8200 display controller and HDMI transmitter definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <jh7110disp.h>

#define JH7110DC_TAG                        'cDhJ'
#define JH7110DC_WORKING_SURFACE_COUNT      12UL

#define JH_HDMI_HOTPLUG_POLL_MS             1000

typedef struct _JH7110DC_CONTEXT
{
    PUCHAR Ccache;
    JH7110DISP Display;
    PVOID Scanout;
    PHYSICAL_ADDRESS ScanoutPhysical;
    SIZE_T ScanoutSize;
    ULONG Pitch;
    KEVENT HotPlugStop;
    PVOID HotPlugThread;
    BOOLEAN HotPlug;
    BOOLEAN Running;
} JH7110DC_CONTEXT, *PJH7110DC_CONTEXT;
