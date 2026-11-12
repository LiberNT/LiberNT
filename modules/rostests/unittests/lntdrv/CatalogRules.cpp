/*
 * PROJECT:     LiberNT Driver Installer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the catalog search page parser and cache, the search terms, the hardware-id filter, the search
 *              throttle wait and the offered version
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include "catalog.hpp"
#include "text.hpp"

using namespace lntdrv;

static const char ResultsPage[] =
    "<table id=\"ctl00_catalogBody_updateMatches\">"
    "<tr id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_R1\">"
    "<td id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_C1_R1\"><a>INTEL - System - 10.1.13.2</a></td>"
    "<td id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_C2_R1\">Windows 10 and later drivers</td>"
    "<td id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_C3_R1\">Drivers (Other Hardware)</td>"
    "<td id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_C4_R1\">7/17/1968</td>"
    "<td id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_C5_R1\">n/a</td>"
    "<td><span id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_size\">23 KB</span>"
    "<span id=\"d4bd3c82-d543-40b2-9272-bea87f18c8a0_originalSize\">23552</span></td>"
    "</tr></table>";
static const char NoResultPage[] = "<div id=\"ctl00_catalogBody_noResultText\">We did not find any results</div>";
static const char ThrottledPage[] =
    "<div id=\"ctl00_catalogBody_searchThrottled\" class=\"noResultsSpacing\" role=\"status\">"
    "<span>Too many search requests. Please wait a few minutes and try again.</span></div>";
static const char OtherPage[] = "<html><body>Service unavailable</body></html>";

static void CheckParser()
{
    std::vector<CatalogUpdate> updates;
    size_t rows = 99;
    DWORD error;

    error = ParseResults(ResultsPage, L"PCI\\VEN_8086&DEV_31D4", updates, rows);
    ok(error == ERROR_SUCCESS, "results page: error 0x%lx\n", error);
    ok(rows == 1 && updates.size() == 1, "results page: %u rows, %u updates\n", (unsigned)rows, (unsigned)updates.size());
    if (updates.size() == 1)
    {
        ok(updates[0].Id == L"d4bd3c82-d543-40b2-9272-bea87f18c8a0", "results page: id '%ls'\n", updates[0].Id.c_str());
        ok(updates[0].Title == L"INTEL - System - 10.1.13.2", "results page: title '%ls'\n", updates[0].Title.c_str());
        ok(updates[0].Version == L"n/a", "results page: version '%ls'\n", updates[0].Version.c_str());
        ok(updates[0].Bytes == 23552, "results page: %I64u bytes\n", updates[0].Bytes);
    }

    updates.clear();
    error = ParseResults(NoResultPage, L"PCI\\VEN_8086&DEV_31D5", updates, rows);
    ok(error == ERROR_SUCCESS && rows == 0 && updates.empty(), "no-result page: error 0x%lx, %u rows\n", error,
       (unsigned)rows);

    error = ParseResults(ThrottledPage, L"PCI\\VEN_8086&DEV_31D5", updates, rows);
    ok(error == ERROR_LNTDRV_CATALOG_THROTTLED && updates.empty(), "throttled page: error 0x%lx\n", error);

    error = ParseResults(OtherPage, L"PCI\\VEN_8086&DEV_31D5", updates, rows);
    ok(error == ERROR_LNTDRV_CATALOG_FORMAT, "other page: error 0x%lx\n", error);
}

static void CheckThrottle()
{
    const ULONGLONG Start = 1000000;

    ok(ThrottleDelay(0, Start) == 30000, "unknown window: delay %I64u\n", ThrottleDelay(0, Start));
    ok(ThrottleDelay(Start, Start + 5000) == 305000, "window 5 s old: delay %I64u\n", ThrottleDelay(Start, Start + 5000));
    ok(ThrottleDelay(Start, Start + 309999) == 1, "window ending: delay %I64u\n", ThrottleDelay(Start, Start + 309999));
    ok(ThrottleDelay(Start, Start + 310000) == 30000, "window over: delay %I64u\n", ThrottleDelay(Start, Start + 310000));

    ok(ThrottleWindowStart(0, Start) == Start, "first search starts the window\n");
    ok(ThrottleWindowStart(Start, Start + 299999) == Start, "search inside the window keeps its start\n");
    ok(ThrottleWindowStart(Start, Start + 300000) == Start + 300000, "first search after the window starts a new one\n");

    {
        ULONGLONG lowest = 10000, highest = 0;

        for (ULONGLONG seed = 0; seed < 64; seed++)
        {
            ULONGLONG jitter = ThrottleJitter(Start + seed);

            lowest = jitter < lowest ? jitter : lowest;
            highest = jitter > highest ? jitter : highest;
        }
        ok(highest < 10000, "retry spread reaches %I64u ms\n", highest);
        ok(highest - lowest >= 5000, "retries spread over only %I64u ms\n", highest - lowest);
    }
}

static void CheckSearchCache()
{
    const ULONGLONG Saved = 133000000000000000ULL, Day = 24ULL * 60 * 60 * 10000000;
    std::vector<CatalogUpdate> rows(2), loaded;
    std::wstring text;
    bool last = false;

    rows[0].Id = L"d4bd3c82-d543-40b2-9272-bea87f18c8a0";
    rows[0].Title = L"INTEL - System - 10.1.13.2";
    rows[0].Products = L"Windows 10 and later drivers";
    rows[0].Classification = L"Drivers (Other Hardware)";
    rows[0].LastUpdated = L"7/17/1968";
    rows[0].Version = L"n/a";
    rows[0].Size = L"23 KB";
    rows[0].Bytes = 23552;
    rows[1].Id = L"5bb315b6-e183-43e1-a94f-796aac8e4b7e";
    rows[1].Title = L"Intel Net Driver Update (12.19.2.66)";
    text = EncodeSearchPage(rows, true, Saved);
    ok(!text.empty(), "page with two rows was not encoded\n");

    ok(DecodeSearchPage(text, L"PCI\\VEN_8086&DEV_15BC", Saved + Day - 1, loaded, last), "fresh page not loaded\n");
    ok(last, "fresh page lost its last-page flag\n");
    ok(loaded.size() == 2, "fresh page: %u rows\n", (unsigned)loaded.size());
    if (loaded.size() == 2)
    {
        ok(loaded[0].Id == rows[0].Id && loaded[0].Title == rows[0].Title && loaded[0].Products == rows[0].Products &&
               loaded[0].Classification == rows[0].Classification && loaded[0].LastUpdated == rows[0].LastUpdated &&
               loaded[0].Version == rows[0].Version && loaded[0].Size == rows[0].Size && loaded[0].Bytes == 23552,
           "first row changed: '%ls' '%ls' %I64u\n", loaded[0].Id.c_str(), loaded[0].Title.c_str(), loaded[0].Bytes);
        ok(loaded[1].Title == rows[1].Title && loaded[1].Products.empty() && loaded[1].Bytes == 0,
           "second row changed: '%ls'\n", loaded[1].Title.c_str());
        ok(loaded[0].Query == L"PCI\\VEN_8086&DEV_15BC", "row query '%ls'\n", loaded[0].Query.c_str());
    }

    ok(!DecodeSearchPage(text, L"q", Saved + Day, loaded, last) && loaded.empty(), "day-old page was loaded\n");
    ok(!DecodeSearchPage(text, L"q", Saved - 1, loaded, last), "page saved in the future was loaded\n");

    text = EncodeSearchPage(std::vector<CatalogUpdate>(), false, Saved);
    ok(DecodeSearchPage(text, L"q", Saved, loaded, last) && loaded.empty() && !last, "empty page not loaded\n");

    ok(!DecodeSearchPage(L"", L"q", Saved, loaded, last), "empty text was loaded\n");
    ok(!DecodeSearchPage(L"133000000000000000\t1\nid\ttitle\n", L"q", Saved, loaded, last) && loaded.empty(),
       "row with missing fields was loaded\n");
    ok(!DecodeSearchPage(L"133000000000000000\t2\n", L"q", Saved, loaded, last), "bad last-page flag was loaded\n");

    rows[1].Title = L"tab\there";
    ok(EncodeSearchPage(rows, true, Saved).empty(), "row with a tab was encoded\n");
}

static void CheckCore(const wchar_t *id, const wchar_t *expected)
{
    ok(HardwareIdCore(id) == expected, "'%ls': core '%ls', expected '%ls'\n", id, HardwareIdCore(id).c_str(), expected);
}

static void CheckSearchTerms()
{
    static const wchar_t *Display[] = {
        L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086&REV_03", L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086",
        L"PCI\\VEN_8086&DEV_3185&CC_030000",              L"PCI\\VEN_8086&DEV_3185&CC_0300",
        L"PCI\\VEN_8086&DEV_3185&REV_03",                 L"PCI\\VEN_8086&DEV_3185",
        L"PCI\\VEN_8086&CC_030000",                       L"PCI\\VEN_8086&CC_0300"
    };
    static const wchar_t *Expected[] = { L"PCI\\VEN_8086&DEV_3185", L"PCI\\VEN_8086&CC_030000",
                                         L"PCI\\VEN_8086&CC_0300" };
    std::vector<std::wstring> ids(Display, Display + ARRAYSIZE(Display));
    std::vector<std::wstring> terms = CatalogSearchTerms(ids);

    CheckCore(L"PCI\\VEN_8086&DEV_3185&SUBSYS_20758086&REV_03", L"PCI\\VEN_8086&DEV_3185");
    CheckCore(L"PCI\\VEN_8086&DEV_3185", L"PCI\\VEN_8086&DEV_3185");
    CheckCore(L"USB\\VID_8087&PID_0A2B&REV_0001", L"USB\\VID_8087&PID_0A2B");
    CheckCore(L"HID\\VID_046D&PID_C52B&REV_1203&MI_01&COL01", L"HID\\VID_046D&PID_C52B");
    CheckCore(L"ACPI\\VEN_QEMU&DEV_0002", L"ACPI\\VEN_QEMU&DEV_0002");
    CheckCore(L"PCI\\VEN_8086&CC_030000", L"");
    CheckCore(L"ACPI\\QEMU0002", L"");

    ok(terms.size() == ARRAYSIZE(Expected), "%u search terms for 8 ids, expected %u\n", (unsigned)terms.size(),
       (unsigned)ARRAYSIZE(Expected));
    for (size_t i = 0; i < ARRAYSIZE(Expected) && i < terms.size(); i++)
        ok(terms[i] == Expected[i], "term %u is '%ls', expected '%ls'\n", (unsigned)i, terms[i].c_str(), Expected[i]);

    ids.clear();
    ids.push_back(L"ACPI\\VEN_QEMU&DEV_0002");
    ids.push_back(L"ACPI\\QEMU0002");
    terms = CatalogSearchTerms(ids);
    ok(terms.size() == 2 && terms[0] == ids[0] && terms[1] == ids[1], "ACPI device: %u terms\n",
       (unsigned)terms.size());
}

static void CheckSupported()
{
    const std::wstring Core = L"PCI\\VEN_8086&DEV_7A70";
    std::vector<std::wstring> ids;
    CatalogUpdate update;

    ids.push_back(L"PCI\\VEN_8086&DEV_7A70&SUBSYS_00908086&REV_11");
    ids.push_back(L"PCI\\VEN_8086&DEV_7A70&SUBSYS_00908086");
    ids.push_back(L"PCI\\VEN_8086&DEV_7A70");

    ok(SupportsHardwareIds(update, ids), "update without a hardware-id list was rejected\n");

    update.HardwareIds.push_back(L"PCI\\VEN_8086&DEV_51F0&SUBSYS_00748086");
    update.HardwareIds.push_back(L"PCI\\VEN_8086&DEV_7A70&SUBSYS_00908086");
    ok(SupportsHardwareIds(update, ids), "exact hardware id was rejected\n");

    update.HardwareIds.clear();
    update.HardwareIds.push_back(L"PCI\\VEN_8086&DEV_7A70&SUBSYS_00308086");
    ok(!SupportsHardwareIds(update, ids), "another subsystem in a complete list was accepted\n");

    while (update.HardwareIds.size() < 300)
        update.HardwareIds.push_back(Core + L"&SUBSYS_" + NumberText(update.HardwareIds.size()) + L"8086");
    ok(SupportsHardwareIds(update, ids), "same device in a truncated list was rejected\n");

    update.HardwareIds.clear();
    while (update.HardwareIds.size() < 300)
        update.HardwareIds.push_back(L"PCI\\VEN_8086&DEV_51F0&SUBSYS_" + NumberText(update.HardwareIds.size()) +
                                     L"8086");
    ok(!SupportsHardwareIds(update, ids), "another device in a truncated list was accepted\n");
}

static void CheckRanges(ULONGLONG size, size_t expected)
{
    std::vector<std::pair<ULONGLONG, ULONGLONG>> ranges = DownloadRanges(size);
    ULONGLONG next = 0;
    bool contiguous = true;

    ok(ranges.size() == expected, "%I64u bytes: %u ranges, expected %u\n", size, (unsigned)ranges.size(),
       (unsigned)expected);
    for (const std::pair<ULONGLONG, ULONGLONG> &range : ranges)
    {
        contiguous = contiguous && range.first == next && range.second >= range.first;
        next = range.second + 1;
    }
    ok(contiguous && (ranges.empty() || next == size), "%I64u bytes: ranges do not cover the file exactly\n", size);
}

static void CheckSpeed(ULONGLONG rate, const wchar_t *expected, bool megabytes)
{
    bool unit = false;
    std::wstring number = SpeedNumber(rate, unit);

    ok(number == expected && unit == megabytes, "%I64u B/s: '%ls' %s, expected '%ls' %s\n", rate, number.c_str(),
       unit ? "MB" : "KB", expected, megabytes ? "MB" : "KB");
}

static void CheckVersion(const wchar_t *row, const wchar_t *details, const wchar_t *expected)
{
    CatalogUpdate update;

    update.Version = row;
    update.DriverVersion = details;
    UseDriverVersion(update);
    ok(update.Version == expected, "row '%ls', details '%ls': version '%ls', expected '%ls'\n", row, details,
       update.Version.c_str(), expected);
}

START_TEST(CatalogRules)
{
    CheckParser();
    CheckThrottle();
    CheckSearchCache();
    CheckSearchTerms();
    CheckSupported();
    CheckRanges(0, 0);
    CheckRanges(23552, 0);
    CheckRanges(2 * 1024 * 1024 - 1, 0);
    CheckRanges(2 * 1024 * 1024, 2);
    CheckRanges(5033164, 4);
    CheckRanges(24432640, 8);
    CheckRanges(550478830, 8);
    CheckSpeed(0, L"", false);
    CheckSpeed(400, L"1", false);
    CheckSpeed(838110, L"818", false);
    CheckSpeed(1024 * 1024, L"1.0", true);
    CheckSpeed(12 * 1024 * 1024 + 512 * 1024, L"12.5", true);
    CheckVersion(L"n/a", L"10.1.13.2", L"10.1.13.2");
    CheckVersion(L"", L"10.60.615.2022", L"10.60.615.2022");
    CheckVersion(L"31.0.101.2140", L"31.0.101.2141", L"31.0.101.2140");
    CheckVersion(L"n/a", L"", L"n/a");
    CheckVersion(L"n/a", L"n/a", L"n/a");
}
