/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for software restriction policy rules and their evaluation by SaferIdentifyLevel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <winsafer.h>

#define SAFER_PARENT_PATH L"Software\\Policies\\Microsoft\\Windows"
#define SAFER_POLICY_PATH SAFER_PARENT_PATH L"\\Safer\\CodeIdentifiers"
#define SAFER_EVENT_SOURCE L"Microsoft-Windows-SoftwareRestrictionPolicies"
#define TEST_MARKER L"LiberNTSaferApitest"
#define TEST_REGISTRY_RULE_KEY L"Software\\LiberNTSaferApitest"

static BOOL (WINAPI *pSaferCreateLevel)(DWORD, DWORD, DWORD, SAFER_LEVEL_HANDLE *, PVOID);
static BOOL (WINAPI *pSaferCloseLevel)(SAFER_LEVEL_HANDLE);
static BOOL (WINAPI *pSaferGetLevelInformation)(SAFER_LEVEL_HANDLE, SAFER_OBJECT_INFO_CLASS, PVOID, DWORD, PDWORD);
static BOOL (WINAPI *pSaferSetLevelInformation)(SAFER_LEVEL_HANDLE, SAFER_OBJECT_INFO_CLASS, PVOID, DWORD);
static BOOL (WINAPI *pSaferIdentifyLevel)(DWORD, PSAFER_CODE_PROPERTIES, SAFER_LEVEL_HANDLE *, PVOID);
static BOOL (WINAPI *pSaferGetPolicyInformation)(DWORD, SAFER_POLICY_INFO_CLASS, DWORD, PVOID, PDWORD, PVOID);
static BOOL (WINAPI *pSaferSetPolicyInformation)(DWORD, SAFER_POLICY_INFO_CLASS, DWORD, PVOID, PVOID);
static BOOL (WINAPI *pSaferRecordEventLogEntry)(SAFER_LEVEL_HANDLE, PCWSTR, PVOID);
static BOOL (WINAPI *pSaferiIsExecutableFileType)(PCWSTR, BOOLEAN);
static BOOL (WINAPI *pSaferiSearchMatchingHashRules)(ALG_ID, PBYTE, DWORD, DWORD, PDWORD, PDWORD);

static WCHAR TestDir[MAX_PATH];
static WCHAR TargetPath[MAX_PATH];
static const char TargetContent[] = "LiberNT software restriction policy test image\r\n";
static BYTE TargetMd5[16];

static BOOL
ResolveSafer(void)
{
    HMODULE Advapi = GetModuleHandleW(L"advapi32.dll");

    pSaferCreateLevel = (void *)GetProcAddress(Advapi, "SaferCreateLevel");
    pSaferCloseLevel = (void *)GetProcAddress(Advapi, "SaferCloseLevel");
    pSaferGetLevelInformation = (void *)GetProcAddress(Advapi, "SaferGetLevelInformation");
    pSaferSetLevelInformation = (void *)GetProcAddress(Advapi, "SaferSetLevelInformation");
    pSaferIdentifyLevel = (void *)GetProcAddress(Advapi, "SaferIdentifyLevel");
    pSaferGetPolicyInformation = (void *)GetProcAddress(Advapi, "SaferGetPolicyInformation");
    pSaferSetPolicyInformation = (void *)GetProcAddress(Advapi, "SaferSetPolicyInformation");
    pSaferRecordEventLogEntry = (void *)GetProcAddress(Advapi, "SaferRecordEventLogEntry");
    pSaferiIsExecutableFileType = (void *)GetProcAddress(Advapi, "SaferiIsExecutableFileType");
    pSaferiSearchMatchingHashRules = (void *)GetProcAddress(Advapi, "SaferiSearchMatchingHashRules");

    return pSaferCreateLevel && pSaferCloseLevel && pSaferGetLevelInformation && pSaferSetLevelInformation &&
           pSaferIdentifyLevel && pSaferGetPolicyInformation && pSaferSetPolicyInformation &&
           pSaferRecordEventLogEntry && pSaferiIsExecutableFileType && pSaferiSearchMatchingHashRules;
}

static void
TestGuid(DWORD Index, GUID *Guid, PWSTR Text)
{
    Guid->Data1 = 0x5afe1800;
    Guid->Data2 = 0x0000;
    Guid->Data3 = 0x4000;
    Guid->Data4[0] = 0x80;
    Guid->Data4[1] = 0x00;
    Guid->Data4[2] = 0x00;
    Guid->Data4[3] = 0x00;
    Guid->Data4[4] = 0x00;
    Guid->Data4[5] = 0x00;
    Guid->Data4[6] = 0x00;
    Guid->Data4[7] = (BYTE)Index;
    StringCchPrintfW(Text, 39, L"{5afe1800-0000-4000-8000-0000000000%02lx}", Index);
}

