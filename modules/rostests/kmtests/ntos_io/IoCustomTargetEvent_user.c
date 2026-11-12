/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A custom event a driver reports on a device reaches a window watching a handle to it, with its data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <winreg.h>
#include <dbt.h>

#define RESULT_KEY L"SOFTWARE\\KmtIoCustomTargetEvent"
#define CUSTOM_DATA "KMTEST!!"

static const GUID TestEvent = {0x5a1d3e27, 0x9c4b, 0x4f6e, {0x8b, 0x02, 0x3d, 0x71, 0xc9, 0x48, 0xae, 0x15}};
static BOOLEAN Received;

static
LRESULT
CALLBACK
WindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    PDEV_BROADCAST_HANDLE Event = (PDEV_BROADCAST_HANDLE)LParam;

    if (Message == WM_DEVICECHANGE && WParam == DBT_CUSTOMEVENT && Event != NULL &&
        Event->dbch_devicetype == DBT_DEVTYP_HANDLE && IsEqualGUID(&Event->dbch_eventguid, &TestEvent))
    {
        Received = TRUE;
        ok(Event->dbch_size >= FIELD_OFFSET(DEV_BROADCAST_HANDLE, dbch_data) + sizeof(CUSTOM_DATA) - 1 &&
           memcmp(Event->dbch_data, CUSTOM_DATA, sizeof(CUSTOM_DATA) - 1) == 0,
           "the event carries %lu bytes, not the reported data\n", Event->dbch_size);
        ok_eq_long(Event->dbch_nameoffset, -1L);
        return TRUE;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
BOOLEAN
PublishVolume(VOID)
{
    WCHAR Volume[MAX_PATH];
    LONG Error;
    HKEY Key;

    if (!QueryDosDeviceW(L"C:", Volume, RTL_NUMBER_OF(Volume)))
    {
        ok(FALSE, "C: has no device: %lu\n", GetLastError());
        return FALSE;
    }

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, RESULT_KEY, 0, NULL, REG_OPTION_VOLATILE, KEY_SET_VALUE, NULL, &Key, NULL);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return FALSE;

    Error = RegSetValueExW(Key, L"Device", 0, REG_SZ, (const BYTE *)Volume, (DWORD)(wcslen(Volume) + 1) * sizeof(WCHAR));
    RegCloseKey(Key);
    ok_eq_long(Error, ERROR_SUCCESS);
    return Error == ERROR_SUCCESS;
}

/* The event travels through the user-mode PnP manager; the wait only bounds a lost one */
static
VOID
WaitForEvent(VOID)
{
    DWORD Start = GetTickCount();
    MSG Message;

    while (!Received && GetTickCount() - Start < 2000)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }

    ok(Received, "the custom event did not reach the window\n");
}

START_TEST(IoCustomTargetEvent)
{
    DEV_BROADCAST_HANDLE Filter = { sizeof(Filter), DBT_DEVTYP_HANDLE };
    WNDCLASSW Class = { 0 };
    HDEVNOTIFY Notification;
    HANDLE File;
    HWND Window;

    File = CreateFileW(L"\\\\.\\C:", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "opening C: failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE) return;

    Class.lpfnWndProc = WindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"KmtCustomTargetEvent";
    RegisterClassW(&Class);

    Window = CreateWindowExW(0, Class.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());

    Filter.dbch_handle = File;
    Notification = Window ? RegisterDeviceNotificationW(Window, &Filter, DEVICE_NOTIFY_WINDOW_HANDLE) : NULL;
    ok(Notification != NULL, "RegisterDeviceNotificationW failed: %lu\n", GetLastError());

    if (Notification != NULL && PublishVolume())
    {
        KmtRunKernelTest("IoCustomTargetEventReport");
        WaitForEvent();
    }

    RegDeleteKeyW(HKEY_LOCAL_MACHINE, RESULT_KEY);
    if (Notification != NULL) UnregisterDeviceNotification(Notification);
    if (Window != NULL) DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
    CloseHandle(File);
}
