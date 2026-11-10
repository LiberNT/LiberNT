/*
 * PROJECT:     ReactOS Spooler Router
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Main functions
 * COPYRIGHT:   Copyright 2015 Colin Finck (colin@reactos.org)
 */

#include "precomp.h"

// Global Variables
HANDLE hProcessHeap;
LIST_ENTRY PrintProviderList;


static DWORD
_AddPrintProviderToList(PCWSTR pwszFileName)
{
    DWORD dwErrorCode;
    HINSTANCE hinstPrintProvider;
    PInitializePrintProvidor pfnInitializePrintProvidor;
    PSPOOLSS_PRINT_PROVIDER pPrintProvider = NULL;

    // Try to load it.
    hinstPrintProvider = LoadLibraryW(pwszFileName);
    if (!hinstPrintProvider)
    {
        dwErrorCode = GetLastError();
        ERR("LoadLibraryW failed for \"%S\" with error %lu!\n", pwszFileName, dwErrorCode);
        goto Cleanup;
    }

    // Get the initialization routine.
    pfnInitializePrintProvidor = (PInitializePrintProvidor)GetProcAddress(hinstPrintProvider, "InitializePrintProvidor");
    if (!pfnInitializePrintProvidor)
    {
        dwErrorCode = GetLastError();
        ERR("GetProcAddress failed for \"%S\" with error %lu!\n", pwszFileName, dwErrorCode);
        goto Cleanup;
    }

    // Create a new SPOOLSS_PRINT_PROVIDER structure for it.
    pPrintProvider = DllAllocSplMem(sizeof(SPOOLSS_PRINT_PROVIDER));
    if (!pPrintProvider)
    {
        dwErrorCode = ERROR_NOT_ENOUGH_MEMORY;
        ERR("DllAllocSplMem failed!\n");
        goto Cleanup;
    }

    // Call the Print Provider initialization function.
    if (!pfnInitializePrintProvidor(&pPrintProvider->PrintProvider, sizeof(PRINTPROVIDOR), NULL))
    {
        dwErrorCode = GetLastError();
        ERR("InitializePrintProvidor failed for \"%S\" with error %lu!\n", pwszFileName, dwErrorCode);
        goto Cleanup;
    }

    // Add this Print Provider to the list.
    InsertTailList(&PrintProviderList, &pPrintProvider->Entry);

    // Don't let the cleanup routine free this.
    pPrintProvider = NULL;
    dwErrorCode = ERROR_SUCCESS;

Cleanup:
    if (pPrintProvider)
        DllFreeSplMem(pPrintProvider);

    return dwErrorCode;
}

