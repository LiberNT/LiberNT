/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Cryptographic primitives of the debugger session, backed by SymCrypt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdnet.h"
#include "symcrypt.h"
#include "sc_lib.h"

#define KDNET_ARENA_SIZE        0x20000
#define KDNET_ARENA_ALIGNMENT   64
#define KDNET_ARENA_FREE        0x80000000

typedef struct _KDNET_ARENA_BLOCK
{
    ULONG Size;
    ULONG Previous;
} KDNET_ARENA_BLOCK, *PKDNET_ARENA_BLOCK;

UINT32 g_SymCryptFipsSelftestsPerformed;

SYMCRYPT_ENVIRONMENT_DEFS(Generic);

static DECLSPEC_ALIGN(KDNET_ARENA_ALIGNMENT) UCHAR KdpArena[KDNET_ARENA_SIZE];
static ULONG KdpArenaTop;
static ULONG KdpArenaLast = MAXULONG;
static UCHAR KdpArenaMutex;

static PSYMCRYPT_ECURVE KdpCurve;
static PSYMCRYPT_ECKEY KdpOwnKey;
static PSYMCRYPT_ECKEY KdpPeerKey;

static UCHAR KdpRandomKey[KDNET_HASH_SIZE];
static ULONG64 KdpRandomCounter;

PVOID
SYMCRYPT_CALL
SymCryptCallbackAlloc(
    SIZE_T Size)
{
    PKDNET_ARENA_BLOCK Block;
    ULONG Total;

    if (Size > KDNET_ARENA_SIZE)
        return NULL;

    Total = (ULONG)((Size + KDNET_ARENA_ALIGNMENT - 1) & ~(SIZE_T)(KDNET_ARENA_ALIGNMENT - 1)) +
            KDNET_ARENA_ALIGNMENT;
    if (Total > KDNET_ARENA_SIZE - KdpArenaTop)
        return NULL;

    Block = (PKDNET_ARENA_BLOCK)(KdpArena + KdpArenaTop + KDNET_ARENA_ALIGNMENT - sizeof(*Block));
    Block->Size = Total;
    Block->Previous = KdpArenaLast;
    KdpArenaLast = KdpArenaTop;
    KdpArenaTop += Total;
    RtlZeroMemory(Block + 1, Total - KDNET_ARENA_ALIGNMENT);
    return Block + 1;
}

VOID
SYMCRYPT_CALL
SymCryptCallbackFree(
    PVOID Pointer)
{
    PKDNET_ARENA_BLOCK Block;

    if (!Pointer)
        return;

    Block = (PKDNET_ARENA_BLOCK)Pointer - 1;
    RtlSecureZeroMemory(Pointer, (Block->Size & ~KDNET_ARENA_FREE) - KDNET_ARENA_ALIGNMENT);
    Block->Size |= KDNET_ARENA_FREE;

    while (KdpArenaLast != MAXULONG)
    {
        Block = (PKDNET_ARENA_BLOCK)(KdpArena + KdpArenaLast + KDNET_ARENA_ALIGNMENT - sizeof(*Block));
        if (!(Block->Size & KDNET_ARENA_FREE))
            break;

        KdpArenaTop = KdpArenaLast;
        KdpArenaLast = Block->Previous;
    }
}

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptCallbackRandom(
    PBYTE Buffer,
    SIZE_T Size)
{
    KdCryptoRandom(Buffer, (ULONG)Size);
    return SYMCRYPT_NO_ERROR;
}

PVOID
SYMCRYPT_CALL
SymCryptCallbackAllocateMutexFastInproc(VOID)
{
    return &KdpArenaMutex;
}

VOID
SYMCRYPT_CALL
SymCryptCallbackFreeMutexFastInproc(
    PVOID Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

VOID
SYMCRYPT_CALL
SymCryptCallbackAcquireMutexFastInproc(
    PVOID Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

VOID
SYMCRYPT_CALL
SymCryptCallbackReleaseMutexFastInproc(
    PVOID Mutex)
{
    UNREFERENCED_PARAMETER(Mutex);
}

SYMCRYPT_CPU_FEATURES
SYMCRYPT_CALL
SymCryptCpuFeaturesNeverPresentEnvGeneric(VOID)
{
    return (SYMCRYPT_CPU_FEATURES)~0;
}

VOID
SYMCRYPT_CALL
SymCryptInitEnvGeneric(
    UINT32 Version)
{
    if (g_SymCryptFlags & SYMCRYPT_FLAG_LIB_INITIALIZED)
        return;

    g_SymCryptCpuFeaturesNotPresent = (SYMCRYPT_CPU_FEATURES)~0;
    SymCryptInitEnvCommon(Version);
}

DECLSPEC_NORETURN
VOID
SYMCRYPT_CALL
SymCryptFatalEnvGeneric(
    UINT32 FatalCode)
{
    KeBugCheckEx(KERNEL_SECURITY_CHECK_FAILURE, FatalCode, 0, 0, 0);
    for (;;)
        NOTHING;
}

#if SYMCRYPT_CPU_AMD64 | SYMCRYPT_CPU_X86
SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptSaveXmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

VOID
SYMCRYPT_CALL
SymCryptRestoreXmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
}

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptSaveYmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

VOID
SYMCRYPT_CALL
SymCryptRestoreYmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
}

