/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Threads that run user code with a null GS selector
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

#define GsHolderWait        HEX(00)
#define GsHolderSetEvent    HEX(08)
#define GsHolderTerminate   HEX(10)
#define GsHolderWaitEvent   HEX(18)
#define GsHolderArmedEvent  HEX(20)
#define GsHolderRelease     HEX(28)
#define GsHolderGsBefore    HEX(2C)
#define GsHolderGsAfter     HEX(2E)
#define GsHolderTimeout     HEX(30)

.code64

PUBLIC ReadGsSelector
FUNC ReadGsSelector
    .ENDPROLOG
    mov ax, gs
    ret
ENDFUNC

PUBLIC NullGsWaitHolder
FUNC NullGsWaitHolder
    push rbx
    .PUSHREG rbx
    sub rsp, 32
    .ALLOCSTACK 32
    .ENDPROLOG
    mov rbx, rcx
    mov ax, gs
    mov [rbx + GsHolderGsBefore], ax
    xor eax, eax
    mov gs, ax
    mov rcx, [rbx + GsHolderArmedEvent]
    xor edx, edx
    call qword ptr [rbx + GsHolderSetEvent]
    mov rcx, [rbx + GsHolderWaitEvent]
    xor edx, edx
    lea r8, [rbx + GsHolderTimeout]
    call qword ptr [rbx + GsHolderWait]
    mov ax, gs
    mov [rbx + GsHolderGsAfter], ax
    mov rcx, -2
    xor edx, edx
    call qword ptr [rbx + GsHolderTerminate]
    int 3
ENDFUNC

PUBLIC NullGsSpinHolder
FUNC NullGsSpinHolder
    push rbx
    .PUSHREG rbx
    sub rsp, 32
    .ALLOCSTACK 32
    .ENDPROLOG
    mov rbx, rcx
    mov ax, gs
    mov [rbx + GsHolderGsBefore], ax
    xor eax, eax
    mov gs, ax
    mov rcx, [rbx + GsHolderArmedEvent]
    xor edx, edx
    call qword ptr [rbx + GsHolderSetEvent]
NullGsSpinHolderLoop:
    pause
    cmp dword ptr [rbx + GsHolderRelease], 0
    je NullGsSpinHolderLoop
    mov ax, gs
    mov [rbx + GsHolderGsAfter], ax
    mov rcx, -2
    xor edx, edx
    call qword ptr [rbx + GsHolderTerminate]
    int 3
ENDFUNC

END
