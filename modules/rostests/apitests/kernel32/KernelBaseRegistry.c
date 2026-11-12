/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RegRestoreKeyW, RegOpenUserClassesRoot, SHRegCreateUSKeyW and RegLoadMUIStringA
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <shlwapi.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/sefuncs.h>
#include <ndk/setypes.h>

#define TEST_ROOT L"Software\\LiberNT-apitest-KernelBaseRegistry"
#define TEST_CLASS L"LiberNT.UserClassesTest"

typedef LSTATUS (WINAPI *PFN_REGRESTOREKEYW)(HKEY, LPCWSTR, DWORD);
typedef LSTATUS (WINAPI *PFN_REGOPENUSERCLASSESROOT)(HANDLE, DWORD, REGSAM, PHKEY);
typedef LSTATUS (WINAPI *PFN_SHREGCREATEUSKEYW)(LPCWSTR, REGSAM, HUSKEY, PHUSKEY, DWORD);
typedef LSTATUS (WINAPI *PFN_SHREGCLOSEUSKEY)(HUSKEY);
typedef LSTATUS (WINAPI *PFN_SHREGWRITEUSVALUEW)(HUSKEY, LPCWSTR, DWORD, const void *, DWORD, DWORD);
typedef LSTATUS (WINAPI *PFN_REGLOADMUISTRINGA)(HKEY, LPCSTR, LPSTR, DWORD, LPDWORD, DWORD, LPCSTR);

static PFN_REGRESTOREKEYW pRegRestoreKeyW;
static PFN_REGOPENUSERCLASSESROOT pRegOpenUserClassesRoot;
static PFN_SHREGCREATEUSKEYW pSHRegCreateUSKeyW;
static PFN_SHREGCLOSEUSKEY pSHRegCloseUSKey;
static PFN_SHREGWRITEUSVALUEW pSHRegWriteUSValueW;
static PFN_REGLOADMUISTRINGA pRegLoadMUIStringA;

static
VOID
MakeTempPath(
    _Out_writes_(MAX_PATH) PWSTR Path,
    _In_ PCWSTR Name)
{
    WCHAR Directory[MAX_PATH];

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    StringCchPrintfW(Path, MAX_PATH, L"%s%s", Directory, Name);
    DeleteFileW(Path);
}

static
VOID
DeleteHiveFile(
    _In_ PCWSTR Path)
{
    WCHAR Log[MAX_PATH];

    DeleteFileW(Path);
    StringCchPrintfW(Log, ARRAYSIZE(Log), L"%s.LOG1", Path);
    DeleteFileW(Log);
    StringCchPrintfW(Log, ARRAYSIZE(Log), L"%s.LOG2", Path);
    DeleteFileW(Log);
}

static
VOID
DeleteTestRoot(VOID)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, TEST_ROOT);
    RegDeleteKeyExW(HKEY_CURRENT_USER, TEST_ROOT, 0, 0);
}

static
LONG
CreateKeyWithDword(
    _In_ HKEY Parent,
    _In_ PCWSTR Name,
    _In_opt_ PCWSTR ValueName,
    _In_ DWORD Value,
    _Out_ PHKEY Key)
{
    LONG Error;

    Error = RegCreateKeyExW(Parent, Name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, Key, NULL);
    if (Error != ERROR_SUCCESS || !ValueName)
        return Error;
    return RegSetValueExW(*Key, ValueName, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
}

static
LONG
QueryDword(
    _In_ HKEY Key,
    _In_ PCWSTR ValueName,
    _Out_ PDWORD Value)
{
    DWORD Size = sizeof(*Value);
    DWORD Type = REG_NONE;
    LONG Error;

    *Value = 0;
    Error = RegQueryValueExW(Key, ValueName, NULL, &Type, (BYTE *)Value, &Size);
    if (Error == ERROR_SUCCESS && (Type != REG_DWORD || Size != sizeof(*Value)))
        return ERROR_INVALID_DATA;
    return Error;
}

static
BOOL
SaveKeyToFile(
    _In_ HKEY Key,
    _In_ PCWSTR Path)
{
    BOOLEAN WasEnabled, Ignored;
    LONG Error;

    if (!NT_SUCCESS(RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &WasEnabled)))
    {
        skip("Cannot enable SeBackupPrivilege\n");
        return FALSE;
    }
    Error = RegSaveKeyExW(Key, Path, NULL, REG_LATEST_FORMAT);
    ok_long(Error, ERROR_SUCCESS);
    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, WasEnabled, FALSE, &Ignored);
    return Error == ERROR_SUCCESS;
}