static BOOL
PolicyKeyExists(HKEY Root, PBOOL HasMarker)
{
    HKEY Key;

    *HasMarker = FALSE;
    if (RegOpenKeyExW(Root, SAFER_POLICY_PATH, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return FALSE;
    *HasMarker = RegQueryValueExW(Key, TEST_MARKER, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
    RegCloseKey(Key);
    return TRUE;
}

static void
RemoveTestPolicy(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, SAFER_POLICY_PATH);
    RegDeleteKeyW(HKEY_CURRENT_USER, SAFER_POLICY_PATH);
    RegDeleteKeyW(HKEY_CURRENT_USER, SAFER_PARENT_PATH L"\\Safer");
    RegDeleteTreeW(HKEY_CURRENT_USER, TEST_REGISTRY_RULE_KEY);
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_REGISTRY_RULE_KEY);
}

static BOOL
SetPolicyDword(PCWSTR Name, DWORD Value)
{
    HKEY Key;
    LSTATUS Status;

    Status = RegCreateKeyExW(HKEY_CURRENT_USER, SAFER_POLICY_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Status != ERROR_SUCCESS)
        return FALSE;
    Status = RegSetValueExW(Key, Name, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);
    return Status == ERROR_SUCCESS;
}

static void
DeletePolicyValue(PCWSTR Name)
{
    HKEY Key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, SAFER_POLICY_PATH, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(Key, Name);
    RegCloseKey(Key);
}

static BOOL
GetPolicyDword(PCWSTR Name, PDWORD Value)
{
    DWORD Size = sizeof(*Value);

    return RegGetValueW(HKEY_CURRENT_USER, SAFER_POLICY_PATH, Name, RRF_RT_REG_DWORD, NULL, Value, &Size) ==
           ERROR_SUCCESS;
}

static HKEY
CreateRuleKey(DWORD LevelId, PCWSTR Kind, DWORD Index)
{
    WCHAR Path[MAX_PATH], GuidText[40];
    GUID Guid;
    HKEY Key;

    TestGuid(Index, &Guid, GuidText);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%lu\\%s\\%s", SAFER_POLICY_PATH, LevelId, Kind, GuidText);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL) != ERROR_SUCCESS)
        return NULL;
    return Key;
}

static void
DeleteRule(DWORD LevelId, PCWSTR Kind, DWORD Index)
{
    WCHAR Path[MAX_PATH], GuidText[40];
    GUID Guid;

    TestGuid(Index, &Guid, GuidText);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%lu\\%s\\%s", SAFER_POLICY_PATH, LevelId, Kind, GuidText);
    ok(RegDeleteKeyW(HKEY_CURRENT_USER, Path) == ERROR_SUCCESS, "deleting rule %s failed\n", wine_dbgstr_w(Path));
}

static BOOL
RuleExists(DWORD LevelId, PCWSTR Kind, DWORD Index)
{
    WCHAR Path[MAX_PATH], GuidText[40];
    GUID Guid;
    HKEY Key;

    TestGuid(Index, &Guid, GuidText);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%lu\\%s\\%s", SAFER_POLICY_PATH, LevelId, Kind, GuidText);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return FALSE;
    RegCloseKey(Key);
    return TRUE;
}

static void
AddPathRule(DWORD LevelId, DWORD Index, PCWSTR ItemData, DWORD Type)
{
    DWORD Flags = 0;
    HKEY Key = CreateRuleKey(LevelId, L"Paths", Index);

    ok(Key != NULL, "creating path rule %lu failed\n", Index);
    if (!Key)
        return;
    ok(RegSetValueExW(Key, L"ItemData", 0, Type, (const BYTE *)ItemData,
                      (DWORD)((wcslen(ItemData) + 1) * sizeof(WCHAR))) == ERROR_SUCCESS, "ItemData not set\n");
    RegSetValueExW(Key, L"SaferFlags", 0, REG_DWORD, (const BYTE *)&Flags, sizeof(Flags));
    RegCloseKey(Key);
}

static void
AddHashRule(DWORD LevelId, DWORD Index, const BYTE *Hash, DWORD HashSize, ALG_ID Algorithm, ULONGLONG ItemSize)
{
    DWORD Flags = 0;
    HKEY Key = CreateRuleKey(LevelId, L"Hashes", Index);

    ok(Key != NULL, "creating hash rule %lu failed\n", Index);
    if (!Key)
        return;
    RegSetValueExW(Key, L"ItemData", 0, REG_BINARY, Hash, HashSize);
    RegSetValueExW(Key, L"HashAlg", 0, REG_DWORD, (const BYTE *)&Algorithm, sizeof(DWORD));
    RegSetValueExW(Key, L"ItemSize", 0, REG_QWORD, (const BYTE *)&ItemSize, sizeof(ItemSize));
    RegSetValueExW(Key, L"SaferFlags", 0, REG_DWORD, (const BYTE *)&Flags, sizeof(Flags));
    RegCloseKey(Key);
}

static void
AddZoneRule(DWORD LevelId, DWORD Index, DWORD Zone)
{
    DWORD Flags = 0;
    HKEY Key = CreateRuleKey(LevelId, L"UrlZones", Index);

    ok(Key != NULL, "creating zone rule %lu failed\n", Index);
    if (!Key)
        return;
    RegSetValueExW(Key, L"ItemData", 0, REG_DWORD, (const BYTE *)&Zone, sizeof(Zone));
    RegSetValueExW(Key, L"SaferFlags", 0, REG_DWORD, (const BYTE *)&Flags, sizeof(Flags));
    RegCloseKey(Key);
}

static DWORD
LevelOf(SAFER_LEVEL_HANDLE Level)
{
    DWORD Value = 0xdeadbeef, Size;

    if (!pSaferGetLevelInformation(Level, SaferObjectLevelId, &Value, sizeof(Value), &Size))
        return 0xffffffff;
    return Value;
}

static DWORD
IdentifyEx(PSAFER_CODE_PROPERTIES_V2 Properties, SAFER_LEVEL_HANDLE *KeepLevel)
{
    SAFER_LEVEL_HANDLE Level = NULL;
    DWORD Value;

    Properties->cbSize = sizeof(*Properties);
    if (!pSaferIdentifyLevel(1, (PSAFER_CODE_PROPERTIES)Properties, &Level, NULL))
    {
        ok(0, "SaferIdentifyLevel failed: %lu\n", GetLastError());
        return 0xffffffff;
    }

    Value = LevelOf(Level);
    if (KeepLevel)
        *KeepLevel = Level;
    else
        pSaferCloseLevel(Level);
    return Value;
}

static DWORD
Identify(DWORD Flags, PCWSTR Path)
{
    SAFER_CODE_PROPERTIES_V2 Properties;

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.dwCheckFlags = Flags;
    Properties.ImagePath = Path;
    return IdentifyEx(&Properties, NULL);
}

static void
TestPath(PWSTR Buffer, PCWSTR Name)
{
    StringCchPrintfW(Buffer, MAX_PATH, L"%s\\%s", TestDir, Name);
}

static PEVENTLOGRECORD
NewestSaferEvent(PBYTE Buffer, DWORD BufferSize)
{
    PEVENTLOGRECORD Record;
    HANDLE Log;
    DWORD Read = 0, Needed = 0, Offset = 0;

    Log = OpenEventLogW(NULL, L"Application");
    if (!Log)
        return NULL;

    if (!ReadEventLogW(Log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, Buffer, BufferSize, &Read, &Needed))
        Read = 0;
    CloseEventLog(Log);

    while (Offset + sizeof(EVENTLOGRECORD) <= Read)
    {
        Record = (PEVENTLOGRECORD)(Buffer + Offset);
        if (!_wcsicmp((PCWSTR)(Record + 1), SAFER_EVENT_SOURCE))
            return Record;
        if (Record->Length == 0)
            break;
        Offset += Record->Length;
    }
    return NULL;
}

static void
CheckEvent(SAFER_LEVEL_HANDLE Level, PCWSTR Target, DWORD EventId, PCWSTR Guid, PCWSTR RulePath)
{
    PEVENTLOGRECORD Record;
    PCWSTR String;
    PBYTE Buffer;
    BOOL Ret;

    Ret = pSaferRecordEventLogEntry(Level, Target, NULL);
    ok(Ret, "SaferRecordEventLogEntry failed: %lu\n", GetLastError());
    if (!Ret)
        return;

    Buffer = HeapAlloc(GetProcessHeap(), 0, 0x10000);
    if (!Buffer)
        return;

    Record = NewestSaferEvent(Buffer, 0x10000);
    ok(Record != NULL, "no software restriction event in the Application log\n");
    if (Record)
    {
        ok((Record->EventID & 0xFFFF) == EventId, "event ID = %lu, expected %lu\n", Record->EventID & 0xFFFF, EventId);
        String = (PCWSTR)((PBYTE)Record + Record->StringOffset);
        ok(Record->NumStrings >= 1 && !_wcsicmp(String, Target), "event target = %S\n", String);
        if (Guid)
        {
            ok(Record->NumStrings >= 2, "event has %u strings\n", Record->NumStrings);
            if (Record->NumStrings >= 2)
            {
                String += wcslen(String) + 1;
                ok(!_wcsicmp(String, Guid), "event rule = %S, expected %S\n", String, Guid);
            }
        }
        if (RulePath)
        {
            ok(Record->NumStrings >= 3, "event has %u strings\n", Record->NumStrings);
            if (Record->NumStrings >= 3)
            {
                String += wcslen(String) + 1;
                ok(!_wcsicmp(String, RulePath), "event rule path = %S, expected %S\n", String, RulePath);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
}

static void
TestPathRules(void)
{
    WCHAR Path[MAX_PATH], Other[MAX_PATH], Deep[MAX_PATH], Rule[MAX_PATH], Guid[40];
    SAFER_CODE_PROPERTIES_V2 Properties;
    SAFER_LEVEL_HANDLE Level = NULL;
    GUID Id;

    TestPath(Other, L"other.exe");
    TestPath(Deep, L"sub\\deep\\nested.exe");

    AddPathRule(SAFER_LEVELID_DISALLOWED, 1, TargetPath, REG_SZ);
    ZeroMemory(&Properties, sizeof(Properties));
    Properties.dwCheckFlags = SAFER_CRITERIA_IMAGEPATH;
    Properties.ImagePath = TargetPath;
    ok(IdentifyEx(&Properties, &Level) == SAFER_LEVELID_DISALLOWED, "file rule did not disallow the file\n");
    if (Level)
    {
        TestGuid(1, &Id, Guid);
        CheckEvent(Level, TargetPath, 866, Guid, TargetPath);
        pSaferCloseLevel(Level);
    }
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Other) == SAFER_LEVELID_FULLYTRUSTED, "file rule matched another file\n");
    ok(Identify(0, TargetPath) == SAFER_LEVELID_FULLYTRUSTED, "rule applied without criteria\n");
    ok(Identify(SAFER_CRITERIA_IMAGEHASH, TargetPath) == SAFER_LEVELID_FULLYTRUSTED,
       "path rule applied without the path criterion\n");
    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Paths", 1);

    AddPathRule(SAFER_LEVELID_DISALLOWED, 2, TestDir, REG_SZ);
    AddPathRule(SAFER_LEVELID_FULLYTRUSTED, 3, TargetPath, REG_SZ);
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, TargetPath) == SAFER_LEVELID_FULLYTRUSTED,
       "file rule did not beat the folder rule\n");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Other) == SAFER_LEVELID_DISALLOWED, "folder rule missed a file\n");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Deep) == SAFER_LEVELID_DISALLOWED, "folder rule missed a subfolder\n");

    AddPathRule(SAFER_LEVELID_FULLYTRUSTED, 4, TestDir, REG_SZ);
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Other) == SAFER_LEVELID_DISALLOWED,
       "equal rules did not resolve to the most restrictive level\n");
    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Paths", 2);
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Other) == SAFER_LEVELID_FULLYTRUSTED, "unrestricted folder rule missed\n");

    AddPathRule(SAFER_LEVELID_DISALLOWED, 5, L"*.safertest", REG_SZ);
    TestPath(Path, L"payload.SaferTest");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_DISALLOWED,
       "extension rule did not beat the folder rule\n");
    StringCchPrintfW(Rule, ARRAYSIZE(Rule), L"%s\\*.safertest", TestDir);
    AddPathRule(SAFER_LEVELID_FULLYTRUSTED, 6, Rule, REG_SZ);
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_FULLYTRUSTED,
       "folder extension rule did not beat the extension rule\n");
    DeleteRule(SAFER_LEVELID_FULLYTRUSTED, L"Paths", 6);
    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Paths", 5);
    DeleteRule(SAFER_LEVELID_FULLYTRUSTED, L"Paths", 4);
    DeleteRule(SAFER_LEVELID_FULLYTRUSTED, L"Paths", 3);

    StringCchPrintfW(Rule, ARRAYSIZE(Rule), L"%s\\wild*", TestDir);
    AddPathRule(SAFER_LEVELID_DISALLOWED, 7, Rule, REG_SZ);
    TestPath(Path, L"wildcard\\a.exe");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_DISALLOWED, "'*' folder rule missed\n");
    TestPath(Path, L"tame\\a.exe");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_FULLYTRUSTED, "'*' folder rule overmatched\n");
    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Paths", 7);

    StringCchPrintfW(Rule, ARRAYSIZE(Rule), L"%s\\q?\\", TestDir);
    AddPathRule(SAFER_LEVELID_DISALLOWED, 8, Rule, REG_SZ);
    TestPath(Path, L"q1\\a.exe");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_DISALLOWED, "'?' folder rule missed\n");
    TestPath(Path, L"q12\\a.exe");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_FULLYTRUSTED, "'?' folder rule overmatched\n");
    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Paths", 8);

    AddPathRule(SAFER_LEVELID_DISALLOWED, 9, L"%SystemRoot%\\safer-apitest-env.exe", REG_EXPAND_SZ);
    GetWindowsDirectoryW(Path, ARRAYSIZE(Path));
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\SAFER-APITEST-ENV.EXE");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_DISALLOWED, "environment rule missed\n");
    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Paths", 9);

    ok(RegSetKeyValueW(HKEY_CURRENT_USER, TEST_REGISTRY_RULE_KEY, L"Dir", REG_SZ, TestDir,
                       (DWORD)((wcslen(TestDir) + 1) * sizeof(WCHAR))) == ERROR_SUCCESS, "registry rule value not set\n");
    AddPathRule(SAFER_LEVELID_DISALLOWED, 10, L"%HKEY_CURRENT_USER\\" TEST_REGISTRY_RULE_KEY L"\\Dir%reg.exe", REG_SZ);
    TestPath(Path, L"reg.exe");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_DISALLOWED, "registry path rule missed\n");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Other) == SAFER_LEVELID_FULLYTRUSTED, "registry path rule overmatched\n");
    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Paths", 10);

    AddPathRule(SAFER_LEVELID_NORMALUSER, 11, TargetPath, REG_SZ);
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, TargetPath) == SAFER_LEVELID_NORMALUSER, "Basic User rule missed\n");
    DeleteRule(SAFER_LEVELID_NORMALUSER, L"Paths", 11);
}

