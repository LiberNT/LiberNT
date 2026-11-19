/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for mailslot creation, information and message reads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/obfuncs.h>

START_TEST(Mailslot)
{
    WCHAR Name[64];
    HANDLE Server, Duplicate, Client;
    DWORD MaxSize, NextSize, Count, Timeout, Read, Written, Start;
    CHAR Buffer[128], Big[65];

    StringCchPrintfW(Name, _countof(Name), L"\\\\.\\mailslot\\lnt_apitest_%lu", GetCurrentProcessId());

    Server = CreateMailslotW(Name, 64, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateMailslotW failed (%lu)\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE)
        return;

    SetLastError(0xdeadbeef);
    Duplicate = CreateMailslotW(Name, 0, 0, NULL);
    ok(Duplicate == INVALID_HANDLE_VALUE, "A second mailslot with the same name was created\n");
    ok(GetLastError() == ERROR_ALREADY_EXISTS, "Duplicate name error %lu\n", GetLastError());
    if (Duplicate != INVALID_HANDLE_VALUE)
        CloseHandle(Duplicate);

    ok(GetMailslotInfo(Server, &MaxSize, &NextSize, &Count, &Timeout), "GetMailslotInfo failed (%lu)\n", GetLastError());
    ok(MaxSize == 64, "MaxMessageSize %lu\n", MaxSize);
    ok(NextSize == MAILSLOT_NO_MESSAGE, "NextSize %lu\n", NextSize);
    ok(Count == 0, "MessageCount %lu\n", Count);
    ok(Timeout == 0, "ReadTimeout %lu\n", Timeout);

    SetLastError(0xdeadbeef);
    ok(!ReadFile(Server, Buffer, sizeof(Buffer), &Read, NULL), "Reading an empty mailslot succeeded\n");
    ok(GetLastError() == ERROR_SEM_TIMEOUT, "Empty read error %lu\n", GetLastError());

    Client = CreateFileW(Name, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(Client != INVALID_HANDLE_VALUE, "Opening the client failed (%lu)\n", GetLastError());
    if (Client == INVALID_HANDLE_VALUE)
    {
        CloseHandle(Server);
        return;
    }

    ok(WriteFile(Client, "hello", 5, &Written, NULL) && Written == 5, "First write failed (%lu)\n", GetLastError());
    ok(WriteFile(Client, "world!", 6, &Written, NULL) && Written == 6, "Second write failed (%lu)\n", GetLastError());

    ok(GetMailslotInfo(Server, NULL, &NextSize, &Count, NULL), "GetMailslotInfo failed (%lu)\n", GetLastError());
    ok(NextSize == 5, "NextSize %lu\n", NextSize);
    ok(Count == 2, "MessageCount %lu\n", Count);

    SetLastError(0xdeadbeef);
    ok(!ReadFile(Server, Buffer, 3, &Read, NULL), "A read into a short buffer succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Short read error %lu\n", GetLastError());

    Read = 0;
    ok(ReadFile(Server, Buffer, sizeof(Buffer), &Read, NULL), "First read failed (%lu)\n", GetLastError());
    ok(Read == 5 && memcmp(Buffer, "hello", 5) == 0, "First read returned %lu bytes\n", Read);
    Read = 0;
    ok(ReadFile(Server, Buffer, sizeof(Buffer), &Read, NULL), "Second read failed (%lu)\n", GetLastError());
    ok(Read == 6 && memcmp(Buffer, "world!", 6) == 0, "Second read returned %lu bytes\n", Read);

    memset(Big, 'x', sizeof(Big));
    SetLastError(0xdeadbeef);
    ok(!WriteFile(Client, Big, sizeof(Big), &Written, NULL), "A message above the maximum size was written\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Oversize write error %lu\n", GetLastError());

    ok(SetMailslotInfo(Server, 20), "SetMailslotInfo failed (%lu)\n", GetLastError());
    ok(GetMailslotInfo(Server, NULL, NULL, NULL, &Timeout), "GetMailslotInfo failed (%lu)\n", GetLastError());
    ok(Timeout == 20, "ReadTimeout %lu\n", Timeout);
    Start = GetTickCount();
    SetLastError(0xdeadbeef);
    ok(!ReadFile(Server, Buffer, sizeof(Buffer), &Read, NULL), "Reading an empty mailslot succeeded\n");
    ok(GetLastError() == ERROR_SEM_TIMEOUT, "Timed read error %lu\n", GetLastError());
    ok(GetTickCount() - Start < 2000, "The timed read took %lu ms\n", GetTickCount() - Start);

    {
        OBJECT_BASIC_INFORMATION Basic;
        HANDLE Reduced;

        ok(NT_SUCCESS(NtQueryObject(Server, ObjectBasicInformation, &Basic, sizeof(Basic), NULL)), "NtQueryObject failed\n");
        ok(Basic.GrantedAccess == (FILE_GENERIC_READ | WRITE_DAC), "Server handle access 0x%lx\n", Basic.GrantedAccess);
        ok(DuplicateHandle(GetCurrentProcess(), Server, GetCurrentProcess(), &Reduced, 0, FALSE, 0),
           "DuplicateHandle failed (%lu)\n", GetLastError());
        ok(SetMailslotInfo(Reduced, 30), "SetMailslotInfo without access failed (%lu)\n", GetLastError());
        CloseHandle(Reduced);
        ok(GetMailslotInfo(Server, NULL, NULL, NULL, &Timeout), "GetMailslotInfo failed (%lu)\n", GetLastError());
        ok(Timeout == 30, "ReadTimeout %lu\n", Timeout);
    }

    CloseHandle(Client);
    CloseHandle(Server);

    SetLastError(0xdeadbeef);
    Client = CreateFileW(Name, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(Client == INVALID_HANDLE_VALUE, "A client opened a closed mailslot\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "Closed mailslot error %lu\n", GetLastError());
    if (Client != INVALID_HANDLE_VALUE)
        CloseHandle(Client);
}
