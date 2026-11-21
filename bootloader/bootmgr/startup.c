/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Startup settings chosen from the boot menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>

#define BM_PROFILE_DEBUGGING          1
#if DBG
#define BM_PROFILE_REMOTE_DEBUGGING   2
#endif
#define BM_PROFILE_ISOLATED_DEBUGGING 3

static PCSTR BmStartupNames[] =
{
    "Debugging",
    "Debugging (isolated)",
#if DBG
    "Remote debugging"
#endif
};

static PCSTR BmStartupDescriptions[] =
{
    "Starts LiberNT with the kernel debugger. The log shows on the screen and goes to a log file, "
    "the serial port and the network cable without encryption. A debugger at the other end can "
    "read it and break in. LiberNT maintainers have one.",
    "Like Debugging but LiberNT only opens the disk it starts from. Other disks stay offline and "
    "read-only even if you plug them in later. Nothing on them can be read or changed. When "
    "LiberNT runs from memory it opens no disk at all.",
#if DBG
    "Lets a LiberNT maintainer connect to this computer to help you. The debugger joins the "
    "network and opens an encrypted session. The key to join it shows on the screen. Anyone "
    "with the key controls this PC. Give it only to a maintainer."
#endif
};

static const ULONG BmStartupProfiles[] =
{
    BM_PROFILE_DEBUGGING,
    BM_PROFILE_ISOLATED_DEBUGGING,
#if DBG
    BM_PROFILE_REMOTE_DEBUGGING
#endif
};

C_ASSERT(RTL_NUMBER_OF(BmStartupNames) == RTL_NUMBER_OF(BmStartupProfiles));
C_ASSERT(RTL_NUMBER_OF(BmStartupDescriptions) == RTL_NUMBER_OF(BmStartupProfiles));

BOOLEAN
BmChooseStartupSettings(
    _In_ BOOLEAN Graphical,
    _In_reads_(ToolCount) PCSTR* ToolNames,
    _In_ ULONG ToolCount,
    _Out_ PBM_STARTUP Startup,
    _Out_ PULONG Tool)
{
    PCSTR Names[RTL_NUMBER_OF(BmStartupProfiles) + BM_MAX_TOOLS + 1];
    PCSTR Descriptions[RTL_NUMBER_OF(BmStartupProfiles) + BM_MAX_TOOLS + 1] = { NULL };
    ULONG Choice = 0, Count = 0, i;
    BOOLEAN Chosen;

    RtlZeroMemory(Startup, sizeof(*Startup));
    *Tool = MAXULONG;

    for (i = 0; i < RTL_NUMBER_OF(BmStartupProfiles); i++)
    {
        Descriptions[Count] = BmStartupDescriptions[i];
        Names[Count++] = BmStartupNames[i];
    }

    for (i = 0; i < ToolCount && i < BM_MAX_TOOLS; i++)
        Names[Count++] = ToolNames[i];

    Names[Count] = "Back";

    if (Graphical)
    {
        UiMenuDescriptions = Descriptions;
        Chosen = UiDisplayMenu("Startup options", NULL, Names, Count + 1,
                               0, -1, &Choice, TRUE, NULL, NULL);
        UiMenuDescriptions = NULL;
        if (!Chosen)
            return FALSE;
    }
    else if (!BmUiStartupSettings(Names, Descriptions, Count, &Choice))
    {
        return FALSE;
    }

    if (Choice < RTL_NUMBER_OF(BmStartupProfiles))
    {
        Startup->Profile = BmStartupProfiles[Choice];
        return TRUE;
    }

    if (Choice >= Count)
        return FALSE;

    *Tool = Choice - RTL_NUMBER_OF(BmStartupProfiles);
    return TRUE;
}
