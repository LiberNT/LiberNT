/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AES-128-CMAC (RFC 4493)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_CMAC_H_
#define _RSNA_CMAC_H_

#include "rsna_types.h"

#define RSNA_CMAC_LEN 16

void RsnaAesCmacVector(const rsna_u8 key[16],
                       rsna_size count,
                       const rsna_u8 *const addr[],
                       const rsna_size len[],
                       rsna_u8 mac[RSNA_CMAC_LEN]);

void RsnaAesCmac(const rsna_u8 key[16],
                 const rsna_u8 *data,
                 rsna_size dataLen,
                 rsna_u8 mac[RSNA_CMAC_LEN]);

#endif /* _RSNA_CMAC_H_ */
