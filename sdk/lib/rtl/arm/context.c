/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) context restoration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>

/* The ARM copy ranges of RtlCopyContext (ntdll xstate_wow64.c) are written against this layout. */
C_ASSERT(CONTEXT_ARM == 0x00200000);
C_ASSERT(FIELD_OFFSET(CONTEXT, R0) == 0x4);
C_ASSERT(FIELD_OFFSET(CONTEXT, Sp) == 0x38);
C_ASSERT(FIELD_OFFSET(CONTEXT, Fpscr) == 0x48);
C_ASSERT(FIELD_OFFSET(CONTEXT, Bvr) == 0x150);
C_ASSERT(FIELD_OFFSET(CONTEXT, Padding2) == 0x198);
C_ASSERT(sizeof(CONTEXT) == 0x1a0);
C_ASSERT(TYPE_ALIGNMENT(CONTEXT) == 8);

typedef PVOID (CALLBACK *PRTL_CONSOLIDATE_CALLBACK)(
    _In_ PEXCEPTION_RECORD ExceptionRecord);

DECLSPEC_NORETURN
VOID
NTAPI
RtlpRestoreContextInternal(
    _In_ PCONTEXT ContextRecord);

/* The ARM jump buffer, as <setjmp.h> lays out _JUMP_BUFFER for _M_ARM. */
typedef struct _ARM_JUMP_BUFFER
{
    ULONG Frame;
    ULONG R4;
    ULONG R5;
    ULONG R6;
    ULONG R7;
    ULONG R8;
    ULONG R9;
    ULONG R10;
    ULONG R11;
    ULONG Sp;
    ULONG Pc;
    ULONG Fpscr;
    ULONGLONG D[8];
} ARM_JUMP_BUFFER, *PARM_JUMP_BUFFER;

VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    if (ExceptionRecord && (ExceptionRecord->ExceptionCode == STATUS_LONGJUMP) &&
        (ExceptionRecord->NumberParameters >= 1))
    {
        PARM_JUMP_BUFFER JumpBuffer = (PARM_JUMP_BUFFER)ExceptionRecord->ExceptionInformation[0];

        RtlCopyMemory(&ContextRecord->R4, &JumpBuffer->R4, 8 * sizeof(ULONG));
        ContextRecord->Sp = JumpBuffer->Sp;
        ContextRecord->Pc = JumpBuffer->Pc;
        ContextRecord->Fpscr = JumpBuffer->Fpscr;
        RtlCopyMemory(&ContextRecord->D[8], JumpBuffer->D, sizeof(JumpBuffer->D));
    }

    if (ExceptionRecord && (ExceptionRecord->ExceptionCode == STATUS_UNWIND_CONSOLIDATE) &&
        (ExceptionRecord->NumberParameters >= 1))
    {
        PRTL_CONSOLIDATE_CALLBACK Consolidate = (PRTL_CONSOLIDATE_CALLBACK)ExceptionRecord->ExceptionInformation[0];

        /* FIXME: the callback runs on the dispatching stack, not on a frame the context describes */
        ContextRecord->Pc = (ULONG)(ULONG_PTR)Consolidate(ExceptionRecord);
    }

    RtlpRestoreContextInternal(ContextRecord);
}
