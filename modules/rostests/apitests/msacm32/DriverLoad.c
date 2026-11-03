/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that msacm32.dll does not load codec drivers while it loads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>

typedef MMRESULT (WINAPI *PACM_METRICS)(HACMOBJ, UINT, LPVOID);

static const PCWSTR CodecNames[] =
{
    L"msadp32.acm",
    L"msg711.acm",
    L"msgsm32.acm",
    L"imaadp32.acm",
    L"l3codeca.acm",
};

START_TEST(DriverLoad)
{
    HMODULE Msacm;
    PACM_METRICS Metrics;
    DWORD Count = 0;
    MMRESULT Result;
    ULONG i;

    ok(GetModuleHandleW(L"msacm32.dll") == NULL, "msacm32.dll is already loaded\n");

    Msacm = LoadLibraryW(L"msacm32.dll");
    ok(Msacm != NULL, "LoadLibrary failed with %lu\n", GetLastError());
    if (Msacm == NULL)
        return;

    for (i = 0; i < RTL_NUMBER_OF(CodecNames); i++)
        ok(GetModuleHandleW(CodecNames[i]) == NULL, "%ls loaded with msacm32.dll\n", CodecNames[i]);

    Metrics = (PACM_METRICS)GetProcAddress(Msacm, "acmMetrics");
    ok(Metrics != NULL, "acmMetrics is missing\n");
    if (Metrics != NULL)
    {
        Result = Metrics(NULL, ACM_METRIC_COUNT_DRIVERS, &Count);
        ok(Result == MMSYSERR_NOERROR, "acmMetrics returned %u\n", Result);
        ok(Count != 0, "No drivers after the first ACM call\n");
        for (i = 0; i < RTL_NUMBER_OF(CodecNames); i++)
            trace("%ls after acmMetrics: %p\n", CodecNames[i], GetModuleHandleW(CodecNames[i]));
    }

    FreeLibrary(Msacm);
}
