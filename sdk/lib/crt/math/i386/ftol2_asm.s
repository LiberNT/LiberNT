/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           Run-Time Library
 * FILE:              lib/sdk/crt/math/i386/ftol2_asm.s
 * PROGRAMER:
 *
 */

#include <asm.inc>

EXTERN __ftol:PROC
PUBLIC __ftol2

/* FUNCTIONS ***************************************************************/
.code

/*
 * This routine is called by MSVC-generated code to convert from floating point
 * to integer representation. The floating point number to be converted is
 * on the top of the floating point stack.
 */
__ftol2:
    jmp __ftol

END
