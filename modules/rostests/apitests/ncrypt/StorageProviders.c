/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the enumeration of key storage providers
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <bcrypt.h>
#include <ncrypt.h>

static DWORD
FindProvider(
    _In_ const NCryptProviderName *List,
    _In_ DWORD Count,
    _In_ const WCHAR *Name)
{
    DWORD Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (!lstrcmpW(List[Index].pszName, Name))
            return Index;
    }
    return MAXDWORD;
}

START_TEST(StorageProviders)
{
    NCryptProviderName *List = NULL;
    NCRYPT_PROV_HANDLE Provider = 0;
    DWORD Count = 0, Index, Software, Platform, SmartCard;
    SECURITY_STATUS Status;

    Status = NCryptEnumStorageProviders(&Count, &List, 0);
    ok(Status == ERROR_SUCCESS, "NCryptEnumStorageProviders returned 0x%lx\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(Count != 0, "No storage provider was listed\n");
    ok(List != NULL, "The provider list is NULL\n");
    if (!Count || !List)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        ok(List[Index].pszName && List[Index].pszName[0], "Provider %lu has no name\n", Index);
        ok(List[Index].pszComment == NULL, "Provider %lu has the comment %ls\n", Index, List[Index].pszComment);
    }

    Software = FindProvider(List, Count, MS_KEY_STORAGE_PROVIDER);
    Platform = FindProvider(List, Count, MS_PLATFORM_KEY_STORAGE_PROVIDER);
    SmartCard = FindProvider(List, Count, MS_SMART_CARD_KEY_STORAGE_PROVIDER);
    ok(Software == 0, "The software provider is at %lu\n", Software);
    ok(SmartCard != MAXDWORD, "The smart card provider is not listed\n");
    if (Platform != MAXDWORD)
        ok(Software < Platform, "The platform provider comes before the software provider\n");
    if (Platform != MAXDWORD && SmartCard != MAXDWORD)
        ok(Platform < SmartCard, "The smart card provider comes before the platform provider\n");

    Status = NCryptOpenStorageProvider(&Provider, List[0].pszName, 0);
    ok(Status == ERROR_SUCCESS, "Opening %ls returned 0x%lx\n", List[0].pszName, Status);
    if (Status == ERROR_SUCCESS)
        ok(NCryptFreeObject(Provider) == ERROR_SUCCESS, "NCryptFreeObject failed\n");

    ok(NCryptFreeBuffer(List) == ERROR_SUCCESS, "NCryptFreeBuffer failed\n");

    List = NULL;
    Count = 0;
    Status = NCryptEnumStorageProviders(&Count, &List, NCRYPT_SILENT_FLAG);
    ok(Status == ERROR_SUCCESS, "NCRYPT_SILENT_FLAG returned 0x%lx\n", Status);
    ok(Count != 0, "NCRYPT_SILENT_FLAG listed no provider\n");
    if (List)
        NCryptFreeBuffer(List);

    List = NULL;
    Count = 0;
    Status = NCryptEnumStorageProviders(&Count, &List, 1);
    ok(Status == NTE_BAD_FLAGS, "An unknown flag returned 0x%lx\n", Status);
    ok(Count == 0, "An unknown flag set the count to %lu\n", Count);
    ok(List == NULL, "An unknown flag set the list to %p\n", List);

    Status = NCryptEnumStorageProviders(NULL, &List, 0);
    ok(Status == NTE_INVALID_PARAMETER, "A NULL count returned 0x%lx\n", Status);
    Status = NCryptEnumStorageProviders(&Count, NULL, 0);
    ok(Status == NTE_INVALID_PARAMETER, "A NULL list returned 0x%lx\n", Status);
}
