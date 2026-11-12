/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A thread-agnostic registry change notification outlives the thread that asked for it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_KEY L"Software\\LiberNTTest\\RegNotifyThreadAgnostic"

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
                                          REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_THREAD_AGNOSTIC,
                                          Data->Event,
                                          TRUE);
    return 0;
}

static
VOID
ChangeKey(
    _In_ HKEY Key)
{
    HKEY SubKey;
    LONG Error;

    Error = RegCreateKeyExW(Key, L"SubKey", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &SubKey, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    RegCloseKey(SubKey);
    RegDeleteKeyW(Key, L"SubKey");
}

START_TEST(RegNotifyThreadAgnostic)
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

    Event = CreateEventW(NULL, TRUE, TRUE, NULL);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (Event == NULL)
    {
        RegCloseKey(Key);
        RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
        return;
    }

    Error = RegNotifyChangeKeyValue(Key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_THREAD_AGNOSTIC, Event, TRUE);
    ok_long(Error, ERROR_SUCCESS);
    ok_long(WaitForSingleObject(Event, 0), WAIT_TIMEOUT);

    ChangeKey(Key);
    ok_long(WaitForSingleObject(Event, 0), WAIT_OBJECT_0);
    RegCloseKey(Key);

    Error = RegOpenKeyExW(HKEY_CURRENT_USER, TEST_KEY, 0, KEY_ALL_ACCESS, &Key);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
    {
        CloseHandle(Event);
        RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
        return;
    }

    ZeroMemory(&Data, sizeof(Data));
    Data.Key = Key;
    Data.Event = Event;
    Data.Error = -1;

    Thread = CreateThread(NULL, 0, NotifyAndExit, &Data, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread != NULL)
    {
        ok_long(WaitForSingleObject(Thread, 5000), WAIT_OBJECT_0);
        CloseHandle(Thread);

        ok_long(Data.Error, ERROR_SUCCESS);
        ok_long(WaitForSingleObject(Event, 0), WAIT_TIMEOUT);

        ChangeKey(Key);
        ok_long(WaitForSingleObject(Event, 0), WAIT_OBJECT_0);
    }

    RegCloseKey(Key);
    CloseHandle(Event);
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
}
