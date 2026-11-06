/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Fast display mode changes keep every change working and the adapter usable for Direct3D
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <unknwn.h>

#define SWITCH_ROUNDS 24
#define D3D_SDK_VERSION_9 32

typedef IUnknown *(WINAPI *PFN_DIRECT3D_CREATE9)(UINT);

static LONG
SetMode(ULONG Width, ULONG Height)
{
    DEVMODEW Mode;

    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    Mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    Mode.dmPelsWidth = Width;
    Mode.dmPelsHeight = Height;
    return ChangeDisplaySettingsExW(NULL, &Mode, NULL, CDS_FULLSCREEN, NULL);
}

static BOOL
Direct3D9Available(PFN_DIRECT3D_CREATE9 Create)
{
    IUnknown *Object = Create(D3D_SDK_VERSION_9);

    if (Object == NULL)
        return FALSE;
    Object->lpVtbl->Release(Object);
    return TRUE;
}

START_TEST(mode_switch)
{
    static const struct
    {
        ULONG Width;
        ULONG Height;
    } Modes[] = { { 640, 480 }, { 800, 600 } };
    HMODULE Module = LoadLibraryW(L"d3d9.dll");
    PFN_DIRECT3D_CREATE9 Create;
    LONG Result;
    ULONG Round;

    if (Module == NULL)
    {
        skip("d3d9.dll is not available\n");
        return;
    }
    Create = (PFN_DIRECT3D_CREATE9)GetProcAddress(Module, "Direct3DCreate9");
    ok(Create != NULL, "Direct3DCreate9 is not exported\n");
    if (Create == NULL)
        return;
    ok(Direct3D9Available(Create), "Direct3DCreate9 failed before the mode changes\n");
    Result = SetMode(Modes[0].Width, Modes[0].Height);
    if (Result != DISP_CHANGE_SUCCESSFUL)
    {
        skip("This session cannot change the display mode: %ld\n", Result);
        return;
    }
    for (Round = 1; Round < SWITCH_ROUNDS; ++Round)
    {
        Result = SetMode(Modes[Round % ARRAYSIZE(Modes)].Width, Modes[Round % ARRAYSIZE(Modes)].Height);
        ok(Result == DISP_CHANGE_SUCCESSFUL, "round %lu: mode change failed: %ld\n", Round, Result);
        Sleep(Round % 4 * 40);
    }
    Result = ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Restoring the mode failed: %ld\n", Result);
    ok(Direct3D9Available(Create), "Direct3DCreate9 failed after the mode changes\n");
}
