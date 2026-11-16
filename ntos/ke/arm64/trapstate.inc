/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 exception entry state layout and SIMD register macros shared by the vector code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

.equ ARM64_EARLY_STATE_VECTORID,      0x0
.equ ARM64_EARLY_STATE_ESR,           0x8
.equ ARM64_EARLY_STATE_FAR,           0x10
.equ ARM64_EARLY_STATE_ELR,           0x18
.equ ARM64_EARLY_STATE_SPSR,          0x20
.equ ARM64_EARLY_STATE_REGS,          0x28
.equ ARM64_EARLY_STATE_REG_SP,        0x120   /* X[0..30] = 0x28 + (31*8) = 0x120 */
.equ ARM64_EARLY_STATE_REG_PC,        0x128   /* Sp + 8 = 0x128 */
.equ ARM64_EARLY_STATE_REG_PSTATE,    0x130   /* Pc + 8 = 0x130 */
.equ ARM64_EARLY_STATE_TRAP_PTR,      0x138
.equ ARM64_EARLY_STATE_EXC_PTR,       0x140
.equ ARM64_EARLY_STATE_EXC_FRAME,     0x148
.equ ARM64_EARLY_STATE_TRAP_FRAME,    0x210
.equ ARM64_EARLY_STATE_VFP,           0x360
.equ ARM64_EARLY_STATE_FPCR,          0x368
.equ ARM64_EARLY_STATE_FPSR,          0x36C
.equ ARM64_EARLY_STATE_NEON,          0x370
.equ ARM64_EARLY_STATE_SIZE,          0x580

/*
 * The ARM64 compiler can use FP/SIMD registers for ordinary C struct copies and
 * zeroing. Preserve Q0-Q31 plus FPCR/FPSR around exception/interrupt C handlers
 * so an asynchronous kernel entry cannot corrupt interrupted user or kernel
 * SIMD state.
 */
.macro ARM64_SAVE_NEON Base, Offset
    add x9, \Base, #\Offset
    stp q0,  q1,  [x9, #0x000]
    stp q2,  q3,  [x9, #0x020]
    stp q4,  q5,  [x9, #0x040]
    stp q6,  q7,  [x9, #0x060]
    stp q8,  q9,  [x9, #0x080]
    stp q10, q11, [x9, #0x0A0]
    stp q12, q13, [x9, #0x0C0]
    stp q14, q15, [x9, #0x0E0]
    stp q16, q17, [x9, #0x100]
    stp q18, q19, [x9, #0x120]
    stp q20, q21, [x9, #0x140]
    stp q22, q23, [x9, #0x160]
    stp q24, q25, [x9, #0x180]
    stp q26, q27, [x9, #0x1A0]
    stp q28, q29, [x9, #0x1C0]
    stp q30, q31, [x9, #0x1E0]
.endm

.macro ARM64_RESTORE_NEON Base, Offset
    add x9, \Base, #\Offset
    ldp q0,  q1,  [x9, #0x000]
    ldp q2,  q3,  [x9, #0x020]
    ldp q4,  q5,  [x9, #0x040]
    ldp q6,  q7,  [x9, #0x060]
    ldp q8,  q9,  [x9, #0x080]
    ldp q10, q11, [x9, #0x0A0]
    ldp q12, q13, [x9, #0x0C0]
    ldp q14, q15, [x9, #0x0E0]
    ldp q16, q17, [x9, #0x100]
    ldp q18, q19, [x9, #0x120]
    ldp q20, q21, [x9, #0x140]
    ldp q22, q23, [x9, #0x160]
    ldp q24, q25, [x9, #0x180]
    ldp q26, q27, [x9, #0x1A0]
    ldp q28, q29, [x9, #0x1C0]
    ldp q30, q31, [x9, #0x1E0]
.endm

.macro ARM64_ZERO_VOLATILE_NEON
    movi v0.16b, #0
    movi v1.16b, #0
    movi v2.16b, #0
    movi v3.16b, #0
    movi v4.16b, #0
    movi v5.16b, #0
    movi v6.16b, #0
    movi v7.16b, #0
    movi v16.16b, #0
    movi v17.16b, #0
    movi v18.16b, #0
    movi v19.16b, #0
    movi v20.16b, #0
    movi v21.16b, #0
    movi v22.16b, #0
    movi v23.16b, #0
    movi v24.16b, #0
    movi v25.16b, #0
    movi v26.16b, #0
    movi v27.16b, #0
    movi v28.16b, #0
    movi v29.16b, #0
    movi v30.16b, #0
    movi v31.16b, #0
.endm
