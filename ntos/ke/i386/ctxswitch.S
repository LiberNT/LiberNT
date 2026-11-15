/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 context switch, thread startup and idle wait
 * COPYRIGHT:   Copyright Alex Ionescu <alex@relsoft.net>
 *              Copyright Gregor Anich (FPU Code)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>

EXTERN @KiSwapContextEntry@8:PROC
EXTERN @KiSwapContextExit@8:PROC
EXTERN @KiRetireDpcList@4:PROC
EXTERN @KiRunStackCallout@4:PROC
EXTERN @KiEnterV86Mode@4:PROC
EXTERN @KiExitV86Mode@4:PROC
EXTERN _KeI386FxsrPresent:DWORD

.code

PUBLIC @KiSwapContextInternal@0
@KiSwapContextInternal@0:

    sub esp, 2 * 4
    mov ecx, esp
    jmp @KiSwapContextEntry@8

PUBLIC @KiSwapContext@8
@KiSwapContext@8:

    sub esp, 4 * 4

    mov [esp+12], ebx
    mov [esp+8], esi
    mov [esp+4], edi
    mov [esp+0], ebp

    or dl, cl

    call @KiSwapContextInternal@0

    mov ebp, [esp+0]
    mov edi, [esp+4]
    mov esi, [esp+8]
    mov ebx, [esp+12]

    add esp, 4 * 4
    ret

PUBLIC @KiSwitchThreads@8
@KiSwitchThreads@8:

    mov esp, edx
#if DBG

    /* Restore the frame pointer early to get sensible backtraces */
    mov ebp, [esp+12]
#endif
    call @KiSwapContextExit@8

    add esp, 2 * 4
    ret

PUBLIC @KiRetireDpcListInDpcStack@8
@KiRetireDpcListInDpcStack@8:

    mov eax, esp
    mov esp, edx
    push eax
    call @KiRetireDpcList@4

    pop esp
    ret

PUBLIC @KiCallOnKernelStack@8
@KiCallOnKernelStack@8:
    /* Switch stacks and run the callout; ecx still holds the context */
    mov eax, esp
    mov esp, edx
    push eax
    call @KiRunStackCallout@4

    pop esp
    ret

PUBLIC _Ki386EnableCurrentLargePage@8
_Ki386EnableCurrentLargePage@8:

    mov eax, [esp + 4]

    mov ecx, [esp + 8]

    pushfd

    cli

    sub eax, offset _Ki386EnableCurrentLargePage@8
    add eax, offset _Ki386LargePageIdentityLabel

    mov edx, cr3
    mov cr3, ecx

    jmp eax

_Ki386LargePageIdentityLabel:

    mov eax, cr0
    and eax, NOT CR0_PG
    mov cr0, eax

#ifdef _USE_ML
    jmp $+2
#else
    .byte HEX(EB), 0
#endif

    mov ecx, cr4
    or ecx, CR4_PSE
    mov cr4, ecx

    or eax, CR0_PG
    mov cr0, eax

    mov eax, offset VirtualSpace
    jmp eax

VirtualSpace:

    mov cr3, edx

    popfd

    ret 8

PUBLIC _Ki386SetupAndExitToV86Mode@4
_Ki386SetupAndExitToV86Mode@4:

    pushad
    sub esp, (12 + KTRAP_FRAME_LENGTH + NPX_FRAME_LENGTH + 16)
    mov ecx, esp
    call @KiEnterV86Mode@4
#ifdef _USE_ML
    jmp $
#else
.LKi386V86ModeHang:
    jmp .LKi386V86ModeHang
#endif

PUBLIC @Ki386BiosCallReturnAddress@4
@Ki386BiosCallReturnAddress@4:

    call @KiExitV86Mode@4
    mov esp, eax
    add esp, (12 + KTRAP_FRAME_LENGTH + NPX_FRAME_LENGTH + 16)
    popad
    ret 4

PUBLIC _FrRestore
PUBLIC @Ke386LoadFpuState@4
@Ke386LoadFpuState@4:

    test byte ptr [_KeI386FxsrPresent], 1
    jz _FrRestore

    fxrstor [ecx]
    ret

_FrRestore:
    frstor [ecx]
    ret

END
