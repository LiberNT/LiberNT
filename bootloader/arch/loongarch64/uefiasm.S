/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 UEFI stack and boot services helpers
 */

    .text

    .extern BasicStack
    .extern ExecuteLoaderCleanly
    .extern UefiExitBootServices
    .extern UefiServiceStack

    .globl _changestack
_changestack:
    addi.d  $sp, $sp, -16
    st.d    $s0, $sp, 0
    st.d    $ra, $sp, 8
    move    $s0, $sp
    move    $a0, $sp
    la.local $t0, BasicStack
    ld.d    $sp, $t0, 0
    li.d    $t1, -16
    and     $sp, $sp, $t1
    bl      ExecuteLoaderCleanly
    move    $sp, $s0
    ld.d    $ra, $sp, 8
    ld.d    $s0, $sp, 0
    addi.d  $sp, $sp, 16
    jr      $ra

    .globl _exituefi
_exituefi:
    addi.d  $sp, $sp, -32
    st.d    $s0, $sp, 0
    st.d    $s1, $sp, 8
    st.d    $ra, $sp, 16
    move    $s0, $sp
    move    $s1, $ra
    la.local $t0, UefiServiceStack
    ld.d    $t0, $t0, 0
    beqz    $t0, 1f
    move    $sp, $t0
    li.d    $t1, -16
    and     $sp, $sp, $t1
1:
    bl      UefiExitBootServices
    ibar    0
    move    $sp, $s0
    move    $ra, $s1
    ld.d    $s1, $sp, 8
    ld.d    $s0, $sp, 0
    addi.d  $sp, $sp, 32
    jr      $ra
