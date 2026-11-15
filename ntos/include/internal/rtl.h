#pragma once

#ifdef _WIN64
VOID
NTAPI
RtlInitializeSListHead(IN PSLIST_HEADER ListHead);
#define InitializeSListHead RtlInitializeSListHead
#endif


NTAPI

PVOID
NTAPI
RtlPcToFileHeader(
    _In_ PVOID PcValue,

NTSTATUS
NTAPI
RtlFindCharInUnicodeString(
    _In_ ULONG Flags,
    _In_ PCUNICODE_STRING SearchString,
    _In_ PCUNICODE_STRING MatchString,
