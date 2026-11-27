/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests service window station security: naming, DACL shape and restricted access
 * COPYRIGHT:   Copyright 2026 LiberNT Contributors
 *
 * Windows places processes logged on with batch or service tokens on a
 * per-logon-session window station named Service-0x0-<luid>$.  Measured on
 * Windows 11 26H2 (SYSTEM, LocalService and a fresh user service), those
 * window stations carry NO mandatory integrity label - unlike the interactive
 * WinSta0, which is labeled low no-write-up - and their DACL grants full
 * access to the service account SID plus limited access to administrators.
 * A restricted attacker still cannot open the service window station for
 * write access: the DACL alone enforces that.  This test pins the Windows
 * behavior: the service child reports its window station name, label and
 * full security descriptor, then a restricted attacker probe must be denied.
 */
#include "precomp.h"
#include <winbase.h>
#include <winuser.h>
#include <aclapi.h>
#include <sddl.h>
#include <lmaccess.h>
#include <lmerr.h>
#include <lmapibuf.h>
#include <ntsecapi.h>

#define TEST_USER L"LntSvcStaTest"
#define TEST_PASSWORD L"Lnt!SvcSta#2026q"

/* Resolved at runtime from %SystemDrive% so the child and the parent agree
   on the location regardless of their current drives/directories. */
static WCHAR g_ReportDir[MAX_PATH] = L"";
static WCHAR g_ReportPath[MAX_PATH] = L"";

static void ResolveReportPaths(void)
{
    WCHAR szDrive[8] = L"C:";
    ExpandEnvironmentStringsW(L"%SystemDrive%", szDrive, _countof(szDrive));
    StringCchPrintfW(g_ReportDir, _countof(g_ReportDir), L"%ls\\LntSvcSta", szDrive);
    StringCchPrintfW(g_ReportPath, _countof(g_ReportPath), L"%ls\\svcsta_report.txt", g_ReportDir);
}

/* Report directory shared between the elevated parent and the unprivileged
   service child: created with a world-writable DACL whose inheritance flags
   propagate to the report files inside, so the parent can read what the
   child wrote regardless of which account the child runs as. */
static void CreateSharedDir(void)
{
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR pSD = NULL;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;FA;;;WD)", SDDL_REVISION_1, &pSD, NULL))
    {
        CreateDirectoryW(g_ReportDir, NULL);
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = pSD;
    CreateDirectoryW(g_ReportDir, &sa);
    LocalFree(pSD);
}

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
    WriteReportLine(g_ReportPath, L"runservicechild-entered\n");
    WCHAR szLabel[256];
    WCHAR szLine[2048];
    DWORD cb = _countof(szName);

    if (!GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME,
                                   szName, sizeof(szName), &cb))
    {
        StringCchPrintfW(szLine, _countof(szLine), L"name-error %lu\n", GetLastError());
        WriteReportLine(g_ReportPath, szLine);
        return;
    }
    StringCchPrintfW(szLine, _countof(szLine), L"name %ls\n", szName);
    WriteReportLine(g_ReportPath, szLine);

    GetLabelSddl(GetProcessWindowStation(), szLabel, _countof(szLabel));
    StringCchPrintfW(szLine, _countof(szLine), L"label %ls\n", szLabel[0] ? szLabel : L"(none)");
    WriteReportLine(g_ReportPath, szLine);

    /* Full security descriptor: owner/group/DACL/label in one SDDL string.
       This is the authoritative evidence for how Windows ACLs service winstas. */
    {
        PSECURITY_DESCRIPTOR pSD = NULL;
        LPWSTR pszSddl = NULL;
        SECURITY_INFORMATION si = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                  DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION;
        DWORD error = GetSecurityInfo(GetProcessWindowStation(), SE_WINDOW_OBJECT, si,
                                      NULL, NULL, NULL, NULL, &pSD);
        if (error)
        {
            StringCchPrintfW(szLine, _countof(szLine), L"fullsddl-error %lu\n", error);
        }
        else if (ConvertSecurityDescriptorToStringSecurityDescriptorW(pSD, SDDL_REVISION_1,
                                                                      si, &pszSddl, NULL))
        {
            StringCchPrintfW(szLine, _countof(szLine), L"fullsddl %ls\n", pszSddl);
            LocalFree(pszSddl);
        }
        else
        {
            StringCchPrintfW(szLine, _countof(szLine), L"fullsddl-conv-error %lu\n", GetLastError());
        }
        if (pSD)
            LocalFree(pSD);
        WriteReportLine(g_ReportPath, szLine);
    }

    /* The child's own account SID: on Windows the window station DACL grants
       full access to exactly this SID (not the logon session SID) */
    {
        HANDLE hToken = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        {
            UCHAR buf[256];
            DWORD cb = sizeof(buf);
            if (GetTokenInformation(hToken, TokenUser, buf, cb, &cb))
            {
                PTOKEN_USER tu = (PTOKEN_USER)buf;
                LPWSTR pszSid = NULL;
                if (ConvertSidToStringSidW(tu->User.Sid, &pszSid))
                {
                    StringCchPrintfW(szLine, _countof(szLine), L"usersid %ls\n", pszSid);
                    WriteReportLine(g_ReportPath, szLine);
                    LocalFree(pszSid);
                }
            }
            CloseHandle(hToken);
        }
    }

    /* Keep the logon session (and its window station) alive while the parent
       runs the restricted attack probe */
    Sleep(15000);
}

