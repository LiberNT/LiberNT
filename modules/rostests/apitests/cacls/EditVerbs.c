/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pins the DACL each cacls edit verb produces against Windows 11
 * COPYRIGHT:   Copyright 2026 LiberNT Contributors
 *
 * Windows 11 reference values (explicit part of the DACL, builtin Users = BU):
 *   /E /G Users:R   -> (A;;0x1200a9;;;BU)
 *   /E /R Users     -> the Users entries are gone
 *   /E /D Users     -> (D;;FA;;;BU) only; the allow entry is dropped
 *   /E /P Users:C   -> (A;;0x1301bf;;;BU); the earlier entry is replaced
 *   /G Everyone:R   -> every existing entry replaced by (A;;0x1200a9;;;WD)
 */
#include <apitest.h>
#include <winbase.h>
#include <aclapi.h>
#include <sddl.h>
#include <strsafe.h>

static WCHAR szFile[MAX_PATH];
static WCHAR szCacls[MAX_PATH];

static BOOL RunCaclsInput(LPCWSTR pszArgs, LPCSTR pszInput, DWORD dwWait);

static BOOL RunCacls(LPCWSTR pszArgs, DWORD dwWait)
{
    return RunCaclsInput(pszArgs, NULL, dwWait);
}

/* When pszInput is given, the child inherits a pipe pre-filled with it as
   stdin; the bare /G verb asks for confirmation on stdin. */
static BOOL RunCaclsInput(LPCWSTR pszArgs, LPCSTR pszInput, DWORD dwWait)
{
    WCHAR szCmd[MAX_PATH * 2];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE hPipeRead = NULL, hPipeWrite = NULL;
    BOOL ret;

    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" \"%s\" %s", szCacls, szFile, pszArgs);
    if (pszInput)
    {
        DWORD written;
        SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
        if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0))
            return FALSE;
        WriteFile(hPipeWrite, pszInput, (DWORD)strlen(pszInput), &written, NULL);
        CloseHandle(hPipeWrite);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hPipeRead;
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    ret = CreateProcessW(NULL, szCmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (hPipeRead)
        CloseHandle(hPipeRead);
    if (!ret)
        return FALSE;
    WaitForSingleObject(pi.hProcess, dwWait);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

/* SDDL renders well-known SIDs as two-letter aliases; pin the same form */
static LPCWSTR WellKnownSidAlias(LPCWSTR pszNumeric)
{
    static const struct { LPCWSTR Numeric; LPCWSTR Alias; } Map[] =
    {
        { L"S-1-1-0", L"WD" },      /* Everyone */
        { L"S-1-5-32-545", L"BU" }, /* Builtin Users */
        { L"S-1-5-32-544", L"BA" }, /* Builtin Administrators */
        { L"S-1-5-18", L"SY" },     /* Local System */
        { L"S-1-5-11", L"AU" },     /* Authenticated Users */
        { L"S-1-3-0", L"CO" },      /* Creator Owner */
    };
    SIZE_T i;
    for (i = 0; i < _countof(Map); i++)
    {
        if (!wcscmp(pszNumeric, Map[i].Numeric))
            return Map[i].Alias;
    }
    return pszNumeric;
}

/* Explicit (non-inherited) DACL part as an SDDL fragment */
static void GetExplicitDaclSddl(LPWSTR psz, SIZE_T cch)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    PACL pDacl = NULL;
    BOOL present = FALSE, defaulted = FALSE;
    DWORD i;

    psz[0] = UNICODE_NULL;
    if (GetNamedSecurityInfoW(szFile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              NULL, NULL, &pDacl, NULL, &pSD) != ERROR_SUCCESS ||
        !GetSecurityDescriptorDacl(pSD, &present, &pDacl, &defaulted) || !present)
    {
        StringCchCopyW(psz, cch, L"(no dacl)");
        if (pSD)
            LocalFree(pSD);
        return;
    }

    for (i = 0; i < pDacl->AceCount; i++)
    {
        ACCESS_ALLOWED_ACE *pAce;
        LPWSTR pszSid = NULL;
        WCHAR szEntry[160];
        LPCWSTR pszType;

        if (!GetAce(pDacl, i, (LPVOID *)&pAce))
            continue;
        if (pAce->Header.AceFlags & INHERITED_ACE)
            continue;
        if (!ConvertSidToStringSidW((PSID)&pAce->SidStart, &pszSid))
            continue;
        if (pAce->Header.AceType == ACCESS_ALLOWED_ACE_TYPE)
            pszType = L"A";
        else if (pAce->Header.AceType == ACCESS_DENIED_ACE_TYPE)
            pszType = L"D";
        else
            pszType = L"?";
        StringCchPrintfW(szEntry, _countof(szEntry), L"(%ls;;0x%lx;;;%ls)",
                         pszType, (unsigned long)pAce->Mask, WellKnownSidAlias(pszSid));
        /* FA is how SDDL renders FILE_ALL_ACCESS; match the Windows format */
        {
            WCHAR szMask[16];
            StringCchPrintfW(szMask, _countof(szMask), L"0x%lx", (unsigned long)pAce->Mask);
            if (pAce->Mask == FILE_ALL_ACCESS)
                StringCchPrintfW(szEntry, _countof(szEntry), L"(%ls;;FA;;;%ls)", pszType, WellKnownSidAlias(pszSid));
        }
        StringCchCatW(psz, cch, szEntry);
        LocalFree(pszSid);
    }
    if (!psz[0])
        StringCchCopyW(psz, cch, L"(empty)");
    LocalFree(pSD);
}