static
VOID
TestRegRestoreKey(VOID)
{
    WCHAR File[MAX_PATH];
    WCHAR Missing[MAX_PATH];
    HKEY Root = NULL, Source = NULL, Child = NULL, Target = NULL, Old = NULL, Key = NULL;
    BOOLEAN RestoreWas, BackupWas, Ignored;
    DWORD Value;
    LONG Error;

    if (!pRegRestoreKeyW)
    {
        skip("RegRestoreKeyW is not available\n");
        return;
    }

    Error = pRegRestoreKeyW(NULL, NULL, 0);
    ok_long(Error, ERROR_INVALID_PARAMETER);
    Error = pRegRestoreKeyW(NULL, L"", 0);
    ok_long(Error, ERROR_INVALID_PARAMETER);

    DeleteTestRoot();
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_ROOT, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Root, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return;

    ok_long(CreateKeyWithDword(Root, L"Source", L"Kept", 1, &Source), ERROR_SUCCESS);
    ok_long(CreateKeyWithDword(Source, L"Child", L"Inner", 2, &Child), ERROR_SUCCESS);
    ok_long(CreateKeyWithDword(Root, L"Target", L"Stale", 3, &Target), ERROR_SUCCESS);
    ok_long(CreateKeyWithDword(Target, L"Old", NULL, 0, &Old), ERROR_SUCCESS);
    if (Old)
        RegCloseKey(Old);
    if (Child)
        RegCloseKey(Child);

    MakeTempPath(File, L"LiberNTRestore.hiv");
    MakeTempPath(Missing, L"LiberNTRestoreMissing.hiv");

    if (Source && Target && SaveKeyToFile(Source, File))
    {
        RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, FALSE, FALSE, &RestoreWas);
        Error = pRegRestoreKeyW(Target, File, 0);
        ok_long(Error, ERROR_PRIVILEGE_NOT_HELD);
        Error = RegOpenKeyExW(Target, L"Old", 0, KEY_READ, &Key);
        ok_long(Error, ERROR_SUCCESS);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(Key);

        if (NT_SUCCESS(RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &Ignored)) &&
            NT_SUCCESS(RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &BackupWas)))
        {
            Error = pRegRestoreKeyW(Target, Missing, 0);
            ok_long(Error, ERROR_FILE_NOT_FOUND);

            Error = pRegRestoreKeyW(Target, File, 0);
            ok_long(Error, ERROR_SUCCESS);
            if (Error == ERROR_SUCCESS)
            {
                Error = RegOpenKeyExW(Target, L"Old", 0, KEY_READ, &Key);
                ok_long(Error, ERROR_FILE_NOT_FOUND);
                if (Error == ERROR_SUCCESS)
                    RegCloseKey(Key);

                Error = RegOpenKeyExW(Target, L"Child", 0, KEY_READ, &Key);
                ok_long(Error, ERROR_SUCCESS);
                if (Error == ERROR_SUCCESS)
                {
                    ok_long(QueryDword(Key, L"Inner", &Value), ERROR_SUCCESS);
                    ok_long(Value, 2);
                    RegCloseKey(Key);
                }

                ok_long(QueryDword(Target, L"Kept", &Value), ERROR_SUCCESS);
                ok_long(Value, 1);
                ok_long(QueryDword(Target, L"Stale", &Value), ERROR_FILE_NOT_FOUND);

                Error = RegOpenKeyExW(Target, L"Child", 0, KEY_READ, &Key);
                ok_long(Error, ERROR_SUCCESS);
                if (Error == ERROR_SUCCESS)
                {
                    Error = pRegRestoreKeyW(Target, File, 0);
                    ok(Error != ERROR_SUCCESS, "restore over an open subkey succeeded\n");
                    Error = pRegRestoreKeyW(Target, File, REG_FORCE_RESTORE);
                    ok_long(Error, ERROR_SUCCESS);
                    RegCloseKey(Key);
                }
            }

            RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, BackupWas, FALSE, &Ignored);
        }
        else
        {
            skip("Cannot enable SeRestorePrivilege and SeBackupPrivilege\n");
        }
        RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, RestoreWas, FALSE, &Ignored);
    }

    if (Target)
        RegCloseKey(Target);
    if (Source)
        RegCloseKey(Source);
    RegCloseKey(Root);
    DeleteTestRoot();
    DeleteHiveFile(File);
}

