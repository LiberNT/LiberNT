/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the wintrust ASN.1 codecs registered for Authenticode attributes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <stdio.h>

typedef BOOL (WINAPI *PFN_ENCODE)(DWORD, LPCSTR, const void *, BYTE *, DWORD *);
typedef BOOL (WINAPI *PFN_DECODE)(DWORD, LPCSTR, const BYTE *, DWORD, DWORD, void *, DWORD *);

static HMODULE Wintrust;

static
BOOL
Encode(PCSTR Name, LPCSTR Type, const void *Info, const BYTE *Expected, DWORD ExpectedSize, BYTE *Decoded, DWORD DecodedSize)
{
    CHAR EncodeName[64], DecodeName[64];
    PFN_ENCODE pEncode;
    PFN_DECODE pDecode;
    BYTE Buffer[256];
    DWORD Size = sizeof(Buffer);

    sprintf(EncodeName, "WVTAsn1%sEncode", Name);
    sprintf(DecodeName, "WVTAsn1%sDecode", Name);
    pEncode = (PFN_ENCODE)GetProcAddress(Wintrust, EncodeName);
    pDecode = (PFN_DECODE)GetProcAddress(Wintrust, DecodeName);
    ok(pEncode && pDecode, "%s: codec not exported\n", Name);
    if (!pEncode || !pDecode)
        return FALSE;

    ok(pEncode(X509_ASN_ENCODING, Type, Info, Buffer, &Size), "%s: encode failed %lu\n", Name, GetLastError());
    ok(Size == ExpectedSize && !memcmp(Buffer, Expected, Size), "%s: unexpected encoding of %lu bytes\n", Name, Size);

    Size = DecodedSize;
    ok(pDecode(X509_ASN_ENCODING, Type, Expected, ExpectedSize, 0, Decoded, &Size), "%s: decode failed %lu\n",
       Name, GetLastError());
    return TRUE;
}

