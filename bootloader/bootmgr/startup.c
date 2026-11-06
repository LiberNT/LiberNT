/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Startup settings chosen from the boot menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>

#define BM_PROFILE_DEBUGGING          1

static PCSTR BmStartupNames[] =
{
    "Debugging"
};

static const ULONG BmStartupProfiles[] =
{
    BM_PROFILE_DEBUGGING
};

BOOLEAN
BmChooseStartupSettings(
    _In_ BOOLEAN Graphical,
    _In_reads_(ToolCount) PCSTR* ToolNames,
    _In_ ULONG ToolCount,
    _Out_ PBM_STARTUP Startup,
    _Out_ PULONG Tool)
{
    PCSTR Names[RTL_NUMBER_OF(BmStartupProfiles) + BM_MAX_TOOLS + 1];
    ULONG Choice = 0, Count = 0, i;

    RtlZeroMemory(Startup, sizeof(*Startup));
    *Tool = MAXULONG;

    for (i = 0; i < RTL_NUMBER_OF(BmStartupProfiles); i++)
        Names[Count++] = BmStartupNames[i];

    for (i = 0; i < ToolCount && i < BM_MAX_TOOLS; i++)
        Names[Count++] = ToolNames[i];

    Names[Count] = "Back";

    if (Graphical)
    {
        if (!UiDisplayMenu("Startup options", NULL, Names, Count + 1,
                           0, -1, &Choice, TRUE, NULL, NULL))
        {
            return FALSE;
        }
    }
    else if (!BmUiStartupSettings(Names, Count, &Choice))
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
