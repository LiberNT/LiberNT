/*
 * PROJECT:     LiberNT Task Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hardware display string normalization
 * COPYRIGHT:   Copyright 2026 OpenAI
 */

#pragma once

static inline void TmTrimHardwareText(WCHAR* text)
{
    WCHAR* start = text;
    WCHAR* end;
    WCHAR* out;

    while (*start == L' ' || (*start >= L'\t' && *start <= L'\r')) start++;
    end = start;
    while (*end) end++;
    while (end > start && (end[-1] == L' ' || (end[-1] >= L'\t' && end[-1] <= L'\r'))) end--;
    out = text;
    while (start < end) *out++ = *start++;
    *out = 0;
}
