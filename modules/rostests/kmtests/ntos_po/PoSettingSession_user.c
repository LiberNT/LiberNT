/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A session's power setting callback reached from a thread outside every session
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>

/* The kernel part's setting (PoSettingSession.c) */
static const GUID TestSetting = { 0x6d3c1d2e, 0x4f0a, 0x4b8e, { 0x9b, 0x5a, 0x1c, 0x2d, 0x3e, 0x4f, 0x5a, 0x6b } };

START_TEST(PoSettingSession)
{
    HPOWERNOTIFY Notify;
    HWND Window;

    Window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (Window == NULL)
        return;

    /* The window manager watches the setting with a callback of its own, in session space */
    Notify = RegisterPowerSettingNotification(Window, &TestSetting, DEVICE_NOTIFY_WINDOW_HANDLE);
    ok(Notify != NULL, "RegisterPowerSettingNotification failed with %lu\n", GetLastError());

    KmtRunKernelTest("PoSettingSession");

    if (Notify != NULL)
        ok(UnregisterPowerSettingNotification(Notify), "UnregisterPowerSettingNotification failed with %lu\n", GetLastError());
    DestroyWindow(Window);
}
