/*
 * PROJECT:     LiberNT Kernel-Mode Test Suite
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test kernel BCrypt key pairs, public key exports, ECDH secret agreement and key derivation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <bcrypt.h>

/* P-256 keys from SHA-256("LiberNT ECDH A"/"... B") mod n; the KDF formulas were checked against Windows 11 (WinPE 26100) */
static const UCHAR PrivateA[] = { 0x44, 0x1c, 0x09, 0xa6, 0x15, 0x6f, 0xae, 0xac, 0xc9, 0x67, 0x7c, 0xdf, 0x0c, 0x10, 0x7a, 0x48, 0xa9, 0x5b, 0x53, 0xda, 0xae, 0xa0, 0xb9, 0xb5, 0x34, 0x5d, 0xf1, 0xff, 0x76, 0x91, 0xa0, 0x98 };
static const UCHAR PublicAX[] = { 0xcc, 0x2c, 0xc4, 0xd5, 0xea, 0x12, 0xe2, 0x5d, 0x4e, 0x49, 0x3b, 0x7f, 0xae, 0x24, 0xbe, 0x24, 0xb2, 0x8a, 0xcf, 0x09, 0x29, 0x5f, 0x08, 0x2a, 0xde, 0xb2, 0x93, 0x7c, 0xcd, 0x89, 0x86, 0xb3 };
static const UCHAR PublicAY[] = { 0x2b, 0xe1, 0xbd, 0xe1, 0x05, 0x41, 0x8c, 0xc9, 0xd0, 0x89, 0x37, 0xdc, 0x6b, 0x12, 0x4b, 0xaa, 0xd3, 0x3f, 0xde, 0x9a, 0xe3, 0x53, 0x07, 0x4a, 0x80, 0x28, 0x52, 0x99, 0x2e, 0x83, 0x07, 0xd0 };
static const UCHAR PrivateB[] = { 0x98, 0xab, 0x9e, 0xd2, 0xa5, 0x13, 0x40, 0x7b, 0xa4, 0xe7, 0x60, 0xdf, 0x6f, 0xcd, 0x65, 0xfe, 0xc2, 0x7e, 0xc6, 0xf5, 0x63, 0x21, 0x3e, 0x67, 0x67, 0xb7, 0xc3, 0x8b, 0xba, 0xe7, 0x54, 0x9e };
static const UCHAR PublicBX[] = { 0xb8, 0xdf, 0x6d, 0x93, 0x1e, 0x3c, 0xc0, 0xd0, 0x68, 0xc8, 0xc0, 0xae, 0xd0, 0x91, 0x3d, 0xe5, 0x17, 0x77, 0x2f, 0x61, 0x19, 0x0f, 0x8c, 0xc1, 0xf4, 0x5d, 0xd3, 0x52, 0x39, 0xc1, 0x54, 0x53 };
static const UCHAR PublicBY[] = { 0xef, 0x40, 0xae, 0xe7, 0xbc, 0xe0, 0x0c, 0x35, 0x85, 0xde, 0xc2, 0xe9, 0xf8, 0xe2, 0x01, 0x16, 0xe1, 0x0c, 0x19, 0xa7, 0x8b, 0xf1, 0x8d, 0xe9, 0x67, 0x3f, 0x1c, 0xeb, 0xb0, 0xee, 0x69, 0x5a };
static const UCHAR Raw[] = { 0x60, 0x46, 0x4a, 0x11, 0xd2, 0x49, 0xcc, 0xc9, 0x11, 0x8a, 0x53, 0x0c, 0x1e, 0x5a, 0xe6, 0xdc, 0xa1, 0x0b, 0xef, 0xde, 0xfc, 0x1c, 0xd5, 0xe3, 0x0f, 0x2c, 0xdc, 0x75, 0x6f, 0x7e, 0xb4, 0xe8 };
static const UCHAR HashDefault[] = { 0xf0, 0xb6, 0x52, 0x94, 0x3c, 0xc0, 0xcd, 0x56, 0x1b, 0x92, 0x6e, 0x73, 0x7a, 0x4a, 0xfb, 0xd8, 0x3d, 0x73, 0x41, 0x60 };
static const UCHAR HashSha256[] = { 0x27, 0xf6, 0xf9, 0xcb, 0xd8, 0x50, 0xfe, 0xb9, 0x8b, 0x38, 0x0f, 0x13, 0xd6, 0x63, 0x8e, 0x47, 0xbd, 0xaf, 0xab, 0x70, 0xfa, 0x86, 0xde, 0x52, 0x7b, 0xea, 0x9b, 0x92, 0x5f, 0x7e, 0xae, 0x22 };
static const UCHAR HashSha256PrependAppend[] = { 0xc4, 0x48, 0x8a, 0x45, 0xa3, 0xe3, 0xb8, 0x14, 0xa7, 0xf3, 0x41, 0xf4, 0x4a, 0x76, 0x51, 0xd7, 0x39, 0x98, 0x11, 0x6f, 0xac, 0xa4, 0x6c, 0x05, 0x86, 0x69, 0x34, 0x89, 0x64, 0x1f, 0x96, 0x87 };
static const UCHAR HmacSha256Key[] = { 0xd3, 0x5e, 0x12, 0x13, 0xe3, 0xfa, 0xf0, 0x3f, 0xdf, 0x1a, 0x86, 0x14, 0x25, 0x6b, 0x65, 0x50, 0x6f, 0xda, 0xed, 0x27, 0x26, 0x25, 0xb7, 0xd8, 0xe2, 0x07, 0x90, 0xfb, 0xbd, 0x57, 0x3e, 0x21 };

