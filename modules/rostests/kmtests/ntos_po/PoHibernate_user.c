/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reserving and removing the hibernation file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/pofuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

static
ULONG
HibernateEnabledValue(VOID)
{
    DWORD Value = 0xA5A5A5A5, Size = sizeof(Value);

    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power", L"HibernateEnabled",
                     RRF_RT_REG_DWORD, NULL, &Value, &Size) != ERROR_SUCCESS)
    {
        return 0xA5A5A5A5;
    }
    return Value;
}

static
BOOLEAN
HiberFilePresent(VOID)
{
    SYSTEM_POWER_CAPABILITIES Capabilities;
    NTSTATUS Status;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Status = NtPowerInformation(SystemPowerCapabilities, NULL, 0, &Capabilities, sizeof(Capabilities));
    ok_eq_hex(Status, STATUS_SUCCESS);
    return Capabilities.HiberFilePresent;
}

START_TEST(PoHibernate)
{
    UCHAR Bytes[8] = { 0 };
    PBOOLEAN Enable = (PBOOLEAN)&Bytes[1];
    BOOLEAN Original = FALSE;
    BOOLEAN Ignored;
    BOOLEAN Present;
    NTSTATUS Status;

    Present = HiberFilePresent();
    trace("hibernation file present at the start: %u\n", Present);

    Status = RtlAdjustPrivilege(SE_CREATE_PAGEFILE_PRIVILEGE, FALSE, FALSE, &Original);
    ok_eq_hex(Status, STATUS_SUCCESS);
    *Enable = !Present;
    Status = NtPowerInformation(SystemReserveHiberFile, Enable, sizeof(BOOLEAN), NULL, 0);
    ok(!NT_SUCCESS(Status), "the hibernation file changed without the privilege: 0x%08lx\n", Status);
    ok_eq_uint(HiberFilePresent(), Present);
    Status = RtlAdjustPrivilege(SE_CREATE_PAGEFILE_PRIVILEGE, TRUE, FALSE, &Ignored);
    ok_eq_hex(Status, STATUS_SUCCESS);

    *Enable = FALSE;
    Status = NtPowerInformation(SystemReserveHiberFile, Enable, sizeof(BOOLEAN), NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(!HiberFilePresent(), "the hibernation file is still reported\n");
    ok_eq_ulong(HibernateEnabledValue(), 0UL);

    *Enable = TRUE;
    Status = NtPowerInformation(SystemReserveHiberFile, Enable, sizeof(BOOLEAN), NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(HiberFilePresent(), "the hibernation file is not reported\n");
    ok_eq_ulong(HibernateEnabledValue(), 1UL);

    if (!Present)
    {
        *Enable = FALSE;
        NtPowerInformation(SystemReserveHiberFile, Enable, sizeof(BOOLEAN), NULL, 0);
    }

    RtlAdjustPrivilege(SE_CREATE_PAGEFILE_PRIVILEGE, Original, FALSE, &Ignored);
}