static BOOL
Md5(const void *Data, DWORD Size, PBYTE Hash)
{
    HCRYPTPROV Provider;
    HCRYPTHASH Handle;
    DWORD Length = 16;
    BOOL Ok = FALSE;

    if (!CryptAcquireContextW(&Provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return FALSE;
    if (CryptCreateHash(Provider, CALG_MD5, 0, 0, &Handle))
    {
        Ok = CryptHashData(Handle, Data, Size, 0) && CryptGetHashParam(Handle, HP_HASHVAL, Hash, &Length, 0);
        CryptDestroyHash(Handle);
    }
    CryptReleaseContext(Provider, 0);
    return Ok;
}

static void
TestHashRules(void)
{
    SAFER_CODE_PROPERTIES_V2 Properties;
    SAFER_LEVEL_HANDLE Level = NULL;
    DWORD Size = sizeof(TargetContent) - 1, Found, Flags;
    WCHAR Guid[40];
    GUID Id;
    BOOL Ret;

    AddPathRule(SAFER_LEVELID_FULLYTRUSTED, 20, TargetPath, REG_SZ);
    AddHashRule(SAFER_LEVELID_DISALLOWED, 21, TargetMd5, sizeof(TargetMd5), CALG_MD5, Size);

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.dwCheckFlags = SAFER_CRITERIA_IMAGEPATH | SAFER_CRITERIA_IMAGEHASH;
    Properties.ImagePath = TargetPath;
    ok(IdentifyEx(&Properties, &Level) == SAFER_LEVELID_DISALLOWED, "hash rule did not beat the path rule\n");
    if (Level)
    {
        TestGuid(21, &Id, Guid);
        CheckEvent(Level, TargetPath, 868, Guid, NULL);
        pSaferCloseLevel(Level);
    }
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, TargetPath) == SAFER_LEVELID_FULLYTRUSTED,
       "hash rule applied without the hash criterion\n");

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.dwCheckFlags = SAFER_CRITERIA_IMAGEHASH;
    CopyMemory(Properties.ImageHash, TargetMd5, sizeof(TargetMd5));
    Properties.dwImageHashSize = sizeof(TargetMd5);
    Properties.ImageSize.QuadPart = Size;
    Properties.HashAlgorithm = CALG_MD5;
    ok(IdentifyEx(&Properties, NULL) == SAFER_LEVELID_DISALLOWED, "supplied hash did not match\n");
    Properties.ImageSize.QuadPart = Size + 1;
    ok(IdentifyEx(&Properties, NULL) == SAFER_LEVELID_FULLYTRUSTED, "supplied hash matched with another size\n");

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.dwCheckFlags = SAFER_CRITERIA_IMAGEHASH;
    Properties.pByteBlock = (LPBYTE)TargetContent;
    Properties.ImageSize.QuadPart = Size;
    ok(IdentifyEx(&Properties, NULL) == SAFER_LEVELID_DISALLOWED, "memory image hash did not match\n");

    Found = 0xdeadbeef;
    Flags = 0xdeadbeef;
    Ret = pSaferiSearchMatchingHashRules(CALG_MD5, TargetMd5, sizeof(TargetMd5), Size, &Found, &Flags);
    ok(Ret && Found == SAFER_LEVELID_DISALLOWED && Flags == 0,
       "hash search: ret %d, level 0x%lx, flags 0x%lx, error %lu\n", Ret, Found, Flags, GetLastError());
    Found = 0xdeadbeef;
    Ret = pSaferiSearchMatchingHashRules(0, TargetMd5, sizeof(TargetMd5), 0, &Found, NULL);
    ok(Ret && Found == SAFER_LEVELID_DISALLOWED, "hash search without algorithm and size: ret %d, level 0x%lx\n",
       Ret, Found);
    SetLastError(0xdeadbeef);
    Ret = pSaferiSearchMatchingHashRules(CALG_MD5, TargetMd5, sizeof(TargetMd5), Size + 1, &Found, NULL);
    ok(!Ret && GetLastError() == ERROR_NOT_FOUND, "hash search with another size: ret %d, error %lu\n",
       Ret, GetLastError());
    SetLastError(0xdeadbeef);
    Ret = pSaferiSearchMatchingHashRules(CALG_SHA1, TargetMd5, sizeof(TargetMd5), Size, &Found, NULL);
    ok(!Ret && GetLastError() == ERROR_NOT_FOUND, "hash search with another algorithm: ret %d, error %lu\n",
       Ret, GetLastError());

    DeleteRule(SAFER_LEVELID_DISALLOWED, L"Hashes", 21);
    DeleteRule(SAFER_LEVELID_FULLYTRUSTED, L"Paths", 20);
}

