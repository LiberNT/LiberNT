/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests which bytes RtlGetVersion writes for each declared structure size
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *FN_RtlGetVersion)(PRTL_OSVERSIONINFOW);

START_TEST(RtlGetVersion)
{
    static const ULONG Sizes[] = { 0, 20, 22, 148, 200, 275, 276, 277, 283, 284, 285, 400 };
    FN_RtlGetVersion pRtlGetVersion;
    UCHAR Buffer[512];
    ULONG i, Offset, Last;
    NTSTATUS Status;

    pRtlGetVersion = (FN_RtlGetVersion)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (!pRtlGetVersion)
    {
        skip("RtlGetVersion is missing\n");
        return;
    }
    ok(FIELD_OFFSET(RTL_OSVERSIONINFOW, szCSDVersion) == 20, "szCSDVersion at %lu\n",
       (ULONG)FIELD_OFFSET(RTL_OSVERSIONINFOW, szCSDVersion));
    ok(sizeof(RTL_OSVERSIONINFOEXW) == 284, "RTL_OSVERSIONINFOEXW is %lu bytes\n", (ULONG)sizeof(RTL_OSVERSIONINFOEXW));

    for (i = 0; i < ARRAYSIZE(Sizes); i++)
    {
        PRTL_OSVERSIONINFOEXW Info = (PRTL_OSVERSIONINFOEXW)Buffer;

        memset(Buffer, 0xCC, sizeof(Buffer));
        Info->dwOSVersionInfoSize = Sizes[i];
        Status = pRtlGetVersion((PRTL_OSVERSIONINFOW)Info);
        ok(Status == STATUS_SUCCESS, "size %lu: status %lx\n", Sizes[i], Status);
        ok(Info->dwOSVersionInfoSize == Sizes[i], "size %lu: size changed to %lu\n", Sizes[i], Info->dwOSVersionInfoSize);
        ok(Info->dwMajorVersion == 10, "size %lu: major %lu\n", Sizes[i], Info->dwMajorVersion);
        ok(Info->dwPlatformId == VER_PLATFORM_WIN32_NT, "size %lu: platform %lu\n", Sizes[i], Info->dwPlatformId);
        ok(Info->szCSDVersion[0] == UNICODE_NULL, "size %lu: CSD version starts with %04x\n", Sizes[i], Info->szCSDVersion[0]);

        Last = 0;
        for (Offset = sizeof(ULONG); Offset < sizeof(Buffer); Offset++)
        {
            if (Buffer[Offset] != 0xCC)
                Last = Offset;
        }
        if (Sizes[i] == sizeof(RTL_OSVERSIONINFOEXW))
        {
            ok(Last == FIELD_OFFSET(RTL_OSVERSIONINFOEXW, wReserved) - 1, "size %lu: last written byte %lu\n", Sizes[i], Last);
            ok(Info->wProductType != 0xCC, "size %lu: product type not written\n", Sizes[i]);
            ok(Info->wServicePackMajor == 0 && Info->wServicePackMinor == 0, "size %lu: service pack %u.%u\n",
               Sizes[i], Info->wServicePackMajor, Info->wServicePackMinor);
        }
        else
        {
            ok(Last == FIELD_OFFSET(RTL_OSVERSIONINFOW, szCSDVersion) + sizeof(WCHAR) - 1,
               "size %lu: last written byte %lu\n", Sizes[i], Last);
        }
    }
}
