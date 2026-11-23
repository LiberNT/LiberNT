/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           Stack checker
 * FILE:              lib/sdk/crt/except/i386/chkstk_asm.s
 * PROGRAMER:         KJK::Hyperion <noog@libero.it>
 */

#include <asm.inc>
#include <ks386.inc>

#define PAGE_SIZE 4096

#ifdef CRT_COMPILER_STACK_PROBE_ALIASES
PUBLIC __alloca
PUBLIC ___chkstk
#endif
PUBLIC __chkstk
PUBLIC __alloca_probe
PUBLIC __alloca_probe_16
PUBLIC __alloca_probe_8
.code

__alloca_probe_8:
    push ecx
    lea ecx, [esp + 8]
    sub ecx, eax
    and ecx, 7
    add eax, ecx
    jnc l2
    mov eax, HEX(0ffffffff)
l2:
    pop ecx
    jmp __chkstk

    /* 16 byte aligned alloca probe
     * EAX = size to be allocated */
__alloca_probe_16:
    /* save the ECX register */
    push ecx

    /* ecx = top of the previous stack frame */
    lea ecx, [esp + 8]

    /* Calculate end of allocation */
    sub ecx, eax

    /* Get the misalignment */
    and ecx, 15

    /* Add the misalignment to the original alloc size */
    add eax, ecx

    /* Check for overflow */
    jnc l1

    /* Set maximum value */
    mov eax, HEX(0ffffffff)
l1:
    /* Restore ecx */
    pop ecx
    /* Fall through to __chkstk */

/*
 _chkstk() is called by all stack allocations of more than 4 KB. It grows the
 stack in areas of 4 KB each, trying to access each area. This ensures that the
 guard page for the stack is hit, and the stack growing triggered
 */
#ifdef CRT_COMPILER_STACK_PROBE_ALIASES
__alloca:
___chkstk:
#endif
__chkstk:
__alloca_probe:

    /* EAX = size to be allocated */
    /* save the ECX register */
    push ecx

    /* ECX = top of the previous stack frame */
    lea ecx, [esp + 8]

    /* probe the desired memory, page by page */
    cmp eax, PAGE_SIZE
    jb .l_LessThanAPage

.l_MoreThanAPage:

    /* raise the top of the stack by a page and probe */
    sub ecx, PAGE_SIZE
    test [ecx], eax

    /* loop if still more than a page must be probed */
    sub eax, PAGE_SIZE
    cmp eax, PAGE_SIZE
    jae .l_MoreThanAPage

.l_LessThanAPage:

    /* raise the top of the stack by EAX bytes (size % 4096) and probe */
    sub ecx, eax
    test [ecx], eax

    /* Keep the return address on the active stack, even for alloca(0). */
    lea eax, [esp + 8]
    sub eax, ecx
    pop ecx
    sub esp, eax
    push dword ptr [esp + eax]
    ret 4

/* EOF */
END
