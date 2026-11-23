/*
 * PROJECT:     ReactOS System Control Panel Applet
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/cpl/sysdm/startrec.c
 * PURPOSE:     Computer settings for startup and recovery
 * COPYRIGHT:   Copyright 2006 Ged Murphy <gedmurphy@gmail.com>
 *              Copyright 2006 Christoph von Wittich <Christoph@ApiViewer.de>
 *              Copyright 2007 Johannes Anderwald <johannes.anderwald@reactos.org>
 */

#include "precomp.h"

#define BCD_OBJECTS        L"BCD00000000\\Objects"
#define BCD_BOOTMGR        L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define BCD_DISPLAY_ORDER  L"24000001"
#define BCD_DEFAULT        L"23000003"
#define BCD_TIMEOUT        L"25000004"
#define BCD_DESCRIPTION    L"12000004"
#define BCD_ID_CHARS       39
#define BCD_MAX_TIMEOUT    999

typedef struct _BOOTRECORD
{
    WCHAR szId[BCD_ID_CHARS];
} BOOTRECORD, *PBOOTRECORD;

typedef struct _STARTINFO
{
    WCHAR szDumpFile[MAX_PATH];
    WCHAR szMinidumpDir[MAX_PATH];
    DWORD dwCrashDumpEnabled;
} STARTINFO, *PSTARTINFO;

BOOL SaveRecoveryOptions;

static VOID
SetTimeout(HWND hwndDlg, INT Timeout)
{
    if (Timeout == 0)
    {
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECLISTUPDWN), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECLISTEDIT), FALSE);
    }
    else
    {
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECLISTUPDWN), TRUE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECLISTEDIT), TRUE);
    }
    SendDlgItemMessageW(hwndDlg, IDC_STRRECLISTUPDWN, UDM_SETRANGE, (WPARAM) 0, (LPARAM) MAKELONG((short) 999, 0));
    SendDlgItemMessageW(hwndDlg, IDC_STRRECLISTUPDWN, UDM_SETPOS, (WPARAM) 0, (LPARAM) MAKELONG((short) Timeout, 0));
}

static VOID
SetRecoveryTimeout(HWND hwndDlg, INT Timeout)
{
    if (Timeout == 0)
    {
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECRECUPDWN), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECRECEDIT), FALSE);
    }
    else
    {
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECRECUPDWN), TRUE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECRECEDIT), TRUE);
    }
    SendDlgItemMessageW(hwndDlg, IDC_STRRECRECUPDWN, UDM_SETRANGE, (WPARAM) 0, (LPARAM) MAKELONG((short) 999, 0));
    SendDlgItemMessageW(hwndDlg, IDC_STRRECRECUPDWN, UDM_SETPOS, (WPARAM) 0, (LPARAM) MAKELONG((short) Timeout, 0));
}


static LONG
ReadBcdElement(PCWSTR Object, PCWSTR Element, DWORD Flags, PVOID Data, DWORD *Size)
{
    WCHAR Path[128];

    if (FAILED(StringCchPrintfW(Path, _countof(Path), L"%s\\%s\\Elements\\%s", BCD_OBJECTS, Object, Element)))
        return ERROR_INVALID_PARAMETER;

    return RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"Element", Flags, NULL, Data, Size);
}

static VOID
EnableRestorePrivilege(VOID)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
        return;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, SE_RESTORE_NAME, &Privileges.Privileges[0].Luid))
        AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL);

    CloseHandle(Token);
}

static LONG
WriteBcdElement(PCWSTR Object, PCWSTR Element, DWORD Type, const VOID *Data, DWORD Size)
{
    WCHAR Path[128];
    HKEY Key;
    LONG Error;

    if (FAILED(StringCchPrintfW(Path, _countof(Path), L"%s\\%s\\Elements\\%s", BCD_OBJECTS, Object, Element)))
        return ERROR_INVALID_PARAMETER;

    EnableRestorePrivilege();
    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            Path,
                            0,
                            NULL,
                            REG_OPTION_BACKUP_RESTORE,
                            KEY_SET_VALUE,
                            NULL,
                            &Key,
                            NULL);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = RegSetValueExW(Key, L"Element", 0, Type, Data, Size);
    if (Error == ERROR_SUCCESS)
        Error = RegFlushKey(Key);

    RegCloseKey(Key);
    return Error;
}

