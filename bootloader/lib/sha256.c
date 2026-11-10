/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHA-256 (FIPS 180-4) for the integrity checks of the boot applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <sha256.h>

static const ULONG BlSha256K[64] =
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

#define ROTR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))

static
VOID
BlSha256Block(
    _Inout_ PBL_SHA256 Context,
    _In_reads_bytes_(64) const UCHAR* Block)
{
    ULONG W[64], A, B, C, D, E, F, G, H, T1, T2;
    ULONG i;

    for (i = 0; i < 16; i++)
    {
        W[i] = ((ULONG)Block[i * 4] << 24) | ((ULONG)Block[i * 4 + 1] << 16) |
               ((ULONG)Block[i * 4 + 2] << 8) | (ULONG)Block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++)
    {
        W[i] = (ROTR(W[i - 2], 17) ^ ROTR(W[i - 2], 19) ^ (W[i - 2] >> 10)) + W[i - 7] +
               (ROTR(W[i - 15], 7) ^ ROTR(W[i - 15], 18) ^ (W[i - 15] >> 3)) + W[i - 16];
    }

    A = Context->State[0];
    B = Context->State[1];
    C = Context->State[2];
    D = Context->State[3];
    E = Context->State[4];
    F = Context->State[5];
    G = Context->State[6];
    H = Context->State[7];

    for (i = 0; i < 64; i++)
    {
        T1 = H + (ROTR(E, 6) ^ ROTR(E, 11) ^ ROTR(E, 25)) + ((E & F) ^ (~E & G)) + BlSha256K[i] + W[i];
        T2 = (ROTR(A, 2) ^ ROTR(A, 13) ^ ROTR(A, 22)) + ((A & B) ^ (A & C) ^ (B & C));
        H = G;
        G = F;
        F = E;
        E = D + T1;
        D = C;
        C = B;
        B = A;
        A = T1 + T2;
    }

    Context->State[0] += A;
    Context->State[1] += B;
    Context->State[2] += C;
    Context->State[3] += D;
    Context->State[4] += E;
    Context->State[5] += F;
    Context->State[6] += G;
    Context->State[7] += H;
}

VOID
BlSha256Init(
    _Out_ PBL_SHA256 Context)
{
    static const ULONG Initial[8] =
    {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    RtlCopyMemory(Context->State, Initial, sizeof(Initial));
    Context->Length = 0;
    Context->Used = 0;
}

VOID
BlSha256Update(
    _Inout_ PBL_SHA256 Context,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ SIZE_T Length)
{
    const UCHAR* Bytes = Data;
    SIZE_T Take;

    Context->Length += Length;
    while (Length != 0)
    {
        if (Context->Used == 0 && Length >= sizeof(Context->Block))
        {
            BlSha256Block(Context, Bytes);
            Bytes += sizeof(Context->Block);
            Length -= sizeof(Context->Block);
            continue;
        }

        Take = min(Length, sizeof(Context->Block) - Context->Used);
        RtlCopyMemory(Context->Block + Context->Used, Bytes, Take);
        Context->Used += (ULONG)Take;
        Bytes += Take;
        Length -= Take;
        if (Context->Used == sizeof(Context->Block))
        {
            BlSha256Block(Context, Context->Block);
            Context->Used = 0;
        }
    }
}

VOID
BlSha256Final(
    _Inout_ PBL_SHA256 Context,
    _Out_writes_bytes_(BL_SHA256_SIZE) PUCHAR Digest)
{
    ULONGLONG Bits = Context->Length * 8;
    ULONG i;

    Context->Block[Context->Used++] = 0x80;
    if (Context->Used > 56)
    {
        RtlZeroMemory(Context->Block + Context->Used, sizeof(Context->Block) - Context->Used);
        BlSha256Block(Context, Context->Block);
        Context->Used = 0;
    }
    RtlZeroMemory(Context->Block + Context->Used, 56 - Context->Used);
    for (i = 0; i < 8; i++)
        Context->Block[56 + i] = (UCHAR)(Bits >> (56 - i * 8));
    BlSha256Block(Context, Context->Block);

    for (i = 0; i < 8; i++)
    {
        Digest[i * 4] = (UCHAR)(Context->State[i] >> 24);
        Digest[i * 4 + 1] = (UCHAR)(Context->State[i] >> 16);
        Digest[i * 4 + 2] = (UCHAR)(Context->State[i] >> 8);
        Digest[i * 4 + 3] = (UCHAR)Context->State[i];
    }
}
