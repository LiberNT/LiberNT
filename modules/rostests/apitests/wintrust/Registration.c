/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test what the wintrust registration and unregistration entry points add and remove
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>

typedef HRESULT (WINAPI *PFN_REGISTER)(VOID);

#define CRYPTO_KEY L"SOFTWARE\\Microsoft\\Cryptography\\"

static const WCHAR FinalPolicyKey[] = CRYPTO_KEY L"Providers\\Trust\\FinalPolicy\\{00AAC56B-CD44-11D0-8CC2-00C04FC295EE}";
static const WCHAR SipVerifyKey[] = CRYPTO_KEY L"OID\\EncodingType 0\\CryptSIPDllVerifyIndirectData\\{C689AAB8-8E78-11D0-8C47-00C04FC295EE}";
static const WCHAR SipGetKey[] = CRYPTO_KEY L"OID\\EncodingType 0\\CryptSIPDllGetSignedDataMsg\\{C689AAB8-8E78-11D0-8C47-00C04FC295EE}";
static const WCHAR EncodeKey[] = CRYPTO_KEY L"OID\\EncodingType 1\\CryptDllEncodeObject\\1.3.6.1.4.1.311.2.1.4";
static const WCHAR UsageKey[] = CRYPTO_KEY L"Providers\\Trust\\Usages\\1.3.6.1.5.5.7.3.3";
static const WCHAR GetCapsKey[] = CRYPTO_KEY L"OID\\EncodingType 0\\CryptSIPDllGetCaps\\";
static const WCHAR *const CapsSubjects[] =
{
    L"{9BA61D3F-E73A-11D0-8CD2-00C04FC295EE}", L"{C689AAB8-8E78-11D0-8C47-00C04FC295EE}",
    L"{C689AAB9-8E78-11D0-8C47-00C04FC295EE}", L"{C689AABA-8E78-11D0-8C47-00C04FC295EE}",
    L"{DE351A42-8E59-11D0-8C47-00C04FC295EE}", L"{DE351A43-8E59-11D0-8C47-00C04FC295EE}",
};
static BOOL CapsPresent[RTL_NUMBER_OF(CapsSubjects)];

static
BOOL
KeyExists(const WCHAR *Path)
{
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key))
        return FALSE;
    RegCloseKey(Key);
    return TRUE;
}

static
BOOL
CapsKeyExists(DWORD Index)
{
    WCHAR Path[MAX_PATH];

    wcscpy(Path, GetCapsKey);
    wcscat(Path, CapsSubjects[Index]);
    return KeyExists(Path);
}

static
VOID
RestoreCapsKeys(VOID)
{
    static const WCHAR Dll[] = L"WINTRUST.DLL", Function[] = L"CryptSIPGetCaps";
    WCHAR Path[MAX_PATH];
    DWORD i;
    HKEY Key;

    for (i = 0; i < RTL_NUMBER_OF(CapsSubjects); i++)
    {
        wcscpy(Path, GetCapsKey);
        wcscat(Path, CapsSubjects[i]);
        if (!CapsPresent[i] || RegCreateKeyExW(HKEY_LOCAL_MACHINE, Path, 0, NULL, 0, KEY_WRITE, NULL, &Key, NULL))
            continue;
        RegSetValueExW(Key, L"Dll", 0, REG_SZ, (const BYTE *)Dll, sizeof(Dll));
        RegSetValueExW(Key, L"FuncName", 0, REG_SZ, (const BYTE *)Function, sizeof(Function));
        RegCloseKey(Key);
    }
}

static
VOID
CheckState(const char *When, BOOL FinalPolicy, BOOL Sip, BOOL Encode)
{
    ok(KeyExists(FinalPolicyKey) == FinalPolicy, "%s: final policy key %s\n", When, FinalPolicy ? "missing" : "present");
    ok(KeyExists(SipVerifyKey) == Sip, "%s: SIP verify key %s\n", When, Sip ? "missing" : "present");
    ok(KeyExists(SipGetKey) == Sip, "%s: SIP get key %s\n", When, Sip ? "missing" : "present");
    ok(KeyExists(EncodeKey) == Encode, "%s: encoder key %s\n", When, Encode ? "missing" : "present");
    ok(KeyExists(UsageKey), "%s: usage key missing\n", When);
}

static
VOID
Call(HMODULE Wintrust, const char *Name)
{
    PFN_REGISTER Function = (PFN_REGISTER)GetProcAddress(Wintrust, Name);

    ok(Function != NULL, "%s is not exported\n", Name);
    if (Function)
        ok_hex(Function(), S_OK);
}

START_TEST(Registration)
{
    HMODULE Wintrust = GetModuleHandleW(L"wintrust.dll");
    HKEY Key;
    DWORD i;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, CRYPTO_KEY L"Providers\\Trust", 0, NULL, 0, KEY_WRITE, NULL, &Key, NULL))
    {
        skip("The test needs administrator rights\n");
        return;
    }
    RegCloseKey(Key);

    for (i = 0; i < RTL_NUMBER_OF(CapsSubjects); i++)
        CapsPresent[i] = CapsKeyExists(i);

    CheckState("before", TRUE, TRUE, TRUE);
    Call(Wintrust, "mssip32DllUnregisterServer");
    CheckState("mssip32 unregistered", TRUE, FALSE, TRUE);
    ok(!CapsKeyExists(1), "mssip32 unregistered: PE capabilities key present\n");
    Call(Wintrust, "mssip32DllRegisterServer");
    CheckState("mssip32 registered", TRUE, TRUE, TRUE);
    ok(!CapsKeyExists(1), "mssip32 registered: PE capabilities key present\n");
    Call(Wintrust, "SoftpubDllUnregisterServer");
    CheckState("softpub unregistered", FALSE, TRUE, TRUE);
    Call(Wintrust, "mscat32DllUnregisterServer");
    CheckState("mscat32 unregistered", FALSE, TRUE, TRUE);
    Call(Wintrust, "DllUnregisterServer");
    CheckState("wintrust unregistered", FALSE, FALSE, FALSE);
    Call(Wintrust, "DllRegisterServer");
    CheckState("wintrust registered", TRUE, TRUE, TRUE);
    ok(!CapsKeyExists(1), "wintrust registered: PE capabilities key present\n");

    RestoreCapsKeys();
}