static VOID
DeleteBootRecords(HWND hwndDlg)
{
    LRESULT lIndex;
    LONG index;
    PBOOTRECORD pRecord;

    lIndex = SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_GETCOUNT, (WPARAM)0, (LPARAM)0);
    if (lIndex == CB_ERR)
        return;

    for (index = 0; index <lIndex; index++)
    {
        pRecord = (PBOOTRECORD) SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_GETITEMDATA, (WPARAM)index, (LPARAM)0);
        if ((INT_PTR)pRecord != CB_ERR)
        {
            HeapFree(GetProcessHeap(), 0, pRecord);
        }
    }

    SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_RESETCONTENT, (WPARAM)0, (LPARAM)0);
}

static LRESULT
LoadOSList(HWND hwndDlg)
{
    PWSTR Order;
    WCHAR Default[BCD_ID_CHARS];
    WCHAR Description[MAX_PATH];
    ULONGLONG Timeout = 0;
    PBOOTRECORD pRecord;
    LRESULT lResult, lCount, lIndex;
    DWORD Size;
    PCWSTR Id;

    Size = 0;
    if (ReadBcdElement(BCD_BOOTMGR, BCD_DISPLAY_ORDER, RRF_RT_REG_MULTI_SZ, NULL, &Size) != ERROR_SUCCESS)
        return FALSE;

    Order = HeapAlloc(GetProcessHeap(), 0, Size + 2 * sizeof(WCHAR));
    if (Order == NULL)
        return FALSE;

    Size += 2 * sizeof(WCHAR);
    if (ReadBcdElement(BCD_BOOTMGR, BCD_DISPLAY_ORDER, RRF_RT_REG_MULTI_SZ, Order, &Size) != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Order);
        return FALSE;
    }

    Size = sizeof(Default);
    if (ReadBcdElement(BCD_BOOTMGR, BCD_DEFAULT, RRF_RT_REG_SZ, Default, &Size) != ERROR_SUCCESS)
        Default[0] = UNICODE_NULL;

    Size = sizeof(Timeout);
    if (ReadBcdElement(BCD_BOOTMGR, BCD_TIMEOUT, RRF_RT_REG_BINARY, &Timeout, &Size) != ERROR_SUCCESS ||
        Size != sizeof(Timeout))
    {
        Timeout = 0;
    }

    for (Id = Order; *Id != UNICODE_NULL; Id += wcslen(Id) + 1)
    {
        Size = sizeof(Description);
        if (ReadBcdElement(Id, BCD_DESCRIPTION, RRF_RT_REG_SZ, Description, &Size) != ERROR_SUCCESS)
            StringCchCopyW(Description, _countof(Description), Id);

        pRecord = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BOOTRECORD));
        if (pRecord == NULL)
            break;

        StringCchCopyW(pRecord->szId, _countof(pRecord->szId), Id);
        lResult = SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_ADDSTRING, (WPARAM)0, (LPARAM)Description);
        if (lResult == CB_ERR || lResult == CB_ERRSPACE)
        {
            HeapFree(GetProcessHeap(), 0, pRecord);
            continue;
        }

        SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)pRecord);
    }

    HeapFree(GetProcessHeap(), 0, Order);

    lCount = SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_GETCOUNT, (WPARAM)0, (LPARAM)0);
    for (lIndex = 0; lCount != CB_ERR && lIndex < lCount; lIndex++)
    {
        pRecord = (PBOOTRECORD)SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_GETITEMDATA, (WPARAM)lIndex, (LPARAM)0);
        if ((INT_PTR)pRecord != CB_ERR && _wcsicmp(pRecord->szId, Default) == 0)
        {
            SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_SETCURSEL, (WPARAM)lIndex, (LPARAM)0);
            break;
        }
    }

    if (Timeout != 0)
        SendDlgItemMessageW(hwndDlg, IDC_STRECLIST, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);

    SetTimeout(hwndDlg, (INT)min(Timeout, (ULONGLONG)BCD_MAX_TIMEOUT));
    return TRUE;
}

static VOID
SetCrashDlgItems(HWND hwnd, PSTARTINFO pStartInfo)
{
    if (pStartInfo->dwCrashDumpEnabled == 0)
    {
        /* No crash information required */
        EnableWindow(GetDlgItem(hwnd, IDC_STRRECDUMPFILE), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_STRRECOVERWRITE), FALSE);
    }
    else if (pStartInfo->dwCrashDumpEnabled == 3)
    {
        /* Minidump type */
        EnableWindow(GetDlgItem(hwnd, IDC_STRRECDUMPFILE), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_STRRECOVERWRITE), FALSE);
        SendMessageW(GetDlgItem(hwnd, IDC_STRRECDUMPFILE), WM_SETTEXT, (WPARAM)0, (LPARAM)pStartInfo->szMinidumpDir);
    }
    else if (pStartInfo->dwCrashDumpEnabled == 1 || pStartInfo->dwCrashDumpEnabled == 2)
    {
        /* Kernel or complete dump */
        EnableWindow(GetDlgItem(hwnd, IDC_STRRECDUMPFILE), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_STRRECOVERWRITE), TRUE);
        SendMessageW(GetDlgItem(hwnd, IDC_STRRECDUMPFILE), WM_SETTEXT, (WPARAM)0, (LPARAM)pStartInfo->szDumpFile);
    }
    SendDlgItemMessageW(hwnd, IDC_STRRECDEBUGCOMBO, CB_SETCURSEL, (WPARAM)pStartInfo->dwCrashDumpEnabled, (LPARAM)0);
}

