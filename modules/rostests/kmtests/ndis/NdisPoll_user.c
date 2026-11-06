/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Checks how NDIS calls the poll and notification handlers of a Poll object
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <newdev.h>
#include "NdisPoll.h"

static const GUID NdisPollNetClass = {0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR NdisPollHardwareIds[] = NDISPOLL_HARDWARE_ID L"\0";

static ULONG NdisPollRead(HKEY Key, PCWSTR Name)
{
    DWORD Value = NDISPOLL_UNSET, Size = sizeof(Value), Type = 0;

    if (RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        return NDISPOLL_UNSET;
    return Value;
}

static PCWSTR NdisPollReadEvents(HKEY Key, PCWSTR Name, PWSTR Buffer, DWORD Length)
{
    DWORD Size = Length * sizeof(WCHAR), Type = 0;

    ZeroMemory(Buffer, Size);
    if (RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)Buffer, &Size) != ERROR_SUCCESS || Type != REG_SZ)
        return L"<missing>";
    Buffer[Length - 1] = UNICODE_NULL;
    return Buffer;
}

static BOOL NdisPollGetInfPath(PWSTR InfPath, DWORD Length)
{
    DWORD Written = GetModuleFileNameW(NULL, InfPath, Length);
    PWSTR Filename = (Written && Written < Length) ? wcsrchr(InfPath, L'\\') : NULL;

    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_ndispoll.inf") > Length)
        return FALSE;
    wcscpy(Filename + 1, L"kmtest_ndispoll.inf");
    return TRUE;
}

static void NdisPollCheck(HKEY Key)
{
    WCHAR Events[NDISPOLL_MAX_EVENTS + 1];
    ULONG DataSize = sizeof(PVOID) == 8 ? 152 : 108;

    ok_eq_hex(NdisPollRead(Key, L"Register"), 0);
    ok_eq_wstr(NdisPollReadEvents(Key, L"Registered", Events, RTL_NUMBER_OF(Events)), L"PEP");
    ok_eq_wstr(NdisPollReadEvents(Key, L"Requested", Events, RTL_NUMBER_OF(Events)), L"DPEP");
    ok_eq_wstr(NdisPollReadEvents(Key, L"Rearmed", Events, RTL_NUMBER_OF(Events)), L"DPEP");
    ok_eq_wstr(NdisPollReadEvents(Key, L"Remaining", Events, RTL_NUMBER_OF(Events)), L"DPEP");
    ok_eq_wstr(NdisPollReadEvents(Key, L"Requeue", Events, RTL_NUMBER_OF(Events)), L"DPEPDPEP");

    ok_eq_hex(NdisPollRead(Key, L"DataHeader"), 0x80 | (1 << 8) | (DataSize << 16));
    ok_eq_hex(NdisPollRead(Key, L"NotificationHeader"), 0x80 | (1 << 8) | (6 << 16));
    ok_eq_ulong(NdisPollRead(Key, L"ReceiveInputClear"), 1);
    ok_eq_ulong(NdisPollRead(Key, L"TransmitInputClear"), 1);
    ok(NdisPollRead(Key, L"MaxNblsToIndicate") != 0, "MaxNblsToIndicate is zero\n");
    ok(NdisPollRead(Key, L"MaxNblsToComplete") != 0, "MaxNblsToComplete is zero\n");
    ok(NdisPollRead(Key, L"PollIrql") <= NDISPOLL_DISPATCH_LEVEL, "Poll ran at IRQL %lu\n", NdisPollRead(Key, L"PollIrql"));
    ok(NdisPollRead(Key, L"NotificationIrql") <= NDISPOLL_DISPATCH_LEVEL, "Notification ran at IRQL %lu\n",
       NdisPollRead(Key, L"NotificationIrql"));
    trace("MaxNblsToIndicate %lu, MaxNblsToComplete %lu, poll IRQL %lu, notification IRQL %lu\n",
          NdisPollRead(Key, L"MaxNblsToIndicate"), NdisPollRead(Key, L"MaxNblsToComplete"),
          NdisPollRead(Key, L"PollIrql"), NdisPollRead(Key, L"NotificationIrql"));

    ok_eq_wstr(NdisPollReadEvents(Key, L"AffinityFirst", Events, RTL_NUMBER_OF(Events)), L"DPEP");
    ok_eq_wstr(NdisPollReadEvents(Key, L"AffinitySecond", Events, RTL_NUMBER_OF(Events)), L"DPEP");
    ok_eq_ulong(NdisPollRead(Key, L"AffinityProcessor"), NdisPollRead(Key, L"AffinityTarget"));
    trace("%lu processors\n", NdisPollRead(Key, L"Processors"));

    ok_eq_wstr(NdisPollReadEvents(Key, L"Deregister", Events, RTL_NUMBER_OF(Events)), L"D");
}

START_TEST(NdisPoll)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR InfPath[MAX_PATH];
    HDEVINFO Devices;
    BOOL Reboot = FALSE, Registered;
    ULONG Done = 0;
    HKEY Key = NULL;
    DWORD Start;

    if (!NdisPollGetInfPath(InfPath, RTL_NUMBER_OF(InfPath)))
    {
        ok(FALSE, "Cannot build the INF path\n");
        return;
    }
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, NDISPOLL_PARAMETERS_KEY);

    Devices = SetupDiCreateDeviceInfoList(&NdisPollNetClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;
    Registered = SetupDiCreateDeviceInfoW(Devices, L"KmtestNdisPoll", &NdisPollNetClass, L"Kmtest NDIS poll miniport",
                                          NULL, DICD_GENERATE_ID, &Device) &&
                 SetupDiSetDeviceRegistryPropertyW(Devices, &Device, SPDRP_HARDWAREID, (const BYTE *)NdisPollHardwareIds,
                                                   sizeof(NdisPollHardwareIds)) &&
                 SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Devices, &Device);
    ok(Registered, "Registering the test device failed: %lu\n", GetLastError());

    if (Registered)
    {
        ok(UpdateDriverForPlugAndPlayDevicesW(NULL, NDISPOLL_HARDWARE_ID, InfPath,
                                              INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot),
           "Installing the test miniport failed: %lu\n", GetLastError());

        Start = GetTickCount();
        do
        {
            if (!Key)
                RegOpenKeyExW(HKEY_LOCAL_MACHINE, NDISPOLL_PARAMETERS_KEY, 0, KEY_QUERY_VALUE, &Key);
            if (Key)
                Done = NdisPollRead(Key, L"Done");
            if (Done == 1)
                break;
            Sleep(100);
        } while (GetTickCount() - Start < 20000);
        ok(Done == 1, "The test miniport did not report its results\n");
        if (Key)
        {
            ok_eq_hex(NdisPollRead(Key, L"Registration"), 0);
            ok_eq_hex(NdisPollRead(Key, L"General"), 0);
            if (Done == 1)
                NdisPollCheck(Key);
            RegCloseKey(Key);
        }

        ok(SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device), "Removing the test device failed: %lu\n",
           GetLastError());
    }
    SetupDiDestroyDeviceInfoList(Devices);
}
