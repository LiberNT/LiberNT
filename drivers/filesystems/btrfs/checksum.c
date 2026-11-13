/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Metadata checksums: CRC-32C, xxHash64, SHA-256 (FIPS 180-4) and BLAKE2b-256 (RFC 7693)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

static ULONG BtrfsCrc32cTable[256];
static BOOLEAN BtrfsCrc32cReady;

static
VOID
BtrfsInitCrc32c(VOID)
{
    ULONG Index, Bit, Value;

    for (Index = 0; Index < 256; Index++)
    {
        Value = Index;
        for (Bit = 0; Bit < 8; Bit++)
            Value = (Value & 1) ? (Value >> 1) ^ 0x82F63B78 : (Value >> 1);
        BtrfsCrc32cTable[Index] = Value;
    }
    BtrfsCrc32cReady = TRUE;
}

ULONG
BtrfsCrc32c(
    _In_ ULONG Seed,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ SIZE_T Length)
{
    const UCHAR *Bytes = Data;
    ULONG Crc = Seed;

    if (!BtrfsCrc32cReady) BtrfsInitCrc32c();

    while (Length-- != 0)
        Crc = BtrfsCrc32cTable[(Crc ^ *Bytes++) & 0xFF] ^ (Crc >> 8);

    return Crc;
}

ULONG
BtrfsNameHash(
    _In_reads_bytes_(Length) const CHAR *Name,
    _In_ SIZE_T Length)
{
    return BtrfsCrc32c(~1UL, Name, Length);
}

#define XXH_PRIME1 0x9E3779B185EBCA87ULL
#define XXH_PRIME2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME3 0x165667B19E3779F9ULL
#define XXH_PRIME4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME5 0x27D4EB2F165667C5ULL

static
ULONGLONG
BtrfsRotl64(
    _In_ ULONGLONG Value,
    _In_ ULONG Count)
{
    return (Value << Count) | (Value >> (64 - Count));
}

