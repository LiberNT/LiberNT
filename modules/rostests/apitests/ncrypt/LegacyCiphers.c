/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the DES, DESX, 3DES, 3DES_112 and RC2 block ciphers of bcrypt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <stdio.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <bcrypt.h>

struct cipher
{
    const WCHAR *Name;
    ULONG SecretSize;
    ULONG MinLength;
    ULONG MaxLength;
    ULONG Increment;
    NTSTATUS ShortSecret;
    const char *Cbc;
    const char *CbcPadded;
    const char *Ecb;
    const char *Cfb;
    ULONG ObjectLength;
};

static const struct cipher Ciphers[] =
{
    { L"DES", 8, 64, 64, 0, STATUS_INVALID_PARAMETER, "78031231d88a0ab936ed884b1afdaba0",
      "78031231d88a0ab918cebdec8af6faff", "f59715d92afa3fbb8da60cadafdb9fc2",
      "10bd235a48a79b03da78110b22931b1f", 542 },
    { L"DESX", 24, 192, 192, 0, STATUS_INVALID_PARAMETER, "7e373b80d4d7c8ba725323197bd0933c",
      "7e373b80d4d7c8bae11ff38443f59048", "dc820f82f345ee8302a109e59488c90e",
      "afa35c03dae70913a481e97108ef774d", 574 },
    { L"3DES", 24, 192, 192, 0, STATUS_INVALID_PARAMETER, "016c346b3193a294b2c180b3be7791a2",
      "016c346b3193a29431c2ef4a32ac916f", "2fcb14b2e6558982cdb19fe5db1e4fd9",
      "8253ea5ce75973b2b0d4c1dc4ce98fa1", 558 },
    { L"3DES_112", 16, 128, 128, 0, STATUS_INVALID_PARAMETER, "b7f46b077c32ac45408da73365abd11f",
      "b7f46b077c32ac453bbd1d5b16eb5e6f", "96aea1b795391f7bd38830bcc8495abb",
      "084f3a17489e63f8bc6eff91f5f4322b", 542 },
    { L"RC2", 16, 16, 128, 8, STATUS_SUCCESS, "e547d41eb8e642bb7ea8d89fbd9549ff",
      "e547d41eb8e642bb7a5133d58422a559", "5ef0bae2626432a77d247d68b204bb3d",
      "498a0c94f00c7270401ee8bf02abdbae", 414 },
};

static BOOL
MatchHex(
    _In_reads_bytes_(Size) const BYTE *Data,
    _In_ ULONG Size,
    _In_ const char *Expected)
{
    char Text[160];
    ULONG i;

    Text[0] = 0;
    for (i = 0; i < Size && i < 64; i++)
        sprintf(Text + i * 2, "%02x", Data[i]);
    return !strcmp(Text, Expected);
}

