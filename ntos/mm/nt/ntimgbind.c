/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Export lookup and import binding of system images
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/nt/ntsysimg.h>
#include <apisets.h>
#define NDEBUG
#include <debug.h>

typedef struct _MI_EXPORT_TABLE
{
    PUCHAR Base;
    PIMAGE_EXPORT_DIRECTORY Directory;
    ULONG DirectoryRva;
    ULONG DirectorySize;
    PULONG Functions;
    PULONG Names;
    PUSHORT NameOrdinals;
} MI_EXPORT_TABLE, *PMI_EXPORT_TABLE;

static const PCSTR MiUncountedImports[] = { "ntoskrnl", "hal", "win32k" };
static const PCSTR MiSessionImports[] = { "win32k", "ntoskrnl", "hal", "watchdog", "dxapi", "coverage", "irt" };
static const PCSTR MiUserModeImports[] = { "ntdll", "winsrv", "advapi32", "kernel32", "user32", "gdi32" };

static const UNICODE_STRING MiKernelApiSets[] =
{
    RTL_CONSTANT_STRING(L"api-ms-win-ntos-"),
    RTL_CONSTANT_STRING(L"ext-ms-win-ntos-"),
    RTL_CONSTANT_STRING(L"api-ms-win-core-win32k-"),
    RTL_CONSTANT_STRING(L"ext-ms-win-core-win32k-"),
};

static
BOOLEAN
MiImportNameIn(
    _In_ PCSTR Name,
    _In_reads_(Count) const PCSTR *Prefixes,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (_strnicmp(Name, Prefixes[Index], strlen(Prefixes[Index])) == 0)
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
MiOpenExportTable(
    _In_ PVOID ImageBase,
    _Out_ PMI_EXPORT_TABLE Table)
{
    Table->Directory = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &Table->DirectorySize);
    if (Table->Directory == NULL)
        return FALSE;

    Table->Base = ImageBase;
    Table->DirectoryRva = (ULONG)((PUCHAR)Table->Directory - Table->Base);
    Table->Functions = (PULONG)(Table->Base + Table->Directory->AddressOfFunctions);
    Table->Names = (PULONG)(Table->Base + Table->Directory->AddressOfNames);
    Table->NameOrdinals = (PUSHORT)(Table->Base + Table->Directory->AddressOfNameOrdinals);
    return TRUE;
}

static
BOOLEAN
MiExportSlotByName(
    _In_ PMI_EXPORT_TABLE Table,
    _In_ PCSTR Name,
    _In_ ULONG Hint,
    _Out_ PULONG Slot)
{
    ULONG Low = 0, High = Table->Directory->NumberOfNames, Middle;
    INT Order;

    if (Hint < High && strcmp(Name, (PCSTR)(Table->Base + Table->Names[Hint])) == 0)
    {
        *Slot = Table->NameOrdinals[Hint];
        return TRUE;
    }

    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        Order = strcmp(Name, (PCSTR)(Table->Base + Table->Names[Middle]));
        if (Order == 0)
        {
            *Slot = Table->NameOrdinals[Middle];
            return TRUE;
        }

        if (Order < 0)
            High = Middle;
        else
            Low = Middle + 1;
    }

    return FALSE;
}

static
PLDR_DATA_TABLE_ENTRY
MiFindForwardTarget(
    _In_reads_(Length) PCSTR Module,
    _In_ ULONG Length)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Link;
    ULONG Index;

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (Entry->BaseDllName.Length / sizeof(WCHAR) < Length)
            continue;

        for (Index = 0; Index < Length; Index++)
        {
            if (RtlUpcaseUnicodeChar(Entry->BaseDllName.Buffer[Index]) != RtlUpcaseUnicodeChar((WCHAR)(UCHAR)Module[Index]))
                break;
        }

        if (Index == Length)
            return Entry;
    }

    return NULL;
}