static
VOID
TestAgencyInfo(VOID)
{
    static const BYTE Policy[] = {
        0x30,0x3b,0xa0,0x1f,0x80,0x1d,'h','t','t','p',':','/','/','w','w','w','.','e','x','a','m','p','l','e','.',
        'c','o','m','/','p','o','l','i','c','y',0xa1,0x18,0x80,0x16,0,'P',0,'o',0,'l',0,'i',0,'c',0,'y',0,' ',
        0,'t',0,'e',0,'x',0,'t' };
    static const BYTE Empty[] = { 0x30, 0x00 };
    static const BYTE Logo[] = {
        0x30,0x26,0xa2,0x0e,0xa0,0x0c,0x80,0x0a,'h','t','t','p',':','/','/','i','m','g',0xa3,0x14,0xa2,0x12,0x80,
        0x10,0,'l',0,'o',0,'g',0,'o',0,'.',0,'b',0,'m',0,'p' };
    static const BYTE Blobs[] = {
        0x30,0x14,0xa2,0x12,0x81,0x03,0x01,0x02,0x03,0x82,0x02,0x04,0x05,0x83,0x01,0x06,0x84,0x04,0x07,0x08,0x09,0x0a };
    static const BYTE Bitmap[] = { 1, 2, 3 }, Metafile[] = { 4, 5 }, EnhancedMetafile[] = { 6 }, GifFile[] = { 7, 8, 9, 10 };
    SPC_LINK Url = { SPC_URL_LINK_CHOICE }, File = { SPC_FILE_LINK_CHOICE }, ImageUrl = { SPC_URL_LINK_CHOICE };
    SPC_SP_AGENCY_INFO Info, *Decoded;
    SPC_IMAGE Image;
    BYTE Buffer[512];

    Url.pwszUrl = L"http://www.example.com/policy";
    File.pwszFile = L"logo.bmp";
    ImageUrl.pwszUrl = L"http://img";

    memset(&Info, 0, sizeof(Info));
    Info.pPolicyInformation = &Url;
    Info.pwszPolicyDisplayText = L"Policy text";
    Decoded = (SPC_SP_AGENCY_INFO *)Buffer;
    if (Encode("SpcSpAgencyInfo", SPC_SP_AGENCY_INFO_STRUCT, &Info, Policy, sizeof(Policy), Buffer, sizeof(Buffer)))
    {
        ok(Decoded->pPolicyInformation && Decoded->pPolicyInformation->dwLinkChoice == SPC_URL_LINK_CHOICE &&
           !wcscmp(Decoded->pPolicyInformation->pwszUrl, L"http://www.example.com/policy"), "wrong policy link\n");
        ok(Decoded->pwszPolicyDisplayText && !wcscmp(Decoded->pwszPolicyDisplayText, L"Policy text"), "wrong text\n");
        ok(!Decoded->pLogoImage && !Decoded->pLogoLink, "unexpected logo\n");
    }

    memset(&Info, 0, sizeof(Info));
    if (Encode("SpcSpAgencyInfo", SPC_SP_AGENCY_INFO_STRUCT, &Info, Empty, sizeof(Empty), Buffer, sizeof(Buffer)))
        ok(!Decoded->pPolicyInformation && !Decoded->pwszPolicyDisplayText && !Decoded->pLogoImage && !Decoded->pLogoLink,
           "empty agency info decoded with members\n");

    memset(&Image, 0, sizeof(Image));
    Image.pImageLink = &ImageUrl;
    Info.pLogoImage = &Image;
    Info.pLogoLink = &File;
    if (Encode("SpcSpAgencyInfo", SPC_SP_AGENCY_INFO_STRUCT, &Info, Logo, sizeof(Logo), Buffer, sizeof(Buffer)))
    {
        ok(Decoded->pLogoImage && Decoded->pLogoImage->pImageLink &&
           Decoded->pLogoImage->pImageLink->dwLinkChoice == SPC_URL_LINK_CHOICE &&
           !wcscmp(Decoded->pLogoImage->pImageLink->pwszUrl, L"http://img") && !Decoded->pLogoImage->Bitmap.cbData,
           "wrong logo image\n");
        ok(Decoded->pLogoLink && Decoded->pLogoLink->dwLinkChoice == SPC_FILE_LINK_CHOICE &&
           !wcscmp(Decoded->pLogoLink->pwszFile, L"logo.bmp"), "wrong logo link\n");
    }

    memset(&Image, 0, sizeof(Image));
    Image.Bitmap.cbData = sizeof(Bitmap);
    Image.Bitmap.pbData = (BYTE *)Bitmap;
    Image.Metafile.cbData = sizeof(Metafile);
    Image.Metafile.pbData = (BYTE *)Metafile;
    Image.EnhancedMetafile.cbData = sizeof(EnhancedMetafile);
    Image.EnhancedMetafile.pbData = (BYTE *)EnhancedMetafile;
    Image.GifFile.cbData = sizeof(GifFile);
    Image.GifFile.pbData = (BYTE *)GifFile;
    Info.pLogoLink = NULL;
    if (Encode("SpcSpAgencyInfo", SPC_SP_AGENCY_INFO_STRUCT, &Info, Blobs, sizeof(Blobs), Buffer, sizeof(Buffer)))
    {
        SPC_IMAGE *Logo = Decoded->pLogoImage;

        ok(Logo && !Logo->pImageLink, "wrong logo image\n");
        if (Logo)
        {
            ok(Logo->Bitmap.cbData == sizeof(Bitmap) && !memcmp(Logo->Bitmap.pbData, Bitmap, sizeof(Bitmap)),
               "wrong bitmap\n");
            ok(Logo->Metafile.cbData == sizeof(Metafile) && !memcmp(Logo->Metafile.pbData, Metafile, sizeof(Metafile)),
               "wrong metafile\n");
            ok(Logo->EnhancedMetafile.cbData == sizeof(EnhancedMetafile) &&
               !memcmp(Logo->EnhancedMetafile.pbData, EnhancedMetafile, sizeof(EnhancedMetafile)),
               "wrong enhanced metafile\n");
            ok(Logo->GifFile.cbData == sizeof(GifFile) && !memcmp(Logo->GifFile.pbData, GifFile, sizeof(GifFile)),
               "wrong gif file\n");
        }
    }
}

static
VOID
TestStatementType(VOID)
{
    static const BYTE Both[] = {
        0x30,0x18,0x06,0x0a,0x2b,0x06,0x01,0x04,0x01,0x82,0x37,0x02,0x01,0x15,0x06,0x0a,0x2b,0x06,0x01,0x04,0x01,
        0x82,0x37,0x02,0x01,0x16 };
    static const BYTE Empty[] = { 0x30, 0x00 };
    LPSTR Purposes[] = { SPC_INDIVIDUAL_SP_KEY_PURPOSE_OBJID, SPC_COMMERCIAL_SP_KEY_PURPOSE_OBJID };
    SPC_STATEMENT_TYPE Info = { 2, Purposes }, *Decoded;
    BYTE Buffer[256];

    Decoded = (SPC_STATEMENT_TYPE *)Buffer;
    if (Encode("SpcStatementType", SPC_STATEMENT_TYPE_STRUCT, &Info, Both, sizeof(Both), Buffer, sizeof(Buffer)))
        ok(Decoded->cKeyPurposeId == 2 && !strcmp(Decoded->rgpszKeyPurposeId[0], SPC_INDIVIDUAL_SP_KEY_PURPOSE_OBJID) &&
           !strcmp(Decoded->rgpszKeyPurposeId[1], SPC_COMMERCIAL_SP_KEY_PURPOSE_OBJID), "wrong statement types\n");

    Info.cKeyPurposeId = 0;
    Info.rgpszKeyPurposeId = NULL;
    if (Encode("SpcStatementType", SPC_STATEMENT_TYPE_STRUCT, &Info, Empty, sizeof(Empty), Buffer, sizeof(Buffer)))
        ok(Decoded->cKeyPurposeId == 0, "count %lu\n", Decoded->cKeyPurposeId);
}