static
ULONGLONG
BtrfsRead64(
    _In_reads_bytes_(8) const UCHAR *Data)
{
    ULONGLONG Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static
ULONG
BtrfsRead32(
    _In_reads_bytes_(4) const UCHAR *Data)
{
    ULONG Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static
ULONGLONG
BtrfsXxhRound(
    _In_ ULONGLONG Accumulator,
    _In_ ULONGLONG Lane)
{
    Accumulator += Lane * XXH_PRIME2;
    return BtrfsRotl64(Accumulator, 31) * XXH_PRIME1;
}

static
ULONGLONG
BtrfsXxhMerge(
    _In_ ULONGLONG Accumulator,
    _In_ ULONGLONG Value)
{
    Accumulator ^= BtrfsXxhRound(0, Value);
    return Accumulator * XXH_PRIME1 + XXH_PRIME4;
}

static
ULONGLONG
BtrfsXxHash64(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ SIZE_T Length)
{
    const UCHAR *End = Data + Length;
    ULONGLONG Hash;

    if (Length >= 32)
    {
        ULONGLONG V1 = XXH_PRIME1 + XXH_PRIME2, V2 = XXH_PRIME2, V3 = 0, V4 = 0 - XXH_PRIME1;

        do
        {
            V1 = BtrfsXxhRound(V1, BtrfsRead64(Data));
            V2 = BtrfsXxhRound(V2, BtrfsRead64(Data + 8));
            V3 = BtrfsXxhRound(V3, BtrfsRead64(Data + 16));
            V4 = BtrfsXxhRound(V4, BtrfsRead64(Data + 24));
            Data += 32;
        } while (Data + 32 <= End);

        Hash = BtrfsRotl64(V1, 1) + BtrfsRotl64(V2, 7) + BtrfsRotl64(V3, 12) + BtrfsRotl64(V4, 18);
        Hash = BtrfsXxhMerge(Hash, V1);
        Hash = BtrfsXxhMerge(Hash, V2);
        Hash = BtrfsXxhMerge(Hash, V3);
        Hash = BtrfsXxhMerge(Hash, V4);
    }
    else
    {
        Hash = XXH_PRIME5;
    }

    Hash += Length;

    while (Data + 8 <= End)
    {
        Hash ^= BtrfsXxhRound(0, BtrfsRead64(Data));
        Hash = BtrfsRotl64(Hash, 27) * XXH_PRIME1 + XXH_PRIME4;
        Data += 8;
    }

    if (Data + 4 <= End)
    {
        Hash ^= (ULONGLONG)BtrfsRead32(Data) * XXH_PRIME1;
        Hash = BtrfsRotl64(Hash, 23) * XXH_PRIME2 + XXH_PRIME3;
        Data += 4;
    }

    while (Data < End)
    {
        Hash ^= (ULONGLONG)(*Data++) * XXH_PRIME5;
        Hash = BtrfsRotl64(Hash, 11) * XXH_PRIME1;
    }

    Hash ^= Hash >> 33;
    Hash *= XXH_PRIME2;
    Hash ^= Hash >> 29;
    Hash *= XXH_PRIME3;
    Hash ^= Hash >> 32;
    return Hash;
}

static const ULONG BtrfsSha256K[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static
ULONG
BtrfsRotr32(
    _In_ ULONG Value,
    _In_ ULONG Count)
{
    return (Value >> Count) | (Value << (32 - Count));
}

static
VOID
BtrfsSha256Block(
    _Inout_updates_(8) ULONG *State,
    _In_reads_bytes_(64) const UCHAR *Block)
{
    ULONG W[64], A, B, C, D, E, F, G, H, T1, T2;
    ULONG Index;

    for (Index = 0; Index < 16; Index++)
    {
        W[Index] = ((ULONG)Block[Index * 4] << 24) | ((ULONG)Block[Index * 4 + 1] << 16) |
                   ((ULONG)Block[Index * 4 + 2] << 8) | Block[Index * 4 + 3];
    }
    for (; Index < 64; Index++)
    {
        ULONG S0 = BtrfsRotr32(W[Index - 15], 7) ^ BtrfsRotr32(W[Index - 15], 18) ^ (W[Index - 15] >> 3);
        ULONG S1 = BtrfsRotr32(W[Index - 2], 17) ^ BtrfsRotr32(W[Index - 2], 19) ^ (W[Index - 2] >> 10);
        W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
    }

    A = State[0]; B = State[1]; C = State[2]; D = State[3];
    E = State[4]; F = State[5]; G = State[6]; H = State[7];

    for (Index = 0; Index < 64; Index++)
    {
        T1 = H + (BtrfsRotr32(E, 6) ^ BtrfsRotr32(E, 11) ^ BtrfsRotr32(E, 25)) + ((E & F) ^ (~E & G)) + BtrfsSha256K[Index] + W[Index];
        T2 = (BtrfsRotr32(A, 2) ^ BtrfsRotr32(A, 13) ^ BtrfsRotr32(A, 22)) + ((A & B) ^ (A & C) ^ (B & C));
        H = G; G = F; F = E; E = D + T1;
        D = C; C = B; B = A; A = T1 + T2;
    }

    State[0] += A; State[1] += B; State[2] += C; State[3] += D;
    State[4] += E; State[5] += F; State[6] += G; State[7] += H;
}

static
VOID
BtrfsSha256(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ SIZE_T Length,
    _Out_writes_bytes_(32) UCHAR *Digest)
{
    ULONG State[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    UCHAR Tail[128];
    SIZE_T Remaining = Length, TailLength;
    ULONGLONG Bits = (ULONGLONG)Length * 8;
    ULONG Index;

    while (Remaining >= 64)
    {
        BtrfsSha256Block(State, Data);
        Data += 64;
        Remaining -= 64;
    }

    RtlZeroMemory(Tail, sizeof(Tail));
    RtlCopyMemory(Tail, Data, Remaining);
    Tail[Remaining] = 0x80;
    TailLength = (Remaining < 56) ? 64 : 128;
    for (Index = 0; Index < 8; Index++)
        Tail[TailLength - 1 - Index] = (UCHAR)(Bits >> (Index * 8));

    BtrfsSha256Block(State, Tail);
    if (TailLength == 128) BtrfsSha256Block(State, Tail + 64);

    for (Index = 0; Index < 8; Index++)
    {
        Digest[Index * 4] = (UCHAR)(State[Index] >> 24);
        Digest[Index * 4 + 1] = (UCHAR)(State[Index] >> 16);
        Digest[Index * 4 + 2] = (UCHAR)(State[Index] >> 8);
        Digest[Index * 4 + 3] = (UCHAR)State[Index];
    }
}

static const ULONGLONG BtrfsBlake2bIv[8] =
{
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const UCHAR BtrfsBlake2bSigma[12][16] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 }
};

static
ULONGLONG
BtrfsRotr64(
    _In_ ULONGLONG Value,
    _In_ ULONG Count)
{
    return (Value >> Count) | (Value << (64 - Count));
}

static
VOID
BtrfsBlake2bCompress(
    _Inout_updates_(8) ULONGLONG *State,
    _In_reads_bytes_(128) const UCHAR *Block,
    _In_ ULONGLONG Counter,
    _In_ BOOLEAN Last)
{
    ULONGLONG V[16], M[16];
    ULONG Round, Index;

    for (Index = 0; Index < 16; Index++)
        M[Index] = BtrfsRead64(Block + Index * 8);
    for (Index = 0; Index < 8; Index++)
    {
        V[Index] = State[Index];
        V[Index + 8] = BtrfsBlake2bIv[Index];
    }
    V[12] ^= Counter;
    if (Last) V[14] = ~V[14];

#define BTRFS_B2_G(a, b, c, d, x, y) \
    do { \
        V[a] = V[a] + V[b] + (x); V[d] = BtrfsRotr64(V[d] ^ V[a], 32); \
        V[c] = V[c] + V[d]; V[b] = BtrfsRotr64(V[b] ^ V[c], 24); \
        V[a] = V[a] + V[b] + (y); V[d] = BtrfsRotr64(V[d] ^ V[a], 16); \
        V[c] = V[c] + V[d]; V[b] = BtrfsRotr64(V[b] ^ V[c], 63); \
    } while (0)

    for (Round = 0; Round < 12; Round++)
    {
        const UCHAR *S = BtrfsBlake2bSigma[Round];

        BTRFS_B2_G(0, 4, 8, 12, M[S[0]], M[S[1]]);
        BTRFS_B2_G(1, 5, 9, 13, M[S[2]], M[S[3]]);
        BTRFS_B2_G(2, 6, 10, 14, M[S[4]], M[S[5]]);
        BTRFS_B2_G(3, 7, 11, 15, M[S[6]], M[S[7]]);
        BTRFS_B2_G(0, 5, 10, 15, M[S[8]], M[S[9]]);
        BTRFS_B2_G(1, 6, 11, 12, M[S[10]], M[S[11]]);
        BTRFS_B2_G(2, 7, 8, 13, M[S[12]], M[S[13]]);
        BTRFS_B2_G(3, 4, 9, 14, M[S[14]], M[S[15]]);
    }

#undef BTRFS_B2_G

    for (Index = 0; Index < 8; Index++)
        State[Index] ^= V[Index] ^ V[Index + 8];
}

static
VOID
BtrfsBlake2b256(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ SIZE_T Length,
    _Out_writes_bytes_(32) UCHAR *Digest)
{
    ULONGLONG State[8];
    UCHAR Block[128];
    ULONGLONG Counter = 0;
    ULONG Index;

    for (Index = 0; Index < 8; Index++)
        State[Index] = BtrfsBlake2bIv[Index];
    State[0] ^= 0x01010000ULL ^ 32;

    while (Length > 128)
    {
        Counter += 128;
        BtrfsBlake2bCompress(State, Data, Counter, FALSE);
        Data += 128;
        Length -= 128;
    }

    RtlZeroMemory(Block, sizeof(Block));
    RtlCopyMemory(Block, Data, Length);
    Counter += Length;
    BtrfsBlake2bCompress(State, Block, Counter, TRUE);

    RtlCopyMemory(Digest, State, 32);
}

BOOLEAN
BtrfsChecksumSupported(
    _In_ USHORT Type)
{
    return Type == BTRFS_CSUM_TYPE_CRC32 || Type == BTRFS_CSUM_TYPE_XXHASH ||
           Type == BTRFS_CSUM_TYPE_SHA256 || Type == BTRFS_CSUM_TYPE_BLAKE2;
}

ULONG
BtrfsChecksumSize(
    _In_ USHORT Type)
{
    switch (Type)
    {
        case BTRFS_CSUM_TYPE_CRC32: return 4;
        case BTRFS_CSUM_TYPE_XXHASH: return 8;
        case BTRFS_CSUM_TYPE_SHA256: return 32;
        case BTRFS_CSUM_TYPE_BLAKE2: return 32;
        default: return 0;
    }
}

VOID
BtrfsComputeChecksum(
    _In_ USHORT Type,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ SIZE_T Length,
    _Out_writes_bytes_(BTRFS_CSUM_SIZE) UCHAR *Digest)
{
    ASSERT(BtrfsChecksumSupported(Type));
    RtlZeroMemory(Digest, BTRFS_CSUM_SIZE);

    switch (Type)
    {
        case BTRFS_CSUM_TYPE_CRC32:
        {
            ULONG Crc = ~BtrfsCrc32c(~0UL, Data, Length);
            RtlCopyMemory(Digest, &Crc, sizeof(Crc));
            break;
        }
        case BTRFS_CSUM_TYPE_XXHASH:
        {
            ULONGLONG Hash = BtrfsXxHash64(Data, Length);
            RtlCopyMemory(Digest, &Hash, sizeof(Hash));
            break;
        }
        case BTRFS_CSUM_TYPE_SHA256:
            BtrfsSha256(Data, Length, Digest);
            break;
        case BTRFS_CSUM_TYPE_BLAKE2:
            BtrfsBlake2b256(Data, Length, Digest);
            break;
        default:
            break;
    }
}

BOOLEAN
BtrfsVerifyChecksum(
    _In_ USHORT Type,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ SIZE_T Length,
    _In_reads_bytes_(BTRFS_CSUM_SIZE) const UCHAR *Expected)
{
    UCHAR Digest[BTRFS_CSUM_SIZE];
    ULONG Size = BtrfsChecksumSize(Type);

    if (!BtrfsChecksumSupported(Type)) return FALSE;
    BtrfsComputeChecksum(Type, Data, Length, Digest);
    return RtlCompareMemory(Digest, Expected, Size) == Size;
}
