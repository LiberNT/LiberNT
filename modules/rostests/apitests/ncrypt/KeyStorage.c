/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the software key storage provider and persisted keys
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <stdio.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <bcrypt.h>
#include <ncrypt.h>

static const WCHAR ChildKeyVariable[] = L"LIBERNT_NCRYPT_KEY";
static const WCHAR ChildModeVariable[] = L"LIBERNT_NCRYPT_MODE";

static SECURITY_STATUS
OpenProvider(
    _In_opt_ const WCHAR *Name,
    _Out_ NCRYPT_PROV_HANDLE *Provider)
{
    *Provider = 0;
    return NCryptOpenStorageProvider(Provider, Name, 0);
}

static BOOL
StringProperty(
    _In_ NCRYPT_HANDLE Handle,
    _In_ const WCHAR *Name,
    _Out_writes_(Size) WCHAR *Value,
    _In_ DWORD Size)
{
    DWORD Result = 0;

    Value[0] = 0;
    if (NCryptGetProperty(Handle, Name, (BYTE *)Value, Size * sizeof(WCHAR), &Result, 0))
        return FALSE;
    Value[Size - 1] = 0;
    return TRUE;
}

static DWORD
DwordProperty(
    _In_ NCRYPT_HANDLE Handle,
    _In_ const WCHAR *Name)
{
    DWORD Value = 0xdeadbeef, Result = 0;

    NCryptGetProperty(Handle, Name, (BYTE *)&Value, sizeof(Value), &Result, 0);
    return Value;
}

static void
TestProviders(void)
{
    NCRYPT_PROV_HANDLE Provider;
    SECURITY_STATUS Status;
    WCHAR Name[128];

    Status = OpenProvider(NULL, &Provider);
    ok(Status == ERROR_SUCCESS, "default provider: %#lx\n", Status);
    if (Status == ERROR_SUCCESS)
    {
        ok(StringProperty(Provider, NCRYPT_NAME_PROPERTY, Name, ARRAYSIZE(Name)) &&
           !wcscmp(Name, MS_KEY_STORAGE_PROVIDER), "default provider name %ls\n", Name);
        ok(DwordProperty(Provider, NCRYPT_IMPL_TYPE_PROPERTY) == (NCRYPT_IMPL_SOFTWARE_FLAG | 0x20),
           "impl type %#lx\n", DwordProperty(Provider, NCRYPT_IMPL_TYPE_PROPERTY));
        ok(NCryptIsKeyHandle(Provider) == FALSE, "provider handle reported as a key\n");
        NCryptFreeObject(Provider);
    }

    Status = OpenProvider(MS_KEY_STORAGE_PROVIDER, &Provider);
    ok(Status == ERROR_SUCCESS, "software provider: %#lx\n", Status);
    if (Status == ERROR_SUCCESS)
        NCryptFreeObject(Provider);

    Status = OpenProvider(MS_PLATFORM_KEY_STORAGE_PROVIDER, &Provider);
    if (Status == ERROR_SUCCESS)
    {
        ok(DwordProperty(Provider, NCRYPT_IMPL_TYPE_PROPERTY) & NCRYPT_IMPL_HARDWARE_FLAG,
           "platform provider without a hardware implementation, impl %#lx\n",
           DwordProperty(Provider, NCRYPT_IMPL_TYPE_PROPERTY));
        NCryptFreeObject(Provider);
    }
    else
    {
        ok(Status == NTE_DEVICE_NOT_READY, "platform provider without a TPM: %#lx\n", Status);
    }

    Status = OpenProvider(MS_SMART_CARD_KEY_STORAGE_PROVIDER, &Provider);
    ok(Status == ERROR_SUCCESS, "smart card provider: %#lx\n", Status);
    if (Status == ERROR_SUCCESS)
    {
        ok(StringProperty(Provider, NCRYPT_NAME_PROPERTY, Name, ARRAYSIZE(Name)) &&
           !wcscmp(Name, MS_SMART_CARD_KEY_STORAGE_PROVIDER), "smart card provider name %ls\n", Name);
        ok(DwordProperty(Provider, NCRYPT_IMPL_TYPE_PROPERTY) ==
           (NCRYPT_IMPL_HARDWARE_FLAG | NCRYPT_IMPL_SOFTWARE_FLAG | NCRYPT_IMPL_REMOVABLE_FLAG),
           "smart card provider impl %#lx\n", DwordProperty(Provider, NCRYPT_IMPL_TYPE_PROPERTY));
        NCryptFreeObject(Provider);
    }

    Status = OpenProvider(L"LiberNT Missing Provider", &Provider);
    ok(Status == (SECURITY_STATUS)STATUS_NOT_FOUND, "missing provider: %#lx\n", Status);
    ok(Provider == 0, "missing provider handle %#Ix\n", Provider);
}

