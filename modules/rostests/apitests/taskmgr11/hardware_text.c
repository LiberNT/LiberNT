/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Task Manager hardware text trimming tests
 * COPYRIGHT:   Copyright 2026 OpenAI
 */

#include <apitest.h>
#include <windows.h>
#include "hardware_text.h"

START_TEST(hardware_text)
{
    static const struct { const WCHAR *input, *expected; } cases[] = {
        { L"  AMD Ryzen 9  ", L"AMD Ryzen 9" },
        { L"\t  Disk  model \r\n", L"Disk  model" },
        { L"  \t", L"" },
        { L"", L"" },
        { L"GPU", L"GPU" },
        { L" trailing ", L"trailing" }
    };
    unsigned int i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        WCHAR text[64];
        lstrcpyW(text, cases[i].input);
        TmTrimHardwareText(text);
        ok(!lstrcmpW(text, cases[i].expected), "case %u: got %s, expected %s\n",
           i, wine_dbgstr_w(text), wine_dbgstr_w(cases[i].expected));
    }
}
