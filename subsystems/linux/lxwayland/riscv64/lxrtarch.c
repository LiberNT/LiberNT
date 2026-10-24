/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 entry point and system calls of the Wayland compositor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../lxrt.h"

__asm__(
    ".text\n"
    ".global _start\n"
    ".type _start, @function\n"
    "_start:\n"
    ".option push\n"
    ".option norelax\n"
    "    lla gp, __global_pointer$\n"
    ".option pop\n"
    "    mv s0, zero\n"
    "    mv ra, zero\n"
    "    mv a0, sp\n"
    "    andi sp, sp, -16\n"
    "    call LxStart\n"
    "    ebreak\n");

long
LxSyscall(long Number, long A0, long A1, long A2, long A3, long A4, long A5)
{
    register long A7 __asm__("a7") = Number;
    register long R0 __asm__("a0") = A0;
    register long R1 __asm__("a1") = A1;
    register long R2 __asm__("a2") = A2;
    register long R3 __asm__("a3") = A3;
    register long R4 __asm__("a4") = A4;
    register long R5 __asm__("a5") = A5;

    __asm__ __volatile__("ecall"
                         : "+r"(R0)
                         : "r"(A7), "r"(R1), "r"(R2), "r"(R3), "r"(R4), "r"(R5)
                         : "memory");
    return R0;
}
