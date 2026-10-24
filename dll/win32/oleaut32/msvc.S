
#ifdef _M_ARM

#include <kxarm.h>

    LEAF_ENTRY call_method
        __assertfail
    LEAF_END call_method

#elif defined(_M_ARM64)

    .text

    .align 4
    .seh_proc call_method
    .global call_method
call_method:
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    sub sp, sp, x1, lsl #3
    cbz x1, 2f
1:  sub x1, x1, #1
    ldr x4, [x2, x1, lsl #3]
    str x4, [sp, x1, lsl #3]
    cbnz x1, 1b
2:  mov x16, x0
    mov x9, x3
    ldp d0, d1, [x9]
    ldp d2, d3, [x9, #0x10]
    ldp d4, d5, [x9, #0x20]
    ldp d6, d7, [x9, #0x30]
    ldp x0, x1, [x9, #0x40]
    ldp x2, x3, [x9, #0x50]
    ldp x4, x5, [x9, #0x60]
    ldp x6, x7, [x9, #0x70]
    ldr x8, [x9, #0x80]
    blr x16
    mov sp, x29
    ldp x29, x30, [sp], #16
    ret
    .seh_endproc

    .align 4
    .seh_proc call_float_method
    .global call_float_method
call_float_method:
    b call_method
    .seh_endproc

    .align 4
    .seh_proc call_double_method
    .global call_double_method
call_double_method:
    b call_method
    .seh_endproc

#elif defined(_M_AMD64)

#include <asm.inc>
.code64

EXTERN __chkstk:PROC

// DWORD_PTR
// call_method(
//     void *func, // rcx
//     int nb_args, // edx
//     const DWORD_PTR *args );
PUBLIC call_method
FUNC call_method
    push rbp
    .PUSHREG rbp
    push rsi
    .PUSHREG rsi
    push rdi
    .PUSHREG rdi
    mov rbp, rsp
    .SETFRAME rbp, 0
    .ENDPROLOG

    // We need at least space for 4 parameters (home space)
    mov rax, 4
    cmp rdx, rax
    cmovg rax, rdx

    // Allocate dynamic stack space (aligned to 16 bytes)
    lea rax, [rax * 8 + 15]
    and rax, HEX(0FFFFFFFFFFFFFFF0)
    call __chkstk
    sub rsp, rax

    // Save  function pointer in rax
    mov rax, rcx

    // Copy parameters to the stack
    mov rcx, rdx
    mov rdi, rsp
    mov rsi, r8
    rep movsq

    // Set up parameter registers for the call
    mov rcx, qword ptr [rsp + 0]
    mov rdx, qword ptr [rsp + 8]
    mov r8, qword ptr [rsp + 16]
    mov r9, qword ptr [rsp + 24]
    movq xmm0, qword ptr [rsp + 0]
    movq xmm1, qword ptr [rsp + 8]
    movq xmm2, qword ptr [rsp + 16]
    movq xmm3, qword ptr [rsp + 24]
    call rax

    lea rsp, [rbp]
    pop rdi
    pop rsi
    pop rbp
    ret
ENDFUNC

PUBLIC call_double_method
call_double_method:
    jmp call_method

#else

#include <asm.inc>
.code32

PUBLIC _call_method
_call_method:
    push ebp
    mov ebp, esp
    push esi
    push edi
    mov edx, dword ptr ds:[ebp + 12]
    mov edi, esp
    shl edx, 2
    jz cm1
    sub edi, edx
    and edi, HEX(0FFFFFFF0)
    mov esp, edi
    mov ecx, dword ptr ds:[ebp + 12]
    mov esi, dword ptr ds:[ebp + 16]
    cld
    rep movsd
cm1:
    call dword ptr ds:[ebp + 8]
    sub edi, esp
    mov ecx,[ebp + 20]
    mov [ecx], edi
    lea esp, [ebp - 8]
    pop edi
    pop esi
    pop ebp
    ret

PUBLIC _call_double_method
_call_double_method:
    jmp _call_method

#endif

#ifndef _M_ARM64
    END
#endif
