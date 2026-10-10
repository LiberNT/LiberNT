/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 outline atomic helpers selecting LSE at run time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ksarm64.h>

#define LSE_CAT2_(a, b) a##b
#define LSE_CAT2(a, b) LSE_CAT2_(a, b)
#define LSE_CAT3(a, b, c) LSE_CAT2(LSE_CAT2(a, b), c)
#define LSE_CAT4(a, b, c, d) LSE_CAT2(LSE_CAT3(a, b, c), d)

#if LSE_SIZE == 1
#define LSE_SFX b
#define LSE_REG(n) w##n
#define LSE_EXTEND uxtb
#elif LSE_SIZE == 2
#define LSE_SFX h
#define LSE_REG(n) w##n
#define LSE_EXTEND uxth
#elif LSE_SIZE == 4
#define LSE_SFX
#define LSE_REG(n) w##n
#define LSE_EXTEND mov
#elif LSE_SIZE == 8 || LSE_SIZE == 16
#define LSE_SFX
#define LSE_REG(n) x##n
#define LSE_EXTEND mov
#else
#error LSE_SIZE
#endif

#if LSE_MODEL == 1
#define LSE_SUFFIX _relax
#define LSE_ACQ
#define LSE_REL
#define LLSC_ACQ
#define LLSC_REL
#define LSE_BARRIER
#elif LSE_MODEL == 2
#define LSE_SUFFIX _acq
#define LSE_ACQ a
#define LSE_REL
#define LLSC_ACQ a
#define LLSC_REL
#define LSE_BARRIER
#elif LSE_MODEL == 3
#define LSE_SUFFIX _rel
#define LSE_ACQ
#define LSE_REL l
#define LLSC_ACQ
#define LLSC_REL l
#define LSE_BARRIER
#elif LSE_MODEL == 4
#define LSE_SUFFIX _acq_rel
#define LSE_ACQ a
#define LSE_REL l
#define LLSC_ACQ a
#define LLSC_REL l
#define LSE_BARRIER
#elif LSE_MODEL == 5 && defined(LSE_OP_SWP)
#define LSE_SUFFIX _sync
#define LSE_ACQ a
#define LSE_REL
#define LLSC_ACQ
#define LLSC_REL
#define LSE_BARRIER dmb ish
#elif LSE_MODEL == 5
#define LSE_SUFFIX _sync
#define LSE_ACQ a
#define LSE_REL l
#define LLSC_ACQ
#define LLSC_REL l
#define LSE_BARRIER dmb ish
#else
#error LSE_MODEL
#endif

#if defined(LSE_OP_CAS)
#define LSE_OP cas
#elif defined(LSE_OP_SWP)
#define LSE_OP swp
#elif defined(LSE_OP_LDADD)
#define LSE_OP ldadd
#define LLSC_OP add
#elif defined(LSE_OP_LDCLR)
#define LSE_OP ldclr
#define LLSC_OP bic
#elif defined(LSE_OP_LDEOR)
#define LSE_OP ldeor
#define LLSC_OP eor
#elif defined(LSE_OP_LDSET)
#define LSE_OP ldset
#define LLSC_OP orr
#else
#error LSE_OP
#endif

#define LSE_NAME LSE_CAT4(__aarch64_, LSE_OP, LSE_SIZE, LSE_SUFFIX)
#define LSE_INSN LSE_CAT4(LSE_OP, LSE_ACQ, LSE_REL, LSE_SFX)
#define LLSC_LDXR LSE_CAT4(ld, LLSC_ACQ, xr, LSE_SFX)
#define LLSC_STXR LSE_CAT4(st, LLSC_REL, xr, LSE_SFX)
#define LLSC_LDXP LSE_CAT3(ld, LLSC_ACQ, xp)
#define LLSC_STXP LSE_CAT3(st, LLSC_REL, xp)

#if defined(LSE_KERNEL_MODE)
    .if (USER_SHARED_DATA & 0xffffffff)
    .error "USER_SHARED_DATA does not fit movz/movk at bits 32-63"
    .endif
.macro lse_check_feature label
    movz    x16, #((USER_SHARED_DATA >> 32) & 0xffff), lsl #32
    movk    x16, #((USER_SHARED_DATA >> 48) & 0xffff), lsl #48
    ldrb    w16, [x16, #(UsProcessorFeatures + PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE)]
    cbz     w16, \label
.endm
#elif defined(LSE_USER_MODE)
    .if (MM_SHARED_USER_DATA_VA & ~0xffff0000)
    .error "MM_SHARED_USER_DATA_VA does not fit movz at bits 16-31"
    .endif
.macro lse_check_feature label
    movz    x16, #((MM_SHARED_USER_DATA_VA >> 16) & 0xffff), lsl #16
    ldrb    w16, [x16, #(UsProcessorFeatures + PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE)]
    cbz     w16, \label
.endm
#endif

    .arch_extension lse
    .text
    .balign 16
    .global LSE_NAME
LSE_NAME:

#if defined(LSE_OP_CAS) && LSE_SIZE == 16

#if defined(LSE_KERNEL_MODE) || defined(LSE_USER_MODE)
    lse_check_feature 8f
    LSE_CAT3(casp, LSE_ACQ, LSE_REL) x0, x1, x2, x3, [x4]
    ret
8:
#endif
    mov     x16, x0
    mov     x17, x1
0:
    LLSC_LDXP x0, x1, [x4]
    cmp     x0, x16
    ccmp    x1, x17, #0, eq
    b.ne    1f
    LLSC_STXP w15, x2, x3, [x4]
    cbnz    w15, 0b
    LSE_BARRIER
    ret
1:
    LLSC_STXP w15, x0, x1, [x4]
    cbnz    w15, 0b
    LSE_BARRIER
    ret

#elif defined(LSE_OP_CAS)

#if defined(LSE_KERNEL_MODE) || defined(LSE_USER_MODE)
    lse_check_feature 8f
    LSE_INSN LSE_REG(0), LSE_REG(1), [x2]
    ret
8:
#endif
    LSE_EXTEND LSE_REG(16), LSE_REG(0)
0:
    LLSC_LDXR LSE_REG(0), [x2]
    cmp     LSE_REG(0), LSE_REG(16)
    b.ne    1f
    LLSC_STXR w17, LSE_REG(1), [x2]
    cbnz    w17, 0b
1:
    LSE_BARRIER
    ret

#elif defined(LSE_OP_SWP)

#if defined(LSE_KERNEL_MODE) || defined(LSE_USER_MODE)
    lse_check_feature 8f
    LSE_INSN LSE_REG(0), LSE_REG(0), [x1]
    ret
8:
#endif
    mov     LSE_REG(16), LSE_REG(0)
0:
    LLSC_LDXR LSE_REG(0), [x1]
    LLSC_STXR w17, LSE_REG(16), [x1]
    cbnz    w17, 0b
    LSE_BARRIER
    ret

#else

#if defined(LSE_KERNEL_MODE) || defined(LSE_USER_MODE)
    lse_check_feature 8f
    LSE_INSN LSE_REG(0), LSE_REG(0), [x1]
    ret
8:
#endif
    mov     LSE_REG(16), LSE_REG(0)
0:
    LLSC_LDXR LSE_REG(0), [x1]
    LLSC_OP LSE_REG(17), LSE_REG(0), LSE_REG(16)
    LLSC_STXR w15, LSE_REG(17), [x1]
    cbnz    w15, 0b
    LSE_BARRIER
    ret

#endif