static void DeleteReportFiles(void)
{
    DeleteFileW(g_ReportPath);
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

/* Windows does not grant batch logon to fresh accounts; a service
   installer would grant it, so do the same here.  Best-effort: if the
   policy API is unavailable (or not implemented yet), the logon attempt
   below still tells us whether the platform needed the right. */
static BOOL SetBatchLogonRight(BOOL fGrant)
{
    LSA_OBJECT_ATTRIBUTES oa;
    LSA_HANDLE hPolicy = NULL;
    NTSTATUS status;
    UCHAR sidBuf[128];
    DWORD cbSid = sizeof(sidBuf);
    WCHAR domBuf[128];
    DWORD cchDom = _countof(domBuf);
    SID_NAME_USE use;
    LSA_UNICODE_STRING usRight;
    BOOL fDone = FALSE;

    if (!LookupAccountNameW(NULL, TEST_USER, sidBuf, &cbSid, domBuf, &cchDom, &use))
        return FALSE;

    memset(&oa, 0, sizeof(oa));
    status = LsaOpenPolicy(NULL, &oa, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES, &hPolicy);
    if (status != 0)
        return FALSE;

    usRight.Buffer = (PWSTR)(fGrant ? L"SeServiceLogonRight" : L"SeServiceLogonRight");
    usRight.Length = (USHORT)(wcslen(usRight.Buffer) * sizeof(WCHAR));
    usRight.MaximumLength = usRight.Length + sizeof(WCHAR);
    if (fGrant)
        status = LsaAddAccountRights(hPolicy, sidBuf, &usRight, 1);
    else
        status = LsaRemoveAccountRights(hPolicy, sidBuf, FALSE, &usRight, 1);
    fDone = (status == 0);
    LsaClose(hPolicy);
    return fDone;
}

#define SVC_NAME L"LntSvcStaTestSvc"

static SERVICE_STATUS_HANDLE g_SvcSh;

static void WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    (void)dwCtrl;
}

static void WINAPI SvcMain(DWORD dwArgc, LPWSTR *pszArgv)
{
    SERVICE_STATUS st;

    (void)dwArgc;
    (void)pszArgv;
    WriteReportLine(g_ReportPath, L"svc-main-entered\n");
    g_SvcSh = RegisterServiceCtrlHandlerW(SVC_NAME, SvcCtrlHandler);
    if (!g_SvcSh)
    {
        WriteReportLine(g_ReportPath, L"register-handler-failed\n");
        return;
    }
    memset(&st, 0, sizeof(st));
    st.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    st.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_SvcSh, &st);
    RunServiceChild();
    st.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_SvcSh, &st);
}

/* Fallback for platforms where service-as-user logon is not yet wired up:
   direct token logon plus CreateProcessAsUserW.  The child then runs as a
   plain process rather than a service; SCM cleanup is a no-op for it. */
static HANDLE SpawnChildAsUser(void)
{
    HANDLE hToken = NULL, hProcess = NULL;
    WCHAR szExe[MAX_PATH], szCmd[MAX_PATH * 2];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    if (!LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_BATCH,
                    LOGON32_PROVIDER_DEFAULT, &hToken))
        return NULL;
    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" ServiceWinsta child", szExe);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (CreateProcessAsUserW(hToken, NULL, szCmd, NULL, NULL, FALSE, 0,
                             NULL, NULL, &si, &pi))
    {
        hProcess = pi.hProcess;
        CloseHandle(pi.hThread);
    }
    CloseHandle(hToken);
    return hProcess;
}

/* Real services are spawned by the service control manager: it performs the
   service logon and process creation as SYSTEM, needing no client-side
   privileges.  This reproduces genuine service behavior on Windows, where
   even administrators cannot CreateProcessAsUserW (error 1314). */
