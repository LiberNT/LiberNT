/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests service window station security: integrity label and restricted access
 * COPYRIGHT:   Copyright 2026 LiberNT Contributors
 *
 * Windows places processes logged on with batch or service tokens on a
 * per-logon-session window station named Service-0x0-<luid>$.  On Windows 11
 * those window stations carry the same low-integrity no-write-up label as the
 * interactive WinSta0, so a restricted or low-integrity process cannot write
 * to them.  This test pins that behavior: the service child reports its window
 * station name and label, then a restricted attacker child tries to open the
 * service window station for write access and must be denied.
 */
#include "precomp.h"
#include <winbase.h>
#include <winuser.h>
#include <aclapi.h>
#include <sddl.h>
#include <lmaccess.h>
#include <lmerr.h>
#include <lmapibuf.h>

#define TEST_USER L"LntSvcStaTest"
#define TEST_PASSWORD L"Lnt!SvcSta#2026q"

#define REPORT_PATH L"svcsta_report.txt"
#define ATTACK_PATH L"svcsta_attack.txt"

static void WriteReportLine(LPCWSTR pszPath, LPCWSTR pszLine)
{
    HANDLE hFile;
    DWORD dwWritten;
    char szUtf[512];
    int len;

    hFile = CreateFileW(pszPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    len = WideCharToMultiByte(CP_UTF8, 0, pszLine, -1, szUtf, sizeof(szUtf), NULL, NULL);
    if (len > 0)
        WriteFile(hFile, szUtf, len - 1, &dwWritten, NULL);
    CloseHandle(hFile);
}

static void GetLabelSddl(HANDLE hObject, LPWSTR psz, SIZE_T cch)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    LPWSTR pszSddl = NULL;
    DWORD error;

    psz[0] = UNICODE_NULL;
    error = GetSecurityInfo(hObject, SE_WINDOW_OBJECT, LABEL_SECURITY_INFORMATION,
                            NULL, NULL, NULL, NULL, &pSD);
    if (error)
    {
        StringCchPrintfW(psz, cch, L"error %lu", error);
        return;
    }
    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(pSD, SDDL_REVISION_1,
                                                             LABEL_SECURITY_INFORMATION,
                                                             &pszSddl, NULL))
    {
        StringCchCopyW(psz, cch, pszSddl);
        LocalFree(pszSddl);
    }
    else
    {
        StringCchPrintfW(psz, cch, L"conversion error %lu", GetLastError());
    }
    LocalFree(pSD);
}

/* Child mode: report the window station name and its integrity label */
static void RunServiceChild(void)
{
    WCHAR szName[256] = L"";
    WCHAR szLabel[256];
    WCHAR szLine[600];
    DWORD cb = _countof(szName);

    if (!GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME,
                                   szName, sizeof(szName), &cb))
    {
        StringCchPrintfW(szLine, _countof(szLine), L"name-error %lu\n", GetLastError());
        WriteReportLine(REPORT_PATH, szLine);
        return;
    }
    StringCchPrintfW(szLine, _countof(szLine), L"name %ls\n", szName);
    WriteReportLine(REPORT_PATH, szLine);

    GetLabelSddl(GetProcessWindowStation(), szLabel, _countof(szLabel));
    StringCchPrintfW(szLine, _countof(szLine), L"label %ls\n", szLabel[0] ? szLabel : L"(none)");
    WriteReportLine(REPORT_PATH, szLine);

    /* Keep the logon session (and its window station) alive while the parent
       runs the restricted attack probe */
    Sleep(15000);
}

static void DeleteReportFiles(void)
{
    DeleteFileW(REPORT_PATH);
    DeleteFileW(ATTACK_PATH);
}

