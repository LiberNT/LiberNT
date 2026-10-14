/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* RISC-V64 native context restoration. */
#include <rtl.h>

DECLSPEC_NORETURN VOID NTAPI RtlpRiscv64RestoreContext(PCONTEXT Context);

typedef struct _RISCV64_JUMP_BUFFER
{
    ULONG64 Frame;
    ULONG64 Ra;
    ULONG64 Sp;
    ULONG64 S[12];
    ULONG64 Reserved;
} RISCV64_JUMP_BUFFER;

VOID NTAPI
RtlRestoreContext(PCONTEXT ContextRecord, PEXCEPTION_RECORD ExceptionRecord)
{
    CONTEXT Context;
    EXCEPTION_RECORD Record;
    ULONG_PTR Low, High;
    ULONG Required = CONTEXT_CONTROL | CONTEXT_INTEGER;
    ULONG Allowed = Required | CONTEXT_FLOATING_POINT | CONTEXT_UNWOUND_TO_CALL;

    if (!ContextRecord ||
        !NT_SUCCESS(RtlpRiscv64ReadMemory(&Context, ContextRecord, sizeof(Context))))
        RtlpRiscv64RaiseFatal(STATUS_INVALID_PARAMETER);

    if (ExceptionRecord &&
        !NT_SUCCESS(RtlpRiscv64ReadMemory(&Record, ExceptionRecord, sizeof(Record))))
        RtlpRiscv64RaiseFatal(STATUS_INVALID_PARAMETER);

    if (ExceptionRecord && Record.ExceptionCode == STATUS_LONGJUMP)
    {
        RISCV64_JUMP_BUFFER JumpBuffer;
        ULONG Index;

        if (Record.NumberParameters < 1 ||
            !NT_SUCCESS(RtlpRiscv64ReadMemory(&JumpBuffer, (PVOID)Record.ExceptionInformation[0],
                                              sizeof(JumpBuffer))))
            RtlpRiscv64RaiseFatal(STATUS_INVALID_PARAMETER);

        Context.Pc = JumpBuffer.Ra;
        Context.Ra = JumpBuffer.Ra;
        Context.Sp = JumpBuffer.Sp;
        Context.S0 = JumpBuffer.S[0];
        Context.S1 = JumpBuffer.S[1];
        for (Index = 2; Index < RTL_NUMBER_OF(JumpBuffer.S); Index++)
            Context.X[16 + Index] = JumpBuffer.S[Index];
    }

    RtlpGetStackLimits(&Low, &High);
    if ((Context.ContextFlags & Required) != Required ||
        (Context.ContextFlags & ~Allowed) ||
        !Context.Pc || (Context.Pc & 1) ||
        (LONG64)Context.Pc >> 38 != ((Context.Pc >> 38) & 1 ? -1 : 0) ||
        (LONG64)Context.Sp >> 38 != ((Context.Sp >> 38) & 1 ? -1 : 0) ||
        (Context.Sp & 15) || Context.Sp < Low || Context.Sp > High ||
        (ExceptionRecord && Record.ExceptionCode == STATUS_UNWIND_CONSOLIDATE))
        RtlpRiscv64RaiseFatal(STATUS_INVALID_PARAMETER);

    RtlpRiscv64PrepareContextRestore(&Context);
    RtlpRiscv64RestoreContext(&Context);
}