static void
TestAlgorithms(
    _In_ NCRYPT_PROV_HANDLE Provider)
{
    NCryptAlgorithmName *List = NULL;
    BOOL Rsa = FALSE, Ecdsa = FALSE;
    SECURITY_STATUS Status;
    DWORD Count = 0, i;

    Status = NCryptEnumAlgorithms(Provider, NCRYPT_SIGNATURE_OPERATION, &Count, &List, 0);
    ok(Status == ERROR_SUCCESS, "NCryptEnumAlgorithms: %#lx\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(Count > 0, "no signature algorithms\n");
    for (i = 0; i < Count; i++)
    {
        ok(List[i].dwAlgOperations & NCRYPT_SIGNATURE_OPERATION, "%ls operations %#lx\n", List[i].pszName,
           List[i].dwAlgOperations);
        if (!wcscmp(List[i].pszName, NCRYPT_RSA_ALGORITHM))
            Rsa = TRUE;
        if (!wcscmp(List[i].pszName, NCRYPT_ECDSA_P256_ALGORITHM))
            Ecdsa = TRUE;
    }
    ok(Rsa && Ecdsa, "RSA %d ECDSA_P256 %d in the signature algorithms\n", Rsa, Ecdsa);
    ok(NCryptFreeBuffer(List) == ERROR_SUCCESS, "NCryptFreeBuffer failed\n");
}

static BOOL
FindKey(
    _In_ NCRYPT_PROV_HANDLE Provider,
    _In_ const WCHAR *KeyName,
    _In_ DWORD Flags,
    _Out_writes_(32) WCHAR *Algorithm)
{
    NCryptKeyName *Name;
    PVOID State = NULL;
    SECURITY_STATUS Status;
    BOOL Found = FALSE;
    UINT Count = 0;

    Algorithm[0] = 0;
    while ((Status = NCryptEnumKeys(Provider, NULL, &Name, &State, Flags)) == ERROR_SUCCESS)
    {
        Count++;
        if (!wcscmp(Name->pszName, KeyName))
        {
            Found = TRUE;
            wcsncpy(Algorithm, Name->pszAlgid, 31);
            Algorithm[31] = 0;
        }
        NCryptFreeBuffer(Name);
        if (Count > 10000)
            break;
    }
    ok(Status == NTE_NO_MORE_ITEMS, "NCryptEnumKeys ended with %#lx after %u keys\n", Status, Count);
    if (State)
        NCryptFreeBuffer(State);
    return Found;
}

static DWORD
RunChild(
    _In_ const WCHAR *KeyName,
    _In_ const WCHAR *Mode)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    DWORD ExitCode = 0xdeadbeef;

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    swprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%ls\" KeyStorageChild", Path);
    SetEnvironmentVariableW(ChildKeyVariable, KeyName);
    SetEnvironmentVariableW(ChildModeVariable, Mode);
    if (CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(WaitForSingleObject(Process.hProcess, 30000) == WAIT_OBJECT_0, "child %ls did not exit\n", Mode);
        GetExitCodeProcess(Process.hProcess, &ExitCode);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }
    SetEnvironmentVariableW(ChildKeyVariable, NULL);
    SetEnvironmentVariableW(ChildModeVariable, NULL);
    return ExitCode;
}

