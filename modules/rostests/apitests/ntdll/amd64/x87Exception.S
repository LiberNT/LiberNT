/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x87 instruction sequences that raise each x87 floating-point exception
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <asm.inc>

.code64

PUBLIC X87Reset
FUNC X87Reset
    .ENDPROLOG
    fnclex
    fninit
    ret
ENDFUNC

PUBLIC X87InvalidOperation
PUBLIC X87InvalidOperationFault
PUBLIC X87InvalidOperationWait
FUNC X87InvalidOperation
    .ENDPROLOG
    fninit
    fldcw word ptr [rcx]
    fld qword ptr [rdx]
X87InvalidOperationFault:
    fsqrt
X87InvalidOperationWait:
    fwait
    fnstsw word ptr [r8 + 8]
    fninit
    ret
ENDFUNC

PUBLIC X87StackFault
PUBLIC X87StackFaultFault
PUBLIC X87StackFaultWait
FUNC X87StackFault
    .ENDPROLOG
    fninit
    fldcw word ptr [rcx]
    fld1
    fld1
    fld1
    fld1
    fld1
    fld1
    fld1
    fld1
X87StackFaultFault:
    fld1
X87StackFaultWait:
    fwait
    fnstsw word ptr [r8 + 8]
    fninit
    ret
ENDFUNC

PUBLIC X87Denormal
PUBLIC X87DenormalFault
PUBLIC X87DenormalWait
FUNC X87Denormal
    .ENDPROLOG
    fninit
    fldcw word ptr [rcx]
X87DenormalFault:
    fld qword ptr [rdx]
X87DenormalWait:
    fwait
    fnstsw word ptr [r8 + 8]
    fninit
    ret
ENDFUNC

PUBLIC X87ZeroDivide
PUBLIC X87ZeroDivideFault
PUBLIC X87ZeroDivideWait
FUNC X87ZeroDivide
    .ENDPROLOG
    fninit
    fldcw word ptr [rcx]
    fld1
X87ZeroDivideFault:
    fdiv qword ptr [rdx]
X87ZeroDivideWait:
    fwait
    fnstsw word ptr [r8 + 8]
    fninit
    ret
ENDFUNC

PUBLIC X87Overflow
PUBLIC X87OverflowFault
PUBLIC X87OverflowWait
FUNC X87Overflow
    .ENDPROLOG
    fninit
    fldcw word ptr [rcx]
    fld qword ptr [rdx]
    fmul st(0), st(0)
X87OverflowFault:
    fstp qword ptr [r8]
X87OverflowWait:
    fwait
    fnstsw word ptr [r8 + 8]
    fninit
    ret
ENDFUNC

PUBLIC X87Underflow
PUBLIC X87UnderflowFault
PUBLIC X87UnderflowWait
FUNC X87Underflow
    .ENDPROLOG
    fninit
    fldcw word ptr [rcx]
    fld qword ptr [rdx]
    fmul st(0), st(0)
X87UnderflowFault:
    fstp qword ptr [r8]
X87UnderflowWait:
    fwait
    fnstsw word ptr [r8 + 8]
    fninit
    ret
ENDFUNC

PUBLIC X87Precision
PUBLIC X87PrecisionFault
PUBLIC X87PrecisionWait
FUNC X87Precision
    .ENDPROLOG
    fninit
    fldcw word ptr [rcx]
    fld1
X87PrecisionFault:
    fdiv qword ptr [rdx]
X87PrecisionWait:
    fwait
    fnstsw word ptr [r8 + 8]
    fninit
    ret
ENDFUNC

END
