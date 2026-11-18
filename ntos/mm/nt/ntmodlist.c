/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Loaded module list, import reference lists and boot images
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/nt/ntsysimg.h>
#define NDEBUG
#include <debug.h>

LIST_ENTRY PsLoadedModuleList;
LIST_ENTRY MmLoadedUserImageList;
KSPIN_LOCK PsLoadedModuleSpinLock;
ERESOURCE PsLoadedModuleResource;
ULONG_PTR PsNtosImageBase;
KMUTANT MmSystemLoadLock;
PVOID MmUnloadedDrivers;
PVOID MmLastUnloadedDrivers;

static
PKLDR_DATA_TABLE_ENTRY
MiModuleFromLink(
    _In_ PLIST_ENTRY Link)
{
    return CONTAINING_RECORD(Link, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
}

#if DBG
static
BOOLEAN
MiOwnsSystemImages(VOID)
{
    return (BOOLEAN)(MmSystemLoadLock.OwnerThread == KeGetCurrentThread());
}
#endif

VOID
MiLockSystemImages(VOID)
{
    PAGED_CODE();

    KeEnterCriticalRegion();
    KeWaitForSingleObject(&MmSystemLoadLock, WrVirtualMemory, KernelMode, FALSE, NULL);
    ASSERT(MiOwnsSystemImages());
}

VOID
MiUnlockSystemImages(VOID)
{
    ASSERT(MiOwnsSystemImages());

    KeReleaseMutant(&MmSystemLoadLock, MUTANT_INCREMENT, FALSE, FALSE);
    KeLeaveCriticalRegion();
}

PKLDR_DATA_TABLE_ENTRY
MiFindSystemImage(
    _In_ PCUNICODE_STRING Name,
    _In_ BOOLEAN MatchFullName)
{
    PKLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Link;

    ASSERT(MiOwnsSystemImages());

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Entry = MiModuleFromLink(Link);
        if (RtlEqualUnicodeString(Name, MatchFullName ? &Entry->FullDllName : &Entry->BaseDllName, TRUE))
            return Entry;
    }

    return NULL;
}

static
VOID
MiEditModuleList(
    _In_ PKLDR_DATA_TABLE_ENTRY Entry,
    _In_ BOOLEAN Publish)
{
    KIRQL OldIrql;

    ASSERT(MiOwnsSystemImages());
    ASSERT(KeAreApcsDisabled());

    ExAcquireResourceExclusiveLite(&PsLoadedModuleResource, TRUE);
    OldIrql = KeAcquireSpinLockRaiseToSynch(&PsLoadedModuleSpinLock);

    if (Publish)
    {
        InsertTailList(&PsLoadedModuleList, &Entry->InLoadOrderLinks);
    }
    else
    {
        RemoveEntryList(&Entry->InLoadOrderLinks);
        Entry->InLoadOrderLinks.Flink = NULL;
        Entry->InLoadOrderLinks.Blink = NULL;
    }

    KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);
    ExReleaseResourceLite(&PsLoadedModuleResource);
}

VOID
MiPublishSystemImage(
    _In_ PKLDR_DATA_TABLE_ENTRY Entry)
{
    ASSERT(Entry->InLoadOrderLinks.Flink == NULL);
    MiEditModuleList(Entry, TRUE);
}

VOID
MiWithdrawSystemImage(
    _In_ PKLDR_DATA_TABLE_ENTRY Entry)
{
    ASSERT(Entry->InLoadOrderLinks.Flink != NULL);
    MiEditModuleList(Entry, FALSE);
}

PKLDR_DATA_TABLE_ENTRY
NTAPI
MiLookupDataTableEntry(
    _In_ PVOID Address)
{
    PKLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Link;

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Entry = MiModuleFromLink(Link);
        if ((ULONG_PTR)Address - (ULONG_PTR)Entry->DllBase < Entry->SizeOfImage)
            return Entry;
    }

    return NULL;
}

BOOLEAN
NTAPI
MmIsSessionImageAddress(
    _In_ PVOID Address)
{
    PKLDR_DATA_TABLE_ENTRY Entry;
    BOOLEAN Loaded;

    PAGED_CODE();

    if (!MmIsSessionAddress(Address))
        return FALSE;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);
    Entry = MiLookupDataTableEntry(Address);
    Loaded = Entry != NULL && MiSessionHasImage(PsGetCurrentProcess(), Entry);
    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();
    return Loaded;
}

