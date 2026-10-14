/*
 * PROJECT:     LiberNT MSVCRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 floating-point control and status words
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include "include/float.h"
#include "windef.h"
#include "winbase.h"
#include "msvcrt.h"

#define RISCV_FFLAGS_NX 0x01
#define RISCV_FFLAGS_UF 0x02
#define RISCV_FFLAGS_OF 0x04
#define RISCV_FFLAGS_DZ 0x08
#define RISCV_FFLAGS_NV 0x10
#define RISCV_FFLAGS_MASK 0x1f
#define RISCV_FRM_SHIFT 5
#define RISCV_FRM_MASK (7 << RISCV_FRM_SHIFT)
#define RISCV_FRM_RNE 0
#define RISCV_FRM_RTZ 1
#define RISCV_FRM_RDN 2
#define RISCV_FRM_RUP 3

void msvcrt_arch_setfp(unsigned int *cw, unsigned int cw_mask, unsigned int *sw, unsigned int sw_mask)
{
    ULONG_PTR old_fcsr, fcsr;
    unsigned int flags;

    __asm__ __volatile__( "frcsr %0" : "=r" (fcsr) );
    old_fcsr = fcsr;

    cw_mask &= _MCW_EM | _MCW_RC;
    sw_mask &= _MCW_EM;

    if (sw)
    {
        flags = 0;
        if (fcsr & RISCV_FFLAGS_NV) flags |= _SW_INVALID;
        if (fcsr & RISCV_FFLAGS_DZ) flags |= _SW_ZERODIVIDE;
        if (fcsr & RISCV_FFLAGS_OF) flags |= _SW_OVERFLOW;
        if (fcsr & RISCV_FFLAGS_UF) flags |= _SW_UNDERFLOW;
        if (fcsr & RISCV_FFLAGS_NX) flags |= _SW_INEXACT;

        *sw = (flags & ~sw_mask) | (*sw & sw_mask);
        fcsr &= ~(ULONG_PTR)RISCV_FFLAGS_MASK;
        if (*sw & _SW_INVALID) fcsr |= RISCV_FFLAGS_NV;
        if (*sw & _SW_ZERODIVIDE) fcsr |= RISCV_FFLAGS_DZ;
        if (*sw & _SW_OVERFLOW) fcsr |= RISCV_FFLAGS_OF;
        if (*sw & _SW_UNDERFLOW) fcsr |= RISCV_FFLAGS_UF;
        if (*sw & _SW_INEXACT) fcsr |= RISCV_FFLAGS_NX;
        *sw = flags;
    }

    if (cw)
    {
        flags = _EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT | _EM_DENORMAL;
        switch ((fcsr & RISCV_FRM_MASK) >> RISCV_FRM_SHIFT)
        {
        case RISCV_FRM_RTZ: flags |= _RC_CHOP; break;
        case RISCV_FRM_RDN: flags |= _RC_DOWN; break;
        case RISCV_FRM_RUP: flags |= _RC_UP; break;
        }

        *cw = (flags & ~cw_mask) | (*cw & cw_mask);
        fcsr &= ~(ULONG_PTR)RISCV_FRM_MASK;
        switch (*cw & _MCW_RC)
        {
        case _RC_CHOP: fcsr |= RISCV_FRM_RTZ << RISCV_FRM_SHIFT; break;
        case _RC_DOWN: fcsr |= RISCV_FRM_RDN << RISCV_FRM_SHIFT; break;
        case _RC_UP: fcsr |= RISCV_FRM_RUP << RISCV_FRM_SHIFT; break;
        }
    }

    if (old_fcsr != fcsr)
        __asm__ __volatile__( "fscsr %0" :: "r" (fcsr) );
}
