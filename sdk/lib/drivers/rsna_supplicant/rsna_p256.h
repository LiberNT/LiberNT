/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NIST P-256 field, scalar and point arithmetic
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_P256_H_
#define _RSNA_P256_H_

#include "rsna_types.h"

#define RSNA_P256_LIMBS     8
#define RSNA_P256_BYTES     32

typedef struct _RSNA_P256_FE
{
    rsna_u32 v[RSNA_P256_LIMBS];
} RSNA_P256_FE;

typedef struct _RSNA_P256_POINT
{
    RSNA_P256_FE X;
    RSNA_P256_FE Y;
    RSNA_P256_FE Z;
} RSNA_P256_POINT;

int RsnaP256FieldFromBytes(RSNA_P256_FE *r, const rsna_u8 in[RSNA_P256_BYTES]);
void RsnaP256FieldToBytes(rsna_u8 out[RSNA_P256_BYTES], const RSNA_P256_FE *a);
void RsnaP256CurveRhs(RSNA_P256_FE *r, const RSNA_P256_FE *x);
int RsnaP256IsSquare(const RSNA_P256_FE *a);
void RsnaP256Sqrt(RSNA_P256_FE *r, const RSNA_P256_FE *a);
void RsnaP256FieldNeg(RSNA_P256_FE *r, const RSNA_P256_FE *a);
int RsnaP256FieldIsOdd(const RSNA_P256_FE *a);

int RsnaP256LessThanPrime(const rsna_u8 in[RSNA_P256_BYTES]);
int RsnaP256ScalarIsValid(const rsna_u8 s[RSNA_P256_BYTES]);
void RsnaP256ScalarAdd(rsna_u8 out[RSNA_P256_BYTES], const rsna_u8 a[RSNA_P256_BYTES],
                       const rsna_u8 b[RSNA_P256_BYTES]);

void RsnaP256PointSetAffine(RSNA_P256_POINT *p, const RSNA_P256_FE *x, const RSNA_P256_FE *y);
int RsnaP256PointFromBytes(RSNA_P256_POINT *p, const rsna_u8 x[RSNA_P256_BYTES],
                           const rsna_u8 y[RSNA_P256_BYTES]);
int RsnaP256PointToBytes(rsna_u8 x[RSNA_P256_BYTES], rsna_u8 y[RSNA_P256_BYTES],
                         const RSNA_P256_POINT *p);
void RsnaP256PointAdd(RSNA_P256_POINT *r, const RSNA_P256_POINT *a, const RSNA_P256_POINT *b);
void RsnaP256PointNeg(RSNA_P256_POINT *r, const RSNA_P256_POINT *a);
void RsnaP256PointMul(RSNA_P256_POINT *r, const RSNA_P256_POINT *p, const rsna_u8 k[RSNA_P256_BYTES]);
int RsnaP256PointIsInfinity(const RSNA_P256_POINT *p);

#endif
