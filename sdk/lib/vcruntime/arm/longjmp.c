/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     longjmp on ARM
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <setjmp.h>
#include <stddef.h>
#include <windef.h>

C_ASSERT(offsetof(_JUMP_BUFFER, Frame) == 0);
C_ASSERT(offsetof(_JUMP_BUFFER, R4) == 4);
C_ASSERT(offsetof(_JUMP_BUFFER, Sp) == 36);
C_ASSERT(offsetof(_JUMP_BUFFER, Pc) == 40);
C_ASSERT(offsetof(_JUMP_BUFFER, Fpscr) == 44);
C_ASSERT(offsetof(_JUMP_BUFFER, D) == 48);

__declspec(noreturn)
void
__cdecl
_JumpBufferResume(
    _In_ const _JUMP_BUFFER *Buffer,
    _In_ int Value);

__declspec(noreturn)
void
__cdecl
longjmp(
    _In_reads_(_JBLEN) jmp_buf _Buf,
    _In_ int _Value)
{
    const _JUMP_BUFFER *Buffer = (const _JUMP_BUFFER *)_Buf;
    EXCEPTION_RECORD Record;

    if (_Value == 0)
        _Value = 1;

    /* A buffer filled with a frame leaves through the unwinder, which runs the termination handlers on the way. */
    if (Buffer->Frame != 0)
    {
        Record.ExceptionCode = STATUS_LONGJUMP;
        Record.ExceptionFlags = 0;
        Record.ExceptionRecord = NULL;
        Record.ExceptionAddress = NULL;
        Record.NumberParameters = 1;
        Record.ExceptionInformation[0] = (ULONG_PTR)Buffer;

        RtlUnwind((PVOID)Buffer->Frame, (PVOID)Buffer->Pc, &Record, (PVOID)(ULONG_PTR)_Value);
    }

    _JumpBufferResume(Buffer, _Value);
}