static void
TestZoneRules(void)
{
    SAFER_CODE_PROPERTIES_V2 Properties;

    AddZoneRule(SAFER_LEVELID_DISALLOWED, 30, 3);

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.dwCheckFlags = SAFER_CRITERIA_URLZONE;
    Properties.UrlZoneId = 3;
    ok(IdentifyEx(&Properties, NULL) == SAFER_LEVELID_DISALLOWED, "zone rule missed\n");
    Properties.UrlZoneId = 1;
    ok(IdentifyEx(&Properties, NULL) == SAFER_LEVELID_FULLYTRUSTED, "zone rule matched another zone\n");
    Properties.dwCheckFlags = SAFER_CRITERIA_IMAGEPATH;
    Properties.UrlZoneId = 3;
    Properties.ImagePath = TargetPath;
    ok(IdentifyEx(&Properties, NULL) == SAFER_LEVELID_FULLYTRUSTED, "zone rule applied without the zone criterion\n");

    DeleteRule(SAFER_LEVELID_DISALLOWED, L"UrlZones", 30);
}

static void
TestDefaultLevel(void)
{
    WCHAR None[MAX_PATH], Allowed[MAX_PATH];
    SAFER_CODE_PROPERTIES_V2 Properties;
    SAFER_LEVEL_HANDLE Level = NULL;
    DWORD Value, Size;
    BOOL Ret;

    TestPath(None, L"no-rule.exe");
    TestPath(Allowed, L"allowed.exe");

    ok(SetPolicyDword(L"DefaultLevel", SAFER_LEVELID_DISALLOWED), "DefaultLevel not set\n");
    AddPathRule(SAFER_LEVELID_FULLYTRUSTED, 40, Allowed, REG_SZ);

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.dwCheckFlags = SAFER_CRITERIA_IMAGEPATH;
    Properties.ImagePath = None;
    ok(IdentifyEx(&Properties, &Level) == SAFER_LEVELID_DISALLOWED, "default level not applied\n");
    if (Level)
    {
        CheckEvent(Level, None, 865, NULL, NULL);
        pSaferCloseLevel(Level);
    }
    ok(Identify(0, None) == SAFER_LEVELID_DISALLOWED, "default level not applied without criteria\n");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Allowed) == SAFER_LEVELID_FULLYTRUSTED, "allow rule missed\n");

    Value = 0xdeadbeef;
    Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_USER, SaferPolicyDefaultLevel, sizeof(Value), &Value, &Size, NULL);
    ok(Ret && Value == SAFER_LEVELID_DISALLOWED && Size == sizeof(DWORD),
       "user default level: ret %d, value 0x%lx, size %lu\n", Ret, Value, Size);

    DeleteRule(SAFER_LEVELID_FULLYTRUSTED, L"Paths", 40);
    DeletePolicyValue(L"DefaultLevel");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, None) == SAFER_LEVELID_FULLYTRUSTED, "default level outlived its value\n");
}