static void
TestPersistedKey(
    _In_ NCRYPT_PROV_HANDLE Provider,
    _In_ const WCHAR *Algorithm,
    _In_ DWORD Length,
    _In_ DWORD Flags)
{
    BCRYPT_PKCS1_PADDING_INFO Padding = { BCRYPT_SHA256_ALGORITHM };
    BYTE Hash[32], Signature[512], Plain[16] = "LiberNT secret", Cipher[512], Decrypted[512];
    WCHAR KeyName[96], Value[128], Found[32];
    NCRYPT_KEY_HANDLE Key = 0, Other = 0;
    SECURITY_STATUS Status;
    DWORD Size = 0, CipherSize = 0, PlainSize = 0, i;
    BOOL Rsa = !wcscmp(Algorithm, NCRYPT_RSA_ALGORITHM);
    void *PaddingInfo = Rsa ? &Padding : NULL;
    DWORD PaddingFlag = Rsa ? BCRYPT_PAD_PKCS1 : 0;

    for (i = 0; i < sizeof(Hash); i++)
        Hash[i] = (BYTE)(i * 7 + 1);
    swprintf(KeyName, ARRAYSIZE(KeyName), L"LiberNT.ApiTest.%ls.%lx.%lu", Algorithm, Flags, GetCurrentProcessId());

    Status = NCryptOpenKey(Provider, &Key, KeyName, 0, Flags);
    ok(Status == NTE_BAD_KEYSET, "%ls: open before creation: %#lx\n", KeyName, Status);

    Status = NCryptCreatePersistedKey(Provider, &Key, Algorithm, KeyName, 0, Flags);
    ok(Status == ERROR_SUCCESS, "%ls: create: %#lx\n", KeyName, Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(NCryptIsKeyHandle(Key) == TRUE, "%ls: key handle not recognized\n", KeyName);
    Status = NCryptOpenKey(Provider, &Other, KeyName, 0, Flags);
    ok(Status == NTE_BAD_KEYSET, "%ls: open before finalize: %#lx\n", KeyName, Status);
    if (Rsa)
    {
        Status = NCryptSetProperty(Key, NCRYPT_LENGTH_PROPERTY, (BYTE *)&Length, sizeof(Length), 0);
        ok(Status == ERROR_SUCCESS, "%ls: set length: %#lx\n", KeyName, Status);
    }
    Status = NCryptFinalizeKey(Key, 0);
    ok(Status == ERROR_SUCCESS, "%ls: finalize: %#lx\n", KeyName, Status);

    ok(StringProperty(Key, NCRYPT_NAME_PROPERTY, Value, ARRAYSIZE(Value)) && !wcscmp(Value, KeyName),
       "%ls: name property %ls\n", KeyName, Value);
    ok(StringProperty(Key, NCRYPT_UNIQUE_NAME_PROPERTY, Value, ARRAYSIZE(Value)) && Value[0],
       "%ls: unique name property missing\n", KeyName);
    ok(DwordProperty(Key, NCRYPT_LENGTH_PROPERTY) == Length, "%ls: length %lu\n", KeyName,
       DwordProperty(Key, NCRYPT_LENGTH_PROPERTY));
    ok(DwordProperty(Key, NCRYPT_KEY_TYPE_PROPERTY) == (Flags & NCRYPT_MACHINE_KEY_FLAG ? NCRYPT_MACHINE_KEY_FLAG : 0),
       "%ls: key type %#lx\n", KeyName, DwordProperty(Key, NCRYPT_KEY_TYPE_PROPERTY));

    Size = 0;
    Status = NCryptExportKey(Key, 0, Rsa ? BCRYPT_RSAPRIVATE_BLOB : BCRYPT_ECCPRIVATE_BLOB, NULL, NULL, 0, &Size, 0);
    ok(Status == NTE_NOT_SUPPORTED, "%ls: private export of a non-exportable key: %#lx\n", KeyName, Status);
    Size = 0;
    Status = NCryptExportKey(Key, 0, Rsa ? BCRYPT_RSAPUBLIC_BLOB : BCRYPT_ECCPUBLIC_BLOB, NULL, NULL, 0, &Size, 0);
    ok(Status == ERROR_SUCCESS && Size, "%ls: public export: %#lx size %lu\n", KeyName, Status, Size);

    Size = 0;
    Status = NCryptSignHash(Key, PaddingInfo, Hash, sizeof(Hash), Signature, sizeof(Signature), &Size, PaddingFlag);
    ok(Status == ERROR_SUCCESS, "%ls: sign: %#lx\n", KeyName, Status);
    if (Rsa)
    {
        Status = NCryptEncrypt(Key, Plain, sizeof(Plain), NULL, Cipher, sizeof(Cipher), &CipherSize,
                               NCRYPT_PAD_PKCS1_FLAG);
        ok(Status == ERROR_SUCCESS, "%ls: encrypt: %#lx\n", KeyName, Status);
        Status = NCryptDecrypt(Key, Cipher, CipherSize, NULL, Decrypted, sizeof(Decrypted), &PlainSize,
                               NCRYPT_PAD_PKCS1_FLAG);
        ok(Status == ERROR_SUCCESS && PlainSize == sizeof(Plain) && !memcmp(Decrypted, Plain, sizeof(Plain)),
           "%ls: decrypt: %#lx size %lu\n", KeyName, Status, PlainSize);
    }
    NCryptFreeObject(Key);
    Key = 0;

    Status = NCryptOpenKey(Provider, &Key, KeyName, 0, Flags);
    ok(Status == ERROR_SUCCESS, "%ls: open after finalize: %#lx\n", KeyName, Status);
    if (Status == ERROR_SUCCESS)
    {
        Status = NCryptVerifySignature(Key, PaddingInfo, Hash, sizeof(Hash), Signature, Size, PaddingFlag);
        ok(Status == ERROR_SUCCESS, "%ls: reopened key does not verify the signature: %#lx\n", KeyName, Status);
        ok(DwordProperty(Key, NCRYPT_LENGTH_PROPERTY) == Length, "%ls: reopened length %lu\n", KeyName,
           DwordProperty(Key, NCRYPT_LENGTH_PROPERTY));
        NCryptFreeObject(Key);
        Key = 0;
    }
    Status = NCryptOpenKey(Provider, &Key, KeyName, 0, Flags ^ NCRYPT_MACHINE_KEY_FLAG);
    ok(Status == NTE_BAD_KEYSET, "%ls: open in the other scope: %#lx\n", KeyName, Status);

    ok(FindKey(Provider, KeyName, Flags, Found), "%ls: not enumerated\n", KeyName);
    ok(!wcscmp(Found, Algorithm), "%ls: enumerated algorithm %ls\n", KeyName, Found);

    ok(RunChild(KeyName, Flags & NCRYPT_MACHINE_KEY_FLAG ? L"openmachine" : L"open") == Length,
       "%ls: another process cannot open the key\n", KeyName);

    Key = 0;
    Status = NCryptCreatePersistedKey(Provider, &Key, Algorithm, KeyName, 0, Flags);
    ok(Status == NTE_EXISTS, "%ls: create over an existing key: %#lx\n", KeyName, Status);
    if (Status == ERROR_SUCCESS)
        NCryptFreeObject(Key);
    Status = NCryptCreatePersistedKey(Provider, &Key, Algorithm, KeyName, 0, Flags | NCRYPT_OVERWRITE_KEY_FLAG);
    ok(Status == ERROR_SUCCESS, "%ls: create with overwrite: %#lx\n", KeyName, Status);
    if (Status == ERROR_SUCCESS)
    {
        if (Rsa)
            NCryptSetProperty(Key, NCRYPT_LENGTH_PROPERTY, (BYTE *)&Length, sizeof(Length), 0);
        Status = NCryptFinalizeKey(Key, 0);
        ok(Status == ERROR_SUCCESS, "%ls: finalize with overwrite: %#lx\n", KeyName, Status);
        Status = NCryptDeleteKey(Key, 0);
        ok(Status == ERROR_SUCCESS, "%ls: delete: %#lx\n", KeyName, Status);
        if (Status != ERROR_SUCCESS)
            NCryptFreeObject(Key);
    }
    Status = NCryptOpenKey(Provider, &Key, KeyName, 0, Flags);
    ok(Status == NTE_BAD_KEYSET, "%ls: open after delete: %#lx\n", KeyName, Status);
    ok(RunChild(KeyName, Flags & NCRYPT_MACHINE_KEY_FLAG ? L"openmachine" : L"open") == 0,
       "%ls: another process still opens the deleted key\n", KeyName);
}

static void
TestExportPolicy(
    _In_ NCRYPT_PROV_HANDLE Provider)
{
    BYTE Blob[2048];
    NCRYPT_KEY_HANDLE Key, Imported;
    SECURITY_STATUS Status;
    DWORD Size = 0, Policy;

    Status = NCryptCreatePersistedKey(Provider, &Key, NCRYPT_RSA_ALGORITHM, NULL, 0, 0);
    ok(Status == ERROR_SUCCESS, "ephemeral create: %#lx\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(NCryptFinalizeKey(Key, 0) == ERROR_SUCCESS, "ephemeral finalize failed\n");
    ok(DwordProperty(Key, NCRYPT_EXPORT_POLICY_PROPERTY) == 0, "ephemeral export policy %#lx\n",
       DwordProperty(Key, NCRYPT_EXPORT_POLICY_PROPERTY));
    Status = NCryptExportKey(Key, 0, BCRYPT_RSAPRIVATE_BLOB, NULL, Blob, sizeof(Blob), &Size, 0);
    ok(Status == NTE_NOT_SUPPORTED, "ephemeral private export without the policy: %#lx\n", Status);
    NCryptFreeObject(Key);

    Status = NCryptCreatePersistedKey(Provider, &Key, NCRYPT_RSA_ALGORITHM, NULL, 0, 0);
    if (Status != ERROR_SUCCESS)
        return;
    Policy = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
    NCryptSetProperty(Key, NCRYPT_EXPORT_POLICY_PROPERTY, (BYTE *)&Policy, sizeof(Policy), 0);
    NCryptFinalizeKey(Key, 0);
    Size = 0;
    Status = NCryptExportKey(Key, 0, BCRYPT_RSAPRIVATE_BLOB, NULL, Blob, sizeof(Blob), &Size, 0);
    ok(Status == ERROR_SUCCESS && Size, "private export with the policy: %#lx size %lu\n", Status, Size);
    NCryptFreeObject(Key);
    if (Status != ERROR_SUCCESS)
        return;

    Status = NCryptImportKey(Provider, 0, BCRYPT_RSAPRIVATE_BLOB, NULL, &Imported, Blob, Size, 0);
    ok(Status == ERROR_SUCCESS, "import: %#lx\n", Status);
    if (Status == ERROR_SUCCESS)
    {
        ok(DwordProperty(Imported, NCRYPT_EXPORT_POLICY_PROPERTY) == 0, "imported export policy %#lx\n",
           DwordProperty(Imported, NCRYPT_EXPORT_POLICY_PROPERTY));
        Status = NCryptExportKey(Imported, 0, BCRYPT_RSAPRIVATE_BLOB, NULL, Blob, sizeof(Blob), &Size, 0);
        ok(Status == NTE_NOT_SUPPORTED, "imported private export without the policy: %#lx\n", Status);
        NCryptFreeObject(Imported);
    }
}

static void
TestSymmetricKey(
    _In_ NCRYPT_PROV_HANDLE Provider)
{
    NCRYPT_CIPHER_PADDING_INFO Info;
    NCRYPT_SUPPORTED_LENGTHS Lengths = { 0 };
    NCryptAlgorithmName *List = NULL;
    NCRYPT_KEY_HANDLE Key;
    SECURITY_STATUS Status;
    BYTE Iv[16], Plain[32], Cipher[64], Decrypted[64], Blob[256];
    WCHAR Value[64], KeyName[96];
    DWORD Size = 0, PlainSize = 0, Count = 0, i;
    BOOL Aes = FALSE;

    for (i = 0; i < sizeof(Plain); i++)
        Plain[i] = (BYTE)(i * 3 + 5);
    Status = NCryptEnumAlgorithms(Provider, NCRYPT_CIPHER_OPERATION, &Count, &List, 0);
    ok(Status == ERROR_SUCCESS, "cipher algorithms: %#lx\n", Status);
    if (Status == ERROR_SUCCESS)
    {
        for (i = 0; i < Count; i++)
        {
            ok(List[i].dwClass == NCRYPT_CIPHER_INTERFACE && List[i].dwAlgOperations == NCRYPT_CIPHER_OPERATION,
               "cipher algorithm %ls class %lu ops %#lx\n", List[i].pszName, List[i].dwClass,
               List[i].dwAlgOperations);
            if (!wcscmp(List[i].pszName, NCRYPT_AES_ALGORITHM))
                Aes = TRUE;
        }
        NCryptFreeBuffer(List);
    }
    ok(Aes, "AES is not listed as a cipher algorithm\n");

    Status = NCryptCreatePersistedKey(Provider, &Key, NCRYPT_AES_ALGORITHM, NULL, 0, 0);
    ok(Status == ERROR_SUCCESS, "AES create: %#lx\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(DwordProperty(Key, NCRYPT_LENGTH_PROPERTY) == 256, "AES default length %lu\n",
       DwordProperty(Key, NCRYPT_LENGTH_PROPERTY));
    ok(DwordProperty(Key, NCRYPT_BLOCK_LENGTH_PROPERTY) == 16, "AES block length %lu\n",
       DwordProperty(Key, NCRYPT_BLOCK_LENGTH_PROPERTY));
    ok(!StringProperty(Key, NCRYPT_CHAINING_MODE_PROPERTY, Value, ARRAYSIZE(Value)), "AES chaining mode %ls\n", Value);
    Status = NCryptGetProperty(Key, NCRYPT_CHAINING_MODE_PROPERTY, (BYTE *)Value, sizeof(Value), &Size, 0);
    ok(Status == NTE_NO_KEY, "AES chaining mode property: %#lx\n", Status);
    ok(StringProperty(Key, NCRYPT_ALGORITHM_GROUP_PROPERTY, Value, ARRAYSIZE(Value)) && !wcscmp(Value, L"AES"),
       "AES group %ls\n", Value);
    ok(StringProperty(Key, NCRYPT_ALGORITHM_PROPERTY, Value, ARRAYSIZE(Value)) && !wcscmp(Value, L"AES"),
       "AES algorithm %ls\n", Value);
    Status = NCryptGetProperty(Key, NCRYPT_LENGTHS_PROPERTY, (BYTE *)&Lengths, sizeof(Lengths), &Size, 0);
    ok(Status == ERROR_SUCCESS && Lengths.dwMinLength == 128 && Lengths.dwMaxLength == 256 &&
       Lengths.dwIncrement == 64 && Lengths.dwDefaultLength == 256, "AES lengths %#lx: %lu %lu %lu %lu\n", Status,
       Lengths.dwMinLength, Lengths.dwMaxLength, Lengths.dwIncrement, Lengths.dwDefaultLength);
    Status = NCryptEncrypt(Key, Plain, sizeof(Plain), NULL, Cipher, sizeof(Cipher), &Size, 0);
    ok(Status == NTE_BAD_KEY_STATE, "AES encrypt before finalize: %#lx\n", Status);
    Status = NCryptFinalizeKey(Key, 0);
    ok(Status == ERROR_SUCCESS, "AES finalize: %#lx\n", Status);

    Size = 0;
    Status = NCryptEncrypt(Key, Plain, sizeof(Plain), NULL, Cipher, sizeof(Cipher), &Size, 0);
    ok(Status == ERROR_SUCCESS && Size == sizeof(Plain), "AES encrypt without padding info: %#lx size %lu\n", Status,
       Size);
    if (Status == ERROR_SUCCESS)
    {
        PlainSize = 0;
        Status = NCryptDecrypt(Key, Cipher, Size, NULL, Decrypted, sizeof(Decrypted), &PlainSize, 0);
        ok(Status == ERROR_SUCCESS && PlainSize == sizeof(Plain), "AES decrypt without padding info: %#lx size %lu\n",
           Status, PlainSize);
        ok(memcmp(Plain, Decrypted, 16) != 0, "AES decrypt without padding info restarted the chaining\n");
        ok(!memcmp(Plain + 16, Decrypted + 16, 16), "AES decrypt without padding info does not chain the blocks\n");
    }
    Size = 0;
    Status = NCryptEncrypt(Key, Plain, 20, NULL, Cipher, sizeof(Cipher), &Size, 0);
    ok(Status == (SECURITY_STATUS)STATUS_INVALID_BUFFER_SIZE, "AES encrypt 20 bytes without padding: %#lx\n", Status);

    memset(Iv, 0x42, sizeof(Iv));
    Info.cbSize = sizeof(Info);
    Info.dwFlags = NCRYPT_CIPHER_BLOCK_PADDING_FLAG;
    Info.pbIV = Iv;
    Info.cbIV = sizeof(Iv);
    Info.pbOtherInfo = NULL;
    Info.cbOtherInfo = 0;
    Size = 0;
    Status = NCryptEncrypt(Key, Plain, 20, &Info, Cipher, sizeof(Cipher), &Size, NCRYPT_PAD_CIPHER_FLAG);
    ok(Status == ERROR_SUCCESS && Size == 32, "AES encrypt 20 bytes with block padding: %#lx size %lu\n", Status, Size);
    ok(Iv[0] != 0x42 || Iv[15] != 0x42, "AES encrypt did not update the IV\n");
    if (Status == ERROR_SUCCESS)
    {
        memset(Iv, 0x42, sizeof(Iv));
        PlainSize = 0;
        Status = NCryptDecrypt(Key, Cipher, Size, &Info, Decrypted, sizeof(Decrypted), &PlainSize,
                               NCRYPT_PAD_CIPHER_FLAG);
        ok(Status == ERROR_SUCCESS && PlainSize == 20 && !memcmp(Plain, Decrypted, 20),
           "AES decrypt with block padding: %#lx size %lu\n", Status, PlainSize);
    }
    Size = 0;
    Status = NCryptExportKey(Key, 0, NCRYPT_CIPHER_KEY_BLOB, NULL, Blob, sizeof(Blob), &Size, 0);
    ok(Status == NTE_NOT_SUPPORTED, "AES cipher key blob export: %#lx\n", Status);
    NCryptFreeObject(Key);

    swprintf(KeyName, ARRAYSIZE(KeyName), L"LiberNT.ApiTest.AES.%lu", GetCurrentProcessId());
    Status = NCryptCreatePersistedKey(Provider, &Key, NCRYPT_AES_ALGORITHM, KeyName, 0, 0);
    ok(Status == ERROR_SUCCESS, "persisted AES create: %#lx\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    Status = NCryptFinalizeKey(Key, 0);
    ok(Status == ERROR_SUCCESS, "persisted AES finalize: %#lx\n", Status);
    memset(Iv, 0x24, sizeof(Iv));
    Size = 0;
    Status = NCryptEncrypt(Key, Plain, sizeof(Plain), &Info, Cipher, sizeof(Cipher), &Size, NCRYPT_PAD_CIPHER_FLAG);
    ok(Status == ERROR_SUCCESS && Size == 48, "persisted AES encrypt: %#lx size %lu\n", Status, Size);
    NCryptFreeObject(Key);
    Status = NCryptOpenKey(Provider, &Key, KeyName, 0, 0);
    ok(Status == ERROR_SUCCESS, "persisted AES open: %#lx\n", Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(StringProperty(Key, NCRYPT_ALGORITHM_PROPERTY, Value, ARRAYSIZE(Value)) && !wcscmp(Value, L"AES"),
       "persisted AES reopened algorithm %ls\n", Value);
    ok(DwordProperty(Key, NCRYPT_LENGTH_PROPERTY) == 256, "persisted AES reopened length %lu\n",
       DwordProperty(Key, NCRYPT_LENGTH_PROPERTY));
    memset(Iv, 0x24, sizeof(Iv));
    PlainSize = 0;
    Status = NCryptDecrypt(Key, Cipher, Size, &Info, Decrypted, sizeof(Decrypted), &PlainSize, NCRYPT_PAD_CIPHER_FLAG);
    ok(Status == ERROR_SUCCESS && PlainSize == sizeof(Plain) && !memcmp(Plain, Decrypted, sizeof(Plain)),
       "persisted AES decrypt after reopen: %#lx size %lu\n", Status, PlainSize);
    Status = NCryptDeleteKey(Key, 0);
    ok(Status == ERROR_SUCCESS, "persisted AES delete: %#lx\n", Status);
}

static void
TestLegacySymmetricKey(
    _In_ NCRYPT_PROV_HANDLE Provider,
    _In_ const WCHAR *Algorithm,
    _In_ DWORD Length,
    _In_ DWORD MinLength,
    _In_ DWORD Increment)
{
    NCRYPT_CIPHER_PADDING_INFO Info;
    NCRYPT_SUPPORTED_LENGTHS Lengths = { 0 };
    NCRYPT_KEY_HANDLE Key;
    SECURITY_STATUS Status;
    BYTE Iv[8], Plain[16], Cipher[64], Decrypted[64];
    WCHAR KeyName[96];
    DWORD Size = 0, PlainSize = 0, i;

    for (i = 0; i < sizeof(Plain); i++)
        Plain[i] = (BYTE)(i * 5 + 1);
    Status = NCryptCreatePersistedKey(Provider, &Key, Algorithm, NULL, 0, 0);
    ok(Status == ERROR_SUCCESS, "%ls create: %#lx\n", Algorithm, Status);
    if (Status != ERROR_SUCCESS)
        return;
    Status = NCryptGetProperty(Key, NCRYPT_LENGTHS_PROPERTY, (BYTE *)&Lengths, sizeof(Lengths), &Size, 0);
    ok(DwordProperty(Key, NCRYPT_LENGTH_PROPERTY) == Length && DwordProperty(Key, NCRYPT_BLOCK_LENGTH_PROPERTY) == 8,
       "%ls length %lu block %lu\n", Algorithm, DwordProperty(Key, NCRYPT_LENGTH_PROPERTY),
       DwordProperty(Key, NCRYPT_BLOCK_LENGTH_PROPERTY));
    ok(Status == ERROR_SUCCESS && Lengths.dwMinLength == MinLength && Lengths.dwMaxLength == Length &&
       Lengths.dwIncrement == Increment && Lengths.dwDefaultLength == Length, "%ls lengths %#lx: %lu %lu %lu %lu\n",
       Algorithm, Status, Lengths.dwMinLength, Lengths.dwMaxLength, Lengths.dwIncrement, Lengths.dwDefaultLength);
    Status = NCryptFinalizeKey(Key, 0);
    ok(Status == ERROR_SUCCESS, "%ls finalize: %#lx\n", Algorithm, Status);
    memset(Iv, 0x33, sizeof(Iv));
    Info.cbSize = sizeof(Info);
    Info.dwFlags = NCRYPT_CIPHER_BLOCK_PADDING_FLAG;
    Info.pbIV = Iv;
    Info.cbIV = sizeof(Iv);
    Info.pbOtherInfo = NULL;
    Info.cbOtherInfo = 0;
    Size = 0;
    Status = NCryptEncrypt(Key, Plain, 10, &Info, Cipher, sizeof(Cipher), &Size, NCRYPT_PAD_CIPHER_FLAG);
    ok(Status == ERROR_SUCCESS && Size == 16, "%ls encrypt: %#lx size %lu\n", Algorithm, Status, Size);
    memset(Iv, 0x33, sizeof(Iv));
    Status = NCryptDecrypt(Key, Cipher, Size, &Info, Decrypted, sizeof(Decrypted), &PlainSize, NCRYPT_PAD_CIPHER_FLAG);
    ok(Status == ERROR_SUCCESS && PlainSize == 10 && !memcmp(Plain, Decrypted, 10), "%ls decrypt: %#lx size %lu\n",
       Algorithm, Status, PlainSize);
    NCryptFreeObject(Key);

    swprintf(KeyName, ARRAYSIZE(KeyName), L"LiberNT.ApiTest.%ls.%lu", Algorithm, GetCurrentProcessId());
    Status = NCryptCreatePersistedKey(Provider, &Key, Algorithm, KeyName, 0, 0);
    ok(Status == ERROR_SUCCESS, "persisted %ls create: %#lx\n", Algorithm, Status);
    if (Status != ERROR_SUCCESS)
        return;
    ok(NCryptFinalizeKey(Key, 0) == ERROR_SUCCESS, "persisted %ls finalize failed\n", Algorithm);
    memset(Iv, 0x44, sizeof(Iv));
    Size = 0;
    Status = NCryptEncrypt(Key, Plain, sizeof(Plain), &Info, Cipher, sizeof(Cipher), &Size, NCRYPT_PAD_CIPHER_FLAG);
    ok(Status == ERROR_SUCCESS && Size == 24, "persisted %ls encrypt: %#lx size %lu\n", Algorithm, Status, Size);
    NCryptFreeObject(Key);
    Status = NCryptOpenKey(Provider, &Key, KeyName, 0, 0);
    ok(Status == ERROR_SUCCESS, "persisted %ls open: %#lx\n", Algorithm, Status);
    if (Status != ERROR_SUCCESS)
        return;
    memset(Iv, 0x44, sizeof(Iv));
    PlainSize = 0;
    Status = NCryptDecrypt(Key, Cipher, Size, &Info, Decrypted, sizeof(Decrypted), &PlainSize, NCRYPT_PAD_CIPHER_FLAG);
    ok(Status == ERROR_SUCCESS && PlainSize == sizeof(Plain) && !memcmp(Plain, Decrypted, sizeof(Plain)),
       "persisted %ls decrypt after reopen: %#lx size %lu\n", Algorithm, Status, PlainSize);
    ok(NCryptDeleteKey(Key, 0) == ERROR_SUCCESS, "persisted %ls delete failed\n", Algorithm);
}

START_TEST(KeyStorage)
{
    NCRYPT_PROV_HANDLE Provider;
    SECURITY_STATUS Status;

    TestProviders();
    Status = OpenProvider(MS_KEY_STORAGE_PROVIDER, &Provider);
    if (Status != ERROR_SUCCESS)
    {
        skip("no software key storage provider: %#lx\n", Status);
        return;
    }
    TestAlgorithms(Provider);
    TestExportPolicy(Provider);
    TestSymmetricKey(Provider);
    TestLegacySymmetricKey(Provider, L"RC2", 128, 16, 8);
    TestLegacySymmetricKey(Provider, L"3DES", 192, 192, 0);
    TestLegacySymmetricKey(Provider, L"DES", 64, 64, 0);
    TestLegacySymmetricKey(Provider, L"DESX", 192, 192, 0);
    TestLegacySymmetricKey(Provider, L"3DES_112", 128, 128, 0);
    TestPersistedKey(Provider, NCRYPT_RSA_ALGORITHM, 2048, 0);
    TestPersistedKey(Provider, NCRYPT_ECDSA_P256_ALGORITHM, 256, 0);
    TestPersistedKey(Provider, NCRYPT_RSA_ALGORITHM, 2048, NCRYPT_MACHINE_KEY_FLAG);
    NCryptFreeObject(Provider);
}

START_TEST(KeyStorageChild)
{
    NCRYPT_PROV_HANDLE Provider;
    NCRYPT_KEY_HANDLE Key;
    WCHAR KeyName[96], Mode[16];
    DWORD Result = 0;

    if (!GetEnvironmentVariableW(ChildKeyVariable, KeyName, ARRAYSIZE(KeyName)) ||
        !GetEnvironmentVariableW(ChildModeVariable, Mode, ARRAYSIZE(Mode)))
    {
        skip("KeyStorageChild runs only from KeyStorage\n");
        return;
    }
    if (OpenProvider(MS_KEY_STORAGE_PROVIDER, &Provider) == ERROR_SUCCESS)
    {
        if (NCryptOpenKey(Provider, &Key, KeyName, 0, !wcscmp(Mode, L"openmachine") ? NCRYPT_MACHINE_KEY_FLAG : 0) ==
            ERROR_SUCCESS)
        {
            Result = DwordProperty(Key, NCRYPT_LENGTH_PROPERTY);
            NCryptFreeObject(Key);
        }
        NCryptFreeObject(Provider);
    }
    ExitProcess(Result);
}
