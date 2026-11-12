/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Implementation of longjmp for LoongArch64
 */

#include <setjmp.h>
#include <windef.h>

__declspec(noreturn)
void __longjmp_noframe(const _JUMP_BUFFER* Buffer, int Value);

__declspec(noreturn)
void __cdecl
longjmp(
    _In_reads_(_JBLEN) jmp_buf Buffer,
    _In_ int Value)
{
    /* TODO: unwind the frames between the setjmp and the longjmp once the
       LoongArch64 unwinder exists; the delivery is frame-less until then. */
    __longjmp_noframe((const _JUMP_BUFFER*)Buffer, Value ? Value : 1);
    __builtin_unreachable();
}
