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
static PEMULATION_NOTIFY_MEMORY_FREE LdrpEmulationNotifyFree;
static PEMULATION_NOTIFY_MEMORY_PROTECT LdrpEmulationNotifyProtect;
static PEMULATION_NOTIFY_UNMAP_VIEW LdrpEmulationNotifyUnmap;
static PEMULATION_FLUSH_INSTRUCTION_CACHE LdrpEmulationFlush;
static PEMULATION_UPDATE_PROCESSOR_INFORMATION LdrpEmulationUpdateProcessor;
static PEMULATION_PROCESS_TERM LdrpEmulationProcessTerm;

typedef struct _LDRP_X64_SERVICE
{
    ULONG Native;
    ULONG Raw;
    ULONG Stub;
} LDRP_X64_SERVICE, *PLDRP_X64_SERVICE;

static PLDRP_X64_SERVICE LdrpX64Services;
static ULONG LdrpX64ServiceCount;

static
PIMAGE_SECTION_HEADER
LdrpFindSection(
    _In_ PVOID ImageBase,
    _In_ PCSTR Name)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(ImageBase);
    PIMAGE_SECTION_HEADER Section;
    ULONG Index;

    if (!Headers) return NULL;
    Section = IMAGE_FIRST_SECTION(Headers);
    for (Index = 0; Index < Headers->FileHeader.NumberOfSections; Index++, Section++)
    {
        if (!strncmp((PCSTR)Section->Name, Name, IMAGE_SIZEOF_SHORT_NAME)) return Section;
    }
    return NULL;
}

PCWSTR
LdrpArchGetEmulationHost(
    _In_ USHORT Machine)
{
    PIMAGE_SECTION_HEADER Section;

    if (Machine != IMAGE_FILE_MACHINE_AMD64) return NULL;
    if (LdrpFindSection(&__ImageBase, ".rv64x"))
    {
        Section = LdrpFindSection(&__ImageBase, ".x64svm");
        if (Section)
        {
            LdrpX64Services = (PLDRP_X64_SERVICE)((PUCHAR)&__ImageBase + Section->VirtualAddress);
            LdrpX64ServiceCount = Section->Misc.VirtualSize / sizeof(LDRP_X64_SERVICE);
        }
        LdrpRv64xLoader = TRUE;
        return NULL;
    }
    return EMULATION_HOST_IMAGE_AMD64;
}

VOID
LdrpArchSnapExport(
    _In_ PVOID ExportBase,
    _In_opt_ PVOID ImportBase,
    _Inout_ PULONG_PTR Function)
{
    PIMAGE_NT_HEADERS Headers;
    BOOLEAN X64;
    ULONG_PTR Rva;
    ULONG Index;

    /* TODO: native callers keep the native service, so a patched x64 stub only intercepts x64 callers */
    if (!LdrpX64ServiceCount || ExportBase != &__ImageBase) return;
    Headers = ImportBase ? RtlImageNtHeader(ImportBase) : NULL;
    X64 = Headers && Headers->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;

    Rva = *Function - (ULONG_PTR)&__ImageBase;
    for (Index = 0; Index < LdrpX64ServiceCount; Index++)
    {
        if (X64 && (LdrpX64Services[Index].Native == Rva || LdrpX64Services[Index].Raw == Rva))
            *Function = (ULONG_PTR)&__ImageBase + LdrpX64Services[Index].Stub;
        else if (!X64 && LdrpX64Services[Index].Stub == Rva)
            *Function = (ULONG_PTR)&__ImageBase + LdrpX64Services[Index].Native;
        else
            continue;
        return;
    }
}

