/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RegOpenKeyTransactedA/W
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifndef ERROR_INVALID_TRANSACTION
#define ERROR_INVALID_TRANSACTION 6700
#endif

typedef LONG (WINAPI *FN_RegOpenKeyTransactedW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY, HANDLE, PVOID);
typedef LONG (WINAPI *FN_RegOpenKeyTransactedA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY, HANDLE, PVOID);
typedef HANDLE (WINAPI *FN_CreateTransaction)(LPSECURITY_ATTRIBUTES, LPGUID, DWORD, DWORD, DWORD, DWORD, LPWSTR);
typedef BOOL (WINAPI *FN_RollbackTransaction)(HANDLE);
typedef NTSTATUS (NTAPI *FN_NtOpenKeyTransacted)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, HANDLE);

static FN_RegOpenKeyTransactedW pRegOpenKeyTransactedW;
static FN_RegOpenKeyTransactedA pRegOpenKeyTransactedA;
static FN_CreateTransaction pCreateTransaction;
static FN_RollbackTransaction pRollbackTransaction;
static FN_NtOpenKeyTransacted pNtOpenKeyTransacted;

static void
Test_Parameters(HANDLE hTransaction)
{
    HKEY hKey;
    LONG Error;

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &hKey, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "open: error %ld\n", Error);
    ok(hKey != NULL && hKey != (HKEY)1, "open: key %p\n", hKey);
    if (Error == ERROR_SUCCESS)
    {
        ok(RegQueryInfoKeyW(hKey, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS,
           "the opened key must be usable\n");
        RegCloseKey(hKey);
    }

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\LiberNTNoSuchKeyVx", 0, KEY_READ, &hKey, hTransaction, NULL);
    ok(Error == ERROR_FILE_NOT_FOUND, "missing key: error %ld\n", Error);
    ok(hKey == NULL, "missing key: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &hKey, NULL, NULL);
    ok(Error == ERROR_INVALID_TRANSACTION, "NULL transaction: error %ld\n", Error);
    ok(hKey == (HKEY)1, "NULL transaction: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &hKey, INVALID_HANDLE_VALUE, NULL);
    ok(Error == ERROR_INVALID_TRANSACTION, "invalid transaction: error %ld\n", Error);
    ok(hKey == (HKEY)1, "invalid transaction: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &hKey, hTransaction, (PVOID)1);
    ok(Error == ERROR_INVALID_PARAMETER, "extended parameter: error %ld\n", Error);
    ok(hKey == (HKEY)1, "extended parameter: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, NULL, 0, KEY_READ, &hKey, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "NULL subkey: error %ld\n", Error);
    ok(hKey == HKEY_LOCAL_MACHINE, "NULL subkey: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"", 0, KEY_READ, &hKey, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "empty subkey: error %ld\n", Error);
    ok(hKey == HKEY_LOCAL_MACHINE, "empty subkey: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, NULL, 0, KEY_READ, &hKey, NULL, NULL);
    ok(Error == ERROR_INVALID_TRANSACTION, "NULL subkey and NULL transaction: error %ld\n", Error);
    ok(hKey == (HKEY)1, "NULL subkey and NULL transaction: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW((HKEY)(ULONG_PTR)0xdead0, L"SOFTWARE", 0, KEY_READ, &hKey, hTransaction, NULL);
    ok(Error == ERROR_INVALID_HANDLE, "invalid parent key: error %ld\n", Error);
    ok(hKey == NULL, "invalid parent key: key %p\n", hKey);

    hKey = (HKEY)1;
    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0x100, KEY_READ, &hKey, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "unknown option: error %ld\n", Error);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(hKey);

    Error = pRegOpenKeyTransactedW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, NULL, hTransaction, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL result: error %ld\n", Error);

    if (pRegOpenKeyTransactedA)
    {
        hKey = (HKEY)1;
        Error = pRegOpenKeyTransactedA(HKEY_LOCAL_MACHINE, "SOFTWARE", 0, KEY_READ, &hKey, hTransaction, NULL);
        ok(Error == ERROR_SUCCESS, "ANSI open: error %ld\n", Error);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(hKey);

        hKey = (HKEY)1;
        Error = pRegOpenKeyTransactedA(HKEY_LOCAL_MACHINE, "SOFTWARE\\LiberNTNoSuchKeyVx", 0, KEY_READ, &hKey, hTransaction, NULL);
        ok(Error == ERROR_FILE_NOT_FOUND, "ANSI missing key: error %ld\n", Error);
        ok(hKey == NULL, "ANSI missing key: key %p\n", hKey);
    }
}

static void
Test_NtStatus(void)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE hKey;
    NTSTATUS Status;

    if (!pNtOpenKeyTransacted)
    {
        skip("ntdll!NtOpenKeyTransacted is missing\n");
        return;
    }

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    hKey = (HANDLE)1;
    Status = pNtOpenKeyTransacted(&hKey, KEY_READ, &ObjectAttributes, NULL);
    ok(Status == STATUS_INVALID_HANDLE, "NULL transaction: status 0x%08lx\n", Status);
    ok(hKey == (HANDLE)1, "NULL transaction: handle %p\n", hKey);

    hKey = (HANDLE)1;
    Status = pNtOpenKeyTransacted(&hKey, KEY_READ, &ObjectAttributes, INVALID_HANDLE_VALUE);
    ok(Status == STATUS_OBJECT_TYPE_MISMATCH, "invalid transaction: status 0x%08lx\n", Status);
    ok(hKey == (HANDLE)1, "invalid transaction: handle %p\n", hKey);
}

static void
Test_Isolation(HANDLE hTransaction)
{
    HKEY hParent, hTxParent, hChild;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LiberNTTxOpenTest", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hParent, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW: error %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    Error = pRegOpenKeyTransactedW(HKEY_CURRENT_USER, L"Software\\LiberNTTxOpenTest", 0, KEY_ALL_ACCESS, &hTxParent, hTransaction, NULL);
    ok(Error == ERROR_SUCCESS, "transacted open: error %ld\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegCreateKeyExW(hTxParent, L"Child", 0, NULL, 0, KEY_READ, NULL, &hChild, NULL);
        ok(Error == ERROR_SUCCESS, "create through the transacted key: error %ld\n", Error);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(hChild);

        Error = RegOpenKeyExW(hParent, L"Child", 0, KEY_READ, &hChild);
        ok(Error == ERROR_FILE_NOT_FOUND, "child outside the transaction: error %ld\n", Error);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(hChild);

        Error = RegOpenKeyExW(hTxParent, L"Child", 0, KEY_READ, &hChild);
        ok(Error == ERROR_SUCCESS, "child inside the transaction: error %ld\n", Error);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(hChild);

        RegCloseKey(hTxParent);
    }

    ok(pRollbackTransaction(hTransaction), "RollbackTransaction failed, error %lu\n", GetLastError());

    Error = RegOpenKeyExW(hParent, L"Child", 0, KEY_READ, &hChild);
    ok(Error == ERROR_FILE_NOT_FOUND, "child after rollback: error %ld\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        RegCloseKey(hChild);
        RegDeleteKeyW(hParent, L"Child");
    }

    RegCloseKey(hParent);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\LiberNTTxOpenTest");
}

START_TEST(RegOpenKeyTransacted)
{
    HMODULE hAdvapi32 = GetModuleHandleW(L"advapi32.dll");
    HMODULE hKtm = LoadLibraryW(L"ktmw32.dll");
    HANDLE hTransaction;

    pRegOpenKeyTransactedW = (FN_RegOpenKeyTransactedW)GetProcAddress(hAdvapi32, "RegOpenKeyTransactedW");
    pRegOpenKeyTransactedA = (FN_RegOpenKeyTransactedA)GetProcAddress(hAdvapi32, "RegOpenKeyTransactedA");
    pNtOpenKeyTransacted = (FN_NtOpenKeyTransacted)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtOpenKeyTransacted");
    ok(pRegOpenKeyTransactedW != NULL, "advapi32!RegOpenKeyTransactedW is missing\n");
    ok(pRegOpenKeyTransactedA != NULL, "advapi32!RegOpenKeyTransactedA is missing\n");
    ok(hKtm != NULL, "ktmw32.dll failed to load, error %lu\n", GetLastError());
    if (!pRegOpenKeyTransactedW || !hKtm)
        return;

    pCreateTransaction = (FN_CreateTransaction)GetProcAddress(hKtm, "CreateTransaction");
    pRollbackTransaction = (FN_RollbackTransaction)GetProcAddress(hKtm, "RollbackTransaction");
    ok(pCreateTransaction && pRollbackTransaction, "ktmw32 transaction functions are missing\n");
    if (!pCreateTransaction || !pRollbackTransaction)
        return;

    Test_NtStatus();

    hTransaction = pCreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);
    ok(hTransaction != INVALID_HANDLE_VALUE, "CreateTransaction failed, error %lu\n", GetLastError());
    if (hTransaction == INVALID_HANDLE_VALUE)
        return;
    Test_Parameters(hTransaction);
    CloseHandle(hTransaction);

    hTransaction = pCreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);
    ok(hTransaction != INVALID_HANDLE_VALUE, "CreateTransaction failed, error %lu\n", GetLastError());
    if (hTransaction == INVALID_HANDLE_VALUE)
        return;
    Test_Isolation(hTransaction);
    CloseHandle(hTransaction);
}
