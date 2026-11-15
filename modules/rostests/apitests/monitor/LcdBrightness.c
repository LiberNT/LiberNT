/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the integrated panel brightness IOCTLs on \\.\LCD
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <winioctl.h>
#include <ntddvdeo.h>

static
BOOL
QueryBrightness(
    _In_ HANDLE Lcd,
    _Out_ PDISPLAY_BRIGHTNESS Brightness)
{
    DWORD Returned = 0;
    BOOL Ok;

    ZeroMemory(Brightness, sizeof(*Brightness));
    Ok = DeviceIoControl(Lcd, IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS, NULL, 0, Brightness, sizeof(*Brightness),
                         &Returned, NULL);
    ok(Ok, "IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS failed with %lu\n", GetLastError());
    ok_eq_ulong(Returned, (ULONG)sizeof(*Brightness));
    return Ok;
}

static
BOOL
SetBrightness(
    _In_ HANDLE Lcd,
    _In_ UCHAR Ac,
    _In_ UCHAR Dc)
{
    DISPLAY_BRIGHTNESS Brightness;
    DWORD Returned = 0;

    Brightness.ucDisplayPolicy = DISPLAYPOLICY_BOTH;
    Brightness.ucACBrightness = Ac;
    Brightness.ucDCBrightness = Dc;
    return DeviceIoControl(Lcd, IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS, &Brightness, sizeof(Brightness), NULL, 0,
                           &Returned, NULL);
}

START_TEST(LcdBrightness)
{
    UCHAR Levels[256];
    DISPLAY_BRIGHTNESS Original, Current;
    DWORD Returned = 0, Index;
    UCHAR Target;
    HANDLE Lcd;
    BOOL Ok;

    Lcd = CreateFileW(L"\\\\.\\LCD", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                      OPEN_EXISTING, 0, NULL);
    if (Lcd == INVALID_HANDLE_VALUE)
    {
        skip("No integrated panel (\\\\.\\LCD: %lu)\n", GetLastError());
        return;
    }

    Ok = DeviceIoControl(Lcd, IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS, NULL, 0, Levels, sizeof(Levels), &Returned,
                         NULL);
    if (!Ok)
    {
        skip("Panel without brightness control (%lu)\n", GetLastError());
        CloseHandle(Lcd);
        return;
    }
    ok(Returned >= 2, "Expected at least two brightness levels, got %lu\n", Returned);
    for (Index = 0; Index < Returned; Index++)
        ok(Levels[Index] <= 100, "Level %lu is %u\n", Index, Levels[Index]);

    if (Returned < 2 || !QueryBrightness(Lcd, &Original))
    {
        CloseHandle(Lcd);
        return;
    }

    Target = Levels[0];
    for (Index = 0; Index < Returned; Index++)
    {
        if (abs((int)Levels[Index] - (int)Original.ucACBrightness) >
            abs((int)Target - (int)Original.ucACBrightness))
        {
            Target = Levels[Index];
        }
    }
    ok(Target != Original.ucACBrightness, "No level differs from the current %u\n", Original.ucACBrightness);

    Ok = SetBrightness(Lcd, Target, Target);
    ok(Ok, "IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS to %u failed with %lu\n", Target, GetLastError());
    if (Ok && QueryBrightness(Lcd, &Current))
    {
        ok_eq_uint(Current.ucACBrightness, Target);
        ok_eq_uint(Current.ucDCBrightness, Target);
    }

    Ok = SetBrightness(Lcd, Original.ucACBrightness, Original.ucDCBrightness);
    ok(Ok, "Restoring brightness %u/%u failed with %lu\n", Original.ucACBrightness, Original.ucDCBrightness,
       GetLastError());
    if (Ok && QueryBrightness(Lcd, &Current))
    {
        ok_eq_uint(Current.ucACBrightness, Original.ucACBrightness);
        ok_eq_uint(Current.ucDCBrightness, Original.ucDCBrightness);
    }

    CloseHandle(Lcd);
}
