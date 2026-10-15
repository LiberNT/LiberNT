/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHA-256 (FIPS 180-4)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rsna_sha256.h"

static const rsna_u32 RsnaSha256K[64] =
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

#define RSNA_ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void RsnaSha256Block(RSNA_SHA256_CTX *ctx, const rsna_u8 *block)
{
    rsna_u32 w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
    {
        w[i] = ((rsna_u32)block[i * 4] << 24) | ((rsna_u32)block[i * 4 + 1] << 16) |
               ((rsna_u32)block[i * 4 + 2] << 8) | (rsna_u32)block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++)
    {
        rsna_u32 s0 = RSNA_ROR32(w[i - 15], 7) ^ RSNA_ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        rsna_u32 s1 = RSNA_ROR32(w[i - 2], 17) ^ RSNA_ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++)
    {
        t1 = h + (RSNA_ROR32(e, 6) ^ RSNA_ROR32(e, 11) ^ RSNA_ROR32(e, 25)) +
             ((e & f) ^ (~e & g)) + RsnaSha256K[i] + w[i];
        t2 = (RSNA_ROR32(a, 2) ^ RSNA_ROR32(a, 13) ^ RSNA_ROR32(a, 22)) +
             ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void RsnaSha256Init(RSNA_SHA256_CTX *ctx)
{
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

void RsnaSha256Update(RSNA_SHA256_CTX *ctx, const void *data, rsna_size len)
{
    const rsna_u8 *in = (const rsna_u8 *)data;
    rsna_size fill = (rsna_size)(ctx->count % RSNA_SHA256_BLOCK_LEN);

    ctx->count += len;
    while (len > 0)
    {
        rsna_size take = RSNA_SHA256_BLOCK_LEN - fill;
        rsna_size i;

        if (take > len)
            take = len;
        for (i = 0; i < take; i++)
            ctx->buffer[fill + i] = in[i];
        fill += take;
        in += take;
        len -= take;
        if (fill == RSNA_SHA256_BLOCK_LEN)
        {
            RsnaSha256Block(ctx, ctx->buffer);
            fill = 0;
        }
    }
}

void RsnaSha256Final(RSNA_SHA256_CTX *ctx, rsna_u8 digest[RSNA_SHA256_DIGEST_LEN])
{
    rsna_u64 bits = ctx->count * 8;
    rsna_u8 pad = 0x80, zero = 0, length[8];
    int i;

    RsnaSha256Update(ctx, &pad, 1);
    while (ctx->count % RSNA_SHA256_BLOCK_LEN != 56)
        RsnaSha256Update(ctx, &zero, 1);
    for (i = 0; i < 8; i++)
        length[i] = (rsna_u8)(bits >> (56 - i * 8));
    RsnaSha256Update(ctx, length, 8);

    for (i = 0; i < 8; i++)
    {
        digest[i * 4] = (rsna_u8)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (rsna_u8)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (rsna_u8)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (rsna_u8)ctx->state[i];
    }
}

void RsnaSha256(const void *data, rsna_size len, rsna_u8 digest[RSNA_SHA256_DIGEST_LEN])
{
    RSNA_SHA256_CTX ctx;

    RsnaSha256Init(&ctx);
    RsnaSha256Update(&ctx, data, len);
    RsnaSha256Final(&ctx, digest);
}
