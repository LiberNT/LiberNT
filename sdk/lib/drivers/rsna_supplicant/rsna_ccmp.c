/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IEEE 802.11 CCMP-128 and BIP-CMAC-128 frame protection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Cleanroom implementation from IEEE Std 802.11-2020 12.5.3 (CCMP) and
 * 12.5.4 (BIP), and NIST SP 800-38C (CCM).
 */

#include "rsna_ccmp.h"
#include "rsna_cmac.h"
#include "rsna_aes.h"

#define RSNA_FC0_TYPE_MASK  0x0C
#define RSNA_FC0_TYPE_MGMT  0x00
#define RSNA_FC0_TYPE_DATA  0x08
#define RSNA_FC0_QOS        0x80

static void RsnaCcmpWipe(void *p, rsna_size n)
{
    volatile rsna_u8 *v = (volatile rsna_u8 *)p;
    while (n--)
        *v++ = 0;
}

static rsna_size RsnaCcmpBuildAad(const rsna_u8 *hdr, rsna_size hdrLen,
                                  rsna_u8 aad[32], rsna_u8 nonce[13], rsna_u64 pn)
{
    rsna_u8 type = (rsna_u8)(hdr[0] & RSNA_FC0_TYPE_MASK);
    int data = (type == RSNA_FC0_TYPE_DATA);
    int qos = data && (hdr[0] & RSNA_FC0_QOS);
    int a4 = data && ((hdr[1] & 0x03) == 0x03);
    rsna_size pos = 0, qcOff = 24 + (a4 ? 6 : 0), i;
    rsna_u8 priority = 0;

    if (hdrLen < 24 || (a4 && hdrLen < 30) || (qos && hdrLen < qcOff + 2))
        return 0;

    aad[pos++] = data ? (rsna_u8)(hdr[0] & ~0x70) : hdr[0];
    aad[pos] = (rsna_u8)((hdr[1] & ~(0x08 | 0x10 | 0x20)) | 0x40);
    if (qos)
        aad[pos] &= (rsna_u8)~0x80;
    pos++;
    for (i = 4; i < 22; i++)
        aad[pos++] = hdr[i];
    aad[pos++] = (rsna_u8)(hdr[22] & 0x0F);
    aad[pos++] = 0;
    if (a4)
    {
        for (i = 24; i < 30; i++)
            aad[pos++] = hdr[i];
    }
    if (qos)
    {
        priority = (rsna_u8)(hdr[qcOff] & 0x0F);
        aad[pos++] = priority;
        aad[pos++] = 0;
    }

    nonce[0] = (rsna_u8)(priority | (type == RSNA_FC0_TYPE_MGMT ? 0x10 : 0x00));
    for (i = 0; i < 6; i++)
        nonce[1 + i] = hdr[10 + i];
    for (i = 0; i < 6; i++)
        nonce[7 + i] = (rsna_u8)(pn >> (8 * (5 - i)));
    return pos;
}

static void RsnaCcmComputeTag(const RSNA_AES_KEY *aes,
                              const rsna_u8 nonce[13],
                              const rsna_u8 *aad, rsna_size aadLen,
                              const rsna_u8 *plain, rsna_size plainLen,
                              rsna_u8 tag[16])
{
    rsna_u8 block[16];
    rsna_size i, pos, chunk;

    block[0] = 0x59;
    for (i = 0; i < 13; i++)
        block[1 + i] = nonce[i];
    block[14] = (rsna_u8)(plainLen >> 8);
    block[15] = (rsna_u8)plainLen;
    RsnaAesEncrypt(aes, block, tag);

    for (i = 0; i < 16; i++)
        block[i] = 0;
    block[0] = (rsna_u8)(aadLen >> 8);
    block[1] = (rsna_u8)aadLen;
    pos = 2;
    for (i = 0; i < aadLen; i++)
    {
        block[pos++] = aad[i];
        if (pos == 16)
        {
            for (pos = 0; pos < 16; pos++)
                tag[pos] ^= block[pos];
            RsnaAesEncrypt(aes, tag, tag);
            pos = 0;
        }
    }
    if (pos != 0)
    {
        while (pos < 16)
            block[pos++] = 0;
        for (pos = 0; pos < 16; pos++)
            tag[pos] ^= block[pos];
        RsnaAesEncrypt(aes, tag, tag);
    }

    for (i = 0; i < plainLen; i += 16)
    {
        chunk = plainLen - i < 16 ? plainLen - i : 16;
        for (pos = 0; pos < chunk; pos++)
            tag[pos] ^= plain[i + pos];
        RsnaAesEncrypt(aes, tag, tag);
    }

    RsnaCcmpWipe(block, sizeof(block));
}

