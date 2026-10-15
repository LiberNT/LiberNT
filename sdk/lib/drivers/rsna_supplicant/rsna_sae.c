/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Simultaneous Authentication of Equals over group 19 (IEEE 802.11-2020 12.4)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rsna_sae.h"
#include "rsna_hmac.h"
#include "rsna_kdf.h"

static const rsna_u8 RsnaSaePrimeBytes[RSNA_P256_BYTES] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static void RsnaSaeWipe(void *buffer, rsna_size length)
{
    volatile rsna_u8 *p = (volatile rsna_u8 *)buffer;

    while (length--)
        *p++ = 0;
}

static int RsnaSaeCompare(const rsna_u8 *a, const rsna_u8 *b, rsna_size length)
{
    rsna_size i;

    for (i = 0; i < length; i++)
    {
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

static int RsnaSaeEqualConstantTime(const rsna_u8 *a, const rsna_u8 *b, rsna_size length)
{
    rsna_u8 diff = 0;
    rsna_size i;

    for (i = 0; i < length; i++)
        diff |= (rsna_u8)(a[i] ^ b[i]);
    return diff == 0;
}

int RsnaSaeDerivePwe(RSNA_SAE *sae, const rsna_u8 ownAddress[6], const rsna_u8 peerAddress[6],
                     const rsna_u8 *password, rsna_size passwordLen)
{
    rsna_u8 addresses[12], seed[RSNA_SHA256_DIGEST_LEN], value[RSNA_P256_BYTES], savedX[RSNA_P256_BYTES];
    const rsna_u8 *addr[2];
    rsna_size len[2];
    RSNA_P256_FE x, rhs, y;
    rsna_u8 counter, savedLsb = 0;
    int found = 0, i;

    if (RsnaSaeCompare(ownAddress, peerAddress, 6) > 0)
    {
        for (i = 0; i < 6; i++)
        {
            addresses[i] = ownAddress[i];
            addresses[6 + i] = peerAddress[i];
        }
    }
    else
    {
        for (i = 0; i < 6; i++)
        {
            addresses[i] = peerAddress[i];
            addresses[6 + i] = ownAddress[i];
        }
    }

    addr[0] = password;
    len[0] = passwordLen;
    addr[1] = &counter;
    len[1] = 1;

    for (counter = 1; counter <= RSNA_SAE_HNP_ITERATIONS; counter++)
    {
        RsnaHmacSha256Vector(addresses, sizeof(addresses), 2, addr, len, seed);
        RsnaKdfSha256(seed, sizeof(seed), "SAE Hunting and Pecking",
                      RsnaSaePrimeBytes, sizeof(RsnaSaePrimeBytes), value, 256);
        if (!RsnaP256FieldFromBytes(&x, value))
            continue;
        RsnaP256CurveRhs(&rhs, &x);
        if (RsnaP256IsSquare(&rhs) && !found)
        {
            for (i = 0; i < RSNA_P256_BYTES; i++)
                savedX[i] = value[i];
            savedLsb = seed[RSNA_SHA256_DIGEST_LEN - 1] & 1;
            found = 1;
        }
    }

    RsnaSaeWipe(seed, sizeof(seed));
    RsnaSaeWipe(value, sizeof(value));
    if (!found)
        return 0;

    RsnaP256FieldFromBytes(&x, savedX);
    RsnaSaeWipe(savedX, sizeof(savedX));
    RsnaP256CurveRhs(&rhs, &x);
    RsnaP256Sqrt(&y, &rhs);
    if ((rsna_u8)RsnaP256FieldIsOdd(&y) != savedLsb)
        RsnaP256FieldNeg(&y, &y);

    RsnaP256PointSetAffine(&sae->Pwe, &x, &y);
    sae->HavePwe = 1;
    sae->HaveCommit = 0;
    sae->HaveKeys = 0;
    return 1;
}

int RsnaSaeSetCommit(RSNA_SAE *sae, const rsna_u8 rand[RSNA_SAE_SCALAR_LEN],
                     const rsna_u8 mask[RSNA_SAE_SCALAR_LEN])
{
    RSNA_P256_POINT element;
    int i;

    if (!sae->HavePwe || !RsnaP256ScalarIsValid(rand) || !RsnaP256ScalarIsValid(mask))
        return 0;

    RsnaP256ScalarAdd(sae->Scalar, rand, mask);
    if (!RsnaP256ScalarIsValid(sae->Scalar))
        return 0;

    RsnaP256PointMul(&element, &sae->Pwe, mask);
    RsnaP256PointNeg(&element, &element);
    if (!RsnaP256PointToBytes(sae->Element, sae->Element + RSNA_P256_BYTES, &element))
        return 0;

    for (i = 0; i < RSNA_SAE_SCALAR_LEN; i++)
        sae->Rand[i] = rand[i];
    sae->HaveCommit = 1;
    sae->HaveKeys = 0;
    return 1;
}

int RsnaSaeGenerateCommit(RSNA_SAE *sae, RSNA_SAE_RANDOM_FN random, void *context)
{
    rsna_u8 rand[RSNA_SAE_SCALAR_LEN], mask[RSNA_SAE_SCALAR_LEN];
    int attempt, ok = 0;

    for (attempt = 0; attempt < 64 && !ok; attempt++)
    {
        if (random(context, rand, sizeof(rand)) != 0 || random(context, mask, sizeof(mask)) != 0)
            break;
        if (!RsnaP256ScalarIsValid(rand) || !RsnaP256ScalarIsValid(mask))
            continue;
        ok = RsnaSaeSetCommit(sae, rand, mask);
    }

    RsnaSaeWipe(rand, sizeof(rand));
    RsnaSaeWipe(mask, sizeof(mask));
    return ok;
}

int RsnaSaeProcessCommit(RSNA_SAE *sae, const rsna_u8 peerScalar[RSNA_SAE_SCALAR_LEN],
                         const rsna_u8 peerElement[RSNA_SAE_ELEMENT_LEN])
{
    static const rsna_u8 zeroKey[RSNA_SHA256_DIGEST_LEN] = { 0 };
    RSNA_P256_POINT peer, k;
    rsna_u8 kx[RSNA_P256_BYTES], ky[RSNA_P256_BYTES], keyseed[RSNA_SHA256_DIGEST_LEN];
    rsna_u8 value[RSNA_P256_BYTES], keys[RSNA_SAE_KCK_LEN + RSNA_SAE_PMK_LEN];
    int i, ok = 0;

    if (!sae->HaveCommit || !RsnaP256ScalarIsValid(peerScalar))
        return 0;
    if (!RsnaP256PointFromBytes(&peer, peerElement, peerElement + RSNA_P256_BYTES))
        return 0;
    if (RsnaSaeEqualConstantTime(peerScalar, sae->Scalar, RSNA_SAE_SCALAR_LEN) &&
        RsnaSaeEqualConstantTime(peerElement, sae->Element, RSNA_SAE_ELEMENT_LEN))
    {
        return 0;
    }

    RsnaP256PointMul(&k, &sae->Pwe, peerScalar);
    RsnaP256PointAdd(&k, &k, &peer);
    RsnaP256PointMul(&k, &k, sae->Rand);
    if (RsnaP256PointToBytes(kx, ky, &k))
    {
        RsnaHmacSha256(zeroKey, sizeof(zeroKey), kx, sizeof(kx), keyseed);
        RsnaP256ScalarAdd(value, sae->Scalar, peerScalar);
        RsnaKdfSha256(keyseed, sizeof(keyseed), "SAE KCK and PMK", value, sizeof(value), keys, 512);
        for (i = 0; i < RSNA_SAE_KCK_LEN; i++)
            sae->Kck[i] = keys[i];
        for (i = 0; i < RSNA_SAE_PMK_LEN; i++)
            sae->Pmk[i] = keys[RSNA_SAE_KCK_LEN + i];
        for (i = 0; i < RSNA_SAE_PMKID_LEN; i++)
            sae->Pmkid[i] = value[i];
        for (i = 0; i < RSNA_SAE_SCALAR_LEN; i++)
            sae->PeerScalar[i] = peerScalar[i];
        for (i = 0; i < RSNA_SAE_ELEMENT_LEN; i++)
            sae->PeerElement[i] = peerElement[i];
        sae->HaveKeys = 1;
        ok = 1;
    }

    RsnaSaeWipe(kx, sizeof(kx));
    RsnaSaeWipe(ky, sizeof(ky));
    RsnaSaeWipe(keyseed, sizeof(keyseed));
    RsnaSaeWipe(keys, sizeof(keys));
    RsnaSaeWipe(&k, sizeof(k));
    return ok;
}

static void RsnaSaeConfirmHash(const rsna_u8 kck[RSNA_SAE_KCK_LEN], rsna_u16 sendConfirm,
                               const rsna_u8 *scalar1, const rsna_u8 *element1,
                               const rsna_u8 *scalar2, const rsna_u8 *element2,
                               rsna_u8 confirm[RSNA_SAE_CONFIRM_LEN])
{
    rsna_u8 counter[2];
    const rsna_u8 *addr[5];
    rsna_size len[5];

    counter[0] = (rsna_u8)sendConfirm;
    counter[1] = (rsna_u8)(sendConfirm >> 8);
    addr[0] = counter;
    len[0] = 2;
    addr[1] = scalar1;
    len[1] = RSNA_SAE_SCALAR_LEN;
    addr[2] = element1;
    len[2] = RSNA_SAE_ELEMENT_LEN;
    addr[3] = scalar2;
    len[3] = RSNA_SAE_SCALAR_LEN;
    addr[4] = element2;
    len[4] = RSNA_SAE_ELEMENT_LEN;
    RsnaHmacSha256Vector(kck, RSNA_SAE_KCK_LEN, 5, addr, len, confirm);
}

void RsnaSaeComputeConfirm(const RSNA_SAE *sae, rsna_u16 sendConfirm, rsna_u8 confirm[RSNA_SAE_CONFIRM_LEN])
{
    RsnaSaeConfirmHash(sae->Kck, sendConfirm, sae->Scalar, sae->Element,
                       sae->PeerScalar, sae->PeerElement, confirm);
}

int RsnaSaeVerifyConfirm(const RSNA_SAE *sae, rsna_u16 peerSendConfirm, const rsna_u8 confirm[RSNA_SAE_CONFIRM_LEN])
{
    rsna_u8 expected[RSNA_SAE_CONFIRM_LEN];
    int ok;

    if (!sae->HaveKeys)
        return 0;
    RsnaSaeConfirmHash(sae->Kck, peerSendConfirm, sae->PeerScalar, sae->PeerElement,
                       sae->Scalar, sae->Element, expected);
    ok = RsnaSaeEqualConstantTime(expected, confirm, RSNA_SAE_CONFIRM_LEN);
    RsnaSaeWipe(expected, sizeof(expected));
    return ok;
}

void RsnaSaeClear(RSNA_SAE *sae)
{
    RsnaSaeWipe(sae, sizeof(*sae));
}
