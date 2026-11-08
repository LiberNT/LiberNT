/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the wintrust helper, default usage and backup pause functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>

typedef BOOL (WINAPI *PFN_PAUSE)(DWORD, BOOL);
typedef BOOL (WINAPI *PFN_TRUSTDB_EX)(HWND, DWORD, PVOID *);
typedef BOOL (WINAPI *PFN_DEFAULT_USAGE)(DWORD, const char *, CRYPT_PROVIDER_DEFUSAGE *);
typedef BOOL (WINAPI *PFN_SELF_SIGNED)(DWORD, CERT_INFO *);
typedef LONG (WINAPI *PFN_FILE_HASH)(LPCWSTR, DWORD, PVOID, BYTE *, DWORD *, ALG_ID *);

static
VOID
TestPauseServiceForBackup(HMODULE Wintrust)
{
    PFN_PAUSE Pause = (PFN_PAUSE)GetProcAddress(Wintrust, "CryptCATAdminPauseServiceForBackup");

    ok(Pause != NULL, "CryptCATAdminPauseServiceForBackup is not exported\n");
    if (!Pause)
        return;
    SetLastError(0xdeadbeef);
    ok_int(Pause(0, FALSE), TRUE);
    ok_hex(GetLastError(), ERROR_SUCCESS);
    SetLastError(0xdeadbeef);
    ok_int(Pause(0, TRUE), TRUE);
    ok_hex(GetLastError(), ERROR_SUCCESS);
    SetLastError(0xdeadbeef);
    ok_int(Pause(1, FALSE), TRUE);
    ok_hex(GetLastError(), ERROR_SUCCESS);
}

static
VOID
TestTrustDbDialog(HMODULE Wintrust)
{
    PFN_TRUSTDB_EX TrustDb = (PFN_TRUSTDB_EX)GetProcAddress(Wintrust, "OpenPersonalTrustDBDialogEx");

    ok(TrustDb != NULL, "OpenPersonalTrustDBDialogEx is not exported\n");
    if (!TrustDb)
        return;
    SetLastError(0xdeadbeef);
    ok_int(TrustDb(NULL, WT_TRUSTDBDIALOG_NO_UI_FLAG, NULL), TRUE);
    ok_hex(GetLastError(), 0xdeadbeef);
}

static
VOID
TestHttpsUsage(PFN_DEFAULT_USAGE GetDefault, const char *Oid, DWORD AuthType)
{
    static const GUID HttpsAction = HTTPSPROV_ACTION;
    CRYPT_PROVIDER_DEFUSAGE Usage;
    HTTPSPolicyCallbackData *Policy;

    ZeroMemory(&Usage, sizeof(Usage));
    Usage.cbStruct = sizeof(Usage);
    SetLastError(0xdeadbeef);
    ok_int(GetDefault(DWACTION_ALLOCANDFILL, Oid, &Usage), TRUE);
    ok(IsEqualGUID(&Usage.gActionID, &HttpsAction), "%s: unexpected action %08lx\n", Oid, Usage.gActionID.Data1);
    ok(Usage.pDefSIPClientData == NULL, "%s: SIP data %p\n", Oid, Usage.pDefSIPClientData);
    Policy = Usage.pDefPolicyCallbackData;
    ok(Policy != NULL, "%s: no policy data\n", Oid);
    if (Policy)
    {
        ok_long(Policy->cbStruct, sizeof(*Policy));
        ok_long(Policy->dwAuthType, AuthType);
        ok_hex(Policy->fdwChecks, 0x1000);
        ok(Policy->pwszServerName == NULL, "%s: server name %p\n", Oid, Policy->pwszServerName);
    }
    SetLastError(0xdeadbeef);
    ok_int(GetDefault(DWACTION_FREE, Oid, &Usage), TRUE);
    ok(Usage.pDefPolicyCallbackData == Policy, "%s: policy pointer changed\n", Oid);
}

static LONG UsageAllocCalls, UsageFreeCalls;

__declspec(dllexport)
BOOL
WintrustTestUsageAlloc(const char *Oid, CRYPT_PROVIDER_DEFUSAGE *Usage)
{
    UsageAllocCalls++;
    Usage->pDefPolicyCallbackData = (PVOID)(ULONG_PTR)0x1234;
    return TRUE;
}

__declspec(dllexport)
BOOL
WintrustTestUsageFree(const char *Oid, CRYPT_PROVIDER_DEFUSAGE *Usage)
{
    UsageFreeCalls++;
    return TRUE;
}

