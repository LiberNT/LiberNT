/*
 * PROJECT:     LiberNT Property System
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Registered file property handler lookup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>
#include <shlwapi.h>
#include <strsafe.h>

HRESULT WINAPI
PSLookupPropertyHandlerCLSID(PCWSTR pszFilePath, CLSID *pclsid)
{
    WCHAR Key[MAX_PATH], Value[40];
    DWORD Size = sizeof(Value);
    LPCWSTR Extension;
    LSTATUS Error;
    HRESULT hr;

    if (!pszFilePath || !pclsid)
        return E_INVALIDARG;
    *pclsid = CLSID_NULL;
    Extension = PathFindExtensionW(pszFilePath);
    if (!*Extension)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    hr = StringCchPrintfW(Key, ARRAY_SIZE(Key),
                          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertyHandlers\\%s",
                          Extension);
    if (FAILED(hr))
        return hr;
    Error = SHRegGetValueW(HKEY_LOCAL_MACHINE, Key, NULL, SRRF_RT_REG_SZ, NULL, Value, &Size);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    return CLSIDFromString(Value, pclsid);
}