static
BOOL
GetTokenUserSidString(
    _In_ HANDLE Token,
    _Out_ PUNICODE_STRING SidString)
{
    UCHAR Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PTOKEN_USER User = (PTOKEN_USER)Buffer;
    NTSTATUS Status;
    ULONG Length;

    Status = NtQueryInformationToken(Token, TokenUser, Buffer, sizeof(Buffer), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;
    Status = RtlConvertSidToUnicodeString(SidString, User->User.Sid, TRUE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status);
}

static
VOID
CheckUserClassesView(
    _In_ HKEY Classes)
{
    WCHAR Data[16];
    DWORD Size;
    HKEY Key;
    LONG Error;

    Error = RegOpenKeyExW(Classes, TEST_CLASS, 0, KEY_READ, &Key);
    ok_long(Error, ERROR_SUCCESS);
    if (Error == ERROR_SUCCESS)
    {
        Size = sizeof(Data);
        Error = RegQueryValueExW(Key, NULL, NULL, NULL, (BYTE *)Data, &Size);
        ok_long(Error, ERROR_SUCCESS);
        if (Error == ERROR_SUCCESS)
            ok_wstr(Data, L"user");
        RegCloseKey(Key);
    }

    Error = RegOpenKeyExW(Classes, L"CLSID", 0, KEY_READ, &Key);
    ok_long(Error, ERROR_SUCCESS);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(Key);
}

static
VOID
TestUserClassesWithTemporaryProfile(
    _In_ HANDLE Token,
    _In_ PCUNICODE_STRING SidString)
{
    WCHAR File[MAX_PATH];
    HKEY Profile = NULL, Class = NULL, Classes = NULL, Machine;
    BOOLEAN RestoreWas, Ignored;
    LONG Error;

    Classes = (HKEY)(ULONG_PTR)0xdeadbeef;
    Error = pRegOpenUserClassesRoot(Token, 0, KEY_READ, &Classes);
    ok_long(Error, ERROR_FILE_NOT_FOUND);

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\" TEST_CLASS, 0, KEY_READ, &Machine);
    ok_long(Error, ERROR_FILE_NOT_FOUND);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(Machine);

    DeleteTestRoot();
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_ROOT L"\\Profile", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Profile, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return;
    Error = RegCreateKeyExW(Profile, L"Software\\Classes\\" TEST_CLASS, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Class, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error == ERROR_SUCCESS)
    {
        ok_long(RegSetValueExW(Class, NULL, 0, REG_SZ, (const BYTE *)L"user", sizeof(L"user")), ERROR_SUCCESS);
        RegCloseKey(Class);
    }

    MakeTempPath(File, L"LiberNTUserClasses.hiv");
    if (SaveKeyToFile(Profile, File))
    {
        if (NT_SUCCESS(RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &RestoreWas)))
        {
            Error = RegLoadKeyW(HKEY_USERS, SidString->Buffer, File);
            ok_long(Error, ERROR_SUCCESS);
            if (Error == ERROR_SUCCESS)
            {
                Classes = NULL;
                Error = pRegOpenUserClassesRoot(Token, 0, KEY_READ, &Classes);
                ok_long(Error, ERROR_SUCCESS);
                ok(Classes != NULL, "no key returned\n");
                if (Error == ERROR_SUCCESS && Classes)
                {
                    CheckUserClassesView(Classes);
                    ok_long(RegCloseKey(Classes), ERROR_SUCCESS);
                }
                Error = RegUnLoadKeyW(HKEY_USERS, SidString->Buffer);
                ok_long(Error, ERROR_SUCCESS);
            }
            RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, RestoreWas, FALSE, &Ignored);
        }
        else
        {
            skip("Cannot enable SeRestorePrivilege\n");
        }
    }

    RegCloseKey(Profile);
    DeleteTestRoot();
    DeleteHiveFile(File);
}

