/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Run the FsRtl wildcard expression kmtest against the kernel matchers on the host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

unsigned HostTestsRun;
unsigned HostTestsFailed;
BOOLEAN KmtIsCheckedBuild = FALSE;

#include "FsRtlExpression.c"

int main(void)
{
    Test_FsRtlExpression();
    printf("FsRtlExpression: %u tests executed, %u failures\n", HostTestsRun, HostTestsFailed);
    return HostTestsFailed != 0;
}
