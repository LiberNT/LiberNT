/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Frame-less longjmp for LoongArch64
 */

    .text
    .p2align 2

    .global __longjmp_noframe
__longjmp_noframe:
    ld.d    $ra, $a0, 8
    ld.d    $sp, $a0, 16
    ld.d    $fp, $a0, 24
    ld.d    $s0, $a0, 32
    ld.d    $s1, $a0, 40
    ld.d    $s2, $a0, 48
    ld.d    $s3, $a0, 56
    ld.d    $s4, $a0, 64
    ld.d    $s5, $a0, 72
    ld.d    $s6, $a0, 80
    ld.d    $s7, $a0, 88
    ld.d    $s8, $a0, 96

    ld.d    $t1, $a0, 104
    movgr2fcsr $fcsr0, $t1
    fld.d   $fs0, $a0, 112
    fld.d   $fs1, $a0, 120
    fld.d   $fs2, $a0, 128
    fld.d   $fs3, $a0, 136
    fld.d   $fs4, $a0, 144
    fld.d   $fs5, $a0, 152
    fld.d   $fs6, $a0, 160
    fld.d   $fs7, $a0, 168

    move    $a0, $a1
    jr      $ra
