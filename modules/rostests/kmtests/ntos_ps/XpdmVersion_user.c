/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loads the XPDM version test driver, which checks the versions it is told
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(XpdmVersion)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"XpdmVersion", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    KmtCloseDriver();
    KmtUnloadDriver();
}
