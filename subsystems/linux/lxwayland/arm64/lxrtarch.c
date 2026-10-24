/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 entry point and system calls of the Wayland compositor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxrt.h"

__asm__(
    ".text\n"
    ".global _start\n"
    ".type _start, %function\n"
    "_start:\n"
    "    mov x29, #0\n"
    "    mov x30, #0\n"
    "    mov x0, sp\n"
    "    and x1, x0, #-16\n"
    "    mov sp, x1\n"
    "    bl LxStart\n"
    "    brk #0\n");

long
LxSyscall(long Number, long A0, long A1, long A2, long A3, long A4, long A5)
{
    register long X8 __asm__("x8") = Number;
    register long X0 __asm__("x0") = A0;
    register long X1 __asm__("x1") = A1;
    register long X2 __asm__("x2") = A2;
    register long X3 __asm__("x3") = A3;
    register long X4 __asm__("x4") = A4;
    register long X5 __asm__("x5") = A5;

    __asm__ __volatile__("svc #0"
                         : "+r"(X0)
                         : "r"(X8), "r"(X1), "r"(X2), "r"(X3), "r"(X4), "r"(X5)
                         : "memory");
    return X0;
}
