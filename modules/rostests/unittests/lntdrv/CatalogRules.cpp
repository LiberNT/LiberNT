/*
 * PROJECT:     LiberNT Driver Installer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the catalog search page parser, the search throttle wait and the offered version
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
    CheckVersion(L"n/a", L"10.1.13.2", L"10.1.13.2");
    CheckVersion(L"", L"10.60.615.2022", L"10.60.615.2022");
    CheckVersion(L"31.0.101.2140", L"31.0.101.2141", L"31.0.101.2140");
    CheckVersion(L"n/a", L"", L"n/a");
    CheckVersion(L"n/a", L"n/a", L"n/a");
}
