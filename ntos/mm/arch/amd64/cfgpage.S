/* SPDX-License-Identifier: GPL-3.0-only */
#include <asm.inc>

/* ML sizes forward branches before applying the fixed entry point offsets. */
#ifdef _USE_ML
#define CFG_NEAR near ptr
#else
#define CFG_NEAR
#endif

.code64
PUBLIC MiAmd64CfgTemplate
PUBLIC MiAmd64CfgBitmapAddress
PUBLIC MiAmd64CfgExportSuppression
PUBLIC MiAmd64CfgTemplateEnd
PUBLIC MiAmd64CfgDispatchBound

MiAmd64CfgTemplate:
    .long CfgDispatch - MiAmd64CfgTemplate
    .long CfgDispatchES - MiAmd64CfgTemplate
    .long CfgCheck - MiAmd64CfgTemplate
    .long CfgCheckES - MiAmd64CfgTemplate
    .long CfgInvalid - MiAmd64CfgTemplate
    .long CfgFunction - MiAmd64CfgTemplate

MACRO(CFG_VALIDATE, target, accepted, noAlignedBit)
    test r10, r10
    jz VAL(accepted)
    mov r11, VAL(target)
    shr r11, 47
    jnz CFG_NEAR CfgInvalid
    mov r11, VAL(target)
    shr r11, 6
    movzx r10d, byte ptr [r10 + r11]
    mov r11, VAL(target)
    shr r11, 3
    and r11d, 6
    bt r10d, r11d
    jnc CFG_NEAR VAL(noAlignedBit)
    test VAL(target), 15
    jz VAL(accepted)
    inc r11
    bt r10d, r11d
    jnc CFG_NEAR CfgInvalid
ENDM

MACRO(CFG_VALIDATE_EXPORT, target, accepted)
    test VAL(target), 15
    jnz CFG_NEAR CfgInvalid
    cmp byte ptr MiAmd64CfgExportSuppression[rip], 0
    jne CFG_NEAR CfgInvalid
    inc r11
    bt r10d, r11d
    jc VAL(accepted)
    jmp CFG_NEAR CfgInvalid
ENDM

.org 64
CfgDispatch:
    mov r10, qword ptr MiAmd64CfgBitmapAddress[rip]
MiAmd64CfgDispatchBound:
    CFG_VALIDATE rax, CfgDispatchValid, CfgDispatchExport
CfgDispatchValid:
    jmp rax
CfgDispatchExport:
    CFG_VALIDATE_EXPORT rax, CfgDispatchValid

.org 192
CfgDispatchES:
    mov r10, qword ptr MiAmd64CfgBitmapAddress[rip]
    CFG_VALIDATE rax, CfgDispatchESValid, CfgInvalid
CfgDispatchESValid:
    jmp rax

.org 320
CfgCheck:
    mov r10, qword ptr MiAmd64CfgBitmapAddress[rip]
    CFG_VALIDATE rcx, CfgCheckValid, CfgCheckExport
CfgCheckValid:
    ret
CfgCheckExport:
    CFG_VALIDATE_EXPORT rcx, CfgCheckValid

.org 448
CfgCheckES:
    mov r10, qword ptr MiAmd64CfgBitmapAddress[rip]
    CFG_VALIDATE rcx, CfgCheckESValid, CfgInvalid
CfgCheckESValid:
    ret

.org 576
CfgInvalid:
    mov ecx, 10
    int HEX(29)
    .byte HEX(0f), HEX(0b) /* ud2 */

.org 640
CfgExceptionHandler:
    cmp dword ptr [rcx], HEX(c0000005)
    je CfgInvalid
    mov eax, 1
    ret

.org 664
CfgUnwind:
    .byte 25, 0, 0, 0
    .long CfgExceptionHandler - MiAmd64CfgTemplate
.org 676
CfgFunction:
    .long 0
    .long CfgExceptionHandler - MiAmd64CfgTemplate
    .long CfgUnwind - MiAmd64CfgTemplate

MiAmd64CfgBitmapAddress:
    .quad 0
MiAmd64CfgExportSuppression:
    .byte 0
MiAmd64CfgTemplateEnd:
END