static
BOOLEAN
MiImportListIsTable(
    _In_opt_ PVOID ImportList)
{
    return (BOOLEAN)(ImportList != NULL &&
                     ImportList != MM_SYSLDR_NO_IMPORTS &&
                     ImportList != MM_SYSLDR_BOOT_LOADED &&
                     !((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY));
}

ULONG
MiImportListCount(
    _In_opt_ PVOID ImportList)
{
    if (ImportList == NULL || ImportList == MM_SYSLDR_NO_IMPORTS || ImportList == MM_SYSLDR_BOOT_LOADED)
        return 0;

    if ((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY)
        return 1;

    return (ULONG)((PLOAD_IMPORTS)ImportList)->Count;
}

PKLDR_DATA_TABLE_ENTRY
MiImportListEntry(
    _In_ PVOID ImportList,
    _In_ ULONG Index)
{
    ASSERT(Index < MiImportListCount(ImportList));

    if ((ULONG_PTR)ImportList & MM_SYSLDR_SINGLE_ENTRY)
        return (PKLDR_DATA_TABLE_ENTRY)((ULONG_PTR)ImportList & ~(ULONG_PTR)MM_SYSLDR_SINGLE_ENTRY);

    return ((PLOAD_IMPORTS)ImportList)->Entry[Index];
}

VOID
MiFreeImportList(
    _In_opt_ PVOID ImportList)
{
    if (MiImportListIsTable(ImportList))
        ExFreePoolWithTag(ImportList, TAG_LDR_IMPORTS);
}

PVOID
MiCreateImportList(
    _In_reads_(Count) PKLDR_DATA_TABLE_ENTRY *Modules,
    _In_ ULONG Count)
{
    PLOAD_IMPORTS Table;
    ULONG Index;

    if (Count == 0)
        return MM_SYSLDR_NO_IMPORTS;

    if (Count == 1)
    {
        ASSERT(((ULONG_PTR)Modules[0] & MM_SYSLDR_SINGLE_ENTRY) == 0);
        return (PVOID)((ULONG_PTR)Modules[0] | MM_SYSLDR_SINGLE_ENTRY);
    }

    Table = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(LOAD_IMPORTS, Entry[Count]), TAG_LDR_IMPORTS);
    if (Table == NULL)
        return NULL;

    Table->Count = Count;
    for (Index = 0; Index < Count; Index++)
    {
        ASSERT(Modules[Index] != NULL);
        Table->Entry[Index] = Modules[Index];
    }

    ASSERT(MiImportListIsTable(Table));
    return Table;
}

BOOLEAN
NTAPI
MmIsSystemImageImportingModule(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ModuleName)
{
    PKLDR_DATA_TABLE_ENTRY Entry;
    BOOLEAN Found = FALSE;
    ULONG Index, Count;

    PAGED_CODE();

    if (!ImageBase || !ModuleName || !ModuleName->Buffer)
        return FALSE;

    if (!PsLoadedModuleList.Flink || !PsLoadedModuleList.Blink)
        return FALSE;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    Entry = MiLookupDataTableEntry(ImageBase);
    Count = Entry ? MiImportListCount(Entry->LoadedImports) : 0;

    for (Index = 0; Index < Count && !Found; Index++)
        Found = RtlEqualUnicodeString(&MiImportListEntry(Entry->LoadedImports, Index)->BaseDllName, ModuleName, TRUE);

    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();
    return Found;
}

CODE_SEG("INIT")
static
PKLDR_DATA_TABLE_ENTRY
MiCopyBootImageEntry(
    _In_ PKLDR_DATA_TABLE_ENTRY Source)
{
    PKLDR_DATA_TABLE_ENTRY Copy;
    USHORT BaseBytes = Source->BaseDllName.Length;
    USHORT FullBytes = Source->FullDllName.Length;

    Copy = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Copy) + BaseBytes + sizeof(UNICODE_NULL), TAG_MODULE_OBJECT);
    if (Copy == NULL)
        return NULL;

    *Copy = *Source;
    Copy->InLoadOrderLinks.Flink = NULL;
    Copy->InLoadOrderLinks.Blink = NULL;

    Copy->BaseDllName.Buffer = (PWCH)(Copy + 1);
    Copy->BaseDllName.Length = BaseBytes;
    Copy->BaseDllName.MaximumLength = BaseBytes + sizeof(UNICODE_NULL);
    RtlCopyMemory(Copy->BaseDllName.Buffer, Source->BaseDllName.Buffer, BaseBytes);
    Copy->BaseDllName.Buffer[BaseBytes / sizeof(WCHAR)] = UNICODE_NULL;

    Copy->FullDllName.Buffer = ExAllocatePoolWithTag(PagedPool, FullBytes + sizeof(UNICODE_NULL), TAG_LDR_WSTR);
    if (Copy->FullDllName.Buffer == NULL)
    {
        ExFreePoolWithTag(Copy, TAG_MODULE_OBJECT);
        return NULL;
    }

    Copy->FullDllName.Length = FullBytes;
    Copy->FullDllName.MaximumLength = FullBytes + sizeof(UNICODE_NULL);
    RtlCopyMemory(Copy->FullDllName.Buffer, Source->FullDllName.Buffer, FullBytes);
    Copy->FullDllName.Buffer[FullBytes / sizeof(WCHAR)] = UNICODE_NULL;

    return Copy;
}

