/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RegCreateKeyTransactedA/W
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifndef ERROR_INVALID_TRANSACTION
#define ERROR_INVALID_TRANSACTION 6700
#endif

typedef LONG (WINAPI *FN_RegCreateKeyTransactedW)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM,
                                                  LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD, HANDLE, PVOID);
typedef LONG (WINAPI *FN_RegCreateKeyTransactedA)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM,
                                                  LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD, HANDLE, PVOID);
typedef HANDLE (WINAPI *FN_CreateTransaction)(LPSECURITY_ATTRIBUTES, LPGUID, DWORD, DWORD, DWORD, DWORD, LPWSTR);
typedef BOOL (WINAPI *FN_EndTransaction)(HANDLE);

static FN_RegCreateKeyTransactedW pRegCreateKeyTransactedW;
static FN_RegCreateKeyTransactedA pRegCreateKeyTransactedA;
static FN_CreateTransaction pCreateTransaction;
static FN_EndTransaction pCommitTransaction;
static FN_EndTransaction pRollbackTransaction;

#define TEST_ROOT L"Software\\LiberNTTxCreateTest"

static LONG
OpenPlain(PCWSTR SubKey)
{
    HKEY hKey;
    LONG Error = RegOpenKeyExW(HKEY_CURRENT_USER, SubKey, 0, KEY_READ, &hKey);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(hKey);
    return Error;
}

static void
Cleanup(void)
{
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_ROOT L"\\A\\B\\C");
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_ROOT L"\\A\\B");
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_ROOT L"\\A");
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_ROOT L"\\Committed");
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_ROOT L"\\Ansi");
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_ROOT);
}

static HANDLE
NewTransaction(void)
{
    HANDLE hTransaction = pCreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);
    ok(hTransaction != INVALID_HANDLE_VALUE, "CreateTransaction failed, error %lu\n", GetLastError());
    return hTransaction;
}

static void
Test_Parameters(HANDLE hTransaction)
{
    HKEY hKey;
    DWORD Disposition;
    LONG Error;

    hKey = (HKEY)1;
    Disposition = 0xdead;
    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT, 0, NULL, 0, KEY_READ, NULL, &hKey, &Disposition, NULL, NULL);
    ok(Error == ERROR_INVALID_TRANSACTION, "NULL transaction: error %ld\n", Error);
    ok(hKey == (HKEY)1, "NULL transaction: key %p\n", hKey);
    ok(Disposition == 0xdead, "NULL transaction: disposition %lu\n", Disposition);

    hKey = (HKEY)1;
    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT, 0, NULL, 0, KEY_READ, NULL, &hKey, NULL, INVALID_HANDLE_VALUE, NULL);
    ok(Error == ERROR_INVALID_TRANSACTION, "invalid transaction: error %ld\n", Error);
    ok(hKey == (HKEY)1, "invalid transaction: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT, 0, NULL, 0, KEY_READ, NULL, &hKey, NULL, hTransaction, (PVOID)1);
    ok(Error == ERROR_INVALID_PARAMETER, "extended parameter: error %ld\n", Error);
    ok(hKey == (HKEY)1, "extended parameter: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT, 1, NULL, 0, KEY_READ, NULL, &hKey, NULL, hTransaction, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "nonzero Reserved: error %ld\n", Error);
    ok(hKey == (HKEY)1, "nonzero Reserved: key %p\n", hKey);

    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT, 0, NULL, 0, KEY_READ, NULL, NULL, NULL, hTransaction, NULL);
    ok(Error == ERROR_BADKEY, "NULL result: error %ld\n", Error);
}

