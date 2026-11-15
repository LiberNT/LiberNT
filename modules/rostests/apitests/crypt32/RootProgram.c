/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the trusted roots Windows requires and a root program NotBefore cut-off
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>

/* Microsoft KB 293781: trusted root certificates required by Windows 10 and later */
static const char *RequiredRoots[] =
{
    "A43489159A520F0D93D032CCAF37E7FE20A8B419", /* Microsoft Root Authority */
    "CDD4EEAE6000AC7F40C3802C171E30148030C072", /* Microsoft Root Certificate Authority */
    "3B1EFD3A66EA28B16697394703A72CA340A05BD5", /* Microsoft Root Certificate Authority 2010 */
    "8F43288AD272F3103B6FB1428485EA3014C0BCFE", /* Microsoft Root Certificate Authority 2011 */
    "245C97DF7514E7CF2DF8BE72AE957B9E04741E85", /* Copyright (c) 1997 Microsoft Corp. */
    "7F88CD7223F3C813818C994614A89C99FA3B5247", /* Microsoft Authenticode(tm) Root Authority */
    "18F7C1FCC3090203FD5BAA2F861A754976C8DD25", /* NO LIABILITY ACCEPTED, (c)97 VeriSign, Inc. */
    "BE36A4562FB2EE05DBB3D32323ADF445084ED656", /* Thawte Timestamping CA */
    "92B46C76E13054E104F230517E6E504D43AB10B5", /* Symantec Enterprise Mobile Root for Microsoft */
    "06F1AA330B927B753A40E68CDF22E34BCBEF3352", /* Microsoft ECC Product Root Certificate Authority 2018 */
    "31F9FC8BA3805986B721EA7295C65B3A44534274", /* Microsoft ECC TS Root Certificate Authority 2018 */
    "0119E81BE9A14CD8E22F40AC118C687ECBA3F4D8", /* Microsoft Time Stamp Root Certificate Authority 2014 */
};

static PCCERT_CONTEXT
FindRoot(
    _In_ HCERTSTORE Store,
    _In_ const char *Fingerprint)
{
    BYTE Hash[20];
    CRYPT_HASH_BLOB Blob = { sizeof(Hash), Hash };
    ULONG Index;

    for (Index = 0; Index < sizeof(Hash); Index++)
        sscanf(Fingerprint + 2 * Index, "%2hhx", &Hash[Index]);
    return CertFindCertificateInStore(Store, X509_ASN_ENCODING, 0, CERT_FIND_SHA1_HASH, &Blob, NULL);
}

START_TEST(RootProgram)
{
    /* thawte Primary Root CA: NotBefore 2021-02-01 in Microsoft's authroot.stl */
    static const FILETIME ThawteCutOff = { 0x2f03c000, 0x01d6f82d };
    PCCERT_CONTEXT Cert;
    HCERTSTORE Root;
    FILETIME CutOff;
    DWORD Size;
    ULONG Index;

    Root = CertOpenSystemStoreA(0, "ROOT");
    ok(Root != NULL, "CertOpenSystemStore failed: %lu\n", GetLastError());
    if (Root == NULL)
        return;

    for (Index = 0; Index < ARRAYSIZE(RequiredRoots); Index++)
    {
        Cert = FindRoot(Root, RequiredRoots[Index]);
        ok(Cert != NULL, "Required root %s is missing\n", RequiredRoots[Index]);
        if (Cert != NULL)
            CertFreeCertificateContext(Cert);
    }

    Cert = FindRoot(Root, "91C6D6EE3E8AC86384E548C299295C756C817B81");
    ok(Cert != NULL, "thawte Primary Root CA is missing\n");
    if (Cert != NULL)
    {
        Size = sizeof(CutOff);
        ok(CertGetCertificateContextProperty(Cert, CERT_NOT_BEFORE_FILETIME_PROP_ID, &CutOff, &Size),
           "No NotBefore time: %lu\n", GetLastError());
        ok(Size == sizeof(CutOff) && !memcmp(&CutOff, &ThawteCutOff, sizeof(CutOff)),
           "NotBefore time %08lx%08lx\n", CutOff.dwHighDateTime, CutOff.dwLowDateTime);
        Size = 0;
        ok(CertGetCertificateContextProperty(Cert, CERT_NOT_BEFORE_ENHKEY_USAGE_PROP_ID, NULL, &Size) && Size != 0,
           "No NotBefore usages: %lu\n", GetLastError());
        CertFreeCertificateContext(Cert);
    }
    CertCloseStore(Root, 0);
}