CODE_SEG("INIT")
static
VOID
MiReplaceBootDriverEntry(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKLDR_DATA_TABLE_ENTRY LoaderEntry,
    _In_ PKLDR_DATA_TABLE_ENTRY Entry)
{
    PBOOT_DRIVER_LIST_ENTRY BootEntry;
    PLIST_ENTRY Link;

    for (Link = LoaderBlock->BootDriverListHead.Flink;
         Link != &LoaderBlock->BootDriverListHead;
         Link = Link->Flink)
    {
        BootEntry = CONTAINING_RECORD(Link, BOOT_DRIVER_LIST_ENTRY, Link);
        if (BootEntry->LdrEntry == LoaderEntry)
            BootEntry->LdrEntry = Entry;
    }
}

CODE_SEG("INIT")
static
ULONG
MiCollectBootBindings(
    _In_ PKLDR_DATA_TABLE_ENTRY Importer,
    _Out_writes_(Capacity) PKLDR_DATA_TABLE_ENTRY *Targets,
    _In_ ULONG Capacity)
{
    PIMAGE_IMPORT_DESCRIPTOR Descriptor;
    PKLDR_DATA_TABLE_ENTRY Current = NULL;
    PULONG_PTR Slot;
    ULONG Size, Found = 0, Index;

    Descriptor = RtlImageDirectoryEntryToData(Importer->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &Size);
    if (Descriptor == NULL)
        return 0;

    for (; Descriptor->Name != 0 && Descriptor->OriginalFirstThunk != 0; Descriptor++)
    {
        for (Slot = (PULONG_PTR)((ULONG_PTR)Importer->DllBase + Descriptor->FirstThunk); *Slot != 0; Slot++)
        {
            if (Current != NULL && *Slot - (ULONG_PTR)Current->DllBase < Current->SizeOfImage)
                continue;

            Current = MiLookupDataTableEntry((PVOID)*Slot);
            ASSERTMSG("A boot image is bound to an address outside every loaded image\n", Current != NULL);
            if (Current == NULL)
                continue;

            for (Index = 0; Index < Found && Targets[Index] != Current; Index++)
                NOTHING;

            if (Index == Found)
            {
                ASSERT(Found < Capacity);
                Targets[Found++] = Current;
            }
        }
    }

    return Found;
}