static
VOID
BuildEccBlob(
    _Out_writes_bytes_(104) PUCHAR Blob,
    _In_ ULONG Magic,
    _In_reads_bytes_(32) const UCHAR *X,
    _In_reads_bytes_(32) const UCHAR *Y,
    _In_reads_bytes_opt_(32) const UCHAR *D)
{
    BCRYPT_ECCKEY_BLOB *Header = (BCRYPT_ECCKEY_BLOB *)Blob;

    Header->dwMagic = Magic;
    Header->cbKey = 32;
    RtlCopyMemory(Blob + sizeof(*Header), X, 32);
    RtlCopyMemory(Blob + sizeof(*Header) + 32, Y, 32);
    if (D != NULL)
        RtlCopyMemory(Blob + sizeof(*Header) + 64, D, 32);
}

static
VOID
CheckDerive(
    _In_ BCRYPT_SECRET_HANDLE Secret,
    _In_ PCWSTR Kdf,
    _In_opt_ BCryptBufferDesc *Parameters,
    _In_reads_bytes_(ExpectedSize) const UCHAR *Expected,
    _In_ ULONG ExpectedSize,
    _In_ PCSTR Label)
{
    UCHAR Output[64];
    ULONG Size = 0;
    NTSTATUS Status;

    Status = BCryptDeriveKey(Secret, Kdf, Parameters, NULL, 0, &Size, 0);
    ok(Status == STATUS_SUCCESS, "%s size query: 0x%08lx\n", Label, Status);
    ok(Size == ExpectedSize, "%s size %lu, expected %lu\n", Label, Size, ExpectedSize);

    RtlFillMemory(Output, sizeof(Output), 0xCC);
    Size = 0;
    Status = BCryptDeriveKey(Secret, Kdf, Parameters, Output, sizeof(Output), &Size, 0);
    ok(Status == STATUS_SUCCESS, "%s: 0x%08lx\n", Label, Status);
    ok(Size == ExpectedSize, "%s size %lu, expected %lu\n", Label, Size, ExpectedSize);
    ok(RtlCompareMemory(Output, Expected, ExpectedSize) == ExpectedSize, "%s output differs\n", Label);
}

