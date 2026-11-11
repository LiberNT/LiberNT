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

static void CheckVersion(const wchar_t *installed, const wchar_t *offered, DriverOrder expected)
{
    DeviceInfo device = Device(L"PCI\\VEN_8086&DEV_31D4&SUBSYS_20758086&REV_03\\3&11583659&0&12",
                               { L"PCI\\VEN_8086&DEV_31D4&SUBSYS_20758086&REV_03", L"PCI\\VEN_8086&DEV_31D4" }, {});

    device.Inf = L"oem12.inf";
    device.Service = L"null";
    device.DriverVersion = installed;
    device.MatchingId = L"PCI\\VEN_8086&DEV_31D4";
    ok(CompareVersionText(device, offered) == expected, "installed %ls, offered %ls: order %d, expected %d\n", installed,
       offered, (int)CompareVersionText(device, offered), (int)expected);
}

static void CheckCatalogSource()
{
    const wchar_t *nicIds[] = { L"PCI\\VEN_8086&DEV_15F3&SUBSYS_00008086&REV_03", L"PCI\\VEN_8086&DEV_15F3&SUBSYS_00008086" };
    DeviceInfo first = Device(L"PCI\\VEN_8086&DEV_15F3&SUBSYS_00008086&REV_03\\3&1D0E7AB&0&1", { nicIds[0], nicIds[1] }, {});
    DeviceInfo second = Device(L"PCI\\VEN_8086&DEV_15F3&SUBSYS_00008086&REV_03\\3&1D0E7AB&0&2", { nicIds[0], nicIds[1] }, {});
    DeviceInfo other = Device(L"PCI\\VEN_10EC&DEV_8168&SUBSYS_86771043&REV_15\\3&1D0E7AB&0&3",
                              { L"PCI\\VEN_10EC&DEV_8168&SUBSYS_86771043&REV_15" }, {});
    std::vector<HistoryEntry> history(1);

    history[0].Action = L"install";
    history[0].InstanceId = first.InstanceId;
    history[0].Inf = L"oem3.inf";
    history[0].HardwareId = L"pci\\ven_8086&dev_15f3&subsys_00008086";
    first.Inf = second.Inf = other.Inf = L"oem3.inf";
    ok(CatalogEntryInUse(first, history) == &history[0], "installed device: no catalog entry\n");
    ok(CatalogEntryInUse(second, history) == &history[0], "identical device the install also reached: no catalog entry\n");
    ok(CatalogEntryInUse(other, history) == NULL, "device without the installed hardware ID: catalog entry\n");
    second.Inf = L"oem4.inf";
    ok(CatalogEntryInUse(second, history) == NULL, "device on another INF: catalog entry\n");
    history[0].Result = ERROR_NO_MORE_ITEMS;
    ok(CatalogEntryInUse(first, history) == NULL, "failed install: catalog entry\n");
}

static void CheckInfClass(const char *klass, const char *guid, bool function)
{
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    char text[512];
    HANDLE file;
    DWORD written;
    int length;

    if (!GetTempPathW(ARRAYSIZE(directory), directory) || !GetTempFileNameW(directory, L"lnt", 0, path))
    {
        skip("no temporary INF for class %s\n", klass);
        return;
    }
    length = sprintf(text,
                     "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\nClass=%s\r\nClassGuid=%s\r\n"
                     "ExtensionId={7C4BC2E8-56F6-4F2B-A2B9-6C1D9A20A41D}\r\nProvider=LiberNT\r\n"
                     "DriverVer=01/01/2026,1.0.0.0\r\n",
                     klass, guid);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "class %s: cannot write the INF (%lu)\n", klass, GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, text, (DWORD)length, &written, NULL);
    CloseHandle(file);
    ok(FunctionDriverInf(path) == function, "class %s: function driver %d, expected %d\n", klass,
       FunctionDriverInf(path), function);
    DeleteFileW(path);
}

START_TEST(DeviceRules)
{
    CheckInfClass("System", "{4d36e97d-e325-11ce-bfc1-08002be10318}", true);
    CheckInfClass("Extension", "{e2f84ce7-8efa-411c-aa69-97454ca4cb57}", false);
    CheckInfClass("SoftwareComponent", "{5c4c3332-344d-483c-8739-259e934c9cc8}", false);

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
    {
        DeviceInfo inbox = Device(L"PCI\\VEN_1AF4&DEV_1000&SUBSYS_00011AF4&REV_00\\3&11583659&0&38",
                                  { L"PCI\\VEN_1AF4&DEV_1000&SUBSYS_00011AF4&REV_00", L"PCI\\VEN_1AF4&DEV_1000&SUBSYS_00011AF4",
                                    L"PCI\\VEN_1AF4&DEV_1000&CC_020000", L"PCI\\VEN_1AF4&DEV_1000&CC_0200" },
                                  { L"PCI\\VEN_1AF4&DEV_1000&REV_00", L"PCI\\VEN_1AF4&DEV_1000", L"PCI\\VEN_1AF4&CC_020000",
                                    L"PCI\\VEN_1AF4&CC_0200", L"PCI\\VEN_1AF4", L"PCI\\CC_020000", L"PCI\\CC_0200" });

        inbox.Inf = L"netkvm.inf";
        inbox.Service = L"netkvm";
        inbox.MatchingId = L"PCI\\VEN_1AF4&DEV_1000&SUBSYS_00011AF4&REV_00";
        CheckQueries(inbox,
                     { L"PCI\\VEN_1AF4&DEV_1000&SUBSYS_00011AF4&REV_00", L"PCI\\VEN_1AF4&DEV_1000&SUBSYS_00011AF4",
                       L"PCI\\VEN_1AF4&DEV_1000&CC_020000", L"PCI\\VEN_1AF4&DEV_1000&CC_0200" },
                     false);
    }

    CheckVersion(L"10.1.13.2", L"10.1.13.3", DriverOrder::Newer);
    CheckVersion(L"10.1.13.2", L"10.1.13.2", DriverOrder::Same);
    CheckVersion(L"10.1.13.2", L"9.18.0.0", DriverOrder::Older);
    CheckVersion(L"31.0.101.2140", L"32.0.101.2140", DriverOrder::Newer);
    CheckVersion(L"65535.0.0.0", L"1.0.0.0", DriverOrder::Older);
    CheckVersion(L"1.65535.0.0", L"2.0.0.0", DriverOrder::Newer);
    CheckCatalogSource();
}
