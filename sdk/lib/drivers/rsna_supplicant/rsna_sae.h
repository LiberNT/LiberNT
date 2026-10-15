/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Simultaneous Authentication of Equals over group 19 (IEEE 802.11-2020 12.4)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_SAE_H_
#define _RSNA_SAE_H_

#include "rsna_types.h"
#include "rsna_p256.h"

#define RSNA_SAE_GROUP_P256         19
#define RSNA_SAE_SCALAR_LEN         32
#define RSNA_SAE_ELEMENT_LEN        64
#define RSNA_SAE_KCK_LEN            32
#define RSNA_SAE_PMK_LEN            32
#define RSNA_SAE_PMKID_LEN          16
#define RSNA_SAE_CONFIRM_LEN        32
#define RSNA_SAE_HNP_ITERATIONS     40
#define RSNA_SAE_H2E_VALUE_LEN      48

typedef int (*RSNA_SAE_RANDOM_FN)(void *context, rsna_u8 *buffer, rsna_size length);

typedef struct _RSNA_SAE
{
    RSNA_P256_POINT Pwe;
    rsna_u8 Rand[RSNA_SAE_SCALAR_LEN];
    rsna_u8 Scalar[RSNA_SAE_SCALAR_LEN];
    rsna_u8 Element[RSNA_SAE_ELEMENT_LEN];
    rsna_u8 PeerScalar[RSNA_SAE_SCALAR_LEN];
    rsna_u8 PeerElement[RSNA_SAE_ELEMENT_LEN];
    rsna_u8 Kck[RSNA_SAE_KCK_LEN];
    rsna_u8 Pmk[RSNA_SAE_PMK_LEN];
    rsna_u8 Pmkid[RSNA_SAE_PMKID_LEN];
    int HavePwe;
    int HaveCommit;
    int HaveKeys;
} RSNA_SAE;

int RsnaSaeDerivePwe(RSNA_SAE *sae, const rsna_u8 ownAddress[6], const rsna_u8 peerAddress[6],
                     const rsna_u8 *password, rsna_size passwordLen);
int RsnaSaeDerivePt(rsna_u8 pt[RSNA_SAE_ELEMENT_LEN], const rsna_u8 *ssid, rsna_size ssidLen,
                    const rsna_u8 *password, rsna_size passwordLen,
                    const rsna_u8 *identifier, rsna_size identifierLen);
int RsnaSaeDerivePweFromPt(RSNA_SAE *sae, const rsna_u8 pt[RSNA_SAE_ELEMENT_LEN],
                           const rsna_u8 ownAddress[6], const rsna_u8 peerAddress[6]);
int RsnaSaeSetCommit(RSNA_SAE *sae, const rsna_u8 rand[RSNA_SAE_SCALAR_LEN],
                     const rsna_u8 mask[RSNA_SAE_SCALAR_LEN]);
int RsnaSaeGenerateCommit(RSNA_SAE *sae, RSNA_SAE_RANDOM_FN random, void *context);
int RsnaSaeProcessCommit(RSNA_SAE *sae, const rsna_u8 peerScalar[RSNA_SAE_SCALAR_LEN],
                         const rsna_u8 peerElement[RSNA_SAE_ELEMENT_LEN]);
void RsnaSaeComputeConfirm(const RSNA_SAE *sae, rsna_u16 sendConfirm, rsna_u8 confirm[RSNA_SAE_CONFIRM_LEN]);
int RsnaSaeVerifyConfirm(const RSNA_SAE *sae, rsna_u16 peerSendConfirm, const rsna_u8 confirm[RSNA_SAE_CONFIRM_LEN]);
void RsnaSaeClear(RSNA_SAE *sae);

#endif
