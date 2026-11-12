/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The keys of the loaded boot configuration store carry the store root's access list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
PSECURITY_DESCRIPTOR
GetDacl(
    _In_ HKEY Key,
    _Out_ PDWORD Length)
{
    PSECURITY_DESCRIPTOR Descriptor;
    LONG Error;

    *Length = 0;
    Error = RegGetKeySecurity(Key, DACL_SECURITY_INFORMATION, NULL, Length);
    ok_long(Error, ERROR_INSUFFICIENT_BUFFER);
    if (Error != ERROR_INSUFFICIENT_BUFFER) return NULL;

    Descriptor = HeapAlloc(GetProcessHeap(), 0, *Length);
    ok(Descriptor != NULL, "No memory for %lu bytes\n", *Length);
    if (Descriptor == NULL) return NULL;

    Error = RegGetKeySecurity(Key, DACL_SECURITY_INFORMATION, Descriptor, Length);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Descriptor);
        return NULL;
    }

    return Descriptor;
}

START_TEST(BcdStoreAccess)
{
    PSECURITY_DESCRIPTOR StoreDacl, ObjectsDacl;
    DWORD StoreLength, ObjectsLength, Length;
    HKEY Store, Objects, Object;
    WCHAR Name[64];
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"BCD00000000", 0, KEY_READ, &Store);
    if (Error == ERROR_FILE_NOT_FOUND)
    {
        skip("No boot configuration store is loaded\n");
        return;
    }
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("Reading the boot configuration store needs administrator rights\n");
        return;
    }
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    Error = RegOpenKeyExW(Store, L"Objects", 0, KEY_READ, &Objects);
    ok_long(Error, ERROR_SUCCESS);
    if (Error == ERROR_SUCCESS)
    {
        Length = ARRAYSIZE(Name);
        Error = RegEnumKeyExW(Objects, 0, Name, &Length, NULL, NULL, NULL, NULL);
        ok_long(Error, ERROR_SUCCESS);
        if (Error == ERROR_SUCCESS)
        {
            Error = RegOpenKeyExW(Objects, Name, 0, KEY_READ, &Object);
            ok(Error == ERROR_SUCCESS, "Opening object %S failed: %ld\n", Name, Error);
            if (Error == ERROR_SUCCESS) RegCloseKey(Object);
        }

        StoreDacl = GetDacl(Store, &StoreLength);
        ObjectsDacl = GetDacl(Objects, &ObjectsLength);
        if (StoreDacl != NULL && ObjectsDacl != NULL)
        {
            ok(StoreLength == ObjectsLength && !memcmp(StoreDacl, ObjectsDacl, StoreLength),
               "Objects carries a different access list than the store root (%lu and %lu bytes)\n",
               ObjectsLength, StoreLength);
        }
        if (StoreDacl != NULL) HeapFree(GetProcessHeap(), 0, StoreDacl);
        if (ObjectsDacl != NULL) HeapFree(GetProcessHeap(), 0, ObjectsDacl);

        RegCloseKey(Objects);
    }

    RegCloseKey(Store);
}
