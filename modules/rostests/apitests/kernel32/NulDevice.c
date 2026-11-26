/*
 * PROJECT:     LiberNT/RosTests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Tests for the NUL device: it is a bit bucket openable
 *              read/write by every user, not only admins and SYSTEM.
 * COPYRIGHT:   Copyright 2025 LiberNT Contributors
 */

#include "precomp.h"

START_TEST(NulDevice)
{
    HANDLE hNul;
    DWORD Written = 0, Read = 0;
    CHAR Buffer[8] = {0};

    /* Any user can open NUL for read and write: every user-mode
     * redirect to NUL relies on it (win32-openssh's sanitise_stdfd
     * dies when this fails). */
    hNul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
    ok(hNul != INVALID_HANDLE_VALUE, "CreateFile(NUL, rw) failed, gle %lu\n", GetLastError());
    if (hNul == INVALID_HANDLE_VALUE)
        return;

    /* Writes are swallowed whole */
    ok(WriteFile(hNul, "test", 4, &Written, NULL) == TRUE, "WriteFile failed, gle %lu\n", GetLastError());
    ok(Written == 4, "Written = %lu\n", Written);

    /* Reads return end-of-file */
    ok(ReadFile(hNul, Buffer, sizeof(Buffer), &Read, NULL) == TRUE, "ReadFile failed, gle %lu\n", GetLastError());
    ok(Read == 0, "Read = %lu\n", Read);

    CloseHandle(hNul);
}
