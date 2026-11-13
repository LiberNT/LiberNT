/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Implementation of setjmp for LoongArch64
 */

    .text
    .p2align 2

    .global _setjmp
_setjmp:
    move    $t0, $zero
    b       .Lsave_context

    .global __intrinsic_setjmp
__intrinsic_setjmp:
    move    $t0, $zero
    b       .Lsave_context

    .global _setjmpex
_setjmpex:
    move    $t0, $a1
    b       .Lsave_context

    .global __intrinsic_setjmpex
__intrinsic_setjmpex:
    move    $t0, $a1

.Lsave_context:
    st.d    $t0, $a0, 0
    st.d    $ra, $a0, 8
    st.d    $sp, $a0, 16
    st.d    $fp, $a0, 24
    st.d    $s0, $a0, 32
    st.d    $s1, $a0, 40
    st.d    $s2, $a0, 48
    st.d    $s3, $a0, 56
    st.d    $s4, $a0, 64
    st.d    $s5, $a0, 72
    st.d    $s6, $a0, 80
    st.d    $s7, $a0, 88
    st.d    $s8, $a0, 96

    movfcsr2gr $t1, $fcsr0
    st.d    $t1, $a0, 104
    fst.d   $fs0, $a0, 112
    fst.d   $fs1, $a0, 120
    fst.d   $fs2, $a0, 128
    fst.d   $fs3, $a0, 136
    fst.d   $fs4, $a0, 144
    fst.d   $fs5, $a0, 152
    fst.d   $fs6, $a0, 160
    fst.d   $fs7, $a0, 168

    move    $a0, $zero
    jr      $ra
