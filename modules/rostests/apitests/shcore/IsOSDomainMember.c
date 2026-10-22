/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests IsOS(OS_DOMAINMEMBER) against the LSA primary domain
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <shlwapi.h>
#include <ntsecapi.h>

START_TEST(IsOSDomainMember)
{
    LSA_OBJECT_ATTRIBUTES Attributes = { sizeof(Attributes) };
    POLICY_PRIMARY_DOMAIN_INFO *Info;
    LSA_HANDLE Policy;
    BOOL Expected;
    NTSTATUS Status;

    Status = LsaOpenPolicy(NULL, &Attributes, POLICY_VIEW_LOCAL_INFORMATION, &Policy);
    ok(Status == 0, "LsaOpenPolicy failed, status 0x%lx\n", Status);
    if (Status)
        return;
    Status = LsaQueryInformationPolicy(Policy, PolicyPrimaryDomainInformation, (PVOID *)&Info);
    ok(Status == 0, "LsaQueryInformationPolicy failed, status 0x%lx\n", Status);
    if (!Status)
    {
        Expected = Info->Sid != NULL;
        ok(IsOS(OS_DOMAINMEMBER) == Expected, "IsOS(OS_DOMAINMEMBER) is not %d\n", Expected);
        LsaFreeMemory(Info);
    }
    LsaClose(Policy);
}