CODE_SEG("INIT")
static
BOOLEAN
MiLinkBootImageImports(
    _In_ PKLDR_DATA_TABLE_ENTRY Kernel,
    _In_ PKLDR_DATA_TABLE_ENTRY Hal,
    _In_ ULONG ModuleCount)
{
    PKLDR_DATA_TABLE_ENTRY *Targets, Importer;
    PLIST_ENTRY Link;
    ULONG Found, Index, Kept;
    PVOID List;

    Targets = ExAllocatePoolWithTag(PagedPool, ModuleCount * sizeof(*Targets), TAG_LDR_IMPORTS);
    if (Targets == NULL)
        return FALSE;

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Importer = MiModuleFromLink(Link);
        Found = MiCollectBootBindings(Importer, Targets, ModuleCount);

        for (Index = 0, Kept = 0; Index < Found; Index++)
        {
            if (Targets[Index] == Kernel || Targets[Index] == Hal)
                continue;

            Targets[Index]->LoadCount++;
            Targets[Kept++] = Targets[Index];
        }

        if (Importer == Kernel || Importer == Hal)
            continue;

        List = MiCreateImportList(Targets, Kept);
        if (List == NULL)
        {
            ExFreePoolWithTag(Targets, TAG_LDR_IMPORTS);
            return FALSE;
        }

        Importer->LoadedImports = List;
    }

    ExFreePoolWithTag(Targets, TAG_LDR_IMPORTS);
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
MiInitializeLoadedModuleList(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNICODE_STRING KernelName = RTL_CONSTANT_STRING(L"ntoskrnl.exe");
    UNICODE_STRING HalName = RTL_CONSTANT_STRING(L"hal.dll");
    PKLDR_DATA_TABLE_ENTRY Source, Copy, Kernel = NULL, Hal = NULL;
    PLIST_ENTRY Link;
    ULONG Count = 0;

    ExInitializeResourceLite(&PsLoadedModuleResource);
    KeInitializeSpinLock(&PsLoadedModuleSpinLock);
    InitializeListHead(&PsLoadedModuleList);
    InitializeListHead(&MmLoadedUserImageList);

    ASSERT(!IsListEmpty(&LoaderBlock->LoadOrderListHead));
    PsNtosImageBase = (ULONG_PTR)MiModuleFromLink(LoaderBlock->LoadOrderListHead.Flink)->DllBase;

    for (Link = LoaderBlock->LoadOrderListHead.Flink; Link != &LoaderBlock->LoadOrderListHead; Link = Link->Flink)
    {
        Source = MiModuleFromLink(Link);
        if (RtlImageNtHeader(Source->DllBase) == NULL)
            continue;

        Copy = MiCopyBootImageEntry(Source);
        if (Copy == NULL)
            return FALSE;

        Copy->LoadedImports = MM_SYSLDR_BOOT_LOADED;
        InsertTailList(&PsLoadedModuleList, &Copy->InLoadOrderLinks);
        MiReplaceBootDriverEntry(LoaderBlock, Source, Copy);

        if (Kernel == NULL && RtlEqualUnicodeString(&Copy->BaseDllName, &KernelName, TRUE))
            Kernel = Copy;
        else if (Hal == NULL && RtlEqualUnicodeString(&Copy->BaseDllName, &HalName, TRUE))
            Hal = Copy;

        Count++;
    }

    ASSERT(Kernel != NULL && Hal != NULL);
    if (Kernel == NULL || Hal == NULL)
        return TRUE;

    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
    {
        Copy = MiModuleFromLink(Link);
        Copy->LoadCount = ((Copy->Flags & LDRP_DRIVER_DEPENDENT_DLL) && Copy != Kernel && Copy != Hal) ? 0 : 1;
        if (Copy != Kernel && Copy != Hal)
            Copy->LoadedImports = MM_SYSLDR_NO_IMPORTS;
    }

    return MiLinkBootImageImports(Kernel, Hal, Count);
}

typedef struct _MI_BOOT_DLL_FRAME
{
    PKLDR_DATA_TABLE_ENTRY Module;
    ULONG Next;
} MI_BOOT_DLL_FRAME, *PMI_BOOT_DLL_FRAME;

typedef struct _MI_BOOT_DLL_WALK
{
    PKLDR_DATA_TABLE_ENTRY *Seen;
    PMI_BOOT_DLL_FRAME Stack;
    ULONG Count;
    ULONG Capacity;
} MI_BOOT_DLL_WALK, *PMI_BOOT_DLL_WALK;

CODE_SEG("INIT")
static
BOOLEAN
MiBootDllSeen(
    _In_ PMI_BOOT_DLL_WALK Walk,
    _In_ PKLDR_DATA_TABLE_ENTRY Module)
{
    ULONG Index;

    for (Index = 0; Index < Walk->Count; Index++)
    {
        if (Walk->Seen[Index] == Module)
            return TRUE;
    }

    return FALSE;
}

