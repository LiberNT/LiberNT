/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS WinSock 2 API
 * FILE:        dll/win32/ws2_32/src/spinstal.c
 * PURPOSE:     Transport Service Provider Installation
 * PROGRAMMER:  Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES ******************************************************************/

#include <ws2_32.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

typedef struct _WS_PACKED_CATALOG_ITEM
{
    CHAR DllPath[MAX_PATH];
    WSAPROTOCOL_INFOW ProtocolInfo;
} WS_PACKED_CATALOG_ITEM;

static
INT
WsTcInstallProvider(IN LPGUID lpProviderId,
                    IN CONST WCHAR *lpszProviderDllPath,
                    IN CONST WSAPROTOCOL_INFOW *lpProtocolInfoList,
                    IN DWORD dwNumberOfEntries)
{
    WS_PACKED_CATALOG_ITEM PackedItem;
    PTCATALOG_ENTRY CatalogEntry;
    PTCATALOG Catalog;
    HKEY RootKey, EntriesKey = NULL, EntryKey, LockKey;
    CHAR LockName[32];
    CHAR EntryName[13];
    DWORD Serial, CurrentSerial, EntryCount, NextId, Disposition, RegType, RegSize, i;
    BOOL Locked = FALSE;
    LONG Status;
    INT ErrorCode = WSANO_RECOVERY;

    if (!lpProviderId || !lpszProviderDllPath || !lpProtocolInfoList || !dwNumberOfEntries)
        return WSAEINVAL;

    RtlZeroMemory(&PackedItem, sizeof(PackedItem));
    if (!WideCharToMultiByte(CP_ACP,
                             0,
                             lpszProviderDllPath,
                             -1,
                             PackedItem.DllPath,
                             sizeof(PackedItem.DllPath),
                             NULL,
                             NULL))
    {
        return WSAEINVAL;
    }

    RootKey = WsOpenRegistryRoot();
    if (!RootKey)
        return WSANO_RECOVERY;

    Catalog = WsTcAllocate();
    if (!Catalog)
    {
        RegCloseKey(RootKey);
        return WSA_NOT_ENOUGH_MEMORY;
    }

    if (!WsTcOpen(Catalog, RootKey))
    {
        RegCloseKey(RootKey);
        WsTcDelete(Catalog);
        return WSANO_RECOVERY;
    }
    RegCloseKey(RootKey);

    if (RegOpenKeyEx(Catalog->CatalogKey,
                     "Catalog_Entries",
                     0,
                     KEY_ALL_ACCESS,
                     &EntriesKey) != ERROR_SUCCESS)
    {
        EntriesKey = NULL;
        goto Cleanup;
    }

    RegType = REG_DWORD;
    RegSize = sizeof(EntryCount);
    if (RegQueryValueEx(Catalog->CatalogKey,
                        "Num_Catalog_Entries",
                        0,
                        &RegType,
                        (LPBYTE)&EntryCount,
                        &RegSize) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }

    for (i = 1; i <= EntryCount; i++)
    {
        CatalogEntry = WsTcEntryAllocate();
        if (!CatalogEntry)
        {
            ErrorCode = WSA_NOT_ENOUGH_MEMORY;
            goto Cleanup;
        }

        if (WsTcEntryInitializeFromRegistry(CatalogEntry, EntriesKey, i) != ERROR_SUCCESS ||
            IsEqualGUID(&CatalogEntry->ProtocolInfo.ProviderId, lpProviderId))
        {
            WsTcEntryDereference(CatalogEntry);
            goto Cleanup;
        }
        WsTcEntryDereference(CatalogEntry);
    }

    RegType = REG_DWORD;
    RegSize = sizeof(Serial);
    if (RegQueryValueEx(Catalog->CatalogKey,
                        "Serial_Access_Num",
                        0,
                        &RegType,
                        (LPBYTE)&Serial,
                        &RegSize) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }

    sprintf(LockName, "%8.8lX", Serial);
    if (RegCreateKeyEx(Catalog->CatalogKey,
                       LockName,
                       0,
                       NULL,
                       REG_OPTION_VOLATILE,
                       KEY_ALL_ACCESS,
                       NULL,
                       &LockKey,
                       &Disposition) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }
    RegCloseKey(LockKey);
    if (Disposition != REG_CREATED_NEW_KEY)
        goto Cleanup;
    Locked = TRUE;

    RegType = REG_DWORD;
    RegSize = sizeof(CurrentSerial);
    if (RegQueryValueEx(Catalog->CatalogKey,
                        "Serial_Access_Num",
                        0,
                        &RegType,
                        (LPBYTE)&CurrentSerial,
                        &RegSize) != ERROR_SUCCESS ||
        CurrentSerial != Serial)
    {
        goto Cleanup;
    }

    RegType = REG_DWORD;
    RegSize = sizeof(EntryCount);
    if (RegQueryValueEx(Catalog->CatalogKey,
                        "Num_Catalog_Entries",
                        0,
                        &RegType,
                        (LPBYTE)&EntryCount,
                        &RegSize) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }

    RegType = REG_DWORD;
    RegSize = sizeof(NextId);
    if (RegQueryValueEx(Catalog->CatalogKey,
                        "Next_Catalog_Entry_ID",
                        0,
                        &RegType,
                        (LPBYTE)&NextId,
                        &RegSize) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }

    for (i = 0; i < dwNumberOfEntries; i++)
    {
        PackedItem.ProtocolInfo = lpProtocolInfoList[i];
        PackedItem.ProtocolInfo.ProviderId = *lpProviderId;
        PackedItem.ProtocolInfo.dwCatalogEntryId = NextId + i;

        sprintf(EntryName, "%0""12""lu", EntryCount + i + 1);
        if (RegCreateKeyEx(EntriesKey,
                           EntryName,
                           0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           KEY_ALL_ACCESS,
                           NULL,
                           &EntryKey,
                           &Disposition) != ERROR_SUCCESS)
        {
            goto Cleanup;
        }

        Status = RegSetValueEx(EntryKey,
                               "PackedCatalogItem",
                               0,
                               REG_BINARY,
                               (LPBYTE)&PackedItem,
                               sizeof(PackedItem));
        RegCloseKey(EntryKey);
        if (Status != ERROR_SUCCESS)
            goto Cleanup;
    }

    NextId += dwNumberOfEntries;
    if (RegSetValueEx(Catalog->CatalogKey,
                      "Next_Catalog_Entry_ID",
                      0,
                      REG_DWORD,
                      (LPBYTE)&NextId,
                      sizeof(NextId)) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }

    EntryCount += dwNumberOfEntries;
    if (RegSetValueEx(Catalog->CatalogKey,
                      "Num_Catalog_Entries",
                      0,
                      REG_DWORD,
                      (LPBYTE)&EntryCount,
                      sizeof(EntryCount)) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }

    Serial++;
    RegSetValueEx(Catalog->CatalogKey,
                  "Serial_Access_Num",
                  0,
                  REG_DWORD,
                  (LPBYTE)&Serial,
                  sizeof(Serial));
    ErrorCode = ERROR_SUCCESS;

Cleanup:
    if (EntriesKey)
        RegCloseKey(EntriesKey);
    if (Locked)
        RegDeleteKey(Catalog->CatalogKey, LockName);
    WsTcDelete(Catalog);
    return ErrorCode;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCInstallProvider(IN LPGUID lpProviderId,
                   IN CONST WCHAR * lpszProviderDllPath,
                   IN CONST LPWSAPROTOCOL_INFOW lpProtocolInfoList,
                   IN DWORD dwNumberOfEntries,
                   OUT LPINT lpErrno)
{
    INT ErrorCode;

    ErrorCode = WsTcInstallProvider(lpProviderId,
                                    lpszProviderDllPath,
                                    lpProtocolInfoList,
                                    dwNumberOfEntries);
    if (ErrorCode != ERROR_SUCCESS)
    {
        *lpErrno = ErrorCode;
        return SOCKET_ERROR;
    }

    return ERROR_SUCCESS;
}

INT
WSPAPI
WSCInstallProvider64_32(IN LPGUID lpProviderId,
                        IN CONST WCHAR * lpszProviderDllPath,
                        IN CONST LPWSAPROTOCOL_INFOW lpProtocolInfoList,
                        IN DWORD dwNumberOfEntries,
                        OUT LPINT lpErrno)
{
    return WSCInstallProvider(lpProviderId,
                              lpszProviderDllPath,
                              lpProtocolInfoList,
                              dwNumberOfEntries,
                              lpErrno);
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCDeinstallProvider(IN LPGUID lpProviderId,
                     OUT LPINT lpErrno)
{
    UNIMPLEMENTED;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCWriteProviderOrder(IN LPDWORD lpwdCatalogEntryId,
                      IN DWORD dwNumberOfEntries)
{
    UNIMPLEMENTED;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}
