/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Implementation of __setfp
 * COPYRIGHT:   Copyright Timo Kreuzer (timo.kreuzer@reactos.org)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global __setfp
    .thumb_func
__setfp:
    vmsr    fpscr, r0
    bx      lr
