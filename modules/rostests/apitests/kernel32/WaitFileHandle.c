/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test waiting on file handles, as cmd's PAUSE does on redirected input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(WaitFileHandle)
{
    HANDLE File, Event, Handles[2];
    DWORD Read = 0xdead, Mode = 0xdead;
    CHAR Byte;
    BOOL Success;

    File = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(NUL) failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Handles[0] = File;
    Handles[1] = Event;

    SetLastError(0xdeadbeef);
    ok(!GetConsoleMode(File, &Mode), "GetConsoleMode(NUL) succeeded with mode 0x%lx\n", Mode);
    ok_long(GetLastError(), ERROR_INVALID_HANDLE);

    ok_long(WaitForSingleObject(File, 0), WAIT_OBJECT_0);
    ok_long(WaitForMultipleObjects(2, Handles, FALSE, 0), WAIT_OBJECT_0);

    Success = ReadFile(File, &Byte, 1, &Read, NULL);
    ok(Success, "ReadFile(NUL) failed with %lu\n", GetLastError());
    ok_long(Read, 0);

    ok_long(WaitForSingleObject(File, 0), WAIT_OBJECT_0);
    ok_long(WaitForMultipleObjects(2, Handles, FALSE, 0), WAIT_OBJECT_0);

    CloseHandle(Event);
    CloseHandle(File);

    File = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(NUL) failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    SetLastError(0xdeadbeef);
    Success = DeviceIoControl(File, CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS), NULL, 0,
                              &Mode, sizeof(Mode), &Read, NULL);
    ok(!Success, "DeviceIoControl(NUL) succeeded\n");
    ok_long(GetLastError(), ERROR_INVALID_FUNCTION);
    ok_long(WaitForSingleObject(File, 0), WAIT_TIMEOUT);
    CloseHandle(File);
}