static
NTSTATUS
MiResolveExport(
    _In_ PMI_EXPORT_TABLE Table,
    _In_opt_ PCSTR Name,
    _In_ ULONG HintOrOrdinal,
    _In_ ULONG Depth,
    _Out_ PULONG_PTR Address)
{
    MI_EXPORT_TABLE Next;
    PLDR_DATA_TABLE_ENTRY Target;
    PCSTR Forward, Dot;
    ULONG Slot, Rva;

    if (Name != NULL)
    {
        if (!MiExportSlotByName(Table, Name, HintOrOrdinal, &Slot))
            return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
    }
    else
    {
        Slot = HintOrOrdinal - Table->Directory->Base;
    }

    if (Slot >= Table->Directory->NumberOfFunctions)
        return STATUS_DRIVER_ORDINAL_NOT_FOUND;

    Rva = Table->Functions[Slot];
    if (Rva == 0)
        return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;

    if (Rva - Table->DirectoryRva >= Table->DirectorySize)
    {
        *Address = (ULONG_PTR)(Table->Base + Rva);
        return STATUS_SUCCESS;
    }

    Forward = (PCSTR)(Table->Base + Rva);
    Dot = strchr(Forward, '.');
    if (Dot == NULL || Depth >= MI_EXPORT_FORWARD_DEPTH)
        return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;

    Target = MiFindForwardTarget(Forward, (ULONG)(Dot - Forward) + 1);
    if (Target == NULL || !MiOpenExportTable(Target->DllBase, &Next))
        return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;

    return MiResolveExport(&Next, Dot + 1, 0, Depth + 1, Address);
}

PVOID
MiFindExport(
    _In_ PVOID ImageBase,
    _In_ PCSTR Name)
{
    MI_EXPORT_TABLE Table;
    ULONG Slot, Rva;

    if (!MiOpenExportTable(ImageBase, &Table) || !MiExportSlotByName(&Table, Name, MAXULONG, &Slot))
        return NULL;

    if (Slot >= Table.Directory->NumberOfFunctions)
        return NULL;

    Rva = Table.Functions[Slot];
    if (Rva == 0 || Rva - Table.DirectoryRva < Table.DirectorySize)
        return NULL;

    return Table.Base + Rva;
}

PVOID
NTAPI
RtlFindExportedRoutineByName(
    _In_ PVOID ImageBase,
    _In_ PCSTR ExportName)
{
    PAGED_CODE();
    return MiFindExport(ImageBase, ExportName);
}

NTSTATUS
NTAPI
MmCallDllInitialize(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PLIST_ENTRY ModuleListHead)
{
    static const UNICODE_STRING Services = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\");
    PMM_DLL_INITIALIZE Initialize;
    UNICODE_STRING ServiceName, KeyPath;
    USHORT Index;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ModuleListHead);

    Initialize = (PMM_DLL_INITIALIZE)MiFindExport(LdrEntry->DllBase, "DllInitialize");
    if (Initialize == NULL)
        return STATUS_SUCCESS;

    ServiceName = LdrEntry->BaseDllName;
    for (Index = 0; Index < ServiceName.Length / sizeof(WCHAR); Index++)
    {
        if (ServiceName.Buffer[Index] == L'.')
        {
            ServiceName.Length = Index * sizeof(WCHAR);
            break;
        }
    }

    KeyPath.Length = 0;
    KeyPath.MaximumLength = Services.Length + ServiceName.Length + sizeof(UNICODE_NULL);
    KeyPath.Buffer = ExAllocatePoolWithTag(PagedPool, KeyPath.MaximumLength, TAG_LDR_WSTR);
    if (KeyPath.Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlAppendUnicodeStringToString(&KeyPath, &Services);
    RtlAppendUnicodeStringToString(&KeyPath, &ServiceName);
    KeyPath.Buffer[KeyPath.Length / sizeof(WCHAR)] = UNICODE_NULL;

    Status = Initialize(&KeyPath);

    ExFreePoolWithTag(KeyPath.Buffer, TAG_LDR_WSTR);
    return Status;
}