static void ReadReport(LPCWSTR pszPath, LPWSTR psz, SIZE_T cch)
{
    HANDLE hFile;
    DWORD dwRead;
    char szUtf[1024] = "";
    DWORD dwSize;

    psz[0] = UNICODE_NULL;
    hFile = CreateFileW(pszPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    dwSize = GetFileSize(hFile, NULL);
    if (dwSize < sizeof(szUtf) - 1)
    {
        ReadFile(hFile, szUtf, dwSize, &dwRead, NULL);
        szUtf[dwRead] = '\0';
        MultiByteToWideChar(CP_UTF8, 0, szUtf, -1, psz, (int)cch);
    }
    CloseHandle(hFile);
}

static BOOL CreateTestUser(void)
{
    USER_INFO_1 ui;
    DWORD dwError = 0;
    NET_API_STATUS status;

    memset(&ui, 0, sizeof(ui));
    ui.usri1_name = TEST_USER;
    ui.usri1_password = TEST_PASSWORD;
    ui.usri1_priv = USER_PRIV_USER;
    ui.usri1_flags = UF_NORMAL_ACCOUNT | UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    status = NetUserAdd(NULL, 1, (LPBYTE)&ui, &dwError);
    return status == NERR_Success || status == NERR_UserExists;
}

static void RemoveTestUser(void)
{
    NetUserDel(NULL, TEST_USER);
}

START_TEST(ServiceWinsta)
{
    WCHAR szCmd[MAX_PATH * 2], szExe[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE hToken = NULL;
    HANDLE hProcessToken;
    SID_IDENTIFIER_AUTHORITY world = { SECURITY_WORLD_SID_AUTHORITY };
    SID_AND_ATTRIBUTES restrictSids[1];
    PSID pEveryone = NULL;
    WCHAR szReport[1024];
    WCHAR szWinstaName[256] = L"";
    LPWSTR pEq;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        RunServiceChild();
        return;
    }
    if (!CreateTestUser())
    {
        skip("cannot create test user\n");
        return;
    }

    if (!LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_BATCH,
                    LOGON32_PROVIDER_DEFAULT, &hToken))
    {
        skip("LogonUserW batch failed %lu\n", GetLastError());
        RemoveTestUser();
        return;
    }

    DeleteReportFiles();
    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" ServiceWinsta child", szExe);
    if (!CreateProcessAsUserW(hToken, NULL, szCmd, NULL, NULL, FALSE, 0,
                              NULL, NULL, &si, &pi))
    {
        skip("CreateProcessAsUserW failed %lu\n", GetLastError());
        CloseHandle(hToken);
        RemoveTestUser();
        return;
    }
    /* The child writes its report quickly, then sleeps to keep the logon
       session alive; attack while it is still running */
    Sleep(4000);
    ReadReport(REPORT_PATH, szReport, _countof(szReport));
    trace("service child report: %ls\n", szReport);

    /* The child must sit on a per-logon-session service window station */
    pEq = wcsstr(szReport, L"name Service-0x0-");
    ok(pEq != NULL, "service window station name missing in report: %ls\n", szReport);
    if (pEq)
    {
        LPCWSTR pStart = pEq + 5;
        LPCWSTR pEnd = wcschr(pStart, L'\n');
        SIZE_T cchName = pEnd ? (SIZE_T)(pEnd - pStart) : wcslen(pStart);
        if (cchName >= _countof(szWinstaName))
            cchName = _countof(szWinstaName) - 1;
        memcpy(szWinstaName, pStart, cchName * sizeof(WCHAR));
        szWinstaName[cchName] = UNICODE_NULL;
    }

    /* On Windows 11 the service window station carries the low-integrity
       no-write-up label, the same as interactive WinSta0 */
    ok(wcsstr(szReport, L"label S:(ML;;NW;;;LW)") != NULL,
       "service window station label missing or wrong in report: %ls\n", szReport);

    /* A restricted attacker must not obtain write access to it.  Impersonate
       the restricted token in this thread: the access check then runs against
       the restricted token without needing a child process. */
    if (szWinstaName[0] &&
        OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hProcessToken))
    {
        HANDLE hRestrictedPri = NULL, hImp = NULL;
        HWINSTA hAttack = NULL;
        DWORD dwAttackError = 0;
        BOOL opened = FALSE;

        AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
                                 &pEveryone);
        restrictSids[0].Sid = pEveryone;
        restrictSids[0].Attributes = 0;
        if (CreateRestrictedToken(hProcessToken, DISABLE_MAX_PRIVILEGE, 0, NULL,
                                  0, NULL, 1, restrictSids, &hRestrictedPri) &&
            DuplicateTokenEx(hRestrictedPri, TOKEN_IMPERSONATE | TOKEN_QUERY, NULL,
                             SecurityImpersonation, TokenImpersonation, &hImp))
        {
            if (ImpersonateLoggedOnUser(hImp))
            {
                hAttack = OpenWindowStationW(szWinstaName, FALSE,
                                             WINSTA_WRITEATTRIBUTES | WRITE_DAC);
                dwAttackError = GetLastError();
                opened = (hAttack != NULL);
                if (hAttack)
                    CloseWindowStation(hAttack);
                RevertToSelf();
            }
            else
            {
                ok(0, "ImpersonateLoggedOnUser failed %lu\n", GetLastError());
            }
        }
        else
        {
            ok(0, "restricted impersonation token setup failed %lu\n", GetLastError());
        }
        ok(!opened,
           "restricted impersonation opened the service window station for write access\n");
        if (!opened)
            trace("restricted attack denied with error %lu\n", dwAttackError);
        if (hImp)
            CloseHandle(hImp);
        if (hRestrictedPri)
            CloseHandle(hRestrictedPri);
        if (pEveryone)
            FreeSid(pEveryone);
        CloseHandle(hProcessToken);
    }

    WaitForSingleObject(pi.hProcess, 20000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hToken);
    DeleteReportFiles();
    RemoveTestUser();
}
