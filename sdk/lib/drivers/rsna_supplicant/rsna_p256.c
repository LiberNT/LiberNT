/*
 * PROJECT:     LiberNT Native WiFi - RSNA supplicant
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NIST P-256 field, scalar and point arithmetic
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rsna_p256.h"

static const rsna_u32 RsnaP256Prime[RSNA_P256_LIMBS] =
{
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF
};

static const rsna_u32 RsnaP256Order[RSNA_P256_LIMBS] =
{
    0xFC632551, 0xF3B9CAC2, 0xA7179E84, 0xBCE6FAAD, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF
};

static const rsna_u32 RsnaP256B[RSNA_P256_LIMBS] =
{
    0x27D2604B, 0x3BCE3C3E, 0xCC53B0F6, 0x651D06B0, 0x769886BC, 0xB3EBBD55, 0xAA3A93E7, 0x5AC635D8
};

static const rsna_u32 RsnaP256MontOne[RSNA_P256_LIMBS] =
{
    0x00000001, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0x00000000
};

static rsna_u32 RsnaBnAdd(rsna_u32 *r, const rsna_u32 *a, const rsna_u32 *b)
{
    rsna_u64 carry = 0;
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
    {
        carry += (rsna_u64)a[i] + b[i];
        r[i] = (rsna_u32)carry;
        carry >>= 32;
    }
    return (rsna_u32)carry;
}

static rsna_u32 RsnaBnSub(rsna_u32 *r, const rsna_u32 *a, const rsna_u32 *b)
{
    rsna_u64 borrow = 0;
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
    {
        rsna_u64 d = (rsna_u64)a[i] - b[i] - borrow;
        r[i] = (rsna_u32)d;
        borrow = (d >> 32) & 1;
    }
    return (rsna_u32)borrow;
}

static int RsnaBnCompare(const rsna_u32 *a, const rsna_u32 *b)
{
    int i;

    for (i = RSNA_P256_LIMBS - 1; i >= 0; i--)
    {
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

static int RsnaBnIsZero(const rsna_u32 *a)
{
    rsna_u32 acc = 0;
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
        acc |= a[i];
    return acc == 0;
}

static void RsnaBnFromBytes(rsna_u32 *r, const rsna_u8 in[RSNA_P256_BYTES])
{
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
    {
        const rsna_u8 *p = in + RSNA_P256_BYTES - 4 * (i + 1);
        r[i] = ((rsna_u32)p[0] << 24) | ((rsna_u32)p[1] << 16) | ((rsna_u32)p[2] << 8) | p[3];
    }
}

static void RsnaBnToBytes(rsna_u8 out[RSNA_P256_BYTES], const rsna_u32 *a)
{
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
    {
        rsna_u8 *p = out + RSNA_P256_BYTES - 4 * (i + 1);
        p[0] = (rsna_u8)(a[i] >> 24);
        p[1] = (rsna_u8)(a[i] >> 16);
        p[2] = (rsna_u8)(a[i] >> 8);
        p[3] = (rsna_u8)a[i];
    }
}

static void RsnaFeCopy(RSNA_P256_FE *r, const RSNA_P256_FE *a)
{
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
        r->v[i] = a->v[i];
}

static void RsnaFeAdd(RSNA_P256_FE *r, const RSNA_P256_FE *a, const RSNA_P256_FE *b)
{
    rsna_u32 t[RSNA_P256_LIMBS];
    rsna_u32 carry = RsnaBnAdd(r->v, a->v, b->v);
    rsna_u32 borrow = RsnaBnSub(t, r->v, RsnaP256Prime);
    int i;

    if (carry || !borrow)
    {
        for (i = 0; i < RSNA_P256_LIMBS; i++)
            r->v[i] = t[i];
    }
}

static void RsnaFeSub(RSNA_P256_FE *r, const RSNA_P256_FE *a, const RSNA_P256_FE *b)
{
    if (RsnaBnSub(r->v, a->v, b->v))
        RsnaBnAdd(r->v, r->v, RsnaP256Prime);
}

static void RsnaFeMul(RSNA_P256_FE *r, const RSNA_P256_FE *a, const RSNA_P256_FE *b)
{
    rsna_u32 t[RSNA_P256_LIMBS + 2], u[RSNA_P256_LIMBS];
    rsna_u64 s, c;
    rsna_u32 m;
    int i, j;

    for (i = 0; i < RSNA_P256_LIMBS + 2; i++)
        t[i] = 0;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
    {
        c = 0;
        for (j = 0; j < RSNA_P256_LIMBS; j++)
        {
            s = (rsna_u64)t[j] + (rsna_u64)a->v[j] * b->v[i] + c;
            t[j] = (rsna_u32)s;
            c = s >> 32;
        }
        s = (rsna_u64)t[RSNA_P256_LIMBS] + c;
        t[RSNA_P256_LIMBS] = (rsna_u32)s;
        t[RSNA_P256_LIMBS + 1] = (rsna_u32)(s >> 32);

        m = t[0];
        s = (rsna_u64)t[0] + (rsna_u64)m * RsnaP256Prime[0];
        c = s >> 32;
        for (j = 1; j < RSNA_P256_LIMBS; j++)
        {
            s = (rsna_u64)t[j] + (rsna_u64)m * RsnaP256Prime[j] + c;
            t[j - 1] = (rsna_u32)s;
            c = s >> 32;
        }
        s = (rsna_u64)t[RSNA_P256_LIMBS] + c;
        t[RSNA_P256_LIMBS - 1] = (rsna_u32)s;
        t[RSNA_P256_LIMBS] = t[RSNA_P256_LIMBS + 1] + (rsna_u32)(s >> 32);
    }

    if (RsnaBnSub(u, t, RsnaP256Prime) == 0 || t[RSNA_P256_LIMBS])
    {
        for (i = 0; i < RSNA_P256_LIMBS; i++)
            r->v[i] = u[i];
    }
    else
    {
        for (i = 0; i < RSNA_P256_LIMBS; i++)
            r->v[i] = t[i];
    }
}

static void RsnaFeSqr(RSNA_P256_FE *r, const RSNA_P256_FE *a)
{
    RsnaFeMul(r, a, a);
}

static void RsnaFeSetOne(RSNA_P256_FE *r)
{
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
        r->v[i] = RsnaP256MontOne[i];
}

static void RsnaFeMontR2(RSNA_P256_FE *r)
{
    int i;

    RsnaFeSetOne(r);
    for (i = 0; i < 256; i++)
        RsnaFeAdd(r, r, r);
}

static void RsnaFeToMont(RSNA_P256_FE *r, const rsna_u32 *a)
{
    RSNA_P256_FE raw, r2;
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
        raw.v[i] = a[i];
    RsnaFeMontR2(&r2);
    RsnaFeMul(r, &raw, &r2);
}

static void RsnaFeFromMont(rsna_u32 *out, const RSNA_P256_FE *a)
{
    RSNA_P256_FE one, t;
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
        one.v[i] = 0;
    one.v[0] = 1;
    RsnaFeMul(&t, a, &one);
    for (i = 0; i < RSNA_P256_LIMBS; i++)
        out[i] = t.v[i];
}

static void RsnaFePow(RSNA_P256_FE *r, const RSNA_P256_FE *a, const rsna_u32 *exponent)
{
    RSNA_P256_FE acc;
    int bit;

    RsnaFeSetOne(&acc);
    for (bit = 255; bit >= 0; bit--)
    {
        RsnaFeSqr(&acc, &acc);
        if ((exponent[bit / 32] >> (bit % 32)) & 1)
            RsnaFeMul(&acc, &acc, a);
    }
    RsnaFeCopy(r, &acc);
}

static void RsnaFeInv(RSNA_P256_FE *r, const RSNA_P256_FE *a)
{
    rsna_u32 e[RSNA_P256_LIMBS], two[RSNA_P256_LIMBS] = { 2, 0, 0, 0, 0, 0, 0, 0 };

    RsnaBnSub(e, RsnaP256Prime, two);
    RsnaFePow(r, a, e);
}

static int RsnaFeEqual(const RSNA_P256_FE *a, const RSNA_P256_FE *b)
{
    return RsnaBnCompare(a->v, b->v) == 0;
}

int RsnaP256FieldFromBytes(RSNA_P256_FE *r, const rsna_u8 in[RSNA_P256_BYTES])
{
    rsna_u32 a[RSNA_P256_LIMBS];

    RsnaBnFromBytes(a, in);
    if (RsnaBnCompare(a, RsnaP256Prime) >= 0)
        return 0;
    RsnaFeToMont(r, a);
    return 1;
}

void RsnaP256FieldToBytes(rsna_u8 out[RSNA_P256_BYTES], const RSNA_P256_FE *a)
{
    rsna_u32 t[RSNA_P256_LIMBS];

    RsnaFeFromMont(t, a);
    RsnaBnToBytes(out, t);
}

void RsnaP256CurveRhs(RSNA_P256_FE *r, const RSNA_P256_FE *x)
{
    RSNA_P256_FE x3, threeX, b;

    RsnaFeSqr(&x3, x);
    RsnaFeMul(&x3, &x3, x);
    RsnaFeAdd(&threeX, x, x);
    RsnaFeAdd(&threeX, &threeX, x);
    RsnaFeToMont(&b, RsnaP256B);
    RsnaFeSub(r, &x3, &threeX);
    RsnaFeAdd(r, r, &b);
}

int RsnaP256IsSquare(const RSNA_P256_FE *a)
{
    rsna_u32 e[RSNA_P256_LIMBS], one[RSNA_P256_LIMBS] = { 1, 0, 0, 0, 0, 0, 0, 0 };
    RSNA_P256_FE t, m1;
    int i;

    RsnaBnSub(e, RsnaP256Prime, one);
    for (i = 0; i < RSNA_P256_LIMBS; i++)
        e[i] = (e[i] >> 1) | (i + 1 < RSNA_P256_LIMBS ? e[i + 1] << 31 : 0);
    RsnaFePow(&t, a, e);
    RsnaFeSetOne(&m1);
    return RsnaFeEqual(&t, &m1);
}

void RsnaP256Sqrt(RSNA_P256_FE *r, const RSNA_P256_FE *a)
{
    rsna_u32 e[RSNA_P256_LIMBS], one[RSNA_P256_LIMBS] = { 1, 0, 0, 0, 0, 0, 0, 0 };
    rsna_u32 carry;
    int i;

    carry = RsnaBnAdd(e, RsnaP256Prime, one);
    for (i = 0; i < RSNA_P256_LIMBS; i++)
        e[i] = (e[i] >> 2) | (i + 1 < RSNA_P256_LIMBS ? e[i + 1] << 30 : carry << 30);
    RsnaFePow(r, a, e);
}

void RsnaP256FieldNeg(RSNA_P256_FE *r, const RSNA_P256_FE *a)
{
    RSNA_P256_FE zero;
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
        zero.v[i] = 0;
    RsnaFeSub(r, &zero, a);
}

int RsnaP256FieldIsOdd(const RSNA_P256_FE *a)
{
    rsna_u32 t[RSNA_P256_LIMBS];

    RsnaFeFromMont(t, a);
    return (int)(t[0] & 1);
}

static void RsnaFeSelect(RSNA_P256_FE *r, const RSNA_P256_FE *a, const RSNA_P256_FE *b, int useA)
{
    rsna_u32 mask = (rsna_u32)0 - (rsna_u32)(useA != 0);
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
        r->v[i] = (a->v[i] & mask) | (b->v[i] & ~mask);
}

static void RsnaFeFromSmallNegative(RSNA_P256_FE *r, rsna_u32 value)
{
    rsna_u32 t[RSNA_P256_LIMBS], v[RSNA_P256_LIMBS] = { 0 };

    v[0] = value;
    RsnaBnSub(t, RsnaP256Prime, v);
    RsnaFeToMont(r, t);
}

static void RsnaFeFromReducedBytes(RSNA_P256_FE *r, const rsna_u8 in[RSNA_P256_BYTES])
{
    rsna_u32 a[RSNA_P256_LIMBS], t[RSNA_P256_LIMBS];

    RsnaBnFromBytes(a, in);
    if (RsnaBnSub(t, a, RsnaP256Prime) == 0)
        RsnaFeToMont(r, t);
    else
        RsnaFeToMont(r, a);
}

void RsnaP256FieldFromWide(RSNA_P256_FE *r, const rsna_u8 *in, rsna_size len)
{
    rsna_u8 high[RSNA_P256_BYTES] = { 0 }, low[RSNA_P256_BYTES] = { 0 };
    RSNA_P256_FE h, l, shift;
    rsna_size i, lowLen = len < RSNA_P256_BYTES ? len : RSNA_P256_BYTES;

    for (i = 0; i < lowLen; i++)
        low[RSNA_P256_BYTES - lowLen + i] = in[len - lowLen + i];
    for (i = 0; i < len - lowLen && i < RSNA_P256_BYTES; i++)
        high[RSNA_P256_BYTES - (len - lowLen) + i] = in[i];

    RsnaFeFromReducedBytes(&h, high);
    RsnaFeFromReducedBytes(&l, low);
    RsnaFeToMont(&shift, RsnaP256MontOne);
    RsnaFeMul(r, &h, &shift);
    RsnaFeAdd(r, r, &l);
}

void RsnaP256Sswu(RSNA_P256_POINT *r, const RSNA_P256_FE *u)
{
    RSNA_P256_FE z, a, b, one, zu2, m, t, x1a, x1b, x1, gx1, x2, gx2, x, v, y, ny, tmp;
    int zero, square;

    RsnaFeFromSmallNegative(&z, 10);
    RsnaFeFromSmallNegative(&a, 3);
    RsnaFeToMont(&b, RsnaP256B);
    RsnaFeSetOne(&one);

    RsnaFeSqr(&zu2, u);
    RsnaFeMul(&zu2, &zu2, &z);
    RsnaFeSqr(&m, &zu2);
    RsnaFeAdd(&m, &m, &zu2);
    zero = RsnaBnIsZero(m.v);
    RsnaFeInv(&t, &m);

    RsnaFeMul(&tmp, &z, &a);
    RsnaFeInv(&tmp, &tmp);
    RsnaFeMul(&x1a, &b, &tmp);

    RsnaFeInv(&tmp, &a);
    RsnaFeMul(&tmp, &tmp, &b);
    RsnaP256FieldNeg(&tmp, &tmp);
    RsnaFeAdd(&x1b, &one, &t);
    RsnaFeMul(&x1b, &x1b, &tmp);

    RsnaFeSelect(&x1, &x1a, &x1b, zero);
    RsnaP256CurveRhs(&gx1, &x1);
    RsnaFeMul(&x2, &zu2, &x1);
    RsnaP256CurveRhs(&gx2, &x2);

    square = RsnaP256IsSquare(&gx1);
    RsnaFeSelect(&x, &x1, &x2, square);
    RsnaFeSelect(&v, &gx1, &gx2, square);
    RsnaP256Sqrt(&y, &v);
    RsnaP256FieldNeg(&ny, &y);
    RsnaFeSelect(&y, &y, &ny, RsnaP256FieldIsOdd(u) == RsnaP256FieldIsOdd(&y));
    RsnaP256PointSetAffine(r, &x, &y);
}

void RsnaP256ScalarReduceNonZero(rsna_u8 out[RSNA_P256_BYTES], const rsna_u8 in[RSNA_P256_BYTES])
{
    rsna_u32 a[RSNA_P256_LIMBS], t[RSNA_P256_LIMBS], orderMinusOne[RSNA_P256_LIMBS];
    rsna_u32 one[RSNA_P256_LIMBS] = { 1, 0, 0, 0, 0, 0, 0, 0 };

    RsnaBnFromBytes(a, in);
    RsnaBnSub(orderMinusOne, RsnaP256Order, one);
    if (RsnaBnSub(t, a, orderMinusOne) == 0)
        RsnaBnAdd(a, t, one);
    else
        RsnaBnAdd(a, a, one);
    RsnaBnToBytes(out, a);
}

int RsnaP256LessThanPrime(const rsna_u8 in[RSNA_P256_BYTES])
{
    rsna_u32 a[RSNA_P256_LIMBS];

    RsnaBnFromBytes(a, in);
    return RsnaBnCompare(a, RsnaP256Prime) < 0;
}

int RsnaP256ScalarIsValid(const rsna_u8 s[RSNA_P256_BYTES])
{
    rsna_u32 a[RSNA_P256_LIMBS], one[RSNA_P256_LIMBS] = { 1, 0, 0, 0, 0, 0, 0, 0 };

    RsnaBnFromBytes(a, s);
    return RsnaBnCompare(a, one) > 0 && RsnaBnCompare(a, RsnaP256Order) < 0;
}

void RsnaP256ScalarAdd(rsna_u8 out[RSNA_P256_BYTES], const rsna_u8 a[RSNA_P256_BYTES],
                       const rsna_u8 b[RSNA_P256_BYTES])
{
    rsna_u32 x[RSNA_P256_LIMBS], y[RSNA_P256_LIMBS], s[RSNA_P256_LIMBS], t[RSNA_P256_LIMBS];
    rsna_u32 carry;

    RsnaBnFromBytes(x, a);
    RsnaBnFromBytes(y, b);
    carry = RsnaBnAdd(s, x, y);
    if (RsnaBnSub(t, s, RsnaP256Order) == 0 || carry)
        RsnaBnToBytes(out, t);
    else
        RsnaBnToBytes(out, s);
}

int RsnaP256PointIsInfinity(const RSNA_P256_POINT *p)
{
    return RsnaBnIsZero(p->Z.v);
}

void RsnaP256PointSetAffine(RSNA_P256_POINT *p, const RSNA_P256_FE *x, const RSNA_P256_FE *y)
{
    RsnaFeCopy(&p->X, x);
    RsnaFeCopy(&p->Y, y);
    RsnaFeSetOne(&p->Z);
}

int RsnaP256PointFromBytes(RSNA_P256_POINT *p, const rsna_u8 x[RSNA_P256_BYTES],
                           const rsna_u8 y[RSNA_P256_BYTES])
{
    RSNA_P256_FE fx, fy, lhs, rhs;

    if (!RsnaP256FieldFromBytes(&fx, x) || !RsnaP256FieldFromBytes(&fy, y))
        return 0;
    RsnaFeSqr(&lhs, &fy);
    RsnaP256CurveRhs(&rhs, &fx);
    if (!RsnaFeEqual(&lhs, &rhs))
        return 0;
    RsnaP256PointSetAffine(p, &fx, &fy);
    return 1;
}

int RsnaP256PointToBytes(rsna_u8 x[RSNA_P256_BYTES], rsna_u8 y[RSNA_P256_BYTES],
                         const RSNA_P256_POINT *p)
{
    RSNA_P256_FE zinv, zinv2, t;

    if (RsnaP256PointIsInfinity(p))
        return 0;
    RsnaFeInv(&zinv, &p->Z);
    RsnaFeSqr(&zinv2, &zinv);
    RsnaFeMul(&t, &p->X, &zinv2);
    RsnaP256FieldToBytes(x, &t);
    RsnaFeMul(&zinv2, &zinv2, &zinv);
    RsnaFeMul(&t, &p->Y, &zinv2);
    RsnaP256FieldToBytes(y, &t);
    return 1;
}

static void RsnaP256PointDouble(RSNA_P256_POINT *r, const RSNA_P256_POINT *a)
{
    RSNA_P256_FE delta, gamma, beta, alpha, t1, t2, x3, y3, z3;

    RsnaFeSqr(&delta, &a->Z);
    RsnaFeSqr(&gamma, &a->Y);
    RsnaFeMul(&beta, &a->X, &gamma);
    RsnaFeSub(&t1, &a->X, &delta);
    RsnaFeAdd(&t2, &a->X, &delta);
    RsnaFeMul(&alpha, &t1, &t2);
    RsnaFeAdd(&t1, &alpha, &alpha);
    RsnaFeAdd(&alpha, &t1, &alpha);

    RsnaFeSqr(&x3, &alpha);
    RsnaFeAdd(&t1, &beta, &beta);
    RsnaFeAdd(&t1, &t1, &t1);
    RsnaFeAdd(&t2, &t1, &t1);
    RsnaFeSub(&x3, &x3, &t2);

    RsnaFeAdd(&z3, &a->Y, &a->Z);
    RsnaFeSqr(&z3, &z3);
    RsnaFeSub(&z3, &z3, &gamma);
    RsnaFeSub(&z3, &z3, &delta);

    RsnaFeSub(&t1, &t1, &x3);
    RsnaFeMul(&y3, &alpha, &t1);
    RsnaFeSqr(&t2, &gamma);
    RsnaFeAdd(&t2, &t2, &t2);
    RsnaFeAdd(&t2, &t2, &t2);
    RsnaFeAdd(&t2, &t2, &t2);
    RsnaFeSub(&y3, &y3, &t2);

    RsnaFeCopy(&r->X, &x3);
    RsnaFeCopy(&r->Y, &y3);
    RsnaFeCopy(&r->Z, &z3);
}

void RsnaP256PointAdd(RSNA_P256_POINT *r, const RSNA_P256_POINT *a, const RSNA_P256_POINT *b)
{
    RSNA_P256_FE z1z1, z2z2, u1, u2, s1, s2, h, i, j, rr, v, t, x3, y3, z3;

    if (RsnaP256PointIsInfinity(a))
    {
        *r = *b;
        return;
    }
    if (RsnaP256PointIsInfinity(b))
    {
        *r = *a;
        return;
    }

    RsnaFeSqr(&z1z1, &a->Z);
    RsnaFeSqr(&z2z2, &b->Z);
    RsnaFeMul(&u1, &a->X, &z2z2);
    RsnaFeMul(&u2, &b->X, &z1z1);
    RsnaFeMul(&s1, &a->Y, &b->Z);
    RsnaFeMul(&s1, &s1, &z2z2);
    RsnaFeMul(&s2, &b->Y, &a->Z);
    RsnaFeMul(&s2, &s2, &z1z1);
    RsnaFeSub(&h, &u2, &u1);
    RsnaFeSub(&rr, &s2, &s1);

    if (RsnaBnIsZero(h.v))
    {
        if (RsnaBnIsZero(rr.v))
        {
            RsnaP256PointDouble(r, a);
        }
        else
        {
            RsnaFeSetOne(&r->X);
            RsnaFeSetOne(&r->Y);
            RsnaFeSub(&r->Z, &r->Z, &r->Z);
        }
        return;
    }

    RsnaFeAdd(&i, &h, &h);
    RsnaFeSqr(&i, &i);
    RsnaFeMul(&j, &h, &i);
    RsnaFeAdd(&rr, &rr, &rr);
    RsnaFeMul(&v, &u1, &i);

    RsnaFeSqr(&x3, &rr);
    RsnaFeSub(&x3, &x3, &j);
    RsnaFeAdd(&t, &v, &v);
    RsnaFeSub(&x3, &x3, &t);

    RsnaFeSub(&t, &v, &x3);
    RsnaFeMul(&y3, &rr, &t);
    RsnaFeMul(&t, &s1, &j);
    RsnaFeAdd(&t, &t, &t);
    RsnaFeSub(&y3, &y3, &t);

    RsnaFeAdd(&z3, &a->Z, &b->Z);
    RsnaFeSqr(&z3, &z3);
    RsnaFeSub(&z3, &z3, &z1z1);
    RsnaFeSub(&z3, &z3, &z2z2);
    RsnaFeMul(&z3, &z3, &h);

    RsnaFeCopy(&r->X, &x3);
    RsnaFeCopy(&r->Y, &y3);
    RsnaFeCopy(&r->Z, &z3);
}

void RsnaP256PointNeg(RSNA_P256_POINT *r, const RSNA_P256_POINT *a)
{
    RsnaFeCopy(&r->X, &a->X);
    RsnaP256FieldNeg(&r->Y, &a->Y);
    RsnaFeCopy(&r->Z, &a->Z);
}

static void RsnaP256CondSwap(RSNA_P256_POINT *a, RSNA_P256_POINT *b, rsna_u32 swap)
{
    rsna_u32 mask = (rsna_u32)0 - swap, t;
    int i;

    for (i = 0; i < RSNA_P256_LIMBS; i++)
    {
        t = mask & (a->X.v[i] ^ b->X.v[i]); a->X.v[i] ^= t; b->X.v[i] ^= t;
        t = mask & (a->Y.v[i] ^ b->Y.v[i]); a->Y.v[i] ^= t; b->Y.v[i] ^= t;
        t = mask & (a->Z.v[i] ^ b->Z.v[i]); a->Z.v[i] ^= t; b->Z.v[i] ^= t;
    }
}

void RsnaP256PointMul(RSNA_P256_POINT *r, const RSNA_P256_POINT *p, const rsna_u8 k[RSNA_P256_BYTES])
{
    RSNA_P256_POINT r0, r1;
    rsna_u32 bit;
    int i;

    RsnaFeSetOne(&r0.X);
    RsnaFeSetOne(&r0.Y);
    for (i = 0; i < RSNA_P256_LIMBS; i++)
        r0.Z.v[i] = 0;
    r1 = *p;

    for (i = 255; i >= 0; i--)
    {
        bit = (k[RSNA_P256_BYTES - 1 - i / 8] >> (i % 8)) & 1;
        RsnaP256CondSwap(&r0, &r1, bit);
        RsnaP256PointAdd(&r1, &r0, &r1);
        RsnaP256PointDouble(&r0, &r0);
        RsnaP256CondSwap(&r0, &r1, bit);
    }

    *r = r0;
}
