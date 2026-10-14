/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Startup settings chosen from the boot menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <bmui.h>

#define BM_PROFILE_DEBUGGING          1
#define BM_PROFILE_REMOTE_ASSISTANCE  2

typedef struct _BM_STARTUP_SETTING
{
    PCSTR Text;
    ULONG Profile;
    ULONG Mode;
    ULONG Flags;
} BM_STARTUP_SETTING;

static const BM_STARTUP_SETTING BmStandardSettings[] =
{
    {"Enable debugging", BM_PROFILE_DEBUGGING, NO_OPTION, 0},
    {"Enable boot logging", 0, NO_OPTION, BOOT_LOGGING},
    {"Enable low-resolution video", 0, NO_OPTION, BOOT_VGA_MODE},
    {"Enable Safe Mode", 0, SAFEBOOT, 0},
    {"Enable Safe Mode with Networking", 0, SAFEBOOT_NETWORK, 0},
    {"Enable Safe Mode with Command Prompt", 0, SAFEBOOT_ALTSHELL, 0},
    {"Disable driver signature enforcement", 0, NO_OPTION, BOOT_NO_INTEGRITY_CHECKS},
    {"Disable early launch anti-malware protection", 0, NO_OPTION, BOOT_NO_EARLY_LAUNCH},
    {"Disable automatic restart after failure", 0, NO_OPTION, BOOT_NO_AUTO_REBOOT},
};

static const BM_STARTUP_SETTING BmMoreSettings[] =
{
    {"Enable remote assistance", BM_PROFILE_REMOTE_ASSISTANCE, NO_OPTION, 0},
};

VOID
BmChooseStartupSettings(
    _Out_ PBM_STARTUP Startup)
{
    PCSTR Standard[RTL_NUMBER_OF(BmStandardSettings)];
    PCSTR More[RTL_NUMBER_OF(BmMoreSettings)];
    const BM_STARTUP_SETTING* Setting;
    ULONG Choice, i;

    RtlZeroMemory(Startup, sizeof(*Startup));

    for (i = 0; i < RTL_NUMBER_OF(Standard); i++)
        Standard[i] = BmStandardSettings[i].Text;
    for (i = 0; i < RTL_NUMBER_OF(More); i++)
        More[i] = BmMoreSettings[i].Text;

    switch (BmUiStartupSettings(Standard, RTL_NUMBER_OF(Standard), More, RTL_NUMBER_OF(More), &Choice))
    {
        case BmUiSettingStandard:
            Setting = &BmStandardSettings[Choice];
            break;

        case BmUiSettingMore:
            Setting = &BmMoreSettings[Choice];
            break;

        default:
            return;
    }

    Startup->Profile = Setting->Profile;
    Startup->Mode = Setting->Mode;
    Startup->Flags = Setting->Flags;
}
