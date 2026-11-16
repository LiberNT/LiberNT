/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Platform extension plug-in registration block sizes by version
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <acpiioct.h>
#include <pepfx.h>
#include "../../../../ntos/po/pepcore.h"

START_TEST(PoPepVersions)
{
    ok_eq_ulong(PopPepInformationSize(PEP_INFORMATION_V3), (ULONG)sizeof(PEP_INFORMATION));
    ok_eq_ulong(PopPepInformationSize(POP_PEP_VERSION_2), (ULONG)FIELD_OFFSET(PEP_INFORMATION, AcceptAcpiNotification));
    ok_eq_ulong(PopPepInformationSize(1), 0UL);
    ok_eq_ulong(PopPepInformationSize(4), 0UL);

    ok_eq_ulong(PopPepKernelInformationSize(PEP_KERNEL_INFORMATION_V3), (ULONG)sizeof(PEP_KERNEL_INFORMATION));
    ok_eq_ulong(PopPepKernelInformationSize(POP_PEP_VERSION_2),
                (ULONG)FIELD_OFFSET(PEP_KERNEL_INFORMATION, RequestCommon));
    ok_eq_ulong(PopPepKernelInformationSize(1), 0UL);
    ok_eq_ulong(PopPepKernelInformationSize(4), 0UL);
    ok(PopPepKernelInformationSize(POP_PEP_VERSION_2) + sizeof(PVOID) == PopPepKernelInformationSize(PEP_KERNEL_INFORMATION_V3),
       "a version 2 plug-in's block ends where the version 3 common request callback starts\n");
}
