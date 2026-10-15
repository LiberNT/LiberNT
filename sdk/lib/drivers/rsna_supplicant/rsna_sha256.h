/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHA-256 (FIPS 180-4)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_SHA256_H_
#define _RSNA_SHA256_H_

#include "rsna_types.h"

#define RSNA_SHA256_DIGEST_LEN  32
#define RSNA_SHA256_BLOCK_LEN   64

typedef struct _RSNA_SHA256_CTX
{
    rsna_u32 state[8];
    rsna_u64 count;
    rsna_u8  buffer[RSNA_SHA256_BLOCK_LEN];
} RSNA_SHA256_CTX;

void RsnaSha256Init(RSNA_SHA256_CTX *ctx);
void RsnaSha256Update(RSNA_SHA256_CTX *ctx, const void *data, rsna_size len);
void RsnaSha256Final(RSNA_SHA256_CTX *ctx, rsna_u8 digest[RSNA_SHA256_DIGEST_LEN]);
void RsnaSha256(const void *data, rsna_size len, rsna_u8 digest[RSNA_SHA256_DIGEST_LEN]);

#endif