static BOOL
_InitializePrintProviderList(VOID)
{
    DWORD cbFileName;
    DWORD cchMaxSubKey;
    DWORD cchPrintProviderName;
    DWORD dwErrorCode;
    DWORD dwSubKeys;
    DWORD i;
    HKEY hKey = NULL;
    HKEY hSubKey = NULL;
    PWSTR pwszPrintProviderName = NULL;
    WCHAR wszFileName[MAX_PATH];

    // Initialize an empty list for our Print Providers.
    InitializeListHead(&PrintProviderList);

    // First add the Local Spooler.
    // This one must exist and must be the first one in the list.
    dwErrorCode = _AddPrintProviderToList(L"localspl");
    if (dwErrorCode != ERROR_SUCCESS)
    {
        ERR("The Local Spooler could not be loaded!\n");
        goto Cleanup;
    }

    // Now add additional Print Providers from the registry.
    // First of all, open the key containing print providers.
    dwErrorCode = (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Print\\Providers", 0, KEY_READ, &hKey);
    if (dwErrorCode != ERROR_SUCCESS)
    {
        ERR("RegOpenKeyExW failed with status %lu!\n", dwErrorCode);
        goto Cleanup;
    }

    // Get the number of Print Providers and maximum sub key length.
    dwErrorCode = (DWORD)RegQueryInfoKeyW(hKey, NULL, NULL, NULL, &dwSubKeys, &cchMaxSubKey, NULL, NULL, NULL, NULL, NULL, NULL);
    if (dwErrorCode != ERROR_SUCCESS)
    {
        ERR("RegQueryInfoKeyW failed with status %lu!\n", dwErrorCode);
        goto Cleanup;
    }

    // Allocate a temporary buffer for the Print Provider names.
    pwszPrintProviderName = DllAllocSplMem((cchMaxSubKey + 1) * sizeof(WCHAR));
    if (!pwszPrintProviderName)
    {
        dwErrorCode = ERROR_NOT_ENOUGH_MEMORY;
        ERR("DllAllocSplMem failed!\n");
        goto Cleanup;
    }

    // Loop through all available Print Providers.
    for (i = 0; i < dwSubKeys; i++)
    {
        // Cleanup tasks from the previous run
        if (hSubKey)
        {
            RegCloseKey(hSubKey);
            hSubKey = NULL;
        }

        // Get the name of this Print Provider.
        cchPrintProviderName = cchMaxSubKey + 1;
        dwErrorCode = (DWORD)RegEnumKeyExW(hKey, i, pwszPrintProviderName, &cchPrintProviderName, NULL, NULL, NULL, NULL);
        if (dwErrorCode != ERROR_SUCCESS)
        {
            ERR("RegEnumKeyExW failed for iteration %lu with status %lu!\n", i, dwErrorCode);
            continue;
        }

        // Open this Print Provider's registry key.
        dwErrorCode = (DWORD)RegOpenKeyExW(hKey, pwszPrintProviderName, 0, KEY_READ, &hSubKey);
        if (dwErrorCode != ERROR_SUCCESS)
        {
            ERR("RegOpenKeyExW failed for Print Provider \"%S\" with status %lu!\n", pwszPrintProviderName, dwErrorCode);
            continue;
        }

        // Get the file name of the Print Provider.
        cbFileName = MAX_PATH * sizeof(WCHAR);
        dwErrorCode = (DWORD)RegQueryValueExW(hKey, L"Driver", NULL, NULL, (PBYTE)wszFileName, &cbFileName);
        if (dwErrorCode != ERROR_SUCCESS)
        {
            ERR("RegQueryValueExW failed with status %lu!\n", dwErrorCode);
            continue;
        }

        // Load and add it to the list.
        dwErrorCode = _AddPrintProviderToList(wszFileName);
        if (dwErrorCode != ERROR_SUCCESS)
            continue;
    }

    dwErrorCode = ERROR_SUCCESS;

Cleanup:
    // Inside the loop
    if (hSubKey)
        RegCloseKey(hSubKey);

    // Outside the loop
    if (pwszPrintProviderName)
        DllFreeSplMem(pwszPrintProviderName);

    if (hKey)
        RegCloseKey(hKey);

    SetLastError(dwErrorCode);
    return (dwErrorCode == ERROR_SUCCESS);
}

BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
			hProcessHeap = GetProcessHeap();
            break;
    }

    return TRUE;
}

BOOL WINAPI
InitializeRouter(HANDLE SpoolerStatusHandle)
{
    return _InitializePrintProviderList();
}

BOOL WINAPI
SplInitializeWinSpoolDrv(PVOID* pTable)
{
    HINSTANCE hWinspool;
    int i;

    hWinspool = LoadLibraryW(L"winspool.drv");
    if (!hWinspool)
    {
        ERR("Could not load winspool.drv, last error is %lu!\n", GetLastError());
        return FALSE;
    }

    // Get the function pointers which are meant to be returned by this function.
    pTable[0] = GetProcAddress(hWinspool, "OpenPrinterW");
    pTable[1] = GetProcAddress(hWinspool, "ClosePrinter");
    pTable[2] = GetProcAddress(hWinspool, "SpoolerDevQueryPrintW");
    pTable[3] = GetProcAddress(hWinspool, "SpoolerPrinterEvent");
    pTable[4] = GetProcAddress(hWinspool, "DocumentPropertiesW");
    pTable[5] = GetProcAddress(hWinspool, (LPSTR)212);
    pTable[6] = GetProcAddress(hWinspool, (LPSTR)213);
    pTable[7] = GetProcAddress(hWinspool, (LPSTR)214);
    pTable[8] = GetProcAddress(hWinspool, (LPSTR)215);

    // Verify that all calls succeeded.
    for (i = 0; i < 9; i++)
    {
        if (!pTable[i])
        {
            FreeLibrary(hWinspool);
            return FALSE;
        }
    }

    return TRUE;
}

BOOL WINAPI
SplIsUpgrade(VOID)
{
	return FALSE;
}

