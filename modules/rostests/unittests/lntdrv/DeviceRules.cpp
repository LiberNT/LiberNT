/*
 * PROJECT:     LiberNT Driver Installer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which device IDs the driver installer searches the catalog for
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include "devices.hpp"

#include <initializer_list>

using namespace lntdrv;

static DeviceInfo Device(const wchar_t *instance, std::initializer_list<const wchar_t *> hardware,
                         std::initializer_list<const wchar_t *> compatible)
{
    DeviceInfo device;

    device.InstanceId = instance;
    for (const wchar_t *id : hardware)
        device.HardwareIds.push_back(id);
    for (const wchar_t *id : compatible)
        device.CompatibleIds.push_back(id);
    return device;
}

static void CheckQueries(const DeviceInfo &device, std::initializer_list<const wchar_t *> expected, bool missing)
{
    std::vector<std::wstring> queries = CatalogQueries(device);
    size_t i = 0;

    ok(queries.size() == expected.size(), "%ls: %u queries, expected %u\n", device.InstanceId.c_str(),
       (unsigned)queries.size(), (unsigned)expected.size());
    for (const wchar_t *id : expected)
    {
        ok(i < queries.size() && queries[i] == id, "%ls: query %u is '%ls', expected '%ls'\n", device.InstanceId.c_str(),
           (unsigned)i, i < queries.size() ? queries[i].c_str() : L"", id);
        i++;
    }
    ok(NeedsDriver(device) == missing, "%ls: NeedsDriver %d, expected %d\n", device.InstanceId.c_str(),
       NeedsDriver(device), missing);
}

START_TEST(DeviceRules)
{
    CheckQueries(Device(L"ACPI\\VEN_PNP&DEV_0C14\\0", { L"ACPI\\VEN_PNP&DEV_0C14", L"ACPI\\PNP0C14", L"*PNP0C14" }, {}),
                 {}, false);
    CheckQueries(Device(L"ACPI\\ACPI0010\\2&CCB86D17&0", { L"ACPI\\VEN_ACPI&DEV_0010", L"ACPI\\ACPI0010", L"*ACPI0010" },
                        {}),
                 {}, false);
    CheckQueries(Device(L"ACPI\\QEMU0002\\2&CCB86D17&0", { L"ACPI\\VEN_QEMU&DEV_0002", L"ACPI\\QEMU0002", L"*QEMU0002" },
                        {}),
                 { L"ACPI\\VEN_QEMU&DEV_0002", L"ACPI\\QEMU0002" }, true);
    CheckQueries(Device(L"ACPI\\BCM2849\\0", { L"ACPI\\VEN_BCM&DEV_2849", L"ACPI\\BCM2849", L"*BCM2849" }, {}),
                 { L"ACPI\\VEN_BCM&DEV_2849", L"ACPI\\BCM2849" }, true);
    CheckQueries(Device(L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086&REV_03\\3&11583659&0&10",
                        { L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086&REV_03", L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086",
                          L"PCI\\VEN_8086&DEV_3185&CC_030000", L"PCI\\VEN_8086&DEV_3185&CC_0300" },
                        { L"PCI\\VEN_8086&DEV_3185&REV_03", L"PCI\\VEN_8086&DEV_3185", L"PCI\\VEN_8086&CC_030000",
                          L"PCI\\VEN_8086&CC_0300", L"PCI\\VEN_8086", L"PCI\\CC_030000", L"PCI\\CC_0300" }),
                 { L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086&REV_03", L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086",
                   L"PCI\\VEN_8086&DEV_3185&CC_030000", L"PCI\\VEN_8086&DEV_3185&CC_0300",
                   L"PCI\\VEN_8086&DEV_3185&REV_03", L"PCI\\VEN_8086&DEV_3185", L"PCI\\VEN_8086&CC_030000",
                   L"PCI\\VEN_8086&CC_0300" },
                 true);
    CheckQueries(Device(L"ROOT\\SYSTEM\\0000", { L"ROOT\\SWENUM" }, {}), {}, false);
}
