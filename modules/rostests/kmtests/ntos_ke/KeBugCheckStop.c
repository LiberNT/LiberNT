/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops the system on purpose, so that what a stop reports can be checked
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(KeBugCheckStop)
{
    KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0x11, 0x22, 0x33, 0x44);
}
