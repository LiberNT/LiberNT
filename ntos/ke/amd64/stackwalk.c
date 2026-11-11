/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Steps a stack walk back one frame from the unwind data of the image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
BOOLEAN
KiLookupFunctionEntryUnlocked(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PRUNTIME_FUNCTION *FunctionEntry)
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

    *FunctionEntry = NULL;

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
            *FunctionEntry = &Table[Middle];
            break;
        }
    }

    return TRUE;
}

BOOLEAN
NTAPI
KiUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN CallerFrame)
{
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase = 0;
    ULONG64 ControlPc = Context->Rip;
    ULONG64 OldRsp = Context->Rsp;
    ULONG64 EstablisherFrame;
    ULONG64 ReturnAddress;
    PVOID HandlerData;
    UCHAR Probe;

    /* A return address after a call that ends its function already lies in the next one */
    if (!KiLookupFunctionEntryUnlocked(CallerFrame ? ControlPc - 1 : ControlPc, &ImageBase, &FunctionEntry))
    {
        /* A kernel fault outside every image is a call through a bad pointer: its caller's return address is on top */
        if (CallerFrame || ((Context->SegCs & MODE_MASK) != KernelMode))
            return FALSE;
    }

    if (FunctionEntry == NULL)
    {
        if (!KiReadValidMemory(&ReturnAddress, (ULONG_PTR)Context->Rsp, sizeof(ReturnAddress)))
            return FALSE;

        Context->Rip = ReturnAddress;
        Context->Rsp += sizeof(ReturnAddress);
        return (ReturnAddress != 0);
    }

    if (ControlPc - ImageBase >= FunctionEntry->EndAddress)
        ControlPc--;

    /* FIXME: RtlVirtualUnwind reads past the checked stack, code and unwind bytes directly */
    if (!KiReadValidMemory(&Probe, (ULONG_PTR)Context->Rsp, sizeof(Probe)) ||
        !KiReadValidMemory(&Probe, (ULONG_PTR)ControlPc, sizeof(Probe)) ||
        !KiReadValidMemory(&Probe, (ULONG_PTR)(ImageBase + FunctionEntry->UnwindData), sizeof(Probe)))
    {
        return FALSE;
    }

    RtlVirtualUnwind(UNW_FLAG_NHANDLER,
                     ImageBase,
                     ControlPc,
                     FunctionEntry,
                     Context,
                     &HandlerData,
                     &EstablisherFrame,
                     NULL);

    return (Context->Rsp > OldRsp) && (Context->Rip != 0);
}

/* EOF */
