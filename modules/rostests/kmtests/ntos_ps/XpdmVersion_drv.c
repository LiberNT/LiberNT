/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A VideoPort miniport built for an NT5 target is told the version its image targets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

DECLSPEC_IMPORT UCHAR NTAPI VideoPortGetCurrentIrql(VOID);

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    RTL_OSVERSIONINFOW Version;
    ULONG Major = 0, Minor = 0, Build = 0;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    *DeviceName = L"XpdmVersion";

    ok_eq_uint(VideoPortGetCurrentIrql(), PASSIVE_LEVEL);

    RtlZeroMemory(&Version, sizeof(Version));
    Version.dwOSVersionInfoSize = sizeof(Version);
    ok_eq_hex(RtlGetVersion(&Version), STATUS_SUCCESS);
    ok_eq_ulong(Version.dwMajorVersion, 5UL);
    ok_eq_ulong(Version.dwMinorVersion, 1UL);
    ok_eq_ulong(Version.dwBuildNumber, 2600UL);

    PsGetVersion(&Major, &Minor, &Build, NULL);
    ok_eq_ulong(Major, 5UL);
    ok_eq_ulong(Minor, 1UL);
    ok_eq_ulong(Build, 2600UL);

    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
}
