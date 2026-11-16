/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Implementation of __getfp
 * COPYRIGHT:   Copyright Timo Kreuzer (timo.kreuzer@reactos.org)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global __getfp
    .thumb_func
__getfp:
    vmrs    r0, fpscr
    bx      lr
