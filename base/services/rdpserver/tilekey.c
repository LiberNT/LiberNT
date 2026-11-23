/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keyed hash that names a tile in the graphics cache
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "tilekey.h"
#include <bcrypt.h>

#define ROTL(x, b) (((x) << (b)) | ((x) >> (64 - (b))))

typedef struct SIP_STATE
{
    UINT64 V0, V1, V2, V3;
    UINT64 Bytes;
} SIP_STATE;

static void SipRound(SIP_STATE *s)
{
    s->V0 += s->V1; s->V1 = ROTL(s->V1, 13); s->V1 ^= s->V0; s->V0 = ROTL(s->V0, 32);
    s->V2 += s->V3; s->V3 = ROTL(s->V3, 16); s->V3 ^= s->V2;
    s->V0 += s->V3; s->V3 = ROTL(s->V3, 21); s->V3 ^= s->V0;
    s->V2 += s->V1; s->V1 = ROTL(s->V1, 17); s->V1 ^= s->V2; s->V2 = ROTL(s->V2, 32);
}

static void SipWord(SIP_STATE *s, UINT64 m)
{
    s->V3 ^= m;
    SipRound(s);
    SipRound(s);
    s->V0 ^= m;
    s->Bytes += 8;
}

BOOL TileKeySecret(TILE_KEY_SECRET *secret)
{
    return BCryptGenRandom(NULL, (PUCHAR)secret, sizeof(*secret), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

UINT64 TileKeyOf(const TILE_KEY_SECRET *secret, const BYTE *pixels, UINT32 stride, UINT32 left, UINT32 top,
                 UINT32 width, UINT32 height)
{
    SIP_STATE s;
    UINT64 word = 0;
    UINT32 x, y, half = 0;
    s.V0 = secret->K0 ^ 0x736f6d6570736575ull;
    s.V1 = secret->K1 ^ 0x646f72616e646f6dull;
    s.V2 = secret->K0 ^ 0x6c7967656e657261ull;
    s.V3 = secret->K1 ^ 0x7465646279746573ull;
    s.Bytes = 0;
    SipWord(&s, (UINT64)width << 32 | height);
    for (y = 0; y < height; ++y)
    {
        const UINT32 *row = (const UINT32 *)(pixels + (size_t)(top + y) * stride + (size_t)left * 4);
        for (x = 0; x < width; ++x)
        {
            word |= (UINT64)(row[x] & 0x00FFFFFF) << (half * 32);
            if (++half == 2)
            {
                SipWord(&s, word);
                word = 0;
                half = 0;
            }
        }
    }
    if (half) SipWord(&s, word);
    s.V3 ^= s.Bytes << 56;
    SipRound(&s);
    SipRound(&s);
    s.V0 ^= s.Bytes << 56;
    s.V2 ^= 0xff;
    SipRound(&s);
    SipRound(&s);
    SipRound(&s);
    SipRound(&s);
    return s.V0 ^ s.V1 ^ s.V2 ^ s.V3;
}