static
VOID
TestRegOpenUserClassesRoot(VOID)
{
    UNICODE_STRING SidString;
    HANDLE Token, NoQuery;
    HKEY Key, Classes;
    NTSTATUS Status;
    LONG Error;

    if (!pRegOpenUserClassesRoot)
    {
        skip("RegOpenUserClassesRoot is not available\n");
        return;
    }

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &Token);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Classes = (HKEY)(ULONG_PTR)0xdeadbeef;
    Error = pRegOpenUserClassesRoot(Token, 1, KEY_READ, &Classes);
    ok_long(Error, ERROR_INVALID_PARAMETER);
    Error = pRegOpenUserClassesRoot(Token, 0, KEY_READ, NULL);
    ok_long(Error, ERROR_INVALID_PARAMETER);
    Error = pRegOpenUserClassesRoot(NULL, 0, KEY_READ, &Classes);
    ok_long(Error, ERROR_INVALID_HANDLE);

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY_SOURCE, &NoQuery);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Error = pRegOpenUserClassesRoot(NoQuery, 0, KEY_READ, &Classes);
        ok_long(Error, ERROR_ACCESS_DENIED);
        NtClose(NoQuery);
    }

    if (GetTokenUserSidString(Token, &SidString))
    {
        Error = RegOpenKeyExW(HKEY_USERS, SidString.Buffer, 0, KEY_READ, &Key);
        if (Error == ERROR_SUCCESS)
        {
            RegCloseKey(Key);
            Classes = NULL;
            Error = pRegOpenUserClassesRoot(Token, 0, KEY_READ, &Classes);
            trace("profile of %ls is loaded: RegOpenUserClassesRoot returned %ld\n", SidString.Buffer, Error);
            ok(Error == ERROR_SUCCESS || Error == ERROR_FILE_NOT_FOUND, "RegOpenUserClassesRoot returned %ld\n", Error);
            if (Error == ERROR_SUCCESS)
            {
                Error = RegOpenKeyExW(Classes, L"CLSID", 0, KEY_READ, &Key);
                ok_long(Error, ERROR_SUCCESS);
                if (Error == ERROR_SUCCESS)
                    RegCloseKey(Key);
                RegCloseKey(Classes);
            }
        }
        else
        {
            ok_long(Error, ERROR_FILE_NOT_FOUND);
            TestUserClassesWithTemporaryProfile(Token, &SidString);
        }
        RtlFreeUnicodeString(&SidString);
    }

    NtClose(Token);
}

static
VOID
CheckDwordValue(
    _In_ HKEY Root,
    _In_ PCWSTR Path,
    _In_ DWORD Expected)
{
    DWORD Value;
    HKEY Key;
    LONG Error;

    Error = RegOpenKeyExW(Root, Path, 0, KEY_READ, &Key);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return;
    ok_long(QueryDword(Key, L"Value", &Value), ERROR_SUCCESS);
    ok_long(Value, Expected);
    RegCloseKey(Key);
}

static
VOID
DeleteKeyOnBothSides(
    _In_ PCWSTR Path)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, Path);
    RegDeleteKeyExW(HKEY_CURRENT_USER, Path, 0, 0);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, Path);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, Path, 0, 0);
}