static VOID
WriteStartupRecoveryOptions(HWND hwndDlg, PSTARTINFO pStartInfo)
{
    HKEY hKey;
    DWORD lResult;

    lResult = (DWORD)RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                    L"System\\CurrentControlSet\\Control\\CrashControl",
                                    0,
                                    NULL,
                                    REG_OPTION_NON_VOLATILE,
                                    KEY_WRITE,
                                    NULL,
                                    &hKey,
                                    NULL);
    if (lResult != ERROR_SUCCESS)
    {
         /* Failed to open key */
        SetLastError(lResult);
        ShowLastWin32Error(hwndDlg);

        return;
    }

    lResult = (DWORD) SendDlgItemMessage(hwndDlg, IDC_STRRECWRITEEVENT, BM_GETCHECK, (WPARAM)0, (LPARAM)0);
    RegSetValueExW(hKey, L"LogEvent", 0, REG_DWORD, (LPBYTE)&lResult, sizeof(lResult));

    lResult = (DWORD) SendDlgItemMessage(hwndDlg, IDC_STRRECSENDALERT, BM_GETCHECK, (WPARAM)0, (LPARAM)0);
    RegSetValueExW(hKey, L"SendAlert", 0, REG_DWORD, (LPBYTE)&lResult, sizeof(lResult));

    lResult = (DWORD) SendDlgItemMessage(hwndDlg, IDC_STRRECRESTART, BM_GETCHECK, (WPARAM)0, (LPARAM)0);
    RegSetValueExW(hKey, L"AutoReboot", 0, REG_DWORD, (LPBYTE)&lResult, sizeof(lResult));

    lResult = (DWORD) SendDlgItemMessage(hwndDlg, IDC_STRRECOVERWRITE, BM_GETCHECK, (WPARAM)0, (LPARAM)0);
    RegSetValueExW(hKey, L"Overwrite", 0, REG_DWORD, (LPBYTE)&lResult, sizeof(lResult));


    if (pStartInfo->dwCrashDumpEnabled == 1 || pStartInfo->dwCrashDumpEnabled == 2)
    {
        SendDlgItemMessage(hwndDlg, IDC_STRRECDUMPFILE, WM_GETTEXT, (WPARAM)sizeof(pStartInfo->szDumpFile) / sizeof(WCHAR), (LPARAM)pStartInfo->szDumpFile);
        RegSetValueExW(hKey, L"DumpFile", 0, REG_EXPAND_SZ, (LPBYTE)pStartInfo->szDumpFile, (wcslen(pStartInfo->szDumpFile) + 1) * sizeof(WCHAR));
    }
    else if (pStartInfo->dwCrashDumpEnabled == 3)
    {
        SendDlgItemMessage(hwndDlg, IDC_STRRECDUMPFILE, WM_GETTEXT, (WPARAM)sizeof(pStartInfo->szDumpFile) / sizeof(WCHAR), (LPARAM)pStartInfo->szDumpFile);
        RegSetValueExW(hKey, L"MinidumpDir", 0, REG_EXPAND_SZ, (LPBYTE)pStartInfo->szDumpFile, (wcslen(pStartInfo->szDumpFile) + 1) * sizeof(WCHAR));
    }

    RegSetValueExW(hKey, L"CrashDumpEnabled", 0, REG_DWORD, (LPBYTE)&pStartInfo->dwCrashDumpEnabled, sizeof(pStartInfo->dwCrashDumpEnabled));
    RegCloseKey(hKey);
}