static void
TestCipher(
    _In_ const struct cipher *Cipher)
{
    BCRYPT_KEY_LENGTHS_STRUCT Lengths = { 0 };
    BCRYPT_ALG_HANDLE Algorithm;
    BCRYPT_KEY_HANDLE Key;
    BYTE Secret[32], Iv[8], Plain[16], Output[64], Decrypted[64];
    WCHAR Mode[64];
    ULONG Size, Result, Block = 0, i;
    NTSTATUS Status;

    for (i = 0; i < sizeof(Secret); i++)
        Secret[i] = (BYTE)(0x11 * (i + 1));
    for (i = 0; i < sizeof(Plain); i++)
        Plain[i] = (BYTE)(i * 9 + 3);

    Status = BCryptOpenAlgorithmProvider(&Algorithm, Cipher->Name, NULL, 0);
    ok(Status == STATUS_SUCCESS, "%ls: open: %#lx\n", Cipher->Name, Status);
    if (Status != STATUS_SUCCESS)
        return;
    Status = BCryptGetProperty(Algorithm, BCRYPT_BLOCK_LENGTH, (UCHAR *)&Block, sizeof(Block), &Size, 0);
    ok(Status == STATUS_SUCCESS && Block == 8, "%ls: block length %#lx %lu\n", Cipher->Name, Status, Block);
    Status = BCryptGetProperty(Algorithm, BCRYPT_KEY_LENGTHS, (UCHAR *)&Lengths, sizeof(Lengths), &Size, 0);
    ok(Status == STATUS_SUCCESS && Lengths.dwMinLength == Cipher->MinLength && Lengths.dwMaxLength == Cipher->MaxLength &&
       Lengths.dwIncrement == Cipher->Increment, "%ls: key lengths %#lx %lu %lu %lu\n", Cipher->Name, Status,
       Lengths.dwMinLength, Lengths.dwMaxLength, Lengths.dwIncrement);
    Result = 0;
    Status = BCryptGetProperty(Algorithm, BCRYPT_OBJECT_LENGTH, (UCHAR *)&Result, sizeof(Result), &Size, 0);
    ok(Status == STATUS_SUCCESS && Result == Cipher->ObjectLength, "%ls: object length %#lx %lu\n", Cipher->Name,
       Status, Result);
    Mode[0] = 0;
    Status = BCryptGetProperty(Algorithm, BCRYPT_CHAINING_MODE, (UCHAR *)Mode, sizeof(Mode), &Size, 0);
    ok(Size == 64, "%ls: chaining mode size %lu\n", Cipher->Name, Size);
    ok(Status == STATUS_SUCCESS && !wcscmp(Mode, BCRYPT_CHAIN_MODE_CBC), "%ls: chaining mode %#lx %ls\n",
       Cipher->Name, Status, Mode);

    Status = BCryptGenerateSymmetricKey(Algorithm, &Key, NULL, 0, Secret, Cipher->SecretSize - 1, 0);
    ok(Status == Cipher->ShortSecret, "%ls: short secret %#lx\n", Cipher->Name, Status);
    if (Status == STATUS_SUCCESS)
        BCryptDestroyKey(Key);

    Status = BCryptGenerateSymmetricKey(Algorithm, &Key, NULL, 0, Secret, Cipher->SecretSize, 0);
    ok(Status == STATUS_SUCCESS, "%ls: generate: %#lx\n", Cipher->Name, Status);
    if (Status != STATUS_SUCCESS)
    {
        BCryptCloseAlgorithmProvider(Algorithm, 0);
        return;
    }
    if (!wcscmp(Cipher->Name, L"RC2"))
    {
        Status = BCryptGetProperty(Key, BCRYPT_EFFECTIVE_KEY_LENGTH, (UCHAR *)&Result, sizeof(Result), &Size, 0);
        ok(Status == STATUS_NOT_SUPPORTED, "%ls: effective key length on the key %#lx\n", Cipher->Name, Status);
    }

    memset(Iv, 0x5A, sizeof(Iv));
    Result = 0;
    Status = BCryptEncrypt(Key, Plain, sizeof(Plain), NULL, Iv, sizeof(Iv), Output, sizeof(Output), &Result, 0);
    ok(Status == STATUS_SUCCESS && Result == sizeof(Plain), "%ls: CBC encrypt: %#lx size %lu\n", Cipher->Name, Status,
       Result);
    ok(MatchHex(Output, Result, Cipher->Cbc), "%ls: CBC ciphertext differs from Windows\n", Cipher->Name);
    ok(!memcmp(Iv, Output + Result - sizeof(Iv), sizeof(Iv)), "%ls: CBC IV is not the last ciphertext block\n",
       Cipher->Name);
    memset(Iv, 0x5A, sizeof(Iv));
    Status = BCryptDecrypt(Key, Output, Result, NULL, Iv, sizeof(Iv), Decrypted, sizeof(Decrypted), &Size, 0);
    ok(Status == STATUS_SUCCESS && Size == sizeof(Plain) && !memcmp(Plain, Decrypted, sizeof(Plain)),
       "%ls: CBC decrypt: %#lx size %lu\n", Cipher->Name, Status, Size);

    memset(Iv, 0x5A, sizeof(Iv));
    Status = BCryptEncrypt(Key, Plain, 10, NULL, Iv, sizeof(Iv), Output, sizeof(Output), &Result, BCRYPT_BLOCK_PADDING);
    ok(Status == STATUS_SUCCESS && Result == 16, "%ls: padded encrypt: %#lx size %lu\n", Cipher->Name, Status, Result);
    ok(MatchHex(Output, Result, Cipher->CbcPadded), "%ls: padded ciphertext differs from Windows\n", Cipher->Name);
    memset(Iv, 0x5A, sizeof(Iv));
    Status = BCryptDecrypt(Key, Output, Result, NULL, Iv, sizeof(Iv), Decrypted, sizeof(Decrypted), &Size,
                           BCRYPT_BLOCK_PADDING);
    ok(Status == STATUS_SUCCESS && Size == 10 && !memcmp(Plain, Decrypted, 10), "%ls: padded decrypt: %#lx size %lu\n",
       Cipher->Name, Status, Size);
    Status = BCryptEncrypt(Key, Plain, 10, NULL, Iv, sizeof(Iv), Output, sizeof(Output), &Result, 0);
    ok(Status == STATUS_INVALID_BUFFER_SIZE, "%ls: unpadded partial block %#lx\n", Cipher->Name, Status);
    BCryptDestroyKey(Key);

    Status = BCryptSetProperty(Algorithm, BCRYPT_CHAINING_MODE, (UCHAR *)BCRYPT_CHAIN_MODE_CFB,
                               sizeof(BCRYPT_CHAIN_MODE_CFB), 0);
    ok(Status == STATUS_SUCCESS, "%ls: set CFB %#lx\n", Cipher->Name, Status);
    if (Status == STATUS_SUCCESS &&
        BCryptGenerateSymmetricKey(Algorithm, &Key, NULL, 0, Secret, Cipher->SecretSize, 0) == STATUS_SUCCESS)
    {
        memset(Iv, 0x5A, sizeof(Iv));
        Status = BCryptEncrypt(Key, Plain, sizeof(Plain), NULL, Iv, sizeof(Iv), Output, sizeof(Output), &Result, 0);
        ok(Status == STATUS_SUCCESS && Result == sizeof(Plain) && MatchHex(Output, Result, Cipher->Cfb),
           "%ls: CFB encrypt %#lx %lu\n", Cipher->Name, Status, Result);
        memset(Iv, 0x5A, sizeof(Iv));
        Status = BCryptDecrypt(Key, Output, Result, NULL, Iv, sizeof(Iv), Decrypted, sizeof(Decrypted), &Size, 0);
        ok(Status == STATUS_SUCCESS && Size == sizeof(Plain) && !memcmp(Plain, Decrypted, sizeof(Plain)),
           "%ls: CFB decrypt %#lx %lu\n", Cipher->Name, Status, Size);
        BCryptDestroyKey(Key);
    }
    Status = BCryptSetProperty(Algorithm, BCRYPT_CHAINING_MODE, (UCHAR *)BCRYPT_CHAIN_MODE_ECB,
                               sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    ok(Status == STATUS_SUCCESS, "%ls: set ECB %#lx\n", Cipher->Name, Status);
    if (Status == STATUS_SUCCESS &&
        BCryptGenerateSymmetricKey(Algorithm, &Key, NULL, 0, Secret, Cipher->SecretSize, 0) == STATUS_SUCCESS)
    {
        Status = BCryptEncrypt(Key, Plain, sizeof(Plain), NULL, NULL, 0, Output, sizeof(Output), &Result, 0);
        ok(Status == STATUS_SUCCESS && Result == sizeof(Plain), "%ls: ECB encrypt: %#lx size %lu\n", Cipher->Name,
           Status, Result);
        ok(MatchHex(Output, Result, Cipher->Ecb), "%ls: ECB ciphertext differs from Windows\n", Cipher->Name);
        Status = BCryptDecrypt(Key, Output, Result, NULL, NULL, 0, Decrypted, sizeof(Decrypted), &Size, 0);
        ok(Status == STATUS_SUCCESS && Size == sizeof(Plain) && !memcmp(Plain, Decrypted, sizeof(Plain)),
           "%ls: ECB decrypt: %#lx size %lu\n", Cipher->Name, Status, Size);
        BCryptDestroyKey(Key);
    }
    BCryptCloseAlgorithmProvider(Algorithm, 0);
}

START_TEST(LegacyCiphers)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Ciphers); i++)
        TestCipher(&Ciphers[i]);
}