static void ReadBackAndReport(const char *pszStep, LPWSTR pszOut, SIZE_T cch)
{
    GetExplicitDaclSddl(pszOut, cch);
    trace("%s: %ls\n", pszStep, pszOut);
}

START_TEST(EditVerbs)
{
    WCHAR szTemp[MAX_PATH];
    WCHAR szSddl[1024];
    HANDLE hFile;
    DWORD written;

    if (!GetTempPathW(_countof(szTemp), szTemp))
    {
        skip("no temp path\n");
        return;
    }
    if (!GetTempFileNameW(szTemp, L"acl", 0, szFile))
    {
        skip("no temp file name\n");
        return;
    }
    hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        skip("cannot create temp file (%lu)\n", GetLastError());
        return;
    }
    WriteFile(hFile, "x", 1, &written, NULL);
    CloseHandle(hFile);

    if (!GetModuleFileNameW(NULL, szCacls, _countof(szCacls)))
    {
        skip("no module path\n");
        return;
    }
    /* cacls.exe sits next to the test binary in the rostests layout */
    {
        LPWSTR pSlash = wcsrchr(szCacls, L'\\');
        if (pSlash)
            StringCchCopyW(pSlash + 1, _countof(szCacls) - (pSlash + 1 - szCacls), L"cacls.exe");
    }
    if (GetFileAttributesW(szCacls) == INVALID_FILE_ATTRIBUTES)
    {
        skip("cacls.exe not found next to the test binary\n");
        return;
    }

    /* /E /G Users:R -> (A;;0x1200a9;;;BU) present */
    ok(RunCacls(L"/E /G Users:R", 15000), "spawn /G failed\n");
    ReadBackAndReport("after /E /G Users:R", szSddl, _countof(szSddl));
    ok(wcsstr(szSddl, L"(A;;0x1200a9;;;BU)") != NULL,
       "grant read missing: %ls\n", szSddl);

    /* /E /R Users -> Users entries gone */
    ok(RunCacls(L"/E /R Users", 15000), "spawn /R failed\n");
    ReadBackAndReport("after /E /R Users", szSddl, _countof(szSddl));
    ok(wcsstr(szSddl, L";;;BU)") == NULL,
       "revoke left Users entries: %ls\n", szSddl);

    /* /E /D Users -> only (D;;FA;;;BU) */
    ok(RunCacls(L"/E /D Users", 15000), "spawn /D failed\n");
    ReadBackAndReport("after /E /D Users", szSddl, _countof(szSddl));
    ok(!wcscmp(szSddl, L"(D;;FA;;;BU)"),
       "deny result is not the lone deny entry: %ls\n", szSddl);
    ok(wcsstr(szSddl, L"(A;;") == NULL,
       "deny kept an allow entry: %ls\n", szSddl);

    /* /E /P Users:C replaces the earlier entry with (A;;0x1301bf;;;BU).
       Done before the deny sequence would matter: on a file whose DACL the
       caller can still write, replacement semantics are what is pinned. */
    hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "recreate temp file failed %lu\n", GetLastError());
    if (hFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(hFile, "x", 1, &written, NULL);
        CloseHandle(hFile);
    }
    ok(RunCacls(L"/E /G Users:R", 15000), "spawn pre-/P grant failed\n");
    ok(RunCacls(L"/E /P Users:C", 15000), "spawn /P failed\n");
    ReadBackAndReport("after /E /P Users:C", szSddl, _countof(szSddl));
    ok(wcsstr(szSddl, L"(A;;0x1301bf;;;BU)") != NULL,
       "replace change missing: %ls\n", szSddl);
    ok(wcsstr(szSddl, L"(A;;0x1200a9;;;BU)") == NULL,
       "replace kept the old grant: %ls\n", szSddl);

    /* bare /G Everyone:R asks for confirmation and then replaces the whole
       explicit DACL with the single Everyone read entry */
    ok(RunCaclsInput(L"/G Everyone:R", "Y\r\n", 15000), "spawn bare /G failed\n");
    ReadBackAndReport("after /G Everyone:R", szSddl, _countof(szSddl));
    ok(wcsstr(szSddl, L"(A;;0x1200a9;;;WD)") != NULL,
       "bare grant result missing Everyone read: %ls\n", szSddl);
    ok(wcsstr(szSddl, L";;;BU)") == NULL,
       "bare grant did not replace the existing entries: %ls\n", szSddl);

    DeleteFileW(szFile);
}
