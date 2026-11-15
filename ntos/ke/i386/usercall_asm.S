/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 user-mode callback entry and return
 * COPYRIGHT:   Copyright Alex Ionescu <alex@relsoft.net>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>
#include <internal/i386/asmmacro.S>

EXTERN _MmGrowKernelStack@4:PROC
EXTERN _KeUserCallbackDispatcher:DWORD
EXTERN @KiServiceExit@8:PROC
EXTERN _KeGetCurrentIrql@0:PROC
EXTERN _KeBugCheckEx@20:PROC
EXTERN @KiUserModeCallout@4:PROC

.code

PUBLIC _KiCallUserMode@8
_KiCallUserMode@8:

    push ebp
    push ebx
    push esi
    push edi

    lea ecx, [esp - 12]

    sub esp, 12 + NPX_FRAME_LENGTH + KTRAP_FRAME_LENGTH + 16

    call @KiUserModeCallout@4

    add esp, 12 + NPX_FRAME_LENGTH + KTRAP_FRAME_LENGTH + 16

    pop edi
    pop esi
    pop ebx
    pop ebp

    ret 8

PUBLIC @KiCallbackReturn@8
@KiCallbackReturn@8:

    mov esp, ecx

    mov eax, edx
    pop edi
    pop esi
    pop ebx
    pop ebp

    ret 8

PUBLIC _KeSwitchKernelStack@8
_KeSwitchKernelStack@8:

    push esi
    push edi

    mov edx, fs:[KPCR_CURRENT_THREAD]

    mov edi, [esp+12]
    mov ecx, [edx+KTHREAD_STACK_BASE]

    sub ebp, ecx
    add ebp, edi

    mov eax, [edx+KTHREAD_TRAP_FRAME]
    sub eax, ecx
    add eax, edi
    mov [edx+KTHREAD_TRAP_FRAME], eax

    sub ecx, esp

    sub edi, ecx
    mov esi, esp

    push edi

    rep movsb

    pop edi

    mov eax, [edx+KTHREAD_STACK_BASE]
    mov ecx, [esp+12]
    mov esi, [esp+16]

    cli

    mov [edx+KTHREAD_STACK_BASE], ecx
    mov [edx+KTHREAD_STACK_LIMIT], esi

    mov byte ptr [edx+KTHREAD_LARGE_STACK], 1

    mov [edx+KTHREAD_INITIAL_STACK], ecx

    mov esi, [edx+KTHREAD_TRAP_FRAME]

    mov edx, fs:[KPCR_TSS]

    test dword ptr [esi+KTRAP_FRAME_EFLAGS], EFLAGS_V86_MASK

    lea ecx, [ecx-NPX_FRAME_LENGTH]
    jnz V86Switch
    sub ecx, 16

V86Switch:

    mov [edx+KTSS_ESP0], ecx

    mov esp, edi

    sti
    pop edi
    pop esi
    ret 8

END
