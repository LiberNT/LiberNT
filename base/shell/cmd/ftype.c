/*
 * PROJECT:     LiberNT Command Processor
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     FTYPE internal command
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifdef INCLUDE_CMD_FTYPE

#define FTYPE_COMMAND_SUBKEY _T("\\Shell\\Open\\Command")

static BOOL
BuildCommandKey(
    IN PCTSTR pszFileType,
    OUT PTSTR pszKey,
    IN SIZE_T cchKey)
{
    if (_tcslen(pszFileType) + _tcslen(FTYPE_COMMAND_SUBKEY) >= cchKey)
        return FALSE;

    _tcscpy(pszKey, pszFileType);
    _tcscat(pszKey, FTYPE_COMMAND_SUBKEY);
    return TRUE;
}

static LONG
PrintFileTypeEx(
    IN HKEY hKeyClasses,
    IN PCTSTR pszFileType)
{
    TCHAR szKey[MAX_PATH];
    DWORD dwType, dwSize = 0;
    PTSTR pszCommand;
    HKEY hKey;
    LONG lRet;

    if (!BuildCommandKey(pszFileType, szKey, ARRAYSIZE(szKey)))
        return ERROR_FILE_NOT_FOUND;

    lRet = RegOpenKeyEx(hKeyClasses, szKey, 0, KEY_QUERY_VALUE, &hKey);
    if (lRet != ERROR_SUCCESS)
        return lRet;

    lRet = RegQueryValueEx(hKey, NULL, NULL, &dwType, NULL, &dwSize);
    if ((lRet == ERROR_SUCCESS) &&
        ((dwType != REG_SZ && dwType != REG_EXPAND_SZ) || (dwSize <= sizeof(TCHAR))))
    {
        lRet = ERROR_FILE_NOT_FOUND;
    }
    if (lRet != ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return lRet;
    }

    pszCommand = cmd_alloc(dwSize + sizeof(TCHAR));
    if (!pszCommand)
    {
        RegCloseKey(hKey);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    lRet = RegQueryValueEx(hKey, NULL, NULL, NULL, (LPBYTE)pszCommand, &dwSize);
    RegCloseKey(hKey);
    if (lRet == ERROR_SUCCESS)
    {
        pszCommand[dwSize / sizeof(TCHAR)] = _T('\0');
        ConOutPrintf(_T("%s=%s\n"), pszFileType, pszCommand);
    }

    cmd_free(pszCommand);
    return lRet;
}

static LONG
PrintFileType(
    IN PCTSTR pszFileType)
{
    HKEY hKeyClasses;
    LONG lRet;

    lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Classes"), 0,
                        KEY_ENUMERATE_SUB_KEYS, &hKeyClasses);
    if (lRet != ERROR_SUCCESS)
    {
        ErrorMessage(lRet, NULL);
        return lRet;
    }

    lRet = PrintFileTypeEx(hKeyClasses, pszFileType);
    RegCloseKey(hKeyClasses);
    return lRet;
}

static LONG
PrintAllFileTypes(VOID)
{
    HKEY hKeyClasses;
    DWORD dwIndex, dwNumKeys = 0, dwNameLen = 0;
    PTSTR pszName;
    LONG lRet;

    lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Classes"), 0,
                        KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &hKeyClasses);
    if (lRet != ERROR_SUCCESS)
    {
        ErrorMessage(lRet, NULL);
        return lRet;
    }

    lRet = RegQueryInfoKey(hKeyClasses, NULL, NULL, NULL, &dwNumKeys, &dwNameLen,
                           NULL, NULL, NULL, NULL, NULL, NULL);
    if (lRet != ERROR_SUCCESS)
    {
        ErrorMessage(lRet, NULL);
        RegCloseKey(hKeyClasses);
        return lRet;
    }

    ++dwNameLen;
    pszName = cmd_alloc(dwNameLen * sizeof(TCHAR));
    if (!pszName)
    {
        RegCloseKey(hKeyClasses);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    for (dwIndex = 0; dwIndex < dwNumKeys; ++dwIndex)
    {
        DWORD dwSize = dwNameLen;

        lRet = RegEnumKeyEx(hKeyClasses, dwIndex, pszName, &dwSize, NULL, NULL, NULL, NULL);
        if (lRet != ERROR_SUCCESS)
            break;
        if (*pszName != _T('.'))
            PrintFileTypeEx(hKeyClasses, pszName);
    }

    cmd_free(pszName);
    RegCloseKey(hKeyClasses);
    return (lRet == ERROR_NO_MORE_ITEMS) ? ERROR_SUCCESS : lRet;
}

static LONG
SetFileTypeCommand(
    IN PCTSTR pszFileType,
    IN PCTSTR pszCommand)
{
    TCHAR szKey[MAX_PATH];
    HKEY hKeyClasses, hKey;
    LONG lRet;

    if (!BuildCommandKey(pszFileType, szKey, ARRAYSIZE(szKey)))
        return ERROR_INVALID_PARAMETER;

    lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Classes"), 0,
                        KEY_CREATE_SUB_KEY, &hKeyClasses);
    if (lRet != ERROR_SUCCESS)
        return lRet;

    lRet = RegCreateKeyEx(hKeyClasses, szKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                          KEY_SET_VALUE, NULL, &hKey, NULL);
    RegCloseKey(hKeyClasses);
    if (lRet != ERROR_SUCCESS)
        return lRet;

    lRet = RegSetValueEx(hKey, NULL, 0, REG_SZ, (LPBYTE)pszCommand,
                         (DWORD)(_tcslen(pszCommand) + 1) * sizeof(TCHAR));
    RegCloseKey(hKey);
    return lRet;
}

static LONG
RemoveFileTypeCommand(
    IN PCTSTR pszFileType)
{
    TCHAR szKey[MAX_PATH];
    HKEY hKeyClasses, hKey;
    LONG lRet;

    if (!BuildCommandKey(pszFileType, szKey, ARRAYSIZE(szKey)))
        return ERROR_FILE_NOT_FOUND;

    lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Classes"), 0,
                        KEY_ENUMERATE_SUB_KEYS, &hKeyClasses);
    if (lRet != ERROR_SUCCESS)
        return lRet;

    lRet = RegOpenKeyEx(hKeyClasses, szKey, 0, KEY_SET_VALUE, &hKey);
    RegCloseKey(hKeyClasses);
    if (lRet != ERROR_SUCCESS)
        return lRet;

    lRet = RegDeleteValue(hKey, NULL);
    RegCloseKey(hKey);
    return lRet;
}

static VOID
TrimTrailingSpaces(
    IN OUT PTSTR pszString)
{
    SIZE_T Length = _tcslen(pszString);

    while (Length > 0 && _istspace(pszString[Length - 1]))
        pszString[--Length] = _T('\0');
}

INT CommandFType(LPTSTR param)
{
    PTCHAR pEqualSign;
    LONG lRet;
    INT retval = 0;

    if (!_tcsncmp(param, _T("/?"), 2))
    {
        ConOutResPaging(TRUE, STRING_FTYPE_HELP);
        return 0;
    }

    TrimTrailingSpaces(param);

    if (!*param)
    {
        lRet = PrintAllFileTypes();
        retval = (lRet == ERROR_SUCCESS) ? 0 : 1;
    }
    else if ((pEqualSign = _tcschr(param, _T('='))) != NULL)
    {
        PTSTR pszCommand = pEqualSign + 1;

        *pEqualSign = _T('\0');
        TrimTrailingSpaces(param);

        if (*pszCommand == _T('\0'))
        {
            lRet = RemoveFileTypeCommand(param);
            if ((lRet != ERROR_SUCCESS) && (lRet != ERROR_FILE_NOT_FOUND))
            {
                ConErrResPrintf(STRING_ERROR_WHILE_PROCESSING, param);
                retval = 1;
            }
        }
        else
        {
            lRet = SetFileTypeCommand(param, pszCommand);
            if (lRet == ERROR_SUCCESS)
            {
                PrintFileType(param);
            }
            else
            {
                ConErrResPrintf(STRING_ERROR_WHILE_PROCESSING, param);
                retval = 1;
            }
        }
    }
    else if (PrintFileType(param) != ERROR_SUCCESS)
    {
        ConErrResPrintf(STRING_FTYPE_ERROR, param);
        retval = 1;
    }

    if (BatType != CMD_TYPE)
    {
        if (retval != 0)
            nErrorLevel = retval;
    }
    else
    {
        nErrorLevel = retval;
    }

    return retval;
}

#endif /* INCLUDE_CMD_FTYPE */
