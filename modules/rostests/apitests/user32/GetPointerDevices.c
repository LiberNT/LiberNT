/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests GetPointerDevices count and buffer queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PGET_POINTER_DEVICES)(UINT32 *, POINTER_DEVICE_INFO *);

START_TEST(GetPointerDevices)
{
    PGET_POINTER_DEVICES pGetPointerDevices;
    POINTER_DEVICE_INFO *Devices;
    UINT32 Count = 0, Size;

    pGetPointerDevices = (PGET_POINTER_DEVICES)GetProcAddress(LoadLibraryW(L"user32.dll"), "GetPointerDevices");
    if (!pGetPointerDevices)
    {
        skip("GetPointerDevices is not exported\n");
        return;
    }

    ok(!pGetPointerDevices(NULL, NULL), "NULL count accepted\n");
    ok(pGetPointerDevices(&Count, NULL), "Count query failed, error %lu\n", GetLastError());

    Size = Count + 1;
    Devices = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size * sizeof(*Devices));
    ok(Devices != NULL, "HeapAlloc failed\n");
    if (!Devices)
        return;
    SetLastError(0xdeadbeef);
    if (Count)
    {
        ok(pGetPointerDevices(&Size, Devices), "Buffer query failed, error %lu\n", GetLastError());
        ok(Size == Count, "Buffer query returned %u devices, count query %u\n", Size, Count);
    }
    else
    {
        ok(!pGetPointerDevices(&Size, Devices), "Buffer query succeeded without devices\n");
        ok(GetLastError() == ERROR_SYSTEM_DEVICE_NOT_FOUND, "Error is %lu\n", GetLastError());
    }
    HeapFree(GetProcessHeap(), 0, Devices);
}