static
NTSTATUS
LdrpExposeX64Services(VOID)
{
    PIMAGE_EXPORT_DIRECTORY Exports;
    PULONG Functions, Names;
    PUSHORT Ordinals;
    PCSTR Name;
    PVOID Base;
    SIZE_T Size;
    ULONG ExportSize, Index, Service, OldProtect, IgnoredProtect;
    NTSTATUS Status;

    Exports = RtlImageDirectoryEntryToData(&__ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &ExportSize);
    if (!Exports) return STATUS_INVALID_IMAGE_FORMAT;
    Functions = (PULONG)((PUCHAR)&__ImageBase + Exports->AddressOfFunctions);
    Names = (PULONG)((PUCHAR)&__ImageBase + Exports->AddressOfNames);
    Ordinals = (PUSHORT)((PUCHAR)&__ImageBase + Exports->AddressOfNameOrdinals);

    Base = Functions;
    Size = Exports->NumberOfFunctions * sizeof(ULONG);
    Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, PAGE_READWRITE, &OldProtect);
    if (!NT_SUCCESS(Status)) return Status;
    for (Index = 0; Index < Exports->NumberOfNames; Index++)
    {
        Name = (PCSTR)&__ImageBase + Names[Index];
        if (Name[0] != 'N' || Name[1] != 't') continue;
        for (Service = 0; Service < LdrpX64ServiceCount; Service++)
        {
            if (Functions[Ordinals[Index]] != LdrpX64Services[Service].Native) continue;
            Functions[Ordinals[Index]] = LdrpX64Services[Service].Stub;
            break;
        }
    }
    return ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, OldProtect, &IgnoredProtect);
}

static
BOOLEAN
LdrpCaptureRange(
    _In_ PVOID *BaseAddress,
    _In_ PSIZE_T RegionSize,
    _Out_ PVOID *Base,
    _Out_ PSIZE_T Size)
{
    BOOLEAN Captured = TRUE;

    _SEH2_TRY
    {
        *Base = *BaseAddress;
        *Size = *RegionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Captured = FALSE;
    }
    _SEH2_END;
    return Captured;
}

/* TODO: notify the emulation host of other processes, as the CHPE cross-process work list does */
static
NTSTATUS
NTAPI
LdrpEmulatedFreeVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG FreeType)
{
    PVOID Base;
    SIZE_T Size;
    NTSTATUS Status;

    if (!RtlIsCurrentProcess(ProcessHandle) || !LdrpCaptureRange(BaseAddress, RegionSize, &Base, &Size))
        return ZwFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);

    LdrpEmulationNotifyFree(Base, Size, FreeType, FALSE, STATUS_SUCCESS);
    Status = ZwFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);
    LdrpCaptureRange(BaseAddress, RegionSize, &Base, &Size);
    LdrpEmulationNotifyFree(Base, Size, FreeType, TRUE, Status);
    return Status;
}

static
NTSTATUS
NTAPI
LdrpEmulatedProtectVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG NewProtect,
    _Out_ PULONG OldProtect)
{
    PVOID Base;
    SIZE_T Size;
    NTSTATUS Status;

    if (!RtlIsCurrentProcess(ProcessHandle) || !LdrpCaptureRange(BaseAddress, RegionSize, &Base, &Size))
        return ZwProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize, NewProtect, OldProtect);

    LdrpEmulationNotifyProtect(Base, Size, NewProtect, FALSE, STATUS_SUCCESS);
    Status = ZwProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize, NewProtect, OldProtect);
    LdrpCaptureRange(BaseAddress, RegionSize, &Base, &Size);
    LdrpEmulationNotifyProtect(Base, Size, NewProtect, TRUE, Status);
    return Status;
}

static
NTSTATUS
NTAPI
LdrpEmulatedUnmapViewOfSection(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress)
{
    NTSTATUS Status;

    if (!RtlIsCurrentProcess(ProcessHandle)) return ZwUnmapViewOfSection(ProcessHandle, BaseAddress);

    LdrpEmulationNotifyUnmap(BaseAddress, FALSE, STATUS_SUCCESS);
    Status = ZwUnmapViewOfSection(ProcessHandle, BaseAddress);
    LdrpEmulationNotifyUnmap(BaseAddress, TRUE, Status);
    return Status;
}

static
NTSTATUS
NTAPI
LdrpEmulatedTerminateProcess(
    _In_opt_ HANDLE ProcessHandle,
    _In_ NTSTATUS ExitStatus)
{
    NTSTATUS Status;

    if (ProcessHandle) return ZwTerminateProcess(ProcessHandle, ExitStatus);

    LdrpEmulationProcessTerm(ProcessHandle, FALSE, STATUS_SUCCESS);
    Status = ZwTerminateProcess(ProcessHandle, ExitStatus);
    LdrpEmulationProcessTerm(ProcessHandle, TRUE, Status);
    return Status;
}