static
VOID
TestDerivations(
    _In_ BCRYPT_KEY_HANDLE PrivateKeyA,
    _In_ BCRYPT_KEY_HANDLE PublicKeyB,
    _In_ BCRYPT_KEY_HANDLE PrivateKeyB,
    _In_ BCRYPT_KEY_HANDLE PublicKeyA)
{
    static const UCHAR HmacKey[] = { 1, 2, 3, 4 };
    static const UCHAR Prepend[] = { 0xAA };
    static const UCHAR Append[] = { 0xBB, 0xCC };
    BCRYPT_SECRET_HANDLE Secret = NULL, Reverse = NULL, Unused = NULL;
    BCRYPT_KEY_HANDLE Ecdsa = NULL;
    BCryptBuffer Buffers[3];
    BCryptBufferDesc Desc = { BCRYPTBUFFER_VERSION, 0, Buffers };
    UCHAR Output[32];
    ULONG Size;
    NTSTATUS Status;

    Status = BCryptSecretAgreement(PrivateKeyA, PublicKeyB, &Unused, 1);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = BCryptGenerateKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, &Ecdsa, 256, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(BCryptFinalizeKeyPair(Ecdsa, 0), STATUS_SUCCESS);
        Status = BCryptSecretAgreement(Ecdsa, PublicKeyB, &Unused, 0);
        ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
        BCryptDestroyKey(Ecdsa);
    }
    Status = BCryptSecretAgreement(PrivateKeyA, PublicKeyB, &Secret, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = BCryptSecretAgreement(PrivateKeyB, PublicKeyA, &Reverse, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || Secret == NULL)
    {
        if (Secret != NULL)
            BCryptDestroySecret(Secret);
        return;
    }

    CheckDerive(Secret, BCRYPT_KDF_RAW_SECRET, NULL, Raw, sizeof(Raw), "raw");
    CheckDerive(Reverse, BCRYPT_KDF_RAW_SECRET, NULL, Raw, sizeof(Raw), "raw reverse");
    CheckDerive(Secret, BCRYPT_KDF_HASH, NULL, HashDefault, sizeof(HashDefault), "hash default");
    Buffers[0].BufferType = KDF_HASH_ALGORITHM;
    Buffers[0].cbBuffer = sizeof(BCRYPT_SHA256_ALGORITHM);
    Buffers[0].pvBuffer = (PVOID)BCRYPT_SHA256_ALGORITHM;
    Desc.cBuffers = 1;
    CheckDerive(Secret, BCRYPT_KDF_HASH, &Desc, HashSha256, sizeof(HashSha256), "hash sha256");
    Buffers[1].BufferType = KDF_SECRET_PREPEND;
    Buffers[1].cbBuffer = sizeof(Prepend);
    Buffers[1].pvBuffer = (PVOID)Prepend;
    Buffers[2].BufferType = KDF_SECRET_APPEND;
    Buffers[2].cbBuffer = sizeof(Append);
    Buffers[2].pvBuffer = (PVOID)Append;
    Desc.cBuffers = 3;
    CheckDerive(Secret, BCRYPT_KDF_HASH, &Desc, HashSha256PrependAppend, sizeof(HashSha256PrependAppend), "hash prepend append");
    Desc.cBuffers = 1;
    Size = 0;
    Status = BCryptDeriveKey(Secret, BCRYPT_KDF_HMAC, &Desc, NULL, 0, &Size, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Size, 32UL);
    Status = BCryptDeriveKey(Secret, BCRYPT_KDF_HMAC, &Desc, Output, sizeof(Output), &Size, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Buffers[1].BufferType = KDF_HMAC_KEY;
    Buffers[1].cbBuffer = sizeof(HmacKey);
    Buffers[1].pvBuffer = (PVOID)HmacKey;
    Desc.cBuffers = 2;
    CheckDerive(Secret, BCRYPT_KDF_HMAC, &Desc, HmacSha256Key, sizeof(HmacSha256Key), "hmac sha256 key");

    Size = 0;
    Status = BCryptDeriveKey(Secret, L"NoSuchKdf", NULL, NULL, 0, &Size, 0);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);

    ok_eq_hex(BCryptDestroySecret(Secret), STATUS_SUCCESS);
    ok_eq_hex(BCryptDestroySecret(Reverse), STATUS_SUCCESS);
}

