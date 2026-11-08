/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the thread and window DPI hosting behavior
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef DPI_HOSTING_BEHAVIOR (WINAPI *PFN_SET_HOSTING)(DPI_HOSTING_BEHAVIOR);
typedef DPI_HOSTING_BEHAVIOR (WINAPI *PFN_GET_HOSTING)(VOID);
typedef DPI_HOSTING_BEHAVIOR (WINAPI *PFN_GET_WINDOW_HOSTING)(HWND);

static PFN_SET_HOSTING pSetThreadDpiHostingBehavior;
static PFN_GET_HOSTING pGetThreadDpiHostingBehavior;
static PFN_GET_WINDOW_HOSTING pGetWindowDpiHostingBehavior;

static
HWND
CreateTestWindow(HWND Parent)
{
    return CreateWindowExW(0, L"STATIC", L"DpiHostingBehavior",
                           Parent ? WS_CHILD : WS_OVERLAPPEDWINDOW,
                           0, 0, 100, 100, Parent, NULL, NULL, NULL);
}

static
DPI_AWARENESS
WindowAwareness(HWND Window)
{
    return GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(Window));
}

START_TEST(DpiHostingBehavior)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    HWND Mixed, Default, MixedChild, DefaultChild;
    DPI_AWARENESS_CONTEXT Previous;

    pSetThreadDpiHostingBehavior = (PFN_SET_HOSTING)GetProcAddress(User32, "SetThreadDpiHostingBehavior");
    pGetThreadDpiHostingBehavior = (PFN_GET_HOSTING)GetProcAddress(User32, "GetThreadDpiHostingBehavior");
    pGetWindowDpiHostingBehavior = (PFN_GET_WINDOW_HOSTING)GetProcAddress(User32, "GetWindowDpiHostingBehavior");
    ok(pSetThreadDpiHostingBehavior && pGetThreadDpiHostingBehavior && pGetWindowDpiHostingBehavior,
       "user32 does not export the DPI hosting behavior functions\n");
    if (!pSetThreadDpiHostingBehavior || !pGetThreadDpiHostingBehavior || !pGetWindowDpiHostingBehavior)
        return;

    ok_int(pGetThreadDpiHostingBehavior(), DPI_HOSTING_BEHAVIOR_DEFAULT);

    SetLastError(0xdeadbeef);
    ok_int(pSetThreadDpiHostingBehavior((DPI_HOSTING_BEHAVIOR)2), DPI_HOSTING_BEHAVIOR_INVALID);
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);
    ok_int(pGetThreadDpiHostingBehavior(), DPI_HOSTING_BEHAVIOR_DEFAULT);

    SetLastError(0xdeadbeef);
    ok_int(pGetWindowDpiHostingBehavior(NULL), DPI_HOSTING_BEHAVIOR_INVALID);
    ok_int(GetLastError(), ERROR_INVALID_PARAMETER);

    Previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ok(Previous != NULL, "SetThreadDpiAwarenessContext failed with %lu\n", GetLastError());

    ok_int(pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED), DPI_HOSTING_BEHAVIOR_DEFAULT);
    ok_int(pGetThreadDpiHostingBehavior(), DPI_HOSTING_BEHAVIOR_MIXED);
    Mixed = CreateTestWindow(NULL);
    ok(Mixed != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    ok_int(pGetWindowDpiHostingBehavior(Mixed), DPI_HOSTING_BEHAVIOR_MIXED);

    ok_int(pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_DEFAULT), DPI_HOSTING_BEHAVIOR_MIXED);
    Default = CreateTestWindow(NULL);
    ok(Default != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    ok_int(pGetWindowDpiHostingBehavior(Default), DPI_HOSTING_BEHAVIOR_DEFAULT);
    ok_int(pGetWindowDpiHostingBehavior(Mixed), DPI_HOSTING_BEHAVIOR_MIXED);

    /* Only a mixed parent hosts a child of another awareness */
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    MixedChild = CreateTestWindow(Mixed);
    DefaultChild = CreateTestWindow(Default);
    ok(MixedChild && DefaultChild, "CreateWindowExW failed with %lu\n", GetLastError());
    ok_int(WindowAwareness(Mixed), DPI_AWARENESS_PER_MONITOR_AWARE);
    ok_int(WindowAwareness(MixedChild), DPI_AWARENESS_UNAWARE);
    ok_int(WindowAwareness(DefaultChild), DPI_AWARENESS_PER_MONITOR_AWARE);

    SetThreadDpiAwarenessContext(Previous);
    DestroyWindow(Mixed);
    DestroyWindow(Default);
}
