/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HeadlessDispatch replies without a headless terminal
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <kmt_test.h>
#include "../../../../ntos/include/internal/hdl.h"

START_TEST(ExHeadless)
{
    HEADLESS_RSP_QUERY_INFO Info;
    SIZE_T Size;
    NTSTATUS Status;

    RtlFillMemory(&Info, sizeof(Info), 0xA5);
    Size = sizeof(Info);
    Status = HeadlessDispatch(HeadlessCmdQueryInformation,
                              NULL,
                              0,
                              &Info,
                              &Size);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_int(Info.PortType, HeadlessUndefinedPortType);
}
