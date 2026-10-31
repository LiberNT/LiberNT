/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 hooks for processes that run under an emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>
#include <reactos/emulation.h>

extern IMAGE_DOS_HEADER __ImageBase;

static BOOLEAN LdrpRv64xLoader;
static PEMULATION_ATTACH_PROCESS LdrpEmulationAttach;
static PEMULATION_DETACH_THREAD LdrpEmulationDetach;
static PEMULATION_DETACH_THREAD LdrpEmulationDetachThread;

static
BOOLEAN
LdrpIsRv64xImage(
    _In_ PVOID ImageBase)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(ImageBase);
    PIMAGE_SECTION_HEADER Section;
    ULONG Index;

    if (!Headers) return FALSE;
    Section = IMAGE_FIRST_SECTION(Headers);
    for (Index = 0; Index < Headers->FileHeader.NumberOfSections; Index++, Section++)
    {
        if (!memcmp(Section->Name, ".rv64x", sizeof(".rv64x"))) return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
LdrpRv64xLoaderRequested(VOID)
{
    static const WCHAR Switch[] = L"RV64X_SINGLE_LOADER=1";
    PRTL_USER_PROCESS_PARAMETERS Parameters = NtCurrentPeb()->ProcessParameters;
    PCWSTR Entry;

    if (!Parameters || !(Entry = Parameters->Environment)) return FALSE;
    for (; *Entry; Entry += wcslen(Entry) + 1)
    {
        if (!_wcsicmp(Entry, Switch)) return TRUE;
    }
    return FALSE;
}

PCWSTR
LdrpArchGetEmulationHost(
    _In_ USHORT Machine)
{
    if (Machine != IMAGE_FILE_MACHINE_AMD64) return NULL;
    /* TODO: drop the opt-in once x64 exceptions cross native frames in the single loader */
    if (LdrpIsRv64xImage(&__ImageBase) && LdrpRv64xLoaderRequested())
    {
        LdrpRv64xLoader = TRUE;
        return NULL;
    }
    return EMULATION_HOST_IMAGE_AMD64;
}

NTSTATUS
LdrpArchLoadEmulation(VOID)
{
    static const struct
    {
        PCSTR Name;
        PVOID *Routine;
    } Exports[] =
    {
        { EMULATION_ATTACH_EXPORT, (PVOID *)&LdrpEmulationAttach },
        { EMULATION_DETACH_THREAD_EXPORT, (PVOID *)&LdrpEmulationDetach },
    };
    UNICODE_STRING Library = RTL_CONSTANT_STRING(EMULATION_LIBRARY_AMD64);
    ANSI_STRING Export;
    PVOID Base;
    ULONG Index;
    NTSTATUS Status;

    if (!LdrpRv64xLoader) return STATUS_SUCCESS;
    Status = LdrLoadDll(NULL, NULL, &Library, &Base);
    for (Index = 0; NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Exports); Index++)
    {
        RtlInitAnsiString(&Export, Exports[Index].Name);
        Status = LdrGetProcedureAddress(Base, &Export, 0, Exports[Index].Routine);
    }
    return Status;
}

NTSTATUS
LdrpArchStartEmulation(VOID)
{
    NTSTATUS Status;

    if (!LdrpEmulationAttach) return STATUS_SUCCESS;
    Status = LdrpEmulationAttach();
    if (NT_SUCCESS(Status)) LdrpEmulationDetachThread = LdrpEmulationDetach;
    return Status;
}

VOID
LdrpArchDetachThread(VOID)
{
    if (LdrpEmulationDetachThread) LdrpEmulationDetachThread();
}

VOID
LdrpArchEnterEmulationHost(
    _Inout_ PCONTEXT Context,
    _In_ PTEB HostTeb,
    _In_ PVOID Stack,
    _In_ PVOID Entry,
    _In_ PVOID Argument)
{
    Context->Pc = (ULONG_PTR)Entry;
    Context->Ra = 0;
    Context->Sp = (ULONG_PTR)Stack;
    Context->Tp = (ULONG_PTR)HostTeb;
    Context->A0 = (ULONG_PTR)Argument;
}
