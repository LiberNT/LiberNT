/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode part of the battery class status wait test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "PoIrp.h"

START_TEST(BatteryClassNotify)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"BatteryClassNotify", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    KmtSendToDriver(IOCTL_RUN_TEST);
    KmtCloseDriver();
    KmtUnloadDriver();
}