BOOL WINAPI
SpoolerInit(VOID)
{
    // Nothing to do here yet
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL
_AddOtherName(PWSTR** ppNames, PDWORD pcNames, PCWSTR pwszName)
{
    DWORD cbName = (wcslen(pwszName) + 1) * sizeof(WCHAR);
    PWSTR* pNewNames = ReallocSplMem(*ppNames, *pcNames * sizeof(PWSTR), (*pcNames + 1) * sizeof(PWSTR));

    if (!pNewNames)
        return FALSE;

    *ppNames = pNewNames;
    pNewNames[*pcNames] = DllAllocSplMem(cbName);
    if (!pNewNames[*pcNames])
        return FALSE;

    CopyMemory(pNewNames[*pcNames], pwszName, cbName);
    (*pcNames)++;
    return TRUE;
}

static VOID
_FreeOtherNames(PWSTR* pNames, DWORD cNames)
{
    DWORD i;

    if (!pNames)
        return;

    for (i = 0; i < cNames; i++)
        DllFreeSplMem(pNames[i]);

    DllFreeSplMem(pNames);
}

static BOOL
_AddAdapterAddresses(PWSTR** ppNames, PDWORD pcNames, PIP_ADAPTER_ADDRESSES pAdapters, ADDRESS_FAMILY Family)
{
    PIP_ADAPTER_ADDRESSES pAdapter;
    PIP_ADAPTER_UNICAST_ADDRESS pAddress;
    WCHAR wszAddress[INET6_ADDRSTRLEN];

    for (pAdapter = pAdapters; pAdapter; pAdapter = pAdapter->Next)
    {
        if (pAdapter->OperStatus != IfOperStatusUp || pAdapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;

        for (pAddress = pAdapter->FirstUnicastAddress; pAddress; pAddress = pAddress->Next)
        {
            if (pAddress->Address.lpSockaddr->sa_family != Family)
                continue;

            if (Family == AF_INET6)
                RtlIpv6AddressToStringW(&((PSOCKADDR_IN6)pAddress->Address.lpSockaddr)->sin6_addr, wszAddress);
            else
                RtlIpv4AddressToStringW(&((PSOCKADDR_IN)pAddress->Address.lpSockaddr)->sin_addr, wszAddress);

            if (!_AddOtherName(ppNames, pcNames, wszAddress))
                return FALSE;
        }
    }

    return TRUE;
}

BOOL WINAPI
BuildOtherNamesFromMachineName(LPVOID * ptr1, ULONG * ptr2)
{
    PWSTR* pNames = NULL;
    DWORD cNames = 0;
    WCHAR wszComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD cchComputerName = _countof(wszComputerName);
    PIP_ADAPTER_ADDRESSES pAdapters = NULL;
    ULONG cbAdapters = 0;
    ULONG ulFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
    DWORD dwErrorCode;

    TRACE("BuildOtherNamesFromMachineName(%p, %p)\n", ptr1, ptr2);

    if (!ptr1 || !ptr2)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *ptr1 = NULL;
    *ptr2 = 0;

    if (!GetComputerNameW(wszComputerName, &cchComputerName))
        return FALSE;

    if (!_AddOtherName(&pNames, &cNames, L"") || !_AddOtherName(&pNames, &cNames, wszComputerName))
        goto Failure;

    dwErrorCode = GetAdaptersAddresses(AF_UNSPEC, ulFlags, NULL, NULL, &cbAdapters);
    while (dwErrorCode == ERROR_BUFFER_OVERFLOW)
    {
        DllFreeSplMem(pAdapters);
        pAdapters = DllAllocSplMem(cbAdapters);
        if (!pAdapters)
            goto Failure;

        dwErrorCode = GetAdaptersAddresses(AF_UNSPEC, ulFlags, NULL, pAdapters, &cbAdapters);
    }

    if (dwErrorCode == ERROR_SUCCESS)
    {
        if (!_AddAdapterAddresses(&pNames, &cNames, pAdapters, AF_INET6) ||
            !_AddAdapterAddresses(&pNames, &cNames, pAdapters, AF_INET))
        {
            goto Failure;
        }
    }

    DllFreeSplMem(pAdapters);
    *ptr1 = pNames;
    *ptr2 = cNames;
    return TRUE;

Failure:
    DllFreeSplMem(pAdapters);
    _FreeOtherNames(pNames, cNames);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
}

VOID WINAPI
FreeOtherNames(LPVOID * ptr1, ULONG ptr2)
{
    TRACE("FreeOtherNames(%p, %lu)\n", ptr1, ptr2);

    _FreeOtherNames((PWSTR*)ptr1, ptr2);
}