static BOOL StartChildService(DWORD *pdwError)
{
    SC_HANDLE hScm, hSvc;
    WCHAR szExe[MAX_PATH], szBin[MAX_PATH * 2];
    BOOL fOk = FALSE;

    *pdwError = ERROR_SUCCESS;
    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!hScm)
    {
        *pdwError = GetLastError();
        return FALSE;
    }
    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szBin, _countof(szBin), L"\"%s\" ServiceWinsta child", szExe);
    hSvc = CreateServiceW(hScm, SVC_NAME, SVC_NAME, SERVICE_ALL_ACCESS,
                          SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                          SERVICE_ERROR_IGNORE, szBin, NULL, NULL, NULL,
                          L".\\" TEST_USER, TEST_PASSWORD);
    if (hSvc)
    {
        fOk = StartServiceW(hSvc, 0, NULL);
        if (!fOk)
        {
            *pdwError = GetLastError();
            if (*pdwError == ERROR_SERVICE_ALREADY_RUNNING)
                fOk = TRUE;
        }
        CloseServiceHandle(hSvc);
    }
    else
    {
        *pdwError = GetLastError();
    }
    CloseServiceHandle(hScm);
    return fOk;
}

static void CleanupChildService(void)
{
    SC_HANDLE hScm, hSvc;
    SERVICE_STATUS st;

    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hScm)
        return;
    hSvc = OpenServiceW(hScm, SVC_NAME, SERVICE_STOP | DELETE);
    if (hSvc)
    {
        ControlService(hSvc, SERVICE_CONTROL_STOP, &st);
        DeleteService(hSvc);
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hScm);
}

START_TEST(ServiceWinsta)
{
    HANDLE hProcessToken;
    SID_IDENTIFIER_AUTHORITY world = { SECURITY_WORLD_SID_AUTHORITY };
    SID_AND_ATTRIBUTES restrictSids[1];
    PSID pEveryone = NULL;
    WCHAR szReport[1024];
    WCHAR szWinstaName[256] = L"";
    LPWSTR pEq;
    char **argv;
    int argc;

    ResolveReportPaths();
    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        SERVICE_TABLE_ENTRYW tbl[] = {
            { (LPWSTR)SVC_NAME, SvcMain },
            { NULL, NULL }
        };
        WriteReportLine(g_ReportPath, L"child-main-entered\n");
        if (!StartServiceCtrlDispatcherW(tbl) &&
            GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
        {
            /* Not started by the service control manager: plain-process
               fallback path, just do the child work directly */
            RunServiceChild();
        }
        return;
    }
    if (!CreateTestUser())
    {
        skip("cannot create test user\n");
        return;
    }

    /* The service logon right is what a service installer would grant */
    SetBatchLogonRight(TRUE);

    CreateSharedDir();
    DeleteReportFiles();
    DWORD scmError = ERROR_SUCCESS;
    if (!StartChildService(&scmError))
    {
        HANDLE hChild = SpawnChildAsUser();
        trace("service start failed %lu, fallback spawn %s\n",
              scmError, hChild ? "ok" : "failed");
        if (!hChild)
        {
            skip("cannot spawn service child, scm error %lu\n", scmError);
            CleanupChildService();
            SetBatchLogonRight(FALSE);
            RemoveTestUser();
            return;
        }
        CloseHandle(hChild);
    }
    /* The child writes its report quickly, then keeps running as a service;
       attack while it is still alive */
    Sleep(5000);
    ReadReport(g_ReportPath, szReport, _countof(szReport));
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

    /* Measured on Windows 11 26H2 (SYSTEM, LocalService and fresh user
       services): service window stations carry NO mandatory integrity label,
       unlike the interactive WinSta0 which is labeled low no-write-up. */
    ok(wcsstr(szReport, L"label S:(ML") == NULL,
       "service window station must not carry a mandatory label, report: %ls\n", szReport);

    /* The DACL of the service window station grants the service account SID
       itself, not the logon session SID (Windows 11 26H2 measurement) */
    {
        LPCWSTR pSid = wcsstr(szReport, L"usersid ");
        LPCWSTR pSddl = wcsstr(szReport, L"fullsddl ");
        BOOL fDaclOk = FALSE;
        if (pSid && pSddl)
        {
            WCHAR szSid[128];
            LPCWSTR pEnd = wcschr(pSid + 8, L'\n');
            SIZE_T cch = pEnd ? (SIZE_T)(pEnd - (pSid + 8)) : 0;
            if (cch && cch < _countof(szSid))
            {
                memcpy(szSid, pSid + 8, cch * sizeof(WCHAR));
                szSid[cch] = UNICODE_NULL;
                fDaclOk = (wcsstr(pSddl, szSid) != NULL);
            }
        }
        ok(fDaclOk, "service window station DACL does not grant the service account SID, report: %ls\n", szReport);
    }

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

    CleanupChildService();
    SetBatchLogonRight(FALSE);
    DeleteReportFiles();
    RemoveTestUser();
}