static void RsnaCcmCtr(const RSNA_AES_KEY *aes,
                       const rsna_u8 nonce[13],
                       const rsna_u8 *in, rsna_size len,
                       rsna_u8 *out)
{
    rsna_u8 ctr[16], stream[16];
    rsna_size i, pos, chunk;
    rsna_u16 count = 1;

    ctr[0] = 0x01;
    for (i = 0; i < 13; i++)
        ctr[1 + i] = nonce[i];

    for (i = 0; i < len; i += 16, count++)
    {
        ctr[14] = (rsna_u8)(count >> 8);
        ctr[15] = (rsna_u8)count;
        RsnaAesEncrypt(aes, ctr, stream);
        chunk = len - i < 16 ? len - i : 16;
        for (pos = 0; pos < chunk; pos++)
            out[i + pos] = (rsna_u8)(in[i + pos] ^ stream[pos]);
    }

    RsnaCcmpWipe(stream, sizeof(stream));
}

static void RsnaCcmMaskTag(const RSNA_AES_KEY *aes,
                           const rsna_u8 nonce[13],
                           rsna_u8 tag[16])
{
    rsna_u8 ctr[16], stream[16];
    rsna_size i;

    ctr[0] = 0x01;
    for (i = 0; i < 13; i++)
        ctr[1 + i] = nonce[i];
    ctr[14] = 0;
    ctr[15] = 0;
    RsnaAesEncrypt(aes, ctr, stream);
    for (i = 0; i < RSNA_CCMP_MIC_LEN; i++)
        tag[i] ^= stream[i];
    RsnaCcmpWipe(stream, sizeof(stream));
}

int RsnaCcmpEncrypt(const rsna_u8 tk[16],
                    const rsna_u8 *hdr, rsna_size hdrLen,
                    rsna_u64 pn, rsna_u8 keyId,
                    const rsna_u8 *plain, rsna_size plainLen,
                    rsna_u8 *out)
{
    RSNA_AES_KEY aes;
    rsna_u8 aad[32], nonce[13], tag[16];
    rsna_size aadLen, i;

    if (plainLen > 0xFFFF)
        return -1;
    aadLen = RsnaCcmpBuildAad(hdr, hdrLen, aad, nonce, pn);
    if (aadLen == 0)
        return -1;

    out[0] = (rsna_u8)pn;
    out[1] = (rsna_u8)(pn >> 8);
    out[2] = 0;
    out[3] = (rsna_u8)(0x20 | ((keyId & 0x03) << 6));
    out[4] = (rsna_u8)(pn >> 16);
    out[5] = (rsna_u8)(pn >> 24);
    out[6] = (rsna_u8)(pn >> 32);
    out[7] = (rsna_u8)(pn >> 40);

    RsnaAesSetEncryptKey(&aes, tk, 128);
    RsnaCcmComputeTag(&aes, nonce, aad, aadLen, plain, plainLen, tag);
    RsnaCcmCtr(&aes, nonce, plain, plainLen, out + RSNA_CCMP_HDR_LEN);
    RsnaCcmMaskTag(&aes, nonce, tag);
    for (i = 0; i < RSNA_CCMP_MIC_LEN; i++)
        out[RSNA_CCMP_HDR_LEN + plainLen + i] = tag[i];

    RsnaCcmpWipe(&aes, sizeof(aes));
    RsnaCcmpWipe(tag, sizeof(tag));
    return 0;
}