static VOID
LoadRecoveryOptions(HWND hwndDlg, PSTARTINFO pStartInfo)
{
    HKEY hKey;
    WCHAR szName[MAX_PATH];
    DWORD dwValue, dwValueLength, dwType, dwResult;

    dwResult = (DWORD)RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                      L"System\\CurrentControlSet\\Control\\CrashControl",
                                      0,
                                      NULL,
                                      REG_OPTION_NON_VOLATILE,
                                      KEY_READ,
                                      NULL,
                                      &hKey,
                                      NULL);
    if (dwResult != ERROR_SUCCESS)
    {
        /* Failed to open key */
        SetLastError(dwResult);
        ShowLastWin32Error(hwndDlg);

        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECWRITEEVENT), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECSENDALERT), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECRESTART), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECDEBUGCOMBO), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECDUMPFILE), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_STRRECOVERWRITE), FALSE);

        SaveRecoveryOptions = FALSE;
        return;
    }

    dwValueLength = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"LogEvent", NULL, &dwType, (LPBYTE)&dwValue, &dwValueLength) == ERROR_SUCCESS && dwType == REG_DWORD && dwValue)
        SendDlgItemMessageW(hwndDlg, IDC_STRRECWRITEEVENT, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);

    dwValueLength = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"SendAlert", NULL, &dwType, (LPBYTE)&dwValue, &dwValueLength) == ERROR_SUCCESS && dwType == REG_DWORD && dwValue)
        SendDlgItemMessageW(hwndDlg, IDC_STRRECSENDALERT, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);

    dwValueLength = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"AutoReboot", NULL, &dwType, (LPBYTE)&dwValue, &dwValueLength) == ERROR_SUCCESS && dwType == REG_DWORD && dwValue)
        SendDlgItemMessageW(hwndDlg, IDC_STRRECRESTART, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);

    dwValueLength = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"Overwrite", NULL, &dwType, (LPBYTE)&dwValue, &dwValueLength) == ERROR_SUCCESS && dwType == REG_DWORD && dwValue)
        SendDlgItemMessageW(hwndDlg, IDC_STRRECOVERWRITE, BM_SETCHECK, (WPARAM)BST_CHECKED, (LPARAM)0);

    dwValueLength = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"CrashDumpEnabled", NULL, &dwType, (LPBYTE)&dwValue, &dwValueLength) == ERROR_SUCCESS && dwType == REG_DWORD && dwValue)
        pStartInfo->dwCrashDumpEnabled = dwValue;

   dwValueLength = sizeof(pStartInfo->szDumpFile);
   if (RegQueryValueExW(hKey, L"DumpFile", NULL, &dwType, (LPBYTE)pStartInfo->szDumpFile, &dwValueLength) != ERROR_SUCCESS)
       pStartInfo->szDumpFile[0] = L'\0';

    dwValueLength = sizeof(pStartInfo->szMinidumpDir);
    if (RegQueryValueExW(hKey, L"MinidumpDir", NULL, &dwType, (LPBYTE)pStartInfo->szMinidumpDir, &dwValueLength) != ERROR_SUCCESS)
        pStartInfo->szMinidumpDir[0] = L'\0';

    if (LoadStringW(hApplet, IDS_NO_DUMP, szName, sizeof(szName) / sizeof(WCHAR)))
    {
        szName[(sizeof(szName)/sizeof(WCHAR))-1] = L'\0';
        SendDlgItemMessageW(hwndDlg, IDC_STRRECDEBUGCOMBO, CB_ADDSTRING, (WPARAM)0, (LPARAM) szName);
    }

    if (LoadStringW(hApplet, IDS_FULL_DUMP, szName, sizeof(szName) / sizeof(WCHAR)))
    {
        szName[(sizeof(szName)/sizeof(WCHAR))-1] = L'\0';
        SendDlgItemMessageW(hwndDlg, IDC_STRRECDEBUGCOMBO, CB_ADDSTRING, (WPARAM)0, (LPARAM) szName);
    }

    if (LoadStringW(hApplet, IDS_KERNEL_DUMP, szName, sizeof(szName) / sizeof(WCHAR)))
    {
        szName[(sizeof(szName)/sizeof(WCHAR))-1] = L'\0';
        SendDlgItemMessageW(hwndDlg, IDC_STRRECDEBUGCOMBO, CB_ADDSTRING, (WPARAM)0, (LPARAM) szName);
    }

    if (LoadStringW(hApplet, IDS_MINI_DUMP, szName, sizeof(szName) / sizeof(WCHAR)))
    {
        szName[(sizeof(szName)/sizeof(WCHAR))-1] = L'\0';
        SendDlgItemMessageW(hwndDlg, IDC_STRRECDEBUGCOMBO, CB_ADDSTRING, (WPARAM)0, (LPARAM) szName);
    }

    SetCrashDlgItems(hwndDlg, pStartInfo);
    RegCloseKey(hKey);

    SaveRecoveryOptions = TRUE;
}