VOID
SYMCRYPT_CALL
SymCryptCpuidExFuncEnvGeneric(
    int CpuInfo[4],
    int Function,
    int SubFunction)
{
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(SubFunction);
    CpuInfo[0] = CpuInfo[1] = CpuInfo[2] = CpuInfo[3] = 0;
}
#endif

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptEcDsaPct(
    PCSYMCRYPT_ECKEY Key)
{
    UNREFERENCED_PARAMETER(Key);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

VOID
SYMCRYPT_CALL
SymCryptEcDsaSelftest(VOID)
{
    SymCryptFatal('EcdT');
}

VOID
SYMCRYPT_CALL
SymCryptEcDhSecretAgreementSelftest(VOID)
{
    SymCryptFatal('EDhT');
}

VOID
SYMCRYPT_CALL
SymCryptTestInjectErrorEnvGeneric(
    PBYTE Buffer,
    SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
}

VOID
KdCryptoHash(
    _In_reads_bytes_opt_(Length1) const UCHAR *Data1,
    _In_ ULONG Length1,
    _In_reads_bytes_opt_(Length2) const UCHAR *Data2,
    _In_ ULONG Length2,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Hash)
{
    SYMCRYPT_SHA256_STATE State;

    SymCryptSha256Init(&State);
    if (Length1)
        SymCryptSha256Append(&State, Data1, Length1);
    if (Length2)
        SymCryptSha256Append(&State, Data2, Length2);
    SymCryptSha256Result(&State, Hash);
}

VOID
KdCryptoHmac(
    _In_reads_bytes_(KeyLength) const UCHAR *Key,
    _In_ ULONG KeyLength,
    _In_reads_bytes_opt_(Length1) const UCHAR *Data1,
    _In_ ULONG Length1,
    _In_reads_bytes_opt_(Length2) const UCHAR *Data2,
    _In_ ULONG Length2,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Mac)
{
    SYMCRYPT_HMAC_SHA256_EXPANDED_KEY Expanded;
    SYMCRYPT_HMAC_SHA256_STATE State;

    SymCryptHmacSha256ExpandKey(&Expanded, Key, KeyLength);
    SymCryptHmacSha256Init(&State, &Expanded);
    if (Length1)
        SymCryptHmacSha256Append(&State, Data1, Length1);
    if (Length2)
        SymCryptHmacSha256Append(&State, Data2, Length2);
    SymCryptHmacSha256Result(&State, Mac);
    SymCryptWipe(&Expanded, sizeof(Expanded));
}

VOID
KdCryptoHkdf(
    _In_reads_(KDNET_HASH_SIZE) const UCHAR *ChainingKey,
    _In_reads_bytes_opt_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Output1,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Output2,
    _Out_writes_opt_(KDNET_HASH_SIZE) PUCHAR Output3)
{
    UCHAR Temporary[KDNET_HASH_SIZE], First[KDNET_HASH_SIZE], Second[KDNET_HASH_SIZE];
    static const UCHAR One = 1, Two = 2, Three = 3;

    KdCryptoHmac(ChainingKey, KDNET_HASH_SIZE, Input, Length, NULL, 0, Temporary);
    KdCryptoHmac(Temporary, sizeof(Temporary), &One, 1, NULL, 0, First);
    KdCryptoHmac(Temporary, sizeof(Temporary), First, sizeof(First), &Two, 1, Second);
    if (Output3)
        KdCryptoHmac(Temporary, sizeof(Temporary), Second, sizeof(Second), &Three, 1, Output3);

    RtlCopyMemory(Output1, First, sizeof(First));
    RtlCopyMemory(Output2, Second, sizeof(Second));
    SymCryptWipe(Temporary, sizeof(Temporary));
    SymCryptWipe(First, sizeof(First));
    SymCryptWipe(Second, sizeof(Second));
}

static
VOID
KdpNonce(
    _In_ ULONG64 Counter,
    _Out_writes_(12) PUCHAR Nonce)
{
    KdNetPutLe32(Nonce, 0);
    KdNetPutLe64(Nonce + 4, Counter);
}

VOID
KdCryptoSeal(
    _In_reads_(KDNET_KEY_SIZE) const UCHAR *Key,
    _In_ ULONG64 Counter,
    _In_reads_bytes_opt_(AssociatedLength) const UCHAR *Associated,
    _In_ ULONG AssociatedLength,
    _In_reads_bytes_(Length) const UCHAR *Plain,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length + KDNET_TAG_SIZE) PUCHAR Cipher)
{
    UCHAR Nonce[12];

    KdpNonce(Counter, Nonce);
    SymCryptChaCha20Poly1305Encrypt(Key, KDNET_KEY_SIZE, Nonce, sizeof(Nonce),
                                    Associated, AssociatedLength,
                                    Plain, Cipher, Length,
                                    Cipher + Length, KDNET_TAG_SIZE);
}