static
VOID
TestUsageCallbacks(PFN_DEFAULT_USAGE GetDefault)
{
    static const char Oid[] = "1.3.6.1.4.1.99999.7";
    GUID Action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    CRYPT_PROVIDER_REGDEFUSAGE Register;
    CRYPT_PROVIDER_DEFUSAGE Usage;
    WCHAR Self[MAX_PATH];
    HKEY Key;

    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));
    ZeroMemory(&Register, sizeof(Register));
    Register.cbStruct = sizeof(Register);
    Register.pgActionID = &Action;
    Register.pwszDllName = Self;
    Register.pwszLoadCallbackDataFunctionName = (char *)"WintrustTestUsageAlloc";
    Register.pwszFreeCallbackDataFunctionName = (char *)"WintrustTestUsageFree";
    if (!WintrustAddDefaultForUsage(Oid, &Register))
    {
        skip("Cannot register the test usage (%lu)\n", GetLastError());
        return;
    }

    ZeroMemory(&Usage, sizeof(Usage));
    Usage.cbStruct = sizeof(Usage);
    ok_int(GetDefault(DWACTION_ALLOCANDFILL, Oid, &Usage), TRUE);
    ok(Usage.pDefPolicyCallbackData == (PVOID)(ULONG_PTR)0x1234, "policy data %p\n", Usage.pDefPolicyCallbackData);
    ok_long(UsageAllocCalls, 1);
    ok_long(UsageFreeCalls, 0);
    ok_int(GetDefault(3, Oid, &Usage), TRUE);
    ok_int(GetDefault(0, Oid, &Usage), TRUE);
    ok_long(UsageAllocCalls, 1);
    ok_long(UsageFreeCalls, 0);
    ok_int(GetDefault(DWACTION_FREE, Oid, &Usage), TRUE);
    ok_long(UsageAllocCalls, 1);
    ok_long(UsageFreeCalls, 1);

    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography\\Providers\\Trust\\Usages", 0,
                       KEY_ALL_ACCESS, &Key))
    {
        RegDeleteKeyW(Key, L"1.3.6.1.4.1.99999.7");
        RegCloseKey(Key);
    }
}

static
VOID
TestDefaultForUsage(HMODULE Wintrust)
{
    static const GUID GenericVerify = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    PFN_DEFAULT_USAGE GetDefault = (PFN_DEFAULT_USAGE)GetProcAddress(Wintrust, "WintrustGetDefaultForUsage");
    CRYPT_PROVIDER_DEFUSAGE Usage;
    PVOID Pattern;

    FillMemory(&Pattern, sizeof(Pattern), 0xcc);
    ok(GetDefault != NULL, "WintrustGetDefaultForUsage is not exported\n");
    if (!GetDefault)
        return;

    FillMemory(&Usage, sizeof(Usage), 0xcc);
    Usage.cbStruct = sizeof(Usage);
    SetLastError(0xdeadbeef);
    ok_int(GetDefault(DWACTION_ALLOCANDFILL, szOID_PKIX_KP_CODE_SIGNING, &Usage), TRUE);
    ok_hex(GetLastError(), 0xdeadbeef);
    ok(IsEqualGUID(&Usage.gActionID, &GenericVerify), "unexpected action %08lx\n", Usage.gActionID.Data1);
    ok(Usage.pDefPolicyCallbackData == Pattern, "policy data %p\n", Usage.pDefPolicyCallbackData);
    ok(Usage.pDefSIPClientData == Pattern, "SIP data %p\n", Usage.pDefSIPClientData);
    SetLastError(0xdeadbeef);
    ok_int(GetDefault(DWACTION_FREE, szOID_PKIX_KP_CODE_SIGNING, &Usage), TRUE);
    ok_hex(GetLastError(), 0xdeadbeef);

    ZeroMemory(&Usage, sizeof(Usage));
    Usage.cbStruct = sizeof(Usage);
    SetLastError(0xdeadbeef);
    ok_int(GetDefault(DWACTION_ALLOCANDFILL, "1.2.3.4.5", &Usage), FALSE);
    ok_hex(GetLastError(), 0xdeadbeef);

    Usage.cbStruct = 0;
    SetLastError(0xdeadbeef);
    ok_int(GetDefault(DWACTION_ALLOCANDFILL, szOID_PKIX_KP_CODE_SIGNING, &Usage), FALSE);
    ok_hex(GetLastError(), ERROR_INVALID_PARAMETER);

    Usage.cbStruct = sizeof(Usage);
    SetLastError(0xdeadbeef);
    ok_int(GetDefault(3, szOID_PKIX_KP_CODE_SIGNING, &Usage), TRUE);
    ok_hex(GetLastError(), 0xdeadbeef);

    TestHttpsUsage(GetDefault, szOID_PKIX_KP_SERVER_AUTH, AUTHTYPE_SERVER);
    TestHttpsUsage(GetDefault, szOID_PKIX_KP_CLIENT_AUTH, AUTHTYPE_CLIENT);
    TestHttpsUsage(GetDefault, szOID_SERVER_GATED_CRYPTO, AUTHTYPE_SERVER);
    TestHttpsUsage(GetDefault, szOID_SGC_NETSCAPE, AUTHTYPE_SERVER);
    TestUsageCallbacks(GetDefault);
}