int RsnaCcmpDecrypt(const rsna_u8 tk[16],
                    const rsna_u8 *hdr, rsna_size hdrLen,
                    const rsna_u8 *body, rsna_size bodyLen,
                    rsna_u8 *plain, rsna_u64 *pn)
{
    RSNA_AES_KEY aes;
    rsna_u8 aad[32], nonce[13], tag[16];
    rsna_size aadLen, plainLen, i;
    rsna_u64 packetNumber;
    rsna_u8 diff = 0;

    if (bodyLen < RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN || !(body[3] & 0x20))
        return -1;
    plainLen = bodyLen - RSNA_CCMP_HDR_LEN - RSNA_CCMP_MIC_LEN;

    packetNumber = (rsna_u64)body[0] |
                   ((rsna_u64)body[1] << 8) |
                   ((rsna_u64)body[4] << 16) |
                   ((rsna_u64)body[5] << 24) |
                   ((rsna_u64)body[6] << 32) |
                   ((rsna_u64)body[7] << 40);

    aadLen = RsnaCcmpBuildAad(hdr, hdrLen, aad, nonce, packetNumber);
    if (aadLen == 0)
        return -1;

    RsnaAesSetEncryptKey(&aes, tk, 128);
    RsnaCcmCtr(&aes, nonce, body + RSNA_CCMP_HDR_LEN, plainLen, plain);
    RsnaCcmComputeTag(&aes, nonce, aad, aadLen, plain, plainLen, tag);
    RsnaCcmMaskTag(&aes, nonce, tag);
    for (i = 0; i < RSNA_CCMP_MIC_LEN; i++)
        diff |= (rsna_u8)(tag[i] ^ body[RSNA_CCMP_HDR_LEN + plainLen + i]);

    RsnaCcmpWipe(&aes, sizeof(aes));
    RsnaCcmpWipe(tag, sizeof(tag));
    if (diff != 0)
    {
        RsnaCcmpWipe(plain, plainLen);
        return -1;
    }

    if (pn != RSNA_NULL)
        *pn = packetNumber;
    return 0;
}

int RsnaBipVerify(const rsna_u8 igtk[16],
                  const rsna_u8 *frame, rsna_size frameLen,
                  rsna_u16 *keyId, rsna_u64 *ipn)
{
    static const rsna_u8 zeroMic[RSNA_CCMP_MIC_LEN] = { 0 };
    const rsna_u8 *mmie;
    const rsna_u8 *addr[4];
    rsna_size len[4];
    rsna_u8 aad[20], mac[RSNA_CMAC_LEN];
    rsna_u8 diff = 0;
    rsna_size i;

    if (frameLen < 24 + RSNA_MMIE_LEN)
        return -1;
    mmie = frame + frameLen - RSNA_MMIE_LEN;
    if (mmie[0] != RSNA_MMIE_ID || mmie[1] != RSNA_MMIE_LEN - 2)
        return -1;

    aad[0] = frame[0];
    aad[1] = (rsna_u8)(frame[1] & ~(0x08 | 0x10 | 0x20));
    for (i = 0; i < 18; i++)
        aad[2 + i] = frame[4 + i];

    addr[0] = aad;
    len[0] = sizeof(aad);
    addr[1] = frame + 24;
    len[1] = frameLen - 24 - RSNA_CCMP_MIC_LEN;
    addr[2] = zeroMic;
    len[2] = RSNA_CCMP_MIC_LEN;
    RsnaAesCmacVector(igtk, 3, addr, len, mac);

    for (i = 0; i < RSNA_CCMP_MIC_LEN; i++)
        diff |= (rsna_u8)(mac[i] ^ mmie[10 + i]);
    RsnaCcmpWipe(mac, sizeof(mac));
    if (diff != 0)
        return -1;

    if (keyId != RSNA_NULL)
        *keyId = (rsna_u16)(mmie[2] | (mmie[3] << 8));
    if (ipn != RSNA_NULL)
    {
        *ipn = 0;
        for (i = 0; i < 6; i++)
            *ipn |= (rsna_u64)mmie[4 + i] << (8 * i);
    }
    return 0;
}
