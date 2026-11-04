/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the return to the registry display mode after a dynamic mode change
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static BOOL
FindOtherMode(
    _In_ const DEVMODEW *Current,
    _Out_ DEVMODEW *Target)
{
    DEVMODEW Mode = { 0 };
    DWORD Index;

    Mode.dmSize = sizeof(Mode);
    for (Index = 0; EnumDisplaySettingsW(NULL, Index, &Mode); Index++)
    {
        if (Mode.dmBitsPerPel == Current->dmBitsPerPel &&
            (Mode.dmPelsWidth != Current->dmPelsWidth || Mode.dmPelsHeight != Current->dmPelsHeight))
        {
            ZeroMemory(Target, sizeof(*Target));
            Target->dmSize = sizeof(*Target);
            Target->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
            Target->dmBitsPerPel = Mode.dmBitsPerPel;
            Target->dmPelsWidth = Mode.dmPelsWidth;
            Target->dmPelsHeight = Mode.dmPelsHeight;
            return TRUE;
        }
    }
    return FALSE;
}

static void
CheckScreen(
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ PCSTR What)
{
    DEVMODEW Current = { 0 };

    Current.dmSize = sizeof(Current);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Current), "%s: no current settings\n", What);
    ok(Current.dmPelsWidth == Width && Current.dmPelsHeight == Height,
       "%s: current mode %lux%lu, expected %lux%lu\n", What, Current.dmPelsWidth, Current.dmPelsHeight, Width, Height);
    ok(GetSystemMetrics(SM_CXSCREEN) == (int)Width && GetSystemMetrics(SM_CYSCREEN) == (int)Height,
       "%s: screen %dx%d, expected %lux%lu\n", What, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
       Width, Height);
}

START_TEST(ChangeDisplaySettingsReset)
{
    DEVMODEW Registry = { 0 }, After = { 0 }, Current = { 0 }, Target;
    LONG Result;

    Registry.dmSize = After.dmSize = Current.dmSize = sizeof(DEVMODEW);
    ok(EnumDisplaySettingsW(NULL, ENUM_REGISTRY_SETTINGS, &Registry), "No registry settings\n");
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Current), "No current settings\n");
    ok((Registry.dmFields & (DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT)) ==
       (DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT), "Registry fields 0x%lx\n", Registry.dmFields);
    if (Current.dmPelsWidth != Registry.dmPelsWidth || Current.dmPelsHeight != Registry.dmPelsHeight)
    {
        skip("The display already runs a dynamic mode %lux%lu instead of %lux%lu\n",
             Current.dmPelsWidth, Current.dmPelsHeight, Registry.dmPelsWidth, Registry.dmPelsHeight);
        return;
    }
    if (!FindOtherMode(&Current, &Target))
    {
        skip("The display has a single mode\n");
        return;
    }

    Result = ChangeDisplaySettingsExW(NULL, &Target, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Dynamic change to %lux%lu returned %ld\n",
       Target.dmPelsWidth, Target.dmPelsHeight, Result);
    if (Result != DISP_CHANGE_SUCCESSFUL)
        return;
    CheckScreen(Target.dmPelsWidth, Target.dmPelsHeight, "Dynamic mode");
    ok(EnumDisplaySettingsW(NULL, ENUM_REGISTRY_SETTINGS, &After), "No registry settings after the change\n");
    ok(After.dmPelsWidth == Registry.dmPelsWidth && After.dmPelsHeight == Registry.dmPelsHeight,
       "The dynamic change wrote %lux%lu to the registry\n", After.dmPelsWidth, After.dmPelsHeight);

    Result = ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Returning to the registry mode returned %ld\n", Result);
    CheckScreen(Registry.dmPelsWidth, Registry.dmPelsHeight, "ChangeDisplaySettingsExW(NULL)");

    Result = ChangeDisplaySettingsW(&Target, 0);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Second dynamic change returned %ld\n", Result);
    Result = ChangeDisplaySettingsW(NULL, 0);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "ChangeDisplaySettingsW(NULL) returned %ld\n", Result);
    CheckScreen(Registry.dmPelsWidth, Registry.dmPelsHeight, "ChangeDisplaySettingsW(NULL)");

    if (Result != DISP_CHANGE_SUCCESSFUL)
        ChangeDisplaySettingsExW(NULL, &Registry, NULL, 0, NULL);
}