static
VOID
TestAgreement(VOID)
{
    UCHAR BlobA[104], BlobB[104];
    BCRYPT_KEY_HANDLE PrivateKeyA = NULL, PublicKeyB = NULL, PrivateKeyB = NULL, PublicKeyA = NULL;
    NTSTATUS Status;

    BuildEccBlob(BlobA, BCRYPT_ECDH_PRIVATE_P256_MAGIC, PublicAX, PublicAY, PrivateA);
    BuildEccBlob(BlobB, BCRYPT_ECDH_PUBLIC_P256_MAGIC, PublicBX, PublicBY, NULL);
    Status = BCryptImportKeyPair(BCRYPT_ECDH_P256_ALG_HANDLE, NULL, BCRYPT_ECCPRIVATE_BLOB, &PrivateKeyA, BlobA, 104, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = BCryptImportKeyPair(BCRYPT_ECDH_P256_ALG_HANDLE, NULL, BCRYPT_ECCPUBLIC_BLOB, &PublicKeyB, BlobB, 72, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    BuildEccBlob(BlobB, BCRYPT_ECDH_PRIVATE_P256_MAGIC, PublicBX, PublicBY, PrivateB);
    BuildEccBlob(BlobA, BCRYPT_ECDH_PUBLIC_P256_MAGIC, PublicAX, PublicAY, NULL);
    Status = BCryptImportKeyPair(BCRYPT_ECDH_P256_ALG_HANDLE, NULL, BCRYPT_ECCPRIVATE_BLOB, &PrivateKeyB, BlobB, 104, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = BCryptImportKeyPair(BCRYPT_ECDH_P256_ALG_HANDLE, NULL, BCRYPT_ECCPUBLIC_BLOB, &PublicKeyA, BlobA, 72, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (PrivateKeyA != NULL && PublicKeyB != NULL && PrivateKeyB != NULL && PublicKeyA != NULL)
        TestDerivations(PrivateKeyA, PublicKeyB, PrivateKeyB, PublicKeyA);
    ok_eq_hex(BCryptDestroySecret(NULL), STATUS_INVALID_HANDLE);

    if (PrivateKeyA != NULL)
        BCryptDestroyKey(PrivateKeyA);
    if (PublicKeyB != NULL)
        BCryptDestroyKey(PublicKeyB);
    if (PrivateKeyB != NULL)
        BCryptDestroyKey(PrivateKeyB);
    if (PublicKeyA != NULL)
        BCryptDestroyKey(PublicKeyA);
}

static
VOID
TestEccKeyPair(
    _In_ BCRYPT_ALG_HANDLE Algorithm,
    _In_ ULONG PublicMagic,
    _In_ ULONG PrivateMagic,
    _In_ PCSTR Label)
{
    BCRYPT_KEY_HANDLE Key = NULL, Pending = NULL;
    UCHAR Blob[128];
    ULONG Size;
    NTSTATUS Status;

    Status = BCryptGenerateKeyPair(Algorithm, &Pending, 384, 0);
    ok(Status == STATUS_INVALID_PARAMETER, "%s 384-bit pair: 0x%08lx\n", Label, Status);
    Status = BCryptGenerateKeyPair(Algorithm, &Pending, 0, 0);
    ok(Status == STATUS_SUCCESS, "%s default-size pair: 0x%08lx\n", Label, Status);
    if (NT_SUCCESS(Status))
        BCryptDestroyKey(Pending);
    Status = BCryptGenerateKeyPair(Algorithm, &Pending, 256, 0);
    ok(Status == STATUS_SUCCESS, "%s pair: 0x%08lx\n", Label, Status);
    if (!NT_SUCCESS(Status))
        return;
    Size = 0;
    Status = BCryptExportKey(Pending, NULL, BCRYPT_ECCPUBLIC_BLOB, Blob, sizeof(Blob), &Size, 0);
    ok(Status == STATUS_INVALID_HANDLE, "%s export before finalize: 0x%08lx\n", Label, Status);
    Status = BCryptFinalizeKeyPair(Pending, 1);
    ok(Status == STATUS_INVALID_PARAMETER, "%s finalize flags: 0x%08lx\n", Label, Status);
    Status = BCryptFinalizeKeyPair(Pending, 0);
    ok(Status == STATUS_SUCCESS, "%s finalize: 0x%08lx\n", Label, Status);
    Status = BCryptFinalizeKeyPair(Pending, 0);
    ok(Status == STATUS_INVALID_HANDLE, "%s finalize again: 0x%08lx\n", Label, Status);
    Key = Pending;

    Size = 0;
    Status = BCryptExportKey(Key, NULL, BCRYPT_ECCPUBLIC_BLOB, NULL, 0, &Size, 0);
    ok(Status == STATUS_SUCCESS && Size == 72, "%s public size query: 0x%08lx %lu\n", Label, Status, Size);
    Status = BCryptExportKey(Key, NULL, BCRYPT_ECCPUBLIC_BLOB, Blob, 71, &Size, 0);
    ok(Status == STATUS_BUFFER_TOO_SMALL && Size == 72, "%s short buffer: 0x%08lx %lu\n", Label, Status, Size);
    Status = BCryptExportKey(Key, NULL, BCRYPT_ECCPUBLIC_BLOB, Blob, sizeof(Blob), &Size, 0);
    ok(Status == STATUS_SUCCESS && Size == 72, "%s public: 0x%08lx %lu\n", Label, Status, Size);
    ok(((BCRYPT_ECCKEY_BLOB *)Blob)->dwMagic == PublicMagic, "%s public magic 0x%08lx\n", Label, ((BCRYPT_ECCKEY_BLOB *)Blob)->dwMagic);
    ok(((BCRYPT_ECCKEY_BLOB *)Blob)->cbKey == 32, "%s public cbKey %lu\n", Label, ((BCRYPT_ECCKEY_BLOB *)Blob)->cbKey);
    Status = BCryptExportKey(Key, NULL, BCRYPT_ECCPRIVATE_BLOB, Blob, sizeof(Blob), &Size, 0);
    ok(Status == STATUS_SUCCESS && Size == 104, "%s private: 0x%08lx %lu\n", Label, Status, Size);
    ok(((BCRYPT_ECCKEY_BLOB *)Blob)->dwMagic == PrivateMagic, "%s private magic 0x%08lx\n", Label, ((BCRYPT_ECCKEY_BLOB *)Blob)->dwMagic);
    Status = BCryptExportKey(Key, NULL, BCRYPT_RSAPUBLIC_BLOB, Blob, sizeof(Blob), &Size, 0);
    ok(Status == STATUS_NOT_SUPPORTED, "%s as RSA blob: 0x%08lx\n", Label, Status);
    BCryptDestroyKey(Key);
}

static
VOID
TestRsaKeyPair(VOID)
{
    BCRYPT_KEY_HANDLE Key = NULL;
    BCRYPT_RSAKEY_BLOB *Header;
    PUCHAR Blob;
    ULONG Size = 0;
    NTSTATUS Status;

    Status = BCryptGenerateKeyPair(BCRYPT_RSA_ALG_HANDLE, &Key, 2048, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = BCryptFinalizeKeyPair(Key, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = BCryptExportKey(Key, NULL, BCRYPT_RSAPUBLIC_BLOB, NULL, 0, &Size, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Size, (ULONG)(sizeof(BCRYPT_RSAKEY_BLOB) + 3 + 256));
    Blob = ExAllocatePoolWithTag(PagedPool, 512, 'tCbK');
    if (Blob != NULL)
    {
        Status = BCryptExportKey(Key, NULL, BCRYPT_RSAPUBLIC_BLOB, Blob, 512, &Size, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Header = (BCRYPT_RSAKEY_BLOB *)Blob;
        ok_eq_hex(Header->Magic, BCRYPT_RSAPUBLIC_MAGIC);
        ok_eq_ulong(Header->BitLength, 2048UL);
        ok_eq_ulong(Header->cbPublicExp, 3UL);
        ok_eq_ulong(Header->cbModulus, 256UL);
        ok_eq_ulong(Header->cbPrime1, 0UL);
        ok(Blob[sizeof(*Header)] == 1 && Blob[sizeof(*Header) + 1] == 0 && Blob[sizeof(*Header) + 2] == 1,
           "Exponent %02x%02x%02x\n", Blob[sizeof(*Header)], Blob[sizeof(*Header) + 1], Blob[sizeof(*Header) + 2]);
        ok(Blob[sizeof(*Header) + 3] & 0x80, "Modulus is not 2048 bits\n");
        ExFreePoolWithTag(Blob, 'tCbK');
    }
    BCryptDestroyKey(Key);
}

START_TEST(KsecBcryptKeyPair)
{
    TestAgreement();
    TestEccKeyPair(BCRYPT_ECDH_P256_ALG_HANDLE, BCRYPT_ECDH_PUBLIC_P256_MAGIC, BCRYPT_ECDH_PRIVATE_P256_MAGIC, "ECDH");
    TestEccKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, BCRYPT_ECDSA_PUBLIC_P256_MAGIC, BCRYPT_ECDSA_PRIVATE_P256_MAGIC, "ECDSA");
    TestRsaKeyPair();
}
