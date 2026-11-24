/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Follow the input desktop and inject a client's keyboard and mouse input into it
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#include <windows.h>

typedef struct DESKTOP_INPUT
{
    BOOL Keys[512];
    DWORD MouseButtons;
    ULONGLONG Inputs;
} DESKTOP_INPUT;

typedef struct DESKTOP_THREAD
{
    HDESK Desktop, HomeDesktop;
} DESKTOP_THREAD;

typedef void (*DESKTOP_LOG)(const char *fmt, ...);

int DesktopFollowInput(DESKTOP_THREAD *thread, DESKTOP_LOG log);
void DesktopLeave(DESKTOP_THREAD *thread);
BOOL DesktopKeyboard(DESKTOP_INPUT *input, UINT16 flags, UINT8 code);
BOOL DesktopUnicode(UINT16 flags, UINT16 code);
BOOL DesktopMouse(DESKTOP_INPUT *input, int width, int height, UINT16 flags, UINT16 x, UINT16 y);
void DesktopReleaseInput(DESKTOP_INPUT *input);
