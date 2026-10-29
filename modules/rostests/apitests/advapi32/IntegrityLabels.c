/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for mandatory labels, the token default DACL and restricted impersonation
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winbase.h>
#include <winuser.h>
#include <aclapi.h>
#include <sddl.h>

static void GetLabelSddl(HANDLE hObject, SE_OBJECT_TYPE type, LPCWSTR pszName, LPWSTR psz, SIZE_T cch)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    LPWSTR pszSddl = NULL;
    DWORD error;

    psz[0] = UNICODE_NULL;
    if (pszName)
        error = GetNamedSecurityInfoW((LPWSTR)pszName, type, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
    else
        error = GetSecurityInfo(hObject, type, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
    if (error)
    {
        StringCchPrintfW(psz, cch, L"error %lu", error);
        return;
    }
    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(pSD, SDDL_REVISION_1, LABEL_SECURITY_INFORMATION,
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

START_TEST(IntegrityLabels)
{
    WCHAR sz[256], szPath[MAX_PATH], szFile[MAX_PATH];
    PSECURITY_DESCRIPTOR pLow = NULL;
    HANDLE hFile;
    BOOL ret;

    GetLabelSddl(GetProcessWindowStation(), SE_WINDOW_OBJECT, NULL, sz, _countof(sz));
    ok(!wcscmp(sz, L"S:(ML;;NW;;;LW)"), "WinSta0 label is %ls\n", sz);
    GetLabelSddl(GetThreadDesktop(GetCurrentThreadId()), SE_WINDOW_OBJECT, NULL, sz, _countof(sz));
    ok(!wcscmp(sz, L"S:(ML;;NW;;;LW)"), "desktop label is %ls\n", sz);

    if (GetEnvironmentVariableW(L"USERPROFILE", szPath, _countof(szPath)))
    {
        StringCchCatW(szPath, _countof(szPath), L"\\AppData\\LocalLow");
        ok(GetFileAttributesW(szPath) != INVALID_FILE_ATTRIBUTES, "%ls is missing\n", szPath);
        GetLabelSddl(NULL, SE_FILE_OBJECT, szPath, sz, _countof(sz));
        ok(!wcscmp(sz, L"S:AI(ML;OICI;NW;;;LW)"), "LocalLow label is %ls\n", sz);
    }

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"S:(ML;;NW;;;LW)", SDDL_REVISION_1, &pLow, NULL),
       "cannot build the label\n");
    GetTempPathW(_countof(szPath), szPath);
    GetTempFileNameW(szPath, L"lbl", 0, szFile);

    hFile = CreateFileW(szFile, WRITE_DAC, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "open failed %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = SetKernelObjectSecurity(hFile, LABEL_SECURITY_INFORMATION, pLow);
    ok(!ret && GetLastError() == ERROR_ACCESS_DENIED, "WRITE_DAC handle: ret %d error %lu\n", ret, GetLastError());
    CloseHandle(hFile);

    hFile = CreateFileW(szFile, WRITE_OWNER, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "open failed %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = SetKernelObjectSecurity(hFile, LABEL_SECURITY_INFORMATION, pLow);
    ok(ret, "WRITE_OWNER handle: error %lu\n", GetLastError());
    CloseHandle(hFile);

    DeleteFileW(szFile);
    GetTempFileNameW(szPath, L"lbl", 0, szFile);
    ret = SetFileSecurityW(szFile, LABEL_SECURITY_INFORMATION, pLow);
    ok(ret, "SetFileSecurityW error %lu\n", GetLastError());
    GetLabelSddl(NULL, SE_FILE_OBJECT, szFile, sz, _countof(sz));
    ok(!wcscmp(sz, L"S:(ML;;NW;;;LW)"), "file label is %ls\n", sz);
    DeleteFileW(szFile);

    GetTempFileNameW(szPath, L"lbl", 0, szFile);
    {
        PACL pSacl = NULL;
        BOOL fPresent = FALSE, fDefaulted = FALSE;
        DWORD error;
        GetSecurityDescriptorSacl(pLow, &fPresent, &pSacl, &fDefaulted);
        error = SetNamedSecurityInfoW(szFile, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, pSacl);
        ok(error == ERROR_SUCCESS, "SetNamedSecurityInfoW error %lu\n", error);
    }
    GetLabelSddl(NULL, SE_FILE_OBJECT, szFile, sz, _countof(sz));
    ok(!wcscmp(sz, L"S:AI(ML;;NW;;;LW)"), "label set through SetNamedSecurityInfoW is %ls\n", sz);
    DeleteFileW(szFile);
    LocalFree(pLow);
}

static PSID GetLogonSid(HANDLE hToken)
{
    TOKEN_GROUPS *pGroups;
    DWORD cb = 0, i;
    PSID pSid = NULL;

    GetTokenInformation(hToken, TokenGroups, NULL, 0, &cb);
    pGroups = HeapAlloc(GetProcessHeap(), 0, cb);
    if (!pGroups)
        return NULL;
    if (GetTokenInformation(hToken, TokenGroups, pGroups, cb, &cb))
    {
        for (i = 0; i < pGroups->GroupCount; i++)
        {
            if ((pGroups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            {
                DWORD cbSid = GetLengthSid(pGroups->Groups[i].Sid);
                pSid = HeapAlloc(GetProcessHeap(), 0, cbSid);
                if (pSid)
                    CopySid(cbSid, pSid, pGroups->Groups[i].Sid);
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, pGroups);
    return pSid;
}

START_TEST(TokenDefaultDacl)
{
    HANDLE hToken;
    TOKEN_DEFAULT_DACL *pDacl;
    DWORD cb = 0, i;
    PSID pLogonSid;
    BOOL fLogon = FALSE, fSystem = FALSE;
    ACCESS_MASK logonMask = 0;

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken), "OpenProcessToken failed\n");
    pLogonSid = GetLogonSid(hToken);
    ok(pLogonSid != NULL, "no logon SID\n");
    GetTokenInformation(hToken, TokenDefaultDacl, NULL, 0, &cb);
    pDacl = HeapAlloc(GetProcessHeap(), 0, cb);
    if (pDacl && GetTokenInformation(hToken, TokenDefaultDacl, pDacl, cb, &cb) && pDacl->DefaultDacl)
    {
        for (i = 0; i < pDacl->DefaultDacl->AceCount; i++)
        {
            ACCESS_ALLOWED_ACE *pAce;
            LPWSTR pszSid = NULL;
            if (!GetAce(pDacl->DefaultDacl, i, (void **)&pAce) || pAce->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
                continue;
            ConvertSidToStringSidW((PSID)&pAce->SidStart, &pszSid);
            trace("default DACL ACE %lu: %ls mask 0x%lx\n", i, pszSid, pAce->Mask);
            if (pszSid && !wcscmp(pszSid, L"S-1-5-18"))
                fSystem = TRUE;
            if (pLogonSid && EqualSid((PSID)&pAce->SidStart, pLogonSid))
            {
                fLogon = TRUE;
                logonMask = pAce->Mask;
            }
            LocalFree(pszSid);
        }
    }
    ok(fSystem, "no SYSTEM ACE\n");
    ok(fLogon, "no logon SID ACE\n");
    ok_long(logonMask, GENERIC_READ | GENERIC_EXECUTE);
    HeapFree(GetProcessHeap(), 0, pDacl);
    HeapFree(GetProcessHeap(), 0, pLogonSid);
    CloseHandle(hToken);
}

static void CheckImpersonation(const char *pszProcess, HANDLE hImpersonation, const char *pszToken, SECURITY_IMPERSONATION_LEVEL expected)
{
    HANDLE hThreadToken = NULL;
    SECURITY_IMPERSONATION_LEVEL level = 0xdead;
    DWORD cb;
    BOOL ret;

    ret = SetThreadToken(NULL, hImpersonation);
    ok(ret, "%s/%s: SetThreadToken failed %lu\n", pszProcess, pszToken, GetLastError());
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hThreadToken))
    {
        GetTokenInformation(hThreadToken, TokenImpersonationLevel, &level, sizeof(level), &cb);
        CloseHandle(hThreadToken);
    }
    else
    {
        ok(0, "%s/%s: OpenThreadToken failed %lu\n", pszProcess, pszToken, GetLastError());
    }
    ok(level == expected, "%s process, %s token: level %d, expected %d\n", pszProcess, pszToken, level, expected);
    RevertToSelf();
}

static HANDLE MakeImpersonationToken(HANDLE hSource)
{
    HANDLE hToken = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    DuplicateTokenEx(hSource, TOKEN_ALL_ACCESS, &sa, SecurityImpersonation, TokenImpersonation, &hToken);
    return hToken;
}

START_TEST(ImpersonateRestricted)
{
    HANDLE hProcessToken, hRestricted = NULL, hImpFull, hImpRestricted;
    SID_IDENTIFIER_AUTHORITY world = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
    SID_AND_ATTRIBUTES restrictSids[4];
    PSID pEveryone = NULL, pUsers = NULL, pAuthenticated = NULL, pLogonSid;
    WCHAR szCmd[MAX_PATH * 2], szExe[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD dwExit = 0xdeadbeef;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 4)
    {
        HANDLE hFull = (HANDLE)(ULONG_PTR)strtoul(argv[2], NULL, 16);
        HANDLE hRestrictedImp = (HANDLE)(ULONG_PTR)strtoul(argv[3], NULL, 16);
        CheckImpersonation("restricted", hFull, "unrestricted", SecurityIdentification);
        CheckImpersonation("restricted", hRestrictedImp, "restricted", SecurityImpersonation);
        return;
    }

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hProcessToken), "OpenProcessToken failed\n");
    AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &pEveryone);
    AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &pUsers);
    AllocateAndInitializeSid(&nt, 1, SECURITY_AUTHENTICATED_USER_RID, 0, 0, 0, 0, 0, 0, 0, &pAuthenticated);
    pLogonSid = GetLogonSid(hProcessToken);
    ok(pLogonSid != NULL, "no logon SID\n");
    restrictSids[0].Sid = pEveryone;
    restrictSids[1].Sid = pUsers;
    restrictSids[2].Sid = pAuthenticated;
    restrictSids[3].Sid = pLogonSid;
    restrictSids[0].Attributes = restrictSids[1].Attributes = restrictSids[2].Attributes = restrictSids[3].Attributes = 0;
    ok(CreateRestrictedToken(hProcessToken, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, pLogonSid ? 4 : 3, restrictSids, &hRestricted),
       "CreateRestrictedToken failed %lu\n", GetLastError());

    hImpFull = MakeImpersonationToken(hProcessToken);
    hImpRestricted = MakeImpersonationToken(hRestricted);
    CheckImpersonation("unrestricted", hImpFull, "unrestricted", SecurityImpersonation);
    CheckImpersonation("unrestricted", hImpRestricted, "restricted", SecurityImpersonation);

    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" ImpersonateRestricted %p %p", szExe, hImpFull, hImpRestricted);
    if (CreateProcessAsUserW(hRestricted, NULL, szCmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 30000);
        GetExitCodeProcess(pi.hProcess, &dwExit);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        ok_long(dwExit, 0);
    }
    else
    {
        ok(0, "CreateProcessAsUserW failed %lu\n", GetLastError());
    }

    CloseHandle(hImpFull);
    CloseHandle(hImpRestricted);
    CloseHandle(hRestricted);
    CloseHandle(hProcessToken);
    HeapFree(GetProcessHeap(), 0, pLogonSid);
    FreeSid(pAuthenticated);
    FreeSid(pUsers);
    FreeSid(pEveryone);
}