static void
TestRuleRoundTrip(void)
{
    BYTE Buffer[sizeof(SAFER_PATHNAME_IDENTIFICATION) + MAX_PATH * sizeof(WCHAR)];
    PSAFER_PATHNAME_IDENTIFICATION Rule = (PSAFER_PATHNAME_IDENTIFICATION)Buffer;
    SAFER_PATHNAME_IDENTIFICATION NewRule;
    SAFER_IDENTIFICATION_HEADER Delete;
    WCHAR Path[MAX_PATH], Data[MAX_PATH], GuidText[40];
    SAFER_LEVEL_HANDLE Level;
    DWORD Size, Type;
    GUID Id;
    BOOL Ret;

    if (!pSaferCreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_DISALLOWED, SAFER_LEVEL_OPEN, &Level, NULL))
    {
        ok(0, "SaferCreateLevel failed: %lu\n", GetLastError());
        return;
    }

    TestPath(Path, L"api-rule.exe");
    TestGuid(50, &Id, GuidText);
    ZeroMemory(&NewRule, sizeof(NewRule));
    NewRule.header.dwIdentificationType = SaferIdentityTypeImageName;
    NewRule.header.cbStructSize = sizeof(NewRule);
    NewRule.header.IdentificationGuid = Id;
    StringCchCopyW(NewRule.Description, ARRAYSIZE(NewRule.Description), L"api test rule");
    NewRule.ImageName = Path;
    Ret = pSaferSetLevelInformation(Level, SaferObjectSingleIdentification, &NewRule, sizeof(NewRule));
    ok(Ret, "creating a path rule failed: %lu\n", GetLastError());
    ok(RuleExists(SAFER_LEVELID_DISALLOWED, L"Paths", 50), "path rule key missing\n");

    Size = sizeof(Data);
    Data[0] = UNICODE_NULL;
    if (RegGetValueW(HKEY_CURRENT_USER, SAFER_POLICY_PATH L"\\0\\Paths\\{5afe1800-0000-4000-8000-000000000032}",
                     L"ItemData", RRF_RT_ANY, &Type, Data, &Size) == ERROR_SUCCESS)
    {
        ok(Type == REG_SZ, "ItemData type = %lu\n", Type);
        ok(!wcscmp(Data, Path), "ItemData = %S\n", Data);
    }
    else
    {
        ok(0, "ItemData missing\n");
    }
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_DISALLOWED, "API-created rule not applied\n");

    ZeroMemory(Buffer, sizeof(Buffer));
    Rule->header.IdentificationGuid = Id;
    Size = 0;
    SetLastError(0xdeadbeef);
    Ret = pSaferGetLevelInformation(Level, SaferObjectSingleIdentification, Rule, sizeof(SAFER_IDENTIFICATION_HEADER),
                                    &Size);
    ok(!Ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "short rule query: ret %d, error %lu\n",
       Ret, GetLastError());
    ok(Size == sizeof(SAFER_PATHNAME_IDENTIFICATION) + (wcslen(Path) + 1) * sizeof(WCHAR), "rule size = %lu\n", Size);

    Ret = pSaferGetLevelInformation(Level, SaferObjectSingleIdentification, Rule, sizeof(Buffer), &Size);
    ok(Ret, "rule query failed: %lu\n", GetLastError());
    if (Ret)
    {
        ok(Rule->header.dwIdentificationType == SaferIdentityTypeImageName, "rule type = %d\n",
           Rule->header.dwIdentificationType);
        ok(Rule->header.cbStructSize == sizeof(SAFER_PATHNAME_IDENTIFICATION), "rule size = %lu\n",
           Rule->header.cbStructSize);
        ok(IsEqualGUID(&Rule->header.IdentificationGuid, &Id), "rule GUID changed\n");
        ok(Rule->ImageName && !wcscmp(Rule->ImageName, Path), "rule image name = %S\n", Rule->ImageName);
        ok(!wcscmp(Rule->Description, L"api test rule"), "rule description = %S\n", Rule->Description);
        ok(Rule->dwSaferFlags == 0, "rule flags = 0x%lx\n", Rule->dwSaferFlags);
    }

    ZeroMemory(&Delete, sizeof(Delete));
    Delete.dwIdentificationType = SaferIdentityDefault;
    Delete.cbStructSize = sizeof(Delete);
    Delete.IdentificationGuid = Id;
    Ret = pSaferSetLevelInformation(Level, SaferObjectSingleIdentification, &Delete, sizeof(Delete));
    ok(Ret, "deleting the rule failed: %lu\n", GetLastError());
    ok(!RuleExists(SAFER_LEVELID_DISALLOWED, L"Paths", 50), "path rule key still present\n");
    ok(Identify(SAFER_CRITERIA_IMAGEPATH, Path) == SAFER_LEVELID_FULLYTRUSTED, "deleted rule still applied\n");

    SetLastError(0xdeadbeef);
    Ret = pSaferSetLevelInformation(Level, SaferObjectSingleIdentification, &Delete, sizeof(Delete));
    ok(!Ret && GetLastError() == ERROR_NOT_FOUND, "deleting a missing rule: ret %d, error %lu\n", Ret, GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pSaferGetLevelInformation(Level, SaferObjectSingleIdentification, Rule, sizeof(Buffer), &Size);
    ok(!Ret && GetLastError() == ERROR_NOT_FOUND, "querying a missing rule: ret %d, error %lu\n", Ret, GetLastError());

    pSaferCloseLevel(Level);
}

