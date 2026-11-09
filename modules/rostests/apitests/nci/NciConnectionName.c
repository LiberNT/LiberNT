/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NciGetConnectionName and NciSetConnectionName
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <iphlpapi.h>
#include <objbase.h>

typedef DWORD (WINAPI *PFN_NCI_GET)(const GUID *, PWSTR, DWORD, DWORD *);
typedef DWORD (WINAPI *PFN_NCI_SET)(const GUID *, PCWSTR);

static PFN_NCI_GET pNciGetConnectionName;
static PFN_NCI_SET pNciSetConnectionName;

static ULONG
GetConnections(GUID *Guids, WCHAR Names[][256])
{
    IP_ADAPTER_ADDRESSES *Addresses, *Adapter;
    ULONG Size = 0, Count = 0;
    WCHAR Text[64];

    GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &Size);
    Addresses = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Addresses || GetAdaptersAddresses(AF_UNSPEC, 0, NULL, Addresses, &Size) != NO_ERROR)
    {
        HeapFree(GetProcessHeap(), 0, Addresses);
        return 0;
    }
    for (Adapter = Addresses; Adapter && Count < 2; Adapter = Adapter->Next)
    {
        if (Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            !MultiByteToWideChar(CP_ACP, 0, Adapter->AdapterName, -1, Text, ARRAYSIZE(Text)) ||
            FAILED(CLSIDFromString(Text, &Guids[Count])))
        {
            continue;
        }
        lstrcpynW(Names[Count], Adapter->FriendlyName, 256);
        Count++;
    }
    HeapFree(GetProcessHeap(), 0, Addresses);
    return Count;
}

START_TEST(NciConnectionName)
{
    HMODULE hNci = LoadLibraryW(L"nci.dll");
    GUID Guids[2], Unknown = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    WCHAR Names[2][256], Name[256];
    DWORD Error, Size;
    ULONG Count;

    if (!hNci)
    {
        skip("nci.dll is missing\n");
        return;
    }
    pNciGetConnectionName = (PFN_NCI_GET)GetProcAddress(hNci, "NciGetConnectionName");
    pNciSetConnectionName = (PFN_NCI_SET)GetProcAddress(hNci, "NciSetConnectionName");
    if (!pNciGetConnectionName || !pNciSetConnectionName)
    {
        skip("nci.dll has no connection name functions\n");
        return;
    }

    Size = sizeof(Name);
    Error = pNciGetConnectionName(&Unknown, Name, sizeof(Name), &Size);
    trace("NciGetConnectionName(unknown GUID) returned %lu\n", Error);
    ok(Error != NO_ERROR, "A connection that does not exist has a name\n");

    Count = GetConnections(Guids, Names);
    if (Count == 0)
    {
        skip("No connection to test with\n");
        return;
    }

    Size = 0;
    Error = pNciGetConnectionName(&Guids[0], NULL, 0, &Size);
    ok(Error == NO_ERROR, "NciGetConnectionName(NULL, 0) returned %lu\n", Error);
    ok(Size == (lstrlenW(Names[0]) + 1) * sizeof(WCHAR), "Size %lu for name %S\n", Size, Names[0]);

    Size = sizeof(Name);
    Error = pNciGetConnectionName(&Guids[0], Name, sizeof(Name), &Size);
    ok(Error == NO_ERROR, "NciGetConnectionName returned %lu\n", Error);
    ok(Error != NO_ERROR || !lstrcmpW(Name, Names[0]), "Name %S, expected %S\n", Name, Names[0]);

    Error = pNciSetConnectionName(&Guids[0], Names[0]);
    ok(Error == NO_ERROR, "Setting the current name returned %lu\n", Error);

    if (Count < 2)
    {
        skip("A second connection is needed to test a duplicate name\n");
        return;
    }
    Error = pNciSetConnectionName(&Guids[0], Names[1]);
    trace("Setting the name of another connection returned %lu\n", Error);
    ok(Error != NO_ERROR, "The name of another connection was accepted\n");
    Size = sizeof(Name);
    if (pNciGetConnectionName(&Guids[0], Name, sizeof(Name), &Size) == NO_ERROR)
        ok(!lstrcmpW(Name, Names[0]), "The name changed to %S\n", Name);
    if (Error == NO_ERROR)
        pNciSetConnectionName(&Guids[0], Names[0]);
}
