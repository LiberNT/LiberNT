/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for PhysicalToLogicalPoint
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *FN_PhysicalToLogicalPoint)(HWND, LPPOINT);

START_TEST(PhysicalToLogicalPoint)
{
    FN_PhysicalToLogicalPoint pPhysicalToLogicalPoint;
    HWND hDesktop = GetDesktopWindow();
    HWND hDestroyed;
    POINT pt;
    RECT rc;
    BOOL ret;

    pPhysicalToLogicalPoint = (FN_PhysicalToLogicalPoint)GetProcAddress(GetModuleHandleW(L"user32.dll"), "PhysicalToLogicalPoint");
    ok(pPhysicalToLogicalPoint != NULL, "user32!PhysicalToLogicalPoint is missing\n");
    if (!pPhysicalToLogicalPoint)
        return;

    ok(GetWindowRect(hDesktop, &rc), "GetWindowRect failed, error %lu\n", GetLastError());

    pt.x = rc.left + 10;
    pt.y = rc.top + 10;
    SetLastError(0xdeadbeef);
    ret = pPhysicalToLogicalPoint(hDesktop, &pt);
    ok(ret, "point inside the window: returned %d\n", ret);
    ok(GetLastError() == 0xdeadbeef, "point inside the window: error %lu\n", GetLastError());
    ok(pt.x == rc.left + 10 && pt.y == rc.top + 10, "point inside the window: %ld,%ld\n", pt.x, pt.y);

    pt.x = rc.right + 5000;
    pt.y = rc.bottom + 5000;
    SetLastError(0xdeadbeef);
    ret = pPhysicalToLogicalPoint(hDesktop, &pt);
    ok(!ret, "point outside the window: returned %d\n", ret);
    ok(GetLastError() == 0xdeadbeef, "point outside the window: error %lu\n", GetLastError());
    ok(pt.x == rc.right + 5000 && pt.y == rc.bottom + 5000, "point outside the window: %ld,%ld\n", pt.x, pt.y);

    pt.x = 123;
    pt.y = 456;
    SetLastError(0xdeadbeef);
    ret = pPhysicalToLogicalPoint(NULL, &pt);
    ok(!ret, "NULL window: returned %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "NULL window: error %lu\n", GetLastError());
    ok(pt.x == 123 && pt.y == 456, "NULL window: %ld,%ld\n", pt.x, pt.y);

    hDestroyed = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hDestroyed != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hDestroyed)
        return;
    DestroyWindow(hDestroyed);

    SetLastError(0xdeadbeef);
    ret = pPhysicalToLogicalPoint(hDestroyed, &pt);
    ok(!ret, "destroyed window: returned %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "destroyed window: error %lu\n", GetLastError());
}