static
VOID
TestSelfSigned(HMODULE Wintrust)
{
    PFN_SELF_SIGNED SelfSigned = (PFN_SELF_SIGNED)GetProcAddress(Wintrust, "WTHelperCertIsSelfSigned");
    BYTE NameData[128], IssuerData[128];
    CERT_NAME_BLOB Name = { sizeof(NameData), NameData }, Issuer = { sizeof(IssuerData), IssuerData };
    PCCERT_CONTEXT Cert;
    CERT_INFO Info;

    ok(SelfSigned != NULL, "WTHelperCertIsSelfSigned is not exported\n");
    if (!SelfSigned)
        return;
    if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=LiberNT self signed", CERT_X500_NAME_STR, NULL, NameData, &Name.cbData, NULL) ||
        !CertStrToNameW(X509_ASN_ENCODING, L"CN=LiberNT issuer", CERT_X500_NAME_STR, NULL, IssuerData, &Issuer.cbData, NULL))
    {
        skip("CertStrToNameW failed\n");
        return;
    }
    Cert = CertCreateSelfSignCertificate(0, &Name, 0, NULL, NULL, NULL, NULL, NULL);
    ok(Cert != NULL, "CertCreateSelfSignCertificate failed with %lu\n", GetLastError());
    if (!Cert)
        return;

    ok_int(SelfSigned(X509_ASN_ENCODING, Cert->pCertInfo), TRUE);
    Info = *Cert->pCertInfo;
    Info.Issuer = Issuer;
    ok_int(SelfSigned(X509_ASN_ENCODING, &Info), FALSE);
    CertFreeCertificateContext(Cert);
}

static
VOID
TestFileHashUnsigned(HMODULE Wintrust)
{
    PFN_FILE_HASH FileHash = (PFN_FILE_HASH)GetProcAddress(Wintrust, "WTHelperGetFileHash");
    WCHAR Path[MAX_PATH];
    BYTE Hash[64];
    DWORD Size;
    ALG_ID Algorithm;

    ok(FileHash != NULL, "WTHelperGetFileHash is not exported\n");
    if (!FileHash)
        return;

    GetModuleFileNameW(NULL, Path, RTL_NUMBER_OF(Path));
    Size = sizeof(Hash);
    ok_hex(FileHash(Path, 0, NULL, Hash, &Size, &Algorithm), TRUST_E_NOSIGNATURE);
    ok_long(Size, 0);
}

static
VOID
TestKnownUsages(VOID)
{
    static const struct { const char *Oid; const WCHAR *Name; } Expected[] = {
        { szOID_PKIX_KP_SERVER_AUTH, L"Server Authentication" },
        { "1.3.6.1.4.1.311.21.5", L"Private Key Archival" },
        { "2.5.29.37.0", L"Any Purpose" },
        { "1.3.6.1.4.1.311.61.1.1", L"Kernel Mode Code Signing" },
        { "1.3.6.1.4.1.311.92.1.1", L"Windows Hello Recovery Key Encryption" },
    };
    typedef BOOL (WINAPI *PFN_KNOWN_USAGES)(DWORD, PCCRYPT_OID_INFO **);
    PFN_KNOWN_USAGES KnownUsages = (PFN_KNOWN_USAGES)GetProcAddress(GetModuleHandleW(L"wintrust.dll"),
                                                                    "WTHelperGetKnownUsages");
    PCCRYPT_OID_INFO *Usages = NULL;
    DWORD Count, i, j, Found;

    ok(KnownUsages != NULL, "WTHelperGetKnownUsages is not exported\n");
    if (!KnownUsages)
        return;
    ok(KnownUsages(1, &Usages), "WTHelperGetKnownUsages failed %lu\n", GetLastError());
    if (!Usages)
        return;
    for (Count = 0; Usages[Count]; Count++)
    {
        ok_long(Usages[Count]->dwGroupId, CRYPT_ENHKEY_USAGE_OID_GROUP_ID);
        for (j = 0; j < Count; j++)
            ok(strcmp(Usages[j]->pszOID, Usages[Count]->pszOID), "%s is listed twice\n", Usages[Count]->pszOID);
    }
    ok_long(Count, 66);
    for (i = 0; i < RTL_NUMBER_OF(Expected); i++)
    {
        for (j = 0, Found = 0; j < Count; j++)
            if (!strcmp(Usages[j]->pszOID, Expected[i].Oid))
            {
                Found++;
                ok(!wcscmp(Usages[j]->pwszName, Expected[i].Name), "%s is named %ls\n", Expected[i].Oid, Usages[j]->pwszName);
            }
        ok_long(Found, 1);
    }
    KnownUsages(2, &Usages);
}

START_TEST(TrustHelpers)
{
    HMODULE Wintrust = GetModuleHandleW(L"wintrust.dll");

    TestPauseServiceForBackup(Wintrust);
    TestTrustDbDialog(Wintrust);
    TestDefaultForUsage(Wintrust);
    TestSelfSigned(Wintrust);
    TestFileHashUnsigned(Wintrust);
    TestKnownUsages();
}
