/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Steps a stack walk back one frame from the unwind data of the image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <sdk/lib/rtl/riscv64/unwind.h>
#define NDEBUG
#include <debug.h>

extern UCHAR KiRiscvTrapHandlerCall[], KiRiscvTrapHandlerReturn[];

static
NTSTATUS
NTAPI
KiStackWalkReadMemory(
    _In_opt_ PVOID ReadContext,
    _In_ ULONG64 Address,
    _Out_writes_bytes_all_(Size) PVOID Buffer,
    _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(ReadContext);

    if ((Size > MAXULONG) || !KiReadValidMemory(Buffer, (ULONG_PTR)Address, (ULONG)Size))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

/* The walk runs from a stop with other processors frozen, so it takes no lock and reads only valid memory */
static
BOOLEAN
KiLookupFunctionEntryUnlocked(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PULONG64 ImageSize,
    _Out_ PRUNTIME_FUNCTION FunctionEntry,
    _Out_ PBOOLEAN Found)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    IMAGE_NT_HEADERS64 NtHeaders;
    PIMAGE_DATA_DIRECTORY Directory;
    PRUNTIME_FUNCTION Table;
    RUNTIME_FUNCTION Entry;
    BOOLEAN InKernel;
    PUCHAR Base;
    LONG NtOffset;
    ULONG Low;
    ULONG High;
    ULONG Rva;

    *Found = FALSE;
    *ImageBase = 0;
    *ImageSize = 0;

    Base = KiPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, &LdrEntry, FALSE, &InKernel);
    if (Base == NULL)
        return FALSE;

    if (!KiReadValidMemory(&NtOffset, (ULONG_PTR)(Base + FIELD_OFFSET(IMAGE_DOS_HEADER, e_lfanew)), sizeof(NtOffset)) ||
        !KiReadValidMemory(&NtHeaders, (ULONG_PTR)(Base + NtOffset), sizeof(NtHeaders)) ||
        (NtHeaders.Signature != IMAGE_NT_SIGNATURE) ||
        (NtHeaders.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION))
    {
        return FALSE;
    }

    *ImageBase = (ULONG64)(ULONG_PTR)Base;
    *ImageSize = NtHeaders.OptionalHeader.SizeOfImage;
    ASSERT(ControlPc >= *ImageBase);
    Directory = &NtHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    Table = (PRUNTIME_FUNCTION)(Base + Directory->VirtualAddress);
    Rva = (ULONG)(ControlPc - *ImageBase);
    Low = 0;
    High = Directory->Size / sizeof(RUNTIME_FUNCTION);

    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;

        if (!KiReadValidMemory(&Entry, (ULONG_PTR)&Table[Middle], sizeof(Entry)))
            return FALSE;

        if (Rva < Entry.BeginAddress)
        {
            High = Middle;
        }
        else if (Rva >= Entry.EndAddress)
        {
            Low = Middle + 1;
        }
        else
        {
            *FunctionEntry = Entry;
            *Found = TRUE;
            break;
        }
    }

    return TRUE;
}

/* An interrupt runs on its own stack, so the context it interrupted may be on another stack entirely */
static
BOOLEAN
KiUnwindTrapFrame(
    _Inout_ PCONTEXT Context)
{
    KTRAP_FRAME Frame;

    /* TODO: a trap from user mode ends the walk; the user pc it holds is not shown */
    if ((Context->Sp & 15) ||
        !KiReadValidMemory(&Frame, (ULONG_PTR)Context->Sp, sizeof(Frame)) ||
        KiUserTrap(&Frame) ||
        (Frame.Context.Sp & 15) ||
        (Frame.Context.Sp < (ULONG64)(ULONG_PTR)MmSystemRangeStart) ||
        (Frame.Context.Pc < (ULONG64)(ULONG_PTR)MmSystemRangeStart) ||
        (Frame.Context.Pc & 1))
    {
        return FALSE;
    }

    *Context = Frame.Context;
    Context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
    return TRUE;
}

BOOLEAN
NTAPI
KiUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN CallerFrame)
{
    RTL_RISCV64_UNWIND_RESULT Result;
    RTL_RISCV64_UNWIND_VIEW View;
    RUNTIME_FUNCTION Entry;
    ULONG64 ControlPc = Context->Pc;
    ULONG64 OldSp = Context->Sp;
    ULONG64 ImageBase;
    ULONG64 ImageSize;
    BOOLEAN ReturnAddress;
    BOOLEAN InImage;
    BOOLEAN Found;
    NTSTATUS Status;

    /* The walk crosses only frames of the stack it starts on, which is no larger than a large kernel stack */
    RtlZeroMemory(&View, sizeof(View));
    View.StackLow = OldSp;
    View.StackHigh = (OldSp > MAXULONG64 - KERNEL_LARGE_STACK_SIZE) ? MAXULONG64 : OldSp + KERNEL_LARGE_STACK_SIZE;
    View.ReadMemory = KiStackWalkReadMemory;

    /* A pc a frame was unwound to is a return address, looked up as the call before it, as the dispatcher does */
    ReturnAddress = CallerFrame && ((Context->ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0);
    if (ReturnAddress)
        ControlPc -= 2;

    if ((ControlPc >= (ULONG64)(ULONG_PTR)KiRiscvTrapHandlerCall) &&
        (ControlPc < (ULONG64)(ULONG_PTR)KiRiscvTrapHandlerReturn))
    {
        return KiUnwindTrapFrame(Context);
    }

    InImage = KiLookupFunctionEntryUnlocked(ControlPc, &ImageBase, &ImageSize, &Entry, &Found);

    /* A leaf, or a call through a bad pointer, still holds its return address in ra; a caller never does */
    if (ReturnAddress && (!InImage || !Found))
        return FALSE;

    if (Found)
    {
        View.ImageBase = ImageBase;
        View.ImageSize = ImageSize;
    }

    Status = RtlpRiscv64UnwindFrame(UNW_FLAG_NHANDLER,
                                    ControlPc,
                                    Found ? &Entry : NULL,
                                    &View,
                                    Context,
                                    &Result,
                                    NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;

    ASSERT(Context->Sp >= OldSp);
    return (Context->Pc != 0) && (Context->Pc >= (ULONG64)(ULONG_PTR)MmSystemRangeStart);
}

/* EOF */