static void
Test_Rollback(void)
{
    HANDLE hTransaction = NewTransaction();
    HKEY hKey;
    DWORD Disposition;
    LONG Error;

    if (hTransaction == INVALID_HANDLE_VALUE)
        return;

    Disposition = 0;
    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT L"\\A\\B\\C", 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                                     &hKey, &Disposition, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "nested create: error %ld\n", Error);
    ok(Disposition == REG_CREATED_NEW_KEY, "nested create: disposition %lu\n", Disposition);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(hKey);

    ok(OpenPlain(TEST_ROOT L"\\A\\B\\C") == ERROR_FILE_NOT_FOUND, "the new key must not be visible outside the transaction\n");

    Disposition = 0;
    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT L"\\A\\B\\C", 0, NULL, 0, KEY_READ, NULL,
                                     &hKey, &Disposition, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "second create: error %ld\n", Error);
    ok(Disposition == REG_OPENED_EXISTING_KEY, "second create: disposition %lu\n", Disposition);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(hKey);

    ok(pRollbackTransaction(hTransaction), "RollbackTransaction failed, error %lu\n", GetLastError());
    CloseHandle(hTransaction);

    ok(OpenPlain(TEST_ROOT L"\\A\\B\\C") == ERROR_FILE_NOT_FOUND, "the key must be gone after rollback\n");
    ok(OpenPlain(TEST_ROOT L"\\A") == ERROR_FILE_NOT_FOUND, "intermediate keys must be gone after rollback\n");
}

static void
Test_Commit(void)
{
    HANDLE hTransaction = NewTransaction();
    HKEY hKey;
    DWORD Disposition;
    LONG Error;

    if (hTransaction == INVALID_HANDLE_VALUE)
        return;

    Disposition = 0;
    Error = pRegCreateKeyTransactedW(HKEY_CURRENT_USER, TEST_ROOT L"\\Committed", 0, NULL, 0, KEY_READ, NULL,
                                     &hKey, &Disposition, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "create: error %ld\n", Error);
    ok(Disposition == REG_CREATED_NEW_KEY, "create: disposition %lu\n", Disposition);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(hKey);

    if (pRegCreateKeyTransactedA)
    {
        Error = pRegCreateKeyTransactedA(HKEY_CURRENT_USER, "Software\\LiberNTTxCreateTest\\Ansi", 0, NULL, 0, KEY_READ, NULL,
                                         &hKey, NULL, hTransaction, NULL);
        ok(Error == ERROR_SUCCESS, "ANSI create: error %ld\n", Error);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(hKey);
    }

    ok(pCommitTransaction(hTransaction), "CommitTransaction failed, error %lu\n", GetLastError());
    CloseHandle(hTransaction);

    ok(OpenPlain(TEST_ROOT L"\\Committed") == ERROR_SUCCESS, "the key must exist after commit\n");
    if (pRegCreateKeyTransactedA)
        ok(OpenPlain(TEST_ROOT L"\\Ansi") == ERROR_SUCCESS, "the ANSI key must exist after commit\n");
}

START_TEST(RegCreateKeyTransacted)
{
    HMODULE hAdvapi32 = GetModuleHandleW(L"advapi32.dll");
    HMODULE hKtm = LoadLibraryW(L"ktmw32.dll");
    HANDLE hTransaction;

    pRegCreateKeyTransactedW = (FN_RegCreateKeyTransactedW)GetProcAddress(hAdvapi32, "RegCreateKeyTransactedW");
    pRegCreateKeyTransactedA = (FN_RegCreateKeyTransactedA)GetProcAddress(hAdvapi32, "RegCreateKeyTransactedA");
    ok(pRegCreateKeyTransactedW != NULL, "advapi32!RegCreateKeyTransactedW is missing\n");
    ok(pRegCreateKeyTransactedA != NULL, "advapi32!RegCreateKeyTransactedA is missing\n");
    ok(hKtm != NULL, "ktmw32.dll failed to load, error %lu\n", GetLastError());
    if (!pRegCreateKeyTransactedW || !hKtm)
        return;

    pCreateTransaction = (FN_CreateTransaction)GetProcAddress(hKtm, "CreateTransaction");
    pCommitTransaction = (FN_EndTransaction)GetProcAddress(hKtm, "CommitTransaction");
    pRollbackTransaction = (FN_EndTransaction)GetProcAddress(hKtm, "RollbackTransaction");
    ok(pCreateTransaction && pCommitTransaction && pRollbackTransaction, "ktmw32 transaction functions are missing\n");
    if (!pCreateTransaction || !pCommitTransaction || !pRollbackTransaction)
        return;

    Cleanup();

    hTransaction = NewTransaction();
    if (hTransaction != INVALID_HANDLE_VALUE)
    {
        Test_Parameters(hTransaction);
        pRollbackTransaction(hTransaction);
        CloseHandle(hTransaction);
    }

    Test_Rollback();
    Test_Commit();
    Cleanup();
}