static
VOID
TestSHRegCreateUSKey(VOID)
{
    static const WCHAR Path[] = TEST_ROOT L"US";
    HUSKEY UsKey;
    DWORD Value;
    HKEY Key;
    LONG Error;

    if (!pSHRegCreateUSKeyW || !pSHRegCloseUSKey || !pSHRegWriteUSValueW)
    {
        skip("SHRegCreateUSKeyW, SHRegCloseUSKey or SHRegWriteUSValueW is not available\n");
        return;
    }

    Error = pSHRegCreateUSKeyW(Path, KEY_ALL_ACCESS, NULL, NULL, SHREGSET_FORCE_HKCU);
    ok_long(Error, ERROR_INVALID_PARAMETER);

    DeleteKeyOnBothSides(Path);

    UsKey = (HUSKEY)(ULONG_PTR)0xdeadbeef;
    Error = pSHRegCreateUSKeyW(Path, KEY_ALL_ACCESS, NULL, &UsKey, 0);
    ok_long(Error, ERROR_INVALID_PARAMETER);
    ok_ptr(UsKey, NULL);

    UsKey = NULL;
    Error = pSHRegCreateUSKeyW(Path, KEY_ALL_ACCESS, NULL, &UsKey, SHREGSET_FORCE_HKCU);
    ok_long(Error, ERROR_SUCCESS);
    ok(UsKey != NULL, "no key returned\n");
    if (UsKey)
    {
        Value = 1;
        ok_long(pSHRegWriteUSValueW(UsKey, L"Value", REG_DWORD, &Value, sizeof(Value), SHREGSET_FORCE_HKCU), ERROR_SUCCESS);
        ok_long(pSHRegCloseUSKey(UsKey), ERROR_SUCCESS);
        CheckDwordValue(HKEY_CURRENT_USER, Path, 1);
    }
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key);
    ok_long(Error, ERROR_FILE_NOT_FOUND);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(Key);
    DeleteKeyOnBothSides(Path);

    UsKey = NULL;
    Error = pSHRegCreateUSKeyW(Path, KEY_ALL_ACCESS, NULL, &UsKey, SHREGSET_FORCE_HKLM);
    ok_long(Error, ERROR_SUCCESS);
    ok(UsKey != NULL, "no key returned\n");
    if (UsKey)
    {
        Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key);
        ok_long(Error, ERROR_SUCCESS);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(Key);
        Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_READ, &Key);
        ok_long(Error, ERROR_FILE_NOT_FOUND);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(Key);
        Value = 2;
        ok_long(pSHRegWriteUSValueW(UsKey, L"Value", REG_DWORD, &Value, sizeof(Value), SHREGSET_FORCE_HKLM), ERROR_SUCCESS);
        ok_long(pSHRegCloseUSKey(UsKey), ERROR_SUCCESS);
        CheckDwordValue(HKEY_LOCAL_MACHINE, Path, 2);
    }
    DeleteKeyOnBothSides(Path);

    UsKey = NULL;
    Error = pSHRegCreateUSKeyW(Path, KEY_ALL_ACCESS, NULL, &UsKey, SHREGSET_DEFAULT);
    ok_long(Error, ERROR_SUCCESS);
    ok(UsKey != NULL, "no key returned\n");
    if (UsKey)
    {
        Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_READ, &Key);
        ok_long(Error, ERROR_SUCCESS);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(Key);
        Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key);
        ok_long(Error, ERROR_SUCCESS);
        if (Error == ERROR_SUCCESS)
            RegCloseKey(Key);
        Value = 3;
        ok_long(pSHRegWriteUSValueW(UsKey, L"Value", REG_DWORD, &Value, sizeof(Value), SHREGSET_DEFAULT), ERROR_SUCCESS);
        ok_long(pSHRegCloseUSKey(UsKey), ERROR_SUCCESS);
        CheckDwordValue(HKEY_CURRENT_USER, Path, 3);
        CheckDwordValue(HKEY_LOCAL_MACHINE, Path, 3);
    }
    DeleteKeyOnBothSides(Path);
}

static
VOID
TestRegLoadMUIStringA(VOID)
{
    CHAR Buffer[64];
    DWORD Size;
    LONG Error;

    if (!pRegLoadMUIStringA)
    {
        skip("RegLoadMUIStringA is not available\n");
        return;
    }

    Size = 0xdeadbeef;
    Error = pRegLoadMUIStringA(HKEY_LOCAL_MACHINE, "LiberNTMissingValue", Buffer, sizeof(Buffer), &Size, 0, NULL);
    ok_long(Error, ERROR_CALL_NOT_IMPLEMENTED);
}

START_TEST(KernelBaseRegistry)
{
    HMODULE KernelBase, Kernel32;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    KernelBase = GetModuleHandleW(L"kernelbase.dll");
    ok(KernelBase != NULL, "kernelbase.dll is not loaded\n");
    if (!KernelBase)
        return;

    pRegRestoreKeyW = (PFN_REGRESTOREKEYW)GetProcAddress(KernelBase, "RegRestoreKeyW");
    pRegOpenUserClassesRoot = (PFN_REGOPENUSERCLASSESROOT)GetProcAddress(KernelBase, "RegOpenUserClassesRoot");
    pSHRegCreateUSKeyW = (PFN_SHREGCREATEUSKEYW)GetProcAddress(KernelBase, "SHRegCreateUSKeyW");
    pSHRegCloseUSKey = (PFN_SHREGCLOSEUSKEY)GetProcAddress(KernelBase, "SHRegCloseUSKey");
    pSHRegWriteUSValueW = (PFN_SHREGWRITEUSVALUEW)GetProcAddress(KernelBase, "SHRegWriteUSValueW");
    pRegLoadMUIStringA = (PFN_REGLOADMUISTRINGA)GetProcAddress(KernelBase, "RegLoadMUIStringA");

    ok(GetProcAddress(Kernel32, "RegRestoreKeyW") != NULL, "kernel32 does not export RegRestoreKeyW\n");
    ok(GetProcAddress(Kernel32, "RegOpenUserClassesRoot") != NULL, "kernel32 does not export RegOpenUserClassesRoot\n");

    TestRegRestoreKey();
    TestRegOpenUserClassesRoot();
    TestSHRegCreateUSKey();
    TestRegLoadMUIStringA();
}
