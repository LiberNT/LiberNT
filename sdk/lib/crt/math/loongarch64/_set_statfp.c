/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 implementation of _set_statfp for scalar libm
 */

#include <stdint.h>

void
_set_statfp(uintptr_t Mask)
{
    uintptr_t Flags;
    uintptr_t Raised = 0;

    if (Mask & 0x0001) Raised |= 0x10; /* invalid operation -> V */
    if (Mask & 0x0004) Raised |= 0x08; /* divide by zero -> Z */
    if (Mask & 0x0008) Raised |= 0x04; /* overflow -> O */
    if (Mask & 0x0010) Raised |= 0x02; /* underflow -> U */
    if (Mask & 0x0020) Raised |= 0x01; /* inexact -> I */

    __asm__ __volatile__("movfcsr2gr %0, $fcsr0" : "=r"(Flags));
    Flags |= Raised;
    __asm__ __volatile__("movgr2fcsr $fcsr0, %0" :: "r"(Flags));
}
