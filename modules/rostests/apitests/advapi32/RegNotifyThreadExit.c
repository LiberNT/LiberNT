/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A registry change notification is signaled when the thread that asked for it exits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_KEY L"Software\\LiberNTTest\\RegNotifyThreadExit"

typedef struct _NOTIFY_THREAD
{
    HKEY Key;
    HANDLE Event;
    LONG Error;
} NOTIFY_THREAD, *PNOTIFY_THREAD;

static DWORD WINAPI
NotifyAndExit(LPVOID Parameter)
{
    PNOTIFY_THREAD Data = Parameter;

    Data->Error = RegNotifyChangeKeyValue(Data->Key,
                                          TRUE,
                                          REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                          Data->Event,
                                          TRUE);
    return 0;
}

START_TEST(RegNotifyThreadExit)
{
    NOTIFY_THREAD Data;
    HANDLE Thread;
    HANDLE Event;
    HKEY Key;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_KEY, 0, NULL, REG_OPTION_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &Key, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (Event == NULL)
    {
        RegCloseKey(Key);
        RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
        return;
    }

    Error = RegNotifyChangeKeyValue(Key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                    Event, TRUE);
    ok_long(Error, ERROR_SUCCESS);
    ok_long(WaitForSingleObject(Event, 0), WAIT_TIMEOUT);

    ZeroMemory(&Data, sizeof(Data));
    Data.Key = Key;
    Data.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Data.Event != NULL, "CreateEventW failed: %lu\n", GetLastError());

    Thread = (Data.Event != NULL) ? CreateThread(NULL, 0, NotifyAndExit, &Data, 0, NULL) : NULL;
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread != NULL)
    {
        ok_long(WaitForSingleObject(Thread, 5000), WAIT_OBJECT_0);
        CloseHandle(Thread);

        ok_long(Data.Error, ERROR_SUCCESS);
        ok_long(WaitForSingleObject(Data.Event, 100), WAIT_OBJECT_0);
    }

    if (Data.Event != NULL) CloseHandle(Data.Event);

    RegCloseKey(Key);
    CloseHandle(Event);
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
}
