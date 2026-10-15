/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AES-128-CMAC (RFC 4493)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rsna_cmac.h"
#include "rsna_aes.h"

static void RsnaCmacShift(const rsna_u8 in[16], rsna_u8 out[16])
{
    rsna_u8 carry = (in[0] & 0x80) ? 0x87 : 0x00;
    int i;

    for (i = 0; i < 15; i++)
        out[i] = (rsna_u8)((in[i] << 1) | (in[i + 1] >> 7));
    out[15] = (rsna_u8)((in[15] << 1) ^ carry);
}

void RsnaAesCmacVector(const rsna_u8 key[16],
                       rsna_size count,
                       const rsna_u8 *const addr[],
                       const rsna_size len[],
                       rsna_u8 mac[RSNA_CMAC_LEN])
{
    RSNA_AES_KEY aes;
    rsna_u8 x[16], block[16], subkey[16];
    rsna_size total = 0, done = 0, element = 0, offset = 0, i;
    int fill = 0;

    for (i = 0; i < count; i++)
        total += len[i];

    RsnaAesSetEncryptKey(&aes, key, 128);
    for (i = 0; i < 16; i++)
        x[i] = 0;

    while (total - done > 16)
    {
        for (fill = 0; fill < 16; fill++)
        {
            while (offset >= len[element])
            {
                element++;
                offset = 0;
            }
            block[fill] = addr[element][offset++];
        }
        for (i = 0; i < 16; i++)
            x[i] ^= block[i];
        RsnaAesEncrypt(&aes, x, x);
        done += 16;
    }

    for (fill = 0; done < total; fill++, done++)
    {
        while (offset >= len[element])
        {
            element++;
            offset = 0;
        }
        block[fill] = addr[element][offset++];
    }

    for (i = 0; i < 16; i++)
        subkey[i] = 0;
    RsnaAesEncrypt(&aes, subkey, subkey);
    RsnaCmacShift(subkey, subkey);

    if (fill == 16 && total != 0)
    {
        for (i = 0; i < 16; i++)
            x[i] ^= block[i] ^ subkey[i];
    }
    else
    {
        RsnaCmacShift(subkey, subkey);
        block[fill++] = 0x80;
        while (fill < 16)
            block[fill++] = 0;
        for (i = 0; i < 16; i++)
            x[i] ^= block[i] ^ subkey[i];
    }

    RsnaAesEncrypt(&aes, x, mac);

    for (i = 0; i < 16; i++)
    {
        x[i] = 0;
        block[i] = 0;
        subkey[i] = 0;
    }
    for (i = 0; i < sizeof(aes); i++)
        ((volatile rsna_u8 *)&aes)[i] = 0;
}

void RsnaAesCmac(const rsna_u8 key[16],
                 const rsna_u8 *data,
                 rsna_size dataLen,
                 rsna_u8 mac[RSNA_CMAC_LEN])
{
    RsnaAesCmacVector(key, 1, &data, &dataLen, mac);
}
