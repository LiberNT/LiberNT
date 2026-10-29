/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WFP listen authorization of a WSK socket, user-mode part
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <kmt_test.h>

#include "wfp_listen.h"

START_TEST(WfpListen)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"WfpListen", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        return;
    }

    Error = KmtSendToDriver(IOCTL_WFPLISTEN_RUN);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
}