/* Property page dialog callback */
INT_PTR CALLBACK
StartRecDlgProc(HWND hwndDlg,
                UINT uMsg,
                WPARAM wParam,
                LPARAM lParam)
{
    PSTARTINFO pStartInfo;
    PBOOTRECORD pRecord;
    int iTimeout;
    LRESULT lResult;

    UNREFERENCED_PARAMETER(lParam);

    pStartInfo = (PSTARTINFO)GetWindowLongPtr(hwndDlg, DWLP_USER);

    switch(uMsg)
    {
        case WM_INITDIALOG:
            pStartInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(STARTINFO));
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)pStartInfo);

            LoadRecoveryOptions(hwndDlg, pStartInfo);
            SetRecoveryTimeout(hwndDlg, 0);
            return LoadOSList(hwndDlg);

        case WM_DESTROY:
            DeleteBootRecords(hwndDlg);
            HeapFree(GetProcessHeap(), 0, pStartInfo);
            break;

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDOK:
                {
                    ULONGLONG Timeout;

                    if (SendDlgItemMessage(hwndDlg, IDC_STRECLIST, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED)
                        iTimeout = SendDlgItemMessage(hwndDlg, IDC_STRRECLISTUPDWN, UDM_GETPOS, (WPARAM)0, (LPARAM)0);
                    else
                        iTimeout = 0;

                    Timeout = (ULONGLONG)max(0, min(iTimeout, BCD_MAX_TIMEOUT));
                    WriteBcdElement(BCD_BOOTMGR, BCD_TIMEOUT, REG_BINARY, &Timeout, sizeof(Timeout));

                    lResult = SendDlgItemMessageW(hwndDlg, IDC_STRECOSCOMBO, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
                    if (lResult != CB_ERR)
                    {
                        pRecord = (PBOOTRECORD)SendDlgItemMessage(hwndDlg, IDC_STRECOSCOMBO, CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
                        if ((INT_PTR)pRecord != CB_ERR)
                        {
                            WriteBcdElement(BCD_BOOTMGR,
                                            BCD_DEFAULT,
                                            REG_SZ,
                                            pRecord->szId,
                                            (DWORD)((wcslen(pRecord->szId) + 1) * sizeof(WCHAR)));
                        }
                    }

                    if (SaveRecoveryOptions)
                    {
                        WriteStartupRecoveryOptions(hwndDlg, pStartInfo);
                    }

                    EndDialog(hwndDlg,
                              LOWORD(wParam));
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hwndDlg,
                              LOWORD(wParam));
                    return TRUE;

                case IDC_STRECLIST:
                    if (SendDlgItemMessage(hwndDlg, IDC_STRECLIST, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED)
                        SetTimeout(hwndDlg, 30);
                    else
                        SetTimeout(hwndDlg, 0);
                    break;

                case IDC_STRRECREC:
                    if (SendDlgItemMessage(hwndDlg, IDC_STRRECREC, BM_GETCHECK, (WPARAM)0, (LPARAM)0) == BST_CHECKED)
                        SetRecoveryTimeout(hwndDlg, 30);
                    else
                        SetRecoveryTimeout(hwndDlg, 0);
                    break;

                case IDC_STRRECDEBUGCOMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        LRESULT lResult;

                        lResult = SendDlgItemMessage(hwndDlg, IDC_STRRECDEBUGCOMBO, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
                        if (lResult != CB_ERR && lResult != (LRESULT)pStartInfo->dwCrashDumpEnabled)
                        {
                            if (pStartInfo->dwCrashDumpEnabled == 1 || pStartInfo->dwCrashDumpEnabled == 2)
                            {
                                SendDlgItemMessageW(hwndDlg, IDC_STRRECDUMPFILE, WM_GETTEXT, (WPARAM)sizeof(pStartInfo->szDumpFile) / sizeof(WCHAR), (LPARAM)pStartInfo->szDumpFile);
                            }
                            else if (pStartInfo->dwCrashDumpEnabled == 3)
                            {
                                SendDlgItemMessageW(hwndDlg, IDC_STRRECDUMPFILE, WM_GETTEXT, (WPARAM)sizeof(pStartInfo->szMinidumpDir) / sizeof(WCHAR), (LPARAM)pStartInfo->szMinidumpDir);
                            }

                            pStartInfo->dwCrashDumpEnabled = (DWORD)lResult;
                            SetCrashDlgItems(hwndDlg, pStartInfo);
                        }
                    }
                    break;
            }
            break;
    }

    return FALSE;
}