static
VOID
MiReleaseModule(
    _In_ PLDR_DATA_TABLE_ENTRY Module)
{
    PMM_DLL_UNLOAD Unload;
    PVOID Imports;

    if (Module->LoadedImports == MM_SYSLDR_BOOT_LOADED)
        return;

    ASSERT(Module->LoadCount != 0);
    if (--Module->LoadCount != 0)
        return;

    Unload = (PMM_DLL_UNLOAD)MiFindExport(Module->DllBase, "DllUnload");
    if (Unload == NULL || !NT_SUCCESS(Unload()))
    {
        Module->LoadCount = 1;
        return;
    }

    Imports = Module->LoadedImports;
    Module->LoadedImports = MM_SYSLDR_NO_IMPORTS;
    Module->LoadCount = 1;
    MmUnloadSystemImage(Module);

    MiDropImportReferences(Imports);
    MiFreeImportList(Imports);
}

VOID
MiDropImportReferences(
    _In_opt_ PVOID ImportList)
{
    ULONG Index, Count = MiImportListCount(ImportList);

    PAGED_CODE();

    for (Index = 0; Index < Count; Index++)
        MiReleaseModule(MiImportListEntry(ImportList, Index));
}

static
NTSTATUS
MiCheckImportNames(
    _In_ PVOID ImageBase,
    _In_ PIMAGE_IMPORT_DESCRIPTOR Descriptor,
    _In_ PCUNICODE_STRING ImageName,
    _Out_ PULONG Count)
{
    BOOLEAN SessionLink = FALSE, OtherLink = FALSE;
    PCSTR Name;

    *Count = 0;

    for (; Descriptor->Name != 0 && Descriptor->OriginalFirstThunk != 0; Descriptor++)
    {
        Name = (PCSTR)ImageBase + Descriptor->Name;

        if (MiImportNameIn(Name, MiUserModeImports, RTL_NUMBER_OF(MiUserModeImports)))
        {
            DPRINT1("%wZ imports the user-mode DLL %s\n", ImageName, Name);
            return STATUS_PROCEDURE_NOT_FOUND;
        }

        SessionLink |= (BOOLEAN)(_strnicmp(Name, "win32k", sizeof("win32k") - 1) == 0);
        OtherLink |= (BOOLEAN)!MiImportNameIn(Name, MiSessionImports, RTL_NUMBER_OF(MiSessionImports));
        (*Count)++;
    }

    if (SessionLink && OtherLink)
    {
        DPRINT1("%wZ imports from win32k and from drivers outside session space\n", ImageName);
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
MiLoadDependencyFrom(
    _In_ PCUNICODE_STRING Directory,
    _In_opt_ PCUNICODE_STRING SubDirectory,
    _In_ PCUNICODE_STRING ModuleName,
    _Out_ PLDR_DATA_TABLE_ENTRY *Module)
{
    UNICODE_STRING Path;
    ULONG Bytes;
    PVOID Base;
    NTSTATUS Status;

    Bytes = Directory->Length + (SubDirectory ? SubDirectory->Length : 0) + ModuleName->Length + sizeof(UNICODE_NULL);
    if (Bytes > UNICODE_STRING_MAX_BYTES)
        return STATUS_NAME_TOO_LONG;

    Path.Length = 0;
    Path.MaximumLength = (USHORT)Bytes;
    Path.Buffer = ExAllocatePoolWithTag(PagedPool, Bytes, TAG_LDR_WSTR);
    if (Path.Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlAppendUnicodeStringToString(&Path, Directory);
    if (SubDirectory != NULL)
        RtlAppendUnicodeStringToString(&Path, SubDirectory);
    RtlAppendUnicodeStringToString(&Path, ModuleName);

    Status = MmLoadSystemImage(&Path, NULL, NULL, 0, (PVOID *)Module, &Base);
    ASSERT(!NT_SUCCESS(Status) || (*Module)->DllBase == Base);

    ExFreePoolWithTag(Path.Buffer, TAG_LDR_WSTR);
    return Status;
}

static
NTSTATUS
MiLoadDependency(
    _In_ PCUNICODE_STRING ModuleName,
    _In_ PCUNICODE_STRING Directory,
    _Out_ PLDR_DATA_TABLE_ENTRY *Module)
{
    static const UNICODE_STRING Drivers = RTL_CONSTANT_STRING(L"drivers\\");
    static const UNICODE_STRING SystemDrivers = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\drivers\\");
    NTSTATUS Status;

    Status = MiLoadDependencyFrom(Directory, NULL, ModuleName, Module);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        Status = MiLoadDependencyFrom(Directory, &Drivers, ModuleName, Module);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
        Status = MiLoadDependencyFrom(&SystemDrivers, NULL, ModuleName, Module);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MmCallDllInitialize(*Module, &PsLoadedModuleList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DllInitialize of %wZ failed with 0x%lx\n", &(*Module)->BaseDllName, Status);
        MmUnloadSystemImage(*Module);
        *Module = NULL;
    }

    return Status;
}

static
NTSTATUS
MiBindKernelServiceRoutines(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ImageName,
    _In_ PIMAGE_IMPORT_DESCRIPTOR Descriptor)
{
    PIMAGE_THUNK_DATA Lookup = (PIMAGE_THUNK_DATA)((PUCHAR)ImageBase + Descriptor->OriginalFirstThunk);
    PIMAGE_THUNK_DATA Bound = (PIMAGE_THUNK_DATA)((PUCHAR)ImageBase + Descriptor->FirstThunk);
    PIMAGE_IMPORT_BY_NAME ByName;
    PVOID Routine;

    for (; Lookup->u1.AddressOfData != 0; Lookup++, Bound++)
    {
        if (IMAGE_SNAP_BY_ORDINAL(Lookup->u1.Ordinal))
        {
            DPRINT1("%wZ imports a kernel service routine by ordinal\n", ImageName);
            return STATUS_DRIVER_ORDINAL_NOT_FOUND;
        }

        ByName = (PIMAGE_IMPORT_BY_NAME)((PUCHAR)ImageBase + Lookup->u1.AddressOfData);
        Routine = IopResolveKsrApiSetRoutine((PCSTR)ByName->Name);
        if (Routine == NULL)
        {
            DPRINT1("%wZ imports the kernel service routine %s, which is missing\n", ImageName, ByName->Name);
            return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
        }

        Bound->u1.Function = (ULONG_PTR)Routine;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
MiMapKernelApiSet(
    _In_ PCSTR Name,
    _Inout_ PUNICODE_STRING ModuleName,
    _Out_ PBOOLEAN Hostless)
{
    UNICODE_STRING Host;
    BOOLEAN Resolved = FALSE;
    ULONG Index;
    NTSTATUS Status;

    *Hostless = FALSE;

    for (Index = 0; Index < RTL_NUMBER_OF(MiKernelApiSets); Index++)
    {
        if (RtlPrefixUnicodeString(&MiKernelApiSets[Index], ModuleName, TRUE))
            break;
    }

    if (Index == RTL_NUMBER_OF(MiKernelApiSets))
        return STATUS_SUCCESS;

    Status = ApiSetResolveToHost(APISET_WIN10, ModuleName, &Resolved, &Host);
    if (!NT_SUCCESS(Status) || !Resolved)
        return Status;

    if (Host.Length == 0)
    {
        *Hostless = TRUE;
        return STATUS_SUCCESS;
    }

    RtlFreeUnicodeString(ModuleName);
    Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE, &Host, ModuleName);
    DPRINT("Kernel API set %s is hosted by %wZ\n", Name, ModuleName);
    return Status;
}

static
NTSTATUS
MiBindThunks(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ImageName,
    _In_ PLDR_DATA_TABLE_ENTRY Module,
    _In_ PIMAGE_IMPORT_DESCRIPTOR Descriptor)
{
    PIMAGE_THUNK_DATA Lookup = (PIMAGE_THUNK_DATA)((PUCHAR)ImageBase + Descriptor->OriginalFirstThunk);
    PIMAGE_THUNK_DATA Bound = (PIMAGE_THUNK_DATA)((PUCHAR)ImageBase + Descriptor->FirstThunk);
    PIMAGE_IMPORT_BY_NAME ByName;
    MI_EXPORT_TABLE Table;
    ULONG_PTR Address;
    NTSTATUS Status;

    if (!MiOpenExportTable(Module->DllBase, &Table))
    {
        DPRINT1("%wZ imports from %wZ, which exports nothing\n", ImageName, &Module->BaseDllName);
        return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
    }

    for (; Lookup->u1.AddressOfData != 0; Lookup++, Bound++)
    {
        if (IMAGE_SNAP_BY_ORDINAL(Lookup->u1.Ordinal))
        {
            Status = MiResolveExport(&Table, NULL, (ULONG)IMAGE_ORDINAL(Lookup->u1.Ordinal), 0, &Address);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("%wZ imports ordinal %lu from %wZ, which does not export it\n",
                        ImageName, (ULONG)IMAGE_ORDINAL(Lookup->u1.Ordinal), &Module->BaseDllName);
                return Status;
            }
        }
        else
        {
            ByName = (PIMAGE_IMPORT_BY_NAME)((PUCHAR)ImageBase + Lookup->u1.AddressOfData);
            Status = MiResolveExport(&Table, (PCSTR)ByName->Name, ByName->Hint, 0, &Address);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("%wZ imports %s from %wZ, which does not export it\n", ImageName, ByName->Name, &Module->BaseDllName);
                return Status;
            }
        }

        Bound->u1.Function = Address;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
MiBindDescriptor(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ImageName,
    _In_ PCUNICODE_STRING Directory,
    _In_ PIMAGE_IMPORT_DESCRIPTOR Descriptor,
    _Out_ PLDR_DATA_TABLE_ENTRY *Referenced)
{
    PCSTR Name = (PCSTR)ImageBase + Descriptor->Name;
    BOOLEAN Counted = !MiImportNameIn(Name, MiUncountedImports, RTL_NUMBER_OF(MiUncountedImports));
    PLDR_DATA_TABLE_ENTRY Module;
    UNICODE_STRING ModuleName;
    ANSI_STRING AnsiName;
    BOOLEAN Hostless;
    NTSTATUS Status;

    *Referenced = NULL;

    RtlInitAnsiString(&AnsiName, Name);
    Status = RtlAnsiStringToUnicodeString(&ModuleName, &AnsiName, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MiMapKernelApiSet(Name, &ModuleName, &Hostless);
    if (!NT_SUCCESS(Status))
        goto Exit;

    if (Hostless)
    {
        if (_stricmp(Name, "ext-ms-win-ntos-ksr-l1-1-5.dll") == 0 || _stricmp(Name, "ext-ms-win-ntos-ksr-l1-1-5") == 0)
        {
            Status = MiBindKernelServiceRoutines(ImageBase, ImageName, Descriptor);
        }
        else
        {
            DPRINT1("%wZ imports the API set %s, which has no host\n", ImageName, Name);
            Status = STATUS_PROCEDURE_NOT_FOUND;
        }

        goto Exit;
    }

    Module = MiFindSystemImage(&ModuleName, FALSE);
    if (Module == NULL)
    {
        Status = MiLoadDependency(&ModuleName, Directory, &Module);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("%wZ needs %wZ, which could not be loaded (0x%lx)\n", ImageName, &ModuleName, Status);
            goto Exit;
        }
    }
    else if (Counted && !(Module->Flags & LDRP_LOAD_IN_PROGRESS))
    {
        Module->LoadCount++;
    }

    if (Counted && !(Module->Flags & LDRP_LOAD_IN_PROGRESS))
        *Referenced = Module;

    Status = MiBindThunks(ImageBase, ImageName, Module, Descriptor);
    if (!NT_SUCCESS(Status) && *Referenced != NULL)
    {
        MiReleaseModule(*Referenced);
        *Referenced = NULL;
    }

Exit:
    RtlFreeUnicodeString(&ModuleName);
    return Status;
}

NTSTATUS
MiBindImports(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ImageName,
    _In_ PCUNICODE_STRING Directory,
    _Out_ PVOID *ImportList)
{
    PIMAGE_IMPORT_DESCRIPTOR Descriptor;
    PLDR_DATA_TABLE_ENTRY *Held, Module;
    ULONG Size, Count, HeldCount = 0, Index;
    NTSTATUS Status;
    PVOID List;

    PAGED_CODE();

    *ImportList = MM_SYSLDR_NO_IMPORTS;

    Descriptor = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &Size);
    if (Descriptor == NULL)
        return STATUS_SUCCESS;

    Status = MiCheckImportNames(ImageBase, Descriptor, ImageName, &Count);
    if (!NT_SUCCESS(Status) || Count == 0)
        return Status;

    Held = ExAllocatePoolWithTag(PagedPool, Count * sizeof(*Held), TAG_LDR_IMPORTS);
    if (Held == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (; Descriptor->Name != 0 && Descriptor->OriginalFirstThunk != 0; Descriptor++)
    {
        Status = MiBindDescriptor(ImageBase, ImageName, Directory, Descriptor, &Module);
        if (!NT_SUCCESS(Status))
            break;

        if (Module != NULL)
        {
            ASSERT(HeldCount < Count);
            Held[HeldCount++] = Module;
        }
    }

    if (NT_SUCCESS(Status))
    {
        List = MiCreateImportList(Held, HeldCount);
        if (List != NULL)
            *ImportList = List;
        else
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!NT_SUCCESS(Status))
    {
        for (Index = 0; Index < HeldCount; Index++)
            MiReleaseModule(Held[Index]);
    }

    ExFreePoolWithTag(Held, TAG_LDR_IMPORTS);
    return Status;
}

static
BOOLEAN
MiIsUnimplementedExport(
    _In_ PVOID ImageBase,
    _In_ PVOID Address)
{
    PIMAGE_NT_HEADERS NtHeaders = RtlImageNtHeader(ImageBase);
    PIMAGE_SECTION_HEADER Section;
    ULONG_PTR Rva = (ULONG_PTR)Address - (ULONG_PTR)ImageBase;
    ULONG Index;

    if (NtHeaders == NULL)
        return FALSE;

    Section = IMAGE_FIRST_SECTION(NtHeaders);
    for (Index = 0; Index < NtHeaders->FileHeader.NumberOfSections; Index++, Section++)
    {
        if (Rva >= Section->VirtualAddress &&
            Rva < (ULONG_PTR)Section->VirtualAddress + max(Section->Misc.VirtualSize, Section->SizeOfRawData))
        {
            return RtlCompareMemory(Section->Name, ".stubtxt", IMAGE_SIZEOF_SHORT_NAME) == IMAGE_SIZEOF_SHORT_NAME;
        }
    }

    return FALSE;
}

PVOID
NTAPI
MmGetSystemRoutineAddress(
    _In_ PUNICODE_STRING SystemRoutineName)
{
    static const UNICODE_STRING Providers[] =
    {
        RTL_CONSTANT_STRING(L"ntoskrnl.exe"),
        RTL_CONSTANT_STRING(L"hal.dll"),
    };
    PLDR_DATA_TABLE_ENTRY Entry;
    ANSI_STRING RoutineName;
    PVOID Address = NULL;
    PLIST_ENTRY Link;
    ULONG Index;

    if (!NT_SUCCESS(RtlUnicodeStringToAnsiString(&RoutineName, SystemRoutineName, TRUE)))
        return NULL;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    for (Index = 0; Index < RTL_NUMBER_OF(Providers) && Address == NULL; Index++)
    {
        for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
        {
            Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            if (!RtlEqualUnicodeString(&Providers[Index], &Entry->BaseDllName, TRUE))
                continue;

            Address = MiFindExport(Entry->DllBase, RoutineName.Buffer);
            if (Address != NULL && MiIsUnimplementedExport(Entry->DllBase, Address))
            {
                DPRINT1("%wZ!%Z is not implemented (caller %p)\n", &Entry->BaseDllName, &RoutineName, _ReturnAddress());
                Address = NULL;
            }

            break;
        }
    }

    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();

    RtlFreeAnsiString(&RoutineName);
    return Address;
}
