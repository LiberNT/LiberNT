/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Support routines required by the boot RTL and registry libraries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <ndk/mmtypes.h>

PVOID MmHighestUserAddress = (PVOID)MI_HIGHEST_USER_ADDRESS;

PVOID (*BlHeapAllocator)(SIZE_T Size);

PVOID
BlAllocate(
    _In_ SIZE_T Size)
{
    return BlHeapAllocator ? BlHeapAllocator(Size) : FwAllocate(Size);
}

VOID
BlFree(
    _In_ PVOID Memory)
{
    if (!BlHeapAllocator)
        FwFree(Memory);
}

PVOID
NTAPI
CmpAllocate(
    IN SIZE_T Size,
    IN BOOLEAN Paged,
    IN ULONG Tag)
{
    UNREFERENCED_PARAMETER(Paged);
    UNREFERENCED_PARAMETER(Tag);
    return BlAllocate(Size);
}

VOID
NTAPI
CmpFree(
    IN PVOID Ptr,
    IN ULONG Quota)
{
    UNREFERENCED_PARAMETER(Quota);
    BlFree(Ptr);
}

BOOLEAN
NTAPI
CmpFileFlush(
    IN PHHIVE RegistryHive,
    IN ULONG FileType,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length)
{
    UNREFERENCED_PARAMETER(RegistryHive);
    UNREFERENCED_PARAMETER(FileType);
    UNREFERENCED_PARAMETER(FileOffset);
    UNREFERENCED_PARAMETER(Length);
    return FALSE;
}

PVOID
NTAPI
RtlpAllocateMemory(
    SIZE_T Bytes,
    ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    return BlAllocate(Bytes);
}

VOID
NTAPI
RtlpFreeMemory(
    PVOID Mem,
    ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    BlFree(Mem);
}

NTSTATUS
NTAPI
RtlpSafeCopyMemory(
    _Out_writes_bytes_all_(Length) VOID UNALIGNED *Destination,
    _In_reads_bytes_(Length) CONST VOID UNALIGNED *Source,
    _In_ SIZE_T Length)
{
    RtlCopyMemory(Destination, Source, Length);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlpImageNtHeaderEx(
    _In_ ULONG Flags,
    _In_ PVOID Base,
    _In_ ULONG64 Size,
    _Out_ PIMAGE_NT_HEADERS *OutHeaders);

NTSTATUS
NTAPI
RtlImageNtHeaderEx(
    _In_ ULONG Flags,
    _In_ PVOID Base,
    _In_ ULONG64 Size,
    _Out_ PIMAGE_NT_HEADERS *OutHeaders)
{
    return RtlpImageNtHeaderEx(Flags, Base, Size, OutHeaders);
}

#if DBG
VOID
FASTCALL
CHECK_PAGED_CODE_RTL(
    char *file,
    int line)
{
    UNREFERENCED_PARAMETER(file);
    UNREFERENCED_PARAMETER(line);
}
#endif

ULONG
__cdecl
DbgPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Text[512];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);

    FwDebugPrint(Text);
    return 0;
}

VOID
NTAPI
RtlAssert(
    _In_ PVOID FailedAssertion,
    _In_ PVOID FileName,
    _In_ ULONG LineNumber,
    _In_opt_ PSTR Message)
{
    FwPrint(L"Assertion '%hs' failed at %hs line %lu: %hs\r\n",
            (PCSTR)FailedAssertion,
            (PCSTR)FileName,
            LineNumber,
            Message ? Message : "");
    FwHalt();
}

DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheckEx(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4)
{
    FwPrint(L"*** STOP: 0x%08lX (0x%p,0x%p,0x%p,0x%p)\r\n",
            BugCheckCode,
            (PVOID)BugCheckParameter1,
            (PVOID)BugCheckParameter2,
            (PVOID)BugCheckParameter3,
            (PVOID)BugCheckParameter4);
    FwHalt();
}

VOID
UiMessageBoxCritical(
    _In_ PCSTR MessageText)
{
    FwPrint(L"%hs\r\n", MessageText);
}
