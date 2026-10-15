/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IEEE 802.11 CCMP-128 and BIP-CMAC-128 frame protection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_CCMP_H_
#define _RSNA_CCMP_H_

#include "rsna_types.h"

#define RSNA_CCMP_HDR_LEN   8
#define RSNA_CCMP_MIC_LEN   8
#define RSNA_MMIE_LEN       18
#define RSNA_MMIE_ID        76

int RsnaCcmpEncrypt(const rsna_u8 tk[16],
                    const rsna_u8 *hdr, rsna_size hdrLen,
                    rsna_u64 pn, rsna_u8 keyId,
                    const rsna_u8 *plain, rsna_size plainLen,
                    rsna_u8 *out);

int RsnaCcmpDecrypt(const rsna_u8 tk[16],
                    const rsna_u8 *hdr, rsna_size hdrLen,
                    const rsna_u8 *body, rsna_size bodyLen,
                    rsna_u8 *plain, rsna_u64 *pn);

int RsnaBipVerify(const rsna_u8 igtk[16],
                  const rsna_u8 *frame, rsna_size frameLen,
                  rsna_u16 *keyId, rsna_u64 *ipn);

#endif /* _RSNA_CCMP_H_ */
