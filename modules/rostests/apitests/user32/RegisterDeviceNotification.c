/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that registering a window for device notifications does not wait for the window's thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <dbt.h>

typedef struct _REGISTER_CONTEXT
{
    HWND Window;
    HANDLE Done;
    HDEVNOTIFY Notify;
    DWORD Error;
} REGISTER_CONTEXT, *PREGISTER_CONTEXT;

static DWORD WINAPI RegisterThread(PVOID Parameter)
{
    PREGISTER_CONTEXT Context = Parameter;
    DEV_BROADCAST_DEVICEINTERFACE_W Filter;

    ZeroMemory(&Filter, sizeof(Filter));
    Filter.dbcc_size = sizeof(Filter);
    Filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    SetLastError(0xdeadbeef);
    Context->Notify = RegisterDeviceNotificationW(Context->Window, &Filter,
                                                  DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    Context->Error = GetLastError();
    SetEvent(Context->Done);
    return 0;
}

static void PumpUntilSignaled(HANDLE Event, DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Msg;

    while (MsgWaitForMultipleObjects(1, &Event, FALSE, 50, QS_ALLINPUT) != WAIT_OBJECT_0)
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Msg);
        if (GetTickCount() - Start > Milliseconds)
            return;
    }
}

START_TEST(RegisterDeviceNotification)
{
    REGISTER_CONTEXT Context;
    HANDLE Thread;
    DWORD Wait;

    ZeroMemory(&Context, sizeof(Context));
    Context.Window = CreateWindowExW(0, L"STATIC", L"b1", WS_POPUP, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ok(Context.Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!Context.Window)
        return;
    Context.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Context.Done != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (!Context.Done)
    {
        DestroyWindow(Context.Window);
        return;
    }

    Thread = CreateThread(NULL, 0, RegisterThread, &Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread)
    {
        Wait = WaitForSingleObject(Context.Done, 2000);
        ok(Wait == WAIT_OBJECT_0, "RegisterDeviceNotificationW from another thread waited for the window's thread (%lu)\n", Wait);
        if (Wait != WAIT_OBJECT_0)
            PumpUntilSignaled(Context.Done, 5000);
        ok(WaitForSingleObject(Thread, 1000) == WAIT_OBJECT_0, "the registering thread did not finish\n");
        CloseHandle(Thread);
        ok(Context.Notify != NULL, "RegisterDeviceNotificationW failed with %lu\n", Context.Error);
        if (Context.Notify)
            ok(UnregisterDeviceNotification(Context.Notify), "UnregisterDeviceNotification failed with %lu\n", GetLastError());
    }

    CloseHandle(Context.Done);
    DestroyWindow(Context.Window);
}
