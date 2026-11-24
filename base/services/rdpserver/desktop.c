/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Follow the input desktop and inject a client's keyboard and mouse input into it
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "desktop.h"
#include <freerdp/input.h>

/* Winlogon owns input before logon and while locked; follow the input desktop. */
int DesktopFollowInput(DESKTOP_THREAD *thread, DESKTOP_LOG log)
{
    char inputName[64], currentName[64];
    DWORD length;
    HDESK current = GetThreadDesktop(GetCurrentThreadId());
    HDESK input = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!input) return 0;
    if (GetUserObjectInformationA(input, UOI_NAME, inputName, sizeof(inputName), &length) &&
        GetUserObjectInformationA(current, UOI_NAME, currentName, sizeof(currentName), &length) &&
        !_stricmp(inputName, currentName))
    {
        CloseDesktop(input);
        return 0;
    }
    if (!thread->HomeDesktop) thread->HomeDesktop = current;
    if (!SetThreadDesktop(input))
    {
        log("Cannot switch to the input desktop error=%lu", GetLastError());
        CloseDesktop(input);
        return -1;
    }
    if (thread->Desktop) CloseDesktop(thread->Desktop);
    thread->Desktop = input;
    log("Following input desktop %s", inputName);
    return 1;
}

void DesktopLeave(DESKTOP_THREAD *thread)
{
    if (thread->Desktop && SetThreadDesktop(thread->HomeDesktop)) CloseDesktop(thread->Desktop);
    thread->Desktop = NULL;
}

BOOL DesktopKeyboard(DESKTOP_INPUT *input, UINT16 flags, UINT8 code)
{
    INPUT event = {0};
    unsigned index = code | ((flags & KBD_FLAGS_EXTENDED) ? 256 : 0);
    event.type = INPUT_KEYBOARD;
    event.ki.wScan = code;
    event.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (flags & KBD_FLAGS_EXTENDED) event.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (flags & KBD_FLAGS_RELEASE) event.ki.dwFlags |= KEYEVENTF_KEYUP;
    if (SendInput(1, &event, sizeof(event)) != 1) return FALSE;
    input->Keys[index] = !(flags & KBD_FLAGS_RELEASE);
    ++input->Inputs;
    return TRUE;
}

BOOL DesktopUnicode(UINT16 flags, UINT16 code)
{
    INPUT event = {0};
    event.type = INPUT_KEYBOARD;
    event.ki.wScan = code;
    event.ki.dwFlags = KEYEVENTF_UNICODE;
    if (flags & KBD_FLAGS_RELEASE) event.ki.dwFlags |= KEYEVENTF_KEYUP;
    return SendInput(1, &event, sizeof(event)) == 1;
}

BOOL DesktopMouse(DESKTOP_INPUT *input, int width, int height, UINT16 flags, UINT16 x, UINT16 y)
{
    INPUT event = {0};
    BOOL down = (flags & PTR_FLAGS_DOWN) != 0;
    if (width < 2 || height < 2) return TRUE;
    event.type = INPUT_MOUSE;
    if (x >= width) x = (UINT16)(width - 1);
    if (y >= height) y = (UINT16)(height - 1);
    event.mi.dx = MulDiv(x, 65535, width - 1);
    event.mi.dy = MulDiv(y, 65535, height - 1);
    event.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    if (flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL))
    {
        LONG delta = flags & 0x1ff;
        if (flags & PTR_FLAGS_WHEEL_NEGATIVE) delta -= 0x200;
        event.mi.mouseData = (DWORD)delta;
        event.mi.dwFlags |= (flags & PTR_FLAGS_HWHEEL) ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    }
    else
    {
        if (flags & PTR_FLAGS_BUTTON1) event.mi.dwFlags |= down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        if (flags & PTR_FLAGS_BUTTON2) event.mi.dwFlags |= down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        if (flags & PTR_FLAGS_BUTTON3) event.mi.dwFlags |= down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        if (down) input->MouseButtons |= flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3);
        else input->MouseButtons &= ~(flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3));
    }
    if (SendInput(1, &event, sizeof(event)) != 1) return FALSE;
    ++input->Inputs;
    return TRUE;
}

/* A disconnected controller must not leave Ctrl/Alt or mouse buttons held. */
void DesktopReleaseInput(DESKTOP_INPUT *input)
{
    INPUT event = {0};
    unsigned i;
    for (i = 0; i < ARRAYSIZE(input->Keys); ++i)
        if (input->Keys[i])
        {
            event.type = INPUT_KEYBOARD;
            event.ki.wScan = i & 255;
            event.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            if (i & 256) event.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            SendInput(1, &event, sizeof(event));
            input->Keys[i] = FALSE;
        }
    memset(&event, 0, sizeof(event));
    event.type = INPUT_MOUSE;
    if (input->MouseButtons & PTR_FLAGS_BUTTON1) event.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
    if (input->MouseButtons & PTR_FLAGS_BUTTON2) event.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
    if (input->MouseButtons & PTR_FLAGS_BUTTON3) event.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;
    if (event.mi.dwFlags) SendInput(1, &event, sizeof(event));
    input->MouseButtons = 0;
}
