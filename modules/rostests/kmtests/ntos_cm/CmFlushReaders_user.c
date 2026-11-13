/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Registry readers during a held hive flush, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#include "CmFlushReaders.h"

START_TEST(CmFlushReaders)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"CmFlushReaders", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        return;
    }

    Error = KmtSendToDriver(IOCTL_CMFLUSH_REGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_CMFLUSH_RUN);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_CMFLUSH_UNREGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
}
