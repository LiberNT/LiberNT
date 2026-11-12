/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test SetMessageWaitingIndicator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_SET_MESSAGE_WAITING_INDICATOR)(HANDLE, ULONG);

START_TEST(MessageWaitingIndicator)
{
    PFN_SET_MESSAGE_WAITING_INDICATOR pSetMessageWaitingIndicator;
    HANDLE Indicator;

    pSetMessageWaitingIndicator = (PFN_SET_MESSAGE_WAITING_INDICATOR)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                                    "SetMessageWaitingIndicator");
    if (!pSetMessageWaitingIndicator)
    {
        skip("SetMessageWaitingIndicator is not available\n");
        return;
    }

    Indicator = CreateFileW(L"\\\\.\\MessageIndicator", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
    trace("Opening the message indicator device: %p, error %lu\n", Indicator, GetLastError());

    SetLastError(0xdeadbeef);
    ok_int(pSetMessageWaitingIndicator(Indicator, 1), FALSE);
    ok_long(GetLastError(), ERROR_CALL_NOT_IMPLEMENTED);

    SetLastError(0xdeadbeef);
    ok_int(pSetMessageWaitingIndicator(Indicator, 0), FALSE);
    ok_long(GetLastError(), ERROR_CALL_NOT_IMPLEMENTED);

    SetLastError(0xdeadbeef);
    ok_int(pSetMessageWaitingIndicator(NULL, 3), FALSE);
    ok_long(GetLastError(), ERROR_CALL_NOT_IMPLEMENTED);

    if (Indicator != INVALID_HANDLE_VALUE)
        CloseHandle(Indicator);
}