CODE_SEG("INIT")
static
VOID
MiInitializeBootDll(
    _Inout_ PMI_BOOT_DLL_WALK Walk,
    _In_ PKLDR_DATA_TABLE_ENTRY Root)
{
    PKLDR_DATA_TABLE_ENTRY Module;
    PMI_BOOT_DLL_FRAME Frame;
    ULONG Depth = 0;
    NTSTATUS Status;

    ASSERT(Walk->Count <= Walk->Capacity);

    if (MiBootDllSeen(Walk, Root))
        return;

    ASSERT(Walk->Count < Walk->Capacity);
    if (Walk->Count == Walk->Capacity)
        return;

    Walk->Seen[Walk->Count++] = Root;
    Walk->Stack[Depth].Module = Root;
    Walk->Stack[Depth].Next = 0;
    Depth++;

    while (Depth != 0)
    {
        Frame = &Walk->Stack[Depth - 1];
        ASSERT(Frame->Module->LoadedImports != MM_SYSLDR_BOOT_LOADED);

        if (Frame->Next < MiImportListCount(Frame->Module->LoadedImports))
        {
            Module = MiImportListEntry(Frame->Module->LoadedImports, Frame->Next++);
            ASSERT(Module != NULL);
            if (MiBootDllSeen(Walk, Module))
                continue;

            ASSERT(Walk->Count < Walk->Capacity);
            ASSERT(Depth < Walk->Capacity);
            if (Walk->Count == Walk->Capacity || Depth == Walk->Capacity)
                return;

            Walk->Seen[Walk->Count++] = Module;
            Walk->Stack[Depth].Module = Module;
            Walk->Stack[Depth].Next = 0;
            Depth++;
            continue;
        }

        Module = Frame->Module;
        Depth--;

        Status = MmCallDllInitialize(Module, &PsLoadedModuleList);
        if (!NT_SUCCESS(Status))
        {
            /* FIXME: a boot DLL whose DllInitialize fails stays loaded and the drivers that import it still start */
            DPRINT1("DllInitialize of the boot DLL %wZ failed with 0x%lx\n", &Module->BaseDllName, Status);
        }
    }
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
MmCallBootDllInitialize(
    _In_ PLIST_ENTRY BootDriverListHead)
{
    PBOOT_DRIVER_LIST_ENTRY BootEntry;
    PIMAGE_NT_HEADERS NtHeaders;
    PKLDR_DATA_TABLE_ENTRY Module;
    MI_BOOT_DLL_WALK Walk;
    PLIST_ENTRY Link;
    ULONG Index;

    PAGED_CODE();

    RtlZeroMemory(&Walk, sizeof(Walk));

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);
    for (Link = PsLoadedModuleList.Flink; Link != &PsLoadedModuleList; Link = Link->Flink)
        Walk.Capacity++;
    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();

    ASSERT(Walk.Capacity != 0);

    Walk.Seen = ExAllocatePoolWithTag(PagedPool, Walk.Capacity * sizeof(*Walk.Seen), TAG_LDR_IMPORTS);
    Walk.Stack = ExAllocatePoolWithTag(PagedPool, Walk.Capacity * sizeof(*Walk.Stack), TAG_LDR_IMPORTS);
    if (Walk.Seen == NULL || Walk.Stack == NULL)
    {
        if (Walk.Seen != NULL)
            ExFreePoolWithTag(Walk.Seen, TAG_LDR_IMPORTS);
        if (Walk.Stack != NULL)
            ExFreePoolWithTag(Walk.Stack, TAG_LDR_IMPORTS);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Link = BootDriverListHead->Flink; Link != BootDriverListHead; Link = Link->Flink)
    {
        BootEntry = CONTAINING_RECORD(Link, BOOT_DRIVER_LIST_ENTRY, Link);
        Module = BootEntry->LdrEntry;
        if (Module == NULL)
            continue;

        NtHeaders = RtlImageNtHeader(Module->DllBase);
        if (NtHeaders == NULL || NtHeaders->OptionalHeader.AddressOfEntryPoint == 0)
        {
            MiInitializeBootDll(&Walk, Module);
            continue;
        }

        for (Index = 0; Index < MiImportListCount(Module->LoadedImports); Index++)
            MiInitializeBootDll(&Walk, MiImportListEntry(Module->LoadedImports, Index));
    }

    ASSERT(Walk.Count <= Walk.Capacity);

    ExFreePoolWithTag(Walk.Stack, TAG_LDR_IMPORTS);
    ExFreePoolWithTag(Walk.Seen, TAG_LDR_IMPORTS);
    return STATUS_SUCCESS;
}