static
NTSTATUS
NTAPI
LdrpEmulatedFlushInstructionCache(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytesToFlush)
{
    NTSTATUS Status;

    Status = ZwFlushInstructionCache(ProcessHandle, BaseAddress, NumberOfBytesToFlush);
    if (NT_SUCCESS(Status) && RtlIsCurrentProcess(ProcessHandle))
        LdrpEmulationFlush(BaseAddress, NumberOfBytesToFlush);
    return Status;
}

static
NTSTATUS
NTAPI
LdrpEmulatedQuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
    _Out_writes_bytes_to_opt_(SystemInformationLength, *ReturnLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    NTSTATUS Status;

    Status = ZwQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
    if (NT_SUCCESS(Status) && SystemInformationClass == SystemProcessorInformation)
        LdrpEmulationUpdateProcessor(SystemInformation);
    return Status;
}

static
NTSTATUS
LdrpPatchServiceEntry(
    _In_ PVOID Entry,
    _In_ PVOID Wrapper)
{
    struct
    {
        ULONG Code[4];
        ULONG_PTR Target;
    } Patch = {{0x00000297, 0x0102B283, 0x00028067, 0x00000013}, (ULONG_PTR)Wrapper}; /* auipc t0, 0; ld t0, 16(t0); jr t0; nop; address */
    PVOID Base = Entry;
    SIZE_T Size = sizeof(Patch);
    ULONG OldProtect, IgnoredProtect;
    NTSTATUS Status, FlushStatus;

    C_ASSERT(sizeof(Patch) == 24);
    Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, PAGE_EXECUTE_READWRITE, &OldProtect);
    if (!NT_SUCCESS(Status)) return Status;
    RtlCopyMemory(Entry, &Patch, sizeof(Patch));
    FlushStatus = ZwFlushInstructionCache(NtCurrentProcess(), Entry, sizeof(Patch));
    Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, OldProtect, &IgnoredProtect);
    return NT_SUCCESS(Status) ? FlushStatus : Status;
}

static
NTSTATUS
LdrpInstallServiceWrappers(VOID)
{
    static const struct
    {
        PVOID Entry;
        PVOID Wrapper;
    } Wrappers[] =
    {
        { (PVOID)NtFreeVirtualMemory, (PVOID)LdrpEmulatedFreeVirtualMemory },
        { (PVOID)NtProtectVirtualMemory, (PVOID)LdrpEmulatedProtectVirtualMemory },
        { (PVOID)NtUnmapViewOfSection, (PVOID)LdrpEmulatedUnmapViewOfSection },
        { (PVOID)NtFlushInstructionCache, (PVOID)LdrpEmulatedFlushInstructionCache },
        { (PVOID)NtQuerySystemInformation, (PVOID)LdrpEmulatedQuerySystemInformation },
        { (PVOID)NtTerminateProcess, (PVOID)LdrpEmulatedTerminateProcess },
    };
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(Wrappers); Index++)
    {
        Status = LdrpPatchServiceEntry(Wrappers[Index].Entry, Wrappers[Index].Wrapper);
        if (!NT_SUCCESS(Status)) return Status;
    }
    return STATUS_SUCCESS;
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
        { EMULATION_NOTIFY_MEMORY_FREE_EXPORT, (PVOID *)&LdrpEmulationNotifyFree },
        { EMULATION_NOTIFY_MEMORY_PROTECT_EXPORT, (PVOID *)&LdrpEmulationNotifyProtect },
        { EMULATION_NOTIFY_UNMAP_VIEW_EXPORT, (PVOID *)&LdrpEmulationNotifyUnmap },
        { EMULATION_FLUSH_INSTRUCTION_CACHE_EXPORT, (PVOID *)&LdrpEmulationFlush },
        { EMULATION_UPDATE_PROCESSOR_INFORMATION_EXPORT, (PVOID *)&LdrpEmulationUpdateProcessor },
        { EMULATION_PROCESS_TERM_EXPORT, (PVOID *)&LdrpEmulationProcessTerm },
    };
    UNICODE_STRING Library = RTL_CONSTANT_STRING(EMULATION_LIBRARY_AMD64);
    ANSI_STRING Export;
    PVOID Base;
    ULONG Index;
    NTSTATUS Status;

    if (!LdrpRv64xLoader) return STATUS_SUCCESS;
    Status = LdrpExposeX64Services();
    if (NT_SUCCESS(Status)) Status = LdrLoadDll(NULL, NULL, &Library, &Base);
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
    if (NT_SUCCESS(Status)) Status = LdrpInstallServiceWrappers();
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
