/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 exception dispatch and frame-chain walk
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * This image is built with the dummy PSEH (_USE_DUMMY_PSEH, set for
 * loongarch64 in sdk/cmake/clang.cmake): _SEH2_TRY expands to a plain block
 * and no exception registration record is ever linked into the frame chain,
 * because the compiler has no native table-based SEH for this target. There
 * is therefore no handler for RtlDispatchException to find, and inventing one
 * by walking .eh_frame would need the full DWARF CFI unwinder that this port
 * does not have (the RISC-V64 port's RVUW engine is what supplies it there).
 *
 * Kernel-mode faults are still described, reported to a debugger and, when
 * nobody handles them, turned into a bugcheck by KiDispatchException; that
 * path is what ntos/ke/loongarch64/trap.c drives. User-mode faults leave
 * through the user dispatcher built in the same trap handler.
 */

#include <rtl.h>

/*
 * The whole tree is compiled with -fno-omit-frame-pointer, and this port's
 * stack machinery depends on the psABI frame record: [fp-8] holds the
 * caller's $ra and [fp-16] the caller's $fp (see KiLa64PrepareKernelStack in
 * ntos/ke/loongarch64/stack.c). Walking that record gives the return
 * addresses of the callers that still have a frame on this stack.
 */
ULONG
NTAPI
RtlWalkFrameChain(
    _Out_writes_to_(Count, return) PVOID *Callers,
    _In_ ULONG Count,
    _In_ ULONG Flags)
{
    ULONG_PTR FramePointer, Low, High, ReturnAddress, CallerFrame;
    ULONG Captured = 0, Skip = Flags >> 8;

    /* Bit zero requests a kernel-to-user stack transition, which needs the
     * trap frame rather than this stack's records. */
    if (!Callers || !Count || (Flags & 0xff))
        return 0;

    RtlpGetStackLimits(&Low, &High);

    __asm__ __volatile__("move %0, $fp" : "=r"(FramePointer));

    while (Captured < Count)
    {
        if (!FramePointer ||
            (FramePointer < Low + 2 * sizeof(ULONG_PTR)) ||
            (FramePointer > High - sizeof(ULONG_PTR)))
        {
            break;
        }

        ReturnAddress = ((PULONG_PTR)FramePointer)[-1];
        CallerFrame = ((PULONG_PTR)FramePointer)[-2];
        if (ReturnAddress == 0)
            break;

        if (Skip)
        {
            --Skip;
        }
        else
        {
            Callers[Captured++] = (PVOID)ReturnAddress;
        }

        /* The chain only ever moves towards the stack base. */
        if (CallerFrame <= FramePointer)
            break;
        FramePointer = CallerFrame;
    }

    return Captured;
}

/*
 * No frame-registered or table-registered handler exists on this port, so
 * there is nothing to dispatch: report "not handled" and let the caller's
 * debugger and termination paths do their work. See the file comment.
 */
BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ContextRecord);

    return FALSE;
}

/* EOF */
