/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Startup settings chosen from the boot menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>

#define BM_PROFILE_DEBUGGING          1
#define BM_PROFILE_REMOTE_ASSISTANCE  2

static PCSTR BmStartupNames[] =
{
    "Debugging",
    "Remote assistance",
    "Back"
};

static const ULONG BmStartupProfiles[] =
{
    BM_PROFILE_DEBUGGING,
    BM_PROFILE_REMOTE_ASSISTANCE
};

BOOLEAN
BmChooseStartupSettings(
    _In_ BOOLEAN Graphical,
    _Out_ PBM_STARTUP Startup)
{
    ULONG Choice = 0;

    RtlZeroMemory(Startup, sizeof(*Startup));

    if (Graphical)
    {
        if (!UiDisplayMenu("Startup options", NULL, BmStartupNames, RTL_NUMBER_OF(BmStartupNames),
                           0, -1, &Choice, TRUE, NULL, NULL))
        {
            return FALSE;
        }
    }
    else if (!BmUiStartupSettings(BmStartupNames, RTL_NUMBER_OF(BmStartupProfiles), &Choice))
    {
        return FALSE;
    }

    if (Choice >= RTL_NUMBER_OF(BmStartupProfiles))
        return FALSE;

    Startup->Profile = BmStartupProfiles[Choice];
    return TRUE;
}