static void
TestPolicyRoundTrip(void)
{
    DWORD Value, Size;
    BOOL Ret;

    Value = SAFER_LEVELID_NORMALUSER;
    Ret = pSaferSetPolicyInformation(SAFER_SCOPEID_USER, SaferPolicyDefaultLevel, sizeof(Value), &Value, NULL);
    ok(Ret, "setting the user default level failed: %lu\n", GetLastError());
    Value = 0xdeadbeef;
    ok(GetPolicyDword(L"DefaultLevel", &Value) && Value == SAFER_LEVELID_NORMALUSER, "DefaultLevel = 0x%lx\n", Value);
    Value = 0xdeadbeef;
    Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_USER, SaferPolicyDefaultLevel, sizeof(Value), &Value, &Size, NULL);
    ok(Ret && Value == SAFER_LEVELID_NORMALUSER, "user default level: ret %d, value 0x%lx\n", Ret, Value);
    DeletePolicyValue(L"DefaultLevel");

    Value = 0x123;
    SetLastError(0xdeadbeef);
    Ret = pSaferSetPolicyInformation(SAFER_SCOPEID_USER, SaferPolicyDefaultLevel, sizeof(Value), &Value, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "invalid default level: ret %d, error %lu\n",
       Ret, GetLastError());

    Value = 1;
    SetLastError(0xdeadbeef);
    Ret = pSaferSetPolicyInformation(SAFER_SCOPEID_USER, SaferPolicyScopeFlags, sizeof(Value), &Value, NULL);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "user scope flags: ret %d, error %lu\n",
       Ret, GetLastError());

    Value = 1;
    Ret = pSaferSetPolicyInformation(SAFER_SCOPEID_USER, SaferPolicyEnableTransparentEnforcement, sizeof(Value),
                                     &Value, NULL);
    ok(Ret, "setting transparent enforcement failed: %lu\n", GetLastError());
    Value = 0xdeadbeef;
    ok(GetPolicyDword(L"TransparentEnabled", &Value) && Value == 1, "TransparentEnabled = %lu\n", Value);
    Value = 0xdeadbeef;
    Ret = pSaferGetPolicyInformation(SAFER_SCOPEID_USER, SaferPolicyEnableTransparentEnforcement, sizeof(Value),
                                     &Value, &Size, NULL);
    ok(Ret && Value == 1, "transparent enforcement: ret %d, value %lu\n", Ret, Value);
    DeletePolicyValue(L"TransparentEnabled");
}

