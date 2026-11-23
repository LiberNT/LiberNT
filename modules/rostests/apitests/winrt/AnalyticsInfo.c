/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Windows.System.Profile.AnalyticsInfo
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "winrt_apitest.h"

#define WIDL_using_Windows_System_Profile
#include <windows.system.profile.h>

typedef LONG (NTAPI *PFN_RTL_GET_VERSION)(RTL_OSVERSIONINFOW *);

static UINT64
ExpectedVersion(void)
{
    RTL_OSVERSIONINFOW Info = { sizeof(Info) };
    PFN_RTL_GET_VERSION pRtlGetVersion;
    DWORD Revision = 0, Size = sizeof(Revision);

    pRtlGetVersion = (PFN_RTL_GET_VERSION)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    ok(pRtlGetVersion != NULL, "no RtlGetVersion\n");
    if (!pRtlGetVersion || pRtlGetVersion(&Info) < 0)
        return 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                     RRF_RT_REG_DWORD, NULL, &Revision, &Size) != ERROR_SUCCESS)
        Revision = 0;
    return ((UINT64)Info.dwMajorVersion << 48) | ((UINT64)Info.dwMinorVersion << 32) |
           ((UINT64)Info.dwBuildNumber << 16) | (Revision & 0xffff);
}

START_TEST(AnalyticsInfo)
{
    IAnalyticsInfoStatics *Statics = NULL;
    IAnalyticsVersionInfo *Version = NULL, *Version2 = NULL;
    IAgileObject *Agile = NULL;
    HSTRING Family = NULL, FamilyVersion = NULL, Name = NULL;
    const WCHAR *Text;
    WCHAR *End;
    UINT64 Value;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "RoInitialize returned %#lx\n", hr);
    TraceActivatableClass(L"Windows.System.Profile.AnalyticsInfo");

    hr = GetFactory(L"Windows.System.Profile.AnalyticsInfo", &IID_IAnalyticsInfoStatics, (void **)&Statics);
    ok(hr == S_OK, "RoGetActivationFactory returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;

    hr = IAnalyticsInfoStatics_QueryInterface(Statics, &IID_IAgileObject, (void **)&Agile);
    ok(hr == S_OK, "IAgileObject returned %#lx\n", hr);
    if (Agile) IAgileObject_Release(Agile);

    hr = IAnalyticsInfoStatics_get_VersionInfo(Statics, NULL);
    ok(hr == E_POINTER, "VersionInfo(NULL) returned %#lx\n", hr);

    hr = IAnalyticsInfoStatics_get_VersionInfo(Statics, &Version);
    ok(hr == S_OK, "VersionInfo returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;

    hr = IAnalyticsVersionInfo_GetRuntimeClassName(Version, &Name);
    ok(hr == S_OK, "GetRuntimeClassName returned %#lx\n", hr);
    ok(!wcscmp(WindowsGetStringRawBuffer(Name, NULL), L"Windows.System.Profile.AnalyticsVersionInfo"),
       "class name %ls\n", WindowsGetStringRawBuffer(Name, NULL));

    hr = IAnalyticsVersionInfo_get_DeviceFamily(Version, &Family);
    ok(hr == S_OK, "DeviceFamily returned %#lx\n", hr);
    ok(!wcscmp(WindowsGetStringRawBuffer(Family, NULL), L"Windows.Desktop"),
       "DeviceFamily %ls\n", WindowsGetStringRawBuffer(Family, NULL));

    hr = IAnalyticsVersionInfo_get_DeviceFamilyVersion(Version, &FamilyVersion);
    ok(hr == S_OK, "DeviceFamilyVersion returned %#lx\n", hr);
    Text = WindowsGetStringRawBuffer(FamilyVersion, NULL);
    Value = _wcstoui64(Text, &End, 10);
    ok(*Text && !*End, "DeviceFamilyVersion %ls is not a decimal number\n", Text);
    ok(Value == ExpectedVersion(), "DeviceFamilyVersion %ls, expected %I64u\n", Text, ExpectedVersion());

    hr = IAnalyticsInfoStatics_get_VersionInfo(Statics, &Version2);
    ok(hr == S_OK, "second VersionInfo returned %#lx\n", hr);

done:
    WindowsDeleteString(Name);
    WindowsDeleteString(Family);
    WindowsDeleteString(FamilyVersion);
    if (Version2) IAnalyticsVersionInfo_Release(Version2);
    if (Version) IAnalyticsVersionInfo_Release(Version);
    if (Statics) IAnalyticsInfoStatics_Release(Statics);
    RoUninitialize();
}
