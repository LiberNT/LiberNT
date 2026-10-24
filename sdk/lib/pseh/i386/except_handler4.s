/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     EH4 frame-based exception handler and local unwind for x86
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>

PARAM_ExceptionRecord       =  8
PARAM_RegistrationFrame     = 12
PARAM_Context               = 16

FRAME_xpointers             = -4
FRAME_scopetable            =  8
FRAME_trylevel              = 12
FRAME_ebp                   = 16

SCOPETABLE_V4_entries       = 16
SCOPETABLE_previousTryLevel =  0
SCOPETABLE_lpfnFilter       =  4
SCOPETABLE_lpfnHandler      =  8

EXTERN _RtlUnwind@16:PROC

#ifdef _USE_ML
EXTERN ___security_cookie (__except_handler4):DWORD
#elif defined(__clang__)
.section .drectve, "yn"
.ascii " /alternatename:___security_cookie=__except_handler4"
#else
.weak ___security_cookie
#endif

PUBLIC __except_handler4
PUBLIC __except_handler4_local_unwind

.code

_unwind_handler4:

    mov ecx, [esp+4]
    test dword ptr [ecx+EXCEPTION_RECORD_EXCEPTION_FLAGS], EXCEPTION_EXIT_UNWIND + EXCEPTION_UNWINDING
    mov eax, ExceptionContinueSearch
    jz unwind_handler4_return

    mov eax, [esp+8]
    push dword ptr [eax+36]
    push dword ptr [eax+32]
    push dword ptr [eax+28]
    call __except_handler4_local_unwind
    add esp, 12

    mov eax, [esp+8]
    mov edx, [esp+16]
    mov [edx], eax

    mov eax, ExceptionCollidedUnwind

unwind_handler4_return:
    ret


__except_handler4_local_unwind:

    push ebx
    push esi
    push edi
    push ebp

    PUSH_OFFSET _unwind_handler4
    push dword ptr fs:[0]
    mov dword ptr fs:[0], esp

unwind4_loop:
    mov eax, [esp+32]
    mov ebx, [eax+FRAME_scopetable]
    mov ecx, [esp+28]
    xor ebx, [ecx]
    mov esi, [eax+FRAME_trylevel]

    cmp esi, -2
    je unwind4_return
    cmp esi, [esp+36]
    je unwind4_return

    lea esi, [esi+esi*2]
    mov ecx, [ebx+esi*4+SCOPETABLE_V4_entries+SCOPETABLE_previousTryLevel]
    mov [eax+FRAME_trylevel], ecx

    cmp dword ptr [ebx+esi*4+SCOPETABLE_V4_entries+SCOPETABLE_lpfnFilter], 0
    jnz unwind4_loop

    lea ebp, [eax+FRAME_ebp]
    call dword ptr [ebx+esi*4+SCOPETABLE_V4_entries+SCOPETABLE_lpfnHandler]
    jmp unwind4_loop

unwind4_return:
    pop dword ptr fs:[0]
    add esp, 4
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret


__except_handler4:

    push ebp
    mov ebp, esp
    sub esp, 8
    push ebx
    push esi
    push edi
    push ebp

    cld

    mov ebx, [ebp+PARAM_RegistrationFrame]
    mov eax, [ebp+PARAM_ExceptionRecord]

    test dword ptr [eax+EXCEPTION_RECORD_EXCEPTION_FLAGS], EXCEPTION_EXIT_UNWIND + EXCEPTION_UNWINDING
    jnz except_unwind4

    mov [ebp-8+EXCEPTION_POINTERS_EXCEPTION_RECORD], eax
    mov eax, [ebp+PARAM_Context]
    mov [ebp-8+EXCEPTION_POINTERS_CONTEXT_RECORD], eax
    lea eax, [ebp-8]
    mov [ebx+FRAME_xpointers], eax

    mov esi, [ebx+FRAME_trylevel]
    mov edi, [ebx+FRAME_scopetable]
    xor edi, dword ptr [___security_cookie]

except_loop4:
    cmp esi, -2
    je except_search4

    lea ecx, [esi+esi*2]
    mov eax, [edi+ecx*4+SCOPETABLE_V4_entries+SCOPETABLE_lpfnFilter]
    test eax, eax
    jz except_continue4

    push esi
    push ebp
    lea ebp, [ebx+FRAME_ebp]
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    call eax
    pop ebp
    pop esi

    mov ebx, [ebp+PARAM_RegistrationFrame]
    test eax, eax
    jz except_continue4
    js except_dismiss4

    push esi
    push ebp
    push 0
    push dword ptr [ebp+PARAM_ExceptionRecord]
    push 0
    push ebx
    call _RtlUnwind@16
    pop ebp
    pop esi
    mov ebx, [ebp+PARAM_RegistrationFrame]

    push esi
    push ebx
    PUSH_OFFSET ___security_cookie
    call __except_handler4_local_unwind
    add esp, 12

    mov edi, [ebx+FRAME_scopetable]
    xor edi, dword ptr [___security_cookie]
    lea ecx, [esi+esi*2]
    mov eax, [edi+ecx*4+SCOPETABLE_V4_entries+SCOPETABLE_previousTryLevel]
    mov [ebx+FRAME_trylevel], eax

    mov eax, [edi+ecx*4+SCOPETABLE_V4_entries+SCOPETABLE_lpfnHandler]
    lea ebp, [ebx+FRAME_ebp]
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    call eax

except_continue4:
    mov edi, [ebx+FRAME_scopetable]
    xor edi, dword ptr [___security_cookie]
    lea ecx, [esi+esi*2]
    mov esi, [edi+ecx*4+SCOPETABLE_V4_entries+SCOPETABLE_previousTryLevel]
    jmp except_loop4

except_dismiss4:
    mov eax, ExceptionContinueExecution
    jmp except_return4

except_unwind4:
    push -2
    push ebx
    PUSH_OFFSET ___security_cookie
    call __except_handler4_local_unwind
    add esp, 12

except_search4:
    mov eax, ExceptionContinueSearch

except_return4:
    pop ebp
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret

END