static void
TestExecutableTypes(void)
{
    static const WCHAR Types[] = L"SAFERTEST\0.XYZ\0";
    HKEY Key;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, SAFER_POLICY_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL) !=
        ERROR_SUCCESS)
    {
        ok(0, "policy key not opened\n");
        return;
    }
    ok(RegSetValueExW(Key, L"ExecutableTypes", 0, REG_MULTI_SZ, (const BYTE *)Types, sizeof(Types)) == ERROR_SUCCESS,
       "ExecutableTypes not set\n");
    RegCloseKey(Key);

    ok(pSaferiIsExecutableFileType(L"C:\\a\\b.safertest", FALSE), "designated type not executable\n");
    ok(pSaferiIsExecutableFileType(L"b.xyz", TRUE), "designated type with a dot not executable\n");
    ok(!pSaferiIsExecutableFileType(L"b.exe", FALSE), "the designated list did not replace the defaults\n");

    DeletePolicyValue(L"ExecutableTypes");
    ok(pSaferiIsExecutableFileType(L"b.exe", FALSE), "defaults not back without the designated list\n");
}

static BOOL
PrepareTarget(void)
{
    WCHAR Temp[MAX_PATH];
    HANDLE File;
    DWORD Written;

    if (!GetTempPathW(ARRAYSIZE(Temp), Temp) || !GetLongPathNameW(Temp, TestDir, ARRAYSIZE(TestDir)))
        return FALSE;
    StringCchCatW(TestDir, ARRAYSIZE(TestDir), L"SaferRulesApitest");
    CreateDirectoryW(TestDir, NULL);
    TestPath(TargetPath, L"target.exe");

    File = CreateFileW(TargetPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(File, TargetContent, sizeof(TargetContent) - 1, &Written, NULL);
    CloseHandle(File);

    return Md5(TargetContent, sizeof(TargetContent) - 1, TargetMd5);
}

START_TEST(SaferRules)
{
    BOOL HasMarker;
    DWORD Marker = 1;
    LSTATUS Status;
    HKEY Key;

    if (!ResolveSafer())
    {
        skip("SAFER functions are not available\n");
        return;
    }

    if (PolicyKeyExists(HKEY_LOCAL_MACHINE, &HasMarker))
    {
        skip("a machine software restriction policy is configured\n");
        return;
    }

    if (PolicyKeyExists(HKEY_CURRENT_USER, &HasMarker))
    {
        if (!HasMarker)
        {
            skip("a user software restriction policy is configured\n");
            return;
        }
        RemoveTestPolicy();
    }

    if (!PrepareTarget())
    {
        skip("test image not created: %lu\n", GetLastError());
        return;
    }

    Status = RegCreateKeyExW(HKEY_CURRENT_USER, SAFER_POLICY_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Status != ERROR_SUCCESS)
    {
        skip("user policy key not writable: %ld\n", Status);
        DeleteFileW(TargetPath);
        RemoveDirectoryW(TestDir);
        return;
    }
    RegSetValueExW(Key, TEST_MARKER, 0, REG_DWORD, (const BYTE *)&Marker, sizeof(Marker));
    RegCloseKey(Key);

    TestPathRules();
    TestHashRules();
    TestZoneRules();
    TestDefaultLevel();
    TestRuleRoundTrip();
    TestPolicyRoundTrip();
    TestExecutableTypes();

    RemoveTestPolicy();
    DeleteFileW(TargetPath);
    RemoveDirectoryW(TestDir);
}
