/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CertNameToStrW renders an attribute without an X.500 key name as OID.<oid>
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <wincrypt.h>

static void CheckName(const CERT_NAME_BLOB *Blob, DWORD StrType, PCWSTR Expected)
{
    WCHAR Text[256];
    DWORD Needed, Written;

    Needed = CertNameToStrW(X509_ASN_ENCODING, (PCERT_NAME_BLOB)Blob, StrType, NULL, 0);
    ok(Needed == wcslen(Expected) + 1, "type 0x%lx: %lu characters needed, expected %Iu\n", StrType, Needed,
       wcslen(Expected) + 1);
    Written = CertNameToStrW(X509_ASN_ENCODING, (PCERT_NAME_BLOB)Blob, StrType, Text, ARRAYSIZE(Text));
    ok(Written == Needed, "type 0x%lx: %lu characters written, %lu needed\n", StrType, Written, Needed);
    ok(wcscmp(Text, Expected) == 0, "type 0x%lx: got \"%ls\", expected \"%ls\"\n", StrType, Text, Expected);
}

START_TEST(NameToStr)
{
    CERT_RDN_ATTR OrgUnit = { (LPSTR)szOID_ORGANIZATIONAL_UNIT_NAME, CERT_RDN_PRINTABLE_STRING,
                              { 9, (BYTE *)"Microsoft" } };
    CERT_RDN_ATTR Person[2] = {
        { (LPSTR)szOID_COMMON_NAME, CERT_RDN_PRINTABLE_STRING, { 8, (BYTE *)"Joe Cool" } },
        { (LPSTR)szOID_TITLE, CERT_RDN_PRINTABLE_STRING, { 10, (BYTE *)"Programmer" } },
    };
    CERT_RDN_ATTR Unknown = { (LPSTR)"1.2.3.4.5.6", CERT_RDN_PRINTABLE_STRING, { 7, (BYTE *)"Unknown" } };
    CERT_RDN Rdns[3] = { { 1, &OrgUnit }, { 2, Person }, { 1, &Unknown } };
    CERT_NAME_INFO Info = { 3, Rdns };
    CERT_NAME_BLOB Blob = { 0, NULL };
    BOOL Ret;

    Ret = CryptEncodeObjectEx(X509_ASN_ENCODING, X509_NAME, &Info, CRYPT_ENCODE_ALLOC_FLAG, NULL, &Blob.pbData,
                              &Blob.cbData);
    ok(Ret, "CryptEncodeObjectEx failed with %lu\n", GetLastError());
    if (!Ret)
        return;

    CheckName(&Blob, CERT_X500_NAME_STR, L"OU=Microsoft, CN=Joe Cool + T=Programmer, OID.1.2.3.4.5.6=Unknown");
    CheckName(&Blob, CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
              L"OID.1.2.3.4.5.6=Unknown, CN=Joe Cool + T=Programmer, OU=Microsoft");
    CheckName(&Blob, CERT_OID_NAME_STR, L"2.5.4.11=Microsoft, 2.5.4.3=Joe Cool + 2.5.4.12=Programmer, 1.2.3.4.5.6=Unknown");
    CheckName(&Blob, CERT_SIMPLE_NAME_STR, L"Microsoft, Joe Cool + Programmer, Unknown");

    LocalFree(Blob.pbData);
}