static
VOID
TestSigInfo(VOID)
{
    static const BYTE Pe[] = {
        0x30,0x24,0x02,0x01,0x01,0x04,0x10,0xb8,0xaa,0x89,0xc6,0x78,0x8e,0xd0,0x11,0x8c,0x47,0x00,0xc0,0x4f,0xc2,
        0x95,0xee,0x02,0x01,0x00,0x02,0x01,0x00,0x02,0x01,0x00,0x02,0x01,0x00,0x02,0x01,0x00 };
    static const BYTE Flat[] = {
        0x30,0x24,0x02,0x01,0x02,0x04,0x10,0x42,0x1a,0x35,0xde,0x59,0x8e,0xd0,0x11,0x8c,0x47,0x00,0xc0,0x4f,0xc2,
        0x95,0xee,0x02,0x01,0x01,0x02,0x01,0x02,0x02,0x01,0x03,0x02,0x01,0x04,0x02,0x01,0x05 };
    static const GUID PeSubject = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    static const GUID FlatSubject = { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    SPC_SIGINFO Info, Decoded;

    memset(&Info, 0, sizeof(Info));
    Info.dwSipVersion = 1;
    Info.gSIPGuid = PeSubject;
    if (Encode("SpcSigInfo", SPC_SIGINFO_STRUCT, &Info, Pe, sizeof(Pe), (BYTE *)&Decoded, sizeof(Decoded)))
        ok(Decoded.dwSipVersion == 1 && IsEqualGUID(&Decoded.gSIPGuid, &PeSubject) && !Decoded.dwReserved5,
           "wrong sig info\n");

    Info.dwSipVersion = 2;
    Info.gSIPGuid = FlatSubject;
    Info.dwReserved1 = 1;
    Info.dwReserved2 = 2;
    Info.dwReserved3 = 3;
    Info.dwReserved4 = 4;
    Info.dwReserved5 = 5;
    if (Encode("SpcSigInfo", SPC_SIGINFO_STRUCT, &Info, Flat, sizeof(Flat), (BYTE *)&Decoded, sizeof(Decoded)))
        ok(!memcmp(&Decoded, &Info, sizeof(Info)), "sig info did not round trip\n");
}

static
VOID
TestMinimalCriteria(VOID)
{
    static const BYTE True[] = { 0x01, 0x01, 0xff };
    static const BYTE False[] = { 0x01, 0x01, 0x00 };
    BOOL Value, Decoded;

    Value = TRUE;
    if (Encode("SpcMinimalCriteriaInfo", SPC_MINIMAL_CRITERIA_STRUCT, &Value, True, sizeof(True), (BYTE *)&Decoded, sizeof(Decoded)))
        ok(Decoded == TRUE, "decoded %d\n", Decoded);
    Value = FALSE;
    if (Encode("SpcMinimalCriteriaInfo", SPC_MINIMAL_CRITERIA_STRUCT, &Value, False, sizeof(False), (BYTE *)&Decoded, sizeof(Decoded)))
        ok(Decoded == FALSE, "decoded %d\n", Decoded);
}

static
VOID
TestSealing(VOID)
{
    static const BYTE IntentEncoded[] = { 0x30, 0x06, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00 };
    static const BYTE IntentSealed[] = { 0x30, 0x09, 0x02, 0x04, 0x31, 0xa9, 0x00, 0x28, 0x01, 0x01, 0xff };
    static const BYTE SignatureEncoded[] = {
        0x30, 0x10, 0x02, 0x04, 0x31, 0xa9, 0x00, 0x28, 0x02, 0x02, 0x7f, 0xf6, 0x30, 0x02, 0x06, 0x00, 0x04, 0x00 };
    static const BYTE TimestampEncoded[] = { 0x30, 0x08, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x04, 0x00 };
    static const BYTE SignatureAlgorithm[] = {
        0x30, 0x1a, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
        0x01, 0x01, 0x0b, 0x05, 0x00, 0x04, 0x03, 0x01, 0x02, 0x03 };
    static const BYTE SignatureEmptyOid[] = {
        0x30, 0x11, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x30, 0x04, 0x06, 0x00, 0x05, 0x00, 0x04, 0x03, 0x01, 0x02, 0x03 };
    static const BYTE Digest[] = { 1, 2, 3 };
    INTENT_TO_SEAL_ATTRIBUTE Intent, *DecodedIntent;
    SEALING_SIGNATURE_ATTRIBUTE Signature, *DecodedSignature;
    SEALING_TIMESTAMP_ATTRIBUTE Timestamp, *DecodedTimestamp;
    BYTE Buffer[512];

    ZeroMemory(&Intent, sizeof(Intent));
    Intent.version = 1;
    FillMemory(Buffer, sizeof(Buffer), 0xcc);
    DecodedIntent = (INTENT_TO_SEAL_ATTRIBUTE *)Buffer;
    if (Encode("IntentToSealAttribute", INTENT_TO_SEAL_ATTRIBUTE_STRUCT, &Intent, IntentEncoded, sizeof(IntentEncoded),
               Buffer, sizeof(Buffer)))
    {
        ok(DecodedIntent->version == 1 && DecodedIntent->seal == FALSE, "intent %lu %u\n", DecodedIntent->version,
           DecodedIntent->seal);
        ok_hex(Buffer[FIELD_OFFSET(INTENT_TO_SEAL_ATTRIBUTE, seal) + 1], 0xcc);
    }
    Intent.version = 0x31a90028;
    Intent.seal = TRUE;
    FillMemory(Buffer, sizeof(Buffer), 0xcc);
    if (Encode("IntentToSealAttribute", INTENT_TO_SEAL_ATTRIBUTE_STRUCT, &Intent, IntentSealed, sizeof(IntentSealed),
               Buffer, sizeof(Buffer)))
        ok(DecodedIntent->version == 0x31a90028 && DecodedIntent->seal == TRUE, "sealed intent %#lx %u\n",
           DecodedIntent->version, DecodedIntent->seal);

    ZeroMemory(&Signature, sizeof(Signature));
    Signature.version = 0x31a90028;
    Signature.signerIndex = 0x7ff6;
    DecodedSignature = (SEALING_SIGNATURE_ATTRIBUTE *)Buffer;
    if (Encode("SealingSignatureAttribute", SEALING_SIGNATURE_ATTRIBUTE_STRUCT, &Signature, SignatureEncoded,
               sizeof(SignatureEncoded), Buffer, sizeof(Buffer)))
        ok(DecodedSignature->version == 0x31a90028 && DecodedSignature->signerIndex == 0x7ff6 &&
           DecodedSignature->signatureAlgorithm.pszObjId && !DecodedSignature->signatureAlgorithm.pszObjId[0] &&
           !DecodedSignature->signatureAlgorithm.Parameters.cbData && !DecodedSignature->encryptedDigest.cbData,
           "unexpected sealing signature\n");

    Signature.version = 1;
    Signature.signerIndex = 0;
    Signature.signatureAlgorithm.pszObjId = (LPSTR)szOID_RSA_SHA256RSA;
    Signature.encryptedDigest.cbData = sizeof(Digest);
    Signature.encryptedDigest.pbData = (BYTE *)Digest;
    if (Encode("SealingSignatureAttribute", SEALING_SIGNATURE_ATTRIBUTE_STRUCT, &Signature, SignatureAlgorithm,
               sizeof(SignatureAlgorithm), Buffer, sizeof(Buffer)))
        ok(DecodedSignature->signatureAlgorithm.pszObjId &&
           !strcmp(DecodedSignature->signatureAlgorithm.pszObjId, szOID_RSA_SHA256RSA) &&
           DecodedSignature->encryptedDigest.cbData == sizeof(Digest) &&
           !memcmp(DecodedSignature->encryptedDigest.pbData, Digest, sizeof(Digest)), "unexpected algorithm signature\n");
    Signature.signatureAlgorithm.pszObjId = (LPSTR)"";
    if (Encode("SealingSignatureAttribute", SEALING_SIGNATURE_ATTRIBUTE_STRUCT, &Signature, SignatureEmptyOid,
               sizeof(SignatureEmptyOid), Buffer, sizeof(Buffer)))
        ok(DecodedSignature->signatureAlgorithm.Parameters.cbData == 2, "unexpected parameters\n");

    ZeroMemory(&Timestamp, sizeof(Timestamp));
    Timestamp.version = 1;
    DecodedTimestamp = (SEALING_TIMESTAMP_ATTRIBUTE *)Buffer;
    if (Encode("SealingTimestampAttribute", SEALING_TIMESTAMP_ATTRIBUTE_STRUCT, &Timestamp, TimestampEncoded,
               sizeof(TimestampEncoded), Buffer, sizeof(Buffer)))
        ok(DecodedTimestamp->version == 1 && !DecodedTimestamp->signerIndex && !DecodedTimestamp->sealTimeStampToken.cbData,
           "unexpected sealing timestamp\n");
}

START_TEST(Asn1Codecs)
{
    Wintrust = GetModuleHandleW(L"wintrust.dll");

    TestAgencyInfo();
    TestStatementType();
    TestSigInfo();
    TestMinimalCriteria();
    TestSealing();
}