BOOLEAN
KdCryptoOpen(
    _In_reads_(KDNET_KEY_SIZE) const UCHAR *Key,
    _In_ ULONG64 Counter,
    _In_reads_bytes_opt_(AssociatedLength) const UCHAR *Associated,
    _In_ ULONG AssociatedLength,
    _In_reads_bytes_(Length) const UCHAR *Cipher,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length - KDNET_TAG_SIZE) PUCHAR Plain)
{
    UCHAR Nonce[12];

    if (Length < KDNET_TAG_SIZE)
        return FALSE;

    KdpNonce(Counter, Nonce);
    return SymCryptChaCha20Poly1305Decrypt(Key, KDNET_KEY_SIZE, Nonce, sizeof(Nonce),
                                           Associated, AssociatedLength,
                                           Cipher, Plain, Length - KDNET_TAG_SIZE,
                                           Cipher + Length - KDNET_TAG_SIZE,
                                           KDNET_TAG_SIZE) == SYMCRYPT_NO_ERROR;
}

BOOLEAN
KdCryptoExchange(
    _In_reads_(KDNET_DH_SIZE) const UCHAR *PeerPublic,
    _Out_writes_(KDNET_DH_SIZE) PUCHAR Public,
    _Out_writes_(KDNET_DH_SIZE) PUCHAR Secret)
{
    if (!KdpOwnKey || !KdpPeerKey)
        return FALSE;

    if (SymCryptEckeySetValue(NULL, 0, PeerPublic, KDNET_DH_SIZE,
                              SYMCRYPT_NUMBER_FORMAT_LSB_FIRST, SYMCRYPT_ECPOINT_FORMAT_X,
                              SYMCRYPT_FLAG_ECKEY_ECDH | SYMCRYPT_FLAG_KEY_NO_FIPS,
                              KdpPeerKey) != SYMCRYPT_NO_ERROR)
    {
        return FALSE;
    }

    if (SymCryptEckeySetRandom(SYMCRYPT_FLAG_ECKEY_ECDH | SYMCRYPT_FLAG_KEY_NO_FIPS,
                               KdpOwnKey) != SYMCRYPT_NO_ERROR)
    {
        return FALSE;
    }

    if (SymCryptEckeyGetValue(KdpOwnKey, NULL, 0, Public, KDNET_DH_SIZE,
                              SYMCRYPT_NUMBER_FORMAT_LSB_FIRST, SYMCRYPT_ECPOINT_FORMAT_X,
                              0) != SYMCRYPT_NO_ERROR)
    {
        return FALSE;
    }

    return SymCryptEcDhSecretAgreement(KdpOwnKey, KdpPeerKey, SYMCRYPT_NUMBER_FORMAT_LSB_FIRST,
                                       0, Secret, KDNET_DH_SIZE) == SYMCRYPT_NO_ERROR;
}

BOOLEAN
KdCryptoEqual(
    _In_reads_bytes_(Length) const UCHAR *Left,
    _In_reads_bytes_(Length) const UCHAR *Right,
    _In_ ULONG Length)
{
    return SymCryptEqual(Left, Right, Length) != 0;
}

VOID
KdCryptoWipe(
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length)
{
    SymCryptWipe(Data, Length);
}

VOID
KdCryptoAddEntropy(
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    KdCryptoHmac(KdpRandomKey, sizeof(KdpRandomKey), Data, Length, NULL, 0, KdpRandomKey);
}

VOID
KdCryptoRandom(
    _Out_writes_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    static const UCHAR Output = 'o', Rekey = 'k';
    UCHAR Block[KDNET_HASH_SIZE], Counter[8];
    ULONG Chunk;

    while (Length)
    {
        KdNetPutLe64(Counter, KdpRandomCounter++);
        KdCryptoHmac(KdpRandomKey, sizeof(KdpRandomKey), Counter, sizeof(Counter), &Output, 1, Block);
        Chunk = min(Length, sizeof(Block));
        RtlCopyMemory(Data, Block, Chunk);
        Data += Chunk;
        Length -= Chunk;
    }

    KdNetPutLe64(Counter, KdpRandomCounter++);
    KdCryptoHmac(KdpRandomKey, sizeof(KdpRandomKey), Counter, sizeof(Counter), &Rekey, 1, KdpRandomKey);
    SymCryptWipe(Block, sizeof(Block));
}

NTSTATUS
KdCryptoInitialize(VOID)
{
    SymCryptInit();

    KdpCurve = SymCryptEcurveAllocate(SymCryptEcurveParamsCurve25519, 0);
    if (!KdpCurve)
        return STATUS_INSUFFICIENT_RESOURCES;

    KdpOwnKey = SymCryptEckeyAllocate(KdpCurve);
    KdpPeerKey = SymCryptEckeyAllocate(KdpCurve);
    if (!KdpOwnKey || !KdpPeerKey)
        return STATUS_INSUFFICIENT_RESOURCES;

    return STATUS_SUCCESS;
}
